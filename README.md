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

**Build-verified. Not yet booted on hardware.**

- `vmlinux` links on **7.1.5**: 24 MB, ELF 32-bit MSB MIPS32r2, SMP PREEMPT,
  `rtl9607c_engboard` builtin DTB.
- The whole vendor stack is linked in — FleetConntrack (1572 syms), GPON (1995),
  `dal_rtl9607c` (892), EPON (543), switch, GPIO, GMAC.
- Rootfs builds through the SDK: static busybox 1.37.0 + s6 (95 binaries),
  684 initramfs entries.
- `images/uImage` packages: 8.5 MiB lzma, load `0x80001000`.
- `overlay/` is proven sufficient: pristine 7.1.5 + SDK vendor + overlay
  reproduces the built tree with **zero differing source files**.

The entire 6.18 → 7.1 jump cost **one** source change in the vendor tree (a lost
transitive `#include`), four merge conflicts, and one Kconfig-strictness fix.
See [`docs/porting-6.18-to-7.1.md`](docs/porting-6.18-to-7.1.md).

> **Known bug in the shared SDK.** `rootfs/build-rootfs.sh:72` chmods
> `etc/s6/rc.boot`, which no longer exists in the tracked skeleton, so the script
> exits 1 even though the rootfs tree is complete. Under `set -e` that would kill
> the build — in both BSPs. `build.sh` here works around it by verifying the tree
> (`init`, `busybox`, `getty-console/run`) and continuing with a warning, but the
> real fix is a one-line Phoebus-SDK commit. See PORT_NOTES.md.

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
