/*
 * Copyright (C) 2018 Realtek Semiconductor Corp.
 * All Rights Reserved.
 *
 * This program is the proprietary software of Realtek Semiconductor
 * Corporation and/or its licensors, and only be used, duplicated,
 * modified or distributed under the authorized license from Realtek.
 *
 * ANY USE OF THE SOFTWARE OTHER THAN AS AUTHORIZED UNDER
 * THIS LICENSE OR COPYRIGHT LAW IS PROHIBITED.
*/
#define COMPILE_RTK_L34_FC_MGR_MODULE 1

#include <linux/netfilter.h>
/* NF_IP_PRI_* used below. Up to 6.18 this arrived transitively via
 * <linux/netfilter/x_tables.h>; that chain is gone in 7.1, so include the
 * defining header directly (rtk_fc_assistant.c always did). */
#include <linux/netfilter_ipv4.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/skbuff.h>
#include <net/netfilter/nf_conntrack.h>

#include <rtk_fc_helper.h>
#include <rtk_fc_mgr.h>
#include <rtk_fc_helper_wlan.h>
#include <rtk_fc_api.h>
#include <rtk_fc_helper_multicast.h>
//#include <rtk_fc_callback.h>



//#if defined(CONFIG_RTK_L34_NETFILTER_HOOK)

#if IS_ENABLED(CONFIG_BRIDGE)
#include <linux/netfilter_bridge.h>
#endif

int devStackto_skb_fcIngressData(struct sk_buff *skb,struct net_device *dev,int isPostRouting)
{
	int stackingStart=0,stackingend=DEV_STACK_MAX;
	int firstInvalid=FAIL;
	int fcDevIdx=DEVIFIDX_INVALID_MIN;
	unsigned char *devStacking;
	fcDevIdx = rtk_fc_devGwMacIdx_get(dev);

	if((fcDevIdx == DEVIFIDX_INVALID_MIN) )
	{
		FCMGR_PRK("inindex(%d) == DEVIFIDX_VALID_MAX(%d)",fcDevIdx,DEVIFIDX_INVALID_MIN);
		return FAILED;
	}

	if(isPostRouting)
		devStacking=&skb->fcIngressData.egrDevStacking[0];
	else
		devStacking=&skb->fcIngressData.igrDevStacking[0];


	if(dev->priv_flags&IFF_EBRIDGE)
	{
		if(devStacking[stackingStart]==DEVIFIDX_INVALID_MIN)
		{
			//if non-record any device record brx first 
			devStacking[stackingStart] = fcDevIdx;
			return SUCCESS;
		}
		else
		{
			//if any device here ,we ignore all brx
			return FAILED;
		}
	}
	else
	{

		if(devStacking[stackingStart]!=DEVIFIDX_INVALID_MIN )
		{
			unsigned int dev_priv_flags = rtk_fc_devGwPrivFlag_getByFcIdx(devStacking[stackingStart]);
			//if first entry is brx ,replace brx to new stacking dev
			if(dev_priv_flags&IFF_EBRIDGE)
			{
				devStacking[stackingStart] = fcDevIdx;
				return SUCCESS;
			}
		}
	}

	//normal stacking
	if(skb->fcIngressData.isDownStream && isPostRouting)	//always update
	{
		firstInvalid=stackingStart;
	}else{
		for(; stackingStart< stackingend ;stackingStart++)
		{
			//if brx here replace it (we do not like stacking brx)
			if(devStacking[stackingStart]==DEVIFIDX_INVALID_MIN )
			{
				if(firstInvalid==FAIL)
					firstInvalid=stackingStart;
			}
			else if((devStacking[stackingStart]==fcDevIdx)||
				(!skb->fcIngressData.isDownStream && !isPostRouting))	//keep the oldest one
			{
				//already stacking
				return SUCCESS;
			}
		}
	}

	if(firstInvalid==FAIL)
	{
		FCMGR_ERR("stack full please check  DEV_STACK_MAX[%d] size isdownstream:%d isPostRouting:%d",DEV_STACK_MAX,skb->fcIngressData.isDownStream,isPostRouting);
		FCMGR_ERR("devStacking[0]=%d, devStacking[1]=%d, devStacking[2]=%d, devStacking[3]=%d",devStacking[0],devStacking[1],devStacking[2],devStacking[3]);
		return FAILED;
	}

	devStacking[firstInvalid] = fcDevIdx;
	
	return SUCCESS;

}


/* for non netfilter system decision ingress/egresss netdevice */
int rtk_fc_decision_ingress_interface(struct sk_buff *skb,struct net_device *dev0,struct net_device *dev1,struct net_device *dev2)
{
	if(dev0)
		devStackto_skb_fcIngressData(skb,dev0,0);
	if(dev1)
		devStackto_skb_fcIngressData(skb,dev1,0);
	if(dev2)
		devStackto_skb_fcIngressData(skb,dev2,0);
	return SUCCESS;

}
int rtk_fc_decision_egress_interface(struct sk_buff *skb,struct net_device *dev0,struct net_device *dev1,struct net_device *dev2)
{
	if(dev0)
		devStackto_skb_fcIngressData(skb,dev0,1);
	if(dev1)
		devStackto_skb_fcIngressData(skb,dev1,1);
	if(dev2)
		devStackto_skb_fcIngressData(skb,dev2,1);
	return SUCCESS;
}



#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
static unsigned int fc_nf_prerouting_cache(void *priv,struct sk_buff *skb,const struct nf_hook_state *state)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
static unsigned int fc_nf_prerouting_cache(const struct nf_hook_ops *ops,struct sk_buff *skb,const struct net_device *in,const struct net_device *out,int (*okfn)(struct sk_buff *))
#endif
{

	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct=nf_ct_get(skb, &ctinfo);

	if(skb->fcIngressData.doLearning) 
	{
		//only stack Input dev In pre-routing
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
		if(state && state->in)
			devStackto_skb_fcIngressData(skb,state->in,0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
		if(in)
			devStackto_skb_fcIngressData(skb,in,0)
#endif
		if (skb->fcIngressData.ct == NULL)
		{
			if (ct)
				nf_conntrack_get(&(ct->ct_general));
			skb->fcIngressData.ct = ct;
		}

		FCMGR_PRK("skb[%p] cache prerouting ct[%p] to fcIngressData\n", skb, ct);
	}

#ifdef CONFIG_RTK_SOC_RTL8198D
	if (fc_mgr_db.extFlowMibControl.enable) {
		if (fc_mgr_db.extFlowMibControl.mode == RTK_EXT_FLOW_MIB_MAC_BASED) {
			memcpy(skb->fcIngressData.ingress_sa, (skb_mac_header(skb) + ETH_ALEN), ETH_ALEN);
			memcpy(skb->fcIngressData.ingress_da, skb_mac_header(skb), ETH_ALEN);

			FCMGR_PRK("skb[%p] data[%p] mac_hdr[%p] ingress_sa[%pM] ingress_da[%pM]\n", skb, skb->data, skb_mac_header(skb), skb->fcIngressData.ingress_sa, skb->fcIngressData.ingress_da);
		}
#ifdef CONFIG_OPENWRT_SDK
		else {
			struct iphdr *iph = ip_hdr(skb);
			skb->stat_ip = ntohl(iph->saddr);

			FCMGR_PRK("skb[%p] data[%p] iph_hdr[%p] stat_ip[%pI4h]\n", skb, skb->data, iph, &skb->stat_ip);
		}
#endif
	}
#endif

       return NF_ACCEPT;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
static unsigned int fc_nf_postRouting_cache(void *priv,struct sk_buff *skb,const struct nf_hook_state *state)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
static unsigned int fc_nf_postRouting_cache(const struct nf_hook_ops *ops,struct sk_buff *skb,const struct net_device *in,const struct net_device *out,int (*okfn)(struct sk_buff *))
#endif

{
	if(skb->fcIngressData.doLearning ) 
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
		if(state && state->out)
			devStackto_skb_fcIngressData(skb,state->out,1);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
		if(out)
			devStackto_skb_fcIngressData(skb,out,1)
#endif
	}

       return NF_ACCEPT;
}


#if IS_ENABLED(CONFIG_BRIDGE)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
static unsigned int fc_nf_br_preRouting_cache(void *priv,struct sk_buff *skb,const struct nf_hook_state *state)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
static unsigned int fc_nf_br_preRouting_cache(const struct nf_hook_ops *ops,struct sk_buff *skb,const struct net_device *in, const struct net_device *out,int (*okfn)(struct sk_buff *))
#endif
{
	if(skb->fcIngressData.doLearning) 
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
		if(state && state->in)
			devStackto_skb_fcIngressData(skb,state->in,0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
		if(in)
			devStackto_skb_fcIngressData(skb,in,0)
#endif
	}

#ifdef CONFIG_RTK_SOC_RTL8198D
	if (fc_mgr_db.extFlowMibControl.enable && fc_mgr_db.extFlowMibControl.mode == RTK_EXT_FLOW_MIB_MAC_BASED) {
		memcpy(skb->fcIngressData.ingress_sa, (skb_mac_header(skb) + ETH_ALEN), ETH_ALEN);
		memcpy(skb->fcIngressData.ingress_da, skb_mac_header(skb), ETH_ALEN);

		FCMGR_PRK("skb[%p] data[%p] mac_hdr[%p] ingress_sa[%pM] ingress_da[%pM]\n", skb, skb->data, skb_mac_header(skb), skb->fcIngressData.ingress_sa, skb->fcIngressData.ingress_da);
	}
#endif

       return NF_ACCEPT;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
static unsigned int fc_nf_br_forward_cache(void *priv,struct sk_buff *skb,const struct nf_hook_state *state)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
static unsigned int fc_nf_br_forward_cache(const struct nf_hook_ops *ops,struct sk_buff *skb,const struct net_device *in, const struct net_device *out,int (*okfn)(struct sk_buff *))
#endif
{
	if(skb->fcIngressData.doLearning) {
		if(skb->cloned)
		{
			FCMGR_PRK("skb[%p] skb->cloned is TRUE. Bridge packet may flooding by PS\n", skb);
			skb->fcIngressData.skbCloned = 1;
		}
	}

	return NF_ACCEPT;
}

static unsigned int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
fc_nf_br_postRouting_first_cache(void *priv,struct sk_buff *skb,const struct nf_hook_state *state)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
fc_nf_br_postRouting_first_cache(const struct nf_hook_ops *ops,struct sk_buff *skb,const struct net_device *in,const struct net_device *out,int (*okfn)(struct sk_buff *))
#endif
{
	if(skb->fcIngressData.doLearning ) 
	{
		//only stack output dev In post-routing
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
		if(state && state->out)
			devStackto_skb_fcIngressData(skb,state->out,1);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
		if(out)
			devStackto_skb_fcIngressData(skb,out,1)
#endif

#if defined(CONFIG_RTK_SOC_RTL8198D)
		if (!skb->fcIngressData.skbCloned && skb->cloned) {
			FCMGR_PRK("skb[%p] skb->cloned is TRUE. Bridge local out packet may flooding by PS\n", skb);
			skb->fcIngressData.skbCloned = 1;
		}
#endif
	}
	

       return NF_ACCEPT;
}

static unsigned int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
fc_nf_br_postRouting_cache(void *priv,struct sk_buff *skb,const struct nf_hook_state *state)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
fc_nf_br_postRouting_cache(const struct nf_hook_ops *ops,struct sk_buff *skb,const struct net_device *in,const struct net_device *out,int (*okfn)(struct sk_buff *))
#endif
{
	if(skb->fcIngressData.doLearning ) 
	{
		//only stack output dev In post-routing
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
		if(state && state->out)
			devStackto_skb_fcIngressData(skb,state->out,1);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
		if(out)
			devStackto_skb_fcIngressData(skb,out,1)
#endif

	}
	

       return NF_ACCEPT;
}
#endif

static struct nf_hook_ops ct_cache_ops[]  __read_mostly =
{
	/* CT caching */
	// ipv4
	{
		.hook		= fc_nf_prerouting_cache,
		.pf			= NFPROTO_IPV4,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM - 1,
	},
	// ipv6
	{
		.hook		= fc_nf_prerouting_cache,
		.pf			= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM - 1,
	},
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	/* DEV caching */
	{
		.hook           = fc_nf_postRouting_cache,
		.pf             = NFPROTO_IPV4,
		.hooknum        = NF_INET_POST_ROUTING,
		.priority       = NF_IP_PRI_FIRST,
	},
	{
		.hook           = fc_nf_postRouting_cache,
		.pf             = NFPROTO_IPV6,
		.hooknum        = NF_INET_POST_ROUTING,
		.priority       = NF_IP_PRI_LAST,
	},
#endif
#if IS_ENABLED(CONFIG_BRIDGE)
	// bridge
	{
		.hook 		= fc_nf_br_preRouting_cache,
		.pf 		= NFPROTO_BRIDGE,
		.hooknum 	= NF_BR_PRE_ROUTING,
		.priority 	= NF_BR_PRI_FIRST,
	},
	{
		.hook 		= fc_nf_br_postRouting_first_cache,
		.pf 		= NFPROTO_BRIDGE,
		.hooknum 	= NF_BR_POST_ROUTING,
		.priority 	= NF_BR_PRI_FIRST,
	},
	{
		.hook 		= fc_nf_br_forward_cache,
		.pf 		= NFPROTO_BRIDGE,
		.hooknum 	= NF_BR_FORWARD,
		.priority 	= NF_BR_PRI_BRNF - 2,		// higher than br_nf_forward_ip
	},
	{
		.hook 		= fc_nf_br_postRouting_cache,
		.pf 		= NFPROTO_BRIDGE,
		.hooknum 	= NF_BR_POST_ROUTING,
		.priority 	= NF_BR_PRI_LAST,
	},
#endif
};


static int  rtk_fc_ext_ct_cache_init(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	return nf_register_net_hooks(&init_net, ct_cache_ops, ARRAY_SIZE(ct_cache_ops));
#else
       return nf_register_hooks(ct_cache_ops, ARRAY_SIZE(ct_cache_ops));
#endif
}

static void  rtk_fc_ext_ct_cache_exit(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	nf_unregister_net_hooks(&init_net, ct_cache_ops, ARRAY_SIZE(ct_cache_ops));
#else
       nf_unregister_hooks(ct_cache_ops, ARRAY_SIZE(ct_cache_ops));
#endif
}

//#endif

int rtk_fc_converter_ct(struct nf_conn *ct, struct rt_nfconn *rtct)
{
	rtct->ct			= ct;
	rtct->proto		= &ct->proto;
	rtct->status		= ct->status;
#if defined(CONFIG_NF_CONNTRACK_MARK)
	rtct->mark		= ct->mark;
#else
	rtct->mark		= 0;
#endif
	rtct->ct_general = &ct->ct_general;
	rtct->lock		= &ct->lock;

	return SUCCESS;
}

int rtk_fc_helper_init(void)
{
	fc_mgr_db.mgr_null_pointer=NULL;

//#if defined(CONFIG_RTK_L34_NETFILTER_HOOK)
       rtk_fc_ext_ct_cache_init();
//#endif

	rtk_fc_helper_register(FC_HELPER_TYPE_END, NULL);

	return 0;
}

void rtk_fc_helper_exit(void)
{
	FCMGR_PRK("helper func exit\n");

//#if defined(CONFIG_RTK_L34_NETFILTER_HOOK)
	rtk_fc_ext_ct_cache_exit();
//#endif

	rtk_fc_core_exit();

	return;
}


EXPORT_SYMBOL(rtk_fc_decision_ingress_interface);
EXPORT_SYMBOL(rtk_fc_decision_egress_interface);



