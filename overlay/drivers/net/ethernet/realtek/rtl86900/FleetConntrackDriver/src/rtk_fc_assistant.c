/*
 * Copyright (C) 2017 Realtek Semiconductor Corp.
 * All Rights Reserved.
 *
 * This program is the proprietary software of Realtek Semiconductor
 * Corporation and/or its licensors, and only be used, duplicated,
 * modified or distributed under the authorized license from Realtek.
 *
 * ANY USE OF THE SOFTWARE OTHER THAN AS AUTHORIZED UNDER
 * THIS LICENSE OR COPYRIGHT LAW IS PROHIBITED.
*/
#include <linux/version.h>

#include <linux/netfilter_ipv4.h>
#include <linux/inetdevice.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <net/addrconf.h>
#ifdef CONFIG_RTK_FC_TCP_SPI_SUPPORT
#include <net/netfilter/nf_conntrack.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
#include <net/netfilter/nf_conntrack_l3proto.h>
#endif
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#endif

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
#include "re8686_nic.h"
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
#include "ca_ext.h"
#endif

#include <rtk_fc_mgr.h>
#include <rtk_fc_wfo.h>
#include <rtk_fc_assistant.h>
#if defined(CONFIG_RTK_FC_IPSEC_FASTFWD)

#include <linux/crypto.h>

#include <crypto/hash.h>
#include <linux/scatterlist.h>
#endif

#if (defined(CONFIG_RTK_L34_XPON_PLATFORM) && IS_MODULE(CONFIG_RTK_L34_FC_KERNEL_MODULE)) 
#if !defined( RTK_FC_FLOWENT_ALIGNBUF)
#define RTK_FC_FLOWENT_ALIGNBUF		1023
#endif
char _flowEntryDataPool[(RTK_FC_TABLESIZE_HW_FLOW*sizeof(int)*8) + RTK_FC_FLOWENT_ALIGNBUF];
#endif

extern struct neigh_table arp_tbl;
#if IS_ENABLED(CONFIG_IPV6)
extern struct neigh_table nd_tbl;
#endif


int (*g_smp_call_function_single_async)(int cpu, __call_single_data_t *csd);
struct pid *(*g_find_vpid)(int nr);
void (*g_rcu_read_lock)(void);
void (*g_rcu_read_unlock)(void);
void (*g_rcu_read_lock_bh)(void);
void (*g_rcu_read_unlock_bh)(void);
void (*g_call_rcu)(struct rcu_head *head, rcu_callback_t func);
void (*g_synchronize_rcu)(void);
int (*g_irq_set_affinity_hint)(unsigned int irq, const struct cpumask *m);
void (*g_nf_ct_iterate_cleanup)(struct net *net,
			   int (*iter)(struct nf_conn *i, void *data),
			   void *data, u32 portid, int report);
#ifdef CONFIG_RTK_FC_TCP_SPI_SUPPORT
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
bool (*g_nf_ct_get_tuple)(const struct sk_buff *skb,
				unsigned int nhoff,
				unsigned int dataoff,
				u_int16_t l3num,
				u_int8_t protonum,
				struct net *net,
				struct nf_conntrack_tuple *tuple);
#else
bool (*g_nf_ct_get_tuple)(const struct sk_buff *skb,
		unsigned int nhoff,
		unsigned int dataoff,
		u_int16_t l3num,
		u_int8_t protonum,
		struct net *net,
		struct nf_conntrack_tuple *tuple,
		const struct nf_conntrack_l3proto *l3proto,
		const struct nf_conntrack_l4proto *l4proto);
#endif
#endif

static rtk_fc_export_symbol_t fc_ext_symbols;

int rtk_fc_ext_symbol_register(rtk_fc_export_symbol_t *ext_symbol)
{
	memcpy(&fc_ext_symbols, ext_symbol, sizeof(rtk_fc_export_symbol_t));

	return 0;
}

int rtk_fc_wlan_register(rtk_fc_wlan_devidx_t wlanDevIdx, struct net_device *dev)
{
	if(fc_ext_symbols._rtk_fc_wlan_register)
		return fc_ext_symbols._rtk_fc_wlan_register(wlanDevIdx, dev);
	else
		return -1;
}

int rtk_fc_igmp_mdb_callback_register(fc_igmp_mdb_callback searchMdbFunc)
{
	if(fc_ext_symbols._rtk_fc_igmp_mdb_callback_register)
		return fc_ext_symbols._rtk_fc_igmp_mdb_callback_register(searchMdbFunc);
	else
		return -1;

}

int rtk_fc_igmp_mdb_callback_unregister(void)
{
	if(fc_ext_symbols._rtk_fc_igmp_mdb_callback_unregister)
		return fc_ext_symbols._rtk_fc_igmp_mdb_callback_unregister();
	else
		return -1;

}



int rtk_fc_wlan_devMask2RtmbssidMask(rtk_fc_wlan_devmask_t *wlanDevIdMask, rt_wlan_mbssid_mask_t *rtWlanMbssidMsk)
{
	if(fc_ext_symbols._rtk_fc_wlan_devMask2RtmbssidMask)
		return fc_ext_symbols._rtk_fc_wlan_devMask2RtmbssidMask(wlanDevIdMask, rtWlanMbssidMsk);
	else
		return -1;
}
int rtk_fc_dev2wlanDevIdx(struct net_device *dev, rtk_fc_wlan_devidx_t *wlan_dev_idx)
{
	if(fc_ext_symbols._rtk_fc_dev2wlanDevIdx)
		return fc_ext_symbols._rtk_fc_dev2wlanDevIdx(dev, wlan_dev_idx);
	else
		return -1;
}
int rtk_fc_dev_get_realdev_phyport(struct net_device *dev, rtk_fc_realdev_t *rdev)
{
	if(fc_ext_symbols._rtk_fc_dev_get_realdev_phyport)
		return fc_ext_symbols._rtk_fc_dev_get_realdev_phyport(dev, rdev);
	else
		return -1;
}

int fwdEngine_wifi_rx(struct sk_buff *skb)
{
	if(fc_ext_symbols._rtk_fc_skb_wifi_rx)
		return fc_ext_symbols._rtk_fc_skb_wifi_rx(skb);
	else
		return RE8670_RX_CONTINUE;
}

int rtk_fc_skb_wifi_rx(struct sk_buff *skb)
{
	if(fc_ext_symbols._rtk_fc_skb_wifi_rx)
		return fc_ext_symbols._rtk_fc_skb_wifi_rx(skb);
	else
		return RE8670_RX_CONTINUE;
}

int rtk_fc_fastfwd_netif_rx(struct sk_buff *skb)
{
	if(fc_ext_symbols._rtk_fc_fastfwd_netif_rx)
		return fc_ext_symbols._rtk_fc_fastfwd_netif_rx(skb);
	else
		return RE8670_RX_CONTINUE;
}

gro_result_t rtk_fc_fastfwd_napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	if(fc_ext_symbols._rtk_fc_fastfwd_napi_gro_receive)
		return fc_ext_symbols._rtk_fc_fastfwd_napi_gro_receive(napi, skb);
	else
		return RE8670_RX_CONTINUE;
}

int rtk_fc_wifi_amsdu_pe_offload_mac_id_set(unsigned int mac_id, rtk_fc_wifi_amsdu_pe_offload_sta_conf_sel_t sta_conf_sel, rtk_fc_wifi_amsdu_pe_offload_sta_info_t sta_conf, unsigned char* mac_addr)
{
	if(fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mac_id_set)
		return fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mac_id_set(mac_id, sta_conf_sel, sta_conf, mac_addr);
	else
		return -1;
}

int rtk_fc_wifi_amsdu_pe_offload_mac_id_del(unsigned int mac_id)
{
	if(fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mac_id_del)
		return fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mac_id_del(mac_id);
	else
		return -1;
}

int rtk_fc_wifi_amsdu_pe_offload_mac_id_flush(void)
{
	if(fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mac_id_flush)
		return fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mac_id_flush();
	else
		return -1;
}

int rtk_fc_wifi_amsdu_pe_offload_mode_set(rtk_fc_wifi_amsdu_pe_offload_mode_t mode)
{
	if(fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mode_set)
		return fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mode_set(mode);
	else
		return -1;
}

int rtk_fc_wifi_amsdu_pe_offload_mode_get(rtk_fc_wifi_amsdu_pe_offload_mode_t *mode)
{
	if(fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mode_get)
		return fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_mode_get(mode);
	else
		return -1;
}

int rtk_fc_wifi_dev_attr_set(struct net_device *dev, rtk_fc_wifi_dev_attr_t attr)
{
	if(fc_ext_symbols._rtk_fc_wifi_dev_attr_set)
		return fc_ext_symbols._rtk_fc_wifi_dev_attr_set(dev, attr);
	else
		return -1;
}

int rtk_fc_wifi_dev_attr_get(struct net_device *dev, rtk_fc_wifi_dev_attr_t *attr)
{
	if(fc_ext_symbols._rtk_fc_wifi_dev_attr_get)
		return fc_ext_symbols._rtk_fc_wifi_dev_attr_get(dev, attr);
	else
		return -1;
}

int rtk_fc_wifi_dev_to_devidx_get(struct net_device *dev, unsigned int *wlan_dev_idx)
{
	if(fc_ext_symbols._rtk_fc_wifi_dev_to_devidx_get)
		return fc_ext_symbols._rtk_fc_wifi_dev_to_devidx_get(dev, wlan_dev_idx);
	else
		return -1;
}

int rtk_fc_wifi_event_handling_cb_register(rtk_fc_wifi_event_handling_callback pfunc)
{
	if(fc_ext_symbols._rtk_fc_wifi_event_handling_cb_register)
		return fc_ext_symbols._rtk_fc_wifi_event_handling_cb_register(pfunc);
	else
		return -1;
}

#if 1 // backward compatible but to be removed.
int rtk_fc_wifi_client_mode_cb_register(rtk_fc_wifi_callback pfunc)
{
	if(fc_ext_symbols._rtk_fc_wifi_client_mode_cb_register)
		return fc_ext_symbols._rtk_fc_wifi_client_mode_cb_register(pfunc);
	else
		return -1;
}

int rtk_fc_wifi_dmacHandle_cb_register(rtk_fc_wifi_dmacHandle_callback pfunc)
{
	if(fc_ext_symbols._rtk_fc_wifi_dmacHandle_cb_register)
		return fc_ext_symbols._rtk_fc_wifi_dmacHandle_cb_register(pfunc);
	else
		return -1;
}
#endif

int rtk_fc_wifi_amsdu_pe_offload_wifiPri_to_amsduQ_set(unsigned int wifi_pri, rtk_fc_wifi_amsdu_pe_queueid_t amsdu_qid)
{
	if(fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_wifiPri_to_amsduQ_set)
		return fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_wifiPri_to_amsduQ_set(wifi_pri, amsdu_qid);
	else
		return -1;
}

int rtk_fc_wifi_amsdu_pe_offload_wifiPri_to_amsduQ_get(unsigned int wifi_pri, rtk_fc_wifi_amsdu_pe_queueid_t *amsdu_qid)
{
	if(fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_wifiPri_to_amsduQ_get)
		return fc_ext_symbols._rtk_fc_wifi_amsdu_pe_offload_wifiPri_to_amsduQ_get(wifi_pri, amsdu_qid);
	else
		return -1;
}

int rtk_fc_wifi_ipDscp_to_wifiPri_set(unsigned int ip_dscp, unsigned int wifi_pri)
{
	if(fc_ext_symbols._rtk_fc_wifi_ipDscp_to_wifiPri_set)
		return fc_ext_symbols._rtk_fc_wifi_ipDscp_to_wifiPri_set(ip_dscp, wifi_pri);
	else
		return -1;
}

int rtk_fc_wifi_ipDscp_to_wifiPri_get(unsigned int ip_dscp, unsigned int *wifi_pri)
{
	if(fc_ext_symbols._rtk_fc_wifi_ipDscp_to_wifiPri_get)
		return fc_ext_symbols._rtk_fc_wifi_ipDscp_to_wifiPri_get(ip_dscp, wifi_pri);
	else
		return -1;
}

#if defined(CONFIG_RTK_FC_CRYPTO_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
int rtk_fc_crypto_set_src_desc(unsigned int first_dw, unsigned int second_dw)
{
	if(fc_ext_symbols._rtk_fc_crypto_set_src_desc)
		return fc_ext_symbols._rtk_fc_crypto_set_src_desc(first_dw, second_dw);
	else
		return -1;
}

int rtk_fc_crypto_set_dst_desc(unsigned int first_dw, unsigned int second_dw, unsigned char start, unsigned char end)
{
	if(fc_ext_symbols._rtk_fc_crypto_set_dst_desc)
		return fc_ext_symbols._rtk_fc_crypto_set_dst_desc(first_dw, second_dw, start, end);
	else
		return -1;
}

int rtk_fc_crypto_ps_pop_done(void)
{
	if(fc_ext_symbols._rtk_fc_crypto_ps_pop_done)
		return fc_ext_symbols._rtk_fc_crypto_ps_pop_done();
	else
		return -1;
}

int rtk_fc_crypto_send_desc(void)
{
	if(fc_ext_symbols._rtk_fc_crypto_send_desc)
		return fc_ext_symbols._rtk_fc_crypto_send_desc();
	else
		return -1;
}

int rtk_fc_crypto_wait_put_desc_done(void)
{
	if(fc_ext_symbols._rtk_fc_crypto_wait_put_desc_done)
		return fc_ext_symbols._rtk_fc_crypto_wait_put_desc_done();
	else
		return -1;
}

int rtk_fc_crypto_register_hw_crypto_ready_cnt_callback(rtk_fc_crypto_hw_crypto_ready_cnt_callback cb_func)
{
	if(fc_ext_symbols._rtk_fc_crypto_register_hw_crypto_ready_cnt_callback)
		return fc_ext_symbols._rtk_fc_crypto_register_hw_crypto_ready_cnt_callback(cb_func);
	else
		return -1;
}
#endif

void rtk_fc_ext_dma_cache_wback_inv(unsigned long start, unsigned long sz)
{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	dma_cache_wback_inv(start, sz);
#endif
}

bool rtk_fc_ext_br_allowed_ingress(struct net_bridge *br,
			struct net_bridge_port *p, struct sk_buff *skb,
			u16 *vid)
{
#if IS_BUILTIN(CONFIG_BRIDGE) 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	uint8 state = p ? p->state : 0;		/* BR_STATE_DISABLED=0 */
	struct net_bridge_vlan *vlan = NULL;

	if(!p)
		return 0;
	else
		return br_allowed_ingress(br, nbp_vlan_group_rcu(p), skb, vid, &state, &vlan);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)

	if(!p)
		return 0;
	else
		return br_allowed_ingress(br, nbp_vlan_group_rcu(p), skb, vid);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)

	if(!p)
		return 0;
	else
		return br_allowed_ingress(br, nbp_get_vlan_info(p), skb, vid);

#else

	printk("%s@%d - FIXME to support linux kernel api\n", __FUNCTION__, __LINE__);
	return 0;
	
#endif

#else
	return 0;

#endif
}

struct net_bridge_fdb_entry *rtk_fc_ext_br_fdb_get(struct net_bridge *br,
					  const unsigned char *addr,
					  __u16 vid)
{

#if IS_BUILTIN(CONFIG_BRIDGE) 



	/*  
	 *   2019 / 01 / 24, Wen
	 *
     *   When br_vlan_enabled (Need CONFIG_BRIDGE_VLAN_FILTERING on and "echo 1 > sys/devices/virtual/net/br0/bridge/vlan_filtering" )
     *   linux kernel will assign default pvid to vid when learning FDB, so we need to query fdb with pvid when br_vlan_enabled is on.
     *   
     *   On another hand, when br_vlan_enabled is not on, linux kernel will learn FDB with vid = 0, thus we need to set vid = 0 when br_vlan_enabled is not on. 
     *
	 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	
	
	//struct net_bridge_vlan_group *vg=NULL;

	if( br_vlan_enabled(br->dev) ){
		//vg = br_vlan_group(br);
		vid = br_get_pvid(br_vlan_group_rcu(br));
	}else
		vid = 0;
	return br_fdb_find_rcu(br, addr, vid);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	
	
	//struct net_bridge_vlan_group *vg=NULL;

	if( br_vlan_enabled(br) ){
		//vg = br_vlan_group(br);
		vid = br_get_pvid(br_vlan_group_rcu(br));
	}else
		vid = 0;
	
	return __br_fdb_get(br, addr, vid);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
	//struct net_port_vlans *pv = NULL;

	if( br_vlan_enabled(br) ){
		//pv = br_get_vlan_info(br);
		vid = br_get_pvid(br_get_vlan_info(br));
	}else
		vid = 0;
	return __br_fdb_get(br, addr, vid);

#else
	printk("%s@%d - FIXME to support linux kernel api\n", __FUNCTION__, __LINE__);
	return NULL;
	
#endif
	
#else
	return NULL;
#endif
}
struct net_bridge_fdb_entry *rtk_fc_ext_br_fdb_get_by_pvid(struct net_bridge *br,
					  const unsigned char *addr,
					  __u16 vid)
{

#if IS_BUILTIN(CONFIG_BRIDGE) 



	/*  
	 *   2019 / 01 / 24, Wen
	 *
     *   When br_vlan_enabled (Need CONFIG_BRIDGE_VLAN_FILTERING on and "echo 1 > sys/devices/virtual/net/br0/bridge/vlan_filtering" )
     *   linux kernel will assign default pvid to vid when learning FDB, so we need to query fdb with pvid when br_vlan_enabled is on.
     *   
     *   On another hand, when br_vlan_enabled is not on, linux kernel will learn FDB with vid = 0, thus we need to set vid = 0 when br_vlan_enabled is not on. 
     *
	 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	
	
	return br_fdb_find_rcu(br, addr, vid);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
		
	return __br_fdb_get(br, addr, vid);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
	//struct net_port_vlans *pv = NULL;

	return __br_fdb_get(br, addr, vid);

#else
	printk("%s@%d - FIXME to support linux kernel api\n", __FUNCTION__, __LINE__);
	return NULL;
	
#endif
	
#else
	return NULL;
#endif
}

int rtk_fc_g_smp_call_function_single_async(int cpu, __call_single_data_t *csd)
{
	return g_smp_call_function_single_async(cpu, csd);
}
struct pid *rtk_fc_g_find_vpid(int nr)
{
	return g_find_vpid(nr);
}

struct inet6_dev *rtk_fc_g_in6_dev_get(const struct net_device *dev)
{
	return __in6_dev_get(dev);
}

struct in_device *rtk_fc_g_in_dev_get_rcu(const struct net_device *dev)
{
	return __in_dev_get_rcu(dev);
}

struct net_bridge_port *rtk_fc_g_br_port_get_rcu(const struct net_device *dev)
{
	//return br_port_get_rcu(dev);
	return rcu_dereference(dev->rx_handler_data);
}


int rtk_fc_g_skb_skb_cow_data(struct sk_buff *skb, int len, struct sk_buff **trailer)
{
	return skb_cow_data(skb, len,trailer);
}

void rtk_fc_g_rcu_read_lock(void)
{
	g_rcu_read_lock();
	return;
}
void rtk_fc_g_rcu_read_unlock(void)
{
	g_rcu_read_unlock();
	return;
}
void rtk_fc_g_rcu_read_lock_bh(void)
{
	g_rcu_read_lock_bh();
	return;
}
void rtk_fc_g_rcu_read_unlock_bh(void)
{
	g_rcu_read_unlock_bh();
	return;
}
void rtk_fc_g_call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	g_call_rcu(head, func);
	return;
}
void rtk_fc_g_synchronize_rcu(void)
{
	g_synchronize_rcu();
	return;
}

int  rtk_fc_neigh_for_each_read(struct neigh_table *tbl, int (*cb)(struct neighbour *, unsigned char *), unsigned char *mac)
{
	int chain;
	struct neigh_hash_table *nht;
	int ret = -1;

	rtk_fc_g_rcu_read_lock_bh();
	nht = rcu_dereference_bh(tbl->nht);

	read_lock(&tbl->lock); /* avoid resizes */
	for (chain = 0; chain < (1 << nht->hash_shift); chain++) {
		struct neighbour *n;

		neigh_for_each_in_bucket_rcu(n, &nht->hash_heads[chain]) {
			if((ret = cb(n, mac)) == 0/*SUCCESS*/)
				break;
		}

		if(ret == 0)
			break;
	}
	read_unlock(&tbl->lock);
	rtk_fc_g_rcu_read_unlock_bh();

	return ret;
}

int  rtk_fc_g_neigh_for_each_read_v4(int (*cb)(struct neighbour *, unsigned char *), unsigned char *mac)
{
	return rtk_fc_neigh_for_each_read(&arp_tbl, cb, mac);
}

int  rtk_fc_g_neigh_for_each_read_v6(int (*cb)(struct neighbour *, unsigned char *), unsigned char *mac)
{
	return rtk_fc_neigh_for_each_read(&nd_tbl, cb, mac);
}
	
int rtk_fc_neigh_enumerate(struct neigh_table *tbl, void (*cb)(struct neighbour *, void *), void *cookie)
{
	int ret=FAIL;

	if(cb==NULL || cookie==NULL) return ret;

	neigh_for_each(tbl, cb, cookie);

	return SUCCESS;
}

int rtk_fc_g_neighv4_enumerate(void (*cb)(struct neighbour *, void *), void *cookie)
{
	return rtk_fc_neigh_enumerate(&arp_tbl, cb, cookie);
}

int rtk_fc_g_neighv6_enumerate(void (*cb)(struct neighbour *, void *), void *cookie)
{
	return rtk_fc_neigh_enumerate(&nd_tbl, cb, cookie);
}
int rtk_fc_g_irq_set_affinity_hint(unsigned int irq, const struct cpumask *m)
{
	return g_irq_set_affinity_hint(irq, m);
}
void rtk_fc_g_nf_ct_iterate_cleanup(struct net *net,
			   int (*iter)(struct nf_conn *i, void *data),
			   void *data, u32 portid, int report)
{

#if IS_BUILTIN(CONFIG_NF_CONNTRACK)
	g_nf_ct_iterate_cleanup(net, iter, data, portid, report);
#else
	printk("[FCEXT] %s not support \n",__func__);
#endif
	return;
}

#ifdef CONFIG_RTK_FC_TCP_SPI_SUPPORT
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
bool rtk_fc_g_nf_ct_get_tuple(const struct sk_buff *skb,
		unsigned int nhoff,
		unsigned int dataoff,
		u_int16_t l3num,
		u_int8_t protonum,
		struct net *net,
		struct nf_conntrack_tuple *tuple)
{
#if IS_BUILTIN(CONFIG_NF_CONNTRACK)
		return g_nf_ct_get_tuple(skb, nhoff, dataoff, l3num, protonum, net, tuple);
#else
		return false;
#endif
}
#else
bool rtk_fc_g_nf_ct_get_tuple(const struct sk_buff *skb,
	   unsigned int nhoff,
	   unsigned int dataoff,
	   u_int16_t l3num,
	   u_int8_t protonum,
	   struct net *net,
	   struct nf_conntrack_tuple *tuple,
	   const struct nf_conntrack_l3proto *l3proto,
	   const struct nf_conntrack_l4proto *l4proto)
{
#if IS_BUILTIN(CONFIG_NF_CONNTRACK)
	return g_nf_ct_get_tuple(skb, nhoff, dataoff, l3num, protonum, net, tuple, l3proto, l4proto);
#else
	return false;
#endif
}
#endif
#endif

int rtk_fc_g_ct_helper_exist_check(struct nf_conn *ct, struct nf_conntrack_helper **helper)
{
	const struct nf_conn_help *help=NULL;

	if ((help = nfct_help(ct))!=NULL){
		if ((*helper = rcu_dereference(help->helper))!=NULL){
			return SUCCESS;
		}
	}

	return FAIL;
}

 int rtk_fc_extmodule_init(void)
{
	printk("[FCEXT] %s \n",__func__);

	memset(&fc_ext_symbols, 0, sizeof(fc_ext_symbols));

	g_smp_call_function_single_async = smp_call_function_single_async;
	g_find_vpid = find_vpid;
	g_rcu_read_lock = rcu_read_lock;
	g_rcu_read_unlock = rcu_read_unlock;
	g_rcu_read_lock_bh = rcu_read_lock_bh;
	g_rcu_read_unlock_bh = rcu_read_unlock_bh;
	g_call_rcu = call_rcu;
	g_synchronize_rcu = synchronize_rcu;
	g_irq_set_affinity_hint = irq_set_affinity_hint;
	
#if IS_BUILTIN(CONFIG_NF_CONNTRACK)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	g_nf_ct_iterate_cleanup = nf_ct_iterate_cleanup_net;
#else
	g_nf_ct_iterate_cleanup = nf_ct_iterate_cleanup;
#endif

#ifdef CONFIG_RTK_FC_TCP_SPI_SUPPORT
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	g_nf_ct_get_tuple = rtk_fc_nf_ct_get_tuple;
#else
	g_nf_ct_get_tuple = nf_ct_get_tuple;
#endif
#endif

#endif

	return 0;
}

void rtk_fc_extmodule_exit(void)
{
	printk("[FCEXT] %s \n",__func__);
	
	return;
}


module_init(rtk_fc_extmodule_init);
module_exit(rtk_fc_extmodule_exit);

#if (defined(CONFIG_RTK_L34_XPON_PLATFORM) && IS_MODULE(CONFIG_RTK_L34_FC_KERNEL_MODULE)) 
EXPORT_SYMBOL(_flowEntryDataPool);
#endif

EXPORT_SYMBOL(rtk_fc_ext_symbol_register);

EXPORT_SYMBOL(rtk_fc_wlan_register);
EXPORT_SYMBOL(rtk_fc_wlan_devMask2RtmbssidMask);
EXPORT_SYMBOL(rtk_fc_dev2wlanDevIdx);
EXPORT_SYMBOL(rtk_fc_dev_get_realdev_phyport);

EXPORT_SYMBOL(rtk_fc_igmp_mdb_callback_register);
EXPORT_SYMBOL(rtk_fc_igmp_mdb_callback_unregister);


EXPORT_SYMBOL(fwdEngine_wifi_rx);
EXPORT_SYMBOL(rtk_fc_skb_wifi_rx);
EXPORT_SYMBOL(rtk_fc_fastfwd_napi_gro_receive);
EXPORT_SYMBOL(rtk_fc_fastfwd_netif_rx);
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
EXPORT_SYMBOL(rtk_fc_wifi_amsdu_pe_offload_mac_id_set);
EXPORT_SYMBOL(rtk_fc_wifi_amsdu_pe_offload_mac_id_del);
EXPORT_SYMBOL(rtk_fc_wifi_amsdu_pe_offload_mac_id_flush);
EXPORT_SYMBOL(rtk_fc_wifi_amsdu_pe_offload_mode_set);
EXPORT_SYMBOL(rtk_fc_wifi_amsdu_pe_offload_mode_get);
EXPORT_SYMBOL(rtk_fc_wifi_dev_attr_set);
EXPORT_SYMBOL(rtk_fc_wifi_dev_attr_get);
EXPORT_SYMBOL(rtk_fc_wifi_dev_to_devidx_get);
#if 1 // backward compatible but to be removed.
EXPORT_SYMBOL(rtk_fc_wifi_client_mode_cb_register);
EXPORT_SYMBOL(rtk_fc_wifi_dmacHandle_cb_register);
#endif
EXPORT_SYMBOL(rtk_fc_wifi_amsdu_pe_offload_wifiPri_to_amsduQ_set);
EXPORT_SYMBOL(rtk_fc_wifi_amsdu_pe_offload_wifiPri_to_amsduQ_get);
#endif
EXPORT_SYMBOL(rtk_fc_wifi_event_handling_cb_register);
EXPORT_SYMBOL(rtk_fc_wifi_ipDscp_to_wifiPri_set);
EXPORT_SYMBOL(rtk_fc_wifi_ipDscp_to_wifiPri_get);

#if defined(CONFIG_RTK_FC_CRYPTO_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)	
EXPORT_SYMBOL(rtk_fc_crypto_set_src_desc);
EXPORT_SYMBOL(rtk_fc_crypto_set_dst_desc);
EXPORT_SYMBOL(rtk_fc_crypto_ps_pop_done);
EXPORT_SYMBOL(rtk_fc_crypto_send_desc);
EXPORT_SYMBOL(rtk_fc_crypto_wait_put_desc_done);
EXPORT_SYMBOL(rtk_fc_crypto_register_hw_crypto_ready_cnt_callback);
#endif

EXPORT_SYMBOL(rtk_fc_ext_dma_cache_wback_inv);
EXPORT_SYMBOL(rtk_fc_ext_br_allowed_ingress);
EXPORT_SYMBOL(rtk_fc_ext_br_fdb_get);
EXPORT_SYMBOL(rtk_fc_ext_br_fdb_get_by_pvid);

EXPORT_SYMBOL(rtk_fc_g_smp_call_function_single_async);
EXPORT_SYMBOL(rtk_fc_g_find_vpid);
EXPORT_SYMBOL(rtk_fc_g_in6_dev_get);
EXPORT_SYMBOL(rtk_fc_g_in_dev_get_rcu);
EXPORT_SYMBOL(rtk_fc_g_br_port_get_rcu);
EXPORT_SYMBOL(rtk_fc_g_rcu_read_lock);
EXPORT_SYMBOL(rtk_fc_g_rcu_read_unlock);
EXPORT_SYMBOL(rtk_fc_g_rcu_read_lock_bh);
EXPORT_SYMBOL(rtk_fc_g_rcu_read_unlock_bh);
EXPORT_SYMBOL(rtk_fc_g_call_rcu);
EXPORT_SYMBOL(rtk_fc_g_synchronize_rcu);
EXPORT_SYMBOL(rtk_fc_g_neigh_for_each_read_v4);
EXPORT_SYMBOL(rtk_fc_g_neigh_for_each_read_v6);
EXPORT_SYMBOL(rtk_fc_g_neighv4_enumerate);
EXPORT_SYMBOL(rtk_fc_g_neighv6_enumerate);
EXPORT_SYMBOL(rtk_fc_g_irq_set_affinity_hint);
EXPORT_SYMBOL(rtk_fc_g_nf_ct_iterate_cleanup);
#ifdef CONFIG_RTK_FC_TCP_SPI_SUPPORT
EXPORT_SYMBOL(rtk_fc_g_nf_ct_get_tuple);
#endif
EXPORT_SYMBOL(rtk_fc_g_ct_helper_exist_check);


