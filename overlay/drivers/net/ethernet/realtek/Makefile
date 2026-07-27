# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Realtek network device drivers.
#

obj-$(CONFIG_8139CP) += 8139cp.o
obj-$(CONFIG_8139TOO) += 8139too.o
r8169-y += r8169_main.o r8169_firmware.o r8169_phy_config.o
r8169-$(CONFIG_R8169_LEDS) += r8169_leds.o
obj-$(CONFIG_R8169) += r8169.o
obj-$(CONFIG_RTASE) += rtase/

# In-tree fallbacks for variables the vendor SDK top-level Makefile exports
DIR_LINUX ?= $(srctree)
NETDIR ?= $(srctree)/drivers/net/ethernet/realtek
APOLLODIR ?= $(srctree)/drivers/net/ethernet/realtek/rtl86900

#obj-$(CONFIG_RTL9607C_SERIES) += ethctl_implement.o

ifeq ($(CONFIG_MODULE_BUILD_IN),y)
obj-$(CONFIG_RTL8686_SWITCH) += rtl86900/
else
obj-m += rtl86900/
endif

obj-$(CONFIG_RTK_DEV_AP) += rtk_vlan_common.o
obj-$(CONFIG_RTK_DEV_AP) += rtk_common_utility.o
obj-$(CONFIG_BOLOCK_IPTV_FROM_LAN_SERVER) += rtk_common_utility.o
obj-$(CONFIG_RTK_DNS_TRAP) += rtk_dns_trap.o
obj-$(CONFIG_RTK_DNS_TRAP) += rtk_common_utility.o
obj-$(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE) += rtk_fc_system.o
obj-$(CONFIG_RTW_MEMPOOL) += rtk_mempool.o

# Add symbolic link to the NIC driver .h file
ifeq ($(CONFIG_RTL9600_SERIES),y)
${shell ln -fs $(NETDIR)/rtl86900/nicDriver/re8686.h $(NETDIR)/re8686_nic.h}
endif
ifeq ($(CONFIG_RTL9601B_SERIES),y)
${shell ln -fs $(NETDIR)/rtl86900/nicDriver/re8686_rtl9601b.h $(NETDIR)/re8686_nic.h}
endif
ifeq ($(CONFIG_RTL9602C_SERIES),y)
${shell ln -fs $(NETDIR)/rtl86900/nicDriver/re8686_rtl9602c.h $(NETDIR)/re8686_nic.h}
endif
ifeq ($(CONFIG_RTL9607C_SERIES),y)
${shell ln -fs $(NETDIR)/rtl86900/nicDriver/re8686_rtl9607c.h $(NETDIR)/re8686_nic.h}
endif
ifeq ($(CONFIG_RG_G3_SERIES),y)
${shell ln -fs $(NETDIR)/rtl86900/nicDriver/re8686_rtl9607c.h $(NETDIR)/re8686_rtl9607c.h}
endif

ifeq ($(CONFIG_RTK_DEV_AP),y)
obj-$(CONFIG_RTK_LINUX_VLAN_SUPPORT) += rtk_linux_vlan.o
endif
obj-$(CONFIG_RTK_VLAN_PASSTHROUGH_SUPPORT) += rtk_vlan_passthrough.o
obj-$(CONFIG_RTK_VLAN_BINDING_SUPPORT) += rtk_lan_vlan.o
obj-$(CONFIG_RTK_CTCAPD_FILTER_SUPPORT) += rtk_dns_filter.o

ifeq ($(CONFIG_RTL_MULTI_ETH_WAN),y)
obj-$(CONFIG_RTK_DEV_AP) += rtk_multi_wan_vlan.o
obj-$(CONFIG_RTL_SMUX_LEGACY) += smux.o
endif

ifeq ($(CONFIG_RTL8686_SWITCH),y)
EXTRA_CFLAGS += -I$(DIR_LINUX)/drivers/net/ethernet/realtek/rtl86900/sdk/include/
EXTRA_CFLAGS += -I$(DIR_LINUX)/drivers/net/ethernet/realtek/rtl86900/sdk/include/common/
EXTRA_CFLAGS += -I$(DIR_LINUX)/drivers/net/ethernet/realtek/rtl86900/sdk/system/include/
EXTRA_CFLAGS += -I$(DIR_LINUX)/drivers/net/ethernet/realtek/rtl86900/nicDriver/
endif
#ifeq ($(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE),y)
ifeq ($(CONFIG_COMMON_RT_API),y)
EXTRA_CFLAGS += -I$(DIR_LINUX)/drivers/net/ethernet/realtek/rtl86900/sdk/include/rtk/rt/
EXTRA_CFLAGS += -I$(DIR_LINUX)/drivers/net/ethernet/realtek/rtl86900/FleetConntrackDriver/rt/include/
EXTRA_CFLAGS += -I$(DIR_LINUX)/drivers/net/ethernet/realtek/rtl86900/igmpHookModule/rt_igmpHook/include/
endif

ifeq ($(CONFIG_MULTICHIP_ONEIMAGE),y)
ifeq ($(CONFIG_ARCH_CORTINA_VENUS), y)
CA_TARGET = venus
else ifeq ($(CONFIG_RTL8277C_SERIES), y)
CA_TARGET = 77c
else
CA_TARGET = 07f
endif # CONFIG_ARCH_CORTINA_VENUS
endif # CONFIG_MULTICHIP_ONEIMAGE

ifeq ($(CONFIG_LUNA_G3_SERIES),y)
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-plat/1.0-r0/ca-plat-1.0/inc/
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-plat/1.0-r0/ca-plat-1.0/osal/osal/linux_kernel/
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-plat/1.0-r0/ca-plat-1.0/osal/include/
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-plat/1.0-r0/ca-plat-1.0/peri/
ifeq ($(CONFIG_MULTICHIP_ONEIMAGE),y)
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0-$(CA_TARGET)/cortina-api/include/
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0-$(CA_TARGET)/include/generated/
else
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0/cortina-api/include/
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0/include/generated/
endif
ifeq ($(CONFIG_RTL8198F_SERIES),y)
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0/ni-drv/
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0/aal/include/
endif
ifeq ($(CONFIG_CA8279_SERIES),y)
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0/ni-drv-gen1/
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0/aal-gen1/include/
endif
ifeq ($(CONFIG_CA8277B_SERIES),y)
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0/ni-drv-gen2/
EXTRA_CFLAGS += -I$(DIR_LINUX)/../ca_packages/ca-network-engine/1.0-r0/ca-network-engine-1.0/aal-gen2/include/
endif
endif

-include $(NETDIR)/rtl86900/Compiler_Flag
EXTRA_CFLAGS += $(PON_CFLAGS)
#endif

ifeq ($(CONFIG_RTL_83XX_SUPPORT),y)
obj-m += rtl83xx/
endif

obj-$(CONFIG_RTL_8226B_SUPPORT) += rtl8226B/
obj-$(CONFIG_RTL_8221B_SUPPORT) += rtl8221B/
# only for 8261
# obj-$(CONFIG_RTL8261_SUPPORT) += rtl82xx/
# for 8261 + 8261n
obj-$(CONFIG_RTL8224_SUPPORT) += rtk-ms-phyOnly/
obj-$(CONFIG_RTL8261_SUPPORT) += rtk-ms-phyOnly/
obj-$(CONFIG_RTL8367_SUPPORT) += rtl8367/

RTK_EXT_GPHY_MODULE_NAME = extGphy
$(RTK_EXT_GPHY_MODULE_NAME)-objs := rtk_ext_gphy.o
obj-$(CONFIG_RTK_EXT_GPHY) += $(RTK_EXT_GPHY_MODULE_NAME).o

RTK_SDS_FIBER_MODULE_NAME = sdsFiber
$(RTK_SDS_FIBER_MODULE_NAME)-objs := rtk_sds_fiber.o
obj-$(CONFIG_RTK_SDS_FIBER) += $(RTK_SDS_FIBER_MODULE_NAME).o

subdir-ccflags-y += -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=missing-prototypes -Wno-error=enum-conversion -Wno-error=return-type -Wno-error=discarded-qualifiers -Wno-error=implicit-int -Wno-error=old-style-declaration -Wno-error=date-time -Wno-error=enum-int-mismatch -Wno-error=empty-body -Wno-error=implicit-fallthrough -Wno-error=address
