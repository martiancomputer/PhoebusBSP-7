#!/bin/bash
# Cross-build the userspace the s6 boot bundle needs but the SDK's
# rootfs/build-rootfs.sh does not install: hostapd (+libnl), iptables, dropbear, dnsmasq,
# wireless_tools. Also copies the SDK's rootfs/usr/ tree, which build-rootfs.sh
# never copies.
#
# Without this, a clean build boots with working radios and no way to use them:
#   ./run: exec: line 4: hostapd: not found
#   ./run: line 21: dropbearkey: not found      -> dropbear: ECDSA keygen FAILED
#   /etc/s6/scripts/nat-up: line 21: iptables: not found   (x12, NAT dead)
#
# Hardware-specific sources come from the vendor GPL drop
# (rtl8198d-sdk-main). Security-facing daemons come from current upstream
# release archives and are pinned by sha256 below.
#
# Usage: tools/build-userspace.sh <rootfs-tree> [vendor-sdk-root]
set -e

OUT="${1:?usage: build-userspace.sh <rootfs-tree> [vendor-sdk-root]}"
VENDOR_SDK="${2:-${VENDOR_SDK:-}}"
CROSS_COMPILE="${CROSS_COMPILE:-mips-buildroot-linux-gnu-}"
HOST="${CROSS_COMPILE%-}"
HERE=$(cd "$(dirname "$0")" && pwd)
BSP=$(cd "$HERE/.." && pwd)
WORK="${WORK:-$BSP/build}/userspace"
STAGING="$WORK/staging"

[ -d "$OUT" ] || { echo "ERROR: rootfs tree not found: $OUT" >&2; exit 1; }

# --- locate the vendor GPL drop -------------------------------------------
if [ -z "$VENDOR_SDK" ]; then
	for c in "$BSP/../OpenWRT1500/rtl8198d-sdk-main" \
	         "$BSP/../Luna_Project/GPL-Sources/rtl8198d-sdk-main" \
	         "$HOME/Luna_Project/GPL-Sources/rtl8198d-sdk-main" \
	         "$HOME/OpenWRT1500/rtl8198d-sdk-main" \
	         "$BSP/../rtl8198d-sdk-main"; do
		[ -d "$c/user" ] && { VENDOR_SDK=$(cd "$c" && pwd); break; }
	done
fi

# Fail loudly rather than skipping. The SDK's wireless_tools block guards on an
# unset $WT_SRC, so `[ -d "" ]` is always false and it has silently never run --
# do not repeat that.
WT_SRC="$VENDOR_SDK/user/wireless_tools"
# dnsmasq is the one source NOT taken from the vendor drop. The drop's
# dnsmasq-2.85 is Realtek-patched -- src/dnsmasq.h unconditionally does
#     #include <rtk/options.h>
# and no rtk/ headers ship anywhere in the drop, so it cannot build from it:
#     dnsmasq.h:67:10: fatal error: rtk/options.h: No such file or directory
# Upstream 2.90 is fetched and pinned by hash instead. That also matches the
# version the SDK's dnsmasq.conf was written against.
DROPBEAR_VER=2026.94
DROPBEAR_SHA256=e098034a843699200c8c977a991fff73159735bf795d5f72ef672c41a6b1ae81
DROPBEAR_URL="https://matt.ucc.asn.au/dropbear/releases/dropbear-$DROPBEAR_VER.tar.bz2"
DNSMASQ_VER=2.93
DNSMASQ_SHA256=0c00d4e5c97c8306e5fb932b348b34269c9c29a0e7df0e8e82958b407092bc19
DNSMASQ_URL="https://thekelleys.org.uk/dnsmasq/dnsmasq-$DNSMASQ_VER.tar.xz"
LIBNL_VER=3.12.0
LIBNL_SHA256=fc51ca7196f1a3f5fdf6ffd3864b50f4f9c02333be28be4eeca057e103c0dd18
LIBNL_URL="https://github.com/thom311/libnl/releases/download/libnl3_12_0/libnl-$LIBNL_VER.tar.gz"
HOSTAPD_VER=2.12
HOSTAPD_SHA256=f43502561c28ba47ab77e18e1a973d07361c68cc8b14178e619bd5796b70eabd
HOSTAPD_URL="https://w1.fi/releases/hostapd-$HOSTAPD_VER.tar.gz"
IPTABLES_VER=1.8.13
IPTABLES_SHA256=1afcd33da9e8f913ace6a2126788162e207e26f5d5e29c6573c0e581ffc58b99
IPTABLES_URL="https://www.netfilter.org/projects/iptables/files/iptables-$IPTABLES_VER.tar.xz"
OPENSSL_VER=3.5.8
OPENSSL_SHA256=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VER/openssl-$OPENSSL_VER.tar.gz"

missing=""
for d in "$WT_SRC"; do
	[ -d "$d" ] || missing="$missing\n    $d"
done
if [ -n "$VENDOR_SDK" ] && [ -n "$missing" ] || [ -z "$VENDOR_SDK" ]; then
	if [ -z "$VENDOR_SDK" ]; then
		echo "ERROR: vendor GPL drop not found." >&2
	else
		echo "ERROR: vendor GPL drop at $VENDOR_SDK is missing:" >&2
		printf "$missing\n" >&2
	fi
	echo "  Pass it explicitly:  VENDOR_SDK=/path/to/rtl8198d-sdk-main $0 $OUT" >&2
	echo "  It is the unpacked Realtek GPL release; it is not in any repo." >&2
	exit 1
fi

command -v "${CROSS_COMPILE}gcc" >/dev/null || { echo "ERROR: ${CROSS_COMPILE}gcc not on PATH" >&2; exit 1; }
SYSROOT="$(${CROSS_COMPILE}gcc -print-sysroot)"
mkdir -p "$WORK" "$STAGING" "$OUT/sbin" "$OUT/lib" "$OUT/usr/sbin"

say() { echo "userspace: $*"; }

fetch_source() {
	name=$1 url=$2 sha=$3 archive=$4 dest=$5
	mkdir -p "$WORK/dl"
	tarball="$WORK/dl/$archive"
	if [ ! -f "$tarball" ] ||
	   ! echo "$sha  $tarball" | sha256sum -c - >/dev/null 2>&1; then
		rm -f "$tarball"
		curl -fL --retry 3 -o "$tarball" "$url"
		echo "$sha  $tarball" | sha256sum -c - >/dev/null || {
			echo "ERROR: $name archive failed its sha256 check" >&2
			exit 1
		}
	fi
	rm -rf "$dest"; mkdir -p "$dest"
	tar xf "$tarball" -C "$dest" --strip-components=1
}

# --- 1. libnl (hostapd's nl80211 transport) --------------------------------
if [ ! -f "$STAGING/.libnl-$LIBNL_VER" ]; then
	say "libnl-$LIBNL_VER"
	fetch_source "libnl-$LIBNL_VER" "$LIBNL_URL" "$LIBNL_SHA256" \
		"libnl-$LIBNL_VER.tar.gz" "$WORK/libnl"
	( cd "$WORK/libnl"
	  ./configure --host="$HOST" --prefix="$STAGING" \
	              --disable-cli --disable-static --enable-shared >/dev/null
	  make -j"$(nproc)" >/dev/null && make install >/dev/null )
	touch "$STAGING/.libnl-$LIBNL_VER"
fi
# -L (dereference), NOT -a alone. libtool installs libnl-3.so.200 as a symlink to
# libnl-3.so.200.20.0; `cp -a` copies the *link* and leaves it dangling in the
# image, which the loader reports at runtime as
#     hostapd: error while loading shared libraries: libnl-3.so.200:
#     cannot open shared object file: No such file or directory
# Copying the target under the SONAME is what the loader actually wants.
for l in libnl-3.so.200 libnl-genl-3.so.200; do
	rm -f "$OUT/lib/$l"          # cp refuses to write *through* a dangling link
	cp -aL "$STAGING/lib/$l" "$OUT/lib/$l"
done

# --- 2. openssl (static; SAE needs real EC crypto) -------------------------
# hostapd's internal crypto (CONFIG_TLS=internal + libtommath) has no ECC, so
# CONFIG_SAE fails to link:
#     undefined reference to `crypto_ecdh_get_pubkey'
#     undefined reference to `crypto_bignum_deinit'
# The shipped hostapd.conf uses `wpa_key_mgmt=WPA-PSK SAE` and `sae_pwe=2`, so
# SAE is not optional here -- build openssl and use CONFIG_TLS=openssl.
if [ ! -f "$STAGING/.openssl-$OPENSSL_VER" ]; then
	say "openssl-$OPENSSL_VER LTS (static libcrypto/libssl for SAE)"
	fetch_source "openssl-$OPENSSL_VER" "$OPENSSL_URL" "$OPENSSL_SHA256" \
		"openssl-$OPENSSL_VER.tar.gz" "$WORK/openssl"
	( cd "$WORK/openssl"
	  ./Configure linux-mips32 --prefix="$STAGING" \
	      --cross-compile-prefix="$CROSS_COMPILE" \
	      no-shared no-async no-dso no-engine no-tests no-ssl3 no-comp >/dev/null
	  make -j"$(nproc)" build_libs >/dev/null
	  make install_dev >/dev/null )
	touch "$STAGING/.openssl-$OPENSSL_VER"
fi

# --- 3. hostapd ------------------------------------------------------------
say "hostapd-$HOSTAPD_VER (nl80211, openssl crypto, WPA2 + WPA3/SAE)"
fetch_source "hostapd-$HOSTAPD_VER" "$HOSTAPD_URL" "$HOSTAPD_SHA256" \
	"hostapd-$HOSTAPD_VER.tar.gz" "$WORK/hostapd"
# This option set is the one that produced a working SAE hostapd for the 6.18
# image; do not swap TLS=openssl for internal without re-checking SAE links.
cat > "$WORK/hostapd/hostapd/.config" <<'CFG'
CONFIG_DRIVER_NL80211=y
CONFIG_LIBNL32=y
CONFIG_IEEE80211N=y
CONFIG_IEEE80211AC=y
CONFIG_IEEE80211AX=y
CONFIG_TLS=openssl
CONFIG_SAE=y
CONFIG_ELOOP=eloop
CONFIG_NO_RANDOM_POOL=y
CFG
# hostapd Makefile bug: its `ifdef CONFIG_SAE` block sets NEED_HMAC_SHA384_KDF
# but NOT NEED_SHA384, so NEED_SHA384 never gates in -DCONFIG_SHA384,
# crypto_openssl.o is built without it, and the link dies with
#     undefined reference to `hmac_sha384_vector'
# The CONFIG_OWE block immediately below sets both, which is what it should look
# like. Patch the SAE block only -- NEED_SHA384=y already appears in several
# unrelated blocks, so a file-wide grep guard silently skips the fix.
python3 - "$WORK/hostapd/hostapd/Makefile" <<'PY'
import sys
p = sys.argv[1]
lines = open(p).read().split('\n')

# Find the CONFIG_SAE block by matching ifdef/endif DEPTH -- a non-greedy regex
# to the first `endif` stops at the nested CONFIG_SAE_PK one and silently
# patches nothing.
start = next((i for i, l in enumerate(lines) if l.strip() == 'ifdef CONFIG_SAE'), None)
if start is None:
    sys.exit("ERROR: no `ifdef CONFIG_SAE` in hostapd's Makefile")
depth, end = 0, None
for i in range(start, len(lines)):
    st = lines[i].strip()
    if st.startswith(('ifdef ', 'ifndef ', 'ifeq', 'ifneq')):
        depth += 1
    elif st == 'endif':
        depth -= 1
        if depth == 0:
            end = i
            break
if end is None:
    sys.exit("ERROR: unterminated CONFIG_SAE block in hostapd's Makefile")

block = lines[start:end]
if any(l.strip() == 'NEED_SHA384=y' for l in block):
    sys.exit(0)                                  # already correct
tgt = next((i for i, l in enumerate(block) if l.strip() == 'NEED_HMAC_SHA384_KDF=y'), None)
if tgt is None:
    # hostapd 2.12 no longer unconditionally requests SHA-384 for SAE. Its
    # later dependency block enables SHA-384 and the KDF together only when a
    # selected feature (for example 802.11be) actually needs them.
    print("userspace: hostapd SAE SHA-384 dependency logic is already current")
    sys.exit(0)
block.insert(tgt + 1, 'NEED_SHA384=y')
lines[start:end] = block
open(p, 'w').write('\n'.join(lines))

# Verify, rather than assuming. Reporting success without checking is how the
# first two attempts at this patch "succeeded" while changing nothing.
check = open(p).read().split('\n')
s2 = next(i for i, l in enumerate(check) if l.strip() == 'ifdef CONFIG_SAE')
d, e2 = 0, None
for i in range(s2, len(check)):
    st = check[i].strip()
    if st.startswith(('ifdef ', 'ifndef ', 'ifeq', 'ifneq')): d += 1
    elif st == 'endif':
        d -= 1
        if d == 0: e2 = i; break
if not any(l.strip() == 'NEED_SHA384=y' for l in check[s2:e2]):
    sys.exit("ERROR: NEED_SHA384 patch did not take")
print("userspace: patched NEED_SHA384 into hostapd's CONFIG_SAE block")
PY
# PKG_CONFIG_LIBDIR (not _PATH) or pkg-config falls back to the HOST's
# /usr/include/libnl3 and gcc rejects it as an unsafe cross path.
( cd "$WORK/hostapd/hostapd"
  make clean >/dev/null 2>&1 || true
  PKG_CONFIG_LIBDIR="$STAGING/lib/pkgconfig" \
  CC="${CROSS_COMPILE}gcc" LD="${CROSS_COMPILE}ld" \
  CFLAGS="-I$STAGING/include -I$STAGING/include/libnl3 -O2" \
  LDFLAGS="-L$STAGING/lib" LIBS="-latomic" \
  make -j"$(nproc)" hostapd hostapd_cli >/dev/null )
cp "$WORK/hostapd/hostapd/hostapd"     "$OUT/sbin/hostapd"
cp "$WORK/hostapd/hostapd/hostapd_cli" "$OUT/sbin/hostapd_cli"
${CROSS_COMPILE}strip "$OUT/sbin/hostapd" "$OUT/sbin/hostapd_cli" 2>/dev/null || true
# OpenSSL 3.5 uses 64-bit atomics that MIPS32 supplies through libatomic.
cp -aL "$SYSROOT/lib/libatomic.so.1" "$OUT/lib/libatomic.so.1"
${CROSS_COMPILE}strip "$OUT/lib/libatomic.so.1" 2>/dev/null || true

# --- 4. iptables (legacy setsockopt backend) -------------------------------
say "iptables-$IPTABLES_VER (legacy backend)"
fetch_source "iptables-$IPTABLES_VER" "$IPTABLES_URL" "$IPTABLES_SHA256" \
	"iptables-$IPTABLES_VER.tar.xz" "$WORK/iptables"
( cd "$WORK/iptables"
  # These three need libraries we do not ship; none matter for NAT.
  rm -f extensions/libxt_connlabel.c extensions/libxt_macrange.c extensions/libxt_TCPTERMAC.c
  # NB: no --with-kernel. That drags *internal* kernel headers in and dies on
  # asm/rwonce.h; the sysroot's exported headers are the right ones.
  PKG_CONFIG_LIBDIR="$STAGING/lib/pkgconfig" \
  ./configure --host="$HOST" --prefix="$STAGING" \
              --disable-nftables --disable-shared --enable-static --disable-ipv6 >/dev/null
  # utils/nfnl_osf wants libnfnetlink and aborts the recursive build before it
  # reaches the binary, so build the pieces directly. libiptc first --
  # xtables-multi links ../libiptc/libip4tc.la and the top-level make never gets
  # far enough to produce it.
  make -C libxtables -j"$(nproc)" >/dev/null
  make -C libiptc    -j"$(nproc)" >/dev/null
  make -C extensions -j"$(nproc)" >/dev/null
  make -C iptables   -j"$(nproc)" >/dev/null )
cp "$WORK/iptables/iptables/xtables-legacy-multi" "$OUT/sbin/xtables-multi"
${CROSS_COMPILE}strip "$OUT/sbin/xtables-multi" 2>/dev/null || true
for t in iptables iptables-save iptables-restore; do
	ln -sf xtables-multi "$OUT/sbin/$t"
done
cp -a "$SYSROOT/lib/libresolv.so.2" "$OUT/lib/" 2>/dev/null || true

# --- 5. libxcrypt (dropbear password auth needs crypt()) -------------------
# The bootlin glibc sysroot ships no crypt.h and no libcrypt, so dropbear stops
# with:  sysoptions.h:239: #error "DROPBEAR_SVR_PASSWORD_AUTH requires `crypt()'."
# libxcrypt is NOT in the vendor GPL drop, so it has to be found separately.
if [ ! -f "$STAGING/lib/libcrypt.a" ]; then
	if [ -z "$LIBXCRYPT_SRC" ]; then
		for c in "$BSP/../OpenWRT1500/sdk-rtl9607c-6.18/net-build/libxcrypt" \
		         "$HOME/OpenWRT1500/sdk-rtl9607c-6.18/net-build/libxcrypt" \
		         "$VENDOR_SDK/lib/libxcrypt"; do
			[ -f "$c/configure" ] && { LIBXCRYPT_SRC=$(cd "$c" && pwd); break; }
		done
	fi
	if [ -z "$LIBXCRYPT_SRC" ]; then
		echo "ERROR: libxcrypt source not found, and the toolchain sysroot has no crypt()." >&2
		echo "  dropbear's password auth cannot be built without it, and the image" >&2
		echo "  provisions an admin password hash in /etc/shadow, so pubkey-only is" >&2
		echo "  not a usable fallback here." >&2
		echo "  Set LIBXCRYPT_SRC=/path/to/libxcrypt (4.4.x) and re-run." >&2
		exit 1
	fi
	say "libxcrypt $(basename "$LIBXCRYPT_SRC") (static libcrypt for dropbear)"
	rm -rf "$WORK/libxcrypt"; cp -a "$LIBXCRYPT_SRC" "$WORK/libxcrypt"
	( cd "$WORK/libxcrypt"
	  make clean >/dev/null 2>&1 || true
	  ./configure --host="$HOST" --prefix="$STAGING" \
	              --disable-shared --enable-static --disable-werror >/dev/null
	  make -j"$(nproc)" >/dev/null && make install >/dev/null )
fi

# --- 6. dropbear -----------------------------------------------------------
say "dropbear-$DROPBEAR_VER"
mkdir -p "$WORK/dl"
DROPBEAR_TAR="$WORK/dl/dropbear-$DROPBEAR_VER.tar.bz2"
if [ ! -f "$DROPBEAR_TAR" ] ||
   ! echo "$DROPBEAR_SHA256  $DROPBEAR_TAR" | sha256sum -c - >/dev/null 2>&1; then
	rm -f "$DROPBEAR_TAR"
	curl -fL --retry 3 -o "$DROPBEAR_TAR" "$DROPBEAR_URL"
	echo "$DROPBEAR_SHA256  $DROPBEAR_TAR" | sha256sum -c - >/dev/null || {
		echo "ERROR: dropbear-$DROPBEAR_VER archive failed its sha256 check" >&2
		exit 1
	}
fi
rm -rf "$WORK/dropbear"; mkdir -p "$WORK/dropbear"
tar xf "$DROPBEAR_TAR" -C "$WORK/dropbear" --strip-components=1
( cd "$WORK/dropbear"
  make clean >/dev/null 2>&1 || true
  ./configure --host="$HOST" --prefix=/usr \
              CPPFLAGS="-I$STAGING/include" LDFLAGS="-L$STAGING/lib" LIBS="-lcrypt" \
              --disable-zlib --disable-lastlog --disable-utmp --disable-utmpx \
              --disable-wtmp --disable-wtmpx --disable-pututline \
              --disable-pututxline --enable-bundled-libtom >/dev/null
  make -j"$(nproc)" PROGRAMS="dropbear dropbearkey dropbearconvert scp" MULTI=1 >/dev/null )
cp "$WORK/dropbear/dropbearmulti" "$OUT/sbin/dropbearmulti"
${CROSS_COMPILE}strip "$OUT/sbin/dropbearmulti" 2>/dev/null || true
for t in dropbear dropbearkey dropbearconvert scp; do
	ln -sf dropbearmulti "$OUT/sbin/$t"
done

# --- 7. wireless_tools -----------------------------------------------------
# The SDK has this block but guards it on an unset $WT_SRC, so it never runs.
say "wireless_tools (iwconfig/iwlist/iwpriv)"
rm -rf "$WORK/wt"; cp -a "$WT_SRC" "$WORK/wt"
( cd "$WORK/wt"
  make clean >/dev/null 2>&1 || true
  make CC="${CROSS_COMPILE}gcc" AR="${CROSS_COMPILE}ar" RANLIB="${CROSS_COMPILE}ranlib" \
       LDFLAGS="-lm" -j"$(nproc)" >/dev/null 2>&1 || true )
for b in iwconfig iwlist iwpriv iwgetid; do
	[ -f "$WORK/wt/$b" ] && { cp "$WORK/wt/$b" "$OUT/sbin/$b"; ${CROSS_COMPILE}strip "$OUT/sbin/$b" 2>/dev/null || true; }
done
cp -a "$SYSROOT/lib/libm.so.6" "$OUT/lib/" 2>/dev/null || true

# --- 8. dnsmasq (DNS forwarder for LAN/WiFi clients) -----------------------
# udhcpd hands clients the router LAN address as DNS, so without a
# resolver listening on 53 every client gets an address that answers nothing.
# SDK 88dce28 added the service and the config but nothing built the binary,
# so the s6 longrun would exec a missing file and crash-loop forever.
#
# Forwarder only. DHCP is udhcpd's job here and DNSSEC/TFTP/auth/scripts are
# not wanted: each one compiled out is attack surface not shipped. That also
# keeps the dependency set at libc alone -- no nettle/gmp for DNSSEC.
say "dnsmasq-$DNSMASQ_VER (forwarder only)"
mkdir -p "$WORK/dl"
TAR="$WORK/dl/dnsmasq-$DNSMASQ_VER.tar.xz"
# Verify on every run, not just after downloading: a truncated or tampered
# cache file must not be trusted just because it is already on disk.
if [ ! -f "$TAR" ] || ! echo "$DNSMASQ_SHA256  $TAR" | sha256sum -c - >/dev/null 2>&1; then
	rm -f "$TAR"
	curl -fL --retry 3 -o "$TAR" "$DNSMASQ_URL"
	echo "$DNSMASQ_SHA256  $TAR" | sha256sum -c - >/dev/null \
		|| { echo "ERROR: dnsmasq-$DNSMASQ_VER.tar.xz failed its sha256 check" >&2; exit 1; }
fi
rm -rf "$WORK/dnsmasq"; mkdir -p "$WORK/dnsmasq"
tar xf "$TAR" -C "$WORK/dnsmasq" --strip-components=1
( cd "$WORK/dnsmasq"
  make -j"$(nproc)" \
       CC="${CROSS_COMPILE}gcc" \
       COPTS="-DNO_DHCP -DNO_DHCP6 -DNO_TFTP -DNO_DNSSEC -DNO_AUTH -DNO_SCRIPT -DNO_DUMPFILE -DNO_IPSET -DNO_LOOP" \
       >/dev/null )
cp "$WORK/dnsmasq/src/dnsmasq" "$OUT/usr/sbin/dnsmasq"
${CROSS_COMPILE}strip "$OUT/usr/sbin/dnsmasq" 2>/dev/null || true

# dnsmasq drops privileges to user=nobody/group=nogroup per the SDK's config,
# and refuses to start if it cannot resolve them. The SDK adds both to
# etc/passwd and etc/group; fail here rather than at boot if that regressed.
for ent in "nobody:etc/passwd" "nogroup:etc/group"; do
	name=${ent%%:*}; file=${ent#*:}
	grep -q "^$name:" "$OUT/$file" 2>/dev/null || {
		echo "ERROR: dnsmasq needs '$name' in $file (user=/group= in dnsmasq.conf)" >&2
		exit 1
	}
done

# --- 9. the SDK's rootfs/usr, which build-rootfs.sh never copies -----------
# usr/share/udhcpc/default.script is what `udhcpc -s` execs; without it the WAN
# gets a lease and never configures the interface.
if [ -d "$BSP/sdk/rootfs/usr" ]; then
	say "sdk rootfs/usr (udhcpc default.script, phoebus-check)"
	cp -a "$BSP/sdk/rootfs/usr/." "$OUT/usr/"
	[ -f "$OUT/usr/share/udhcpc/default.script" ] && chmod 0755 "$OUT/usr/share/udhcpc/default.script"
	[ -f "$OUT/usr/bin/phoebus-check" ] && chmod 0755 "$OUT/usr/bin/phoebus-check"
fi

# --- 10. guard: every binary the boot bundle execs must now exist -----------
# This is the check whose absence let hostapd/dropbear/iptables ship missing.
#
# The service loop below derives the binary from each run script's `exec` line
# rather than matching a fixed list of names. The list is still used for the
# other tools a script happens to call, but it must not be the only source:
# when SDK 88dce28 added the dnsmasq service, "dnsmasq" was not in the list, so
# this guard passed a bundle whose new longrun could only crash-loop. An
# allowlist can only catch what someone already thought of, which is the wrong
# shape for a check meant to catch the thing nobody thought of.
say "verifying the boot bundle's binaries resolve"
fail=0
for svc in $(cat "$OUT/etc/s6/source/ok-all/contents" 2>/dev/null); do
	run="$OUT/etc/s6/source/$svc/run"
	[ -f "$run" ] || continue
	execd=$(sed -n 's|^[[:space:]]*exec[[:space:]]\{1,\}\(/[^[:space:]]*/\)\{0,1\}\([A-Za-z0-9_.-]\{1,\}\).*|\2|p' "$run")
	for w in $(printf '%s\n%s\n' "$execd" \
	           "$(grep -oE '\b(hostapd|hostapd_cli|dropbear|dropbearkey|iptables|udhcpd|udhcpc|syslogd|klogd|brctl|ip)\b' "$run")" \
	           | grep -v '^$' | sort -u); do
		found=0
		for d in bin sbin usr/bin usr/sbin; do
			[ -e "$OUT/$d/$w" ] && { found=1; break; }
		done
		[ "$found" = 1 ] || { echo "  MISSING: $w (needed by service '$svc')" >&2; fail=1; }
	done
done
for s in etc/s6/scripts/nat-up etc/s6/scripts/network-up; do
	[ -f "$OUT/$s" ] || continue
	for w in $(grep -oE '\b(iptables|brctl|udhcpc|ip)\b' "$OUT/$s" | sort -u); do
		found=0
		for d in bin sbin usr/bin usr/sbin; do
			[ -e "$OUT/$d/$w" ] && { found=1; break; }
		done
		[ "$found" = 1 ] || { echo "  MISSING: $w (needed by $s)" >&2; fail=1; }
	done
done
[ "$fail" = 0 ] || { echo "ERROR: the image would boot with unusable services" >&2; exit 1; }

# --- 11. guard: every DT_NEEDED library must RESOLVE inside the image ------
# Checking only that a file of the right NAME exists is not enough: a dangling
# symlink passes that and then fails at exec time. Resolve each one for real.
say "verifying shared libraries resolve"
# `[ -e ]` follows symlinks, so a dangling link fails this the way it fails exec.
unresolved=$(
	for b in "$OUT"/sbin/* "$OUT"/bin/* "$OUT"/usr/sbin/*; do
		[ -f "$b" ] || continue
		${CROSS_COMPILE}readelf -d "$b" 2>/dev/null \
			| grep -oP '(?<=Shared library: \[)[^]]+'
	done | sort -u | while read -r lib; do
		found=0
		for d in lib usr/lib; do [ -e "$OUT/$d/$lib" ] && found=1; done
		[ "$found" = 1 ] || echo "$lib"
	done)
if [ -n "$unresolved" ]; then
	echo "ERROR: these libraries are missing or dangling in the image:" >&2
	echo "$unresolved" | sed 's/^/    /' >&2
	exit 1
fi

# --- 12. runtime dirs the services expect ---------------------------------
# udhcpd: can't open '/var/lib/misc/udhcpd.leases': No such file or directory
mkdir -p "$OUT/var/lib/misc" "$OUT/etc/dropbear"
: > "$OUT/var/lib/misc/udhcpd.leases"

say "done -> $OUT"
