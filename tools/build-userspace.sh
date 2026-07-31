#!/bin/bash
# Cross-build the userspace the s6 boot bundle needs but the SDK's
# rootfs/build-rootfs.sh does not install: hostapd (+libnl), iptables, dropbear,
# wireless_tools. Also copies the SDK's rootfs/usr/ tree, which build-rootfs.sh
# never copies.
#
# Without this, a clean build boots with working radios and no way to use them:
#   ./run: exec: line 4: hostapd: not found
#   ./run: line 21: dropbearkey: not found      -> dropbear: ECDSA keygen FAILED
#   /etc/s6/scripts/nat-up: line 21: iptables: not found   (x12, NAT dead)
#
# All sources ship in the vendor GPL drop (rtl8198d-sdk-main); nothing is
# downloaded. Point VENDOR_SDK at it.
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
	         "$HOME/OpenWRT1500/rtl8198d-sdk-main" \
	         "$BSP/../rtl8198d-sdk-main"; do
		[ -d "$c/user" ] && { VENDOR_SDK=$(cd "$c" && pwd); break; }
	done
fi

# Fail loudly rather than skipping. The SDK's wireless_tools block guards on an
# unset $WT_SRC, so `[ -d "" ]` is always false and it has silently never run --
# do not repeat that.
LIBNL_SRC="$VENDOR_SDK/lib/libnl/libnl-3.2.25"
HOSTAPD_SRC="$VENDOR_SDK/user/hostapd/hostapd-2.11"
IPTABLES_SRC="$VENDOR_SDK/user/iptables-1.4.21"
DROPBEAR_SRC="$VENDOR_SDK/user/dropbear/dropbear-2019.78"
WT_SRC="$VENDOR_SDK/user/wireless_tools"
OPENSSL_SRC="$VENDOR_SDK/lib/libssl/openssl-1.1.1t"

missing=""
for d in "$LIBNL_SRC" "$HOSTAPD_SRC" "$IPTABLES_SRC" "$DROPBEAR_SRC" "$WT_SRC" "$OPENSSL_SRC"; do
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

# --- 1. libnl (hostapd's nl80211 transport) --------------------------------
if [ ! -f "$STAGING/lib/libnl-3.so" ]; then
	say "libnl-3.2.25"
	rm -rf "$WORK/libnl"; cp -a "$LIBNL_SRC" "$WORK/libnl"
	( cd "$WORK/libnl"
	  ./configure --host="$HOST" --prefix="$STAGING" \
	              --disable-cli --disable-static --enable-shared >/dev/null
	  make -j"$(nproc)" >/dev/null && make install >/dev/null )
fi
for l in libnl-3.so.200 libnl-genl-3.so.200; do
	cp -a "$STAGING/lib/$l" "$OUT/lib/" 2>/dev/null || true
done

# --- 2. openssl (static; SAE needs real EC crypto) -------------------------
# hostapd's internal crypto (CONFIG_TLS=internal + libtommath) has no ECC, so
# CONFIG_SAE fails to link:
#     undefined reference to `crypto_ecdh_get_pubkey'
#     undefined reference to `crypto_bignum_deinit'
# The shipped hostapd.conf uses `wpa_key_mgmt=WPA-PSK SAE` and `sae_pwe=2`, so
# SAE is not optional here -- build openssl and use CONFIG_TLS=openssl.
if [ ! -f "$STAGING/lib/libcrypto.a" ]; then
	say "openssl-1.1.1t (static libcrypto/libssl for SAE)"
	rm -rf "$WORK/openssl"; cp -a "$OPENSSL_SRC" "$WORK/openssl"
	( cd "$WORK/openssl"
	  ./Configure linux-mips32 --prefix="$STAGING" \
	      --cross-compile-prefix="$CROSS_COMPILE" \
	      no-shared no-async no-dso no-engine no-tests no-ssl3 no-comp >/dev/null
	  make -j"$(nproc)" build_libs >/dev/null
	  make install_dev >/dev/null )
fi

# --- 3. hostapd ------------------------------------------------------------
say "hostapd-2.11 (nl80211, openssl crypto, WPA2 + WPA3/SAE)"
rm -rf "$WORK/hostapd"; cp -a "$HOSTAPD_SRC" "$WORK/hostapd"
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
    sys.exit("ERROR: CONFIG_SAE block has no NEED_HMAC_SHA384_KDF to anchor on")
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
  LDFLAGS="-L$STAGING/lib" \
  make -j"$(nproc)" hostapd hostapd_cli >/dev/null )
cp "$WORK/hostapd/hostapd/hostapd"     "$OUT/sbin/hostapd"
cp "$WORK/hostapd/hostapd/hostapd_cli" "$OUT/sbin/hostapd_cli"
${CROSS_COMPILE}strip "$OUT/sbin/hostapd" "$OUT/sbin/hostapd_cli" 2>/dev/null || true

# --- 4. iptables (legacy setsockopt backend) -------------------------------
say "iptables-1.4.21 (legacy backend)"
rm -rf "$WORK/iptables"; cp -a "$IPTABLES_SRC" "$WORK/iptables"
( cd "$WORK/iptables"
  # These three need libraries we do not ship; none matter for NAT.
  rm -f extensions/libxt_connlabel.c extensions/libxt_macrange.c extensions/libxt_TCPTERMAC.c
  # NB: no --with-kernel. That drags *internal* kernel headers in and dies on
  # asm/rwonce.h; the sysroot's exported headers are the right ones.
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
cp "$WORK/iptables/iptables/xtables-multi" "$OUT/sbin/xtables-multi"
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
say "dropbear-2019.78"
rm -rf "$WORK/dropbear"; cp -a "$DROPBEAR_SRC" "$WORK/dropbear"
( cd "$WORK/dropbear"
  make clean >/dev/null 2>&1 || true
  ./configure --host="$HOST" --prefix=/usr \
              CPPFLAGS="-I$STAGING/include" LDFLAGS="-L$STAGING/lib" LIBS="-lcrypt" \
              --disable-zlib --disable-utmp --disable-utmpx --disable-wtmp \
              --disable-lastlog --disable-pututline --disable-pututxline >/dev/null
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

# --- 8. the SDK's rootfs/usr, which build-rootfs.sh never copies -----------
# usr/share/udhcpc/default.script is what `udhcpc -s` execs; without it the WAN
# gets a lease and never configures the interface.
if [ -d "$BSP/sdk/rootfs/usr" ]; then
	say "sdk rootfs/usr (udhcpc default.script, phoebus-check)"
	cp -a "$BSP/sdk/rootfs/usr/." "$OUT/usr/"
	[ -f "$OUT/usr/share/udhcpc/default.script" ] && chmod 0755 "$OUT/usr/share/udhcpc/default.script"
	[ -f "$OUT/usr/bin/phoebus-check" ] && chmod 0755 "$OUT/usr/bin/phoebus-check"
fi

# --- 9. guard: every binary the boot bundle execs must now exist -----------
# This is the check whose absence let hostapd/dropbear/iptables ship missing.
say "verifying the boot bundle's binaries resolve"
fail=0
for svc in $(cat "$OUT/etc/s6/source/ok-all/contents" 2>/dev/null); do
	run="$OUT/etc/s6/source/$svc/run"
	[ -f "$run" ] || continue
	for w in $(grep -oE '\b(hostapd|hostapd_cli|dropbear|dropbearkey|iptables|udhcpd|udhcpc|syslogd|klogd|brctl|ip)\b' "$run" | sort -u); do
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

say "done -> $OUT"
