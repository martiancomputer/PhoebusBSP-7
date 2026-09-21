#!/bin/sh
# PhoebusBSP-7 — build a bootable mainline Linux 7.3-rc for RTL9607C/Cv2.
#
# Reconstructs the ported kernel tree from three ingredients:
#   1. pristine upstream linux-${KVER} (downloaded)
#   2. pristine vendor SoC code from the Phoebus-SDK submodule (grafted in)
#   3. our 7.1 port, applied as the overlay/ tree
# then configures, builds, and packages a U-Boot image.
#
# Usage: ./build.sh              # full build -> images/
#        KVER=7.3-rc4 ./build.sh # rebase onto another mainline release
set -e

BSP=$(cd "$(dirname "$0")" && pwd)
KVER="${KVER:-7.3-rc3}"
KMAJ="${KVER%%.*}"                   # 7 -> cdn.kernel.org/pub/linux/kernel/v7.x
SDK="$BSP/sdk"                       # Phoebus-SDK submodule
WORK="$BSP/build"
K="$WORK/linux-$KVER"
PRISTINE="$WORK/pristine/linux-$KVER"
CROSS_COMPILE="${CROSS_COMPILE:-mips-buildroot-linux-gnu-}"
JOBS="${JOBS:-$(nproc)}"

[ -d "$SDK/vendor/realtek-net" ] || { echo "ERROR: Phoebus-SDK submodule missing. Run: git submodule update --init"; exit 1; }

# --- 0. host tool preflight (fail fast, not 10 minutes into the build) ---
missing=""
for t in make gcc git bison flex bc mkimage lzma; do
	command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
done
if [ -n "$missing" ]; then
	echo "ERROR: missing host tools:$missing"
	echo "  Arch/Artix : sudo pacman -S base-devel bc uboot-tools xz"
	echo "  Debian/Ubu : sudo apt install build-essential bc u-boot-tools xz-utils flex bison"
	echo "  (mkimage = uboot-tools/u-boot-tools; bc = kernel timeconst; lzma = xz-utils)"
	exit 1
fi

# --- 1. toolchain (downloaded by the SDK, not committed) ---
"$SDK/scripts/fetch-toolchain.sh" "$BSP/toolchain"
export PATH="$BSP/toolchain/mips32--glibc--stable-2025.08-1/bin:$PATH"
export ARCH=mips CROSS_COMPILE

# --- 2. pristine kernel ---
# Mainline release candidates are taken from Linus's official kernel.org Git
# tree. Stable releases retain the faster CDN tarball path. Keep an immutable
# pristine cache and reconstruct the disposable build tree for every run.
mkdir -p "$WORK/pristine"; cd "$WORK"
if [ ! -d "$PRISTINE" ]; then
	case "$KVER" in
	*-rc*) git clone --depth 1 --branch "v$KVER" --single-branch \
		       https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git \
		       "$PRISTINE" ;;
	*) [ -f "linux-$KVER.tar.xz" ] || curl -fL --retry 3 -O \
		"https://cdn.kernel.org/pub/linux/kernel/v$KMAJ.x/linux-$KVER.tar.xz"
		mkdir -p "$PRISTINE"
		tar xf "linux-$KVER.tar.xz" -C "$PRISTINE" --strip-components=1 ;;
	esac
fi
EXPECTED_KVER="$KVER"
case "$KVER" in
*-rc*) EXPECTED_KVER="${KVER%%-rc*}.0-rc${KVER##*-rc}" ;;
esac
test "$(make -s -C "$PRISTINE" kernelversion)" = "$EXPECTED_KVER" || {
	echo "ERROR: pristine cache is not Linux $KVER: $PRISTINE" >&2
	exit 1
}
rm -rf "$K"
cp -a --reflink=auto "$PRISTINE" "$K"

# --- 3. graft pristine vendor SoC code (must match the baseline the overlay was cut against) ---
cp -a "$SDK/vendor/realtek-net/."                                "$K/drivers/net/ethernet/realtek/"
# Wi-Fi. This also replaces drivers/net/wireless/realtek/{Kconfig,Makefile} with
# the vendor pair that source/build g6_wifi_driver (5GHz RTL8832BR) and rtl8192cd
# (2.4GHz RTL8192F). Without it the overlay still drops its handful of modified
# driver files into the tree, but upstream's Kconfig never sources them, so the
# symbols do not exist, olddefconfig silently discards CONFIG_RTLWIFI6 /
# CONFIG_RTL8192CD from our defconfig, and the build succeeds with no Wi-Fi at
# all -- vmlinux 33MB instead of 136MB and not one driver symbol in it.
# The comment that used to sit here said the SDK had no Wi-Fi drivers; that was
# true at 73627f6 and stopped being true at 2e1f9b3.
cp -a "$SDK/vendor/realtek-wireless/."                           "$K/drivers/net/wireless/realtek/"
cp -a "$SDK/vendor/platform/arch/mips/rtl9607c"                  "$K/arch/mips/"
mkdir -p "$K/arch/mips/boot/dts/realtek"
cp -a "$SDK/vendor/platform/arch/mips/boot/dts/realtek/."        "$K/arch/mips/boot/dts/realtek/"
cp -a "$SDK/vendor/platform/arch/mips/include/asm/mach-rtl960xc" "$K/arch/mips/include/asm/"
cp -a "$SDK/vendor/platform/drivers/clk/realtek"                 "$K/drivers/clk/"
cp -a "$SDK/vendor/platform/drivers/gpio/gpio-rtk-soc.c"         "$K/drivers/gpio/"
cp -a "$SDK/vendor/platform/drivers/watchdog/rtl819x_wdt.c"      "$K/drivers/watchdog/"
cp -a "$SDK/vendor/include/net/rtl"                              "$K/include/net/"
cp -a "$SDK/vendor/include/soc/cortina"                          "$K/include/soc/"
cp -a "$SDK/vendor/include/dt-bindings/soc/9607xc_irqs.h"        "$K/include/dt-bindings/soc/"

# --- 4. apply the 7.1 port: overlay the ported versions of changed files ---
# (overlay/ holds the exact ported sources — robust against the CRLF/fuzz that a
#  unified-diff patch trips on; docs/port-vs-upstream-*.diff is the human changelog)
cp -a "$BSP/overlay/." "$K/"

# --- 5. rootfs (BEFORE the kernel: the initramfs is baked in during the kernel build) ---
"$SDK/rootfs/build-rootfs.sh" "$WORK/rootfs-tree"

# --- 5b. the userspace the SDK's rootfs builder does not install -----------
# hostapd, iptables, dropbear, the iw* tools and rootfs/usr/ are all referenced
# by the shipped s6 services but never built or copied by build-rootfs.sh, so
# without this the image boots both radios and cannot use them. Set SKIP_USERSPACE=1
# for a kernel-only build; VENDOR_SDK points at the unpacked Realtek GPL drop.
if [ -z "$SKIP_USERSPACE" ]; then
	"$BSP/tools/build-userspace.sh" "$WORK/rootfs-tree"
fi

# --- 6. configure + build ---
cp "$BSP/configs/rtl9607c.config" "$K/.config"
# Point the built-in initramfs at the rootfs we just built. This overrides any
# CONFIG_INITRAMFS_SOURCE value in the committed config (which must NOT carry a
# machine-specific absolute path — that was a portability bug).
sed -i "s|^CONFIG_INITRAMFS_SOURCE=.*|CONFIG_INITRAMFS_SOURCE=\"$WORK/rootfs-tree $SDK/rootfs/initramfs-devnodes.txt\"|" "$K/.config"
# host bc is required by the kernel build; the SDK ships a fallback if the host lacks it
command -v bc >/dev/null 2>&1 || export PATH="$SDK/tools/hostbin:$PATH"
make -C "$K" olddefconfig
make -C "$K" -j"$JOBS" uImage.lzma

# --- 7. package ---
# (initramfs is baked in via CONFIG_INITRAMFS_SOURCE; for a separate squashfs+vm.img
#  use the SDK image tools — see README)
mkdir -p "$BSP/images"
cp "$K/arch/mips/boot/uImage.lzma" "$BSP/images/uImage"
# The rootfs is baked into the kernel, so uImage IS the initramfs image. The
# TFTP/boot scripts in use fetch it as `uImage-initramfs`, so publish that name
# too. A SYMLINK, deliberately, not a copy: a stale second copy that some step
# forgets to refresh has already cost real flash cycles, and a link cannot go
# stale relative to what it points at.
ln -sfn uImage "$BSP/images/uImage-initramfs"
echo
echo "Build complete: $BSP/images/uImage  (load 0x80001000)"
echo "               (also linked as images/uImage-initramfs for TFTP boot scripts)"
echo "RAM-boot test on the board (no flash writes):"
echo "  U-Boot> setenv bootargs console=ttyS0,115200 loglevel=8"
echo "  U-Boot> loady 0x83000000    (send images/uImage via ymodem)"
echo "  U-Boot> bootm 0x83000000"
