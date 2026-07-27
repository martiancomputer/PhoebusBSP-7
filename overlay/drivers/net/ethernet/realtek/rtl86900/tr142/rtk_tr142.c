#define __LINUX_KERNEL__

#include <linux/string.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <if_smux.h>

#include <rtk/init.h>
#include <hal/common/halctrl.h>

#ifdef CONFIG_LUNA_G3_SERIES
#include <DRV/platform/include/ca8279/omci_pf_ca8279.h>
#endif

#ifdef CONFIG_APOLLO_ROMEDRIVER
#include <rtk_rg_struct.h>
#include <rtk_rg_liteRomeDriver.h>
#endif

#include <module/gpon/gpon.h>
#include <module/gpon/gpon_defs.h>
#ifdef CONFIG_RTL8686_SWITCH
#include <module/intr_bcaster/intr_bcaster.h>
#endif

#include <omci_dm_cb.h>
#include <rtk/rtk_tr142.h>

#if defined(CONFIG_RTL_MULTI_ETH_WAN)
#include <linux/timer.h>
#endif

#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/net_namespace.h>

#if defined(CONFIG_CMCC) || defined(CONFIG_CU_BASEON_CMCC)|| defined(CONFIG_QOS_SUPPORT_8_QUEUES)
#define ACL_QOS_INTERNAL_PRIORITY_START 8
#else
#define ACL_QOS_INTERNAL_PRIORITY_START 4
#endif


rtk_tr142_control_t g_rtk_tr142_ctrl;

#ifdef CONFIG_LUNA_G3_SERIES
ca8279_gpon_schedule_info_t scheInfo;
#else
rtk_gpon_schedule_info_t scheInfo;
#endif
#define WAN_PONMAC_QUEUE_ID_MAX (scheInfo.max_pon_queue)

omci_dmm_cb_t omci_cb;		//call back function for OMCI

#if defined(CONFIG_LUNA_G3_SERIES)
#define MAX_GPON_RATE 4294967295	//10G
#elif defined(CONFIG_APOLLO)
#define MAX_GPON_RATE 131071		//1G
#else
#define MAX_GPON_RATE 262143		//2.5G
#endif

static int major;
static char *rtk_tr142_dev_name = "rtk_tr142";
static dev_t rtk_tr142_dev = 0;
static struct cdev rtk_tr142_cdev;
static struct class *rtk_tr142_class = NULL;

static rtk_tr142_qos_queues_t queues;
#ifdef CONFIG_APOLLO_ROMEDRIVER
static unsigned char queue_num = 0;
#else
static unsigned char queue_num = 4;
#endif
static int is_queues_ready = 0;
static int is_hwnat_disabled = 0;

#if defined(CONFIG_RTL_MULTI_ETH_WAN)
#define MAX_WAN_NUMBER 16 
static int tr142_omci_recover_wan_timeout = 5;
static struct timer_list tr142_timer[MAX_WAN_NUMBER];
#endif
static unsigned char is_dot1p_value_byCF = 0;

typedef struct pon_wan_info_list_s
{
	omci_dm_pon_wan_info_t info;
	int defualt_acl_entry_idx;
	int defualt_swacl_entry_idx;
	int acl_entry_idx[WAN_PONMAC_QUEUE_MAX];
	int swacl_entry_idx[WAN_PONMAC_QUEUE_MAX];
	int queue2ponqIdx[WAN_PONMAC_QUEUE_MAX];
	unsigned char ponq_used[WAN_PONMAC_QUEUE_MAX];
	int default_qos_queue;
	struct list_head list;
	int patch_rule_idx[PATCH_END][WAN_PONMAC_QUEUE_MAX];
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	int dotip_mark_acl_idx;
#endif
} pon_wan_info_list_t;
pon_wan_info_list_t pon_wan_list;

struct mutex update_qos_lock;


typedef struct 
{
	int wan_idx;
	char rif[IFNAMSIZ+1];
	char vif[IFNAMSIZ+1];
	int port_bind;
	struct list_head list;
} pon_wan_map_list_t;
pon_wan_map_list_t pon_wan_map_list;

static pon_wan_map_list_t* find_wan_map(int idx);
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
#if defined(CONFIG_RTL_SMUX_TXMARK_STREAMID)
//stream id enable bit [16]
#define SKBMARK_SIDEN_S             16
#define SKBMARK_SIDEN_M             (0x1 << SKBMARK_SIDEN_S)
#define SKBMARK_GET_SIDEN(MARK)     ((MARK & SKBMARK_SIDEN_M) >> SKBMARK_SIDEN_S)
#define SKBMARK_SET_SIDEN(MARK, V)  ((MARK & ~SKBMARK_SIDEN_M) | (V << SKBMARK_SIDEN_S))
//stream id enable bit [3:9]
#define SKBMARK_SID_S             3
#define SKBMARK_SID_M             (0x7F << SKBMARK_SID_S)
#define SKBMARK_GET_SID(MARK)     ((MARK & SKBMARK_SID_M) >> SKBMARK_SID_S)
#define SKBMARK_SET_SID(MARK, V)  ((MARK & ~SKBMARK_SID_M) | (V << SKBMARK_SID_S))
#endif
int rtk_tr142_smux_change_carrier(int wanIdx, int carrier);
#endif

#if defined(CONFIG_RTL_MULTI_ETH_WAN)
void tr142_timer_cb(int wanidx)
{
	rtk_tr142_smux_change_carrier(wanidx, 1);
}

int init_tr142_timer(int idx)
{
	init_timer(&tr142_timer[idx]);
	tr142_timer[idx].expires = jiffies + msecs_to_jiffies(tr142_omci_recover_wan_timeout*1000);  //5 seconds is minimum...?!
	tr142_timer[idx].data = idx;
	tr142_timer[idx].function = tr142_timer_cb;
	add_timer(&tr142_timer[idx]);
	
	return 0;
}

void cleanup_tr142_timer(int idx)
{
	if(timer_pending(&tr142_timer[idx]))
	{
		timer_delete_sync(&tr142_timer[idx]);
	}
	return;
}
#endif

/***** Platform dependent codes ****************/
#ifdef CONFIG_LUNA_G3_SERIES
static int gpon_flow_set(unsigned int flowid, unsigned char pri, pon_wan_info_list_t *node)
{
	ca8279_gpon_usFlow_t flow_attr = {0};
	int ret;

	flow_attr.gemPortId = node->info.gemPortId;
	flow_attr.tcontId = node->info.tcontId;
	flow_attr.tcQueueId = pri;
	flow_attr.isExpand = 1;
	flow_attr.aesState = 0;	//tr142 don't care.

	ret = pf_ca8279_gpon_usFlow_set(flowid, &flow_attr);
	if(ret != OMCI_ERR_OK)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] rtk_rg_gpon_usFlow_set returns %d\n", ret);
		return -1;
	}

	return 0;
}

static int gpon_flow_delete(unsigned int flowid)
{
	int ret = pf_ca8279_gpon_usFlow_del(flowid);

	if(ret != OMCI_ERR_OK)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] pf_ca8279_gpon_usFlow_del returns %d\n", ret);
		return -1;
	}

	return 0;
}

static int pon_queue_add(unsigned int tcont, unsigned char qid, int pri, rtk_tr142_qos_queue_conf_t *user_queue_conf, rtk_ponmac_queueCfg_t *pon_queue_conf, int gcdBase)
{
	ca8279_ponmac_queueCfg_t g3_queue_conf;
	rtk_ponmac_queue_t ponq;
	
	ponq.schedulerId = tcont;
	ponq.queueId = qid;

	g3_queue_conf.scheduleType =  pon_queue_conf->type = user_queue_conf->type;
	g3_queue_conf.weight = pon_queue_conf->weight = user_queue_conf->weight;
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	if((user_queue_conf->pir>0) && (user_queue_conf->pir<MAX_GPON_RATE))
		g3_queue_conf.pir = pon_queue_conf->pir = user_queue_conf->pir;
	else
		g3_queue_conf.pir = pon_queue_conf->pir = MAX_GPON_RATE;
#else
	g3_queue_conf.pir = pon_queue_conf->pir = MAX_GPON_RATE;
#endif

	pf_ca8279_gpon_pon_queue_add(tcont, pri, qid, &g3_queue_conf);
	rtk_ponmac_queue_add(&ponq, pon_queue_conf);
	return 0;
}

static int pon_queue_reset(unsigned int tcont, unsigned char qid, int pri, rtk_ponmac_queueCfg_t *pon_queue_conf)
{
	ca8279_ponmac_queueCfg_t g3_queue_conf;

	g3_queue_conf.scheduleType =  pon_queue_conf->type = STRICT_PRIORITY;
	g3_queue_conf.weight = pon_queue_conf->weight = 0;
	g3_queue_conf.pir = pon_queue_conf->pir = MAX_GPON_RATE;

	pf_ca8279_gpon_pon_queue_add(tcont, pri, qid, &g3_queue_conf);

	return 0;
}
#else
static int gpon_flow_set(unsigned int flowid, unsigned char pri, pon_wan_info_list_t *node)
{
	rtk_gpon_usFlow_attr_t flow_attr = {0};
	int ret;

	flow_attr.gem_port_id = node->info.gemPortId;
	flow_attr.tcont_id = node->info.tcontId;
	flow_attr.type = RTK_GPON_FLOW_TYPE_ETH;

	ret = rtk_gpon_usFlow_set(flowid, &flow_attr);
	if(ret != 0)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] rtk_rg_gpon_usFlow_set returns %d\n", ret);

		return -1;
	}
	return 0;
}

static int gpon_flow_delete(unsigned int flowid)
{
	rtk_gpon_usFlow_attr_t flow_attr = {0};

	rtk_gpon_usFlow_get(flowid, &flow_attr);
	if(flow_attr.gem_port_id != RTK_GPON_GEMPORT_ID_NOUSE)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
			"[tr142] Clearing us flow id %d\n", flowid);
		flow_attr.gem_port_id = RTK_GPON_GEMPORT_ID_NOUSE;
		rtk_gpon_usFlow_set(flowid, &flow_attr);
	}

	return 0;
}

static int pon_queue_add(unsigned int tcont, unsigned char qid, int pri, rtk_tr142_qos_queue_conf_t *user_queue_conf, rtk_ponmac_queueCfg_t *pon_queue_conf, int gcdBase)
{
	rtk_ponmac_queue_t ponq;

	ponq.schedulerId = tcont;
	ponq.queueId = qid;

	pon_queue_conf->type = user_queue_conf->type;
	pon_queue_conf->weight = user_queue_conf->weight/gcdBase;
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	if((user_queue_conf->pir>0) && (user_queue_conf->pir<MAX_GPON_RATE))
		pon_queue_conf->pir = user_queue_conf->pir;
	else
		pon_queue_conf->pir = MAX_GPON_RATE;
#else
	pon_queue_conf->pir = MAX_GPON_RATE;
#endif

	rtk_ponmac_queue_add(&ponq, pon_queue_conf);
	return 0;
}

static int pon_queue_reset(unsigned int tcont, unsigned char qid, int pri, rtk_ponmac_queueCfg_t *pon_queue_conf)
{
	rtk_ponmac_queue_t ponq;

	ponq.schedulerId = tcont;
	ponq.queueId = qid;
	pon_queue_conf->type = STRICT_PRIORITY;
	pon_queue_conf->weight = 0;
	pon_queue_conf->pir = MAX_GPON_RATE;
	rtk_ponmac_queue_add(&ponq, pon_queue_conf);

	return 0;
}
#endif
/***** End of platform dependent codes *********/

struct sock *rtk_tr142_nl_sk = NULL;
int pid = 0;
int rtk_tr142_nl_send_msg(int wanIndex);

static void dump_buff(char *user_data, int len)
{
    char *it;
    char *tail = user_data + len;

    printk("----%s point:%p, len:%d------------------:\n", __FUNCTION__,
           user_data, len);
    for (it = user_data; it != tail; ++it) {
        char c = *(char *) it;
        printk(" 0x%2x ", c);
    }
    printk("\n");
    printk("---------------------------------------------------\n");
}

typedef enum rtk_tr142_nlmsg_cmd_e {
	RTK_TR142_NLMSG_CMD_NONE = 0,
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	RTK_TR142_NLMSG_CMD_CMCC = 1,
#endif
#ifdef CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE
	RTK_TR142_NLMSG_CMD_FLOW_ID = 2,
#endif
} rtk_tr142_nlmsg_cmd_t;

#ifdef CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE
#define WAN_QOS_QUEUE_MAX (8)
typedef struct
{
	unsigned int ifIndex;
	int flowId[WAN_QOS_QUEUE_MAX]; // -1: disable
	int priority[WAN_QOS_QUEUE_MAX]; // -1: disable
	unsigned char action; //0: delete, 1: add
} rtk_tr142_nlmsg_flow_id_ctrl_t;
#endif

typedef struct RTK_TR142_MSG_H {
	rtk_tr142_nlmsg_cmd_t rtk_tr142_nlmsg_cmd;
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	int wanIndex;
#endif
#ifdef CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE
	rtk_tr142_nlmsg_flow_id_ctrl_t flow_id_ctrl_info;
#endif
} rtk_tr142_msg_header_T, *rtk_tr142_msg_header_Tp;

static void rtk_tr142_nl_recv_msg(struct sk_buff *skb)
{
    struct nlmsghdr *nlh;

    char *msg = "Response from kernel";
    printk(KERN_INFO "Entering: %s\n", __FUNCTION__);
    //   DEBUG_PRINT(KERN_INFO "Entering: %s\n", __FUNCTION__);
    printk("Entering: %s\n",  __FUNCTION__);

    nlh = (struct nlmsghdr *) skb->data;
    printk(KERN_INFO "Netlink received msg payload:%s, pid:%d\n", (char *)nlmsg_data(nlh), nlh->nlmsg_pid);
    //  DEBUG_PRINT(KERN_INFO "Netlink received msg payload:%s, pid:%d\n", (char *)nlmsg_data(nlh), nlh->nlmsg_pid);
    printk("user space application pid(%d)\n", nlh->nlmsg_pid);
    pid = nlh->nlmsg_pid;       /*pid of sending process */
}

int rtk_tr142_nl_send_msg(int wanIndex)
{
    struct sk_buff *skb_out;
    int res = -1;
    int msg_size;
    rtk_tr142_msg_header_T msgh = { {'\0'} };
    struct nlmsghdr *nlh;
    char *data_p;

    if (pid == 0) {
        printk("Missing user space application pid(%d)\n", pid);
        return -1;
    }

    //DBG_DYN_PRINT("sizeof(rtk_tr142_msg_header_T)=%d, strlen(msg)=%d\n",
    //              sizeof(rtk_tr142_msg_header_T), strlen(msg));

	printk("Entering: %s wanIndex %d\n",  __FUNCTION__, wanIndex);

	memset(&msgh, 0, sizeof(rtk_tr142_msg_header_T));
	msgh.rtk_tr142_nlmsg_cmd = RTK_TR142_NLMSG_CMD_NONE;

	msg_size = sizeof(rtk_tr142_msg_header_T);
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	msgh.rtk_tr142_nlmsg_cmd = RTK_TR142_NLMSG_CMD_CMCC;
	msgh.wanIndex = wanIndex;
#endif

    //DEBUG_PRINT(" msgh.bundlename :%s , len:%d\n", msgh.bundlename,  strlen(msgh.bundlename));
//      DEBUG_PRINT("msg:\n%s\n", msg);

    skb_out = nlmsg_new(msg_size+1, GFP_ATOMIC);

    if (!skb_out) {
        printk("Failed to allocate new skb\n");
        return -1;
    }
    nlh = nlmsg_put(skb_out, 0, 1, NLMSG_DONE, msg_size+1, 0);

    data_p = nlmsg_data(nlh);
    memcpy(data_p, &msgh, sizeof(rtk_tr142_msg_header_T));

    data_p = data_p + sizeof(rtk_tr142_msg_header_T);

    //debug
    //dump_buff(nlmsg_data(nlh), msg_size);


    res = nlmsg_multicast(rtk_tr142_nl_sk, skb_out, 0, 1, GFP_KERNEL);
    //DBG_DYN_PRINT("  Send mesg to user app(%d): res(%d)\n", pid, res);
    if (res < 0) {
        printk("Error while sending msg  to user. error(%d)\n",
               res);
    }

    return res;
}

int rtk_tr142_nl_init(void)
{
    //This is for 3.6 kernels and above.
	struct netlink_kernel_cfg cfg = {
		.groups = 1,
		.input = rtk_tr142_nl_recv_msg,
	};

    rtk_tr142_nl_sk = netlink_kernel_create(&init_net, NETLINK_REALTEK_TR142, &cfg);

	printk("Initial tr142 raw socket.\n");
    if (!rtk_tr142_nl_sk) {
        printk("Error creating tr142 raw socket.\n");
        return -1;
    }
	//printk("%s\n", __FUNCTION__);
    return 0;
}

void rtk_tr142_nl_exit(void)
{
    netlink_kernel_release(rtk_tr142_nl_sk);
}

// Find the highest priority unused queue
static int get_next_ponq(pon_wan_info_list_t *node)
{
	int i;
	int qidx = -1;

	if(node == NULL)
		return -1;

	for(i = 0 ; i < queue_num ; i++)
	{
		if(node->ponq_used[i] == 0
			&& node->info.queueSts[i] >= 0
			&& node->info.queueSts[i] < WAN_PONMAC_QUEUE_ID_MAX)
		{
			if(qidx == -1 || node->info.queueSts[i] > node->info.queueSts[qidx])
				qidx = i;
		}
		else
			node->ponq_used[i] = 1;	//mark invalid pon queue
	}
	return qidx;
}

static int get_default_ponq(pon_wan_info_list_t *node)
{
	int i;
	int qidx = 0;

	if(node == NULL)
		return -1;

	for(i = 1 ; i < queue_num ; i++)
	{
		if(node->info.queueSts[i] >= 0
			&& node->info.queueSts[i] < WAN_PONMAC_QUEUE_ID_MAX)
		{
			if(node->info.queueSts[i] < node->info.queueSts[qidx])
				qidx = i;
		}
	}
	return qidx;
}

static int setup_default_rules(pon_wan_info_list_t *node)
{
	int ret = 0;

#ifdef CONFIG_APOLLO_ROMEDRIVER
	rtk_rg_aclFilterAndQos_t acl = {0};

	memset(&acl, 0, sizeof(rtk_rg_aclFilterAndQos_t));
	ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_RG_ACL, &acl);
	if (ret != 0)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
	}
	if(acl.action_type == ACL_ACTION_TYPE_QOS)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
			"[tr142] Assign flow id %d on wanIdx %d, action_type = %d, tcontId=%u, gemPortId=%u, queueSts=%u\n",
				node->info.usFlowId[0], node->info.wanIdx, acl.action_type,
				node->info.tcontId, 	node->info.gemPortId, node->info.queueSts[0]);
		acl.qos_actions |= ACL_ACTION_STREAM_ID_OR_LLID_BIT;
		acl.action_stream_id_or_llid = node->info.usFlowId[0];
		ret = rtk_rg_aclFilterAndQos_add(&acl, &node->defualt_acl_entry_idx);
		if(ret != 0 )
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
				"[tr142] <%s:%d> rtk_rg_aclFilterAndQos_add error: %d\n", __FUNCTION__, __LINE__, ret);
		}
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
					"[tr142] rtk_rg_aclFilterAndQos_add got index %d\n", node->defualt_acl_entry_idx);
	}

	memset(&acl, 0, sizeof(rtk_rg_aclFilterAndQos_t));
	ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_RG_US_SW_ACL, &acl);
	if (ret != 0)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
	}
	if(acl.fwding_type_and_direction != 0)
	{
		if(acl.action_type == ACL_ACTION_TYPE_QOS)
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
				"[tr142][SwAclEntry] Assign flow id %d on wanIdx %d, action_type = %d\n", node->info.usFlowId[0], node->info.wanIdx, acl.action_type);
			acl.qos_actions|= ACL_ACTION_STREAM_ID_OR_LLID_BIT;
			acl.action_stream_id_or_llid = node->info.usFlowId[0];
			ret = rtk_rg_aclFilterAndQos_add(&acl, &node->defualt_swacl_entry_idx);
			if(ret != 0)
			{
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
					"[tr142][SwAclEntry] <%s:%d> rtk_rg_aclFilterAndQos_add error: %d\n", __FUNCTION__, __LINE__, ret);
			}
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
					"[tr142][SwAclEntry] rtk_rg_aclFilterAndQos_add got index %d\n", node->defualt_swacl_entry_idx);
		}
	}
#endif
	return ret;
}

static int setup_flows(pon_wan_info_list_t *node)
{
	int i = 0, j = 0;
#ifdef CONFIG_APOLLO_ROMEDRIVER
	rtk_rg_aclFilterAndQos_t acl = {0};
#endif
	rtk_ponmac_queue_t ponq = {0};
	int ret = 0;

	if(node == NULL)
		return RTK_TR142_ERR_WAN_INFO_NULL;

	if(node->defualt_acl_entry_idx == -1)
		setup_default_rules(node);

	if(is_queues_ready)
	{
		int default_ponq;

		default_ponq = get_default_ponq(node);

		for(i = 0 ; i < queue_num ; i++)
		{
			int ponq_idx;

			if(queues.queue[i].enable == 0)
				continue;

			ponq_idx = node->queue2ponqIdx[i];

			if(ponq_idx == -1)
				break;

			// OMCI have mapped first queue & flow for us
			if(ponq_idx != 0)
			{
#ifdef CONFIG_LUNA_G3_SERIES
				gpon_flow_set(node->info.usFlowId[ponq_idx], node->info.tcontQId[ponq_idx], node);
#else
				gpon_flow_set(node->info.usFlowId[ponq_idx], ACL_QOS_INTERNAL_PRIORITY_START - i - 1, node);
#endif
				ponq.schedulerId = node->info.tcontId;
				ponq.queueId = node->info.queueSts[ponq_idx];
				ret = rtk_ponmac_flow2Queue_set(node->info.usFlowId[ponq_idx], &ponq);
				if(ret != 0)
				{
					TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
						"[tr142] rtk_rg_ponmac_flow2Queue_set returns %d\n", ret);
				}

				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
					"[tr142] Map flow %d to queue %d\n", node->info.usFlowId[ponq_idx], node->info.queueSts[ponq_idx]);
			}
#ifdef CONFIG_APOLLO_ROMEDRIVER
			memset(&acl, 0, sizeof(rtk_rg_aclFilterAndQos_t));
			ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_RG_ACL, &acl);
			if (ret != 0)
			{
			    TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			       "[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
			}
			if(acl.action_type == ACL_ACTION_TYPE_QOS)
			{
				// Default queue do not need to set internal priority
				//Default queue: last available pon queue
				// or last enabled user space QoS queue.
				if(ponq_idx != default_ponq && i != node->default_qos_queue)
				{
					acl.filter_fields |= INTERNAL_PRI_BIT;
					acl.internal_pri = ACL_QOS_INTERNAL_PRIORITY_START - i - 1;
					if(acl.internal_pri < 0)
						acl.internal_pri = 0;

				}
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
						"[tr142] Assign pri %d for flow %d on wanIdx %d, action_type = %d\n", acl.internal_pri, node->info.usFlowId[ponq_idx], node->info.wanIdx, acl.action_type);
				acl.qos_actions |= ACL_ACTION_STREAM_ID_OR_LLID_BIT;
				acl.action_stream_id_or_llid = node->info.usFlowId[ponq_idx];
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
				if(acl.qos_actions || ACL_ACTION_ACL_CVLANTAG_BIT){
					acl.action_acl_cvlan.cvlanCpriDecision =ACL_CVLAN_CPRI_NOP;
				}
#endif
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
					"[tr142] %s@(%d): acl.action_acl_cvlan.cvlanCpriDecision = %d, is_dot1p_value_byCF = %02x\n", __FILE__, __LINE__, acl.action_acl_cvlan.cvlanCpriDecision, is_dot1p_value_byCF);

				if(is_dot1p_value_byCF == 0){
					if(acl.action_acl_cvlan.cvlanCpriDecision ==ACL_CVLAN_CPRI_ASSIGN){
						acl.action_acl_cvlan.cvlanCpriDecision =ACL_CVLAN_CPRI_NOP; 							
					}
				}
				acl.acl_weight= 1;
				ret = rtk_rg_aclFilterAndQos_add(&acl, &node->acl_entry_idx[ponq_idx]);
				if(ret != 0)
				{
					TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
						"[tr142] <%s:%d> rtk_rg_aclFilterAndQos_add error: %d\n", __FUNCTION__, __LINE__, ret);
				}
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
						"[tr142] rtk_rg_aclFilterAndQos_add got index %d\n", node->acl_entry_idx[ponq_idx]);
			}
			memset(&acl, 0, sizeof(rtk_rg_aclFilterAndQos_t));
			ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_RG_US_SW_ACL, &acl);
			if (ret != 0)
			{
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
				       "[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
			}
			if(acl.fwding_type_and_direction != 0)
			{
				if(acl.action_type == ACL_ACTION_TYPE_QOS)
				{
					if(ponq_idx != default_ponq && i != node->default_qos_queue)
					{
						acl.filter_fields |= INTERNAL_PRI_BIT;
						acl.internal_pri = ACL_QOS_INTERNAL_PRIORITY_START - i -1;
						if(acl.internal_pri < 0)
							acl.internal_pri = 0;


					}
					TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
							"[tr142][SwAclEntry] Assign pri %d for flow %d on wanIdx %d, action_type = %d\n", acl.internal_pri, node->info.usFlowId[ponq_idx], node->info.wanIdx, acl.action_type);

					acl.qos_actions |= ACL_ACTION_STREAM_ID_OR_LLID_BIT;
					acl.action_stream_id_or_llid = node->info.usFlowId[ponq_idx];
		
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)	
					if(acl.qos_actions || ACL_ACTION_ACL_CVLANTAG_BIT){
						//20180420 ramen--fix the QOS 802.1p mark when wan interface without 8021p
						//when wan interface without 8021p,the OMCI rgUsSwAclEntry will use ACL_CVLAN_CPRI_COPY_FROM_INTERNAL_PRI action
						if(acl.action_acl_cvlan.cvlanCpriDecision ==ACL_CVLAN_CPRI_ASSIGN||acl.action_acl_cvlan.cvlanCpriDecision ==ACL_CVLAN_CPRI_COPY_FROM_INTERNAL_PRI)
						{
							acl.action_acl_cvlan.cvlanCpriDecision =ACL_CVLAN_CPRI_NOP;								
						}
					}

#endif
					TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
						"[tr142] %s@(%d): acl.action_acl_cvlan.cvlanCpriDecision = %d, is_dot1p_value_byCF = %02x\n", __FILE__, __LINE__, acl.action_acl_cvlan.cvlanCpriDecision, is_dot1p_value_byCF);
					if(is_dot1p_value_byCF == 0){
						if(acl.action_acl_cvlan.cvlanCpriDecision ==ACL_CVLAN_CPRI_ASSIGN){
							acl.action_acl_cvlan.cvlanCpriDecision =ACL_CVLAN_CPRI_NOP;								
						}
					}

#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
					if(queues.queue[i].dscpval){						
								acl.qos_actions |= ACL_ACTION_DSCP_REMARKING_BIT	;
								acl.action_dscp_remarking_pri = queues.queue[i].dscpval;
					}
#if 0		
					if(queues.queue[i].logEnable){
								acl.qos_actions |= ACL_ACTION_CF_LOG_COUNTER_BIT;
								acl.action_cf_log_counter = i*2;
								//rtk_stat_logCtrl_set(acl.action_cf_log_counter,STAT_LOG_MODE_64BITS);
								//rtk_stat_logCtrl_set(acl.action_cf_log_counter,STAT_LOG_TYPE_BYTECNT);	
								rtk_rg_aclLogCounterControl_set(acl.action_cf_log_counter,STAT_LOG_TYPE_BYTECNT,STAT_LOG_MODE_64BITS);
					}
#endif
					//printk("%s %d queue=%d set dscp %d acl.qos_actions=0x%08x action_cf_log_counter=%d\n",__FUNCTION__, __LINE__,i,queues.queue[i].dscpval,acl.qos_actions,acl.action_cf_log_counter);
#endif					
					acl.acl_weight= 1;
					ret = rtk_rg_aclFilterAndQos_add(&acl, &node->swacl_entry_idx[ponq_idx]);
					if(ret != 0)
					{
						TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
							"[tr142][SwAclEntry] <%s:%d> rtk_rg_aclFilterAndQos_add error: %d\n", __FUNCTION__, __LINE__, ret);
					}
					TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
						"[tr142][SwAclEntry] rtk_rg_aclFilterAndQos_add got index %d\n", node->swacl_entry_idx[ponq_idx]);
				}
			}

			for(j = 0 ; j < PATCH_END ; j++)
			{
				if(node->info.rgPatchEntry[j].valid)
				{
					// skip rules for dpi if hwnat is enabled
					if(j == PATCH_DPI && !is_hwnat_disabled)
						continue;

					if(node->info.rgPatchEntry[j].ruleOpt == OMCI_CF_RULE_RG_OPT_ACL)
					{
						memset(&acl, 0, sizeof(rtk_rg_aclFilterAndQos_t));
						ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_PATCH_DS_9602C_A+j, &acl);
						if (ret != 0)
						{
					            TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
					                "[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
						}

						// Default queue do not need to set internal priority
						//Default queue: last available pon queue
						// or last enabled user space QoS queue.
						if(ponq_idx != default_ponq && i != node->default_qos_queue)
						{
							acl.filter_fields |= INTERNAL_PRI_BIT;
							acl.internal_pri = ACL_QOS_INTERNAL_PRIORITY_START - i -1;
							if(acl.internal_pri < 0)
								acl.internal_pri = 0;		

						}

						acl.qos_actions |= ACL_ACTION_STREAM_ID_OR_LLID_BIT;
						acl.action_stream_id_or_llid = node->info.usFlowId[ponq_idx];
						ret = rtk_rg_aclFilterAndQos_add(&acl, &node->patch_rule_idx[j][ponq_idx]);
						if(ret != 0)
						{
							TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
								"[tr142][patch%d] <%s:%d> rtk_rg_aclFilterAndQos_add error: %d\n", j, __FUNCTION__, __LINE__, ret);
						}
						TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
								"[tr142][patch%d] rtk_rg_aclFilterAndQos_add got index %d\n", j, node->patch_rule_idx[j][ponq_idx]);
					}
					else
					{
						rtk_rg_classifyEntry_t cf;
						memset(&cf, 0, sizeof(rtk_rg_classifyEntry_t));
						ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_PATCH_DS_9602C_A+j, &cf);
						if (ret != 0)
						{
						    TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
						        "[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
						}

#if 0
						// Default queue do not need to set internal priority
						//Default queue: last available pon queue
						// or last enabled user space QoS queue.
						if(ponq_idx != default_ponq && i != node->default_qos_queue)
						{
							cf.filter_fields |= INTERNAL_PRI_BIT;
#if defined(CONFIG_EPON_FEATURE)
							// highest priority for EPON OAM
							cf.internalPri = 6 - i;
#else
							cf.internalPri = 7 - i;
#endif
						}

						cf.us_action_field |= CF_US_ACTION_SID_BIT;
						cf.action_sid_or_llid.sidDecision = ACL_SID_LLID_ASSIGN;
						cf.action_sid_or_llid.assignedSid_or_llid = node->info.usFlowId[ponq_idx];
#endif
						ret = rtk_rg_classifyEntry_add(&cf);
						if(ret != 0)
						{
							TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
								"[tr142][patch%d] <%s:%d> rtk_rg_classifyEntry_add error: %d\n", j, __FUNCTION__, __LINE__, ret);
						}
						TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
								"[tr142][patch%d] rtk_rg_classifyEntry_add OK\n", j);
					}
				}
			}
#else
// TBD, FC driver
#endif
		}
	}
	else
	{
#ifdef CONFIG_APOLLO_ROMEDRIVER
		// QoS is disabled, use first pon queue and flow directly.
		for(j = 0 ; j < PATCH_END ; j++)
		{
			if(node->info.rgPatchEntry[j].valid)
			{
				// skip rules for dpi if hwnat is enabled
				if(j == PATCH_DPI && !is_hwnat_disabled)
					continue;

				if(node->info.rgPatchEntry[j].ruleOpt == OMCI_CF_RULE_RG_OPT_ACL)
				{
					memset(&acl, 0, sizeof(rtk_rg_aclFilterAndQos_t));
					ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_PATCH_DS_9602C_A+j, &acl);
					if (ret != 0)
					{
					   TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
					        "[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
					}
					acl.qos_actions|= ACL_ACTION_STREAM_ID_OR_LLID_BIT;
					acl.action_stream_id_or_llid = node->info.usFlowId[0];
					ret = rtk_rg_aclFilterAndQos_add(&acl, &node->patch_rule_idx[j][0]);
					if(ret != 0)
					{
						TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
							"[tr142][patch] <%s:%d> rtk_rg_aclFilterAndQos_add error: %d\n", __FUNCTION__, __LINE__, ret);
					}
					TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
							"[tr142][patch] rtk_rg_aclFilterAndQos_add got index %d\n", node->patch_rule_idx[j][0]);
				}
				else
				{
					rtk_rg_classifyEntry_t cf;
					memset(&cf, 0, sizeof(rtk_rg_classifyEntry_t));
					ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_PATCH_DS_9602C_A+j, &cf);
					if (ret != 0)
					{
					    TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
					        "[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
					}

					cf.us_action_field |= CF_US_ACTION_SID_BIT;
					cf.action_sid_or_llid.sidDecision = ACL_SID_LLID_ASSIGN;
					cf.action_sid_or_llid.assignedSid_or_llid = node->info.usFlowId[0];
					ret = rtk_rg_classifyEntry_add(&cf);
					if(ret != 0)
					{
						TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
							"[tr142][patch%d] <%s:%d> rtk_rg_classifyEntry_add error: %d\n", j, __FUNCTION__, __LINE__, ret);
					}
					TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
							"[tr142][patch%d] rtk_rg_classifyEntry_add OK\n", j);
				}
			}
		}
#else
//TBD, for FC driver
TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
            "[tr142][FC driver] wanIdx %d, wanType %u\n" \
            "                   rgAclEntry.wanVid %d, rgAclEntry.wanPri %d, rgAclEntry.ponVId %d, rgAclEntry.flowId %d\n" \
            "                   gemPortId %u, qSts[0] %u, tcontId %u, usFlow[0] %u\n" \
            "                   rgUsSwAclEntry.wanVid %d, rgUsSwAclEntry.wanPri %d, rgUsSwAclEntry.ponVId %d, rgUsSwAclEntry.flowId %d\n",
            node->info.wanIdx,
            node->info.wanType,
            node->info.rgAclEntry.wanVid,
            node->info.rgAclEntry.wanPri,
            node->info.rgAclEntry.ponVid,
            node->info.rgAclEntry.flowId,
            node->info.gemPortId,
            node->info.queueSts[0],
            node->info.tcontId,
            node->info.usFlowId[0],
            node->info.rgUsSwAclEntry.wanVid,
            node->info.rgUsSwAclEntry.wanPri,
            node->info.rgUsSwAclEntry.ponVid,
            node->info.rgUsSwAclEntry.flowId);
#endif
	}
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	memset(&acl, 0, sizeof(rtk_rg_aclFilterAndQos_t));
	ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_RG_US_SW_ACL, &acl);
	if (ret != 0)
	{
	    TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
    	        "[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
	}

	if(acl.fwding_type_and_direction != 0 && acl.action_acl_cvlan.cvlanCpriDecision ==ACL_CVLAN_CPRI_ASSIGN){
							rtk_rg_aclFilterAndQos_t assign_8021p_acl = {0};
			memcpy(&assign_8021p_acl, &acl, sizeof(rtk_rg_aclFilterAndQos_t));
							assign_8021p_acl.qos_actions =ACL_ACTION_1P_REMARKING_BIT;
			assign_8021p_acl.action_dot1p_remarking_pri = acl.action_acl_cvlan.assignedCpri;
							int acl_idx = 0;
							ret = rtk_rg_aclFilterAndQos_add(&assign_8021p_acl, &acl_idx);
							if(ret != 0)
							{
								TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
									"[tr142][SwAclEntry] <%s:%d> rtk_rg_aclFilterAndQos_add error: %d\n", __FUNCTION__, __LINE__, ret);
							}							
					}
#endif

#if defined(CONFIG_RTL_MULTI_ETH_WAN)
#if defined(CONFIG_RTL_SMUX_TXMARK_STREAMID) && defined(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE)
	unsigned int mark = 0, mask = 0;
	struct smux_args sargs;
	pon_wan_map_list_t *map = find_wan_map(node->info.wanIdx);
	if(map)
	{
		mask = SKBMARK_SIDEN_M | SKBMARK_SID_M;
		mark = SKBMARK_SET_SIDEN(mark, 1) | SKBMARK_SET_SID(mark, node->info.usFlowId[0]);
					
		memset(&sargs, 0, sizeof(struct smux_args));
		sargs.args.cmd = GET_SMUX_CMD;
		strcpy(sargs.args.ifname, map->rif);
		strcpy(sargs.args.u.ifname, map->vif);
		if(get_smux_device_info(&sargs) == 0)
		{
			sargs.args.cmd = SET_SMUX_CMD;
			sargs.args.mark = mark;
			sargs.args.mask = mask;
			if(set_smux_device_info(&sargs) != 0)
			{
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
					"[tr142] Fail to set smux information for interface %s\n", map->vif);
			}
		}
		else
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
					"[tr142] Fail to get smux information for interface %s\n", map->vif);
		}
	}
#endif
#endif
	return RTK_TR142_ERR_OK;
}

static int is_qos_enable(void)
{
	int i;

	for(i = 0 ; i< queue_num ; i++)
	{
		if(queues.queue[i].enable)
			return 1;
	}
	return 0;
}

static int weight_gcd(int wr1, int wr2)
{
	if(wr1<wr2)
	{
		return weight_gcd(wr2,wr1); 
	}
	else if(wr2==0)
	{
		return wr1;	
	}
	else
	{
		return weight_gcd(wr2,wr1%wr2);
	}
}

static int find_ponmac_queue_weight_gcd(pon_wan_info_list_t *node)
{
	int i;
	int ponq_idx;
	int gcdBase = 100;
	if(is_queues_ready)
	{
		for(i = 0 ; i < queue_num ; i++)
		{
			if(queues.queue[i].enable == 0)
				continue;

			ponq_idx = get_next_ponq(node);
			if(ponq_idx == -1)
				break;
			if(queues.queue[i].type == WFQ_WRR_PRIORITY && (queues.queue[i].weight >0)){
				gcdBase = weight_gcd(gcdBase, queues.queue[i].weight);
			}
		}
	}	
	return gcdBase;
}

static int setup_ponmac_queue(pon_wan_info_list_t *node)
{
	int i;
	int ponq_got = 0;
	int ponq_idx;
	int gcdbase;

	if(is_queues_ready)
	{
		for(i = 0 ; i < queue_num ; i++)
		{
			if(queues.queue[i].enable == 0)
				continue;

			ponq_idx = get_next_ponq(node);
			if(ponq_idx == -1)
				break;

			node->default_qos_queue = i;
			gcdbase = find_ponmac_queue_weight_gcd(node);
#ifdef CONFIG_LUNA_G3_SERIES
			pon_queue_add(node->info.tcontId, node->info.queueSts[ponq_idx], node->info.tcontQId[ponq_idx], &queues.queue[i], &node->info.queueCfg[i], gcdbase);
#else
			pon_queue_add(node->info.tcontId, node->info.queueSts[ponq_idx], ACL_QOS_INTERNAL_PRIORITY_START - i - 1, &queues.queue[i], &node->info.queueCfg[i], gcdbase);
#endif

			node->ponq_used[ponq_idx] = 1;
			node->queue2ponqIdx[i] = ponq_idx;
			ponq_got = 1;
		}
		if(ponq_got == 0)
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
				"[tr142] queue_num = %d, WAN_PONMAC_QUEUE_ID_MAX=%d\n", queue_num, WAN_PONMAC_QUEUE_ID_MAX);
			return RTK_TR142_ERR_PONQ_UNAVAILABLE;
		}
	}

	TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
		"[tr142] Setup PON Queue Completed, default QoS queue: %d\n", node->default_qos_queue);
	return RTK_TR142_ERR_OK;
}

static int is_gem_port_duplicated(pon_wan_info_list_t *wan_info, unsigned int flow_id)
{
	pon_wan_info_list_t *node = NULL, *tmp = NULL;

	list_for_each_entry_safe(node, tmp, &(pon_wan_list.list), list)
	{
		if(node == wan_info)
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
				"[tr142] Skip check duplicated for node %p\n", node);
			continue;
		}

		if(node->info.gemPortId == wan_info->info.gemPortId)
			return 1;
	}

	return 0;
}

static void clear_pon_qos_conf_by_wan(pon_wan_info_list_t *wan_info, int clear_defaul_acl)
{
	int i, j;

	TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
		"[tr142] Clear wan node %p\n", wan_info);

	if(clear_defaul_acl)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
				"[tr142] Deleting default acl entry %d, %d\n", wan_info->defualt_acl_entry_idx, wan_info->defualt_swacl_entry_idx);
#ifdef CONFIG_APOLLO_ROMEDRIVER
		if(wan_info->defualt_acl_entry_idx >= 0)
			rtk_rg_aclFilterAndQos_del(wan_info->defualt_acl_entry_idx);
#endif
		wan_info->defualt_acl_entry_idx = -1;

#ifdef CONFIG_APOLLO_ROMEDRIVER
		if(wan_info->defualt_swacl_entry_idx >= 0)
			rtk_rg_aclFilterAndQos_del(wan_info->defualt_swacl_entry_idx);
#endif
		wan_info->defualt_swacl_entry_idx = -1;
	}

	for(i=0 ; i < WAN_PONMAC_QUEUE_MAX ; i++)
	{
		if(wan_info->acl_entry_idx[i] >= 0)
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
				"[tr142] Deleting acl entry %d\n", wan_info->acl_entry_idx[i]);

#ifdef CONFIG_APOLLO_ROMEDRIVER
			rtk_rg_aclFilterAndQos_del(wan_info->acl_entry_idx[i]);
#endif
		}
		wan_info->acl_entry_idx[i] = -1;

		if(wan_info->swacl_entry_idx[i] >= 0)
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
				"[tr142] Deleting sw acl entry %d\n", wan_info->swacl_entry_idx[i]);
#ifdef CONFIG_APOLLO_ROMEDRIVER
			rtk_rg_aclFilterAndQos_del(wan_info->swacl_entry_idx[i]);
#endif
		}
		wan_info->swacl_entry_idx[i] = -1;

		for(j = 0 ; j < PATCH_END ; j++)
		{
			if(wan_info->patch_rule_idx[j][i] >= 0)
			{
				if(wan_info->info.rgPatchEntry[j].ruleOpt == OMCI_CF_RULE_RG_OPT_ACL)
				{
					TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
						"[tr142] Deleting patch acl entry %d\n", wan_info->patch_rule_idx[j][i]);
#ifdef CONFIG_APOLLO_ROMEDRIVER
					rtk_rg_aclFilterAndQos_del(wan_info->patch_rule_idx[j][i]);
#endif
				}
				// OMCI will delete CF rule for TR-142
			}
			wan_info->patch_rule_idx[j][i] = -1;
		}

		// Do not clear first because OMCI will do this for us.
		if((i != 0) && (wan_info->ponq_used[i] == 1))
		{
			if(!is_gem_port_duplicated(wan_info, wan_info->info.usFlowId[i]))
			{
				gpon_flow_delete(wan_info->info.usFlowId[i]);
			}
			else
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
						"[tr142] Us flow id %d is reserved\n", wan_info->info.usFlowId[i]);
		}
	
		//reset pon queue to default
		if(wan_info->ponq_used[i] == 1)
		{
			int ponq_idx;

			ponq_idx = wan_info->queue2ponqIdx[i];
#ifdef CONFIG_LUNA_G3_SERIES
			pon_queue_reset(wan_info->info.tcontId, wan_info->info.queueSts[ponq_idx], wan_info->info.tcontQId[ponq_idx], &wan_info->info.queueCfg[i]);
#else
			pon_queue_reset(wan_info->info.tcontId, wan_info->info.queueSts[ponq_idx], ACL_QOS_INTERNAL_PRIORITY_START - i - 1, &wan_info->info.queueCfg[i]);
#endif
		}

		wan_info->queue2ponqIdx[i] = -1;
		wan_info->ponq_used[i] = 0;
	}
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
		if(wan_info->dotip_mark_acl_idx>0){
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
					"[tr142] Deleting sw acl entry %d\n", wan_info->dotip_mark_acl_idx);
				rtk_rg_aclFilterAndQos_del(wan_info->dotip_mark_acl_idx);
				wan_info->dotip_mark_acl_idx = -1;
		}
#endif

	wan_info->default_qos_queue = -1;
}

static void clear_pon_qos_conf(void)
{
	pon_wan_info_list_t *node = NULL, *tmp = NULL;

	list_for_each_entry_safe(node, tmp, &(pon_wan_list.list), list)
	{
		clear_pon_qos_conf_by_wan(node, 0);
	}
}

static int update_pon_qos_by_wan(pon_wan_info_list_t *wan_info)
{
	int ret;

	if(wan_info == NULL)
		return RTK_TR142_ERR_WAN_INFO_NULL;

	clear_pon_qos_conf_by_wan(wan_info, 1);

	ret = setup_ponmac_queue(wan_info);
	if(ret != RTK_TR142_ERR_OK)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] setup_ponmac_queue failed. err = %d\n", ret);
		return ret;
	}

	ret = setup_flows(wan_info);

	return ret;
}

static void update_pon_qos(void)
{
	pon_wan_info_list_t *node = NULL, *tmp = NULL;

	clear_pon_qos_conf();

	list_for_each_entry_safe(node, tmp, &(pon_wan_list.list), list)
	{
		setup_ponmac_queue(node);
		setup_flows(node);
	}
}

int rtk_tr142_pon_wan_info_set(omci_dm_pon_wan_info_t *pon_wan_info)
{
	pon_wan_info_list_t *node = NULL, *tmp = NULL;
	int updated = 0;
	int ret;

	if(pon_wan_info == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] pon_wan_info is NULL, ignore it!\n");
		return RTK_TR142_ERR_WAN_INFO_NULL;
	}

	mutex_lock(&update_qos_lock);
	// Check we need to add a new one, or update existed one
	list_for_each_entry_safe(node, tmp, &(pon_wan_list.list), list)
	{
		if(node->info.wanIdx == pon_wan_info->wanIdx)
		{
			// Update existed WAN
			memcpy(&node->info, pon_wan_info, sizeof(omci_dm_pon_wan_info_t));
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] %s Updating wan %d\n", __func__, pon_wan_info->wanIdx);
			updated = 1;
			break;
		}
	}

	if(!updated)
	{
		int i, j;

		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_INFO,
			"[tr142] %s Adding a new WAN %d\n", __func__, pon_wan_info->wanIdx);
		// Add a new one
		node = kmalloc(sizeof(pon_wan_info_list_t), GFP_KERNEL);
		if(node == NULL)
		{
			mutex_unlock(&update_qos_lock);
			return RTK_TR142_ERR_NO_MEM;
		}
		memset(node, 0, sizeof(pon_wan_info_list_t));

		// Initialization
		memcpy(&node->info, pon_wan_info, sizeof(omci_dm_pon_wan_info_t));
		node->defualt_acl_entry_idx = -1;
		node->defualt_swacl_entry_idx = -1;
		for(i=0 ; i < WAN_PONMAC_QUEUE_MAX ; i++)
		{
			node->acl_entry_idx[i] = -1;
			node->swacl_entry_idx[i] = -1;
			node->queue2ponqIdx[i] = -1;
			for(j = 0 ; j < PATCH_END ; j++)
				node->patch_rule_idx[j][i] = -1;
		}
		node->default_qos_queue = -1;

		list_add_tail(&(node->list), &(pon_wan_list.list));
		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_DEBUG,
			"[tr142] Added WAN node %p\n", node);
	}

	ret = update_pon_qos_by_wan(node);
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	rtk_tr142_nl_send_msg(pon_wan_info->wanIdx);
#endif
	if(!updated && ret != RTK_TR142_ERR_OK)
	{
		list_del(&(node->list));
		kfree(node);
	}
	
	mutex_unlock(&update_qos_lock);

#if defined(CONFIG_RTL_MULTI_ETH_WAN)
	if(ret == RTK_TR142_ERR_OK) rtk_tr142_smux_change_carrier(node->info.wanIdx, 1);
#endif

	TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
		"[tr142] %s returns %d\n", __func__, ret);

	return ret;
}
//EXPORT_SYMBOL(rtk_tr142_pon_wan_info_set);


int rtk_tr142_pon_wan_info_del(unsigned int wanIdx)
{
	pon_wan_info_list_t *node = NULL, *tmp = NULL;

	mutex_lock(&update_qos_lock);
	list_for_each_entry_safe(node, tmp, &(pon_wan_list.list), list)
	{
		if(node->info.wanIdx == wanIdx)
		{
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
			cleanup_tr142_timer(wanIdx);
#endif
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
			rtk_tr142_nl_send_msg(wanIdx);
#endif
			clear_pon_qos_conf_by_wan(node, 1);
			list_del(&node->list);
			kfree(node);
			mutex_unlock(&update_qos_lock);
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] wanIdx %d is deleted\n", wanIdx);
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
			rtk_tr142_smux_change_carrier(node->info.wanIdx, 0);
#endif
			return RTK_TR142_ERR_OK;
		}
	}
	mutex_unlock(&update_qos_lock);

	return RTK_TR142_ERR_WAN_IDX_NOT_FOUND;
}

/*
	For speed up TR-143 throughput test.
	Because TR-143 will disable RG & HWNAT.
	We need add a CF rule to match local out traffic by vlan tag to assign correct sid.
*/
#define HTTP_SPPEDUP_MAX_RECODRD 8
static int rtk_tr142_http_rg_idx[HTTP_SPPEDUP_MAX_RECODRD];
static int rtk_tr142_http_rule_cnt = 0;

static void rtk_tr142_http_speedup_init(void)
{
	int i;

	for(i = 0 ; i < HTTP_SPPEDUP_MAX_RECODRD ; i++)
		rtk_tr142_http_rg_idx[i] = -1;

	rtk_tr142_http_rule_cnt = 0;
}

static int rtk_tr142_set_http_speedup(int enable)
{
	pon_wan_info_list_t *node = NULL, *tmp = NULL;
#ifdef CONFIG_APOLLO_ROMEDRIVER
	rtk_rg_aclFilterAndQos_t acl, save_sw_acl;
#endif
	int ret, i;
	int rg_idx;

	//clear old settings
	for(i = 0 ; i < HTTP_SPPEDUP_MAX_RECODRD ; i++)
	{
		if(rtk_tr142_http_rg_idx[i] != -1)
        {
#ifdef CONFIG_APOLLO_ROMEDRIVER
			rtk_rg_aclFilterAndQos_del(rtk_tr142_http_rg_idx[i]);
#endif
        }
		rtk_tr142_http_rg_idx[i] = -1;
	}
	rtk_tr142_http_rule_cnt = 0;

	if(!enable)
		return 0;

	list_for_each_entry_safe(node, tmp, &(pon_wan_list.list), list)
	{
		if(node->info.wanType == SMUX_PROTO_BRIDGE)
			continue;

		if(node->defualt_acl_entry_idx == -1)
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
				"[tr142] %s wanIdx %d is not ready\n", __func__, node->info.wanIdx);
			return -1;
		}
#ifdef CONFIG_APOLLO_ROMEDRIVER
		memset(&save_sw_acl, 0, sizeof(rtk_rg_aclFilterAndQos_t));
		ret = omci_dmm_pon_wan_rule_xlate(&(node->info), OMCI_PON_WAN_RULE_POS_RG_US_SW_ACL, &save_sw_acl);
		if (ret != 0)
		{
        		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
                		"[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
		}
		if(save_sw_acl.fwding_type_and_direction != 0)
			rg_idx = node->defualt_swacl_entry_idx;
		else
			rg_idx = node->defualt_acl_entry_idx;

		ret = rtk_rg_aclFilterAndQos_find(&acl, &rg_idx);
		if(ret != 0)
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
				"[tr142] %s rtk_rg_aclFilterAndQos_get fail! rg_idx=%d, ret = %d\n", __func__, rg_idx, ret);
			return -1;
		}

		acl.filter_fields |= EGRESS_CTAG_VID_BIT;

		acl.egress_ctag_vid = acl.action_acl_cvlan.assignedCvid;
		acl.egress_intf_idx = 0;

		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] %s fields=%llx, vid=%d, wan=%d\n", __func__, acl.filter_fields, acl.egress_ctag_vid, node->info.wanIdx);

		ret = rtk_rg_aclFilterAndQos_add(&acl, &rtk_tr142_http_rg_idx[rtk_tr142_http_rule_cnt]);
		if(ret != 0)
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
				"[tr142] %s rtk_rg_aclFilterAndQos_add fail! ret = %d\n", __func__, ret);
			return -1;
		}
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] %s added stream id for wanIdx %d, vid %d\n", __func__, node->info.wanIdx, acl.egress_ctag_vid);
#else
        //TBD FC driver
#endif
		rtk_tr142_http_rule_cnt++;
	}
	TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] %s total %d rules added\n", __func__, rtk_tr142_http_rule_cnt);

	return 0;
}


/* For PPPoE emulation whose vlan has no veip setup by OMCI */
typedef struct pppoe_emu_helper_list_s
{
	rtk_tr142_pppoe_emu_info_t info;
	int acl_idx;
	struct list_head list;
} pppoe_emu_helper_list_t;
pppoe_emu_helper_list_t helper_list;

static void rtk_tr142_pppoe_emu_helper_init(void)
{
	helper_list.info.rg_wan_index = -1;
	helper_list.info.vid = -1;
	helper_list.acl_idx = -1;
	INIT_LIST_HEAD(&helper_list.list);
}


int rtk_tr142_set_pppoe_emu_helper(rtk_tr142_pppoe_emu_info_t *info)
{
	pppoe_emu_helper_list_t *node = NULL, *tmp = NULL;
	pon_wan_info_list_t *wan_node = NULL, *wan_tmp = NULL, *first_wan = NULL;

#ifdef CONFIG_APOLLO_ROMEDRIVER
	rtk_rg_aclFilterAndQos_t acl, save_sw_acl;
#endif
	int rg_idx = -1, ret = -1;

	// Check we need to add a new one, or update existed one
	list_for_each_entry_safe(node, tmp, &(helper_list.list), list)
	{
		if(node->info.rg_wan_index == info->rg_wan_index)
		{
			// Clear old rule
			if(node->acl_idx != -1)
            {
#ifdef CONFIG_APOLLO_ROMEDRIVER
				rtk_rg_aclFilterAndQos_del(node->acl_idx);
#endif
            }
			node->acl_idx = -1;

			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] %s Updating PPPoE emulation helper wanIdx %d\n", __func__, info->rg_wan_index);
			break;
		}
	}

	if(node == NULL)
	{
		// Not found, add one
		node = kmalloc(sizeof(pppoe_emu_helper_list_t), GFP_KERNEL);
		if(node == NULL)
			return -1;

		list_add_tail(&(node->list), &(helper_list.list));
	}

	memcpy(&node->info, info, sizeof(rtk_tr142_pppoe_emu_info_t));

	list_for_each_entry_safe(wan_node, wan_tmp, &(pon_wan_list.list), list)
	{
		// data path is OK
		if(wan_node->info.wanIdx == info->rg_wan_index)
			return 0;

		if(first_wan == NULL && wan_node->defualt_acl_entry_idx != -1)
			first_wan = wan_node;
	}

	if(first_wan == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] %s no valid wan available for wan_index %d\n", __func__, info->rg_wan_index);
		return -1;
	}
#ifdef CONFIG_APOLLO_ROMEDRIVER
	memset(&save_sw_acl, 0, sizeof(rtk_rg_aclFilterAndQos_t));
	ret = omci_dmm_pon_wan_rule_xlate(&(first_wan->info), OMCI_PON_WAN_RULE_POS_RG_US_SW_ACL, &save_sw_acl);
	if (ret != 0)
	{
        	TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
        		"[tr142] omci_dmm_pon_wan_rule_xlate returns %d\n", ret);
	}
	if(save_sw_acl.fwding_type_and_direction != 0)
		rg_idx = first_wan->defualt_acl_entry_idx;
	else
		rg_idx = first_wan->defualt_swacl_entry_idx;

	ret = rtk_rg_aclFilterAndQos_find(&acl, &rg_idx);
	if(ret != 0)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] %s rtk_rg_aclFilterAndQos_get fail! rg_idx=%d, ret = %x\n", __func__, rg_idx, ret);
		return -1;
	}

	acl.filter_fields |= EGRESS_CTAG_VID_BIT;
	acl.egress_ctag_vid = info->vid;
	acl.egress_intf_idx = info->rg_wan_index;

	acl.qos_actions |= ACL_ACTION_ACL_CVLANTAG_BIT;
	acl.action_acl_cvlan.assignedCvid = info->vid;

	TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
			"[tr142] %s fields=%llx, action = %x, vid=%d, wan=%d\n", __func__, acl.filter_fields, acl.qos_actions, info->vid, info->rg_wan_index);

	ret = rtk_rg_aclFilterAndQos_add(&acl, &node->acl_idx);
	if(ret != 0)
	{
		TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
			"[tr142] %s rtk_rg_aclFilterAndQos_add fail! ret = %d\n", __func__, ret);
		return -1;
	}
#else
    // TBD FC driver
#endif
	TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
			"[tr142] %s added stream id for wanIdx %d, vid %d to acl entry %d\n", __func__, info->rg_wan_index, info->vid, node->acl_idx);

	return 0;
}

int rtk_tr142_del_pppoe_emu_helper(int wan_index)
{
	pppoe_emu_helper_list_t *node = NULL, *tmp = NULL;

	// Check we need to add a new one, or update existed one
	list_for_each_entry_safe(node, tmp, &(helper_list.list), list)
	{
		if(node->info.rg_wan_index == wan_index)
		{
			// Clear rule
			if(node->acl_idx != -1)
            {
#ifdef CONFIG_APOLLO_ROMEDRIVER
				rtk_rg_aclFilterAndQos_del(node->acl_idx);
#endif
            }

			list_del(&node->list);
			kfree(node);

			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] %s PPPoE emulation helper wanIdx %d Deleted\n", __func__, wan_index);
			break;
		}
	}

	return 0;
}

static long rtk_tr142_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
	case RTK_TR142_IOCTL_GET_QOS_QUEUES:
		copy_to_user((char *)arg, (const void *)&queues, sizeof(rtk_tr142_qos_queues_t));
		break;

	case RTK_TR142_IOCTL_SET_QOS_QUEUES:
		{
			int i;
			mutex_lock(&update_qos_lock);
			copy_from_user((void *)&queues, (char *)arg, sizeof(rtk_tr142_qos_queues_t));
			for(i = 0 ; i<queue_num ; i++)
			{
#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
					"[tr142] queue %d: enable = %d, policy=%s, weight = %d pir = %d\n",
					i, queues.queue[i].enable,
					(queues.queue[i].type == WFQ_WRR_PRIORITY) ? "WRR" : "STRICT",
					queues.queue[i].weight,queues.queue[i].pir);
#else
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
					"[tr142] queue %d: enable = %d, policy=%s, weight = %d\n",
					i, queues.queue[i].enable,
					(queues.queue[i].type == WFQ_WRR_PRIORITY) ? "WRR" : "STRICT",
					queues.queue[i].weight);
#endif
			}

			if(queue_num!=0)
			{
				is_queues_ready = is_qos_enable();
				update_pon_qos();
			}
			else
			{
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
					"[tr142] Wan queue num is 0. Please set wan queue num first!\n");
			}
			mutex_unlock(&update_qos_lock);
		}
		break;
	case RTK_TR142_IOCTL_SET_HTTP_SPEEDUP:
		{
			int enable;

			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] got ioctl RTK_TR142_IOCTL_SET_HTTP_SPEEDUP\n");

			mutex_lock(&update_qos_lock);
			copy_from_user((void *)&enable, (char *)arg, sizeof(int));

			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] http speedup set: enable = %d\n", enable);
			rtk_tr142_set_http_speedup(enable);
			mutex_unlock(&update_qos_lock);
		}
		break;
	case RTK_TR142_IOCTL_SET_PPPOE_EMU_HELPER:
		{
			rtk_tr142_pppoe_emu_info_t info;

			mutex_lock(&update_qos_lock);
			copy_from_user((void *)&info, (char *)arg, sizeof(rtk_tr142_pppoe_emu_info_t));

			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] pppoe emulation helper set: wan_index = %d, vid = %d\n", info.rg_wan_index, info.vid);
			rtk_tr142_set_pppoe_emu_helper(&info);
			mutex_unlock(&update_qos_lock);
		}
		break;
	case RTK_TR142_IOCTL_DEL_PPPOE_EMU_HELPER:
		{
			int wan_idx;

			copy_from_user((void *)&wan_idx, (char *)arg, sizeof(int));

			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_INFO,
				"[tr142] pppoe emulation helper delete: wan_index = %d\n", wan_idx);
			rtk_tr142_del_pppoe_emu_helper(wan_idx);
		}
		break;
	case RTK_TR142_IOCTL_SET_DSIABLE_HWNAT_PATCH:
		{
			int enable;

			copy_from_user((void *)&enable, (char *)arg, sizeof(int));
			is_hwnat_disabled = enable;

			mutex_lock(&update_qos_lock);
			update_pon_qos();
			mutex_unlock(&update_qos_lock);
		}
		break;
	case RTK_TR142_IOCTL_SET_WAN_QUEUE_NUM:
		{
			unsigned char num;
			mutex_lock(&update_qos_lock);
			copy_from_user((void *)&num, (char *)arg, sizeof(char));
			if(num > WAN_PONMAC_QUEUE_MAX)
			{
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
					"[tr142] Set wan queue num fail! Queue num %d exceed max wan queue num %d!\n", num, WAN_PONMAC_QUEUE_MAX);
			}
			else
			{
				queue_num = num;
				TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
					"[tr142] Set wan queue num:%d\n",queue_num);
			}
			mutex_unlock(&update_qos_lock);
		}
		break;
	case RTK_TR142_IOCTL_SET_DOT1P_VALUE_BYCF:
		{
			unsigned char dot1pValueByCF;

			copy_from_user((void *)&dot1pValueByCF, (char *)arg, sizeof(char));
			is_dot1p_value_byCF = dot1pValueByCF;
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_DEBUG,
				"[tr142] Set dot1p value by CF:%d\n",dot1pValueByCF);

			mutex_lock(&update_qos_lock);
			update_pon_qos();
			mutex_unlock(&update_qos_lock);
		}
		break;	
	default:
		return -ENOTTY;
	}
	return 0;
}

static int proc_log_module_read(struct seq_file *seq, void *v)
{
	seq_printf(seq, "Realtek TR142 Log Modules: %d\n\n", g_rtk_tr142_ctrl.log_module);

	seq_printf(seq, "None: %d\n", TR142_LOG_MOD_NONE);
	seq_printf(seq, "QoS: %d\n", TR142_LOG_MOD_QOS);
	seq_printf(seq, "Multicast: %d\n", TR142_LOG_MOD_MCAST);
	seq_printf(seq, "ALL: %d\n", TR142_LOG_MOD_ALL);

	return 0;
}

static int proc_log_module_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_log_module_read, inode->i_private);
}

static int proc_log_module_write(struct file *filp, const char *buf, size_t count, loff_t *offp)
{
	char tmpbuf[64] = {0};
	int module;

	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		module = simple_strtol(tmpbuf, NULL, 10);

		g_rtk_tr142_ctrl.log_module = module;
	}

	return count;
}

static int proc_log_level_read(struct seq_file *seq, void *v)
{
	seq_printf(seq, "Realtek TR142 Log Level: %d\n\n", g_rtk_tr142_ctrl.log_level);

	seq_printf(seq, "Accept values: %d ~ %d\n", TR142_LOG_LEVEL_OFF, TR142_LOG_LEVEL_DEBUG);

	return 0;
}

static int proc_log_level_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_log_level_read, inode->i_private);
}

static int proc_log_level_write(struct file *filp, const char *buf, size_t count, loff_t *offp)
{
	char tmpbuf[64] = {0};
	int level;

	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		level = simple_strtol(tmpbuf, NULL, 10);
		if(level < TR142_LOG_LEVEL_OFF || level > TR142_LOG_LEVEL_DEBUG)
		{
			TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
				"only accept %d ~ %d!\n", TR142_LOG_LEVEL_OFF, TR142_LOG_LEVEL_DEBUG);
			return -EFAULT;
		}

		g_rtk_tr142_ctrl.log_level = level;
	}

	return count;
}

static int proc_hwnat_disabled_read(struct seq_file *seq, void *v)
{
	seq_printf(seq, "hwnat_disabled=%d\n", is_hwnat_disabled);

	return 0;
}

static int proc_hwnat_disabled_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_hwnat_disabled_read, inode->i_private);
}

static int proc_hwnat_disabled_write(struct file *filp, const char *buf, size_t count, loff_t *offp)
{
	char tmpbuf[64] = {0};
	int old = is_hwnat_disabled;

	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		is_hwnat_disabled = simple_strtol(tmpbuf, NULL, 10);
		if(old != is_hwnat_disabled)
		{
			mutex_lock(&update_qos_lock);
			update_pon_qos();
			mutex_unlock(&update_qos_lock);
		}
	}

	return count;
}


static int proc_wan_info_read(struct seq_file *seq, void *v)
{
	pon_wan_info_list_t *node = NULL, *tmp = NULL;
	int i;
	int cnt = 0;

	seq_printf(seq, "Realtek TR142 Colleted WAN Info:\n");

	list_for_each_entry_safe(node, tmp, &(pon_wan_list.list), list)
	{
		seq_printf(seq, "WAN[%d]:\n", cnt);
		seq_printf(seq, "\twanIdx = %d\n", node->info.wanIdx);
		seq_printf(seq, "\twanType = %d\n", node->info.wanType);
		seq_printf(seq, "\tgemPortId = %d\n", node->info.gemPortId);
		seq_printf(seq, "\ttcontId = %d\n", node->info.tcontId);

		seq_printf(seq, "\tusFlowId =");
		for(i = 0 ; i < queue_num; i++)
			seq_printf(seq, " %d", node->info.usFlowId[i]);
		seq_printf(seq, "\n");

		seq_printf(seq, "\tqueueSts =");
		for(i = 0 ; i < queue_num; i++)
			seq_printf(seq, " %d", node->info.queueSts[i]);
		seq_printf(seq, "\n");

		seq_printf(seq, "\tdefualt_acl_entry_idx = %d, %d\n", node->defualt_acl_entry_idx, node->defualt_swacl_entry_idx);

		seq_printf(seq, "\taclEntryIdx =");
		for(i = 0; i < WAN_PONMAC_QUEUE_MAX; i++){		
			seq_printf(seq, " %d", node->acl_entry_idx[i]);
		}		
		seq_printf(seq, "\n");

		seq_printf(seq, "\tswAclEntryIdx =");
		for(i = 0; i < WAN_PONMAC_QUEUE_MAX; i++){		
			seq_printf(seq, " %d", node->swacl_entry_idx[i]);
		}		
		seq_printf(seq, "\n");

		cnt++;
	}

	return 0;
}

static int proc_wan_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_wan_info_read, inode->i_private);
}

static int proc_queue_map_read(struct seq_file *seq, void *v)
{
	pon_wan_info_list_t *node = NULL, *tmp = NULL;
	int i;

	list_for_each_entry_safe(node, tmp, &(pon_wan_list.list), list)
	{
		seq_printf(seq, "WAN Index %d:\n", node->info.wanIdx);
		if(is_queues_ready)
		{
			seq_printf(seq, "User Queue\tQueue Type\tStreamId\tGPON Queue\n");
			for(i = 0 ; i < queue_num ; i++)
				seq_printf(seq, "Q%d(%s)\t%s\t\t%d\t\t%d\n", i+1,
				(queues.queue[i].enable) ? "Enabled" : "Disabled",
				(queues.queue[i].type == WFQ_WRR_PRIORITY) ? "WRR" : "STRICT",
				node->info.usFlowId[node->queue2ponqIdx[i]], node->info.queueSts[node->queue2ponqIdx[i]]);
			seq_printf(seq, "\n");
		}
		else
		{
			seq_printf(seq, "QoS is disabled, default queue: %d, streamId = %d\n\n"
				, node->info.queueSts[node->queue2ponqIdx[0]]
				, node->info.usFlowId[node->queue2ponqIdx[0]]);
		}
	}

	return 0;
}

static int proc_queue_map_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_queue_map_read, inode->i_private);
}

static int proc_wan_idx_map_fops_read(struct seq_file *seq, void *v)
{
	pon_wan_map_list_t *node = NULL, *tmp = NULL;

	list_for_each_entry_safe(node, tmp, &(pon_wan_map_list.list), list)
	{
		seq_printf(seq, "%5d %16s %16s %5d\n", node->wan_idx, node->rif, node->vif, node->port_bind);
	}
	
	return 0;
}
static int proc_wan_idx_map_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_wan_idx_map_fops_read, inode->i_private);
}

static pon_wan_map_list_t* find_wan_map(int idx)
{
	pon_wan_map_list_t *node = NULL, *tmp = NULL;

	list_for_each_entry_safe(node, tmp, &(pon_wan_map_list.list), list)
	{
		if(node->wan_idx == idx)
			return node;
	}
	return NULL;
}

#define xlen(x)  # x
#define xLen(x)  xlen(x)
static int proc_wan_idx_map_fops_write(struct file *filp, const char *buf, size_t count, loff_t *offp)
{
	int wan_idx, port_bind = 0;
	char rif[IFNAMSIZ+1], vif[IFNAMSIZ+1];
	pon_wan_map_list_t *node = NULL, *tmp = NULL;
	char tmpbuf[64] = {0};
	
	if(buf && count < (sizeof(tmpbuf)-1) && 
		!copy_from_user(tmpbuf, buf, count))
	{
		if(!strncmp(tmpbuf, "clear", 5) && (tmpbuf[5]=='\n' || tmpbuf[5]=='\r' || tmpbuf[5]=='\0'))
		{
			mutex_lock(&update_qos_lock);
			list_for_each_entry_safe(node, tmp, &(pon_wan_map_list.list), list)
			{
				list_del(&node->list);
				kfree(node);
			}
			mutex_unlock(&update_qos_lock);
		}
		else if(sscanf(tmpbuf, "clear %d", &wan_idx) == 1)
		{
			mutex_lock(&update_qos_lock);
			node = find_wan_map(wan_idx);
			if(node != NULL)
			{
				list_del(&node->list);
				kfree(node);
			}
			mutex_unlock(&update_qos_lock);
		}
		else if(sscanf(tmpbuf, "%d %"xLen(IFNAMSIZ)"s %"xLen(IFNAMSIZ)"s %d", &wan_idx, rif, vif, &port_bind) >= 3)
		{
			mutex_lock(&update_qos_lock);
			node = find_wan_map(wan_idx);
			
			if(!node)
			{
				node = kmalloc(sizeof(pon_wan_map_list_t), GFP_KERNEL);
				memset(node, 0, sizeof(pon_wan_map_list_t));
				if(node) list_add_tail(&(node->list), &(pon_wan_map_list.list));
			}
			if(node != NULL)
			{
				node->wan_idx = wan_idx;
				strcpy(node->rif, rif);
				strcpy(node->vif, vif);
				node->port_bind = port_bind;
			}
			mutex_unlock(&update_qos_lock);
		}
		else{
			printk("error!!!!\n");
		}
	}

	return count;
}

#if defined(CONFIG_RTL_MULTI_ETH_WAN)
static int proc_tr142_omci_recover_wan_timeout_read(struct seq_file *seq, void *v)
{
	seq_printf(seq, "tr142_omci_recover_wan_timeout=%d\n", tr142_omci_recover_wan_timeout);

	return 0;
}

static int proc_tr142_omci_recover_wan_timeout_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_tr142_omci_recover_wan_timeout_read, inode->i_private);
}

static int proc_tr142_omci_recover_wan_timeout_write(struct file *filp, const char *buf, size_t count, loff_t *offp)
{
	char tmpbuf[64] = {0};
	int old = tr142_omci_recover_wan_timeout;

	if (buf && !copy_from_user(tmpbuf, buf, count))
		tr142_omci_recover_wan_timeout = simple_strtol(tmpbuf, NULL, 10);

	return count;
}
#endif

static struct proc_dir_entry *procfs = NULL;

static const struct file_operations log_module_fops = {
        .owner          = THIS_MODULE,
        .open           = proc_log_module_open,
        .read           = seq_read,
        .write          = proc_log_module_write,
        .llseek         = seq_lseek,
        .release        = single_release,
};

static const struct file_operations log_level_fops = {
        .owner          = THIS_MODULE,
        .open           = proc_log_level_open,
        .read           = seq_read,
        .write          = proc_log_level_write,
        .llseek         = seq_lseek,
        .release        = single_release,
};

static const struct file_operations hwnat_disabled_fops = {
        .owner          = THIS_MODULE,
        .open           = proc_hwnat_disabled_open,
        .read           = seq_read,
        .write          = proc_hwnat_disabled_write,
        .llseek         = seq_lseek,
        .release        = single_release,
};

static const struct file_operations wan_info_fops = {
        .owner          = THIS_MODULE,
        .open           = proc_wan_info_open,
        .read           = seq_read,
        .write          = NULL,
        .llseek         = seq_lseek,
        .release        = single_release,
};

static const struct file_operations queue_map_fops = {
        .owner          = THIS_MODULE,
        .open           = proc_queue_map_open,
        .read           = seq_read,
        .write          = NULL,
        .llseek         = seq_lseek,
        .release        = single_release,
};

static const struct file_operations wan_idx_map_fops = {
        .owner          = THIS_MODULE,
        .open           = proc_wan_idx_map_fops_open,
        .read           = seq_read,
        .write          = proc_wan_idx_map_fops_write,
        .llseek         = seq_lseek,
        .release        = single_release,
};

#if defined(CONFIG_RTL_MULTI_ETH_WAN)
static const struct file_operations tr142_omci_recover_wan_timeout_fops = {
        .owner          = THIS_MODULE,
        .open           = proc_tr142_omci_recover_wan_timeout_open,
        .read           = seq_read,
        .write          = proc_tr142_omci_recover_wan_timeout_write,
        .llseek         = seq_lseek,
        .release        = single_release,
};
#endif

static int rtk_tr142_init_procfs(void)
{
	struct proc_dir_entry *entry = NULL;

	/* create a directory */
	procfs = proc_mkdir(rtk_tr142_dev_name, NULL);
	if(procfs == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
			"Register /proc/%s failed\n", rtk_tr142_dev_name);
		return -ENOMEM;
	}

	entry = proc_create("log_module", 0644, procfs, &log_module_fops);
	if (entry == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
			"Register /proc/%s/log_module failed\n", rtk_tr142_dev_name);
		remove_proc_entry(rtk_tr142_dev_name, NULL);
		return -ENOMEM;
	}

	entry = proc_create("log_level", 0644, procfs, &log_level_fops);
	if (entry == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
			"Register /proc/%s/log_level failed\n", rtk_tr142_dev_name);
		remove_proc_entry(rtk_tr142_dev_name, NULL);
		return -ENOMEM;
	}

	entry = proc_create("hwnat_disabled", 0644, procfs, &hwnat_disabled_fops);
	if (entry == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
			"Register /proc/%s/hwnat_disabled failed\n", rtk_tr142_dev_name);
		remove_proc_entry(rtk_tr142_dev_name, NULL);
		return -ENOMEM;
	}

	entry = proc_create("wan_info",  0444, procfs, &wan_info_fops);
	if (entry == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
			"Register /proc/%s/wan_info failed\n", rtk_tr142_dev_name);
		remove_proc_entry(rtk_tr142_dev_name, NULL);
		return -ENOMEM;
	}

	entry = proc_create("queue_map",  0444, procfs, &queue_map_fops);
	if (entry == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
			"Register /proc/%s/queue_map failed\n", rtk_tr142_dev_name);
		remove_proc_entry(rtk_tr142_dev_name, NULL);
		return -ENOMEM;
	}

	entry = proc_create("wan_idx_map",  0444, procfs, &wan_idx_map_fops);
	if (entry == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
			"Register /proc/%s/wan_idx_map failed\n", rtk_tr142_dev_name);
		remove_proc_entry(rtk_tr142_dev_name, NULL);
		return -ENOMEM;
	}
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
	entry = proc_create("tr142_omci_recover_wan_timeout",  0644, procfs, &tr142_omci_recover_wan_timeout_fops);
	if (entry == NULL)
	{
		TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
			"Register /proc/%s/tr142_omci_recover_wan_timeout failed\n", rtk_tr142_dev_name);
		remove_proc_entry(rtk_tr142_dev_name, NULL);
		return -ENOMEM;
	}
#endif
	return 0;
}

#if defined(CONFIG_RTL_MULTI_ETH_WAN)
int rtk_tr142_smux_change_carrier(int wanIdx, int carrier)
{
	struct smux_args sargs;
	pon_wan_map_list_t *map = find_wan_map(wanIdx);
	if(map)
	{
					
		memset(&sargs, 0, sizeof(struct smux_args));
		//sargs.args.cmd = GET_SMUX_CMD;
		strcpy(sargs.args.ifname, map->rif);
		strcpy(sargs.args.u.ifname, map->vif);
		//if(get_smux_device_info(&sargs) == 0)
		{
			sargs.args.cmd = SET_SMUX_CMD;
			sargs.args.carrier = carrier;
			if(set_smux_device_info(&sargs) != 0)
			{
				TR142_LOG(TR142_LOG_MOD_NONE, TR142_LOG_LEVEL_ERROR,
					"[tr142] Fail to set smux information for interface %s\n", map->vif);
			}
		}
		/*else
		{
			TR142_LOG(TR142_LOG_MOD_QOS, TR142_LOG_LEVEL_ERROR,
					"[tr142] Fail to get smux information for interface %s\n", map->vif);
		}*/
		return 1;
	}
	else{
		TR142_LOG(TR142_LOG_MOD_NONE, TR142_LOG_LEVEL_ERROR,
			"[tr142] Fail to get map for idx %d\n", wanIdx);
	}
	return 0;
}
#endif

#ifdef CONFIG_RTL8686_SWITCH
struct netif_carrier_dev_mapping
{
	unsigned char 	ifname[IFNAMSIZ];
	struct net_device *phy_dev;
	unsigned char status; // 0 : disable , 1 : enable
	int linkchangetimes;
};

static int pon_onu_state=0;
static void rtk_tr142_pon_wan_status_chanege(intrBcasterMsg_t *pMsgData)
{
#if 0	
	printk("[%s] intrType	= %d\n", __FUNCTION__, pMsgData->intrType);
	printk("[%s] intrSubType	= %u\n", __FUNCTION__, pMsgData->intrSubType);
	printk("[%s] intrBitMask	= %u\n", __FUNCTION__, pMsgData->intrBitMask);
	printk("[%s] intrStatus	= %d\n", __FUNCTION__, pMsgData->intrStatus);
#endif
	pon_wan_info_list_t *wan_node = NULL, *wan_tmp = NULL, *first_wan = NULL;
	int i=0;

	if(pMsgData->intrType == MSG_TYPE_ONU_STATE) // ONU state Change event
	{
		if(pMsgData->intrSubType == GPON_STATE_O5) // smux up
		{
			pon_onu_state = 1;
			list_for_each_entry_safe(wan_node, wan_tmp, &(pon_wan_list.list), list)
			{
				if(wan_node->defualt_acl_entry_idx != -1)
				{
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
					if(tr142_omci_recover_wan_timeout==0)
						rtk_tr142_smux_change_carrier(wan_node->info.wanIdx, 1);
					else
						init_tr142_timer(wan_node->info.wanIdx);
#endif
				}
			}
		}
		else if (pMsgData->intrSubType != GPON_STATE_O5) //smux down
		{
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
			if(pon_onu_state){
				for(i=0; i<MAX_WAN_NUMBER; i++)
					cleanup_tr142_timer(i);
			}
#endif
			pon_onu_state = 0;
		}
	}
}

// Handle Link Change Interrupt for Netlink
static intrBcasterNotifier_t GMAConuStateChange142Notifier = {
	.notifyType = MSG_TYPE_ONU_STATE,
	.notifierCb = rtk_tr142_pon_wan_status_chanege,
};

#endif

static struct file_operations rtk_tr142_fops =
{
	.unlocked_ioctl = rtk_tr142_ioctl,
};

static int __init rtk_tr142_module_init(void)
{
	int err;

#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	rtk_tr142_nl_init();
#endif
	//TBD default value for log, should be configurable
	g_rtk_tr142_ctrl.log_module = TR142_LOG_MOD_ALL;
	g_rtk_tr142_ctrl.log_level = TR142_LOG_LEVEL_INFO;

#ifdef CONFIG_LUNA_G3_SERIES
	pf_ca8279_gpon_scheInfo_get(&scheInfo);
#else
	rtk_gpon_scheInfo_get(&scheInfo);
#endif

	//create device node for ioctl
	err = alloc_chrdev_region(&rtk_tr142_dev, 0, 1, rtk_tr142_dev_name);
	if (err < 0)
	{
	        TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_ERROR,
				"alloc_chrdev_region() failed. err = %d\n", err);
	        return -1;
	}
	major = MAJOR(rtk_tr142_dev);

	rtk_tr142_class = class_create(THIS_MODULE, rtk_tr142_dev_name);
    if (rtk_tr142_class == NULL)
	{
		unregister_chrdev_region(rtk_tr142_dev, 1);
		return -1;
    }

	if (device_create(rtk_tr142_class, NULL, rtk_tr142_dev, NULL, rtk_tr142_dev_name) == NULL)
	{
		class_destroy(rtk_tr142_class);
		unregister_chrdev_region(rtk_tr142_dev, 1);
		return -1;
	}

	cdev_init(&rtk_tr142_cdev, &rtk_tr142_fops);

	if(cdev_add(&rtk_tr142_cdev, rtk_tr142_dev, 1) == -1)
	{
		device_destroy(rtk_tr142_class, rtk_tr142_dev);
		class_destroy(rtk_tr142_class);
		unregister_chrdev_region(rtk_tr142_dev, 1);
		return -1;
	}

	// Initialize data structures
	memset((void *)&queues, 0, sizeof(rtk_tr142_qos_queues_t));
	memset(&pon_wan_list, 0, sizeof(pon_wan_info_list_t));
	INIT_LIST_HEAD(&pon_wan_list.list);
	mutex_init(&update_qos_lock);

	memset(&pon_wan_map_list, 0, sizeof(pon_wan_map_list_t));
	INIT_LIST_HEAD(&pon_wan_map_list.list);
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
	memset(tr142_timer, 0, MAX_WAN_NUMBER*sizeof(struct timer_list));
#endif

	err = rtk_tr142_init_procfs();

	// HTTP speedup
	rtk_tr142_http_speedup_init();

	// PPPoE emulation helper
	rtk_tr142_pppoe_emu_helper_init();

	// Register OMCI Dual Management API
	omci_cb.omci_dm_pon_wan_info_set = rtk_tr142_pon_wan_info_set;
	omci_cb.omci_dm_pon_wan_info_del = rtk_tr142_pon_wan_info_del;

	omci_dmm_cb_register(&omci_cb);

#ifdef CONFIG_RTL8686_SWITCH
	//Register OMCI link state change API
	if(intr_bcaster_notifier_cb_register(&GMAConuStateChange142Notifier) != 0)
		printk("Interrupt Broadcaster for onu state Change Error !! \n");
#endif

	TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_INFO,
		"Realtek TR-142 Module initialized. err = %d, max_ponq_id=%d\n", err, WAN_PONMAC_QUEUE_ID_MAX);
	return err;
}

static void __exit rtk_tr142_module_exit(void)
{
	pon_wan_info_list_t *node, *tmp = NULL;
	pon_wan_map_list_t *node1 = NULL, *tmp1 = NULL;
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
	int i;
#endif

#if defined(CONFIG_CMCC) || defined(CONFIG_CU)
	rtk_tr142_nl_exit();
#endif

	// remove /dev/rtk_tr142
	cdev_del(&rtk_tr142_cdev);
	device_destroy(rtk_tr142_class, rtk_tr142_dev);
	class_destroy(rtk_tr142_class);
	unregister_chrdev_region(rtk_tr142_dev, 1);

	// remove proc entries
	remove_proc_entry("wan_idx_map", procfs);
	list_for_each_entry_safe(node1, tmp1, &(pon_wan_map_list.list), list)
	{
		list_del(&node1->list);
		kfree(node1);
	}
#if defined(CONFIG_RTL_MULTI_ETH_WAN)
	for(i=0; i<MAX_WAN_NUMBER; i++)
		cleanup_tr142_timer(i);
#endif

	remove_proc_entry("log_module", procfs);
	remove_proc_entry("log_level", procfs);
	remove_proc_entry("hwnat_disabled", procfs);
	remove_proc_entry("wan_info", procfs);
	remove_proc_entry("queue_map", procfs);
	remove_proc_entry(rtk_tr142_dev_name, NULL);

	//unregister OMCI DM callback functions
	omci_dmm_cb_unregister();

	// free resources
	mutex_lock(&update_qos_lock);
	list_for_each_entry_safe(node, tmp, &(pon_wan_list.list), list)
	{
		clear_pon_qos_conf_by_wan(node, 1);
		list_del(&node->list);
		kfree(node);
	}
	mutex_unlock(&update_qos_lock);
	mutex_destroy(&update_qos_lock);

	TR142_LOG(TR142_LOG_MOD_ALL, TR142_LOG_LEVEL_INFO,
		"Realtek TR-142 Module unregistered.\n");
}

module_init(rtk_tr142_module_init);
module_exit(rtk_tr142_module_exit);
MODULE_LICENSE("GPL");

