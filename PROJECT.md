# Phoebus-OS — current mainline on the RTL9607C

The **mainline** half of Phoebus-OS: the same board support package as
[PhoebusBSP-6](https://github.com/martiancomputer/PhoebusBSP-6), carried forward
from **6.18.39 LTS** through the original **7.1.5** port and now to
**7.3-rc3**, on a **TP-Link Archer AX10 v3 / AX1500** whose SoC (Realtek
RTL9607C) has never been supported by any mainline or OpenWrt tree.

**This is not OpenWrt.** No shared code, build system or package format.
OpenWrt's `realtek` target covers the RTL838x/839x/930x *switch* SoCs; the 9607C
is a GPON/router part and is absent from it.

**This is not a product.** It boots from RAM over TFTP, nothing has ever been
written to the router's flash, and it is reflashed several times an hour.

## Read BSP-6's PROJECT.md first

Hardware, the measured port map, NAND layout, the s6 service model, and the
hard-won driver findings are **documented once**, in
[`PhoebusBSP-6/PROJECT.md`](https://github.com/martiancomputer/PhoebusBSP-6/blob/main/PROJECT.md).
They are properties of the *board*, not of the kernel version, and they apply
here unchanged.

They are deliberately not restated here. Two copies of the same hardware notes
drift, and this project has already been bitten several times by exactly that —
a committed overlay that no longer matched the tree that built, a README status
line that stayed true for a week. This file covers only what is genuinely
different about the moving mainline line.

---

## 1. Why two kernel lines at all

6.18 is an LTS: it is where the board is *made to work*, because a defect there
is a defect in the driver rather than in the kernel underneath it. BSP-7 tracks
mainline: it is where the port is *kept honest*, because upstream keeps changing
the APIs the vendor tree relies on, and finding that out continuously is cheaper
than discovering it after a multi-year gap.

The practical rule that has emerged:

> **Fix it on 6.18, then forward-port to the current BSP-7 kernel.**

Hardware debugging happens once, on the LTS line, against a kernel that is not
also moving. BSP-7 then takes the result. Every driver fix in this repo's history
arrived that way, and the forward-ports have been mechanical — usually a file
copy, occasionally a three-line merge — precisely *because* the debugging was
done somewhere the kernel was holding still.

The inverse direction has never been useful and should be resisted.

---

## 2. Architecture

```
Phoebus-SDK ──── pristine vendor code, rootfs, s6 tree, tooling. No kernel.
   │  submodule
   ├──────────────┬──────────────
   ▼              ▼
PhoebusBSP-6   PhoebusBSP-7        each: a defconfig + an overlay/ + a build.sh
overlay vs      overlay vs
6.18.39         current KVER (7.3-rc3)
```

Three repositories, one shared vendor snapshot:

| repo | contents |
|---|---|
| **Phoebus-SDK** (submodule at `sdk/`) | vendor drivers, rootfs skeleton, s6 service tree, `s6-hpd`, provisioning, tooling |
| **PhoebusBSP-6** | defconfig + `overlay/` against 6.18.39 |
| **PhoebusBSP-7** (this) | defconfig + `overlay/` against the current mainline target (7.3-rc3) |

### The overlay contract

`overlay/` mirrors paths under the selected upstream kernel. The current default is `linux-7.3-rc3/`. A build is:

```
pristine kernel.org tarball
  + graft  sdk/vendor/{realtek-net, realtek-wireless, platform, include}
  + overlay/
  = the tree that compiles
```

Nothing else. `build.sh` does `rm -rf` on the work tree every run, so **whatever
is not in the graft or the overlay does not exist**. That property is the whole
value of the layout, and it is also the thing most easily broken: a file left
behind by a previous manual build keeps working locally and vanishes on a clean
clone. Both BSPs have shipped that bug at least once (§6).

The corollary is that **`overlay/` must match the tree you actually tested**. A
clean-clone reconstruction that diffs to empty is the only real proof:

```sh
diff -rq --no-dereference \
  --exclude='*.o' --exclude='*.a' --exclude='.*.cmd' --exclude=generated \
  --exclude=config --exclude='*.dtb' --exclude='vmlinux*' --exclude='.config*' \
  --exclude='data_*.c' --exclude=autoconf.h --exclude=re8686_nic.h \
  "$REF" "$K" | grep -E '^(Files|Only in)'
```

Note `Only in` in that filter. `diff -rq` reports a one-sided file that way and
collapses an entire missing directory tree into a single such line, so filtering
to `^Files` prints *nothing* when a whole driver is absent — the loudest possible
failure producing the quietest possible output. That exact bug hid a missing
Wi-Fi graft here for several commits.

---

## 3. What 6.18 → 7.1 actually cost (historical baseline)

Written up in full in [`docs/porting-6.18-to-7.1.md`](docs/porting-6.18-to-7.1.md).
The summary, because the size of it is the interesting part:

**Non-Wi-Fi:** one source change (`NF_IP_PRI_*` lost a transitive include), four
merge conflicts, one Kconfig-strictness fix. Two of the four conflicts resolved
in *upstream's* favour — the tree had converged on fixes this port had already
made independently.

**Wi-Fi added three:**

| change | effect |
|---|---|
| `cfg80211_ops` key/station ops take `struct wireless_dev *` | shim layer passing `wdev->netdev`, both drivers |
| PPPoE uapi flexible arrays hidden from kernel code | `pppoe_hdr_tags()` / `pppoe_tag_data()` accessors |
| `<linux/of_gpio.h>` removed | `of_parse_phandle_with_args` → `gpio_device_find_by_fwnode` → `desc_to_gpio` |

**Kconfig got stricter.** Duplicate values inside a `choice` are now rejected.
The fix generates `select X if (<that entry's own depends>)` rather than unioning
the conditions — a union silently enables the wrong RF front end on boards where
only one branch should apply, which is the kind of bug that reaches the antenna
before it reaches a compiler.

That is genuinely all of it. **The vendor tree is far more portable than its
reputation suggests**; nearly all the pain in this project has been hardware
behaviour, not kernel API churn.

---

## 4. Build

```sh
./build.sh                     # kernel + rootfs + initramfs → images/uImage
SKIP_USERSPACE=1 ./build.sh    # NOT a shortcut — see below
```

Pipeline:

1. fetch toolchain (bootlin `mips32--glibc--stable-2025.08-1`, GCC 14.3)
2. obtain pristine `linux-$KVER` (7.3-rc3 by default; RCs from Linus's kernel.org Git tree, stable releases from kernel.org tarballs)
3. graft `sdk/vendor/*` — **including `realtek-wireless`**, which supplies the
   `drivers/net/wireless/realtek/{Kconfig,Makefile}` pair that sources the two
   vendor drivers. Without it the build silently produces a kernel with no radios
4. apply `overlay/`
5. `sdk/rootfs/build-rootfs.sh` → rootfs tree
6. `tools/build-userspace.sh` → BSP-7's network-facing userspace layer
7. configure, `olddefconfig`, build, package `uImage.lzma`

`SKIP_USERSPACE=1` skips step 6 and the rootfs is rebuilt from scratch every run,
so it produces an image with **no hostapd, dropbear, iptables or dnsmasq** — the
s6 bundle references all of them. It is for kernel-only iteration, not a fast path.

### `tools/build-userspace.sh` — BSP-7 only

BSP-6 has no equivalent; its userspace comes from binaries already sitting in the
working tree, which is why its clean-clone image is incomplete. This script
cross-builds libnl, OpenSSL, hostapd 2.12 (SAE), iptables, libxcrypt, Dropbear
2026.94, wireless_tools and dnsmasq 2.93, then runs two guards:

- **every binary the boot bundle execs must exist**
- **every `DT_NEEDED` library must resolve** — name-matching is not enough, a
  dangling symlink passes that and fails at exec

Hardware-specific source still comes from the vendor GPL drop. Security-facing
daemons are fetched from pinned upstream release archives. The vendor
`dnsmasq-2.85` remains unusable outside Realtek's build tree because it
unconditionally expects private `rtk/` headers, so BSP-7 uses upstream 2.93.

---

## 5. State

**Current target:** Linux **7.3-rc3**.

**Current mainline hardware verification:** the 7.3-rc3 image has been run on the
RTL9607C hardware continuously for roughly **5 hours** with the kernel and router
userspace active.

The run ended when the Linux conntrack table filled. The failure was traced to
the port calling the wrong conntrack cleanup ABI, so stale entries were not
being reclaimed correctly. The correction is now pushed in commit
`52b02eaa29a6` (`net: repair FleetConntrack table flush`). A fresh multi-hour
soak is still required to verify conntrack entry lifetime and reclamation after
the fix.

That 7.3 run supersedes the old 7.1.5 boot as the newest hardware-verification
boundary. The earlier 7.1.5 results remain useful subsystem history: 4-CPU SMP,
console, GPIO, watchdog, switch/xPON core, FleetConntrack, PCIe, both radios
probing with RF tables loaded, the full s6 stack, and 5 GHz SAE
authentication/association.

Build/link/symbol verification is still kept distinct from runtime validation.
The current mainline port has now passed sustained silicon runtime; the
conntrack cleanup defect is the known failure exposed by that runtime.

The current SDK submodule is the sanitized `e4c12263` history head used by both
BSPs at the time of the 7.3-rc3 rebase.

Typical RAM-boot arguments remain:

```
setenv bootargs 'console=ttyS0,115200 loglevel=8 phoebus_verbose ethaddr=<board-base-MAC> rfe2g=23 sds=0 wan=eth0.8 lan=eth0.2,eth0.3,eth0.4,eth0.5'
run fl
```

`sds=1` costs you the 2.4 GHz radio — SGMII1 shares a lane with PCIe port 1.

---

## 6. The historical 7.1 mainline defect: 5 GHz EAPOL

**Observed on the 7.1.5 hardware run. The 7.3-rc3 image has since completed a
multi-hour hardware run, but this specific probe has not yet been re-isolated
with the same instrumentation.**

SAE completes (commit + confirm, status 0, PMKID cached). Association completes.
Then hostapd times out after ~4.2 s and the driver deauths with reason 23,
because `key_installed` is false — `add_key(pairwise=1)` never arrives. The
4-way handshake never happens on the wire.

What makes it tractable is an A/B against the LTS line. The same `PHOEBUS-EAPOL-TX`
printk in `update_attrib_sec_info()`:

| | 6.18 | 7.1.5 |
|---|---|---|
| EAPOL frames reaching the driver TX path | **2** (1/4 and 3/4, 15 ms apart, handshake done in ~35 ms) | **0** |

Same client MAC, same hostapd, same config. Not a stale image — the string is in
`vmlinux`, the uImage md5 matched, and the only early return ahead of the printk
is gated on `ether_type != ETH_P_EAPOL`.

The driver is not the difference: `xmit_linux.c` and `os_intfs.c` are
**byte-identical** between the working 6.18 tree and this one, and `rtw_xmit.c`
differs only by instrumentation. hostapd's path is a plain `sendto()` on a
`PF_PACKET/SOCK_DGRAM` socket with `sll_ifindex = wlan0`, which should land in
`ndo_start_xmit`.

So the frame is lost either before `rtw_xmit_entry` or between it and
`update_attrib_sec_info`, and those need opposite fixes. A `PHOEBUS-XMIT-ENTRY`
probe now fires at the netdev entry, before any driver state is consulted:

- **absent** → the kernel never delivered it; look at `packet_snd`/qdisc
- **present, no `PHOEBUS-EAPOL-TX`** → the driver swallowed it in between

Not yet run. Note this diagnosis predates the 2.4 GHz/WAN/OOM ports and has not
been re-observed since.

> Distinct from BSP-6's open 5 GHz defect (downstream retry bursts and rate-control
> thrashing). Different layer, different line — do not conflate them.

---

## 7. Deliberate divergences from BSP-6

Tracked because an undocumented divergence looks like an oversight later.

| | BSP-7 | why |
|---|---|---|
| CAKE/SQM kernel stack | **absent** | still deferred while the mainline Wi-Fi TX/EAPOL path is unresolved; the current SDK has the service, but this defconfig leaves `CONFIG_NET_SCH_CAKE` off |
| `tc` | not built | BSP-7's userspace builder does not currently invoke the SDK's `net/build-tc.sh` |
| hostapd | 2.12, sha256-pinned | security-facing daemon is built from a current upstream release |
| dnsmasq | 2.93, sha256-pinned | the vendor 2.85 tree depends on private Realtek headers that are not shipped |
| Dropbear | 2026.94, sha256-pinned | security-facing daemon is built from a current upstream release |
| read-only SPI-NAND MTD | **present on 7.3** | observation/validation only; no program, erase, markbad or automatic UBI attach |
| `CONFIG_RUSTC_*` | left at 0 | host-detected values do not belong in a portable defconfig and regenerate through `olddefconfig` |

The SDK submodule is current at `e4c12263` for this rebase. Future catch-up
should be evaluated as an explicit dependency change rather than inferred from
branch names.

---

## 8. Verification discipline

BSP-6's PROJECT.md states the house rules. This section records what happened
here when they were not followed, because the abstract version is easy to nod at.

**Every failure below passed its build.**

- **A kernel with no Wi-Fi at all.** `build.sh` had no `realtek-wireless` graft,
  under a comment saying the SDK had no Wi-Fi drivers — true at one SDK commit,
  false since. Upstream's `realtek/Kconfig` does not source the vendor drivers,
  so `CONFIG_RTLWIFI6`/`CONFIG_RTL8192CD` were not symbols, `olddefconfig`
  silently discarded them, and the build exited 0 with a 33 MB `vmlinux` instead
  of 136 MB. Every working image before that came from a hand-managed tree; the
  repo could not reproduce them.
- **A guard that could not catch what it existed for.** The check that stops a
  service shipping without its binary matched run scripts against a hardcoded
  list of names. A new `dnsmasq` service was not on the list and sailed through.
  It now derives the binary from each `exec` line. *It is still incomplete*: it
  reads `run` but not oneshot `up` files, so the new `cake` service and its `tc`
  dependency would slip past. Same mistake, moved from binary names to file names.
- **"The SDK is current."** Checked with `git log OURS..origin/main`. The SDK's
  default branch is `master`; `origin/main` does not exist; `|| echo "(none)"`
  swallowed the error. It was three commits behind.
- **A dnsmasq that could not compile.** Chose the vendor drop's copy to avoid a
  network fetch — sound reasoning — and validated it by checking all eleven
  config options existed in its `option.c`. Never checked it builds. It does not.
- **A build "success" that was an `echo`.** `./build.sh > log 2>&1; echo "EXIT=$?"`
  makes the shell's status the *echo's*. The task notification reported exit 0
  for a build that died with exit 2.

The pattern is one thing: **a check that was structurally incapable of reporting
the problem.** Hence:

- assert on the artefact — `nm vmlinux | grep -c rtw_`, image size, the generated
  `.config`, whether the `.o` was built at all — never on a return code
- pair every negative result with a control that must produce output
- when a query returns nothing, first ask whether it *could* have returned something

---

## 9. Open threads

1. **Retest the pushed conntrack cleanup-ABI correction** (`52b02eaa29a6`) —
   the previous 7.3-rc3 soak reached roughly five hours before the table
   exhausted.
2. **Repeat the 7.3 soak after the fix** and confirm conntrack entry lifetime,
   reclamation and long-duration routing behaviour.
3. **5 GHz EAPOL probe** (§6) — determine whether the historical 7.1.5 failure
   still exists on the current mainline tree before changing qdisc or driver
   policy around it.
4. **CAKE/tc integration** — the current SDK carries the CAKE service, but BSP-7
   deliberately lacks `sch_cake` and a built `tc`.
5. **The bundle guard** (§8) — it still derives longrun executables from `run`
   files and checks only named oneshot scripts; it should cover every oneshot
   `up` file and the full `etc/s6/scripts/*-up` set.
6. **Read-only NAND validation** — 7.3 now has an RTL9607C SPI-NAND MTD reader;
   ECC, physical offsets and bad-block behaviour must be independently verified
   before any write/install path exists.
7. **Inherited board limitation** — FleetConntrack hardware offload still cannot
   correctly represent the PCIe Wi-Fi path (`DEV_STACK_MAX[4]` overflow), so
   routed forwarding remains software-only for the working topology.

The 6.18 BSP remains the hardware reference; mainline changes should continue to
be proven there first when the defect is board/driver behaviour rather than an
upstream API change.
