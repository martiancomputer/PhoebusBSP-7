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

#include <net/netfilter/nf_conntrack.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>

#include <net/addrconf.h>
#include <linux/inetdevice.h>
#include <net/vxlan.h>
#include <linux/seq_file.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
#include <linux/sched/signal.h>
#endif

#include <net/ipv6.h>
#include <net/ip6_fib.h>

#ifdef CONFIG_IPV6_MAPE_IPID_ADJUST
#include <net/ip6_tunnel.h>
#endif

#include "rtk_fc_helper.h"
#include "rtk_fc_mgr.h"
#include "rtk_fc_assistant.h"

#include "internal.h"

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
#include <rtk/trap.h>
#include <rtk/cpu.h>
#include <rtk/l2.h>
#endif

extern uint8 MOD_PROBE_LOG;

#if defined(CONFIG_SMP)
static uint8 wifi_tx_dis_smp_rr_cur[RTK_FC_WLAN_BAND_NUM] = {0};
#endif

static int kill_all(struct nf_conn *i, void *data)
{
	return 1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
// there is no struct net_bridge_mdb_htable in linux 5.10
#else
static inline int __br_ip4_hash(struct net_bridge_mdb_htable *mdb, __be32 ip,
				__u16 vid)
{
	return jhash_2words((__force u32)ip, vid, mdb->secret) & (mdb->max - 1);
}

#if IS_ENABLED(CONFIG_IPV6)
static inline int __br_ip6_hash(struct net_bridge_mdb_htable *mdb,
				const struct in6_addr *ip,
				__u16 vid)
{
	return jhash_2words(ipv6_addr_hash(ip), vid,
			    mdb->secret) & (mdb->max - 1);
}
#endif

static inline int br_ip_hash(struct net_bridge_mdb_htable *mdb,
			     struct br_ip *ip)
{
	switch (ip->proto) {
	case htons(ETH_P_IP):
		return __br_ip4_hash(mdb, ip->u.ip4, ip->vid);
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		return __br_ip6_hash(mdb, &ip->u.ip6, ip->vid);
#endif
	}
	return 0;
}
#endif
static inline int br_ip_equal(const struct br_ip *a, const struct br_ip *b)
{
	if (a->proto != b->proto)
		return 0;
	if (a->vid != b->vid)
		return 0;
	switch (a->proto) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	case htons(ETH_P_IP):
		return (a->src.ip4 == b->src.ip4) && (a->dst.ip4 == b->dst.ip4);
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		return (ipv6_addr_equal(&a->src.ip6, &b->src.ip6) && ipv6_addr_equal(&a->dst.ip6, &b->dst.ip6));
#endif
#else
	case htons(ETH_P_IP):
		return a->u.ip4 == b->u.ip4;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		return ipv6_addr_equal(&a->u.ip6, &b->u.ip6);
#endif
#endif
	}
	return 0;
}

#if 0 // not support 
int  rtk_fc_neigh_for_each_write(struct neigh_table *tbl, int (*cb)(struct neighbour *, unsigned char *), unsigned char *mac)
{
	int chain;
	struct neigh_hash_table *nht;
	int ret = -1;

	write_lock(&tbl->lock);
	nht = rcu_dereference_protected(tbl->nht,
					lockdep_is_held(&tbl->lock));


	for (chain = 0; chain < (1 << nht->hash_shift); chain++) {
		struct neighbour *n;
		struct neighbour __rcu **np;

		np = &nht->hash_buckets[chain];

		while ((n = rcu_dereference_protected(*np,
					lockdep_is_held(&tbl->lock))) != NULL) {

			write_lock(&n->lock);

			if((ret = cb(n, mac)) == 0/*SUCCESS*/){
				write_unlock(&n->lock);
				break;
			}

			write_unlock(&n->lock);
			np = &n->next;
		}

		if(ret == 0)
			break;
	}

	write_unlock(&tbl->lock);

	return ret;
}
#endif

int rtk_fc_neigh_lookup_cb(struct neighbour *n, unsigned char *mac)
{
	if(!memcmp(n->ha, mac, ETH_ALEN))
	{
		if(n->nud_state&NUD_VALID)
		{
			//PSTACK("[Neighbor hit] State[%d] is %s", pNei->nud_state, (pNei->nud_state&NUD_VALID)?"valid":"invalid");

			return SUCCESS;
		}
	}

	return FAIL;
}

int rtk_fc_ct_get(struct sk_buff *skb, struct nf_conn **ct)
{
	enum ip_conntrack_info ctinfo;
	*ct = nf_ct_get(skb, &ctinfo);

	return SUCCESS;
}

int rtk_fc_ct_is_dying(struct nf_conn *ct)
{
	return nf_ct_is_dying(ct);
}

unsigned long rtk_fc_ct_status_get(struct nf_conn *ct)
{
	return ct->status;
}

int rtk_fc_ct_expiretime_get(struct nf_conn *ct, unsigned long *time)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	*time = ct->timeout;
#else
	*time = ct->timeout.expires;
#endif

	return SUCCESS;
}

int rtk_fc_ct_protonum_get(struct nf_conn *ct, unsigned char *protonum)
{
	*protonum = nf_ct_protonum(ct);

	return SUCCESS;
}

int rtk_fc_ct_session_alive_check(struct nf_conn *ct, bool *alive)
{

	if(atomic_read(&ct->ct_general.use)==0){
		goto DYING;
        }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)

#else
	// to guarantee ct timer is still alive
       if(ct->timeout.data != (unsigned long)ct){
		goto DYING;
        }
#endif

	if(nf_ct_is_dying(ct)){
		goto DYING;
	}

	*alive = TRUE;
	return SUCCESS;

DYING:
	*alive = FALSE;
	return FAIL;
}

int rtk_fc_ct_helper_exist_check(struct nf_conn *ct, struct nf_conntrack_helper **helper)
{
	//rcu_dereference
	return rtk_fc_g_ct_helper_exist_check(ct, helper);
}

int rtk_fc_ct_helper_is_conenat_check(struct nf_conntrack_helper *helper)
{
#if defined(CONFIG_IP_NF_TARGET_CONENAT)
	extern struct nf_conntrack_helper nf_conntrack_helper_cone_nat;

	if(!helper || !(helper->name))
		return FAIL;

	if(!strncmp(helper->name,nf_conntrack_helper_cone_nat.name,strlen(nf_conntrack_helper_cone_nat.name)))
		return SUCCESS;
#endif

	return FAIL;
}

int  rtk_fc_ct_timer_refresh(struct nf_conn *ct)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	static unsigned int nf_ct_timeout;
	unsigned long newtime;
	#ifdef CONFIG_IPV6_MAPE_PSID_KERNEL_HOOK
	extern __u32 ftp_alg_get_new_ct_timeout(void *nf_ct, __u8 state, __u32 ct_timeout);
	#endif

	if (ct == NULL)
		return FAIL;

        if(atomic_read(&ct->ct_general.use)==0){
		//DEBUG("ct sync fail");
		return FAIL;
        }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)

#else
        if(ct->timeout.data != (unsigned long)ct){	// important checking to guarantee ct timer is still alive
		//DEBUG("ct sync fail");
		return FAIL;
        }
#endif
	if(nf_ct_is_dying(ct))
		return FAIL;
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)

#else
	if(ct->timeout.function == NULL){
		FCMGR_ERR("ERROR invalid ct timer function\n");
		return FAIL;
	}
#endif

	spin_lock_bh(&ct->lock);

	if(nf_ct_protonum(ct) == IPPROTO_TCP)
	{
		if(ct->proto.tcp.state < TCP_CONNTRACK_TIMEOUT_MAX)
			nf_ct_timeout = init_net.ct.nf_ct_proto.tcp.timeouts[ct->proto.tcp.state];
		else{
			FCMGR_ERR("ERROR invalid ct state %d\n", ct->proto.tcp.state);
			nf_ct_timeout = 10 * HZ;	// 10 sec
		}
		//FCMGR_PRK("TCP flow (TCP state: %u), timeout : %u sec", ct->proto.tcp.state, nf_ct_timeout/CONFIG_HZ);

		#ifdef CONFIG_IPV6_MAPE_PSID_KERNEL_HOOK
		/*set MAP-E ftp alg data session TIME_WAIT timeout from 60 to 120*/
		nf_ct_timeout = ftp_alg_get_new_ct_timeout(ct, ct->proto.tcp.state, nf_ct_timeout);
		#endif
	}
	else if(nf_ct_protonum(ct) == IPPROTO_UDP)
	{
		//FCMGR_PRK("UDP_CT_UNREPLIED: %u (udp.timeouts[%d]), UDP_CT_REPLIED: %u (udp.timeouts[%d])", netnsct->nf_ct_proto.udp.timeouts[UDP_CT_UNREPLIED], UDP_CT_UNREPLIED, netnsct->nf_ct_proto.udp.timeouts[UDP_CT_REPLIED], UDP_CT_REPLIED);
		if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status))
		{
			nf_ct_timeout = init_net.ct.nf_ct_proto.udp.timeouts[UDP_CT_REPLIED];
			//FCMGR_PRK("UDP flow (ct status: %lu), timeout : %u  sec (UDP_CT_REPLIED)", ct->status, nf_ct_timeout/CONFIG_HZ);
		}
		else
		{
			nf_ct_timeout = init_net.ct.nf_ct_proto.udp.timeouts[UDP_CT_UNREPLIED];
			//FCMGR_PRK("UDP flow (ct status: %lu), timeout : %u sec (UDP_CT_UNREPLIED)", ct->status, nf_ct_timeout/CONFIG_HZ);
		}
	}
	else if(nf_ct_protonum(ct) == IPPROTO_GRE)
	{
		if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status))
		{
			nf_ct_timeout = ct->proto.gre.stream_timeout;
			//FCMGR_PRK("GRE flow (ct status: %lu), timeout : %u  sec (GRE_CT_REPLIED)", ct->status, nf_ct_timeout/CONFIG_HZ);
		}
		else
		{
			nf_ct_timeout = ct->proto.gre.timeout;
			//FCMGR_PRK("GRE flow (ct status: %lu), timeout : %u sec (GRE_CT_UNREPLIED)", ct->status, nf_ct_timeout/CONFIG_HZ);
		}
	}
	else if(nf_ct_protonum(ct) == IPPROTO_ICMP)
	{
		nf_ct_timeout = init_net.ct.nf_ct_proto.icmp.timeout;
		//FCMGR_PRK("ICMP flow, timeout : %u  sec", nf_ct_timeout/CONFIG_HZ);
	}
	else{
		nf_ct_timeout = init_net.ct.nf_ct_proto.generic.timeout;
		//FCMGR_PRK("non TCP/UDP flow, timeout : %u sec", nf_ct_timeout/CONFIG_HZ);
	}

	newtime = nf_ct_timeout + jiffies;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	ct->timeout = newtime;
#else
	if ( (newtime - ct->timeout.expires >= HZ)){
		mod_timer_pending(&ct->timeout, newtime);
	}
#endif

	spin_unlock_bh(&ct->lock);
	
#endif
	return SUCCESS;
}

int  rtk_fc_ct_flush(void)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	rtk_fc_g_nf_ct_iterate_cleanup(&init_net, kill_all, NULL, 0, 0);
#endif
	return SUCCESS;
}

int rtk_fc_ct_tcp_liberal_set(struct nf_conn *ct)
{
	if(!ct)
		return FAIL;
	
	spin_lock_bh(&ct->lock);

	if(nf_ct_protonum(ct) == IPPROTO_TCP)
	{
		//FCMGR_PRK("ct[%p] tcp flags: 0x%x, 0x%x", ct, ct->proto.tcp.seen[0].flags, ct->proto.tcp.seen[1].flags);
		ct->proto.tcp.seen[0].flags |= IP_CT_TCP_FLAG_BE_LIBERAL;
		ct->proto.tcp.seen[1].flags |= IP_CT_TCP_FLAG_BE_LIBERAL;
	}

	spin_unlock_bh(&ct->lock);
	
	return SUCCESS;
}

int rtk_fc_ct_rt_5tuple_info_get(struct nf_conn *ct, rt_nf_5tuple_info_t *nf_tuple_info)
{
	uint16 l3num;
	uint8 l4protonum;
	memset(nf_tuple_info, 0, sizeof(rt_nf_5tuple_info_t));

	l3num = nf_ct_l3num(ct);
	l4protonum = nf_ct_protonum(ct);

	if(l3num == AF_INET)
	{
		// IPv4
		nf_tuple_info->l3_type = RT_NF_5TUPLE_l3TYPE_IPV4;
		nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].src_ip.v4_addr = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.ip;
		nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].dest_ip.v4_addr = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3.ip;
		nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].src_ip.v4_addr = ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u3.ip;
		nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].dest_ip.v4_addr = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u3.ip;
	}
	else if(l3num == AF_INET6)
	{
		// IPv6
		nf_tuple_info->l3_type = RT_NF_5TUPLE_l3TYPE_IPV6;
		memcpy(&(nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].src_ip.v6_addr), &(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.in6), sizeof(nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].src_ip.v6_addr));
		memcpy(&(nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].dest_ip.v6_addr), &(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3.in6), sizeof(nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].dest_ip.v6_addr));
		memcpy(&(nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].src_ip.v6_addr), &(ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u3.in6), sizeof(nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].src_ip.v6_addr));
		memcpy(&(nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].dest_ip.v6_addr), &(ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u3.in6), sizeof(nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].dest_ip.v6_addr));
	}
	if(l4protonum == IPPROTO_TCP)
	{
		nf_tuple_info->l4_type = RT_NF_5TUPLE_l4TYPE_TCP;
		nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].src_port = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.tcp.port;
		nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].dest_port = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.tcp.port;
		nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].src_port = ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u.tcp.port;
		nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].dest_port = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u.tcp.port;
	}
	else if(l4protonum == IPPROTO_UDP)
	{
		nf_tuple_info->l4_type = RT_NF_5TUPLE_l4TYPE_UDP;
		nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].src_port = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.udp.port;
		nf_tuple_info->tuple[RT_IP_CT_DIR_ORIGINAL].dest_port = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u.udp.port;
		nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].src_port = ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u.udp.port;
		nf_tuple_info->tuple[RT_IP_CT_DIR_REPLY].dest_port = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u.udp.port;
	}
	return SUCCESS;
}

int rtk_fc_ct_rt_tcp_state_get(struct nf_conn *ct, rt_tcp_conntrack_state_t *rt_tcp_state)
{
	switch(ct->proto.tcp.state)
	{
		case(TCP_CONNTRACK_NONE):
			*rt_tcp_state = RT_TCP_CONNTRACK_NONE;
			break;
		case(TCP_CONNTRACK_SYN_SENT):
			*rt_tcp_state = RT_TCP_CONNTRACK_SYN_SENT;
			break;
		case(TCP_CONNTRACK_SYN_RECV):
			*rt_tcp_state = RT_TCP_CONNTRACK_SYN_RECV;
			break;
		case(TCP_CONNTRACK_ESTABLISHED):
			*rt_tcp_state = RT_TCP_CONNTRACK_ESTABLISHED;
			break;
		case(TCP_CONNTRACK_FIN_WAIT):
			*rt_tcp_state = RT_TCP_CONNTRACK_FIN_WAIT;
			break;
		case(TCP_CONNTRACK_CLOSE_WAIT):
			*rt_tcp_state = RT_TCP_CONNTRACK_CLOSE_WAIT;
			break;
		case(TCP_CONNTRACK_LAST_ACK):
			*rt_tcp_state = RT_TCP_CONNTRACK_LAST_ACK;
			break;
		case(TCP_CONNTRACK_TIME_WAIT):
			*rt_tcp_state = RT_TCP_CONNTRACK_TIME_WAIT;
			break;
		case(TCP_CONNTRACK_CLOSE):
			*rt_tcp_state = RT_TCP_CONNTRACK_CLOSE;
			break;
		case(TCP_CONNTRACK_LISTEN):
			*rt_tcp_state = RT_TCP_CONNTRACK_LISTEN;
			break;
		case(TCP_CONNTRACK_IGNORE):
			*rt_tcp_state = RT_TCP_CONNTRACK_IGNORE;
			break;
		default:
			*rt_tcp_state = RT_TCP_CONNTRACK_MAX;
			break;
	}
	return SUCCESS;
}

int rtk_fc_ct_rt_tcp_state_set(struct nf_conn *ct, rt_tcp_conntrack_state_t rt_tcp_state)
{
	switch(rt_tcp_state)
	{
		case(RT_TCP_CONNTRACK_NONE):
			ct->proto.tcp.state = TCP_CONNTRACK_NONE;
			break;
		case(RT_TCP_CONNTRACK_SYN_SENT):
			ct->proto.tcp.state = TCP_CONNTRACK_SYN_SENT;
			break;
		case(RT_TCP_CONNTRACK_SYN_RECV):
			ct->proto.tcp.state = TCP_CONNTRACK_SYN_RECV;
			break;
		case(RT_TCP_CONNTRACK_ESTABLISHED):
			ct->proto.tcp.state = TCP_CONNTRACK_ESTABLISHED;
			break;
		case(RT_TCP_CONNTRACK_FIN_WAIT):
			ct->proto.tcp.state = TCP_CONNTRACK_FIN_WAIT;
			break;
		case(RT_TCP_CONNTRACK_CLOSE_WAIT):
			ct->proto.tcp.state = TCP_CONNTRACK_CLOSE_WAIT;
			break;
		case(RT_TCP_CONNTRACK_LAST_ACK):
			ct->proto.tcp.state = TCP_CONNTRACK_LAST_ACK;
			break;
		case(RT_TCP_CONNTRACK_TIME_WAIT):
			ct->proto.tcp.state = TCP_CONNTRACK_TIME_WAIT;
			break;
		case(RT_TCP_CONNTRACK_CLOSE):
			ct->proto.tcp.state = TCP_CONNTRACK_CLOSE;
			break;
		case(RT_TCP_CONNTRACK_LISTEN):
			ct->proto.tcp.state = TCP_CONNTRACK_LISTEN;
			break;
		case(RT_TCP_CONNTRACK_MAX):
			ct->proto.tcp.state = TCP_CONNTRACK_MAX;
			break;
		case(RT_TCP_CONNTRACK_IGNORE):
			ct->proto.tcp.state = TCP_CONNTRACK_IGNORE;
			break;
		default:
			//others, no change
			break;
	}
	return SUCCESS;
}


int rtk_fc_nf_ct_put(struct nf_conntrack *nfct)
{
	nf_conntrack_put(nfct);
	return SUCCESS;
}

int rtk_fc_br_allowed_ingress(struct net_bridge *br,
			struct net_bridge_port *p, struct sk_buff *skb,
			u16 *vid, bool *allow)
{

	*allow =  rtk_fc_ext_br_allowed_ingress(br, p, skb, vid);
	return SUCCESS;
}

char rtk_fc_br_multicast_disabled_get(struct net_bridge* br)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	return !br_opt_get(br, BROPT_MULTICAST_ENABLED);
#else
	return br->multicast_disabled;
#endif
}

char rtk_fc_br_multicast_querier_get(struct net_bridge* br)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	return br->multicast_ctx.multicast_querier;
#else
	return br->multicast_querier;
#endif
}

/*
*	return value: 	SUCCESS if found
				FAIL if not found
*/
int rtk_fc_fdb_time_refresh(struct net_device *br_netdev, unsigned char *mac, unsigned short vid, unsigned short use_pvid)
{
	struct net_bridge *br = NULL;
	struct net_bridge_fdb_entry *fdb = NULL;
	bool fdb_used = FALSE;

	if(br_netdev==NULL || mac==NULL)
	{
		//FCMGR_ERR("[Fail to lookup fdb] mac is NULL");
		return FAIL;
	}

	if(!(br_netdev->priv_flags & IFF_EBRIDGE)) // check bridge dev
		return FAIL;

	br = netdev_priv(br_netdev);

	spin_lock_bh(&br->hash_lock);

	if(use_pvid)
		fdb = rtk_fc_ext_br_fdb_get_by_pvid(br, mac, vid);
	else
		fdb = rtk_fc_ext_br_fdb_get(br, mac, vid);

	if(fdb && fdb->dst)
	{
		if (!(fdb->dst->state==BR_STATE_LEARNING || fdb->dst->state==BR_STATE_FORWARDING))
		{
			//FCMGR_PRK("[Del fdb] Lut[%d]'s fdb->dst->state is %s", pLutEntry->lutIdx, br_port_state_names[pFdb->dst->state]);
			fdb_used = FALSE;
		}
		else
		{
			if(fdb->dst->dev)
			{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
				//there is no br_port_exists in Linux 5.10
				if((fdb->dst->dev->priv_flags & IFF_BRIDGE_PORT)==0x0)
#else
				if(br_port_exists(fdb->dst->dev)==0x0)
#endif
				{
					//FCMGR_PRK("[Del fdb] Lut[%d]'s dev is not bridge port", pLutEntry->lutIdx);
					fdb_used = FALSE;
				}
				else
					fdb_used = TRUE;
			}
			else
			{
				//FCMGR_PRK("[Del fdb] Lut[%d]'s fdb->dst->dev is %s", pLutEntry->lutIdx, (fdb->dst->dev==NULL)?"NULL":"illegal");
				fdb_used = FALSE;
			}
		}
	}

	if(fdb && fdb_used)
	{
		//Update Linux fdb
		fdb->updated = jiffies;
		spin_unlock_bh(&br->hash_lock);
		return SUCCESS;
	}else{
		spin_unlock_bh(&br->hash_lock);
		return FAIL;
	}
}

/*
*	return value: 	SUCCESS if found
				FAIL if not found
*/
int rtk_fc_fdb_time_get(struct net_device *br_netdev, unsigned char *mac, unsigned short vid, rtk_fc_fdb_entry_t *fc_fdb)
{
	struct net_bridge *br = NULL;
	struct net_bridge_fdb_entry *fdb = NULL;
	bool find = FALSE;

	if(br_netdev==NULL || mac==NULL || fc_fdb==NULL)
	{
		//FCMGR_ERR("[Fail to lookup fdb] mac is NULL");
		return FAIL;
	}

	if(!(br_netdev->priv_flags & IFF_EBRIDGE)) // check bridge dev
		return FAIL;

	memset(fc_fdb, 0, sizeof(rtk_fc_fdb_entry_t));
	
	br = netdev_priv(br_netdev);

	rtk_fc_g_rcu_read_lock();

	fdb = rtk_fc_ext_br_fdb_get(br, mac, vid);

	if(fdb && fdb->dst && (fdb->dst->state==BR_STATE_LEARNING || fdb->dst->state==BR_STATE_FORWARDING)){
		find = TRUE;
		fc_fdb->updated = fdb->updated;
		fc_fdb->br_port_netdev = fdb->dst->dev;
		//FCMGR_PRK("Find fdb of mac[%pM] in dev[%s]", mac, br->dev->name);

	}else{
		find = FALSE;
		fc_fdb->updated = 0;
		fc_fdb->br_port_netdev = NULL;
	}

	rtk_fc_g_rcu_read_unlock();

	if(find)
		return SUCCESS;
	else
		return FAIL;
}

/*
*	return value: 	SUCCESS if found
				FAIL if not found
*/
int rtk_fc_neighv4_exist_check(unsigned char *mac)
{
	int ret=FAIL;

	if(mac==NULL) return ret;

	ret = rtk_fc_g_neigh_for_each_read_v4(rtk_fc_neigh_lookup_cb, mac);

	return ret;
}

/*
*	return value: 	SUCCESS if found
				FAIL if not found
*/
int rtk_fc_neighv6_exist_check(unsigned char *mac)
{

	int ret=FAIL;

	if(mac==NULL) return ret;

#if IS_ENABLED(CONFIG_IPV6)
	ret = rtk_fc_g_neigh_for_each_read_v6(rtk_fc_neigh_lookup_cb, mac);
#endif

	return ret;
}

int rtk_fc_neighv4_enumerate(void (*cb)(struct neighbour *, void *), void *cookie)
{
	rtk_fc_g_neighv4_enumerate(cb, cookie);

	return SUCCESS;
}

int rtk_fc_neighv6_enumerate(void (*cb)(struct neighbour *, void *), void *cookie)
{
	rtk_fc_g_neighv6_enumerate(cb, cookie);

	return SUCCESS;
}

char *rtk_fc_neigh_key_get(struct neighbour *neigh)
{
	if (neigh)
		return neigh->primary_key;

	return NULL;
}

int rtk_fc_neigh_key_nud_compare(struct neighbour *neigh, rtk_fc_helper_neigh_key_type_t type, void *cookie, unsigned char state)
{
	int ret=FAIL;

	if(neigh==NULL || cookie==NULL) return ret;

	if(type==FC_HELPER_KEY_PRIMARY_KEY){
		if(state!=NUD_NONE){
			//printk("%s %d neigh->nud_state=%x, primary key is %pI4, cookie is %pI4\n",__FUNCTION__,__LINE__,neigh->nud_state,neigh->primary_key,cookie);
			if(neigh && (neigh->nud_state & state) && !memcmp(neigh->primary_key,cookie,sizeof(int)))
				ret=SUCCESS;
		}else{
			if(neigh && !memcmp(neigh->primary_key,cookie,sizeof(int)))
				ret=SUCCESS;
		}
	}else if(type==FC_HELPER_KEY_HA){
		if(state!=NUD_NONE)		{
			if(neigh && (neigh->nud_state & state) && !memcmp(neigh->ha,cookie,ETH_ALEN))
				ret=SUCCESS;
		}else{
			if(neigh && !memcmp(neigh->ha,cookie,ETH_ALEN))
				ret=SUCCESS;
		}
	}

	return ret;
}

int rtk_fc_neigh_nud_state_update(struct neighbour *neigh, unsigned char state, unsigned int flag)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	neigh_update(neigh, NULL, state, flag, 0);
#else
	neigh_update(neigh, NULL, state, flag);
#endif

	return SUCCESS;
}

int rtk_fc_skb_dev_alloc_skb(unsigned int length, struct sk_buff **skb)
{
	*skb=dev_alloc_skb(length);

	return SUCCESS;
}

int rtk_fc_skb_dev_allocCritical_skb(unsigned int length, struct sk_buff **skb)
{
	*skb=dev_alloc_skb(length);

	return SUCCESS;
}

int rtk_fc_skb_swap_recycle_skb(struct sk_buff *skb, struct sk_buff **newskb)
{
#ifdef CONFIG_RTL_ETH_RECYCLED_SKB
	*newskb=recycle_skb_swap(skb);
	return SUCCESS;
#else
	return FAIL;
#endif
}

int rtk_fc_skb_dev_kfree_skb_any(struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);

	return SUCCESS;
}


int rtk_fc_skb_eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	skb->protocol = eth_type_trans(skb, dev);

	return SUCCESS;
}

#if defined(CONFIG_RTK_WFOAX)
int (*__wfo_check_srcmac_for_ax_driver)(struct sk_buff *skb) = NULL;
EXPORT_SYMBOL(__wfo_check_srcmac_for_ax_driver);
#endif /* CONFIG_RTK_WFOAX */

int rtk_fc_skb_netif_rx(struct sk_buff *skb)
{
#if defined(CONFIG_RTK_WFOAX)
	if (wfo_enable && __wfo_check_srcmac_for_ax_driver) {
		if (__wfo_check_srcmac_for_ax_driver(skb))
			return NET_RX_DROP;
	}
#endif /* CONFIG_RTK_WFOAX */


#if defined(CONFIG_RTK_FC_TESTING)
	if(fc_mgr_db._rtk_fc_post_fctestHook_netif_rx)
	{
		if(fc_mgr_db._rtk_fc_post_fctestHook_netif_rx(skb)==RTK_FC_NIC_RX_CONTINUE)
		{
			netif_rx(skb);
		}
	}
	else
#endif
	{
		netif_rx(skb);
	}
	return SUCCESS;
}


int rtk_fc_skb_netif_rx_with_hook(struct sk_buff *skb)
{

	rtk_fc_ps_netif_rx_info_t netif_rxInfo;
	netif_rxInfo.skb = skb;
	rtk_fc_smp_ps_netif_rx_dispatch(&netif_rxInfo);

	return SUCCESS;
}

int rtk_fc_skb_netif_receive_skb(struct sk_buff *skb)
{
	netif_receive_skb(skb);

	return SUCCESS;
}


int rtk_fc_skb_skb_push(struct sk_buff *skb, unsigned int len, unsigned char **pData)
{
	*pData = skb_push(skb, len);

	return SUCCESS;
}

int rtk_fc_skb_skb_pull(struct sk_buff *skb, unsigned int len, unsigned char **pData)
{
	*pData = skb_pull(skb, len);

	return SUCCESS;
}

int rtk_fc_skb_skb_put(struct sk_buff *skb, unsigned int len, unsigned char **pData)
{
	*pData = skb_put(skb, len);

	return SUCCESS;
}

int rtk_fc_skb_skb_cloned(struct sk_buff * skb)
{
	return skb_cloned(skb);
}

int rtk_fc_skb_skb_clone(struct sk_buff *skb, gfp_t gfp_mask, struct sk_buff **newskb)
{
	*newskb = skb_clone(skb, gfp_mask);

	return SUCCESS;
}

int rtk_fc_skb_skb_copy(struct sk_buff *skb, gfp_t gfp_mask, struct sk_buff **newskb)
{
	*newskb = skb_copy(skb, gfp_mask);

	return SUCCESS;
}

int rtk_fc_skb_skb_cow_head(struct sk_buff *skb, unsigned int headroom)
{
	return skb_cow_head(skb, headroom);
}


int rtk_fc_skb_skb_trim(struct sk_buff *skb, unsigned int len)
{
	skb_trim(skb, len);
	return SUCCESS;
}

int rtk_fc_skb_skb_reset_mac_header(struct sk_buff *skb)
{
	skb_reset_mac_header(skb);
	return SUCCESS;
}

int rtk_fc_skb_skb_set_network_header(struct sk_buff *skb, const int offset)
{
	skb_set_network_header(skb, offset);
	return SUCCESS;
}

int rtk_fc_skb_skb_set_transport_header(struct sk_buff *skb, const int offset)
{
	skb_set_transport_header(skb, offset);
	return SUCCESS;
}

int rtk_fc_skb_ip_summed_set(struct sk_buff *skb, uint8 value)
{
	skb->ip_summed = value;
	return SUCCESS;
}

int rtk_fc_skb_ip_summed_get(struct sk_buff *skb, uint8* value)
{
	*value = skb->ip_summed;
	return SUCCESS;
}

int rtk_fc_skb_param_set(struct sk_buff *skb, rtk_fc_skb_param_t param ,uint32 value)
{
	switch(param)
	{
		case RTK_FC_SKB_PARAM_PKTTYPE:
			skb->pkt_type = (value&0x7);
			break;
		case RTK_FC_SKB_PARAM_LEN:
			skb->len = value;
			break;
	}

	return SUCCESS;
}

int rtk_fc_skb_param_get(struct sk_buff *skb, rtk_fc_skb_param_t param ,uint32 *value)
{
	switch(param)
	{
		case RTK_FC_SKB_PARAM_PKTTYPE:
			*value = skb->pkt_type;
			break;
		case RTK_FC_SKB_PARAM_LEN:
			*value = skb->len;
			break;
	}

	return SUCCESS;
}

int rtk_fc_dev_get_by_index(struct net *net, int ifindex, struct net_device **dev)
{
	*dev = __dev_get_by_index(net, ifindex);

	return SUCCESS;
}



void *rtk_fc_dev_get_priv(struct net_device *dev)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	return netdev_priv(dev);
#else
	return dev->priv;
#endif
}

int rtk_fc_dev_get_priv_flags(struct net_device *dev, unsigned int *privflags)
{
	*privflags = dev->priv_flags;
	return SUCCESS;
}

bool rtk_fc_dev_is_priv_flags_set(struct net_device *dev, rtk_fc_netdev_priv_flag_type_t flag)
{
	bool ret = FALSE;

	switch(flag)
	{
#ifdef RTK_NETDEV_PRIV_FLAGS
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_DOMAIN_WAN):
			ret = is_priv_flags_set(dev, PRIV_DOMAIN_WAN)?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_DOMAIN_WLAN):
			printk("[WARNING] FC do not get priv flag DOMAIN_LAN %s:%d\n",__FUNCTION__,__LINE__);
			ret = is_priv_flags_set(dev, PRIV_DOMAIN_WLAN)?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_DOMAIN_ELAN):
			ret = is_priv_flags_set(dev, PRIV_DOMAIN_ELAN)?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_VSMUX):
			ret = is_priv_flags_set(dev, PRIV_VSMUX)?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_RSMUX):
			ret = is_priv_flags_set(dev, PRIV_RSMUX)?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_OSMUX):
			ret = is_priv_flags_set(dev, PRIV_OSMUX)?TRUE:FALSE;
			break;
#else
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_DOMAIN_WAN):
			ret = (dev->priv_flags & IFF_DOMAIN_WAN)?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_DOMAIN_WLAN):
			printk("[WARNING] FC do not get priv flag DOMAIN_LAN %s:%d\n",__FUNCTION__,__LINE__);
			ret = (dev->priv_flags & IFF_DOMAIN_WLAN)?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_DOMAIN_ELAN):
			ret = (dev->priv_flags & IFF_DOMAIN_ELAN)?TRUE:FALSE;
			break;
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_VSMUX):
			ret = (dev->priv_flags & IFF_VSMUX)?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_RSMUX):
			ret = (dev->priv_flags & IFF_RSMUX)?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_OSMUX):
			ret = (dev->priv_flags & IFF_OSMUX)?TRUE:FALSE;
			break;
#endif
#endif
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_EBRIDGE):
			ret = ((dev->priv_flags & IFF_EBRIDGE)||(dev->priv_flags & IFF_OPENVSWITCH))?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_BRIDGE_PORT):
			ret = ((dev->priv_flags & IFF_BRIDGE_PORT)||(dev->priv_flags & IFF_OVS_DATAPATH))?TRUE:FALSE;
			break;
		case(RTK_FC_NETDEV_PRIV_FLAG_TYPE_LIVE_ADDR_CHANGE):
			ret = (dev->priv_flags & IFF_LIVE_ADDR_CHANGE)?TRUE:FALSE;
			break;
		default:
			break;
	}

	return ret;
}

int _rtk_fc_dev_get_realdev_phyport(struct net_device *dev, rtk_fc_realdev_t *rdev)
{
	struct net_device *realdev = dev;
	bool found;
	int nest_level = 0;
	unsigned int phyport = 0;


	rdev->rdev = NULL;
	rdev->physicalPid = (int)RTK_FC_MAC_PORT_MAX;

RESCAN:

	found = FALSE;

	if(realdev == NULL || realdev->type == ARPHRD_PPP){
		return FAIL;
	}	

	// find real dev
#if defined(CONFIG_RTL_SMUX_DEV)
	if (rtk_fc_dev_is_priv_flags_set(realdev, RTK_FC_NETDEV_PRIV_FLAG_TYPE_VSMUX)) {
		struct net_device *lower_dev;
		struct list_head *iter;
		netdev_for_each_lower_dev(realdev, lower_dev, iter) {
			realdev = lower_dev;
			found = TRUE;
			break;
		}
	}else if(rtk_fc_dev_is_priv_flags_set(realdev, RTK_FC_NETDEV_PRIV_FLAG_TYPE_RSMUX)) {
		//printk("input dev %s find real dev %s\n", dev->name, realdev->name);
	}
#endif

	if(realdev && (realdev->priv_flags & IFF_802_1Q_VLAN)){	// is_vlan_dev()
		realdev = vlan_dev_priv(realdev)->real_dev;
		found = TRUE;
	}

	if(realdev && (realdev->priv_flags & IFF_EBRIDGE)){
		return FAIL;
	}

	if(found && nest_level < 5) {
		nest_level++;
		FCMGR_PRK("input dev %s find real dev %s\n", dev->name, realdev->name);
		goto RESCAN;
	}

	// find the physical port of real dev
	if (rtk_fc_dev_is_priv_flags_set(realdev, RTK_FC_NETDEV_PRIV_FLAG_TYPE_DOMAIN_ELAN) || rtk_fc_dev_is_priv_flags_set(realdev, RTK_FC_NETDEV_PRIV_FLAG_TYPE_DOMAIN_WAN)) {

#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		ca_eth_private_t *cep = netdev_priv(realdev);

		if(cep) {
			phyport = cep->port_cfg.tx_ldpid;
		}

#elif defined(CONFIG_RTK_L34_XPON_PLATFORM)
		struct re_dev_private *cp = ((struct re_dev_private*)netdev_priv(realdev));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)

		if(cp) {
			phyport = __ffs(cp->txPortMask);
		}
		
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
		if(cp && cp->pCp) {		
			int portidx = 0;
			for(portidx = 0; portidx < SW_PORT_NUM; portidx++) {
				if(realdev == cp->pCp->port2dev[portidx]) {

					phyport = portidx;
					break;
				}
			}
		}
#endif

#endif

		rdev->rdev = realdev;
		rdev->physicalPid = phyport;

		return (rdev->physicalPid != ((int)RTK_FC_MAC_PORT_MAX)) ? SUCCESS : FAIL;
	}


	return FAIL;
}

int rtk_fc_dev_get_dev_type(struct net_device *dev, unsigned int *devtype)
{
	*devtype = dev->type;

	return SUCCESS;
}

bool rtk_fc_dev_netif_running(struct net_device *dev)
{
	return netif_running(dev);
}

void rtk_fc_dev_dev_hold(struct net_device *dev)
{
	dev_hold(dev);
	return ;
}

void rtk_fc_dev_dev_put(struct net_device *dev)
{
	dev_put(dev);
	return ;
}


struct net_device* rtk_fc_vlan_dev_get_real_dev(struct net_device *dev)
{
	if (dev && is_vlan_dev(dev))
		return vlan_dev_priv(dev)->real_dev;

	return NULL;
}

int rtk_fc_copy_from_user(char *procBuffer, const char __user * userbuf, size_t count)
{
	int ret = 0;
	
	ret = copy_from_user(procBuffer, userbuf, count);
	return ret;
}


int rtk_fc_pid_task(struct task_struct **pTask, struct pid *pid, enum pid_type type)
{
	*pTask = pid_task(pid, type);
	return SUCCESS;
}

char *rtk_fc_get_task_comm(struct task_struct *pTask)
{
	return pTask->comm;
}

struct signal_struct *rtk_fc_get_task_signal(struct task_struct *pTask)
{
	if (pTask)
		return pTask->signal;

	return NULL;
}

struct tty_struct *rtk_fc_get_task_signal_tty(struct task_struct *pTask)
{
	if (pTask && pTask->signal)
		return pTask->signal->tty;

	return NULL;
}

int rtk_fc_find_vpid(struct pid **pPid, int nr)
{
	*pPid = rtk_fc_g_find_vpid(nr);
	return SUCCESS;
}

#if defined(CONFIG_FC_RTL8198F_SERIES)
static void ip6_copy_metadata(struct sk_buff *to, struct sk_buff *from)
{
	to->pkt_type = from->pkt_type;
	to->priority = from->priority;
	to->protocol = from->protocol;
	skb_dst_drop(to);
	skb_dst_set(to, dst_clone(skb_dst(from)));
	to->dev = from->dev;
	to->mark = from->mark;

#ifdef CONFIG_NET_SCHED
	to->tc_index = from->tc_index;
#endif
	nf_copy(to, from);
#if IS_ENABLED(CONFIG_NETFILTER_XT_TARGET_TRACE)
	to->nf_trace = from->nf_trace;
#endif
	skb_copy_secmark(to, from);
}


int rtk_fc_dslite_ipv6_fragment(struct sk_buff *skb, struct net_device *dev, ca_ni_tx_config_t *tx_config,
	netdev_tx_t (*xmit)(struct sk_buff *, struct net_device *, ca_ni_tx_config_t *))
{
	struct sk_buff *frag;
	struct frag_hdr *fh;
	__be32 frag_id = 0;

	unsigned int mtu, hlen, left, len;
	int hroom, troom;
	int ptr, offset = 0, err=0;
	u8 nexthdr = 0;
	int l2HdrLen = 0;

	struct net *net = dev_net(dev);

	l2HdrLen = skb_network_header(skb)-skb_mac_header(skb);//eth_hdr, vlan_hdr, ppp_hdr

	hlen =  sizeof(struct ipv6hdr);
	nexthdr = ipv6_hdr(skb)->nexthdr;
	ipv6_hdr(skb)->nexthdr = NEXTHDR_FRAGMENT;

	mtu = dev->mtu - (hlen + sizeof(struct frag_hdr));

	left = skb->len- l2HdrLen - hlen; 	/* Space per frame */
	ptr = l2HdrLen + hlen; 		/* Where to start from */

	/*
	 *	Fragment the datagram.
	 */
	hroom = dev->needed_headroom;
	troom = dev->needed_tailroom;

//	printk("[Ds-Lite][UDP fragment] hroom=%d, troom=%d, l2HdrLen=%d, left=%d, mtu=%d\n", hroom, troom, l2HdrLen, left, mtu);

	while( left > 0){
		len = left;
		/* IF: it doesn't fit, use 'mtu' - the data space left */
		if (len > mtu)
			len = mtu;
		/* IF: we are not sending up to and including the packet end
		   then align the next start on an eight byte boundary */
		if (len < left) {
			len &= ~7;
		}

		/*
		 *	Allocate buffer.
		 */

		if ((frag = alloc_skb(l2HdrLen + len + hlen + sizeof(struct frag_hdr) +
				      hroom + troom, GFP_ATOMIC)) == NULL) {
			err = -ENOMEM;
			goto fail;
		}

		/*
		 *	Set up data on packet
		 */

		ip6_copy_metadata(frag, skb);

		skb_reserve(frag, hroom);
		skb_reset_mac_header(frag);

		skb_put(frag, l2HdrLen + len + hlen + sizeof(struct frag_hdr));
		skb_set_network_header(frag, l2HdrLen);

		fh = (struct frag_hdr *)(skb_network_header(frag) + hlen);
		frag->transport_header = (frag->network_header + hlen +
					  sizeof(struct frag_hdr));
		/*
		 *	Charge the memory for the fragment to any owner
		 *	it might possess
		 */
		if (skb->sk)
			skb_set_owner_w(frag, skb->sk);

		/*
		 *	Copy the packet header into the new buffer.
		 */
		skb_copy_from_linear_data(skb, skb_mac_header(frag), l2HdrLen + hlen);//eth_hdr+IPv6_hdr

		/*
		 *	Build fragment header.
		 */
		fh->nexthdr = nexthdr;
		fh->reserved = 0;

		if (!frag_id) {
			frag_id = ipv6_select_ident(net, &ipv6_hdr(skb)->daddr, &ipv6_hdr(skb)->saddr);
		}
		fh->identification = frag_id;

	//	printk("[Ds-Lite][UDP fragment] len=%d, frag_id=0x%x\n", len, frag_id);

		/*
		 *	Copy a block of the IP datagram.
		 */
		if (skb_copy_bits(skb, ptr, skb_transport_header(frag), len))
			BUG();
		left -= len;

		fh->frag_off = htons(offset);
		if (left > 0)
			fh->frag_off |= htons(IP6_MF);
		ipv6_hdr(frag)->payload_len = htons(frag->len - l2HdrLen -
						    sizeof(struct ipv6hdr));

		ptr += len;
		offset += len;

		/*
		 *	Put this fragment into the sending queue.
		 */
		xmit(frag, dev, tx_config);
	}
	consume_skb(skb);
	return SUCCESS;

fail:
	kfree_skb(skb);
	return FAIL;
}

#endif


int rtk_fc_mape_get_ipid(struct nf_conn *ct, uint16 *ipid)
{
#ifdef CONFIG_IPV6_MAPE_IPID_ADJUST	
	uint16 port;

	if (!ct || !ipid)
		return FAIL;

	if (is_mape_session(ct) == 0)
		return FAIL;

	port = fc_get_mape_ipid(ct->out_mape_tunnel_name);
	if (port > 0){
		*ipid = port;
		return SUCCESS;
	}
	else
#endif
		return FAIL;
			
}

int rtk_set_hw_csum(char *dev_name)
{
	// not support

	return FALSE;
}

int rtk_fc_spin_lock_init(spinlock_t* lock)
{
	spin_lock_init(lock);
	return SUCCESS;
}

int rtk_fc_spin_lock(spinlock_t* lock)
{
	fc_spin_lock(lock);
	return SUCCESS;
}
int rtk_fc_spin_unlock(spinlock_t* lock)
{
	fc_spin_unlock(lock);
	return SUCCESS;
}

int rtk_fc_spin_lock_bh(spinlock_t* lock)
{
	fc_spin_lock_bh(lock);
	return SUCCESS;
}

int rtk_fc_spin_unlock_bh(spinlock_t* lock)
{
	fc_spin_unlock_bh(lock);
	return SUCCESS;
}

int rtk_fc_spin_lock_bh_irq_protect(spinlock_t* lock)
{
	fc_spin_lock_bh_irq_protect(lock);
	return SUCCESS;
}

int rtk_fc_spin_unlock_bh_irq_protect(spinlock_t* lock)
{
	fc_spin_unlock_bh_irq_protect(lock);
	return SUCCESS;
}

int rtk_fc_synchronize_rcu(void)
{
	rtk_fc_g_synchronize_rcu();
	return SUCCESS;
}

int rtk_fc_call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	rtk_fc_g_call_rcu(head, func);
	return SUCCESS;
}

int rtk_fc_rcu_read_lock(void)
{
	rtk_fc_g_rcu_read_lock();
	return SUCCESS;
}

int rtk_fc_rcu_read_unlock(void)
{
	rtk_fc_g_rcu_read_unlock();
	return SUCCESS;
}
int rtk_fc_local_bh_disable(void)
{
	local_bh_disable();
	return SUCCESS;
}
int rtk_fc_local_bh_enable(void)
{
	local_bh_enable();
	return SUCCESS;
}

int rtk_fc_irq_set_affinity_hint(unsigned int irq, const struct cpumask *m)
{
	rtk_fc_g_irq_set_affinity_hint(irq, m);
	return SUCCESS;
}

void* rtk_fc_kmalloc(size_t size, gfp_t gfp_flag)
{
	void *p=NULL;
	p=kmalloc(size, gfp_flag|__GFP_ZERO);
	if(p==NULL)
		FCMGR_ERR("memory allocate fail size %d", (int)size);
	return p;
}
int rtk_fc_kfree(const void *ptr,size_t size)
{

       kfree(ptr);
       return SUCCESS;

}


int rtk_fc_timer_timer_pending(rtk_fc_timer_list_t *pTimer)
{
	return timer_pending(&pTimer->timer);
}
int rtk_fc_timer_del_timer(rtk_fc_timer_list_t *pTimer)
{
	return timer_delete(&pTimer->timer);
}
int timer_init_timer(rtk_fc_timer_list_t *pTimer, void * function)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	timer_setup(&pTimer->timer, (void (*)(struct timer_list *))function, 0);
#else
	init_timer(&pTimer->timer);
#endif
	return SUCCESS;
}
int rtk_fc_timer_mod_timer(rtk_fc_timer_list_t *pTimer, unsigned long expires)
{
	return mod_timer(&pTimer->timer, expires);
}
int rtk_fc_timer_setup_timer(rtk_fc_timer_list_t *pTimer, void * function, unsigned long data)
{
	pTimer->data = data;
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	timer_setup(&pTimer->timer, function, 0);
#else
	setup_timer(&pTimer->timer, function, data);
#endif
	return SUCCESS;
}

unsigned long rtk_fc_timer_data_get(unsigned long callbackFunc_data)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct timer_list *pTimer = (struct timer_list *)callbackFunc_data;
	rtk_fc_timer_list_t *pTimerList;

	pTimerList = container_of(pTimer, rtk_fc_timer_list_t, timer);
	return pTimerList->data;
#else
	return callbackFunc_data;
#endif	
}

int rtk_fc_rcu_get_bridgePort_by_dev(const struct net_device *dev, struct net_bridge_port **p)
{
	//p = rcu_dereference(rx_handler_data);
	*p = rtk_fc_g_br_port_get_rcu(dev);

	return SUCCESS;
}

int rtk_fc_helper_get_bridgeDev_by_bridgePort(struct net_bridge_port *p, struct net_device** dev)
{
	struct net_bridge *br=NULL;

	if(p)
		br = p->br;
	if(br)
		*dev = br->dev;

	return SUCCESS;

}
int rtk_fc_helper_get_fdb_by_pvid( struct net_device *br_netdev, unsigned char *mac, int pvid, rtk_fc_fdb_entry_t *fc_fdb)
{
	struct net_bridge *br = NULL;
	struct net_bridge_fdb_entry *fdb = NULL;
	bool find = FALSE;

	//printk("pvid = %d, mac = %pM \n", pvid,mac);
	if(br_netdev==NULL || mac==NULL || fc_fdb==NULL)
	{
		//FCMGR_ERR("[Fail to lookup fdb] mac is NULL");
		return FAIL;
	}

	if(!(br_netdev->priv_flags & IFF_EBRIDGE)) // check bridge dev
		return FAIL;

	br = netdev_priv(br_netdev);

	rtk_fc_g_rcu_read_lock();

	fdb = rtk_fc_ext_br_fdb_get_by_pvid(br, mac, pvid);

	if(fdb && fdb->dst && (fdb->dst->state==BR_STATE_LEARNING || fdb->dst->state==BR_STATE_FORWARDING)){
		find = TRUE;
		fc_fdb->updated = fdb->updated;
		fc_fdb->br_port_netdev = fdb->dst->dev;
		//printk("Find fdb of mac[%pM] in dev[%s]\n", mac, br->dev->name);

	}else{
		find = FALSE;
		fc_fdb->updated = 0;
		fc_fdb->br_port_netdev = NULL;
	}

	rtk_fc_g_rcu_read_unlock();

	if(find)
		return SUCCESS;
	else
		return FAIL;

}

int rtk_fc_helper_get_bridgePort_pvid(struct net_bridge_port *p, int *pvid)
{
	if(p)
	{
		*pvid = br_get_pvid(nbp_vlan_group_rcu(p));
		//printk("pvid = %d\n",*pvid);
	}
	

	return SUCCESS;

}



int rtk_fc_helper_get_bridge_by_bridgePort(struct net_bridge_port *p, struct net_bridge** br)
{
	if(p)
		*br = p->br;

	return SUCCESS;
}


unsigned long rtk_fc_helper_bridge_port_flags_get(struct net_bridge_port *p)
{
	if (p)
		return p->flags;

	return 0;
}



int rtk_fc_helper_mc_kernel_snooping_disable_get(struct net_bridge* br)
{
#if defined(CONFIG_BRIDGE_IGMP_SNOOPING)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	if(br && ((br_opt_get(br, BROPT_MULTICAST_ENABLED) == 0) || (br->multicast_ctx.multicast_querier == 0)))
#else
	if(br && (br->multicast_disabled==1  || br->multicast_querier==0))
#endif
		return SUCCESS;
	else 
		return FAIL;
#else
	return FAIL;
#endif
}

int rtk_fc_rcu_get_in6_dev(struct net_device *dev, struct inet6_dev **in6_dev)
{
	*in6_dev = rtk_fc_g_in6_dev_get(dev);
	return SUCCESS;
}


int rtk_fc_rcu_indev_get_addr(struct net_device* dev, int *ipAddr)
{
	//*in6_dev = __in6_dev_get(dev);
	struct in_device *in_dev;
	in_dev = rtk_fc_g_in_dev_get_rcu(dev);
	if(in_dev!=NULL)
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
		struct in_ifaddr *ifa=in_dev->ifa_list;
		if(ifa) {
			*ipAddr = ntohl(ifa->ifa_local);
		}
#else
		for_ifa(in_dev) {
			*ipAddr = ntohl(ifa->ifa_local);

			break;
		} endfor_ifa(in_dev);
#endif
	}
	

	return SUCCESS;
}

int rtk_fc_netdev_get_if_info(struct net_device* dev, struct in_ifaddr **if_info)
{
	struct in_device* in_dev = NULL;
	//struct in_ifaddr* if_info = NULL;

	in_dev = (struct in_device *)dev->ip_ptr;
	if(in_dev!=NULL){
		*if_info = in_dev->ifa_list;
		if(*if_info!=NULL)
			return SUCCESS;
		else
			return FAIL;
	}else
		return FAIL;
}

int rtk_fc_netdev_ifa_global_addr6_set(struct inet6_dev *in6_dev, uint32 *global_ipv6_addr_set)
{
	struct inet6_ifaddr *ifa_cur, *ifa_next;

	list_for_each_entry_safe(ifa_cur, ifa_next, &in6_dev->addr_list, if_list)
	{
		if(ipv6_addr_src_scope(&ifa_cur->addr) == IPV6_ADDR_SCOPE_GLOBAL)
		{
			*global_ipv6_addr_set = 1;
			break;
		}
	}
	return SUCCESS;
}

int rtk_fc_netdev_set_nf_cs_checking(struct net_device **dev)
{
	struct net *net = dev_net(*dev);
	net->ct.sysctl_checksum = 0;
	return SUCCESS;
}
int rtk_fc_netdev_notifierInfo_to_Dev(void *ptr, struct net_device **dev)
{
	*dev = netdev_notifier_info_to_dev(ptr);
	//printk("?�rtk_fc_netdev_notifierInfo_to_Dev�?dev = %p\n",*dev);
	return SUCCESS;
}

int rtk_fc_netdev_ifav6_to_dev(void *ptr, struct net_device **dev)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)ptr;
	*dev = ifa->idev->dev;

	return SUCCESS;
}

int rtk_fc_netdev_ifa_to_dev(void *ptr, struct net_device	**dev)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	*dev = ifa->ifa_dev->dev;

	return SUCCESS;
}

int rtk_fc_netdev_set_nfTbl_macAddr(struct net_device* dev, uint8 *octet)
{
	if(dev->dev_addr!=NULL){
		if((dev->dev_addr[0]|dev->dev_addr[1]|dev->dev_addr[2]|dev->dev_addr[3]|dev->dev_addr[4]|dev->dev_addr[5])!=0x0) {
			// e.g. non-ppp dev
			memcpy(octet, dev->dev_addr, ETH_ALEN);
		}
	}
	return SUCCESS;
}

int rtk_fc_netdev_get_dev_data(struct net_device *dev, unsigned int *data, rtk_fc_netdevice_getData_type_t data_type)
{
	switch(data_type){
		case RTK_FC_NETDEV_GET_MTU: //get mtu
			*data = dev->mtu;
			break;
		case RTK_FC_NETDEV_GET_TYPE:	//get type
			*data = dev->type;
			break;
		case RTK_FC_NETDEV_GET_PRI_FLAGS:	//get pri_flags
			*data = dev->priv_flags;
			break;
		case RTK_FC_NETDEV_GET_FLAGS:	//get flags
			*data = dev->flags;
			break;
		default:
			break;
	}

	return SUCCESS;
}

int rtk_fc_netdev_check_vxlan_dev(struct net_device *dev)
{
	if(dev==NULL)
		return -1;

	if(dev->dev.type==NULL)
		return -1;
	//printk("rtk_fc_netdev_check_vxlan_dev: dev->dev.type->name = %s\n",dev->dev.type->name);
	if(!strncmp(dev->dev.type->name, "vxlan", 5))
	{
		return 1;
	}
	else if(!strncmp(dev->dev.type->name, "vlan", 4) && !strncmp(dev->name, "vxlan", 5) )
	{
		return 0;
	}
	else
		return -1;

}

int rtk_fc_netdev_get_vxlan_dev_vni(struct net_device *dev, int *vni)
{
	struct vxlan_dev *vxlan = NULL;
	if(dev == NULL)
		return FAIL;
	
	vxlan = netdev_priv(dev);
	if(vxlan==NULL)
		return FAIL;

	
	//printk("vxlan->default_dst->remote_vni: %d \n", vxlan->default_dst.remote_vni);
	*vni = vxlan->default_dst.remote_vni;

	return SUCCESS;
}

struct net_device* rtk_fc_first_net_device_get(struct net *net)
{
	return first_net_device(net);
}


struct net_device* rtk_fc_next_net_device_get(struct net_device *dev)
{
	return next_net_device(dev);
}

uint32 _rtk_mgr_flowHashIPv6SrcAddr_get(uint8 ipDes[16])
{
	/* Dst hashidx = {MC_bit, v6hsh[30:0]} */
	uint32 hashIdx = ntohl((*(uint32*)&ipDes[0])) ^ ntohl((*(uint32*)&ipDes[4])) ^ ntohl((*(uint32*)&ipDes[8])) ^ ntohl((*(uint32*)&ipDes[12]));
	//DEBUG("hashIdx = 0x%x", hashIdx);

	return hashIdx;
}
uint32 _rtk_mgr_flowHashIPv6DstAddr_get(uint8 ipDes[16])
{
	/* Dst hashidx = {MC_bit, v6hsh[30:0]} */
	uint32 hashIdx = ntohl((*(uint32*)&ipDes[0])) ^ ntohl((*(uint32*)&ipDes[4])) ^ ntohl((*(uint32*)&ipDes[8])) ^ ntohl((*(uint32*)&ipDes[12]));
	hashIdx = (hashIdx >> 31) ^ (hashIdx & 0x7fffffff);
	/* Set MC bit to 1 if ipv6 address is started with ffxx*/
	if(ipDes[0] == 0xff)
		hashIdx |= (1<<31);
	//DEBUG("hashIdx = 0x%x", hashIdx);

	return hashIdx;
}

int rtk_fc_netdev_compare_v6addr_by_v6AddrHash(struct net_device *dev, uint32 v6AddrHash, uint8 direct)
{
	struct inet6_dev *in6_dev;
	struct inet6_ifaddr *ifa_cur, *ifa_next;
	int found = FALSE;
	uint gateway_v6Hash=0;

	rtk_fc_g_rcu_read_lock();
	in6_dev = rtk_fc_g_in6_dev_get(dev);
	if(in6_dev == NULL){
		rtk_fc_g_rcu_read_unlock();
		return FAIL;
	}
	list_for_each_entry_safe(ifa_cur, ifa_next, &in6_dev->addr_list, if_list)
	{
		if(ipv6_addr_src_scope(&ifa_cur->addr) == IPV6_ADDR_SCOPE_GLOBAL)
		{
			if (direct == RTK_FC_HELPER_FLOW_DIRECTION_UPSTREAM)
			{
				gateway_v6Hash = _rtk_mgr_flowHashIPv6SrcAddr_get(ifa_cur->addr.s6_addr);
			}
			else if (direct == RTK_FC_HELPER_FLOW_DIRECTION_DOWNSTREAM)
			{
				gateway_v6Hash = _rtk_mgr_flowHashIPv6DstAddr_get(ifa_cur->addr.s6_addr);
			}
			else
			{
				FCMGR_ERR("ERROR direct %d\n", direct);
				break;
			}
			
			if(gateway_v6Hash==v6AddrHash)
			{
				found = TRUE;
				break;
			}
			
		}
	}
	rtk_fc_g_rcu_read_unlock();
	
	if(found==TRUE)
		return SUCCESS;
	else
		return FAIL;
}


int rtk_fc_netdev_compare_v6addr_by_v6Addr(struct net_device *dev, struct	in6_addr	*saddr)
{
	struct inet6_dev *in6_dev;
	struct inet6_ifaddr *ifa_cur, *ifa_next;
	int found = FALSE;

	rtk_fc_g_rcu_read_lock();
	in6_dev = rtk_fc_g_in6_dev_get(dev);
	if(in6_dev==NULL){
		rtk_fc_g_rcu_read_unlock();
		return FAIL;
	}
	list_for_each_entry_safe(ifa_cur, ifa_next, &in6_dev->addr_list, if_list)
	{
		if(ipv6_addr_src_scope(&ifa_cur->addr) == IPV6_ADDR_SCOPE_GLOBAL)
		{
			//printk("%pI6 %pI6\n", &saddr->s6_addr32[0],&ifa_cur->addr.s6_addr32[0]);
			if(!memcmp(saddr, &ifa_cur->addr, sizeof(struct in6_addr)))
			{
				found = TRUE;
				break;
			}
			
		}
	}
	rtk_fc_g_rcu_read_unlock();
	
	
	if(found==TRUE)
		return SUCCESS;
	else
		return FAIL;
}

int rtk_fc_netdev_ignore_lookup(struct net_device *dev, unsigned long event)
{
/* ref: ..\linux-4.4.x\drivers\net\veth.c */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
#define VETH_FEATURES (NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_HW_CSUM | \
                       NETIF_F_RXCSUM | NETIF_F_SCTP_CRC | NETIF_F_HIGHDMA | \
                       NETIF_F_GSO_SOFTWARE | NETIF_F_GSO_ENCAP_ALL | \
                       NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX | \
                       NETIF_F_HW_VLAN_STAG_TX | NETIF_F_HW_VLAN_STAG_RX )
#else
#define VETH_FEATURES (NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_ALL_TSO |    \
		       NETIF_F_HW_CSUM | NETIF_F_RXCSUM | NETIF_F_HIGHDMA | \
		       NETIF_F_GSO_GRE | NETIF_F_GSO_UDP_TUNNEL |	    \
		       NETIF_F_GSO_IPIP | NETIF_F_GSO_SIT | NETIF_F_UFO	|   \
		       NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX | \
		       NETIF_F_HW_VLAN_STAG_TX | NETIF_F_HW_VLAN_STAG_RX )
#endif
		       
	int ignore = FALSE;
		
	if(!strncmp(dev->name, "lo", 2))
		ignore = TRUE;
	else if(dev->type == ARPHRD_IPGRE)
		ignore = TRUE;
	else if((dev->features & VETH_FEATURES) == VETH_FEATURES)
		ignore = TRUE;

	return ignore;
}

int rtk_fc_mgr_debug_config_set(int value)
{
	fc_mgr_db.debug_prk = value;
	return SUCCESS;
}

int rtk_fc_mgr_debug_config_get(int *value)
{
	*value = fc_mgr_db.debug_prk;
	return SUCCESS;
}

int rtk_fc_mgr_eth_trunking_portmask_set(uint8 is_lan, uint8 port_1st_mask, uint8 port_2nd_mask, uint8 port_3rd_mask, uint8 port_4th_mask, uint8 port_5th_mask)
{
	if (is_lan) {
		fc_mgr_db.lanport_trunking_port_1st_mask = port_1st_mask;
		fc_mgr_db.lanport_trunking_port_2nd_mask = port_2nd_mask;
		fc_mgr_db.lanport_trunking_port_3rd_mask = port_3rd_mask;
		fc_mgr_db.lanport_trunking_port_4th_mask = port_4th_mask;
		fc_mgr_db.lanport_trunking_port_5th_mask = port_5th_mask;
	}
	else {
		fc_mgr_db.wanport_trunking_port_1st_mask = port_1st_mask;
		fc_mgr_db.wanport_trunking_port_2nd_mask = port_2nd_mask;
		fc_mgr_db.wanport_trunking_port_3rd_mask = port_3rd_mask;
		fc_mgr_db.wanport_trunking_port_4th_mask = port_4th_mask;
		fc_mgr_db.wanport_trunking_port_5th_mask = port_5th_mask;
	}

	return SUCCESS;
}

int rtk_fc_mgr_sw_check_hwflow_set(uint8 value)
{
	fc_mgr_db.sw_check_hwflow = value;
	return SUCCESS;
}

int rtk_fc_mgr_mirror_config_set(bool if_enable, uint8 mirrored_port)
{
	fc_mgr_db.mirror_enable = if_enable;
	fc_mgr_db.mirrored_port = mirrored_port;
	return SUCCESS;
}

int rtk_fc_mgr_mirror_config_get(bool *if_enable, uint8 *mirrored_port)
{
	*if_enable = fc_mgr_db.mirror_enable?TRUE:FALSE;
	*mirrored_port = fc_mgr_db.mirrored_port;
	return SUCCESS;
}

#if defined(CONFIG_SMP)

int rtk_fc_mgr_ingress_rps_set(uint16 mode, uint16 cpu_bit_maps, int *cpu_port_mapping_array)
{
	int i , j=0, k=0;
	if(mode == RTK_FC_RPS_DISPATCH_MODE_ORIGINAL)
	{
		if(fls(cpu_bit_maps) > NR_CPUS)
		{
			printk("[ERROR]Setting over CPU number %d\n",NR_CPUS);
			return FAIL;
		}
		/*
		*
		*  Set CPU MAPS
		*/
		fc_mgr_db.fc_rps_cpu_bit_mask = cpu_bit_maps;
		fc_mgr_db.fc_rps_maps.mode = RTK_FC_RPS_DISPATCH_MODE_ORIGINAL;
		// Reset CPU maps
		for(i = 0; i < 16 ; i++)
		{
			fc_mgr_db.fc_rps_maps.cpus[i] = -1;
		}
		/*
			Set CPU maps
		*	E.g. set mask CPU 0 1 3 -> mask= b ( 0x1101 )
		* 
		*		fc_mgr_db.fc_rps_maps.cpus[0] =  0;
		*		fc_mgr_db.fc_rps_maps.cpus[1] =  1;
		*		fc_mgr_db.fc_rps_maps.cpus[2] =  3;
		*		fc_mgr_db.fc_rps_maps.cpus[3] = -1;
		*		fc_mgr_db.fc_rps_maps.len = 3;
		*/
		for(i = 0; i < NR_CPUS ; i++)
		{
			if( (cpu_bit_maps)&(1<<i) )
				fc_mgr_db.fc_rps_maps.cpus[j++] = i;
		}
		fc_mgr_db.fc_rps_maps.len = j;
	
		/*
		*	Set the complete bit map for better CPU distribution
		*
		*	E.g. set mask CPU 0 1 3 -> mask= b ( 0x1101 )
		*	fc_mgr_db.fc_rps_maps.cpus[0] =  0;
		*	fc_mgr_db.fc_rps_maps.cpus[1] =  1;
		*	fc_mgr_db.fc_rps_maps.cpus[2] =  3;
		*	fc_mgr_db.fc_rps_maps.cpus[3] =  0;
		*	fc_mgr_db.fc_rps_maps.cpus[4] =  1;
		*	fc_mgr_db.fc_rps_maps.cpus[5] =  3;
		*	fc_mgr_db.fc_rps_maps.cpus[6] =  0;
		*	fc_mgr_db.fc_rps_maps.cpus[7] =  1;
		*	fc_mgr_db.fc_rps_maps.cpus[8] =  3;
		*	fc_mgr_db.fc_rps_maps.cpus[9] =  0;
		*	fc_mgr_db.fc_rps_maps.cpus[10] =  1;
		*	fc_mgr_db.fc_rps_maps.cpus[11] =  3;
		*	fc_mgr_db.fc_rps_maps.cpus[12] =  0;
		*	fc_mgr_db.fc_rps_maps.cpus[13] =  1;
		*	fc_mgr_db.fc_rps_maps.cpus[14] =  3;
		*	fc_mgr_db.fc_rps_maps.cpus[15] =  0;
		*
		*	0: 6 times,  1: 5  times, 3: 5 times
		*
		*/
		for(i = j; i < 16; i++ )
		{
			fc_mgr_db.fc_rps_maps.cpus[i] = fc_mgr_db.fc_rps_maps.cpus[k++];
			
		}
	}
	else if (mode == RTK_FC_RPS_DISPATCH_MODE_PORT)
	{
		fc_mgr_db.fc_rps_maps.len = 1;
		fc_mgr_db.fc_rps_maps.mode = RTK_FC_RPS_DISPATCH_MODE_PORT;

		for(i = 0;i < RTK_FC_MAC_PORT_MAX; i++)
		{
			fc_mgr_db.fc_rps_maps.port_to_cpu[i] = -1;
		}
		for(i = 0;i < RTK_FC_MAC_PORT_MAX; i++)
		{
			if(cpu_port_mapping_array[i]!=-1 && cpu_port_mapping_array[i]<NR_CPUS)
			{
				printk("Set port %d use cpu :%d\n",i,cpu_port_mapping_array[i]);
				fc_mgr_db.fc_rps_maps.port_to_cpu[i] = cpu_port_mapping_array[i];
			}
		
		}
		for(i = 0;i < RTK_FC_MAC_PORT_MAX; i++)
		{
			if(cpu_port_mapping_array[i]!=-1)
				printk("port_to_cpu[%d] :%d\n",i,fc_mgr_db.fc_rps_maps.port_to_cpu[i]);
			//fc_mgr_db.fc_rps_maps.port_to_cpu[i] = 2;
		}
	}
	else if (mode == RTK_FC_RPS_DISPATCH_MODE_IPSEC_SC)
	{
		if(fls(cpu_bit_maps) > NR_CPUS)
		{
			printk("[ERROR]Setting over CPU number %d\n",NR_CPUS);
			return FAIL;
		}
		/*
		*
		*  Set CPU MAPS
		*/
		fc_mgr_db.fc_rps_cpu_bit_mask = cpu_bit_maps;
		//printk("%s(%d): cpu_bit_maps = %d\n", __func__,__LINE__,cpu_bit_maps);
		fc_mgr_db.fc_rps_maps.mode = RTK_FC_RPS_DISPATCH_MODE_IPSEC_SC;
		// Reset CPU maps
		for(i = 0; i < 16 ; i++)
		{
			fc_mgr_db.fc_rps_maps.cpus[i] = -1;
		}
		for(i = 0; i < NR_CPUS ; i++)
		{
			if( (cpu_bit_maps)&(1<<i) )
				fc_mgr_db.fc_rps_maps.cpus[j++] = i;
		}
		fc_mgr_db.fc_rps_maps.len = j;
		
		for(i = j; i < 16; i++ )
		{
			fc_mgr_db.fc_rps_maps.cpus[i] = fc_mgr_db.fc_rps_maps.cpus[k++];
			printk("fc_mgr_db.fc_rps_maps.cpus[%d] = %d\n",i,fc_mgr_db.fc_rps_maps.cpus[i]);
			
		}
		for(i = 0;i < RTK_FC_MAC_PORT_MAX; i++)
		{
			fc_mgr_db.fc_rps_maps.port_to_cpu[i] = -1;
		}
		for(i = 0;i < RTK_FC_MAC_PORT_MAX; i++)
		{
			if(cpu_port_mapping_array[i]!=-1 && cpu_port_mapping_array[i]<NR_CPUS)
			{
				printk("Set port %d use cpu :%d\n",i,cpu_port_mapping_array[i]);
				fc_mgr_db.fc_rps_maps.port_to_cpu[i] = cpu_port_mapping_array[i];
			}
		
		}
		for(i = 0;i < RTK_FC_MAC_PORT_MAX; i++)
		{
			if(cpu_port_mapping_array[i]!=-1)
				printk("port_to_cpu[%d] :%d\n",i,fc_mgr_db.fc_rps_maps.port_to_cpu[i]);
			//fc_mgr_db.fc_rps_maps.port_to_cpu[i] = 2;
		}
	}
	else if(mode == RTK_FC_RPS_DISPATCH_MODE_END)
	{
		printk("FC RPS OFF!\n");
		fc_mgr_db.fc_rps_maps.len = 0;
	}
	
	return SUCCESS;

}

int rtk_fc_mgr_ingress_rps_get(uint16 *fc_rps_bit_mask, uint16 *mode, int *cpu_port_mapping_array)
{
	*fc_rps_bit_mask = fc_mgr_db.fc_rps_cpu_bit_mask;
	*mode = fc_mgr_db.fc_rps_maps.mode;
	if(fc_mgr_db.fc_rps_maps.mode == RTK_FC_RPS_DISPATCH_MODE_PORT)
		memcpy(cpu_port_mapping_array, fc_mgr_db.fc_rps_maps.port_to_cpu, sizeof(fc_mgr_db.fc_rps_maps.port_to_cpu));
	return SUCCESS;
}
#if defined(CONFIG_RTK_L34_FC_IPI_NIC_RX)
int rtk_fc_mgr_dispatch_nic_rx_ring_free(void)
{
	int i;
	int freeidx, scheduleidx;

	for(i = 0 ; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nicrx_ipi[i])
		{
			scheduleidx = atomic_read(&fc_mgr_db.nicrx_ipi[i]->priv_sched_idx);
			freeidx = atomic_read(&fc_mgr_db.nicrx_ipi[i]->priv_free_idx);
			if(scheduleidx == freeidx)
			{
				if(fc_mgr_db.nicrx_ring[i])
				{
					kfree(fc_mgr_db.nicrx_ring[i]);
					fc_mgr_db.nicrx_ring[i] = NULL;
				}
				kfree(fc_mgr_db.nicrx_ipi[i]);
				fc_mgr_db.nicrx_ipi[i] = NULL;
			}
			else
				FCMGR_ERR("nicrx_ring for cpu %d is not empty, not to free related memory", i);
		}
	}

	return SUCCESS;
}

int rtk_fc_mgr_dispatch_nic_rx_ring_alloc(void)
{
	int i;

	// do init for each cpu, priv_work_cnt and *priv_work should be prepared.
	for(i = 0 ; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nicrx_ipi[i] == NULL)
		{
			fc_mgr_db.nicrx_ipi[i] = kzalloc(sizeof(rtk_fc_nicrx_ipi_ctrl_t), GFP_ATOMIC);
			if(fc_mgr_db.nicrx_ipi[i] == NULL)
			{
				FCMGR_ERR("alloc nicrx_ipi for cpu %d failed", i);
				goto ALLOC_FAIL;
			}
			memset(fc_mgr_db.nicrx_ipi[i], 0, sizeof(rtk_fc_nicrx_ipi_ctrl_t));
			fc_mgr_db.nicrx_ipi[i]->priv_work_cnt = MAX_FC_NIC_RX_QUEUE_SIZE;

			atomic_set(&fc_mgr_db.nicrx_ipi[i]->csd_available, IPI_IDLE);

			fc_mgr_db.nicrx_ipi[i]->csd.func = rtk_fc_smp_nic_rx_tasklet;
			fc_mgr_db.nicrx_ipi[i]->csd.info = fc_mgr_db.nicrx_ipi[i];
			tasklet_init(&fc_mgr_db.nicrx_ipi[i]->tasklet, rtk_fc_smp_nic_rx_exec, (unsigned long) fc_mgr_db.nicrx_ipi[i]);
			tasklet_init(&fc_mgr_db.nicrx_ipi[i]->tasklet_empty_check, rtk_fc_smp_nic_rx_exec, (unsigned long) fc_mgr_db.nicrx_ipi[i]);

			atomic_set(&fc_mgr_db.nicrx_ipi[i]->priv_free_idx, 0);
			atomic_set(&fc_mgr_db.nicrx_ipi[i]->priv_sched_idx, 0);

			if(fc_mgr_db.nicrx_ring[i] == NULL)
			{
				fc_mgr_db.nicrx_ring[i] = kzalloc(sizeof(rtk_fc_nicrx_ring_ctrl_t), GFP_ATOMIC);
				if(fc_mgr_db.nicrx_ring[i] == NULL)
				{
					FCMGR_ERR("alloc nicrx_ring for cpu %d failed", i);
					goto ALLOC_FAIL;
				}

				fc_mgr_db.nicrx_ipi[i]->priv_work = &fc_mgr_db.nicrx_ring[i]->priv_work[0];
				memset(fc_mgr_db.nicrx_ring[i], 0, sizeof(rtk_fc_nicrx_ring_ctrl_t));
			}
		}
	}
	return SUCCESS;
ALLOC_FAIL:
	rtk_fc_mgr_dispatch_nic_rx_ring_free();
	return FAILED;
}

int rtk_fc_mgr_dispatch_nic_rx_hi_ring_free(void)
{
	int i;
	int freeidx, scheduleidx;

	for(i = 0 ; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nicrx_hi_ipi[i])
		{
			scheduleidx = atomic_read(&fc_mgr_db.nicrx_hi_ipi[i]->priv_sched_idx);
			freeidx = atomic_read(&fc_mgr_db.nicrx_hi_ipi[i]->priv_free_idx);
			if(scheduleidx == freeidx)
			{
				if(fc_mgr_db.nicrx_hiring[i])
				{
					kfree(fc_mgr_db.nicrx_hiring[i]);
					fc_mgr_db.nicrx_hiring[i] = NULL;
				}
				kfree(fc_mgr_db.nicrx_hi_ipi[i]);
				fc_mgr_db.nicrx_hi_ipi[i] = NULL;
			}
			else
				FCMGR_ERR("nicrx_hi_ring for cpu %d is not empty, not to free related memory", i);
		}
	}

	return SUCCESS;
}

int rtk_fc_mgr_dispatch_nic_rx_hi_ring_alloc(void)
{
	int i;

	for(i = 0 ; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nicrx_hi_ipi[i] == NULL)
		{
			fc_mgr_db.nicrx_hi_ipi[i] = kzalloc(sizeof(rtk_fc_nicrx_ipi_ctrl_t), GFP_ATOMIC);
			if(fc_mgr_db.nicrx_hi_ipi[i] == NULL)
			{
				FCMGR_ERR("alloc nicrx_hi_ipi for cpu %d failed", i);
				goto ALLOC_FAIL;
			}
			memset(fc_mgr_db.nicrx_hi_ipi[i], 0, sizeof(rtk_fc_nicrx_ipi_ctrl_t));
			fc_mgr_db.nicrx_hi_ipi[i]->priv_work_cnt = MAX_FC_NIC_RX_HIQUEUE_SIZE;

			atomic_set(&fc_mgr_db.nicrx_hi_ipi[i]->csd_available, IPI_IDLE);

			fc_mgr_db.nicrx_hi_ipi[i]->csd.func = rtk_fc_smp_nic_rx_tasklet;
			fc_mgr_db.nicrx_hi_ipi[i]->csd.info = fc_mgr_db.nicrx_hi_ipi[i];
			tasklet_init(&fc_mgr_db.nicrx_hi_ipi[i]->tasklet, rtk_fc_smp_nic_rx_exec, (unsigned long) fc_mgr_db.nicrx_hi_ipi[i]);
			tasklet_init(&fc_mgr_db.nicrx_hi_ipi[i]->tasklet_empty_check, rtk_fc_smp_nic_rx_exec, (unsigned long) fc_mgr_db.nicrx_hi_ipi[i]);

			atomic_set(&fc_mgr_db.nicrx_hi_ipi[i]->priv_free_idx, 0);
			atomic_set(&fc_mgr_db.nicrx_hi_ipi[i]->priv_sched_idx, 0);

			if(fc_mgr_db.nicrx_hiring[i] == NULL)
			{
				fc_mgr_db.nicrx_hiring[i] = kzalloc(sizeof(rtk_fc_nicrx_hiring_ctrl_t), GFP_ATOMIC);
				if(fc_mgr_db.nicrx_hiring[i] == NULL)
				{
					FCMGR_ERR("alloc nicrx_hiring for cpu %d failed", i);
					goto ALLOC_FAIL;
				}
				fc_mgr_db.nicrx_hi_ipi[i]->priv_work = &fc_mgr_db.nicrx_hiring[i]->priv_work[0];
				memset(fc_mgr_db.nicrx_hiring[i], 0, sizeof(rtk_fc_nicrx_hiring_ctrl_t));
			}
		}
	}
	return SUCCESS;
ALLOC_FAIL:
	rtk_fc_mgr_dispatch_nic_rx_hi_ring_free();
	return FAILED;
}
#endif

#if defined(CONFIG_RTK_L34_FC_IPI_NIC_TX)
int rtk_fc_mgr_dispatch_nic_tx_ring_free(void)
{
	int i;
	int freeidx, scheduleidx;

	for(i = 0 ; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nictx_ipi[i])
		{
			scheduleidx = atomic_read(&fc_mgr_db.nictx_sched_idx[i]);
			freeidx = atomic_read(&fc_mgr_db.nictx_free_idx[i]);
			if(scheduleidx == freeidx)
			{
				if(fc_mgr_db.nictx_work[i])
				{
					kfree(fc_mgr_db.nictx_work[i]);
					fc_mgr_db.nictx_work[i] = NULL;
				}
				kfree(fc_mgr_db.nictx_ipi[i]);
				fc_mgr_db.nictx_ipi[i] = NULL;
			}
			else
				FCMGR_ERR("nictx_ring for cpu %d is not empty, not to free related memory", i);
		}
	}

	return SUCCESS;
}

int rtk_fc_mgr_dispatch_nic_tx_ring_alloc(void)
{
	int i;
	for(i = 0 ; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nictx_ipi[i] == NULL)
		{
			atomic_set(&fc_mgr_db.nictx_free_idx[i], 0);
			atomic_set(&fc_mgr_db.nictx_sched_idx[i], 0);
			fc_mgr_db.nictx_ipi[i] = kzalloc(sizeof(rtk_fc_smp_ipi_ctrl_t), GFP_ATOMIC);
			if(fc_mgr_db.nictx_ipi[i] == NULL)
			{
				FCMGR_ERR("alloc nictx_ipi for cpu %d failed", i);
				goto ALLOC_FAIL;
			}

			// interrupt irq handler
			fc_mgr_db.nictx_ipi[i]->csd.func = rtk_fc_smp_nic_tx_tasklet;
			fc_mgr_db.nictx_ipi[i]->csd.info = fc_mgr_db.nictx_ipi[i];
			tasklet_init(&fc_mgr_db.nictx_ipi[i]->tasklet, rtk_fc_smp_nic_tx_exec, (unsigned long) i);
			atomic_set(&fc_mgr_db.nictx_ipi[i]->csd_available, IPI_IDLE);
			tasklet_init(&fc_mgr_db.nictx_ipi[i]->tasklet_empty_check, rtk_fc_smp_nic_tx_exec, (unsigned long) i);

			if(fc_mgr_db.nictx_work[i] == NULL)
			{
				fc_mgr_db.nictx_work[i] = kzalloc(sizeof(rtk_fc_smp_nicTx_work_t) * MAX_FC_NIC_TX_QUEUE_SIZE, GFP_ATOMIC);
				if(fc_mgr_db.nictx_work[i] == NULL)
				{
					FCMGR_ERR("alloc nictx_work for cpu %d failed", i);
					goto ALLOC_FAIL;
				}
			}
			memset(fc_mgr_db.nictx_work[i], 0, sizeof(rtk_fc_smp_nicTx_work_t) * MAX_FC_NIC_TX_QUEUE_SIZE);
		}
	}
	return SUCCESS;
ALLOC_FAIL:
	rtk_fc_mgr_dispatch_nic_tx_ring_free();
	return FAILED;
}
#endif

#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_RX)
int rtk_fc_mgr_dispatch_wifi_rx_ring_free(void)
{
	int i;
	int freeidx, scheduleidx;

	for(i = 0 ; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.wlanrx_ipi[i])
		{
			scheduleidx = atomic_read(&fc_mgr_db.wlanrx_sched_idx[i]);
			freeidx = atomic_read(&fc_mgr_db.wlanrx_free_idx[i]);
			if(scheduleidx == freeidx)
			{
				if(fc_mgr_db.wlanrx_work[i])
				{
					kfree(fc_mgr_db.wlanrx_work[i]);
					fc_mgr_db.wlanrx_work[i] = NULL;
				}
				kfree(fc_mgr_db.wlanrx_ipi[i]);
				fc_mgr_db.wlanrx_ipi[i] = NULL;
			}
			else
				FCMGR_ERR("wifirx_ring for cpu %d is not empty, not to free related memory", i);
		}
	}

	return SUCCESS;
}

int rtk_fc_mgr_dispatch_wifi_rx_ring_alloc(void)
{
	int i;
	for(i = 0; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.wlanrx_ipi[i] == NULL)
		{
			atomic_set(&fc_mgr_db.wlanrx_free_idx[i], 0);
			atomic_set(&fc_mgr_db.wlanrx_sched_idx[i], 0);
			fc_mgr_db.wlanrx_ipi[i] = kzalloc(sizeof(rtk_fc_smp_ipi_ctrl_t), GFP_ATOMIC);
			if(fc_mgr_db.wlanrx_ipi[i] == NULL)
			{
				FCMGR_ERR("alloc wlanrx_ipi for cpu %d failed", i);
				goto ALLOC_FAIL;
			}

			// interrupt irq handler
			fc_mgr_db.wlanrx_ipi[i]->csd.func = rtk_fc_smp_wlan_rx_tasklet;
			fc_mgr_db.wlanrx_ipi[i]->csd.info = fc_mgr_db.wlanrx_ipi[i];
			tasklet_init(&fc_mgr_db.wlanrx_ipi[i]->tasklet, rtk_fc_smp_wlan_rx_exec, (unsigned long) i);
			atomic_set(&fc_mgr_db.wlanrx_ipi[i]->csd_available, IPI_IDLE);
			tasklet_init(&fc_mgr_db.wlanrx_ipi[i]->tasklet_empty_check, rtk_fc_smp_wlan_rx_exec, (unsigned long) i);

			if(fc_mgr_db.wlanrx_work[i] == NULL)
			{
				fc_mgr_db.wlanrx_work[i] = kzalloc(sizeof(rtk_fc_smp_wlanRx_work_t) * MAX_FC_WLAN_RX_QUEUE_SIZE, GFP_ATOMIC);
				if(fc_mgr_db.wlanrx_work[i] == NULL)
				{
					FCMGR_ERR("alloc wlanrx_work for cpu %d failed", i);
					goto ALLOC_FAIL;
				}
				memset(fc_mgr_db.wlanrx_work[i], 0, sizeof(rtk_fc_smp_wlanRx_work_t) * MAX_FC_WLAN_RX_QUEUE_SIZE);
			}
		}
	}
	return SUCCESS;
ALLOC_FAIL:
	rtk_fc_mgr_dispatch_wifi_rx_ring_free();
	return FAILED;
}
#endif

#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_TX)
int rtk_fc_mgr_dispatch_wifi_tx_ring_free(void)
{
	int i;
	int freeidx, scheduleidx;

	for(i = 0 ; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.wlantx_ipi[i])
		{
			scheduleidx = atomic_read(&fc_mgr_db.wlantx_sched_idx[i]);
			freeidx = atomic_read(&fc_mgr_db.wlantx_free_idx[i]);
			if(scheduleidx == freeidx)
			{
				if(fc_mgr_db.wlantx_work[i])
				{
					kfree(fc_mgr_db.wlantx_work[i]);
					fc_mgr_db.wlantx_work[i] = NULL;
				}
				kfree(fc_mgr_db.wlantx_ipi[i]);
				fc_mgr_db.wlantx_ipi[i] = NULL;
			}
			else
				FCMGR_ERR("wifitx_ring for cpu %d is not empty, not to free related memory", i);
		}
	}

	return SUCCESS;
}

int rtk_fc_mgr_dispatch_wifi_tx_ring_alloc(void)
{
	int i;

	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.wlantx_ipi[i] == NULL)
		{
			atomic_set(&fc_mgr_db.wlantx_free_idx[i], 0);
			atomic_set(&fc_mgr_db.wlantx_sched_idx[i], 0);

			fc_mgr_db.wlantx_ipi[i] = kzalloc(sizeof(rtk_fc_smp_ipi_ctrl_t), GFP_ATOMIC);
			if(fc_mgr_db.wlantx_ipi[i] == NULL)
			{
				FCMGR_ERR("alloc wlantx_ipi for cpu %d failed", i);
				goto ALLOC_FAIL;
			}
			fc_mgr_db.wlantx_ipi[i]->csd.func = rtk_fc_smp_wlan_tx_tasklet;
			fc_mgr_db.wlantx_ipi[i]->csd.info = fc_mgr_db.wlantx_ipi[i];
			tasklet_init(&fc_mgr_db.wlantx_ipi[i]->tasklet, rtk_fc_smp_wlan_tx_exec, (unsigned long) i);
			atomic_set(&fc_mgr_db.wlantx_ipi[i]->csd_available, IPI_IDLE);
			tasklet_init(&fc_mgr_db.wlantx_ipi[i]->tasklet_empty_check, rtk_fc_smp_wlan_tx_exec, (unsigned long) i);

			if(fc_mgr_db.wlantx_work[i] == NULL)
			{
				fc_mgr_db.wlantx_work[i] = kzalloc(sizeof(rtk_fc_smp_wlanTx_work_t) * MAX_FC_WLAN_TX_QUEUE_SIZE, GFP_ATOMIC);
				if(fc_mgr_db.wlantx_work[i] == NULL)
				{
					FCMGR_ERR("alloc wlantx_work for cpu %d failed", i);
					goto ALLOC_FAIL;
				}
				memset(fc_mgr_db.wlantx_work[i] , 0, sizeof(rtk_fc_smp_wlanTx_work_t) * MAX_FC_WLAN_TX_QUEUE_SIZE);
			}
		}
	}
	return SUCCESS;
ALLOC_FAIL:
	rtk_fc_mgr_dispatch_wifi_tx_ring_free();
	return FAILED;

}
#endif

int rtk_fc_mgr_dispatch_ps_netif_rx_ring_free(void)
{
	int i;
	int freeidx, scheduleidx;

	for(i = 0 ; i < num_online_cpus(); i++)
	{
		if(fc_mgr_db.ps_netif_rx_ipi[i])
		{
			scheduleidx = atomic_read(&fc_mgr_db.ps_netif_rx_sched_idx[i]);
			freeidx = atomic_read(&fc_mgr_db.ps_netif_rx_free_idx[i]);
			if(scheduleidx == freeidx)
			{
				if(fc_mgr_db.ps_netif_rx_work[i])
				{
					kfree(fc_mgr_db.ps_netif_rx_work[i]);
					fc_mgr_db.ps_netif_rx_work[i] = NULL;
				}
				kfree(fc_mgr_db.ps_netif_rx_ipi[i]);
				fc_mgr_db.ps_netif_rx_ipi[i] = NULL;
			}
			else
				FCMGR_ERR("ps_netif_rx for cpu %d is not empty, not to free related memory", i);
		}
	}

	return SUCCESS;
}

int rtk_fc_mgr_dispatch_ps_netif_rxring_alloc(void)
{
	int i;

	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.ps_netif_rx_ipi[i] == NULL)
		{
			atomic_set(&fc_mgr_db.ps_netif_rx_free_idx[i], 0);
			atomic_set(&fc_mgr_db.ps_netif_rx_sched_idx[i], 0);

			fc_mgr_db.ps_netif_rx_ipi[i] = kzalloc(sizeof(rtk_fc_smp_ipi_ctrl_t), GFP_ATOMIC);
			if(fc_mgr_db.ps_netif_rx_ipi[i] == NULL)
			{
				FCMGR_ERR("alloc ps_netif_rx for cpu %d failed", i);
				goto ALLOC_FAIL;
			}
			fc_mgr_db.ps_netif_rx_ipi[i]->csd.func = rtk_fc_smp_ps_netif_rx_tasklet;
			fc_mgr_db.ps_netif_rx_ipi[i]->csd.info = fc_mgr_db.ps_netif_rx_ipi[i];
			tasklet_init(&fc_mgr_db.ps_netif_rx_ipi[i]->tasklet, rtk_fc_smp_ps_netif_rx_exec, (unsigned long) i);
			atomic_set(&fc_mgr_db.ps_netif_rx_ipi[i]->csd_available, IPI_IDLE);

			if(fc_mgr_db.ps_netif_rx_work[i] == NULL)
			{
				fc_mgr_db.ps_netif_rx_work[i] = kzalloc(sizeof(rtk_fc_smp_ps_netif_rx_work_t) * MAX_FC_PS_NETIF_RX_QUEUE_SIZE, GFP_ATOMIC);
				if(fc_mgr_db.ps_netif_rx_work[i] == NULL)
				{
					FCMGR_ERR("alloc ps_netif_rx for cpu %d failed", i);
					goto ALLOC_FAIL;
				}
				memset(fc_mgr_db.ps_netif_rx_work[i] , 0, sizeof(rtk_fc_smp_ps_netif_rx_work_t) * MAX_FC_PS_NETIF_RX_QUEUE_SIZE);
			}
		}
	}
	return SUCCESS;
ALLOC_FAIL:
	rtk_fc_mgr_dispatch_ps_netif_rx_ring_free();
	return FAILED;

}



int rtk_fc_mgr_dispatchMode_set(rtk_fc_dispatch_mode_t dispatch_mode, uint32 mask, rtk_fc_dispatch_point_t dispatch_point, int *cpu_port_mapping_array)
{
#if !defined(CONFIG_RTK_L34_FC_IPI_NIC_RX)
	if((dispatch_point >= RTK_FC_MGR_DISPATCH_NIC_RX) && (dispatch_point <= RTK_FC_MGR_DISPATCH_HIGHPRI_NIC_RX))
		return FAIL;
#endif
#if !defined(CONFIG_RTK_L34_FC_IPI_NIC_TX)
	if(dispatch_point == RTK_FC_MGR_DISPATCH_NIC_TX)
		return FAIL;
#endif
#if !defined(CONFIG_RTK_L34_FC_IPI_WIFI_RX)
	if((dispatch_point >= RTK_FC_MGR_DISPATCH_WLAN0_RX) && (dispatch_point <= RTK_FC_MGR_DISPATCH_WLAN2_RX))
		return FAIL;
#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
	if((dispatch_point >= RTK_FC_MGR_DISPATCH_WLANMLO_RX) && (dispatch_point <= RTK_FC_MGR_DISPATCH_WLANMLO_0_RX))
		return FAIL;
#endif
#endif
#if !defined(CONFIG_RTK_L34_FC_IPI_WIFI_TX)
	if((dispatch_point >= RTK_FC_MGR_DISPATCH_WLAN0_TX) && (dispatch_point <= RTK_FC_MGR_DISPATCH_WLAN2_TX))
		return FAIL;
#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
	if((dispatch_point >= RTK_FC_MGR_DISPATCH_WLANMLO_TX) && (dispatch_point <= RTK_FC_MGR_DISPATCH_WLANMLO_0_TX))
		return FAIL;
#endif
#endif

#if defined(CONFIG_NR_CPUS)
	if(((dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_HASH) || (dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_RR))
		&& (dispatch_point >= RTK_FC_MGR_DISPATCH_WLAN0_TX) &&
#if !defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
		(dispatch_point <= RTK_FC_MGR_DISPATCH_WLAN2_TX)
#else
		(dispatch_point <= RTK_FC_MGR_DISPATCH_WLANMLO_0_TX)
#endif
		)
	{
		// wifi TX and dispatch mode is SMP mode => smp_id is CPU mask
		if(dispatch_mode.smp_id >= (1 << num_online_cpus()))
		{
			FCMGR_ERR("cpu mask[0x%x] for smp dispathch point %d is not supported! (max cpu num is %d)", dispatch_mode.smp_id, dispatch_point, num_online_cpus());
		}
	}
	else if(dispatch_mode.mode == RTK_FC_DISPATCH_MODE_RPS)
	{
		if(dispatch_mode.smp_id >= (1 << num_online_cpus()))
		{
			FCMGR_ERR("cpu mask[0x%x] for smp dispathch point %d is not supported! (max cpu num is %d)", dispatch_mode.smp_id, dispatch_point, num_online_cpus());
		}
	}
	else if(dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_IPSEC_SC)
	{
		if(dispatch_mode.smp_id >= (1 << CONFIG_NR_CPUS))
		{
			FCMGR_ERR("cpu mask[0x%x] for smp dispathch point %d is not supported! (max cpu num is %d)", dispatch_mode.smp_id, dispatch_point, CONFIG_NR_CPUS);
		}
	}
	else
	{
		if(dispatch_mode.smp_id >= num_online_cpus()) {
			FCMGR_ERR("cpu idx[%d] for smp dispathch point %d is not supported! (max cpu num is %d)", dispatch_mode.smp_id, dispatch_point, num_online_cpus());
			return FAIL;
		}
	}

	
#endif

	if((RTK_FC_MGR_DISPATCH_START <= dispatch_point) && (dispatch_point < RTK_FC_MGR_DISPATCH_MAX))
	{
		rtk_fc_dispatchcpu_t ori_smp_id = fc_mgr_db.smp_dispatch[dispatch_point].smp_id;
		bool if_buf_alloc_failed = FALSE;

		if(mask & RTK_FC_MGR_DISPATCH_MASK_CPU)
			fc_mgr_db.smp_dispatch[dispatch_point].smp_id = dispatch_mode.smp_id;

		if(mask & RTK_FC_MGR_DISPATCH_MASK_MODE)
		{
			if(((1 << fc_mgr_db.smp_dispatch[dispatch_point].mode) & RTK_FC_DISPATCH_NEED_SW_RING_BUF)
				&& !((1 << dispatch_mode.mode) & RTK_FC_DISPATCH_NEED_SW_RING_BUF))
			{
				fc_mgr_db.smp_dispatch[dispatch_point].mode = dispatch_mode.mode; /*** mode chage should be done BEFORE memory free ***/
				// free ring buffer memory: need sw ring buffer => no need sw ring buffe
				switch(dispatch_point)
				{
#if defined(CONFIG_RTK_L34_FC_IPI_NIC_RX)
					case RTK_FC_MGR_DISPATCH_NIC_RX:
						rtk_fc_mgr_dispatch_nic_rx_ring_free();
						break;
					case RTK_FC_MGR_DISPATCH_HIGHPRI_NIC_RX:
						rtk_fc_mgr_dispatch_nic_rx_hi_ring_free();
						break;
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_NIC_TX)
					case RTK_FC_MGR_DISPATCH_NIC_TX:
						rtk_fc_mgr_dispatch_nic_tx_ring_free();
						break;
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_RX)
					case RTK_FC_MGR_DISPATCH_WLAN0_RX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLAN1_RX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLAN2_RX:
#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLANMLO_RX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLANMLO_0_RX:
#endif
						{
							int band=0, needed=0;
							for(band=0; band<RTK_FC_WLAN_ID_MAX; band++) {
								if(dispatch_point == (RTK_FC_MGR_DISPATCH_WLAN0_RX+band))
									continue;
								if((1 << fc_mgr_db.smp_dispatch[RTK_FC_MGR_DISPATCH_WLAN0_RX+band].mode) & RTK_FC_DISPATCH_NEED_SW_RING_BUF) {
									needed = 1;
									break;
								}
							}
							if(needed==0)
								rtk_fc_mgr_dispatch_wifi_rx_ring_free();
						}
						break;
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_TX)
					case RTK_FC_MGR_DISPATCH_WLAN0_TX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLAN1_TX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLAN2_TX:
#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLANMLO_TX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLANMLO_0_TX:
#endif
						{
							int band=0, needed=0;
							for(band=0; band<RTK_FC_WLAN_ID_MAX; band++) {
								if(dispatch_point == (RTK_FC_MGR_DISPATCH_WLAN0_TX+band))
									continue;
								if((1 << fc_mgr_db.smp_dispatch[RTK_FC_MGR_DISPATCH_WLAN0_TX+band].mode) & RTK_FC_DISPATCH_NEED_SW_RING_BUF) {
									needed = 1;
									break;
								}
							}
							if(needed==0)
								rtk_fc_mgr_dispatch_wifi_tx_ring_free();
						}
						break;
#endif
					case RTK_FC_MGR_DISPATCH_PS_NETIF_RX:
						rtk_fc_mgr_dispatch_ps_netif_rx_ring_free();
						/* fall through */
					default:
						break;
				}
			}
			else if(!((1 << fc_mgr_db.smp_dispatch[dispatch_point].mode) & RTK_FC_DISPATCH_NEED_SW_RING_BUF)
				&& ((1 << dispatch_mode.mode) & RTK_FC_DISPATCH_NEED_SW_RING_BUF))
			{
				// allocate ring buffer memory: no need sw ring buffer => need sw ring buffer
				switch(dispatch_point)
				{
#if defined(CONFIG_RTK_L34_FC_IPI_NIC_RX)
					case RTK_FC_MGR_DISPATCH_NIC_RX:
						if(rtk_fc_mgr_dispatch_nic_rx_ring_alloc())
						{
							FCMGR_ERR("nic rx ring buf allocate failed!! keep original setting");
							if_buf_alloc_failed = TRUE;
						}
						break;
					case RTK_FC_MGR_DISPATCH_HIGHPRI_NIC_RX:
						if(rtk_fc_mgr_dispatch_nic_rx_hi_ring_alloc())
						{
							FCMGR_ERR("nic rx hi ring buf allocate failed!! keep original setting");
							if_buf_alloc_failed = TRUE;
						}
						break;
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_NIC_TX)
					case RTK_FC_MGR_DISPATCH_NIC_TX:
						if(rtk_fc_mgr_dispatch_nic_tx_ring_alloc())
						{
							FCMGR_ERR("nic tx ring buf allocate failed!! keep original setting");
							if_buf_alloc_failed = TRUE;
						}
						break;
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_RX)
					case RTK_FC_MGR_DISPATCH_WLAN0_RX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLAN1_RX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLAN2_RX:
#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLANMLO_RX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLANMLO_0_RX:
#endif
						if(rtk_fc_mgr_dispatch_wifi_rx_ring_alloc())
						{
							FCMGR_ERR("wifi rx ring buf allocate failed!! keep original setting");
							if_buf_alloc_failed = TRUE;
						}
						break;
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_TX)
					case RTK_FC_MGR_DISPATCH_WLAN0_TX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLAN1_TX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLAN2_TX:
#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLANMLO_TX:
						/* fall through */
					case RTK_FC_MGR_DISPATCH_WLANMLO_0_TX:
#endif
						if(rtk_fc_mgr_dispatch_wifi_tx_ring_alloc())
						{
							FCMGR_ERR("wifi tx ring buf allocate failed!! keep original setting");
							if_buf_alloc_failed = TRUE;
						}
						break;
#endif
					case RTK_FC_MGR_DISPATCH_PS_NETIF_RX:
						if(rtk_fc_mgr_dispatch_ps_netif_rxring_alloc())
						{
							FCMGR_ERR("netif_rx ring buf allocate failed!! keep original setting");
							if_buf_alloc_failed = TRUE;
						}
						/* fall through */
					default:
						break;
				}
				if(if_buf_alloc_failed)
					fc_mgr_db.smp_dispatch[dispatch_point].smp_id = ori_smp_id;// keep original setting
				else
					fc_mgr_db.smp_dispatch[dispatch_point].mode = dispatch_mode.mode; /*** mode chage should be done AFTER memory allocate ***/
			}
			else
				fc_mgr_db.smp_dispatch[dispatch_point].mode = dispatch_mode.mode;
		}

		if(((dispatch_point >= RTK_FC_MGR_DISPATCH_WLAN0_TX) &&
#if !defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
			(dispatch_point <= RTK_FC_MGR_DISPATCH_WLAN2_TX)
#else
			(dispatch_point <= RTK_FC_MGR_DISPATCH_WLANMLO_0_TX)
#endif
			) && 
			((fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_SMP_BY_HASH) || (fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_SMP_BY_RR)))
		{
			int i;
			uint valid_count = 0;
			for_each_possible_cpu(i)
			{
				if((dispatch_mode.smp_id & (1 << i)) && (i < NR_CPUS))
					fc_mgr_db.wifi_tx_smp_mode_cpu_sel[dispatch_point - RTK_FC_MGR_DISPATCH_WLAN0_TX].cpu_id[(valid_count++)] = i;
			}
			fc_mgr_db.wifi_tx_smp_mode_cpu_sel[dispatch_point - RTK_FC_MGR_DISPATCH_WLAN0_TX].valid_cpu_count = valid_count;

			if(fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_SMP_BY_RR)
				wifi_tx_dis_smp_rr_cur[dispatch_point - RTK_FC_MGR_DISPATCH_WLAN0_TX] = 0;
		}

		if(fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_RPS)
		{
			rtk_fc_mgr_ingress_rps_set(RTK_FC_RPS_DISPATCH_MODE_ORIGINAL, fc_mgr_db.smp_dispatch[dispatch_point].smp_id, NULL);
			fc_mgr_db.smp_dispatch[dispatch_point].smp_id = RTK_FC_DEFAULT_DISPATCH_CPU; // Set default SMP_ID for getting hash failed
		}

		if(fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_SMP_BY_PORT)
		{
			if(cpu_port_mapping_array == NULL)
				printk("Setting nic rx dispatch by port failed!\n");
			else
				rtk_fc_mgr_ingress_rps_set(RTK_FC_RPS_DISPATCH_MODE_PORT, fc_mgr_db.smp_dispatch[dispatch_point].smp_id, cpu_port_mapping_array);
		}

		if(fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_SMP_IPSEC_SC)
		{
			printk("IPSEC nic rx mode!  fc_mgr_db.smp_dispatch[%d].smp_id = %d\n", dispatch_point, fc_mgr_db.smp_dispatch[dispatch_point].smp_id);
			rtk_fc_mgr_ingress_rps_set(RTK_FC_RPS_DISPATCH_MODE_IPSEC_SC, fc_mgr_db.smp_dispatch[dispatch_point].smp_id, cpu_port_mapping_array);
		}
	}
	
	return SUCCESS;
}
int rtk_fc_mgr_dispatch_smp_mode_wifi_tx_cpuId_get(uint8 *cpuId, uint16 *wifi_tx_dispatch_mode, uint32 hashIdx, rtk_fc_dispatch_point_t dispatch_point)
{
	uint n;
	int band = dispatch_point - RTK_FC_MGR_DISPATCH_WLAN0_TX;
#if !defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
	if((dispatch_point < RTK_FC_MGR_DISPATCH_WLAN0_TX) || (dispatch_point > RTK_FC_MGR_DISPATCH_WLAN2_TX))
#else
	if((dispatch_point < RTK_FC_MGR_DISPATCH_WLAN0_TX) || (dispatch_point > RTK_FC_MGR_DISPATCH_WLANMLO_0_TX))
#endif
		return FAIL;
	*wifi_tx_dispatch_mode = fc_mgr_db.smp_dispatch[dispatch_point].mode;

	if(*wifi_tx_dispatch_mode == RTK_FC_DISPATCH_MODE_SMP_BY_HASH)
	{
		if(fc_mgr_db.wifi_tx_smp_mode_cpu_sel[band].valid_cpu_count == 0)
			return FAIL; // smp_id is cpu mask here, should not be 0.

		// decide cpuId by hash index
		n = hashIdx % fc_mgr_db.wifi_tx_smp_mode_cpu_sel[band].valid_cpu_count;
		*cpuId = fc_mgr_db.wifi_tx_smp_mode_cpu_sel[band].cpu_id[n];
	}
	else if(*wifi_tx_dispatch_mode == RTK_FC_DISPATCH_MODE_SMP_BY_RR)
	{
		if(fc_mgr_db.wifi_tx_smp_mode_cpu_sel[band].valid_cpu_count == 0)
			return FAIL; // smp_id is cpu mask here, should not be 0.

		// decide cpuId by round-robin
		n = wifi_tx_dis_smp_rr_cur[band];
		*cpuId = fc_mgr_db.wifi_tx_smp_mode_cpu_sel[band].cpu_id[n];
		wifi_tx_dis_smp_rr_cur[band] = (wifi_tx_dis_smp_rr_cur[band] + 1) % fc_mgr_db.wifi_tx_smp_mode_cpu_sel[band].valid_cpu_count;
	}
	if(*cpuId >= NR_CPUS)
	{
		return FAIL;
	}

	return SUCCESS;
}

int rtk_fc_mgr_dispatchMode_get(rtk_fc_dispatch_mode_t *dispatch_mode, rtk_fc_dispatch_point_t dispatch_point)
{
	int i ;
	if((dispatch_point >= RTK_FC_MGR_DISPATCH_START ) && (dispatch_point < RTK_FC_MGR_DISPATCH_MAX))
	{
		(*dispatch_mode).smp_id = fc_mgr_db.smp_dispatch[dispatch_point].smp_id;
		(*dispatch_mode).mode = fc_mgr_db.smp_dispatch[dispatch_point].mode;
	}
	if(fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_RPS || fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_SMP_IPSEC_SC)
	{
		printk("FC RPS use CPU: ");
	
		for(i = 0; i < NR_CPUS ; i++)
		{
			if( (fc_mgr_db.fc_rps_cpu_bit_mask)&(1<<i) )
				printk(" %d",i);
		}
		printk("\n");
	}
	if(fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_SMP_BY_PORT || fc_mgr_db.smp_dispatch[dispatch_point].mode == RTK_FC_DISPATCH_MODE_SMP_IPSEC_SC)
	{
		int i ;
		
		for(i = 0;i < RTK_FC_MAC_PORT_MAX; i++)
		{
			if(fc_mgr_db.fc_rps_maps.port_to_cpu[i]!=-1)
			printk("port[%d] will dispatch to cpu[%d]\n",i,fc_mgr_db.fc_rps_maps.port_to_cpu[i]);
			//fc_mgr_db.fc_rps_maps.port_to_cpu[i] = 2;
		}
	}
	return SUCCESS;
}

int rtk_fc_mgr_ring_buffer_state_get(rtk_fc_mgr_dispatch_point_t func, rtk_fc_mgr_ring_buf_state_t *state)
{
	int i;

	i = 0;
	
	switch(func) 
	{
#if defined(CONFIG_RTK_L34_FC_IPI_NIC_RX)
		case RTK_FC_MGR_DISPATCH_NIC_RX:
			{
				for_each_possible_cpu(i) {
					if((i < num_online_cpus()) && (i < CONFIG_NR_CPUS)){
						if(fc_mgr_db.nicrx_hi_ipi[i])
						{
							rtk_fc_nicrx_ipi_ctrl_t *smpnicrx = fc_mgr_db.nicrx_hi_ipi[i];
						
							state->free_idx = atomic_read(&smpnicrx->priv_free_idx);
							state->sched_idx = atomic_read(&smpnicrx->priv_sched_idx);

							printk("%-15s: cpu[%d] [priv] freeIdx %-4d schedIdx %-4d waiting for processing %-4d \r\n\t\t\t [global] csd %d\n",
								"NIC Rx hiPri",  i,
								state->free_idx, state->sched_idx, (state->free_idx+smpnicrx->priv_work_cnt-state->sched_idx)&(smpnicrx->priv_work_cnt-1)
								, atomic_read(&smpnicrx->csd_available));
						}
						else
							printk("%-15s: cpu[%d] n.a.\n", "NIC Rx hiPri", i);
					}
					
				}

				for_each_possible_cpu(i) {
					if((i < num_online_cpus()) && (i < CONFIG_NR_CPUS)){
						if(fc_mgr_db.nicrx_ipi[i])
						{
							rtk_fc_nicrx_ipi_ctrl_t *smpnicrx = fc_mgr_db.nicrx_ipi[i];

							state->free_idx = atomic_read(&smpnicrx->priv_free_idx);
							state->sched_idx = atomic_read(&smpnicrx->priv_sched_idx);

							printk("%-15s: cpu[%d] [priv] freeIdx %-4d schedIdx %-4d waiting for processing %-4d \r\n\t\t\t [global] csd %d\n",
								"NIC Rx norPri",  i,
								state->free_idx, state->sched_idx, (state->free_idx+smpnicrx->priv_work_cnt-state->sched_idx)&(smpnicrx->priv_work_cnt-1)
								, atomic_read(&smpnicrx->csd_available));
						}
						else
							printk("%-15s: cpu[%d] n.a.\n", "NIC Rx norPri", i);
					}
				}
			}
			break;
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_NIC_TX)
		case RTK_FC_MGR_DISPATCH_NIC_TX:
			for_each_possible_cpu(i) {
				if((i < num_online_cpus()) && (i < CONFIG_NR_CPUS)){
					if(fc_mgr_db.nictx_ipi[i])
					{
						state->free_idx = atomic_read(&fc_mgr_db.nictx_free_idx[i]);
						state->sched_idx = atomic_read(&fc_mgr_db.nictx_sched_idx[i]);

						FCMGR_PRK("nic tx ipi csd available %d", atomic_read(&fc_mgr_db.nictx_ipi[i]->csd_available));

						printk("%-15s: cpu[%d] [priv] freeIdx %-4d schedIdx %-4d waiting for processing %-4d\r\n\t\t\t       [global] csd %d\n",
							"NIC Tx", i, state->free_idx, state->sched_idx, (state->free_idx+MAX_FC_NIC_TX_QUEUE_SIZE-state->sched_idx)&(MAX_FC_NIC_TX_QUEUE_SIZE-1)
							, atomic_read(&fc_mgr_db.nictx_ipi[i]->csd_available));
					}
					else
						printk("%-15s: cpu[%d] n.a.\n", "NIC Tx", i);
				}
			}
			break;
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_RX)
		case RTK_FC_MGR_DISPATCH_WLAN0_RX:
		case RTK_FC_MGR_DISPATCH_WLAN1_RX:
		case RTK_FC_MGR_DISPATCH_WLAN2_RX:
#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
		case RTK_FC_MGR_DISPATCH_WLANMLO_RX:
		case RTK_FC_MGR_DISPATCH_WLANMLO_0_RX:
#endif
			for_each_possible_cpu(i) {
				if((i < num_online_cpus()) && (i < CONFIG_NR_CPUS)){
					if(fc_mgr_db.wlanrx_ipi[i])
					{
						state->free_idx = atomic_read(&fc_mgr_db.wlanrx_free_idx[i]);
						state->sched_idx = atomic_read(&fc_mgr_db.wlanrx_sched_idx[i]);

						FCMGR_PRK("%-15s: cpu[%d] rx ipi csd available %d", "wifi rx", i, atomic_read(&fc_mgr_db.wlanrx_ipi[i]->csd_available));

						printk("%-15s: cpu[%d] [priv] freeIdx %-4d schedIdx %-4d waiting for processing %-4d\r\n\t\t\t       [global] csd %d max queued %d/%d\n",
							"Wifi Rx", i, state->free_idx, state->sched_idx, (state->free_idx+fc_mgr_db.max_fc_wlan_rx_queue_size-state->sched_idx)&(fc_mgr_db.max_fc_wlan_rx_queue_size-1)
							, atomic_read(&fc_mgr_db.wlanrx_ipi[i]->csd_available), fc_mgr_db.wlanrx_max_queued[i], fc_mgr_db.max_fc_wlan_rx_queue_size);
					}
					else
						printk("%-15s: cpu[%d] n.a.\n", "Wifi Rx", i);
				}
			}
			break;
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_TX)
			case RTK_FC_MGR_DISPATCH_WLAN0_TX:
			case RTK_FC_MGR_DISPATCH_WLAN1_TX:
			case RTK_FC_MGR_DISPATCH_WLAN2_TX:
#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
			case RTK_FC_MGR_DISPATCH_WLANMLO_TX:
			case RTK_FC_MGR_DISPATCH_WLANMLO_0_TX:
#endif
			for_each_possible_cpu(i) {
				if((i < num_online_cpus()) && (i < CONFIG_NR_CPUS)){
					if(fc_mgr_db.wlantx_ipi[i])
					{
						state->free_idx = atomic_read(&fc_mgr_db.wlantx_free_idx[i]);
						state->sched_idx = atomic_read(&fc_mgr_db.wlantx_sched_idx[i]);

						FCMGR_PRK("%-15s: cpu[%d] tx ipi csd available %d", "wifi tx", i, atomic_read(&fc_mgr_db.wlantx_ipi[i]->csd_available));

						printk("%-15s: cpu[%d] [priv] freeIdx %-4d schedIdx %-4d waiting for processing %-4d\r\n\t\t\t       [global] csd %d\n",
						"Wifi Tx", i, state->free_idx, state->sched_idx, (state->free_idx+MAX_FC_WLAN_TX_QUEUE_SIZE-state->sched_idx)&(MAX_FC_WLAN_TX_QUEUE_SIZE-1)
						, atomic_read(&fc_mgr_db.wlantx_ipi[i]->csd_available));
					}
					else
						printk("%-15s: cpu[%d] n.a.\n", "Wifi Tx", i);
				}
			}
			break;
#endif
		case RTK_FC_MGR_DISPATCH_PS_NETIF_RX:
			for_each_possible_cpu(i) {
				if((i < num_online_cpus()) && (i < CONFIG_NR_CPUS)){
					if(fc_mgr_db.ps_netif_rx_ipi[i])
					{
						state->free_idx = atomic_read(&fc_mgr_db.ps_netif_rx_free_idx[i]);
						state->sched_idx = atomic_read(&fc_mgr_db.ps_netif_rx_sched_idx[i]);

						FCMGR_PRK("%-15s: cpu[%d] rx ipi csd available %d", "ps_netif_rx", i, atomic_read(&fc_mgr_db.ps_netif_rx_ipi[i]->csd_available));

						printk("%-15s: cpu[%d] [priv] freeIdx %-4d schedIdx %-4d waiting for processing %-4d\r\n\t\t\t       [global] csd %d\n",
						"NETIF_RX", i, state->free_idx, state->sched_idx, (state->free_idx+MAX_FC_PS_NETIF_RX_QUEUE_SIZE-state->sched_idx)&(MAX_FC_PS_NETIF_RX_QUEUE_SIZE-1)
						, atomic_read(&fc_mgr_db.ps_netif_rx_ipi[i]->csd_available));
					}
					else
						printk("%-15s: cpu[%d] n.a.\n", "NETIF_RX", i);
				}
			}
			break;

		default:
			// not support
			break;
	}

	return SUCCESS;
}

#endif

int rtk_fc_mgr_smp_statistic_get(rtk_fc_mgr_smp_static_t *smp_statics, rtk_fc_mgr_smp_static_type_t type)
{
	memcpy(smp_statics, &fc_mgr_db.mgr_smp_statistic[type], sizeof(rtk_fc_mgr_smp_static_t));
	return SUCCESS;
}

int rtk_fc_mgr_smp_statistic_set(bool flag)
{
	uint32 i, j;

	fc_mgr_db.smpStatistic = flag;
	for(j = 0 ; j < NR_CPUS ; j++) {
#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_RX)
		fc_mgr_db.wlanrx_max_queued[j] = 0;
#endif
		for(i = 0 ; i < FC_MGR_SMP_STATIC_TYPE_MAX ; i++)
			atomic_set(&fc_mgr_db.mgr_smp_statistic[i].smp_static[j], 0);
	}
	return SUCCESS;
}

int rtk_fc_mgr_memInfo_dump()
{
#if defined(CONFIG_SMP) && (defined(CONFIG_RTK_L34_FC_IPI_NIC_RX) || defined(CONFIG_RTK_L34_FC_IPI_NIC_TX) || defined(CONFIG_RTK_L34_FC_IPI_WIFI_RX) || defined(CONFIG_RTK_L34_FC_IPI_WIFI_TX))
	int i;
	char temp[50];
#endif
	printk("%-50s%12d (%6d KB)\n","1. fc_mgr_db:", (int)sizeof(fc_mgr_db), (int)sizeof(fc_mgr_db)/1024);

	printk("     +++++++++++++++ %-20s +++++++++++++++\n", "system related");
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     wlan_port_to_devidx:", (int)sizeof(fc_mgr_db.wlan_port_to_devidx), (int)sizeof(fc_mgr_db.wlan_port_to_devidx[0]), MAX_GMAC_NUM);
#endif
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     wlanDevMap:", (int)sizeof(fc_mgr_db.wlanDevMap), (int)sizeof(fc_mgr_db.wlanDevMap[0]), RTK_FC_WLANX_END_INTF);
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     wlanIfidxDevHead:", (int)sizeof(fc_mgr_db.wlanIfidxDevHead), (int)sizeof(fc_mgr_db.wlanIfidxDevHead[0]), RTK_FC_WLANX_END_INTF);
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     wlanPortDevHead:", (int)sizeof(fc_mgr_db.wlanPortDevHead), (int)sizeof(fc_mgr_db.wlanPortDevHead[0]), RTK_FC_WLAN_PORT_BUCKET_SIZE);
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     mgr_smp_statistic:", (int)sizeof(fc_mgr_db.mgr_smp_statistic), (int)sizeof(fc_mgr_db.mgr_smp_statistic[0]), FC_MGR_SMP_STATIC_TYPE_MAX);
#if defined(CONFIG_RTK_FC_PER_SW_FLOW_MIB)
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     sw_flow_mib:", (int)sizeof(fc_mgr_db.sw_flow_mib), (int)sizeof(fc_mgr_db.sw_flow_mib[0]), RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE);
#endif
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     mgr_skbmark:", (int)sizeof(fc_mgr_db.mgr_skbmark), (int)sizeof(fc_mgr_db.mgr_skbmark[0]), FC_MGR_SKBMARK_END);
#if defined(CONFIG_RTK_SOC_RTL8198D)
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     extFlowMibTbl:", (int)sizeof(fc_mgr_db.extFlowMibTbl), (int)sizeof(fc_mgr_db.extFlowMibTbl[0]), RT_STAT_EXT_FLOWMIB_TABLE_SIZE);
#endif

	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     flow_lock:", (int)sizeof(fc_mgr_db.flow_lock), (int)sizeof(fc_mgr_db.flow_lock[0]), RTK_FC_FLOW_LOCK_CNT);
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     dbgprint_buf:", (int)sizeof(fc_mgr_db.dbgprint_buf), (int)sizeof(fc_mgr_db.dbgprint_buf[0]), 256);

#if defined(CONFIG_SMP)
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     smp_dispatch:", (int)sizeof(fc_mgr_db.smp_dispatch), (int)sizeof(fc_mgr_db.smp_dispatch[0]), RTK_FC_MGR_DISPATCH_ARRAY_SIZE);
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     fc_rps_maps:", (int)sizeof(fc_mgr_db.fc_rps_maps), (int)sizeof(fc_mgr_db.fc_rps_maps), 1);
	printk("%-50s%12d (%6d btyes * %6d entrys)\n","     wifi_tx_smp_mode_cpu_sel:", (int)sizeof(fc_mgr_db.wifi_tx_smp_mode_cpu_sel), (int)sizeof(fc_mgr_db.wifi_tx_smp_mode_cpu_sel[0]), RTK_FC_WLAN_BAND_NUM);

#if defined(CONFIG_RTK_L34_FC_IPI_NIC_RX)
	printk("     +++++++++++++++ %-20s +++++++++++++++ (MAX_FC_NIC_RX_QUEUE_SIZE: %d, MAX_FC_NIC_RX_HIQUEUE_SIZE: %d)\n", "nic rx ipi related", MAX_FC_NIC_RX_QUEUE_SIZE, MAX_FC_NIC_RX_HIQUEUE_SIZE);
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     nicrx_ring   (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.nicrx_ring), (int)sizeof(fc_mgr_db.nicrx_ring[0]), 1, NR_CPUS);
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nicrx_ring[i])
		{
			sprintf(temp, "          nicrx_ring (kmalloc for cpu%d)", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_nicrx_ring_ctrl_t), (int)sizeof(rtk_fc_nicrx_ring_ctrl_t), 1);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n","          - nicrx_ring.priv_work:", (int)sizeof(rtk_fc_smp_nicRx_work_info_t) * MAX_FC_NIC_RX_QUEUE_SIZE, (int)sizeof(rtk_fc_smp_nicRx_work_info_t), MAX_FC_NIC_RX_QUEUE_SIZE);
		}
	}
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     nicrx_ipi    (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.nicrx_ipi), (int)sizeof(fc_mgr_db.nicrx_ipi[0]), 1, num_online_cpus());
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nicrx_ipi[i])
		{
			sprintf(temp, "          nicrx_ipi (kmalloc for cpu%d):", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_nicrx_ipi_ctrl_t), (int)sizeof(rtk_fc_nicrx_ipi_ctrl_t), 1);
		}
	}
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     nicrx_hiring (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.nicrx_hiring), (int)sizeof(fc_mgr_db.nicrx_hiring[0]), 1, NR_CPUS);
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nicrx_hiring[i])
		{
			sprintf(temp, "          nicrx_hiring (kmalloc for cpu%d):", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_nicrx_hiring_ctrl_t), (int)sizeof(rtk_fc_nicrx_hiring_ctrl_t), 1);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n","          - nicrx_hiring.priv_work:", (int)sizeof(rtk_fc_smp_nicRx_work_info_t) * MAX_FC_NIC_RX_HIQUEUE_SIZE, (int)sizeof(rtk_fc_smp_nicRx_work_info_t), MAX_FC_NIC_RX_HIQUEUE_SIZE);
		}
	}
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     nicrx_hi_ipi   (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.nicrx_hi_ipi), (int)sizeof(fc_mgr_db.nicrx_hi_ipi[0]), 1, NR_CPUS);
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nicrx_hi_ipi[i])
		{
			sprintf(temp, "     nicrx_hi_ipi:  (kmalloc for cpu%d)", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_nicrx_ipi_ctrl_t), (int)sizeof(rtk_fc_nicrx_ipi_ctrl_t), 1);
		}
	}
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_NIC_TX)
	printk("     +++++++++++++++ %-20s +++++++++++++++ (MAX_FC_NIC_TX_QUEUE_SIZE: %d)\n", "nic tx ipi related", MAX_FC_NIC_TX_QUEUE_SIZE);
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     nictx_ipi (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.nictx_ipi), (int)sizeof(fc_mgr_db.nictx_ipi[0]), 1, num_online_cpus());
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nictx_ipi[i])
		{
			sprintf(temp, "          nictx_ipi (kmalloc for cpu%d):", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_smp_ipi_ctrl_t), (int)sizeof(rtk_fc_smp_ipi_ctrl_t), 1);
		}
	}
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     nictx_free_idx:", (int)sizeof(fc_mgr_db.nictx_free_idx), (int)sizeof(fc_mgr_db.nictx_free_idx[0]), 1, num_online_cpus());
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     nictx_sched_idx:", (int)sizeof(fc_mgr_db.nictx_sched_idx), (int)sizeof(fc_mgr_db.nictx_sched_idx[0]), 1, num_online_cpus());
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     nictx_work (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.nictx_work), (int)sizeof(fc_mgr_db.nictx_work[0]), 1, num_online_cpus());
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.nictx_work[i])
		{
			sprintf(temp, "          nictx_work (kmalloc for cpu%d):", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_smp_nicTx_work_t) * MAX_FC_NIC_TX_QUEUE_SIZE, (int)sizeof(rtk_fc_smp_nicTx_work_t), MAX_FC_NIC_TX_QUEUE_SIZE);
		}
	}
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_RX)
	printk("     +++++++++++++++ %-20s +++++++++++++++ (MAX_FC_WLAN_RX_QUEUE_SIZE: %d)\n", "wifi rx ipi related", MAX_FC_WLAN_RX_QUEUE_SIZE);
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     wlanrx_ipi (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.wlanrx_ipi), (int)sizeof(fc_mgr_db.wlanrx_ipi[0]), 1, num_online_cpus());
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.wlanrx_ipi[i])
		{
			sprintf(temp, "          wlanrx_ipi (kmalloc for cpu%d):", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n",temp , (int)sizeof(rtk_fc_smp_ipi_ctrl_t), (int)sizeof(rtk_fc_smp_ipi_ctrl_t), 1);
		}
	}
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     wlanrx_free_idx:", (int)sizeof(fc_mgr_db.wlanrx_free_idx), (int)sizeof(fc_mgr_db.wlanrx_free_idx[0]), 1, num_online_cpus());
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     wlanrx_sched_idx:", (int)sizeof(fc_mgr_db.wlanrx_sched_idx), (int)sizeof(fc_mgr_db.wlanrx_sched_idx[0]), 1, num_online_cpus());
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     wlanrx_work (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.wlanrx_work), (int)sizeof(fc_mgr_db.wlanrx_work[0]), 1, num_online_cpus());
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.wlanrx_work[i])
		{
			sprintf(temp, "          wlanrx_work (kmalloc for cpu%d):", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_smp_wlanRx_work_t) * MAX_FC_WLAN_RX_QUEUE_SIZE, (int)sizeof(rtk_fc_smp_wlanRx_work_t), MAX_FC_WLAN_RX_QUEUE_SIZE);
		}
	}
#endif
#if defined(CONFIG_RTK_L34_FC_IPI_WIFI_TX)
	printk("     +++++++++++++++ %-20s +++++++++++++++ (MAX_FC_WLAN_TX_QUEUE_SIZE: %d)\n", "wifi tx ipi related", MAX_FC_WLAN_TX_QUEUE_SIZE);
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     wlantx_ipi (prt):", (int)sizeof(fc_mgr_db.wlantx_ipi), (int)sizeof(fc_mgr_db.wlantx_ipi[0]), 1, num_online_cpus());
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.wlantx_ipi[i])
		{
			sprintf(temp, "          wlantx_ipi: (kmalloc for cpu%d)", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_smp_ipi_ctrl_t), (int)sizeof(rtk_fc_smp_ipi_ctrl_t), 1);
		}
	}
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     wlantx_free_idx:", (int)sizeof(fc_mgr_db.wlantx_free_idx), (int)sizeof(fc_mgr_db.wlantx_free_idx[0]), 1, num_online_cpus());
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     wlantx_sched_idx:", (int)sizeof(fc_mgr_db.wlantx_sched_idx), (int)sizeof(fc_mgr_db.wlantx_sched_idx[0]), 1, num_online_cpus());
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     wlantx_work (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.wlantx_work), (int)sizeof(fc_mgr_db.wlantx_work[0]), 1, num_online_cpus());
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.wlantx_work[i])
		{
			sprintf(temp, "          wlantx_work (kmalloc for cpu%d):", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_smp_wlanTx_work_t) * MAX_FC_WLAN_TX_QUEUE_SIZE, (int)sizeof(rtk_fc_smp_wlanTx_work_t), MAX_FC_WLAN_TX_QUEUE_SIZE);
		}
	}
#endif
	printk("	 +++++++++++++++ %-20s +++++++++++++++ (MAX_FC_PS_NETIF_RX_QUEUE_SIZE: %d)\n", "wifi tx ipi related", MAX_FC_PS_NETIF_RX_QUEUE_SIZE);
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n"," 	wlantx_ipi (prt):", (int)sizeof(fc_mgr_db.ps_netif_rx_ipi), (int)sizeof(fc_mgr_db.ps_netif_rx_ipi[0]), 1, num_online_cpus());
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.ps_netif_rx_work[i])
		{
			sprintf(temp, " 		 ps_netif_rx_ipi: (kmalloc for cpu%d)", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_smp_ipi_ctrl_t), (int)sizeof(rtk_fc_smp_ipi_ctrl_t), 1);
		}
	}
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n"," 	ps_netif_rx_free_idx:", (int)sizeof(fc_mgr_db.ps_netif_rx_free_idx), (int)sizeof(fc_mgr_db.ps_netif_rx_free_idx[0]), 1, num_online_cpus());
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n"," 	ps_netif_rx_sched_idx:", (int)sizeof(fc_mgr_db.ps_netif_rx_sched_idx), (int)sizeof(fc_mgr_db.ps_netif_rx_sched_idx[0]), 1, num_online_cpus());
	printk("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n"," 	ps_netif_rx_work (ptr in fc_mgr_db):", (int)sizeof(fc_mgr_db.ps_netif_rx_work), (int)sizeof(fc_mgr_db.ps_netif_rx_work[0]), 1, num_online_cpus());
	for(i  = 0; i  < num_online_cpus(); i++)
	{
		if(fc_mgr_db.ps_netif_rx_work[i])
		{
			sprintf(temp, " 		 ps_netif_rx_work (kmalloc for cpu%d):", i);
			printk("%-50s%12d (%6d btyes * %6d entrys)\n", temp, (int)sizeof(rtk_fc_smp_ps_netif_rx_work_t) * MAX_FC_PS_NETIF_RX_QUEUE_SIZE, (int)sizeof(rtk_fc_smp_ps_netif_rx_work_t), MAX_FC_PS_NETIF_RX_QUEUE_SIZE);
		}
	}

#endif
	return SUCCESS;
}

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
int rtk_fc_mgr_wifi_flow_ctrl_info_get(rtk_fc_wifi_flow_crtl_info_t *wifi_flow_crtl_info)
{
	memcpy(wifi_flow_crtl_info, &fc_mgr_db.wifi_flow_crtl_info, sizeof(rtk_fc_wifi_flow_crtl_info_t));
	return SUCCESS;
}

int rtk_fc_mgr_wifi_flow_ctrl_info_set(bool if_en)
{
	fc_mgr_db.wifi_flow_crtl_info.wlan0_accumulate_bit = 0;
	fc_mgr_db.wifi_flow_crtl_info.wlan1_accumulate_bit = 0;
	fc_mgr_db.wifi_flow_crtl_info.wifi_flow_ctrl_auto_en = if_en;
	return SUCCESS;
}

int rtk_fc_mgr_wifi_flow_ctrl_info_clear(void)
{
	fc_mgr_db.wifi_flow_crtl_info.wlan0_accumulate_bit = 0;
	fc_mgr_db.wifi_flow_crtl_info.wlan1_accumulate_bit = 0;
	return SUCCESS;
}

#endif

int rtk_fc_mgr_epon_llid_format_set(int formatConf)
{
	atomic_set(&fc_mgr_db.epon_llid_format, formatConf);
	return SUCCESS;
}

int rtk_fc_mgr_hwnat_mode_set(rt_flow_hwnat_mode_t hwnat_mode)
{
	// notify wifi driver to turn off/on "rx_deamsdu_by_nic"
	if(fc_mgr_db._rtk_fc_wifi_event_handling_cb_register) {
		rtk_fc_wifi_event_t event = {0};
		if(((1<<hwnat_mode) & HWNAT_MODE_SKIP_FC_FUNC_BITMASK)){
			event.code = RTK_FC_WIFI_EVENT_DISABLE_ACC;
		}else{
			event.code = RTK_FC_WIFI_EVENT_ENABLE_ACC;
		}
		fc_mgr_db._rtk_fc_wifi_event_handling_cb_register(&event);
	}
	
	fc_mgr_db.hwnat_mode = hwnat_mode;
	return SUCCESS;
}

int rtk_fc_mgr_wan_port_mask_set(rtk_fc_port_mask_t portmask)
{
	fc_mgr_db.wanPortMask.portmask = portmask;
	return SUCCESS;
}

int rtk_fc_mgr_max_fc_wlan_rx_queue_size_get(uint32 *wlan_rx_queue_size, uint32 *wlan_rx_queue_size_offset)
{
	*wlan_rx_queue_size = fc_mgr_db.max_fc_wlan_rx_queue_size;
	*wlan_rx_queue_size_offset = fc_mgr_db.max_fc_wlan_rx_queue_size_offset;
	return SUCCESS;
}

int rtk_fc_mgr_max_fc_wlan_rx_queue_size_set(uint32 wlan_rx_queue_size, uint32 wlan_rx_queue_size_offset)
{
	if(wlan_rx_queue_size)
		fc_mgr_db.max_fc_wlan_rx_queue_size = wlan_rx_queue_size;
	if(wlan_rx_queue_size_offset)
		fc_mgr_db.max_fc_wlan_rx_queue_size_offset = wlan_rx_queue_size_offset;
	return SUCCESS;
}

int rtk_fc_mgr_br0_ip_mac_set(uint32 ip_addr, uint8 *mac_addr, uint8 flag)
{
	if (flag == RTK_FC_BR0_RECORD_ALL) {
		if (mac_addr) {
			fc_mgr_db.br0_ip = ip_addr;
			memcpy(fc_mgr_db.br0_mac, mac_addr, ETHER_ADDR_LEN);
			return SUCCESS;
		}
	}
	else if (flag == RTK_FC_BR0_RECORD_IP) {	// ip only
		fc_mgr_db.br0_ip = ip_addr;
		return SUCCESS;
	}
	else {	// mac only
		if (mac_addr) {
			memcpy(fc_mgr_db.br0_mac, mac_addr, ETHER_ADDR_LEN);
			return SUCCESS;
		}
	}

	return FAIL;
}

int rtk_fc_mgr_skbmark_conf_set(rtk_fc_mgr_skbmarkTarget_t target, rtk_fc_skbmark_t skbmark_conf)
{
	if((FC_MGR_SKBMARK_QID <= target) && (target < FC_MGR_SKBMARK_END))
		memcpy(&fc_mgr_db.mgr_skbmark[target], &skbmark_conf, sizeof(fc_mgr_db.mgr_skbmark[target]));
	return SUCCESS;
}


int rtk_fc_mgr_sw_flow_mib_add(int flowTblIdx, rt_flow_op_sw_flow_mib_info_t counts)
{
#if defined(CONFIG_RTK_FC_PER_SW_FLOW_MIB)
	if(0 <= flowTblIdx && flowTblIdx < RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE)
	{
		fc_mgr_db.sw_flow_mib[flowTblIdx].ingress_packet_count += counts.ingress_packet_count;
		fc_mgr_db.sw_flow_mib[flowTblIdx].ingress_byte_count += counts.ingress_byte_count;
		return SUCCESS;
	}
		
#endif
	return FAIL; //invalid parameter or not support
}

int rtk_fc_mgr_sw_flow_mib_get(int flowTblIdx, rt_flow_op_sw_flow_mib_info_t *counts)
{
#if defined(CONFIG_RTK_FC_PER_SW_FLOW_MIB)
	if((0 <= flowTblIdx) && (flowTblIdx < RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE))
	{
		memcpy(counts, &fc_mgr_db.sw_flow_mib[flowTblIdx], sizeof(rt_flow_op_sw_flow_mib_info_t));
		return SUCCESS;
	}
#endif
	return FAIL; //invalid parameter or not support
}

int rtk_fc_mgr_sw_flow_mib_clear(int flowTblIdx)
{
#if defined(CONFIG_RTK_FC_PER_SW_FLOW_MIB)
	if((0 <= flowTblIdx) && (flowTblIdx < RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE))
	{
		memset(&fc_mgr_db.sw_flow_mib[flowTblIdx], 0, sizeof(fc_mgr_db.sw_flow_mib[flowTblIdx]));
		return SUCCESS;
	}		
#endif
	return FAIL; //invalid parameter or not support
}

int rtk_fc_mgr_sw_flow_mib_clearAll(void)
{
#if defined(CONFIG_RTK_FC_PER_SW_FLOW_MIB)
	memset(&fc_mgr_db.sw_flow_mib[0], 0, (sizeof(fc_mgr_db.sw_flow_mib[0]) * (RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE)));
	return SUCCESS;
#endif
	return FAIL; //invalid parameter or not support

}

int rtk_fc_mgr_proc_fops_size(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	return sizeof(struct proc_ops);
#else
	return sizeof(struct file_operations);
#endif
}

void *rtk_fc_mgr_proc_fops_kmalloc(void)
{
	void *pProc_fops = NULL;
	unsigned int allocSize = rtk_fc_mgr_proc_fops_size();
	
	pProc_fops = kmalloc(allocSize, GFP_ATOMIC);
	if (pProc_fops) {
		memset(pProc_fops, 0, allocSize);
	}

	return pProc_fops;
}

void rtk_fc_mgr_proc_fops_kfree(void *ptr)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct proc_ops *pProc_fops = (struct proc_ops *)ptr;
#else
	struct file_operations *pProc_fops = (struct file_operations *)ptr;
#endif
	kfree(pProc_fops);
	return;
}

void rtk_fc_mgr_proc_fops_open_set(void *pProc_fops, void *pFunc)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct proc_ops *ptr_proc_fops = (struct proc_ops *)pProc_fops;
	ptr_proc_fops->proc_open = pFunc;
#else
	struct file_operations *ptr_proc_fops = (struct file_operations *)pProc_fops;
	ptr_proc_fops->open = pFunc;
#endif
	return;
}

void rtk_fc_mgr_proc_fops_write_set(void *pProc_fops, void *pFunc)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct proc_ops *ptr_proc_fops = (struct proc_ops *)pProc_fops;
	ptr_proc_fops->proc_write = pFunc;
#else
	struct file_operations *ptr_proc_fops = (struct file_operations *)pProc_fops;
	ptr_proc_fops->write = pFunc;
#endif
	return;
}

void rtk_fc_mgr_proc_fops_read_set(void *pProc_fops, void *pFunc)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct proc_ops *ptr_proc_fops = (struct proc_ops *)pProc_fops;
	ptr_proc_fops->proc_read = pFunc;
#else
	struct file_operations *ptr_proc_fops = (struct file_operations *)pProc_fops;
	ptr_proc_fops->read = pFunc;
#endif
	return;
}

void rtk_fc_mgr_proc_fops_lseek_set(void *pProc_fops, void *pFunc)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct proc_ops *ptr_proc_fops = (struct proc_ops *)pProc_fops;
	ptr_proc_fops->proc_lseek = pFunc;
#else
	struct file_operations *ptr_proc_fops = (struct file_operations *)pProc_fops;
	ptr_proc_fops->llseek = pFunc;
#endif
	return;
}

void rtk_fc_mgr_proc_fops_release_set(void *pProc_fops, void *pFunc)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct proc_ops *ptr_proc_fops = (struct proc_ops *)pProc_fops;
	ptr_proc_fops->proc_release = pFunc;
#else
	struct file_operations *ptr_proc_fops = (struct file_operations *)pProc_fops;
	ptr_proc_fops->release = pFunc;
#endif
	return;
}

struct proc_dir_entry *rtk_fc_mgr_proc_create_data(const char *name, umode_t mode,
															struct proc_dir_entry *parent,
															void *pProc_fops, void *data)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct proc_ops *ptr_proc_fops = (struct proc_ops *)pProc_fops;
#else
	struct file_operations *ptr_proc_fops = (struct file_operations *)pProc_fops;
#endif
	return proc_create_data(name, mode, parent, ptr_proc_fops, data);
}

unsigned int rtk_fc_mgr_proc_inode_id_get(struct proc_dir_entry *pProc_dir)
{
	return pProc_dir->low_ino;
}

void *rtk_fc_mgr_timer_list_kmalloc(void)
{
	void *timerList = NULL;
	timerList = kmalloc(sizeof(rtk_fc_timer_list_t), GFP_ATOMIC);
	if(timerList)
		memset(timerList, 0, sizeof(rtk_fc_timer_list_t));
	
	return timerList;
}

void rtk_fc_mgr_timer_list_kfree(rtk_fc_timer_list_t *timerList)
{
	if(timerList)
		kfree(timerList);
	return;
}

int rtk_fc_mgr_skipHwlookUp_stat_set(rtk_fc_skipHwlookUp_stat_t skipHwlookUp_stat)
{
	memcpy(&fc_mgr_db.skipHwlookUp_stat, &skipHwlookUp_stat, sizeof(fc_mgr_db.skipHwlookUp_stat));
	return SUCCESS;
}

int rtk_fc_mgr_skipHwlookUp_stat_get(rtk_fc_skipHwlookUp_stat_t *skipHwlookUp_stat)
{
	memcpy(skipHwlookUp_stat, &fc_mgr_db.skipHwlookUp_stat, sizeof(rtk_fc_skipHwlookUp_stat_t));
	return SUCCESS;
}

int rtk_fc_mgr_skipHwlookUp_stat_clear(void)
{
	memset(&fc_mgr_db.skipHwlookUp_stat, 0, sizeof(fc_mgr_db.skipHwlookUp_stat));
	return SUCCESS;
}

#if defined(CONFIG_FC_RTL9607C_SERIES)
static void rtk_fc_mgr_pon_ds_p7_loopback(bool enable)
{
	
	if(enable){
		
		(void)rtk_cpu_trapInsertTagByPort_set(RTK_FC_MAC_PORT_SLAVECPU, DISABLED);
		(void)rtk_port_macLocalLoopbackEnable_set(RTK_FC_MAC_PORT_SLAVECPU, ENABLED);
		(void)rtk_l2_illegalPortMoveAction_set(RTK_FC_MAC_PORT_SLAVECPU, ACTION_FORWARD);
	}else{
	
		(void)rtk_cpu_trapInsertTagByPort_set(RTK_FC_MAC_PORT_SLAVECPU, ENABLED);
		(void)rtk_port_macLocalLoopbackEnable_set(RTK_FC_MAC_PORT_SLAVECPU, DISABLED);
		(void)rtk_l2_illegalPortMoveAction_set(RTK_FC_MAC_PORT_SLAVECPU, ACTION_FORWARD);
	}
}
#endif

int rtk_fc_mgr_glb_config_set(rtk_fc_mgr_config_type_t glbConfig, void *config)
{
	int ret = SUCCESS;
	
	switch(glbConfig){
		case FC_MGR_GLB_CONFIG_WIFI_RX_HASH:
			fc_mgr_db.wifi_rx_hash_en = (*(uint32 *)config) & 0x1;
			break;
		case FC_MGR_GLB_CONFIG_WIFI_RX_GMAC_AUTO_SEL:
			fc_mgr_db.wifi_rx_gmac_auto_sel_en = (*(uint32 *)config) & 0x1;
			break;
#if defined(CONFIG_FC_RTL9607C_SERIES)
		case FC_MGR_GLB_CONFIG_PON_DS_P7_LOOPBACK:
			fc_mgr_db.pon_ds_p7_loopback_en = (*(uint32 *)config) & 0x1;
			rtk_fc_mgr_pon_ds_p7_loopback(fc_mgr_db.pon_ds_p7_loopback_en);
			break;
#endif
		default:
			ret = FAIL;
			break;
	}

	return ret;
}

int rtk_fc_mgr_check_config_setting(rtk_fc_mgr_config_type_t type)
{
	int ret = FAIL;
	switch(type){
		case FC_MGR_GET_CONFIG_RT_API:
#if defined(CONFIG_COMMON_RT_API)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
		case FC_MGR_GET_CONFIG_FLOW_LOCK_GROUP_SIZE:
			ret = RTK_FC_FLOW_LOCK_GROUP_SIZE;
			break;
		case FC_MGR_GET_CONFIG_LOOPBACK_MODE:
#if defined(CONFIG_RG_G3_L2FE_POL_OFFLOAD)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
		case FC_MGR_GET_CONFIG_PROBE_LOG:
			ret = (MOD_PROBE_LOG) ? TRUE : FALSE;
			break;
		case FC_MGR_GET_CONFIG_PORT4_HSGMII_EN:
#if defined(CONFIG_LAN_SDS_FEATURE)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
		case FC_MGR_GET_CONFIG_SPECIAL_FAST_FORWARD:
#if defined(CONFIG_FC_SPECIAL_FAST_FORWARD)
			ret = TRUE;
#else
			ret = FALSE;
#endif			
			break;
		case FC_MGR_GET_CONFIG_HWNAT_CUSTOMIZE:
#if defined(HWNAT_CUSTOMIZE)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
	case FC_MGR_GET_CONFIG_EXTERNAL_SWITCH_ENABLE:
#if defined(CONFIG_EXTERNAL_SWITCH)
			ret = RTK_FC_EXTERNAL_PORT_SWITCH;
#elif defined(CONFIG_EXTERNAL_VIRTUAL_PORT)
			ret = RTK_FC_EXTERNAL_PORT_VIRTUALPORT;
#else
			ret = RTK_FC_EXTERNAL_PORT_DISABLE;
#endif
			break;
	case FC_MGR_GET_CONFIG_EXTERNAL_SWITCH_PORT_OFFSET:
#if defined(CONFIG_EXTERNAL_SWITCH_PORT_OFFSET)
			ret = CONFIG_EXTERNAL_SWITCH_PORT_OFFSET;
#elif defined(CONFIG_EXTERNAL_VIRTUAL_PORT_OFFSET)
			ret = CONFIG_EXTERNAL_VIRTUAL_PORT_OFFSET;
#else
			ret = FALSE;
#endif
			break;
	case FC_MGR_GET_CONFIG_HWNAT_CUSTOMIZE_NPTV6_SRAM_ACC_V2:
#if defined(HWNAT_CUSTOMIZE_NPTV6_SRAM_ACC_V2)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
	case FC_MGR_GET_CONFIG_GMAC2_USABLE:
#if defined(CONFIG_GMAC2_USABLE)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
	case FC_MGR_GET_CONFIG_PER_SW_FLOW_MIB:
#if defined(CONFIG_RTK_FC_PER_SW_FLOW_MIB)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
	case FC_MGR_GET_CONFIG_MEMLEAK_DEBUG:
#if defined(CONFIG_RTK_FC_MEMLEAK_DEBUG)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
	case FC_MGR_GLB_CONFIG_WIFI_RX_HASH:
			ret = fc_mgr_db.wifi_rx_hash_en;
			break;
	case FC_MGR_GLB_CONFIG_WIFI_RX_GMAC_AUTO_SEL:
			ret = fc_mgr_db.wifi_rx_gmac_auto_sel_en;
			break;
	case FC_MGR_GLB_CONFIG_WIFI_TX_TRAP_HASH:
#if defined(CONFIG_FC_WIFI_TRAP_HASH_SUPPORT)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
	case FC_MGR_GLB_CONFIG_WIFI_TX_GMAC_AUTO_SEL:
#if defined(CONFIG_FC_WIFI_TX_GMAC_AUTO_SEL_SUPPORT)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
	case FC_MGR_GLB_CONFIG_WIFI_GMAC_TRUNKING_NUM:
			ret = GMAC_TRUNKING_NUM;
			break;
	case FC_MGR_GET_CONFIG_VLAN_FILTERING:
#if defined(CONFIG_BRIDGE_VLAN_FILTERING)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
	case FC_MGR_GET_CONFIG_IPSEC_FASTFWD:
#if defined(CONFIG_RTK_FC_IPSEC_FASTFWD)
			ret = TRUE;
#else
			ret = FALSE;
#endif
			break;
#if defined(CONFIG_FC_RTL9607C_SERIES)
	case FC_MGR_GLB_CONFIG_PON_DS_P7_LOOPBACK:
			ret = fc_mgr_db.pon_ds_p7_loopback_en;
			break;
#endif
	case FC_MGR_GLB_CONFIG_WIFI_MULTIBAND_ACC:
			ret = fc_mgr_db.wifi_multiband_acc;
			break;
	case FC_MGR_GLB_CONFIG_SW_CHECK_HWFLOW:
			ret = fc_mgr_db.sw_check_hwflow;
			break;
	case FC_MGR_GLB_CONFIG_ETHPORT_TRUNKING:
			ret = fc_mgr_db.ethport_trunking;
			break;
		default:
			break;
	}
	return ret;
}



int rtk_fc_proc_file_seq_file_setting(struct file *file, struct seq_file *p, const struct seq_operations *op)
{
#if 1
	(void)seq_open(file, op);
#else
	p = file->private_data;

	if (!p) {
			p = kmalloc(sizeof(*p), GFP_ATOMIC);
			if (!p){
				return -ENOMEM;
			}
			file->private_data = p;
	}
	memset(p, 0, sizeof(*p));
	mutex_init(&p->lock);
	p->op = op;
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 4, 79)
	p->file = file;
#else
#ifdef CONFIG_USER_NS
	p->user_ns = file->f_cred->user_ns;
#endif
#endif

	/*
	 * Wrappers around seq_open(e.g. swaps_open) need to be
	 * aware of this. If they set f_version themselves, they
	 * should call seq_open first and then set f_version.
	 */
	file->f_version = 0;

	/*
	 * seq_files support lseek() and pread().  They do not implement
	 * write() at all, but we clear FMODE_PWRITE here for historical
	 * reasons.
	 *
	 * If a client of seq_files a) implements file.write() and b) wishes to
	 * support pwrite() then that client will need to implement its own
	 * file.open() which calls seq_open() and then sets FMODE_PWRITE.
	 */
	file->f_mode &= ~FMODE_PWRITE;
#endif
	return SUCCESS;
}

int rtk_fc_proc_file_set_private_data(struct file *file, void *data)
{

	((struct seq_file *)file->private_data)->private = data;
	return SUCCESS;
}

int rtk_fc_proc_file_set_private_data_buf(struct file *file, char *buf, size_t size)
{
	((struct seq_file *)file->private_data)->buf = buf;
	((struct seq_file *)file->private_data)->size = size;
	return SUCCESS;
}

unsigned int rtk_fc_proc_file_get_inode_id(struct seq_file *s)
{
	return (unsigned int)s->file->f_inode->i_ino;
}


struct proc_dir_entry* rtk_fc_proc_mkdir(const char *name, struct proc_dir_entry *parent)
{
       return proc_mkdir(name, parent);
}


static void *rtk_fc_single_start(struct seq_file *p, loff_t *pos)
{
       return NULL + (*pos == 0);
}

static void *rtk_fc_single_next(struct seq_file *p, void *v, loff_t *pos)
{
       ++*pos;
       return NULL;
}

static void rtk_fc_single_stop(struct seq_file *p, void *v)
{
}
static void *rtk_fc_seq_buf_alloc(unsigned long size)
{
	void *buf;
	gfp_t gfp = GFP_ATOMIC;

	/*
	 * For high order allocations, use __GFP_NORETRY to avoid oom-killing -
	 * it's better to fall back to vmalloc() than to kill things.  For small
	 * allocations, just use GFP_KERNEL which will oom kill, thus no need
	 * for vmalloc fallback.
	 */
	if (size > PAGE_SIZE)
		gfp |= __GFP_NORETRY | __GFP_NOWARN;
	buf = kmalloc(size, gfp);
	if (!buf && size > PAGE_SIZE)
		buf = vmalloc(size);
	return buf;
}

int rtk_fc_seq_open(struct file *file, const struct seq_operations *op)
{
       struct seq_file *p = NULL;

       rtk_fc_proc_file_seq_file_setting(file, p, op);

       return 0;
}

int rtk_fc_proc_single_open(struct file *file, int (*show)(struct seq_file *, void *),void *data)
{
       struct seq_operations *op = kmalloc(sizeof(*op), GFP_ATOMIC);
       int res = -ENOMEM;

       if (op) {
               op->start = rtk_fc_single_start;
               op->next = rtk_fc_single_next;
               op->stop = rtk_fc_single_stop;
               op->show = show;
               res = rtk_fc_seq_open(file, op);
               if (!res)
                       rtk_fc_proc_file_set_private_data(file, data);
               else
                       kfree(op);
       }
       return res;
}
int rtk_fc_proc_single_open_size(struct file *file, int (*show)(struct seq_file *, void *),		void *data, size_t size)
{
	char *buf = rtk_fc_seq_buf_alloc(size);
	int ret;
	if (!buf)
		return -ENOMEM;
	ret = rtk_fc_proc_single_open(file, show, data);
	if (ret) {
		kvfree(buf);
		return ret;
	}
	((struct seq_file *)file->private_data)->buf = buf;
	((struct seq_file *)file->private_data)->size = size;
	return 0;
}


int rtk_fc_netlink_get_nlhdr_fromSKBData(struct sk_buff *skb, struct nlmsghdr **nlh)
{
	*nlh = (struct nlmsghdr *) skb->data;
	return SUCCESS;
}
int rtk_fc_netlink_get_net_from_sock(struct sock *sk, struct net **net)
{
	*net = sock_net(sk);
	return SUCCESS;
}

int rtk_fc_netlink_set_sock_data(struct sock *sk, void	(*sk_data_ready)(struct sock *sk), gfp_t sk_allocation)
{
	sk->sk_data_ready = sk_data_ready;
	sk->sk_allocation = sk_allocation;
	return SUCCESS;
}

int rtk_fc_msecs_to_jiffies(int msecs, unsigned long int *jiffies)
{
	*jiffies = msecs_to_jiffies(msecs);
	return SUCCESS;
}

int rtk_fc_jiffies_to_secs(unsigned long int jiffies, int *secs)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
	struct timespec64 ts;
	jiffies_to_timespec64(jiffies, &ts);

	*secs = ts.tv_sec;
#else
	struct timeval tv;
	jiffies_to_timeval(jiffies, &tv);

	*secs = tv.tv_sec;
#endif
	return SUCCESS;
}

int rtk_fc_skb_set_pkt_type_offset_sw_hash(struct sk_buff *skb)
{
	skb->sw_hash=1;
	return SUCCESS;
}


int rtk_fc_skb_set_pkt_type_offset_l4_hash(struct sk_buff *skb)
{
	skb->l4_hash=1;
	return SUCCESS;
}


int rtk_fc_skb_set_pkt_priority(struct sk_buff *skb,uint32 priority)
{

	skb->priority = priority;
	
	return SUCCESS;
}

