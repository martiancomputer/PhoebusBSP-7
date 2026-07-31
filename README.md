# PhoebusBSP-7

Linux **7.1 mainline** board support for Realtek Phoebus (RTL9607C / RTL9607Cv2 —
big-endian MIPS32r2 interAptiv, MIPS_CPS + MT SMP / 4 VPEs, GIC).

Built on the shared [Phoebus-SDK](https://github.com/martiancomputer/Phoebus-SDK)
(vendor SoC code + toolchain fetch + image tools + rootfs), wired in as the
`sdk/` submodule. The LTS line lives in
[PhoebusBSP-6](https://github.com/martiancomputer/PhoebusBSP-6).

This is the bleeding-edge tree: it tracks the current mainline stable series
rather than an LTS one, so it moves when upstream moves. `KVER=7.1.6 ./build.sh`
rebases onto another point release without touching the port.

## Scope

Full board support: SoC platform (SMP, GIC, timers, console, GPIO, watchdog),
the Realtek switch core, GPON/EPON, the built-in NIC, FleetConntrack, PCIe, and
**both Wi-Fi radios** — `g6_wifi_driver` (5 GHz RTL8832BR) and `rtl8192cd`
(2.4 GHz RTL8192F).

> **The image has radios but no Wi-Fi userspace.** The SDK's
> `rootfs/build-rootfs.sh` does not install `hostapd`, `dropbear` or the `iw*`
> tools, and never copies `rootfs/usr/`, even though the matching s6 services are
> in the boot bundle. So a clean build boots both radios with their RF
> calibration tables but cannot bring up an AP, accept SSH, or get a WAN address.
> This affects PhoebusBSP-6 identically; see PORT_NOTES.md.

## What's here (the *port*, not the kernel)

```
build.sh        one-shot: fetch kernel + toolchain, graft vendor, overlay port, build, package
configs/
  rtl9607c.config   working kernel .config (RTK_SOC_RTL9607C=y, CPS+MT SMP, GIC, 8250,
                    squashfs, initramfs, netfilter legacy backend for NAT)
overlay/        exact ported sources for the files we changed vs upstream 7.1.5
                (arch/mips platform + Kconfig/Makefile wiring, the rtl86900 SDK 7.1
                 API edits, the Wi-Fi cfg80211/PPPoE port, header deltas,
                 scripts/Makefile.lib). build.sh copies this over the
                 pristine+vendor tree — reliable against the SDK's CRLF files.
docs/
  port-vs-upstream-7.1.5.diff   changelog of every edit vs pristine 7.1.5
  porting-6.18-to-7.1.md        what actually broke moving off the LTS, and why
tools/
  rebase-overlay.sh       rebase overlay/ onto a newer kernel (classify + 3-way merge)
  fix-kconfig-choices.py  collapse duplicate choice values the 7.1 kconfig rejects
sdk/            Phoebus-SDK submodule
```

Nothing here duplicates upstream or the vendor SDK: `build.sh` reconstructs a full
buildable tree from three pinned ingredients (pristine kernel + `sdk/` vendor code +
`overlay/`).

## Build

Host prerequisites (build.sh preflights these):
- Arch/Artix: `sudo pacman -S base-devel bc uboot-tools xz`
- Debian/Ubuntu: `sudo apt install build-essential bc u-boot-tools xz-utils flex bison`

`mkimage` (uboot-tools) wraps the uImage; `bc` is needed by the kernel's
`timeconst.h` rule; the cross toolchain is fetched automatically.

> **`bc` really is required.** The kernel regenerates
> `include/generated/timeconst.h` on *every* build via a `filechk` rule with a
> `FORCE` prerequisite, so a cached header does not save you. Without `bc` the
> build dies early at `prepare0` with `Error 127`.

```sh
git clone --recurse-submodules https://github.com/martiancomputer/PhoebusBSP-7
cd PhoebusBSP-7
git submodule update --init          # pulls Phoebus-SDK
./build.sh                            # -> images/uImage  (load 0x80001000)
```

## Boot / test on hardware (no flash writes)

U-Boot is `Phoebus#`, NAND+UBI. RAM-boot the image over serial (no Ethernet needed):

```
U-Boot> setenv bootargs console=ttyS0,115200 loglevel=8 ethaddr=<your-mac>
U-Boot> loady 0x83000000        # send images/uImage via ymodem (picocom: sb)
U-Boot> bootm 0x83000000
```

Useful cmdline arguments beyond the above:

| Arg | Effect |
|---|---|
| `ethaddr=aa:bb:...` | real MAC, applied to the wired ports and bridge |
| `wan=<if>` | choose the WAN port (default `nas0`) |
| `lan=<if>[,<if>]` | choose bridge members (default `eth0.2`–`eth0.7`) |
| `phoebus_verbose` | keep the full kernel log on the console |

### Verify you are booting what you think you are

A ymodem cycle is ~15 minutes, so confirm before you spend one. U-Boot prints
`Data Size:` — check it against the file you sent:

```sh
md5sum images/uImage arch/mips/boot/uImage.lzma
```

Stale build artifacts are the single most expensive mistake here.

## Status

**Boots on real hardware.** RAM-booted over serial on an RTL9607C; reached an
s6-supervised shell with `uname -r` = 7.1.5.

Confirmed working on silicon:
- 4-CPU SMP (interAptiv MT, VPE {2,2}), 231/256 MB, L2 256 kB, GIC clocksource
- ttyS0 console (TX and RX), GPIO (3 banks + IRQs), Luna watchdog, gpio-keys
- Realtek switch/xPON core, switch link-change IRQ, interrupt broadcaster
- FleetConntrack manager across 4 CPUs; bridge, 802.1Q, PPPoE/PPTP/L2TP
- eth0 up, br0 forwarding, MAC provisioned from the `ethaddr=` cmdline
- s6: `s6-rc-compile` + `s6-svscan` + `s6-hpd`

That boot was the pre-Wi-Fi image. It turned up four defects, all since fixed —
two of them inherited from stale files in BSP-6's committed overlay rather than
caused by 7.1. See `PORT_NOTES.md`.

**Wi-Fi is now in**, and link-verified but **not yet booted**: both radios build
into one kernel (`rtw_`/`phl_`/`halbb`/`halrf` for the 5 GHz RTL8832BR,
`rtl8192cd` for the 2.4 GHz RTL8192F), both PCI ID tables present, zero
duplicate symbols, switch/GPON unregressed.

The non-Wi-Fi part of the 6.18 → 7.1 jump cost **one** source change in the
vendor tree (a lost transitive `#include`), four merge conflicts, and one
Kconfig-strictness fix. Wi-Fi added three more API changes — `cfg80211_ops`
moving to `wireless_dev *`, the PPPoE uapi flexible arrays being hidden from
kernel code, and `<linux/of_gpio.h>` being removed. All are written up in
[`docs/porting-6.18-to-7.1.md`](docs/porting-6.18-to-7.1.md).

The SDK bugs this BSP used to work around are all fixed upstream as of
`c2cf097`, so `build.sh` carries no workarounds. What remains is the rootfs
userspace gap described under Scope.

xPON/GPON/EPON retained and Kconfig-selectable.

## Relationship to PhoebusBSP-6

The two BSPs are the same port against two kernel lines, sharing one vendor SDK:

```
        Phoebus-SDK  (pristine vendor code, tools, rootfs — no kernel)
             |                          |
      PhoebusBSP-6                PhoebusBSP-7
   overlay vs 6.18.39          overlay vs 7.1.5
```

`docs/porting-6.18-to-7.1.md` is the diff between those two overlays — i.e. the
list of things upstream changed under us between the LTS and mainline.
