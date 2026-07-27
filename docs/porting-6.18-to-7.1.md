# Porting the Phoebus BSP from 6.18 LTS to 7.1 mainline

This is the delta between [PhoebusBSP-6](https://github.com/martiancomputer/PhoebusBSP-6)'s
overlay and this one — i.e. everything upstream changed under the port between
6.18.39 and 7.1.5. The original 5.10 → 6.18 port is documented in BSP-6; this
file only covers what moving off the LTS cost.

## Method

The port was rebased, not redone. Every file BSP-6 changed was classified against
pristine 6.18.39:

| Class | Count | Treatment |
|---|---|---|
| Vendor-only (file does not exist upstream) | 40 | copied verbatim — upstream cannot have changed it |
| Upstream file, byte-identical in 6.18.39 and 7.1.5 | 22 | BSP-6's ported version copied verbatim |
| Upstream file that moved in 7.1.5 | 32 | 3-way merge: base = pristine 6.18.39, ours = pristine 7.1.5, theirs = BSP-6's port |
| Upstream file deleted in 7.1.5 | 0 | — |

28 of the 32 three-way merges applied without conflict. The four that conflicted
are below. [`tools/rebase-overlay.sh`](../tools/rebase-overlay.sh) does this
classification mechanically, so the same procedure re-runs against 7.2 or later:

```sh
tools/rebase-overlay.sh build/linux-7.1.5 build/linux-7.2.1 ./overlay /tmp/overlay-7.2
```

## The four merge conflicts

### 1. `arch/mips/boot/dts/Makefile` — resolved in upstream's favour

7.1 made every `subdir-` entry in this file unconditional (`subdir-y += realtek`
instead of `subdir-$(CONFIG_MACH_REALTEK_RTL) += realtek`). BSP-6 had added a
second line, `subdir-$(CONFIG_RTK_MIPS_SOC) += realtek`, precisely to get the
directory descended into on a Luna SoC. That is now redundant, so the line is
dropped and upstream's file is used as-is.

### 2. `arch/mips/boot/dts/realtek/Makefile` — upstream converged on our fix

BSP-6 gated the two Otto (RTL838x/930x) dtbs on `CONFIG_MACH_REALTEK_RTL` so they
would not be built into a Luna image. 7.1 made exactly that change upstream. Kept
upstream's two lines and re-appended the Luna-specific part (the `RTK_SOC_*` dtb
entries and the `memory.dts` symlink rule).

### 3. `drivers/net/ethernet/realtek/Kconfig` — kept our removal

`NET_VENDOR_REALTEK` gates the whole vendor menu, including the rtl86900 switch,
GPON/EPON and the SoC's built-in NIC — none of which are PCI devices. BSP-6
removed its `depends on PCI || (PARPORT && X86)`; 7.1 narrowed the same line to
`depends on PCI`. The dependency stays removed, or the entire vendor menu
disappears on a PCI-less config.

### 4. `include/linux/netdevice.h` — dropped dead vendor code

7.1 moved `netif_is_rxfh_configured()` out of line. The only thing BSP-6 had in
that region was this, sitting at file scope directly after the function:

```c
#ifdef CONFIG_RTK_MIRROR
	extern void rtk_mirror_tx(struct sk_buff *skb);
	rtk_mirror_tx(skb);
#endif
```

Those are bare statements outside any function — they would not compile. They
never had to: no `config RTK_MIRROR` exists anywhere in the vendor tree, so the
block has always been preprocessed away. It looks like a port-mirroring hook that
was meant to land inside `netdev_start_xmit()` and was pasted at the wrong offset.
Dropped rather than carried forward. If port mirroring is ever wanted, it needs to
be written properly inside `netdev_start_xmit()`.

## Kconfig: duplicate choice values are now rejected

The first hard failure, before a single object compiled:

```
drivers/net/wireless/realtek/rtl8192cd/Kconfig:646: error: choice value must not have a prompt in another entry
```

39 symbols across the two Wi-Fi Kconfigs (23 in `rtl8192cd`, 16 in
`g6_wifi_driver`) hit this. The vendor idiom is to declare the same choice value
once per supported chip so each gets its own prompt string:

```kconfig
config SLOT_0_RFE_TYPE_3
depends on (SLOT_0_8814AE || SLOT_0_8194AE)
bool "Type 3: external PA/LNA (2G SE2623L, 5G SKY85405)"
select SLOT_0_EXT_PA
select SLOT_0_EXT_LNA

config SLOT_0_RFE_TYPE_3
depends on (SLOT_0_8192FE || SLOT_0_8192EE_8192FE)
bool "Type 3: internal PA/LNA 2-LAYER/6-LAYER"
```

6.18's kconfig accepted this; 7.1's does not.
[`tools/fix-kconfig-choices.py`](../tools/fix-kconfig-choices.py) collapses each
group into a single entry — first prompt wins, the `depends on` expressions are
OR-ed so the value stays offerable in exactly the same set of configurations.

**The `select`s must not be unioned.** As the example above shows, "RFE Type 3"
means *external* PA/LNA on an 8814AE and *internal* PA/LNA on our 8192FE — the
second entry deliberately selects nothing. Unioning the selects sets
`SLOT_0_EXT_PA`/`SLOT_0_EXT_LNA` on a board whose 2.4 GHz front end is internal,
which would drive an RF path that is not there. Each select is therefore
re-emitted guarded by its own entry's condition:

```kconfig
select SLOT_0_EXT_PA if ((SLOT_0_8814AE || SLOT_0_8194AE))
```

Verified afterwards: the generated `.config` selects `CONFIG_SLOT_0_RFE_TYPE_3=y`
with `SLOT_0_EXT_PA`/`SLOT_0_EXT_LNA` unset, byte-identical to the 6.18 tree's
RF selection.

This BSP ships Wi-Fi-less (see the README's Scope section), so neither Kconfig is
in `overlay/` — but the fix and the script are kept here because this is the one
7.1 blocker standing between BSP-6's Wi-Fi port and a Wi-Fi-capable BSP-7, and
rediscovering it is expensive.

## Kernel API drift

Strikingly little, given the version jump. The whole vendor tree — the rtl86900
switch SDK, GPON/EPON, the NIC, FleetConntrack, the MIPS platform code — needed
exactly one source change.

### `NF_IP_PRI_*` lost its transitive include

```
FleetConntrackDriver/src/rtk_fc_helper.c:331:35:
    error: 'NF_IP_PRI_CONNTRACK_CONFIRM' undeclared here (not in a function)
    error: 'NF_IP_PRI_FIRST' undeclared here; did you mean 'NF_BR_PRI_FIRST'?
    error: 'NF_IP_PRI_LAST' undeclared here; did you mean 'NF_BR_PRI_LAST'?
```

`rtk_fc_helper.c` uses `NF_IP_PRI_*` but never included
`<linux/netfilter_ipv4.h>`. Under 6.18 it got them anyway: the 6.18 dependency
file for that object lists `include/linux/netfilter/x_tables.h` pulling in
`linux/netfilter_ipv4.h`. That chain does not reach the file in 7.1.

The enum itself is alive and in the same header — 7.1 only reworded it, swapping
`INT_MIN`/`INT_MAX` for `__KERNEL_INT_MIN`/`__KERNEL_INT_MAX` out of the new
`<linux/typelimits.h>`, so that userspace no longer needs `<limits.h>`:

```diff
-	NF_IP_PRI_FIRST = INT_MIN,
+	NF_IP_PRI_FIRST = __KERNEL_INT_MIN,
```

Fix: include the defining header directly in `rtk_fc_helper.c`. Its sibling
`rtk_fc_assistant.c` always did; this file was relying on an accident.

### Watched for, did not bite

- **`xt_register_table()` gained a `template_ops` argument** in 7.1, and
  `xt_match`/`xt_target` gained a `check_hooks` callback. The vendor tree has no
  callers — only upstream `ip_tables.c`/`ip6_tables.c`/`arp_tables.c` call it —
  so the merged `include/linux/netfilter/x_tables.h` picked up both upstream
  changes with the vendor's additions intact and nothing downstream broke.
- **`netif_is_rxfh_configured()` going out-of-line** — see conflict 4 above.
- `nf_register_net_hook(s)`, `nf_conntrack_helper_register()`: unchanged, and the
  vendor tree uses them heavily.
