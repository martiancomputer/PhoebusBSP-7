# Realtek Semiconductor Corp.
#
# bsp/Makefile
#     bsp Makefile
#
# Copyright (C) 2006-2015 Tony Wu (tonywu@realtek.com)
#

core-$(CONFIG_RTK_SOC_RTL8198D)   += arch/mips/rtl8198d/
core-$(CONFIG_RTK_SOC_RTL9607C) += arch/mips/rtl9607c/
cflags-$(CONFIG_RTK_SOC_RTL8198D) += -I$(srctree)/arch/mips/rtl8198d/
cflags-$(CONFIG_RTK_SOC_RTL9607C) += -I$(srctree)/arch/mips/rtl9607c/
#load-y   = 0x80010000

obj-y := setup.o irq.o prom.o timer.o promlib.o dma.o
obj-y += init_l2cache.o
obj-$(CONFIG_HWMON) += phoebus_hwmon.o
obj-$(CONFIG_LEDS_PWM) += phoebus_led_boot.o
#obj-$(CONFIG_RTL8686_SWITCH) += bspgpio.o dying_gasp.o
obj-$(CONFIG_RTL9603CVD_SERIES) += 9603cvd_scache.o 9603cvd_scache_init.o 9603cvd_usb.o 9603cvd_pci.o
obj-$(CONFIG_OF)  += of.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_USB) += usb.o
obj-$(CONFIG_SMP) += smp.o
obj-$(CONFIG_EARLY_PRINTK) += early-printk.o
obj-$(CONFIG_MTD_SPI)	   += flash.o
obj-$(CONFIG_CPU_HAS_WB) += wb_flush.o
obj-$(CONFIG_SOC_TIMER) += cevt-ext.o
obj-$(CONFIG_LUNA_CMU) += rtk-cmu.o

#obj-$(CONFIG_LUNA_WDT) += luna_wdt.o
obj-$(CONFIG_LUNA_WDT_KTHREAD) += luna_watchdog.o
obj-$(CONFIG_RTL819X_WDT) += rtl8198d_wdt.o
##
## uncomment following lines for customized linker script
##
#CPPFLAGS_vmlinux.lds := -DTEXT_OFFSET=$(TEXT_OFFSET)
#export KBUILD_LDS := arch/arm64/bsp/vmlinux.lds
#extra-y += vmlinux.lds
