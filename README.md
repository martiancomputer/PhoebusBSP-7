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

This BSP builds a **Wi-Fi-less** kernel: SoC platform (SMP, GIC, timers, console,
GPIO, watchdog), the Realtek switch core, GPON/EPON, the built-in NIC and
FleetConntrack. The vendor Wi-Fi drivers (`g6_wifi_driver` for the 5 GHz
RTL8832BR, `rtl8192cd` for the 2.4 GHz RTL8192F) are **not** part of this port.

They are not in Phoebus-SDK — the shared SDK ships pristine vendor code, and the
Wi-Fi trees were never committed to it, so neither BSP can graft them from a
clean clone today. Adding them to the SDK is the prerequisite for a Wi-Fi-capable
BSP-7; until then this tree deliberately does not reference them, and
`CONFIG_WLAN_VENDOR_REALTEK` is off. `CONFIG_PCI` is off with it, matching
BSP-6's committed config.

The port work that *would* be needed is bounded and known: BSP-6's 6.18 Wi-Fi
port touches ~37 files in `g6_wifi_driver` and ~31 in `rtl8192cd`, and the one
7.1-specific blocker is already solved here — see the Kconfig section of
[`docs/porting-6.18-to-7.1.md`](docs/porting-6.18-to-7.1.md), which covers both
Wi-Fi Kconfigs.

## What's here (the *port*, not the kernel)

```
build.sh        one-shot: fetch kernel + toolchain, graft vendor, overlay port, build, package
configs/
  rtl9607c.config   working kernel .config (RTK_SOC_RTL9607C=y, CPS+MT SMP, GIC, 8250,
                    squashfs, initramfs, netfilter legacy backend for NAT)
overlay/        exact ported sources for the files we changed vs upstream 7.1.5
                (arch/mips platform + Kconfig/Makefile wiring, the rtl86900 SDK 7.1
                 API edits, header deltas, scripts/Makefile.lib). build.sh copies
                this over the pristine+vendor tree — reliable against the SDK's
                CRLF files.
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

The entire 6.18 → 7.1 jump cost **one** source change in the vendor tree (a lost
transitive `#include`), four merge conflicts, and one Kconfig-strictness fix.
See [`docs/porting-6.18-to-7.1.md`](docs/porting-6.18-to-7.1.md).

The first boot turned up four defects, all now fixed (see `PORT_NOTES.md`); the
fixed image is build-verified but **not yet re-booted on hardware**. Two of them
were inherited: BSP-6's committed `overlay/` is missing two fixes its own notes
describe as hardware-confirmed (the `eth_hw_addr_set` conversions and the
disabled `plat_serial_init` initcall). An audit of all 94 of BSP-6's overlay
files against its live tree found exactly those two stale.

> **Known bugs in the shared SDK**, all worked around in `build.sh` rather than
> patched in `sdk/`, because PhoebusBSP-6 consumes that same skeleton:
> - `rootfs/build-rootfs.sh:72` chmods `etc/s6/rc.boot`, which no longer exists,
>   so the script exits 1 and `set -e` would kill the build. Worked around by
>   verifying the tree and continuing (step 5).
> - `etc/s6/scripts/network-up` ends with `[ test ] && echo` inside a loop, so it
>   exits 1 whenever the last interface has no carrier — which fails the whole
>   s6-rc bundle. Worked around by appending `exit 0` (step 5b).
> - `/init` hardcodes `Linux 6.18.39` in its banner. Rewritten to `$(uname -r)`
>   (step 5b).

xPON/GPON/EPON retained and Kconfig-selectable. Wi-Fi and PCI are off — see Scope.

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
