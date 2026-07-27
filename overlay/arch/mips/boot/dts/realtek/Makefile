# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_MACH_REALTEK_RTL)	+= cisco_sg220-26.dtb
dtb-$(CONFIG_MACH_REALTEK_RTL)	+= cameo-rtl9302c-2x-rtl8224-2xge.dtb

# Luna SoCs: exactly one board dtb may be enabled — with BUILTIN_DTB_ALL the
# generic kbuild wrapper links every dtb-y into .dtb.init.rodata, and the
# platform code boots from __dtb_start (the first one).
dtb-$(CONFIG_RTK_SOC_RTL8198D)	+= rtl8198d_engboard.dtb
dtb-$(CONFIG_RTK_SOC_RTL9607C)	+= rtl9607c_engboard.dtb

ifneq ($(CONFIG_RTK_MIPS_SOC),)
ifeq ($(CONFIG_LUNA_MEMORY_AUTO_DETECTION),y)
$(shell cd $(srctree)/arch/mips/boot/dts/realtek; \
	ln -f -s memory-16.dts memory/memory.dts; \
	ln -f -s memory/memory.dts memory.dts)
else
$(shell cd $(srctree)/arch/mips/boot/dts/realtek; \
	ln -f -s memory-$(CONFIG_SOC_MEM_SIZE).dts memory/memory.dts; \
	ln -f -s memory/memory.dts memory.dts)
endif
endif
