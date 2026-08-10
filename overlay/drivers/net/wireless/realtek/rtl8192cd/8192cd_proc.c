/*
 *  Handle routines for proc file system
 *
 *  Copyright (c) 2017 Realtek Semiconductor Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#if !defined(__OSK__)
#define _8192CD_PROC_C_

#ifdef __KERNEL__
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/time.h>
#endif

#include "./8192cd_cfg.h"
#include "./8192cd.h"
#ifdef __KERNEL__
#include "./ieee802_mib.h"
#elif defined(__ECOS)
#include <cyg/io/eth/rltk/819x/wlan/ieee802_mib.h>
#include <cyg/io/eth/rltk/819x/wrapper/wrapper.h>
#endif
#ifdef RTK_NL80211
#include "./8192cd_cfg80211.h"
#endif
#include "./8192cd_headers.h"
#if defined(CONFIG_WLAN_HAL)
#include "./WlanHAL/HalMac88XX/halmac_reg2.h"
#endif

#if defined(_INCLUDE_PROC_FS_) || defined(__ECOS)
#ifdef __KERNEL__
#include <asm/uaccess.h>
#endif

#ifdef CTC_WIFI_DIAG
struct rtk_timer_list ctcwifi_diag_timer;
struct ctcwifi_diag_log_record *diag_log_record[2] = {NULL, NULL};
struct ctcwifi_diag_log_record *assoc_error_record[2] = {NULL, NULL};
unsigned int diag_log_head[2] = {0, 0}, diag_log_tail[2] = {0, 0};
unsigned int assoc_error_head[2] = {0, 0}, assoc_error_tail[2] = {0, 0};

enum {
	CTCWIFI_TYPE_DIAG_LOG = 0,
	CTCWIFI_TYPE_ASSOC_ERR,
	CTCWIFI_TYPE_FRAME_BODY
};

enum {
	CTCWIFI_2G = 0,
	CTCWIFI_5G
};

struct rtl8192cd_priv *ctcwifi_priv[2] = {NULL};
#endif

#ifdef CONFIG_ARCH_LUNA_SLAVE
#define SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(_p_)	(_p_)->size = 0x1000
#else
#define SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(_p_)
#endif

#ifdef CONFIG_RTL_PROC_NEW
#ifndef __LINUX_3_9__
#ifdef __LINUX_2_6_20__
static inline struct inode *file_inode(struct file *f)
{
	return f->f_path.dentry->d_inode;
}
#else  // !(__LINUX_2_6_20__)
static inline struct inode *file_inode(struct file *f)
{
	return f->f_dentry->d_inode;
}
#endif // __LINUX_2_6_20__
#endif // !(__LINUX_3_9__)

#ifndef __LINUX_3_10__
void *pde_data(const struct inode *inode)
{
	return PDE(inode)->data;
}
#endif // !(__LINUX_3_10__)

#define DECLARE_PROC_OPEN_CALLBACK(proc_name) \
	static int proc_name##_open(struct inode *inode, struct file *file) \
	{ \
		return(single_open(file, proc_name, pde_data(file_inode(file)))); \
	} \

#define DECLARE_PROC_WRITE_CALLBACK(proc_name, write_func) \
	static ssize_t proc_name##_write(struct file * file, const char __user * userbuf, \
		     size_t count, loff_t * off) \
	{ \
		return write_func(file, userbuf, count, pde_data(file_inode(file))); \
	} \

// Replace file_operations with proc_ops for /proc subsystem operations
#ifdef __LINUX_5_6__
#define RTK_DECLARE_READ_PROC_OPS(read_proc) \
	DECLARE_PROC_OPEN_CALLBACK(read_proc) \
	static struct proc_ops read_proc##_ops = { \
		.proc_open	= read_proc##_open, \
		.proc_read	= seq_read, \
		.proc_lseek	= seq_lseek, \
		.proc_release	= single_release, \
	}

#define RTK_DECLARE_WRITE_PROC_OPS(write_proc) \
	DECLARE_PROC_WRITE_CALLBACK(write_proc, write_proc) \
	static struct proc_ops write_proc##_ops = { \
		.proc_write	= write_proc##_write, \
	}

#define RTK_DECLARE_READ_WRITE_PROC_OPS(read_proc, write_proc) \
	DECLARE_PROC_OPEN_CALLBACK(read_proc) \
	DECLARE_PROC_WRITE_CALLBACK(read_proc, write_proc) \
	static struct proc_ops read_proc##_ops = { \
		.proc_open	= read_proc##_open, \
		.proc_read	= seq_read, \
		.proc_write	= read_proc##_write, \
		.proc_lseek	= seq_lseek, \
		.proc_release	= single_release, \
	}

#else  // !(__LINUX_5_6__)
#define RTK_DECLARE_READ_PROC_OPS(read_proc) \
	DECLARE_PROC_OPEN_CALLBACK(read_proc) \
	static struct file_operations read_proc##_ops = { \
		.open		= read_proc##_open, \
		.read		= seq_read, \
		.llseek 	= seq_lseek, \
		.release	= single_release, \
	}

#define RTK_DECLARE_WRITE_PROC_OPS(write_proc) \
	DECLARE_PROC_WRITE_CALLBACK(write_proc, write_proc) \
	static struct file_operations write_proc##_ops = { \
		.write		= write_proc##_write, \
	}

#define RTK_DECLARE_READ_WRITE_PROC_OPS(read_proc, write_proc) \
	DECLARE_PROC_OPEN_CALLBACK(read_proc) \
	DECLARE_PROC_WRITE_CALLBACK(read_proc, write_proc) \
	static struct file_operations read_proc##_ops = { \
		.open		= read_proc##_open, \
		.read		= seq_read, \
		.write		= read_proc##_write, \
		.llseek 	= seq_lseek, \
		.release	= single_release, \
	}

#endif // __LINUX_5_6__

#define RTK_CREATE_PROC_ENTRY(name) \
{ \
		proc_create_data(name, 0644, rtl8192cd_proc_root, &rtl8192cd_proc_##name##_ops, NULL); \
}

#define RTK_CREATE_PROC_READ_ENTRY(p, name, func) \
{ \
		p = proc_create_data(name, 0644, rtl8192cd_proc_root, &func##_ops, (void *)dev); \
		SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p); \
}

#define RTK_CREATE_PROC_READ_WRITE_ENTRY(p, name, func, write_func) \
{ \
		p = proc_create_data(name, 0644, rtl8192cd_proc_root, &func##_ops, (void *)dev); \
		SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p); \
}

#define RTK_CREATE_PROC_WRITE_ENTRY(p, name, write_func) \
{ \
		p = proc_create_data(name, 0644, rtl8192cd_proc_root, &write_func##_ops, (void *)dev); \
		SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p); \
}

#ifdef CTC_WIFI_DIAG
#define CTCWIFI_CREATE_PROC_READ_ENTRY(p, name, func) \
{ \
		p = proc_create_data(name, 0644, ctcwifi_proc_root, &func##_ops, (void *)dev); \
		SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p); \
}

#define CTCWIFI_CREATE_PROC_READ_WRITE_ENTRY(p, name, func, write_func) \
{ \
		p = proc_create_data(name, 0644, ctcwifi_proc_root, &func##_ops, (void *)dev); \
		SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p); \
}
#endif // CTC_WIFI_DIAG

#else  // !(CONFIG_RTL_PROC_NEW)
#define RTK_CREATE_PROC_ENTRY(name) \
	if ( create_proc_entry (name, 0644, rtl8192cd_proc_root, \
			rtl8192cd_proc_##name, (void *)dev) == NULL ) { \
		printk("create proc %s failed!\n", name); \
		return; \
     }

#define RTK_CREATE_PROC_READ_ENTRY(p, name, func) \
	if ( (p = create_proc_read_entry (name, 0644, rtl8192cd_proc_root, \
			func, (void *)dev)) == NULL ) { \
		printk("create proc %s failed!\n", name); \
		return; \
     } \
     SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p);
	
#define RTK_CREATE_PROC_READ_WRITE_ENTRY(p, name, func, write_func) \
{\
	if ( (p = create_proc_read_entry (name, 0644, rtl8192cd_proc_root, \
			func, (void *)dev)) == NULL ) { \
		printk("create proc %s failed!\n", name); \
		return; \
     } \
	 p->write_proc = write_func;\
	 SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p); \
}
#define RTK_CREATE_PROC_WRITE_ENTRY(p, name, write_func) \
{ \
		if ( (p = create_proc_entry(name, 0644, rtl8192cd_proc_root)) == NULL ) { \
			printk("create proc %s failed!\n", name); \
			return; \
		} \
		p->write_proc = write_func; \
		p->data = (void *)dev; \
		SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p); \
}

#ifdef CTC_WIFI_DIAG
#define CTCWIFI_CREATE_PROC_READ_ENTRY(p, name, func) \
	if ( (p = create_proc_read_entry (name, 0644, ctcwifi_proc_root, \
			func, (void *)dev)) == NULL ) { \
		printk("create proc %s failed!\n", name); \
		return; \
     } \
     SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p);

#define CTCWIFI_CREATE_PROC_READ_WRITE_ENTRY(p, name, func, write_func) \
{ \
		if ((p = create_proc_read_entry(name, 0644, ctcwifi_proc_root, \
			func, (void *)dev)) == NULL) { \
			printk("create proc %s failed!\n", name); \
			return; \
		} \
		p->write_proc = write_func; \
		SET_DEFAULT_SIZE_FOR_LUNA_SLAVE(p); \
}
#endif // CTC_WIFI_DIAG

#endif // CONFIG_RTL_PROC_NEW



#ifdef __ECOS
const char* MCS_DATA_RATEStr[2][2][32] =
#else
const unsigned char* MCS_DATA_RATEStr[2][2][32] =
#endif
{
	{{"6.5", "13", "19.5", "26", "39", "52", "58.5", "65", 
	  "13", "26", "39" ,"52", "78", "104", "117", "130", 
	  "19.5", "39", "58.5", "78" ,"117", "156", "175.5", "195",
	  "26", "52", "78", "104", "156", "208", "234", "260"},					// Long GI, 20MHz
	  
	 {"7.2", "14.4", "21.7", "28.9", "43.3", "57.8", "65", "72.2", 
	  "14.4", "28.9", "43.3", "57.8", "86.7", "115.6", "130", "144.5", 
	  "21.7", "43.3", "65", "86.7", "130", "173.3", "195", "216.7",
	  "28.9", "57.8", "86.7", "115.6", "173.3", "231.1", "260", "288.9"}},	// Short GI, 20MHz
	  
	{{"13.5", "27", "40.5", "54", "81", "108", "121.5", "135", 
	  "27", "54", "81", "108", "162", "216", "243", "270", 
	  "40.5", "81", "121.5", "162", "243", "324", "364.5", "405",
	  "54", "108", "162", "216", "324", "432", "486", "540"},				// Long GI, 40MHz
	  
	 {"15", "30", "45", "60", "90", "120", "135", "150", 
	  "30", "60", "90", "120", "180", "240", "270", "300", 
	  "45", "90", "135", "180", "270", "360", "405", "450",
	  "60", "120", "180", "240", "360", "480", "540", "600"}	}			// Short GI, 40MHz
};


#ifdef  RTK_AC_SUPPORT

extern const u2Byte VHT_MCS_DATA_RATE[4][2][40];
int query_vht_rate(struct stat_info *pstat) 
{
	int txrate = pstat->current_tx_rate;
	if(is_MCS_rate(txrate)) {
		unsigned char sg = (pstat->ht_current_tx_info & TX_USE_SHORT_GI) ? 1 : 0;
		if(is_VHT_rate(txrate)) {
			txrate = VHT_MCS_DATA_RATE[MIN_NUM(pstat->tx_bw, 3)][sg][(txrate - VHT_RATE_ID)];
		} else {			
			char index = txrate & 0xf;
			txrate=  VHT_MCS_DATA_RATE[MIN_NUM(pstat->tx_bw, 1)][sg][(index <8) ? index :(index+2)];
		}
	} 
	return (txrate>>1);
}

#if (MU_BEAMFORMING_SUPPORT == 1)
int query_mu_vht_rate(struct stat_info *pstat) 
{
	int txrate = (pstat->mu_rate & 0x7f) - 44 + 0xa0;
	if(is_MCS_rate(txrate)) {
		unsigned char sg = (pstat->mu_rate & 0x80) ? 1:0;
		if(is_VHT_rate(txrate)) {
			txrate = VHT_MCS_DATA_RATE[MIN_NUM(pstat->tx_bw, 2)][sg][(txrate - VHT_RATE_ID)];
		} else {			
			char index = txrate & 0xf;
			txrate=  VHT_MCS_DATA_RATE[MIN_NUM(pstat->tx_bw, 1)][sg][(index <8) ? index :(index+2)];
		}
	} 
	return (txrate>>1);
}
#endif

int query_vht_rx_rate(struct stat_info *pstat) 
{
	int rxrate = pstat->rx_rate;
	if(is_MCS_rate(rxrate)) {
		unsigned char sg = (pstat->rx_splcp) ? 1 : 0;		
		if(is_VHT_rate(rxrate)) {
			rxrate = VHT_MCS_DATA_RATE[MIN_NUM(pstat->rx_bw, 3)][sg][(rxrate - VHT_RATE_ID)];
		} else {			
			char index = rxrate & 0xf;
			rxrate=  VHT_MCS_DATA_RATE[MIN_NUM(pstat->rx_bw, 1)][sg][(index <8) ? index :(index+2)];
		}
	} 
	return (rxrate>>1);
}

#endif

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_staconfig(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_staconfig(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();

	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	int pos = 0, i;
#ifdef __ECOS
	char tmpbuf[100];
#else
	unsigned char tmpbuf[100];
#endif
	tmpbuf[0] = '\0';

	PRINT_ONE("  Dot11StationConfigEntry...", "%s", 1);
	PRINT_ARRAY_ARG("    dot11Bssid: ",
			priv->pmib->dot11StationConfigEntry.dot11Bssid, "%02x", 6);

	if (priv->pmib->dot11StationConfigEntry.dot11DesiredSSIDLen < sizeof(priv->pmib->dot11StationConfigEntry.dot11DesiredSSID)
		&& priv->pmib->dot11StationConfigEntry.dot11DesiredSSIDLen < sizeof(tmpbuf)) {
		memcpy(tmpbuf, priv->pmib->dot11StationConfigEntry.dot11DesiredSSID, priv->pmib->dot11StationConfigEntry.dot11DesiredSSIDLen);
		tmpbuf[priv->pmib->dot11StationConfigEntry.dot11DesiredSSIDLen] = '\0';
	}
	PRINT_ONE("    dot11DesiredSSID:(Len ", "%s", 0);
	PRINT_ONE(priv->pmib->dot11StationConfigEntry.dot11DesiredSSIDLen, "%d) ", 0);
	PRINT_ONE(tmpbuf, "%s", 1);

	if (priv->pmib->dot11StationConfigEntry.dot11DefaultSSIDLen < sizeof(priv->pmib->dot11StationConfigEntry.dot11DefaultSSID)
		&& priv->pmib->dot11StationConfigEntry.dot11DefaultSSIDLen < sizeof(tmpbuf)) {
		memcpy(tmpbuf, priv->pmib->dot11StationConfigEntry.dot11DefaultSSID, priv->pmib->dot11StationConfigEntry.dot11DefaultSSIDLen);
		tmpbuf[priv->pmib->dot11StationConfigEntry.dot11DefaultSSIDLen] = '\0';
	}
	PRINT_ONE("    dot11DefaultSSID:(Len ", "%s", 0);
	PRINT_ONE(priv->pmib->dot11StationConfigEntry.dot11DefaultSSIDLen, "%d) ", 0);
	PRINT_ONE(tmpbuf, "%s", 1);

	if (priv->pmib->dot11StationConfigEntry.dot11SSIDtoScanLen < sizeof(priv->pmib->dot11StationConfigEntry.dot11SSIDtoScan)
		&& priv->pmib->dot11StationConfigEntry.dot11SSIDtoScanLen < sizeof(tmpbuf)) {
		memcpy(tmpbuf, priv->pmib->dot11StationConfigEntry.dot11SSIDtoScan, priv->pmib->dot11StationConfigEntry.dot11SSIDtoScanLen);
		tmpbuf[priv->pmib->dot11StationConfigEntry.dot11SSIDtoScanLen] = '\0';
	}
	PRINT_ONE("    dot11SSIDtoScan:(Len ", "%s", 0);
	PRINT_ONE(priv->pmib->dot11StationConfigEntry.dot11SSIDtoScanLen, "%d) ", 0);
	PRINT_ONE(tmpbuf, "%s", 1);

	PRINT_ARRAY_ARG("    dot11DesiredBssid: ",
			priv->pmib->dot11StationConfigEntry.dot11DesiredBssid, "%02x", 6);
	PRINT_ARRAY_ARG("    dot11OperationalRateSet: ",
			priv->pmib->dot11StationConfigEntry.dot11OperationalRateSet, "%02x",
			priv->pmib->dot11StationConfigEntry.dot11OperationalRateSetLen);
	PRINT_SINGL_ARG("    dot11OperationalRateSetLen: ",
			priv->pmib->dot11StationConfigEntry.dot11OperationalRateSetLen, "%d");
	PRINT_SINGL_ARG("    dot11BeaconPeriod: ",
			priv->pmib->dot11StationConfigEntry.dot11BeaconPeriod, "%d");
	PRINT_SINGL_ARG("    dot11DTIMPeriod: ",
			priv->pmib->dot11StationConfigEntry.dot11DTIMPeriod, "%d");
	PRINT_SINGL_ARG("    dot11swcrypto: ",
			priv->pmib->dot11StationConfigEntry.dot11swcrypto, "%d");
	PRINT_SINGL_ARG("    dot11AclMode: ",
			priv->pmib->dot11StationConfigEntry.dot11AclMode, "%d");
	PRINT_SINGL_ARG("    dot11AclNum: ",
			priv->pmib->dot11StationConfigEntry.dot11AclNum, "%d");

	for (i=0; i<priv->pmib->dot11StationConfigEntry.dot11AclNum; i++) {
		snprintf(tmpbuf, sizeof(tmpbuf), "    dot11AclAddr[%d]: ", i);
		PRINT_ARRAY_ARG(tmpbuf, priv->pmib->dot11StationConfigEntry.dot11AclAddr[i], "%02x", 6);
	}
#ifdef D_ACL	
	//if(priv->pmib->dot11StationConfigEntry.dot11AclMode) {
		struct list_head	*phead, *plist;
		struct wlan_acl_node	*paclnode;
#ifdef SMP_SYNC
		unsigned long flags = 0;
#endif
		phead = &priv->wlan_acl_list;

		SMP_LOCK_ACL(flags);
		
		plist = phead->next;
		if (plist != phead) {
			PRINT_ONE("    Access control list...", "%s", 1);
			do {
				paclnode = list_entry(plist, struct wlan_acl_node, list);
				plist = plist->next;
				PRINT_ONE("	 hwaddr: ", "%s", 0); 
				PRINT_ARRAY( paclnode->addr, "%02x", MACADDRLEN, 0);		
				PRINT_ONE((paclnode->mode == ACL_ALLOW) ? "allow" : ((paclnode->mode == ACL_DENY) ? "deny" : "disabled"), "(%s)", 0);
				PRINT_ONE( " BlockTimes: ", "%s", 0);
				PRINT_ONE( paclnode->block_time, "%d",1);
			} while (plist != phead);
		}
		
		SMP_UNLOCK_ACL(flags);
	//}
#endif
	
	PRINT_SINGL_ARG("    dot11SupportedRates: ",
			priv->pmib->dot11StationConfigEntry.dot11SupportedRates, "0x%x");
	PRINT_SINGL_ARG("    dot11BasicRates: ",
			priv->pmib->dot11StationConfigEntry.dot11BasicRates, "0x%x");
	PRINT_SINGL_ARG("    dot11RegDomain: ",
			priv->pmib->dot11StationConfigEntry.dot11RegDomain, "%d");
	PRINT_SINGL_ARG("    autoRate: ",
			priv->pmib->dot11StationConfigEntry.autoRate, "%d");
	PRINT_SINGL_ARG("    fixedTxRate: ",
			priv->pmib->dot11StationConfigEntry.fixedTxRate, "0x%x");
#ifdef RTK_AC_SUPPORT  //vht rate 
	PRINT_SINGL_ARG("    dot11Supported VHT Rates: ",
			priv->pmib->dot11acConfigEntry.dot11SupportedVHT, "%x");
	PRINT_SINGL_ARG("    dot11 VHT tx rate map: ",
			priv->pmib->dot11acConfigEntry.dot11VHT_TxMap, "%x");
#endif
	PRINT_SINGL_ARG("    swTkipMic: ",
			priv->pmib->dot11StationConfigEntry.swTkipMic, "%d");
	PRINT_SINGL_ARG("    protectionDisabled: ",
			priv->pmib->dot11StationConfigEntry.protectionDisabled, "%d");
	PRINT_SINGL_ARG("    olbcDetectDisabled: ",
			priv->pmib->dot11StationConfigEntry.olbcDetectDisabled, "%d");
	PRINT_SINGL_ARG("    nmlscDetectDisabled: ",
			priv->pmib->dot11StationConfigEntry.nmlscDetectDisabled, "%d");
	PRINT_SINGL_ARG("    legacySTADeny: ",
			priv->pmib->dot11StationConfigEntry.legacySTADeny, "%d");
#ifdef CLIENT_MODE
	PRINT_SINGL_ARG("    fastRoaming: ",
			priv->pmib->dot11StationConfigEntry.fastRoaming, "%d");
#endif
	PRINT_SINGL_ARG("    lowestMlcstRate: ",
			priv->pmib->dot11StationConfigEntry.lowestMlcstRate, "%d");
	PRINT_SINGL_ARG("    supportedStaNum: ",
			priv->pmib->dot11StationConfigEntry.supportedStaNum, "%d");
	PRINT_SINGL_ARG("    totalMaxStaNum: ",
			priv->pshare->rf_ft_var.dynamic_max_num_stat, "%d");
#if defined(CONFIG_RTL_SIMPLE_CONFIG)
	memcpy(tmpbuf, priv->pmib->dot11StationConfigEntry.sc_device_name, strlen(priv->pmib->dot11StationConfigEntry.sc_device_name));
	tmpbuf[strlen(priv->pmib->dot11StationConfigEntry.sc_device_name)] = '\0';
	PRINT_ONE("    scDeviceName: ", "%s", 0);
	PRINT_ONE(tmpbuf, "%s", 1);

	PRINT_SINGL_ARG("    scDeviceType: ",
			priv->pmib->dot11StationConfigEntry.sc_device_type, "%d");

	memcpy(tmpbuf, priv->pmib->dot11StationConfigEntry.dot11DesiredSSID, priv->pmib->dot11StationConfigEntry.dot11DesiredSSIDLen);
	tmpbuf[priv->pmib->dot11StationConfigEntry.dot11DesiredSSIDLen] = '\0';
	PRINT_ONE("    scSSID: ", "%s", 0);
	PRINT_ONE(tmpbuf, "%s", 1);
	
	memcpy(tmpbuf, priv->pmib->dot11StationConfigEntry.sc_passwd, strlen(priv->pmib->dot11StationConfigEntry.sc_passwd));
	tmpbuf[strlen(priv->pmib->dot11StationConfigEntry.sc_passwd)] = '\0';
	PRINT_ONE("    scPassword: ", "%s", 0);
	PRINT_ONE(tmpbuf, "%s", 1);

	PRINT_SINGL_ARG("    scPinEnabled: ",
		priv->pmib->dot11StationConfigEntry.sc_pin_enabled, "%d");
		
	memcpy(tmpbuf, priv->pmib->dot11StationConfigEntry.sc_pin, strlen(priv->pmib->dot11StationConfigEntry.sc_pin));
	tmpbuf[strlen(priv->pmib->dot11StationConfigEntry.sc_pin)] = '\0';
	PRINT_ONE("    scPin: ", "%s", 0);
	PRINT_ONE(tmpbuf, "%s", 1);

	memcpy(tmpbuf, priv->pmib->dot11StationConfigEntry.sc_default_pin, strlen(priv->pmib->dot11StationConfigEntry.sc_default_pin));
	tmpbuf[strlen(priv->pmib->dot11StationConfigEntry.sc_default_pin)] = '\0';
	PRINT_ONE("    scDefaultPin: ", "%s", 0);
	PRINT_ONE(tmpbuf, "%s", 1);
	
	PRINT_SINGL_ARG("    scSyncVxdToRoot: ",
			priv->pmib->dot11StationConfigEntry.sc_sync_vxd_to_root, "%d");
	PRINT_SINGL_ARG("    scControlIP: ",
			priv->pmib->dot11StationConfigEntry.sc_control_ip, "%08x");

	PRINT_SINGL_ARG("    scStatus: ",
			priv->pmib->dot11StationConfigEntry.sc_status, "%d");
#endif
#if defined(UNIVERSAL_REPEATER)
	PRINT_SINGL_ARG("    Support Broadcast SSID inheritance: ", priv->pmib->dot11StationConfigEntry.bcastSSID_inherit, "%d");
	PRINT_SINGL_ARG("    Inherited Broadcast SSID: ", (priv->take_over_hidden)? 0:1, "%d");
#endif

	PRINT_SINGL_ARG("    BeaconRate: ",
			priv->pmib->dot11StationConfigEntry.beacon_rate, "%d");

	if(priv->pptyIE && priv->pptyIE->content && (priv->pptyIE->length < 65)) {
		unsigned char cie_str[100]={0}, cie_content[65]={0};

		memcpy(cie_content,priv->pptyIE->content,priv->pptyIE->length);
		cie_content[priv->pptyIE->length] = '\0';
		snprintf(cie_str,sizeof(cie_str),"[%d]%02x%02x%02x-%s",priv->pptyIE->id,priv->pptyIE->oui[0],priv->pptyIE->oui[1],priv->pptyIE->oui[2],cie_content);
		PRINT_SINGL_ARG("    Beacon with customized IE: ", cie_str, "%s");
	}

	for (i = 0; i < 8; i++) {
		PRINT_ONE("    swq_max_xmit[", "%s", 0);
		PRINT_ONE(i, "%d]: ", 0);
		PRINT_ONE(priv->pshare->swq_max_xmit[i], "%d", 1);
	}

	return pos;
}

#ifdef AUTH_SAE
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_sae_peertable(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_sae_peertable(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int tbl_sz = 1 << GET_ROOT(priv)->sae_peer_table->table_size_power;
	int i;
	struct candidate *peer_entry;
	unsigned long now = RTL_JIFFIES_TO_SECOND(jiffies);

	PRINT_ONE("SAE peer table:", "%s", 1);
	PRINT_SINGL_ARG("now:", now, "%ul");

	for (i = 0; i < tbl_sz; i++) {
		peer_entry = ((struct candidate *)GET_ROOT(priv)->sae_peer_table->entry_array[i].data);
		PRINT_SINGL_ARG(" [idx]",	i, "%d");
		PRINT_ARRAY_ARG("  peer_mac:", peer_entry->peer_mac, "%02x", 6);
		PRINT_ARRAY_ARG("  my_mac:", peer_entry->my_mac, "%02x", 6);
		PRINT_SINGL_ARG("  t0:", RTL_JIFFIES_TO_SECOND(peer_entry->t0), "%ul");
		PRINT_SINGL_ARG("  t1:", RTL_JIFFIES_TO_SECOND(peer_entry->t1), "%ul");
		PRINT_SINGL_ARG("  t2:", RTL_JIFFIES_TO_SECOND(peer_entry->t2), "%ul");
		PRINT_SINGL_ARG("  dirty:", GET_ROOT(priv)->sae_peer_table->entry_array[i].dirty, "%02x");
	}

	return 0;
}
#endif

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_pmk(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_pmk(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	int idx=0;
	PRINT_ONE("SAE PMK cache:", "%s", 1);
#ifdef AUTH_SAE
	PRINT_SINGL_ARG(" pmkid_caching_idx: ",
		priv->wpa_global_info->pmkid_cache.pmkid_cache_idx, "%d");
	for(idx=0; idx<NUM_PMKID_CACHE; idx++){
		PRINT_SINGL_ARG(" [idx]",	idx, "%d");
		PRINT_ARRAY_ARG("  pmk: ",
			priv->wpa_global_info->pmkid_cache.pmk[idx], "%02x", 6);
		PRINT_ARRAY_ARG("  pmkid: ",
			priv->wpa_global_info->pmkid_cache.pmkid[idx], "%02x", 6);
		PRINT_ARRAY_ARG("  peermac: ",
			priv->wpa_global_info->pmkid_cache.peermac[idx], "%02x", 6);
	}
#endif

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_auth(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_auth(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
#ifdef AUTH_SAE
	unsigned char bignum_buf[64];
	unsigned char bignum_len;
	GD *current_gd=NULL;
	int idx=0;
#endif

	PRINT_ONE("  Dot1180211AuthEntry...", "%s", 1);
	PRINT_SINGL_ARG("    dot11AuthAlgrthm: ",
			priv->pmib->dot1180211AuthEntry.dot11AuthAlgrthm, "%d");
	PRINT_SINGL_ARG("    dot11PrivacyAlgrthm: ",
			priv->pmib->dot1180211AuthEntry.dot11PrivacyAlgrthm, "%d");
	PRINT_SINGL_ARG("    dot11PrivacyKeyIndex: ",
			priv->pmib->dot1180211AuthEntry.dot11PrivacyKeyIndex, "%d");
	PRINT_SINGL_ARG("    dot11PrivacyKeyLen: ",
			priv->pmib->dot1180211AuthEntry.dot11PrivacyKeyLen, "%d");
#if defined(INCLUDE_WPA_PSK) || defined(WIFI_HAPD) || defined(RTK_NL80211)
	PRINT_SINGL_ARG("    dot11EnablePSK: ",
			priv->pmib->dot1180211AuthEntry.dot11EnablePSK, "%d");
	PRINT_SINGL_ARG("    dot11WPACipher: ",
			priv->pmib->dot1180211AuthEntry.dot11WPACipher, "%d");
#ifdef RTL_WPA2
	PRINT_SINGL_ARG("    dot11WPA2Cipher: ",
			priv->pmib->dot1180211AuthEntry.dot11WPA2Cipher, "%d");
#endif
	PRINT_SINGL_ARG("    dot11WPA3Cipher: ",
			priv->pmib->dot1180211AuthEntry.dot11WPA3Cipher, "%d");
#ifdef CONFIG_IEEE80211W
	PRINT_SINGL_ARG("    dot11IEEE80211W: ",
			priv->pmib->dot1180211AuthEntry.dot11IEEE80211W, "%d");
	PRINT_SINGL_ARG("    dot11EnableSHA256: ",
			priv->pmib->dot1180211AuthEntry.dot11EnableSHA256, "%d");
#endif
	PRINT_SINGL_ARG("    dot11PassPhrase: ",
			priv->pmib->dot1180211AuthEntry.dot11PassPhrase, "%s");
	PRINT_SINGL_ARG("    dot11PassPhraseGuest: ",
			priv->pmib->dot1180211AuthEntry.dot11PassPhraseGuest, "%s");
	PRINT_SINGL_ARG("    dot11GKRekeyTime: ",
			priv->pmib->dot1180211AuthEntry.dot11GKRekeyTime, "%u");
	PRINT_SINGL_ARG("    dot11UKRekeyTime: ",
			priv->pmib->dot1180211AuthEntry.dot11UKRekeyTime, "%u");
#endif

#ifdef CLIENT_MODE
    if (OPMODE & WIFI_STATION_STATE) {
        PRINT_SINGL_ARG("    4-way status: ",
            priv->dot114WayStatus, "%d");
		PRINT_SINGL_ARG("    4-way finished: ",
            priv->is_4Way_finished, "%d");
    }
#endif

	PRINT_ONE("  Dot118021xAuthEntry...", "%s", 1);
	PRINT_SINGL_ARG("    dot118021xAlgrthm: ",
			priv->pmib->dot118021xAuthEntry.dot118021xAlgrthm, "%d");
	PRINT_SINGL_ARG("    dot118021xDefaultPort: ",
			priv->pmib->dot118021xAuthEntry.dot118021xDefaultPort, "%d");
	PRINT_SINGL_ARG("    dot118021xcontrolport: ",
			priv->pmib->dot118021xAuthEntry.dot118021xcontrolport, "%d");
	PRINT_ONE("  RADIUS Accounting...", "%s", 1);
	PRINT_SINGL_ARG("    Enabled: ",priv->pmib->dot118021xAuthEntry.acct_enabled, "%d");	
	PRINT_SINGL_ARG("    Idle period to leave STA: ",priv->pmib->dot118021xAuthEntry.acct_timeout_period, "%lu min(s)");
	PRINT_SINGL_ARG("    Idle throughput to leave STA: ", priv->pmib->dot118021xAuthEntry.acct_timeout_throughput, "%d Kbpm");

	PRINT_ONE("  Dot11RsnIE...", "%s", 1);
	PRINT_ARRAY_ARG("    rsnie: ",
			priv->pmib->dot11RsnIE.rsnie, "%02x", priv->pmib->dot11RsnIE.rsnielen);
	PRINT_SINGL_ARG("    rsnielen: ", priv->pmib->dot11RsnIE.rsnielen, "%d");
	PRINT_ARRAY_ARG("	 IGTK: ", priv->pmib->dot11IGTKTable.dot11EncryptKey.dot11TTKey.skey, "%02x", 16);

#ifdef AUTH_SAE
	for(idx=0;idx<AP_MODE_SUPPORT_GROUP_NUM;idx++){
		if(idx==0)
			current_gd =  priv->gd;
		if(idx==1 && priv->gd)
			current_gd =  priv->gd->next;
		if(idx==2&&priv->gd->next)
			current_gd =  priv->gd->next->next;
		if(current_gd) {
			PRINT_ONE("  SAE:", "%s", 1);
			PRINT_SINGL_ARG("	group: ", current_gd->group_num, "%d");
			bignum_len=mbedtls_mpi_size(current_gd->prime);
			mbedtls_mpi_write_binary(current_gd->prime, bignum_buf, 64);
			PRINT_ARRAY_ARG("	prime: ", bignum_buf, "%02x", bignum_len);
			bignum_len=mbedtls_mpi_size(current_gd->order);
			mbedtls_mpi_write_binary(current_gd->order, bignum_buf, 64);
			PRINT_ARRAY_ARG("	order: ", bignum_buf, "%02x", bignum_len);
			PRINT_SINGL_ARG("	passwd len: ", strlen(current_gd->password), "%d");
			PRINT_ARRAY_ARG("	passwd: ", current_gd->password, "%02x", 64);
		}else
			break;
	}
#endif
#ifdef CONFIG_RTL_WAPI_SUPPORT
	PRINT_ONE("  Dot1180211WAPIEntry...", "%s", 1);
	PRINT_SINGL_ARG("    dot11EnableWAPI: ",
			priv->pmib->wapiInfo.wapiType, "%d");
#ifdef WAPI_SUPPORT_MULTI_ENCRYPT
	PRINT_SINGL_ARG("    dot11wapiUCastEncodeType: ",
			priv->pmib->wapiInfo.wapiUCastEncodeType, "%d");
	PRINT_SINGL_ARG("    dot11wapiMCastEncodeType: ",
			priv->pmib->wapiInfo.wapiMCastEncodeType, "%d");
#endif
	PRINT_ARRAY_ARG("    dot11wapiPsk: ",
			priv->pmib->wapiInfo.wapiPsk.octet, "%02x", WAPI_PSK_LEN);
	PRINT_SINGL_ARG("    dot11wapiwapiPsklen: ",
			priv->pmib->wapiInfo.wapiPsk.len, "%d");
	PRINT_SINGL_ARG("    dot11wapiUpdateUCastKeyType: ",
			priv->pmib->wapiInfo.wapiUpdateUCastKeyType, "%u");
	PRINT_SINGL_ARG("    dot11wapiUpdateUCastKeyTimeout: ",
			(unsigned int)priv->pmib->wapiInfo.wapiUpdateUCastKeyTimeout, "%u");
	PRINT_SINGL_ARG("    dot11wapiUpdateUCastKeyPktNum: ",
			(unsigned int)priv->pmib->wapiInfo.wapiUpdateUCastKeyPktNum, "%u");
	PRINT_SINGL_ARG("    dot11wapiUpdateMCastKeyType: ",
			priv->pmib->wapiInfo.wapiUpdateMCastKeyType, "%u");
	PRINT_SINGL_ARG("    dot11wapiUpdateMCastKeyTimeout: ",
			(unsigned int)priv->pmib->wapiInfo.wapiUpdateMCastKeyTimeout, "%u");
	PRINT_SINGL_ARG("    dot11wapiUpdateMCastKeyPktNum: ",
			(unsigned int)priv->pmib->wapiInfo.wapiUpdateMCastKeyPktNum, "%u");
	PRINT_ARRAY_ARG("    dot11wapiTimeout: ",
			priv->pmib->wapiInfo.wapiTimeout, "%08x", wapiTimeoutTotalNum);
#endif

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_dkeytbl(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_dkeytbl(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  Dot11DefaultKeysTable...", "%s", 1);
	PRINT_ARRAY_ARG("    keytype[0].skey: ",
			priv->pmib->dot11DefaultKeysTable.keytype[0].skey, "%02x", 16);
	PRINT_ARRAY_ARG("    keytype[1].skey: ",
			priv->pmib->dot11DefaultKeysTable.keytype[1].skey, "%02x", 16);
	PRINT_ARRAY_ARG("    keytype[2].skey: ",
			priv->pmib->dot11DefaultKeysTable.keytype[2].skey, "%02x", 16);
	PRINT_ARRAY_ARG("    keytype[3].skey: ",
			priv->pmib->dot11DefaultKeysTable.keytype[3].skey, "%02x", 16);
	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_gkeytbl(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_gkeytbl(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	unsigned char *ptr;

	PRINT_ONE("  Dot11GroupKeysTable...", "%s", 1);
#ifdef CONFIG_RTL_WAPI_SUPPORT
	if (priv->pmib->wapiInfo.wapiType!=wapiDisable)
	{
		PRINT_SINGL_ARG("    dot11Privacy: ",
				priv->pmib->wapiInfo.wapiType, "%d");
		PRINT_SINGL_ARG("    WAPI Multicast Encrypt Algorithm: ",
				priv->pmib->wapiInfo.wapiMCastEncodeType, "%d");
		PRINT_SINGL_ARG("    keyInCam: ", (priv->pmib->dot11GroupKeysTable.keyInCam? "yes" : "no"), "%s");
		PRINT_SINGL_ARG("    WAPI Multicast Data KeyLen: ",
				WAPI_KEY_LEN, "%d");
		PRINT_SINGL_ARG("    WAPI Multicast Mic KeyLen: ",
				WAPI_KEY_LEN, "%d");
		PRINT_SINGL_ARG("    WAPI Multicast Key Index: ",
				priv->wapiMCastKeyId, "%d");
		PRINT_ARRAY_ARG("    WAPI Multicast Data Key: ",
				priv->wapiMCastKey[priv->wapiMCastKeyId].dataKey, "%02x", WAPI_KEY_LEN);
		PRINT_ARRAY_ARG("    WAPI Multicast Mic Key: ",
				priv->wapiMCastKey[priv->wapiMCastKeyId].micKey, "%02x", WAPI_KEY_LEN);
	}
	else
#endif
	{
		PRINT_SINGL_ARG("    dot11Privacy: ",
				priv->pmib->dot11GroupKeysTable.dot11Privacy, "%d");
		PRINT_SINGL_ARG("    keyInCam: ", (priv->pmib->dot11GroupKeysTable.keyInCam? "yes" : "no"), "%s");
		PRINT_SINGL_ARG("    dot11EncryptKey.dot11TTKeyLen: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey.dot11TTKeyLen, "%d");
		PRINT_SINGL_ARG("    dot11EncryptKey.dot11TMicKeyLen: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey.dot11TMicKeyLen, "%d");
		PRINT_ARRAY_ARG("    dot11EncryptKey.dot11TTKey.skey: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey.dot11TTKey.skey, "%02x", 16);
		PRINT_ARRAY_ARG("    dot11EncryptKey.dot11TMicKey1.skey: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey.dot11TMicKey1.skey, "%02x", 16);
		PRINT_ARRAY_ARG("    dot11EncryptKey.dot11TMicKey2.skey: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey.dot11TMicKey2.skey, "%02x", 16);
		ptr = (unsigned char *)&priv->pmib->dot11GroupKeysTable.dot11EncryptKey.dot11TXPN48.val48;
		PRINT_ARRAY_ARG("    dot11EncryptKey.dot11TXPN48.val48: ", ptr, "%02x", 8);
		ptr = (unsigned char *)&priv->pmib->dot11GroupKeysTable.dot11EncryptKey.dot11RXPN48.val48;
		PRINT_ARRAY_ARG("    dot11EncryptKey.dot11RXPN48.val48: ", ptr, "%02x", 8);
#ifdef CONFIG_IEEE80211W
		PRINT_SINGL_ARG("    dot11IGTKTable.dot11EncryptKey.dot11TTKeyLen: ",
				priv->pmib->dot11IGTKTable.dot11EncryptKey.dot11TTKeyLen, "%d");
		PRINT_SINGL_ARG("    dot11IGTKTable.dot11EncryptKey.dot11TMicKeyLen: ",
				priv->pmib->dot11IGTKTable.dot11EncryptKey.dot11TMicKeyLen, "%d");
		PRINT_ARRAY_ARG("    dot11IGTKTable.dot11EncryptKey.dot11TTKey.skey: ",
				priv->pmib->dot11IGTKTable.dot11EncryptKey.dot11TTKey.skey, "%02x", 16);
		PRINT_ARRAY_ARG("    dot11IGTKTable.dot11EncryptKey.dot11TMicKey1.skey: ",
				priv->pmib->dot11IGTKTable.dot11EncryptKey.dot11TMicKey1.skey, "%02x", 16);
#endif

		PRINT_SINGL_ARG("    dot11EncryptKey2.dot11TTKeyLen: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey2.dot11TTKeyLen, "%d");
		PRINT_SINGL_ARG("    dot11EncryptKey2.dot11TMicKeyLen: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey2.dot11TMicKeyLen, "%d");
		PRINT_ARRAY_ARG("    dot11EncryptKey2.dot11TTKey.skey: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey2.dot11TTKey.skey, "%02x", 16);
		PRINT_ARRAY_ARG("    dot11EncryptKey2.dot11TMicKey1.skey: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey2.dot11TMicKey1.skey, "%02x", 16);
		PRINT_ARRAY_ARG("    dot11EncryptKey2.dot11TMicKey2.skey: ",
				priv->pmib->dot11GroupKeysTable.dot11EncryptKey2.dot11TMicKey2.skey, "%02x", 16);
				ptr = (unsigned char *)&priv->pmib->dot11GroupKeysTable.dot11EncryptKey2.dot11TXPN48.val48;
		PRINT_ARRAY_ARG("    dot11EncryptKey2.dot11TXPN48.val48: ", ptr, "%02x", 8);
				ptr = (unsigned char *)&priv->pmib->dot11GroupKeysTable.dot11EncryptKey2.dot11RXPN48.val48;
		PRINT_ARRAY_ARG("    dot11EncryptKey2.dot11RXPN48.val48: ", ptr, "%02x", 8);
	}

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_operation(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_operation(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	char tmpbuf[20];
	int idx = 0;

	PRINT_ONE("  Dot11OperationEntry...", "%s", 1);
	PRINT_ARRAY_ARG("    hwaddr: ",	priv->pmib->dot11OperationEntry.hwaddr, "%02x", 6);
	PRINT_SINGL_ARG("    opmode: ", priv->pmib->dot11OperationEntry.opmode, "0x%x");
#ifdef MBSSID	
	PRINT_SINGL_ARG("    vap_init_seq: ", priv->vap_init_seq, "0x%x");
#endif	
#ifdef CONFIG_RTL_OFFLOAD_DRIVER
	PRINT_SINGL_ARG("    if_id: ", priv->if_id, "0x%x");
#endif
	PRINT_SINGL_ARG("    hiddenAP: ", priv->pmib->dot11OperationEntry.hiddenAP, "%d");
	PRINT_SINGL_ARG("    dot11RTSThreshold: ", priv->pmib->dot11OperationEntry.dot11RTSThreshold, "%d");
	PRINT_SINGL_ARG("    dot11FragmentationThreshold: ", priv->pmib->dot11OperationEntry.dot11FragmentationThreshold, "%d");
	PRINT_SINGL_ARG("    dot11ShortRetryLimit: ", priv->pmib->dot11OperationEntry.dot11ShortRetryLimit, "%d");
	PRINT_SINGL_ARG("    dot11LongRetryLimit: ", priv->pmib->dot11OperationEntry.dot11LongRetryLimit, "%d");
	PRINT_SINGL_ARG("    expiretime: ", priv->pmib->dot11OperationEntry.expiretime, "%d");
	PRINT_SINGL_ARG("    led_type: ", priv->pmib->dot11OperationEntry.ledtype, "%d");
#ifdef RTL8190_SWGPIO_LED
	PRINT_SINGL_ARG("    led_route: ", priv->pmib->dot11OperationEntry.ledroute, "0x%x");
#endif
	PRINT_SINGL_ARG("    iapp_enable: ", priv->pmib->dot11OperationEntry.iapp_enable, "%d");
	PRINT_SINGL_ARG("    block_relay: ", priv->pmib->dot11OperationEntry.block_relay, "%d");
	PRINT_SINGL_ARG("    deny_any: ", priv->pmib->dot11OperationEntry.deny_any, "%d");
	PRINT_SINGL_ARG("    crc_log: ", priv->pmib->dot11OperationEntry.crc_log, "%d");
	PRINT_SINGL_ARG("    wifi_specific: ", priv->pmib->dot11OperationEntry.wifi_specific, "%d");
#ifdef WIFI_WMM
	PRINT_SINGL_ARG("    qos_enable: ", priv->pmib->dot11QosEntry.dot11QosEnable, "%d");
#if defined(WIFI_QOS_ENHANCE)
	PRINT_SINGL_ARG("    qos_enhance_enable: ", priv->pshare->rf_ft_var.qos_enhance_enable, "%d");	
	PRINT_SINGL_ARG("    qos_enhance_rssi_low_thd: ", priv->pshare->rf_ft_var.qos_enhance_rssi_low_thd, "%d");
	PRINT_SINGL_ARG("    qos_enhance_rssi_high_thd: ", priv->pshare->rf_ft_var.qos_enhance_rssi_high_thd, "%d");
	PRINT_SINGL_ARG("    qos_enhance_tp_low_thd: ", priv->pshare->rf_ft_var.qos_enhance_tp_low_thd, "%d");
	PRINT_SINGL_ARG("    qos_enhance_tp_middle_thd: ", priv->pshare->rf_ft_var.qos_enhance_tp_middle_thd, "%d");
	PRINT_SINGL_ARG("    qos_enhance_tp_high_thd: ", priv->pshare->rf_ft_var.qos_enhance_tp_high_thd, "%d");
	PRINT_SINGL_ARG("    qos_enhance_rssi_edge_thd: ", priv->pshare->rf_ft_var.qos_enhance_rssi_edge_thd, "%d");
	PRINT_ONE("  qos_enhance_sta list:", "%s", 1);
	for (idx=0; idx<NUM_STAT; idx++) {
		if(memcmp("\x0\x0\x0\x0\x0\x0", priv->pshare->rf_ft_var.qos_enhance_sta[idx].qos_enhance_sta_addr, MACADDRLEN)){
			PRINT_ARRAY_ARG("		", priv->pshare->rf_ft_var.qos_enhance_sta[idx].qos_enhance_sta_addr, "%02x", MACADDRLEN);
		}
	}
	idx=0;
#endif
#ifdef WMM_APSD
	PRINT_SINGL_ARG("    apsd_enable: ", priv->pmib->dot11QosEntry.dot11QosAPSD, "%d");
#ifdef CLIENT_MODE
	if ((OPMODE & WIFI_STATION_STATE) && QOS_ENABLE && APSD_ENABLE) {
		PRINT_SINGL_ARG("        uapsd_assoc: ", priv->uapsd_assoc, "%d");
		PRINT_SINGL_ARG("        UAPSD_AC_VO: ", priv->pmib->dot11QosEntry.UAPSD_AC_VO, "%d");
		PRINT_SINGL_ARG("        UAPSD_AC_VI: ", priv->pmib->dot11QosEntry.UAPSD_AC_VI, "%d");
		PRINT_SINGL_ARG("        UAPSD_AC_BE: ", priv->pmib->dot11QosEntry.UAPSD_AC_BE, "%d");
		PRINT_SINGL_ARG("        UAPSD_AC_BK: ", priv->pmib->dot11QosEntry.UAPSD_AC_BK, "%d");
	}
#endif
#endif
#endif

	if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11A)
		tmpbuf[idx++] = 'A';
	if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11B)
		tmpbuf[idx++] = 'B';
	if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11G)
		tmpbuf[idx++] = 'G';
	if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11N)
		tmpbuf[idx++] = 'N';
#ifdef RTK_AC_SUPPORT 		
	if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11AC) {
		tmpbuf[idx++] = 'A';
		tmpbuf[idx++] = 'C';
	}
#endif	
	
	tmpbuf[idx] = '\0';
	PRINT_SINGL_ARG("    net_work_type: ", tmpbuf, "%s");

#ifdef TX_SHORTCUT
	PRINT_SINGL_ARG("    disable_txsc: ", priv->pmib->dot11OperationEntry.disable_txsc, "%d");
#endif

#ifdef RX_SHORTCUT
	PRINT_SINGL_ARG("    disable_rxsc: ", priv->pmib->dot11OperationEntry.disable_rxsc, "%d");
#endif

#ifdef TX_SHORTCUT
#ifdef SUPPORT_TX_AMSDU_SHORTCUT
	PRINT_SINGL_ARG("    disable_amsdu_txsc: ", priv->pmib->dot11OperationEntry.disable_amsdu_txsc, "%d");
#endif
#endif

#ifdef BR_SHORTCUT
	PRINT_SINGL_ARG("    disable_brsc: ", priv->pmib->dot11OperationEntry.disable_brsc, "%d");
#endif

	PRINT_SINGL_ARG("    guest_access: ", priv->pmib->dot11OperationEntry.guest_access, "%d");
	PRINT_SINGL_ARG("    tdls_prohibited:  ", priv->pmib->dot11OperationEntry.tdls_prohibited, "%d");
	PRINT_SINGL_ARG("    tdls_cs_prohibited:  ", priv->pmib->dot11OperationEntry.tdls_cs_prohibited, "%d");
#ifdef CONFIG_IEEE80211V
	if(WNM_ENABLE) {
		PRINT_SINGL_ARG("    BssTransEnable:  ", 	priv->pmib->wnmEntry.dot11vBssTransEnable, "%d");
   		PRINT_SINGL_ARG("    BssReqMode:  ", 		priv->pmib->wnmEntry.dot11vReqMode, "%d");
		PRINT_SINGL_ARG("    BssDiassocImminent:  ", priv->pmib->wnmEntry.dot11vDiassocImminent, "%d");
		PRINT_SINGL_ARG("    BssDiassocDeadline:  ", priv->pmib->wnmEntry.dot11vDiassocDeadline, "%d");
	}
#endif

#ifdef HAPD_11K
	PRINT_SINGL_ARG(" rrm_enable: ", priv->hapd_11k_ie_len,"%d");
	if(priv->hapd_11k_ie_len){
		PRINT_ARRAY_ARG(" rrm cap: ", priv->hapd_11k_ie, "%02x", 5);
	}
#endif
#ifdef HAPD_11V
	PRINT_SINGL_ARG(" hapd_11v enabled:  ", 	priv->hapd_11v_enable, "%d");
#endif

#ifdef CONFIG_RTL_OFFLOAD_DRIVER
	PRINT_SINGL_ARG("    beacon_updated: ", priv->beacon_updated, "%d");
	PRINT_SINGL_ARG("    dev_core_state: ", priv->pshare->dev_core_state, "%d");
	PRINT_SINGL_ARG("    cil_pkt_id_tx: ", priv->pshare->cil_pkt_id_tx, "%d");
#endif

#ifdef CONFIG_RTL_MULTI_WIFI_MODULES
	if (priv->available_chnl_num) {
		PRINT_ONE("    avail_chnl: ", "%s", 0);
		for (idx = 0; idx < priv->available_chnl_num; idx++)
			PRINT_ONE(priv->available_chnl[idx], "%d ", 0);
		PRINT_ONE("", "%s", 1);
	}
#endif
	return pos;
}

#if (BEAMFORMING_SUPPORT == 1)
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_txbf(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_txbf(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	int idx;
	PRT_BEAMFORMING_INFO 		pBeamInfo = &(priv->pshare->BeamformingInfo);
	PRT_BEAMFORMING_ENTRY		pEntry = NULL;
	PRT_BEAMFORMER_ENTRY		pTXBFerEntry = NULL;
	struct aid_obj *aidobj;

#if (MU_BEAMFORMING_SUPPORT == 1) && defined(CONFIG_WLAN_HAL_8812FE)
	PRINT_SINGL_ARG("    IC_support_MU: ",  priv->pshare->IC_support_MU, "%d");	
#endif
	PRINT_ONE("  TXBFee Entry...", "%s", 1);
	#if (MU_BEAMFORMING_SUPPORT == 1)	
	PRINT_SINGL_ARG("    beamformee_su_reg_maping: ", pBeamInfo->beamformee_su_reg_maping, "%x");		
	PRINT_SINGL_ARG("    beamformer_su_cnt: ", pBeamInfo->beamformer_su_cnt, "%d");		
	PRINT_SINGL_ARG("    beamformee_su_cnt: ", pBeamInfo->beamformee_su_cnt, "%d");		

	if(priv->pmib->dot11RFEntry.txbf_mu) {
		PRINT_SINGL_ARG("    beamformee_mu_reg_maping: ", pBeamInfo->beamformee_mu_reg_maping, "%x");		
		PRINT_SINGL_ARG("    beamformer_mu_cnt: ", pBeamInfo->beamformer_mu_cnt, "%d");		
		PRINT_SINGL_ARG("    beamformee_mu_cnt: ", pBeamInfo->beamformee_mu_cnt, "%d");		
	#ifdef CONFIG_WLAN_HAL_8812FE
		PRINT_SINGL_ARG("    isSTAGrp: ", priv->pshare->isSTAGrp, "%d");		
		PRINT_SINGL_ARG("    isSTAGrp_pre: ", priv->pshare->isSTAGrp_pre, "%d");
	#endif
	}
	#endif	
	for(idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		pEntry = &(pBeamInfo->BeamformeeEntry[idx]);
		PRINT_ONE(idx,  " %d: txbfee entry...", 1);
		PRINT_SINGL_ARG("    bUsed: ", pEntry->bUsed, "%x");
		if(pEntry->bUsed && pEntry->pSTA) {			
			aidobj = container_of(pEntry->pSTA, struct aid_obj, station);
			priv = aidobj->priv;
			PRINT_ONE(aidobj->priv->dev->name, "    Interface: %s\n" , 0);
		}
		PRINT_ARRAY_ARG("    MacAddr: ", pEntry->MacAddr, "%02x", MACADDRLEN);
		PRINT_SINGL_ARG("    MacId: ", pEntry->MacId, "%d");
		PRINT_SINGL_ARG("    pAID: ", pEntry->P_AID, "%d");
		PRINT_SINGL_ARG("    bSound: ", pEntry->bSound, "%x");
		PRINT_SINGL_ARG("    BeamformEntryCap: ", pEntry->BeamformEntryCap, "%x");
		#if (MU_BEAMFORMING_SUPPORT == 1)
		if(priv->pmib->dot11RFEntry.txbf_mu) {
			PRINT_SINGL_ARG("    is_mu_sta: ", pEntry->is_mu_sta, "%d");
			if(pEntry->is_mu_sta) {
				PRINT_SINGL_ARG("    mu_reg_index: ", pEntry->mu_reg_index, "%d");
			} else {
				PRINT_SINGL_ARG("    su_reg_index: ", pEntry->su_reg_index, "%d");
			}
		}
		else
		#endif
		{
			PRINT_SINGL_ARG("    su_reg_index: ", pEntry->su_reg_index, "%d");
		}
	}
	PRINT_ONE("  TXBFer Entry...", "%s", 1);
	for(idx = 0; idx < BEAMFORMER_ENTRY_NUM; idx++) {
		pTXBFerEntry = &(pBeamInfo->BeamformerEntry[idx]);
		PRINT_ONE(idx,  " %d: txbfer entry...", 1);
		PRINT_SINGL_ARG("    bUsed: ", pTXBFerEntry->bUsed, "%x");		
		PRINT_ARRAY_ARG("    MacAddr: ", pTXBFerEntry->MacAddr, "%02x", MACADDRLEN);
		PRINT_SINGL_ARG("    BeamformEntryCap: ", pTXBFerEntry->BeamformEntryCap, "%x");	
		#if (MU_BEAMFORMING_SUPPORT == 1)
		if(priv->pmib->dot11RFEntry.txbf_mu) {
			PRINT_SINGL_ARG("    is_mu_ap: ", pTXBFerEntry->is_mu_ap, "%x");
			if(!pTXBFerEntry->is_mu_ap) {
				PRINT_SINGL_ARG("    su_reg_index: ", pTXBFerEntry->su_reg_index, "%d");
			}
		}
		else
		#endif
		{
			PRINT_SINGL_ARG("    su_reg_index: ", pTXBFerEntry->su_reg_index, "%d");
		}
	}	
	return pos;
}
#endif

#ifdef DFS
struct rtk_timer_list *get_channel_timer(struct rtl8192cd_priv *priv, int channel)
{
	switch(channel) {
		case 52:
			return &priv->ch52_timer;
			break;
		case 56:
			return &priv->ch56_timer;
			break;
		case 60:
			return &priv->ch60_timer;
			break;
		case 64:
			return &priv->ch64_timer;
			break;
		case 100:
			return &priv->ch100_timer;
			break;
		case 104:
			return &priv->ch104_timer;
			break;
		case 108:
			return &priv->ch108_timer;
			break;
		case 112:
			return &priv->ch112_timer;
			break;
		case 116:
			return &priv->ch116_timer;
			break;
		case 120:
			return &priv->ch120_timer;
			break;
		case 124:
			return &priv->ch124_timer;
			break;
		case 128:
			return &priv->ch128_timer;
			break;
		case 132:
			return &priv->ch132_timer;
			break;
		case 136:
			return &priv->ch136_timer;
			break;
		case 140:
			return &priv->ch140_timer;
			break;
		case 144:
			return &priv->ch144_timer;
			break;
		case 149:
			if(priv->pmib->dot11DFSEntry.band4_DFS_en)
				return &priv->ch149_timer;
			else
				printk("DFS_timer: Channel match none!\n");
			break;
		case 153:
			if(priv->pmib->dot11DFSEntry.band4_DFS_en)
				return &priv->ch153_timer;
			else
				printk("DFS_timer: Channel match none!\n");
			break;
		case 157:
			if(priv->pmib->dot11DFSEntry.band4_DFS_en)
				return &priv->ch157_timer;
			else
				printk("DFS_timer: Channel match none!\n");
			break;
		case 161:
			if(priv->pmib->dot11DFSEntry.band4_DFS_en)
				return &priv->ch161_timer;
			else
				printk("DFS_timer: Channel match none!\n");
			break;
		case 165:
			if(priv->pmib->dot11DFSEntry.band4_DFS_en)
				return &priv->ch165_timer;
			else
				printk("DFS_timer: Channel match none!\n");
			break;
		default:
			printk("DFS_timer: Channel match none!\n");
			return NULL;
			break;
	}

	return NULL;
}


#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_DFS(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_DFS(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	int i;
	struct rtk_timer_list *ch_timer;
	unsigned long rem_time;

	PRINT_ONE("  Dot11DFSEntry...", "%s", 1);

	PRINT_SINGL_ARG("    DFS version: ", get_DFS_version(), "%s");
	PRINT_SINGL_ARG("    disable_DFS: ", priv->pmib->dot11DFSEntry.disable_DFS, "%d");
	PRINT_SINGL_ARG("    DFS_timeout: ", priv->pmib->dot11DFSEntry.DFS_timeout, "%d");
	PRINT_SINGL_ARG("    DFS_detected: ", priv->pmib->dot11DFSEntry.DFS_detected, "%d");
	PRINT_SINGL_ARG("    NOP_timeout: ", priv->pmib->dot11DFSEntry.NOP_timeout, "%d");
	PRINT_SINGL_ARG("    DFS_TXPAUSE_timeout: ", priv->pmib->dot11DFSEntry.DFS_TXPAUSE_timeout, "%d");	
	PRINT_SINGL_ARG("    disable_tx: ", priv->pmib->dot11DFSEntry.disable_tx, "%d");
	PRINT_SINGL_ARG("    CAC_enable: ", priv->pmib->dot11DFSEntry.CAC_enable, "%d");

	if(priv->NOP_chnl_num != 0) {
		PRINT_ONE("    NOP channels...", "%s", 1);
		PRINT_ONE("      ", "%s", 0);
		for (i=0; i<priv->NOP_chnl_num; i++) {
			PRINT_ONE(priv->NOP_chnl[i], "[%d]:", 0);
			ch_timer = get_channel_timer(priv, priv->NOP_chnl[i]);

			if (ch_timer) {
#ifdef __KERNEL__
				rem_time = RTL_JIFFIES_TO_SECOND(TSF_DIFF(rtk_timer_get_expires(ch_timer), jiffies));
#else
				rem_time = RTL_JIFFIES_TO_SECOND(TSF_DIFF(ch_timer->e.delta, jiffies));
#endif
				if (rem_time > 0) {
					PRINT_ONE(rem_time, "%dsec ", 0);
				}
				else {
					PRINT_ONE("< 1sec ", "%s" , 0);
				}
			}
		}
		PRINT_ONE(" ", "%s", 1);
	}
	
	if(priv->available_chnl_num) {
  		PRINT_ARRAY_ARG("    AVAIL_CH: ", priv->available_chnl, "%d ", priv->available_chnl_num);
 	} else {
  		PRINT_ONE("  AVAIL_CH: None", "%s", 1);
 	}
	
	return pos;
}
#endif


#ifdef RTK_AC_SUPPORT
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_rf_ac(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_rf_ac(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  Dot11ACEntry...", "%s", 1);
	
	PRINT_ARRAY_ARG("    pwrdiff_5G_20BW1S_OFDM1T_A: ", priv->pmib->dot11RFEntry.pwrdiff_5G_20BW1S_OFDM1T_A, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_40BW2S_20BW2S_A: ", priv->pmib->dot11RFEntry.pwrdiff_5G_40BW2S_20BW2S_A, "%02x", MAX_5G_CHANNEL_NUM);
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)
	PRINT_ARRAY_ARG("    pwrdiff_5G_40BW3S_20BW3S_A: ", priv->pmib->dot11RFEntry.pwrdiff_5G_40BW3S_20BW3S_A, "%02x", MAX_5G_CHANNEL_NUM);
#endif
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW1S_160BW1S_A: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW1S_160BW1S_A, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW2S_160BW2S_A: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW2S_160BW2S_A, "%02x", MAX_5G_CHANNEL_NUM);
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW3S_160BW3S_A: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW3S_160BW3S_A, "%02x", MAX_5G_CHANNEL_NUM);
#endif

	
	PRINT_ARRAY_ARG("    pwrdiff_5G_20BW1S_OFDM1T_B: ", priv->pmib->dot11RFEntry.pwrdiff_5G_20BW1S_OFDM1T_B, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_40BW2S_20BW2S_B: ", priv->pmib->dot11RFEntry.pwrdiff_5G_40BW2S_20BW2S_B, "%02x", MAX_5G_CHANNEL_NUM);
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)	
	PRINT_ARRAY_ARG("    pwrdiff_5G_40BW3S_20BW3S_B: ", priv->pmib->dot11RFEntry.pwrdiff_5G_40BW3S_20BW3S_B, "%02x", MAX_5G_CHANNEL_NUM);
#endif

	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW1S_160BW1S_B: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW1S_160BW1S_B, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW2S_160BW2S_B: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW2S_160BW2S_B, "%02x", MAX_5G_CHANNEL_NUM);
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)	
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW3S_160BW3S_B: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW3S_160BW3S_B, "%02x", MAX_5G_CHANNEL_NUM);
#endif

#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)	
	PRINT_ARRAY_ARG("    pwrdiff_5G_20BW1S_OFDM1T_C: ", priv->pmib->dot11RFEntry.pwrdiff_5G_20BW1S_OFDM1T_C, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_40BW2S_20BW2S_C: ", priv->pmib->dot11RFEntry.pwrdiff_5G_40BW2S_20BW2S_C, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_40BW3S_20BW3S_C: ", priv->pmib->dot11RFEntry.pwrdiff_5G_40BW3S_20BW3S_C, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW1S_160BW1S_C: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW1S_160BW1S_C, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW2S_160BW2S_C: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW2S_160BW2S_C, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW3S_160BW3S_C: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW3S_160BW3S_C, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_20BW1S_OFDM1T_D: ", priv->pmib->dot11RFEntry.pwrdiff_5G_20BW1S_OFDM1T_D, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_40BW2S_20BW2S_D: ", priv->pmib->dot11RFEntry.pwrdiff_5G_40BW2S_20BW2S_D, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_40BW3S_20BW3S_D: ", priv->pmib->dot11RFEntry.pwrdiff_5G_40BW3S_20BW3S_D, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW1S_160BW1S_D: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW1S_160BW1S_D, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW2S_160BW2S_D: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW2S_160BW2S_D, "%02x", MAX_5G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_5G_80BW3S_160BW3S_D: ", priv->pmib->dot11RFEntry.pwrdiff_5G_80BW3S_160BW3S_D, "%02x", MAX_5G_CHANNEL_NUM);
#endif
	
	PRINT_ARRAY_ARG("    pwrdiff_20BW1S_OFDM1T_A: ", priv->pmib->dot11RFEntry.pwrdiff_20BW1S_OFDM1T_A, "%02x", MAX_2G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_40BW2S_20BW2S_A: ", priv->pmib->dot11RFEntry.pwrdiff_40BW2S_20BW2S_A, "%02x", MAX_2G_CHANNEL_NUM);
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)	
	PRINT_ARRAY_ARG("    pwrdiff_40BW3S_20BW3S_A: ", priv->pmib->dot11RFEntry.pwrdiff_40BW3S_20BW3S_A, "%02x", MAX_2G_CHANNEL_NUM);
#endif
	PRINT_ARRAY_ARG("    pwrdiff_20BW1S_OFDM1T_B: ", priv->pmib->dot11RFEntry.pwrdiff_20BW1S_OFDM1T_B, "%02x", MAX_2G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_40BW2S_20BW2S_B: ", priv->pmib->dot11RFEntry.pwrdiff_40BW2S_20BW2S_B, "%02x", MAX_2G_CHANNEL_NUM);
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)	
	PRINT_ARRAY_ARG("    pwrdiff_40BW3S_20BW3S_B: ", priv->pmib->dot11RFEntry.pwrdiff_40BW3S_20BW3S_B, "%02x", MAX_2G_CHANNEL_NUM);
#endif
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)	
	PRINT_ARRAY_ARG("    pwrdiff_20BW1S_OFDM1T_C: ", priv->pmib->dot11RFEntry.pwrdiff_20BW1S_OFDM1T_C, "%02x", MAX_2G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_40BW2S_20BW2S_C: ", priv->pmib->dot11RFEntry.pwrdiff_40BW2S_20BW2S_C, "%02x", MAX_2G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_40BW3S_20BW3S_C: ", priv->pmib->dot11RFEntry.pwrdiff_40BW3S_20BW3S_C, "%02x", MAX_2G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_20BW1S_OFDM1T_D: ", priv->pmib->dot11RFEntry.pwrdiff_20BW1S_OFDM1T_D, "%02x", MAX_2G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_40BW2S_20BW2S_D: ", priv->pmib->dot11RFEntry.pwrdiff_40BW2S_20BW2S_D, "%02x", MAX_2G_CHANNEL_NUM);
	PRINT_ARRAY_ARG("    pwrdiff_40BW3S_20BW3S_D: ", priv->pmib->dot11RFEntry.pwrdiff_40BW3S_20BW3S_D, "%02x", MAX_2G_CHANNEL_NUM);	
#endif

	return pos;
}
#endif

#ifdef BT_COEXIST
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_bt_coexist(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_bt_coexist(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	extern int c2h_bt_cnt;
	extern int bt_state;

	PRINT_ONE("  BT Coexist Info:", "%s", 1);	
	PRINT_SINGL_ARG("    btc            : ", priv->pshare->rf_ft_var.btc, "%u");
	PRINT_SINGL_ARG("    bt_state       : ", bt_state, "%d");
	PRINT_SINGL_ARG("    c2h_bt_cnt     : ", c2h_bt_cnt, "%d");
	PRINT_SINGL_ARG("    0x40           : ", phy_query_bb_reg(priv, 0x40, 0xffffffff), "%x");	
	PRINT_SINGL_ARG("    0x4f           : ", phy_query_bb_reg(priv, 0x4c,  0xff000000), "%x");	
	PRINT_SINGL_ARG("    0x522          : ", phy_query_bb_reg(priv, 0x520, 0x00ff0000), "%x");	
	PRINT_SINGL_ARG("    0x550          : ", phy_query_bb_reg(priv, 0x550, 0xffffffff), "%x");	
	PRINT_SINGL_ARG("    0x6c0   BT Slot: ", phy_query_bb_reg(priv, 0x6c0, 0xffffffff), "%x");	
	PRINT_SINGL_ARG("    0x6c4 Wifi Slot: ", phy_query_bb_reg(priv, 0x6c4, 0xffffffff), "%x");	
	PRINT_SINGL_ARG("    0x6c8          : ", phy_query_bb_reg(priv, 0x6c8, 0xffffffff), "%x");	
	PRINT_SINGL_ARG("    0x770   (hp rx): ", phy_query_bb_reg(priv, 0x770, 0xffff0000), "%d");	
	PRINT_SINGL_ARG("    0x770   (hp tx): ", phy_query_bb_reg(priv, 0x770, 0x0000ffff), "%d");	
	PRINT_SINGL_ARG("    0x774   (lp rx): ", phy_query_bb_reg(priv, 0x774, 0xffff0000), "%d");	
	PRINT_SINGL_ARG("    0x774   (lp tx): ", phy_query_bb_reg(priv, 0x774, 0x0000ffff), "%d");	
	PRINT_SINGL_ARG("    0x778          : ", phy_query_bb_reg(priv, 0x778, 0xffffffff), "%x");	
	PRINT_SINGL_ARG("    0x92c BT/Wifi Switch: ", phy_query_bb_reg(priv, 0x92c, 0xffffffff), "%x");	
	PRINT_SINGL_ARG("    0x930  HW/SW Control: ", phy_query_bb_reg(priv, 0x930, 0xffffffff), "%x");	
	PRINT_SINGL_ARG("    0xc50          : ", phy_query_bb_reg(priv, 0xc50, 0xffffffff), "%x");	
	PRINT_SINGL_ARG("    0xc58          : ", phy_query_bb_reg(priv, 0xc58, 0xffffffff), "%x");	

	return pos;
}
#endif

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_wmm(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_wmm(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	int rf_path;
#ifdef __ECOS
	char tmpbuf[16];
#else
	unsigned char tmpbuf[16];
#endif
	panic_printk("        Cwin	Cmax  AIFS	TXOP\n");
	panic_printk("    BK  %3d	%3d   %3d	%3d (%x)\n",phy_query_bb_reg(priv, 0x50c, 0xf00),phy_query_bb_reg(priv, 0x50c, 0xf000),phy_query_bb_reg(priv, 0x50c, bMaskByte0),phy_query_bb_reg(priv, 0x50c, 0x7ff0000),phy_query_bb_reg(priv, 0x50c, bMaskDWord));
	panic_printk("    BE  %3d	%3d   %3d	%3d (%x)\n",phy_query_bb_reg(priv, 0x508, 0xf00),phy_query_bb_reg(priv, 0x508, 0xf000),phy_query_bb_reg(priv, 0x508, bMaskByte0),phy_query_bb_reg(priv, 0x508, 0x7ff0000),phy_query_bb_reg(priv, 0x508, bMaskDWord));
	panic_printk("    VI  %3d	%3d   %3d	%3d (%x)\n",phy_query_bb_reg(priv, 0x504, 0xf00),phy_query_bb_reg(priv, 0x504, 0xf000),phy_query_bb_reg(priv, 0x504, bMaskByte0),phy_query_bb_reg(priv, 0x504, 0x7ff0000),phy_query_bb_reg(priv, 0x504, bMaskDWord));
	panic_printk("    VO  %3d	%3d   %3d	%3d (%x)\n\n",phy_query_bb_reg(priv, 0x500, 0xf00),phy_query_bb_reg(priv, 0x500, 0xf000),phy_query_bb_reg(priv, 0x500, bMaskByte0),phy_query_bb_reg(priv, 0x500, 0x7ff0000),phy_query_bb_reg(priv, 0x500, bMaskDWord));

#if defined(CONFIG_WLAN_HAL_8197F) || defined(CONFIG_WLAN_HAL_8198F)
	PRINT_SINGL_ARG("    High queue page count 0x230: ", phy_query_bb_reg(priv, 0x230, bMaskDWord), "%x");
	PRINT_SINGL_ARG("    Low queue page count 0x234: ", phy_query_bb_reg(priv, 0x234, bMaskDWord), "%x");
	PRINT_SINGL_ARG("    Normal queue page count 0x238: ", phy_query_bb_reg(priv, 0x238, bMaskDWord), "%x");
	PRINT_SINGL_ARG("    Extra queue page count 0x23c: ", phy_query_bb_reg(priv, 0x23c, bMaskDWord), "%x");
	PRINT_SINGL_ARG("    Public queue page count 0x240: ", phy_query_bb_reg(priv, 0x240, bMaskDWord), "%x");
	PRINT_SINGL_ARG("    Queue mapping 0x10c: ", phy_query_bb_reg(priv, 0x10c, bMaskDWord), "%x");
#endif
	
#ifdef WIFI_WMM
	PRINT_SINGL_ARG("    VO_pkt_count: ",priv->pshare->phw->VO_pkt_count, "%u");
	PRINT_SINGL_ARG("    VI_pkt_count: ",priv->pshare->phw->VI_pkt_count, "%u");
	PRINT_SINGL_ARG("    BE_pkt_count: ",priv->pshare->phw->BE_pkt_count, "%u");
	PRINT_SINGL_ARG("    BK_pkt_count: ",priv->pshare->phw->BK_pkt_count, "%u");

	PRINT_SINGL_ARG("    iot_mode_VO_exist: ",priv->pshare->iot_mode_VO_exist, "%u");
	PRINT_SINGL_ARG("    iot_mode_VI_exist: ",priv->pshare->iot_mode_VI_exist, "%u");
	PRINT_SINGL_ARG("    iot_mode_BK_exist: ",priv->pshare->iot_mode_BK_exist, "%u");
	PRINT_SINGL_ARG("    iot_mode_BE_exist: ",priv->pshare->iot_mode_BE_exist, "%u");

	PRINT_SINGL_ARG("    VI_rx_pkt_count: ",priv->pshare->phw->VI_rx_pkt_count, "%u");
	PRINT_SINGL_ARG("    wifi_beq_iot: ",priv->pshare->rf_ft_var.wifi_beq_iot, "%u");
	PRINT_SINGL_ARG("    BE_pkt_average: ",priv->pshare->phw->BE_pkt_average, "%u");
	PRINT_SINGL_ARG("    NonBE_pkt_average: ",priv->pshare->phw->NonBE_pkt_average, "%u");
#endif	
	PRINT_SINGL_ARG("    txop_enlarge: ",priv->pshare->txop_enlarge, "%u");
	PRINT_SINGL_ARG("    txop_enlarge_lower: ",priv->pshare->rf_ft_var.txop_enlarge_lower, "%u");
	PRINT_SINGL_ARG("    txop_enlarge_upper: ",priv->pshare->rf_ft_var.txop_enlarge_upper, "%u");
#ifdef LOW_TP_TXOP	
	PRINT_SINGL_ARG("    BE_cwmax_enhance: ",priv->pshare->BE_cwmax_enhance, "%u");
#endif
	PRINT_SINGL_ARG("    wifi_specific: ",priv->pmib->dot11OperationEntry.wifi_specific, "%u");

	PRINT_SINGL_ARG("    VO drop: ", priv->pshare->phw->VO_droppkt_count, "%d");
	PRINT_SINGL_ARG("    VI drop: ", priv->pshare->phw->VI_droppkt_count, "%d");
	PRINT_SINGL_ARG("    BE drop: ", priv->pshare->phw->BE_droppkt_count, "%d");
	PRINT_SINGL_ARG("    BK drop: ", priv->pshare->phw->BK_droppkt_count, "%d");
	return pos;
}

#ifdef IDLE_NOISE_LEVEL
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_noise_level(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_noise_level(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	struct dm_struct *dm = ODMPTR;
	int pos = 0;
	char tmpstr[32];
	
	signed short noise_level=0;

	if (!(priv->drv_state & DRV_STATE_OPEN))
		return 0;

	if(IS_OUTSRC_CHIP(priv)) {
		memset(tmpstr,0,32);
		noise_level = phydm_cmn_info_query(dm, PHYDM_INFO_NHM_PWR) - 100;
		sprintf(tmpstr,"[%d]",noise_level);
		//GDEBUG("noise_level=[%d]\n",noise_level);
		PRINT_SINGL_ARG("noise level:", tmpstr, "%s");	
	}

	return pos;
}
#endif

/* Phoebus: hardware state dump for the 2.4GHz (8192F) RX investigation.
 *
 * wlan1 beacons happily -- beacon_ok climbs, hostapd reports AP-ENABLED on
 * ch6/2437 -- while every receive counter stays at exactly zero: rx_packets,
 * rx_mgnt_pkts, rx_crc_errors, and hostapd's olbc. Sweeping rfe_type (1 and 3)
 * and toggling the eFuse read changed nothing, byte for byte, which says the
 * fault is upstream of every knob those touch.
 *
 * Rather than keep inferring hardware state from counters, read the registers.
 * On demand rather than once at open() so the same dump can be taken before and
 * after hostapd sets the channel:
 *   cat /proc/wlan1/phoebus_rf
 *
 * What each line is for:
 *   RCR       receive filter. Wants AB|AM|APM|AAP (0x0f000000-ish) and ACRC32.
 *             If the accept bits are clear the MAC drops every frame and all
 *             four counters read zero exactly as observed.
 *   CR        MAC function enable -- RXDMA/MACRX must be on.
 *   HIMR0/1   interrupt masks. The beacon ISR demonstrably runs, but that only
 *             proves the beacon bit is unmasked, not the RX-OK bit.
 *   HISR0/1   pending status; RX bits stuck set means nobody serviced them.
 *   RXBD      RX descriptor ring base + count. A zero base is a dead ring.
 *   RF A/B 0  RF mode word. 0 means the synthesiser is powered down, which
 *             would make TX-completes-but-RX-hears-nothing a single fault.
 *   xcap      crystal load trim; never programmed in our build (the read is
 *             behind CONFIG_RTL_TRIBAND_SUPPORT, which we do not set).
 */
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_phoebus_rf(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_phoebus_rf(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	if (!(priv->drv_state & DRV_STATE_OPEN)) {
		PRINT_SINGL_ARG("state: ", "driver not open", "%s");
		return pos;
	}

	PRINT_SINGL_ARG("  chip_ver:   ", GET_CHIP_VER(priv), "%d");
	PRINT_SINGL_ARG("  channel:    ", priv->pmib->dot11RFEntry.dot11channel, "%d");
	PRINT_SINGL_ARG("  rfe_type:   ", priv->pmib->dot11RFEntry.rfe_type, "%u");
	PRINT_SINGL_ARG("  xcap:       ", priv->pmib->dot11RFEntry.xcap, "0x%02x");
	PRINT_SINGL_ARG("  share_xcap: ", priv->pmib->dot11RFEntry.share_xcap, "%d");
	PRINT_SINGL_ARG("  ther:       ", priv->pmib->dot11RFEntry.ther, "0x%02x");
	PRINT_SINGL_ARG("  efuse_en:   ", priv->pmib->efuseEntry.enable_efuse, "%d");
	/* EN_EFUSE is derived from CONFIG_SLOT_n_ENABLE_EFUSE (8192cd_cfg.h:779),
	 * so with those off the whole eFuse subsystem -- including this flag and
	 * the crystal/TX-power/thermal reads -- is compiled out, not merely idle. */
#ifdef EN_EFUSE
	PRINT_SINGL_ARG("  autoloadfail:", priv->AutoloadFailFlag, "%d");
#else
	PRINT_SINGL_ARG("  efuse subsys:", "compiled out (EN_EFUSE unset)", "%s");
#endif

	/* Where does calibration actually come from?
	 *
	 * Per the RTL8192CE datasheet §5.6, pin 14 (EEPROMSEL) is a power-on strap:
	 * high = external 93C46 EEPROM, low = internal eFuse. The chip latches it
	 * into 9346CR, so this register is the board telling us where its MAC,
	 * xcap and TX power really live. We have only ever read the eFuse, and it
	 * comes back blank -- if this says EEPROM, that is why. */
	PRINT_SINGL_ARG("  9346CR(00A):", (unsigned int)RTL_R16(0x000A), "0x%04x");
	/* Bit values per 8812_reg.h:891-892 (CmdEEPROM_En / CmdEERPOMSEL). Spelled
	 * out here rather than using those names: they live in a chip header that
	 * is not on this file's include path. */
	PRINT_SINGL_ARG("   -> boot:   ",
			(RTL_R16(0x000A) & 0x0010) ? "EEPROM(93C46)" : "eFuse", "%s");
	PRINT_SINGL_ARG("   -> ee_en:  ",
			(RTL_R16(0x000A) & 0x0020) ? "yes" : "no", "%s");

	/* RF control pins are physically shared with the LED pins (datasheet
	 * Tables 4 and 5): ANT_SEL_P/N share LED0/LED1, and TRSWN[0] -- the
	 * transmit/receive path select -- shares LED2/GPIO8. Which function a pin
	 * drives is chosen by these control registers. If they are left in LED
	 * mode, the T/R switch is never driven, the antenna stays parked on the
	 * transmit path, and the result is exactly what wlan1 shows: beacons go
	 * out and the receiver hears nothing at all. */
	PRINT_SINGL_ARG("  GPIO_MUX(40):", (unsigned int)RTL_R32(0x0040), "0x%08x");
	PRINT_SINGL_ARG("  GPIO_PIN(44):", (unsigned int)RTL_R32(0x0044), "0x%08x");
	PRINT_SINGL_ARG("  LEDCFG0(4C):", (unsigned int)RTL_R8(0x004C), "0x%02x");
	PRINT_SINGL_ARG("  LEDCFG1(4D):", (unsigned int)RTL_R8(0x004D), "0x%02x");
	PRINT_SINGL_ARG("  LEDCFG2(4E):", (unsigned int)RTL_R8(0x004E), "0x%02x");

	/* MAC: receive filter and function enables */
	PRINT_SINGL_ARG("  RCR(0x608): ", (unsigned int)RTL_R32(0x0608), "0x%08x");
	PRINT_SINGL_ARG("  CR(0x100):  ", (unsigned int)RTL_R32(0x0100), "0x%08x");
	PRINT_SINGL_ARG("  SYSFEN(02): ", (unsigned int)RTL_R16(0x0002), "0x%04x");

	/* Interrupts: is the RX-OK bit even unmasked? */
	PRINT_SINGL_ARG("  HIMR0(0B0): ", (unsigned int)RTL_R32(0x00B0), "0x%08x");
	PRINT_SINGL_ARG("  HISR0(0B4): ", (unsigned int)RTL_R32(0x00B4), "0x%08x");
	PRINT_SINGL_ARG("  HIMR1(0B8): ", (unsigned int)RTL_R32(0x00B8), "0x%08x");
	PRINT_SINGL_ARG("  HISR1(0BC): ", (unsigned int)RTL_R32(0x00BC), "0x%08x");

	/* RX descriptor ring */
	PRINT_SINGL_ARG("  RXBD_DESA:  ", (unsigned int)RTL_R32(0x0338), "0x%08x");
	PRINT_SINGL_ARG("  RXBD_NUM:   ", (unsigned int)RTL_R16(0x0382), "0x%04x");

	/* RF synthesiser state -- 0 here means the radio is powered down */
	PRINT_SINGL_ARG("  RF_A_0x00:  ",
			phy_query_rf_reg(priv, RF_PATH_A, 0x00, 0xfffff, 1), "0x%05x");
	PRINT_SINGL_ARG("  RF_B_0x00:  ",
			phy_query_rf_reg(priv, RF_PATH_B, 0x00, 0xfffff, 1), "0x%05x");
	PRINT_SINGL_ARG("  RF_A_0x18:  ",
			phy_query_rf_reg(priv, RF_PATH_A, 0x18, 0xfffff, 1), "0x%05x");

	/* BB: 0x800 is the system control word, 0x808 carries the RX path enables */
	PRINT_SINGL_ARG("  BB_0x800:   ", phy_query_bb_reg(priv, 0x800, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x808:   ", phy_query_bb_reg(priv, 0x808, 0xffffffff), "0x%08x");

	/* RFE pin-control block. phydm_init_hw_info_by_rfe_type_8192f() programs
	 * these per rfe_type -- 0x944 selects which front-end pins are driven and
	 * 0x940 what they output, including the T/R switch. That function has no
	 * default branch, so for an rfe_type it does not recognise (1 and 3 among
	 * them) these keep whatever the BB table left behind. Dump them so the
	 * question "did the RFE config actually run" is answerable rather than
	 * inferred: rfe_type 23 should show 0x944 = ...81C and 0x940 = 0x00400100,
	 * where 17/21/22 would give ...81F and 0x00400104. */
	PRINT_SINGL_ARG("  BB_0x92c:   ", phy_query_bb_reg(priv, 0x92c, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x930:   ", phy_query_bb_reg(priv, 0x930, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x938:   ", phy_query_bb_reg(priv, 0x938, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x93c:   ", phy_query_bb_reg(priv, 0x93c, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x940:   ", phy_query_bb_reg(priv, 0x940, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x944:   ", phy_query_bb_reg(priv, 0x944, 0xffffffff), "0x%08x");

	/* RX path enables -- the registers that decide whether the receiver has
	 * any antenna path switched on at all.
	 *
	 * phydm_config_ofdm_rx_path() (phydm_api.c:226) writes, for 8192E/8192F:
	 *     0xc04 bits[7:0] = (val << 4) | val
	 *     0xd04 bits[3:0] = val
	 * where val is 1 = path A, 2 = path B, 3 = both. So a healthy 2x2 setup
	 * reads 0xc04 low byte = 0x33 and 0xd04 low nibble = 0x3. Zero in either
	 * means that demodulator has no path enabled and will deliver nothing --
	 * which looks identical from userspace to a dead radio.
	 * 0x804/0x824/0x82c are the RF mode table, 0x90c the TX path mapping. */
	PRINT_SINGL_ARG("  BB_0xc04:   ", phy_query_bb_reg(priv, 0xc04, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0xd04:   ", phy_query_bb_reg(priv, 0xd04, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x804:   ", phy_query_bb_reg(priv, 0x804, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x824:   ", phy_query_bb_reg(priv, 0x824, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x82c:   ", phy_query_bb_reg(priv, 0x82c, 0xffffffff), "0x%08x");
	PRINT_SINGL_ARG("  BB_0x90c:   ", phy_query_bb_reg(priv, 0x90c, 0xffffffff), "0x%08x");

	/* RCR bit8 (BIT_ACRC32, halmac_bit_8822b.h:13290) gates whether frames
	 * that fail FCS are handed up at all. It reads back CLEAR on this board,
	 * so rx_crc_errors staying at 0 says nothing about whether the receiver
	 * hears anything -- errored frames are dropped in the MAC before any
	 * counter sees them. rx_packets and rx_mgnt_pkts are the honest signals. */
	PRINT_SINGL_ARG("  RCR ACRC32: ",
			(RTL_R32(0x0608) & 0x00000100) ? "set (CRC errs counted)"
						       : "CLEAR (CRC errs invisible)", "%s");

	return pos;
}

/* Phoebus: live register poke, companion to phoebus_rf above.
 *
 * The board can only be reflashed by taking the single network interface away
 * from the internet and pointing it at TFTP, so a debugging session gets one
 * image and no second chance. Reading registers alone would mean one datapoint
 * per flash. This lets a candidate fix be tried in seconds instead:
 *
 *   echo "bb 0xc04 0xff 0x33"    > /proc/wlan1/phoebus_poke
 *   echo "rf 0 0x00 0xfffff 0x33e70" > /proc/wlan1/phoebus_poke
 *   echo "m32 0x608 0xf118230e"  > /proc/wlan1/phoebus_poke
 *
 * Deliberately has no eFuse path of any kind. The eFuse is one-time
 * programmable and a wrong write is unrecoverable, so it is not reachable from
 * here at all rather than being guarded by a check that could be got wrong.
 * Everything this can touch is volatile: a bad poke is cleared by a power
 * cycle, and the rootfs is a RAM initramfs so nothing persists either way.
 */
#ifdef __ECOS
void rtl8192cd_proc_phoebus_poke(void *data)
#else
static int rtl8192cd_proc_phoebus_poke(struct file *file, const char *buffer,
				unsigned long count, void *data)
#endif
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	char buf[80], kind[8];
	unsigned int a = 0, b = 0, c = 0, d = 0;
	int n;

	if (count == 0 || count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	n = sscanf(buf, "%7s %i %i %i %i", kind, &a, &b, &c, &d);
	if (n < 2)
		goto usage;

	if (!strcmp(kind, "bb") && n >= 4) {
		phy_set_bb_reg(priv, a, b, c);
		printk("PHOEBUS-POKE: bb[0x%03x] mask 0x%08x <- 0x%x  (now 0x%08x)\n",
		       a, b, c, phy_query_bb_reg(priv, a, 0xffffffff));
	} else if (!strcmp(kind, "rf") && n >= 5) {
		phy_set_rf_reg(priv, a, b, c, d);
		printk("PHOEBUS-POKE: rf[path %u][0x%02x] mask 0x%05x <- 0x%x  (now 0x%05x)\n",
		       a, b, c, phy_query_rf_reg(priv, a, b, 0xfffff, 1));
	} else if (!strcmp(kind, "m32") && n >= 3) {
		RTL_W32(a, b);
		printk("PHOEBUS-POKE: mac32[0x%03x] <- 0x%08x  (now 0x%08x)\n",
		       a, b, (unsigned int)RTL_R32(a));
	} else if (!strcmp(kind, "m8") && n >= 3) {
		RTL_W8(a, b);
		printk("PHOEBUS-POKE: mac8[0x%03x] <- 0x%02x  (now 0x%02x)\n",
		       a, b, (unsigned int)RTL_R8(a));
	} else {
		goto usage;
	}
	return count;

usage:
	printk("PHOEBUS-POKE usage:\n"
	       "  bb  <addr> <mask> <val>\n"
	       "  rf  <path> <addr> <mask> <val>\n"
	       "  m32 <addr> <val>\n"
	       "  m8  <addr> <val>\n");
	return count;
}

static void _getChipVersion(struct rtl8192cd_priv *priv, unsigned char *tmpbuf, unsigned int tmpbuf_size)
{
	char chip_ver = '\0';
	memset(tmpbuf, 0, tmpbuf_size);

#ifdef CONFIG_RTL_92C_SUPPORT
	if (GET_CHIP_VER(priv) == VERSION_8188C) {
		if (IS_UMC_B_CUT_88C(priv))
			snprintf(tmpbuf, tmpbuf_size, "RTL6195B");
		else if (IS_88RE(priv))
			snprintf(tmpbuf, tmpbuf_size, "RTL8188R");
		else
			snprintf(tmpbuf, tmpbuf_size, "RTL8188C");
	} else if (GET_CHIP_VER(priv) == VERSION_8192C) {
		snprintf(tmpbuf, tmpbuf_size, "RTL8192C");
	}

	if ((GET_CHIP_VER(priv) == VERSION_8192C) ||
			(GET_CHIP_VER(priv) == VERSION_8188C)) {
		if (IS_TEST_CHIP(priv))
			strncat(tmpbuf, "t", (tmpbuf_size - strlen(tmpbuf) - 1));
		else
			strncat(tmpbuf, "n", (tmpbuf_size - strlen(tmpbuf) - 1));
	}
#endif /* CONFIG_RTL_92C_SUPPORT */

#ifdef CONFIG_RTL_92D_SUPPORT
	if (GET_CHIP_VER(priv) == VERSION_8192D) {
		snprintf(tmpbuf, tmpbuf_size, "RTL8192D");
	}
#endif

#ifdef CONFIG_RTL_88E_SUPPORT
	if (GET_CHIP_VER(priv) == VERSION_8188E) {
		snprintf(tmpbuf, tmpbuf_size, "RTL8188E");
#ifdef SUPPORT_RTL8188E_TC
		if (IS_TEST_CHIP(priv))
			strncat(tmpbuf, "t", (tmpbuf_size - strlen(tmpbuf) - 1));
#endif
	}
#endif /* CONFIG_RTL_88E_SUPPORT */

#ifdef CONFIG_RTL_8723B_SUPPORT
	if (GET_CHIP_VER(priv) == VERSION_8723B) {
		snprintf(tmpbuf, tmpbuf_size, "RTL8188E");	// for MP tool use
	}
#endif

#ifdef CONFIG_RTL_8812_SUPPORT
	if (GET_CHIP_VER(priv) == VERSION_8812E) {
#ifdef CONFIG_RTL_8812AR_VN_SUPPORT
		snprintf(tmpbuf, tmpbuf_size, "RTL8812AR-VN");
#else
		snprintf(tmpbuf, tmpbuf_size, "RTL8812");
#endif
		if (IS_TEST_CHIP(priv))
			strncat(tmpbuf, "t", (tmpbuf_size - strlen(tmpbuf) - 1));
		else if (IS_C_CUT_8812(priv))
			strncat(tmpbuf, "c", (tmpbuf_size - strlen(tmpbuf) - 1));
	}
#endif /* CONFIG_RTL_8812_SUPPORT */

#ifdef CONFIG_WLAN_HAL_8881A
	if (GET_CHIP_VER(priv) == VERSION_8881A) {
		snprintf(tmpbuf, tmpbuf_size, "RTL8881A");

		if (get_bonding_type_8881A() == BOND_8881AB)
			strncat(tmpbuf, "B", (tmpbuf_size - strlen(tmpbuf) - 1));
		else if (get_bonding_type_8881A() == BOND_8881AM)
			strncat(tmpbuf, "M", (tmpbuf_size - strlen(tmpbuf) - 1));
		else if (get_bonding_type_8881A() == BOND_8881AQ)
			strncat(tmpbuf, "Q", (tmpbuf_size - strlen(tmpbuf) - 1));
		else if (get_bonding_type_8881A() == BOND_8881AN)
			strncat(tmpbuf, "N", (tmpbuf_size - strlen(tmpbuf) - 1));
	}
#endif /* CONFIG_WLAN_HAL_8881A */

#ifdef CONFIG_WLAN_HAL_8192EE
	if (GET_CHIP_VER(priv) == VERSION_8192E) {
		if (_GET_HAL_DATA(priv)->bTestChip)  {
			snprintf(tmpbuf, tmpbuf_size, "RTL8192Et");
		} else {
			snprintf(tmpbuf, tmpbuf_size, "RTL8192En");
		}

		if(_GET_HAL_DATA(priv)->cutVersion == ODM_CUT_A)
			strncat(tmpbuf, "A", (tmpbuf_size - strlen(tmpbuf) - 1));
		else if(_GET_HAL_DATA(priv)->cutVersion == ODM_CUT_B)
			strncat(tmpbuf, "B", (tmpbuf_size - strlen(tmpbuf) - 1));
		else if(_GET_HAL_DATA(priv)->cutVersion == ODM_CUT_C)
			strncat(tmpbuf, "C", (tmpbuf_size - strlen(tmpbuf) - 1));		
		else if(_GET_HAL_DATA(priv)->cutVersion == ODM_CUT_D)
			strncat(tmpbuf, "D", (tmpbuf_size - strlen(tmpbuf) - 1));		
		else
			strncat(tmpbuf, "O", (tmpbuf_size - strlen(tmpbuf) - 1));	//others
	}
#endif /* CONFIG_WLAN_HAL_8192EE */

#ifdef CONFIG_WLAN_HAL_8814AE
	if (GET_CHIP_VER(priv) == VERSION_8814A) {
		if (IS_TEST_CHIP_8814(priv)) {
			snprintf(tmpbuf, tmpbuf_size, "RTL8814A-TC");
		} else {
			if (GET_CHIP_VER_8814(priv) == 0x0)
				snprintf(tmpbuf, tmpbuf_size, "RTL8814A-MP-A");
			else
				snprintf(tmpbuf, tmpbuf_size, "RTL8814A-MP-B");
		}
	}
#endif /* CONFIG_WLAN_HAL_8814AE */

#ifdef CONFIG_WLAN_HAL_8822BE
	if (GET_CHIP_VER(priv) == VERSION_8822B) {
		if (_GET_HAL_DATA(priv)->bTestChip)
			snprintf(tmpbuf, tmpbuf_size, "RTL8822B Test");
		else
			snprintf(tmpbuf, tmpbuf_size, "RTL8822B MP");
	}
#endif /* CONFIG_WLAN_HAL_8822BE */

#ifdef CONFIG_WLAN_HAL_8822CE
	if (GET_CHIP_VER(priv) == VERSION_8822C) {
		if (_GET_HAL_DATA(priv)->bTestChip)
			snprintf(tmpbuf, tmpbuf_size, "RTL8822C Test");
		else
			snprintf(tmpbuf, tmpbuf_size, "RTL8822C MP");
	}
#endif /* CONFIG_WLAN_HAL_8822CE */

#ifdef CONFIG_WLAN_HAL_8812FE
	if (GET_CHIP_VER(priv) == VERSION_8812F) {
		if (_GET_HAL_DATA(priv)->bTestChip)
			snprintf(tmpbuf, tmpbuf_size, "RTL8812F Test");
		else
			snprintf(tmpbuf, tmpbuf_size, "RTL8812F MP");
	}
#endif /* CONFIG_WLAN_HAL_8812FE */

#ifdef CONFIG_WLAN_HAL_8821CE
	if (GET_CHIP_VER(priv) == VERSION_8821C) {
		if (_GET_HAL_DATA(priv)->bTestChip)
			snprintf(tmpbuf, tmpbuf_size, "RTL8821C Test");
		else
			snprintf(tmpbuf, tmpbuf_size, "RTL8821C MP");
	}
#endif /* CONFIG_WLAN_HAL_8821CE */

#ifdef CONFIG_WLAN_HAL_8197F
	if (GET_CHIP_VER(priv) == VERSION_8197F) {
		if (_GET_HAL_DATA(priv)->bTestChip) {
			snprintf(tmpbuf, tmpbuf_size, "RTL8197F Test");
		} else {
			snprintf(tmpbuf, tmpbuf_size, "RTL8197F");
			extern unsigned int rtl819x_bond_option(void);
			if(rtl819x_bond_option() == 1)
				strncat(tmpbuf, "B", (tmpbuf_size - strlen(tmpbuf) - 1));
			else if(rtl819x_bond_option() == 2)
				strncat(tmpbuf, "N", (tmpbuf_size - strlen(tmpbuf) - 1));
			else if(rtl819x_bond_option() == 3)
				strncat(tmpbuf, "S", (tmpbuf_size - strlen(tmpbuf) - 1));
			strncat(tmpbuf, " MP", (tmpbuf_size - strlen(tmpbuf) - 1));

			if(_GET_HAL_DATA(priv)->cutVersion == ODM_CUT_A)
				strncat(tmpbuf, " A", (tmpbuf_size - strlen(tmpbuf) - 1));
			else if(_GET_HAL_DATA(priv)->cutVersion == ODM_CUT_B)
				strncat(tmpbuf, " B", (tmpbuf_size - strlen(tmpbuf) - 1));
			else if(_GET_HAL_DATA(priv)->cutVersion == ODM_CUT_C)
				strncat(tmpbuf, " C", (tmpbuf_size - strlen(tmpbuf) - 1));
			else if(_GET_HAL_DATA(priv)->cutVersion == ODM_CUT_D)
				strncat(tmpbuf, " D", (tmpbuf_size - strlen(tmpbuf) - 1));
			else
				strncat(tmpbuf, " O", (tmpbuf_size - strlen(tmpbuf) - 1));	//others
		}
	}
#endif /* CONFIG_WLAN_HAL_8197F */

#ifdef CONFIG_WLAN_HAL_8192FE
	if (GET_CHIP_VER(priv) == VERSION_8192F) {
		if(_GET_HAL_DATA(priv)->cutVersion < ODM_CUT_TEST)
			chip_ver = 'A' + _GET_HAL_DATA(priv)->cutVersion;
		else
			chip_ver = 'O';
		snprintf(tmpbuf, tmpbuf_size, "RTL8192Fn%c", chip_ver);
	}
#endif /* CONFIG_WLAN_HAL_8192FE */

#ifdef CONFIG_WLAN_HAL_8198F
	if (GET_CHIP_VER(priv) == VERSION_8198F) {
		if (_GET_HAL_DATA(priv)->bTestChip)
			sprintf(tmpbuf, "RTL8198F");
		else
			sprintf(tmpbuf, "RTL8198F Test");
	}
#endif /* CONFIG_WLAN_HAL_8198F */

#ifdef CONFIG_WLAN_HAL_8814BE
	if (GET_CHIP_VER(priv) == VERSION_8814B) {
		if (IS_TEST_CHIP_8814(priv)) {
			snprintf(tmpbuf, tmpbuf_size, "RTL8814B-TC");
		} else {
			if(_GET_HAL_DATA(priv)->cutVersion < ODM_CUT_TEST)
				chip_ver = 'A' + _GET_HAL_DATA(priv)->cutVersion;
			else
				chip_ver = 'X';
			snprintf(tmpbuf, tmpbuf_size, "RTL8814B-MP-%c", chip_ver);
		}
	}
#endif /* CONFIG_WLAN_HAL_8814BE */

	if (IS_UMC_A_CUT(priv))
		strncat(tmpbuf, "u", (tmpbuf_size - strlen(tmpbuf) - 1));
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_rf(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_rf(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	int rf_path;
#ifdef __ECOS
	char tmpbuf[16];
#else
	unsigned char tmpbuf[16];
#endif
	unsigned int current_thermal = 0;

	unsigned int cpu_temp = 0;
	unsigned int cpu_t_offset = 0;
	unsigned int cpu_t_coefficient = 0;
	unsigned int cpu_t_factor = 0;
	unsigned int cpu_t_th = 0;

#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)
	if((GET_CHIP_VER(priv) == VERSION_8814A)||(GET_CHIP_VER(priv) == VERSION_8814B))
		rf_path = 4;
	else
#endif
		rf_path = 2;

	PRINT_ONE("  Dot11RFEntry...", "%s", 1);	
	PRINT_SINGL_ARG("    dot11channel: ", priv->pmib->dot11RFEntry.dot11channel, "%d");	
	PRINT_SINGL_ARG("    dot11ch_low: ", priv->pmib->dot11RFEntry.dot11ch_low, "%d");
	PRINT_SINGL_ARG("    dot11ch_hi: ", priv->pmib->dot11RFEntry.dot11ch_hi, "%d");
	
#if defined(CONFIG_RTL_92D_SUPPORT) || defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8881A) || defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8822CE) || defined(CONFIG_WLAN_HAL_8821CE) || defined(CONFIG_WLAN_HAL_8814BE) || defined(CONFIG_WLAN_HAL_8812FE)	//FOR_8812_MP
//	if ((GET_CHIP_VER(priv)==VERSION_8192D) && (priv->pmib->dot11RFEntry.phyBandSelect & PHY_BAND_5G)) {
	if ((GET_CHIP_VER(priv)==VERSION_8192D)||(GET_CHIP_VER(priv)==VERSION_8812E)||(GET_CHIP_VER(priv)==VERSION_8881A)||(GET_CHIP_VER(priv)==VERSION_8814A)||(GET_CHIP_VER(priv)==VERSION_8822B)||(GET_CHIP_VER(priv)==VERSION_8822C)||(GET_CHIP_VER(priv)==VERSION_8821C)||(GET_CHIP_VER(priv)==VERSION_8814B)||(GET_CHIP_VER(priv)==VERSION_8812F)){
		PRINT_ARRAY_ARG("    pwrlevel5GHT40_1S_A: ", priv->pmib->dot11RFEntry.pwrlevel5GHT40_1S_A, "%02x", MAX_5G_CHANNEL_NUM);
		PRINT_ARRAY_ARG("    pwrlevel5GHT40_1S_B: ", priv->pmib->dot11RFEntry.pwrlevel5GHT40_1S_B, "%02x", MAX_5G_CHANNEL_NUM);
		if(GET_CHIP_VER(priv)!=VERSION_8812E && GET_CHIP_VER(priv) != VERSION_8814A) {
			PRINT_ARRAY_ARG("    pwrdiff5GHT40_2S: ", priv->pmib->dot11RFEntry.pwrdiff5GHT40_2S, "%02x", MAX_5G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrdiff5GHT20: ", priv->pmib->dot11RFEntry.pwrdiff5GHT20, "%02x", MAX_5G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrdiff5GOFDM: ", priv->pmib->dot11RFEntry.pwrdiff5GOFDM, "%02x", MAX_5G_CHANNEL_NUM);
		}
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)		
		if((GET_CHIP_VER(priv) == VERSION_8814A)||(GET_CHIP_VER(priv)==VERSION_8814B)) {
			PRINT_ARRAY_ARG("    pwrlevel5GHT40_1S_C: ", priv->pmib->dot11RFEntry.pwrlevel5GHT40_1S_C, "%02x", MAX_5G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrlevel5GHT40_1S_D: ", priv->pmib->dot11RFEntry.pwrlevel5GHT40_1S_D, "%02x", MAX_5G_CHANNEL_NUM);
		}
#endif	
	}

	if ((GET_CHIP_VER(priv)==VERSION_8814B)||(GET_CHIP_VER(priv)==VERSION_8812F)){
		PRINT_ARRAY_ARG("    pwrlevel_TSSI5GHT40_1S_A: ", priv->pmib->dot11RFEntry.pwrlevel_TSSI5GHT40_1S_A, "%02x", MAX_5G_CHANNEL_NUM);
		PRINT_ARRAY_ARG("    pwrlevel_TSSI5GHT40_1S_B: ", priv->pmib->dot11RFEntry.pwrlevel_TSSI5GHT40_1S_B, "%02x", MAX_5G_CHANNEL_NUM);

		if (GET_CHIP_VER(priv)==VERSION_8814B){
			PRINT_ARRAY_ARG("    pwrlevel_TSSI5GHT40_1S_C: ", priv->pmib->dot11RFEntry.pwrlevel_TSSI5GHT40_1S_C, "%02x", MAX_5G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrlevel_TSSI5GHT40_1S_D: ", priv->pmib->dot11RFEntry.pwrlevel_TSSI5GHT40_1S_D, "%02x", MAX_5G_CHANNEL_NUM);
		}
	}
	
	//} else
#endif
	{
		PRINT_ARRAY_ARG("    pwrlevelCCK_A: ", priv->pmib->dot11RFEntry.pwrlevelCCK_A, "%02x", MAX_2G_CHANNEL_NUM);
		PRINT_ARRAY_ARG("    pwrlevelCCK_B: ", priv->pmib->dot11RFEntry.pwrlevelCCK_B, "%02x", MAX_2G_CHANNEL_NUM);
#if defined(CONFIG_WLAN_HAL_8814AE)	|| defined(CONFIG_WLAN_HAL_8198F) || defined(CONFIG_WLAN_HAL_8814BE)
		PRINT_ARRAY_ARG("    pwrlevelCCK_C: ", priv->pmib->dot11RFEntry.pwrlevelCCK_C, "%02x", MAX_2G_CHANNEL_NUM);
		PRINT_ARRAY_ARG("    pwrlevelCCK_D: ", priv->pmib->dot11RFEntry.pwrlevelCCK_D, "%02x", MAX_2G_CHANNEL_NUM);
#endif
		PRINT_ARRAY_ARG("    pwrlevelHT40_1S_A: ", priv->pmib->dot11RFEntry.pwrlevelHT40_1S_A, "%02x", MAX_2G_CHANNEL_NUM);
		PRINT_ARRAY_ARG("    pwrlevelHT40_1S_B: ", priv->pmib->dot11RFEntry.pwrlevelHT40_1S_B, "%02x", MAX_2G_CHANNEL_NUM);
#if defined(CONFIG_WLAN_HAL_8814AE)	|| defined(CONFIG_WLAN_HAL_8198F) || defined(CONFIG_WLAN_HAL_8814BE)
		PRINT_ARRAY_ARG("    pwrlevelHT40_1S_C: ", priv->pmib->dot11RFEntry.pwrlevelHT40_1S_C, "%02x", MAX_2G_CHANNEL_NUM);
		PRINT_ARRAY_ARG("    pwrlevelHT40_1S_D: ", priv->pmib->dot11RFEntry.pwrlevelHT40_1S_D, "%02x", MAX_2G_CHANNEL_NUM);
#endif				
		if(GET_CHIP_VER(priv)==VERSION_8198F) {
			PRINT_ARRAY_ARG("    pwrdiff_20BW1S_OFDM1T_A: ", priv->pmib->dot11RFEntry.pwrdiff_20BW1S_OFDM1T_A, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrdiff_20BW1S_OFDM1T_B: ", priv->pmib->dot11RFEntry.pwrdiff_20BW1S_OFDM1T_B, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrdiff_20BW1S_OFDM1T_C: ", priv->pmib->dot11RFEntry.pwrdiff_20BW1S_OFDM1T_C, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrdiff_20BW1S_OFDM1T_D: ", priv->pmib->dot11RFEntry.pwrdiff_20BW1S_OFDM1T_D, "%02x", MAX_2G_CHANNEL_NUM);
		}else	if(GET_CHIP_VER(priv)!=VERSION_8812E) {
			PRINT_ARRAY_ARG("    pwrdiffHT40_2S: ", priv->pmib->dot11RFEntry.pwrdiffHT40_2S, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrdiffHT20: ", priv->pmib->dot11RFEntry.pwrdiffHT20, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrdiffOFDM: ", priv->pmib->dot11RFEntry.pwrdiffOFDM, "%02x", MAX_2G_CHANNEL_NUM);
		}
#if defined(CONFIG_WLAN_HAL_8814BE)
		if ((GET_CHIP_VER(priv)==VERSION_8814B)){
			PRINT_ARRAY_ARG("    pwrlevel_TSSICCK_A: ", priv->pmib->dot11RFEntry.pwrlevel_TSSICCK_A, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrlevel_TSSICCK_B: ", priv->pmib->dot11RFEntry.pwrlevel_TSSICCK_B, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrlevel_TSSICCK_C: ", priv->pmib->dot11RFEntry.pwrlevel_TSSICCK_C, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrlevel_TSSICCK_D: ", priv->pmib->dot11RFEntry.pwrlevel_TSSICCK_D, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrlevel_TSSIHT40_1S_A: ", priv->pmib->dot11RFEntry.pwrlevel_TSSIHT40_1S_A, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrlevel_TSSIHT40_1S_B: ", priv->pmib->dot11RFEntry.pwrlevel_TSSIHT40_1S_B, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrlevel_TSSIHT40_1S_C: ", priv->pmib->dot11RFEntry.pwrlevel_TSSIHT40_1S_C, "%02x", MAX_2G_CHANNEL_NUM);
			PRINT_ARRAY_ARG("    pwrlevel_TSSIHT40_1S_D: ", priv->pmib->dot11RFEntry.pwrlevel_TSSIHT40_1S_D, "%02x", MAX_2G_CHANNEL_NUM);
		}
#endif
	}

#if 0
		PRINT_ONE("  <Power by rate >", "%s", 1);
		PRINT_ARRAY_ARG("	 CCKTxAgc_A : ", (priv->pshare->phw->CCKTxAgc_A), "%d ", 4);
		PRINT_ARRAY_ARG("	 OFDMTxAgcOffset_A : ", (priv->pshare->phw->OFDMTxAgcOffset_A), "%d ", 8);
		PRINT_ARRAY_ARG("	 MCSTxAgcOffset_A : ", (priv->pshare->phw->MCSTxAgcOffset_A), "%d ", 16);
		PRINT_ARRAY_ARG("	 VHTTxAgcOffset_A : ", (priv->pshare->phw->VHTTxAgcOffset_A), "%d ", 20);
		
		PRINT_ARRAY_ARG("	 CCKTxAgc_B : ", (priv->pshare->phw->CCKTxAgc_B), "%d ", 4);
		PRINT_ARRAY_ARG("	 OFDMTxAgcOffset_B : ", (priv->pshare->phw->OFDMTxAgcOffset_B), "%d ", 8);
		PRINT_ARRAY_ARG("	 MCSTxAgcOffset_B : ", (priv->pshare->phw->MCSTxAgcOffset_B), "%d ", 16);
		PRINT_ARRAY_ARG("	 VHTTxAgcOffset_B : ", (priv->pshare->phw->VHTTxAgcOffset_B), "%d ", 20);
#endif

#ifdef RTK_NL80211
	PRINT_SINGL_ARG("    TxPowerRate : ", (priv->rtk->pwr_rate), "%d(percent)");
#endif

#ifdef TXPWR_LMT
	PRINT_SINGL_ARG("    disable_txpwrlmt : ", (priv->pshare->rf_ft_var.disable_txpwrlmt), "%d");
	PRINT_SINGL_ARG("    txpwr_lmt_index : ", (priv->pmib->dot11StationConfigEntry.txpwr_lmt_index), "%d");
	PRINT_SINGL_ARG("    regdomain : ", (priv->pmib->dot11StationConfigEntry.dot11RegDomain), "%d");
	PRINT_SINGL_ARG("    txpwr_lmt_CCK : ", (priv->pshare->txpwr_lmt_CCK), "%d");
	PRINT_SINGL_ARG("    txpwr_lmt_OFDM : ", (priv->pshare->txpwr_lmt_OFDM), "%d");
	PRINT_SINGL_ARG("    txpwr_lmt_HT1S : ", (priv->pshare->txpwr_lmt_HT1S), "%d");
	PRINT_SINGL_ARG("    txpwr_lmt_HT2S : ", (priv->pshare->txpwr_lmt_HT2S), "%d");	
#if defined(TXPWR_LMT_8814A) || defined(TXPWR_LMT_8198F) || defined(TXPWR_LMT_8814B)
	if((GET_CHIP_VER(priv)== VERSION_8814A)||(GET_CHIP_VER(priv)== VERSION_8198F)||(GET_CHIP_VER(priv)== VERSION_8814B)){
		PRINT_SINGL_ARG("    txpwr_lmt_HT3S : ", (priv->pshare->txpwr_lmt_HT3S), "%d");
		PRINT_SINGL_ARG("    txpwr_lmt_HT4S : ", (priv->pshare->txpwr_lmt_HT4S), "%d");
	}
#endif
#ifdef TXPWR_LMT_NEWFILE
	if((GET_CHIP_VER(priv)== VERSION_8812E) || 
		(GET_CHIP_VER(priv)== VERSION_8188E) ||
		(IS_HAL_CHIP(priv))){
#if defined(TXPWR_LMT_8812) || defined(TXPWR_LMT_8881A) || defined(TXPWR_LMT_8814A) || defined(TXPWR_LMT_8822B) || defined(TXPWR_LMT_8822C) || defined(TXPWR_LMT_8198F) || defined(TXPWR_LMT_8814B) || defined(TXPWR_LMT_8812F)
			PRINT_SINGL_ARG("    txpwr_lmt_VHT1S : ", (priv->pshare->txpwr_lmt_VHT1S), "%d");
			PRINT_SINGL_ARG("    txpwr_lmt_VHT2S : ", (priv->pshare->txpwr_lmt_VHT2S), "%d");
#endif
#if defined(TXPWR_LMT_8814A) || defined(TXPWR_LMT_8198F) || defined(TXPWR_LMT_8814B)
			if((GET_CHIP_VER(priv)== VERSION_8814A)||(GET_CHIP_VER(priv)== VERSION_8198F)||(GET_CHIP_VER(priv)== VERSION_8814B)){
				PRINT_SINGL_ARG("    txpwr_lmt_VHT3S : ", (priv->pshare->txpwr_lmt_VHT3S), "%d");
				PRINT_SINGL_ARG("    txpwr_lmt_VHT4S : ", (priv->pshare->txpwr_lmt_VHT4S), "%d");
			}			
#endif
#ifdef BEAMFORMING_AUTO
			if(priv->pshare->rf_ft_var.txbf_pwrlmt == TXBF_TXPWRLMT_AUTO) {
				PRINT_SINGL_ARG("	 txpwr_lmt_TXBF_HT1S : ", (priv->pshare->txpwr_lmt_TXBF_HT1S), "%d");
				PRINT_SINGL_ARG("	 txpwr_lmt_TXBF_HT2S : ", (priv->pshare->txpwr_lmt_TXBF_HT2S), "%d");	
#if defined(TXPWR_LMT_8814A) || defined(TXPWR_LMT_8198F) || defined(TXPWR_LMT_8814B)
				if((GET_CHIP_VER(priv)== VERSION_8814A)||(GET_CHIP_VER(priv)== VERSION_8198F)||(GET_CHIP_VER(priv)== VERSION_8814B)){
					PRINT_SINGL_ARG("	 txpwr_lmt_TXBF_HT3S : ", (priv->pshare->txpwr_lmt_TXBF_HT3S), "%d");
					PRINT_SINGL_ARG("	 txpwr_lmt_TXBF_HT4S : ", (priv->pshare->txpwr_lmt_TXBF_HT4S), "%d");
				}
#endif
#if defined(TXPWR_LMT_8812) || defined(TXPWR_LMT_8881A) || defined(TXPWR_LMT_8814A) || defined(TXPWR_LMT_8198F) || defined(TXPWR_LMT_8814B)
				PRINT_SINGL_ARG("    txpwr_lmt_TXBF_VHT1S : ", (priv->pshare->txpwr_lmt_TXBF_VHT1S), "%d");
				PRINT_SINGL_ARG("    txpwr_lmt_TXBF_VHT2S : ", (priv->pshare->txpwr_lmt_TXBF_VHT2S), "%d");
#endif
#if defined(TXPWR_LMT_8814A) || defined(TXPWR_LMT_8198F) || defined(TXPWR_LMT_8814B)
				if((GET_CHIP_VER(priv)== VERSION_8814A)||(GET_CHIP_VER(priv)== VERSION_8198F)||(GET_CHIP_VER(priv)== VERSION_8814B)){
					PRINT_SINGL_ARG("    txpwr_lmt_TXBF_VHT3S : ", (priv->pshare->txpwr_lmt_TXBF_VHT3S), "%d");
					PRINT_SINGL_ARG("    txpwr_lmt_TXBF_VHT4S : ", (priv->pshare->txpwr_lmt_TXBF_VHT4S), "%d");
				}			
#endif
			}
#endif // BEAMFORMING_AUTO

#if defined(TXPWR_LMT_8198F) || defined(TXPWR_LMT_8814B) || defined(TXPWR_LMT_8812F)
		if((GET_CHIP_VER(priv)== VERSION_8198F)||(GET_CHIP_VER(priv)== VERSION_8814B)||(GET_CHIP_VER(priv)== VERSION_8812F)){
			PRINT_SINGL_ARG("	 tgpwr_CCK_NEW : ", (priv->pshare->tgpwr_CCK_NEW), "%d");
			PRINT_SINGL_ARG("	 tgpwr_OFDM_NEW : ", (priv->pshare->tgpwr_OFDM_NEW), "%d");
			PRINT_SINGL_ARG("	 tgpwr_HT1S_NEW : ", (priv->pshare->tgpwr_HT1S_NEW), "%d");
			PRINT_SINGL_ARG("	 tgpwr_HT2S_NEW : ", (priv->pshare->tgpwr_HT2S_NEW), "%d");
			PRINT_SINGL_ARG("	 tgpwr_HT3S_NEW : ", (priv->pshare->tgpwr_HT3S_NEW), "%d");
			PRINT_SINGL_ARG("	 tgpwr_HT4S_NEW : ", (priv->pshare->tgpwr_HT4S_NEW), "%d");
			PRINT_SINGL_ARG("	 tgpwr_VHT1S_NEW : ", (priv->pshare->tgpwr_VHT1S_NEW), "%d");
			PRINT_SINGL_ARG("	 tgpwr_VHT2S_NEW : ", (priv->pshare->tgpwr_VHT2S_NEW), "%d");
			PRINT_SINGL_ARG("	 tgpwr_VHT3S_NEW : ", (priv->pshare->tgpwr_VHT3S_NEW), "%d");
			PRINT_SINGL_ARG("	 tgpwr_VHT4S_NEW : ", (priv->pshare->tgpwr_VHT4S_NEW), "%d");

		}else
#endif
{
			PRINT_ARRAY_ARG("    tgpwr_CCK_new : ", (priv->pshare->tgpwr_CCK_new), "%d ", rf_path);
			PRINT_ARRAY_ARG("    tgpwr_OFDM_new : ", (priv->pshare->tgpwr_OFDM_new), "%d ", rf_path);
			PRINT_ARRAY_ARG("    tgpwr_HT1S_new : ", (priv->pshare->tgpwr_HT1S_new), "%d ", rf_path);
			PRINT_ARRAY_ARG("    tgpwr_HT2S_new : ", (priv->pshare->tgpwr_HT2S_new), "%d ", rf_path);
#if defined(TXPWR_LMT_8814A)
	if((GET_CHIP_VER(priv)== VERSION_8814A)){
			PRINT_ARRAY_ARG("    tgpwr_HT3S_new : ", (priv->pshare->tgpwr_HT3S_new), "%d ", rf_path);
			PRINT_ARRAY_ARG("    tgpwr_HT4S_new : ", (priv->pshare->tgpwr_HT4S_new), "%d ", rf_path);
	}			
#endif
			
#if defined(TXPWR_LMT_8812) || defined(TXPWR_LMT_8881A) || defined(TXPWR_LMT_8814A) || defined(TXPWR_LMT_8822B) || defined(TXPWR_LMT_8822C) || defined(TXPWR_LMT_8812F)
			PRINT_ARRAY_ARG("    tgpwr_VHT1S_new : ", (priv->pshare->tgpwr_VHT1S_new), "%d ", rf_path);
			PRINT_ARRAY_ARG("    tgpwr_VHT2S_new : ", (priv->pshare->tgpwr_VHT2S_new), "%d ", rf_path);
#endif
#if defined(TXPWR_LMT_8814A)
	if((GET_CHIP_VER(priv)== VERSION_8814A)){
			PRINT_ARRAY_ARG("    tgpwr_VHT3S_new : ", (priv->pshare->tgpwr_VHT3S_new), "%d ", rf_path);
			PRINT_ARRAY_ARG("    tgpwr_VHT4S_new : ", (priv->pshare->tgpwr_VHT4S_new), "%d ", rf_path);
	}			
#endif
}
	} else
#endif
	{
	PRINT_SINGL_ARG("    target_CCK : ", (priv->pshare->tgpwr_CCK), "%d");
	PRINT_SINGL_ARG("    target_OFDM : ", (priv->pshare->tgpwr_OFDM), "%d");
	PRINT_SINGL_ARG("    target_HT1S : ", (priv->pshare->tgpwr_HT1S), "%d");
	PRINT_SINGL_ARG("    target_HT2S : ", (priv->pshare->tgpwr_HT2S), "%d");
	}
#endif
#ifdef POWER_PERCENT_ADJUSTMENT
	PRINT_SINGL_ARG("    power_percent: ", priv->pmib->dot11RFEntry.power_percent, "%d");
#endif
	PRINT_SINGL_ARG("    shortpreamble: ", priv->pmib->dot11RFEntry.shortpreamble, "%d");
	PRINT_SINGL_ARG("    trswitch: ", priv->pmib->dot11RFEntry.trswitch, "%d");
	PRINT_SINGL_ARG("    disable_ch14_ofdm: ", priv->pmib->dot11RFEntry.disable_ch14_ofdm, "%d");
	PRINT_SINGL_ARG("    xcap: ", priv->pmib->dot11RFEntry.xcap, "%d");
	PRINT_SINGL_ARG("    share_xcap: ", priv->pmib->dot11RFEntry.share_xcap, "%d");
	PRINT_SINGL_ARG("    tssi1: ", priv->pmib->dot11RFEntry.tssi1, "%d");
	PRINT_SINGL_ARG("    tssi2: ", priv->pmib->dot11RFEntry.tssi2, "%d");
	PRINT_SINGL_ARG("    ther: ", priv->pmib->dot11RFEntry.ther, "%d");
	if(netif_running(dev))
	{
#if defined(CONFIG_WLAN_HAL_8812FE)
	if (GET_CHIP_VER(priv)== VERSION_8812F){
		PRINT_SINGL_ARG("    ther path A: ", priv->pmib->dot11RFEntry.thermal[0], "%d");
		PRINT_SINGL_ARG("    ther path B: ", priv->pmib->dot11RFEntry.thermal[1], "%d");
		phy_set_rf_reg(priv, RF_PATH_A, 0x42, BIT(19), 0x1);
		phy_set_rf_reg(priv, RF_PATH_A, 0x42, BIT(19), 0x0);
		phy_set_rf_reg(priv, RF_PATH_A, 0x42, BIT(19), 0x1);
		delay_ms(10);
		current_thermal = phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0x7e, 1)+ phydm_get_multi_thermal_offset(ODMPTR, RF_PATH_A);
		PRINT_SINGL_ARG("    current thermal path A: ", current_thermal, "%u");
		cpu_t_th = current_thermal;
		phy_set_rf_reg(priv, RF_PATH_B, 0x42, BIT(19), 0x1);
		phy_set_rf_reg(priv, RF_PATH_B, 0x42, BIT(19), 0x0);
		phy_set_rf_reg(priv, RF_PATH_B, 0x42, BIT(19), 0x1);
		delay_ms(10);
		current_thermal = phy_query_rf_reg(priv, RF_PATH_B, 0x42, 0x7e, 1) + phydm_get_multi_thermal_offset(ODMPTR, RF_PATH_B);
		PRINT_SINGL_ARG("    current thermal path B: ", current_thermal, "%u");
		/* cpu temperature */
		cpu_t_offset = 6290;
		cpu_t_coefficient = 1450;
		cpu_t_factor = 1000;

		cpu_temp = (cpu_t_offset + cpu_t_coefficient*cpu_t_th) / cpu_t_factor;
		PRINT_SINGL_ARG("    cputemperature: ", cpu_temp, "%u");
	}else
#endif
#if defined(CONFIG_WLAN_HAL_8814BE)
	if (GET_CHIP_VER(priv) == VERSION_8814B){
		PRINT_SINGL_ARG("    ther path A: ", priv->pmib->dot11RFEntry.thermal[0], "%d");
		PRINT_SINGL_ARG("    ther path B: ", priv->pmib->dot11RFEntry.thermal[1], "%d");
		PRINT_SINGL_ARG("    ther path C: ", priv->pmib->dot11RFEntry.thermal[2], "%d");
		PRINT_SINGL_ARG("    ther path D: ", priv->pmib->dot11RFEntry.thermal[3], "%d");		
		phy_set_rf_reg(priv, RF_PATH_A, 0x42, BIT(17), 0x1);
		phy_set_rf_reg(priv, RF_PATH_A, 0x42, BIT(17), 0x0);
		delay_ms(10);
		current_thermal = phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0xfc00, 1) + phydm_get_multi_thermal_offset(ODMPTR, RF_PATH_A);
		PRINT_SINGL_ARG("    current thermal path A: ", current_thermal, "%u");
		phy_set_rf_reg(priv, RF_PATH_B, 0x42, BIT(17), 0x1);
		phy_set_rf_reg(priv, RF_PATH_B, 0x42, BIT(17), 0x0);
		delay_ms(10);
		current_thermal = phy_query_rf_reg(priv, RF_PATH_B, 0x42, 0xfc00, 1) + phydm_get_multi_thermal_offset(ODMPTR, RF_PATH_B);
		PRINT_SINGL_ARG("    current thermal path B: ", current_thermal, "%u");
		phy_set_rf_reg(priv, RF_PATH_C, 0x42, BIT(17), 0x1);
		phy_set_rf_reg(priv, RF_PATH_C, 0x42, BIT(17), 0x0);
		delay_ms(10);
		current_thermal = phy_query_rf_reg(priv, RF_PATH_C, 0x42, 0xfc00, 1) + phydm_get_multi_thermal_offset(ODMPTR, RF_PATH_C);
		PRINT_SINGL_ARG("    current thermal path C: ", current_thermal, "%u");
		phy_set_rf_reg(priv, RF_PATH_D, 0x42, BIT(17), 0x1);
		phy_set_rf_reg(priv, RF_PATH_D, 0x42, BIT(17), 0x0);
		delay_ms(10);
		current_thermal = phy_query_rf_reg(priv, RF_PATH_D, 0x42, 0xfc00, 1) + phydm_get_multi_thermal_offset(ODMPTR, RF_PATH_D);
		PRINT_SINGL_ARG("    current thermal path D: ", current_thermal, "%u");
	}else
#endif
	{
		current_thermal = phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0xfc00, 1);
		PRINT_SINGL_ARG("    current thermal: ", current_thermal, "%u");
	}

	}
	else
	{
		PRINT_SINGL_ARG("    current thermal: ", 0, "%d");
	}
#ifdef THER_TRIM	
	PRINT_SINGL_ARG("    ther_trim_enable: ", priv->pshare->rf_ft_var.ther_trim_enable, "%d");
	PRINT_SINGL_ARG("    ther_trim_val: ", priv->pshare->rf_ft_var.ther_trim_val, "%d");
#endif
	switch (priv->pshare->phw->MIMO_TR_hw_support) {
	case RF_1T2R:
		snprintf(tmpbuf, sizeof(tmpbuf), "1T2R");
		break;
	case RF_1T1R:
		snprintf(tmpbuf, sizeof(tmpbuf), "1T1R");
		break;
	case RF_2T2R:
		snprintf(tmpbuf, sizeof(tmpbuf), "2T2R");
		break;
	case RF_3T3R:
		snprintf(tmpbuf, sizeof(tmpbuf), "3T3R");
		break;
	case RF_4T4R:
		snprintf(tmpbuf, sizeof(tmpbuf), "4T4R");
		break;
	default:
		snprintf(tmpbuf, sizeof(tmpbuf), "2T4R");
		break;
	}
	PRINT_SINGL_ARG("    MIMO_TR_hw_support: ", tmpbuf, "%s");

	switch (priv->pmib->dot11RFEntry.MIMO_TR_mode) {
	case RF_1T2R:
		snprintf(tmpbuf, sizeof(tmpbuf), "1T2R");
		break;
	case RF_1T1R:
		snprintf(tmpbuf, sizeof(tmpbuf), "1T1R");
		break;
	case RF_2T2R:
		snprintf(tmpbuf, sizeof(tmpbuf), "2T2R");
		break;
	case RF_3T3R:
		snprintf(tmpbuf, sizeof(tmpbuf), "3T3R");
		break;
	case RF_4T4R:
		snprintf(tmpbuf, sizeof(tmpbuf), "4T4R");
		break;
	default:
		snprintf(tmpbuf, sizeof(tmpbuf), "2T4R");
		break;
	}
	PRINT_SINGL_ARG("    MIMO_TR_mode: ", tmpbuf, "%s");
	PRINT_SINGL_ARG("    TXPowerOffset:     ", priv->pshare->phw->TXPowerOffset, "%u");
#ifdef RF_MIMO_SWITCH
if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
	switch (priv->pshare->rf_status) {
	case RF_1T1R:
		snprintf(tmpbuf, sizeof(tmpbuf), "1T1R");
		break;
	case RF_2T2R:
		snprintf(tmpbuf, sizeof(tmpbuf), "2T2R");
		break;
	case RF_3T3R:
		snprintf(tmpbuf, sizeof(tmpbuf), "3T3R");
		break;
	case RF_4T4R:
		snprintf(tmpbuf, sizeof(tmpbuf), "4T4R");
		break;
	default:
		snprintf(tmpbuf, sizeof(tmpbuf), "N/A");
		break;
	}
	PRINT_SINGL_ARG("    RF status: ", tmpbuf, "%s");
	if(priv->pshare->rf_ft_var.idle_ps_ext)
		if(priv->pshare->idle_txpwr_reduction != 0)	
			PRINT_SINGL_ARG("    idle_txpwr_reduction:     ", priv->pshare->idle_txpwr_reduction, "%u");
}
#endif

	PRINT_SINGL_ARG("    phyBandSelect: ", priv->pmib->dot11RFEntry.phyBandSelect, "%d");
#ifdef CONFIG_RTL_92D_SUPPORT
	if (GET_CHIP_VER(priv) == VERSION_8192D) {
		switch (priv->pmib->dot11RFEntry.macPhyMode) {
		case SINGLEMAC_SINGLEPHY:
			snprintf(tmpbuf, sizeof(tmpbuf), "SMSP");
			break;
		case DUALMAC_SINGLEPHY:
			snprintf(tmpbuf, sizeof(tmpbuf), "DMSP");
			break;
		case DUALMAC_DUALPHY:
			snprintf(tmpbuf, sizeof(tmpbuf), "DMDP");
			break;
		default:
			snprintf(tmpbuf, sizeof(tmpbuf), "unknown");
			break;
		}
		PRINT_SINGL_ARG("    macPhyMode: ", tmpbuf, "%s");
	}
#endif

	_getChipVersion(priv, tmpbuf, sizeof(tmpbuf));
	PRINT_SINGL_ARG("    chipVersion: ", tmpbuf, "%s");

#ifdef EN_EFUSE
	if(priv->pmib->efuseEntry.enable_efuse)	{
		int k;
		PRINT_SINGL_ARG("    autoload fail: ", priv->AutoloadFailFlag, "%d");
		PRINT_SINGL_ARG("    efuse used bytes: ", priv->EfuseUsedBytes, "%d");

		PRINT_ONE("efuse init map...", "%s", 1);
		for(k=0; k<priv->EfuseMapLen; k+=16)
			PRINT_ARRAY_ARG("    ", (priv->EfuseMap[EFUSE_INIT_MAP]+k), "%02x", 16);
		PRINT_ONE("efuse modify map...", "%s", 1);
		for(k=0; k<priv->EfuseMapLen; k+=16)
			PRINT_ARRAY_ARG("    ", (priv->EfuseMap[EFUSE_MODIFY_MAP]+k), "%02x", 16);
	}
#endif
#ifdef POWER_TRIM
	PRINT_SINGL_ARG("    kfree_enable:", priv->pmib->dot11RFEntry.kfree_enable, "%d");
	PRINT_SINGL_ARG("    kfree_value: 0x", priv->pshare->kfree_value, "%x");
#endif
#if defined (HW_ANT_SWITCH) && !defined(CONFIG_RTL_92C_SUPPORT) && !defined(CONFIG_RTL_92D_SUPPORT)
	if (ODMPTR->support_ability & ODM_BB_ANT_DIV) {
		PRINT_SINGL_ARG("    Antdiv_Type : ", ((priv->pshare->_dmODM.ant_div_type == CGCS_RX_HW_ANTDIV) ? "RX_ANTDIV" : ((priv->pshare->_dmODM.ant_div_type == CG_TRX_HW_ANTDIV)?"TRX_ANTDIV":"NONE")), "%s");
		PRINT_SINGL_ARG("    Antdiv switch : ", ((priv->pshare->_dmODM.antdiv_select==0) ? "Auto" : ((priv->pshare->_dmODM.antdiv_select==1)?"A1":"A2")), "%s");
	}
#endif

#ifdef SW_ANT_SWITCH
	PRINT_SINGL_ARG("    SW Ant switch enable: ", (SW_DIV_ENABLE ? "enable" : "disable"), "%s");
	PRINT_SINGL_ARG("    SW Diversity Antenna : ", priv->pshare->DM_SWAT_Table.CurAntenna, "%d");
#endif

#if defined (HW_ANT_SWITCH) &&( defined(CONFIG_RTL_92C_SUPPORT) || defined(CONFIG_RTL_92D_SUPPORT))
	PRINT_SINGL_ARG("    HW Ant switch enable: ", (HW_DIV_ENABLE ? "enable" : "disable"), "%s");
	PRINT_SINGL_ARG("    RxIdle Antenna : ", (priv->pshare->rf_ft_var.CurAntenna==0 ? 2 : 1), "%d");
#endif
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)
	PRINT_SINGL_ARG("    tx3path: ", priv->pmib->dot11RFEntry.tx3path, "%d");
	PRINT_SINGL_ARG("    tx4path: ", priv->pmib->dot11RFEntry.tx4path, "%d");
#endif
	PRINT_SINGL_ARG("    tx2path: ", priv->pmib->dot11RFEntry.tx2path, "%d");
	PRINT_SINGL_ARG("    txbf: ", priv->pmib->dot11RFEntry.txbf, "%d");

#ifdef BEAMFORMING_AUTO
	if(priv->pshare->rf_ft_var.txbf_pwrlmt == TXBF_TXPWRLMT_AUTO) {
		PRINT_SINGL_ARG("    txbferVHT2TX: ", priv->pshare->txbferVHT2TX, "%d");
#ifdef CONFIG_WLAN_HAL_8814AE   		
		PRINT_SINGL_ARG("    txbferVHT3TX: ", priv->pshare->txbferVHT3TX, "%d");
#endif		
		PRINT_SINGL_ARG("    txbferHT2TX: ", priv->pshare->txbferHT2TX, "%d");
#ifdef CONFIG_WLAN_HAL_8814AE   		
		PRINT_SINGL_ARG("    txbferHT3TX: ", priv->pshare->txbferHT3TX, "%d");
#endif		
		PRINT_SINGL_ARG("    txbf2TXbackoff: ", priv->pshare->txbf2TXbackoff, "%d");
#ifdef CONFIG_WLAN_HAL_8814AE   		
		PRINT_SINGL_ARG("    txbf3TXbackoff: ", priv->pshare->txbf3TXbackoff, "%d");
#endif		
	}
#endif

#ifdef RTL8192D_INT_PA
	PRINT_SINGL_ARG("    use_intpa92d: ", priv->pshare->rf_ft_var.use_intpa92d, "%d");
#endif
#ifdef	HIGH_POWER_EXT_PA
	PRINT_SINGL_ARG("    use_ext_pa: ", priv->pshare->rf_ft_var.use_ext_pa, "%d");
#endif
#ifdef HIGH_POWER_EXT_LNA
	PRINT_SINGL_ARG("    use_ext_lna: ", priv->pshare->rf_ft_var.use_ext_lna, "%d");
#endif
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8822CE) || defined(CONFIG_WLAN_HAL_8197F) || defined(CONFIG_WLAN_HAL_8821CE) || defined(CONFIG_WLAN_HAL_8198F) || defined(CONFIG_WLAN_HAL_8814BE) || defined(CONFIG_WLAN_HAL_8812FE) || defined(CONFIG_WLAN_HAL_8192FE)
	if ((GET_CHIP_VER(priv) == VERSION_8814A) || (GET_CHIP_VER(priv) == VERSION_8822B) || (GET_CHIP_VER(priv) == VERSION_8822C) ||
			(GET_CHIP_VER(priv) == VERSION_8197F) || (GET_CHIP_VER(priv) == VERSION_8821C) || (GET_CHIP_VER(priv) == VERSION_8198F) ||
			(GET_CHIP_VER(priv) == VERSION_8814B) || (GET_CHIP_VER(priv) == VERSION_8812F) || (GET_CHIP_VER(priv) == VERSION_8192F))
		PRINT_SINGL_ARG("    rfe_type: ", priv->pmib->dot11RFEntry.rfe_type, "%u");
#endif
	PRINT_SINGL_ARG("    pa_type: ", priv->pmib->dot11RFEntry.pa_type, "%d");
	PRINT_SINGL_ARG("    acs_type: ", priv->pmib->dot11RFEntry.acs_type, "%d");

#if	(defined(CONFIG_SLOT_0_8192EE) && defined(CONFIG_SLOT_0_EXT_LNA))||(defined(CONFIG_SLOT_1_8192EE) && defined(CONFIG_SLOT_1_EXT_LNA))	
	if (GET_CHIP_VER(priv) == VERSION_8192E)
		PRINT_SINGL_ARG("    lna_type: ", priv->pshare->rf_ft_var.lna_type, "%d");
#endif
	
#ifdef CONFIG_8881A_HP
	PRINT_SINGL_ARG("    hp_8881a: ", priv->pshare->rf_ft_var.hp_8881a, "%u");
#endif

#ifdef CONFIG_8881A_2LAYER
	PRINT_SINGL_ARG("    use_8881a_2layer: ", priv->pshare->rf_ft_var.use_8881a_2layer, "%u");
#endif

	PRINT_SINGL_ARG("    txpwr_reduction: ", priv->pmib->dot11RFEntry.txpwr_reduction, "%d");

#if defined(CONFIG_WLAN_HAL_8192EE)
    if (GET_CHIP_VER(priv) == VERSION_8192E) {
        if (priv->pshare->swr_mode == LDO_MODE) {
            PRINT_SINGL_ARG("    swr_mode: ", "LDO MODE", "%s");
        } else {
            PRINT_SINGL_ARG("    swr_mode: ", "SPS MODE", "%s");
        }
    }        
#endif
	return pos;
}

#ifdef THERMAL_CONTROL
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_thermal_control(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_thermal_control(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	struct list_head *phead, *plist;
	struct stat_info *pstat;
	unsigned long flags=0;
	unsigned char current_thermal;
	int num=1;

	int pos = 0;
	int rf_path;
#ifdef __ECOS
	char tmpbuf[16];
#else
	unsigned char tmpbuf[16];
#endif
	PRINT_ONE("[THERMAL CONTROL]", "%s", 1);
	PRINT_SINGL_ARG("    calibrated ther: ", priv->pmib->dot11RFEntry.ther, "%d");

#ifdef CONFIG_WLAN_HAL_8812FE
	if (GET_CHIP_VER(priv) == VERSION_8812F)
		current_thermal = phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0x7e, 1);
	else
#endif
		current_thermal = phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0xfc00, 1);

	PRINT_SINGL_ARG("    current thermal: ", current_thermal, "%d");
	PRINT_SINGL_ARG("	 del_ther: ", priv->pshare->rf_ft_var.del_ther, "%d");
	PRINT_SINGL_ARG("	 ther_hi : ", priv->pshare->rf_ft_var.ther_hi, "%u");
	PRINT_SINGL_ARG("	 ther_low : ", priv->pshare->rf_ft_var.ther_low, "%u");
	PRINT_SINGL_ARG("	 man: ", priv->pshare->rf_ft_var.man, "%d");
	PRINT_SINGL_ARG("	 ther_dm: ", priv->pshare->rf_ft_var.ther_dm, "%d");
	PRINT_SINGL_ARG("	 ther_dm_period: ", priv->pshare->rf_ft_var.ther_dm_period, "%d");
	PRINT_ONE("[STATE]", "%s", 1);
	PRINT_SINGL_ARG("	 state: ", priv->pshare->rf_ft_var.state==0?"Initial State":"Thermal Control State", "%s");
	PRINT_SINGL_ARG("	 monitor_time: ", priv->pshare->rf_ft_var.monitor_time, "%d");
	PRINT_SINGL_ARG("	 countdown: ", priv->pshare->rf_ft_var.countdown, "%d");
	PRINT_SINGL_ARG("	 ther_drop: ", priv->pshare->rf_ft_var.ther_drop, "%d");
	PRINT_ONE("[Tx Power]", "%s", 1);
	PRINT_SINGL_ARG("	 low_power : ", priv->pshare->rf_ft_var.low_power, "%u");
	PRINT_SINGL_ARG("	 power_desc : ", priv->pshare->rf_ft_var.power_desc, "%u");
	PRINT_SINGL_ARG("	 current_power_desc : ", priv->pshare->rf_ft_var.current_power_desc, "%u");
	PRINT_SINGL_ARG("	 current power: ", (priv->pshare->rf_ft_var.low_power==1)?"dynamically per STA":((priv->pshare->rf_ft_var.low_power==2)?((priv->pshare->rf_ft_var.current_power_desc==3)?"-11dB":((priv->pshare->rf_ft_var.current_power_desc==2)?"-7dB":((priv->pshare->rf_ft_var.current_power_desc==1)?"-3dB":((priv->pshare->rf_ft_var.current_power_desc==0)?"0dB":"unknown")))):(priv->pshare->rf_ft_var.low_power==0)?"diable power degradation":"unknown"), "%s");
	PRINT_ONE("	 [Note] low_power =0: as default, 1: dynamic, 2: decided by power_desc", "%s", 1);
	PRINT_ONE("[Tx Path]", "%s", 1);
	PRINT_SINGL_ARG("	 path: ", (priv->pshare->rf_ft_var.path==1)?"1T":((priv->pshare->rf_ft_var.path==2)?"2T":((priv->pshare->rf_ft_var.path==0)?"tx2path":"unknown")), "%s");
	PRINT_SINGL_ARG("	 path : ", priv->pshare->rf_ft_var.path, "%u");
	PRINT_SINGL_ARG("	 current_path: ", priv->pshare->rf_ft_var.current_path, "%u");
	PRINT_SINGL_ARG("	 path_select: ", priv->pshare->rf_ft_var.path_select, "%u");
	PRINT_SINGL_ARG("	 chosen_path: ", priv->pshare->rf_ft_var.chosen_path, "%u");
	PRINT_ONE("	 [Note] path = 0:as default, 1: 1T, 2: 2T", "%s", 1);
	PRINT_ONE("[Tx Duty Cycle]", "%s", 1);
	PRINT_SINGL_ARG("	 txduty: ", priv->pshare->rf_ft_var.txduty, "%u");
	PRINT_SINGL_ARG("	 txduty_level: ", priv->pshare->rf_ft_var.txduty_level, "%u");
	PRINT_SINGL_ARG("	 limit percent (pa): ", priv->pshare->rf_ft_var.pa, "%u");
	PRINT_SINGL_ARG("	 limit_90pa: ", priv->pshare->rf_ft_var.limit_90pa, "%u");
	PRINT_SINGL_ARG("	 sta_bwc_to: ", priv->pshare->rf_ft_var.sta_bwc_to, "%u");
#if CFG_HAL_PWR_REDUCE
	PRINT_ONE("[FW Tx Duty Cycle]", "%s", 1);
	PRINT_SINGL_ARG("	 fwduty: ", priv->pshare->rf_ft_var.fwduty, "%u");
	PRINT_SINGL_ARG("	 fwduty_level: ", priv->pshare->rf_ft_var.fwduty_level, "%u");
#endif

	SAVE_INT_AND_CLI(flags);
	SMP_LOCK_ASOC_LIST(flags);

	PRINT_SINGL_ARG("[STA Info] -- active: ", priv->assoc_num, "%d");

	phead = &priv->asoc_list;
	if (!netif_running(dev) || list_empty(phead)) {
		goto _ret;
	}

	plist = phead->next;
	while (plist != phead) {
		pstat = list_entry(plist, struct stat_info, asoc_list);

#ifdef THERMAL_CONTROL
		PRINT_ONE(num,	" %d: stat_info...", 1);
		
		PRINT_SINGL_ARG("	  power: ", ((pstat->power==3)?"-11dB":((pstat->power==2)?"-7dB":((pstat->power==1)?"-3dB":((pstat->power==0)?"0dB":"unknown")))), "%s");

		PRINT_SINGL_ARG("	  got_limit_tp: ", pstat->got_limit_tp, "%u");
		PRINT_SINGL_ARG("	  tx_tp_base (Kbps): ", pstat->tx_tp_base , "%d"); 
		PRINT_SINGL_ARG("	  tx_tp_limit (Kbps): ", pstat->tx_tp_limit , "%d");	
#ifdef RTK_STA_BWC
		PRINT_SINGL_ARG("	  sta_tx_limit (Kbps): ", pstat->sta_bwc_tx_limit , "%d");
#endif
		PRINT_SINGL_ARG("	  tx_tp_base (Mbps): ", (pstat->tx_tp_base)/1024 , "%d");	
		PRINT_SINGL_ARG("	  tx_tp_limit (Mbps): ", (pstat->tx_tp_limit)/1024 , "%d");	
#ifdef RTK_STA_BWC		
		PRINT_SINGL_ARG("	  sta_tx_limit (Mbps): ", (pstat->sta_bwc_tx_limit)/1024 , "%d");
#endif
#endif
		CHECK_LEN;
		plist = plist->next;
		num++;
	}
		
_ret:
	SMP_UNLOCK_ASOC_LIST(flags);
	RESTORE_INT(flags);


	return pos;
}
#endif

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_bssdesc(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_bssdesc(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	unsigned char tmpbuf[33];
	tmpbuf[0] = '\0';

	PRINT_ONE("  bss_desc...", "%s", 1);
	PRINT_ARRAY_ARG("    bssid: ", priv->pmib->dot11Bss.bssid, "%02x", MACADDRLEN);

	if (priv->pmib->dot11Bss.ssidlen < sizeof(tmpbuf)) {
		memcpy(tmpbuf, priv->pmib->dot11Bss.ssid, priv->pmib->dot11Bss.ssidlen);
		tmpbuf[priv->pmib->dot11Bss.ssidlen] = '\0';
	}
	PRINT_SINGL_ARG("    ssid: ", tmpbuf, "%s");

	PRINT_SINGL_ARG("    ssidlen: ", priv->pmib->dot11Bss.ssidlen, "%d");
	PRINT_SINGL_ARG("    bsstype: ", priv->pmib->dot11Bss.bsstype, "%x");
	PRINT_SINGL_ARG("    beacon_prd: ", priv->pmib->dot11Bss.beacon_prd, "%d");
	PRINT_SINGL_ARG("    dtim_prd: ", priv->pmib->dot11Bss.dtim_prd, "%d");
#ifdef CLIENT_MODE
	if (OPMODE & WIFI_STATION_STATE)
		PRINT_SINGL_ARG("    client mode aid: ", _AID, "%d");
#endif
	PRINT_ARRAY_ARG("    t_stamp(hex): ", priv->pmib->dot11Bss.t_stamp, "%08x", 2);
	PRINT_SINGL_ARG("    ibss_par.atim_win: ", priv->pmib->dot11Bss.ibss_par.atim_win, "%d");
	PRINT_SINGL_ARG("    capability(hex): ", priv->pmib->dot11Bss.capability, "%02x");
	PRINT_SINGL_ARG("    channel: ", priv->pmib->dot11Bss.channel, "%d");
	PRINT_SINGL_ARG("    channel2: ", priv->pmib->dot11Bss.channel2, "%d");
	PRINT_SINGL_ARG("    basicrate(hex): ", priv->pmib->dot11Bss.basicrate, "%x");
	PRINT_SINGL_ARG("    supportrate(hex): ", priv->pmib->dot11Bss.supportrate, "%x");
	PRINT_ARRAY_ARG("    bdsa: ", priv->pmib->dot11Bss.bdsa, "%02x", MACADDRLEN);
	PRINT_SINGL_ARG("    rssi: ", priv->pmib->dot11Bss.rssi, "%d");
	PRINT_SINGL_ARG("    sq: ", priv->pmib->dot11Bss.sq, "%d");

	return pos;
}
#ifdef CONFIG_RTL_WLAN_DIAGNOSTIC
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_diagnostic(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_diagnostic(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	PRINT_ONE(diag_log_buff, "%s", 1);
	return pos;
}
#endif
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_erp(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_erp(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  ERP info...", "%s", 1);
	PRINT_SINGL_ARG("    protection: ", priv->pmib->dot11ErpInfo.protection, "%d");
	PRINT_SINGL_ARG("    nonErpStaNum: ", priv->pmib->dot11ErpInfo.nonErpStaNum, "%d");
	PRINT_SINGL_ARG("    olbcDetected: ", priv->pmib->dot11ErpInfo.olbcDetected, "%d");
	PRINT_SINGL_ARG("    olbcExpired: ", priv->pmib->dot11ErpInfo.olbcExpired, "%d");
	PRINT_SINGL_ARG("    shortSlot: ", priv->pmib->dot11ErpInfo.shortSlot, "%d");
	PRINT_SINGL_ARG("    ctsToSelf: ", priv->pmib->dot11ErpInfo.ctsToSelf, "%d");
	PRINT_SINGL_ARG("    longPreambleStaNum: ", priv->pmib->dot11ErpInfo.longPreambleStaNum, "%d");

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_cam_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_cam_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;
	unsigned char TempOutputMac[6];
	unsigned char TempOutputKey[16];
	unsigned short TempOutputCfg=0;
	
	if (!(priv->drv_state & DRV_STATE_OPEN))
		return 0;

	PRINT_ONE("  CAM info...", "%s", 1);
	PRINT_ONE("    CAM occupied: ", "%s", 0);
	PRINT_ONE(priv->pshare->CamEntryOccupied, "%d", 1);
	for (i=0; i < priv->pshare->total_cam_entry; i++)
	{
		PRINT_ONE("    Entry", "%s", 0);
		PRINT_ONE(i, " %2d:", 0);
		CAM_read_entry(priv,i,TempOutputMac,TempOutputKey,&TempOutputCfg);
		PRINT_ARRAY_ARG(" MAC addr: ", TempOutputMac, "%02x", 6);
		PRINT_SINGL_ARG("              Config: ", TempOutputCfg, "%x");
		PRINT_ARRAY_ARG("              Key: ", TempOutputKey, "%02x", 16);
	}

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_probe_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_probe_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	int pos = 0, i, idx = 1;
	unsigned long timeout=0;
#ifdef WLAN_DIAGNOSTIC
	PRINT_ONE("  Probe request info...", "%s", 1);
	if(priv->pmib->dot11StationConfigEntry.probe_info_enable == 2){
		for (i=0; i<MAX_PROBE_REQ_STA; i++)
		{
			timeout = jiffies - priv->probe_sta[i].time_stamp;
			if(priv->probe_sta[i].used && timeout <= RTL_SECONDS_TO_JIFFIES(priv->pmib->dot11StationConfigEntry.probe_info_timeout)){
				PRINT_ONE("    Entry", "%s", 0);
				PRINT_ONE(idx++, " %2d:", 1);
				PRINT_SINGL_ARG("	    Channel: ", priv->probe_sta[i].channel, "%d");
				PRINT_ARRAY_ARG("	    MAC addr: ", priv->probe_sta[i].addr, "%02x", MACADDRLEN);
				PRINT_SINGL_ARG("	    Signal strength: ", 100-priv->probe_sta[i].rssi, "-%2d dB");
				PRINT_SINGL_ARG("	    Age timer: ", RTL_JIFFIES_TO_SECOND(timeout), "%lu s");
			}
		}
	}else if(priv->pmib->dot11StationConfigEntry.probe_info_enable == 1){
		PRINT_ONE("    Entry occupied:", "%s", 0);
		PRINT_ONE(priv->ProbeReqEntryOccupied, "%d", 1);
		
		for (i=0; i<priv->ProbeReqEntryOccupied; i++)
		{
			PRINT_ONE("    Entry", "%s", 0);
			PRINT_ONE(priv->probe_sta[i].Entry, " %2d:", 1);
			PRINT_ARRAY_ARG("	    MAC addr: ", priv->probe_sta[i].addr, "%02x", MACADDRLEN);
			PRINT_SINGL_ARG("	    Signal strength: ",100-priv->probe_sta[i].rssi, "-%2d dB");			
		}
	}
#endif
	return pos;
}

#ifdef CONFIG_CMCC_FEATURE_SUPPORT
const char *bw_20m = "20M\0";
const char *bw_40m = "40M\0";
const char *bw_80m = "80M\0";
const char *bw_160m = "160M\0";
const char *bw_80_80m = "80+80M\0";

const char *mode_11a = "A";
const char *mode_11b = "B";
const char *mode_11g = "G";
const char *mode_11n = "N";
const char *mode_11ac = "AC";
const char *mode_11ax = "AX";

static const char *bw_to_str(unsigned char bw)
{
	switch (bw)
	{
		case TX_USE_80M_80M_MODE:
			return bw_80_80m;
		case TX_USE_160M_MODE:
			return bw_160m;
		case TX_USE_80M_MODE:
			return bw_80m;
		case TX_USE_40M_MODE:
			return bw_40m;
		default:
			return bw_20m;
	}
}

static const char *mode_to_str(unsigned char mode)
{
	switch (mode)
	{
		case WIRELESS_11AX:
			return mode_11ax;
		case WIRELESS_11AC:
			return mode_11ac;
		case WIRELESS_11N:
			return mode_11n;
		case WIRELESS_11A:
			return mode_11a;
		case WIRELESS_11G:
			return mode_11g;
		default:
			return mode_11b;
	}
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_sta_cap_table(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_sta_cap_table(char *buf, char **start, off_t offset,
		int length, int *eof, void *data)
#endif
{
	struct net_device	*dev = PROC_GET_DEV();
	struct rtl8192cd_priv	*priv = GET_DEV_PRIV(dev);
	struct sta_max_cap	*sta_cap_entry = priv->sta_cap_table;
	unsigned char *addr = NULL;
	int 	i = 0;

	printk("idx  STA_MAC_address   Mode    BW   Assoc\n");
	printk("-----------------------------------------\n");
	for (i = 0; i < NR_STA_CAP; i++) {
		addr = sta_cap_entry->addr;
		printk("%3d  %02X:%02X:%02X:%02X:%02X:%02X %4s %6s %6s\n",
				i, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5],
				mode_to_str(sta_cap_entry->mode), bw_to_str(sta_cap_entry->bw),
				(sta_cap_entry->assoc ? "T" : ""));

		sta_cap_entry++;
	}

	return 0;
}
#endif /* CONFIG_CMCC_FEATURE_SUPPORT */

#ifdef STA_ASSOC_STATISTIC
#ifdef CONFIG_RTL_PROC_NEW
int rtl8192cd_proc_reject_assoc_info(struct seq_file *s, void *data)
#else	
static int rtl8192cd_proc_reject_assoc_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif	
{
	struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;

	PRINT_ONE("  Reject STA info...", "%s", 1);
	PRINT_ONE("  	Entry occupied:", "%s", 0);
	PRINT_ONE(priv->RejectAssocEntryOccupied, "%d", 1);
	
	for (i=0; i<priv->RejectAssocEntryOccupied; i++)
		{
			PRINT_ONE("    Entry", "%s", 0);
			PRINT_ONE(priv->reject_sta[i].Entry, " %2d:", 1);
			PRINT_ARRAY_ARG("	    MAC addr: ", priv->reject_sta[i].addr, "%02x", MACADDRLEN);
			PRINT_SINGL_ARG("	    RSSI: ", priv->reject_sta[i].rssi, "%02d");			
		}
	for (i=0; i<MAX_PROBE_REQ_STA; i++)
		{		
			priv->reject_sta[i].used = 0;			
			priv->RejectAssocEntryOccupied = 0;
		}
	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
int rtl8192cd_proc_rm_assoc_info(struct seq_file *s, void *data)
#else	
static int rtl8192cd_proc_rm_assoc_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif	
{
	struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;

	PRINT_ONE("  Removed STA info...", "%s", 1);
	PRINT_ONE("  	Entry occupied:", "%s", 0);
	PRINT_ONE(priv->RemoveAssocEntryOccupied, "%d", 1);
	
	for (i=0; i<priv->RemoveAssocEntryOccupied; i++)
		{
			PRINT_ONE("    Entry", "%s", 0);
			PRINT_ONE(priv->removed_sta[i].Entry, " %2d:", 1);
			PRINT_ARRAY_ARG("	    MAC addr: ", priv->removed_sta[i].addr, "%02x", MACADDRLEN);
			PRINT_SINGL_ARG("	    RSSI: ",priv->removed_sta[i].rssi, "%02d");		
		}
	for (i=0; i<MAX_PROBE_REQ_STA; i++)
		{		
			priv->removed_sta[i].used = 0;			
			priv->RemoveAssocEntryOccupied = 0;
		}
	return pos;
}


#ifdef CONFIG_RTL_PROC_NEW
int rtl8192cd_proc_assoc_status_info(struct seq_file *s, void *data)
#else	
static int rtl8192cd_proc_assoc_status_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif	
{
	struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;

	PRINT_ONE("  STA assoc status...", "%s", 1);
	PRINT_ONE("   MAC\t\tDate\t\t\t\tStep\tStatus\tRSSI\tReversed", "%s", 1);
	for (i=0; i<priv->AssocStatusEntryOccupied; i++)
	{
		struct list_head *phead = &(priv->assoc_sta[i].act_list);
		struct list_head *plist = phead->next;
		struct sta_assoc_act *act;
		if (!list_empty(phead)) {
			while(plist != phead)
			{
				if(!plist) {
					panic_printk("phead:%x, n", phead);
					break;
				}
				act = list_entry(plist, struct sta_assoc_act, act_list);
				plist = plist->next;
				PRINT_ONE("   ", "%s", 0);
				PRINT_ARRAY(priv->assoc_sta[i].addr, "%02x", MACADDRLEN, 0);
				PRINT_ONE(act->time_info.tm_year+TM_YEAR_BASE, "\t%u", 0);
				PRINT_ONE(act->time_info.tm_mon+1, "-%02d", 0);
				PRINT_ONE(act->time_info.tm_mday, "-%02d", 0);
				PRINT_ONE(act->time_info.tm_hour, " %02d", 0);
				PRINT_ONE(act->time_info.tm_min, ":%02d", 0);
				PRINT_ONE(act->time_info.tm_sec, ":%02d.", 0); 					
				PRINT_ONE(act->time_info.tm_yday, "%03d\t\t", 0);
				PRINT_ONE(act->step, "%02d\t", 0);
				PRINT_ONE(act->status, "%02d\t", 0);
				PRINT_ONE(act->rssi, "%02d", 0);
				if( act->Reserved[0] != 0xff) {
					PRINT_ONE("\t", "%s", 0);
					PRINT_ARRAY(act->Reserved, "%02x", MACADDRLEN, 1);
				} else {
					PRINT_ONE("", "%s", 1);
				}
			}
		}
	}
#if 0
	for (i=0; i<MAX_PROBE_REQ_STA; i++)
	{		
			priv->assoc_sta[i].used = 0;			
			priv->AssocStatusEntryOccupied = 0;
	}
#endif
	return pos;
}
#endif

#ifdef PROC_STA_CONN_FAIL_INFO
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_sta_conn_fail(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_sta_conn_fail(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	int pos = 0, i;

	PRINT_ONE("  STA conn fail info...", "%s", 1);

	for (i=0; i<64; i++) {
		if (priv->sta_conn_fail[i].used) {
			PRINT_ARRAY_ARG("	    MAC addr: ", priv->sta_conn_fail[i].addr, "%02x", MACADDRLEN);
			PRINT_SINGL_ARG("	    Error state: ", priv->sta_conn_fail[i].error_state, "%d");
		}
	}
	memset(priv->sta_conn_fail, 0, sizeof(struct sta_conn_fail_info) * 64);

	return pos;
}
#endif

#ifdef BSS_MONITOR_SUPPORT
#ifdef CONFIG_RTL_PROC_NEW
int rtl8192cd_proc_monitor_info(struct seq_file *s, void *data)
#else	
static int rtl8192cd_proc_monitor_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif	
{
#ifdef CONFIG_RTL_PROC_NEW
	struct net_device *dev = (struct net_device *)(s->private);
#else
	struct net_device *dev = (struct net_device *)data;
#endif
#ifdef NETDEV_NO_PRIV
		struct rtl8192cd_priv *priv = ((struct rtl8192cd_priv *)netdev_priv(dev))->wlan_priv;
#else
		struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)dev->priv;
#endif
	int pos = 0, i;
	PRINT_ONE("  Motinor info...", "%s", 1);
		for (i=0; i<MAX_MONITOR_STA; i++)
		{
			if(priv->pshare->monitor_sta[i].used && priv->pshare->monitor_sta[i].type==1) {
				struct list_head *phead = &(priv->pshare->monitor_sta[i].asoc_list);
				struct list_head *plist = phead->next;
				struct monitor_station *pmsta;
				PRINT_ONE("BSS:\t     ", "%s", 0); 
				PRINT_ARRAY(priv->pshare->monitor_sta[i].addr, "%02x", MACADDRLEN, 0);
				PRINT_ONE("\tSSID: ", "%s", 0);
				if(priv->pshare->monitor_sta[i].ssid_len) {
					PRINT_ONE(priv->pshare->monitor_sta[i].ssid, "%32s", 0); 
				} else {
					PRINT_ONE("(Unrecongnized AP)", "%32s", 0);
				}
				PRINT_ONE("\tRSSI: ", "%s", 0); 
				PRINT_ONE(priv->pshare->monitor_sta[i].rssi, "%02d", 0); 
				PRINT_ONE("\tChannel: ", "%s", 0); 
				PRINT_ONE(priv->pshare->monitor_sta[i].channel ? priv->pshare->monitor_sta[i].channel:priv->pmib->dot11RFEntry.dot11channel, "%d", 0);				
				PRINT_ONE("\tTx bytes: ", "%s", 0); 
				PRINT_ONE(priv->pshare->monitor_sta[i].txbyte, "%u", 1); 
				
				if (!list_empty(phead)) {
					while(plist != phead)
					{
						if(!plist) {
							panic_printk("phead:%p, n", phead);
							break;
						}
						pmsta = list_entry(plist, struct monitor_station, asoc_list);
						plist = plist->next;
						PRINT_ONE("\tsta: ", "%s", 0); \
						PRINT_ARRAY(pmsta->addr, "%02x", MACADDRLEN, 0); \
						PRINT_ONE("\t      ", "%s", 0);
						PRINT_ONE("", "%32s", 0);
						PRINT_ONE("\tRSSI: ", "%s", 0); \
						PRINT_ONE(pmsta->rssi, "%02d", 0); \
						PRINT_ONE("\t\t\tTx bytes: ", "%s", 0); \
						PRINT_ONE(pmsta->txbyte, "%u", 1); 	
					}
				}
		}
		}
	
	return pos;
}

static int rtl8192cd_proc_monitor_info_clear(struct file *file, const char *buffer,
				unsigned long count, void *data)
{
	struct net_device *dev = (struct net_device *)data;
#ifdef NETDEV_NO_PRIV
		struct rtl8192cd_priv *priv = ((struct rtl8192cd_priv *)netdev_priv(dev))->wlan_priv;
#else
		struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)dev->priv;
#endif

	int i;

	for (i=0; i<MAX_MONITOR_STA; i++)
		priv->pshare->monitor_sta[i].used =0;
	
#ifdef __KERNEL__
					return count;
#endif
}
#endif

#ifdef CONFIG_RTK_BAND_STEERING
#ifdef CONFIG_RTL_PROC_NEW
int rtl8192cd_proc_get_band_steering_block_list(struct seq_file *s, void *data)
#else	
static int rtl8192cd_proc_get_band_steering_block_list(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif	
{
	struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	struct b_steer_block_entry *ent = NULL;
	unsigned char tmpbuf[32];
	unsigned int i = 0;
	int pos = 0;

	if (!IS_DRV_OPEN(priv))
		return pos;

	PRINT_ONE("==========================", "%s", 1);
	PRINT_ONE("[BAND_STEERING] block_list", "%s", 1);
	PRINT_ONE("==========================", "%s", 1);

	for (i = 0; i < B_STEER_ENTRY_NUM; i++) {
		ent = &(priv->bsteerpriv.block_list[i]);
		if (ent->used) {
			sprintf(tmpbuf, "    mac: %02x:%02x:%02x:%02x:%02x:%02x",
				ent->mac[0], ent->mac[1], ent->mac[2],
				ent->mac[3], ent->mac[4], ent->mac[5]);
			PRINT_ONE(tmpbuf, "%s", 1);
			PRINT_SINGL_ARG("    entry_expire: ", ent->entry_expire, "%u");
			PRINT_ONE("---------------------------", "%s", 1);
		}
	}

	return pos;
}
#endif

#ifdef RTK_MULTI_AP
#ifdef CONFIG_RTL_PROC_NEW
int rtl8192cd_proc_get_map_block_list(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_get_map_block_list(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	struct assoc_control_block_list *ent = NULL;
	unsigned char tmpbuf[32];
	unsigned int i = 0;
	int pos = 0;

	if (!IS_DRV_OPEN(priv))
		return pos;

	PRINT_ONE("map_block_list:", "%s", 1);

	for (i = 0; i < MAX_TRANS_LIST_NUM; i++) {
		if ((priv->block_list_bitmask[i>>3] & (1<<(i&7))) != 0) {
			ent = &(priv->block_list[i]);
			if (ent->timer) {
				sprintf(tmpbuf, "%02x:%02x:%02x:%02x:%02x:%02x %u",
					ent->addr[0], ent->addr[1], ent->addr[2],
					ent->addr[3], ent->addr[4], ent->addr[5],
					ent->timer);
				PRINT_ONE(tmpbuf, "%s", 1);
			}
		}
	}

	return pos;
}
#endif

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
static int read_sta_info_down;
#endif
#ifdef STA_RATE_STATISTIC
const u2Byte CCK_OFDM[12] = {1, 2, 5, 11, /*CCK*/
							6, 9, 12, 18, 24, 36, 48, 54/*OFDM*/};
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_sta_rateinfo(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_sta_rateinfo(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int len = 0, rc=1, j;
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
	off_t begin = 0;
	off_t pos = 0;
#endif
	int size, num=0;
	struct list_head *phead, *plist;
	struct stat_info *pstat;
	
#if !defined(SMP_SYNC) || (defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI))        
	unsigned long flags=0;
#endif
	
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
	if (offset == 0) // first calling, reset read_sta_info_down variable
		read_sta_info_down = 0;
	
	// first, kernel call me with length=3072; second, kernel call me with length=1024,
	// third, , kernel call me with length < 1024, I just return 0 when length < 1024 to avoid wasting time.
	if (length < 1024) {
		return 0;
	}
	
	// do not waste time again
	// I sent *eof=1 last time, I do not know why the kernel will call me again.
	if (read_sta_info_down) {
		*eof = 1;
		return 0;
	}
#endif

	SAVE_INT_AND_CLI(flags);
    SMP_LOCK_ASOC_LIST(flags);

	phead = &priv->asoc_list;
    if (!(priv->drv_state & DRV_STATE_OPEN) || list_empty(phead)) {
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
        *eof = 1;
#endif
        goto _ret;
    }

	PRINT_ONE("STA Rate Info...", "%s", 1);
	
	plist = phead->next;
	while (plist != phead) {
		pstat = list_entry(plist, struct stat_info, asoc_list);
		PRINT_ARRAY_ARG("  STA: ", pstat->cmn_info.mac_addr, "%02x", MACADDRLEN);

		PRINT_ONE("  Tx Data Rate ", "%s", 1);
		for(j=0; j<STA_RATE_NUM; j++){
			if(pstat->txrate_stat[j]!=0){					
				if(j<CCK_OFDM_RATE_NUM){//CCK:0~3, OFDM:4~11
					if(j<4){
						PRINT_ONE("    CCK ", "%s", 0);
						PRINT_ONE(CCK_OFDM[j], "%d: ", 0);
						PRINT_ONE(pstat->txrate_stat[j], "%d", 1);
					}else{
						PRINT_ONE("    OFDM ", "%s", 0);
						PRINT_ONE(CCK_OFDM[j], "%d: ", 0);
						PRINT_ONE(pstat->txrate_stat[j], "%d", 1);
					}						
				}else if(j>=CCK_OFDM_RATE_NUM && j<(CCK_OFDM_RATE_NUM+HT_RATE_NUM)){//MCS0~15 MCS16~31
					PRINT_ONE("    HT MCS", "%s", 0);
					PRINT_ONE((j-CCK_OFDM_RATE_NUM), "%d: ", 0);
					PRINT_ONE(pstat->txrate_stat[j], "%d", 1);
				}else{//NSS1 MCS0~9, NSS2 MCS0~9, NSS3 MCS0~9
					PRINT_ONE("    VHT NSS", "%s", 0);
					PRINT_ONE((j-(CCK_OFDM_RATE_NUM+HT_RATE_NUM))/10+1, "%d MCS", 0);
					PRINT_ONE((j-(CCK_OFDM_RATE_NUM+HT_RATE_NUM))%10, "%d: ", 0);
					PRINT_ONE(pstat->txrate_stat[j], "%d", 1);
				}					
			}
		}

		PRINT_ONE("  Rx Data Rate ", "%s", 1);
		for(j=0; j<STA_RATE_NUM; j++){
			if(pstat->rxrate_stat[j]!=0){					
				if(j<CCK_OFDM_RATE_NUM){//CCK:0~3, OFDM:4~11
					if(j<4){
						PRINT_ONE("    CCK ", "%s", 0);
						PRINT_ONE(CCK_OFDM[j], "%d: ", 0);
						PRINT_ONE(pstat->rxrate_stat[j], "%d", 1);
					}else{
						PRINT_ONE("    OFDM ", "%s", 0);
						PRINT_ONE(CCK_OFDM[j], "%d: ", 0);
						PRINT_ONE(pstat->rxrate_stat[j], "%d", 1);
					}						
				}else if(j>=CCK_OFDM_RATE_NUM && j<(CCK_OFDM_RATE_NUM+HT_RATE_NUM)){//MCS0~15 MCS16~31
					PRINT_ONE("    HT MCS", "%s", 0);
					PRINT_ONE((j-CCK_OFDM_RATE_NUM), "%d: ", 0);
					PRINT_ONE(pstat->rxrate_stat[j], "%d", 1);
				}else{//NSS1 MCS0~9, NSS2 MCS0~9, NSS3 MCS0~9
					PRINT_ONE("    VHT NSS", "%s", 0);
					PRINT_ONE((j-(CCK_OFDM_RATE_NUM+HT_RATE_NUM))/10+1, "%d MCS", 0);
					PRINT_ONE((j-(CCK_OFDM_RATE_NUM+HT_RATE_NUM))%10, "%d: ", 0);
					PRINT_ONE(pstat->rxrate_stat[j], "%d", 1);
				}					
			}
		}

		memset(pstat->txrate_stat, 0x0, sizeof(pstat->txrate_stat));
		memset(pstat->rxrate_stat, 0x0, sizeof(pstat->rxrate_stat));

		num++;
		plist = plist->next;
	}

_ret:
	
	SMP_UNLOCK_ASOC_LIST(flags);
	RESTORE_INT(flags);

	return len;
}
#endif

//ACS_SCORE_PROC
#ifdef CONFIG_RTL_PROC_NEW
int rtl8192cd_proc_acs_score_info(struct seq_file *s, void *data)
#else							
static int rtl8192cd_proc_acs_score_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;
	int channel_index=0;
	unsigned int max_score=0;

#if defined(UNIVERSAL_REPEATER) || defined(MBSSID)
	if (!IS_ROOT_INTERFACE(priv))
		return pos;
#endif

	unsigned char tmpbuf[32];
	memset(tmpbuf, 0x0, 32);
	unsigned char buf_chan[16] = {0};
	unsigned char buf_score[16] = {0};

	if (priv->ss_req_ongoing) {
		PRINT_ONE("  SiteSurvey in progress...", "%s", 1);	
	} else {

#if 0	
		//get max_score
		for (channel_index=0; channel_index<priv->available_chnl_num; channel_index++)  {
				if(priv->acs_score[channel_index].score > max_score && priv->acs_score[channel_index].score != 0xffffffff) 
					max_score = priv->acs_score[channel_index].score;
		}
		if(max_score == 0)
			max_score = 1;
		//calcuate relative value
		if(priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G){
			for (channel_index=0; channel_index<priv->available_chnl_num; channel_index++)  {
				if(priv->acs_score[channel_index].score == 0xffffffff)
					priv->acs_score[channel_index].relative_value = 100;
				else if(max_score == 248)
					priv->acs_score[channel_index].relative_value = 0;
				else
					priv->acs_score[channel_index].relative_value = (unsigned long int)priv->acs_score[channel_index].score*95/max_score;
			}
		}
		else if(priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_5G){
			for (channel_index=0; channel_index<priv->available_chnl_num; channel_index++)  {
				if(priv->acs_score[channel_index].score == 0xffffffff)
					priv->acs_score[channel_index].relative_value = 100;
				else
					priv->acs_score[channel_index].relative_value = (unsigned long int)priv->acs_score[channel_index].score*95/max_score;
					
			}
			
		}
#endif
		if(priv->autoch_only_ss_clear_ch!=0) {
			sprintf(tmpbuf, "  ACS Result: CH:%d 2nd_offset:%d", priv->autoch_only_ss_clear_ch, priv->autoch_only_ss_clear_2ndch);
			PRINT_ONE(tmpbuf, "%s", 1);			
		} else {
			sprintf(tmpbuf, "  Original: CH:%d 2nd_offset:%d", priv->pmib->dot11RFEntry.dot11channel, priv->pshare->offset_2nd_chan);
			PRINT_ONE(tmpbuf, "%s", 1);	
		}
		PRINT_ONE("  Channel Score", "%s", 1);
		PRINT_ONE("  ===============", "%s", 1);

		for (i=0; i<priv->available_chnl_num; i++) {
			//if(priv->acs_score[i].channel != 0)
			{
				//memset(tmpbuf, 0x0, 32);
				//sprintf(tmpbuf, "  %d:%d", priv->acs_score[i].channel, priv->acs_score[i].relative_value);
				//PRINT_ONE(tmpbuf, "%s", 1);
				snprintf(buf_chan, sizeof(buf_chan), "%d", priv->available_chnl[i]);
				snprintf(buf_score, sizeof(buf_score), "%d", priv->chnl_ss_score[i]);
				memset(tmpbuf, 0x0, 32);
				sprintf(tmpbuf, "  %s:%s", buf_chan, buf_score);
				PRINT_ONE(tmpbuf, "%s", 1);
			}
		}
	}

	return pos;
}

#ifdef HS2_SUPPORT
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_hs2(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_hs2(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;

	PRINT_ONE("  HS2 info...", "%s", 1);
	PRINT_SINGL_ARG("    hs_enable: ", priv->pmib->hs2Entry.hs_enable, "%d");
	
	PRINT_ARRAY_ARG("    hs2_ie: ",
			priv->pmib->hs2Entry.hs2_ie, "%02x", priv->pmib->hs2Entry.hs2_ielen);
	PRINT_SINGL_ARG("    hs2_ielen: ", priv->pmib->hs2Entry.hs2_ielen, "%d");

	PRINT_SINGL_ARG("    proxy arp:	", priv->proxy_arp, "%d");
	PRINT_SINGL_ARG("    dgaf_disable:	",priv->dgaf_disable, "%d");
	PRINT_SINGL_ARG("    OSU_Present:	",priv->OSU_Present, "%d");
	
	PRINT_ARRAY_ARG("    interworking_ie: ",
			priv->pmib->hs2Entry.interworking_ie, "%02x", priv->pmib->hs2Entry.interworking_ielen);
	PRINT_SINGL_ARG("    interworking_ielen: ", priv->pmib->hs2Entry.interworking_ielen, "%d");

	PRINT_ARRAY_ARG("    QoSMap_ie[0]: ",
			priv->pmib->hs2Entry.QoSMap_ie[0], "%02x", priv->pmib->hs2Entry.QoSMap_ielen[0]);
	PRINT_SINGL_ARG("    QoSMap_ielen[0]: ", priv->pmib->hs2Entry.QoSMap_ielen[0], "%d");
	PRINT_ARRAY_ARG("    QoSMap_ie[1]: ",
			priv->pmib->hs2Entry.QoSMap_ie[1], "%02x", priv->pmib->hs2Entry.QoSMap_ielen[1]);
	PRINT_SINGL_ARG("    QoSMap_ielen[1]: ", priv->pmib->hs2Entry.QoSMap_ielen[1], "%d");
	
	PRINT_ARRAY_ARG("    advt_proto_ie: ",
			priv->pmib->hs2Entry.advt_proto_ie, "%02x", priv->pmib->hs2Entry.advt_proto_ielen);
	PRINT_SINGL_ARG("    advt_proto_ielen: ", priv->pmib->hs2Entry.advt_proto_ielen, "%d");

	PRINT_ARRAY_ARG("    roam_ie: ",
			priv->pmib->hs2Entry.roam_ie, "%02x", priv->pmib->hs2Entry.roam_ielen);
	PRINT_SINGL_ARG("    roam_ielen: ", priv->pmib->hs2Entry.roam_ielen, "%d");

	PRINT_ARRAY_ARG("    timeadvt_ie: ",
			priv->pmib->hs2Entry.timeadvt_ie, "%02x", priv->pmib->hs2Entry.timeadvt_ielen);
	PRINT_SINGL_ARG("    timeadvt_ielen: ", priv->pmib->hs2Entry.timeadvt_ielen, "%d");

	PRINT_ARRAY_ARG("    timezone_ie: ",
			priv->pmib->hs2Entry.timezone_ie, "%02x", priv->pmib->hs2Entry.timezone_ielen);
	PRINT_SINGL_ARG("    timezone_ielen: ", priv->pmib->hs2Entry.timezone_ielen, "%d");

	PRINT_ARRAY_ARG("    MBSSID_ie: ",
			priv->pmib->hs2Entry.MBSSID_ie, "%02x", priv->pmib->hs2Entry.MBSSID_ielen);
	PRINT_SINGL_ARG("    MBSSID_ielen: ", priv->pmib->hs2Entry.MBSSID_ielen, "%d");	

	PRINT_ARRAY_ARG("    MBSSID_ie: ",
			priv->pmib->hs2Entry.MBSSID_ie, "%02x", priv->pmib->hs2Entry.MBSSID_ielen);
	PRINT_SINGL_ARG("    MBSSID_ielen: ", priv->pmib->hs2Entry.MBSSID_ielen, "%d");		

	PRINT_SINGL_ARG("    remedSvrURL: ", priv->pmib->hs2Entry.remedSvrURL, "%s");
	PRINT_SINGL_ARG("    SessionInfoURL: ", priv->pmib->hs2Entry.SessionInfoURL, "%s");

	PRINT_ARRAY_ARG("    bssload_ie: ",
			priv->pmib->hs2Entry.bssload_ie, "%02x", 5);
	PRINT_SINGL_ARG("    ICMPv4ECHO: ", priv->pmib->hs2Entry.ICMPv4ECHO, "%d");
	PRINT_SINGL_ARG("    block_relay: ", priv->pmib->dot11OperationEntry.block_relay, "%d");

	PRINT_SINGL_ARG("    80211W: ", priv->pmib->dot1180211AuthEntry.dot11IEEE80211W, "%d");
	PRINT_SINGL_ARG("    SHA256: ", priv->pmib->dot1180211AuthEntry.dot11EnableSHA256, "%d");

	return pos;
}
#endif // HS2_SUPPORT

#ifdef MULTI_MAC_CLONE
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mbidcam_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mbidcam_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;
	unsigned int CAM[2];
	unsigned char mac[6];
	
	if (!(priv->drv_state & DRV_STATE_OPEN))
		return 0;

	PRINT_ONE("  MBIDCAM info...", "%s", 1);
	PRINT_ONE("    REG MBCAM_NUM: ", "%s", 0);
	PRINT_ONE((RTL_R8(REG_MBID_NUM) & 0x7), "%d", 1);
	for (i=0; i<20; i++) {
		RTL_W32(REG_MBIDCAMCFG_2, 0x80000000|i<<24);
		CAM[0] = RTL_R32(REG_MBIDCAMCFG_1);
		CAM[1] = RTL_R32(REG_MBIDCAMCFG_2);
		PRINT_ONE("    Entry", "%s", 0);
		PRINT_ONE(i, " %2d:", 0);
		PRINT_ONE(" MAC addr: ", "%s", 0);
		mac[0] = CAM[0] & 0x000000ff;
		mac[1] = CAM[0]>>8 & 0x000000ff;
		mac[2] = CAM[0]>>16 & 0x000000ff;
		mac[3] = CAM[0]>>24 & 0x000000ff;
		mac[4] = CAM[1] & 0x000000ff;
		mac[5] = CAM[1]>>8 & 0x000000ff;
		PRINT_ARRAY_ARG("", mac, "%02x", 6);
	}

	return pos;
}
#endif

#ifdef WDS
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_wds(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_wds(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;
#ifdef __ECOS
	char tmpbuf[100];
#else
	unsigned char tmpbuf[100];
#endif

	PRINT_ONE("  WDS info...", "%s", 1);
	PRINT_SINGL_ARG("    wdsEnabled: ", priv->pmib->dot11WdsInfo.wdsEnabled, "%d");
	PRINT_SINGL_ARG("    wdsPure: ", priv->pmib->dot11WdsInfo.wdsPure, "%d");
	PRINT_SINGL_ARG("    wdsPriority: ", priv->pmib->dot11WdsInfo.wdsPriority, "%d");
	PRINT_SINGL_ARG("    wdsNum: ", priv->pmib->dot11WdsInfo.wdsNum, "%d");
	for (i=0; i<priv->pmib->dot11WdsInfo.wdsNum; i++) {
		snprintf(tmpbuf, sizeof(tmpbuf), "    wdsMacAddr[%d]: ", i);
		PRINT_ARRAY_ARG(tmpbuf, priv->pmib->dot11WdsInfo.entry[i].macAddr, "%02x", 6);
		PRINT_SINGL_ARG("    wdsTxRate: ", priv->pmib->dot11WdsInfo.entry[i].txRate, "0x%x");
	}
	PRINT_SINGL_ARG("    wdsPrivacy: ", priv->pmib->dot11WdsInfo.wdsPrivacy, "%d");
	PRINT_ARRAY_ARG("    wdsWepKey: ",
			priv->pmib->dot11WdsInfo.wdsWepKey, "%02x", 16);
#if defined(INCLUDE_WPA_PSK) || defined(WIFI_HAPD) || defined(RTK_NL80211)
	PRINT_SINGL_ARG("    wds_passphrase: ",
			priv->pmib->dot11WdsInfo.wdsPskPassPhrase, "%s");
#endif

	return pos;
}
#endif // WDS


#ifdef CONFIG_IEEE80211V
#ifdef CONFIG_RTL_PROC_NEW
int rtl8192cd_proc_transition_read(struct seq_file *s, void *data)
#else
int rtl8192cd_proc_transition_read(char *buf, char **start, off_t offset,
        int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	int i,j;
	struct stat_info *pstat;

	if((OPMODE & WIFI_AP_STATE) == 0) {
	    panic_printk("\nwarning: invalid command!\n");
	    return pos;
	}

	PRINT_ONE("-- Target Transition List --", "%s", 1);
	j = 1;
	for (i = 0 ; i < MAX_TRANS_LIST_NUM; i++) 
	{
		if((priv->transition_list_bitmask[i>>3] & (1<<(i&7))) == 0) 
		continue;

		pstat = get_stainfo(priv, priv->transition_list[i].addr);
		if(pstat) {
			PRINT_ONE(j, "  [%d]", 0);
			PRINT_ARRAY_ARG("STA:", priv->transition_list[i].addr, "%02x", MACADDRLEN);
			PRINT_ONE("    BSS Trans Rejection Count:", "%s", 0);
			PRINT_ONE(pstat->bssTransRejectionCount, "%d", 1);
			PRINT_ONE("    BSS Trans Trans Expired Time:", "%s", 0);
			PRINT_ONE(pstat->bssTransExpiredTime, "%d", 1);
			j++;
		}
	}
	
	return pos;
}

#define TRANS_LIST_PROC_LEN	50
#ifdef __ECOS
int rtl8192cd_proc_transition_write(char *tmp, void *data)
#else
int rtl8192cd_proc_transition_write(struct file *file, const char *buffer,
        unsigned long count, void *data)
#endif
{
#ifdef __ECOS
	return 0;
#else
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	unsigned char error_code = 0;
	char * tokptr;
	int command = 0;
	int empty_slot;
	int i;
	char tmp[TRANS_LIST_PROC_LEN];
	char *tmpptr;
	int oldest_idx;
	struct target_transition_list list;

	if((OPMODE & WIFI_AP_STATE) == 0) {
	    error_code = 1;
	    goto end;
	}
	if (count < 2 || count >= TRANS_LIST_PROC_LEN) {
	    return -EFAULT;
	}

	if (buffer == NULL || copy_from_user(tmp, buffer, count))
	    return -EFAULT;

	tmp[count] = 0;
	tmpptr = tmp;
	tmpptr = strsep((char **)&tmpptr, "\n");
	tokptr = strsep((char **)&tmpptr, " ");
	if(!memcmp(tokptr, "add", 3))
		command = 1;
	else if (!memcmp(tokptr, "delall", 6)) 
	   	command = 3;
	else if(!memcmp(tokptr, "del", 3))
       	command = 2;

	if(command) 
	{        
		if(command == 1 || command == 2) {
			tokptr = strsep((char **)&tmpptr," ");
			if(tokptr)
				get_array_val(list.addr, tokptr, 12);
			else {
				error_code = 1;
				goto end;
			}
		}
	    
		if(command == 1)   /*add*/
		{	  
			for(i = 0, empty_slot = -1; i < MAX_TRANS_LIST_NUM; i++)
			{
				if((priv->transition_list_bitmask[i>>3] & (1<<(i&7))) == 0) {
					if(empty_slot == -1)
						empty_slot = i;
				}else if(0 == memcmp(list.addr, priv->transition_list[i].addr, MACADDRLEN)) {
					break;
				}	
			}
				
			if(i == MAX_TRANS_LIST_NUM && empty_slot != -1) {/*not found, and has empty slot*/
				i = empty_slot;
			}		
			memcpy(&priv->transition_list[i], &list, sizeof(struct target_transition_list));
			priv->transition_list_bitmask[i>>3] |= (1<<(i&7));
		}
		else if(command == 3)   /*delete all*/
		{
			memset(priv->transition_list_bitmask, 0x00, sizeof(priv->transition_list_bitmask));
		}
		else if(command == 2)  /*delete*/
		{
			for (i = 0 ; i < MAX_TRANS_LIST_NUM; i++) {
				if((priv->transition_list_bitmask[i>>3] & (1<<(i&7))) == 0)
					continue;
			    
				if(0 == memcmp(list.addr, priv->transition_list[i].addr, MACADDRLEN)) {
					priv->transition_list_bitmask[i>>3] &= ~(1<<(i&7));
					break;
				}
			}
		}
	}
	else {
	    error_code = 1;
	    goto end;
	}

end:
	if(error_code == 1)
	    panic_printk("\nwarning: invalid command!\n");
	else if(error_code == 2)
	    panic_printk("\nwarning: neighbor report table full!\n");
	return count;
#endif
}
#endif


#ifdef RTK_BR_EXT
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_brext(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_brext(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  BR Ext info...", "%s", 1);
	PRINT_SINGL_ARG("    nat25_disable: ", priv->pmib->ethBrExtInfo.nat25_disable, "%d");
	PRINT_SINGL_ARG("    macclone_enable: ", priv->pmib->ethBrExtInfo.macclone_enable, "%d");
	PRINT_SINGL_ARG("    dhcp_bcst_disable: ", priv->pmib->ethBrExtInfo.dhcp_bcst_disable, "%d");
	PRINT_SINGL_ARG("    addPPPoETag: ", priv->pmib->ethBrExtInfo.addPPPoETag, "%d");
	PRINT_SINGL_ARG("    nat25sc_disable: ", priv->pmib->ethBrExtInfo.nat25sc_disable, "%d");
	PRINT_ARRAY_ARG("    ukpro_mac: ", priv->ukpro_mac, "%02x", MACADDRLEN);

	return pos;
}


#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_nat25filter_read(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_nat25filter_read(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;


    int i;
    unsigned short ethtype;
    unsigned char ipproto;

    PRINT_ONE(" -- NAT25 Filter info -- ", "%s", 1);
    PRINT_SINGL_ARG("    nat25_filter: ", priv->nat25_filter, "%d");

    PRINT_ONE("    Ethernet Type List:", "%s", 1);
    for (i = 0 ; i < NAT25_FILTER_ETH_NUM; i++) {
        ethtype = priv->nat25_filter_ethlist[i];
        if(ethtype == 0xFFFF)
            break;                
        PRINT_ONE(ethtype,  "\t%04X", ((i % 4) == 3)?1:0);
    }

    PRINT_ONE("", "%s", 1);
    PRINT_ONE("    IP Protocol List:", "%s", 1);
    for (i = 0 ; i < NAT25_FILTER_IPPROTO_NUM; i++) {
        ipproto = priv->nat25_filter_ipprotolist[i];
        if(ipproto == 0xFF)
            break;
        PRINT_ONE(ipproto,  "\t%02X", ((i % 4) == 3)?1:0);
    }
    PRINT_ONE("", "%s", 1);    
    return pos;


}

#ifdef __ECOS
static int rtl8192cd_proc_nat25filter_write(char *tmp, void *data)
#else
static int rtl8192cd_proc_nat25filter_write(struct file *file, const char *buffer,
				unsigned long count, void *data)
#endif
{
    
#ifdef __ECOS
    return 0;
#else
    struct net_device *dev = (struct net_device *)data;
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    unsigned char error_code = 0;
    char * tokptr;
    int command = 0;
    int type = 0; 
    int val;
    int i;

    tokptr = strsep((char **)&buffer, " ");
    if(!memcmp(tokptr, "add", 3))
        command = 1;
    else if(!memcmp(tokptr, "del", 3))
        command = 2;

    if(command) {         
        tokptr = strsep((char **)&buffer," ");
        if(!memcmp(tokptr, "ethtype", 7))
            type = 1;
        else if(!memcmp(tokptr, "ipproto", 7))
            type = 2;
        else {
            error_code = 1;
            goto end;
        }
        tokptr = strsep((char **)&buffer," ");
        val = _atoi(tokptr, 16);
        if(val > 0) {            
            if(type == 1) {/*ethtype*/
                if(command == 1) { /*add*/
                    for(i = 0; i < NAT25_FILTER_ETH_NUM; i++) {
                        if(priv->nat25_filter_ethlist[i] == 0xFFFF ||                          
                            priv->nat25_filter_ethlist[i] == val) {
                            break;
                        }
                    }
                    if(i < NAT25_FILTER_ETH_NUM) { /*found*/
                        priv->nat25_filter_ethlist[i] = val;
                    }
                    else {
                        error_code = 2;
                        goto end;
                    }
                }
                else if(command == 2) {/*delete*/
                    for(i = 0; i < NAT25_FILTER_ETH_NUM; i++) {
                        if(priv->nat25_filter_ethlist[i] == val) {
                            for(;i < NAT25_FILTER_ETH_NUM-1;i++) {
                                priv->nat25_filter_ethlist[i] = priv->nat25_filter_ethlist[i+1];
                            }
                            priv->nat25_filter_ethlist[NAT25_FILTER_ETH_NUM-1] = 0xFFFF;
                            break;
                        }
                    }
                }

            }
            else {/*ipproto*/
                if(command == 1) { /*add*/
                    for(i = 0; i < NAT25_FILTER_IPPROTO_NUM; i++) {
                        if(priv->nat25_filter_ipprotolist[i] == 0xFF ||                          
                            priv->nat25_filter_ipprotolist[i] == val) {
                            break;
                        }
                    }

                    if(i < NAT25_FILTER_IPPROTO_NUM) { /*found*/
                        priv->nat25_filter_ipprotolist[i] = val;
                    }
                    else {
                        error_code = 2;
                        goto end;
                    }
                }
                else if(command == 2) {/*delete*/
                    for(i = 0; i < NAT25_FILTER_IPPROTO_NUM; i++) {
                        if(priv->nat25_filter_ipprotolist[i] == val) {
                            for(;i < NAT25_FILTER_IPPROTO_NUM-1;i++) {
                                priv->nat25_filter_ipprotolist[i] = priv->nat25_filter_ipprotolist[i+1];
                            }
                            priv->nat25_filter_ipprotolist[NAT25_FILTER_IPPROTO_NUM-1] = 0xFF;
                            break;
                        }
                    }
                }
            }            
        }
        else {
            error_code = 1;
            goto end;
        }
    }
    else {
        val = tokptr[0] - '0';
        if(0 <= val && val <= 2) {            
            priv->nat25_filter = val;
        }
        else {
            error_code = 1;
            goto end;
        }
    }

end:
    if(error_code == 1) {
        panic_printk("\nwarning: unknown comman!\n");
    }
    else if(error_code == 2) {
        panic_printk("\nwarning: filter list is full!\n");
    }
    return count;
#endif	

}
#endif //RTK_BR_EXT

#ifdef MULTI_MAC_CLONE
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mstainfo(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mstainfo(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos=0, i;

	PRINT_ONE("  MAC Clone info...", "%s", 1);
	PRINT_SINGL_ARG("    MAX_MAC_CLONE_NUM: ", priv->pshare->mclone_num_max, "%d");
	PRINT_SINGL_ARG("    MCLONE_NUM: ", priv->pshare->mclone_num, "%d");
	PRINT_SINGL_ARG("    mclone_ok: ", priv->pshare->mclone_ok, "%d");

	PRINT_ONE("", "%s", 1);
	PRINT_ONE("  Multi-STA info...", "%s", 1);
	PRINT_ONE("     op   aid  ID Tuse hwaddr       sa_addr      intf", "%s", 1);
	for (i=0; i<priv->pshare->mclone_num_max; i++)
	{
		PRINT_ONE(i, "  %2d", 0);
		PRINT_ONE(priv->pshare->mclone_sta[i].opmode, " %-4x", 0);
		PRINT_ONE(priv->pshare->mclone_sta[i].aid, " %-4x", 0);
		PRINT_ONE(priv->pshare->mclone_sta[i].usedStaAddrId, " %-2x", 0);
		PRINT_ONE(priv->pshare->mclone_sta[i].isTimerInit, " %-4d", 0);
		PRINT_ONE(" ", "%s", 0);
		PRINT_ARRAY(priv->pshare->mclone_sta[i].hwaddr, "%02x", 6, 0);
		PRINT_ONE(" ", "%s", 0);
		PRINT_ARRAY(priv->pshare->mclone_sta[i].sa_addr, "%02x", 6, 0);
		if (priv->pshare->mclone_sta[i].priv==NULL) {
			PRINT_ONE("n/a", " %s", 1);
		}
		else {
			PRINT_ONE(priv->pshare->mclone_sta[i].priv->dev->name, " %s", 1);
		}
	}

	PRINT_ONE("", "%s", 1);
	PRINT_ONE("  Multi-STA Address Pool", "%s", 1);
	for (i=0; i<priv->pshare->mclone_num_max; i++)
	{
		PRINT_ONE(i, "  %2d:", 0);
		PRINT_ARRAY_ARG(" MAC addr: ", priv->pshare->mclone_sta_fixed_addr[i].clone_addr, "%02x", 6);
	}

	return pos;
}
#endif

#ifdef CONFIG_PCI_HCI
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_txdesc_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_txdesc_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    struct rtl8192cd_hw *phw;
    unsigned long *txdescptr;
    unsigned int q_num = priv->txdesc_num;

    int i, len = 0;
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    off_t begin = 0;
    off_t pos = 0;
    int size;
#endif

    phw = GET_HW(priv);
#ifdef __ECOS
    ecos_pr_fun("  Tx queue %d descriptor ..........\n", q_num);
#elif defined(CONFIG_RTL_PROC_NEW)
    seq_printf(s, "  Tx queue %d descriptor ..........\n", q_num);
#else
    size = snprintf(buf, length, "  Tx queue %d descriptor ..........\n", q_num);
    CHECK_LEN;
#endif
#ifdef  CONFIG_WLAN_HAL
    if (IS_HAL_CHIP(priv)) {
        GET_HAL_INTERFACE(priv)->DumpTxBDescTestHandler(priv,
#ifdef CONFIG_RTL_PROC_NEW
            s,
#endif
            q_num);
    } else if(CONFIG_WLAN_NOT_HAL_EXIST)
#endif
    {//not HAL
        if (get_txdesc(phw, q_num)) {
#ifdef __ECOS
            ecos_pr_fun("  tx_desc%d/physical: 0x%.8lx/0x%.8lx\n", q_num, (unsigned long)get_txdesc(phw, q_num),
                    *(unsigned long *)(((unsigned long)&phw->tx_ring0_addr)+sizeof(unsigned long)*q_num));
            ecos_pr_fun("  head/tail: %3d/%-3d  DW0      DW1      DW2      DW3      DW4      DW5\n",
                    get_txhead(phw, q_num), get_txtail(phw, q_num));
#elif defined(CONFIG_RTL_PROC_NEW)
            seq_printf(s, "  tx_desc%d/physical: 0x%.8lx/0x%.8lx\n", q_num, (unsigned long)get_txdesc(phw, q_num),
                    *(unsigned long *)(((unsigned long)&phw->tx_ring0_addr)+sizeof(unsigned long)*q_num));
            seq_printf(s, "  head/tail: %3d/%-3d  DW0      DW1      DW2      DW3      DW4      DW5\n",
                    get_txhead(phw, q_num), get_txtail(phw, q_num));
#else
            size = snprintf(buf+len, length-len, "  tx_desc%d/physical: 0x%.8lx/0x%.8lx\n", q_num, (unsigned long)get_txdesc(phw, q_num),
                    *(unsigned long *)(((unsigned long)&phw->tx_ring0_addr)+sizeof(unsigned long)*q_num));
            CHECK_LEN;
            size = snprintf(buf+len, length-len, "  head/tail: %3d/%-3d  DW0      DW1      DW2      DW3      DW4      DW5\n",
                    get_txhead(phw, q_num), get_txtail(phw, q_num));
            CHECK_LEN;
#endif
            for (i=0; i<CURRENT_NUM_TX_DESC; i++) {
                txdescptr = (unsigned long *)(get_txdesc(phw, q_num) + i);
#ifdef __ECOS
                ecos_pr_fun("%d[%3d]: %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x\n", q_num, i,
                        (UINT)get_desc(txdescptr[0]), (UINT)get_desc(txdescptr[1]),
                        (UINT)get_desc(txdescptr[2]), (UINT)get_desc(txdescptr[3]),
                        (UINT)get_desc(txdescptr[4]), (UINT)get_desc(txdescptr[5]),
                        (UINT)get_desc(txdescptr[6]), (UINT)get_desc(txdescptr[7]),
                        (UINT)get_desc(txdescptr[8]), (UINT)get_desc(txdescptr[9])
                    );
#elif defined(CONFIG_RTL_PROC_NEW)
                seq_printf(s, "%d[%3d]: %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x\n", q_num, i,
                        (UINT)get_desc(txdescptr[0]), (UINT)get_desc(txdescptr[1]),
                        (UINT)get_desc(txdescptr[2]), (UINT)get_desc(txdescptr[3]),
                        (UINT)get_desc(txdescptr[4]), (UINT)get_desc(txdescptr[5]),
                        (UINT)get_desc(txdescptr[6]), (UINT)get_desc(txdescptr[7]),
                        (UINT)get_desc(txdescptr[8]), (UINT)get_desc(txdescptr[9])
                    );
#else
                size = snprintf(buf+len, length-len, "%d[%3d]: %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x\n", q_num, i,
                        (UINT)get_desc(txdescptr[0]), (UINT)get_desc(txdescptr[1]),
                        (UINT)get_desc(txdescptr[2]), (UINT)get_desc(txdescptr[3]),
                        (UINT)get_desc(txdescptr[4]), (UINT)get_desc(txdescptr[5]),
                        (UINT)get_desc(txdescptr[6]), (UINT)get_desc(txdescptr[7]),
                        (UINT)get_desc(txdescptr[8]), (UINT)get_desc(txdescptr[9])
                    );
                CHECK_LEN;
#endif
            }
        }
    }

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    *eof = 1;
_ret:
    *start = buf + (offset - begin);	/* Start of wanted data */
    len -= (offset - begin);	/* Start slop */
    if (len > length)
        len = length;	/* Ending slop */
#endif

    return len;

}


#ifdef CONFIG_RTL_KERNEL_MIPS16_WLAN
__NOMIPS16
#endif
#ifdef __ECOS
void rtl8192cd_proc_txdesc_idx_write(int txdesc_num, void *data)
#else
static int rtl8192cd_proc_txdesc_idx_write(struct file *file, const char *buffer,
		unsigned long count, void *data)
#endif
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
#ifdef __KERNEL__
	char tmp[32];
#endif

#ifdef __ECOS
	priv->txdesc_num = txdesc_num;
	if (priv->txdesc_num > 5) {
		ecos_pr_fun("Invalid tx desc number!\n");
		priv->txdesc_num = 0;
	}
	else
		ecos_pr_fun("Ready to dump tx desc %d\n", priv->txdesc_num);
#else
	if (count < 2)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, 32)) {
		int num = sscanf(tmp, "%d", &priv->txdesc_num);

		if (num != 1)
			panic_printk("Invalid tx desc number!\n");
#ifdef  CONFIG_WLAN_HAL
		else if (priv->txdesc_num >= HCI_TX_DMA_QUEUE_MAX_NUM) 
#else
        else if (priv->txdesc_num > 5) 
#endif
		{
			panic_printk("Invalid tx desc number!\n");
			priv->txdesc_num = 0;
		}
		else
			panic_printk("Ready to dump tx desc %d\n", priv->txdesc_num);
	}
	return count;
#endif
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_rxdesc_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_rxdesc_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    struct rtl8192cd_hw *phw;
    unsigned long *rxdescptr;

    int i, len = 0;
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    off_t begin = 0;
    off_t pos = 0;
    int size;
#endif

#ifdef CONFIG_WLAN_HAL	
    if (IS_HAL_CHIP(priv)) {
        DumpRxBDesc88XX(priv,
#ifdef CONFIG_RTL_PROC_NEW
            s,
#endif
            0);
        return 0;
    } else
#endif
    {
        phw = GET_HW(priv);
#ifdef __ECOS
        ecos_pr_fun("  Rx queue descriptor ..........\n");
#elif defined(CONFIG_RTL_PROC_NEW)
        seq_printf(s, "  Rx queue descriptor ..........\n");
#else
        size = snprintf(buf+len, length-len, "  Rx queue descriptor ..........\n");
        CHECK_LEN;
#endif
        if(phw->rx_descL){
#ifdef __ECOS
            ecos_pr_fun("  rx_descL/physical: 0x%.8lx/0x%.8lx\n", (unsigned long)phw->rx_descL, phw->rx_ring_addr);
            #ifdef DELAY_REFILL_RX_BUF
            ecos_pr_fun("  cur_rx/cur_rx_refill: %d/%d\n", phw->cur_rx, phw->cur_rx_refill);
            #else
            ecos_pr_fun("  cur_rx: %d\n", phw->cur_rx);
            #endif
#elif defined(CONFIG_RTL_PROC_NEW)
            seq_printf(s, "  rx_descL/physical: 0x%.8lx/0x%.8lx\n", (unsigned long)phw->rx_descL, phw->rx_ring_addr);
            #ifdef DELAY_REFILL_RX_BUF
            seq_printf(s, "  cur_rx/cur_rx_refill: %d/%d\n", phw->cur_rx, phw->cur_rx_refill);
            #else
            seq_printf(s, "  cur_rx: %d\n", phw->cur_rx);
            #endif
#else
            size = snprintf(buf+len, length-len, "  rx_descL/physical: 0x%.8lx/0x%.8lx\n", (unsigned long)phw->rx_descL, phw->rx_ring_addr);
            CHECK_LEN;
            #ifdef DELAY_REFILL_RX_BUF
            size = snprintf(buf+len, length-len, "  cur_rx/cur_rx_refill: %d/%d\n", phw->cur_rx, phw->cur_rx_refill);
            #else
            size = snprintf(buf+len, length-len, "  cur_rx: %d\n", phw->cur_rx);
            #endif
            CHECK_LEN;
#endif
            for(i=0; i<NUM_RX_DESC_IF(priv); i++) {
                rxdescptr = (unsigned long *)(phw->rx_descL+i);
#ifdef __ECOS
                ecos_pr_fun("      rxdesc[%02d]: 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x\n", i,
                    (UINT)get_desc(rxdescptr[0]), (UINT)get_desc(rxdescptr[1]),
                    (UINT)get_desc(rxdescptr[2]), (UINT)get_desc(rxdescptr[3]),
                    (UINT)get_desc(rxdescptr[4]), (UINT)get_desc(rxdescptr[5]),
                    (UINT)get_desc(rxdescptr[6]), (UINT)get_desc(rxdescptr[7]));
#elif defined(CONFIG_RTL_PROC_NEW)
                seq_printf(s, "	   rxdesc[%02d]: 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x\n", i,
                    (UINT)get_desc(rxdescptr[0]), (UINT)get_desc(rxdescptr[1]),
                    (UINT)get_desc(rxdescptr[2]), (UINT)get_desc(rxdescptr[3]),
                    (UINT)get_desc(rxdescptr[4]), (UINT)get_desc(rxdescptr[5]),
                    (UINT)get_desc(rxdescptr[6]), (UINT)get_desc(rxdescptr[7]));
#else
                size = snprintf(buf+len, length-len, "      rxdesc[%02d]: 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x 0x%.8x\n", i,
                    (UINT)get_desc(rxdescptr[0]), (UINT)get_desc(rxdescptr[1]),
                    (UINT)get_desc(rxdescptr[2]), (UINT)get_desc(rxdescptr[3]),
                    (UINT)get_desc(rxdescptr[4]), (UINT)get_desc(rxdescptr[5]),
                    (UINT)get_desc(rxdescptr[6]), (UINT)get_desc(rxdescptr[7]));
                CHECK_LEN;
#endif
            }
        }

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
        *eof = 1;
_ret:
        *start = buf + (offset - begin);	/* Start of wanted data */
        len -= (offset - begin);	/* Start slop */
        if (len > length)
            len = length;	/* Ending slop */
#endif
        return len;
    }
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_desc_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_desc_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	struct rtl8192cd_hw *phw = GET_HW(priv);
	int pos = 0;

	
#ifdef  CONFIG_WLAN_HAL
	if (IS_HAL_CHIP(priv)) {

		PHCI_RX_DMA_MANAGER_88XX    prx_dma = (PHCI_RX_DMA_MANAGER_88XX)(_GET_HAL_DATA(priv)->PRxDMA88XX);		
		PHCI_TX_DMA_MANAGER_88XX    ptx_dma = (PHCI_TX_DMA_MANAGER_88XX)(_GET_HAL_DATA(priv)->PTxDMA88XX);	
		PHCI_RX_DMA_QUEUE_STRUCT_88XX	rx_q   = &(prx_dma->rx_queue[0]);
		PHCI_TX_DMA_QUEUE_STRUCT_88XX	tx_q   = &(ptx_dma->tx_queue[MGNT_QUEUE]);
		
		PRINT_ONE("  descriptor info...", "%s", 1);
		PRINT_ONE("    RX queue:", "%s", 1);
		PRINT_ONE(" 	 RDSAR: ", "%s", 0);
		PRINT_ONE((UINT)RTL_R32(RX_DESA), "0x%.8x", 0);
	
		PRINT_ONE("  hwIdx/hostIdx/curHostIdx: ", "%s", 0);
		PRINT_ONE((UINT)rx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)rx_q->host_idx, "%d/", 0);
		PRINT_ONE((UINT)rx_q->cur_host_idx, "%d", 1);
	
		PRINT_ONE("    queue 0:", "%s", 1);
		PRINT_ONE(" 	 TMGDA: ", "%s", 0);
#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4){
            PRINT_ONE((UINT)RTL_R32(REG_P0MGQ_TXBD_DESA_L), "0x%.8x", 0);
        }
#endif   
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3){
		PRINT_ONE((UINT)RTL_R32(REG_MGQ_TXBD_DESA), "0x%.8x", 0);
        }
#endif
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);

		tx_q   = &(ptx_dma->tx_queue[BK_QUEUE]);
		PRINT_ONE("    queue 1:", "%s", 1);
		PRINT_ONE(" 	 TBKDA: ", "%s", 0);
#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4){        
            PRINT_ONE((UINT)RTL_R32(REG_ACH3_TXBD_DESA_L), "0x%.8x", 0);
        }
#endif   
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3){
		PRINT_ONE((UINT)RTL_R32(REG_BKQ_TXBD_DESA), "0x%.8x", 0);
        }
#endif
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);

		tx_q   = &(ptx_dma->tx_queue[BE_QUEUE]);
		PRINT_ONE("    queue 2:", "%s", 1);
		PRINT_ONE(" 	 TBEDA: ", "%s", 0);
#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4){
            PRINT_ONE((UINT)RTL_R32(REG_ACH2_TXBD_DESA_L), "0x%.8x", 0);    
        }
#endif    
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3){
		PRINT_ONE((UINT)RTL_R32(REG_BEQ_TXBD_DESA), "0x%.8x", 0);
        }
#endif
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);

		tx_q   = &(ptx_dma->tx_queue[VI_QUEUE]);
		PRINT_ONE("    queue 3:", "%s", 1);
		PRINT_ONE(" 	 TVIDA: ", "%s", 0);
#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4){
            PRINT_ONE((UINT)RTL_R32(REG_ACH1_TXBD_DESA_L), "0x%.8x", 0);
        }
#endif    
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3){
		PRINT_ONE((UINT)RTL_R32(REG_VIQ_TXBD_DESA), "0x%.8x", 0);
        }
#endif
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);

		tx_q   = &(ptx_dma->tx_queue[VO_QUEUE]);
		PRINT_ONE("    queue 4:", "%s", 1);
		PRINT_ONE(" 	 TVODA: ", "%s", 0);
#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4){
            PRINT_ONE((UINT)RTL_R32(REG_ACH0_TXBD_DESA_L), "0x%.8x", 0);
        }
#endif   
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3){
		PRINT_ONE((UINT)RTL_R32(REG_VOQ_TXBD_DESA), "0x%.8x", 0);
        }
#endif
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);

#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4)
            tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE_V2]);
#endif
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3)
            tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE]);
        PRINT_ONE((UINT)RTL_R32(REG_HI0Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("    queue 5:", "%s", 1);
		PRINT_ONE(" 	 TH0DA: ", "%s", 0);
		PRINT_ONE((UINT)RTL_R32(REG_HI0Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);
#endif

#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4)
            tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE1_V2]);
#endif
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3)
		tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE1]);
        PRINT_ONE((UINT)RTL_R32(REG_HI1Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("    queue 6:", "%s", 1);
		PRINT_ONE(" 	 TH1DA: ", "%s", 0);
		PRINT_ONE((UINT)RTL_R32(REG_HI1Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);
#endif

#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4)
            tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE2_V2]);
#endif
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3)
		tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE2]);
        PRINT_ONE((UINT)RTL_R32(REG_HI2Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("    queue 7:", "%s", 1);
		PRINT_ONE(" 	 TH2DA: ", "%s", 0);
		PRINT_ONE((UINT)RTL_R32(REG_HI2Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);
#endif

#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4)
            tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE3_V2]);
#endif
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3)
		tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE3]);
        PRINT_ONE((UINT)RTL_R32(REG_HI3Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("    queue 8:", "%s", 1);
		PRINT_ONE(" 	 TH3DA: ", "%s", 0);
		PRINT_ONE((UINT)RTL_R32(REG_HI3Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);		
#endif

#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4)
            tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE4_V2]);
#endif
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3)
		tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE4]);
        PRINT_ONE((UINT)RTL_R32(REG_HI4Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("    queue 9:", "%s", 1);
		PRINT_ONE(" 	 TH4DA: ", "%s", 0);
		PRINT_ONE((UINT)RTL_R32(REG_HI4Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);
#endif		
		
#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4)
            tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE5_V2]);
#endif        
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3)
		tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE5]);
        PRINT_ONE((UINT)RTL_R32(REG_HI5Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("    queue 10:", "%s", 1);
		PRINT_ONE(" 	 TH5DA: ", "%s", 0);
		PRINT_ONE((UINT)RTL_R32(REG_HI5Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);
#endif			
		
#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4)
            tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE6_V2]);
#endif        
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3)
		tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE6]);
        PRINT_ONE((UINT)RTL_R32(REG_HI6Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("    queue 11:", "%s", 1);
		PRINT_ONE(" 	 TH6DA: ", "%s", 0);
		PRINT_ONE((UINT)RTL_R32(REG_HI6Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);
#endif			
		
#if IS_RTL88XX_MAC_V4
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v4)
            tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE7_V2]);
#endif        
#if IS_RTL88XX_MAC_V1_V2_V3
        if(_GET_HAL_DATA(priv)->MacVersion.is_MAC_v1_v2_v3)
		tx_q   = &(ptx_dma->tx_queue[HIGH_QUEUE7]);
        PRINT_ONE((UINT)RTL_R32(REG_HI7Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("    queue 12:", "%s", 1);
		PRINT_ONE(" 	 TH7DA: ", "%s", 0);
		PRINT_ONE((UINT)RTL_R32(REG_HI7Q_TXBD_DESA), "0x%.8x", 0);
		PRINT_ONE("  hwIdx/hostIdx: ", "%s", 0);
		PRINT_ONE((UINT)tx_q->hw_idx, "%d/", 0);
		PRINT_ONE((UINT)tx_q->host_idx, "%d", 1);	
#endif

	} else if(CONFIG_WLAN_NOT_HAL_EXIST)
#endif	
	{//not HAL
	PRINT_ONE("  descriptor info...", "%s", 1);
	PRINT_ONE("    RX queue:", "%s", 1);
	PRINT_ONE("      rx_descL/physical: ", "%s", 0);
	PRINT_ONE((unsigned long)phw->rx_descL, "0x%.8lx/", 0);
	PRINT_ONE(phw->rx_ring_addr, "0x%.8lx", 1);
	PRINT_ONE("      RDSAR: ", "%s", 0);
	PRINT_ONE((UINT)RTL_R32(RX_DESA), "0x%.8x", 0);

#ifdef DELAY_REFILL_RX_BUF
	PRINT_ONE("  cur_rx/cur_rx_refill: ", "%s", 0);
	PRINT_ONE((UINT)phw->cur_rx, "%d/", 0);
	PRINT_ONE((UINT)phw->cur_rx_refill, "%d", 1);
#else
	PRINT_ONE("  cur_rx: ", "%s", 0);
	PRINT_ONE((UINT)phw->cur_rx, "%d", 1);
#endif

	PRINT_ONE("    queue 0:", "%s", 1);
	PRINT_ONE("      tx_desc0/physical: ", "%s", 0);
	PRINT_ONE((unsigned long)phw->tx_desc0, "0x%.8lx/", 0);
	PRINT_ONE(phw->tx_ring0_addr, "0x%.8lx", 1);
	PRINT_ONE("      TMGDA: ", "%s", 0);
	PRINT_ONE((UINT)RTL_R32(MGQ_DESA), "0x%.8x", 0);
	PRINT_ONE("  head/tail: ", "%s", 0);
	PRINT_ONE((UINT)phw->txhead0, "%d/", 0);
	PRINT_ONE((UINT)phw->txtail0, "%d", 1);

	PRINT_ONE("    queue 1:", "%s", 1);
	PRINT_ONE("      tx_desc1/physical: ", "%s", 0);
	PRINT_ONE((unsigned long)phw->tx_desc1, "0x%.8lx/", 0);
	PRINT_ONE(phw->tx_ring1_addr, "0x%.8lx", 1);
	PRINT_ONE("      TBKDA: ", "%s", 0);
	PRINT_ONE((UINT)RTL_R32(BKQ_DESA), "0x%.8x", 0);
	PRINT_ONE("  head/tail: ", "%s", 0);
	PRINT_ONE((UINT)phw->txhead1, "%d/", 0);
	PRINT_ONE((UINT)phw->txtail1, "%d", 1);

	PRINT_ONE("    queue 2:", "%s", 1);
	PRINT_ONE("      tx_desc2/physical: ", "%s", 0);
	PRINT_ONE((unsigned long)phw->tx_desc2, "0x%.8lx/", 0);
	PRINT_ONE(phw->tx_ring2_addr, "0x%.8lx", 1);
	PRINT_ONE("      TBEDA: ", "%s", 0);
	PRINT_ONE((UINT)RTL_R32(BEQ_DESA), "0x%.8x", 0);
	PRINT_ONE("  head/tail: ", "%s", 0);
	PRINT_ONE((UINT)phw->txhead2, "%d/", 0);
	PRINT_ONE((UINT)phw->txtail2, "%d", 1);

	PRINT_ONE("    queue 3:", "%s", 1);
	PRINT_ONE("      tx_desc3/physical: ", "%s", 0);
	PRINT_ONE((unsigned long)phw->tx_desc3, "0x%.8lx/", 0);
	PRINT_ONE(phw->tx_ring3_addr, "0x%.8lx", 1);
	PRINT_ONE("      TLPDA: ", "%s", 0);
	PRINT_ONE((UINT)RTL_R32(VIQ_DESA), "0x%.8x", 0);
	PRINT_ONE("  head/tail: ", "%s", 0);
	PRINT_ONE((UINT)phw->txhead3, "%d/", 0);
	PRINT_ONE((UINT)phw->txtail3, "%d", 1);

	PRINT_ONE("    queue 4:", "%s", 1);
	PRINT_ONE("      tx_desc4/physical: ", "%s", 0);
	PRINT_ONE((unsigned long)phw->tx_desc4, "0x%.8lx/", 0);
	PRINT_ONE(phw->tx_ring4_addr, "0x%.8lx", 1);
	PRINT_ONE("      TNPDA: ", "%s", 0);
	PRINT_ONE((UINT)RTL_R32(VOQ_DESA), "0x%.8x", 0);
	PRINT_ONE("  head/tail: ", "%s", 0);
	PRINT_ONE((UINT)phw->txhead4, "%d/", 0);
	PRINT_ONE((UINT)phw->txtail4, "%d", 1);

	PRINT_ONE("    queue 5:", "%s", 1);
	PRINT_ONE("      tx_desc5/physical: ", "%s", 0);
	PRINT_ONE((unsigned long)phw->tx_desc5, "0x%.8lx/", 0);
	PRINT_ONE(phw->tx_ring5_addr, "0x%.8lx", 1);
	PRINT_ONE("      THPDA: ", "%s", 0);
	PRINT_ONE((UINT)RTL_R32(HQ_DESA), "0x%.8x", 0);
	PRINT_ONE("  head/tail: ", "%s", 0);
	PRINT_ONE((UINT)phw->txhead5, "%d/", 0);
	PRINT_ONE((UINT)phw->txtail5, "%d", 1);
	}
#if 0
	PRINT_ONE("    RX cmd queue:", "%s", 1);
	PRINT_ONE("      rxcmd_desc/physical: ", "%s", 0);
	PRINT_ONE((UINT)phw->rxcmd_desc, "0x%.8x/", 0);
	PRINT_ONE((UINT)phw->rxcmd_ring_addr, "0x%.8x", 1);
	PRINT_ONE("      RCDSA: ", "%s", 0);
	PRINT_ONE((UINT)RTL_R32(_RCDSA_), "0x%.8x", 0);
	PRINT_ONE("  cur_rx: ", "%s", 0);
	PRINT_ONE((UINT)phw->cur_rxcmd, "%d", 1);

	PRINT_ONE("    TX cmd queue:", "%s", 1);
	PRINT_ONE("      txcmd_desc/physical: ", "%s", 0);
	PRINT_ONE((UINT)phw->txcmd_desc, "0x%.8x/", 0);
	PRINT_ONE((UINT)phw->txcmd_ring_addr, "0x%.8x", 1);
	PRINT_ONE("      TCDA:  ", "%s", 0);
	PRINT_ONE((UINT)RTL_R32(_TCDA_), "0x%.8x", 0);
	PRINT_ONE("  head/tail: ", "%s", 0);
	PRINT_ONE((UINT)phw->txcmdhead, "%d/", 0);
	PRINT_ONE((UINT)phw->txcmdtail, "%d", 1);
#endif
	return pos;
}
#endif // CONFIG_PCI_HCI

#ifdef CONFIG_USB_HCI
void usb_cancel_pending_urb(struct rtl8192cd_priv *priv, int q_num)
{
	_queue *urb_queue;
	_list *phead, *plist;
	_irqL irqL;
	
	struct xmit_buf *pxmitbuf;
	struct urb *purb;
	int retval;
	
	printk("[%s] q_num=%d\n", __FUNCTION__, q_num);
	
	urb_queue = &priv->pshare->tx_urb_waiting_queue[q_num];
	phead = get_list_head(urb_queue);
	plist = NULL;
	
	_enter_critical(&urb_queue->lock, &irqL);
	
	plist = get_next(phead);
	
	while (rtw_end_of_queue_search(phead, plist) == FALSE) {
		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, tx_urb_list);
		plist = get_next(plist);
		
		purb = pxmitbuf->pxmit_urb;
		retval = usb_unlink_urb(purb);
		if (-EINPROGRESS == retval) {
			printk("usb_unlink_urb() succeed.\n");
		} else {
			printk("usb_unlink_urb() fail!(retval=%d, usb->status=%d\n",
				retval , purb->status);
		}
	}
	
	_exit_critical(&urb_queue->lock, &irqL);
}

#define UNLINK_URB_BASE		10
#define CLEAR_ENDPOINT_HALT_BASE	20
static int rtl8192cd_proc_txurb_info_idx_write(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	char tmp[32];
	int num, cmd_no, retval;
	
	if (count < 2)
		return -EFAULT;
	
	if (count > sizeof(tmp))
		return -EINVAL;
	
	if (buffer && !copy_from_user(tmp, buffer, count)) {
		tmp[count-1] = '\0';
		num = sscanf(tmp, "%d", &cmd_no);
		
		if (num != 1) {
			panic_printk("Invalid tx desc number!\n");
		} else if ((cmd_no >= 0) && (cmd_no < MAX_HW_TX_QUEUE)) {
			priv->txdesc_num = cmd_no;
			panic_printk("Ready to dump tx desc %d\n", priv->txdesc_num);
		} else if ((cmd_no >= UNLINK_URB_BASE) && (cmd_no < (UNLINK_URB_BASE+MAX_HW_TX_QUEUE))) {
			usb_cancel_pending_urb(priv, cmd_no-UNLINK_URB_BASE);
		} else if ((cmd_no >= CLEAR_ENDPOINT_HALT_BASE) && (cmd_no < (CLEAR_ENDPOINT_HALT_BASE+MAX_HW_TX_QUEUE))) {
			retval = usb_clear_halt(priv->pshare->pusbdev, ffaddr2pipehdl(priv, cmd_no-CLEAR_ENDPOINT_HALT_BASE));
			if (0 == retval) {
				printk("usb_clear_halt() succeed for queue %d.\n", cmd_no-CLEAR_ENDPOINT_HALT_BASE);
			} else {
				printk("usb_clear_halt() fail!(retval=%d)\n", retval);
			}
		} else {
			panic_printk("Invalid tx desc number!(%d)\n", cmd_no);
		}
	}
	return count;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_txurb_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_txurb_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	unsigned int q_num = priv->txdesc_num;
	
	_queue *urb_queue;
	_list *phead, *plist;
	_irqL irqL;
	
	struct xmit_buf *pxmitbuf;
	struct urb *purb;
	struct tx_desc_info *pdescinfo;
	struct tx_desc *pdesc;
	int index = 0;
	
	int retval;
	u16 status;

	int len = 0;
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
	off_t begin = 0;
	off_t pos = 0;
	int size;
#endif
	int i;

#if defined(CONFIG_RTL_TRIBAND_SUPPORT)
	retval = usb_get_status(priv->pshare->pusbdev, USB_RECIP_ENDPOINT,
		GET_HAL_INTF_DATA(priv)->Queue2Pipe[q_num], &status);
#else
	retval = usb_get_status(priv->pshare->pusbdev, USB_RECIP_ENDPOINT,
		GET_HAL_INTF_DATA(priv)->Queue2EPNum[q_num], &status);
#endif

	if (retval == 2) {
		printk("usb_get_status()=0x%02X for queue %d\n", status, q_num);
	}
	
	urb_queue = &priv->pshare->tx_urb_waiting_queue[q_num];
	phead = get_list_head(urb_queue);
	
	_enter_critical(&urb_queue->lock, &irqL);
	
	plist = get_next(phead);
	
	while (rtw_end_of_queue_search(phead, plist) == FALSE) {
		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, tx_urb_list);
		plist = get_next(plist);
		
		purb = pxmitbuf->pxmit_urb;
		pdescinfo = pxmitbuf->txdesc_info;
		
#if defined(CONFIG_RTL_PROC_NEW)
		seq_printf(s, "[urb%d] status=%d, transfer_flags=0x%X\n", index++, purb->status, purb->transfer_flags);
		seq_printf(s, "   [xmitbuf] q_num=%d, head=%p, data=%p, tail=%p, end=%p\n",
			pxmitbuf->q_num, pxmitbuf->pkt_head, pxmitbuf->pkt_data, pxmitbuf->pkt_tail, pxmitbuf->pkt_end);
#else
		size = snprintf(buf+len, length-len, "[urb%d] status=%d, transfer_flags=0x%X\n", index++, purb->status, purb->transfer_flags);
		CHECK_LEN;
		
		size = snprintf(buf+len, length-len, "   [xmitbuf] q_num=%d, head=%p, data=%p, tail=%p, end=%p\n",
			pxmitbuf->q_num, pxmitbuf->pkt_head, pxmitbuf->pkt_data, pxmitbuf->pkt_tail, pxmitbuf->pkt_end);
		CHECK_LEN;
#endif

		for (i = 0; i < pxmitbuf->agg_num; ++i) {	
			pdesc = (struct tx_desc *)pdescinfo[i].buf_ptr;
#if defined(CONFIG_RTL_PROC_NEW)
		#if defined(CONFIG_RTL_TRIBAND_SUPPORT)
			seq_printf(s, "   [pkt%02d] buf_prt=%p, buf_len=%d\n", i, pdescinfo[i].buf_ptr, pdescinfo[i].buf_len[0]);
		#else
			seq_printf(s, "   [pkt%02d] buf_prt=%p, buf_len=%d\n", i, pdescinfo[i].buf_ptr, pdescinfo[i].buf_len);
		#endif
			seq_printf(s, "      txdesc: %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x\n",
					(u32)get_desc(pdesc->Dword0), (u32)get_desc(pdesc->Dword1),
					(u32)get_desc(pdesc->Dword2), (u32)get_desc(pdesc->Dword3),
					(u32)get_desc(pdesc->Dword4), (u32)get_desc(pdesc->Dword5),
					(u32)get_desc(pdesc->Dword6), (u32)get_desc(pdesc->Dword7));
#else
			size = snprintf(buf+len, length-len, "   [pkt%02d] buf_prt=%p, buf_len=%d\n", i, pdescinfo[i].buf_ptr, pdescinfo[i].buf_len);
			CHECK_LEN;
			size = snprintf(buf+len, length-len, "      txdesc: %.8x %.8x %.8x %.8x %.8x %.8x %.8x %.8x\n",
					(u32)get_desc(pdesc->Dword0), (u32)get_desc(pdesc->Dword1),
					(u32)get_desc(pdesc->Dword2), (u32)get_desc(pdesc->Dword3),
					(u32)get_desc(pdesc->Dword4), (u32)get_desc(pdesc->Dword5),
					(u32)get_desc(pdesc->Dword6), (u32)get_desc(pdesc->Dword7));
			CHECK_LEN;
#endif
		}
	}
	
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
	*eof = 1;
#endif
	
_ret:
	_exit_critical(&urb_queue->lock, &irqL);

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)	
	*start = buf + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);	/* Start slop */
	if (len > length)
		len = length;	/* Ending slop */
#endif
	return len;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_que_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_que_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
#ifdef MBSSID
	int i;
	struct rtl8192cd_priv *priv_vap;
#endif
	
	PRINT_ONE("    free_xmit_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->free_xmit_queue.qlen, "%3d", 0);
	PRINT_ONE(NR_XMITFRAME, "/%d", 0);
	PRINT_ONE(priv->pshare->nr_out_of_xmitframe, " (fail:%d)", 1);
	PRINT_ONE("    free_xmitbuf_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->free_xmitbuf_queue.qlen, "%2d", 0);
	PRINT_ONE(NR_XMITBUFF, "/%d", 1);
	PRINT_ONE("    free_xmit_extbuf_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->free_xmit_extbuf_queue.qlen, "%2d", 0);
	PRINT_ONE(NR_XMIT_EXTBUFF, "/%d", 1);
	PRINT_ONE("    free_urg_xmitbuf_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->free_urg_xmitbuf_queue.qlen, "%2d", 0);
	PRINT_ONE(NR_URG_XMITBUFF, "/%d", 1);
	PRINT_ONE("    use_hw_queue_bitmap: ", "%s", 0);
	PRINT_ONE(priv->pshare->use_hw_queue_bitmap, "%08lX", 1);
	PRINT_ONE("    stop_netif_tx_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->stop_netif_tx_queue, "%d", 0);
	PRINT_ONE(priv->pshare->nr_stop_netif_tx_queue, " (%d)", 1);
	PRINT_ONE("    tx_mc_queue:", "%s", 1);
	PRINT_ONE("      q_num: ", "%s", 0);
	PRINT_ONE(priv->tx_mc_queue.q_num, "%d", 0);
	PRINT_ONE(" tx_pending: ", "%s", 0);
	PRINT_ONE(!rtw_is_list_empty(&priv->tx_mc_queue.tx_pending), "%d", 0);
	PRINT_ONE("  qlen: ", "%s", 0);
	PRINT_ONE(tx_servq_len(&priv->tx_mc_queue), "%d", 1);
#ifdef MBSSID
	if (priv->pmib->miscEntry.vap_enable) {
		for (i = 0; i < RTL8192CD_NUM_VWLAN; i++) {
			priv_vap = priv->pvap_priv[i];
			if (IS_DRV_OPEN(priv_vap)) {
				PRINT_ONE(priv_vap->vap_id, " [va%d]q_num: ", 0);
				PRINT_ONE(priv_vap->tx_mc_queue.q_num, "%d", 0);
				PRINT_ONE(" tx_pending: ", "%s", 0);
				PRINT_ONE(!rtw_is_list_empty(&priv_vap->tx_mc_queue.tx_pending), "%d", 0);
				PRINT_ONE("  qlen: ", "%s", 0);
				PRINT_ONE(tx_servq_len(&priv_vap->tx_mc_queue), "%d", 1);
			}
		}
	}
#endif // MBSSID
	PRINT_ONE("    tx_mgnt_queue:", "%s", 1);
	PRINT_ONE("      q_num: ", "%s", 0);
	PRINT_ONE(priv->tx_mgnt_queue.q_num, "%d", 0);
	PRINT_ONE(" tx_pending: ", "%s", 0);
	PRINT_ONE(!rtw_is_list_empty(&priv->tx_mgnt_queue.tx_pending), "%d", 0);
	PRINT_ONE("  qlen: ", "%s", 0);
	PRINT_ONE(tx_servq_len(&priv->tx_mgnt_queue), "%d", 1);
#ifdef MBSSID
	if (priv->pmib->miscEntry.vap_enable) {
		for (i = 0; i < RTL8192CD_NUM_VWLAN; i++) {
			priv_vap = priv->pvap_priv[i];
			if (IS_DRV_OPEN(priv_vap)) {
				PRINT_ONE(priv_vap->vap_id, " [va%d]q_num: ", 0);
				PRINT_ONE(priv_vap->tx_mgnt_queue.q_num, "%d", 0);
				PRINT_ONE(" tx_pending: ", "%s", 0);
				PRINT_ONE(!rtw_is_list_empty(&priv_vap->tx_mgnt_queue.tx_pending), "%d", 0);
				PRINT_ONE("  qlen: ", "%s", 0);
				PRINT_ONE(tx_servq_len(&priv_vap->tx_mgnt_queue), "%d", 1);
			}
		}
	}
#endif // MBSSID
	PRINT_ONE("    pspoll_sta_queue:", "%s", 1);
	PRINT_ONE("      q_num: ", "%s", 0);
	PRINT_ONE(priv->pshare->pspoll_sta_queue.q_num, "%d", 0);
	PRINT_ONE(" tx_pending: ", "%s", 0);
	PRINT_ONE(!rtw_is_list_empty(&priv->pshare->pspoll_sta_queue.tx_pending), "%d", 0);
	PRINT_ONE("  qlen: ", "%s", 0);
	PRINT_ONE(tx_servq_len(&priv->pshare->pspoll_sta_queue), "%d", 1);
	PRINT_ONE("    tx_urgent_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_urgent_queue.qlen, "%d", 1);
	PRINT_ONE("    tx_pending_sta_queue:", "%s", 1);
	PRINT_ONE("      MGQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[MGNT_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" BKQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[BK_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" BEQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[BE_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" VIQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[VI_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" VOQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[VO_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" HIQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[HIGH_QUEUE].qlen, "%-4d", 1);
	PRINT_ONE("    tx_urb_waiting_queue:", "%s", 1);
	PRINT_ONE("      MGQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_urb_waiting_queue[MGNT_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" BKQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_urb_waiting_queue[BK_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" BEQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_urb_waiting_queue[BE_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" VIQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_urb_waiting_queue[VI_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" VOQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_urb_waiting_queue[VO_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" HIQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_urb_waiting_queue[HIGH_QUEUE].qlen, "%-4d", 1);

	PRINT_ONE("    free_recv_queue: ", "%s", 0);
	PRINT_ONE(priv->recvpriv.free_recv_queue.qlen, "%d", 0);
	PRINT_ONE(NR_RECVFRAME, "/%d", 0);
	PRINT_ONE(priv->recvpriv.nr_out_of_recvframe, " (fail:%d)", 1);
	PRINT_ONE("    free_recv_buf_queue_cnt: ", "%s", 0);
	PRINT_ONE(priv->recvpriv.free_recv_buf_queue_cnt, "%d", 0);
	PRINT_ONE(NR_RECVBUFF, "/%d", 1);

	PRINT_ONE("    rx_pending_cnt: ", "%s", 0);
	PRINT_ONE(priv->recvpriv.rx_pending_cnt-1, "%d", 1);

#ifdef CONFIG_PREALLOC_RECV_SKB
	PRINT_ONE("    free_recv_skb_queue: ", "%s", 0);
	PRINT_ONE(skb_queue_len(&priv->recvpriv.free_recv_skb_queue), "%d", 0);
	PRINT_ONE(NR_PREALLOC_RECV_SKB, "/%d", 1);
#endif

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	PRINT_ONE("    recv_buf_pending_queue: ", "%s", 0);
	PRINT_ONE(priv->recvpriv.recv_buf_pending_queue.qlen, "%d", 1);
#else
	PRINT_ONE("    rx_skb_queue: ", "%s", 0);
	PRINT_ONE(skb_queue_len(&priv->recvpriv.rx_skb_queue), "%d", 1);
#endif
	
	PRINT_ONE("    wake_event: ", "%s", 0);
	PRINT_ONE(priv->pshare->cmd_wake, "%08lX", 1);
	PRINT_ONE("    cmd_queue: ", "%s", 1);
	PRINT_ONE("      qlen: ", "%s", 0);
	PRINT_ONE(priv->pshare->cmd_queue.qlen, "%-4d", 0);
	PRINT_ONE(" miss: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_cmd_miss, "%-4u", 0);
	PRINT_ONE(" done: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_cmd, "%u", 1);
	
	PRINT_ONE("    rx_mgt_queue: ", "%s", 1);
	PRINT_ONE("      qlen: ", "%s", 0);
	PRINT_ONE(priv->pshare->rx_mgt_queue.qlen, "%-4d", 0);
	PRINT_ONE(" miss: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_rx_mgt_cmd_miss, "%-4u", 0);
	PRINT_ONE(" done: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_rx_mgt_cmd, "%u", 1);
	
	PRINT_ONE("    timer_evt_queue: ", "%s", 1);
	PRINT_ONE("      qlen: ", "%s", 0);
	PRINT_ONE(priv->pshare->timer_evt_queue.qlen, "%-4d", 0);
	PRINT_ONE(" miss: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_timer_evt_miss, "%-4u", 0);
	PRINT_ONE(" done: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_timer_evt, "%u", 1);
	
#if defined(CONFIG_RTL_92D_SUPPORT) || defined(CONFIG_RTL_92C_SUPPORT)
	PRINT_ONE("    h2c_cmd_queue: ", "%s", 1);
	PRINT_ONE("      qlen: ", "%s", 0);
	PRINT_ONE(priv->pshare->h2c_cmd_queue.qlen, "%-4d", 0);
	PRINT_ONE(" miss: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_h2c_cmd_miss, "%-4u", 0);
	PRINT_ONE(" done: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_h2c_cmd, "%u", 1);
#endif
#ifdef CMD_THREAD_FUNC_DEBUG
	PRINT_ONE("    cur_cmd_func: ", "%s", 0);
	PRINT_ONE(priv->pshare->cur_cmd_func, "%p", 1);
#endif

	return pos;
}
#endif // CONFIG_USB_HCI

#ifdef CONFIG_SDIO_HCI
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_que_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_que_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
#ifdef MBSSID
	int i;
	struct rtl8192cd_priv *priv_vap;
#endif
#ifdef __ECOS
	Rltk819x_t *info = dev->info;
#endif

	PRINT_ONE("    free_xmit_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->free_xmit_queue.qlen, "%3d", 0);
	PRINT_ONE(NR_XMITFRAME, "/%d", 0);
#ifdef CONFIG_SDIO_TX_FILTER_BY_PRI
	PRINT_ONE(priv->pshare->nr_out_of_xmitframe, " (fail:%d)", 0);
	PRINT_ONE(priv->pshare->nr_tx_filter_vo, "(%d", 0);
	PRINT_ONE(priv->pshare->nr_tx_filter_vi, "/%d", 0);
	PRINT_ONE(priv->pshare->nr_tx_filter_be, "/%d", 0);
	PRINT_ONE(priv->pshare->nr_tx_filter_bk, "/%d)", 1);
#else
	PRINT_ONE(priv->pshare->nr_out_of_xmitframe, " (fail:%d)", 1);
#endif
	PRINT_ONE("    free_xmitbuf_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->free_xmitbuf_queue.qlen, "%2d", 0);
#ifdef __ECOS
	PRINT_ONE(NR_XMITBUFF, "/%d", 0);
	PRINT_ONE(priv->pshare->nr_out_of_xmitbuf, " (fail:%d)", 1);
#else
	PRINT_ONE(NR_XMITBUFF, "/%d", 1);
#endif
	PRINT_ONE("    free_xmit_extbuf_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->free_xmit_extbuf_queue.qlen, "%2d", 0);
#ifdef __ECOS
	PRINT_ONE(NR_XMIT_EXTBUFF, "/%d", 0);
	PRINT_ONE(priv->pshare->nr_out_of_xmit_extbuf, " (fail:%d)", 1);
#else
	PRINT_ONE(NR_XMIT_EXTBUFF, "/%d", 1);
#endif
	PRINT_ONE("    free_urg_xmitbuf_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->free_urg_xmitbuf_queue.qlen, "%2d", 0);
	PRINT_ONE(NR_URG_XMITBUFF, "/%d", 1);
	PRINT_ONE("    use_hw_queue_bitmap: ", "%s", 0);
	PRINT_ONE(priv->pshare->use_hw_queue_bitmap, "%08lX", 1);
	PRINT_ONE("    stop_netif_tx_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->stop_netif_tx_queue, "%d", 0);
	PRINT_ONE(priv->pshare->nr_stop_netif_tx_queue, " (%d)", 1);
	PRINT_ONE("    tx_mc_queue:", "%s", 1);
	PRINT_ONE("      q_num: ", "%s", 0);
	PRINT_ONE(priv->tx_mc_queue.q_num, "%d", 0);
	PRINT_ONE(" tx_pending: ", "%s", 0);
	PRINT_ONE(!rtw_is_list_empty(&priv->tx_mc_queue.tx_pending), "%d", 0);
	PRINT_ONE("  qlen: ", "%s", 0);
	PRINT_ONE(tx_servq_len(&priv->tx_mc_queue), "%d", 1);
#ifdef MBSSID
	if (priv->pmib->miscEntry.vap_enable) {
		for (i = 0; i < RTL8192CD_NUM_VWLAN; i++) {
			priv_vap = priv->pvap_priv[i];
			if (IS_DRV_OPEN(priv_vap)) {
				PRINT_ONE(priv_vap->vap_id, " [va%d]q_num: ", 0);
				PRINT_ONE(priv_vap->tx_mc_queue.q_num, "%d", 0);
				PRINT_ONE(" tx_pending: ", "%s", 0);
				PRINT_ONE(!rtw_is_list_empty(&priv_vap->tx_mc_queue.tx_pending), "%d", 0);
				PRINT_ONE("  qlen: ", "%s", 0);
				PRINT_ONE(tx_servq_len(&priv_vap->tx_mc_queue), "%d", 1);
			}
		}
	}
#endif // MBSSID
	PRINT_ONE("    tx_mgnt_queue:", "%s", 1);
	PRINT_ONE("      q_num: ", "%s", 0);
	PRINT_ONE(priv->tx_mgnt_queue.q_num, "%d", 0);
	PRINT_ONE(" tx_pending: ", "%s", 0);
	PRINT_ONE(!rtw_is_list_empty(&priv->tx_mgnt_queue.tx_pending), "%d", 0);
	PRINT_ONE("  qlen: ", "%s", 0);
	PRINT_ONE(tx_servq_len(&priv->tx_mgnt_queue), "%d", 1);
#ifdef UNIVERSAL_REPEATER
	if (IS_DRV_OPEN(GET_VXD_PRIV(priv))) {
		struct rtl8192cd_priv *priv_vxd = GET_VXD_PRIV(priv);
		PRINT_ONE(priv_vxd->tx_mgnt_queue.q_num, " [vxd]q_num: %d", 0);
		PRINT_ONE(" tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&priv_vxd->tx_mgnt_queue.tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(tx_servq_len(&priv_vxd->tx_mgnt_queue), "%d", 1);
	}
#endif // UNIVERSAL_REPEATER
#ifdef MBSSID
	if (priv->pmib->miscEntry.vap_enable) {
		for (i = 0; i < RTL8192CD_NUM_VWLAN; i++) {
			priv_vap = priv->pvap_priv[i];
			if (IS_DRV_OPEN(priv_vap)) {
				PRINT_ONE(priv_vap->vap_id, " [va%d]q_num: ", 0);
				PRINT_ONE(priv_vap->tx_mgnt_queue.q_num, "%d", 0);
				PRINT_ONE(" tx_pending: ", "%s", 0);
				PRINT_ONE(!rtw_is_list_empty(&priv_vap->tx_mgnt_queue.tx_pending), "%d", 0);
				PRINT_ONE("  qlen: ", "%s", 0);
				PRINT_ONE(tx_servq_len(&priv_vap->tx_mgnt_queue), "%d", 1);
			}
		}
	}
#endif // MBSSID
	PRINT_ONE("    pspoll_sta_queue:", "%s", 1);
	PRINT_ONE("      q_num: ", "%s", 0);
	PRINT_ONE(priv->pshare->pspoll_sta_queue.q_num, "%d", 0);
	PRINT_ONE(" tx_pending: ", "%s", 0);
	PRINT_ONE(!rtw_is_list_empty(&priv->pshare->pspoll_sta_queue.tx_pending), "%d", 0);
	PRINT_ONE("  qlen: ", "%s", 0);
	PRINT_ONE(tx_servq_len(&priv->pshare->pspoll_sta_queue), "%d", 1);
	PRINT_ONE("    tx_urgent_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_urgent_queue.qlen, "%d", 1);
	PRINT_ONE("    tx_pending_sta_queue:", "%s", 1);
	PRINT_ONE("      MGQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[MGNT_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" BKQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[BK_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" BEQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[BE_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" VIQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[VI_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" VOQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[VO_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" HIQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_pending_sta_queue[HIGH_QUEUE].qlen, "%-4d", 1);
	PRINT_ONE("    pending_xmitbuf_queue: ", "%s", 0);
	PRINT_ONE(priv->pshare->pending_xmitbuf_queue.qlen, "%d", 1);
	PRINT_ONE("      MGQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_xmitbuf_waiting_queue[MGNT_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" BKQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_xmitbuf_waiting_queue[BK_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" BEQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_xmitbuf_waiting_queue[BE_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" VIQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_xmitbuf_waiting_queue[VI_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" VOQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_xmitbuf_waiting_queue[VO_QUEUE].qlen, "%-4d", 0);
	PRINT_ONE(" HIQ: ", "%s", 0);
	PRINT_ONE(priv->pshare->tx_xmitbuf_waiting_queue[HIGH_QUEUE].qlen, "%-4d", 1);
#ifdef CONFIG_SDIO_TX_INTERRUPT
	{
#if IS_EXIST_RTL8822BS || IS_EXIST_RTL8821CS
		u16 *free_tx_page = GET_HAL_INTF_DATA(priv)->SdioTxFIFOFreePage;
#else
		u8 *free_tx_page = GET_HAL_INTF_DATA(priv)->SdioTxFIFOFreePage;
#endif
		PRINT_ONE("    TxFIFOFreePage:", "%s", 1);
		PRINT_ONE("      HIQ: ", "%s", 0);
		PRINT_ONE(free_tx_page[HI_QUEUE_IDX], "%-4d", 0);
		PRINT_ONE(" MIQ: ", "%s", 0);
		PRINT_ONE(free_tx_page[MID_QUEUE_IDX], "%-4d", 0);
		PRINT_ONE(" LOQ: ", "%s", 0);
		PRINT_ONE(free_tx_page[LOW_QUEUE_IDX], "%-4d", 0);
#if defined(CONFIG_WLAN_HAL_8192EE) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8821CE)
		if ((GET_CHIP_VER(priv) == VERSION_8192E) || (GET_CHIP_VER(priv) == VERSION_8822B) || (GET_CHIP_VER(priv) == VERSION_8821C)) {
			PRINT_ONE(" EXQ: ", "%s", 0);
			PRINT_ONE(free_tx_page[EXTRA_QUEUE_IDX], "%-4d", 0);
		}
#endif
		PRINT_ONE(" PUBQ: ", "%s", 0);
		PRINT_ONE(free_tx_page[PUBLIC_QUEUE_IDX], "%-4d", 1);
	}
#endif // CONFIG_SDIO_TX_INTERRUPT

#if defined(CONFIG_WLAN_HAL_8192EE) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8821CE)
	if ((GET_CHIP_VER(priv) == VERSION_8192E) || (GET_CHIP_VER(priv) == VERSION_8822B) || (GET_CHIP_VER(priv) == VERSION_8821C))
	{
		u16 oqtFreeSpace;
		u8 *pFreeSpace;
		
		oqtFreeSpace = SdioLocalCmd52Read2Byte(priv, SDIO_REG_OQT_FREE_SPACE);
#ifdef _BIG_ENDIAN_
		oqtFreeSpace = cpu_to_le16(oqtFreeSpace);
#endif
		pFreeSpace = (u8*) &oqtFreeSpace;
		
		PRINT_ONE("    OQT free space: [AC] ", "%s", 0);
		PRINT_ONE(GET_HAL_INTF_DATA(priv)->SdioTxOQTFreeSpace[TXOQT_TYPE_AC], "%2d", 0);
		PRINT_ONE(pFreeSpace[TXOQT_TYPE_AC], "/%2d", 0);
		PRINT_ONE(" [NOAC] ", "%s", 0);
		PRINT_ONE(GET_HAL_INTF_DATA(priv)->SdioTxOQTFreeSpace[TXOQT_TYPE_NOAC], "%2d", 0);
		PRINT_ONE(pFreeSpace[TXOQT_TYPE_NOAC], "/%2d", 0);
		PRINT_ONE(priv->pshare->nr_out_of_txoqt_space, " (wait:%d)", 1);
	}
#else
	PRINT_ONE("    OQT free space: ", "%s", 0);
	PRINT_ONE(GET_HAL_INTF_DATA(priv)->SdioTxOQTFreeSpace, "%2d", 0);
	PRINT_ONE(SdioLocalCmd52Read1Byte(priv, SDIO_REG_OQT_FREE_SPACE), "/%d", 0);
	PRINT_ONE(priv->pshare->nr_out_of_txoqt_space, " (wait:%d)", 1);
#endif

	PRINT_ONE("    free_recv_queue: ", "%s", 0);
	PRINT_ONE(priv->recvpriv.free_recv_queue.qlen, "%d", 0);
	PRINT_ONE(NR_RECVFRAME, "/%d", 0);
	PRINT_ONE(priv->recvpriv.nr_out_of_recvframe, " (fail:%d)", 1);
	PRINT_ONE("    free_recv_buf_queue: ", "%s", 0);
	PRINT_ONE(priv->recvpriv.free_recv_buf_queue.qlen, "%d", 0);
	PRINT_ONE(NR_RECVBUFF, "/%d", 0);
	PRINT_ONE(priv->recvpriv.nr_out_of_recvbuf, " (fail:%d", 0);
	PRINT_ONE(priv->recvpriv.nr_out_of_recvbuf_mem, "/%d)", 1);

	PRINT_ONE("    recv_buf_pending_queue: ", "%s", 0);
	PRINT_ONE(priv->recvpriv.recv_buf_pending_queue.qlen, "%d", 1);
	
	PRINT_ONE("    wake_event: ", "%s", 0);
	PRINT_ONE(priv->pshare->xmit_wake | priv->pshare->cmd_wake, "%08lX", 1);
	PRINT_ONE("    cmd_queue: ", "%s", 1);
	PRINT_ONE("      qlen: ", "%s", 0);
	PRINT_ONE(priv->pshare->cmd_queue.qlen, "%-4d", 0);
	PRINT_ONE(" miss: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_cmd_miss, "%-4u", 0);
	PRINT_ONE(" done: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_cmd, "%u", 1);
	
	PRINT_ONE("    rx_mgt_queue: ", "%s", 1);
	PRINT_ONE("      qlen: ", "%s", 0);
	PRINT_ONE(priv->pshare->rx_mgt_queue.qlen, "%-4d", 0);
	PRINT_ONE(" miss: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_rx_mgt_cmd_miss, "%-4u", 0);
	PRINT_ONE(" done: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_rx_mgt_cmd, "%u", 1);
	
	PRINT_ONE("    timer_evt_queue: ", "%s", 1);
	PRINT_ONE("      qlen: ", "%s", 0);
	PRINT_ONE(priv->pshare->timer_evt_queue.qlen, "%-4d", 0);
	PRINT_ONE(" miss: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_timer_evt_miss, "%-4u", 0);
	PRINT_ONE(" done: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_timer_evt, "%u", 1);
#ifdef __ECOS
	PRINT_ONE("    iot_mode_enable:", "%s", 0);
	PRINT_ONE(priv->pshare->iot_mode_enable, "%d", 1);
	PRINT_ONE("    iot_mode_VO_exist:", "%s", 0);
	PRINT_ONE(priv->pshare->iot_mode_VO_exist, "%d", 1);
	PRINT_ONE("    iot_mode_VI_exist:", "%s", 0);
	PRINT_ONE(priv->pshare->iot_mode_VI_exist, "%d", 1);
#ifdef WMM_VIBE_PRI
	PRINT_ONE("    iot_mode_BE_exist:", "%s", 0);
	PRINT_ONE(priv->pshare->iot_mode_BE_exist, "%d", 1);
#endif
#ifdef WMM_BEBK_PRI
	PRINT_ONE("    iot_mode_BK_exist:", "%s", 0);
	PRINT_ONE(priv->pshare->iot_mode_BK_exist, "%d", 1);
#endif
#ifdef TX_PKT_FREE_QUEUE
	PRINT_SINGL_ARG("    skb_txdone_queue:  ", info->skb_txdone_queue.qlen, "%d");
	PRINT_SINGL_ARG("    txdone_queue:  ", info->txdone_queue.qlen, "%d");
	dump_free_txdone_queue();
#endif
#endif

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_sdio_dbginfo(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_sdio_dbginfo(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("    pending_cmd: ", "%s", 0);
	PRINT_ONE(priv->pshare->pending_cmd[0], "%08lX", 1);
	PRINT_ONE("    sdio interrupt: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_interrupt, "%d", 1);
	PRINT_ONE("    xmit_thread_run: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_xmit_thread_run, "%d", 1);
	PRINT_ONE("    SdioTxIntStatus: ", "%s", 0);
	PRINT_ONE(GET_HAL_INTF_DATA(priv)->SdioTxIntStatus, "%lu", 1);
	PRINT_ONE("    need_sched_xmit: ", "%s", 0);
	PRINT_ONE(priv->pshare->need_sched_xmit, "%08lX", 1);
	PRINT_ONE("    low_traffic_xmit/_thd: ", "%s", 0);
	PRINT_ONE(priv->pshare->low_traffic_xmit, "%08lX", 0);
	PRINT_ONE(priv->pshare->rf_ft_var.low_traffic_xmit_thd, " /%d", 1);
	PRINT_ONE("    xmitbuf_agg_num: ", "%s", 0);
	PRINT_ONE(priv->pshare->xmitbuf_agg_num, "%d", 1);
	PRINT_ONE("    xmitbuf_handled_in_thread/in_irq: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_xmitbuf_handled_in_thread, "%5d", 0);
	PRINT_ONE(priv->pshare->nr_xmitbuf_handled_in_irq, "/%5d", 1);
#ifdef SDIO_STATISTICS
	{
		int i;
		PRINT_ONE("    write port count/time", "%s", 1);
		for (i = 0; i < MAX_XMITBUF_PKT; ++i) {
			if (priv->pshare->writeport_avg_count[i]) {
				PRINT_ONE(i+1, "     [%2d]", 0);
#ifdef SDIO_STATISTICS_TIME
				PRINT_ONE(priv->pshare->writeport_avg_count[i], "%4d", 0);
				PRINT_ONE(priv->pshare->writeport_avg_time[i], "/%4lluus", 1);
#else
				PRINT_ONE(priv->pshare->writeport_avg_count[i], "%4d", 1);
#endif
			}
		}
	}
#endif // SDIO_STATISTICS

	PRINT_ONE("    recvbuf_handled_in_irq: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_recvbuf_handled_in_irq, "%d", 1);
	PRINT_ONE("    recvbuf_handled_in_tasklet: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_recvbuf_handled_in_tasklet, "%d", 1);
	PRINT_ONE("    recvframe_in_recvbuf: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_recvframe_in_recvbuf, "%d", 1);
#ifdef CONFIG_1RCCA_RF_POWER_SAVING
	PRINT_ONE("    1rcca_ps_active: ", "%s", 0);
	PRINT_ONE(priv->pshare->rf_ft_var.one_path_cca_ps_active, "%d", 1);
#endif
#ifdef SDIO_AP_OFFLOAD
	PRINT_ONE("    pwr_state/offload_ctrl: ", "%s", 0);
	PRINT_ONE(priv->pshare->pwr_state, "%d", 0);
	PRINT_ONE(priv->pshare->offload_function_ctrl, "/%d", 1);
	PRINT_ONE("    offload_prohibited: ", "%s", 0);
	PRINT_ONE(priv->pshare->offload_prohibited, "%lX", 1);
	PRINT_ONE("    update_bcn_fail: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_update_bcn_fail, "%d", 1);
#ifdef CONFIG_POWER_SAVE
	PRINT_ONE("    ps_level: ", "%s", 0);
	PRINT_ONE(priv->pmib->dot11OperationEntry.ps_level, "%d", 0);
	PRINT_ONE(priv->pshare->ap_ps_handle.en_sapps, "  (sapps:%d", 0);
	PRINT_ONE(priv->pshare->ap_ps_handle.en_32k, " 32k:%d)", 1);
	PRINT_ONE("    leave_32k_fail: ", "%s", 0);
	PRINT_ONE(priv->pshare->nr_leave_32k_fail, " (fail:%d)", 1);
	PRINT_SINGL_ARG("    ps_timeout: ", priv->pmib->dot11OperationEntry.ps_timeout, "%d");
	PRINT_SINGL_ARG("    sleep_time: ", priv->pshare->ap_ps_handle.sleep_time, "%d");
#endif
#endif
#ifdef CMD_THREAD_FUNC_DEBUG
	PRINT_ONE("    cur_cmd_func: ", "%s", 0);
	PRINT_ONE(priv->pshare->cur_cmd_func, "%p", 1);
#endif

	return pos;
}
#endif // CONFIG_SDIO_HCI

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
#ifdef CONFIG_RTL_PROC_NEW
static int dump_one_sta_queinfo(struct seq_file *s, int num, struct stat_info *pstat)
#else
static int dump_one_sta_queinfo(int num, struct stat_info *pstat, char *buf, char **start,
			off_t offset, int length, int *eof, void *data)
#endif
{
	int pos = 0;

	PRINT_ONE(num,  " %d: stat_queinfo ", 0);
	PRINT_ONE(pstat->cmn_info.aid, "[aid:%d]", 1);
	PRINT_ONE("    pspoll_list: ", "%s", 0);
	PRINT_ONE(!rtw_is_list_empty(&pstat->pspoll_list), "%d", 1);
	PRINT_ONE("    tx_queue:", "%s", 1);
	if (tx_servq_len(&pstat->tx_queue[MGNT_QUEUE])) {
		PRINT_ONE("      [MGQ] tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&pstat->tx_queue[MGNT_QUEUE].tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(tx_servq_len(&pstat->tx_queue[MGNT_QUEUE]), "%d", 1);
	}
	if (tx_servq_len(&pstat->tx_queue[BK_QUEUE])) {
		PRINT_ONE("      [BKQ] tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&pstat->tx_queue[BK_QUEUE].tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(tx_servq_len(&pstat->tx_queue[BK_QUEUE]), "%d", 1);
	}
	if (tx_servq_len(&pstat->tx_queue[BE_QUEUE])) {
		PRINT_ONE("      [BEQ] tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&pstat->tx_queue[BE_QUEUE].tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(tx_servq_len(&pstat->tx_queue[BE_QUEUE]), "%d", 1);
	}
	if (tx_servq_len(&pstat->tx_queue[VI_QUEUE])) {
		PRINT_ONE("      [VIQ] tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&pstat->tx_queue[VI_QUEUE].tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(tx_servq_len(&pstat->tx_queue[VI_QUEUE]), "%d", 1);
	}
	if (tx_servq_len(&pstat->tx_queue[VO_QUEUE])) {
		PRINT_ONE("      [VOQ] tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&pstat->tx_queue[VO_QUEUE].tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(tx_servq_len(&pstat->tx_queue[VO_QUEUE]), "%d", 1);
	}
#ifdef CONFIG_TCP_ACK_TXAGG
	PRINT_ONE("    tcpack_queue:", "%s", 1);
	if (pstat->tcpack_queue[BK_QUEUE].xframe_queue.qlen) {
		PRINT_ONE("      [BKQ] tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&pstat->tcpack_queue[BK_QUEUE].tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(pstat->tcpack_queue[BK_QUEUE].xframe_queue.qlen, "%d", 1);
	}
	if (pstat->tcpack_queue[BE_QUEUE].xframe_queue.qlen) {
		PRINT_ONE("      [BEQ] tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&pstat->tcpack_queue[BE_QUEUE].tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(pstat->tcpack_queue[BE_QUEUE].xframe_queue.qlen, "%d", 1);
	}
	if (pstat->tcpack_queue[VI_QUEUE].xframe_queue.qlen) {
		PRINT_ONE("      [VIQ] tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&pstat->tcpack_queue[VI_QUEUE].tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(pstat->tcpack_queue[VI_QUEUE].xframe_queue.qlen, "%d", 1);
	}
	if (pstat->tcpack_queue[VO_QUEUE].xframe_queue.qlen) {
		PRINT_ONE("      [VOQ] tx_pending: ", "%s", 0);
		PRINT_ONE(!rtw_is_list_empty(&pstat->tcpack_queue[VO_QUEUE].tx_pending), "%d", 0);
		PRINT_ONE("  qlen: ", "%s", 0);
		PRINT_ONE(pstat->tcpack_queue[VO_QUEUE].xframe_queue.qlen, "%d", 1);
	}
#endif // CONFIG_TCP_ACK_TXAGG

	PRINT_ONE("", "%s", 1);

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_sta_queinfo(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_sta_queinfo(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int len = 0;
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
	off_t begin = 0;
	off_t pos = 0;
	int size;
#endif
	int num=1;
	struct list_head *phead, *plist;
	struct stat_info *pstat;
	unsigned long flags=0;
	
	SAVE_INT_AND_CLI(flags);
	SMP_LOCK_ASOC_LIST(flags);

#ifdef __ECOS
	ecos_pr_fun("-- STA que info table --\n");
#elif defined(CONFIG_RTL_PROC_NEW)
	seq_printf(s, "-- STA que info table --\n");
#else
	size = snprintf(buf, length, "-- STA que info table --\n");
#endif
	CHECK_LEN;
	
	if (!(priv->drv_state & DRV_STATE_OPEN))
		goto _ret;

	phead = &priv->asoc_list;
	plist = phead->next;
	
	while (plist != phead) {
		pstat = list_entry(plist, struct stat_info, asoc_list);
		plist = plist->next;
		
#ifdef CONFIG_RTL_PROC_NEW
		dump_one_sta_queinfo(s, num++, pstat);
#else
		size = dump_one_sta_queinfo(num++, pstat, buf+len, start, offset, length,
					eof, data);
#endif
		CHECK_LEN;
	}

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
	*eof = 1;
#endif

_ret:
	SMP_UNLOCK_ASOC_LIST(flags);
	RESTORE_INT(flags);
	
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
	*start = buf + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);	/* Start slop */
	if (len > length)
		len = length;	/* Ending slop */
#endif
	return len;
}
#endif // CONFIG_USB_HCI || CONFIG_SDIO_HCI

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_buf_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_buf_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  buf info...", "%s", 1);
	PRINT_ONE("    hdr poll:", "%s", 1);
	PRINT_ONE("      head: ", "%s", 0);
	PRINT_ONE((unsigned long)&priv->pshare->wlan_hdrlist, "0x%.8lx", 0);
	PRINT_ONE("    count: ", "%s", 0);
	PRINT_ONE(priv->pshare->pwlan_hdr_poll->count, "%d", 1);

	PRINT_ONE("    hdrllc poll:", "%s", 1);
	PRINT_ONE("      head: ", "%s", 0);
	PRINT_ONE((unsigned long)&priv->pshare->wlanllc_hdrlist, "0x%.8lx", 0);
	PRINT_ONE("    count: ", "%s", 0);
	PRINT_ONE(priv->pshare->pwlanllc_hdr_poll->count, "%d", 1);

	PRINT_ONE("    mgmtbuf poll:", "%s", 1);
	PRINT_ONE("      head: ", "%s", 0);
	PRINT_ONE((unsigned long)&priv->pshare->wlanbuf_list, "0x%.8lx", 0);
	PRINT_ONE("    count: ", "%s", 0);
	PRINT_ONE(priv->pshare->pwlanbuf_poll->count, "%d", 1);

	PRINT_ONE("    icv poll:", "%s", 1);
	PRINT_ONE("      head: ", "%s", 0);
	PRINT_ONE((unsigned long)&priv->pshare->wlanicv_list, "0x%.8lx", 0);
	PRINT_ONE("    count: ", "%s", 0);
	PRINT_ONE(priv->pshare->pwlanicv_poll->count, "%d", 1);

	PRINT_ONE("    mic poll:", "%s", 1);
	PRINT_ONE("      head: ", "%s", 0);
	PRINT_ONE((unsigned long)&priv->pshare->wlanmic_list, "0x%.8lx", 0);
	PRINT_ONE("    count: ", "%s", 0);
	PRINT_ONE(priv->pshare->pwlanmic_poll->count, "%d", 1);

	return pos;
}


#ifdef ENABLE_RTL_SKB_STATS
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_skb_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_skb_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  skb counter...", "%s", 1);
	PRINT_SINGL_ARG("    skb_tx_cnt: ", rtl_atomic_read(&priv->rtl_tx_skb_cnt) , "%d");
	PRINT_SINGL_ARG("    skb_rx_cnt: ", rtl_atomic_read(&priv->rtl_rx_skb_cnt) , "%d");

	return pos;
}
#endif

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_11n(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_11n(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  dot11nConfigEntry...", "%s", 1);

	PRINT_SINGL_ARG("    supportedmcs: ", get_supported_mcs(priv), "%08x");
	PRINT_SINGL_ARG("    basicmcs: ", priv->pmib->dot11nConfigEntry.dot11nBasicMCS, "%08x");
	PRINT_SINGL_ARG("    use40M: ", priv->pmib->dot11nConfigEntry.dot11nUse40M, "%d");
	if (priv->pshare->is_40m_bw == CHANNEL_WIDTH_160) {
		PRINT_ONE("    currBW: 160M", "%s", 1);
	}
	else if (priv->pshare->is_40m_bw == CHANNEL_WIDTH_80_80) {
		PRINT_ONE("    currBW: 80+80M", "%s", 1);
	}
	else {
	PRINT_SINGL_ARG("    currBW: ", (!priv->pshare->is_40m_bw) ? 20 :(priv->pshare->is_40m_bw*40), "%dM");
	}
	if (priv->pshare->CurrentChannelBW == CHANNEL_WIDTH_160) {
		PRINT_ONE("    currBW(op): 160M", "%s", 1);
	}
	else if (priv->pshare->CurrentChannelBW == CHANNEL_WIDTH_80_80) {
		PRINT_ONE("    currBW(op): 80+80M", "%s", 1);
	}
	else {
	PRINT_SINGL_ARG("    currBW(op): ", (!priv->pshare->CurrentChannelBW) ? 20 :(priv->pshare->CurrentChannelBW*40), "%dM");
	}

	PRINT_ONE("    2ndchoffset: ", "%s", 0);
	switch (priv->pshare->offset_2nd_chan) {
	case HT_2NDCH_OFFSET_BELOW:
		PRINT_ONE("below", "%s", 1);
		break;
	case HT_2NDCH_OFFSET_ABOVE:
		PRINT_ONE("above", "%s", 1);
		break;
	default:
		PRINT_ONE("dontcare", "%s", 1);
		break;
	}

	PRINT_SINGL_ARG("    shortGI20M: ", priv->pmib->dot11nConfigEntry.dot11nShortGIfor20M, "%d");
	PRINT_SINGL_ARG("    shortGI40M: ", priv->pmib->dot11nConfigEntry.dot11nShortGIfor40M, "%d");
	PRINT_SINGL_ARG("    shortGI80M: ", priv->pmib->dot11nConfigEntry.dot11nShortGIfor80M, "%d");
	PRINT_SINGL_ARG("    shortGI160M: ", priv->pmib->dot11nConfigEntry.dot11nShortGIfor160M, "%d");
	PRINT_SINGL_ARG("    vht_protocol: ", priv->pmib->dot11nConfigEntry.dot11nVHTProtocol, "%d");
	PRINT_SINGL_ARG("    comply160M: ", priv->pmib->dot11nConfigEntry.dot11nComply160M, "%d");
	PRINT_SINGL_ARG("    stbc: ", priv->pmib->dot11nConfigEntry.dot11nSTBC, "%d");
	PRINT_SINGL_ARG("    ldpc: ", priv->pmib->dot11nConfigEntry.dot11nLDPC, "%d");
	PRINT_SINGL_ARG("    ampdu: ", priv->pmib->dot11nConfigEntry.dot11nAMPDU, "%d");
	PRINT_SINGL_ARG("    amsdu: ", priv->pmib->dot11nConfigEntry.dot11nAMSDU, "%d");
#ifdef SUPPORT_TX_AMSDU_SMALL_PKTS_ONLY
	PRINT_SINGL_ARG("    amsdusmallpkts: ", priv->pmib->dot11nConfigEntry.dot11nAMSDUSmallPkts, "%d");
	PRINT_SINGL_ARG("    amsdusmallpktlen: ", priv->pmib->dot11nConfigEntry.dot11nAMSDUSmallPktLen, "%d");
	PRINT_SINGL_ARG("    amsdudropdupack: ", priv->pmib->dot11nConfigEntry.dot11nAMSDUTCPAckDropDup, "%d");
#endif
	PRINT_SINGL_ARG("    ampduSndSz: ", priv->pmib->dot11nConfigEntry.dot11nAMPDUSendSz, "%d");
	PRINT_SINGL_ARG("    amsduMax: ", priv->pmib->dot11nConfigEntry.dot11nAMSDURecvMax, "%d");
	PRINT_SINGL_ARG("    amsduTimeout: ", priv->pmib->dot11nConfigEntry.dot11nAMSDUSendTimeout, "%d");
	PRINT_SINGL_ARG("    amsduNum: ", priv->pmib->dot11nConfigEntry.dot11nAMSDUSendNum, "%d");
	PRINT_SINGL_ARG("    curAmsduNum: ", priv->pmib->dot11nConfigEntry.dot11nCurAMSDUSendNum, "%d");
	PRINT_SINGL_ARG("    mustAmsdu: ", priv->pshare->rf_ft_var.mustAmsdu, "%d");
	PRINT_SINGL_ARG("    lgyEncRstrct: ", priv->pmib->dot11nConfigEntry.dot11nLgyEncRstrct, "%d");
#ifdef WIFI_11N_2040_COEXIST
	PRINT_SINGL_ARG("    coexist: ", priv->pmib->dot11nConfigEntry.dot11nCoexist, "%d");
	if (COEXIST_ENABLE) {
#ifdef CLIENT_MODE
		if (OPMODE & WIFI_STATION_STATE) {
			PRINT_SINGL_ARG("    coexist_connection: ", priv->coexist_connection, "%d");
			PRINT_SINGL_ARG("    obss_scan:          ", priv->pmib->dot11nConfigEntry.dot11nCoexist_obss_scan, "%d");
		}

		if ((OPMODE & WIFI_AP_STATE) ||
			((OPMODE & WIFI_STATION_STATE) && priv->coexist_connection))
#endif
			PRINT_SINGL_ARG("    bg_ap_timeout: ", priv->bg_ap_timeout, "%d");
#ifdef CLIENT_MODE
		if ((OPMODE & WIFI_STATION_STATE) && priv->coexist_connection) {
			if (priv->bg_ap_timeout)
				PRINT_ARRAY_ARG("    bg_ap_timeout_ch: ", priv->bg_ap_timeout_ch, "%d ", 14);

			PRINT_SINGL_ARG("    intolerant_timeout: ", priv->intolerant_timeout, "%d");
		} else
#endif
		if (OPMODE & WIFI_AP_STATE) {
			PRINT_BITMAP_ARG("    force_20_sta", priv->force_20_sta);
			PRINT_BITMAP_ARG("    switch_20_sta", priv->switch_20_sta);
			PRINT_SINGL_ARG("    bg_ap_rssi_chk_th: ", priv->pmib->dot11nConfigEntry.dot11nBGAPRssiChkTh, "%d");
		}
	}
#endif

	PRINT_SINGL_ARG("    txnoack: ", priv->pmib->dot11nConfigEntry.dot11nTxNoAck, "%d");

	if (priv->ht_cap_len) {
		unsigned char *pbuf = (unsigned char *)&priv->ht_cap_buf;
		PRINT_ARRAY_ARG("    ht_cap: ", pbuf, "%02x", priv->ht_cap_len);
	}
	else {
		PRINT_ONE("    ht_cap: none", "%s", 1);
	}
	if (priv->ht_ie_len) {
		unsigned char *pbuf = (unsigned char *)&priv->ht_ie_buf;
		PRINT_ARRAY_ARG("    ht_ie: ", pbuf, "%02x", priv->ht_ie_len);
	}
	else {
		PRINT_ONE("    ht_ie: none", "%s", 1);
	}

	PRINT_SINGL_ARG("    legacy_obss_to: ", priv->ht_legacy_obss_to, "%d");	
	PRINT_SINGL_ARG("    nomember_legacy_sta_to: ", priv->ht_nomember_legacy_sta_to, "%d");
	PRINT_SINGL_ARG("    legacy_sta_num: ", priv->ht_legacy_sta_num, "%d");
	PRINT_SINGL_ARG("    11nProtection: ", priv->ht_protection, "%d");
//	PRINT_BITMAP_ARG("    has_2r_sta", priv->pshare->has_2r_sta);
#ifdef COCHANNEL_RTS
	PRINT_SINGL_ARG("    cochannel: ", priv->cochannel_to, "%d");
#endif
#ifdef RA_OFFSET_TRAINING
	PRINT_SINGL_ARG("    adjacent_intf: ", priv->pshare->adjacent_intf, "%d");
	PRINT_SINGL_ARG("    env_noisy: ", priv->pshare->env_noisy, "%d");
	PRINT_SINGL_ARG("    ra_training_step: ", priv->pshare->ra_training_step, "%d");
	PRINT_SINGL_ARG("    ra_offset_idx: ", priv->pshare->ra_offset_idx, "%d");
#endif
#ifdef CTC_AP_COEXIST
	PRINT_SINGL_ARG("    rtk_ap_detected_to: ", priv->pshare->rtk_ap_detected_to, "%d");	
	PRINT_SINGL_ARG("    interf_mode: ", priv->pshare->interf_mode, "%d");	
#endif

	return pos;
}


#ifdef CONFIG_RTL_92C_SUPPORT //#ifndef CONFIG_RTL_92D_SUPPORT
#ifndef __ECOS
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_phypara_file_read(struct seq_file *s, void *data)
{
	return 0;
}
#else
static int rtl8192cd_proc_phypara_file_read(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int len=0;

	if ((GET_CHIP_VER(priv) == VERSION_8192C)||(GET_CHIP_VER(priv) == VERSION_8188C)) {
		if (priv->phypara_file_end <= (priv->phypara_file_start + offset + length)) {
			*eof = 1;
			len = priv->phypara_file_end - priv->phypara_file_start - offset;
		}
		else
			len = length;

		if (len <= length)
			memcpy(buf, &priv->phypara_file_start[offset], len);
		*start = buf;
	}
	return len;
}
#endif


#ifdef CONFIG_RTL_KERNEL_MIPS16_WLAN
__NOMIPS16
#endif
static int rtl8192cd_proc_phypara_file_write(struct file *file, const char *buffer,
				unsigned long count, void *data)
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	char tmp[64];

	if (count < 2)
		return -EFAULT;

	if (count > sizeof(tmp))
		return -EINVAL;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		tmp[count-1] = '\0';


#ifdef TESTCHIP_SUPPORT
		if (!strcmp(tmp, "AGC_TAB.txt")) {
			priv->phypara_file_start = data_AGC_TAB_start;
			priv->phypara_file_end = data_AGC_TAB_end;
		}
		else if (!strcmp(tmp, "PHY_REG_1T.txt")) {
			priv->phypara_file_start = data_PHY_REG_1T_start;
			priv->phypara_file_end = data_PHY_REG_1T_end;
		}
		else if (!strcmp(tmp, "PHY_REG_2T.txt")) {
			priv->phypara_file_start = data_PHY_REG_2T_start;
			priv->phypara_file_end = data_PHY_REG_2T_end;
		}
		else if (!strcmp(tmp, "radio_a_1T.txt")) {
			priv->phypara_file_start = data_radio_a_1T_start;
			priv->phypara_file_end = data_radio_a_1T_end;
		}
		else if (!strcmp(tmp, "radio_a_2T.txt")) {
			priv->phypara_file_start = data_radio_a_2T_start;
			priv->phypara_file_end = data_radio_a_2T_end;
		}
		else if (!strcmp(tmp, "radio_b_2T.txt")) {
			priv->phypara_file_start = data_radio_b_2T_start;
			priv->phypara_file_end = data_radio_b_2T_end;
		}
		else
#endif

		if (!strcmp(tmp, "AGC_TAB_n.txt")) {
			priv->phypara_file_start = data_AGC_TAB_n_92C_start;
			priv->phypara_file_end = data_AGC_TAB_n_92C_end;
		}
		else if (!strcmp(tmp, "PHY_REG_1T_n.txt")) {
			priv->phypara_file_start = data_PHY_REG_1T_n_start;
			priv->phypara_file_end = data_PHY_REG_1T_n_end;
		}
		else if (!strcmp(tmp, "PHY_REG_2T_n.txt")) {
			priv->phypara_file_start = data_PHY_REG_2T_n_start;
			priv->phypara_file_end = data_PHY_REG_2T_n_end;
		}
		else if (!strcmp(tmp, "radio_a_2T_n.txt")) {
			priv->phypara_file_start = data_radio_a_2T_n_start;
			priv->phypara_file_end = data_radio_a_2T_n_end;
		}
		else if (!strcmp(tmp, "radio_b_2T_n.txt")) {
			priv->phypara_file_start = data_radio_b_2T_n_start;
			priv->phypara_file_end = data_radio_b_2T_n_end;
		}
		else if (!strcmp(tmp, "radio_a_1T_n.txt")) {
			priv->phypara_file_start = data_radio_a_1T_n_start;
			priv->phypara_file_end = data_radio_a_1T_n_end;
		}
		else if (!strcmp(tmp, "PHY_REG_PG.txt")) {
			priv->phypara_file_start = data_PHY_REG_PG_92C_start;
			priv->phypara_file_end = data_PHY_REG_PG_92C_end;
		}
		else if (!strcmp(tmp, "MACPHY_REG_92C.txt")) {
			priv->phypara_file_start = data_MACPHY_REG_92C_start;//data_MACPHY_REG_92C_start
			priv->phypara_file_end = data_MACPHY_REG_92C_end;
		}
		else if (!strcmp(tmp, "PHY_REG_MP_n.txt")) {
			priv->phypara_file_start = data_PHY_REG_MP_n_92C_start;
			priv->phypara_file_end = data_PHY_REG_MP_n_92C_end;
		}
#ifdef HIGH_POWER_EXT_PA
		else if (!strcmp(tmp, "AGC_TAB_n_hp.txt")) {
			priv->phypara_file_start = data_AGC_TAB_n_hp_start;
			priv->phypara_file_end = data_AGC_TAB_n_hp_end;
		}

		else if (!strcmp(tmp, "PHY_REG_2T_n_hp.txt")) {
			priv->phypara_file_start = data_PHY_REG_2T_n_hp_start;
			priv->phypara_file_end = data_PHY_REG_2T_n_hp_end;
		}
#endif
#ifdef HIGH_POWER_EXT_PA
		else if (!strcmp(tmp, "radio_a_2T_n_lna.txt")) {
			priv->phypara_file_start = data_radio_a_2T_n_lna_start;
			priv->phypara_file_end = data_radio_a_2T_n_lna_end;
		}
		else if (!strcmp(tmp, "radio_b_2T_n_lna.txt")) {
			priv->phypara_file_start = data_radio_b_2T_n_lna_start;
			priv->phypara_file_end = data_radio_b_2T_n_lna_end;
		}
#endif
#ifdef HIGH_POWER_EXT_PA
		else if (!strcmp(tmp, "radio_a_2T_n_hp.txt")) {
			priv->phypara_file_start = data_radio_a_2T_n_hp_start;
			priv->phypara_file_end = data_radio_a_2T_n_hp_end;
		}
		else if (!strcmp(tmp, "radio_b_2T_n_hp.txt")) {
			priv->phypara_file_start = data_radio_b_2T_n_hp_start;
			priv->phypara_file_end = data_radio_b_2T_n_hp_end;
		}
		else if (!strcmp(tmp, "PHY_REG_PG_hp.txt")) {
			priv->phypara_file_start = data_PHY_REG_PG_hp_start;
			priv->phypara_file_end = data_PHY_REG_PG_hp_end;
		}
#endif
		else {
			panic_printk("No file of \"%s\"\n", tmp);
			panic_printk("PHY parameter file name list:\n"
#ifdef TESTCHIP_SUPPORT
				"\tAGC_TAB.txt\n"
				"\tPHY_REG_1T.txt\n"
				"\tPHY_REG_2T.txt\n"
				"\tradio_a_1T.txt\n"
				"\tradio_a_2T.txt\n"
				"\tradio_b_2T.txt\n"
#endif
				"\tAGC_TAB_n_92C.txt\n"
				"\tPHY_REG_1T_n.txt\n"
				"\tPHY_REG_2T_n.txt\n"
				"\tradio_a_2T_n.txt\n"
				"\tradio_b_2T_n.txt\n"
				"\tradio_a_1T_n.txt\n"
				"\tPHY_REG_PG.txt\n"
				"\tMACPHY_REG_92C.txt\n"
				"\tPHY_REG_MP_n.txt\n"
				"\tAGC_TAB_n_hp.txt\n"
				"\tPHY_REG_2T_n_hp.txt\n"
				"\tradio_a_2T_n_lna.txt\n"
				"\tradio_b_2T_n_lna.txt\n"
#ifdef HIGH_POWER_EXT_PA
				"\tradio_a_2T_n_hp.txt\n"
				"\tradio_b_2T_n_hp.txt\n"
				"\tPHY_REG_PG_hp.txt\n"
#endif
				);
			return count;
		}
	}

	panic_printk("Ready to dump \"%s\"\n", tmp);
	return count;
}
#endif
#endif


#ifdef ERR_ACCESS_CNTR
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_err_access(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_err_access(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	int i;
	unsigned char tmpbuf[32];

	for (i=0; i<MAX_ERR_ACCESS_CNTR; i++) {
		if (priv->err_ac_list[i].used) {
			sprintf(tmpbuf, "%02x:%02x:%02x:%02x:%02x:%02x %u",
				priv->err_ac_list[i].mac[0], priv->err_ac_list[i].mac[1], priv->err_ac_list[i].mac[2],
				priv->err_ac_list[i].mac[3], priv->err_ac_list[i].mac[4], priv->err_ac_list[i].mac[5],
				priv->err_ac_list[i].num);
			PRINT_ONE(tmpbuf, "%s", 1);
		}
	}
	memset(priv->err_ac_list, 0, sizeof(struct err_access_list) * MAX_ERR_ACCESS_CNTR);

	return pos;
}
#endif


#ifdef AUTO_TEST_SUPPORT
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_SSR_read(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_SSR_read(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int len = 0;

#ifndef CONFIG_RTL_PROC_NEW    
    off_t begin = 0;
#endif
    off_t pos = 0, base;
    int size;

    int i1;
    unsigned char tmp[6];
    char tmpbuf[64];
    int bw;

    //wait SiteSurvey completed
    if(priv->ss_req_ongoing)	{
        PRINT_ONE("waitting", "%s", 1);
        len = pos;
    }else{
        PRINT_ONE(" SiteSurvey result : ", "%s", 1);
        PRINT_ONE("    ====================", "%s", 1);
        if(priv->site_survey->count_backup==0){
            PRINT_ONE("none", "%s", 1);
            len = pos;
        }else{
            len = pos;

            for(i1=0; i1<priv->site_survey->count_backup ;i1++){
                base = pos;
                memcpy(tmp,priv->site_survey->bss_backup[i1].bssid,MACADDRLEN);
                /*
                            panic_printk("Mac=%02X%02X%02X:%02X%02X%02X ;Channel=%02d ;SSID:%s  \n",
                            tmp[0],tmp[1],tmp[2],tmp[3],tmp[4],tmp[5]
                            ,priv->site_survey->bss_backup[i1].channel
                            ,priv->site_survey->bss_backup[i1].ssid
                            );
                            */
                PRINT_ARRAY_ARG("    HwAddr: ",	tmp, "%02x", MACADDRLEN);
                PRINT_SINGL_ARG("    Channel: ",	priv->site_survey->bss_backup[i1].channel, "%d");
                PRINT_SINGL_ARG("    SSID: ", priv->site_survey->bss_backup[i1].ssid, "%s");
                PRINT_SINGL_ARG("    Type: ", ((priv->site_survey->bss_backup[i1].bsstype == 16) ? "AP" : "Ad-Hoc"), "%s");
                //PRINT_SINGL_ARG("    Type: ", priv->site_survey->bss_backup[i1].bsstype, "%d");
                 PRINT_SINGL_ARG("    RSSI: ",	priv->site_survey->bss_backup[i1].rssi, "%d");
				if (!(priv->site_survey->bss_backup[i1].capability & BIT4)) {
					PRINT_SINGL_ARG("    Encryption: ", "NONE", "%s");
				} else if (priv->site_survey->bss_backup[i1].t_stamp[0] == 0) {
					PRINT_SINGL_ARG("    Encryption: ", "WEP", "%s");
#if defined(CONFIG_RTL_WAPI_SUPPORT)
				} else if (priv->site_survey->bss_backup[i1].t_stamp[0] == SECURITY_INFO_WAPI) {
					PRINT_SINGL_ARG("    Encryption: ", "WAPI", "%s");
#endif
				} else if (priv->site_survey->bss_backup[i1].t_stamp[0] & (BIT8|BIT10|BIT24|BIT26)) {
					int wpa_cipher = 0, wpa2_cipher = 0;

					wpa_cipher = priv->site_survey->bss_backup[i1].t_stamp[0] & (BIT8|BIT10);
					wpa2_cipher = (priv->site_survey->bss_backup[i1].t_stamp[0] >> 16) & (BIT8|BIT10);

					tmpbuf[0] = 0;
					if (wpa_cipher == BIT8) {
						snprintf(tmpbuf, sizeof(tmpbuf), "WPA-TKIP");
					} else if (wpa_cipher == BIT10) {
						snprintf(tmpbuf, sizeof(tmpbuf), "WPA-AES");
					} else if (wpa_cipher == (BIT8|BIT10)) {
						snprintf(tmpbuf, sizeof(tmpbuf), "WPA-TKIP+AES");
					}
					if (wpa_cipher && wpa2_cipher)
						strncat(tmpbuf, "/", (sizeof(tmpbuf) - strlen(tmpbuf) - 1));
					
					if (wpa2_cipher == BIT8) {
						snprintf(tmpbuf, sizeof(tmpbuf), "WPA2-TKIP");
					} else if (wpa2_cipher == BIT10) {
						snprintf(tmpbuf, sizeof(tmpbuf), "WPA2-AES");
					} else if (wpa2_cipher == (BIT8|BIT10)) {
						snprintf(tmpbuf, sizeof(tmpbuf), "WPA2-TKIP+AES");
					}
					PRINT_SINGL_ARG("    Encryption: ", tmpbuf, "%s");
				} else {
					PRINT_SINGL_ARG("    Encryption: ", "Unknown", "%s");
				}

				tmpbuf[0] = 0;
				if (priv->site_survey->bss_backup[i1].network & WIRELESS_11B)
					strncat(tmpbuf, "B", (sizeof(tmpbuf) - strlen(tmpbuf) - 1));
				if (priv->site_survey->bss_backup[i1].network & WIRELESS_11G)
					strncat(tmpbuf, "G", (sizeof(tmpbuf) - strlen(tmpbuf) - 1));
				if (priv->site_survey->bss_backup[i1].network & WIRELESS_11A)
					strncat(tmpbuf, "A", (sizeof(tmpbuf) - strlen(tmpbuf) - 1));
				if (priv->site_survey->bss_backup[i1].network & WIRELESS_11N)
					strncat(tmpbuf, "N", (sizeof(tmpbuf) - strlen(tmpbuf) - 1));
				if (priv->site_survey->bss_backup[i1].network & WIRELESS_11AC)
					strncat(tmpbuf, "AC", (sizeof(tmpbuf) - strlen(tmpbuf) - 1));
				PRINT_SINGL_ARG("    Network: ", tmpbuf, "%s");

#ifdef RTK_AC_SUPPORT
				bw = (priv->site_survey->bss_backup[i1].t_stamp[1] >> BSS_BW_SHIFT) & BSS_BW_MASK;
				if (bw) {
					if (bw == CHANNEL_WIDTH_80) {
						PRINT_SINGL_ARG("    Bandwidth: ", "80MHz", "%s");
					} else if (bw == CHANNEL_WIDTH_160) {
						PRINT_SINGL_ARG("    Bandwidth: ", "160MHz", "%s");
					} else if (bw == CHANNEL_WIDTH_80_80) {
						PRINT_SINGL_ARG("    Bandwidth: ", "80MHz+80MHz", "%s");
					}
				}
				else
#endif
				if ((priv->site_survey->bss_backup[i1].t_stamp[1] & 0x6) == 0) {
                    PRINT_SINGL_ARG("    Bandwidth: ", "20MHz", "%s");
                } else if ((priv->site_survey->bss_backup[i1].t_stamp[1] & 0x4) == 0) {
                    PRINT_SINGL_ARG("    Bandwidth: ", "40MHz above", "%s");
                } else {
                    PRINT_SINGL_ARG("    Bandwidth: ", "40MHz below", "%s");
                }
                PRINT_ONE("    ====================", "%s", 1);

                size = pos - base;
                CHECK_LEN;
                pos = len;
            }
        }
    }

#ifdef CH_LOAD_CAL
    if (IS_ROOT_INTERFACE(priv)) {
        unsigned char tmp2[128];
        PRINT_ONE("\n    ====================", "%s", 1);
        CHECK_LEN;
        len = pos;
        PRINT_ONE("    channel utilization(0~100%)", "%s", 1);
        CHECK_LEN;
        len = pos;
        for(i1=0; i1<SUPPORT_CH_NUM ;i1++){
            if (priv->pshare->ch_ss_info[i1].channel !=0 ) {
                /*sprintf(tmp2, "    Channel:%d, ch_load:%d",
                    priv->pshare->ch_ss_info[i1].channel, priv->pshare->ch_ss_info[i1].chload_time);*/
                sprintf(tmp2, "    Channel:%d, ch_load:%d, free_time:%d, interence_time:%d, noise_level:%d",
                    priv->pshare->ch_ss_info[i1].channel, 
                    priv->pshare->ch_ss_info[i1].chload_time,
                    priv->pshare->ch_ss_info[i1].free_time,
                    priv->pshare->ch_ss_info[i1].interence_time,
                    priv->pshare->ch_ss_info[i1].nhm_pwr - 100);
                PRINT_ONE(tmp2, "%s", 1);
                CHECK_LEN;
                len = pos;
            } else {
                break;   
            }
        }
    }
#endif

#ifdef CONFIG_RTL_PROC_NEW
    return 0;
#else
    *eof = 1;

_ret:
    *start = buf + (offset - begin);	/* Start of wanted data */
    len -= (offset - begin);	/* Start slop */
    if (len > length)
        len = length;	/* Ending slop */

    return len;
#endif	
}
#endif
#ifdef CLIENT_MODE
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_up_read(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_up_read(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int len = 0;
#ifdef __ECOS
	ecos_pr_fun("%d\n",priv->up_flag);
#elif defined(CONFIG_RTL_PROC_NEW)
	seq_printf(s, "%d\n",priv->up_flag);
#else
	len=snprintf(buf, length, "%d",priv->up_flag);
#endif
	return len;
}

#ifdef __ECOS
void rtl8192cd_proc_up_write(char *tmp, void *data)
#else
static int rtl8192cd_proc_up_write(struct file *file, const char *buffer,
				unsigned long count, void *data)
#endif
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
#ifdef __KERNEL__
	char tmp[4];
#endif

#ifdef __ECOS
	if(0 == (tmp[0]-'0'))
		priv->up_flag = 0;
#else
	if (buffer && !copy_from_user(tmp, buffer, 4)) {
		if(0 == (tmp[0]-'0'))
			priv->up_flag = 0;
	}
	return count;
#endif
}
#endif

#ifdef CONFIG_RTL_WLAN_STATUS
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_up_event_read(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_up_event_read(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	int len = 0;

#ifdef __ECOS
	ecos_pr_fun("%d\n",priv->wlan_status_flag);
#elif defined(CONFIG_RTL_PROC_NEW)
	seq_printf(s, "%d\n",priv->wlan_status_flag);
#else
	len=snprintf(buf, length, "%d", priv->wlan_status_flag);
#endif
	return len;
}

#ifdef __ECOS
static int rtl8192cd_proc_up_event_write(char *tmp, void *data)
#else
static int rtl8192cd_proc_up_event_write(struct file *file, const char *buffer,
				unsigned long count, void *data)
#endif				
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
#ifdef __KERNEL__
	char tmp[4];
#endif

#ifdef __ECOS
	priv->wlan_status_flag = (tmp[0]-'0');
	return 0;
#else
	if (buffer && !copy_from_user(tmp, buffer, 4)) {
		priv->wlan_status_flag=tmp[0]-'0';
	}	
	return count;
#endif	
}
#endif

#ifdef CONFIG_RTK_VLAN_SUPPORT
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_vlan_read(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_vlan_read(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	PRINT_ONE("  vlan setting...", "%s", 1);
	PRINT_SINGL_ARG("    global_vlan: ", priv->pmib->vlan.global_vlan, "%d");
	PRINT_SINGL_ARG("    is_lan: ", priv->pmib->vlan.is_lan, "%d");
	PRINT_SINGL_ARG("    vlan_enable: ", priv->pmib->vlan.vlan_enable, "%d");
	PRINT_SINGL_ARG("    vlan_tag: ", priv->pmib->vlan.vlan_tag, "%d");
	PRINT_SINGL_ARG("    vlan_id: ", priv->pmib->vlan.vlan_id, "%d");
	PRINT_SINGL_ARG("    vlan_pri: ", priv->pmib->vlan.vlan_pri, "%d");
	PRINT_SINGL_ARG("    vlan_cfi: ", priv->pmib->vlan.vlan_cfi, "%d");
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)|| defined(CONFIG_RTL_HW_VLAN_SUPPORT)
	PRINT_SINGL_ARG("    vlan_forwarding_rule: ", priv->pmib->vlan.forwarding_rule, "%d");
#endif

	return pos;
}


#ifdef CONFIG_RTL_KERNEL_MIPS16_WLAN
__NOMIPS16
#endif
static int rtl8192cd_proc_vlan_write(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
	#ifdef CONFIG_RTL_PROC_NEW
	struct net_device *dev = pde_data(file_inode(file));
	#else
	struct net_device *dev = (struct net_device *)data;
	#endif
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	char tmp[128];

	if (count < 2)
		return -EFAULT;
	if (buffer && !copy_from_user(tmp, buffer, 128)) {
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)|| defined(CONFIG_RTL_HW_VLAN_SUPPORT)
		int num = sscanf(tmp, "%d %d %d %d %d %d %d %d",
#else
		int num = sscanf(tmp, "%d %d %d %d %d %d %d",
#endif
			&priv->pmib->vlan.global_vlan, &priv->pmib->vlan.is_lan,
			&priv->pmib->vlan.vlan_enable, &priv->pmib->vlan.vlan_tag,
			&priv->pmib->vlan.vlan_id, &priv->pmib->vlan.vlan_pri,
			&priv->pmib->vlan.vlan_cfi
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)|| defined(CONFIG_RTL_HW_VLAN_SUPPORT)
			, &priv->pmib->vlan.forwarding_rule
#endif
			);

#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)|| defined(CONFIG_RTL_HW_VLAN_SUPPORT)
		if (num != 8)
#else
		if (num != 7)
#endif
		{
			panic_printk("invalid vlan parameter!\n");
		}
	}

#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
	rtl_add_vlan_info((struct vlan_info *)&priv->pmib->vlan, dev);
#endif

	return count;
}
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT
int is_rtl_nat_wlan_vlan(struct net_device *dev)
{
   struct rtl8192cd_priv *priv;
   #ifdef NETDEV_NO_PRIV
        priv = ((struct rtl8192cd_priv *)netdev_priv(dev))->wlan_priv;
   #else
        priv = (struct rtl8192cd_priv *)dev->priv;
   #endif
   if(priv->pmib->vlan.global_vlan && priv->pmib->vlan.vlan_enable )
        if(priv->pmib->vlan.vlan_id && (priv->pmib->vlan.forwarding_rule==2)) //NAT rule
           return 1;

    return 0;
}
#endif
#endif // CONFIG_RTK_VLAN_SUPPORT


#ifdef SUPPORT_MULTI_PROFILE
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_ap_profile(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_ap_profile(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	int pos = 0, i;
	unsigned char tmpbuf[100];

	PRINT_ONE("  Client Profile...", "%s", 1);
	PRINT_SINGL_ARG("    enable_profile: ", priv->pmib->ap_profile.enable_profile, "%d");
	PRINT_SINGL_ARG("    profile_num: ", priv->pmib->ap_profile.profile_num, "%d");
	PRINT_SINGL_ARG("    in_use_profile: ", ((priv->profile_idx == 0) ? (priv->pmib->ap_profile.profile_num-1) : (priv->profile_idx-1)), "%d");

	for (i=0; i<priv->pmib->ap_profile.profile_num && i<PROFILE_NUM; i++) {
		snprintf(tmpbuf, sizeof(tmpbuf), "       profile[%d]...", i);
		PRINT_ONE(tmpbuf, "%s", 1);
		PRINT_SINGL_ARG("         ssid: ", priv->pmib->ap_profile.profile[i].ssid, "%s");
		PRINT_SINGL_ARG("         encryption: ", priv->pmib->ap_profile.profile[i].encryption, "%d");
		PRINT_SINGL_ARG("         auth_type: ", priv->pmib->ap_profile.profile[i].auth_type, "%d");
		if (priv->pmib->ap_profile.profile[i].encryption == 1 || priv->pmib->ap_profile.profile[i].encryption == 2) {
			PRINT_SINGL_ARG("         wep_default_key: ", priv->pmib->ap_profile.profile[i].wep_default_key, "%d");
			 if (priv->pmib->ap_profile.profile[i].encryption == 1) {
				PRINT_ARRAY_ARG("         wep_key1: ", priv->pmib->ap_profile.profile[i].wep_key1, "%02x", 5);
				PRINT_ARRAY_ARG("         wep_key2: ", priv->pmib->ap_profile.profile[i].wep_key2, "%02x", 5);
				PRINT_ARRAY_ARG("         wep_key3: ", priv->pmib->ap_profile.profile[i].wep_key3, "%02x", 5);
				PRINT_ARRAY_ARG("         wep_key4: ", priv->pmib->ap_profile.profile[i].wep_key4, "%02x", 5);
			}
			else {
				PRINT_ARRAY_ARG("         wep_key1: ", priv->pmib->ap_profile.profile[i].wep_key1, "%02x", 13);
				PRINT_ARRAY_ARG("         wep_key2: ", priv->pmib->ap_profile.profile[i].wep_key2, "%02x", 13);
				PRINT_ARRAY_ARG("         wep_key3: ", priv->pmib->ap_profile.profile[i].wep_key3, "%02x", 13);
				PRINT_ARRAY_ARG("         wep_key4: ", priv->pmib->ap_profile.profile[i].wep_key4, "%02x", 13);
			}
		}
		else if (priv->pmib->ap_profile.profile[i].encryption == 3 || priv->pmib->ap_profile.profile[i].encryption == 4) {
			PRINT_SINGL_ARG("         wpa_cipher: ", priv->pmib->ap_profile.profile[i].wpa_cipher, "%d");
			PRINT_SINGL_ARG("         wpa_psk: ", priv->pmib->ap_profile.profile[i].wpa_psk, "%s");
		#ifdef CONFIG_IEEE80211W_CLI
			PRINT_SINGL_ARG("         bss_PMF: ", priv->pmib->ap_profile.profile[i].bss_PMF, "%d");
		#endif
		}
	}
	return pos;
}
#endif // SUPPORT_MULTI_PROFILE

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_misc(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_misc(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	int pos = 0;
	int idx;

	PRINT_ONE("  miscEntry...", "%s", 1);
	PRINT_SINGL_ARG("	 telco_selected: ", priv->pmib->miscEntry.telco_selected, "%d");
	PRINT_SINGL_ARG("    show_hidden_bss: ", priv->pmib->miscEntry.show_hidden_bss, "%d");
	PRINT_SINGL_ARG("    ack_timeout: ", (unsigned char)priv->pmib->miscEntry.ack_timeout, "%d");
	PRINT_ARRAY_ARG("    private_ie: ", priv->pmib->miscEntry.private_ie, "%02x", priv->pmib->miscEntry.private_ie_len);
	PRINT_SINGL_ARG("    rxInt: ", priv->pmib->miscEntry.rxInt_thrd, "%d");
#ifdef DRVMAC_LB
	PRINT_SINGL_ARG("    dmlb: ", priv->pmib->miscEntry.drvmac_lb, "%d");
	PRINT_ARRAY_ARG("    lb_da: ", priv->pmib->miscEntry.lb_da, "%02x", MACADDRLEN);
	PRINT_SINGL_ARG("    lb_tps: ", priv->pmib->miscEntry.lb_tps, "%d");
#endif
	PRINT_SINGL_ARG("    groupID: ", priv->pmib->miscEntry.groupID, "%d");
	PRINT_SINGL_ARG("    rc_enable: ", priv->pmib->reorderCtrlEntry.ReorderCtrlEnable, "%d");
	PRINT_SINGL_ARG("    rc_winsz: ", priv->pmib->reorderCtrlEntry.ReorderCtrlWinSz, "%d");
	PRINT_SINGL_ARG("    rc_timeout: ", priv->pmib->reorderCtrlEntry.ReorderCtrlTimeout, "%d");
	PRINT_SINGL_ARG("    rc_timeout_cli: ", priv->pmib->reorderCtrlEntry.ReorderCtrlTimeoutCli, "%d");
	PRINT_SINGL_ARG("    vap_enable: ", priv->pmib->miscEntry.vap_enable, "%d");
#ifdef RESERVE_TXDESC_FOR_EACH_IF
	PRINT_SINGL_ARG("    rsv_txdesc: ", GET_ROOT(priv)->pmib->miscEntry.rsv_txdesc, "%d");
#endif
	PRINT_SINGL_ARG("    func_off: ", priv->pmib->miscEntry.func_off, "%d");
#ifdef AUTO_CHANNEL_TIMEOUT
	PRINT_SINGL_ARG("    autoch_timeout: ", priv->pmib->miscEntry.autoch_timeout, "%d");
	PRINT_SINGL_ARG("    autoch_diff_pkt: ", priv->pmib->miscEntry.autoch_diff_pkt, "%d");
	
	PRINT_SINGL_ARG("    autoch_timeout_count: ", priv->pshare->autoch_timeout_count, "%lu");
	PRINT_SINGL_ARG("    tx_packets_total: ", priv->pshare->tx_packets_total, "%llu");
	PRINT_SINGL_ARG("    tx_packets_pre: ", priv->pshare->tx_packets_pre, "%llu");
	PRINT_SINGL_ARG("    tx_packets_pre2: ", priv->pshare->tx_packets_pre2, "%llu");
	PRINT_SINGL_ARG("    rx_packets_total: ", priv->pshare->rx_packets_total, "%llu");
	PRINT_SINGL_ARG("    rx_packets_pre: ", priv->pshare->rx_packets_pre, "%llu");
	PRINT_SINGL_ARG("    rx_packets_pre2 ", priv->pshare->rx_packets_pre2, "%llu");
	PRINT_SINGL_ARG("    total_tp: ", priv->pshare->total_tp, "%u");
	PRINT_SINGL_ARG("    total_sta_num: ", priv->pshare->total_sta_num, "%d");
#endif
    PRINT_SINGL_ARG("    raku_only: ", priv->pmib->miscEntry.raku_only, "%d");
#ifdef TV_MODE	
    PRINT_SINGL_ARG("    tv_mode_status: ", priv->tv_mode_status, "%d");
#endif	

#ifdef CONFIG_RECORD_CLIENT_HOST
	PRINT_SINGL_ARG("    client_host_sniffer_enable: ", priv->pmib->miscEntry.client_host_sniffer_enable, "%d");
#endif

#ifdef TX_EARLY_MODE
	PRINT_SINGL_ARG("    em_waitq_on: ", GET_ROOT(priv)->pshare->em_waitq_on, "%d");
#endif
#ifdef SUPPORT_MONITOR
	PRINT_SINGL_ARG("	 chan_switch_time: ", priv->pmib->miscEntry.chan_switch_time, "%d");
	PRINT_SINGL_ARG("	 chan_switch_disable: ", priv->pmib->miscEntry.chan_switch_disable, "%d");
	PRINT_SINGL_ARG("	 pkt_filter_len: ", priv->pmib->miscEntry.pkt_filter_len, "%d");
#endif
#if defined(WIFI_11N_2040_COEXIST_EXT)
    PRINT_BITMAP_ARG("    80M map", priv->pshare->_80m_staMap);
	PRINT_BITMAP_ARG("    40M map", priv->pshare->_40m_staMap);
#endif
#ifdef CONFIG_WLAN_HAL
	PRINT_SINGL_ARG("    RxTag mismatch:   ", priv->pshare->RxTagMismatchCount, "%d");
	PRINT_SINGL_ARG("    H2C first stage ctr: ", priv->pshare->h2c_box_to_first, "%d");
	PRINT_SINGL_ARG("    H2C second stage ctr: ", priv->pshare->h2c_box_to_second, "%d");
	PRINT_SINGL_ARG("    H2C full ctr: ", priv->pshare->h2c_box_full, "%d");
#endif
	PRINT_SINGL_ARG("    Unlock pwr: ", priv->pshare->unlock_counter1, "%u");
	PRINT_SINGL_ARG("    Unlock timer: ", priv->pshare->unlock_counter2, "%u");	
	PRINT_SINGL_ARG("    lock counter: ", priv->pshare->lock_counter, "%u");
#ifdef USE_OUT_SRC
#ifdef _OUTSRC_COEXIST
	if(IS_OUTSRC_CHIP(priv))
#endif
	{
		PRINT_SINGL_ARG("    False alarm: ", ODMPTR->false_alm_cnt.cnt_all, "%d");
		PRINT_SINGL_ARG("    CCA: ", ODMPTR->false_alm_cnt.cnt_cca_all, "%d");
	}
#endif
#if !defined(USE_OUT_SRC) || defined(_OUTSRC_COEXIST)
#ifdef _OUTSRC_COEXIST
	if(!IS_OUTSRC_CHIP(priv))
#endif
	{
		PRINT_SINGL_ARG("    False alarm: ", priv->pshare->FA_total_cnt, "%d");
		PRINT_SINGL_ARG("    CCA: ", priv->pshare->CCA_total_cnt, "%d");	
	}
#endif	

#ifdef CONFIG_SPECIAL_ENV_TEST
#if defined (HW_ANT_SWITCH) && !defined(CONFIG_RTL_92C_SUPPORT) && !defined(CONFIG_RTL_92D_SUPPORT)
	if (priv->pshare->_dmODM.ant_div_type) {
		PRINT_SINGL_ARG("    ant_orig: ", priv->pshare->ant_orig, "%d");
	}
#endif
	PRINT_SINGL_ARG("    spec_env_test_en: ", priv->pshare->rf_ft_var.spec_env_test_en, "%d");
	PRINT_SINGL_ARG("    in_spec_env_test_to: ", priv->pshare->in_spec_env_test_to, "%d");
	PRINT_SINGL_ARG("    spirent_sta_num: ", priv->pshare->spirent_sta_num, "%d");
	PRINT_SINGL_ARG("    veriwave_sta_num: ", priv->pshare->veriwave_sta_num, "%d");
	PRINT_SINGL_ARG("    spec_env_latency_test: ", priv->pshare->spec_env_latency_test, "%d");
	PRINT_SINGL_ARG("    intel_sta_num: ", priv->pshare->intel_sta_num, "%d");
	PRINT_SINGL_ARG("    veriwave_sifs_bk_status: ", priv->pshare->veriwave_sifs_bk_status, "%d");
#endif // CONFIG_SPECIAL_ENV_TEST
#ifdef SMP_SYNC
	PRINT_SINGL_ARG("    expire_timer_cpuid: ", priv->pshare->expire_timer_cpuid, "%d");
#if defined(SMP_LOAD_BALANCE_SUPPORT) && (MAX_SKB_XMIT_QUEUE > 1)
	PRINT_SINGL_ARG("    skb_xmit_queue_num: ", priv->pshare->skb_xmit_queue_num, "%d");
#endif
#endif
#ifdef SMP_RX_LOAD_BALANCE
#if (SMP_RX_LB_LEVEL == 1)
	PRINT_SINGL_ARG("    netif_rx_queue: ", skb_queue_len(&priv->pshare->netif_rx_queue), "%ld");
#elif (SMP_RX_LB_LEVEL == 2)
	PRINT_SINGL_ARG("    rxq_qlen:     ", skb_queue_len(&priv->rx_data_queue), "%ld");
	PRINT_SINGL_ARG("    rxq_ind_seq:  ", priv->pshare->rxq_ind_seq, "%ld");
	PRINT_SINGL_ARG("    rxq_proc_seq: ", priv->pshare->rxq_proc_seq, "%ld");
#endif
#endif

#if defined(CONFIG_VERIWAVE_CHECK)
	PRINT_SINGL_ARG("    check_MACBBTX_counter: ", priv->pshare->check_MACBBTX_counter, "%lu");
#endif

#ifdef RTK_MULTI_AP
	PRINT_SINGL_ARG("    BSS Type: ", priv->pmib->multi_ap.multiap_bss_type, "%02x");
#endif

#ifdef CONFIG_WLAN_HAL
	if(IS_HAL_CHIP(priv))
	{
		PHAL_DATA_TYPE      pHalData = _GET_HAL_DATA(priv);
		PRINT_SINGL_ARG("    IntMask[0]: ", pHalData->IntMask[0], "%08x");
		PRINT_SINGL_ARG("    IntMask[1]: ", pHalData->IntMask[1], "%08x");
#if IS_RTL88XX_MAC_V2_V3_V4
		if (_GET_HAL_DATA(priv)->MacVersion.is_MAC_v2_v3_v4) {
			PRINT_SINGL_ARG("    IntMask[2]: ", pHalData->IntMask[2], "%08x");
			PRINT_SINGL_ARG("    IntMask[3]: ", pHalData->IntMask[3], "%08x");
		}
#endif
	}
#endif // CONFIG_WLAN_HAL
	
	PRINT_SINGL_ARG("    manual_thru_mode: ", priv->pmib->miscEntry.manual_thru_mode, "%d");
	PRINT_SINGL_ARG("    active_thru_mode: ", priv->pshare->active_thru_mode, "%d");

#ifdef SSID_PRIORITY_SUPPORT	
		PRINT_SINGL_ARG("	 Manual Priority: ", priv->pmib->miscEntry.manual_priority, "%d");
#endif

//20171205 ICV_DEBUG
#ifdef CONFIG_WLAN_HAL
	if (IS_HAL_CHIP(priv)) {
		PRINT_SINGL_ARG("    HW CRC Err: ", priv->pshare->HW_CRC_error_cnt, "%d");
		PRINT_SINGL_ARG("    HW ICV Err FCS OK: ", priv->pshare->HW_CRC_err_FCS_ok_cnt, "%d");
	}
#endif

#ifdef BEACON_VS_IE
	PRINT_ONE("    Probe vsie entry:", "%s", 1);
	for(idx = 0;idx < NUM_PRBVSIE_SERVICE;idx++)
	{
		if(priv->prb_vsie[idx].service_name_len != 0)
		{
			PRINT_ARRAY_ARG("        ", priv->prb_vsie[idx].oui, "%02x", 3);
		}
	}
#endif
#ifdef CTC_2G_ACS
    PRINT_SINGL_ARG("    ss_score_timeout_count: ", atomic_read(&(priv->pshare->ss_score_timeout_count)), "%d"); 
    PRINT_SINGL_ARG("    autoch_score_timeout: ", atomic_read(&(priv->pshare->autoch_score_timeout)), "%d"); 
#else
    PRINT_SINGL_ARG("    ss_score_timeout_count: ", priv->pshare->ss_score_timeout_count, "%d"); 
#endif /* CTC_2G_ACS */
#ifdef WLAN_DIAGNOSTIC
    PRINT_SINGL_ARG("    unassoc_sta_scan_ongoing: ", priv->pmib->dot11StationConfigEntry.unassoc_sta_scan_ongoing, "%d");
#endif

#ifdef FORCE_QOS_SUPPORT
	PRINT_SINGL_ARG("    force_qos: ", priv->pshare->rf_ft_var.force_qos, "%d");
#endif
#ifdef CMCC_CTC_QOS
	PRINT_SINGL_ARG("    force_qos_timeout: ", priv->pshare->force_qos_timeout, "%d");
#endif

	return pos;
}


#ifdef WIFI_SIMPLE_CONFIG
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_wsc(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_wsc(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  wscEntry...", "%s", 1);
	PRINT_SINGL_ARG("    wsc_enable: ", priv->pmib->wscEntry.wsc_enable, "%d");
	PRINT_ARRAY_ARG("    beacon_ie: ",
			priv->pmib->wscEntry.beacon_ie, "%02x", priv->pmib->wscEntry.beacon_ielen);
	PRINT_SINGL_ARG("    beacon_ielen: ", priv->pmib->wscEntry.beacon_ielen, "%d");
	PRINT_ARRAY_ARG("    probe_rsp_ie: ",
			priv->pmib->wscEntry.probe_rsp_ie, "%02x", priv->pmib->wscEntry.probe_rsp_ielen);
	PRINT_SINGL_ARG("    probe_rsp_ielen: ", priv->pmib->wscEntry.probe_rsp_ielen, "%d");
	PRINT_ARRAY_ARG("    probe_req_ie: ",
			priv->pmib->wscEntry.probe_req_ie, "%02x", priv->pmib->wscEntry.probe_req_ielen);
	PRINT_SINGL_ARG("    probe_req_ielen: ", priv->pmib->wscEntry.probe_req_ielen, "%d");
	PRINT_ARRAY_ARG("    assoc_ie: ",
			priv->pmib->wscEntry.assoc_ie, "%02x", priv->pmib->wscEntry.assoc_ielen);
	PRINT_SINGL_ARG("    assoc_ielen: ", priv->pmib->wscEntry.assoc_ielen, "%d");

	return pos;
}
#endif

#ifdef WLANHAL_MACDM

#if 1
#define MACDM_MAX_PROC_ARG_NUM  7
int rtl8192cd_proc_macdm_write(struct file *file, const char *buffer,
				unsigned long count, void *data)
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	char                tmp[60], command[8], action[10];
	unsigned int        num;
    unsigned int        mode_sel, state_sel, rssi_idx, threshold, state_idx, reg_idx, reg_offset, reg_value;
    PHAL_DATA_TYPE      pHalData = _GET_HAL_DATA(priv);

	if (buffer && !copy_from_user(tmp, buffer, 32)) {
		num = sscanf(tmp, "%s %s", command, action);

		if (num == 0) {
			panic_printk("argument num: 0 (invalid)\n");
			return num;
		}
        else if (num > MACDM_MAX_PROC_ARG_NUM) {
            panic_printk("argument num: %d (invalid)\n", num);
			return num;
        }
	}

	panic_printk("Command: [%s], action: [%s]\n", command, action);

	if (!memcmp(command, "config", 6)) {
        if (!memcmp(action, "mode", 4)) {
    		num = sscanf(tmp, "%s %s %u", command, action, &mode_sel);

            pHalData->MACDM_Mode_Sel = mode_sel;

            if (num != 3) {
                panic_printk("argument num:%d (invalid, should be 3)\n", num);
            }
        }
        else if (!memcmp(action, "thrs", 4)) {
    		num = sscanf(tmp, "%s %s %u %u %u", command, action, &state_sel, &rssi_idx, &threshold);

            pHalData->MACDM_stateThrs[state_sel][rssi_idx] = threshold;

            if (num != 5) {
                panic_printk("argument num:%d (invalid, should be 5)\n", num);
            }
        }
        else if (!memcmp(action, "table", 5)) {
    		num = sscanf(tmp, "%s %s %u %u %u %x %x", command, action, &state_idx, &rssi_idx, &reg_idx, &reg_offset, &reg_value);

            pHalData->MACDM_Table[state_idx][rssi_idx][reg_idx].offset   =  reg_offset;
            pHalData->MACDM_Table[state_idx][rssi_idx][reg_idx].value    =  reg_value;

            if (num != MACDM_MAX_PROC_ARG_NUM) {
                panic_printk("argument num:%d (invalid, should be %d)\n", num, MACDM_MAX_PROC_ARG_NUM);
            }
        }
        else if (!memcmp(action, "help", 4)) {
            panic_printk("Command example:\n");
            panic_printk("  config mode [mode_sel(0~3)]\n");
            panic_printk("  config thrs [state_sel(0~3)] [rssi_idx(0~2)] [threshold(dec)]\n");
            panic_printk("  config table [state_idx(0~2)] [rssi_idx(0~2)] [reg_idx(0~29)] [reg_offset(hex)] [reg_value(hex)]\n");
        }
        else {
            panic_printk("action code: %s (invalid, should be mode/thrs/table/help)\n", action);
        }
	}
    else {
        panic_printk("Command not supported!, please type 'config help'\n");
    }

	return count;
}
#endif

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_macdm(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_macdm(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
    PHAL_DATA_TYPE              pHalData    = _GET_HAL_DATA(priv);
    unsigned int                stateThrs_idx, state_idx, rssi_idx, reg_idx;


	PRINT_ONE("  MACDM...", "%s", 1);
	PRINT_SINGL_ARG("    MACDM_Mode: ", pHalData->MACDM_Mode_Sel, "%d");
    PRINT_SINGL_ARG("    MACDM_state: ", pHalData->MACDM_State, "%d");

    //Threshold
    for (stateThrs_idx=0; stateThrs_idx<MACDM_TP_THRS_MAX_NUM; stateThrs_idx++) {
        for (rssi_idx=0; rssi_idx<RSSI_LVL_MAX_NUM; rssi_idx++) {
            PRINT_ONE(stateThrs_idx, "MACDM_stateThrs[0x%x]", 0);
            PRINT_ONE(rssi_idx, "[0x%x]:", 0);
            PRINT_ONE(pHalData->MACDM_stateThrs[stateThrs_idx][rssi_idx], "0x%x", 1);
        }
    }

    //Table
    for (state_idx=0; state_idx<MACDM_TP_STATE_MAX_NUM; state_idx++) {
        PRINT_SINGL_ARG("\n    MACDM_Table, state: ", state_idx, "%d");
        for(rssi_idx=0; rssi_idx<RSSI_LVL_MAX_NUM; rssi_idx++) {
            PRINT_SINGL_ARG("    MACDM_Table, rssi: ", rssi_idx, "%d");

            for(reg_idx=0; reg_idx<MAX_MACDM_REG_NUM; reg_idx++) {
                if (pHalData->MACDM_Table[state_idx][rssi_idx][reg_idx].offset == 0xffff) {
                    break;
                }

                PRINT_ONE(pHalData->MACDM_Table[state_idx][rssi_idx][reg_idx].offset, "Reg: 0x%x, ", 0);
                PRINT_ONE(pHalData->MACDM_Table[state_idx][rssi_idx][reg_idx].value, "Value: 0x%x", 1);
            }
        }
    }

	return pos;
}
#endif //WLANHAL_MACDM
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_all(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_all(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

#ifndef CONFIG_RTL_PROC_NEW    
    int len = 0;
#endif
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    off_t begin = 0;
    off_t pos = 0;
#endif
    int size;

#if !defined(CONFIG_RTL_PROC_NEW)
	unsigned char *pTmp;

#if defined(__KERNEL__)
	pTmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pTmp) {
		printk("%s: Error! can not allocate memory\n");
		return -ENOMEM;
	}
#endif
#endif

#ifdef __ECOS
    ecos_pr_fun("  Make info: v%d.%d.%d.%d (%s)\n", DRV_VERSION_H, DRV_VERSION_L, DRV_VERSION_SUBL, DRV_VERSION_SUBSUBL, DRV_RELDATE);
#elif defined(CONFIG_RTL_PROC_NEW)
    seq_printf(s, "  Make info: v%d.%d.%d.%d (%s)\n", DRV_VERSION_H, DRV_VERSION_L, DRV_VERSION_SUBL, DRV_VERSION_SUBSUBL, DRV_RELDATE);
#else
    size = snprintf(pTmp, length, "  Make info: v%d.%d.%d.%d (%s)\n", DRV_VERSION_H, DRV_VERSION_L, DRV_VERSION_SUBL, DRV_VERSION_SUBSUBL, DRV_RELDATE);
    CHECK_LEN_E;
#endif

#if defined(SVN_REV) || defined(GIT_SHA)
	if (priv->pmib->miscEntry.show_rev) {
#if defined(SVN_REV)
		seq_printf(s, "  REV: SVN version: r%s\n", SVN_REV);
#elif defined(GIT_SHA)
		seq_printf(s, "  REV: Git SHA-1 ID: %s", GIT_SHA);
		if (strlen(GIT_BR))
			seq_printf(s, ", Branch: %s", GIT_BR);
		if (strlen(GIT_TAG))
			seq_printf(s, ", Tag: %s", GIT_TAG);
		seq_printf(s, "\n");
#endif
	}
#endif // (SVN_REV || GIT_SHA)

#ifdef RTK_SMART_ROAMING
#ifdef __ECOS
    ecos_pr_fun("  %s\n", RTK_SMART_ROAMING_VERSION);
#elif defined(CONFIG_RTL_PROC_NEW)
    seq_printf(s, "  %s\n", RTK_SMART_ROAMING_VERSION);
#else
    size = snprintf(pTmp, length, "  %s\n", RTK_SMART_ROAMING_VERSION);
    CHECK_LEN_E;
#endif
#endif

#ifdef CONFIG_RTL_88E_SUPPORT
#if !(defined(__KERNEL__) && (defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)))
    if (GET_CHIP_VER(priv) != VERSION_8188E)
#endif
#endif
    {
#ifdef __ECOS
        ecos_pr_fun("  Realtek firmware version: %d.%d,  built time: %02d-%02d %02d:%02d\n",
                priv->pshare->fw_version, priv->pshare->fw_sub_version, 
                priv->pshare->fw_date_month, priv->pshare->fw_date_day,
                priv->pshare->fw_date_hour, priv->pshare->fw_date_minute);
#elif defined(CONFIG_RTL_PROC_NEW)
        seq_printf(s, "  Realtek firmware version: %d.%d,  built time: %02d-%02d %02d:%02d\n",
                priv->pshare->fw_version, priv->pshare->fw_sub_version,
                priv->pshare->fw_date_month, priv->pshare->fw_date_day,
                priv->pshare->fw_date_hour, priv->pshare->fw_date_minute);
#else
        size = snprintf(pTmp, length-len, "  Realtek firmware version: %d.%d,  built time: %02d-%02d %02d:%02d\n",
            priv->pshare->fw_version, priv->pshare->fw_sub_version,
            priv->pshare->fw_date_month, priv->pshare->fw_date_day,
            priv->pshare->fw_date_hour, priv->pshare->fw_date_minute);
        CHECK_LEN_E;
#endif
    }
#if defined(CONFIG_WLAN_HAL_8814BE) && defined(CONFIG_RTL_OFFLOAD_DRIVER)
    if (GET_CHIP_VER(priv) == VERSION_8814B)
	{
		HALMAC_FW_VERSION fw_version;
		extern u32 get_ext_fw_version(HAL_PADAPTER Adapter, HALMAC_FW_VERSION *fw_version);
		u32 fw_revision = get_ext_fw_version(priv, &fw_version);
#ifdef __ECOS
        ecos_pr_fun("  DataCPU firmware version: %d.%d.%d/r%d,  built time: %02d-%02d %02d:%02d\n",
                fw_version.version, fw_version.sub_version, fw_version.sub_index, fw_revision,
                fw_version.build_time.month, fw_version.build_time.date,
                fw_version.build_time.hour, fw_version.build_time.min);
#elif defined(CONFIG_RTL_PROC_NEW)
        seq_printf(s, "  DataCPU firmware version: %d.%d.%d/r%d,  built time: %02d-%02d %02d:%02d\n",
                fw_version.version, fw_version.sub_version, fw_version.sub_index, fw_revision,
                fw_version.build_time.month, fw_version.build_time.date,
                fw_version.build_time.hour, fw_version.build_time.min);
#else
        size = snprintf(pTmp, length-len, "  DataCPU firmware version: %d.%d.%d/r%d,  built time: %02d-%02d %02d:%02d\n",
            fw_version.version, fw_version.sub_version, fw_version.sub_index, fw_revision,
            fw_version.build_time.month, fw_version.build_time.date,
            fw_version.build_time.hour, fw_version.build_time.min);
        CHECK_LEN_E;
#endif
	}
#endif

#ifdef CONFIG_RTL_PROC_NEW
    rtl8192cd_proc_mib_rf(s, data);
    rtl8192cd_proc_mib_operation(s, data);
    rtl8192cd_proc_mib_staconfig(s, data);
    rtl8192cd_proc_mib_dkeytbl(s, data);
    rtl8192cd_proc_mib_auth(s, data);
	rtl8192cd_proc_pmk(s, data);
#ifdef AUTH_SAE
    rtl8192cd_proc_sae_peertable(s, data);
#endif
    rtl8192cd_proc_mib_gkeytbl(s, data);
    rtl8192cd_proc_mib_bssdesc(s, data);
    rtl8192cd_proc_mib_erp(s, data);
    rtl8192cd_proc_mib_misc(s, data);
#ifdef BT_COEXIST
	rtl8192cd_proc_bt_coexist(s, data);
#endif
#ifdef WIFI_SIMPLE_CONFIG
    rtl8192cd_proc_mib_wsc(s, data);
#endif
    rtl8192cd_proc_probe_info(s, data);

#ifdef HS2_SUPPORT
    rtl8192cd_proc_mib_hs2(s, data);
#endif

#ifdef WDS
    rtl8192cd_proc_mib_wds(s, data);
#endif

#ifdef RTK_BR_EXT
    rtl8192cd_proc_mib_brext(s, data);
#endif

#if (BEAMFORMING_SUPPORT == 1)
	rtl8192cd_proc_mib_txbf(s, data);
#endif
#ifdef DFS
    rtl8192cd_proc_mib_DFS(s, data);
#endif

#ifdef RTK_AC_SUPPORT
    rtl8192cd_proc_mib_rf_ac(s, data);
#endif

    size = rtl8192cd_proc_mib_11n(s, data);

#ifdef CONFIG_RTK_VLAN_SUPPORT
    rtl8192cd_proc_vlan_read(s, data);
#endif

#ifdef SUPPORT_MULTI_PROFILE
    rtl8192cd_proc_mib_ap_profile(s, data);
#endif
#ifdef THERMAL_CONTROL
	rtl8192cd_proc_thermal_control(s, data);
#endif
#else // !CONFIG_RTL_PROC_NEW
    size = rtl8192cd_proc_mib_rf(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

    size = rtl8192cd_proc_mib_operation(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

    size = rtl8192cd_proc_mib_staconfig(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

    size = rtl8192cd_proc_mib_dkeytbl(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

    size = rtl8192cd_proc_mib_auth(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

    size = rtl8192cd_proc_pmk(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
	
#ifdef AUTH_SAE
    size = rtl8192cd_proc_sae_peertable(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
#endif

    size = rtl8192cd_proc_mib_gkeytbl(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

    size = rtl8192cd_proc_mib_bssdesc(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

    size = rtl8192cd_proc_mib_erp(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

    size = rtl8192cd_proc_mib_misc(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

#ifdef WIFI_SIMPLE_CONFIG
    size = rtl8192cd_proc_mib_wsc(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
#endif
#ifdef BT_COEXIST
	size = rtl8192cd_proc_bt_coexist(pTmp, start, offset, length, eof, data);
	CHECK_LEN_E;
#endif
    size = rtl8192cd_proc_probe_info(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

#ifdef HS2_SUPPORT
    size = rtl8192cd_proc_mib_hs2(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
#endif
#ifdef WDS
    size = rtl8192cd_proc_mib_wds(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
#endif

#ifdef RTK_BR_EXT
    size = rtl8192cd_proc_mib_brext(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
#endif
#if (BEAMFORMING_SUPPORT == 1)
	size = rtl8192cd_proc_mib_txbf(pTmp, start, offset, length, eof, data);
	CHECK_LEN_E;
#endif

#ifdef DFS
    size = rtl8192cd_proc_mib_DFS(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
#endif

#ifdef RTK_AC_SUPPORT
#ifdef __KERNEL__
    if(len)
        goto _ret;
#endif
    size = rtl8192cd_proc_mib_rf_ac(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
#endif

    size = rtl8192cd_proc_mib_11n(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;

#ifdef CONFIG_RTK_VLAN_SUPPORT
    size = rtl8192cd_proc_vlan_read(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
#endif

#ifdef SUPPORT_MULTI_PROFILE
    size = rtl8192cd_proc_mib_ap_profile(pTmp, start, offset, length, eof, data);
    CHECK_LEN_E;
#endif
#ifdef THERMAL_CONTROL
	size = rtl8192cd_proc_thermal_control(buf+len, start, offset, length, eof, data);
	CHECK_LEN;
#endif
#endif // CONFIG_RTL_PROC_NEW

#ifdef CONFIG_RTL_PROC_NEW
    return 0;
#else
#ifdef __KERNEL__
    *eof = 1;

_ret:
    *start = buf + (offset - begin);	/* Start of wanted data */
    len -= (offset - begin);	/* Start slop */
    if (len > length)
        len = length;	/* Ending slop */

	kfree(pTmp);
#endif
    return len;
#endif
}

#if defined(__ECOS) 
#define CHECK_LEN_B do {} while(0)
#else
#if defined(CONFIG_RTL_PROC_NEW)
#define CHECK_LEN_B do {} while(0)
#else
#define MAX_CHAR_IN_A_LINE		80
// macro "CHECK_LEN_B" is used in dump_one_stainfo() only
#define CHECK_LEN_B { \
	if (pos > (remained_len - MAX_CHAR_IN_A_LINE)) {\
		return pos; \
	} \
}
#endif
#endif

//static int dump_one_stainfo(int num, struct stat_info *pstat, char *buf, char **start,
//			off_t offset, int length, int *eof, void *data)
#ifdef CONFIG_RTL_PROC_NEW
static int dump_one_stainfo(struct seq_file *s, int num, struct rtl8192cd_priv *priv, struct stat_info *pstat, int *rc)
#else
static int dump_one_stainfo(int num, struct rtl8192cd_priv *priv, struct stat_info *pstat, char *buf,
			off_t offset, int length, int remained_len, int *rc)
#endif			
{
	int pos = 0, idx = 0, sum = 0;
	unsigned int m, n, i, count;
	char tmp[32];
	//unsigned short rate;
	unsigned char tmpbuf[64];
	unsigned char *rate;
    unsigned char current_rate;

#ifdef SW_TX_QUEUE
	unsigned int	swq_en = 0;
#endif

	PRINT_ONE(num,  " %d: stat_info...", 1);
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    state: ", pstat->state, "%x");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    AuthAlgrthm: ", pstat->AuthAlgrthm, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    ieee8021x_ctrlport: ", pstat->ieee8021x_ctrlport, "%d");
	CHECK_LEN_B;
#ifdef CONFIG_IEEE80211W
	#ifdef CONFIG_IEEE80211W_CLI
	PRINT_SINGL_ARG("    bss support PMF : ", priv->bss_support_pmf, "%d");
	CHECK_LEN_B;
	#endif
	PRINT_SINGL_ARG("    isPMF: ", pstat->isPMF, "%d");
	CHECK_LEN_B;
#endif
	PRINT_ARRAY_ARG("    hwaddr: ",	pstat->cmn_info.mac_addr, "%02x", MACADDRLEN);
	CHECK_LEN_B;
#if defined(CONFIG_VERIWAVE_CHECK)    
	memcpy(priv->wlan_addr, pstat->cmn_info.mac_addr, WLAN_ADDR_LEN);
#endif
	PRINT_ARRAY_ARG("    bssrateset: ", pstat->bssrateset, "%02x", pstat->bssratelen);
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    aid: ", pstat->cmn_info.aid, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    tx_bytes: ", pstat->tx_bytes, "%llu");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    rx_bytes: ", pstat->rx_bytes, "%llu");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    tx_bytes_cnt: ", pstat->tx_byte_cnt, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    rx_bytes_cnt: ", pstat->rx_byte_cnt, "%u");
	CHECK_LEN_B;
		
#ifdef CAM_SWAP
	PRINT_SINGL_ARG("	 delta_tx_bytes: ", pstat->traffic.delta_tx_bytes, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("	 delta_rx_bytes: ", pstat->traffic.delta_rx_bytes, "%u");
	CHECK_LEN_B;

	PRINT_SINGL_ARG("	 level: ", pstat->traffic.level, "%u");
	CHECK_LEN_B;

	PRINT_SINGL_ARG("    keyInCam: ", (pstat->dot11KeyMapping.keyInCam? "yes" : "no"), "%s");
	CHECK_LEN_B;
#endif

#ifdef RADIUS_ACCOUNTING
	PRINT_SINGL_ARG("    tx_bytes per minute: ", pstat->tx_bytes_1m, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    rx_bytes per minute: ", pstat->rx_bytes_1m, "%u");
	CHECK_LEN_B;
#endif
	PRINT_SINGL_ARG("    tx_pkts: ", pstat->tx_pkts, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    rx_pkts: ", pstat->rx_pkts, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    rx_mgnt_pkts: ", pstat->rx_mgnt_pkts, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    tx_fail: ", pstat->tx_fail, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    tx_pkts2: ", pstat->tx_pkts2, "%u");
	CHECK_LEN_B;
//#ifdef CONFIG_VERIWAVE_CHECK
	PRINT_SINGL_ARG("    tx_big_pkts: ", pstat->tx_big_pkts, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    tx_sml_pkts: ", pstat->tx_sml_pkts, "%u");
	CHECK_LEN_B;
//#endif
	PRINT_SINGL_ARG("    tx_mc2uc_pkts: ", pstat->mc2uc_pkts, "%u");
	CHECK_LEN_B;
#ifdef SUPPORT_TX_SWQ_AMSDU
	PRINT_SINGL_ARG("    sml_big_pkt_diff: ", pstat->sml_big_pkt_diff, "%d");
	CHECK_LEN_B;
#endif
	PRINT_SINGL_ARG("    rx_pkts2: ", pstat->rx_pkts2, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    tcpack_pkts: ", pstat->tcpack_pkts, "%u");
	CHECK_LEN_B;
#ifdef TXRETRY_CNT
	if(is_support_TxRetryCnt(priv)) {
	//PRINT_SINGL_ARG("    cur_tx_retry_pkts: ", pstat->cur_tx_retry_pkts, "%u");
	//CHECK_LEN_B;
	PRINT_SINGL_ARG("    cur_tx_retry_cnt: ", pstat->cur_tx_retry_cnt, "%u");
	CHECK_LEN_B;
	//PRINT_SINGL_ARG("    total_tx_retry_pkts: ", pstat->total_tx_retry_pkts, "%u");
	//CHECK_LEN_B;
	PRINT_SINGL_ARG("    total_tx_retry_cnt: ", pstat->total_tx_retry_cnt, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    tx_retry_ratio: ", pstat->txretry_ratio, "%u");
	CHECK_LEN_B;
	}
#endif
	PRINT_SINGL_ARG("    tx_avarage:    ", pstat->tx_avarage, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    rx_avarage:    ", pstat->rx_avarage, "%u");
	CHECK_LEN_B;
#ifdef SW_TX_QUEUE
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
		//for debug
#if 0
	PRINT_SINGL_ARG("    tx_av: ", pstat->tx_avarage, "%u");
        CHECK_LEN_B;
        PRINT_SINGL_ARG("    tx_agn: ", pstat->aggn_cnt, "%u");
        CHECK_LEN_B;
        PRINT_SINGL_ARG("    tx_be_tout: ", pstat->be_timeout_cnt, "%u");
        CHECK_LEN_B;
        PRINT_SINGL_ARG("    tx_bk_tout: ", pstat->bk_timeout_cnt, "%u");
        CHECK_LEN_B;
        PRINT_SINGL_ARG("    tx_vi_tout: ", pstat->vi_timeout_cnt, "%u");
        CHECK_LEN_B;
        PRINT_SINGL_ARG("    tx_vo_tout: ", pstat->vo_timeout_cnt, "%u");
        CHECK_LEN_B;
#endif
	}
#endif
	PRINT_ONE("    rssi: ", "%s", 0);
	PRINT_ONE(pstat->rssi, "%u", 0);
	PRINT_ONE(pstat->rf_info.mimorssi[0], " (%u", 0);
	PRINT_ONE(pstat->rf_info.mimorssi[1], " %u", 0);
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_WLAN_HAL_8814BE)  
	PRINT_ONE(pstat->rf_info.mimorssi[2], " %u", 0);
	PRINT_ONE(pstat->rf_info.mimorssi[3], " %u", 0);
#endif
	PRINT_ONE(")", "%s", 1);
	CHECK_LEN_B;

	PRINT_ONE("    sq: ", "%s", 0);
	PRINT_ONE(pstat->sq, "%u", 0);
	PRINT_ONE(pstat->rf_info.mimosq[0], " (%u", 0);
	PRINT_ONE(pstat->rf_info.mimosq[1], " %u", 0);
#ifdef CONFIG_WLAN_HAL_8814AE
	PRINT_ONE(pstat->rf_info.mimosq[2], " %u", 0);
	PRINT_ONE(pstat->rf_info.mimosq[3], " %u", 0);
#endif
	PRINT_ONE(")", "%s", 1);
	CHECK_LEN_B;
	
	PRINT_ONE("    snr: ", "%s", 0);	

	for (i = 0, count = 0; i < 4; i++) {	
		if (pstat->rf_info.RxSNRdB[i]) {
			sum += pstat->rf_info.RxSNRdB[i];
			count++;
		}	
	}

	sum = (count == 0) ? 0 : (sum / count);
	
	PRINT_ONE(sum, "%d", 0);
	PRINT_ONE(pstat->rf_info.RxSNRdB[0], " (%d", 0);
	PRINT_ONE(pstat->rf_info.RxSNRdB[1], " %d", 0);
#ifdef CONFIG_WLAN_HAL_8814AE
	PRINT_ONE(pstat->rf_info.RxSNRdB[2], " %d", 0);
	PRINT_ONE(pstat->rf_info.RxSNRdB[3], " %d", 0);
#endif
	PRINT_ONE(")", "%s", 1);
	CHECK_LEN_B;

#ifdef WDS
	if (pstat->state & WIFI_WDS
#ifdef LAZY_WDS
			&&  !(pstat->state & WIFI_WDS_LAZY)
#endif
		) {
		PRINT_SINGL_ARG("    idle_time: ", pstat->idle_time, "%d");
		CHECK_LEN_B;
	}
	else
#endif
	{
		PRINT_SINGL_ARG("    expired_time: ", pstat->expire_to, "%d");
		CHECK_LEN_B;
	}

#ifdef CONFIG_IEEE80211R
	PRINT_SINGL_ARG("    ft_auth_expire_to: ", pstat->ft_auth_expire_to, "%d");
	CHECK_LEN_B;
#endif
	PRINT_SINGL_ARG("    sleep: ", (!list_empty(&pstat->sleep_list) ? "yes" : "no"), "%s");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    sleep_cnt: ", pstat->cnt_sleep, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    sleep_drop:", pstat->sleep_drop, "%d");
	CHECK_LEN_B;
#ifdef CONFIG_PCI_HCI
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
		PRINT_SINGL_ARG("    dz_queue_len: ", skb_queue_len(&pstat->dz_queue), "%u");
		CHECK_LEN_B;
	}
#endif
	PRINT_SINGL_ARG("    sta_in_firmware: ",(pstat->cmn_info.ra_info.disable_ra? "no" : "yes"), "%s");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    remapped_aid: ", pstat->cmn_info.mac_id, "%d");
	CHECK_LEN_B;
#if defined(CONFIG_WLAN_HAL)
	if (IS_HAL_CHIP(priv)) {
		// below is add by SD1 Eric 2013/5/6
		// For 92E,8881 MACID Sleep and MACID drop status in driver and SRAM status

		PRINT_SINGL_ARG("    bSleep: ", pstat->txpause_flag, "%02x");
		PRINT_SINGL_ARG("    bDrop:  ", pstat->txpdrop_flag, "%02x");

#ifdef CONFIG_PCI_HCI
		if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
			u1Byte bDrop = 0; // txrpt need allocate 16byte
			if (RT_STATUS_FAILURE == GET_HAL_INTERFACE(priv)->GetTxRPTHandler(priv, REMAP_AID(pstat), TXRPT_VAR_PKT_DROP, 0, &bDrop))
			{
				PRINT_SINGL_ARG("    FW bDrop: ", 0xff,"%02x");
			}
			else
			{
				PRINT_SINGL_ARG("    FW bDrop: ", bDrop,"%02x");
			}
		}
#endif
	}
#endif //defined(CONFIG_WLAN_HAL)
#ifdef DETECT_STA_EXISTANCE
	PRINT_SINGL_ARG("    leave:    ", pstat->leave, "%u");
#endif

#if (MU_BEAMFORMING_SUPPORT == 1)
	PRINT_SINGL_ARG("    force_rate: ", pstat->force_rate, "%d");
	CHECK_LEN_B;

	PRINT_SINGL_ARG("    muPartner_num: ", pstat->muPartner_num, "%d");
	CHECK_LEN_B;
	if(pstat->muPartner_num) {
		for (i = 0; i < pstat->muPartner_num; i++)
			PRINT_SINGL_ARG("    mu Partner aid: ", pstat->muPartner[i]->cmn_info.aid, "%d");
	}
	CHECK_LEN_B;
	
	PRINT_SINGL_ARG("    mu_tx_rate: ", pstat->mu_tx_rate, "%d");
	CHECK_LEN_B;
	
	PRINT_SINGL_ARG("    mu_deq_num: ", pstat->mu_deq_num, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    isSendNDPA: ", pstat->isSendNDPA, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    isRssiApplyMU: ", pstat->isRssiApplyMU, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    isMUCandidate: ", pstat->isMUCandidate, "%d");
	CHECK_LEN_B;
#if defined(CONFIG_WLAN_HAL_8812FE)
	PRINT_SINGL_ARG("    descLDPCCnt: ", pstat->descLDPCCnt, "%d");
	CHECK_LEN_B;
#endif
	
	PRINT_SINGL_ARG("    inTXBFEntry: ", pstat->inTXBFEntry, "%d");
	CHECK_LEN_B;
    PRINT_SINGL_ARG("    muFlagForAMSDU: ", pstat->muFlagForAMSDU, "%d");
    CHECK_LEN_B;
#endif	

	current_rate = pstat->current_tx_rate;
#ifdef RTK_AC_SUPPORT  //vht rate , todo, dump vht rates in Mbps
	if(current_rate >= VHT_RATE_ID){
		int rate = query_vht_rate(pstat);
		PRINT_ONE("    current_tx_rate: VHT NSS", "%s", 0);
		PRINT_ONE(((current_rate - VHT_RATE_ID)/10)+1, "%d", 0);
		PRINT_ONE("-MCS", "%s", 0);
		PRINT_ONE((current_rate - VHT_RATE_ID)%10, "%d", 0);
		PRINT_ONE((pstat->ht_current_tx_info & TX_USE_SHORT_GI)?"s":"", "%s", 0);
		PRINT_ONE(rate, "  %d", 1);
		CHECK_LEN_B;
	}
	else
#endif
	if (is_MCS_rate(current_rate)) {
		PRINT_ONE("    current_tx_rate: MCS", "%s", 0);
		PRINT_ONE((current_rate - HT_RATE_ID), "%d", 0);
		rate = (unsigned char *)MCS_DATA_RATEStr[(pstat->ht_current_tx_info&BIT(0))?1:0][(pstat->ht_current_tx_info&BIT(1))?1:0][(current_rate - HT_RATE_ID)];
		PRINT_ONE(rate, " %s", 1);
		CHECK_LEN_B;
	}
	else
	{
		PRINT_SINGL_ARG("    current_tx_rate: ", current_rate/2, "%d");
		CHECK_LEN_B;
	}

	current_rate = pstat->rx_rate;
#ifdef RTK_AC_SUPPORT  //vht rate , todo, dump vht rates in Mbps
	if(current_rate >= VHT_RATE_ID){
		int rate = query_vht_rx_rate(pstat);
		PRINT_ONE("    current_rx_rate: VHT NSS", "%s", 0);
		PRINT_ONE(((current_rate - VHT_RATE_ID)/10)+1, "%d", 0);
		PRINT_ONE("-MCS", "%s", 0);
		PRINT_ONE((current_rate - VHT_RATE_ID)%10, "%d", 0);
		PRINT_ONE(rate, "  %d", 1);
		CHECK_LEN_B;
	}
	else
#endif
	if (is_MCS_rate(current_rate)) {
		PRINT_ONE("    current_rx_rate: MCS", "%s", 0);
		PRINT_ONE((current_rate - HT_RATE_ID), "%d", 0);
		rate = (unsigned char *)MCS_DATA_RATEStr[pstat->rx_bw&0x01][pstat->rx_splcp&0x01][(current_rate - HT_RATE_ID)];
		PRINT_ONE(rate, " %s", 1);
		CHECK_LEN_B;
	}
	else
	{
		PRINT_SINGL_ARG("    current_rx_rate: ", current_rate/2, "%d");
		CHECK_LEN_B;
	}
#ifdef RTK_CURRENT_RATE_ACCOUNTING
	PRINT_SINGL_ARG("    tx_bytes_1s: ", pstat->tx_bytes_1s, "%d");
	PRINT_SINGL_ARG("    rx_bytes_1s: ", pstat->rx_bytes_1s, "%d");
#endif

	if (pstat->cmn_info.ra_info.curr_tx_bw < CHANNEL_WIDTH_160) {
	PRINT_SINGL_ARG("    current_tx_BW: ", (0x1<<(pstat->cmn_info.ra_info.curr_tx_bw))*20, "%dM");
	}
	else if (pstat->cmn_info.ra_info.curr_tx_bw == CHANNEL_WIDTH_160) {
		PRINT_ONE("   current_tx_BW: 160M", "%s", 1);
	}
	else if (pstat->cmn_info.ra_info.curr_tx_bw == CHANNEL_WIDTH_80_80) {
		PRINT_ONE("    current_tx_BW: 80+80M", "%s", 1);
	}
	else {
		PRINT_ONE("    current_tx_BW: unknown", "%s", 1);
	}
	CHECK_LEN_B;
	
	if (pstat->rx_bw < CHANNEL_WIDTH_160) {
	PRINT_SINGL_ARG("    rx_bw: ", (0x1<<(pstat->rx_bw))*20, "%dM");
	}
	else if (pstat->rx_bw == CHANNEL_WIDTH_160) {
		PRINT_ONE("    rx_bw: 160M", "%s", 1);
	}
	else if (pstat->rx_bw == CHANNEL_WIDTH_80_80) {
		PRINT_ONE("    rx_bw: 80+80M", "%s", 1);
	}
	else {
		PRINT_ONE("    rx_bw: unknown", "%s", 1);
	}
	CHECK_LEN_B;

	if (pstat->tx_bw < CHANNEL_WIDTH_160) {
	PRINT_SINGL_ARG("    tx_bw: ", (pstat->tx_bw ? (pstat->tx_bw*40) : 20), "%dM");
	}
	else if (pstat->tx_bw == CHANNEL_WIDTH_160) {
		PRINT_ONE("    tx_bw: 160M", "%s", 1);
	}
	else if (pstat->tx_bw == CHANNEL_WIDTH_80_80) {
		PRINT_ONE("    tx_bw: 80+80M", "%s", 1);
	}
	else {
		PRINT_ONE("    tx_bw: unknown", "%s", 1);
	}
	CHECK_LEN_B;

	if (pstat->tx_bw_bak < CHANNEL_WIDTH_160) {
	PRINT_SINGL_ARG("    tx_bw_bak: ", (pstat->tx_bw_bak ? (pstat->tx_bw_bak*40) : 20), "%dM");
	}
	else if (pstat->tx_bw_bak == CHANNEL_WIDTH_160) {
		PRINT_ONE("    tx_bw_bak: 160M", "%s", 1);
	}
	else if (pstat->tx_bw_bak == CHANNEL_WIDTH_80_80) {
		PRINT_ONE("    tx_bw_bak: 80+80M", "%s", 1);
	}
	else {
		PRINT_ONE("    tx_bw_bak: unknown", "%s", 1);
	}
	CHECK_LEN_B;

#if defined(RTK_AC_SUPPORT) || defined(RTK_AC_TX_SUPPORT)
	PRINT_SINGL_ARG("    nss: ", (pstat->nss), "%d");	
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    support_mcs: ", cpu_to_le32(pstat->vht_cap_buf.vht_support_mcs[0]), "%x");	
	CHECK_LEN_B;
	if(GET_CHIP_VER(priv) == VERSION_8812E) {
	    PRINT_SINGL_ARG("    shrink_ac_bw: ", pstat->shrink_ac_bw, "%d");
		CHECK_LEN_B;
	}
#endif

	PRINT_SINGL_ARG("    hp_level: ", pstat->hp_level, "%d");
	CHECK_LEN_B;
	
	if(pstat->useCts2self) {
		PRINT_SINGL_ARG("    rts/cts2self: ", "cts2self", "%s");
	} else {
		PRINT_SINGL_ARG("    rts/cts2self: ", "rts", "%s");
	}
	
	CHECK_LEN_B;

#ifdef WIFI_WMM
	PRINT_SINGL_ARG("    QoS Enable: ", pstat->QosEnabled, "%d");
	CHECK_LEN_B;
#ifdef WMM_APSD
	PRINT_SINGL_ARG("    APSD bitmap: ", pstat->apsd_bitmap, "0x%01x");
	CHECK_LEN_B;
#endif
#endif

	PRINT_SINGL_ARG("    tx_ra_bitmap: ", pstat->tx_ra_bitmap, "0x%08x");

	if (pstat->is_realtek_sta) {
		if (pstat->IOTPeer==HT_IOT_PEER_RTK_APCLIENT)
			snprintf((char *)tmpbuf, sizeof(tmpbuf), "Realtek/APCLIENT");
		else if (pstat->IOTPeer==HT_IOT_PEER_REALTEK_92SE)
			snprintf((char *)tmpbuf, sizeof(tmpbuf), "Realtek/8192SE");
		else if (pstat->IOTPeer==HT_IOT_PEER_REALTEK_81XX)
			snprintf((char *)tmpbuf, sizeof(tmpbuf), "Realtek/88C92C");
		else if (pstat->IOTPeer==HT_IOT_PEER_REALTEK_8814)
			snprintf((char *)tmpbuf, sizeof(tmpbuf), "Realtek/8814");
		else
			snprintf((char *)tmpbuf, sizeof(tmpbuf), "Realtek/%d", pstat->IOTPeer);
	}
	else if (pstat->IOTPeer==HT_IOT_PEER_BROADCOM)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "Broadcom");
	else if (pstat->IOTPeer==HT_IOT_PEER_MARVELL)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "Marvell");
	else if (pstat->IOTPeer==HT_IOT_PEER_INTEL)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "Intel");
	else if (pstat->IOTPeer==HT_IOT_PEER_RALINK)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "Ralink");
	else if (pstat->IOTPeer==HT_IOT_PEER_HTC)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "HTC");
	else if (pstat->IOTPeer==HT_IOT_PEER_APPLE)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "APPLE");
	else if (pstat->IOTPeer==HT_IOT_PEER_SPIRENT)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "SPIRENT");
	else if (pstat->IOTPeer==HT_IOT_PEER_VERIWAVE)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "VERIWAVE");
	else if (pstat->IOTPeer==HT_IOT_PEER_NETGEAR)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "NETGEAR");
	else if (pstat->IOTPeer==HT_IOT_PEER_OCTOSCOPE)
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "OCTOSCOPE");
	else
		snprintf((char *)tmpbuf, sizeof(tmpbuf), "--");
	PRINT_SINGL_ARG("    Chip Vendor: ", tmpbuf, "%s");
	CHECK_LEN_B;
#ifdef RTK_WOW
	if (pstat->is_realtek_sta) {
		PRINT_SINGL_ARG("    is_rtk_wow_sta: ", (pstat->is_rtk_wow_sta? "yes":"no"), "%s");
		CHECK_LEN_B;
	}
#endif

	m = pstat->link_time / 86400;
	n = pstat->link_time % 86400;
	if (m)	idx += snprintf(tmp, sizeof(tmp), "%d day ", m);
	m = n / 3600;
	n = n % 3600;
	if (m)	idx += snprintf(tmp+idx, sizeof(tmp)-idx, "%d hr ", m);
	m = n / 60;
	n = n % 60;
	if (m)	idx += snprintf(tmp+idx, sizeof(tmp)-idx, "%d min ", m);
	idx += snprintf(tmp+idx, sizeof(tmp)-idx, "%d sec ", n);
	PRINT_SINGL_ARG("    link_time: ", tmp, "%s");
	CHECK_LEN_B;

	if (pstat->private_ie_len) {
		PRINT_ARRAY_ARG("    private_ie: ", pstat->private_ie, "%02x", pstat->private_ie_len);
		CHECK_LEN_B;
	}
	if (pstat->ht_cap_len) {
		unsigned char *pbuf = (unsigned char *)&pstat->ht_cap_buf;
		PRINT_ARRAY_ARG("    ht_cap: ", pbuf, "%02x", pstat->ht_cap_len);
		CHECK_LEN_B;

		PRINT_ONE("    11n MIMO ps: ", "%s", 0);
		if (!(pstat->MIMO_ps)) {
			PRINT_ONE("no limit", "%s", 1);
		}
		else {
			PRINT_ONE(((pstat->MIMO_ps&BIT(0))?"static":"dynamic"), "%s", 1);
		}
		CHECK_LEN_B;

		PRINT_SINGL_ARG("    Is_8K_AMSDU: ", pstat->is_8k_amsdu, "%d");
		CHECK_LEN_B;
		PRINT_SINGL_ARG("    amsdu_level: ", pstat->amsdu_level, "%d");
		CHECK_LEN_B;
		PRINT_SINGL_ARG("    diffAmpduSz: 0x", pstat->diffAmpduSz, "%x");
		CHECK_LEN_B;
		idx = 0;
		if (pstat->AMSDU_AMPDU_support & STA_RX_AMSDU_SUPPORT)
			idx += snprintf(tmp+idx, sizeof(tmp)-idx, "RX_AMSDU ");
		if (pstat->AMSDU_AMPDU_support & STA_TX_AMSDU_SUPPORT)
			idx += snprintf(tmp+idx, sizeof(tmp)-idx, "TX_AMSDU ");
		if (idx)
			tmp[idx - 1] = '\0';
		else
			snprintf(tmp, sizeof(tmp), "NO_AMSDU\0");
		PRINT_SINGL_ARG("    AMSDU_AMPDU_support: ", tmp, "%s");
		CHECK_LEN_B;
		switch (pstat->aggre_mthd) {
		case AGGRE_MTHD_MPDU:
			snprintf(tmp, sizeof(tmp), "AMPDU");
			break;
		case AGGRE_MTHD_MSDU:
			snprintf(tmp, sizeof(tmp), "AMSDU");
			break;
		case AGGRE_MTHD_MPDU_AMSDU:
			snprintf(tmp, sizeof(tmp), "AMPDU_AMSDU");
			break;
		default:
			snprintf(tmp, sizeof(tmp), "None");
			break;
		}
		PRINT_SINGL_ARG("    aggre mthd: ", tmp, "%s");
		CHECK_LEN_B;

		PRINT_SINGL_ARG("    amsdu_defer_to: ", pstat->amsdu_defer_to, "%d");
		CHECK_LEN_B;

#ifdef SW_TX_QUEUE
		if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
			for(idx=0; idx<8; idx++){
				if(pstat->swq.swq_en[idx])
					swq_en |= BIT(idx);
			}
			PRINT_SINGL_ARG("    swq_en: ", swq_en, "0x%x");
		}
#endif

#if (BEAMFORMING_SUPPORT == 1)
		PRINT_SINGL_ARG("    ht beamformee: ", (cpu_to_le32(pstat->ht_cap_buf.txbf_cap) & 0x8)?"Y":"N", "%s");
		CHECK_LEN_B;
		PRINT_SINGL_ARG("    ht beamformer: ", (cpu_to_le32(pstat->ht_cap_buf.txbf_cap) & 0x10)?"Y":"N", "%s");
		CHECK_LEN_B;
#endif

#ifdef _DEBUG_RTL8192CD_
		PRINT_ONE("    ch_width: ", "%s", 0);
		PRINT_ONE((pstat->ht_cap_buf.ht_cap_info & cpu_to_le16(_HTCAP_SUPPORT_CH_WDTH_))?"40M":"20M", "%s", 1);
		CHECK_LEN_B;
#endif
		PRINT_ONE("    ampdu_mf: ", "%s", 0);
		PRINT_ONE(pstat->ht_cap_buf.ampdu_para & 0x03, "%d", 1);
		CHECK_LEN_B;
		PRINT_ONE("    ampdu_amd: ", "%s", 0);
		PRINT_ONE((pstat->ht_cap_buf.ampdu_para & _HTCAP_AMPDU_SPC_MASK_) >> _HTCAP_AMPDU_SPC_SHIFT_, "%d", 1);
		CHECK_LEN_B;
	}
	else {
		PRINT_ONE("    ht_cap: none", "%s", 1);
		CHECK_LEN_B;
	}


//AC capability
#ifdef RTK_AC_SUPPORT
	if (pstat->vht_cap_len) {
		unsigned char *pbuf = (unsigned char *)&pstat->vht_cap_buf;
		PRINT_ARRAY_ARG("    vht_cap_ie: ", pbuf, "%02x", pstat->vht_cap_len);
		CHECK_LEN_B;

#if (BEAMFORMING_SUPPORT == 1)
		PRINT_SINGL_ARG("    vht beamformee: ", (cpu_to_le32(pstat->vht_cap_buf.vht_cap_info) & BIT(SU_BFEE_S))?"Y":"N", "%s");
		CHECK_LEN_B;
		PRINT_SINGL_ARG("    vht beamformer: ", (cpu_to_le32(pstat->vht_cap_buf.vht_cap_info) & BIT(SU_BFER_S))?"Y":"N", "%s");
		CHECK_LEN_B;
#endif
		
	}
	else {
		PRINT_ONE("    vht_cap_ie: none", "%s", 1);
		CHECK_LEN_B;
	}

	if (pstat->vht_oper_len) {
		unsigned char *pbuf = (unsigned char *)&pstat->vht_oper_buf;
		PRINT_ARRAY_ARG("    vht_oper_ie: ", pbuf, "%02x", pstat->vht_oper_len);
		CHECK_LEN_B;
	}
	else {
		PRINT_ONE("    vht_oper_ie: none", "%s", 1);
		CHECK_LEN_B;
	}

	PRINT_SINGL_ARG("    maxAggNum: ", pstat->maxAggNum, "%d");
	CHECK_LEN_B;
	
#endif

#if (BEAMFORMING_SUPPORT == 1)
	if (priv->pmib->dot11RFEntry.txbf == 1)	{
		u1Byte					Idx = 0;
		PRT_BEAMFORMING_ENTRY	pEntry; 
		pEntry = Beamforming_GetEntryByMacId(priv, pstat->cmn_info.aid, &Idx);
		PRINT_SINGL_ARG("    Activate Tx beamforming : ", (pEntry ? "Y" : "N"),"%s");
	}
#endif

#ifdef SUPPORT_TX_MCAST2UNI
	PRINT_SINGL_ARG("    ipmc_num: ", pstat->ipmc_num, "%d");
	CHECK_LEN_B;
	for (idx=0; idx < pstat->ipmc_num; idx++) {
		PRINT_ARRAY_ARG("    mcmac: ", pstat->ipmc[idx].mcmac, "%02x", MACADDRLEN);
		CHECK_LEN_B;
	}
#endif

#ifdef CLIENT_MODE
	if (pstat->ht_ie_len) {
		unsigned char *pbuf = (unsigned char *)&pstat->ht_ie_buf;
		PRINT_ARRAY_ARG("    ht_ie: ", pbuf, "%02x", pstat->ht_ie_len);
		CHECK_LEN_B;
	}
	else {
		PRINT_ONE("    ht_ie: none", "%s", 1);
		CHECK_LEN_B;
	}

#ifdef HS2_SUPPORT
/* Hotspot 2.0 Release 1 */
	unsigned char hs2_buf[48];
	PRINT_ARRAY_ARG("    sta ip:", pstat->sta_ip, "%d.", 4);
	CHECK_LEN_B;
    memset(hs2_buf,'\0', 48);
    for(idx=0;idx<pstat->v6ipCount;idx++){
        snprintf(hs2_buf, sizeof(hs2_buf), "[%08X][%08X][%08X][%08X]",pstat->sta_v6ip[idx].s6_addr32[0],pstat->sta_v6ip[idx].s6_addr32[1],pstat->sta_v6ip[idx].s6_addr32[2],pstat->sta_v6ip[idx].s6_addr32[3]);
	PRINT_SINGL_ARG("    ipv6: ", hs2_buf, "%s");
    }
    CHECK_LEN_B;    
    memset(hs2_buf,'\0', 48);
#endif

#endif

#ifdef MULTI_MAC_CLONE
	if (OPMODE & WIFI_STATION_STATE) {
		PRINT_ARRAY_ARG("    sa_addr: ", pstat->sa_addr, "%02x", MACADDRLEN);
		CHECK_LEN_B;
		PRINT_SINGL_ARG("    mclone_id: ", pstat->mclone_id, "%d");	
		CHECK_LEN_B;
	}
#endif

	if ((pstat->wireless_mode & WIRELESS_MODE_AC_5G) || (pstat->wireless_mode & WIRELESS_MODE_AC_24G))
		snprintf(tmpbuf, sizeof(tmpbuf), "AC");
	else if (pstat->wireless_mode & WIRELESS_MODE_N_5G)
		snprintf(tmpbuf, sizeof(tmpbuf), "AN");
	else if (pstat->wireless_mode & WIRELESS_MODE_N_24G)
		snprintf(tmpbuf, sizeof(tmpbuf), "BGN");
	else if (pstat->wireless_mode & WIRELESS_MODE_G)
		snprintf(tmpbuf, sizeof(tmpbuf), "BG");
	else if (pstat->wireless_mode & WIRELESS_MODE_A)
		snprintf(tmpbuf, sizeof(tmpbuf), "A");
	else if (pstat->wireless_mode & WIRELESS_MODE_B)
		snprintf(tmpbuf, sizeof(tmpbuf), "B");
	else
		snprintf(tmpbuf, sizeof(tmpbuf), "Unknown wireless_mode:%x\n",pstat->wireless_mode);
	PRINT_SINGL_ARG("    wireless mode: ", tmpbuf, "%s");
	CHECK_LEN_B;

#ifdef A4_STA
	if (priv->pmib->miscEntry.a4_enable) {
		PRINT_SINGL_ARG("    A4 STA: ", (pstat->state & WIFI_A4_STA)?"Y":"N", "%s");
		CHECK_LEN_B;
	}
#endif
	
#ifdef TV_MODE
    if (OPMODE & WIFI_AP_STATE && priv->tv_mode_status & BIT1) /*if tv_mode is auto*/
    {
        PRINT_SINGL_ARG("    TV AUTO: ", pstat->tv_auto_support?"Y":"N", "%s");
        CHECK_LEN_B;
    }
#endif

#ifdef STA_CONTROL
    if (priv->stactrl.stactrl_status) {
		if (priv->stactrl.stactrl_prefer == 0)
	        PRINT_SINGL_ARG("    stactrl_candidate: ", pstat->stactrl_candidate,"%d");
#ifdef STA_CONTROL_BAND_TRANSITION				
		PRINT_SINGL_ARG("    stactrl_dual_band: ", stactrl_chk_dualband(priv, pstat->cmn_info.mac_addr),"%d");		
#endif		
    }	
#endif

#ifdef CONFIG_VERIWAVE_MU_CHECK	
	PRINT_SINGL_ARG("    isVeriwaveInValidSTA: ", pstat->isVeriwaveInValidSTA,"%d");
	PRINT_ARRAY_ARG("    mu_csi_data:", pstat->mu_csi_data, "%02x ", 3);
#endif

#ifdef DOT11K
    if(priv->pmib->dot11StationConfigEntry.dot11RadioMeasurementActivated) {
        PRINT_ARRAY_ARG("    rm_cap: ", pstat->rm.rm_cap, "%02x", 5);
    }
	if(pstat->rm.rm_cap[0] & BIT(5)){
		PRINT_SINGL_ARG("    Beacon_report: ", 1, "%d");
	}
	else{
		PRINT_SINGL_ARG("    Beacon_report: ", 0, "%d");
	}
	CHECK_LEN_B;
#endif
#if defined(HAPD_11K) && defined(AP_NEIGHBOR_INFO)
	PRINT_SINGL_ARG("    rm_nb_rpt_req_dialog_token: ", pstat->rm.rm_nb_rpt_req_dialog_token, "%u");
	PRINT_SINGL_ARG("    rm_nb_rpt_req_ssid_len: ", pstat->rm.rm_nb_rpt_req_ssid_len, "%u");
	PRINT_SINGL_ARG("    rm_nb_rpt_req_ssid: ", pstat->rm.rm_nb_rpt_req_ssid, "%s");
#endif
#ifdef CONFIG_IEEE80211V
    if(WNM_ENABLE) {
    	PRINT_SINGL_ARG("    BSS Trans Support: ", pstat->bssTransSupport, "%d");
		PRINT_SINGL_ARG("    BSS Trans Rsp Status Code: ", pstat->bssTransStatusCode, "%d");
		PRINT_SINGL_ARG("    BSS Trans Rejection Count: ", pstat->bssTransRejectionCount, "%d");
		PRINT_SINGL_ARG("    BSS Trans Expired  Time: ", pstat->bssTransExpiredTime, "%d");
		PRINT_SINGL_ARG("    BSS Trans Event Triggered: ", pstat->bssTransTriggered, "%d");
		PRINT_SINGL_ARG("    BSS Trans 11v Dual Band: ", pstat->dual_band_capable, "%d");
		PRINT_SINGL_ARG("    Rev Neighbor Report: ", pstat->rcvNeighborReport, "%d");
	}
	PRINT_SINGL_ARG("    BSS_transition: ", pstat->bssTransSupport, "%d");
	CHECK_LEN_B;
#endif

    PRINT_SINGL_ARG("    prepare_to_free: ", pstat->prepare_to_free, "%d");   

#ifdef SBWC
	if ( (OPMODE & WIFI_AP_STATE) && (pstat->SBWC_mode != SBWC_MODE_DISABLE) )
	{
		PRINT_SINGL_ARG("    tx_limit: ", pstat->SBWC_tx_limit, "%d");
		CHECK_LEN_B;
		PRINT_SINGL_ARG("    rx_limit: ", pstat->SBWC_rx_limit, "%d");
		CHECK_LEN_B;
		PRINT_SINGL_ARG("    tx_count: ", pstat->SBWC_tx_count, "%d");
		CHECK_LEN_B;
		PRINT_SINGL_ARG("    rx_count: ", pstat->SBWC_rx_count, "%d");
		CHECK_LEN_B;
	}
#endif
#ifdef CONFIG_RECORD_CLIENT_HOST
	char client_ip[24];
	if(priv->pmib->miscEntry.client_host_sniffer_enable){		
		if(pstat->client_host_ip[0]){
			PRINT_SINGL_ARG("    Client host name: ", pstat->client_host_name, "%s");
			snprintf(client_ip,24,"%d:%d:%d:%d",pstat->client_host_ip[0],pstat->client_host_ip[1],pstat->client_host_ip[2],pstat->client_host_ip[3]);		
			PRINT_SINGL_ARG("    Client host ip: ", client_ip, "%s");
		}		
	}
#endif

#if defined(WIFI_QOS_ENHANCE)
	PRINT_SINGL_ARG("    is_qosenhance_sta: ", pstat->is_qosenhance_sta, "%d");
	PRINT_SINGL_ARG("    is_qosenhance_farsta: ", pstat->is_qosenhance_farsta, "%d");
	PRINT_SINGL_ARG("    qosenhance_old_tid: ", pstat->qosenhance_old_tid, "%d");
	PRINT_SINGL_ARG("    qosenhance_new_tid: ", pstat->qosenhance_new_tid, "%d");
#endif
	
#ifdef TCP_ACK_ACC
	if (priv->pmib->miscEntry.tcpack_acc) {
		unsigned int i, cnt = 0;
		for (i=0; i<TCP_SESSION_MAX_ENTRY; i++) {
			if (pstat->tcp_ses[i].used)
				cnt++;
		}
		PRINT_SINGL_ARG("    tcpack_acc_cnt: ", cnt, "%d");
		CHECK_LEN_B;		
		PRINT_SINGL_ARG("    tcpack_acc_en: ", pstat->tcpack_acc_en, "%u");
		CHECK_LEN_B;
	}
#endif
	if(pstat->veriwave_5G_TCP_Rx_test) {
		PRINT_ONE("    veriwave test: 5G Throughput TCP Rx", "%s", 1);
		CHECK_LEN_B;
	}

#ifdef TX2_BK_QUEUE
	PRINT_SINGL_ARG("    tx2bk_on: ", pstat->tx2bk_on, "%u");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    tx2bk_1s_bytes: ", pstat->tx_bytes_1s, "%u");
	CHECK_LEN_B;
#endif

#ifdef CONFIG_VERIWAVE_CHECK
    if(pstat->veriwave_2G_RvR_test) {
        if(pstat->current_tx_pkt_size == VERIWAVE_PKT_SIZE_BIG) {
            PRINT_ONE("    veriwave test: 2G RvR big pkts", "%s", 1);
        }
        else if(pstat->current_tx_pkt_size == VERIWAVE_PKT_SIZE_SML) {
            PRINT_ONE("    veriwave test: 2G RvR small pkts", "%s", 1);
        }
        CHECK_LEN_B;
    }
#endif


#ifdef CONFIG_SPECIAL_ENV_TEST
	if(priv->pshare->in_spec_env_test_to){
		if(pstat->current_tx_pkt_size == VERIWAVE_PKT_SIZE_BIG) {
			PRINT_ONE("    spec_env UDP Tx test: big pkts", "%s", 1);
		}
		else if(pstat->current_tx_pkt_size == VERIWAVE_PKT_SIZE_SML) {
			PRINT_ONE("    spec_env UDP Tx test: small pkts", "%s", 1);
		}
		CHECK_LEN_B;
	}
#endif    

#ifdef SUPPORT_TX_AMSDU
#ifdef SUPPORT_TX_AMSDU_SMALL_PKTS_ONLY
	if (priv->pmib->dot11nConfigEntry.dot11nAMSDUSmallPkts) {
		PRINT_SINGL_ARG("    amsdu_tcpack_en: ", pstat->amsdu_tcpack_en, "%u");
		CHECK_LEN_B;
		if(priv->pmib->dot11nConfigEntry.dot11nAMSDUTCPAckDropDup) {
			PRINT_SINGL_ARG("    amsdu_tcpack_ses_cnt: ", pstat->amsdu_tcpack_ses_cnt, "%u");
			CHECK_LEN_B;		
			PRINT_SINGL_ARG("    amsdu_dropdupack_en: ", pstat->amsdu_dropdupack_en, "%u");
			CHECK_LEN_B;
		}
	}
#endif
#endif

#ifdef RA_OFFSET_TRAINING
	PRINT_SINGL_ARG("    rate_var: ", pstat->ra_offset.rate_var, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    alive: ", pstat->ra_offset.alive, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    rty_ratio: ", pstat->ra_offset.rty_ratio, "%d");
	CHECK_LEN_B;
#endif
#if defined(STA_CONTROL) && (STA_CONTROL_ALGO == STA_CONTROL_ALGO3)
	PRINT_SINGL_ARG("    stactrl_trigger_time: ", pstat->stactrl_trigger_time, "%d");
	CHECK_LEN_B;
#endif
#ifdef STA_CONTROL_BAND_TRANSITION
	PRINT_SINGL_ARG("    stactrl_steer_detect_counter: ", pstat->stactrl_steer_detect_counter, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    stactrl_load_balance_score:   ", pstat->stactrl_load_balance_score, "%d");
	CHECK_LEN_B;
	PRINT_SINGL_ARG("    stactrl_load_balance_score_sc:", pstat->stactrl_load_balance_score_sc, "%d");
	CHECK_LEN_B;
#endif
#ifdef RTK_MULTI_AP
	PRINT_SINGL_ARG("    multiap_profile: ", pstat->multiap_profile, "%d");
	CHECK_LEN_B;
#endif
        PRINT_SINGL_ARG("    rtsdrop_expire_to: ", pstat->rtsdrop_expire_to, "%d");
	CHECK_LEN_B;
	PRINT_ONE("", "%s", 1);

	*rc = 1; //finished, assign okay to return code.
	return pos;
}

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
//static int read_sta_info_down;
#endif
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_stainfo(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_stainfo(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int len = 0, rc=1;
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    off_t begin = 0;
    off_t pos = 0;
#endif
    int size, num=1;
    struct list_head *phead, *plist;
    struct stat_info *pstat;

#if 1/*!defined(SMP_SYNC) || (defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI))        */
    unsigned long flags=0;
#endif

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    if (offset == 0) // first calling, reset read_sta_info_down variable
        read_sta_info_down = 0;

    // first, kernel call me with length=3072; second, kernel call me with length=1024,
    // third, , kernel call me with length < 1024, I just return 0 when length < 1024 to avoid wasting time.
    if (length < 1024) {
        return 0;
    }

    // do not waste time again
    // I sent *eof=1 last time, I do not know why the kernel will call me again.
    if (read_sta_info_down) {
        *eof = 1;
        return 0;
    }
#endif

    SAVE_INT_AND_CLI(flags);
#if defined(SMP_SYNC) && defined(CONFIG_PCI_HCI)
	spin_lock_irqsave(&priv->asoc_list_lock, flags);
#else
	SMP_LOCK_ASOC_LIST(flags);
#endif

    // !!! it REALLY waste the cpu time ==> it will do from beginning when kernel call rtl8192cd_proc_stainfo() function again and again.
#ifdef __ECOS
    ecos_pr_fun("-- STA info table -- (active: %d)\n", priv->assoc_num);
#elif defined(CONFIG_RTL_PROC_NEW)
    seq_printf(s, "-- STA info table -- (active: %d)\n", priv->assoc_num);
#else
    size = snprintf(buf, length, "-- STA info table -- (active: %d)\n", priv->assoc_num);
    CHECK_LEN;
#endif

    phead = &priv->asoc_list;
    if (!(priv->drv_state & DRV_STATE_OPEN) || list_empty(phead)) {
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
        *eof = 1;
#endif
        goto _ret;
    }

    plist = phead->next;
    while (plist != phead) {
        pstat = list_entry(plist, struct stat_info, asoc_list);
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
        //  (length  - len) : the remained length can be used.
        if ((length - len) < MAX_CHAR_IN_A_LINE)
            goto _ret;

        rc = 0; // return code, assume error by default
#endif

#if defined(CONFIG_RTL_TRIBAND_001) && defined(CONFIG_TRIBAND_MESH)
        /* don't show mesh point in sta_info */
        if (!list_empty(&pstat->mesh_mp_ptr)) {
            plist = plist->next;
            continue;
        }
#endif
        //size = dump_one_stainfo(num++, pstat, buf+len, start, offset, length, eof, data);
#ifdef CONFIG_RTL_PROC_NEW
        size = dump_one_stainfo(s, num++, priv, pstat, &rc);
#else		
        size = dump_one_stainfo(num++, priv, pstat, buf+len, offset, length, (length - len), &rc);
#endif
#if defined(__ECOS)
        if(num > NUM_STAT)
        {
            diag_printf("the assoc sta num is %d, but the list of pstat number over NUM_STAT\n", priv->assoc_num);
            goto _ret;
        }
#endif
        CHECK_LEN;

#ifdef CONFIG_RTK_MESH
        if (rc == 0)
            break;

        // 3 line for Throughput statistics (sounder)
        #ifdef CONFIG_RTL_PROC_NEW
        size = dump_mesh_one_mpflow_sta(s, pstat); 
        #else
        size = dump_mesh_one_mpflow_sta(pstat, buf+len, start, offset, length,
            eof, data);        
        #endif  

        CHECK_LEN;
#endif

        plist = plist->next;
    }

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
	if (rc == 1) { // return code is okay. if return code is 0, it means the dump_one_stainfo() is not finished.
		read_sta_info_down = 1;
		*eof = 1;
	}
#endif

_ret:
#if defined(SMP_SYNC) && defined(CONFIG_PCI_HCI)
    spin_unlock_irqrestore(&priv->asoc_list_lock, flags);
#else
    SMP_UNLOCK_ASOC_LIST(flags);
#endif
    RESTORE_INT(flags);

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    *start = buf + (offset - begin);	/* Start of wanted data */
    len -= (offset - begin);	/* Start slop */
    if (len > length)
        len = length;	/* Ending slop */
#endif

    return len;
}


#ifdef CONFIG_RTL_KERNEL_MIPS16_WLAN
__NOMIPS16
#endif
#ifdef CONFIG_RTL_PROC_NEW
static int dump_one_sta_keyinfo(struct seq_file *s, int num, struct stat_info *pstat)
#else
static int dump_one_sta_keyinfo(int num, struct stat_info *pstat, char *buf, char **start,
			off_t offset, int length, int *eof, void *data)
#endif			
{
	int pos = 0;
	unsigned char *ptr;

	PRINT_ONE(num,  " %d: stat_keyinfo...", 1);
	PRINT_ARRAY_ARG("    hwaddr: ",	pstat->cmn_info.mac_addr, "%02x", MACADDRLEN);
	PRINT_SINGL_ARG("    keyInCam: ", (pstat->dot11KeyMapping.keyInCam? "yes" : "no"), "%s");
	PRINT_SINGL_ARG("    dot11Privacy: ",
			pstat->dot11KeyMapping.dot11Privacy, "%d");
	PRINT_SINGL_ARG("    dot11EncryptKey.dot11TTKeyLen: ",
			pstat->dot11KeyMapping.dot11EncryptKey.dot11TTKeyLen, "%d");
	PRINT_SINGL_ARG("    dot11EncryptKey.dot11TMicKeyLen: ",
			pstat->dot11KeyMapping.dot11EncryptKey.dot11TMicKeyLen, "%d");
	PRINT_ARRAY_ARG("    dot11EncryptKey.dot11TTKey.skey: ",
			pstat->dot11KeyMapping.dot11EncryptKey.dot11TTKey.skey, "%02x", 16);
	PRINT_ARRAY_ARG("    dot11EncryptKey.dot11TMicKey1.skey: ",
			pstat->dot11KeyMapping.dot11EncryptKey.dot11TMicKey1.skey, "%02x", 16);
	PRINT_ARRAY_ARG("    dot11EncryptKey.dot11TMicKey2.skey: ",
			pstat->dot11KeyMapping.dot11EncryptKey.dot11TMicKey2.skey, "%02x", 16);
	ptr = (unsigned char *)&pstat->dot11KeyMapping.dot11EncryptKey.dot11TXPN48.val48;
	PRINT_ARRAY_ARG("    dot11EncryptKey.dot11TXPN48.val48: ", ptr, "%02x", 8);
	ptr = (unsigned char *)&pstat->dot11KeyMapping.dot11EncryptKey.dot11RXPN48.val48;
	PRINT_ARRAY_ARG("    dot11EncryptKey.dot11RXPN48.val48: ", ptr, "%02x", 8);

	PRINT_ONE("", "%s", 1);

	return pos;
}

#if defined(CONFIG_RTL_MONITOR_STA_INFO)
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_monitor_stainfo(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_monitor_stainfo(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;
	unsigned long sec;
#ifdef __ECOS
	char tmpbuf[100];
#else
	unsigned char tmpbuf[100];
#endif
	if(priv->pmib->dot11StationConfigEntry.monitor_sta_enabled)
	{
		PRINT_ONE("  Monitor sta info...", "%s", 1);
		for(i=0; i<NUM_MONITOR; i++)
		{
			if(priv->pshare->monitor_sta_info.monitor_sta_ent[i].valid == 1)
			{
				memset(tmpbuf,0,sizeof(tmpbuf));
				sprintf(tmpbuf, "	MacAddr[%d]: ", i);
				PRINT_ARRAY_ARG(tmpbuf, priv->pshare->monitor_sta_info.monitor_sta_ent[i].mac, "%02x", 6);
				PRINT_SINGL_ARG("	Rssi: ", priv->pshare->monitor_sta_info.monitor_sta_ent[i].rssi-100, "%d dBm");
				PRINT_SINGL_ARG("	IsRouter: ", priv->pshare->monitor_sta_info.monitor_sta_ent[i].isAP==1?"Y":"N", "%s");
				if(jiffies/HZ >= priv->pshare->monitor_sta_info.monitor_sta_ent[i].sec)
					sec = jiffies/HZ - priv->pshare->monitor_sta_info.monitor_sta_ent[i].sec;
				else
					sec = jiffies/HZ + ~(unsigned long)0/HZ - priv->pshare->monitor_sta_info.monitor_sta_ent[i].sec;
				PRINT_SINGL_ARG("	Last seen: ", sec, "%u seconds ago");
				PRINT_ONE("", "%s", 1);
			}
			
		}
	}
	else
	{
		PRINT_ONE("  Monitor sta disabled", "%s", 1);
	}
	return pos;
}
#endif

#if defined(AP_NEIGHBOR_INFO)
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ap_neighbor_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ap_neighbor_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;
	unsigned long sec;
	int idx = 0;
#ifdef __ECOS
	char tmpbuf[100];
#else
	unsigned char tmpbuf[100];
#endif
	if(priv->pmib->miscEntry.ap_neighbor_info_enable)
	{
		PRINT_ONE("  AP Neighbor Info...", "%s", 1);
		for(i=0; i<MAX_AP_NEIGHBOR_INFO_NUM; i++)
		{
			if(priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].valid == 1)
			{		
				PRINT_SINGL_ARG("entry: ", i, "%d");
				PRINT_SINGL_ARG("	band: ", priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].band==1?"2G":"5G", "%s");	
				PRINT_SINGL_ARG("	ssid: ", priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].ssid, "%s");				
				PRINT_ARRAY_ARG("	mac: ",  priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].mac, "%02x", 6);
				PRINT_SINGL_ARG("	channel: ", priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].channel, "%d");
				PRINT_SINGL_ARG("	rssi: ", priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].rssi-100, "%d dBm");				
				PRINT_SINGL_ARG("	networktype: ", priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].networktype==0?"AP":"ADHOC", "%s");

				memset(tmpbuf,0,sizeof(tmpbuf));
				idx = 0;
				if (priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].standard & WIRELESS_11A)
					tmpbuf[idx++] = 'A';
				if (priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].standard & WIRELESS_11B)
					tmpbuf[idx++] = 'B';
				if (priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].standard & WIRELESS_11G)
					tmpbuf[idx++] = 'G';
				if (priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].standard & WIRELESS_11N)
					tmpbuf[idx++] = 'N';
#ifdef RTK_AC_SUPPORT 		
				if (priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].standard & WIRELESS_11AC) {
					tmpbuf[idx++] = 'A';
					tmpbuf[idx++] = 'C';
				}
#endif					
				tmpbuf[idx] = '\0';

				PRINT_SINGL_ARG("	standard: ", tmpbuf, "%s");					

				if(jiffies/HZ >= priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].sec)
					sec = jiffies/HZ - priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].sec;
				else
					sec = jiffies/HZ + ~(unsigned long)0/HZ - priv->pshare->ap_neighbor.ap_neighbor_info_ent[i].sec;
				PRINT_SINGL_ARG("	Last seen: ", sec, "%u seconds ago");
				PRINT_ONE("", "%s", 1);
			}			
		}
	}
	else
	{
		PRINT_ONE("  AP Neighbor Collect disabled", "%s", 1);
	}
	return pos;
}
#endif

#ifdef BEACON_VS_IE
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_specific_ap_probereq_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_specific_ap_probereq_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;
	unsigned long sec;

	PRINT_ONE("  AP Probereq get Info...", "%s", 1);
	for(i=0; i<NUM_PRBVSIE_SERVICE; i++)
	{
		if(priv->prbrsp_vsie[i].service == 1 && priv->prbrsp_vsie[i].data_len)
		{

			if(priv->prbrsp_vsie[i].if_info.is_used == 1)
			{
				PRINT_SINGL_ARG("entry: ", i, "%d");
				PRINT_ARRAY_ARG("	mac: ",  priv->prbrsp_vsie[i].mac, "%02X", 6);

				if(jiffies/HZ >= priv->prbrsp_vsie[i].if_info.timestamp)
					sec = jiffies/HZ - priv->prbrsp_vsie[i].if_info.timestamp;
				else
					sec = jiffies/HZ + ~(unsigned long)0/HZ - priv->prbrsp_vsie[i].if_info.timestamp;
				PRINT_SINGL_ARG("	Last seen: ", sec, "%u seconds ago");
				PRINT_ONE("", "%s", 1);

			}

		}
	}

	return pos;

}
#endif

#ifdef CONFIG_RTL_KERNEL_MIPS16_WLAN
__NOMIPS16
#endif
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_sta_keyinfo(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_sta_keyinfo(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int len = 0;
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    off_t begin = 0;
    off_t pos = 0;
#endif
    int size, num=1;
    struct list_head *phead, *plist;
    struct stat_info *pstat;

#if 1/*!defined(SMP_SYNC) || (defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI))    */
    unsigned long flags=0;
#endif

    SAVE_INT_AND_CLI(flags);
    SMP_LOCK_ASOC_LIST(flags);

#ifdef __ECOS
    ecos_pr_fun("-- STA key info table --\n");
#elif defined(CONFIG_RTL_PROC_NEW)
    seq_printf(s, "-- STA key info table --\n");
#else
    strncpy(buf, "-- STA key info table --\n", length-1);
    buf[length-1] = '\0';
    size = strlen(buf);
    CHECK_LEN;
#endif

    phead = &priv->asoc_list;
    if (!(priv->drv_state & DRV_STATE_OPEN) || list_empty(phead))
        goto _ret;

    plist = phead->next;
    while (plist != phead) {
        pstat = list_entry(plist, struct stat_info, asoc_list);
        plist = plist->next;
#ifdef CONFIG_RTL_PROC_NEW
        size = dump_one_sta_keyinfo(s, num++, pstat);
#else
        size = dump_one_sta_keyinfo(num++, pstat, buf+len, start, offset, length, eof, data);
#endif
        CHECK_LEN;
    }
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    *eof = 1;
#endif

_ret:
    SMP_UNLOCK_ASOC_LIST(flags);
    RESTORE_INT(flags);

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    *start = buf + (offset - begin);	/* Start of wanted data */
    len -= (offset - begin);	/* Start slop */
    if (len > length)
        len = length;	/* Ending slop */
#endif

    return len;
}

#ifdef CONFIG_RTL_PROC_NEW
static int dump_one_sta_dbginfo(struct seq_file *s, int num, struct rtl8192cd_priv *priv, struct stat_info *pstat)
#else
static int dump_one_sta_dbginfo(int num, struct rtl8192cd_priv *priv, struct stat_info *pstat, char *buf, char **start,
			off_t offset, int length, int *eof, void *data)
#endif			
{
    int pos = 0;
#ifdef TX_SHORTCUT
    int i;
    struct tx_insn		*txsc_txcfg;
#endif

    PRINT_ARRAY_ARG("    hwaddr: ",	pstat->cmn_info.mac_addr, "%02x", MACADDRLEN);

#ifdef CONFIG_SPECIAL_ENV_TEST
    for(i = 0 ; i < 8;i++) {
        PRINT_ONE("    TID[", "%s", 0);
        PRINT_ONE(i, "%d", 0);
        PRINT_ONE("]: ",  "%s", 0);
        PRINT_ONE(pstat->AC_seq[i], "%d", 1);
    }
    PRINT_SINGL_ARG("    sn_avg_gap:  ", pstat->sn_avg_gap, "%d");
#endif

#ifdef TX_SHORTCUT
    PRINT_SINGL_ARG("    tx_sc_pkts_lv1:  ", pstat->tx_sc_pkts_lv1, "%d");
    PRINT_SINGL_ARG("    tx_sc_pkts_lv2:  ", pstat->tx_sc_pkts_lv2, "%d");
    PRINT_SINGL_ARG("    tx_sc_pkts_slow: ", pstat->tx_sc_pkts_slow, "%u");
#ifdef SUPPORT_TX_AMSDU_SHORTCUT
    PRINT_SINGL_ARG("    tx_sc_amsdu_pkts_lv1:  ", pstat->tx_sc_amsdu_pkts_lv1, "%d");
    PRINT_SINGL_ARG("    tx_sc_amsdu_pkts_lv2:  ", pstat->tx_sc_amsdu_pkts_lv2, "%d");
    PRINT_SINGL_ARG("    tx_sc_amsdu_pkts_slow: ", pstat->tx_sc_amsdu_pkts_slow, "%u");
#endif
#ifdef SUPPORT_TX_AMSDU
    PRINT_SINGL_ARG("    tx_amsdu_pkts_remain[0]: ", skb_queue_len(&pstat->amsdu_tx_que[0]), "%u");
    PRINT_SINGL_ARG("    tx_amsdu_pkts_remain[1]: ", skb_queue_len(&pstat->amsdu_tx_que[1]), "%u");
    PRINT_SINGL_ARG("    tx_amsdu_pkts_remain[2]: ", skb_queue_len(&pstat->amsdu_tx_que[2]), "%u");
    PRINT_SINGL_ARG("    tx_amsdu_pkts_remain[3]: ", skb_queue_len(&pstat->amsdu_tx_que[3]), "%u");
#endif

    PRINT_SINGL_ARG("    rx_sc_pkts:      ", pstat->rx_sc_pkts, "%d");
    PRINT_SINGL_ARG("    rx_sc_pkts_slow: ", pstat->rx_sc_pkts_slow, "%u");

#if defined(RX_SHORTCUT)
	for(i = 0 ; i < RX_SC_ENTRY_NUM;i++) {
		PRINT_ONE("    RX shortcut ", "%s", 0);
		PRINT_ARRAY(pstat->rx_sc_ent[i].rx_ethhdr.daddr, "%02x", MACADDRLEN, 0);
		PRINT_ARRAY(pstat->rx_sc_ent[i].rx_ethhdr.saddr, "%02x", MACADDRLEN, 0);
#if defined(RX_RL_SHORTCUT)
		PRINT_ARRAY(pstat->rx_sc_ent[i].rx_wlanhdr.meshhdr.DestMACAddr, "%02x", MACADDRLEN, 0);
		PRINT_ARRAY(pstat->rx_sc_ent[i].rx_wlanhdr.meshhdr.SrcMACAddr, "%02x", MACADDRLEN, 0);
#endif
		PRINT_ONE("", "%s", 1);
	}
#endif

#ifdef CONFIG_PCI_HCI
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
	    for(i = 0 ; i < MAX_TXSC_ENTRY;i++) {
	        txsc_txcfg = &pstat->tx_sc_ent[i].txcfg;
	        if(txsc_txcfg->fr_len > 0) {
	            PRINT_ONE("    shortcut ", "%s", 0);            
	            PRINT_ONE(i, "%d:", 0);
	            PRINT_ARRAY(pstat->tx_sc_ent[i].ethhdr.saddr, "%02x", MACADDRLEN, 0);           
	            PRINT_ONE(pstat->tx_sc_ent[i].ethhdr.type, " %04X", 0);

	            #if defined(WLAN_HAL_HW_TX_SHORTCUT_REUSE_TXDESC) || defined(SUPPORT_TXDESC_IE)
	            if((IS_SUPPORT_WLAN_HAL_HW_TX_SHORTCUT_REUSE_TXDESC(priv)||IS_SUPPORT_TXDESC_IE(priv)) && pstat->tx_sc_hw_idx == i) {
	                PRINT_ONE(" HW", "%s", 0);            
	            }
	            #endif
	            PRINT_ONE("", "%s", 1);            
	        }
	    }
	}
#endif
#endif
    

#ifdef CONFIG_PCI_HCI
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
		PRINT_SINGL_ARG("    dz_queue_len: ", skb_queue_len(&pstat->dz_queue), "%u");
	}
#endif
	PRINT_SINGL_ARG("    hp_level: ", pstat->hp_level, "%d");

#ifdef RSSI_MONITOR_NCR
	PRINT_SINGL_ARG("	 last report RSSI: ", pstat->rssi_report, "%d");
	PRINT_SINGL_ARG("    RSSI monitor type: ", pstat->rssim_type, "%d");
#endif
#ifdef _DEBUG_RTL8192CD_
	PRINT_ONE("    ch_width:  ", "%s", 0);
	PRINT_ONE((pstat->ht_cap_buf.ht_cap_info & cpu_to_le16(_HTCAP_SUPPORT_CH_WDTH_))?"40M":"20M", "%s", 1);
	PRINT_ONE("    ampdu_mf:  ", "%s", 0);
	PRINT_ONE(pstat->ht_cap_buf.ampdu_para & 0x03, "%d", 1);
	PRINT_ONE("    ampdu_amd: ", "%s", 0);
	PRINT_ONE((pstat->ht_cap_buf.ampdu_para & _HTCAP_AMPDU_SPC_MASK_) >> _HTCAP_AMPDU_SPC_SHIFT_, "%d", 1);
#endif

#if defined(HW_ANT_SWITCH)&&( defined(CONFIG_RTL_92C_SUPPORT) || defined(CONFIG_RTL_92D_SUPPORT))
	PRINT_SINGL_ARG("    Ant 1 packet count:  ", pstat->hwRxAntSel[1], "%u");
	PRINT_SINGL_ARG("    Ant 2 packet count:  ", pstat->hwRxAntSel[0], "%u");
	PRINT_SINGL_ARG("    Antenna select :  ", (pstat->CurAntenna==0 ? 2 : 1), "%u");
#endif

	PRINT_SINGL_ARG("    tx amsdu timerovf:  ", pstat->tx_amsdu_timer_ovf, "%u");
	PRINT_SINGL_ARG("    rx reorder4 count:  ", pstat->rx_rc4_count, "%u");
    PRINT_SINGL_ARG("    rc timer overflow:  ", pstat->rx_rc_timer_ovf, "%u");

#ifdef _AMPSDU_AMSDU_DEBUG_//_DEBUG_RTL8192CD_
	PRINT_SINGL_ARG("    tx amsdu to:      ", pstat->tx_amsdu_to, "%u");
	PRINT_SINGL_ARG("	 tx amsdu bufof:   ", pstat->tx_amsdu_buf_overflow, "%u");
	PRINT_SINGL_ARG("    tx amsdu 1pkt:    ", pstat->tx_amsdu_1pkt, "%u");
	PRINT_SINGL_ARG("    tx amsdu 2pkt:    ", pstat->tx_amsdu_2pkt, "%u");
	PRINT_SINGL_ARG("    tx amsdu 3pkt:    ", pstat->tx_amsdu_3pkt, "%u");
	PRINT_SINGL_ARG("    tx amsdu 4pkt:    ", pstat->tx_amsdu_4pkt, "%u");
	PRINT_SINGL_ARG("    tx amsdu 5pkt:    ", pstat->tx_amsdu_5pkt, "%u");
	PRINT_SINGL_ARG("    tx amsdu gt 5pkt: ", pstat->tx_amsdu_gt5pkt, "%u");
	
	PRINT_SINGL_ARG("    amsdu err:     ", pstat->rx_amsdu_err, "%u");
	PRINT_SINGL_ARG("    amsdu 1pkt:    ", pstat->rx_amsdu_1pkt, "%u");
	PRINT_SINGL_ARG("    amsdu 2pkt:    ", pstat->rx_amsdu_2pkt, "%u");
	PRINT_SINGL_ARG("    amsdu 3pkt:    ", pstat->rx_amsdu_3pkt, "%u");
	PRINT_SINGL_ARG("    amsdu 4pkt:    ", pstat->rx_amsdu_4pkt, "%u");
	PRINT_SINGL_ARG("    amsdu 5pkt:    ", pstat->rx_amsdu_5pkt, "%u");
	PRINT_SINGL_ARG("    amsdu gt 5pkt: ", pstat->rx_amsdu_gt5pkt, "%u");
#endif

#ifdef _DEBUG_RTL8192CD_
	PRINT_SINGL_ARG("    rc drop1:     ", pstat->rx_rc_drop1, "%u");
	PRINT_SINGL_ARG("    rc passup2:   ", pstat->rx_rc_passup2, "%u");
	PRINT_SINGL_ARG("    rc drop3:     ", pstat->rx_rc_drop3, "%u");
	PRINT_SINGL_ARG("    rc reorder3:  ", pstat->rx_rc_reorder3, "%u");
	PRINT_SINGL_ARG("    rc passup3:   ", pstat->rx_rc_passup3, "%u");
	PRINT_SINGL_ARG("    rc drop4:     ", pstat->rx_rc_drop4, "%u");
	PRINT_SINGL_ARG("    rc reorder4:  ", pstat->rx_rc_reorder4, "%u");
	PRINT_SINGL_ARG("    rc passup4:   ", pstat->rx_rc_passup4, "%u");
	PRINT_SINGL_ARG("    rc passupi:   ", pstat->rx_rc_passupi, "%u");
#endif
#ifdef SW_TX_QUEUE
if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
	PRINT_SINGL_ARG("    bk aggnum:   ", pstat->swq.q_aggnum[BK_QUEUE], "%d");
	PRINT_SINGL_ARG("    be en:   ", pstat->swq.swq_en[BE_QUEUE], "%d");
	PRINT_SINGL_ARG("    be aggnum:   ", pstat->swq.q_aggnum[BE_QUEUE], "%d");
	PRINT_SINGL_ARG("    be qlen:   ", skb_queue_len(&pstat->swq.swq_queue[BE_QUEUE]), "%d");
	PRINT_SINGL_ARG("    be qlen max:   ", pstat->swq.swq_queue_len_max[BE_QUEUE], "%d");
	PRINT_SINGL_ARG("    be xmit max:   ", pstat->swq.swq_queue_xmit_max[BE_QUEUE], "%d");
	PRINT_SINGL_ARG("    vi aggnum:   ", pstat->swq.q_aggnum[VI_QUEUE], "%d");
	PRINT_SINGL_ARG("    vo aggnum:   ", pstat->swq.q_aggnum[VO_QUEUE], "%d");
	PRINT_SINGL_ARG("    bk backtime:   ", pstat->swq.q_aggnumIncSlow[BK_QUEUE], "%d");
    PRINT_SINGL_ARG("    be backtime:   ", pstat->swq.q_aggnumIncSlow[BE_QUEUE], "%d");
    PRINT_SINGL_ARG("    vi backtime:   ", pstat->swq.q_aggnumIncSlow[VI_QUEUE], "%d");
    PRINT_SINGL_ARG("    vo backtime:   ", pstat->swq.q_aggnumIncSlow[VO_QUEUE], "%d");
}
#endif

#if (BEAMFORMING_SUPPORT == 1)
	PRINT_SINGL_ARG("    bf score:   ", pstat->bf_score, "%u");
#endif	

	PRINT_ONE("", "%s", 1);

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_sta_dbginfo(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_sta_dbginfo(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int len = 0;
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    off_t begin = 0;
    off_t pos = 0;
#endif
    int size, num=1;
    struct list_head *phead, *plist;
    struct stat_info *pstat;

#if 1/*!defined(SMP_SYNC) || (defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI))    */
    unsigned long flags=0;
#endif

    SAVE_INT_AND_CLI(flags);
    SMP_LOCK_ASOC_LIST(flags);

#ifdef __ECOS
    ecos_pr_fun("-- STA dbg info table --\n");
#elif defined(CONFIG_RTL_PROC_NEW)
    seq_printf(s, "-- STA dbg info table --\n");
#else
    strncpy(buf, "-- STA dbg info table --\n", length-1);
    buf[length-1] = '\0';
    size = strlen(buf);
    CHECK_LEN;
#endif

#ifdef SW_TX_QUEUE
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
		PRINT_SINGL_ARG("    swq_numActiveSTA:   ", priv->pshare->swq_numActiveSTA, "%d");
	}
#endif	

    phead = &priv->asoc_list;
    if (!(priv->drv_state & DRV_STATE_OPEN) || list_empty(phead))
        goto _ret;

    plist = phead->next;
    while (plist != phead) {
        pstat = list_entry(plist, struct stat_info, asoc_list);
        plist = plist->next;
#ifdef CONFIG_RTL_PROC_NEW
        size = dump_one_sta_dbginfo(s, num++, priv, pstat);
#else
        size = dump_one_sta_dbginfo(num++, priv, pstat, buf+len, start, offset, length, eof, data);
#endif
        CHECK_LEN;
    }
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    *eof = 1;
#endif

_ret:
    SMP_UNLOCK_ASOC_LIST(flags);
    RESTORE_INT(flags);

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    *start = buf + (offset - begin);	/* Start of wanted data */
    len -= (offset - begin);	/* Start slop */
    if (len > length)
        len = length;	/* Ending slop */
#endif

    return len;
}


#ifdef PCIE_POWER_SAVING
char pwr_state_str[][20] = {"L0", "L1", "L2", "ASPM_L0s_L1" };
#endif

#if defined(CH_LOAD_CAL) && defined(USE_OUT_SRC)
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ch_load(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ch_load(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	struct rtl8192cd_priv *root_priv = GET_ROOT(priv);
	int pos = 0;
	struct ccx_info *ccx = &(ODMPTR->dm_ccx_info);
	
	priv = root_priv;

	PRINT_ONE("Channel usage (0~100%):", "%s", 1);

	PRINT_SINGL_ARG("    interence_time:     ", priv->pshare->ch_info.interence_time, "%d");
	PRINT_SINGL_ARG("    DUT_Time:           ", priv->pshare->ch_info.dut_time, "%d");
	PRINT_SINGL_ARG("    other_APs_Time:     ", priv->pshare->ch_info.other_dut_time, "%d");
	PRINT_SINGL_ARG("    free_Time:          ", priv->pshare->ch_info.free_time, "%d");
	PRINT_SINGL_ARG("    ch_load:            ", (100-priv->pshare->ch_info.free_time-priv->pshare->ch_info.interence_time), "%d");
	PRINT_SINGL_ARG("    ch_load(include interence):", (100-priv->pshare->ch_info.free_time), "%d");

	if (priv->pshare->rf_ft_var.rssi_dump) {
		PRINT_ONE("Counters:", "%s", 1);

		PRINT_SINGL_ARG("    current_tx_bytes:   ", priv->pshare->current_tx_bytes, "%lu");
		PRINT_SINGL_ARG("    current_rx_bytes:   ", priv->pshare->current_rx_bytes, "%lu");
		PRINT_SINGL_ARG("    Cnt_CCA_all:        ", ODMPTR->false_alm_cnt.cnt_cca_all, "%lu");
		PRINT_SINGL_ARG("    Cnt_FA_all:         ", ODMPTR->false_alm_cnt.cnt_all, "%lu");
		PRINT_SINGL_ARG("    clm_ratio:          ", ccx->clm_ratio, "%lu");
		PRINT_SINGL_ARG("    nhm_ratio:          ", ccx->nhm_ratio, "%lu");
		PRINT_SINGL_ARG("    nhm_duration:       ", ccx->nhm_duration, "%lu");
		PRINT_SINGL_ARG("    nhm_period:         ", ccx->nhm_period, "%lu");
		PRINT_SINGL_ARG("    nhm_igi:            ", ccx->nhm_igi, "%02x");
		PRINT_ARRAY_ARG("    nhm_result(L->H):   ", ccx->nhm_result, "%02x ", NHM_RPT_NUM);
		PRINT_SINGL_ARG("    ch_utilization:	 ", priv->ext_stats.ch_utilization, "%lu");
	}
	return pos;	
}
#endif

#ifdef __ECOS
void rtl8192cd_proc_set_temperature(void *data)
#else
static int rtl8192cd_proc_set_temperature(struct file *file, const char __user *buffer,
				unsigned long count, void *data)
#endif
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	char tmp[1500] = {0};
	char *tmp_ptr = tmp;
	s32 wifi_t_offset = 0;
	u32 wifi_t_coefficient = 0;
	u32 wifi_t_tp_level = 0;
	char delim[] = ",";
	char *substr = NULL;
	int i, j;
	int previous_offset = 0;

	if (count > sizeof(tmp)) {
#ifdef __KERNEL__
		WARN_ON(1);
#endif
		return -EFAULT;
	}

	if (!buffer || copy_from_user(tmp, buffer, count))
		goto exit;

	if (strstr(tmp, "dump")) {
		printk("wifi_t_coefficient = %d, wifi_t_offset = %d, wifi_t_tp_level = %d\n",
				priv->pshare->ther_parameter, priv->pshare->ther_offset,
				priv->pshare->ther_tp_level);
		for (i = 0; i < MAX_THER_TP_LEVEL; i++)
			printk("ther_cps[%d] = %d\n", i, priv->pshare->ther_cps[i]);

		goto exit;
	}

	substr = strsep(&tmp_ptr, delim);
	if (substr == NULL)
		goto exit;


	wifi_t_coefficient = _atoi(substr, 10);

	substr = strsep(&tmp_ptr, delim);
	if (substr == NULL)
		goto exit;

	wifi_t_offset = _atoi(substr, 10);

	substr = strsep(&tmp_ptr, delim);
	if (substr == NULL)
		goto exit;

	wifi_t_tp_level = _atoi(substr, 10);

	if (0 == wifi_t_offset)
		goto exit;
	else if (0 == wifi_t_coefficient)
		goto exit;
	else if (0 == wifi_t_tp_level)
		goto exit;

	priv->pshare->ther_parameter = wifi_t_coefficient;
	priv->pshare->ther_offset = wifi_t_offset;
	priv->pshare->ther_tp_level = wifi_t_tp_level;

	for (i = 0; i < MAX_THER_TP_LEVEL; i++) {
		substr = strsep(&tmp_ptr, delim);
		if (substr == NULL) {
			if (i > 0)
				previous_offset = priv->pshare->ther_cps[i - 1];
			else
				previous_offset = 0;
			for (j = i; j < MAX_THER_TP_LEVEL; j++)
				priv->pshare->ther_cps[j] = previous_offset;
			goto exit;
		}
		priv->pshare->ther_cps[i] = _atoi(substr, 10);
	}

exit:
#ifndef __ECOS
	return count;
#endif
}

static unsigned int _calcTemperature_12F(struct rtl8192cd_priv *priv, unsigned int ther)
{
	u32 wifi_t_coefficient = priv->pshare->ther_parameter;
	s32 wifi_t_offset = priv->pshare->ther_offset;
	u32 wifi_t_tp_level = priv->pshare->ther_tp_level;
	u32 temperature = 0;
	u32 adjusted_ther = 0;
	u32 index = 0;

	if (0 == wifi_t_offset)
		goto exit;
	else if (0 == wifi_t_coefficient)
		goto exit;
	else if (0 == wifi_t_tp_level)
		goto exit;

	/* Adjust thermal based on tput */
	index = priv->pshare->total_tp / wifi_t_tp_level;
	if (ther > priv->pshare->ther_cps[index])
		adjusted_ther = ther - priv->pshare->ther_cps[index];
	else
		adjusted_ther = ther;

	if ((wifi_t_coefficient * adjusted_ther + wifi_t_offset) > 0)
		temperature = (wifi_t_coefficient * adjusted_ther + wifi_t_offset) / 1000;
	else
		temperature = (wifi_t_coefficient * adjusted_ther) / 1000;

exit:
	return temperature;
}

static unsigned int _calcTemperature_92F(struct rtl8192cd_priv *priv, unsigned int ther)
{
	u32 wifi_t_coefficient = priv->pshare->ther_parameter;
	s32 wifi_t_offset = priv->pshare->ther_offset;
	u32 wifi_t_tp_level = priv->pshare->ther_tp_level;
	u32 temperature = 0;
	u32 adjusted_ther = 0;
	u32 index = 0;

	if (0 == wifi_t_offset)
		goto exit;
	else if (0 == wifi_t_coefficient)
		goto exit;
	else if (0 == wifi_t_tp_level)
		goto exit;

	/* Adjust thermal based on tput */
	index = priv->pshare->total_tp / wifi_t_tp_level;
	if (ther > priv->pshare->ther_cps[index])
		adjusted_ther = ther - priv->pshare->ther_cps[index];
	else
		adjusted_ther = ther;

	if ((wifi_t_coefficient * adjusted_ther + wifi_t_offset) > 0)
		temperature = (wifi_t_coefficient * adjusted_ther + wifi_t_offset) / 1000;
	else
		temperature = (wifi_t_coefficient * adjusted_ther) / 1000;

exit:
	return temperature;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_get_temperature(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_get_temperature(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	unsigned char chipVersion[16] = {0};
	unsigned int current_thermal = 0;
	unsigned int temperature = 0;

	_getChipVersion(priv, chipVersion, sizeof(chipVersion));

	if ((GET_CHIP_VER(priv) != VERSION_8812F) &&
			(GET_CHIP_VER(priv) != VERSION_8192F)) {
		printk("[%s] %s Do not support temperature measurement\n",
				chipVersion);
		return -1;
	}

	if (!netif_running(dev)) {
		goto exit;
	}

#ifdef CONFIG_WLAN_HAL_8812FE
	if (GET_CHIP_VER(priv) == VERSION_8812F) {
		/* Get thermal value */
		phy_set_rf_reg(priv, RF_PATH_A, 0x42, BIT(19), 0x1);
		phy_set_rf_reg(priv, RF_PATH_A, 0x42, BIT(19), 0x0);
		phy_set_rf_reg(priv, RF_PATH_A, 0x42, BIT(19), 0x1);
		delay_ms(10);
		current_thermal = phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0x7e, 1) +
			phydm_get_multi_thermal_offset(ODMPTR, RF_PATH_A);

		/* Convert thermal to temperature*/
		temperature = _calcTemperature_12F(priv, current_thermal);
	}
#endif
#ifdef CONFIG_WLAN_HAL_8192FE
	if (GET_CHIP_VER(priv) == VERSION_8192F) {
		/* Get thermal value */
		current_thermal = phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0xfc00, 1);

		/* Convert thermal to temperature*/
		temperature = _calcTemperature_92F(priv, current_thermal);
	}
#endif

exit:
	PRINT_SINGL_ARG("WiFiChip ID: ", chipVersion, "%s");
	PRINT_SINGL_ARG("thermal index:	", current_thermal, "%u");
	PRINT_SINGL_ARG("temperature: ", temperature, "%u");

	return 0;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_stats(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_stats(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, idx = 0;
	unsigned int m, n, print=0;
	char tmp[32];
	unsigned char *rate;
	unsigned char current_rate;
#ifndef SMP_SYNC
	unsigned long flags=0;
#endif
#ifdef CONFIG_PCI_HCI
#if defined(__ECOS) && defined(TX_PKT_FREE_QUEUE)
	Rltk819x_t *info = dev->info;
#endif
#endif

	SAVE_INT_AND_CLI(flags);

	PRINT_ONE("  Statistics...", "%s", 1);

	m = priv->up_time / 86400;
	n = priv->up_time % 86400;
	if (m) {
		idx += snprintf(tmp, sizeof(tmp), "%d day ", m);
		print = 1;
	}
	m = n / 3600;
	n = n % 3600;
	if (m || print) {
		idx += snprintf(tmp+idx, sizeof(tmp) - idx, "%d hr ", m);
		print = 1;
	}
	m = n / 60;
	n = n % 60;
	if (m || print) {
		idx += snprintf(tmp+idx, sizeof(tmp) - idx, "%d min ", m);
		print = 1;
	}
	idx += snprintf(tmp+idx, sizeof(tmp) - idx, "%d sec ", n);
	PRINT_SINGL_ARG("    up_time:       ", tmp, "%s");

#ifdef CH_LOAD_CAL
	/*reference IEEE,Std 802.11-2012,page567,use 0~255 to representing 0~100%*/
	PRINT_SINGL_ARG("    ch_utilization:	", priv->ext_stats.ch_utilization, "%lu");
	PRINT_SINGL_ARG("    tx_time:       ", priv->ext_stats.tx_time, "%lu");
	PRINT_SINGL_ARG("    rx_time:       ", priv->ext_stats.rx_time, "%lu");
#endif
	PRINT_SINGL_ARG("    tx_packets:    ", (unsigned long long)priv->net_stats.tx_packets, "%llu");
#ifdef TRX_DATA_LOG	
	PRINT_SINGL_ARG("    tx_data_pkts:  ", priv->ext_stats.tx_data_packets, "%lu");	
#endif	
	PRINT_SINGL_ARG("    tx_bytes:      ", (unsigned long long)priv->net_stats.tx_bytes, "%llu");
#if defined(CONFIG_RTL8672) || defined(CONFIG_WLAN_STATS_EXTENTION)
	PRINT_SINGL_ARG("    tx_mgnt_pkts: ", priv->ext_stats.tx_mgnt_pkts, "%lu");
	PRINT_SINGL_ARG("    tx_ucast_pkts: ", priv->ext_stats.tx_ucast_pkts_cnt, "%lu");
	PRINT_SINGL_ARG("    tx_mcast_pkts: ", priv->ext_stats.tx_mcast_pkts_cnt, "%lu");
	PRINT_SINGL_ARG("    tx_bcast_pkts: ", priv->ext_stats.tx_bcast_pkts_cnt, "%lu");
#endif
#ifdef CROSSBAND_REPEATER
	PRINT_SINGL_ARG("	 crossband_pathswitch_pkts: ", priv->ext_stats.cb_pathswitch_pkts, "%lu");
#endif
	PRINT_SINGL_ARG("    tx_retrys:     ", priv->ext_stats.tx_retrys, "%lu");
	PRINT_SINGL_ARG("    tx_fails:      ", (unsigned long long)priv->net_stats.tx_errors, "%llu");
	PRINT_SINGL_ARG("    tx_drops:      ", priv->ext_stats.tx_drops, "%lu");
#ifdef CONFIG_RTL_OFFLOAD_DRIVER
	PRINT_SINGL_ARG("    tx_cmd_queue_drops:      ", priv->ext_stats.tx_cmd_queue_drops, "%lu");
#endif	
	PRINT_SINGL_ARG("    tx_drops_noasoc:      ", priv->ext_stats.tx_drops_noasoc, "%lu");	

#ifdef CMCC_CTC_QOS
	PRINT_SINGL_ARG("    cb_data1:      ", priv->ext_stats.cb_data1, "%lu");
	PRINT_SINGL_ARG("    cb_data1_drop: ", priv->ext_stats.cb_data1_drop, "%lu");
#endif

	PRINT_SINGL_ARG("    tx_dma_err:    ", priv->pshare->tx_dma_err, "%lu");
	PRINT_SINGL_ARG("    rx_dma_err:    ", priv->pshare->rx_dma_err, "%lu");
	PRINT_SINGL_ARG("    tx_dma_status: ", priv->pshare->tx_dma_status, "%lx");
	PRINT_SINGL_ARG("    tx_dma_status1: ", priv->pshare->tx_dma_status1, "%lx");
	PRINT_SINGL_ARG("    rx_dma_status: ", priv->pshare->rx_dma_status, "%lx");
	PRINT_SINGL_ARG("    tx_dma_0x40000: ", priv->pshare->tx_dma_0x40000, "%u");
#ifdef RING_BASED_DATA_FRAME_QUEUE
	PRINT_SINGL_ARG("    rx_data_queue head: ", priv->data_queue.head, "%d");
	PRINT_SINGL_ARG("    rx_data_queue tail: ", priv->data_queue.tail, "%d");
	PRINT_SINGL_ARG("    rx_data_queue drop: ", priv->ext_stats.rx_data_queue_drop, "%lu");
#endif
#ifdef SUPPORT_AXI_BUS_EXCEPTION
	PRINT_SINGL_ARG("    axi_exception: ", priv->pshare->axi_exception, "%lu");
#endif //SUPPORT_AXI_BUS_EXCEPTION
#ifdef CONFIG_WLAN_HAL_8814AE
	PRINT_SINGL_ARG("    tx_fovw:       ", priv->ext_stats.tx_fovw, "%lu");
	PRINT_SINGL_ARG("    rx_ht_status:  ", priv->ext_stats.rx_ht_status, "%lu");
	PRINT_SINGL_ARG("    cnt_2s_notx:   ", priv->check_cnt_2s_notx, "%u");
#endif

	PRINT_SINGL_ARG("    rx_packets:    ", (unsigned long long)priv->net_stats.rx_packets, "%llu");
#ifdef TRX_DATA_LOG		
	PRINT_SINGL_ARG("    rx_data_pkts:  ", priv->ext_stats.rx_data_packets, "%lu");	
#endif	
#ifdef TRX_VI_DATA_LOG
	PRINT_SINGL_ARG("    tx_vi_data_pkts:  ", priv->ext_stats.tx_vi_pkts, "%lu");	
	PRINT_SINGL_ARG("    tx_vi_data_bytes:  ", priv->ext_stats.tx_vi_bytes, "%lu");	
	PRINT_SINGL_ARG("    tx_vi_data_error_pkts:  ", priv->ext_stats.tx_vi_error_pkts, "%lu");	
	PRINT_SINGL_ARG("    tx_vi_data_drop_pkts:  ", priv->ext_stats.tx_vi_drop_pkts, "%lu");
	PRINT_SINGL_ARG("    tx_vi_data_retry_pkts:  ", priv->ext_stats.tx_vi_retry_pkts, "%lu");	
	PRINT_SINGL_ARG("    rx_vi_data_pkts:  ", priv->ext_stats.rx_vi_pkts, "%lu");	
	PRINT_SINGL_ARG("    rx_vi_data_bytes:  ", priv->ext_stats.rx_vi_bytes, "%lu");	
	PRINT_SINGL_ARG("    rx_vi_data_error_pkts:  ", priv->ext_stats.rx_vi_error_pkts, "%lu");	
	PRINT_SINGL_ARG("    rx_vi_data_drop_pkts:  ", priv->ext_stats.rx_vi_drop_pkts, "%lu");
#endif
	PRINT_SINGL_ARG("    rx_bytes:      ", (unsigned long long)priv->net_stats.rx_bytes, "%llu");
#if defined(CONFIG_RTL8672) || defined(CONFIG_WLAN_STATS_EXTENTION)
	PRINT_SINGL_ARG("    rx_mgnt_pkts: ", priv->ext_stats.rx_mgnt_pkts, "%lu");
	PRINT_SINGL_ARG("    rx_ucast_pkts: ", priv->ext_stats.rx_ucast_pkts_cnt, "%lu");
	PRINT_SINGL_ARG("    rx_mcast_pkts: ", priv->ext_stats.rx_mcast_pkts_cnt, "%lu");
	PRINT_SINGL_ARG("    rx_bcast_pkts: ", priv->ext_stats.rx_bcast_pkts_cnt, "%lu");
	PRINT_SINGL_ARG("    unknown_pro_pkts: ", priv->ext_stats.unknown_pro_pkts_cnt, "%lu");
	PRINT_SINGL_ARG("    total_mic_fail: ", priv->ext_stats.total_mic_fail, "%lu");
	PRINT_SINGL_ARG("    total_psk_fail: ", priv->ext_stats.total_psk_fail, "%lu");
#endif
	PRINT_SINGL_ARG("    rx_retrys:     ", priv->ext_stats.rx_retrys, "%lu");
	PRINT_SINGL_ARG("    rx_crc_errors: ", (unsigned long long)priv->net_stats.rx_crc_errors, "%llu");
	PRINT_SINGL_ARG("    rx_errors:     ", (unsigned long long)priv->net_stats.rx_errors, "%llu");
	PRINT_SINGL_ARG("    rx_data_drops: ", priv->ext_stats.rx_data_drops, "%lu");
	PRINT_SINGL_ARG("    rx_mc_pn_drops: ", priv->ext_stats.rx_mc_pn_drops, "%lu");
	PRINT_SINGL_ARG("    rx_cnt_psk_to: ", priv->ext_stats.rx_cnt_psk_to, "%lu");
	PRINT_SINGL_ARG("    rx_decache:    ", priv->ext_stats.rx_decache, "%lu");
	PRINT_SINGL_ARG("    rx_fifoO:      ", priv->ext_stats.rx_fifoO, "%lu");
	PRINT_SINGL_ARG("    rx_rdu:        ", priv->ext_stats.rx_rdu, "%lu");
	PRINT_SINGL_ARG("    rx_reuse:      ", priv->ext_stats.rx_reuse, "%lu");
	PRINT_SINGL_ARG("    rx_reused_skb: ", priv->ext_stats.reused_skb, "%lu");
	PRINT_SINGL_ARG("    rx_fail:       ", priv->ext_stats.rx_fail, "%lu");
	PRINT_SINGL_ARG("    rx_amsdu:      ", priv->ext_stats.rx_amsdu, "%lu");
	PRINT_SINGL_ARG("    si_mem_drop:   ", priv->ext_stats.si_mem_drop, "%lu");
	PRINT_SINGL_ARG("    beacon_ok:     ", priv->ext_stats.beacon_ok, "%lu");
	PRINT_SINGL_ARG("    beacon_er:     ", priv->ext_stats.beacon_er, "%lu");
	PRINT_SINGL_ARG("    beacon_dma_err:", priv->ext_stats.beacon_dma_err, "%lu");
	PRINT_SINGL_ARG("    beacon_err_reason:", priv->ext_stats.beacon_err_rson, "%lu");
	PRINT_SINGL_ARG("    freeskb_err:   ", priv->ext_stats.freeskb_err, "%lu");
#ifdef CONFIG_PCI_HCI
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
		PRINT_SINGL_ARG("    dz_queue_len:  ", CIRC_CNT(priv->dz_queue.head, priv->dz_queue.tail, NUM_TXPKT_QUEUE), "%d");
	}
#endif

#ifdef CHECK_HANGUP
if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE)
{
#if defined(UNIVERSAL_REPEATER) || defined(MBSSID)
	if (IS_ROOT_INTERFACE(priv))
#endif
	{
#ifdef CHECK_TX_HANGUP
		PRINT_SINGL_ARG("    check_cnt_tx:  ", priv->check_cnt_tx, "%d");
		PRINT_SINGL_ARG("    check_cnt_tx_dma:  ", priv->check_cnt_tx_dma, "%d");
#endif
#ifdef CHECK_FW_ERROR
		PRINT_SINGL_ARG("    check_cnt_fw:  ", priv->check_cnt_fw, "%d");
#endif
#if defined(CHECK_RX_HANGUP) || defined(CHECK_RX_DMA_ERROR)
		PRINT_SINGL_ARG("    check_cnt_rx:  ", priv->check_cnt_rx, "%d");
#endif
#ifdef CHECK_BEACON_HANGUP
		PRINT_SINGL_ARG("    check_cnt_bcn: ", priv->check_cnt_bcn, "%d");
#endif
#ifdef CHECK_AFTER_RESET
		PRINT_SINGL_ARG("    check_cnt_rst: ", priv->check_cnt_rst, "%d");
#endif
#ifdef CHECK_DATACPU_HANGUP
		PRINT_SINGL_ARG("    check_cnt_datacpu: ", priv->check_cnt_datacpu, "%d");
#endif
	}
}
#endif

#if defined(CONFIG_RTL_TRIBAND_SUPPORT)
//RTL_TRIBAND_SUPPORT
#else
#if (MU_BEAMFORMING_SUPPORT == 1)
PRINT_SINGL_ARG("    cnt_sta1_only:  ", priv->cnt_sta1_only, "%d");
PRINT_SINGL_ARG("    cnt_sta2_only:  ", priv->cnt_sta2_only, "%d");
PRINT_SINGL_ARG("    cnt_sta1_sta2:  ", priv->cnt_sta1_sta2, "%d");
PRINT_SINGL_ARG("    cnt_not_mu:  ", priv->cnt_not_mu, "%d");
#endif
PRINT_SINGL_ARG("    cnt_mu_swqtimeout:  ", priv->cnt_mu_swqtimeout, "%d");
#endif /* defined(CONFIG_RTL_TRIBAND_SUPPORT) */

#if (MU_BEAMFORMING_SUPPORT == 1)
PRINT_ARRAY_ARG("    mu_ok: ",	priv->pshare->rf_ft_var.mu_ok, "%d ", MAX_NUM_BEAMFORMEE_MU);
PRINT_ARRAY_ARG("    mu_fail: ",	priv->pshare->rf_ft_var.mu_fail, "%d ", MAX_NUM_BEAMFORMEE_MU);

PRINT_SINGL_ARG("    mu_BB_ok:  ", priv->pshare->rf_ft_var.mu_BB_ok, "%d");
PRINT_SINGL_ARG("    mu_BB_fail:  ", priv->pshare->rf_ft_var.mu_BB_fail, "%d");


#endif
	PRINT_SINGL_ARG("    VO drop:       ", priv->pshare->phw->VO_droppkt_count, "%d");
	PRINT_SINGL_ARG("    VI drop:       ", priv->pshare->phw->VI_droppkt_count, "%d");
	PRINT_SINGL_ARG("    BE drop:       ", priv->pshare->phw->BE_droppkt_count, "%d");
	PRINT_SINGL_ARG("    BK drop:       ", priv->pshare->phw->BK_droppkt_count, "%d");


#if defined(CONFIG_RTL_92D_SUPPORT) && defined(CONFIG_RTL_NOISE_CONTROL)
	if (GET_CHIP_VER(priv) == VERSION_8192D){
#ifdef _DEBUG_RTL8192CD_
		PRINT_SINGL_ARG("    Reg 0xc50:     ", RTL_R8(0xc50), "0x%02x");
		PRINT_SINGL_ARG("    Reg 0xc58:     ", RTL_R8(0xc58), "0x%02x");
		PRINT_SINGL_ARG("    cck_FA_cnt:    ", priv->pshare->cck_FA_cnt, "%d");
		PRINT_SINGL_ARG("    ofdm_FA_cnt1:  ", priv->pshare->ofdm_FA_cnt1, "%d");
		PRINT_SINGL_ARG("    ofdm_FA_cnt2:  ", priv->pshare->ofdm_FA_cnt2, "%d");
		PRINT_SINGL_ARG("    ofdm_FA_cnt3:  ", priv->pshare->ofdm_FA_cnt3, "%d");
		PRINT_SINGL_ARG("    ofdm_FA_cnt4:  ", priv->pshare->ofdm_FA_cnt4, "%d");
		PRINT_SINGL_ARG("    FA_total_cnt:  ", priv->pshare->FA_total_cnt, "%d");
		//PRINT_SINGL_ARG("    f90[31:16]:    ", priv->pshare->F90_cnt, "%d");
		PRINT_SINGL_ARG("    f94[31:16]:    ", priv->pshare->F94_cnt, "%d");
		PRINT_SINGL_ARG("    f94OK[15:0]:   ", priv->pshare->F94_cntOK, "%d");
		PRINT_SINGL_ARG("    664[19:0]:     ", priv->pshare->Reg664_cnt, "%d");
		PRINT_SINGL_ARG("    664OK[19:0]:   ", priv->pshare->Reg664_cntOK, "%d");
#endif
		PRINT_SINGL_ARG("    DNC_on:        ", priv->pshare->DNC_on, "%d");
		PRINT_SINGL_ARG("    tp_avg_pre:    ", priv->ext_stats.tp_average_pre, "%lu");
	}
#endif

#if defined(CONFIG_RTL_92C_SUPPORT) && defined(CONFIG_RTL_NOISE_CONTROL_92C)
	if ((GET_CHIP_VER(priv) == VERSION_8192C)||(GET_CHIP_VER(priv) == VERSION_8188C)){
		PRINT_SINGL_ARG("    FA_total_cnt:  ", priv->pshare->FA_total_cnt, "%d");
		PRINT_SINGL_ARG("    DNC_on:        ", (priv->pshare->DNC_on ? 1 : 0), "%d");
		PRINT_SINGL_ARG("    tp_avg_pre:    ", priv->ext_stats.tp_average_pre, "%lu");
	}
#endif

#ifdef CONFIG_RTL8190_PRIV_SKB
	{
#ifdef CONCURRENT_MODE
		extern int skb_free_num[];
		PRINT_SINGL_ARG("    skb_free_num:  ", skb_free_num[priv->pshare->wlandev_idx]+priv->pshare->skb_queue.qlen, "%d");
#else
		extern int skb_free_num;
		PRINT_SINGL_ARG("    skb_free_num:  ", skb_free_num+priv->pshare->skb_queue.qlen, "%d");
#endif
	}
#else
#if !(defined(__ECOS) && defined(CONFIG_SDIO_HCI))
	PRINT_SINGL_ARG("    skb_free_num:  ", priv->pshare->skb_queue.qlen, "%d");
#endif
#if !defined(RTL8190_ISR_RX) || !defined(RTL8190_DIRECT_RX)
	PRINT_SINGL_ARG("    rx_enqueue_cnt:    ", rtl_atomic_read(&priv->pshare->rx_enqueue_cnt), "%lu");
	PRINT_SINGL_ARG("    rx_rdu_dis_int:    ", priv->pshare->rx_rdu_dis_int, "%lu");
#endif
#endif
#ifdef CONFIG_PCI_HCI
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
#if defined(__ECOS) && defined(TX_PKT_FREE_QUEUE)
		PRINT_SINGL_ARG("    tx_queue:      ", info->tx_queue.qlen, "%d");
#endif
	}
#endif
	PRINT_SINGL_ARG("    tx_avarage:    ", priv->ext_stats.tx_avarage, "%lu");
	PRINT_SINGL_ARG("    rx_avarage:    ", priv->ext_stats.rx_avarage, "%lu");

#if defined(CONFIG_RTL8196B_TR) || defined(CONFIG_RTL865X_AC) || defined(CONFIG_RTL865X_KLD) || defined(CONFIG_RTL8196B_KLD) || defined(CONFIG_RTL8196C_KLD) || defined(CONFIG_RTL8196C_EC)
	PRINT_SINGL_ARG("    tx_peak:       ", priv->ext_stats.tx_peak, "%lu");
	PRINT_SINGL_ARG("    rx_peak:       ", priv->ext_stats.rx_peak, "%lu");
#endif

	PRINT_SINGL_ARG("    rx_desc_num:   ", RX_DESC_NUM, "%d");
#ifdef CONFIG_RTL8190_PRIV_SKB
	PRINT_SINGL_ARG("    rx_skb_num:    ", RX_MAX_SKB_NUM, "%d");
#endif
	PRINT_SINGL_ARG("    rx_buf_len:    ", RX_BUF_LEN, "%d");

	// avoid current_tx_rate change during the following process to cause out of range in MCS_DATA_RATEStr
	current_rate = priv->pshare->current_tx_rate;
#ifdef RTK_AC_SUPPORT  //vht rate , todo, dump vht rates in Mbps
	if(current_rate >= VHT_RATE_ID){
		int tx_rate = VHT_MCS_DATA_RATE[MIN_NUM(priv->pmib->dot11nConfigEntry.dot11nUse40M, 3)][(priv->pshare->ht_current_tx_info&BIT(1))?1:0][(current_rate - VHT_RATE_ID)];
		tx_rate = tx_rate >> 1;
		PRINT_ONE("    cur_tx_rate: VHT NSS", "%s", 0);
		PRINT_ONE(((current_rate - VHT_RATE_ID)/10)+1, "%d", 0);
		PRINT_ONE("-MCS", "%s", 0);
		PRINT_ONE((current_rate - VHT_RATE_ID)%10, "%d", 0);
		PRINT_ONE(tx_rate, "  %d", 1);
	}
	else
#endif
	if (is_MCS_rate(current_rate)) {
		PRINT_ONE("    cur_tx_rate:   MCS", "%s", 0);
		PRINT_ONE((current_rate - HT_RATE_ID), "%d", 0);
		rate = (unsigned char *)MCS_DATA_RATEStr[(priv->pshare->ht_current_tx_info&BIT(0))?1:0][(priv->pshare->ht_current_tx_info&BIT(1))?1:0][(current_rate - HT_RATE_ID)];
		PRINT_ONE(rate, " %s", 1);
	}
	else
	{
		PRINT_SINGL_ARG("    cur_tx_rate:   ", current_rate/2, "%d");
	}
#ifdef PCIE_POWER_SAVING
	PRINT_SINGL_ARG("    pcie pwr state: ", pwr_state_str[priv->pwr_state], "%s");
#endif

#ifdef RESERVE_TXDESC_FOR_EACH_IF
	if (RSVQ_ENABLE) {
		PRINT_SINGL_ARG("    bkq_used_desc: ", (UINT)priv->use_txdesc_cnt[RSVQ(BK_QUEUE)], "%d");
		PRINT_SINGL_ARG("    beq_used_desc: ", (UINT)priv->use_txdesc_cnt[RSVQ(BE_QUEUE)], "%d");
		PRINT_SINGL_ARG("    viq_used_desc: ", (UINT)priv->use_txdesc_cnt[RSVQ(VI_QUEUE)], "%d");
		PRINT_SINGL_ARG("    voq_used_desc: ", (UINT)priv->use_txdesc_cnt[RSVQ(VO_QUEUE)], "%d");
	}
#endif

#ifdef HS2_SUPPORT
/* Hotspot 2.0 Release 1 */
	PRINT_SINGL_ARG("    proxy arp:     ", priv->proxy_arp, "%d");
	PRINT_SINGL_ARG("    dgaf_disable:	",priv->dgaf_disable, "%d");
	PRINT_SINGL_ARG("    IWLen:         ",priv->pmib->hs2Entry.interworking_ielen, "%d");
#endif

#ifdef SW_TX_QUEUE
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
		PRINT_SINGL_ARG("    swq enable:    ", priv->pshare->swq_en, "%d");
		PRINT_SINGL_ARG("    swq hw timer:  ", priv->pshare->swq_use_hw_timer, "%d");
	}
#endif

#ifdef CONFIG_WLAN_HAL
	PRINT_SINGL_ARG("    use hal:       ", priv->pshare->use_hal, "%d");
#endif
#ifdef USE_OUT_SRC
	PRINT_SINGL_ARG("    use outsrc:    ", priv->pshare->use_outsrc, "%d");
#endif	

#if defined(SHORTCUT_STATISTIC) /*defined(__ECOS) && defined(_DEBUG_RTL8192CD_)*/
	PRINT_SINGL_ARG("    tx_cnt_nosc:   ", priv->ext_stats.tx_cnt_nosc, "%lu");
	PRINT_SINGL_ARG("    tx_cnt_sc1:    ", priv->ext_stats.tx_cnt_sc1, "%lu");
	PRINT_SINGL_ARG("    tx_cnt_sc2:    ", priv->ext_stats.tx_cnt_sc2, "%lu");
	PRINT_SINGL_ARG("    rx_cnt_nosc:   ", priv->ext_stats.rx_cnt_nosc, "%lu");
	PRINT_SINGL_ARG("    rx_cnt_sc:     ", priv->ext_stats.rx_cnt_sc, "%lu");
	PRINT_SINGL_ARG("    br_cnt_nosc:   ", priv->ext_stats.br_cnt_nosc, "%lu");
	PRINT_SINGL_ARG("    br_cnt_sc:     ", priv->ext_stats.br_cnt_sc, "%lu");
#endif

	if(RTL_R8(0x1f)==0){
		PRINT_SINGL_ARG("    rf_lock:       ", "true", "%s");
	}else{
		PRINT_SINGL_ARG("    rf_lock:       ", "false", "%s");
	}
	PRINT_SINGL_ARG("    IQK total count: ", priv->pshare->IQK_total_cnt, "%d");
	PRINT_SINGL_ARG("    Check IQK:     ", priv->pshare->IQK_fail_cnt, "%d");
#ifdef CONFIG_WLAN_HAL_8192EE
	if (GET_CHIP_VER(priv) == VERSION_8192E)
		PRINT_SINGL_ARG("    check spur:	", priv->pshare->PLL_reset_ok, "%d");
#endif

#ifdef USE_OUT_SRC
	if (IS_OUTSRC_CHIP(priv))
		if(ODMPTR->config_bbrf)
			PRINT_SINGL_ARG("    Phy para Version: ", priv->pshare->PhyVersion, "%d");
#endif

	PRINT_SINGL_ARG("    adaptivity_enable: ", priv->pshare->rf_ft_var.adaptivity_enable, "%d");
	if (priv->pshare->rf_ft_var.adaptivity_enable)
	{
		int adaptivity_status;
		adaptivity_status = check_adaptivity_test(priv);
		PRINT_SINGL_ARG("    adaptivity_status: ", adaptivity_status, "%d");
		PRINT_SINGL_ARG("    bcn_dont_ignore_edcca: ", priv->pshare->rf_ft_var.bcn_dont_ignore_edcca, "%d");
	}

#if defined(SUPPORT_TX_AMSDU) && !defined(SUPPORT_TX_SWQ_AMSDU)
	PRINT_SINGL_ARG("    amsdu_to_pkt:          ", priv->pshare->amsdu_to_pkt, "%lu");
	PRINT_SINGL_ARG("    amsdu_check_pkt:       ", priv->pshare->amsdu_chk_pkt, "%lu");
#endif

#ifdef SW_TX_QUEUE
    PRINT_SINGL_ARG("    swq_enque_pkt:         ", priv->ext_stats.swq_enque_pkt, "%lu");
    PRINT_SINGL_ARG("    swq_xmit_out_pkt:      ", priv->ext_stats.swq_xmit_out_pkt, "%lu");
    PRINT_SINGL_ARG("    swq_real_enque_pkt:    ", priv->ext_stats.swq_real_enque_pkt, "%lu");
    PRINT_SINGL_ARG("    swq_real_deque_pkt:    ", priv->ext_stats.swq_real_deque_pkt, "%lu");
    PRINT_SINGL_ARG("    swq_cnt:               ", priv->pshare->swq_cnt, "%lu");
    PRINT_SINGL_ARG("    swq_residual_drop_pkt: ", priv->ext_stats.swq_residual_drop_pkt, "%lu");
    PRINT_SINGL_ARG("    swq_overflow_drop_pkt: ", priv->ext_stats.swq_overflow_drop_pkt, "%lu");
    PRINT_SINGL_ARG("    txdata_queue_overflow_drop_pkt: ", priv->ext_stats.txdata_queue_overflow_drop_pkt, "%lu");
#ifdef SMP_LOAD_BALANCE_SUPPORT
    PRINT_SINGL_ARG("    swq_timeout_seq:     ", priv->pshare->swq_timeout_seq, "%u");
	PRINT_SINGL_ARG("    skb_xmit_queue_len:    ", rtl_atomic_read(&priv->pshare->skb_xmit_queue_len), "%d");
#endif
#endif
#ifdef SMP_LOAD_BALANCE_SUPPORT
	PRINT_SINGL_ARG("    max_xmit_dsr_skb:    ", priv->pshare->max_xmit_dsr_skb, "%u");
	PRINT_SINGL_ARG("    max_xmit_dsr_time:   ", RTL_JIFFIES_TO_MILISECONDS(priv->pshare->max_xmit_dsr_time), "%u");
	PRINT_SINGL_ARG("    xmit_dsr_redo:       ", priv->pshare->xmit_dsr_redo, "%u");
	PRINT_SINGL_ARG("    direct_xmit_num:       ", priv->pshare->direct_xmit_num, "%u");
#endif
#ifdef SMP_SKB_RECYCLE_OFFLOAD
	PRINT_SINGL_ARG("    skb_recycle_queue:   ", skb_queue_len(priv->pshare->skb_recycle_queue), "%u");
#endif
#ifdef CONFIG_SPECIAL_ENV_TEST
    PRINT_SINGL_ARG("    spec_start_xmit_pkt: ", priv->ext_stats.spec_start_xmit_pkt, "%lu");
    PRINT_SINGL_ARG("    spec_netif_rx_pkt:   ", priv->ext_stats.spec_netif_rx_pkt, "%lu");
    PRINT_SINGL_ARG("    netif_rx_pkt:        ", priv->ext_stats.netif_rx_pkt, "%lu");
#endif
	PRINT_SINGL_ARG("    rx_break_by_time:   ", priv->ext_stats.rx_break_by_time, "%lu");
	PRINT_SINGL_ARG("    rx_break_by_pkt:    ", priv->ext_stats.rx_break_by_pkt, "%lu");

#ifdef RTK_NL80211
	if (IS_ROOT_INTERFACE(priv))
		PRINT_SINGL_ARG("    nl80211_state: ", priv->nl80211_state, "%d");
#endif
	
	PRINT_SINGL_ARG("    null_interrupt_cnt1: ", priv->ext_stats.null_interrupt_cnt1, "%lu");
	PRINT_SINGL_ARG("    null_interrupt_cnt2: ", priv->ext_stats.null_interrupt_cnt2, "%lu");
#ifdef CONFIG_RTL_OFFLOAD_DRIVER
	PRINT_SINGL_ARG("	 dc_bcn_mode: ", priv->pshare->rf_ft_var.dc_bcn_mode , "%d");
	PRINT_SINGL_ARG("	 dc_bcn_mode_current: ",priv->pshare->current_dc_bcn_mode , "%d");
#endif

#if defined(CONFIG_RTL9607C) && defined(CONFIG_RTL_OFFLOAD_DRIVER)
#ifdef DYNAMIC_CPU_ARRANGEMENT
    if (OFFLOAD_ENABLE(priv))
        PRINT_SINGL_ARG("    tx_cpu_turbo: ", priv->tx_cpu_turbo, "%d");
#endif
#endif

    PRINT_SINGL_ARG("    mcast_drop: ", priv->mcast_drop, "%d");
	PRINT_SINGL_ARG("    cnt_icverr_deauth: ", priv->cnt_icverr_deauth, "%d");
	
	RESTORE_INT(flags);

	return pos;
}

#ifdef __ECOS
void rtl8192cd_proc_stats_clear(void *data)
#else
static int rtl8192cd_proc_stats_clear(struct file *file, const char *buffer,
				unsigned long count, void *data)
#endif
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

#ifdef __LINUX_3_7__
	memset(&priv->net_stats, 0, sizeof(struct rtnl_link_stats64));
#else
	memset(&priv->net_stats, 0, sizeof(struct net_device_stats));
#endif
	memset(&priv->ext_stats, 0, sizeof(struct extra_stats));
#ifdef CONFIG_RTK_MESH
#ifdef __LINUX_3_7__
	memset(&priv->mesh_stats, 0, sizeof(struct rtnl_link_stats64));
#else
	memset(&priv->mesh_stats, 0, sizeof(struct net_device_stats));
#endif
#endif // CONFIG_RTK_MESH
#ifdef SMP_LOAD_BALANCE_SUPPORT
	priv->pshare->max_xmit_dsr_skb = 0;
	priv->pshare->max_xmit_dsr_time = 0;
	priv->pshare->xmit_dsr_redo = 0;
	priv->pshare->direct_xmit_num = 0;
#endif
#ifdef __KERNEL__
	return count;
#endif
}

#ifdef CTC_WIFI_DIAG
unsigned char diag_enable[2] = {0, 0};
unsigned int diag_duration[2] = {60, 60};
unsigned int temp_diag_duration[2] = {0, 0};

#ifdef CONFIG_RTL_PROC_NEW
static int ctcwifi_diag_enable_read(struct seq_file *s, void *data)
#else
static int ctcwifi_diag_enable_read(char *buf, char **start, off_t offset, int length, int *eof, void *data)
#endif			
{
#ifdef CONFIG_RTL_PROC_NEW
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)s->private);
#else
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)data);
#endif
	int pos = 0;
#ifndef SMP_SYNC
	unsigned long flags = 0;
#endif
	unsigned int idx = 0;

	if (!priv)
		return pos;
	
	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
		idx = CTCWIFI_2G;
	else
		idx = CTCWIFI_5G;

	PRINT_ONE(diag_enable[idx], "%d", 1);
	return pos;
}

#ifdef __ECOS
int ctcwifi_diag_enable_write(char *tmp, void *data)
#else
int ctcwifi_diag_enable_write(struct file *file, const char *buffer, unsigned long count, void *data)
#endif
{
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)data);
#ifdef __KERNEL__
	char tmp[2];
#endif
	unsigned char en = 0;
	unsigned int idx = 0;

	if (!priv)
		return count;
	
#ifdef __ECOS
	if ((tmp[0] - '0' >=0) && (tmp[0] - '0' <=2))
		en = tmp[0] - '0';
#else
	if (buffer && !copy_from_user(tmp, buffer, 1)) {
		if ((tmp[0] - '0' >=0) && (tmp[0] - '0' <=2))
			en = tmp[0] - '0';
	}
#endif
	if (en > 2) {
		printk("diag_enable can only be 0,1,2! Set it to 1!\n");
		en = 1;
	}

	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
		idx = CTCWIFI_2G;
	else
		idx = CTCWIFI_5G;

	diag_enable[idx] = en;
	if (en) {
		temp_diag_duration[idx] = diag_duration[idx];
		memset(diag_log_record[idx], 0, sizeof(struct ctcwifi_diag_log_record) * DIAG_LOG_REC_MAXNUM);
		diag_log_head[idx] = diag_log_tail[idx] = 0;
		rtk_mod_timer(&(priv->pshare->ctc_diag_tx_bcn_timer), jiffies + RTL_MILISECONDS_TO_JIFFIES(100));
	} else {
		temp_diag_duration[idx] = 0;
		rtk_del_timer(&(priv->pshare->ctc_diag_tx_bcn_timer));
	}

	printk("[write] diag_enable = %d\n", diag_enable[idx]);

#ifndef __ECOS
	return count;
#endif
}

#ifdef CONFIG_RTL_PROC_NEW
static int ctcwifi_diag_duration_read(struct seq_file *s, void *data)
#else
static int ctcwifi_diag_duration_read(char *buf, char **start, off_t offset, int length, int *eof, void *data)
#endif			
{
#ifdef CONFIG_RTL_PROC_NEW
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)s->private);
#else
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)data);
#endif
	int pos = 0;
#ifndef SMP_SYNC
	unsigned long flags = 0;
#endif
	unsigned int idx;

	if (!priv)
		return pos;
	
	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
		idx = CTCWIFI_2G;
	else
		idx = CTCWIFI_5G;

	PRINT_ONE(diag_duration[idx], "%d", 1);

	return pos;
}

#ifdef __ECOS
int ctcwifi_diag_duration_write(char *tmp, void *data)
#else
int ctcwifi_diag_duration_write(struct file *file, const char *buffer, unsigned long count, void *data)
#endif
{
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)data);
#ifdef __KERNEL__
	char tmp[4];
#endif
	unsigned int dur = 0, idx;

	if (!priv)
		return count;
	
#ifdef __ECOS
	dur = _atoi(tmp, 10);
#else
	if (buffer && !copy_from_user(tmp, buffer, 3)) {
		dur = _atoi(tmp, 10);
	}
#endif
	if (dur > 300) {
		printk("diag_duration cannot be set larger than 300! Set it to 300 sec!\n");
		dur = 300;
	}

	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
		idx = CTCWIFI_2G;
	else
		idx = CTCWIFI_5G;

	diag_duration[idx] = dur;
	if (diag_enable[idx])
		temp_diag_duration[idx] = diag_duration[idx];

	printk("[write] diag_duration = %d\n", diag_duration[idx]);

#ifndef __ECOS
	return count;
#endif
}

static __always_inline void ctcwifi_diag_timer_func(unsigned long task_priv)
{
	unsigned int idx;

#if defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8812FE) || defined(CONFIG_WLAN_HAL_8814BE)
	for (idx = 0; idx < 2; idx++)
#else
	for (idx = 0; idx < 1; idx++)
#endif
	{
		if (diag_enable[idx])
		{	
			//printk("<diag> enable = %d, duration = %d, temp_duration = %d\n", diag_enable[idx], diag_duration[idx], temp_diag_duration[idx]);
			if (temp_diag_duration[idx] > 0)
				temp_diag_duration[idx]--;
			else {
				printk("diag_duration (%d sec) expires!\n", diag_duration[idx]);
				diag_enable[idx] = 0;
			}
		}
	}

	rtk_mod_timer(&ctcwifi_diag_timer, jiffies + EXPIRE_TO);
}
DEFINE_TIMER_CALLBACK(ctcwifi_diag_timer_func);

static __always_inline void ctcwifi_diag_tx_bcn_timer_func(unsigned long task_priv)
{
	struct rtl8192cd_priv *priv = GET_ROOT(((struct rtl8192cd_priv *)task_priv));
#ifdef MBSSID
	struct rtl8192cd_priv *priv_vap = NULL;
	int i;
#endif
	u8 next_timer = _FALSE;

	// Root
	if (IS_DRV_OPEN(priv) && !(priv->pmib->miscEntry.func_off)) {
		ctcwifi_diag_log(priv, NULL, 1, "Beacon",
				priv->beaconbuf + WLAN_HDR_A3_LEN,
				priv->tx_beacon_len - WLAN_HDR_A3_LEN);
		next_timer = _TRUE;
	}

#ifdef MBSSID
	if (!priv->pmib->miscEntry.vap_enable)
		goto _exit;

	// VAP
	for (i = 0; i < RTL8192CD_NUM_VWLAN; i++) {
		priv_vap = priv->pvap_priv[i];
		if (IS_DRV_OPEN(priv_vap) &&
				!(priv_vap->pmib->miscEntry.func_off)) {
			ctcwifi_diag_log(priv_vap, NULL, 1, "Beacon",
					priv->beaconbuf + WLAN_HDR_A3_LEN,
					priv->tx_beacon_len - WLAN_HDR_A3_LEN);
			next_timer = _TRUE;
		}
	}
#endif // MBSSID

_exit:
	// Set next timer
	i = (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G) ? CTCWIFI_2G : CTCWIFI_5G;
	if ((temp_diag_duration[i] > 0) && (next_timer))
		rtk_mod_timer(&(priv->pshare->ctc_diag_tx_bcn_timer),
				jiffies + RTL_MILISECONDS_TO_JIFFIES(priv->pmib->dot11StationConfigEntry.dot11BeaconPeriod));
}
DEFINE_TIMER_CALLBACK(ctcwifi_diag_tx_bcn_timer_func);

#ifdef CONFIG_RTL_PROC_NEW
static int ctcwifi_proc_diag_log(struct seq_file *s, void *data)
#else
static int ctcwifi_proc_diag_log(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
#ifdef CONFIG_RTL_PROC_NEW
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)s->private);
#else
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)data);
#endif
	unsigned char tmpbuf[255];
	int pos = 0;
	unsigned int head, tail;
	struct ctcwifi_diag_log_record *record;
	unsigned int idx;

	if (!priv)
		return pos;
	
	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
		idx = CTCWIFI_2G;
	else
		idx = CTCWIFI_5G;
	
	head = diag_log_head[idx];
	tail = diag_log_tail[idx];
	while (tail != head) {
		record = diag_log_record[idx] + tail;
		tail = (tail + 1) & (DIAG_LOG_REC_MAXNUM-1);

		if (record->time_rec[0] == 0) {
			PRINT_ONE(record->msg, "%s", 0);
		}
		else {
			snprintf(tmpbuf, 255, "%s %s %s", record->time_rec, record->ssid, record->msg);
			PRINT_ONE(tmpbuf, "%s", 0);	
		}
	}

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int ctcwifi_proc_association_errors(struct seq_file *s, void *data)
#else
static int ctcwifi_proc_association_errors(char *buf, char **start, off_t offset, int length, int *eof, void *data)
#endif	
{
#ifdef CONFIG_RTL_PROC_NEW
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)s->private);
#else
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)data);
#endif
	unsigned char tmpbuf[128];
	int pos = 0;
	unsigned int head = 0, tail = 0;
	struct ctcwifi_diag_log_record *record, *rec;

	if(priv == NULL) return 0;
	
	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G) {
		record = assoc_error_record[CTCWIFI_2G];
		head = assoc_error_head[CTCWIFI_2G];
		tail = assoc_error_tail[CTCWIFI_2G];
	}
#if defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8812FE) || defined(CONFIG_WLAN_HAL_8814BE)
	else {
		record = assoc_error_record[CTCWIFI_5G];
		head = assoc_error_head[CTCWIFI_5G];
		tail = assoc_error_tail[CTCWIFI_5G];
	}
#endif
	
	while ((tail != head) && record != NULL) {
		rec = record + tail;
		tail = (tail + 1) & (DIAG_LOG_REC_MAXNUM-1);

		snprintf(tmpbuf, 128, "%s %02X%02X%02X%02X%02X%02X %s: %s", 
			rec->time_rec, 
			rec->sta_mac[0], rec->sta_mac[1], rec->sta_mac[2], 
			rec->sta_mac[3], rec->sta_mac[4], rec->sta_mac[5], 
			rec->ssid, 
			rec->msg);
		PRINT_ONE(tmpbuf, "%s", 0);		
	}

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int ctcwifi_proc_stats(struct seq_file *s, void *data)
#else
static int ctcwifi_proc_stats(char *buf, char **start, off_t offset, int length, int *eof, void *data)
#endif			
{
#ifdef CONFIG_RTL_PROC_NEW
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)s->private);
#else
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)data);
#endif
	int pos = 0;
#ifndef SMP_SYNC
	unsigned long flags = 0;
#endif

	if (!priv)
		return 0;

	SAVE_INT_AND_CLI(flags);

	PRINT_SINGL_ARG("    tx_packets:    ", (unsigned long long)priv->net_stats.tx_packets, "%llu");
	PRINT_SINGL_ARG("    rx_packets:    ", (unsigned long long)priv->net_stats.rx_packets, "%llu");
	PRINT_SINGL_ARG("    tx_fails:      ", (unsigned long long)priv->net_stats.tx_errors, "%llu");
	PRINT_SINGL_ARG("    tx_drops:      ", priv->ext_stats.tx_drops, "%lu");

	RESTORE_INT(flags);

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int ctcwifi_proc_channel_occupancy(struct seq_file *s, void *data)
#else
static int ctcwifi_proc_channel_occupancy(char *buf, char **start, off_t offset, int length, int *eof, void *data)
#endif			
{
#ifdef CONFIG_RTL_PROC_NEW
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)s->private);
#else
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)data);
#endif
	unsigned char tmpbuf[30];
	int pos = 0;
#ifndef SMP_SYNC
	unsigned long flags = 0;
#endif

	PRINT_ONE("Channel	band	occupancy", "%s", 1);

	if (!priv) {
		printk("priv is NULL!!\n");
		return 0;
	}

	SAVE_INT_AND_CLI(flags);
#ifdef CH_LOAD_CAL
	unsigned char ch_util_precent_float_num;
	unsigned char ch_util_percent[8];
	ch_util_precent_float_num = (priv->ext_stats.ch_utilization*100*100/255) % 100;
	snprintf(ch_util_percent, 8, "%d.%02d", (priv->ext_stats.ch_utilization*100/255), ch_util_precent_float_num);
	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
		snprintf(tmpbuf, 30, "%-7d	%s	%s", priv->pmib->dot11RFEntry.dot11channel, "2G  ", ch_util_percent);
	else
		snprintf(tmpbuf, 30, "%-7d	%s	%s", priv->pmib->dot11RFEntry.dot11channel, "5G  ", ch_util_percent);
#else
	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
		snprintf(tmpbuf, 30, "%-7d	%s	%s", priv->pmib->dot11RFEntry.dot11channel, "2G  ", "(Not defined)");
	else
		snprintf(tmpbuf, 30, "%-7d	%s	%s", priv->pmib->dot11RFEntry.dot11channel, "5G  ", "(Not defined)");
#endif
	PRINT_ONE(tmpbuf, "%s", 1);

	RESTORE_INT(flags);

	return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int ctcwifi_proc_sta_stats(struct seq_file *s, void *data)
#else
static int ctcwifi_proc_sta_stats(char *buf, char **start, off_t offset, int length, int *eof, void *data)
#endif		
{
#ifdef CONFIG_RTL_PROC_NEW
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)s->private);
#else
	struct rtl8192cd_priv *priv = (struct rtl8192cd_priv *)(*(unsigned long *)data);
#endif
	int len = 0, rc = 1, i;
	int size = 0;
	struct list_head *phead, *plist;
	struct stat_info *pstat;
	unsigned char tmpbuf[50];
	unsigned char snr_avg = 0;
#ifdef TXRETRY_CNT
	unsigned char tx_succ = 0;
#endif
	unsigned long flags = 0;

	PRINT_ONE("STA                SNR  RSSI  TXSUCC", "%s", 1);

		if (!priv) {
			printk("priv is NULL!!\n");
			return 0;
		}

		SAVE_INT_AND_CLI(flags);
		SMP_LOCK_ASOC_LIST(flags);

		phead = &priv->asoc_list;
		if (!(priv->drv_state & DRV_STATE_OPEN) || list_empty(phead))
			goto _ret;

		plist = phead->next;
		while (plist != phead) {
			pstat = list_entry(plist, struct stat_info, asoc_list);

			snr_avg = (pstat->rf_info.RxSNRdB[1]) ? ((pstat->rf_info.RxSNRdB[0] + pstat->rf_info.RxSNRdB[1])/2) : pstat->rf_info.RxSNRdB[0];
#ifdef TXRETRY_CNT
			if (is_support_TxRetryCnt(priv))
				tx_succ = 100 - pstat->cur_tx_retry_cnt;
			else
				tx_succ = 0;

			snprintf(tmpbuf, 50, "%02X%02X%02X%02X%02X%02X  %-3d  %-4u  %-3u", 
				pstat->cmn_info.mac_addr[0], pstat->cmn_info.mac_addr[1], pstat->cmn_info.mac_addr[2], pstat->cmn_info.mac_addr[3], pstat->cmn_info.mac_addr[4], pstat->cmn_info.mac_addr[5], 
				snr_avg, pstat->rssi, tx_succ);
#else
			snprintf(tmpbuf, 50, "%02X%02X%02X%02X%02X%02X  %-3d  %-4u  %s", 
				pstat->cmn_info.mac_addr[0], pstat->cmn_info.mac_addr[1], pstat->cmn_info.mac_addr[2], pstat->cmn_info.mac_addr[3], pstat->cmn_info.mac_addr[4], pstat->cmn_info.mac_addr[5], 
				snr_avg, pstat->rssi, "(Not defined)");
#endif
			PRINT_ONE(tmpbuf, "%s", 1);

			CHECK_LEN;

			plist = plist->next;
		}

_ret:
		SMP_UNLOCK_ASOC_LIST(flags);
		RESTORE_INT(flags);

	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
	{
		PRINT_SINGL_ARG("RXCRCERR-2G        ", (unsigned long long)priv->net_stats.rx_crc_errors, "%-llu");
	}
	else
	{
		PRINT_SINGL_ARG("RXCRCERR-5G        ", (unsigned long long)priv->net_stats.rx_crc_errors, "%-llu");
	}
	return len;
}
#endif

#ifdef DROP_RXPKT
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_drop_rxpkt_rate(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_drop_rxpkt_rate(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	int pos = 0;
	unsigned long flags=0;
	SAVE_INT_AND_CLI(flags);

	printk("priv->pmib->miscEntry.drop_rxpkt_en=%lu\n", priv->pmib->miscEntry.drop_rxpkt_en);
	printk("priv->pmib->miscEntry.drop_rxpkt_rate=%lu\n", priv->pmib->miscEntry.drop_rxpkt_rate);
	printk("priv->pmib->miscEntry.G5_drop_rxpkt_rate=%lu\n", priv->pmib->miscEntry.G5_drop_rxpkt_rate);
	printk("priv->pmib->miscEntry.G5G24_drop_rxpkt_rate=%lu\n", priv->pmib->miscEntry.G5G24_drop_rxpkt_rate);

	RESTORE_INT(flags);

	return pos;
}

static int rtl8192cd_proc_drop_rxpkt_rate_w(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	char tmp[128];

	if (buffer && !copy_from_user(tmp, buffer, 128)) {
#if 0
		unsigned long drop_rxpkt_rate = simple_strtoul(tmp, NULL, 0);
		printk("drop_rx_pkt_rate=%lu\n", drop_rxpkt_rate);
		priv->pmib->miscEntry.drop_rxpkt_rate = drop_rxpkt_rate;
#else
		unsigned long G5_drr, G5G24_drr;
		sscanf(tmp, "%lu %lu", &G5_drr, &G5G24_drr);
		priv->pmib->miscEntry.drop_rxpkt_rate = G5_drr;
		priv->pmib->miscEntry.G5_drop_rxpkt_rate = G5_drr;
		priv->pmib->miscEntry.G5G24_drop_rxpkt_rate= G5G24_drr;
#endif
	}		

	return count;
}

#endif

#ifdef RF_FINETUNE
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_rfft(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_rfft(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
#ifdef SUPPORT_TX_MCAST2UNI
	int i;
	int tmpbuf[64];
#endif

	PRINT_ONE("  RF fine tune variables...", "%s", 1);

	PRINT_SINGL_ARG("    rssi: ", priv->pshare->rf_ft_var.rssi_dump, "%d");
	PRINT_SINGL_ARG("    rxfifoO: ", priv->pshare->rf_ft_var.rxfifoO, "%x");
	PRINT_SINGL_ARG("    raGoDownUpper: ", priv->pshare->rf_ft_var.raGoDownUpper, "%d");
	PRINT_SINGL_ARG("    raGoDown20MLower: ", priv->pshare->rf_ft_var.raGoDown20MLower, "%d");
	PRINT_SINGL_ARG("    raGoDown40MLower: ", priv->pshare->rf_ft_var.raGoDown40MLower, "%d");
	PRINT_SINGL_ARG("    raGoUpUpper: ", priv->pshare->rf_ft_var.raGoUpUpper, "%d");
	PRINT_SINGL_ARG("    raGoUp20MLower: ", priv->pshare->rf_ft_var.raGoUp20MLower, "%d");
	PRINT_SINGL_ARG("    raGoUp40MLower: ", priv->pshare->rf_ft_var.raGoUp40MLower, "%d");
	PRINT_SINGL_ARG("    dig_enable: ", priv->pshare->rf_ft_var.dig_enable, "%d");
	PRINT_SINGL_ARG("    digGoLowerLevel: ", priv->pshare->rf_ft_var.digGoLowerLevel, "%d");
	PRINT_SINGL_ARG("    digGoUpperLevel: ", priv->pshare->rf_ft_var.digGoUpperLevel, "%d");
	PRINT_SINGL_ARG("    rssiTx20MUpper: ", priv->pshare->rf_ft_var.rssiTx20MUpper, "%d");
	PRINT_SINGL_ARG("    rssiTx20MLower: ", priv->pshare->rf_ft_var.rssiTx20MLower, "%d");
	PRINT_SINGL_ARG("    rssi_expire_to: ", priv->pshare->rf_ft_var.rssi_expire_to, "%d");

	PRINT_SINGL_ARG("    cck_pwr_max: ", priv->pshare->rf_ft_var.cck_pwr_max, "%d");
	PRINT_SINGL_ARG("    cck_tx_pathB: ", priv->pshare->rf_ft_var.cck_tx_pathB, "%d");

	PRINT_SINGL_ARG("    tx_pwr_ctrl: ", priv->pshare->rf_ft_var.tx_pwr_ctrl, "%d");

	// 11n ap AES debug
	PRINT_SINGL_ARG("    aes_check_th: ", priv->pshare->rf_ft_var.aes_check_th, "%d KB");

	// Tx power tracking
	PRINT_SINGL_ARG("    tpt_period: ", priv->pshare->rf_ft_var.tpt_period, "%d");
#ifdef CONFIG_RF_DPK_SETTING_SUPPORT
	if (!(priv->pshare->WlanSupportAbility & WLAN_RF_DPK_SETTING_SUPPORT)) {
		PRINT_SINGL_ARG("	 dpk_period: ", priv->pshare->rf_ft_var.dpk_period, "%d");
	}
#endif	
	// TXOP enlarge
	PRINT_SINGL_ARG("    txop_enlarge_upper: ", priv->pshare->rf_ft_var.txop_enlarge_upper, "%d");
	PRINT_SINGL_ARG("    txop_enlarge_lower: ", priv->pshare->rf_ft_var.txop_enlarge_lower, "%d");

	// 2.3G support
	PRINT_SINGL_ARG("    frq_2_3G: ", priv->pshare->rf_ft_var.use_frq_2_3G, "%d");

	//Support IP multicast->unicast
#ifdef SUPPORT_TX_MCAST2UNI
	PRINT_SINGL_ARG("    mc2u_disable: ", priv->pshare->rf_ft_var.mc2u_disable, "%d");
#if defined(USER_RESERVED_MAC_TO_DISABLE_M2U)
	if(!priv->pshare->rf_ft_var.mc2u_disable)
	{
	
		PRINT_SINGL_ARG("	 mc2u_reserved_mac_num: ", priv->pshare->rf_ft_var.mc2u_reserved_mac_num, "%d");
		for (i=0; i< priv->pshare->rf_ft_var.mc2u_reserved_mac_num; i++) {
			sprntf(tmpbuf, "    mc2u_reserved_mac[%d]: ", i);
				PRINT_ARRAY_ARG(tmpbuf,	priv->pshare->rf_ft_var.mc2u_reserved_mac[i].macAddr, "%02x", 6);
		}
	}
#endif
	PRINT_SINGL_ARG("    mc2u_drop_unknown: ", priv->pshare->rf_ft_var.mc2u_drop_unknown, "%d");
	PRINT_SINGL_ARG("    mc2u_flood_ctrl: ", priv->pshare->rf_ft_var.mc2u_flood_ctrl, "%d");
	if(priv->pshare->rf_ft_var.mc2u_flood_ctrl)
	{
		PRINT_SINGL_ARG("    mc2u_flood_mac_num: ", priv->pshare->rf_ft_var.mc2u_flood_mac_num, "%d");
		for (i=0; i< priv->pshare->rf_ft_var.mc2u_flood_mac_num; i++) {
			snprintf(tmpbuf, sizeof(tmpbuf), "    mc2u_flood_mac[%d]: ", i);
			PRINT_ARRAY_ARG(tmpbuf,	priv->pshare->rf_ft_var.mc2u_flood_mac[i].macAddr, "%02x", 6);
		}
	}
#endif

#ifdef	HIGH_POWER_EXT_PA
	PRINT_SINGL_ARG("    use_ext_pa: ", priv->pshare->rf_ft_var.use_ext_pa, "%d");
#endif
#ifdef HIGH_POWER_EXT_LNA
	PRINT_SINGL_ARG("    use_ext_lna: ", priv->pshare->rf_ft_var.use_ext_lna, "%d");
#endif
	PRINT_SINGL_ARG("    NDSi_support: ", priv->pshare->rf_ft_var.NDSi_support, "%d");
	PRINT_SINGL_ARG("    EDCCA threshold: ", priv->pshare->rf_ft_var.edcca_thd, "%d");
	PRINT_SINGL_ARG("    1rcca: ", priv->pshare->rf_ft_var.one_path_cca, "%d");

	return pos;
}
#endif


#ifdef GBWC
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_gbwc(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_gbwc(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;

	PRINT_ONE("  miscGBWC...", "%s", 1);

	PRINT_SINGL_ARG("    GBWCMode: ", priv->pmib->gbwcEntry.GBWCMode, "%d");
	PRINT_SINGL_ARG("    GBWCThrd_tx: ", priv->pmib->gbwcEntry.GBWCThrd_tx, "%d kbps");
	PRINT_SINGL_ARG("    GBWCThrd_rx: ", priv->pmib->gbwcEntry.GBWCThrd_rx, "%d kbps");
	PRINT_ONE("    Address List:", "%s", 1);
	for (i=0; i<priv->pmib->gbwcEntry.GBWCNum; i++) {
		PRINT_ARRAY_ARG("      ", priv->pmib->gbwcEntry.GBWCAddr[i], "%02x", MACADDRLEN);
	}
	PRINT_SINGL_ARG("    GBWC_timer_alive: ", priv->GBWC_timer_alive, "%u");

	return pos;
}
#endif

#ifdef SBWC
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_sbwc(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_sbwc(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;

	PRINT_ONE("  miscSBWC...", "%s", 1);

	for ( i = 0 ; i != priv->pmib->sbwcEntry.count ; ++i )
	{
		PRINT_ARRAY_ARG("    hwaddr: ",	priv->pmib->sbwcEntry.entry[i].mac, "%02x", MACADDRLEN);
		PRINT_SINGL_ARG("    tx_lmt: ", priv->pmib->sbwcEntry.entry[i].tx_lmt, "%d kbps");	
		PRINT_SINGL_ARG("    rx_lmt: ", priv->pmib->sbwcEntry.entry[i].rx_lmt, "%d kbps");
	}

	return pos;
}
#endif

#ifdef CONFIG_RTL_KERNEL_MIPS16_WLAN
__NOMIPS16
#endif
#ifdef __ECOS
void rtl8192cd_proc_led(int flag, void *data)
#else
static int rtl8192cd_proc_led(struct file *file, const char *buffer,
				unsigned long count, void *data)
#endif
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
#ifdef __KERNEL__
	char tmpbuf[100];
	int flag;
#endif

#ifdef __ECOS
		if (flag == 0) // disable
			control_wireless_led(priv, 0);
		else if (flag == 1) // enable
			control_wireless_led(priv, 1);
		else if (flag == 2) // restore
			control_wireless_led(priv, 2);
#if defined(CONFIG_CUTE_MAHJONG) || defined(CONFIG_RTL_ULINKER)
		else if (flag == 3) // restore hw 
			control_wireless_led(priv, 3);
		else if (flag == 4) // sw led
			control_wireless_led(priv, 4);
		#endif
		else
			ecos_pr_fun("flag [%d] not supported!\n", flag);
#else
	if (buffer && !copy_from_user(tmpbuf, buffer, count)) {
		if (sscanf(tmpbuf, "%99d", &flag) != 1)
			printk("Invalid input \"%s\" for proc led\n", tmpbuf);
		else if (flag == 0) // disable
			control_wireless_led(priv, 0);
		else if (flag == 1) // enable
			control_wireless_led(priv, 1);
		else if (flag == 2) // restore
			control_wireless_led(priv, 2);
		else
			printk("flag [%d] not supported!\n", flag);
    }
	return count;
#endif
}


#ifdef RTL_MANUAL_EDCA
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_mib_edca(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_mib_edca(char *buf, char **start, off_t offset,
                     int length, int *eof, void *data)
#endif                     
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	char *queue[] = {"", "BK", "BE", "VI", "VO"};

	PRINT_SINGL_ARG("  Manually config EDCA : ", priv->pmib->dot11QosEntry.ManualEDCA, "%d");
	PRINT_ONE("  EDCA for AP...", "%s", 1);
	PRINT_SINGL_ARG("  [BE]slot number (AIFS): ", priv->pmib->dot11QosEntry.AP_manualEDCA[BE].AIFSN, "%d");
	PRINT_SINGL_ARG("      Maximal contention window period: ", priv->pmib->dot11QosEntry.AP_manualEDCA[BE].ECWmax, "%d");
	PRINT_SINGL_ARG("      Minimal contention window period: ", priv->pmib->dot11QosEntry.AP_manualEDCA[BE].ECWmin, "%d");
	PRINT_SINGL_ARG("      TXOP limit: ", priv->pmib->dot11QosEntry.AP_manualEDCA[BE].TXOPlimit, "%d");
	PRINT_SINGL_ARG("  [BK]slot number (AIFS): ", priv->pmib->dot11QosEntry.AP_manualEDCA[BK].AIFSN, "%d");
	PRINT_SINGL_ARG("      Maximal contention window period: ", priv->pmib->dot11QosEntry.AP_manualEDCA[BK].ECWmax, "%d");
	PRINT_SINGL_ARG("      Minimal contention window period: ", priv->pmib->dot11QosEntry.AP_manualEDCA[BK].ECWmin, "%d");
	PRINT_SINGL_ARG("      TXOP limit: ", priv->pmib->dot11QosEntry.AP_manualEDCA[BK].TXOPlimit, "%d");
	PRINT_SINGL_ARG("  [VI]slot number (AIFS)= ", priv->pmib->dot11QosEntry.AP_manualEDCA[VI].AIFSN, "%d");
	PRINT_SINGL_ARG("      Maximal contention window period: ", priv->pmib->dot11QosEntry.AP_manualEDCA[VI].ECWmax, "%d");
	PRINT_SINGL_ARG("      Minimal contention window period: ", priv->pmib->dot11QosEntry.AP_manualEDCA[VI].ECWmin, "%d");
	PRINT_SINGL_ARG("      TXOP limit: ", priv->pmib->dot11QosEntry.AP_manualEDCA[VI].TXOPlimit, "%d");
	PRINT_SINGL_ARG("  [VO]slot number (AIFS): ", priv->pmib->dot11QosEntry.AP_manualEDCA[VO].AIFSN, "%d");
	PRINT_SINGL_ARG("      Maximal contention window period: ", priv->pmib->dot11QosEntry.AP_manualEDCA[VO].ECWmax, "%d");
	PRINT_SINGL_ARG("      Minimal contention window period: ", priv->pmib->dot11QosEntry.AP_manualEDCA[VO].ECWmin, "%d");
	PRINT_SINGL_ARG("      TXOP limit: ", priv->pmib->dot11QosEntry.AP_manualEDCA[VO].TXOPlimit, "%d");
	PRINT_ONE("  EDCA for Wireless client...", "%s", 1);
	PRINT_SINGL_ARG("  [BE]ACM: ", priv->pmib->dot11QosEntry.STA_manualEDCA[BE].ACM, "%d");
	PRINT_SINGL_ARG("      slot number (AIFS): ", priv->pmib->dot11QosEntry.STA_manualEDCA[BE].AIFSN, "%d");
	PRINT_SINGL_ARG("      Maximal contention window period: ", priv->pmib->dot11QosEntry.STA_manualEDCA[BE].ECWmax, "%d");
	PRINT_SINGL_ARG("      Minimal contention window period: ", priv->pmib->dot11QosEntry.STA_manualEDCA[BE].ECWmin, "%d");
	PRINT_SINGL_ARG("      TXOP limit: ", priv->pmib->dot11QosEntry.STA_manualEDCA[BE].TXOPlimit, "%d");
	PRINT_SINGL_ARG("  [BK]ACM:", priv->pmib->dot11QosEntry.STA_manualEDCA[BK].ACM, "%d");
	PRINT_SINGL_ARG("      slot number (AIFS): ", priv->pmib->dot11QosEntry.STA_manualEDCA[BK].AIFSN, "%d");
	PRINT_SINGL_ARG("      Maximal contention window period: ", priv->pmib->dot11QosEntry.STA_manualEDCA[BK].ECWmax, "%d");
	PRINT_SINGL_ARG("      Minimal contention window period: ", priv->pmib->dot11QosEntry.STA_manualEDCA[BK].ECWmin, "%d");
	PRINT_SINGL_ARG("      TXOP limit: ", priv->pmib->dot11QosEntry.STA_manualEDCA[BK].TXOPlimit, "%d");
	PRINT_SINGL_ARG("  [VI]ACM: ", priv->pmib->dot11QosEntry.STA_manualEDCA[VI].ACM, "%d");
	PRINT_SINGL_ARG("      slot number (AIFS): ", priv->pmib->dot11QosEntry.STA_manualEDCA[VI].AIFSN, "%d");
	PRINT_SINGL_ARG("      Maximal contention window period: ", priv->pmib->dot11QosEntry.STA_manualEDCA[VI].ECWmax, "%d");
	PRINT_SINGL_ARG("      Minimal contention window period: ", priv->pmib->dot11QosEntry.STA_manualEDCA[VI].ECWmin, "%d");
	PRINT_SINGL_ARG("      TXOP limit:", priv->pmib->dot11QosEntry.STA_manualEDCA[VI].TXOPlimit, "%d");
	PRINT_SINGL_ARG("  [VO]ACM: ", priv->pmib->dot11QosEntry.STA_manualEDCA[VO].ACM, "%d");
	PRINT_SINGL_ARG("      slot number (AIFS): ", priv->pmib->dot11QosEntry.STA_manualEDCA[VO].AIFSN, "%d");
	PRINT_SINGL_ARG("      Maximal contention window period: ", priv->pmib->dot11QosEntry.STA_manualEDCA[VO].ECWmax, "%d");
	PRINT_SINGL_ARG("      Minimal contention window period: ", priv->pmib->dot11QosEntry.STA_manualEDCA[VO].ECWmin, "%d");
	PRINT_SINGL_ARG("      TXOP limit: ", priv->pmib->dot11QosEntry.STA_manualEDCA[VO].TXOPlimit, "%d");

	PRINT_SINGL_ARG("      TID0 mapping: ", queue[priv->pmib->dot11QosEntry.TID_mapping[0]], "%s");
	PRINT_SINGL_ARG("      TID1 mapping: ", queue[priv->pmib->dot11QosEntry.TID_mapping[1]], "%s");
	PRINT_SINGL_ARG("      TID2 mapping: ", queue[priv->pmib->dot11QosEntry.TID_mapping[2]], "%s");
	PRINT_SINGL_ARG("      TID3 mapping: ", queue[priv->pmib->dot11QosEntry.TID_mapping[3]], "%s");
	PRINT_SINGL_ARG("      TID4 mapping: ", queue[priv->pmib->dot11QosEntry.TID_mapping[4]], "%s");
	PRINT_SINGL_ARG("      TID5 mapping: ", queue[priv->pmib->dot11QosEntry.TID_mapping[5]], "%s");
	PRINT_SINGL_ARG("      TID6 mapping: ", queue[priv->pmib->dot11QosEntry.TID_mapping[6]], "%s");
	PRINT_SINGL_ARG("      TID7 mapping: ", queue[priv->pmib->dot11QosEntry.TID_mapping[7]], "%s");

	return pos;
}
#endif //RTL_MANUAL_EDCA


#ifdef TLN_STATS
#ifdef CONFIG_RTL_PROC_NEW
static int proc_wifi_conn_stats(struct seq_file *s, void *data)
#else
static int proc_wifi_conn_stats(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  Wifi Connection Stats...", "%s", 1);

	PRINT_SINGL_ARG("    Time Interval: ", priv->pshare->rf_ft_var.stats_time_interval, "%d");
	PRINT_SINGL_ARG("    Connected Clients: ", priv->wifi_stats.connected_sta, "%d");
	PRINT_SINGL_ARG("    MAX Clients: ", priv->wifi_stats.max_sta, "%d");
	PRINT_SINGL_ARG("    MAX Clients Timestamp: ", priv->wifi_stats.max_sta_timestamp, "%d");
	PRINT_SINGL_ARG("    Rejected clients: ", priv->wifi_stats.rejected_sta, "%d");

	return pos;
}


static int proc_wifi_conn_stats_clear(struct file *file, const char *buffer,
				unsigned long count, void *data)
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	memset(&priv->wifi_stats, 0, sizeof(struct tln_wifi_stats));
	return count;
}

#ifdef CONFIG_RTL_PROC_NEW
static int proc_ext_wifi_conn_stats(struct seq_file *s, void *data)
#else
static int proc_ext_wifi_conn_stats(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;

	PRINT_ONE("  Extended WiFi Connection Stats...", "%s", 1);

	PRINT_ONE("  Reject Reason/Status: Reject Count", "%s", 1);
	PRINT_ONE(" =====================================================", "%s", 1);
	PRINT_SINGL_ARG("    Unspecified reason: ", priv->ext_wifi_stats.rson_UNSPECIFIED_1, "%d");
	PRINT_SINGL_ARG("    Previous auth no longer valid: ", priv->ext_wifi_stats.rson_AUTH_INVALID_2, "%d");
	PRINT_SINGL_ARG("    Deauth because of leaving (or has left): ", priv->ext_wifi_stats.rson_DEAUTH_STA_LEAVING_3, "%d");
	PRINT_SINGL_ARG("    Disassoc due to inactivity: ", priv->ext_wifi_stats.rson_INACTIVITY_4, "%d");
	PRINT_SINGL_ARG("    Disassoc because AP cannot handle: ", priv->ext_wifi_stats.rson_RESOURCE_INSUFFICIENT_5, "%d");
	PRINT_SINGL_ARG("    Class 2 frame from non-auth STA: ", priv->ext_wifi_stats.rson_UNAUTH_CLS2FRAME_6, "%d");
	PRINT_SINGL_ARG("    Class 3 frame from non-assoc STA: ", priv->ext_wifi_stats.rson_UNAUTH_CLS3FRAME_7, "%d");
	PRINT_SINGL_ARG("    Disassoc because leaving (or has left): ", priv->ext_wifi_stats.rson_DISASSOC_STA_LEAVING_8, "%d");
	PRINT_SINGL_ARG("    STA request (re)assoc did not auth: ", priv->ext_wifi_stats.rson_ASSOC_BEFORE_AUTH_9, "%d");
	PRINT_SINGL_ARG("    Invalid IE: ", priv->ext_wifi_stats.rson_INVALID_IE_13, "%d");
	PRINT_SINGL_ARG("    MIC failure: ", priv->ext_wifi_stats.rson_MIC_FAILURE_14, "%d");
	PRINT_SINGL_ARG("    4-Way Handshake timeout: ", priv->ext_wifi_stats.rson_4WAY_TIMEOUT_15, "%d");
	PRINT_SINGL_ARG("    Group Key Handshake timeout: ", priv->ext_wifi_stats.rson_GROUP_KEY_TIMEOUT_16, "%d");
	PRINT_SINGL_ARG("    IE in 4-Way Handshake different: ", priv->ext_wifi_stats.rson_DIFF_IE_17, "%d");
	PRINT_SINGL_ARG("    Invalid group cipher: ", priv->ext_wifi_stats.rson_MCAST_CIPHER_INVALID_18, "%d");
	PRINT_SINGL_ARG("    Invalid pairwise cipher: ", priv->ext_wifi_stats.rson_UCAST_CIPHER_INVALID_19, "%d");
	PRINT_SINGL_ARG("    Invalid AKMP: ", priv->ext_wifi_stats.rson_AKMP_INVALID_20, "%d");
	PRINT_SINGL_ARG("    Unsupported RSNIE version: ", priv->ext_wifi_stats.rson_UNSUPPORT_RSNIE_VER_21, "%d");
	PRINT_SINGL_ARG("    Invalid RSNIE capabilities: ", priv->ext_wifi_stats.rson_RSNIE_CAP_INVALID_22, "%d");
	PRINT_SINGL_ARG("    IEEE 802.1X auth failed: ", priv->ext_wifi_stats.rson_802_1X_AUTH_FAIL_23, "%d");
	PRINT_SINGL_ARG("    Reason out of scope of the device: ", priv->ext_wifi_stats.rson_OUT_OF_SCOPE, "%d");

	PRINT_SINGL_ARG("    Unspecified failure: ", priv->ext_wifi_stats.status_FAILURE_1, "%d");
	PRINT_SINGL_ARG("    Cannot support all capabilities: ", priv->ext_wifi_stats.status_CAP_FAIL_10, "%d");
	PRINT_SINGL_ARG("    Reassoc denied due to cannot confirm assoc exists: ", priv->ext_wifi_stats.status_NO_ASSOC_11, "%d");
	PRINT_SINGL_ARG("    Assoc denied due to reason beyond: ", priv->ext_wifi_stats.status_OTHER_12, "%d");
	PRINT_SINGL_ARG("    Not support specified auth alg: ", priv->ext_wifi_stats.status_NOT_SUPPORT_ALG_13, "%d");
	PRINT_SINGL_ARG("    Auth seq out of expected: ", priv->ext_wifi_stats.status_OUT_OF_AUTH_SEQ_14, "%d");
	PRINT_SINGL_ARG("    Challenge failure: ", priv->ext_wifi_stats.status_CHALLENGE_FAIL_15, "%d");
	PRINT_SINGL_ARG("    Auth timeout: ", priv->ext_wifi_stats.status_AUTH_TIMEOUT_16, "%d");
	PRINT_SINGL_ARG("    Denied because AP cannot handle: ", priv->ext_wifi_stats.status_RESOURCE_INSUFFICIENT_17, "%d");
	PRINT_SINGL_ARG("    Denied because STA not support all rates: ", priv->ext_wifi_stats.status_RATE_FAIL_18, "%d");
	PRINT_SINGL_ARG("    Status out of scope of the device: ", priv->ext_wifi_stats.status_OUT_OF_SCOPE, "%d");

	return pos;
}


static int proc_ext_wifi_conn_stats_clear(struct file *file, const char *buffer,
				unsigned long count, void *data)
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	memset(&priv->ext_wifi_stats, 0, sizeof(struct tln_ext_wifi_stats));
	return count;
}
#endif


#if defined(RTLWIFINIC_GPIO_CONTROL)
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_gpio_ctrl_read(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_gpio_ctrl_read(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	int i;
	char tmp[16];

	for (i=0; i<12; i++) {
		if (priv->pshare->phw->GPIO_dir[i] == 0x01) {
			snprintf(tmp, sizeof(tmp), "GPIO%d %d", i, RTLWIFINIC_GPIO_read_proc(priv, i));
			PRINT_ONE(tmp, "%s", 1);
		}
	}

	return pos;
}

#ifdef CONFIG_RTL_ASUSWRT
void asus_led_ctrl_write(struct net_device *dev, unsigned int pin, unsigned int val)
{
	RTLWIFINIC_GPIO_init_priv(GET_DEV_PRIV(dev));
	RTLWIFINIC_GPIO_config(pin, 0x10);
	RTLWIFINIC_GPIO_write(pin, val);
}

unsigned char asus_led_get_rssi(struct net_device *dev)
{
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	return priv->pmib->dot11Bss.rssi;
}
#endif

#ifdef __ECOS
int rtl8192cd_proc_gpio_ctrl_write(char *command, int gpio_num, char *action, void *data)
#else
static int rtl8192cd_proc_gpio_ctrl_write(struct file *file, const char *buffer,
				unsigned long count, void *data)
#endif
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int gpio_num_upper=11;
#ifdef __ECOS
	int direction, value, count=0;
#else
	char tmp[32], command[8], action[4];
	unsigned int num, gpio_num=-1, direction, value;
#endif

#ifdef __ECOS
	ecos_pr_fun("Command: [%s] gpio: [%d] action: [%s]\n", command, gpio_num, action);
#else
	if (buffer && !copy_from_user(tmp, buffer, 32)) {
		num = sscanf(tmp, "%8s %d %4s", command, &gpio_num, action);

		if (num != 3) {
			panic_printk("Invalid gpio parameter! Failed!\n");
			return num;
		}
	}
	panic_printk("Command: [%s] gpio: [%d] action: [%s]\n", command, gpio_num, action);
#endif

	if (!memcmp(command, "config", 6)) {
		if (!memcmp(action, "r", 1))
			direction = 0x01;
		else if (!memcmp(action, "w", 1))
			direction = 0x10;
		else {
#ifdef __ECOS
			ecos_pr_fun("Action not supported!\n");
#else
			panic_printk("Action not supported!\n");
#endif
			return count;
		}

#ifdef CONFIG_WLAN_HAL_8814AE
		if(GET_CHIP_VER(priv) == VERSION_8814A)
			gpio_num_upper = 15;
#endif

		if ((gpio_num >= 0) && (gpio_num <= gpio_num_upper))
			priv->pshare->phw->GPIO_dir[gpio_num] = direction;
		else {
#ifdef __ECOS
			ecos_pr_fun("GPIO pin not supported!\n");
#else
			panic_printk("GPIO pin not supported!\n");
#endif
			return count;
		}

		RTLWIFINIC_GPIO_config_proc(priv, gpio_num, direction);
	}
	else if (!memcmp(command, "set", 3)) {
		if (!memcmp(action, "0", 1))
			value = 0;
		else if (!memcmp(action, "1", 1))
			value = 1;
		else {
#ifdef __ECOS
			ecos_pr_fun("Action not supported!\n");
#else
			panic_printk("Action not supported!\n");
#endif
			return count;
		}

		if (((gpio_num >= 0) && (gpio_num <= gpio_num_upper)) && (priv->pshare->phw->GPIO_dir[gpio_num] == 0x10))
			RTLWIFINIC_GPIO_write_proc(priv, gpio_num, value);	
		else {
#ifdef __ECOS
			ecos_pr_fun("GPIO pin not supported!\n");
#else
			panic_printk("GPIO pin not supported!\n");
#endif
			return count;
		}
	}
	else {
#ifdef __ECOS
		ecos_pr_fun("Command not supported!\n");
#else
		panic_printk("Command not supported!\n");
#endif
	}

	return count;
}
#endif

#ifdef RTK_NL80211//openwrt_psd
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_psd_scan_read(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_psd_scan_read(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{

	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	int pos=0;
	int i, idx=0;
	char tmp[200];
	int freq=0, p=0, dBm=0;
	
	memset(tmp, 0x0, sizeof(tmp));
	
	idx += snprintf(tmp+idx, sizeof(tmp)-idx, "CH %d ", priv->rtk->psd_fft_info[0]);
	idx += snprintf(tmp+idx, sizeof(tmp)-idx, "BW %dM ", priv->rtk->psd_fft_info[1]);
	idx += snprintf(tmp+idx, sizeof(tmp)-idx, "PTS %d ", priv->rtk->psd_fft_info[2]);
	PRINT_SINGL_ARG("PSD SCAN: ", tmp, "%s");
	
	idx=0;
	memset(tmp, 0x0, sizeof(tmp));
	if(priv->rtk->psd_fft_info[0]<14)
		freq = 2412+5*(priv->rtk->psd_fft_info[0]-1);
	else
		freq = 5180+5*(priv->rtk->psd_fft_info[0]-36);
	
	idx += snprintf(tmp+idx, sizeof(tmp)-idx, "centeral %dMHz", freq);
	idx += snprintf(tmp+idx, sizeof(tmp)-idx, " from %dMHz to", (freq-20));
	idx += snprintf(tmp+idx, sizeof(tmp)-idx, " %dMHz (per unit is 1.25MHz)", (freq+20));
	PRINT_SINGL_ARG("Channel Frequency: ", tmp, "%s");
	
	idx=0;
	memset(tmp, 0x0, sizeof(tmp));
	
	//fix bandwidth=40 psd_pts=128
#if 1
	for(i=0;i<(128/4);i++)
	{
		p = priv->rtk->psd_fft_info[16+i*4+0]+priv->rtk->psd_fft_info[16+i*4+1]+
			priv->rtk->psd_fft_info[16+i*4+2]+priv->rtk->psd_fft_info[16+i*4+3];
		
		idx += snprintf(tmp+idx, sizeof(tmp)-idx, "%4x ", p);

		if((i+1)%4 == 0)
		{
			PRINT_ONE(tmp, "%s", 1);
			memset(tmp, 0x0, sizeof(tmp));
			idx=0;
		}
	}
#else
	int size=priv->rtk->psd_fft_info[2]+16;
	for (i=16; i<size; i++) 
	{
		idx += snprintf(tmp+idx, sizeof(tmp)-idx, "%3x ", priv->rtk->psd_fft_info[i]);
		if((i+1)%16 == 0)
		{
			PRINT_ONE(tmp, "%s", 1);
			memset(tmp, 0x0, 64);
			idx=0;
		}
	}
#endif
	return pos;
}


static int rtl8192cd_proc_psd_scan_write(struct file *file, const char *buffer,
				unsigned long count, void *data)
{

#ifdef CONFIG_RTL_PROC_NEW
	struct net_device *dev = pde_data(file_inode(file));
#else
	struct net_device *dev = (struct net_device *)data;
#endif
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

	char tmp[32];
	unsigned int num, chnl, bw=40, pts=128;//fix bandwidth=40 scan_pts=128

	if (buffer && !copy_from_user(tmp, buffer, 32)) {
		//num = sscanf(tmp, "%d", &chnl, &bw, &pts);
		num = sscanf(tmp, "%d", &chnl);
		if (num != 1) {
			panic_printk("Invalid psd scan parameter! Failed!\n");
			return count;
		}
	}
	panic_printk("Channel: [%d] Bandwidth: [%d] PSD_PTS: [%d]\n", chnl, bw, pts);

	priv->rtk->psd_chnl = chnl;
	priv->rtk->psd_bw = bw;
	priv->rtk->psd_pts= pts;

	return count;
}
#endif

#ifdef CONFIG_IEEE80211R
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ft_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ft_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0, i;
	struct list_head *phead, *plist;
	struct r0_key_holder *r0kh;
	struct r1_key_holder *r1kh;
	unsigned long flags = 0;

	if (!IS_DRV_OPEN(priv)) {
		PRINT_ONE("Interface is not in OPEN state", "%s", 1);
		return pos;
	}
	
	if (!FT_ENABLE) {
	    PRINT_ONE("11R is not enabled", "%s", 1);
		return pos;
	}
	
	PRINT_ONE("  Fast BSS Transition Info...", "%s", 1);
	PRINT_ONE("    R0KHs:", "%s", 1);
	PRINT_ONE("    ==================================", "%s", 1);
	i = 0;
	
	SAVE_INT_AND_CLI(flags);
	SMP_LOCK_FT_R0KH(flags);
	phead = &priv->r0kh;
	plist = phead->next;
	while (plist && (plist != phead)) {
		r0kh = list_entry(plist, struct r0_key_holder, list);
		plist = plist->next;
		PRINT_SINGL_ARG("    + r0kh ", i++, "%d");
		PRINT_ARRAY_ARG("      sta_mac:   ", r0kh->sta_addr, "%02x", MACADDRLEN);
		PRINT_ARRAY_ARG("      pmk_r0:    ", r0kh->pmk_r0, "%02x", PMK_LEN);
		PRINT_ARRAY_ARG("      pmk_r0_name: ", r0kh->pmk_r0_name, "%02x", PMKID_LEN);
		PRINT_SINGL_ARG("      expire_to: ", r0kh->key_expire_to, "%d");
	}
	SMP_UNLOCK_FT_R0KH(flags);
	PRINT_ONE("", "%s", 1);
	PRINT_ONE("    R1KHs:", "%s", 1);
	PRINT_ONE("    ==================================", "%s", 1);
	i = 0;
	SMP_LOCK_FT_R1KH(flags);
	phead = &priv->r1kh;
	plist = phead->next;
	while (plist && (plist != phead)) {
		r1kh = list_entry(plist, struct r1_key_holder, list);
		plist = plist->next;
		PRINT_SINGL_ARG("    + r1kh ", i++, "%d");
		PRINT_ARRAY_ARG("      sta_mac:   ", r1kh->sta_addr, "%02x", MACADDRLEN);
		PRINT_ARRAY_ARG("      r1kh_id:   ", r1kh->r1kh_id, "%02x", MACADDRLEN);
		PRINT_SINGL_ARG("      r0kh_id:   ", r1kh->r0kh_id, "%s");
		PRINT_ARRAY_ARG("      pmk_r1:    ", r1kh->pmk_r1, "%02x", PMK_LEN);
		PRINT_ARRAY_ARG("      pmk_r1_name: ", r1kh->pmk_r1_name, "%02x", PMKID_LEN);
		PRINT_ARRAY_ARG("      pmk_r0_name: ", r1kh->pmk_r0_name, "%02x", PMKID_LEN);
		PRINT_SINGL_ARG("      pairwise:  ", r1kh->pairwise, "%d");
	}
	SMP_UNLOCK_FT_R1KH(flags);
	RESTORE_INT(flags);

	return pos;
}
#endif


#ifdef USE_OUT_SRC
static char *phydm_msg = NULL;
#define PHYDM_MSG_LEN	80*24

#ifdef CONFIG_RTL_PROC_NEW
static int proc_get_phydm_cmd(struct seq_file *s, void *v)
#else
static int proc_get_phydm_cmd(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
    int len = 0;
    int i;
    u32 *ptr;
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    off_t begin = 0;
    off_t pos = 0;
    int size = 0;
#endif
    struct net_device *netdev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(netdev);
#ifndef CONFIG_RTL_PROC_NEW
    if (offset != 0)
        return offset; //to prevent this function is called over once
#endif

    if (GET_CHIP_VER(priv) < VERSION_8188E) {
        panic_printk("RTL8192C and RTL8192D don't support this cmd\n");
        return -EFAULT;
    }

#ifdef CONFIG_RTL_PROC_NEW
#if (PHYDM_LA_MODE_SUPPORT == 1)
    struct rt_adcsmp *smp = &(ODMPTR->adcsmp);
    struct rt_adcsmp_string *buf = &smp->adc_smp_buf;

    //buf->octet, buf->buffer_size
    if ((ODMPTR->support_ic_type & PHYDM_IC_SUPPORT_LA_MODE) && !(smp->is_la_print)) {
		char tmp[100];
    	PRINT_ONE(smp->la_trig_mode, "[BB Setting] trig_mode = ((%d))," ,0);
    	PRINT_ONE(smp->la_dbg_port, " dbg_port = ((0x%x))," ,0);
    	PRINT_ONE(smp->la_trigger_edge, " Trig_Edge = ((%d))," ,0);
    	PRINT_ONE(smp->la_smp_rate, " smp_rate = ((%d))," ,0);
    	PRINT_ONE(smp->la_trig_sig_sel, " Trig_Sel = ((0x%x))," ,0);
    	PRINT_ONE(smp->la_dma_type, " Dma_type = ((%d))," ,1);
		sprintf(tmp,"%d %d %d %d %d %x %d %d %d",smp->la_trig_mode,smp->la_trig_sig_sel,smp->la_dma_type,smp->la_trigger_time,
				smp->la_mac_mask_or_hdr_sel,smp->la_dbg_port,smp->la_trigger_edge,smp->la_smp_rate,smp->la_count);
		PRINT_SINGL_ARG("echo lamode 1 ", tmp, "%s");
    	for(i = 0;i < (buf->buffer_size >> 2); i+=2) {
    		ptr = (buf->octet + i);
    		PRINT_ARRAY_ARG("", ptr, "%08x", 2);
    	}
    } else
#endif
#endif
    {
    	//allocate memory to phydm_msg
    	if (phydm_msg == NULL) {
    		phydm_msg = rtw_zmalloc(PHYDM_MSG_LEN);
    		if (phydm_msg == NULL)
    			return -EFAULT;
    		phydm_cmd(ODMPTR, NULL, 0, 0, phydm_msg, PHYDM_MSG_LEN);
    	}
    	PRINT_ONE(phydm_msg, "%s", 1);
    }

    PRINT_ONE(phydm_msg, "%s", 1); //print phydm_smg to buf for proc file
#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    size = pos; // size is for the length check macro CHECK_LEN
    CHECK_LEN; //if phydm_msg is larger than length, it must be written in proc file several times 
#endif
    rtw_mfree(phydm_msg, PHYDM_MSG_LEN); //free memory
    phydm_msg = NULL;

#if defined(__KERNEL__) && !defined(CONFIG_RTL_PROC_NEW)
    *eof = 1;
_ret:
    *start = buf + (offset - begin);	/* Start of wanted data */
    len -= (offset - begin);	/* Start slop */
    if (len > length)
        len = length;	/* Ending slop */
#endif
    return len;
}

static int proc_set_phydm_cmd(struct file *file, const char *buffer,
				unsigned long count, void *data)
{

#ifdef CONFIG_RTL_PROC_NEW
    struct net_device *dev = pde_data(file_inode(file));
#else
    struct net_device *dev = (struct net_device *)data;
#endif
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    char tmp[64] = {0};
    int len = min(count, sizeof(tmp) - 1);

    if (GET_CHIP_VER(priv) < VERSION_8188E)  {
        panic_printk("RTL8192C and RTL8192D don't support this cmd\n");
        return -EFAULT;
    }

    if (count < 1)
        return -EFAULT;

    if (count > sizeof(tmp))
        return -EFAULT;

    if (buffer && !copy_from_user(tmp, buffer, len)) { //read cmd from proc file
		if (NULL == phydm_msg) {
			phydm_msg = rtw_zmalloc(PHYDM_MSG_LEN);
			if (NULL == phydm_msg)
				return -ENOMEM;
		} 
		else {
			memset(phydm_msg, 0, PHYDM_MSG_LEN);
		}
		if(!strncmp(tmp, "drvdbg", 6)) {
			drv_cmd(priv, tmp, phydm_msg, PHYDM_MSG_LEN);
		} else {
			phydm_cmd(ODMPTR, tmp, 64, 1, phydm_msg, PHYDM_MSG_LEN);
		}
		if (strlen(phydm_msg) == 0) {
			rtw_mfree(phydm_msg, PHYDM_MSG_LEN);
			phydm_msg = NULL;
		}
    }
    return count;
}
#endif


#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_thermal(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_thermal(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	
	if (netif_running(priv->dev)) {
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8881A)
		if ((GET_CHIP_VER(priv)==VERSION_8814A) || (GET_CHIP_VER(priv)==VERSION_8812E) || (GET_CHIP_VER(priv)==VERSION_8881A))
			phy_set_rf_reg(priv, RF_PATH_A, 0x42, BIT(17), 0x1);
		else
#endif	
#if defined(CONFIG_RTL_88E_SUPPORT) || defined(CONFIG_WLAN_HAL_8192EE) || defined(CONFIG_WLAN_HAL_8814BE)
		if ((GET_CHIP_VER(priv)==VERSION_8188E) || (GET_CHIP_VER(priv)==VERSION_8192E) || (GET_CHIP_VER(priv)==VERSION_8814B))
			phy_set_rf_reg(priv, RF_PATH_A, 0x42, (BIT(17) | BIT(16)), 0x03);
		else
#endif
		if (GET_CHIP_VER(priv)==VERSION_8192D)
			phy_set_rf_reg(priv, RF_PATH_A, RF_T_METER_92D, bMask20Bits, 0x30000);
		else	
			phy_set_rf_reg(priv, RF_PATH_A, 0x24, bMask20Bits, 0x60);

		// delay for 1 second
		delay_ms(1000);

		// query rf reg 0x24[4:0], for thermal meter value
#if defined(CONFIG_WLAN_HAL_8814AE) || defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8881A)
		if ((GET_CHIP_VER(priv)==VERSION_8814A) || (GET_CHIP_VER(priv)==VERSION_8812E) || (GET_CHIP_VER(priv)==VERSION_8881A))
			priv->pshare->rf_ft_var.curr_ther = phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0xfc00, 1);
		else
#endif	
#if defined(CONFIG_RTL_88E_SUPPORT) || defined(CONFIG_WLAN_HAL_8192EE) || defined(CONFIG_WLAN_HAL_8814BE)
		if ((GET_CHIP_VER(priv)==VERSION_8188E) || (GET_CHIP_VER(priv)==VERSION_8192E) || (GET_CHIP_VER(priv)==VERSION_8814B))
			priv->pshare->rf_ft_var.curr_ther = phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0xfc00, 1);
		else
#endif
		if (GET_CHIP_VER(priv)==VERSION_8192D)	
			priv->pshare->rf_ft_var.curr_ther = phy_query_rf_reg(priv, RF_PATH_A, RF_T_METER_92D, 0xf800, 1);
		else	
			priv->pshare->rf_ft_var.curr_ther = phy_query_rf_reg(priv, RF_PATH_A, 0x24, bMask20Bits, 1) & 0x01f;
	}
	PRINT_SINGL_ARG("", priv->pshare->rf_ft_var.curr_ther, "%u");
	return pos;
}

#ifdef RTK_ATM
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_atm(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_atm(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif			
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_ROOT(GET_DEV_PRIV(dev));
	struct stat_info *pstat;
	int pos = 0, i;
	unsigned long flags = 0;

	SAVE_INT_AND_CLI(flags);
	
	PRINT_SINGL_ARG("    atm_en:     ", priv->pshare->rf_ft_var.atm_en, "%u");
	PRINT_SINGL_ARG("    atm_swq_en:     ", priv->pshare->atm_swq_en, "%u");
	PRINT_SINGL_ARG("    atm_mode:     ", priv->pshare->rf_ft_var.atm_mode, "%u");
	PRINT_SINGL_ARG("    atm_ttl_match_stanum:     ", priv->pshare->atm_ttl_match_statime, "%u");
	PRINT_SINGL_ARG("    atm_ttl_match_statime:     ", priv->pshare->atm_ttl_match_stanum, "%u");
	PRINT_SINGL_ARG("    atm_quota(burst base):     ", priv->pshare->rf_ft_var.atm_quota, "%d");
	PRINT_SINGL_ARG("    swq agg max:     ", priv->pshare->rf_ft_var.atm_aggmax, "%d");
	PRINT_SINGL_ARG("    swq agg min:     ", priv->pshare->rf_ft_var.atm_aggmin, "%d");
	PRINT_SINGL_ARG("    min_rate:     ", priv->pshare->atm_min_txrate, "%u");
	PRINT_SINGL_ARG("    max_rate:     ", priv->pshare->atm_max_txrate, "%u");
	PRINT_SINGL_ARG("    atm_rhi:     ", priv->pshare->rf_ft_var.atm_rhi, "%u");
	PRINT_SINGL_ARG("    atm_rlo:     ", priv->pshare->rf_ft_var.atm_rlo, "%u");
	PRINT_SINGL_ARG("    min_burst_size:     ", priv->pshare->atm_min_burstsize, "%u");
	PRINT_SINGL_ARG("    max_burst_size:     ", priv->pshare->atm_max_burstsize, "%u");
	PRINT_SINGL_ARG("    atm_chk_txtime:     ", priv->pshare->rf_ft_var.atm_chk_txtime, "%d");
	PRINT_SINGL_ARG("    atm_chk_hista:     ", priv->pshare->rf_ft_var.atm_chk_hista, "%d");
	PRINT_SINGL_ARG("    atm_chk_newrty:     ", priv->pshare->rf_ft_var.atm_chk_newrty, "%d");
	PRINT_SINGL_ARG("    atm_timer:     ", priv->pshare->atm_timer, "%u");
	PRINT_SINGL_ARG("    atm_timeout_count:     ", priv->pshare->atm_timeout_count, "%u");

	for(i=0; i<NUM_STAT; i++) {
		if (priv->pshare->aidarray[i] && (priv->pshare->aidarray[i]->used == TRUE)){
			pstat = &(priv->pshare->aidarray[i]->station);
			
			PRINT_ONE(i,  " %d: stat_info...", 1);
			PRINT_SINGL_ARG("    aid: ", pstat->cmn_info.aid, "%d");
			PRINT_ARRAY_ARG("    hwaddr: ",	pstat->cmn_info.mac_addr, "%02x", MACADDRLEN);
			PRINT_ARRAY_ARG("    ip: ",	pstat->sta_ip, "%d.", 4);
			PRINT_SINGL_ARG("    airtime: ", pstat->atm_sta_time, "%u");
			PRINT_SINGL_ARG("    tx rate: ", pstat->atm_tx_rate, "%u");
			PRINT_SINGL_ARG("    tx rate: ", pstat->atm_tx_rate, "%u");
			PRINT_SINGL_ARG("    atm_burst_size: ", pstat->atm_burst_size, "%u");
			PRINT_SINGL_ARG("    swq burst unit: ", pstat->atm_burst_unit, "%u");
			PRINT_SINGL_ARG("    swq burst sent: ", pstat->atm_burst_sent, "%u");
			PRINT_SINGL_ARG("    atm_burst_num: ", pstat->atm_burst_num, "%u");
#ifdef TXRETRY_CNT
			PRINT_SINGL_ARG("    tx pkt retry ratio: ", ((pstat->atm_txretry_ratio>100)?pstat->atm_txretry_ratio-100:0), "%u");
			PRINT_SINGL_ARG("    tx pkt retry avg: ", pstat->atm_txretry_avg, "%u");
			PRINT_SINGL_ARG("    tx pkt retry 1s: ", pstat->atm_txretry_1s, "%u");
			PRINT_SINGL_ARG("    tx pkt 1s: ", pstat->atm_txpkt_1s, "%u");
#endif
			PRINT_SINGL_ARG("    swq full cnt: ", pstat->atm_swq_full, "%u");
			PRINT_SINGL_ARG("    txbd_full[0] cnt: ", pstat->atm_txbd_full[0], "%u");
			PRINT_SINGL_ARG("    txbd_full[1] cnt: ", pstat->atm_txbd_full[1], "%u");
			PRINT_SINGL_ARG("    txbd_full[2] cnt: ", pstat->atm_txbd_full[2], "%u");
			PRINT_SINGL_ARG("    txbd_full[3] cnt: ", pstat->atm_txbd_full[3], "%u");
			PRINT_SINGL_ARG("    sent 1 after burst out: ", pstat->atm_full_sent_cnt, "%u");
			PRINT_SINGL_ARG("    wait cnt: ", pstat->atm_wait_cnt, "%u");
			PRINT_SINGL_ARG("    drop cnt: ", pstat->atm_drop_cnt, "%u");
			PRINT_SINGL_ARG("    tx byte 1s: ", pstat->tx_bytes_1s, "%u");
			PRINT_SINGL_ARG("    tx time 1s: ", pstat->atm_tx_time, "%u");
			PRINT_SINGL_ARG("    tx time 1s wo retry: ", pstat->atm_tx_time_orig, "%u");
			PRINT_SINGL_ARG("    atm_is_maxsta: ", pstat->atm_is_maxsta, "%u");
			PRINT_SINGL_ARG("    dis matched sta: ", ((pstat->atm_match_sta_time>0)?1:0), "%u");
		}
	}

	RESTORE_INT(flags);
	return pos;
}
#endif

#ifdef SW_TXQ_ATF
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_swq_atf_read(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_swq_atf_read(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_ROOT(GET_DEV_PRIV(dev));
	struct stat_info *pstat;
	int pos = 0, i;
	unsigned long flags = 0;

#ifndef CONFIG_RTL_PROC_NEW
	if (offset != 0)
		return offset; //to prevent this function is called over once
#endif

	SAVE_INT_AND_CLI(flags);

	PRINT_SINGL_ARG("    swq_atf_en:    ", priv->pshare->rf_ft_var.swq_atf_en, "%d");
	PRINT_SINGL_ARG("    base_qta:      ", priv->pshare->rf_ft_var.swq_atf_base_qta, "%d");
	PRINT_SINGL_ARG("    delay_hi_th:   ", priv->pshare->rf_ft_var.swq_atf_delay_hi_th, "%d");
	PRINT_SINGL_ARG("    delay_lo_th:   ", priv->pshare->rf_ft_var.swq_atf_delay_lo_th, "%d");
	PRINT_SINGL_ARG("    force_max:     ", priv->pshare->rf_ft_var.swq_atf_force_max, "%d");
	PRINT_SINGL_ARG("    nopkt_max:     ", priv->pshare->rf_ft_var.swq_atf_nopkt_max, "%d");
	PRINT_ONE("", "%s", 1);
	PRINT_SINGL_ARG("    swq_atf_state: ", priv->pshare->swq_atf_state, "%u");
	PRINT_SINGL_ARG("    delay_cnt:     ", priv->pshare->swq_atf_delay_cnt, "%u");
	PRINT_ONE("", "%s", 1);
	PRINT_SINGL_ARG("    nosched_cnt:        ", (priv->pshare->swq_atf_nosched_budget_cnt + priv->pshare->swq_atf_nosched_pkt_cnt + priv->pshare->swq_atf_nosched_nopkt_cnt), "%u");
	PRINT_SINGL_ARG("    nosched_budget_cnt: ", priv->pshare->swq_atf_nosched_budget_cnt, "%u");
	PRINT_SINGL_ARG("    nosched_pkt_cnt:    ", priv->pshare->swq_atf_nosched_pkt_cnt, "%u");
	PRINT_SINGL_ARG("    nosched_nopkt_cnt:  ", priv->pshare->swq_atf_nosched_nopkt_cnt, "%u");
	PRINT_ONE("", "%s", 1);
	PRINT_SINGL_ARG("    sched_cnt:          ", (priv->pshare->swq_atf_sched_nobudget_cnt + priv->pshare->swq_atf_sched_force_cnt + priv->pshare->swq_atf_sched_nopkt_cnt), "%u");
	PRINT_SINGL_ARG("    sched_nobudget_cnt: ", priv->pshare->swq_atf_sched_nobudget_cnt, "%u");
	PRINT_SINGL_ARG("    sched_force_cnt:    ", priv->pshare->swq_atf_sched_force_cnt, "%u");
	PRINT_SINGL_ARG("    sched_nopkt_cnt:    ", priv->pshare->swq_atf_sched_nopkt_cnt, "%u");
	PRINT_ONE("", "%s", 1);

	PRINT_ONE("hwaddr", "%-12s", 0);
	PRINT_ONE("aid", "%4s", 0);
	PRINT_ONE("rssi", "%5s", 0);
	PRINT_ONE("rate", "%5s", 0);
	PRINT_ONE("bqta", "%5s", 0);
	PRINT_ONE("quota", "%8s", 0);
	PRINT_ONE("bqnum", "%6s", 0);
	PRINT_ONE("qnum", "%6s", 0);
	PRINT_ONE("budget", "%8s", 0);
	PRINT_ONE("ack_p", "%6s", 0);
	PRINT_ONE("retry", "%6s", 0);
	PRINT_ONE("hwq", "%5s", 0);
	PRINT_ONE("cnt", "%6s", 0);
	PRINT_ONE("drops", "%8s", 0);
	PRINT_ONE("tx_Kbytes", "%13s", 0);
	PRINT_ONE("sleep", "%9s", 1);

	for (i = 0; i < NUM_STAT; i++) {
		if (priv->pshare->aidarray[i] == NULL || priv->pshare->aidarray[i]->used == FALSE)
			continue;

		pstat = &(priv->pshare->aidarray[i]->station);
		PRINT_ARRAY(pstat->cmn_info.mac_addr, "%02x", 6, 0);
		PRINT_ONE(pstat->cmn_info.aid, "%4d", 0);
		PRINT_ONE(pstat->rssi, "%5d", 0);
		PRINT_ONE(pstat->swq.atf_txrate, "%5d", 0);
		PRINT_ONE(pstat->swq.atf_base_quota, "%5d", 0);
		PRINT_ONE(pstat->swq.atf_quota, "%8d", 0);
		PRINT_ONE(pstat->swq.atf_base_enqnum, "%6d", 0);
		PRINT_ONE(pstat->swq.atf_enqnum, "%6d", 0);
		PRINT_ONE(pstat->swq.atf_budget, "%8d", 0);
		PRINT_ONE(pstat->swq.atf_ack_penalty, "%6d", 0);
		PRINT_ONE(pstat->swq.atf_retry_ratio, "%6u", 0);
		PRINT_ONE(rtl_atomic_read(&pstat->swq.atf_hwpkt_bytes) / 125 /
			pstat->swq.atf_txrate * pstat->swq.atf_retry_ratio / 100, "%5d", 0);
		PRINT_ONE(pstat->swq.atf_cnt, "%6lu", 0);
		PRINT_ONE(pstat->swq.atf_drops, "%8lu", 0);
		PRINT_ONE(pstat->swq.atf_tx_bytes/1000, "%13lu", 0);
		PRINT_ONE(RTL_JIFFIES_TO_MILISECONDS(pstat->swq.atf_sleep_time), "%9lu", 1);
	}

	RESTORE_INT(flags);
	return pos;
}

#define ATF_MODIFY_QUOTA	BIT0
#define ATF_MODIFY_BQNUM	BIT1
static int rtl8192cd_proc_swq_atf_write(struct file *file, const char *buffer,
				unsigned long count, void *data)
{
#ifdef CONFIG_RTL_PROC_NEW
	struct net_device *dev = pde_data(file_inode(file));
#else
	struct net_device *dev = (struct net_device *)data;
#endif
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	struct stat_info *pstat;
	char tmp[64], argv[MAX_ARGC][MAX_ARGV], *tmpptr, *token;
	int i, argc = 0, quota, bqnum, action = 0, mac_argv = 0;
	unsigned char mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if (count < 1 || count > sizeof(tmp))
		return -EFAULT;

	if (!buffer || copy_from_user(tmp, buffer, count))
		return -EFAULT;

	/* strip a trailing newline */
	if (tmp[count - 1] == '\n')
		tmp[count - 1] = '\0';

	tmpptr = tmp;

	// parse cmd arguments
	while (argc < MAX_ARGC && (token = strsep((char **)&tmpptr, ", "))) {
		strncpy(argv[argc], token, MAX_ARGV-1);
		argv[argc][MAX_ARGV-1] = '\0';
		argc++;
	}

	if (!strcmp(argv[0], "0")) {
		// clear the related counters
		priv->pshare->swq_atf_nosched_budget_cnt = 0;
		priv->pshare->swq_atf_nosched_pkt_cnt = 0;
		priv->pshare->swq_atf_nosched_nopkt_cnt = 0;

		priv->pshare->swq_atf_sched_nobudget_cnt = 0;
		priv->pshare->swq_atf_sched_force_cnt = 0;
		priv->pshare->swq_atf_sched_nopkt_cnt = 0;
		priv->pshare->swq_atf_delay_cnt = 0;

		for (i = 0; i < NUM_STAT; i++) {
			if (priv->pshare->aidarray[i] == NULL || priv->pshare->aidarray[i]->used == FALSE)
				continue;

			pstat = &(priv->pshare->aidarray[i]->station);
			pstat->swq.atf_drops = 0;
			pstat->swq.atf_tx_bytes = 0;
			pstat->swq.atf_tx_bytes_prev = 0;
			pstat->swq.atf_sleep_time = 0;
		}

		return count;
	}

	for (i = 0; i < argc - 1; i++) {
		if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "mac")) {
			get_array_val(mac, argv[++i], MACADDRLEN*2);
			mac_argv = 1;
		}
		else if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "quota")) {
			quota = _atoi(argv[++i], 10);
			action |= ATF_MODIFY_QUOTA;
		}
		else if (!strcmp(argv[i], "-b") || !strcmp(argv[i], "bqnum")) {
			bqnum = _atoi(argv[++i], 10);
			action |= ATF_MODIFY_BQNUM;
		}
	}

	// do the related command
	if (action && mac_argv) {
		for (i = 0; i < NUM_STAT; i++) {
			if (priv->pshare->aidarray[i] == NULL || priv->pshare->aidarray[i]->used == FALSE)
				continue;

			pstat = &(priv->pshare->aidarray[i]->station);
			if (!memcmp(mac, pstat->cmn_info.mac_addr, 6)) {
				if (action & ATF_MODIFY_QUOTA)
					pstat->swq.atf_base_quota = quota;
				if (action & ATF_MODIFY_BQNUM)
					pstat->swq.atf_base_enqnum = bqnum;
				break;
			}
		}
	}

	return count;
}
#endif

#ifdef CONFIG_RTL_OFFLOAD_DRIVER
#include "Hal8814BFirmware.h"
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ofld(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ofld(char *buf, char **start, off_t offset,
                int length, int *eof, void *data)
#endif
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;
    unsigned int value, magic, cookie;
    int i, j, idx = 0;
    unsigned char trace_str[65];
    struct dcnt_sys_t *dcnt_sys;
    struct dcnt_pe_t *dcnt_pe;

    PRINT_ONE("  Data CPU statistics", "%s", 1);
    
    INDIRECT_READ(DC_SRAM_START, value);
    if (value == 0xdeadbeef || value == 0xdeaddead) {
        PRINT_SINGL_ARG("    keep alive: ", value, "%x");
        for (i = 0; i < 16; i++) {
            INDIRECT_READ(DC_SRAM_START + 0x10 + (4 * i), value);
            for (j = 0; j < 4; j++) {
                trace_str[idx++] = value & 0xff;
                if (trace_str[idx - 1] == 0)
                    break;
                value = value >> 8;
            }
            if (j < 4)
                break;
        }
        PRINT_SINGL_ARG("    trace: ", trace_str, "%s");
    } else
        PRINT_SINGL_ARG("    keep alive: ", value, "%lu");

    INDIRECT_READ(DC_SRAM_START + 0x4, magic);
    PRINT_SINGL_ARG("    magic addr: ", magic, "%08x");

    INDIRECT_READ(magic + (DCNT_CAT_COOKIE * 4), cookie);
    PRINT_SINGL_ARG("    dcnt_cookie: ", cookie, "%08x");

    INDIRECT_READ(DC_SRAM_START + 0x58, value);
    PRINT_SINGL_ARG("    os_loop: ", value, "%08x");
    INDIRECT_READ(DC_SRAM_START + 0x5c, value);
    PRINT_SINGL_ARG("    axidma reset: ", value, "%08x");

    PRINT_ONE("    + HCIDMA", "%s", 1);
    INDIRECT_READ(DC_HCIDMA_START + 0x8, value);
    PRINT_SINGL_ARG("      REG_TXQ_DTXBD_IDX: ", value, "%08x");
    INDIRECT_READ(DC_HCIDMA_START + 0x1c, value);
    PRINT_SINGL_ARG("      REG_DATAQ_DRXBD_IDX: ", value, "%08x");
    INDIRECT_READ(DC_HCIDMA_START + 0x24, value);
    PRINT_SINGL_ARG("      REG_CMDQ_DRXBD_IDX: ", value, "%08x");

    PRINT_ONE("    + AXIDMA", "%s", 1);
    INDIRECT_READ(DC_AXIDMA_START + 0x38, value);
    PRINT_SINGL_ARG("      REG_CH0Q_TXBD_IDX: ", value, "%08x");
    INDIRECT_READ(DC_AXIDMA_START + 0x48, value);
    PRINT_SINGL_ARG("      REG_CH1Q_TXBD_IDX: ", value, "%08x");
    INDIRECT_READ(DC_AXIDMA_START + 0x58, value);
    PRINT_SINGL_ARG("      REG_CH2Q_TXBD_IDX: ", value, "%08x");
    INDIRECT_READ(DC_AXIDMA_START + 0x68, value);
    PRINT_SINGL_ARG("      REG_CH3Q_TXBD_IDX: ", value, "%08x");
    INDIRECT_READ(DC_AXIDMA_START + 0x24, value);
    PRINT_SINGL_ARG("      REG_DATAQ_NRXBD_IDX: ", value, "%08x");
    INDIRECT_READ(DC_AXIDMA_START + 0x14, value);
    PRINT_SINGL_ARG("      REG_CMDQ_NRXBD_IDX: ", value, "%08x");
    PRINT_ONE("", "%s", 1);

    return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ofld_cpu(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ofld_cpu(char *buf, char **start, off_t offset,
                int length, int *eof, void *data)
#endif
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;
    unsigned int magic, cookie, value, addr;
    int i;
    struct dcnt_cpu_t dcnt_cpu;
    char _cpu_state_string[6][12] = {
        "INIT", "RUN", "SLEEP", "EXCEPTION", "BUS_AHD", "WDT" };
    uint32_t cause, sp, sp_s, sp_d, sp_t, sp_b, traps;
    uint32_t cpu_reg[32];

    INDIRECT_READ(DC_SRAM_START + 0x4, magic);
    INDIRECT_READ(magic + (DCNT_CAT_COOKIE * 4), cookie);
    if (cookie != DCNT_CHECK_COOKIE) {
        PRINT_ONE("wrong cookie!", "%s", 1);
        return pos;
    }
    
    INDIRECT_READ(magic + (DCNT_CAT_CPU * 4), value);

    for (i=0; i<sizeof(struct dcnt_cpu_t)/sizeof(u32); i++)
        INDIRECT_READ((value + i * 4), *((u32 *)&dcnt_cpu + i));

    PRINT_ONE(_cpu_state_string[dcnt_cpu.__cpu_state], "STATE   : %s", 1);
    PRINT_ONE(dcnt_cpu.__cpu_loading & 0xff, "LOADING : %d", 1);
    PRINT_ONE(dcnt_cpu.__cpu_sleep, "SLEEP   : %d", 1);
    PRINT_ONE("", "%s", 1);

    if (dcnt_cpu.__cpu_state == DCNT_CPU_S_BUS_AHD) {
        PRINT_ONE("ADDR-HOLE", "%s", 1);
        PRINT_ONE(dcnt_cpu.__bus_ahd_tag, "INFO    : TAG 0x%08x", 0);
        PRINT_ONE(dcnt_cpu.__bus_ahd_code, " | CODE %d", 1);
        PRINT_ONE(dcnt_cpu.__bus_ahd_addr1, "ADDRESS : 0x%08x", 0);
        PRINT_ONE(dcnt_cpu.__bus_ahd_addr0, " 0x%08x", 1);
        PRINT_ONE(dcnt_cpu.__bus_ahd_pld1, "PAYLOAD : 0x%08x", 0);
        PRINT_ONE(dcnt_cpu.__bus_ahd_pld0, " 0x%08x", 1);
        PRINT_ONE("", "%s", 1);
    }

    if (dcnt_cpu.__cpu_state == DCNT_CPU_S_EXCEPTION) {
        PRINT_ONE("EXCEPTION", "%s", 1);
        cause = dcnt_cpu.__cpu_exception_cause;
        if (cause) {
            sp = dcnt_cpu.__cpu_exception_sp;
            sp_s = dcnt_cpu.__cpu_exception_sp_s;
            sp_b = dcnt_cpu.__cpu_exception_sp_b;
            sp_t = dcnt_cpu.__cpu_exception_sp_t;
            sp_d = (uint32_t)(sp_t + sp_s) - (ALIGN((uint32_t)(sp_t + sp_s) - sp, 32) + 64);
            PRINT_ONE(cause, "=CAUSE    %x", 1);
            PRINT_ONE((cause >> 2) & 0x1f, "=ExcCode  %x", 1);
            PRINT_ONE(dcnt_cpu.__cpu_exception_pc, "=EPC      %x", 1);
            PRINT_ONE(dcnt_cpu.__cpu_exception_status, "=STATUS   %x", 1);
            PRINT_ONE(dcnt_cpu.__cpu_exception_badaddr, "=BADADDR  %x", 1);
            PRINT_ONE(sp, "=SP       %x", 1);
            PRINT_ONE(dcnt_cpu.__cpu_exception_ra, "=RA       %x", 1);
            PRINT_ONE(sp_t, "=SP_T     %x", 1);
            PRINT_ONE(sp_b, "=SP_B     %x", 1);
            PRINT_ONE(sp_d, "=SP_D     %x", 1);
            PRINT_ONE("", "%s", 1);

            /* registers */
            traps = ALIGN(sp_t + 0x2000, 8) + 24;
            for (i=0; i<32; i++) {
                INDIRECT_READ((traps + i * 4), cpu_reg[i]);
                if ((i & 0x3) == 0)
                    PRINT_ONE(i, "=$%-3d ", 0);
                PRINT_ONE(cpu_reg[i], "%08x ", 0);
                if ((i & 0x3) == 3)
                    PRINT_ONE("", "%s", 1);
            }
            PRINT_ONE("", "%s", 1);

            /* watchpoint */
            if (((cause >> 2) & 0x1f) == 0x17) {
                uint32_t ctrl = dcnt_cpu.__cpu_wp_ctrl;
                PRINT_ONE(dcnt_cpu.__cpu_wp_addr, "=WMP_ADDR %x", 1);
                PRINT_ONE(dcnt_cpu.__cpu_wp_mask, "=WMP_CTL  %x", 0);
                PRINT_ONE((ctrl >> 0) & 0xff, " I%d", 0);
                PRINT_ONE((ctrl >> 8) & 0xff, " R%d", 0);
                PRINT_ONE((ctrl >> 16) & 0xff, " W%d", 1);
            }

            sp &= 0x3FFFFFFF;
            sp_b &= 0x3FFFFFFF;
            sp_d &= 0x3FFFFFFF;
            for (i = 0, addr = sp_d; addr < sp_b; i++, addr += 4) {
                INDIRECT_READ(addr, value);
                if ((i & 0x7) == 0)
                    PRINT_ONE("", "%s", 1);

                if (addr == sp) {
                    PRINT_ONE(value, "*%08x ", 0);
                } else {
                    PRINT_ONE(value, " %08x ", 0);
                }
            }
            PRINT_ONE("", "%s", 1);
        }
    }

    return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ofld_system(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ofld_system(char *buf, char **start, off_t offset,
                int length, int *eof, void *data)
#endif
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;
    unsigned int magic, cookie, value;
    int i;
    struct dcnt_sys_t dcnt_sys;
    char _irq_string[16][10] = {
        "SW0", "SW1", "RPWM", "", 
        "CPU-TIMER", "SPIC", "UART", "IDDMA-N",
        "PLATFORM", "WDT", "GPIO", "GTIMER",
        "IDDMA-H", "WMAC", "AXIDMA", "HCIDMA" };

    INDIRECT_READ(DC_SRAM_START + 0x4, magic);
    INDIRECT_READ(magic + (DCNT_CAT_COOKIE * 4), cookie);
    if (cookie != DCNT_CHECK_COOKIE) {
        PRINT_ONE("wrong cookie!", "%s", 1);
        return pos;
    }
    
    INDIRECT_READ(magic + (DCNT_CAT_SYS * 4), value);
    for (i=0; i<sizeof(struct dcnt_sys_t)/sizeof(u32); i++)
        INDIRECT_READ((value + i * 4), *((u32 *)&dcnt_sys + i));

    for (i=15; i>=0; i-=4) {
        PRINT_ONE(_irq_string[i], "        : %10s", 0);
        PRINT_ONE(_irq_string[i-1], " | %10s", 0);
        PRINT_ONE(_irq_string[i-2], " | %10s", 0);
        PRINT_ONE(_irq_string[i-3], " | %10s", 1);
        PRINT_ONE(dcnt_sys.__isr_cnt[i], "IRQ-CNT : %10u", 0);
        PRINT_ONE(dcnt_sys.__isr_cnt[i-1], " | %10u", 0);
        PRINT_ONE(dcnt_sys.__isr_cnt[i-2], " | %10u", 0);
        PRINT_ONE(dcnt_sys.__isr_cnt[i-3], " | %10u", 1);
        PRINT_ONE(dcnt_sys.__isr_run[i], "IRQ-RUN : %10u", 0);
        PRINT_ONE(dcnt_sys.__isr_run[i-1], " | %10u", 0);
        PRINT_ONE(dcnt_sys.__isr_run[i-2], " | %10u", 0);
        PRINT_ONE(dcnt_sys.__isr_run[i-3], " | %10u", 1);
        PRINT_ONE(dcnt_sys.__isr_poll[i], "IRQ-POLL: %10u", 0);
        PRINT_ONE(dcnt_sys.__isr_poll[i-1], " | %10u", 0);
        PRINT_ONE(dcnt_sys.__isr_poll[i-2], " | %10u", 0);
        PRINT_ONE(dcnt_sys.__isr_poll[i-3], " | %10u", 1);
    }

    PRINT_ONE(dcnt_sys.__os_ldie_run, "OS_LOOP : IDLE %u", 0);
    PRINT_ONE(dcnt_sys.__os_timer_c, " | TIMER-C %u", 0);
    PRINT_ONE(dcnt_sys.__os_timer_a, " | TIMER-A %u", 1);

    PRINT_ONE(dcnt_sys.__mem_alloc, "MEMORY  : ALLOC %u", 0);
    PRINT_ONE(dcnt_sys.__mem_cp, " | COPY %u", 0);
    PRINT_ONE(dcnt_sys.__mem_mv, " | MOVE %u", 1);
    PRINT_ONE("", "%s", 1);

    return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ofld_pkt_mgnt(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ofld_pkt_mgnt(char *buf, char **start, off_t offset,
                int length, int *eof, void *data)
#endif
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;
    unsigned int magic, cookie, value;
    int i;
    struct dcnt_pe_t dcnt_pe;
    char _skbuf_string[6][8] = {
        "S_DATA", "DATA", "J_DATA", "RX_EVT", "CIL", "FWLOG" };
    char _silpkt_string[8][12] = {
        "GLOBAL", "TX_DATA_N", "TX_DATA_H", "TX_CTRL",
        "RX_DATA", "RX_CTRL", "RX_AMSDU", "RX_ALL" };

    INDIRECT_READ(DC_SRAM_START + 0x4, magic);
    INDIRECT_READ(magic + (DCNT_CAT_COOKIE * 4), cookie);
    if (cookie != DCNT_CHECK_COOKIE) {
        PRINT_ONE("wrong cookie!", "%s", 1);
        return pos;
    }
    
    INDIRECT_READ(magic + (DCNT_CAT_PE * 4), value);
    for (i=0; i<sizeof(struct dcnt_pe_t)/sizeof(u32); i++)
        INDIRECT_READ((value + i * 4), *((u32 *)&dcnt_pe + i));

    PRINT_ONE("SKBUF", "%s", 1);
    for (i=0; i<6; i++) {
	    PRINT_ONE(_skbuf_string[i], " %8s :", 0);
	    PRINT_ONE(dcnt_pe.__skbuf_alloc_cnt[i], " alloc %10u", 0);
	    PRINT_ONE(dcnt_pe.__skbuf_free_cnt[i], " | free %10u", 0);
	    PRINT_ONE(dcnt_pe.__skbuf_allocFail_cnt[i], " | fail %10u", 1);
	}
    PRINT_ONE("", "%s", 1);

    PRINT_ONE("SIL_PKT", "%s", 1);
    for (i=0; i<8; i++) {
	    PRINT_ONE(_silpkt_string[i], " %10s :", 0);
	    PRINT_ONE(dcnt_pe.__sil_pkt_res_cnt[i], " NUM %3u", 0);
	    PRINT_ONE(dcnt_pe.__sil_pkt_num[i], " | CURR %3u", 0);
	    PRINT_ONE(dcnt_pe.__sil_pkt_alloc[i], " | alloc %10u", 0);
	    PRINT_ONE(dcnt_pe.__sil_pkt_allocFail[i], " | fail %10u", 0);
	    PRINT_ONE(dcnt_pe.__sil_pkt_free[i], " | free %10u", 0);
	    PRINT_ONE(dcnt_pe.__sil_pkt_freeReuse[i], " | reuse %10u", 0);
	    PRINT_ONE(dcnt_pe.__sil_pkt_freeChain[i], " | chain %10u", 1);
	}
    PRINT_ONE("", "%s", 1);

    return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ofld_dma_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ofld_dma_info(char *buf, char **start, off_t offset,
                int length, int *eof, void *data)
#endif
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;
    unsigned int magic, cookie, value;
    int i;
    struct dcnt_pe_t dcnt_pe;
    char _skbuf_string[5][8] = {
        "S_DATA", "DATA", "J_DATA", "CIL", "FWLOG" };
    char _silpkt_string[8][12] = {
        "GLOBAL", "TX_DATA_N", "TX_DATA_H", "TX_CTRL",
        "RX_DATA", "RX_CTRL", "RX_AMSDU", "RX_ALL" };

    INDIRECT_READ(DC_SRAM_START + 0x4, magic);
    INDIRECT_READ(magic + (DCNT_CAT_COOKIE * 4), cookie);
    if (cookie != DCNT_CHECK_COOKIE) {
        PRINT_ONE("wrong cookie!", "%s", 1);
        return pos;
    }
    
    INDIRECT_READ(magic + (DCNT_CAT_PE * 4), value);
    for (i=0; i<sizeof(struct dcnt_pe_t)/sizeof(u32); i++)
        INDIRECT_READ((value + i * 4), *((u32 *)&dcnt_pe + i));

    PRINT_ONE("@RX PATH", "%s", 1);
    PRINT_ONE("HCIDMA  :", "%s", 1);
    for (i=0; i<2; i++) {
	    PRINT_ONE(i, " CH%02d   :", 0);
	    PRINT_ONE(dcnt_pe.__hcidma_chan_tx[i], " TX %10u", 0);
	    PRINT_ONE(dcnt_pe.__hcidma_chan_txdone[i], " | TXDONE %10u", 0);
	    PRINT_ONE(dcnt_pe.__hcidma_chan_tx_still[i], " | STILL_DESC %10u", 1);
	}
    PRINT_ONE("AXIDMA  :", "%s", 1);
    for (i=0; i<2; i++) {
	    PRINT_ONE(i, " CH%02d   :", 0);
	    PRINT_ONE(dcnt_pe.__axidma_chan_rx[i], " RX %10u", 0);
	    PRINT_ONE(dcnt_pe.__axidma_chan_rx_dispatch[i], " | RXDISPATCH %10u", 0);
	    PRINT_ONE(dcnt_pe.__axidma_chan_rxdone[i], " | RXDONE %10u", 0);
	    PRINT_ONE(dcnt_pe.__axidma_chan_rx_desc_ua[i], " | DESC_UA %10u", 0);
	    PRINT_ONE(dcnt_pe.__axidma_chan_rx_still[i], " | AVA_DESC %10u", 1);
	}
    PRINT_ONE("", "%s", 1);

    PRINT_ONE("@TX PATH", "%s", 1);
    PRINT_ONE("HCIDMA  :", "%s", 1);
    for (i=0; i<1; i++) {
	    PRINT_ONE(i, " CH%02d   :", 0);
	    PRINT_ONE(dcnt_pe.__hcidma_chan_rx, " RX %10u", 0);
	    PRINT_ONE(dcnt_pe.__hcidma_chan_rx_dispatch, " | RXDISPATCH %10u", 0);
	    PRINT_ONE(dcnt_pe.__hcidma_chan_rxdone, " | RXDONE %10u", 0);
	    PRINT_ONE(dcnt_pe.__hcidma_chan_rx_desc_ua, " | DESC_UA %10u", 0);
	    PRINT_ONE(dcnt_pe.__hcidma_chan_rx_still, " | AVA_DESC %10u", 1);
	}
    PRINT_ONE("AXIDMA  :", "%s", 1);
    for (i=0; i<38; i++) {
	    PRINT_ONE(i, " CH%02d   :", 0);
	    PRINT_ONE(dcnt_pe.__axidma_chan_tx[i], " TX %10u", 0);
	    PRINT_ONE(dcnt_pe.__axidma_chan_txdone[i], " | TXDONE %10u", 0);
	    PRINT_ONE(dcnt_pe.__axidma_chan_tx_still[i], " | STILL_DESC %10u", 1);
	}
    PRINT_ONE("", "%s", 1);
    PRINT_SINGL_ARG("err_d2c_ctrl_skbuf:       ", dcnt_pe.__err_d2c_ctrl_skbuf, "%d");
    PRINT_SINGL_ARG("err_d2h_ctrl_skbuf:       ", dcnt_pe.__err_d2h_ctrl_skbuf, "%d");
    PRINT_SINGL_ARG("err_drop_high: ", dcnt_pe.__err_drop_high, "%d");
    PRINT_SINGL_ARG("      cnt_hiq: ", dcnt_pe.__cnt_hiq, "%d");
    PRINT_SINGL_ARG("err_hcidma_rx_tag_chk:    ", dcnt_pe.__err_hcidma_rx_tag_chk, "%d");
    PRINT_SINGL_ARG("err_hcidma_rx_dma_fail:   ", dcnt_pe.__err_hcidma_rx_dma_fail, "%d");
    PRINT_SINGL_ARG("err_hcidma_rx_buf_exceed: ", dcnt_pe.__err_hcidma_rx_buf_exceed, "%d");
    PRINT_SINGL_ARG("err_axidma_rx_tag_chk:    ", dcnt_pe.__err_axidma_rx_tag_chk, "%d");
    PRINT_SINGL_ARG("err_axidma_rx_dma_fail:   ", dcnt_pe.__err_axidma_rx_dma_fail, "%d");
    PRINT_SINGL_ARG("err_axidma_rx_buf_exceed: ", dcnt_pe.__err_axidma_rx_buf_exceed, "%d");
    PRINT_SINGL_ARG("err_axidma_rx_len_exceed: ", dcnt_pe.__err_axidma_rx_len_exceed, "%d");
    PRINT_SINGL_ARG("err_axidma_rx_skb_empty:  ", dcnt_pe.__err_axidma_rx_skb_empty, "%d");
    PRINT_SINGL_ARG("err_pkt_size_chk:         ", dcnt_pe.__err_pkt_size_chk, "%d");
    PRINT_ONE("", "%s", 1);

    return pos;
}


enum axidma_txdesc_idx {
	AXIDMA_REG_CH0Q_TXBD_DESA = 0,
	AXIDMA_REG_CH1Q_TXBD_DESA,
	AXIDMA_REG_CH2Q_TXBD_DESA,
	AXIDMA_REG_CH3Q_TXBD_DESA,
	AXIDMA_REG_CH4Q_TXBD_DESA,
	AXIDMA_REG_CH5Q_TXBD_DESA,
	AXIDMA_REG_CH6Q_TXBD_DESA,
	AXIDMA_REG_CH7Q_TXBD_DESA,
	AXIDMA_REG_CH8Q_TXBD_DESA,
	AXIDMA_REG_CH9Q_TXBD_DESA,
	AXIDMA_REG_CH10Q_TXBD_DESA,
	AXIDMA_REG_CH11Q_TXBD_DESA,
	AXIDMA_REG_CH12Q_S0Q_TXBD_DESA,
	AXIDMA_REG_CH13Q_S1Q_TXBD_DESA,
	AXIDMA_REG_2G_MGTQ_TXBD_DESA,
	AXIDMA_REG_2G_HI0Q_TXBD_DESA,
	AXIDMA_REG_2G_HI1Q_TXBD_DESA,
	AXIDMA_REG_2G_HI2Q_TXBD_DESA,
	AXIDMA_REG_2G_HI3Q_TXBD_DESA,
	AXIDMA_REG_2G_HI4Q_TXBD_DESA,
	AXIDMA_REG_2G_HI5Q_TXBD_DESA,
	AXIDMA_REG_2G_HI6Q_TXBD_DESA,
	AXIDMA_REG_2G_HI7Q_TXBD_DESA,
	AXIDMA_REG_2G_HI8Q_TXBD_DESA,
	AXIDMA_REG_2G_HI9Q_TXBD_DESA,
	AXIDMA_REG_2G_HI10Q_TXBD_DESA,
	AXIDMA_REG_2G_HI11Q_TXBD_DESA,
	AXIDMA_REG_2G_HI12Q_TXBD_DESA,
	AXIDMA_REG_2G_HI13Q_TXBD_DESA,
	AXIDMA_REG_2G_HI14Q_TXBD_DESA,
	AXIDMA_REG_2G_HI15Q_TXBD_DESA,
	AXIDMA_REG_2G_HI16Q_TXBD_DESA,
	AXIDMA_REG_2G_HI17Q_TXBD_DESA,
	AXIDMA_REG_2G_HI18Q_TXBD_DESA,
	AXIDMA_REG_2G_HI19Q_TXBD_DESA,
/*	AXIDMA_REG_2G_BCNQ_TXBD_DESA,
	AXIDMA_REG_2G_MPBCNQ_TXBD_DESA,*/
	AXIDMA_REG_FWCMD_TXBD_DESA,
	AXIDMA_REG_H2C_TXBD_DESA,

	/* keep last */
	AXIDMA_REG_MAX_NUM,
};

enum hcidma_drxdesc_idx {
	HCIDMA_REG_DATAQ_DRXBD_DESA = 0,
	HCIDMA_REG_CMDQ_DRXBD_DESA,
	
	/* keep last */
	HCIDMA_REG_MAX_NUM,
};

int axidma_txdesc_offset[AXIDMA_REG_MAX_NUM] = {
	0x030, 0x040, 0x050, 0x060, 0x070, 0x080, 0x090, 0x0a0,
	0x0b0, 0x0c0, 0x0d0, 0x0e0, 0x0f0, 0x100, 0x110, 0x120,
	0x130, 0x140, 0x150, 0x160, 0x170, 0x180, 0x190, 0x1a0,
	0x1b0, 0x1c0, 0x1d0, 0x1e0, 0x1f0, 0x200, 0x210, 0x220,
	0x230, 0x240, 0x250, /*0x360, 0x364,*/ 0x330,0x350
};

int hcidma_drxdesc_offset[HCIDMA_REG_MAX_NUM] = {
	0x018, 0x020
};

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ofld_axidma_txdesc(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ofld_axidma_txdesc(char *buf, char **start, off_t offset,
                int length, int *eof, void *data)
#endif
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;
    unsigned int magic, cookie, value[8], addr, num, len, idx, desc;
    int i, j, k;
    struct dcnt_pe_t dcnt_pe;
    char _skbuf_string[5][8] = {
        "S_DATA", "DATA", "J_DATA", "CIL", "FWLOG" };
    char _silpkt_string[8][12] = {
        "GLOBAL", "TX_DATA_N", "TX_DATA_H", "TX_CTRL",
        "RX_DATA", "RX_CTRL", "RX_AMSDU", "RX_ALL" };

    INDIRECT_READ(DC_SRAM_START + 0x4, magic);
    INDIRECT_READ(magic + (DCNT_CAT_COOKIE * 4), cookie);
    if (cookie != DCNT_CHECK_COOKIE) {
        PRINT_ONE("wrong cookie!", "%s", 1);
        return pos;
    }
    
	PRINT_ONE(priv->axidma_txdesc_num, "AXIDMA Tx queue %d descriptor ..........\n", 1);
	PRINT_ONE(DC_AXIDMA_START+axidma_txdesc_offset[priv->axidma_txdesc_num], "Base Address: %08x\n", 1);

    INDIRECT_READ(DC_AXIDMA_START+axidma_txdesc_offset[priv->axidma_txdesc_num] , addr);
    PRINT_SINGL_ARG("DC_TXBD_ADDR:   ", addr, "%08x");
    INDIRECT_READ(DC_AXIDMA_START+axidma_txdesc_offset[priv->axidma_txdesc_num]+4, num);
    PRINT_SINGL_ARG("DC_TXBD_NUM:    ", num, "%08x");
    INDIRECT_READ(DC_AXIDMA_START+axidma_txdesc_offset[priv->axidma_txdesc_num]+8, idx);
    PRINT_SINGL_ARG("DC_TXBD_HW_IDX: ", idx, "%08x");

	for (i=0; i<num; i++) {
		PRINT_ONE(i, " TXBD[%02d]  :", 0);

		for (j=0; j<8; j++) {
			INDIRECT_READ(addr+i*32+j*4, value[j]);
			PRINT_ONE(value[j], " %08x", 0);
		}
		PRINT_ONE("", "%s", 1);

		/* Tx desc */
		len = value[0] & 0xffff;
		if (len) {
			PRINT_ONE(" Tx desc:   ", "%s", 0);
			for (j=0; j<len; j+=4) {
				INDIRECT_READ(value[1]+j, desc);
				PRINT_ONE(desc, " %08x", 0);

				if ((j+4)%32 == 0) {
					PRINT_ONE("", "%s", 1);
					PRINT_ONE(" 	    ", "%s", 0);
				}
			}
			PRINT_ONE("", "%s", 1);
		}

		/* Mac header */
		len = value[2] & 0xffff;
		len = (len > 32) ? 32 : len;
		if (len) { //Tx desc
			PRINT_ONE(" MAC header:", "%s", 0);
			for (j=0; j<len; j+=4) {
				INDIRECT_READ(value[3]+j, desc);
				PRINT_ONE(desc, " %08x", 0);
			}
			PRINT_ONE("", "%s", 1);
		}

		/* AMSDU info */
		len = value[4] & 0xffff;
		if ((value[4] & 0x80000000) && len) { // AMSDU info
			PRINT_ONE(" AMSDU info:", "%s", 0);
			for (j=0; j<len; j+=4) {
				INDIRECT_READ(value[5]+j, desc);
				PRINT_ONE(desc, " %08x", 0);
			}
			PRINT_ONE("", "%s", 1);
		}

		PRINT_ONE(" ----", "%s", 1);
	}
	
    return pos;
}

#ifdef CONFIG_RTL_KERNEL_MIPS16_WLAN
__NOMIPS16
#endif
#ifdef __ECOS
void rtl8192cd_proc_ofld_axidma_txdesc_idx_write(int txdesc_num, void *data)
#else
static int rtl8192cd_proc_ofld_axidma_txdesc_idx_write(struct file *file, const char *buffer,
		unsigned long count, void *data)
#endif
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
#ifdef __KERNEL__
	char tmp[32];
#endif


	if (count < 2)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, 32)) {
		int num = sscanf(tmp, "%d", &priv->axidma_txdesc_num);

		if (num != 1)
			panic_printk("Invalid tx desc number!\n");
		else if (priv->axidma_txdesc_num >= AXIDMA_REG_MAX_NUM) 
		{
			panic_printk("Invalid axidma tx desc number!\n");
			priv->axidma_txdesc_num = 0;
		}
		else
			panic_printk("Ready to dump axidma tx desc %d\n", priv->axidma_txdesc_num);
	}
	return count;

}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_ofld_hcidma_drxdesc(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_ofld_hcidma_drxdesc(char *buf, char **start, off_t offset,
                int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;
    unsigned int magic, cookie, value[8], addr, num, len, idx, desc;
    int i, j;

    INDIRECT_READ(DC_SRAM_START + 0x4, magic);
    INDIRECT_READ(magic + (DCNT_CAT_COOKIE * 4), cookie);
    if (cookie != DCNT_CHECK_COOKIE) {
        PRINT_ONE("wrong cookie!", "%s", 1);
        return pos;
    }

	PRINT_ONE(priv->axidma_txdesc_num, "HCIDMA Tx queue %d descriptor ..........\n", 1);
	PRINT_ONE(DC_HCIDMA_START+hcidma_drxdesc_offset[priv->hcidma_drxdesc_num], "Base Address: %08x\n", 1);

    INDIRECT_READ(DC_HCIDMA_START+hcidma_drxdesc_offset[priv->hcidma_drxdesc_num] , addr);
    PRINT_SINGL_ARG("DC_DRXBD_ADDR:   ", addr, "%08x");
    INDIRECT_READ(DC_HCIDMA_START+hcidma_drxdesc_offset[priv->hcidma_drxdesc_num]+(priv->hcidma_drxdesc_num? 0x14: 0x18), num);
    PRINT_SINGL_ARG("DC_DRXBD_NUM:    ", num, "%08x");
    INDIRECT_READ(DC_HCIDMA_START+hcidma_drxdesc_offset[priv->hcidma_drxdesc_num]+4, idx);
    PRINT_SINGL_ARG("DC_DRXBD_HW_IDX: ", idx, "%08x");

	for (i=0; i<num; i++) {
		PRINT_ONE(i, " DRXBD[%02d]  :", 0);

		for (j=0; j<8; j++) {
			INDIRECT_READ(addr+i*32+j*4, value[j]);
			PRINT_ONE(value[j], " %08x", 0);
		}
		PRINT_ONE("", "%s", 1);

		/* DRx desc */
		len = value[0] & 0xffff;
		if (len) {
			PRINT_ONE(" DRx desc:   ", "%s", 0);
			for (j=0; j<len; j+=4) {
				INDIRECT_READ(value[1]+j, desc);
				PRINT_ONE(desc, " %08x", 0);

				if ((j+4)%32 == 0) {
					PRINT_ONE("", "%s", 1);
					PRINT_ONE(" 	    ", "%s", 0);
				}
			}
			PRINT_ONE("", "%s", 1);
		}

		/* Mac header */
		len = value[2] & 0xffff;
		len = (len > 32) ? 32 : len;
		if (len) { //DRx desc
			PRINT_ONE(" MAC header:", "%s", 0);
			for (j=0; j<len; j+=4) {
				INDIRECT_READ(value[3]+j, desc);
				PRINT_ONE(desc, " %08x", 0);
			}
			PRINT_ONE("", "%s", 1);
		}

		/* AMSDU info */
		len = value[4] & 0xffff;
		if ((value[4] & 0x80000000) && len) { // AMSDU info
			PRINT_ONE(" AMSDU info:", "%s", 0);
			for (j=0; j<len; j+=4) {
				INDIRECT_READ(value[5]+j, desc);
				PRINT_ONE(desc, " %08x", 0);
			}
			PRINT_ONE("", "%s", 1);
		}

		PRINT_ONE(" ----", "%s", 1);
	}
	return pos;
}

#ifdef __ECOS
void rtl8192cd_proc_ofld_hcidma_drxdesc_idx_write(int txdesc_num, void *data)
#else
static int rtl8192cd_proc_ofld_hcidma_drxdesc_idx_write(struct file *file, const char *buffer,
		unsigned long count, void *data)
#endif
{
	struct net_device *dev = (struct net_device *)data;
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
#ifdef __KERNEL__
	char tmp[32];
#endif


	if (count < 2)
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, 32)) {
		int num = sscanf(tmp, "%d", &priv->hcidma_drxdesc_num);

		if (num != 1)
			panic_printk("Invalid drx desc number!\n");
		else if (priv->hcidma_drxdesc_num >= HCIDMA_REG_MAX_NUM) 
		{
			panic_printk("Invalid hcidma drx desc number!\n");
			priv->hcidma_drxdesc_num = 0;
		}
		else
			panic_printk("Ready to dump hcidma drx desc %d\n", priv->hcidma_drxdesc_num);
	}
	return count;

}

#define RPT_CTRL_INFO           0x18660000
#define RPT_REPORT              0x18662000
#define RPT_MU_SCORE_TABLE      0x18663000

#ifdef CONFIG_RTL_PROC_NEW
    static int rtl8192cd_proc_muinfo(struct seq_file *s, void *data)
#else
    static int rtl8192cd_proc_muinfo(char *buf, char **start, off_t offset,
                int length, int *eof, void *data)
#endif
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;
    int i, j, idx = 0, num_sta;
    unsigned int val32;
    unsigned char mu_rate[4][128];
    int pri, sec1, sec2, start_ofst;

    PRINT_SINGL_ARG("assoc_num: ", priv->assoc_num, "%d");
    num_sta = 3;//priv->assoc_num;
    PRINT_ONE("Ctrl Info 660", "%s", 1);
    for (i = 16; i < ((num_sta + 1) * 16); i++) { /* dump MACID 1 ~ 3 */
        if (i % 4 == 0)
            PRINT_ONE((0x8000 + (i * 4)), "%04x | ", 0);
        INDIRECT_READ(RPT_CTRL_INFO + (i * 4), val32);
        PRINT_ONE(val32, "%08x ", 0);
        if (i % 4 == 3)
            PRINT_ONE("", "%s", 1);
        if (i % 16 == 15)
            PRINT_ONE("------------------------------------------", "%s", 1);
    }
    PRINT_ONE("", "%s", 1);

    PRINT_ONE("MU score table 663", "%s", 1);
    for (i = 0; i < 4; i++) {
        PRINT_ONE((0x8000 + (i * 16)), "%04x | ", 0);
        for (j = 0; j < 4; j++) {
            INDIRECT_READ(RPT_MU_SCORE_TABLE + (i * 16) + (j * 4), val32);
            PRINT_ONE(val32, "%08x ", 0);
        }
        PRINT_ONE("", "%s", 1);
    }
    PRINT_ONE("", "%s", 1);

    PRINT_ONE("Registers", "%s", 1);
    if (num_sta >= 2) {
        PRINT_ONE("1-1", "%s", 1);
        PRINT_SINGL_ARG("  2f20 23: ", RTL_R32(0x2f20), "%08x");
        PRINT_SINGL_ARG("  2f2c 32: ", RTL_R32(0x2f2c), "%08x");
        PRINT_SINGL_ARG("  2f24 24: ", RTL_R32(0x2f24), "%08x");
        PRINT_SINGL_ARG("  2f38 42: ", RTL_R32(0x2f38), "%08x");
        PRINT_SINGL_ARG("  2f30 34: ", RTL_R32(0x2f30), "%08x");
        PRINT_SINGL_ARG("  2f3c 43: ", RTL_R32(0x2f3c), "%08x");
    }
    if (num_sta >= 3) {
        PRINT_ONE("1-1-1", "%s", 1);
        PRINT_SINGL_ARG("  2f54 234: ", RTL_R32(0x2f54), "%08x");
        PRINT_SINGL_ARG("  2f60 324: ", RTL_R32(0x2f60), "%08x");
        PRINT_SINGL_ARG("  2f6c 423: ", RTL_R32(0x2f6c), "%08x");
    }
    if (num_sta >= 4) {
        PRINT_ONE("1-1-1-1", "%s", 1);
        PRINT_SINGL_ARG("  2f70 1234: ", RTL_R32(0x2f70), "%08x");
        PRINT_SINGL_ARG("  2f74 2134: ", RTL_R32(0x2f74), "%08x");
        PRINT_SINGL_ARG("  2f78 3124: ", RTL_R32(0x2f78), "%08x");
        PRINT_SINGL_ARG("  2f7c 4123: ", RTL_R32(0x2f7c), "%08x");
    }

    return pos;
}

#ifdef CONFIG_RTL_PROC_NEW
    static int rtl8192cd_proc_murate(struct seq_file *s, void *data)
#else
    static int rtl8192cd_proc_murate(char *buf, char **start, off_t offset,
                int length, int *eof, void *data)
#endif
{
    struct net_device *dev = PROC_GET_DEV();
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    int pos = 0;
    int i, j, idx = 0, num_sta;
    unsigned int val32;
    unsigned char mu_rate[4][128], rate;
    int pri, sec1, sec2, start_ofst;
    
    PRINT_SINGL_ARG("assoc_num: ", priv->assoc_num, "%d");
    num_sta = 3;//priv->assoc_num;

    PRINT_ONE("rate index", "%s", 1);
    PRINT_ONE("    0  1  2  3  4  5  6  7  8  9", "%s", 1);
    PRINT_ONE("1S  2C 2D 2E 2F 30 31 32 33 34 35", "%s", 1);
    PRINT_ONE("2S  36 37 38 39 3A 3B 3C 3D 3E 3F", "%s", 1);
    PRINT_ONE("", "%s", 1);

    PRINT_ONE("MU rate table 663 offset 80", "%s", 1);
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 128; j += 4) {
            if (j == 0) {
                PRINT_ONE((0x8080 + (i * 128)), "%04x | ", 0);
            } else if (j % 32 == 0) {
                PRINT_ONE("\n     | ", "%s", 0);
            }
            INDIRECT_READ(RPT_MU_SCORE_TABLE + 0x80 + (i * 128) + j, val32);
            PRINT_ONE(val32, "%08x ", 0);
            *(unsigned int *)&mu_rate[i][j] = cpu_to_le32(val32);
        }
        PRINT_ONE("", "%s", 1);
    }
    PRINT_ONE("1-1", "%s", 1);
    for (pri = 0; pri < 4; pri++) {
        PRINT_ONE((128 + pri * 19), "%3d ", 0);
        PRINT_ONE((pri+1), "S%d | ", 0);
        start_ofst = 0;
        for (sec1 = 0; sec1 < 6; sec1++) {
            if (sec1 == pri)
                continue;
			if (sec1 > 3) {
				start_ofst += 2; /* 2 STA */
				continue;
			}
            PRINT_ONE(pri+1, "%d", 0);
            PRINT_ONE(sec1+1, "%d", 0);
			rate = mu_rate[pri][start_ofst++];
            if (rate >= 0x2C && rate <= 0x53) {
                PRINT_ONE((((rate - 0x2c) / 10) + 1) * 10 + ((rate - 0x2c) % 10), "(%02d-", 0);
            } else {
                PRINT_ONE("(  -", "%s", 0);
            }
			rate = mu_rate[pri][start_ofst++];
            if (rate >= 0x2C && rate <= 0x53) {
                PRINT_ONE((((rate - 0x2c) / 10) + 1) * 10 + ((rate - 0x2c) % 10), "%02d) ", 0);
            } else {
                PRINT_ONE("  ) ", "%s", 0);
            }
        }
        PRINT_ONE("", "%s", 1);
    }

    PRINT_ONE("1-1-1", "%s", 1);
    for (pri = 0; pri < 4; pri++) {
        PRINT_ONE((134 + pri * 19), "%3d ", 0);
        PRINT_ONE((pri+1), "S%d | ", 0);
        start_ofst = 16;
        for (sec1 = 0; sec1 < 6; sec1++) {
            if (sec1 == pri)
                continue;
            for (sec2 = sec1 + 1; sec2 < 6; sec2++) {
                if (sec2 == pri)
                    continue;
				if (sec1 > 3 || sec2 > 3) {
					start_ofst += 4; /* 3 STA + 1 rsvd */
					continue;
				}
                PRINT_ONE(pri+1, "%d", 0);
                PRINT_ONE(sec1+1, "%d", 0);
                PRINT_ONE(sec2+1, "%d", 0);
				rate = mu_rate[pri][start_ofst++];
                if (rate >= 0x2C && rate <= 0x53) {
                    PRINT_ONE((((rate - 0x2c) / 10) + 1) * 10 + ((rate - 0x2c) % 10), "(%02d-", 0);
                } else {
                    PRINT_ONE("(  -", "%s", 0);
                }
				rate = mu_rate[pri][start_ofst++];
                if (rate >= 0x2C && rate <= 0x53) {
                    PRINT_ONE((((rate - 0x2c) / 10) + 1) * 10 + ((rate - 0x2c) % 10), "%02d-", 0);
                } else {
                    PRINT_ONE("  -", "%s", 0);
                }
				rate = mu_rate[pri][start_ofst++];
                if (rate >= 0x2C && rate <= 0x53) {
                    PRINT_ONE((((rate - 0x2c) / 10) + 1) * 10 + ((rate - 0x2c) % 10), "%02d) ", 0);
                } else {
                    PRINT_ONE("  ) ", "%s", 0);
                }
                start_ofst++; //dummy
            }
        }
        PRINT_ONE("", "%s", 1);
    }

    return pos;
}
#endif // CONFIG_RTL_OFFLOAD_DRIVER

#ifdef CONFIG_RTL_PROC_NEW
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_all);
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_get_temperature, rtl8192cd_proc_set_temperature);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_rf);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_phoebus_rf);
	RTK_DECLARE_WRITE_PROC_OPS(rtl8192cd_proc_phoebus_poke);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_operation);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_staconfig);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_dkeytbl);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_auth);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_pmk);
	#ifdef AUTH_SAE
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_sae_peertable);
	#endif
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_gkeytbl);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_bssdesc);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_stainfo);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_sta_keyinfo);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_wmm);
#if defined(CONFIG_RTL_MONITOR_STA_INFO)
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_monitor_stainfo);
#endif
#if defined(AP_NEIGHBOR_INFO)
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_ap_neighbor_info);
#endif
#if defined(BEACON_VS_IE)
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_specific_ap_probereq_info);
#endif
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_sta_queinfo);
#endif
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_sta_dbginfo);
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_stats, rtl8192cd_proc_stats_clear);
#ifdef CTC_WIFI_DIAG
	RTK_DECLARE_READ_WRITE_PROC_OPS(ctcwifi_diag_enable_read, ctcwifi_diag_enable_write);
	RTK_DECLARE_READ_WRITE_PROC_OPS(ctcwifi_diag_duration_read, ctcwifi_diag_duration_write);
	RTK_DECLARE_READ_PROC_OPS(ctcwifi_proc_diag_log);
	RTK_DECLARE_READ_PROC_OPS(ctcwifi_proc_association_errors);
	RTK_DECLARE_READ_PROC_OPS(ctcwifi_proc_stats);
	RTK_DECLARE_READ_PROC_OPS(ctcwifi_proc_channel_occupancy);
	RTK_DECLARE_READ_PROC_OPS(ctcwifi_proc_sta_stats);
#endif

#if defined(CH_LOAD_CAL) && defined(USE_OUT_SRC)
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_ch_load);
#endif

	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_erp);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_probe_info);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_acs_score_info);//ACS_SCORE_PROC
#ifdef CONFIG_CMCC_FEATURE_SUPPORT
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_sta_cap_table);
#endif
#ifdef STA_ASSOC_STATISTIC
	 RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_reject_assoc_info);
	 RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_rm_assoc_info);
	 RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_assoc_status_info);
#endif
#ifdef PROC_STA_CONN_FAIL_INFO
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_sta_conn_fail);
#endif
#ifdef BSS_MONITOR_SUPPORT
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_monitor_info, rtl8192cd_proc_monitor_info_clear);
#endif
#ifdef STA_RATE_STATISTIC
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_sta_rateinfo);
#endif
#ifdef CONFIG_RTL_WLAN_DIAGNOSTIC
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_diagnostic);
#endif
#ifdef HS2_SUPPORT
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_hs2);
#endif
#ifdef WDS
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_wds);
#endif

#ifdef RTK_BR_EXT
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_brext);
    RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_nat25filter_read, rtl8192cd_proc_nat25filter_write);
#endif

#ifdef DOT11K
    RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_ap_channel_report_read, rtl8192cd_proc_ap_channel_report_write);
    RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_neighbor_read, rtl8192cd_proc_neighbor_write);
#endif

#ifdef CONFIG_IEEE80211V
    RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_transition_read, rtl8192cd_proc_transition_write);
#endif

#if (BEAMFORMING_SUPPORT == 1)
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_txbf);
#endif
#ifdef DFS
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_DFS);
#endif

#ifdef RTK_AC_SUPPORT
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_rf_ac);
#endif

#ifdef IDLE_NOISE_LEVEL
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_noise_level);
#endif

	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_misc);

#ifdef WIFI_SIMPLE_CONFIG
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_wsc);
#endif

#ifdef GBWC
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_gbwc);
#endif

#ifdef SBWC
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_sbwc);
#endif

#ifdef CONFIG_RTK_BAND_STEERING
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_get_band_steering_block_list);
#endif

#ifdef RTK_MULTI_AP
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_get_map_block_list);
#endif

	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_11n);

#ifdef RTL_MANUAL_EDCA
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_edca);
#endif
#ifdef CONFIG_RTK_VLAN_SUPPORT
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_vlan_read, rtl8192cd_proc_vlan_write);
#endif
#ifdef SUPPORT_MULTI_PROFILE
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mib_ap_profile);
#endif

#ifdef CONFIG_PCI_HCI
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_txdesc_info, rtl8192cd_proc_txdesc_idx_write);
#endif
#ifdef CLIENT_MODE
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_up_read, rtl8192cd_proc_up_write);
#endif

#if defined(CONFIG_PCI_HCI)
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_rxdesc_info);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_desc_info);
#endif
#ifdef CONFIG_USB_HCI
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_txurb_info, rtl8192cd_proc_txurb_info_idx_write);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_que_info);
#endif
#ifdef CONFIG_SDIO_HCI
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_que_info);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_sdio_dbginfo);
#endif
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_buf_info);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_cam_info);
#ifdef MULTI_MAC_CLONE
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mbidcam_info);
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_mstainfo);
#endif
#ifdef ENABLE_RTL_SKB_STATS
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_skb_info);
#endif
#ifdef RF_FINETUNE
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_rfft);
#endif
	RTK_DECLARE_WRITE_PROC_OPS(rtl8192cd_proc_led);
#ifdef AUTO_TEST_SUPPORT
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_SSR_read);
#endif
#if defined(RTLWIFINIC_GPIO_CONTROL)
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_gpio_ctrl_read, rtl8192cd_proc_gpio_ctrl_write);
#endif
#ifdef CONFIG_RTL_WLAN_STATUS
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_up_event_read, rtl8192cd_proc_up_event_write);
#endif

#ifdef A4_STA
    RTK_DECLARE_READ_PROC_OPS(a4_dump_sta_info);
#endif

#ifdef STA_CONTROL
    RTK_DECLARE_READ_WRITE_PROC_OPS(stactrl_info_read, stactrl_info_write);
#endif

#ifdef CONFIG_RTK_MESH
#ifdef MESH_BOOTSEQ_AUTH
	RTK_DECLARE_READ_PROC_OPS(mesh_auth_mpinfo);
#endif
	RTK_DECLARE_READ_PROC_OPS(mesh_unEstablish_mpinfo);
	RTK_DECLARE_READ_PROC_OPS(mesh_assoc_mpinfo);
	RTK_DECLARE_READ_PROC_OPS(mesh_stats);

	// 6 line for Throughput statistics (sounder)
	RTK_DECLARE_READ_WRITE_PROC_OPS(mesh_proc_flow_stats_read, mesh_proc_flow_stats_write);

	RTK_DECLARE_READ_PROC_OPS(mesh_pathsel_routetable_info);
	RTK_DECLARE_READ_PROC_OPS(mesh_proxy_table_info);
	RTK_DECLARE_READ_PROC_OPS(mesh_portal_table_info);
	RTK_DECLARE_READ_PROC_OPS(mesh_root_info);

#ifdef MESH_USE_METRICOP
	// change metric method
	RTK_DECLARE_READ_WRITE_PROC_OPS(mesh_metric_r, mesh_metric_w);
#endif

#ifdef _MESH_DEBUG_
	RTK_DECLARE_READ_PROC_OPS(mesh_proc_clear_table);
#ifdef MESH_BOOTSEQ_AUTH
	RTK_DECLARE_READ_PROC_OPS(mesh_proc_issueAuthReq);
	RTK_DECLARE_READ_PROC_OPS(mesh_proc_issueAuthRsp);
	RTK_DECLARE_READ_PROC_OPS(mesh_proc_issueDeAuth);
#endif
	RTK_DECLARE_READ_PROC_OPS(mesh_proc_openConnect);
	RTK_DECLARE_READ_PROC_OPS(mesh_proc_issueOpen);
	RTK_DECLARE_READ_PROC_OPS(mesh_proc_issueConfirm);
	RTK_DECLARE_READ_PROC_OPS(mesh_proc_issueClose);
	RTK_DECLARE_READ_PROC_OPS(mesh_proc_closeConnect);
	RTK_DECLARE_WRITE_PROC_OPS(mesh_setMACAddr);
#endif // _MESH_DEBUG_
#endif // CONFIG_RTK_MESH

#ifdef RTK_NL80211
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_psd_scan_read, rtl8192cd_proc_psd_scan_write);
#endif
#ifdef BT_COEXIST
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_bt_coexist);
#endif
#ifdef CONFIG_IEEE80211R
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_ft_info);
#endif

#ifdef USE_OUT_SRC
	RTK_DECLARE_READ_WRITE_PROC_OPS( proc_get_phydm_cmd, proc_set_phydm_cmd);
#endif

#ifdef DROP_RXPKT
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_drop_rxpkt_rate);
	RTK_DECLARE_WRITE_PROC_OPS(rtl8192cd_proc_drop_rxpkt_rate_w);
#endif
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_thermal);
#ifdef RTK_ATM
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_atm);
#endif
#ifdef SW_TXQ_ATF
	RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_swq_atf_read, rtl8192cd_proc_swq_atf_write);
#endif
#ifdef CONFIG_RTL_OFFLOAD_DRIVER
    RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_ofld);
    RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_ofld_cpu);
    RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_ofld_system);
    RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_ofld_pkt_mgnt);
    RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_ofld_dma_info);
    RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_ofld_axidma_txdesc, rtl8192cd_proc_ofld_axidma_txdesc_idx_write);
    RTK_DECLARE_READ_WRITE_PROC_OPS(rtl8192cd_proc_ofld_hcidma_drxdesc, rtl8192cd_proc_ofld_hcidma_drxdesc_idx_write);
    RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_muinfo);
    RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_murate);
#endif // CONFIG_RTL_OFFLOAD_DRIVER
#endif // CONFIG_RTL_PROC_NEW

#ifdef CONFIG_RTL_OFFLOAD_DRIVER
#ifdef CONFIG_RTL_PROC_NEW
static int rtl8192cd_proc_halq_info(struct seq_file *s, void *data)
#else
static int rtl8192cd_proc_halq_info(char *buf, char **start, off_t offset,
			int length, int *eof, void *data)
#endif
{
	struct net_device *dev = PROC_GET_DEV();
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	int pos = 0;
	PHCI_RX_DMA_MANAGER_88XX prx_dma = (PHCI_RX_DMA_MANAGER_88XX)(_GET_HAL_DATA(priv)->PRxDMA88XX);
	PHCI_TX_DMA_MANAGER_88XX ptx_dma = (PHCI_TX_DMA_MANAGER_88XX)(_GET_HAL_DATA(priv)->PTxDMA88XX);
	int i, halQnum;

	PRINT_ONE("Resource Info.", "%s", 1);
	PRINT_SINGL_ARG("  DESC cpu_addr:   ", _GET_HAL_DATA(priv)->alloc_dma_buf, "%p");
	PRINT_SINGL_ARG("  DESC dma_handle: ", _GET_HAL_DATA(priv)->ring_dma_addr, "%p");
	PRINT_SINGL_ARG("  DESC dma_len:    ", _GET_HAL_DATA(priv)->ring_buf_len, "%p");
	PRINT_SINGL_ARG("  FWDL cpu_addr:   ", _GET_HAL_DATA(priv)->h2d_fwdl_cpu_addr, "%p");
	PRINT_SINGL_ARG("  FWDL dma_handle: ", _GET_HAL_DATA(priv)->h2d_fwdl_dma_handle, "%p");
	PRINT_SINGL_ARG("  TxDMA manager:   ", _GET_HAL_DATA(priv)->PTxDMA88XX, "%p");
	PRINT_SINGL_ARG("  RxDMA manager:   ", _GET_HAL_DATA(priv)->PRxDMA88XX, "%p");
#if 0
{ // dump RxBD
	PRX_BUFFER_DESCRIPTOR ptr;
	ptr = (PRX_BUFFER_DESCRIPTOR)prx_dma->rx_queue[0].pRXBD_head;
	for (i=0; i< RX_DESC_NUM; i++, ptr++) {
		printk("[%3d] %p %08x %08x\n", i, ptr, ptr->Dword0, ptr->Dword1);
	}
}
#endif
	PRINT_ONE("Rx", "%s", 1);
	PRINT_SINGL_ARG("  Buffer Len:   ", RX_BUF_LEN, "%d");
	PRINT_SINGL_ARG("  size of BD:   ", sizeof(RX_BUFFER_DESCRIPTOR), "%d");
	for(i=0; i<HCI_RX_DMA_QUEUE_MAX_NUM; i++) {
		PRINT_SINGL_ARG("  DRV Queue: ", i, "%d");
		PRINT_SINGL_ARG("    pRXBD_head:     ", prx_dma->rx_queue[i].pRXBD_head, "%p");
		PRINT_SINGL_ARG("    avail_rxbd_num: ", prx_dma->rx_queue[i].avail_rxbd_num, "%d");
		PRINT_SINGL_ARG("    total_txbd_num: ", prx_dma->rx_queue[i].total_rxbd_num, "%d");
		PRINT_SINGL_ARG("    hw_idx:         ", prx_dma->rx_queue[i].hw_idx, "%d");
		PRINT_SINGL_ARG("    host_idx:       ", prx_dma->rx_queue[i].host_idx, "%d");
		PRINT_SINGL_ARG("    cur_host_idx:   ", prx_dma->rx_queue[i].cur_host_idx, "%d");
		PRINT_SINGL_ARG("    reg_rwptr_idx:  ", prx_dma->rx_queue[i].reg_rwptr_idx, "%x");
	}

	PRINT_ONE("Tx", "%s", 1);
	PRINT_SINGL_ARG("  size of BD:   ", sizeof(TX_BUFFER_DESCRIPTOR), "%d");
	for(i=0; i<=CMD_QUEUE; i++) {
		halQnum = GET_HAL_INTERFACE(priv)->MappingTxQueueHandler(priv, i);
		PRINT_SINGL_ARG("  DRV Queue: ", i, "%d");
		PRINT_SINGL_ARG("  HAL Queue: ", halQnum, "%d");
		PRINT_SINGL_ARG("    pTXBD_head:     ", ptx_dma->tx_queue[halQnum].pTXBD_head, "%p");
		PRINT_SINGL_ARG("    ptx_desc_head:  ", ptx_dma->tx_queue[halQnum].ptx_desc_head, "%p");
		PRINT_SINGL_ARG("    avail_txbd_num: ", rtl_atomic_read(&(ptx_dma->tx_queue[halQnum].avail_txbd_num)), "%d");
		PRINT_SINGL_ARG("    total_txbd_num: ", ptx_dma->tx_queue[halQnum].total_txbd_num, "%d");
		PRINT_SINGL_ARG("    hw_idx:         ", ptx_dma->tx_queue[halQnum].hw_idx, "%d");
		PRINT_SINGL_ARG("    host_idx:       ", ptx_dma->tx_queue[halQnum].host_idx, "%d");
		PRINT_SINGL_ARG("    reg_rwptr_idx:  ", ptx_dma->tx_queue[halQnum].reg_rwptr_idx, "%x");
	}

#if defined(CONFIG_8814B_FWLOG_UDPSOCK)
{
	extern int fwlog_udp_send(u8 *buf, int len);
	fwlog_udp_send("hello world", 12);
}
#endif

	return pos;
}

	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_halq_info);
#endif

#ifdef CONFIG_RTL_WLAN_DIAGNOSTIC
char diag_log_buff[DIAGNOSTIC_LOG_SIZE];
char tmp_log[128];
#endif

#ifdef THERMAL_CONTROL
	RTK_DECLARE_READ_PROC_OPS(rtl8192cd_proc_thermal_control);
#endif

#ifdef __KERNEL__
void MDL_DEVINIT rtl8192cd_proc_init(struct net_device *dev)
{
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	struct proc_dir_entry *rtl8192cd_proc_root = NULL;
	struct proc_dir_entry *p;

	rtl8192cd_proc_root = proc_mkdir(dev->name, NULL);
	priv->proc_root = rtl8192cd_proc_root;
	if (rtl8192cd_proc_root == NULL) {
		printk("create proc root failed!\n");
		return;
	}

	RTK_CREATE_PROC_READ_ENTRY(p, "mib_all", rtl8192cd_proc_mib_all);

#ifdef CONFIG_ARCH_LUNA_SLAVE
	p->size = 0x3000;
#endif

#ifdef CONFIG_RTL_WLAN_DIAGNOSTIC
	RTK_CREATE_PROC_READ_ENTRY(p, "diagnostic", rtl8192cd_proc_diagnostic);
#endif

	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "temperature", rtl8192cd_proc_get_temperature, rtl8192cd_proc_set_temperature);
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_rf", rtl8192cd_proc_mib_rf);
	RTK_CREATE_PROC_READ_ENTRY(p, "phoebus_rf", rtl8192cd_proc_phoebus_rf);
	RTK_CREATE_PROC_WRITE_ENTRY(p, "phoebus_poke", rtl8192cd_proc_phoebus_poke);

#ifdef IDLE_NOISE_LEVEL
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_noise_level", rtl8192cd_proc_mib_noise_level);
#endif

	RTK_CREATE_PROC_READ_ENTRY(p, "mib_operation", rtl8192cd_proc_mib_operation);
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_staconfig", rtl8192cd_proc_mib_staconfig);
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_dkeytbl", rtl8192cd_proc_mib_dkeytbl);
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_auth", rtl8192cd_proc_mib_auth);
	RTK_CREATE_PROC_READ_ENTRY(p, "pmkcache", rtl8192cd_proc_pmk);

#ifdef AUTH_SAE
	RTK_CREATE_PROC_READ_ENTRY(p, "sae_peertable", rtl8192cd_proc_sae_peertable);
#endif

	RTK_CREATE_PROC_READ_ENTRY(p, "mib_gkeytbl", rtl8192cd_proc_mib_gkeytbl);
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_bssdesc", rtl8192cd_proc_mib_bssdesc);
	RTK_CREATE_PROC_READ_ENTRY(p, "sta_info", rtl8192cd_proc_stainfo);

	RTK_CREATE_PROC_READ_ENTRY(p, "wmm", rtl8192cd_proc_wmm);

#ifdef CONFIG_ARCH_LUNA_SLAVE
	p->size = 0x3000;
#endif

	RTK_CREATE_PROC_READ_ENTRY(p, "sta_keyinfo", rtl8192cd_proc_sta_keyinfo);

#if defined(CONFIG_RTL_MONITOR_STA_INFO)
	RTK_CREATE_PROC_READ_ENTRY(p, "monitor_sta_info", rtl8192cd_proc_monitor_stainfo);
#endif

#if defined(AP_NEIGHBOR_INFO)	
	RTK_CREATE_PROC_READ_ENTRY(p, "ap_neighbor_info", rtl8192cd_proc_ap_neighbor_info);
#endif

#if defined(BEACON_VS_IE)
	RTK_CREATE_PROC_READ_ENTRY(p, "ap_probereq_info", rtl8192cd_proc_specific_ap_probereq_info);
#endif

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
	if (GET_HCI_TYPE(priv) == RTL_HCI_USB || GET_HCI_TYPE(priv) == RTL_HCI_SDIO) {
		RTK_CREATE_PROC_READ_ENTRY(p, "sta_queinfo", rtl8192cd_proc_sta_queinfo);
	}
#endif

	RTK_CREATE_PROC_READ_ENTRY(p, "sta_dbginfo", rtl8192cd_proc_sta_dbginfo);

#ifdef CONFIG_ARCH_LUNA_SLAVE
	p->size = 0x3000;
#endif

	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "stats", rtl8192cd_proc_stats, rtl8192cd_proc_stats_clear); 	

#if defined(CH_LOAD_CAL) && defined(USE_OUT_SRC)
	RTK_CREATE_PROC_READ_ENTRY(p, "ch_load", rtl8192cd_proc_ch_load);
#endif

	RTK_CREATE_PROC_READ_ENTRY(p, "mib_erp", rtl8192cd_proc_mib_erp);

	RTK_CREATE_PROC_READ_ENTRY(p, "probe_info", rtl8192cd_proc_probe_info);
	RTK_CREATE_PROC_READ_ENTRY(p, "acs_score_info", rtl8192cd_proc_acs_score_info);//ACS_SCORE_PROC
#ifdef CONFIG_CMCC_FEATURE_SUPPORT
	RTK_CREATE_PROC_READ_ENTRY(p, "sta_cap_table", rtl8192cd_proc_sta_cap_table);
#endif

#ifdef STA_ASSOC_STATISTIC
	RTK_CREATE_PROC_READ_ENTRY(p, "reject_assoc_info", rtl8192cd_proc_reject_assoc_info);
	RTK_CREATE_PROC_READ_ENTRY(p, "rm_assoc_info", rtl8192cd_proc_rm_assoc_info);		
	RTK_CREATE_PROC_READ_ENTRY(p, "sta_assoc_status", rtl8192cd_proc_assoc_status_info);			
#endif

#ifdef PROC_STA_CONN_FAIL_INFO
	RTK_CREATE_PROC_READ_ENTRY(p, "sta_conn_fail", rtl8192cd_proc_sta_conn_fail);
#endif

#ifdef BSS_MONITOR_SUPPORT
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "monitor_status", rtl8192cd_proc_monitor_info, rtl8192cd_proc_monitor_info_clear);				
#endif

#ifdef STA_RATE_STATISTIC
	RTK_CREATE_PROC_READ_ENTRY(p, "sta_rateinfo", rtl8192cd_proc_sta_rateinfo);
#endif

#ifdef HS2_SUPPORT
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_hs2", rtl8192cd_proc_mib_hs2);
#endif

#ifdef WDS
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_wds", rtl8192cd_proc_mib_wds);
#endif

#ifdef RTK_BR_EXT
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_brext", rtl8192cd_proc_mib_brext);
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "nat25filter", rtl8192cd_proc_nat25filter_read, rtl8192cd_proc_nat25filter_write);
#endif

#ifdef DOT11K
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "rm_ap_channel_report", rtl8192cd_proc_ap_channel_report_read, rtl8192cd_proc_ap_channel_report_write);
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "rm_neighbor_report", rtl8192cd_proc_neighbor_read, rtl8192cd_proc_neighbor_write);
#endif

#ifdef CONFIG_IEEE80211V
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "wnm_transition_list", rtl8192cd_proc_transition_read, rtl8192cd_proc_transition_write);		
#endif

#if (BEAMFORMING_SUPPORT == 1)
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_txbf", rtl8192cd_proc_mib_txbf);
#endif

#ifdef DFS
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_dfs", rtl8192cd_proc_mib_DFS);
#endif

#ifdef RTK_AC_SUPPORT
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_rf_ac", rtl8192cd_proc_mib_rf_ac);
#endif

	RTK_CREATE_PROC_READ_ENTRY(p, "mib_misc", rtl8192cd_proc_mib_misc);

#ifdef WIFI_SIMPLE_CONFIG
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_wsc", rtl8192cd_proc_mib_wsc);
#endif

#if defined(GBWC) && defined(CONFIG_PCI_HCI)
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
		RTK_CREATE_PROC_READ_ENTRY(p, "mib_gbwc", rtl8192cd_proc_mib_gbwc);
	}
#endif

#ifdef SBWC
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_sbwc", rtl8192cd_proc_mib_sbwc);
#endif

#ifdef CONFIG_RTK_BAND_STEERING
	RTK_CREATE_PROC_READ_ENTRY(p, "band_steering_block_list", rtl8192cd_proc_get_band_steering_block_list);
#endif

#ifdef RTK_MULTI_AP
	RTK_CREATE_PROC_READ_ENTRY(p, "map_block_list", rtl8192cd_proc_get_map_block_list);
#endif

	RTK_CREATE_PROC_READ_ENTRY(p, "mib_11n", rtl8192cd_proc_mib_11n);

#ifdef RTL_MANUAL_EDCA
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_EDCA", rtl8192cd_proc_mib_edca);
#endif

#ifdef CONFIG_RTK_VLAN_SUPPORT
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "mib_vlan", rtl8192cd_proc_vlan_read, rtl8192cd_proc_vlan_write);
#endif

#ifdef TLN_STATS
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "wifi_conn_stats", proc_wifi_conn_stats, proc_wifi_conn_stats_clear);
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "ext_wifi_conn_stats", proc_ext_wifi_conn_stats, proc_ext_wifi_conn_stats_clear);
#endif

#ifdef SUPPORT_MULTI_PROFILE
	RTK_CREATE_PROC_READ_ENTRY(p, "mib_ap_profile", rtl8192cd_proc_mib_ap_profile);
#endif

#if defined(UNIVERSAL_REPEATER) || defined(MBSSID)
	if (IS_ROOT_INTERFACE(priv))  // is root interface
#endif
	{
#ifdef CONFIG_PCI_HCI
		if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
			RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "txdesc", rtl8192cd_proc_txdesc_info, rtl8192cd_proc_txdesc_idx_write);
			RTK_CREATE_PROC_READ_ENTRY(p, "rxdesc", rtl8192cd_proc_rxdesc_info);
			RTK_CREATE_PROC_READ_ENTRY(p, "desc_info", rtl8192cd_proc_desc_info);
		}
#endif // CONFIG_PCI_HCI

#if defined(CONFIG_USB_HCI)
		if (GET_HCI_TYPE(priv) == RTL_HCI_USB) {
			RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "txurb", rtl8192cd_proc_txurb_info, rtl8192cd_proc_txurb_info_idx_write);
			RTK_CREATE_PROC_READ_ENTRY(p, "que_info", rtl8192cd_proc_que_info);
		}
#endif

#if defined(CONFIG_SDIO_HCI)
		if (GET_HCI_TYPE(priv) == RTL_HCI_SDIO) {
			RTK_CREATE_PROC_READ_ENTRY(p, "que_info", rtl8192cd_proc_que_info);
			RTK_CREATE_PROC_READ_ENTRY(p, "sdio_dbginfo", rtl8192cd_proc_sdio_dbginfo);
		}
#endif

		RTK_CREATE_PROC_READ_ENTRY(p, "buf_info", rtl8192cd_proc_buf_info);
		RTK_CREATE_PROC_READ_ENTRY(p, "cam_info", rtl8192cd_proc_cam_info);

#ifdef CONFIG_ARCH_LUNA_SLAVE
		p->size = 0x3000;
#endif

#ifdef MULTI_MAC_CLONE
		RTK_CREATE_PROC_READ_ENTRY(p, "mbidcam_info", rtl8192cd_proc_mbidcam_info);
		RTK_CREATE_PROC_READ_ENTRY(p, "msta_info", rtl8192cd_proc_mstainfo);
#endif

#ifdef ENABLE_RTL_SKB_STATS
		RTK_CREATE_PROC_READ_ENTRY(p, "skb_info", rtl8192cd_proc_skb_info);
#endif

#ifdef RF_FINETUNE
		RTK_CREATE_PROC_READ_ENTRY(p, "rf_finetune", rtl8192cd_proc_rfft);
#endif

#ifdef CLIENT_MODE
		RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "up_flag", rtl8192cd_proc_up_read, rtl8192cd_proc_up_write);
#endif

#ifdef CONFIG_RTL_92C_SUPPORT 
#ifndef CONFIG_RTL_PROC_NEW
		if ((GET_CHIP_VER(priv) == VERSION_8192C) || (GET_CHIP_VER(priv) == VERSION_8188C)) {
			RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "phypara_file", rtl8192cd_proc_phypara_file_read, rtl8192cd_proc_phypara_file_write);
		}
#endif		
#endif // CONFIG_RTL_92C_SUPPORT

		RTK_CREATE_PROC_WRITE_ENTRY(p, "led", rtl8192cd_proc_led);		

#ifdef AUTO_TEST_SUPPORT
		RTK_CREATE_PROC_READ_ENTRY(p, "SS_Result", rtl8192cd_proc_SSR_read);
#endif

#ifdef RTLWIFINIC_GPIO_CONTROL
		RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "gpio_ctrl", rtl8192cd_proc_gpio_ctrl_read, rtl8192cd_proc_gpio_ctrl_write);

#ifdef CONFIG_ARCH_LUNA_SLAVE
		p->size = 0x10;
#endif
#endif // RTLWIFINIC_GPIO_CONTROL

#ifdef CONFIG_RTL_OFFLOAD_DRIVER
		RTK_CREATE_PROC_READ_ENTRY(p, "halq_info", rtl8192cd_proc_halq_info);
#endif
	}

#ifdef STA_CONTROL
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "stactrl_info", stactrl_info_read, stactrl_info_write);
#endif


#ifdef WLANHAL_MACDM
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "macdm", rtl8192cd_proc_macdm, rtl8192cd_proc_macdm_write);
#endif //WLANHAL_MACDM

#ifdef CONFIG_RTL_WLAN_STATUS
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "up_event", rtl8192cd_proc_up_event_read, rtl8192cd_proc_up_event_write);
#endif

#ifdef A4_STA
	RTK_CREATE_PROC_READ_ENTRY(p, "a4_sta_info", a4_dump_sta_info);
#endif

#ifdef ERR_ACCESS_CNTR
	RTK_CREATE_PROC_READ_ENTRY(p, "err_access", rtl8192cd_proc_err_access);
#endif

#ifdef CONFIG_RTK_MESH
#ifdef MESH_BOOTSEQ_AUTH
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_auth_mpinfo", mesh_auth_mpinfo);
#endif

	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_unestablish_mpinfo", mesh_unEstablish_mpinfo);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_assoc_mpinfo", mesh_assoc_mpinfo);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_stats", mesh_stats);

	// 6 line for Throughput statistics (sounder)
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "flow_stats", mesh_proc_flow_stats_read, mesh_proc_flow_stats_write);

	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_pathsel_routetable", mesh_pathsel_routetable_info);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_proxy_table", mesh_proxy_table_info);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_portal_table", mesh_portal_table_info);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_root_info", mesh_root_info);

#ifdef MESH_USE_METRICOP
	// change metric method
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "mesh_metric", mesh_metric_r, mesh_metric_w);		
#endif

#ifdef _MESH_DEBUG_
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_clearalltable", mesh_proc_clear_table);
#ifdef MESH_BOOTSEQ_AUTH
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_issueauthreq", mesh_proc_issueAuthReq);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_issueauthrsp", mesh_proc_issueAuthRsp);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_issuedeauth", mesh_proc_issueDeAuth);
#endif
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_openconnect", mesh_proc_openConnect);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_issueopen", mesh_proc_issueOpen);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_issueconfirm", mesh_proc_issueConfirm);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_issueclose", mesh_proc_issueClose);
	RTK_CREATE_PROC_READ_ENTRY(p, "mesh_closeconnect", mesh_proc_closeConnect);
	RTK_CREATE_PROC_WRITE_ENTRY(p, "mesh_setmacaddr", mesh_setMACAddr);
#endif // _MESH_DEBUG_
#endif // CONFIG_RTK_MESH

#ifdef RTK_NL80211
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "psd_scan", rtl8192cd_proc_psd_scan_read, rtl8192cd_proc_psd_scan_write); 	
#endif

#ifdef BT_COEXIST
	RTK_CREATE_PROC_READ_ENTRY(p, "bt_coexist", rtl8192cd_proc_bt_coexist);
#endif

#ifdef CONFIG_IEEE80211R
	RTK_CREATE_PROC_READ_ENTRY(p, "ft_info", rtl8192cd_proc_ft_info);	
#endif

#ifdef USE_OUT_SRC
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "cmd", proc_get_phydm_cmd, proc_set_phydm_cmd); 	
#endif

#ifdef THERMAL_CONTROL
	RTK_CREATE_PROC_READ_ENTRY(p, "thermal_control", rtl8192cd_proc_thermal_control);
#endif

#ifdef DROP_RXPKT
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "drop_rxpkt_rate", rtl8192cd_proc_drop_rxpkt_rate, rtl8192cd_proc_drop_rxpkt_rate_w); 
#ifndef CONFIG_RTL_PROC_NEW
	p->size = 0x1000;
#endif
#endif // DROP_RXPKT

	RTK_CREATE_PROC_READ_ENTRY(p, "thermal", rtl8192cd_proc_thermal);

#ifdef RTK_ATM
	RTK_CREATE_PROC_READ_ENTRY(p, "atm", rtl8192cd_proc_atm);
#endif

#ifdef SW_TXQ_ATF
	RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "swq_atf", rtl8192cd_proc_swq_atf_read, rtl8192cd_proc_swq_atf_write);
#endif

#ifdef CONFIG_RTL_OFFLOAD_DRIVER
	if (OFFLOAD_ENABLE(priv)) {
		RTK_CREATE_PROC_READ_ENTRY(p, "ofld", rtl8192cd_proc_ofld);
		RTK_CREATE_PROC_READ_ENTRY(p, "ofld_cpu", rtl8192cd_proc_ofld_cpu);
		RTK_CREATE_PROC_READ_ENTRY(p, "ofld_sys", rtl8192cd_proc_ofld_system);
		RTK_CREATE_PROC_READ_ENTRY(p, "ofld_pkt", rtl8192cd_proc_ofld_pkt_mgnt);
		RTK_CREATE_PROC_READ_ENTRY(p, "ofld_dma", rtl8192cd_proc_ofld_dma_info);
		RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "ofld_axidma_txdesc", rtl8192cd_proc_ofld_axidma_txdesc, rtl8192cd_proc_ofld_axidma_txdesc_idx_write);
		RTK_CREATE_PROC_READ_WRITE_ENTRY(p, "ofld_hcidma_drxdesc", rtl8192cd_proc_ofld_hcidma_drxdesc, rtl8192cd_proc_ofld_hcidma_drxdesc_idx_write);
		RTK_CREATE_PROC_READ_ENTRY(p, "muinfo", rtl8192cd_proc_muinfo);
		RTK_CREATE_PROC_READ_ENTRY(p, "murate", rtl8192cd_proc_murate);
	}
#endif //CONFIG_RTL_OFFLOAD_DRIVER
}

void MDL_EXIT rtl8192cd_proc_remove(struct net_device *dev)
{
	struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
	struct proc_dir_entry *rtl8192cd_proc_root = priv->proc_root;

	if (rtl8192cd_proc_root == NULL)
		return;

	remove_proc_entry("mib_all", rtl8192cd_proc_root);

#ifdef CONFIG_RTL_WLAN_DIAGNOSTIC
	remove_proc_entry("diagnostic", rtl8192cd_proc_root);
#endif

	remove_proc_entry("mib_rf", rtl8192cd_proc_root);

#ifdef IDLE_NOISE_LEVEL	
	remove_proc_entry("mib_noise_level", rtl8192cd_proc_root);
#endif	

	remove_proc_entry("mib_operation", rtl8192cd_proc_root);
	remove_proc_entry("mib_staconfig", rtl8192cd_proc_root);
	remove_proc_entry("mib_dkeytbl", rtl8192cd_proc_root);
	remove_proc_entry("mib_auth", rtl8192cd_proc_root);
	remove_proc_entry("pmkcache", rtl8192cd_proc_root);

#ifdef AUTH_SAE
	remove_proc_entry("sae_peertable", rtl8192cd_proc_root);
#endif

	remove_proc_entry("mib_gkeytbl", rtl8192cd_proc_root);
	remove_proc_entry("mib_bssdesc", rtl8192cd_proc_root);
	remove_proc_entry("sta_info", rtl8192cd_proc_root);

	remove_proc_entry("wmm", rtl8192cd_proc_root);

	remove_proc_entry("sta_keyinfo", rtl8192cd_proc_root);

#if defined(CONFIG_RTL_MONITOR_STA_INFO)
	remove_proc_entry("monitor_sta_info", rtl8192cd_proc_root);
#endif

#if defined(AP_NEIGHBOR_INFO)	
	remove_proc_entry("ap_neighbor_info", rtl8192cd_proc_root);
#endif

#if defined(BEACON_VS_IE)
	remove_proc_entry("ap_probereq_info", rtl8192cd_proc_root);
#endif

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
	if (GET_HCI_TYPE(priv) == RTL_HCI_USB || GET_HCI_TYPE(priv) == RTL_HCI_SDIO) {
		remove_proc_entry("sta_queinfo", rtl8192cd_proc_root);
	}
#endif

	remove_proc_entry("sta_dbginfo", rtl8192cd_proc_root);

	remove_proc_entry("stats", rtl8192cd_proc_root);

#if defined(CH_LOAD_CAL) && defined(USE_OUT_SRC)
	remove_proc_entry("ch_load", rtl8192cd_proc_root);
#endif

	remove_proc_entry("mib_erp", rtl8192cd_proc_root);

	remove_proc_entry("probe_info", rtl8192cd_proc_root);
	remove_proc_entry("acs_score_info", rtl8192cd_proc_root); //ACS_SCORE_PROC
#ifdef CONFIG_CMCC_FEATURE_SUPPORT
	remove_proc_entry("sta_cap_table", rtl8192cd_proc_sta_cap_table);
#endif

#ifdef STA_ASSOC_STATISTIC
	remove_proc_entry("reject_assoc_info", rtl8192cd_proc_root);
	remove_proc_entry("rm_assoc_info", rtl8192cd_proc_root);
	remove_proc_entry("sta_assoc_status", rtl8192cd_proc_root);
#endif

#ifdef PROC_STA_CONN_FAIL_INFO
	remove_proc_entry("sta_conn_fail", rtl8192cd_proc_root);
#endif

#ifdef BSS_MONITOR_SUPPORT
	remove_proc_entry("monitor_status", rtl8192cd_proc_root);
#endif	

#ifdef STA_RATE_STATISTIC
	remove_proc_entry("sta_rateinfo", rtl8192cd_proc_root);
#endif

#ifdef HS2_SUPPORT
	remove_proc_entry("mib_hs2", rtl8192cd_proc_root);
#endif

#ifdef WDS
	remove_proc_entry("mib_wds", rtl8192cd_proc_root);
#endif

#ifdef RTK_BR_EXT
	remove_proc_entry("mib_brext", rtl8192cd_proc_root);
	remove_proc_entry("nat25filter", rtl8192cd_proc_root);
#endif

#ifdef DOT11K
	remove_proc_entry("rm_ap_channel_report", rtl8192cd_proc_root);
	remove_proc_entry("rm_neighbor_report", rtl8192cd_proc_root);
#endif

#ifdef CONFIG_IEEE80211V
	remove_proc_entry("wnm_transition_list", rtl8192cd_proc_root);
#endif

#if (BEAMFORMING_SUPPORT == 1)
	remove_proc_entry("mib_txbf", rtl8192cd_proc_root);
#endif

#ifdef DFS
	remove_proc_entry("mib_dfs", rtl8192cd_proc_root);
#endif

#ifdef RTK_AC_SUPPORT /*eric-8822*/
	remove_proc_entry("mib_rf_ac", rtl8192cd_proc_root);
#endif

	remove_proc_entry("mib_misc", rtl8192cd_proc_root);

#ifdef WIFI_SIMPLE_CONFIG
	remove_proc_entry("mib_wsc", rtl8192cd_proc_root);
#endif

#if defined(GBWC) && defined(CONFIG_PCI_HCI)
	if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
		remove_proc_entry("mib_gbwc", rtl8192cd_proc_root);
	}
#endif

#ifdef SBWC
	remove_proc_entry("mib_sbwc", rtl8192cd_proc_root);
#endif

#ifdef CONFIG_RTK_BAND_STEERING
	remove_proc_entry("band_steering_block_list", rtl8192cd_proc_root);
#endif

#ifdef RTK_MULTI_AP
	remove_proc_entry("map_block_list", rtl8192cd_proc_root);
#endif

	remove_proc_entry("mib_11n", rtl8192cd_proc_root);

#ifdef RTL_MANUAL_EDCA
	remove_proc_entry("mib_EDCA", rtl8192cd_proc_root);
#endif

#ifdef CONFIG_RTK_VLAN_SUPPORT
	remove_proc_entry("mib_vlan", rtl8192cd_proc_root);
#endif

#ifdef TLN_STATS
	remove_proc_entry("wifi_conn_stats", rtl8192cd_proc_root);
	remove_proc_entry("ext_wifi_conn_stats", rtl8192cd_proc_root);
#endif

#ifdef SUPPORT_MULTI_PROFILE
	remove_proc_entry("mib_ap_profile", rtl8192cd_proc_root);
#endif

#if defined(UNIVERSAL_REPEATER) || defined(MBSSID)
	if (IS_ROOT_INTERFACE(priv))  // is root interface
#endif
	{
#if defined(CONFIG_PCI_HCI)
		if (GET_HCI_TYPE(priv) == RTL_HCI_PCIE) {
			remove_proc_entry("txdesc", rtl8192cd_proc_root);
			remove_proc_entry("rxdesc", rtl8192cd_proc_root);
			remove_proc_entry("desc_info", rtl8192cd_proc_root);
		}
#endif

#if defined(CONFIG_USB_HCI)
		if (GET_HCI_TYPE(priv) == RTL_HCI_USB) {
			remove_proc_entry("txurb", rtl8192cd_proc_root);
			remove_proc_entry("que_info", rtl8192cd_proc_root);
		}
#endif

#if defined(CONFIG_SDIO_HCI)
		if (GET_HCI_TYPE(priv) == RTL_HCI_SDIO) {
			remove_proc_entry("que_info", rtl8192cd_proc_root);
			remove_proc_entry("sdio_dbginfo", rtl8192cd_proc_root);
		}
#endif

		remove_proc_entry("buf_info", rtl8192cd_proc_root);
		remove_proc_entry("cam_info", rtl8192cd_proc_root);

#ifdef MULTI_MAC_CLONE
		remove_proc_entry("mbidcam_info", rtl8192cd_proc_root);
		remove_proc_entry("msta_info", rtl8192cd_proc_root);
#endif

#ifdef ENABLE_RTL_SKB_STATS
		remove_proc_entry("skb_info", rtl8192cd_proc_root);
#endif

#ifdef RF_FINETUNE
		remove_proc_entry("rf_finetune", rtl8192cd_proc_root);
#endif

#ifdef CLIENT_MODE
		remove_proc_entry("up_flag", rtl8192cd_proc_root);
#endif

#ifdef CONFIG_RTL_92C_SUPPORT
#ifndef CONFIG_RTL_PROC_NEW
		if ((GET_CHIP_VER(priv) == VERSION_8192C) || (GET_CHIP_VER(priv) == VERSION_8188C)) {
			remove_proc_entry("phypara_file", rtl8192cd_proc_root);
		}
#endif
#endif // CONFIG_RTL_92C_SUPPORT

		remove_proc_entry("led", rtl8192cd_proc_root);

#ifdef AUTO_TEST_SUPPORT
		remove_proc_entry("SS_Result", rtl8192cd_proc_root);
#endif

#ifdef RTLWIFINIC_GPIO_CONTROL
		remove_proc_entry("gpio_ctrl", rtl8192cd_proc_root);
#endif

#ifdef CONFIG_RTL_OFFLOAD_DRIVER
		remove_proc_entry("halq_info", rtl8192cd_proc_root);
#endif
	}

#ifdef STA_CONTROL
	remove_proc_entry("stactrl_info", rtl8192cd_proc_root);
#endif

#ifdef WLANHAL_MACDM
	remove_proc_entry("macdm", rtl8192cd_proc_root);
#endif //WLANHAL_MACDM

#ifdef CONFIG_RTL_WLAN_STATUS
	remove_proc_entry("up_event", rtl8192cd_proc_root);
#endif

#ifdef A4_STA
	remove_proc_entry("a4_sta_info", rtl8192cd_proc_root);
#endif

#ifdef ERR_ACCESS_CNTR
	remove_proc_entry("err_access", rtl8192cd_proc_root);
#endif

#ifdef CONFIG_RTK_MESH
#ifdef MESH_BOOTSEQ_AUTH
	remove_proc_entry("mesh_auth_mpinfo", rtl8192cd_proc_root);
#endif

	remove_proc_entry("mesh_unestablish_mpinfo", rtl8192cd_proc_root);
	remove_proc_entry("mesh_assoc_mpinfo", rtl8192cd_proc_root);
	remove_proc_entry("mesh_stats", rtl8192cd_proc_root);

	remove_proc_entry("flow_stats", rtl8192cd_proc_root);

	remove_proc_entry("mesh_pathsel_routetable", rtl8192cd_proc_root);
	remove_proc_entry("mesh_proxy_table", rtl8192cd_proc_root);
	remove_proc_entry("mesh_root_info", rtl8192cd_proc_root);
	remove_proc_entry("mesh_portal_table", rtl8192cd_proc_root);

#ifdef MESH_USE_METRICOP // remove proc file
	remove_proc_entry("mesh_metric", rtl8192cd_proc_root);
#endif

#ifdef _MESH_DEBUG_
	remove_proc_entry("mesh_clearalltable", rtl8192cd_proc_root);
#ifdef MESH_BOOTSEQ_AUTH
	remove_proc_entry("mesh_issueauthreq", rtl8192cd_proc_root);
	remove_proc_entry("mesh_issueauthrsp", rtl8192cd_proc_root);
	remove_proc_entry("mesh_issuedeauth", rtl8192cd_proc_root);
#endif
	remove_proc_entry("mesh_openconnect", rtl8192cd_proc_root);
	remove_proc_entry("mesh_issueopen", rtl8192cd_proc_root);
	remove_proc_entry("mesh_issueconfirm", rtl8192cd_proc_root);
	remove_proc_entry("mesh_issueclose", rtl8192cd_proc_root);
	remove_proc_entry("mesh_closeconnect", rtl8192cd_proc_root);
	remove_proc_entry("mesh_setmacaddr", rtl8192cd_proc_root);
#endif // _MESH_DEBUG_
#endif // CONFIG_RTK_MESH

#ifdef RTK_NL80211
	remove_proc_entry("psd_scan", rtl8192cd_proc_root);
#endif

#ifdef BT_COEXIST
	remove_proc_entry("bt_coexist", rtl8192cd_proc_root);
#endif

#ifdef CONFIG_IEEE80211R
	remove_proc_entry("ft_info", rtl8192cd_proc_root);	
#endif

#ifdef USE_OUT_SRC
	remove_proc_entry("cmd", rtl8192cd_proc_root);
#endif

#ifdef THERMAL_CONTROL
	remove_proc_entry("thermal_control", rtl8192cd_proc_root);
#endif

#ifdef DROP_RXPKT
	remove_proc_entry("drop_rxpkt_rate", rtl8192cd_proc_root);
#endif

	remove_proc_entry("thermal", rtl8192cd_proc_root);

#ifdef RTK_ATM
	remove_proc_entry("atm", rtl8192cd_proc_root);
#endif

#ifdef SW_TXQ_ATF
	remove_proc_entry("swq_atf", rtl8192cd_proc_root);
#endif

#ifdef CONFIG_RTL_OFFLOAD_DRIVER
	if (OFFLOAD_ENABLE(priv)) {
		remove_proc_entry("ofld", rtl8192cd_proc_root);
		remove_proc_entry("ofld_cpu", rtl8192cd_proc_root);
		remove_proc_entry("ofld_sys", rtl8192cd_proc_root);
		remove_proc_entry("ofld_pkt", rtl8192cd_proc_root);
		remove_proc_entry("ofld_dma", rtl8192cd_proc_root);
		remove_proc_entry("ofld_axidma_txdesc", rtl8192cd_proc_root);
		remove_proc_entry("ofld_hcidma_drxdesc", rtl8192cd_proc_root);
		remove_proc_entry("muinfo", rtl8192cd_proc_root);
		remove_proc_entry("murate", rtl8192cd_proc_root);
	}
#endif // CONFIG_RTL_OFFLOAD_DRIVER

	remove_proc_entry(dev->name, NULL);
	rtl8192cd_proc_root = NULL;
	priv->proc_root = NULL;
}

#ifdef CTC_WIFI_DIAG
#if defined(CONFIG_BACKPORTED_CFG80211_MODULE) && defined(CONFIG_CTC_FEATURE) /* g6_wifi_driver's config */
extern struct proc_dir_entry *ctcwifi_proc_export;
#endif
struct proc_dir_entry *ctcwifi_proc_root = NULL;

void ctcwifi_priv_set(struct rtl8192cd_priv *priv)
{
	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
		ctcwifi_priv[CTCWIFI_2G] = priv;
#if defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8812FE) || defined(CONFIG_WLAN_HAL_8814BE)
	else
		ctcwifi_priv[CTCWIFI_5G] = priv;
#endif
}

void MDL_DEVINIT ctcwifi_proc_init(void)
{
	struct proc_dir_entry *ctc_root = NULL;
	struct proc_dir_entry *p;
	unsigned long *dev = NULL;

#if defined(CONFIG_BACKPORTED_CFG80211_MODULE) && defined(CONFIG_CTC_FEATURE) /* g6_wifi_driver's config */
	ctc_root = ctcwifi_proc_export;
#else
	ctc_root = proc_mkdir("ctcwifi", NULL);
#endif
	if (ctc_root == NULL) {
		printk("create ctc root failed!\n");
		return;
	}
	ctcwifi_proc_root = ctc_root;

	dev = (unsigned long *)&ctcwifi_priv[CTCWIFI_2G];
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "association_errors_2G", ctcwifi_proc_association_errors);
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "Stat2G", ctcwifi_proc_stats);
	CTCWIFI_CREATE_PROC_READ_WRITE_ENTRY(p, "diag_enable_2G", ctcwifi_diag_enable_read, ctcwifi_diag_enable_write);
	CTCWIFI_CREATE_PROC_READ_WRITE_ENTRY(p, "diag_duration_2G", ctcwifi_diag_duration_read, ctcwifi_diag_duration_write);
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "diag_log_2G", ctcwifi_proc_diag_log);
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "channel_occupancy_2G", ctcwifi_proc_channel_occupancy);
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "stats_2G", ctcwifi_proc_sta_stats);
	
	diag_log_record[CTCWIFI_2G] = kmalloc(sizeof(struct ctcwifi_diag_log_record) * DIAG_LOG_REC_MAXNUM, GFP_KERNEL);
	if (!diag_log_record[CTCWIFI_2G]) {
		printk("allocate diag_log_record failed\n");
		return;
	}
	memset(diag_log_record[CTCWIFI_2G], 0, sizeof(struct ctcwifi_diag_log_record) * DIAG_LOG_REC_MAXNUM);

	assoc_error_record[CTCWIFI_2G] = kmalloc(sizeof(struct ctcwifi_diag_log_record) * DIAG_LOG_REC_MAXNUM, GFP_KERNEL);
	if (!assoc_error_record[CTCWIFI_2G]) {
		printk("allocate assoc_error_record failed\n");
		return;
	}
	memset(assoc_error_record[CTCWIFI_2G], 0, sizeof(struct ctcwifi_diag_log_record) * DIAG_LOG_REC_MAXNUM);

#if defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8812FE) || defined(CONFIG_WLAN_HAL_8814BE)
	dev = (unsigned long *)&ctcwifi_priv[CTCWIFI_5G];
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "association_errors_5G", ctcwifi_proc_association_errors);
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "Stat5G", ctcwifi_proc_stats);
	CTCWIFI_CREATE_PROC_READ_WRITE_ENTRY(p, "diag_enable_5G", ctcwifi_diag_enable_read, ctcwifi_diag_enable_write);
	CTCWIFI_CREATE_PROC_READ_WRITE_ENTRY(p, "diag_duration_5G", ctcwifi_diag_duration_read, ctcwifi_diag_duration_write);
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "diag_log_5G", ctcwifi_proc_diag_log);
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "channel_occupancy_5G", ctcwifi_proc_channel_occupancy);
	CTCWIFI_CREATE_PROC_READ_ENTRY(p, "stats_5G", ctcwifi_proc_sta_stats);

	diag_log_record[CTCWIFI_5G] = kmalloc(sizeof(struct ctcwifi_diag_log_record) * DIAG_LOG_REC_MAXNUM, GFP_KERNEL);
	if (!diag_log_record[CTCWIFI_5G]) {
		printk("allocate diag_log_record failed\n");
		return;
	}
	memset(diag_log_record[CTCWIFI_5G], 0, sizeof(struct ctcwifi_diag_log_record) * DIAG_LOG_REC_MAXNUM);

	assoc_error_record[CTCWIFI_5G] = kmalloc(sizeof(struct ctcwifi_diag_log_record) * DIAG_LOG_REC_MAXNUM, GFP_KERNEL);
	if (!assoc_error_record[CTCWIFI_5G]) {
		printk("allocate assoc_error_record failed\n");
		return;
	}
	memset(assoc_error_record[CTCWIFI_5G], 0, sizeof(struct ctcwifi_diag_log_record) * DIAG_LOG_REC_MAXNUM);
#endif

	dev = NULL;

	rtk_timer_setup(&(ctcwifi_diag_timer), ctcwifi_diag_timer_func, 0UL, 0);
	rtk_mod_timer(&ctcwifi_diag_timer, jiffies + EXPIRE_TO);
}

void MDL_EXIT ctcwifi_proc_remove(void)
{
	if (ctcwifi_proc_root != NULL) {
		if (diag_log_record[CTCWIFI_2G])
			kfree(diag_log_record[CTCWIFI_2G]);
		if (assoc_error_record[CTCWIFI_2G])
			kfree(assoc_error_record[CTCWIFI_2G]);
		
		remove_proc_entry("diag_enable_2G", ctcwifi_proc_root);
		remove_proc_entry("diag_duration_2G", ctcwifi_proc_root);
		remove_proc_entry("diag_log_2G", ctcwifi_proc_root);
		remove_proc_entry("association_errors_2G", ctcwifi_proc_root);
		remove_proc_entry("Stat2G", ctcwifi_proc_root);
		remove_proc_entry("channel_occupancy_2G", ctcwifi_proc_root);
		remove_proc_entry("stats_2G", ctcwifi_proc_root);

#if defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8812FE) || defined(CONFIG_WLAN_HAL_8814BE)
		if (diag_log_record[CTCWIFI_5G])
			kfree(diag_log_record[CTCWIFI_5G]);
		if (assoc_error_record[CTCWIFI_5G])
			kfree(assoc_error_record[CTCWIFI_5G]);

		remove_proc_entry("diag_enable_5G", ctcwifi_proc_root);
		remove_proc_entry("diag_duration_5G", ctcwifi_proc_root);
		remove_proc_entry("diag_log_5G", ctcwifi_proc_root);
		remove_proc_entry("association_errors_5G", ctcwifi_proc_root);
		remove_proc_entry("Stat5G", ctcwifi_proc_root);
		remove_proc_entry("channel_occupancy_5G", ctcwifi_proc_root);
		remove_proc_entry("stats_5G", ctcwifi_proc_root);
#endif

		rtk_del_timer(&ctcwifi_diag_timer);

#if !defined(CONFIG_BACKPORTED_CFG80211_MODULE) && !defined(CONFIG_CTC_FEATURE) /* g6_wifi_driver's config */
		remove_proc_entry("ctcwifi", NULL);
#endif
		ctcwifi_proc_root = NULL;
	}
}

void ctcwifi_conn_record(struct rtl8192cd_priv *priv, unsigned char *mac, const unsigned char *msg, unsigned int msg_type)
{
#ifdef __LINUX_2_6_32__
	struct tm timeinfo;
#endif
	int i, n;
	unsigned char temp_buf[20];
	struct ctcwifi_diag_log_record *record = NULL;
	unsigned int *head, *tail;

	if (msg_type == CTCWIFI_TYPE_DIAG_LOG || msg_type == CTCWIFI_TYPE_FRAME_BODY) {
		if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
			n = CTCWIFI_2G;
#if defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8812FE) || defined(CONFIG_WLAN_HAL_8814BE)
		else
			n = CTCWIFI_5G;
#endif
		record = diag_log_record[n];
		head = &diag_log_head[n];
		tail = &diag_log_tail[n];
	}
	else if (msg_type == CTCWIFI_TYPE_ASSOC_ERR) {
		if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
			n = CTCWIFI_2G;
#if defined(CONFIG_RTL_8812_SUPPORT) || defined(CONFIG_WLAN_HAL_8822BE) || defined(CONFIG_WLAN_HAL_8812FE) || defined(CONFIG_WLAN_HAL_8814BE)
		else
			n = CTCWIFI_5G;
#endif
		record = assoc_error_record[n];
		head = &assoc_error_head[n];
		tail = &assoc_error_tail[n];
	}

	if (record == NULL)
		return;
	if ((msg_type == CTCWIFI_TYPE_DIAG_LOG) && !diag_enable[n])
		return;

#ifdef __LINUX_4_20__
	time64_to_tm(ktime_get_real_seconds()-((time64_t)sys_tz.tz_minuteswest * 60), 0, &timeinfo);
#elif defined(__LINUX_2_6_32__)
	time_to_tm(get_seconds()-(sys_tz.tz_minuteswest * 60), 0, &timeinfo);
#endif

	record += *head;
	*head = (*head + 1) & (DIAG_LOG_REC_MAXNUM-1);
	if (*head == *tail) {
		*tail = (*tail + 1) & (DIAG_LOG_REC_MAXNUM-1);
	}

	if (msg_type == CTCWIFI_TYPE_FRAME_BODY) {
		record->time_rec[0] = 0;
	}
	else {
#ifdef __LINUX_2_6_32__
	snprintf(temp_buf, 20, "%04d-%02d-%02d %02d:%02d:%02d", 
		(1900+timeinfo.tm_year), (1+timeinfo.tm_mon), timeinfo.tm_mday, (timeinfo.tm_hour), (timeinfo.tm_min), (timeinfo.tm_sec));
#else
	snprintf(temp_buf, 20, "%d", get_seconds());
#endif
	memcpy(record->time_rec, temp_buf, sizeof(temp_buf));
	memcpy(record->sta_mac, mac, MACADDRLEN);
	memcpy(record->ssid, SSID, SSID_LEN);
	}
	
	n = strlen(msg);
	if (n < DIAG_LOG_MSG_MAXLEN)
		strncpy(record->msg, msg, n);
	else
		strncpy(record->msg, msg, DIAG_LOG_MSG_MAXLEN-1);
}

void __ctcwifi_diag_log(struct rtl8192cd_priv *priv, unsigned char *mac, unsigned int direction, unsigned char *frame_type, unsigned char *frame_body, unsigned int frame_len)
{
	unsigned char _msg[197];
	int i, n;
	unsigned char hex[3];
	unsigned char bcmac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if (frame_body == NULL)
		return;

	if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
		n = CTCWIFI_2G;
	else
		n = CTCWIFI_5G;

	if (!diag_enable[n])
		return;
	
	if (mac == NULL)
		mac = bcmac;
	
	if (direction)
		snprintf(_msg, 196, "sends %s to %02X:%02X:%02X:%02X:%02X:%02X [", frame_type, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	else
		snprintf(_msg, 196, "receives %s from %02X:%02X:%02X:%02X:%02X:%02X [", frame_type, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	ctcwifi_conn_record(priv, mac, _msg, 0);

	_msg[0] = 0;
	for (i = 0; i < frame_len; i++) {
		sprintf(hex, "%02X", frame_body[i]);
		strcat(_msg, hex);
		if ((i > 0) && (((i+1) % (196/2)) == 0)) {
			ctcwifi_conn_record(priv, mac, _msg, 2);
			_msg[0] = 0;
		}
	}
	if ((i % (196/2)) == 0)
		ctcwifi_conn_record(priv, mac, "]\n", 2);
	else {
		strcat(_msg, "]\n");
		ctcwifi_conn_record(priv, mac, _msg, 2);
	}
}
#endif // CTC_WIFI_DIAG

#endif

#if defined(_SINUX_) || !defined(__KERNEL__)

struct _proc_table_
{
	char *cmd;
	int (*read_func)(char *buf, char **start, off_t offset,
			int length, int *eof, void *data);
	int (*write_func)(struct file *file, const char *buffer,
		unsigned long count, void *data);
};

static struct _proc_table_ proc_table[] =
{
    {"mib_all",				rtl8192cd_proc_mib_all,NULL},
    {"mib_temp",			rtl8192cd_proc_get_temperature, NULL},
    {"mib_rf",				rtl8192cd_proc_mib_rf,NULL},
    {"mib_operation",		rtl8192cd_proc_mib_operation,NULL},
    {"mib_staconfig",		rtl8192cd_proc_mib_staconfig,NULL},
    {"mib_dkeytbl",			rtl8192cd_proc_mib_dkeytbl,NULL},
    {"mib_auth",			rtl8192cd_proc_mib_auth,NULL},
    {"pmkcache",			rtl8192cd_proc_pmk,NULL},
#ifdef AUTH_SAE
    {"sae_peertable",		rtl8192cd_proc_sae_peertable,NULL},
#endif
    {"mib_gkeytbl",			rtl8192cd_proc_mib_gkeytbl,NULL},
    {"mib_bssdesc",			rtl8192cd_proc_mib_bssdesc,NULL},
    {"sta_info",			rtl8192cd_proc_stainfo,NULL},
    {"sta_keyinfo",			rtl8192cd_proc_sta_keyinfo,NULL},
    {"probe_info",			rtl8192cd_proc_probe_info,NULL},
#ifdef CONFIG_CMCC_FEATURE_SUPPORT
    {"sta_cap_table",			rtl8192cd_proc_sta_cap_table, NULL},
#endif
	{"wmm",					rtl8192cd_proc_wmm, NULL},
#if defined(CONFIG_RTL_MONITOR_STA_INFO)
	{"monitor_sta_info",	rtl8192cd_proc_monitor_stainfo, NULL},
#endif
#if defined(AP_NEIGHBOR_INFO)	
	{"ap_neighbor_info",	rtl8192cd_proc_ap_neighbor_info, NULL},
#endif
#if defined(BEACON_VS_IE)
	{"ap_probereq_info",	rtl8192cd_proc_specific_ap_probereq_info, NULL},
#endif
#ifdef PROC_STA_CONN_FAIL_INFO
    {"sta_conn_fail", 		rtl8192cd_proc_sta_conn_fail,NULL},
#endif
#ifdef STA_RATE_STATISTIC
	{"sta_rateinfo",		rtl8192cd_proc_sta_rateinfo,NULL},
#endif        
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
    {"sta_queinfo",			rtl8192cd_proc_sta_queinfo,NULL},
#endif
    {"sta_dbginfo",			rtl8192cd_proc_sta_dbginfo,NULL},
#if defined(CONFIG_PCI_HCI)
    {"txdesc",				rtl8192cd_proc_txdesc_info,NULL},
    {"rxdesc",				rtl8192cd_proc_rxdesc_info,NULL},
    {"desc_info",			rtl8192cd_proc_desc_info,NULL},
#endif
#if defined(CONFIG_SDIO_HCI)
    {"que_info",			rtl8192cd_proc_que_info,NULL},
    {"sdio_dbginfo",		rtl8192cd_proc_sdio_dbginfo,NULL},
#endif
    {"buf_info",			rtl8192cd_proc_buf_info,NULL},
    {"stats",				rtl8192cd_proc_stats,NULL},
#if defined(CH_LOAD_CAL) && defined(USE_OUT_SRC)
    {"ch_load",				rtl8192cd_proc_ch_load,NULL},
#endif
    
    {"mib_erp",				rtl8192cd_proc_mib_erp,NULL},
    {"cam_info",			rtl8192cd_proc_cam_info,NULL},
#ifdef MULTI_MAC_CLONE
    {"mbidcam_info",		rtl8192cd_proc_mbidcam_info,NULL},
    {"msta_info",			rtl8192cd_proc_mstainfo,NULL},
#endif
#ifdef HS2_SUPPORT
    {"mib_hs2", 			rtl8192cd_proc_mib_hs2,NULL},
#endif
#ifdef WDS
    {"mib_wds",				rtl8192cd_proc_mib_wds,NULL},
#endif
#ifdef __ECOS
#ifdef CONFIG_RTK_MESH
#ifdef MESH_BOOTSEQ_AUTH
    {"mesh_auth_mpinfo",			mesh_auth_mpinfo,NULL},
#endif
    {"mesh_assoc_mpinfo",			mesh_assoc_mpinfo,NULL},
    {"mesh_stats",					mesh_stats,NULL},
    {"flow_stats",					mesh_proc_flow_stats_read,NULL},
    {"mesh_pathsel_routetable", 	mesh_pathsel_routetable_info,NULL},
    {"mesh_proxy_table", 			mesh_proxy_table_info,NULL},
    {"mesh_portal_table", 			mesh_portal_table_info,NULL},
    {"mesh_root_info", 				mesh_root_info,NULL},
#endif
#endif
#ifdef RTK_BR_EXT
    {"mib_brext",			rtl8192cd_proc_mib_brext,NULL},
    {"nat25filter",          rtl8192cd_proc_nat25filter_read,NULL},
#endif

#ifdef DOT11K
    {"rm_ap_channel_report",      rtl8192cd_proc_ap_channel_report_read,NULL},    
    {"rm_neighbor_report",      rtl8192cd_proc_neighbor_read,NULL},
#endif

#ifdef CONFIG_IEEE80211V
    {"wnm_transition_list",      rtl8192cd_proc_transition_read,NULL},
#endif

#ifdef ENABLE_RTL_SKB_STATS
    {"skb_info",			rtl8192cd_proc_skb_info,NULL},
#endif
#if (BEAMFORMING_SUPPORT == 1)
	{"mib_txbf",				rtl8192cd_proc_mib_txbf,NULL},
#endif
#ifdef DFS
    {"mib_dfs",				rtl8192cd_proc_mib_DFS,NULL},
#endif
#ifdef RTK_AC_SUPPORT /*eric-8822*/
    {"mib_rf_ac", 			rtl8192cd_proc_mib_rf_ac,NULL},
#endif
    {"mib_misc",			rtl8192cd_proc_mib_misc,NULL},
#ifdef WIFI_SIMPLE_CONFIG
    {"mib_wsc",			rtl8192cd_proc_mib_wsc,NULL},
#endif
#ifdef GBWC
    {"mib_gbwc",			rtl8192cd_proc_mib_gbwc,NULL},
#endif
#ifdef SBWC
	{"mib_sbwc",			rtl8192cd_proc_mib_sbwc,NULL},
#endif

#ifdef CONFIG_RTK_BAND_STEERING
	{"band_steering_block_list",		rtl8192cd_proc_get_band_steering_block_list,NULL},
#endif

#ifdef RTK_MULTI_AP
	{"map_block_list",		rtl8192cd_proc_get_map_block_list,NULL},
#endif

    {"mib_11n",			rtl8192cd_proc_mib_11n,NULL},
#ifdef RTL_MANUAL_EDCA
    {"mib_EDCA",			rtl8192cd_proc_mib_edca,NULL},
#endif
#ifdef CLIENT_MODE
    {"up_flag",			rtl8192cd_proc_up_read,NULL},
#endif
#if defined(RTLWIFINIC_GPIO_CONTROL)
    {"gpio_ctrl",			rtl8192cd_proc_gpio_ctrl_read,NULL},
#endif
#ifdef TLN_STATS
    {"wifi_conn_stats",		proc_wifi_conn_stats,NULL},
    {"ext_wifi_conn_stats",	proc_ext_wifi_conn_stats,NULL},
#endif

#ifdef AUTO_TEST_SUPPORT
    {"SS_Result",			rtl8192cd_proc_SSR_read,NULL},
#endif

#ifdef A4_STA
    {"a4_sta_info",          a4_dump_sta_info,NULL},
#endif

#ifdef STA_CONTROL
    {"stactrl_info",          stactrl_info_read,NULL},
#endif

#ifdef _MESH_DEBUG_ // 802.11s output debug information
    {"mesh_unestablish_mpinfo",	mesh_unEstablish_mpinfo,NULL},
    {"mesh_assoc_mpinfo",	mesh_assoc_mpinfo,NULL},
    {"mesh_stats",			mesh_stats,NULL},
#endif	// _MESH_DEBUG_
#ifdef BT_COEXIST
	{"bt_coexist", rtl8192cd_proc_bt_coexist,NULL},
#endif
#ifdef CONFIG_IEEE80211R
	{"ft_info",				rtl8192cd_proc_ft_info,NULL},
#endif
#ifdef USE_OUT_SRC
    {"cmd",	proc_get_phydm_cmd,proc_set_phydm_cmd},
#endif
    {"thermal",				rtl8192cd_proc_thermal,NULL},
#ifdef THERMAL_CONTROL
	{"therm_control",				rtl8192cd_proc_thermal_control},
#endif
#ifdef RTK_ATM
	{"atm",				rtl8192cd_proc_atm,NULL},
#endif
#ifdef SW_TXQ_ATF
	{"swq_atf", 			rtl8192cd_proc_swq_atf_read,NULL},
#endif
#ifdef CONFIG_RTL_OFFLOAD_DRIVER
    {"ofld",				rtl8192cd_proc_ofld},
    {"ofld_cpu",			rtl8192cd_proc_ofld_cpu},
    {"ofld_sys",			rtl8192cd_proc_ofld_system},
    {"ofld_pkt",			rtl8192cd_proc_ofld_pkt_mgnt},
    {"ofld_dma",			rtl8192cd_proc_ofld_dma_info},
    {"ofld_axidma_txdesc",		rtl8192cd_proc_ofld_axidma_txdesc},
    {"ofld_hcidma_drxdesc",		rtl8192cd_proc_ofld_hcidma_drxdesc},
#endif
};

#define NUM_CMD_TABLE_ENTRY		(sizeof(proc_table) / sizeof(struct _proc_table_))

#ifdef __ECOS
void rtl8192cd_proc_help(char *name)
{
	int i;

	for (i=0; i<NUM_CMD_TABLE_ENTRY; i++) {
		ecos_pr_fun("%s %s\n", name, proc_table[i].cmd);
	}
	ecos_pr_fun("%s led\n", name);
}

void rtl8192cd_proc_debug(struct net_device *dev, int argc,char *argv[])
{
	int i, eof;
	char *tmpbuf, *start;
	char cmd[128] = {0};
	int write = 0;
	for(i=1;i<argc;i++)
	{
		if(!strcmp(argv[i],"write"))
		{
			write = 1;
			continue;
		}
		if(!strcmp(argv[i],"read"))
		{
			continue;
		}
		if(strlen(cmd)==0)
		{
			strcpy(cmd,argv[i]);
		}
		else
		{
			strcat(cmd," ");
			strcat(cmd,argv[i]);
		}
	}
	start = tmpbuf = 0;
	for (i=0; i<NUM_CMD_TABLE_ENTRY; i++) {
		if (!strcmp(argv[0], proc_table[i].cmd)) {
			if(write){
				if(proc_table[i].write_func!=NULL)
					proc_table[i].write_func(NULL, cmd,strlen(cmd),dev);
			}
			else{
				if(proc_table[i].read_func!=NULL)
					proc_table[i].read_func(tmpbuf, &start, 0, 0, &eof, dev);
			}
			break;
		}
	}
}
#else
void rtl8192cd_proc_debug(struct net_device *dev, char *cmd)
{
	int i, j, eof, len;
	char *tmpbuf, *start;

	start = tmpbuf = (char *)kmalloc(4096, 0);
	for (i=0; i<NUM_CMD_TABLE_ENTRY; i++) {
		if (!strcmp(cmd, proc_table[i].cmd)) {
			memset(tmpbuf, 0, 4096);
			len = proc_table[i].func(tmpbuf, &start, 0, 4096, &eof, dev);
			for(j=0; j<len; j++)
				printk("%c", tmpbuf[j]);
		}
	}
	kfree(tmpbuf);
}
#endif

#ifdef CONFIG_MSC
int rtl8192cd_show_wifi_debug(char *dev_name, char *cmd, char *str)
{
	int i, j, eof, len;
	char *tmpbuf, *start;
	struct net_device *dev;

	printk("dev_name=%s, cmd=%s.\n", dev_name, cmd);

    dev = dev_get_by_name(dev_name);
    if (dev) {
    	start = tmpbuf = (char *)kmalloc(4096, 0);
    	for (i=0; i<NUM_CMD_TABLE_ENTRY; i++) {
    		if (!strcmp(cmd, proc_table[i].cmd)) {
    			memset(tmpbuf, 0, 4096);
    			len = proc_table[i].func(tmpbuf, &start, 0, 4096, &eof, dev);
    			strcpy(str, tmpbuf);
    			for(j=0; j<len; j++)
    				printk("%c", tmpbuf[j]);
    		}
    	}
	    kfree(tmpbuf);
	    dev_put(dev);
	}
	else
	    return 1;

	return 0;
}


EXPORT_SYMBOL(rtl8192cd_show_wifi_debug);
EXPORT_SYMBOL(rtl8192cd_proc_debug);
#endif
#endif // __KERNEL__

#if defined(CONFIG_RTL_OFFLOAD_DRIVER)
#include <linux/debugfs.h>
static int debugfs_open(struct inode *inode, struct file *file)
{
    file->private_data = inode->i_private;

    return 0;
}

static ssize_t reg_r32_read(struct file *file,
                            char __user *user_buf,
                            size_t count,
                            loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len = 0;
    u8 buf[32];

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = scnprintf(buf, sizeof(buf), "0x%08x %d\n", priv->debugfs_val,
                                                     priv->debugfs_val);

    return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t reg_r32_write(struct file *file,
                             const char __user *user_buf,
                             size_t count,
                             loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len;
    char *addr;
    char buf[32];
    u32 reg_addr;

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = min(count, sizeof(buf) - 1);
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';
    addr = buf;
    if (kstrtou32(addr, 0, &reg_addr))
        return -EINVAL;

    priv->debugfs_addr = reg_addr;
    priv->debugfs_val = 0;

    priv->debugfs_val = RTL_R32(reg_addr);

    return count;
}

static const struct file_operations fops_reg_r32 = {
    .read = reg_r32_read,
    .write = reg_r32_write,
    .open = debugfs_open,
    .owner = THIS_MODULE,
    .llseek = default_llseek,
};

static ssize_t reg_w32_read(struct file *file,
                            char __user *user_buf,
                            size_t count,
                            loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len = 0;
    u8 buf[32];

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = scnprintf(buf, sizeof(buf), "0x%08x\n", priv->debugfs_val);

    return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t reg_w32_write(struct file *file,
                             const char __user *user_buf,
                             size_t count,
                             loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len;
    char *val, *addr;
    char buf[32];
    u32 reg_addr, reg_val;

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = min(count, sizeof(buf) - 1);
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';
    val = buf;
    addr = strsep(&val, "=");
    if ((!addr) || (!val))
        return -EINVAL;

    if (kstrtou32(addr, 0, &reg_addr))
        return -EINVAL;

    if (kstrtou32(val, 0, &reg_val))
        return -EINVAL;

    priv->debugfs_addr = reg_addr;
    priv->debugfs_val = reg_val;

	RTL_W32(reg_addr, reg_val);

    return count;
}

static const struct file_operations fops_reg_w32 = {
    .read = reg_w32_read,
    .write = reg_w32_write,
    .open = debugfs_open,
    .owner = THIS_MODULE,
    .llseek = default_llseek,
};

static ssize_t reg_r16_read(struct file *file,
                            char __user *user_buf,
                            size_t count,
                            loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len = 0;
    u8 buf[32];

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = scnprintf(buf, sizeof(buf), "0x%04x %d\n", priv->debugfs_val,
                                                     priv->debugfs_val);

    return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t reg_r16_write(struct file *file,
                             const char __user *user_buf,
                             size_t count,
                             loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len;
    char *addr;
    char buf[32];
    u32 reg_addr;

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = min(count, sizeof(buf) - 1);
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';
    addr = buf;
    if (kstrtou32(addr, 0, &reg_addr))
        return -EINVAL;

    priv->debugfs_addr = reg_addr;
    priv->debugfs_val = 0;

    priv->debugfs_val = RTL_R16(reg_addr);

    return count;
}

static const struct file_operations fops_reg_r16 = {
    .read = reg_r16_read,
    .write = reg_r16_write,
    .open = debugfs_open,
    .owner = THIS_MODULE,
    .llseek = default_llseek,
};

static ssize_t reg_w16_read(struct file *file,
                            char __user *user_buf,
                            size_t count,
                            loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len = 0;
    u8 buf[32];

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = scnprintf(buf, sizeof(buf), "0x%04x\n", priv->debugfs_val);

    return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t reg_w16_write(struct file *file,
                             const char __user *user_buf,
                             size_t count,
                             loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len;
    char *val, *addr;
    char buf[32];
    u32 reg_addr;
    u16 reg_val;

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = min(count, sizeof(buf) - 1);
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';
    val = buf;
    addr = strsep(&val, "=");
    if ((!addr) || (!val))
        return -EINVAL;

    if (kstrtou32(addr, 0, &reg_addr))
        return -EINVAL;

    if (kstrtou16(val, 0, &reg_val))
        return -EINVAL;

    priv->debugfs_addr = reg_addr;
    priv->debugfs_val = reg_val;

	RTL_W16(reg_addr, reg_val);

    return count;
}

static const struct file_operations fops_reg_w16 = {
    .read = reg_w16_read,
    .write = reg_w16_write,
    .open = debugfs_open,
    .owner = THIS_MODULE,
    .llseek = default_llseek,
};

static ssize_t reg_r8_read(struct file *file,
                            char __user *user_buf,
                            size_t count,
                            loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len = 0;
    u8 buf[32];

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = scnprintf(buf, sizeof(buf), "0x%02x %d\n", priv->debugfs_val,
                                                     priv->debugfs_val);

    return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t reg_r8_write(struct file *file,
                             const char __user *user_buf,
                             size_t count,
                             loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len;
    char *addr;
    char buf[32];
    u32 reg_addr;

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = min(count, sizeof(buf) - 1);
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';
    addr = buf;
    if (kstrtou32(addr, 0, &reg_addr))
        return -EINVAL;

    priv->debugfs_addr = reg_addr;
    priv->debugfs_val = 0;

    priv->debugfs_val = RTL_R8(reg_addr);

    return count;
}

static const struct file_operations fops_reg_r8 = {
    .read = reg_r8_read,
    .write = reg_r8_write,
    .open = debugfs_open,
    .owner = THIS_MODULE,
    .llseek = default_llseek,
};

static ssize_t reg_w8_read(struct file *file,
                            char __user *user_buf,
                            size_t count,
                            loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len = 0;
    u8 buf[32];

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = scnprintf(buf, sizeof(buf), "0x%02x\n", priv->debugfs_val);

    return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t reg_w8_write(struct file *file,
                             const char __user *user_buf,
                             size_t count,
                             loff_t *ppos)
{
    struct rtl8192cd_priv *priv;
    unsigned int len;
    char *val, *addr;
    char buf[32];
    u32 reg_addr;
    u8 reg_val;

    priv = (struct rtl8192cd_priv *)(file->private_data);

    len = min(count, sizeof(buf) - 1);
    if (copy_from_user(buf, user_buf, len))
        return -EFAULT;

    buf[len] = '\0';
    val = buf;
    addr = strsep(&val, "=");
    if ((!addr) || (!val))
        return -EINVAL;

    if (kstrtou32(addr, 0, &reg_addr))
        return -EINVAL;

    if (kstrtou8(val, 0, &reg_val))
        return -EINVAL;

    priv->debugfs_addr = reg_addr;
    priv->debugfs_val = reg_val;

	RTL_W8(reg_addr, reg_val);

    return count;
}

static const struct file_operations fops_reg_w8 = {
    .read = reg_w8_read,
    .write = reg_w8_write,
    .open = debugfs_open,
    .owner = THIS_MODULE,
    .llseek = default_llseek,
};

int debugfs_init(struct net_device *dev)
{
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);
    struct dentry *debugfs;
    unsigned char debugfs_name[32];

    debugfs = (void *)debugfs_create_dir("rtk_wlan", NULL);
    if (!debugfs)
        return -ENOMEM;
    priv->debugfs = debugfs;

    debugfs_create_file("r32",
                        S_IRUSR | S_IWUSR,
                        debugfs,
                        priv,
                        &fops_reg_r32);

    debugfs_create_file("w32",
                        S_IRUSR | S_IWUSR,
                        debugfs,
                        priv,
                        &fops_reg_w32);

    debugfs_create_file("r16",
                        S_IRUSR | S_IWUSR,
                        debugfs,
                        priv,
                        &fops_reg_r16);

    debugfs_create_file("w16",
                        S_IRUSR | S_IWUSR,
                        debugfs,
                        priv,
                        &fops_reg_w16);

    debugfs_create_file("r8",
                        S_IRUSR | S_IWUSR,
                        debugfs,
                        priv,
                        &fops_reg_r8);

    debugfs_create_file("w8",
                        S_IRUSR | S_IWUSR,
                        debugfs,
                        priv,
                        &fops_reg_w8);

	return 0;
}

void debugfs_deinit(struct net_device *dev)
{
    struct rtl8192cd_priv *priv = GET_DEV_PRIV(dev);

    debugfs_remove_recursive(priv->debugfs);
    priv->debugfs = NULL;

    return;
}
#endif // CONFIG_RTL_OFFLOAD_DRIVER

#endif // __INCLUDE_PROC_FS__
#endif //__OSK__
