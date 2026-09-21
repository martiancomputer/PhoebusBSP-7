#!/bin/sh
# Serve images/ over TFTP to the board's U-Boot.
#
#   sudo ./tools/tftp-serve.sh [iface]
#
# The defaults use the documentation-only TEST-NET-1 range. Override them in
# tools/tftp.env for the isolated board link when different addresses are
# required. That file is deliberately ignored by Git.
#
# Leave this running in its own terminal; it logs every TFTP request, so you can
# see the board connect. Ctrl-C to stop.
set -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
TFTP_ENV_FILE=${TFTP_ENV_FILE:-$SCRIPT_DIR/tftp.env}
[ ! -f "$TFTP_ENV_FILE" ] || . "$TFTP_ENV_FILE"

HOST_CIDR=${TFTP_HOST_CIDR:-192.0.2.2/24}
HOST=${HOST_CIDR%/*}
BOARD=${TFTP_BOARD_IP:-192.0.2.10}
BOARD_MAC=${TFTP_BOARD_MAC:-}
IMAGES=$(cd "$(dirname "$0")/../images" && pwd)

[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }

# Cloudflare WARP installs policy-routing and nftables state that can swallow
# U-Boot's TFTP packets before they reach the server.  Stop both its system
# daemon and the invoking desktop user's relaunch units before configuring the
# isolated TFTP interface.  Missing services/processes are harmless.
stop_warp() {
	echo "warp      : stopping Cloudflare WARP"
	if command -v systemctl >/dev/null 2>&1; then
		systemctl stop warp-svc.service 2>/dev/null || true

		warp_user=${SUDO_USER:-}
		if [ -n "$warp_user" ] && [ "$warp_user" != root ]; then
			warp_uid=$(id -u "$warp_user" 2>/dev/null || true)
			if [ -n "$warp_uid" ] && command -v runuser >/dev/null 2>&1; then
				runuser -u "$warp_user" -- env \
					XDG_RUNTIME_DIR="/run/user/$warp_uid" \
					systemctl --user stop warp-daemon.service \
					warp-taskbar.service 2>/dev/null || true
			fi
		fi
	fi
	pkill -x warp-taskbar 2>/dev/null || true
	pkill -x warp-svc 2>/dev/null || true
}
stop_warp

# --- pick the interface ---------------------------------------------------
IF="$1"
if [ -z "$IF" ]; then
	# prefer a USB ethernet adapter that has carrier
	for c in /sys/class/net/*; do
		i=$(basename "$c")
		case "$i" in lo|wl*|tailscale*|virbr*|docker*) continue ;; esac
		[ "$(cat "$c/carrier" 2>/dev/null)" = 1 ] && IF=$i && break
	done
fi
[ -n "$IF" ] || { echo "no wired interface with carrier; pass one explicitly" >&2
                  ip -o link | awk -F': ' '{print "  " $2}' >&2; exit 1; }

echo "interface : $IF"
echo "tftp root : $IMAGES"

# Dump the firewall. We are root here, so unlike an unprivileged check this
# cannot fail silently and be mistaken for "no rules" -- which is exactly the
# error that sent this debugging session down a blind alley. CloudflareWARP and
# similar VPN clients install nftables rules that drop anything not going out
# their tunnel, and that drop is invisible to tcpdump and to every /proc
# counter.
if command -v nft >/dev/null 2>&1; then
	RULES=$(nft list ruleset 2>/dev/null)
	if [ -n "$RULES" ]; then
		echo "$RULES" > /tmp/phoebus-nft.txt
		echo "firewall  : $(echo "$RULES" | grep -c .) lines of nftables rules -> /tmp/phoebus-nft.txt"
		echo "$RULES" | grep -nE '\b(drop|reject)\b' | head -8 | sed 's/^/            /'
	else
		echo "firewall  : nftables ruleset is empty"
	fi
fi

# --- take the interface off NetworkManager and address it -----------------
# Hand the interface back when we exit, or the box has no internet afterwards
# and it looks like this script broke networking.
PCAP=/tmp/phoebus-tftp.pcap
TCPDUMP_PID=""

# Kernel counters, sampled either side of the session.
#
# tcpdump taps at AF_PACKET, upstream of IP and netfilter, so a packet can sit
# in the capture having never reached any socket -- which is exactly what
# happened: the board's read requests were captured, and neither dnsmasq nor
# our own tftpd saw one. The deltas below say which layer ate them.
#
#   UdpNoPorts     up  -> nothing was bound to port 69
#   UdpInCsumErrors up -> corrupt on the wire
#   IpInAddrErrors up  -> wrong destination / martian
#   all flat           -> netfilter dropped it (no counter for that)
counters() { nstat -az 2>/dev/null | awk '/^(UdpNoPorts|UdpInCsumErrors|IpInAddrErrors|IpInHdrErrors)/ {print $1"="$2}'; }
C_BEFORE=$(counters)
cleanup() {
	[ -n "$TCPDUMP_PID" ] && kill "$TCPDUMP_PID" 2>/dev/null
	command -v nmcli >/dev/null 2>&1 && nmcli device set "$IF" managed yes 2>/dev/null
	[ -s "$PCAP" ] && echo "capture: $PCAP ($(stat -c%s "$PCAP") bytes)"

	echo
	echo "--- kernel counter deltas over this session ---"
	after=$(counters)
	for kv in $C_BEFORE; do
		k=${kv%%=*}; b=${kv#*=}
		a=$(echo "$after" | sed -n "s/^$k=//p")
		[ -n "$a" ] && [ "$a" != "$b" ] && echo "  $k +$((a - b))"
	done
	echo "  (nothing listed = no counter moved => netfilter dropped the packets)"
	return 0
}
trap cleanup EXIT INT TERM

if command -v nmcli >/dev/null 2>&1; then
	nmcli device set "$IF" managed no 2>/dev/null || true
	sleep 1        # let NM actually release before we touch addresses
fi
ip link set "$IF" up
ip addr flush dev "$IF"
ip addr add "$HOST_CIDR" dev "$IF"

# Turn off every receive-side offload.
#
# The board answers ARP in 0.7ms but its TFTP read request never reaches
# dnsmasq -- and ARP is the one protocol here with no checksum. If U-Boot's
# LUNA GMAC emits a wrong IP or UDP checksum, the adapter's RX offload drops
# the frame in hardware and nothing upstream ever logs it, which is exactly
# the silence we get. With offload off the kernel validates in software, and
# tcpdump below sees the frame either way.
#
# GRO/LRO go too: they coalesce segments before tcpdump taps the path, which
# would misrepresent what actually arrived on the wire.
if command -v ethtool >/dev/null 2>&1; then
	ethtool -K "$IF" rx off tx off gro off lro off tso off gso off 2>/dev/null
	echo "offloads  : rx/tx checksum, gro, lro, tso, gso -> off"
fi

# rp_filter drops replies that would leave by a different route than they came
# in on -- easy to hit with several interfaces up, and it looks exactly like the
# board's ARP going unanswered.
sysctl -qw net.ipv4.conf."$IF".rp_filter=0 2>/dev/null || true
sysctl -qw net.ipv4.conf.all.rp_filter=0   2>/dev/null || true

# --- verify before serving ------------------------------------------------
# WAIT for the link rather than sampling it. `ip link set up` above only STARTS
# autonegotiation, which takes 2-4s on gigabit; reading carrier immediately
# after always returned 0 and printed the warning below even when the link was
# seconds from coming up. That false alarm sent a whole debugging session after
# cables and switch ports while the real fault was elsewhere -- so report the
# time it took, and only call it dead after a timeout a real link cannot miss.
printf 'waiting for link on %s ' "$IF"
carrier=0
i=0
while [ "$i" -lt 100 ]; do          # 100 x 0.1s = 10s
	carrier=$(cat /sys/class/net/"$IF"/carrier 2>/dev/null || echo 0)
	[ "$carrier" = 1 ] && break
	printf '.'
	sleep 0.1
	i=$((i + 1))
done
if [ "$carrier" = 1 ]; then
	printf ' up after %s.%ss -- %s Mb/s %s duplex\n' \
		$((i / 10)) $((i % 10)) \
		"$(cat /sys/class/net/"$IF"/speed 2>/dev/null)" \
		"$(cat /sys/class/net/"$IF"/duplex 2>/dev/null)"
else
	printf '\nWARNING: still no carrier after 10s.\n'
	echo "  The cable is not in a live port, or the board has not run swcore_init"
	echo "  yet -- U-Boot only powers the switch PHYs when the network is first"
	echo "  used, so plug in and then run 'run fl' rather than the other way round."
fi
ip -4 -br addr show "$IF"
ls -l "$IMAGES/uImage-initramfs" 2>/dev/null || echo "WARNING: no uImage-initramfs in $IMAGES"

if [ -n "$BOARD_MAC" ]; then
	MAC_ENV="setenv ethaddr $BOARD_MAC"
else
	MAC_ENV='# setenv ethaddr <board-mac>  # optional; configure in tools/tftp.env'
fi

cat <<EOF

--- paste into U-Boot (once; saveenv makes it stick) ---------------------
setenv ipaddr $BOARD
setenv serverip $HOST
$MAC_ENV
setenv fl 'tftpboot 0x83000000 uImage-initramfs; bootm 0x83000000'
saveenv

--- every flash after that is just ---------------------------------------
run fl
--------------------------------------------------------------------------

serving (Ctrl-C to stop)...
EOF

# Capture everything on the wire alongside the server, so a failed flash leaves
# evidence instead of just an absence of log lines. -p (no promisc) keeps this
# honest about what the adapter would actually have accepted; -s0 gets whole
# frames so checksums can be verified after the fact.
if command -v tcpdump >/dev/null 2>&1; then
	rm -f "$PCAP"
	tcpdump -i "$IF" -p -s 0 -w "$PCAP" -U 2>/dev/null &
	TCPDUMP_PID=$!
	sleep 0.5
	echo "capturing : $PCAP (pid $TCPDUMP_PID)"
fi

# Serve with our own tftpd rather than dnsmasq.
#
# dnsmasq silently dropped every read request from this board. tcpdump showed
# the RRQ arriving intact and no ICMP port-unreachable going back, so port 69
# was bound and something consumed the packet -- yet dnsmasq neither replied nor
# logged anything. --user=root made no difference and dnsmasq had not been
# upgraded in a month, so the cause was never found. Eight retries, zero
# replies, no diagnostic.
#
# tools/tftpd.py logs every packet and reports why a transfer failed, which is
# the property that was missing. It also honours the blksize the board asks for
# (1468), where the old invocation forced 512 with --tftp-no-blocksize -- about
# 3x fewer round trips on a 14MB image.
#
# Not exec'd: the trap has to run to stop tcpdump and give the NIC back to NM.
python3 "$(dirname "$0")/tftpd.py" "$IMAGES" "$HOST" 69
