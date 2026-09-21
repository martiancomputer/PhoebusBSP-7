# PhoebusBSP-7

Mainline/bleeding-edge Linux board support for Realtek **RTL9607C / RTL9607Cv2**
(big-endian MIPS32r2 interAptiv, MIPS CPS + MT SMP / 4 VPEs, GIC).

The current default kernel is **Linux 7.3-rc3**. This repository is the forward-port
line for Phoebus-OS; hardware debugging is done first on the
[6.18 LTS BSP](https://github.com/martiancomputer/PhoebusBSP-6), then carried
forward here.

Shared vendor code, userspace, provisioning and tooling live in
[Phoebus-SDK](https://github.com/martiancomputer/Phoebus-SDK), pinned as the
`sdk/` submodule.

## Verification boundary

Linux **7.3-rc3 has been booted and exercised on the RTL9607C hardware**.

The current mainline image completed a roughly **5-hour continuous hardware run**
with the kernel and router userspace active. The run ended when the Linux
conntrack table filled because the port was using the wrong cleanup ABI, so
expired entries were not being reclaimed correctly.

A cleanup-ABI correction exists locally and will be pushed separately. It is not
part of this documentation commit and still needs a fresh hardware retest.

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
KVER=7.3-rc3
```

Release candidates are cloned from Linus's kernel.org Git tree; stable releases
use kernel.org tarballs. A different mainline version can be selected with
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

1. `sdk/rootfs/build-rootfs.sh` builds the shared BusyBox/s6 rootfs and tracked
   SDK files.
2. `tools/build-userspace.sh` builds the network-facing third-party programs
   that BSP-7 intentionally owns.

The current BSP userspace builder uses current pinned releases including
hostapd 2.12, dnsmasq 2.93 and Dropbear 2026.94, and verifies runtime
`DT_NEEDED` resolution rather than checking filenames alone.

`SKIP_USERSPACE=1 ./build.sh` is for kernel-only iteration. It is not a
deployable router image: services such as hostapd, dnsmasq, Dropbear and
iptables will be absent.

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
