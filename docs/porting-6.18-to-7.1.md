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

### Wi-Fi: `cfg80211_ops` moved to `wireless_dev *`

7.1 changed the second argument of a family of `cfg80211_ops` callbacks from
`struct net_device *` to `struct wireless_dev *`. Ten changed; both vendor
drivers assign nine of them:

```
add_key  get_key  del_key  set_default_mgmt_key
add_station  del_station  change_station  get_station  dump_station
```

(the tenth, `set_default_beacon_key`, is unused here).

**`set_default_key` did NOT change** — it still takes `struct net_device *`,
sitting immediately between four that did. Enumerate the real signatures out of
`include/net/cfg80211.h` rather than pattern-matching on the compiler errors:

```sh
diff <(sed -n '/^struct cfg80211_ops {/,/^};/p' old/include/net/cfg80211.h) \
     <(sed -n '/^struct cfg80211_ops {/,/^};/p' new/include/net/cfg80211.h)
```

Both drivers already had a shim layer from BSP-6's 5.10 → 6.18 round (`ph_*` in
g6, `rtk_shim_*` in rtl8192cd) that absorbs the `link_id`/`radio_idx` additions,
so the 7.1 change extends that layer: the shim takes the `wireless_dev *` and
passes `wdev->netdev` to the untouched vendor handler.

> Do **not** reach for `-Wno-error=incompatible-pointer-types` here. BSP-6's
> notes are explicit that doing so hid a `cfg80211_ops` mismatch which then
> panicked the board at runtime. A wrong entry in this table is a crash, not a
> warning.

The `cfg80211_new_sta()` / `cfg80211_del_sta()` *call* sites moved the same way;
they take the `wireless_dev *`, i.e. `dev->ieee80211_ptr` (7 sites).

### Wi-Fi: PPPoE uapi structs lost their flexible array members

```
8192cd_br_ext.c: error: 'struct pppoe_hdr' has no member named 'tag'
rtw_br_ext.c:    error: 'struct pppoe_tag' has no member named 'tag_data'
```

7.1 hid both behind `#ifndef __KERNEL__` in `include/uapi/linux/if_pppox.h`:

```diff
 struct pppoe_tag {
 	__be16 tag_type;
 	__be16 tag_len;
+#ifndef __KERNEL__
 	char tag_data[];
+#endif
 } __attribute__ ((packed));
```

The intent is that in-kernel code computes the offsets itself. Both Realtek
bridge-extension files parse PPPoE tags (20 sites between them), and both
include `<linux/if_pppox.h>` — which this port already carries in `overlay/` —
so the accessors live there once:

```c
#define pppoe_hdr_tags(ph)	((unsigned char *)((ph) + 1))
#define pppoe_tag_data(t)	((unsigned char *)((t) + 1))
```

Both structs are `__packed`, so `sizeof()` is exactly the on-wire header length
and `(x + 1)` lands on the payload.

### PCIe: `<linux/of_gpio.h>` is gone

`arch/mips/rtl9607c/pci.c` compiles for the first time in this BSP once
`CONFIG_PCI=y` (it was off while Wi-Fi was out of scope). 7.1 removed
`<linux/of_gpio.h>` and `of_get_named_gpio()` along with the rest of the legacy
OF GPIO lookup. The *number*-based API in `<linux/gpio.h>` survives
(`gpio_request_one`, `gpio_to_desc`, `GPIOF_OUT_INIT_LOW`), so only the DT
lookup needed rewriting.

The board DT names the pin with a bare property:

```dts
pci0_gpio_rst = <&gpio1 8 GPIO_ACTIVE_HIGH>;
```

not the `<con-id>-gpios` form the `fwnode_gpiod_get_index()` helpers require, so
the phandle is resolved by hand — `of_parse_phandle_with_args()` →
`gpio_device_find_by_fwnode()` → `gpio_device_get_desc()` → `desc_to_gpio()` —
and handed back to the existing legacy calls.

### Watched for, did not bite

- **`xt_register_table()` gained a `template_ops` argument** in 7.1, and
  `xt_match`/`xt_target` gained a `check_hooks` callback. The vendor tree has no
  callers — only upstream `ip_tables.c`/`ip6_tables.c`/`arp_tables.c` call it —
  so the merged `include/linux/netfilter/x_tables.h` picked up both upstream
  changes with the vendor's additions intact and nothing downstream broke.
- **`netif_is_rxfh_configured()` going out-of-line** — see conflict 4 above.
- `nf_register_net_hook(s)`, `nf_conntrack_helper_register()`: unchanged, and the
  vendor tree uses them heavily.
