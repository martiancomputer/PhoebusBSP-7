# PhoebusBSP-7

Mainline/bleeding-edge Linux board support for Realtek **RTL9607C / RTL9607Cv2**
(big-endian MIPS32r2 interAptiv, MIPS CPS + MT SMP / 4 VPEs, GIC).

The current default kernel is **Linux 7.3 final**. This repository is the forward-port
line for Phoebus-OS; hardware debugging is done first on the
[6.18 LTS BSP](https://github.com/martiancomputer/PhoebusBSP-6), then carried
forward here.

Shared vendor code, userspace, provisioning and tooling live in
[Phoebus-SDK](https://github.com/martiancomputer/Phoebus-SDK), pinned as the
`sdk/` submodule.

## Verification boundary

Linux **7.3-rc3 has been booted and exercised on the RTL9607C hardware**.

Linux 7.3 final has now booted with WPA3 connections on both radios after
correcting firewall startup and preserving hostapd's RSN element in the 2.4 GHz
driver. The hwmon parent-device fix has also been validated by reading the
SoC temperature sensor on the board. Off-device logging preserves evidence for
the separate long-run freeze and FC internal-table exhaustion investigations;
neither is declared solved.

The current mainline image completed a roughly **5-hour continuous hardware run**
with the kernel and router userspace active. The run ended when the Linux
conntrack table filled because the port was using the wrong cleanup ABI, so
expired entries were not being reclaimed correctly.

The cleanup-ABI correction is now pushed in commit `52b02eaa29a6`
(`net: repair FleetConntrack table flush`). It still needs a fresh multi-hour
hardware soak to confirm that conntrack entries are reclaimed correctly over
long runtimes.

The earlier 7.1.5 bring-up remains useful historical evidence for the individual
subsystems, but it is no longer the newest hardware-verification boundary.

Compile/link/symbol checks are still treated separately from runtime validation:
the 7.3-rc3 port has now passed a sustained hardware run, while the conntrack
cleanup defect remains the known failure exposed by that run.

## Scope

The BSP carries the kernel-version-specific part of the port:

- RTL9607C MIPS platform, SMP, GIC, timers, serial, GPIO and watchdog
- Realtek switch/NIC/xPON and FleetConntrack integration
- PCIe
- RTL8832BR 5 GHz vendor driver (`rtk_wifi6`)
- RTL8192F 2.4 GHz vendor driver (`rtl8192cd`)
- mainline API shims and Kconfig/build-system adaptations
- read-only RTL9607C SPI-NAND MTD bring-up on the 7.3 line

The shared SDK carries the vendor source snapshot, s6 rootfs, networking policy,
wireless configuration, provisioning and board tooling.

The AX10/AX1500 v3 board overlay now uses its stock LED and button wiring.
Seven software-PWM channels expose LED brightness from 0 to 255 at 200 Hz;
power defaults on and the other indicators default off. The timer and pattern
LED triggers are available for later sequences. Full-on/off states stop the
PWM toggling timer. Simultaneous dimming on all seven channels has been
visually validated with matching kernel duty-cycle readback; its effect under
heavy network load remains unbenchmarked. `python3 tools/check-led-pwm.py` checks the compiled DTB wiring,
polarity, button isolation and software-PWM endpoint behavior.

FC starts with learning disabled until userspace reads the NIC's physical WAN
mask and verifies the accelerator policy. The normal boot and `rtk-fc load`
default is mode 1 (hardware acceleration with software fallback); `hwnat=N`
or an explicit load argument overrides it. The WAN-mask correction has been
checked on the board, but long-run stability remains a separate test.

## Repository layout

```
build.sh
    reconstructs a disposable kernel tree, builds userspace + kernel, packages uImage

configs/rtl9607c.config
    RTL9607C kernel configuration for the current mainline target

overlay/
    exact files that differ after grafting the shared vendor snapshot onto the
    selected upstream kernel

tools/build-userspace.sh
    BSP-7 network userspace build: libnl, OpenSSL, hostapd, iptables,
    libxcrypt, Dropbear, wireless_tools and dnsmasq

tools/rebase-overlay.sh
    helper for moving the overlay to another upstream kernel

tools/fix-kconfig-choices.py
    vendor Kconfig choice normalisation required by newer Kconfig

docs/porting-6.18-to-7.1.md
docs/port-vs-upstream-7.1.5.diff
    historical migration material for the first LTS -> mainline jump

sdk/
    Phoebus-SDK submodule
```

The 7.1 documents are deliberately retained as history. They describe the first
major forward-port and the API breakage encountered there; they are not the
current kernel-version declaration.

## Build model

A build is reconstructed from three inputs:

```
pristine upstream Linux
  + sdk/vendor/{realtek-net,realtek-wireless,platform,include}
  + overlay/
  = build/linux-$KVER
```

The work tree is disposable. Anything that exists only in a hand-managed build
tree is therefore treated as missing from the repository.

Default:

```sh
git clone --recurse-submodules https://github.com/martiancomputer/PhoebusBSP-7
cd PhoebusBSP-7
git submodule update --init
./build.sh
```

Current default:

```
KVER=7.3
```

Release candidates are cloned from Linus's kernel.org Git tree; stable releases
use kernel.org tarballs, falling back to the official Git release tag when a
new tag precedes CDN publication. A different mainline version can be selected with
`KVER=...`.

Host packages:

```sh
# Arch / Artix
sudo pacman -S base-devel bc uboot-tools xz

# Debian / Ubuntu
sudo apt install build-essential bc u-boot-tools xz-utils flex bison
```

The Bootlin big-endian MIPS32 glibc GCC 14 toolchain is fetched automatically.

### Userspace

A full BSP-7 build has two userspace stages:

1. `sdk/rootfs/build-rootfs.sh` builds the shared BusyBox/s6/btop rootfs and tracked
   SDK files.
2. `tools/build-userspace.sh` builds the network-facing third-party programs
   that BSP-7 intentionally owns.

The current BSP userspace builder uses current pinned releases including
hostapd 2.12, dnsmasq 2.93 and Dropbear 2026.94, and verifies runtime
`DT_NEEDED` resolution rather than checking filenames alone.

`SKIP_USERSPACE=1 ./build.sh` is for kernel-only iteration. It is not a
deployable router image: services such as hostapd, dnsmasq, Dropbear and
iptables will be absent.

The SDK cross-builds btop for MIPS32r2, replacing BusyBox `top`. Its CPU panel
discovers all four logical VPEs; its network panel can select each LAN/WAN
netdev, including bridge members without individual IP addresses. See the
SDK's resource-monitor section for runtime-library, UTF-8 and hardware-offload
counter limitations. Host GNU `localedef` is required for the UTF-8 locale.

### Off-device diagnostics

The default LAN is `10.1.0.0/24`, router `10.1.0.1`. DHCP allocates `.100` through
`.171`; higher addresses remain available for statically configured infrastructure.
All physical LAN sockets remain members of the same bridge.

Copy `sdk/rootfs/etc/phoebus-telemetry.conf` into the ignored
`sdk/secrets/telemetry.env`, and set `REMOTE_LOG_HOST` to a trusted wired collector.
The build installs that private configuration with mode 0600. Public builds with
no configured destination do not send logs to an arbitrary host.

Run the unprivileged collector on the wired host:

```sh
python3 tools/phoebus-log-receiver.py --listen "$COLLECTOR_IP" \
    --router "$ROUTER_IP" --directory "$PRIVATE_LOG_DIR"
```

It listens on UDP 1514 (system/health), 6666 (kernel records), and 6667 (optional
netconsole). Logs are separate rotating JSONL files with receive timestamps,
private permissions and periodic fsync. The receiver filters source addresses;
UDP logs are not encrypted/authenticated and belong on a trusted wired network.

Logging is an optional `logging` service bundle, not a dependency of core
networking or the local system logger. Configure the collector after the base
network has been validated, restart `syslogd`, then start it explicitly:

```sh
s6-svc -r /run/service/syslogd
s6-rc -l /run/s6-rc/live -u change logging
```

When enabled, the router forwards system messages, independently relays `/dev/kmsg` including
boot history, and emits bounded memory/conntrack/CPU/network/task counters. It
does not read credential files, process environments, full command lines or packet
payloads. Raw kernel/system messages can still contain sensitive data and must
remain private. Logging UDP is marked untracked before conntrack so table exhaustion
does not itself prevent those messages being sent. No automatic FC flush,
module unload, reboot or panic injection is performed.

Netconsole support is built but remains opt-in: the vendor NIC's panic-time
polling path needs board validation with UART available. After that validation,
`NETCONSOLE_IFACE=auto` resolves the collector's bridge FDB entry to a wired LAN
member rather than guessing a socket-to-netdev mapping or broadcasting logs.
Neither UDP nor netconsole can guarantee a final message if the NIC or all CPUs
are already locked; an independently recorded UART is still the stronger fallback.

btop labels the CPU/VPEs and discovers the read-only `rtl9607c` hwmon SoC sensor.
The driver uses the initialized SDK getter with cached eFuse correction. One
package reading is exposed, not invented per-VPE temperatures or a guessed
critical threshold. No verified board power/energy interface is currently exposed.

## RAM boot

Persistent installation is not the default test path. The normal development
loop keeps the stock U-Boot and RAM-boots the generated image.

```
setenv bootargs 'console=ttyS0,115200 loglevel=8 phoebus_verbose ethaddr=<board-base-MAC> rfe2g=23 sds=0 wan=eth0.8 lan=eth0.2,eth0.3,eth0.4,eth0.5'
loady 0x83000000
bootm 0x83000000
```

`images/uImage-initramfs` is a symlink to `images/uImage`; the rootfs is
compiled into the kernel image.

Important board-specific arguments:

| Argument | Purpose |
|---|---|
| `ethaddr=` | base MAC used by wired and derived radio addresses |
| `rfe2g=23` | RTL8192F RF front-end selection used by this board |
| `sds=0` | initialise SDS0 for the WAN path |
| `wan=eth0.8` | measured WAN netdev |
| `lan=eth0.2,eth0.3,eth0.4,eth0.5` | four physical LAN ports |
| `phoebus_verbose` | retain full kernel console verbosity |

Do not substitute `sds=1`: SGMII1 shares a lane with PCIe port 1 and costs the
2.4 GHz radio.

## Current mainline state

The current tree carries forward the LTS hardware work for:

- four physical gigabit LAN ports
- WAN SerDes initialisation and external PHY handling
- DHCP/NAT/router userspace
- RTL8192F 2.4 GHz bring-up
- RTL8832BR 5 GHz + WPA2/WPA3/SAE integration
- SDK-side 802.11ax, 80 MHz, private-MIB tuning and ACS policy
- switch PHY power-up
- removal of the vendor skb recycle-pool configuration that caused OOM
- s6 service supervision and diagnostics

### Open mainline-specific Wi-Fi investigation

The last 7.1.5 hardware run exposed a mainline-only 5 GHz EAPOL failure:
SAE authentication and association completed, but the EAPOL frames for the
4-way handshake did not reach the expected driver TX path.

Instrumentation in the overlay places a probe at `rtw_xmit_entry` to divide
the failure into two cases:

- no entry probe: the kernel/qdisc path never delivered the frame
- entry probe but no EAPOL probe: the vendor driver swallowed it later

That diagnostic predates the later forward-ports. The current 7.3-rc3 image has
now completed a multi-hour hardware run, but this specific old EAPOL probe has
not yet been re-isolated with the same instrumentation. Treat the 7.1 result as
historical evidence, not proof that 7.3 has the identical failure.

## Deliberate gaps

### CAKE/SQM

The shared SDK contains the CAKE service, but BSP-7 currently leaves
`CONFIG_NET_SCH_CAKE` disabled and does not build `tc`. That remains
deliberate while the mainline Wi-Fi TX/EAPOL path is unresolved; changing the
qdisc stack at the same time would make attribution worse.

The 6.18 BSP is the hardware reference for CAKE/SQM.

### NAND

7.3-rc3 adds `CONFIG_MTD_RTL9607C_SPINAND_RO`, a stock-layout-aware,
**read-only** MTD driver for the board's SPI-NAND.

It intentionally has no program, erase, markbad or automatic UBI attach path.
The point is to validate controller behaviour, ECC and physical offsets before
any persistent installation work is attempted.

## Relationship to BSP-6

```
               Phoebus-SDK
      shared vendor + userspace + tooling
             /                    \
            /                      \
   PhoebusBSP-6               PhoebusBSP-7
    Linux 6.18 LTS          current mainline
  hardware reference       currently 7.3-rc3
```

The operating rule is simple:

> Fix hardware behaviour on 6.18, then forward-port the known-good change to
> the current BSP-7 kernel.

This keeps hardware debugging separate from upstream API churn.

## Board status indicators

The board's seven active-low channels use 200 Hz GPIO-backed PWM. The kernel
flashes all green channels, turns them off, and blinks power until `/init`
signals userspace readiness. The `gpio-daemon` s6 service then handles independent
radio, WAN, LAN and VPN status without blocking network service startup.

The native C process blocks in epoll on RTNETLINK, persistent hostapd controls,
and `/run/phoebus/gpio.sock`. There are no recurring shell polling workers.
Protected s6/DHCP/hotplug callbacks use the bounded `gpioctl` API. One timerfd
services animations and bounded connectivity probes; steady LEDs have no timer.
See [the API](sdk/gpio/API.md) and [architecture](sdk/gpio/ARCHITECTURE.md).

- Each Wi-Fi indicator blinks at service startup and becomes solid only when
  hostapd reports `ENABLED`; service start alone cannot falsely assert readiness.
- Internet stays off without WAN carrier, blinks orange while awaiting a DHCP
  lease, turns solid orange while connectivity is unconfirmed, then green on
  a successful WAN-bound native ICMP probe. Three consecutive failures, not a
  single lost reply, revoke online status. Probes run only with carrier/address.
- The aggregate LAN indicator announces a newly connected port with one through
  four short pulses. The latest connection preempts an earlier announcement;
  already connected ports do not replay. After a one-second steady interval,
  a DHCP ACK attributed to that port through the bridge FDB produces one tick.
- The lock indicator reflects a running Tailscale backend or a recent WireGuard
  handshake, not merely the existence of a tunnel device.

`python3 tools/test-led-status.py` checks synthetic lease decoding, renewal vs.
periodic rewrites, numbered announcements, preemption and the DHCP tick.
`python3 tools/check-led-pwm.py` checks the compiled board wiring and PWM polarity.
Physical event timing still requires a board test after flashing.

## Optional ephemeral Tailscale

`tools/build-tailscale.sh` builds a pinned official Tailscale source revision for
big-endian MIPS with static linking. The combined `build/tailscaled-mips` binary
contains both daemon and CLI; a `tailscale` symlink selects the CLI. The small
feature set includes kernel routing, NAT traversal, embedded trust roots and
the notification bus required for interactive authentication.

The binary is intentionally **not** copied into the initramfs. For a RAM-only
trial, transfer it into a private directory under `/run`, use `--state=mem:`
and a socket in that directory, and authenticate interactively. The kernel
configuration includes TUN. Correct system time is required for TLS.
Log uploads can be disabled with `--no-logs-no-support`. Leave DNS acceptance,
route acceptance and netfilter management disabled during initial validation;
no subnet-router, exit-node or SSH service is enabled by this build recipe.

The temporary binary and authentication disappear on reboot. A successful
build or node login alone does not establish encrypted data-path throughput,
firewall reachability or reboot persistence.

## 2.4 GHz restart and capabilities

The RTL8192F AP stop callback closes hardware while leaving the netdev
administratively up. AP startup now reopens that hardware when needed instead
of repeatedly returning `-EIO` after a hostapd restart. RF-specific HT/VHT
capability edits are kept per-wiphy, and the 2T2R HT receive mask is explicit.
Registration derives stream capability from the probed hardware, not the
zero-initialized MIMO policy field whose defaults are installed only later.
Boot reports the RF mode and advertised HT mask for comparison with hostapd.
These correctness changes do not establish that the remaining HT20 throughput
gap is fixed; channel occupancy, retries and client rates must be compared on
hardware. WPA3, PMF and hardware encryption remain enabled.

## Verification discipline

Several historical failures compiled successfully, so build exit status is not
used as the definition of correctness.

For changes to this port:

- verify generated `.config` symbols
- verify expected objects/symbols in `vmlinux`
- verify the vendor Wi-Fi graft exists, not only the modified overlay files
- verify runtime shared-library targets resolve
- distinguish clean-clone reproducibility from staged-tree success
- distinguish build verification from hardware verification
- keep flash access read-only until ECC/bad-block/offset handling is independently
  validated

For detailed board findings and historical failure analysis, see
`PROJECT.md` and the LTS BSP's `PROJECT.md`.
