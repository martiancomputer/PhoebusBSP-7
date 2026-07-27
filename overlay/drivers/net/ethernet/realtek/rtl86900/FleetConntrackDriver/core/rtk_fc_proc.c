/*
 * Copyright (C) 2016 Realtek Semiconductor Corp.
 * All Rights Reserved.
 *
 * This program is the proprietary software of Realtek Semiconductor
 * Corporation and/or its licensors, and only be used, duplicated,
 * modified or distributed under the authorized license from Realtek.
 *
 * ANY USE OF THE SOFTWARE OTHER THAN AS AUTHORIZED UNDER
 * THIS LICENSE OR COPYRIGHT LAW IS PROHIBITED.
*/

#include <br_private.h>
#include <linux/proc_fs.h>

//#include "../../../../net/bridge/br_private.h"

#include "rtk_fc_internal.h"
#include "rtk_fc_proc.h"
#include "rtk_fc_callback.h"
#include "rtk_fc_multicast.h"
#include "rtk_fc_mappingAPI.h"
#include "rtk_fc_external.h"
#include "rtk_fc_mgr.h"
#include "rtk_fc_acl.h"

#include <linux/vmalloc.h>
#include "rtk_fc_vxlan.h"
#include "rtk_fc_ipsec.h"

#if defined(CONFIG_RTK_L34_G3_PLATFORM) && !defined(CONFIG_FC_CA8277AB_SERIES)
//#define CONFIG_FC_HASH_TEST
#include <aal_l2_vlan.h>
#include <aal_l3_glb.h>
#include <aal_l2_specpkt.h>
#endif

#if defined(CONFIG_FC_HASH_TEST)

uint8 tmp_flowTbl[RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE];
uint16 tmp_flowTbl_bucket[RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE];

#endif

#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#include "aal_ldma.h"
//#include <linux/ptool.h>
#endif

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
#include "rtk_rg_apolloPro_internal.h"
#endif

#if defined(CONFIG_RTK_SOC_RTL8198D)
#include "rtk_fc_helper_multicast.h"
#endif

#if (defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) || defined(CONFIG_RTK_FC_CRYPTO_OFFLOAD_BY_PE)) && defined(CONFIG_REALTEK_IPC2RCPU)
#include "ca_ipc_pe.h"
#endif

#if defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#include <aal_l3_specpkt.h>
#include <aal_l2_tm_cb.h>
#endif
#if defined(CONFIG_RTL8198XB_SERIES) || defined(CONFIG_RTL8198E_SERIES)
#include <aal_l2_cls.h>
#include <aal_l2_classification.h>
#endif
#endif

#if defined(CONFIG_FC_RTL9607C_SERIES)
extern uint32 dynamic_sram_desc;
#endif

#if defined(CONFIG_RTK_L34_G3_PLATFORM)
extern uint32_t asic_debug;
#endif

char *name_of_dispatch_mode[]={
	"Disable mode",
	"IPI dispatch mode",
	"RPS mode",
	"RSS mode (hash)",
	"RSS mode (RR)",
	"RSS mode (DIP hash)",
	"SMP mode (ingress port)"
	"SPECIAL mode (WAN rps, lan port rps)"
};

char *name_of_hwnat_mode[RT_FLOW_HWNAT_MODE_END]={
	"Disable acceleration (Trap to ps)",
	"Enable acceleration (default)",
	"Disable h/w acceleration (sw acc. only)",
	"Disable s/w acceleration (hw acc. only)",
	"Redirect all packet to cpu (including unknown mc packet), and disable acceleration",
	"Disable upstream sw/hw acceleration (downstream acc only)",
};

char *name_of_smp_statistic_mode[FC_SMP_STATISTICS_DUMP_MODE_MAX]={
	"disabled",
	"show counter only",
	"show ipi queues state and counter",
};

char *name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_MAX]={
	"lan_queue_ability_enhance",
	"asic_driver_debug",
	"acl_lan_portmask",
	"hash_crc_debug",
	"tx_with_hdr_debug",
	"tx_busy_to_ps",
	"wifi_rx_hash",
	"wifi_rx_gmac_auto_sel",
	"wifi_tx_trap_hash",
	"wifi_tx_gmac_auto_sel",
	"pon_ds_p7_loopback",
	"pon_pm_cht_mix_mode",
	"shortcut_early_check",
	"hw_sw_hybrid_fwd",
	"hw_sw_hybrid_fwd_to_pe",
	"acl_para_dump",
	"event_rec_ctrl",
	"mc2uc_en",
	"bc_flowDelay_time_unit1ms",
	"wifi_multiband_acc",
	"ethport_trunking",
	"internal_Debug",
	"l2gre_tunnel_hwNetif_limit",		
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	"netifMib_timer",
	"ipsec_skip_ct",
	"pon_frag_size",
	"br_flow_mtu_check_en",
#endif
};

char *name_of_sw_shaperMib_update_mode[SW_SHAPER_UPDATE_END]={
	"     0: packet length mode, SW shaper and mib update by total packet length (include header length)",
	"     1: payload length mode, SW shaper and mib update by payload length (exclude L2/L3/L4 header length)",
	"     2: mixed mode, SW shaper by paylod, mib update by total packet length",
	"     3: mixed mode, SW shaper by total packet length, mib update by payload length",
};

#if defined(CONFIG_FC_RTL9607C_SERIES)
char *name_of_wifi_gmac_sel[RTK_FC_WIFI_GMAC_MAX]={
	"GMAC0",
	"GMAC1",
	"GMAC2",
	"RR",
	"TBD",
};
#endif

struct proc_dir_entry *rtk_fc_proc_dir=NULL;
struct proc_dir_entry *rtk_rg_sw_dump_proc_dir=NULL;
struct proc_dir_entry *rtk_rg_hw_dump_proc_dir=NULL;
struct proc_dir_entry *rtk_rg_trace_proc_dir=NULL;
struct proc_dir_entry *rtk_rg_ctrl_proc_dir=NULL;

typedef enum hashPaternType_e
{
	HASHPATERNTYPE_CPRI=0,
	HASHPATERNTYPE_DSCP,
	HASHPATERNTYPE_CVID,
	HASHPATERNTYPE_SVID,
	HASHPATERNTYPE_SPRI,
	HASHPATERNTYPE_VLAN_DEICFI,
	HASHPATERNTYPE_STREAMID,
	HASHPATERNTYPE_MAC,
	HASHPATERNTYPE_MAX
}hashPaternType_t;

rtk_fc_proc_t fcProc[]=
{

	// ==================== CTRL ====================
	{
		.name="global_ctrl",
		.get = rtk_fc_proc_global_ctrl_get,
		.set = rtk_fc_proc_global_ctrl_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="acl_multiple_hit_cfg",
		.get = rtk_fc_proc_acl_multiple_hit_cfg_get,
		.set = rtk_fc_proc_acl_multiple_hit_cfg_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="acl_redirect_open_ran",
		.get = rtk_fc_proc_acl_redirect_open_ran_get,
		.set = rtk_fc_proc_acl_redirect_open_ran_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="rstconntrack",
		.get = NULL,
		.set = rtk_fc_proc_rstconntrack,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="send_from_cpu" ,
		.get = _rtk_fc_proc_sendFromCpu_uasge ,
		.set = _rtk_fc_proc_sendFromCpu ,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="mc_set" ,
		.get = rtk_fc_proc_mc_set_get ,
		.set = rtk_fc_proc_mc_set_write ,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="umc_rate_limit" ,
		.get = rtk_fc_proc_umc_rate_limit_get ,
		.set = rtk_fc_proc_umc_rate_limit_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="dynamic_pe_for_tc" ,
		.get = rtk_fc_dynamic_pe_cfg_get ,
		.set = rtk_fc_dynamic_pe_cfg_write ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="mape_fmr_netif" ,
		.get = rtk_fc_proc_mapeNetifFmr_get ,
		.set = rtk_fc_proc_mapeNetifFmr_write ,
		.dir = PROC_DIR_CTRL,
	},	
	{
		.name="mapt_fmr_netif" ,
		.get = rtk_fc_proc_mapeNetifFmr_get ,
		.set = rtk_fc_proc_mapeNetifFmr_write ,
		.dir = PROC_DIR_CTRL,
	},
#endif	
	{
		.name="mapt_dmr_netif" ,
		.get = rtk_fc_proc_maptNetifDmr_get ,
		.set = rtk_fc_proc_maptNetifDmr_write ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="mcSetMode" ,
		.get = rtk_fc_proc_mcSetMode_get ,
		.set = rtk_fc_proc_mcSetMode_set ,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="meter_assign" ,
		.get =  rtk_fc_proc_fb_meter_get,
		.set =  rtk_fc_proc_fb_meter_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wan_port_mask" ,
		.get = rtk_fc_proc_wanPortMask_get ,
		.set = rtk_fc_proc_wanPortMask_set ,
		.dir = PROC_DIR_CTRL,
	},
	#if defined(CONFIG_CUSTOM_EXTERNAL_SWITCH)
	{
		.name="wan_vid_group" ,
		.get = rtk_fc_proc_wanVidGroup_get ,
		.set = rtk_fc_proc_wanVidGroup_set ,
		.dir = PROC_DIR_CTRL,
	},
	#endif
	{
		.name="mac_port_mask_without_cpu" ,
		.get = rtk_fc_proc_macPortMaskWithoutCpu_get ,
		.set = rtk_fc_proc_macPortMaskWithoutCpu_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="max_fc_wlan_rx_queue_size" ,
		.get = rtk_fc_proc_max_fc_wlan_rx_queue_size_get ,
		.set = rtk_fc_proc_max_fc_wlan_rx_queue_size_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_sync_period" ,
		.get = rtk_fc_proc_flowSyncPeriod_get ,
		.set = rtk_fc_proc_flowSyncPeriod_set ,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
	{
		.name="wifi_amsdu_pkt_set" ,
		.get = rtk_fc_proc_amsdu_pkt_get ,
		.set = rtk_fc_proc_amsdu_pkt_set ,
		.dir = PROC_DIR_CTRL,
		.unlockBefortWrite=1,
		.unlockBeforeRead=1,
	},
	{
		.name="wifi_amsdu_pkt_size_set" ,
		.get = rtk_fc_proc_amsdu_pkt_size_get ,
		.set = rtk_fc_proc_amsdu_pkt_size_set ,
		.dir = PROC_DIR_CTRL,
		.unlockBefortWrite=1,
		.unlockBeforeRead=1,
	},
	{
		.name="wifi_amsdu_time_out_set" ,
		.get = rtk_fc_proc_amsdu_time_out_get ,
		.set = rtk_fc_proc_amsdu_time_out_set ,
		.dir = PROC_DIR_CTRL,
		.unlockBefortWrite=1,
		.unlockBeforeRead=1,
	},
	{
		.name="pe_offload_wifi_amsdu_en" ,
		.get = rtk_fc_proc_pe_offload_wifi_amsdu_en_get ,
		.set = rtk_fc_proc_pe_offload_wifi_amsdu_en_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pe_offload_wifi_amsdu_func_switching" ,
		.get = rtk_fc_proc_pe_offload_wifi_amsdu_func_switching_get ,
		.set = rtk_fc_proc_pe_offload_wifi_amsdu_func_switching_set ,
		.unlockBefortWrite=1, //call FC API in the set function, thus enable .unlockBefortWrite to prevent deadlock
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wifi_amsdu_wlan_idx_table" ,
		.get = rtk_fc_proc_wifi_amsdu_wlan_idx_table_get,
		.set = NULL ,
		.dir = PROC_DIR_CTRL,
		.unlockBefortWrite=1,
		.unlockBeforeRead=1,
	},
#endif
	{
		.name="non_fdb_uc_timeout_sec" ,
		.get = rtk_fc_proc_nonfdbUc_timeout_get ,
		.set = rtk_fc_proc_nonfdbUc_timeout_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="non_static_mc_timeout_sec" ,
		.get = rtk_fc_proc_nonStaticMc_timeout_get ,
		.set = rtk_fc_proc_nonStaticMc_timeout_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="igmp_unknown_flood" ,
		.get = rtk_fc_proc_igmp_unknown_flood_get ,
		.set = rtk_fc_proc_igmp_unknown_flood_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="igmp_unknown_unTrack" ,
		.get = rtk_fc_proc_igmp_unknown_unTrack_get ,
		.set = rtk_fc_proc_igmp_unknown_unTrack_set ,
		.dir = PROC_DIR_CTRL,
	},	
	{
		.name="non_ct_flow_timeout" ,
		.get = rtk_fc_proc_nonCtFlowTimeoutSec_get ,
		.set = rtk_fc_proc_nonCtFlowTimeoutSec_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_flush" ,
		.get = NULL,
		.set = rtk_fc_proc_flowFlush_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_not_update_in_real_time" ,
		.get = rtk_fc_proc_flow_not_update_in_real_time_get,
		.set = rtk_fc_proc_flow_not_update_in_real_time_set,
		.dir = PROC_DIR_CTRL,
	},
#if !(defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
	{
		.name="prehashptn_sport" ,
		.get = rtk_fc_proc_prehashptn_sport_get,
		.set = rtk_fc_proc_prehashptn_sport_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="prehashptn_dport" ,
		.get = rtk_fc_proc_prehashptn_dport_get ,
		.set = rtk_fc_proc_prehashptn_dport_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="prehashptn_sip" ,
		.get = rtk_fc_proc_prehashptn_sip_get ,
		.set = rtk_fc_proc_prehashptn_sip_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="prehashptn_dip" ,
		.get = rtk_fc_proc_prehashptn_dip_get ,
		.set = rtk_fc_proc_prehashptn_dip_set ,
		.dir = PROC_DIR_CTRL,
	},
#endif
#if defined(CONFIG_FC_FLOW_AUTO_EXTEND)
	{
		.name="flow_sessionAutoExtend" ,
		.get = rtk_fc_proc_flowSessionAutoExtend_get,
		.set = rtk_fc_proc_flowSessionAutoExtend_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="flow_l2_skipPsTracking" ,
		.get = rtk_fc_proc_flow_l2_skipPsTracking_get,
		.set = rtk_fc_proc_flow_l2_skipPsTracking_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_skipAllPsTracking" ,
		.get = rtk_fc_proc_flowSkipAllPsTracking_get,
		.set = rtk_fc_proc_flowSkipAllPsTracking_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wifiSlowPathRxbyNic" ,
		.get = rtk_fc_proc_wifiSlowPathRxbyNic_get,
		.set = rtk_fc_proc_wifiSlowPathRxbyNic_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="stag_tpid" ,
		.get = rtk_fc_proc_stagTPID_get,
		.set = rtk_fc_proc_stagTPID_set,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	{
		.name="flow_mode",
		.get = rtk_fc_proc_fbmode_get,
		.set = rtk_fc_proc_fbmode_set,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="prevent_control_packet_drop",
		.get = rtk_fc_proc_prevent_control_packet_drop_get,
		.set = rtk_fc_proc_prevent_control_packet_drop_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wlan0_flow_ctrl_on_threshold_mbps",
		.get = rtk_fc_proc_wifi_flow_ctrl_threshold_get,
		.set = rtk_fc_proc_wlan0_flow_ctrl_on_threshold_mbps_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wlan0_flow_ctrl_off_threshold_mbps",
		.get = rtk_fc_proc_wifi_flow_ctrl_threshold_get,
		.set = rtk_fc_proc_wlan0_flow_ctrl_off_threshold_mbps_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wlan1_flow_ctrl_on_threshold_mbps",
		.get = rtk_fc_proc_wifi_flow_ctrl_threshold_get,
		.set = rtk_fc_proc_wlan1_flow_ctrl_on_threshold_mbps_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wlan1_flow_ctrl_off_threshold_mbps",
		.get = rtk_fc_proc_wifi_flow_ctrl_threshold_get,
		.set = rtk_fc_proc_wlan1_flow_ctrl_off_threshold_mbps_set,
		.dir = PROC_DIR_CTRL,
	},
#endif //CONFIG_RTK_L34_XPON_PLATFORM
	{
		.name="wifi_tx_qos_mapping",
		.get = rtk_fc_proc_wifi_tx_qos_mapping_get,
		.set = rtk_fc_proc_wifi_tx_qos_mapping_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="l2_macEgrLearning",
		.get = rtk_fc_proc_l2_macEgrLearning_get,
		.set = rtk_fc_proc_l2_macEgrLearning_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="l2_macPortGroup",
		.get = rtk_fc_proc_l2_mac_portGroup_get,
		.set = rtk_fc_proc_l2_mac_portGroup_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="epon_llid_format" ,
		.get = rtk_fc_proc_epon_llid_format_get,
		.set = rtk_fc_proc_epon_llid_format_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_tcp_learning_state" ,
		.get = rtk_fc_proc_flow_tcp_learning_state_get,
		.set = rtk_fc_proc_flow_tcp_learning_state_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_tcp_bidirection_learning" ,
		.get = rtk_fc_proc_flow_tcp_bidirection_learning_get,
		.set = rtk_fc_proc_flow_tcp_bidirection_learning_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pppoe_connectionAutoExtend_period" ,
		.get = rtk_fc_proc_pppoe_connectionAutoExtend_period_get,
		.set = rtk_fc_proc_pppoe_connectionAutoExtend_period_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_hash_input_config",
		.get = rtk_fc_proc_flow_hashInputConfig_get,
		.set = rtk_fc_proc_flow_hashInputConfig_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="host_policing_config" ,
		.get = rtk_fc_proc_host_policing_config_get,
		.set = rtk_fc_proc_host_policing_config_set,
		.unlockBefortWrite=1, //call FC API in the set function, thus enable .unlockBefortWrite to prevent deadlock
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="lanhost_routing_logging" ,
		.get = rtk_fc_proc_lanhost_routing_logging_get,
		.set = rtk_fc_proc_lanhost_routing_logging_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_operation",
		.get = rtk_fc_proc_flow_operation_get,
		.set = rtk_fc_proc_flow_operation_set,
		.unlockBefortWrite=1, //call FC API in the set function, thus enable .unlockBefortWrite to prevent deadlock
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="bridge_5tuple_flow_accelerate_by_2tuple",
		.get = rtk_fc_proc_bridge_5tuple_flow_accelerate_by_2tuple_get,
		.set = rtk_fc_proc_bridge_5tuple_flow_accelerate_by_2tuple_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="mc_5tuple_flow_accelerate_by_2tuple",
		.get = rtk_fc_proc_bridge_5tuple_flow_accelerate_by_2tuple_get,
		.set = rtk_fc_proc_mc_5tuple_flow_accelerate_by_2tuple_set ,
		.dir = PROC_DIR_CTRL,
	},	
#if !defined(CONFIG_FC_CA8277AB_SERIES)
	{
		.name="bridge_2tuple_flow_ethtype_check",
		.get = rtk_fc_proc_bridge_2tuple_flow_ethtype_check_get,
		.set = rtk_fc_proc_bridge_2tuple_flow_ethtype_check_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="bcDomain_hwflow_accelerate",
		.get = rtk_fc_proc_bc_hwflow_accelerate_get,
		.set = rtk_fc_proc_bc_hwflow_accelerate_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="abstrSwFlowTmpType",
		.get = rtk_fc_proc_abstrSwFlowTemplateType_get,
		.set = rtk_fc_proc_abstrSwFlowTemplateType_set ,
		.dir = PROC_DIR_CTRL,
	},


#if defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES)
	{
		.name="xgspon_ds_to_l3fe",
		.get = rtk_fc_proc_xgspon_ds_to_l3fe_get,
		.set = rtk_fc_proc_xgspon_ds_to_l3fe_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="flow_meter_mib_conf_dependence",
		.get = rtk_fc_proc_flow_meter_mib_conf_dependence_get,
		.set = rtk_fc_proc_flow_meter_mib_conf_dependence_set ,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="hw_error_pkt_check",
		.get = rtk_fc_proc_hw_error_pkt_check_get,
		.set = rtk_fc_proc_hw_error_pkt_check_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="vlan_parsing_mode" ,
		.get = rtk_fc_proc_vlanParsingMode_get ,
		.set = rtk_fc_proc_vlanParsingMode_write ,
		.dir = PROC_DIR_CTRL,
	},
#endif
#endif
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	{
		.name="dsliteHwAcceleration_disable" ,
		.get = rtk_fc_proc_dsliteHwAcceleration_disable_get,
		.set = rtk_fc_proc_dsliteHwAcceleration_disable_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="dsliteV6TosFromV4" ,
		.get = rtk_fc_proc_dsliteV6TosFromV4_get,
		.set = rtk_fc_proc_dsliteV6TosFromV4_set,
		.dir = PROC_DIR_CTRL,
	},

	{
		.name="v6RDSwAcceleration_disable" ,
		.get = rtk_fc_proc_v6RDSwAcceleration_disable_get,
		.set = rtk_fc_proc_v6RDSwAcceleration_disable_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_streamId_enable" ,
		.get = rtk_fc_proc_skbmark_streamId_enable_get,
		.set = rtk_fc_proc_skbmark_streamId_enable_set,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_RTK_PE)
	{
		.name="skbmark_pe_aware" ,
		.get = rtk_fc_proc_skbmark_pe_aware_get,
		.set = rtk_fc_proc_skbmark_pe_aware_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="skbmark_streamId" ,
		.get = rtk_fc_proc_skbmark_streamId_get,
		.set = rtk_fc_proc_skbmark_streamId_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_swshaper_enable" ,
		.get = rtk_fc_proc_skbmark_swshaper_enable_get,
		.set = rtk_fc_proc_skbmark_swshaper_enable_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_psFloodfwdAcc" ,
		.get = rtk_fc_proc_skbmark_psFloodFwdAcc_get,
		.set = rtk_fc_proc_skbmark_psFloodFwdAcc_set,
		.dir = PROC_DIR_CTRL,
	},	
	{
		.name="skbmark_meterId_enable" ,
		.get = rtk_fc_proc_skbmark_meterId_enable_get,
		.set = rtk_fc_proc_skbmark_meterId_enable_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_meterId" ,
		.get = rtk_fc_proc_skbmark_meterId_get,
		.set = rtk_fc_proc_skbmark_meterId_set,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if !defined(CONFIG_FC_RTL9607F_SERIES)
	//TODO: 9607F flow cache mib
	{
		.name="skbmark_flow_cache_mib_enable" ,
		.get = rtk_fc_proc_skbmark_flow_cache_mib_enable_get,
		.set = rtk_fc_proc_skbmark_flow_cache_mib_enable_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="skbmark_mapet_fmr" ,
		.get = rtk_fc_proc_skbmark_mapet_fmr_get,
		.set = rtk_fc_proc_skbmark_mapet_fmr_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="skbmark_mibId_enable" ,
		.get = rtk_fc_proc_skbmark_mibId_enable_get,
		.set = rtk_fc_proc_skbmark_mibId_enable_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_mibId" ,
		.get = rtk_fc_proc_skbmark_mibId_get,
		.set = rtk_fc_proc_skbmark_mibId_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_mib2Id_enable" ,
		.get = rtk_fc_proc_skbmark_mib2Id_enable_get,
		.set = rtk_fc_proc_skbmark_mib2Id_enable_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_mib2Id" ,
		.get = rtk_fc_proc_skbmark_mib2Id_get,
		.set = rtk_fc_proc_skbmark_mib2Id_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_qid" ,
		.get = rtk_fc_proc_skbmark_qid_get,
		.set = rtk_fc_proc_skbmark_qid_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_fwdByPs" ,
		.get = rtk_fc_proc_skbmark_fwdByPs_get,
		.set = rtk_fc_proc_skbmark_fwdByPs_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_skipFcFunc" ,
		.get = rtk_fc_proc_skbmark_skipFcFunc_get,
		.set = rtk_fc_proc_skbmark_skipFcFunc_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_wanDsLoopback_en" ,
		.get = rtk_fc_proc_skbmark_wanDsLoopback_en_get,
		.set = rtk_fc_proc_skbmark_wanDsLoopback_en_set,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	{
		.name="skbmark_cvlan_vid" ,
		.get = rtk_fc_proc_skbmark_cvlan_vid_get,
		.set = rtk_fc_proc_skbmark_cvlan_vid_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_cvlan_pri" ,
		.get = rtk_fc_proc_skbmark_cvlan_pri_get,
		.set = rtk_fc_proc_skbmark_cvlan_pri_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_cvlan_action" ,
		.get = rtk_fc_proc_skbmark_cvlan_action_get,
		.set = rtk_fc_proc_skbmark_cvlan_action_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_svlan_vid" ,
		.get = rtk_fc_proc_skbmark_svlan_vid_get,
		.set = rtk_fc_proc_skbmark_svlan_vid_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_svlan_pri" ,
		.get = rtk_fc_proc_skbmark_svlan_pri_get,
		.set = rtk_fc_proc_skbmark_svlan_pri_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_svlan_action" ,
		.get = rtk_fc_proc_skbmark_svlan_action_get,
		.set = rtk_fc_proc_skbmark_svlan_action_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_svlan_tpid" ,
		.get = rtk_fc_proc_skbmark_svlan_tpid_get,
		.set = rtk_fc_proc_skbmark_svlan_tpid_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="skbmark_swIntfIdx_assign_en" ,
		.get = rtk_fc_proc_skbmark_swIntfIdx_assign_en_get,
		.set = rtk_fc_proc_skbmark_swIntfIdx_assign_en_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_swIntfIdx_assign_inOut" ,
		.get = rtk_fc_proc_skbmark_swIntfIdx_assign_inOut_get,
		.set = rtk_fc_proc_skbmark_swIntfIdx_assign_inOut_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_swIntfIdx_assign_idx" ,
		.get = rtk_fc_proc_skbmark_swIntfIdx_assign_idx_get,
		.set = rtk_fc_proc_skbmark_swIntfIdx_assign_idx_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_skip_fc_alg_check" ,
		.get = rtk_fc_proc_skbmark_skip_fc_alg_check_get,
		.set = rtk_fc_proc_skbmark_skip_fc_alg_check_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="skbmark_forceIntpriToWifiPri" ,
		.get = rtk_fc_proc_skbmark_forceIntpriToWifiPri_get,
		.set = rtk_fc_proc_skbmark_forceIntpriToWifiPri_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wan_access_limit_IP_list_reset",
		.get = NULL ,
		.set = rtk_fc_proc_wan_access_limit_IP_list_reset ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wan_access_limit_probe_interval",
		.get = rtk_fc_proc_wan_access_limit_probeInterval_get ,
		.set = rtk_fc_proc_wan_access_limit_probeInterval_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="hwnat",
		.get = rtk_fc_proc_hwnat_mode_selection_get ,
		.set = rtk_fc_proc_hwnat_mode_selection_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="shortcut_flow_count",
		.get = rtk_fc_proc_shortcut_flow_count_get ,
		.set = rtk_fc_proc_shortcut_flow_count_set ,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_SMP)
	{
		.name="smp_dispatch_nic_rx",
		.get = rtk_fc_proc_smp_dispatch_nic_rx_get,
		.set = rtk_fc_proc_smp_dispatch_nic_rx_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="smp_dispatch_nic_tx",
		.get = rtk_fc_proc_smp_dispatch_nic_tx_get,
		.set = rtk_fc_proc_smp_dispatch_nic_tx_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="smp_dispatch_wifi_rx",
		.get = rtk_fc_proc_smp_dispatch_wifi_rx_get,
		.set = rtk_fc_proc_smp_dispatch_wifi_rx_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="smp_dispatch_wifi_tx",
		.get = rtk_fc_proc_smp_dispatch_wifi_tx_get,
		.set = rtk_fc_proc_smp_dispatch_wifi_tx_set,
		.dir = PROC_DIR_CTRL,
	},

#endif
	{
		.name="flow_delay",
		.get = rtk_fc_proc_flow_delay_get,
		.set = rtk_fc_proc_flow_delay_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="ethertype_bypass",
		.get = rtk_fc_proc_ethertype_bypass_get,
		.set = rtk_fc_proc_ethertype_bypass_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="sw_shaperMib_update_mode_flow",
		.get = rtk_fc_proc_sw_shaperMib_update_mode_flow_get,
		.set = rtk_fc_proc_sw_shaperMib_update_mode_flow_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="sw_shaperMib_update_mode_host",
		.get = rtk_fc_proc_sw_shaperMib_update_mode_host_get,
		.set = rtk_fc_proc_sw_shaperMib_update_mode_host_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="sw_ackDelay_ctrl",
		.get = rtk_fc_proc_sw_ackDelay_ctrl_get,
		.set = rtk_fc_proc_sw_ackDelay_ctrl_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_swFwded_aclTrapPri_mask",
		.get = rtk_fc_proc_flow_swFwded_aclTrapPri_mask_get,
		.set = rtk_fc_proc_flow_swFwded_aclTrapPri_mask_set,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_FC_RTL8198F_SERIES)
	{
		.name="dslite_udp_fragment" ,
		.get = rtk_fc_proc_dsliteUdpFrag_get ,
		.set = rtk_fc_proc_dsliteUdpFrag_set ,
		.dir = PROC_DIR_CTRL,
	},
#endif
#if defined(CONFIG_RTK_SOC_RTL8198D)
	{
		.name="user_group",
		.get = rtk_fc_proc_user_group_get,
		.set = rtk_fc_proc_user_group_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_limit" ,
		.get = dump_flow_limit,
		.set = rtk_fc_proc_flow_limit_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="ip_frag_shortcut",
		.get = rtk_fc_proc_ip_frag_shortcut_get,
		.set = rtk_fc_proc_ip_frag_shortcut_set,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)

	{
		.name="dynamic_prehashPattern" ,
		.get = rtk_fc_proc_dynamic_prehashPattern_get,
		.set = rtk_fc_proc_dynamic_prehashPattern_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	{
		.name="ct_invalid_delete_flow_immediatly" ,
		.get = rtk_fc_proc_house_keeping_flow_delete_immediatly_get,
		.set = rtk_fc_proc_house_keeping_flow_delete_immediatly_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="l2_port_moving_check_with_vlan" ,
		.get = rtk_fc_proc_l2_port_moving_check_with_vlan_get,
		.set = rtk_fc_proc_l2_port_moving_check_with_vlan_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="disable_port_port_moving" ,
		.get = rtk_fc_proc_disable_port_port_moving_get,
		.set = rtk_fc_proc_disable_port_port_moving_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="external_switch_info" ,
		.get = rtk_fc_proc_external_switch_info_get,
		.set = rtk_fc_proc_external_switch_info_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="l2tp_v6_en_hwacc" ,
		.get = rtk_fc_proc_l2tp_v6_en_hwacc_get,
		.set = rtk_fc_proc_l2tp_v6_en_hwacc_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pptp_v6_en_hwacc" ,
		.get = rtk_fc_proc_pptp_v6_en_hwacc_get,
		.set = rtk_fc_proc_pptp_v6_en_hwacc_set,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="vxlan_v6_en_hwacc" ,
		.get = rtk_fc_proc_vxlan_v6_en_hwacc_get,
		.set = rtk_fc_proc_vxlan_v6_en_hwacc_set,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_RTK_FC_IPSEC_FASTFWD)	
	{
		.name="ipsec_en_shortCut" ,
		.get = rtk_fc_proc_ipsec_en_shortCut_get,
		.set = rtk_fc_proc_ipsec_en_shortCut_set,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="ipsec_en_pe_offload" ,
		.get = rtk_fc_proc_ipsec_en_pe_offload_get,
		.set = rtk_fc_proc_ipsec_en_pe_offload_set,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_CTRL,
	},
#endif	
#if defined(CONFIG_RTK_FC_SRV6_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
	{
		.name="pe_srv6_offload" ,
		.get = rtk_fc_proc_pe_srv6_offload_get,
		.set = rtk_fc_proc_pe_srv6_offload_set,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="srv6_netif" ,
		.get = rtk_fc_proc_srv6NetIf_get ,
		.set = rtk_fc_proc_srv6NetIf_write ,
		.dir = PROC_DIR_CTRL,
	},
#endif
#endif
#endif
#if defined(CONFIG_RTK_FC_HTTP_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
	{
		.name="pe_tcp_download_congestion_mode" ,
		.get = rtk_fc_proc_pe_tcp_download_congestion_mode_get,
		.set = rtk_fc_proc_pe_tcp_download_congestion_mode_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pe_tcp_download_non_congestion_completely_done" ,
		.get = rtk_fc_proc_pe_tcp_download_non_congestion_completely_done_get,
		.set = rtk_fc_proc_pe_tcp_download_non_congestion_completely_done_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
#if defined(CONFIG_RTK_FC_PKT_QUEUE_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
	{
		.name="pe_offload_pkt_queue_portmask" ,
		.get = rtk_fc_proc_pe_offload_pkt_queue_get,
		.set = rtk_fc_proc_pe_offload_pkt_queue_set,
		.unlockBefortWrite=1,
		.unlockBeforeRead=1,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pe_offload_pkt_queue_downstream_tx_portmask" ,
		.get = rtk_fc_proc_pe_offload_pkt_queue_downstream_tx_portmask_get,
		.set = rtk_fc_proc_pe_offload_pkt_queue_downstream_tx_portmask_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pe_offload_pkt_queue_memory_cost_Mbytes" ,
		.get = rtk_fc_proc_pe_offload_pkt_queue_memory_cost_Mbytes_get,
		.set = rtk_fc_proc_pe_offload_pkt_queue_memory_cost_Mbytes_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pe_offload_pkt_queue_tc_mode" ,
		.get = rtk_fc_proc_pe_offload_pkt_queue_tc_mode_get,
		.set = rtk_fc_proc_pe_offload_pkt_queue_tc_mode_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pe_offload_pkt_queue_tc_mode_slave_portmask" ,
		.get = rtk_fc_proc_pe_offload_pkt_queue_tc_mode_slave_portmask_get,
		.set = rtk_fc_proc_pe_offload_pkt_queue_tc_mode_slave_portmask_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pe_offload_pkt_queue_tc_mode_only_for_small_pkt" ,
		.get = rtk_fc_proc_pe_offload_pkt_queue_tc_mode_only_for_small_pkt_get,
		.set = rtk_fc_proc_pe_offload_pkt_queue_tc_mode_only_for_small_pkt_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="pe_offload_pkt_queue_tc_mode_ldma_delay_us" ,
		.get = NULL,
		.set = rtk_fc_proc_pe_offload_pkt_queue_tc_mode_ldma_delay_us_set,
		.unlockBefortWrite=1,
		.unlockBeforeRead=1,
		.dir = PROC_DIR_CTRL,
	},
#endif
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	{
		.name="pe_debug_mode_en" ,
		.get = rtk_fc_pe_debug_mode_en_get ,
		.set = rtk_fc_pe_debug_mode_en_set ,
		.dir = PROC_DIR_CTRL,
		.unlockBefortWrite=1,
		//.unlockBeforeRead=1,
	},
#if defined(CONFIG_REALTEK_IPC2RCPU)
	{
		.name="pe_debug_mode_pe_debug" ,
		.get = rtk_fc_pe_debug_mode_pe_debug ,
		.set = NULL ,
		.dir = PROC_DIR_CTRL,
		.unlockBefortWrite=1,
		.unlockBeforeRead=1,
	},
#endif
#endif
	{
		.name="pe_func_on_pe_cpu_num",
		.get = rtk_fc_proc_pe_func_on_pe_cpu_num_get,
		.set = rtk_fc_proc_pe_func_on_pe_cpu_num_set ,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="flow_mib_mode" ,
		.get = rtk_fc_proc_flow_mib_mode_get,
		.set = rtk_fc_proc_flow_mib_mode_set,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="flow_miss_meter" ,
		.get = rtk_fc_proc_flow_miss_meter_get,
		.set = rtk_fc_proc_flow_miss_meter_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="wan_ingress_shaper" ,
		.get = rtk_fc_proc_wan_ingress_shaper_get,
		.set = rtk_fc_proc_wan_ingress_shaper_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="dual_extra_cls_info" ,
		.get = rtk_fc_proc_dual_extra_cls_info_get,
		.set = NULL,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif
	{
		.name="netlink_sync_lut",
		.get = rtk_fc_proc_netlink_sync_lut_get,
		.set = rtk_fc_proc_netlink_sync_lut_set ,
		.dir = PROC_DIR_CTRL,
	},
#if defined(CONFIG_RTK_SOC_RTL8198D) || defined(CONFIG_RTL8198X_SERIES)
	{
		.name="skip_acc_port_mask" ,
		.get = rtk_fc_proc_skip_acc_port_mask_get,
		.set = rtk_fc_proc_skip_acc_port_mask_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
#if defined(CONFIG_RTL8198XB_SERIES) || defined(CONFIG_RTL8198E_SERIES)
	{
		.name="improve_mc_priority" ,
		.get = rtk_fc_proc_improve_mc_priority_get,
		.set = rtk_fc_proc_improve_mc_priority_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
#if defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="dmalso_backpressure_rule" ,
		.get = rtk_fc_proc_dmalso_backpressure_rule_get,
		.set = rtk_fc_proc_dmalso_backpressure_rule_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
	// ==================== HW DUMP ====================
	{
		.name="l2" ,
		.get = dump_lut_table ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="flow" ,
		.get = dump_flow_table ,
		.set = dump_flow_table_by_filter ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="netif" ,
		.get = dump_netif ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="flow_statistic" ,
		.get = rtk_fc_proc_flowStatistic_read ,
		.set = rtk_fc_proc_flowStatistic_write ,
		.dir = PROC_DIR_HW_DUMP,
	},
#if defined(CONFIG_RTK_L34_G3_PLATFORM) && defined(CONFIG_FC_CA8277AB_SERIES)
	{
		.name="acl_ca" ,
		.get = dump_acl_ca ,
		.set = dump_acl_ca_by_index ,
		.dir = PROC_DIR_HW_DUMP,
	},
#endif
	{
		.name="acl" ,
		.get = dump_acl ,
		.set = dump_acl_by_index ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="acl_reserved_info" ,
		.get = dump_acl_reserved_info ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="acl_range_table" ,
		.get = dump_acl_range_table ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="flowmib" ,
		.get = dump_flow_mib,
		.set = rtk_fc_proc_hw_flow_mib_reset ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="flowmib2" ,
		.get = dump_flow_mib2,
		.set = rtk_fc_proc_hw_flow_mib2_reset ,
		.dir = PROC_DIR_HW_DUMP,
	},
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	{
		.name="hs" ,
		.get = dump_hs ,
		.set = tracefilterRULE0_dump_hs_timer,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="l34hs" ,
		.get = dump_l34hs ,
		.set = tracefilterRULE0_dump_hs_timer,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="flowdram" ,
		.get = dump_flowdram_table ,
		.set = dump_flowdram_table_by_filter ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="flowtag" ,
		.get = dump_flowtag_table ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="camtag" ,
		.get = dump_camtag_table ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="flowtrf" ,
		.get = dump_flowtrf_table ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="indmac" ,
		.get = dump_indmac_table ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="extpmask" ,
		.get = dump_extpmask_table ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
	{
		.name="headera",
		.get = dump_headera,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="headeri",
		.get = dump_headeri,
		.set = rtk_fc_headeri_latchMode_set,
		.overPageSizeDisplay = 1,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="dropcount",
		.get = dump_dropcount,
		.set = dump_dropcount_opcode,
		.overPageSizeDisplay = 1,
		.dir = PROC_DIR_HW_DUMP,
	},
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="dmaAftAction",
		.get = dump_dmaAftAction_table,
		.set = dump_dmaAftAction_table_by_idx ,
		.dir = PROC_DIR_HW_DUMP,
	},
#if defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="dmaAftMap",
		.get = dump_dmaAftMap_table,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
#endif
#endif
#endif
	{
		.name="ethtype" ,
		.get = dump_ethtype_table ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="extratag" ,
		.get = dump_extratag_table ,
		.set = NULL ,
		.dir = PROC_DIR_HW_DUMP,
	},
	{
		.name="host_policing_mib",
		.get = dump_hw_host_policing_mib,
		.set = rtk_fc_proc_hw_host_policing_mib_reset ,
		.dir = PROC_DIR_HW_DUMP,
	},
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
	{
		.name="wfo_per_flow_counter",
		.get = rtk_fc_proc_wfo_per_flow_share_cnt_hw_get,
		.set = rtk_fc_proc_wfo_per_flow_share_cnt_hw_clear,
		.dir = PROC_DIR_HW_DUMP,
	},
#endif

	// ==================== SW DUMP ====================
	{
		.name="abstrSwflow" ,
		.get = dump_abstrSwFlow_table ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},	
	{
		.name="flow" ,
		.get = dump_sw_flow_table ,
		.set = dump_sw_flow_table_by_filter ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="flow_list" ,
		.get = dump_sw_flow_list ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	{
		.name="flowTcam_list" ,
		.get = dump_sw_flowTcam_list ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) && RTK_FC_TABLESIZE_OVERFLOW_FLOW
	{
		.name="flowOverFlow_list" ,
		.get = dump_sw_flowOverFlow_list_list ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif
	{
		.name="flow_natloopback" ,
		.get = rtk_fc_proc_flow_natloopback,
		.set = NULL,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="netif" ,
		.get = dump_sw_netif ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="netifmib" ,
		.get = dump_sw_netifmib ,
		.set = rtk_fc_proc_netif_mib_reset,
		.dir = PROC_DIR_SW_DUMP,

	},
	{
		.name="acl" ,
		.get = dump_sw_acl ,
		.set = dump_sw_acl_by_index ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="flowmib" ,
		.get = dump_sw_flow_mib,
		.set = rtk_fc_proc_sw_flow_mib_reset,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="flowmib2" ,
		.get = dump_sw_flow_mib2,
		.set = rtk_fc_proc_sw_flow_mib2_reset,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="l2" ,
		.get = rtk_fc_proc_flowLut_read ,
		.set = rtk_fc_proc_flowLut_write ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="l2_ivl" ,
		.get = rtk_fc_proc_l2ivl_read ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="lutCam_list" ,
		.get = dump_lutCam_list,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="flow_statistic" ,
		.get = rtk_fc_proc_sw_flowStatistic_get ,
		.set = rtk_fc_proc_sw_flowStatistic_set ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="fwd_statistic" ,
		.get = rtk_fc_proc_fwdStatistic_get ,
		.set = rtk_fc_proc_fwdStatistic_set ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="fwd_statistic_drop" ,
		.get = rtk_fc_proc_fwdStatistic_drop_get ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="version" ,
		.get = rtk_fc_proc_version_get ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="devGwMac" ,
		.get = rtk_fc_proc_devGwMac_get ,
		.set = rtk_fc_proc_devGwMac_set ,
		.dir = PROC_DIR_SW_DUMP,
	},	
	{
		.name="wlan_devmap" ,
		.get = rtk_fc_proc_wlan_devmap_dump ,
		.set = rtk_fc_proc_wlan_devmap_set,
		.dir = PROC_DIR_SW_DUMP,
		.unlockBefortWrite=1,
	},
#if defined(CONFIG_RTK_FC_IPSEC_FASTFWD)
	{
		.name="ipsec_hook_table" ,
		.get = rtk_fc_proc_ipsec_hook_table_get ,
		.set = rtk_fc_proc_ipsec_hook_table_set ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="ipsec_ipi_info" ,
		.get = rtk_fc_proc_ipsec_ipi_info_get ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="ipsec_debug" ,
		.get = rtk_fc_proc_ipsec_debug_get ,
		.set = rtk_fc_proc_ipsec_debug_set ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="ipsec_key_dump" ,
		.get = rtk_fc_proc_ipsec_key_dump_get,
		.set = NULL,
		//.unlockBefortWrite=1,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif	
	{
		.name="smp_statistic" ,
		.get = rtk_fc_smpStatistic_get ,
		.set = rtk_fc_smpStatistic_set ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{

		.name="mcExtFlowIdxTbl",
		.get = rtk_fc_igmp_show_mcExtFlowIdxTbl,
		.set = rtk_fc_igmp_mcExtFlowIdxTbl_write,
		.dir = PROC_DIR_SW_DUMP,
	},	
	{

		.name="igmp_driver",
		.get = rtk_fc_igmp_mld_dirver_show,
		.set = NULL,
		.dir = PROC_DIR_SW_DUMP,
	},
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	{
		.name="wifi_amsdu_pe_offload_mac_id_tbl",
		.get = rtk_fc_wifi_amsdu_pe_offload_mode_macId_get,
		.set = rtk_fc_wifi_amsdu_pe_offload_mode_macId_set,
		.dir = PROC_DIR_SW_DUMP,
		.unlockBefortWrite=1,
	},
	{
		.name="mc_amsdu_flow" ,
		.get = rtk_fc_proc_mc_amsdu_flow_get ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},		
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
	{
		.name="wfo_per_flow_counter",
		.get = rtk_fc_proc_wfo_per_flow_share_cnt_sw_get,
		.set = rtk_fc_proc_wfo_per_flow_share_cnt_sw_clear,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="wfo_per_hw_flow_share_mem",
		.get = rtk_fc_proc_wfo_per_hw_flow_share_mem_get,
		.set = rtk_fc_proc_wfo_per_hw_flow_share_mem_set,
		.dir = PROC_DIR_SW_DUMP,
		.unlockBefortWrite=1,
	},
#endif

#endif
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	{

		.name="pon_streamid",
		.get = dump_ponStreamid_table,
		.set = NULL,
		.dir = PROC_DIR_SW_DUMP,
	},
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="dmaAftAction",
		.get = dump_sw_dmaAftAction_table,
		.set = dump_sw_dmaAftAction_table_by_idx ,
		.dir = PROC_DIR_SW_DUMP,
	},
#if defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="dmaAftMap",
		.get = dump_sw_dmaAftMap_table,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif
#endif
#endif
#if defined(CONFIG_RTK_SOC_RTL8198D) || defined(CONFIG_RTL8198X_SERIES)
	{
		.name="ext_flowmib" ,
		.get = dump_ext_flow_mib,
		.set = rtk_fc_proc_ext_flow_mib_set,
		.unlockBefortWrite=1,
		.unlockBeforeRead=1,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif
#if defined(CONFIG_RTK_SOC_RTL8198D) || defined(CONFIG_FC_RTL8198F_SERIES) || defined(CONFIG_RTL8198X_SERIES)
	{
		.name="tcp_in_window",
		.get = rtk_fc_tcp_in_window_get,
		.set = rtk_fc_tcp_in_window_set ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="default_route_info",
		.get = rtk_fc_default_route_info_get,
		.set = NULL,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="sw_check_hwflow" ,
		.get = rtk_fc_sw_check_hwflow_get,
		.set = rtk_fc_sw_check_hwflow_set,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif
	{
		.name="host_policing_mib",
		.get = dump_sw_host_policing_mib,
		.set = rtk_fc_proc_sw_host_policing_mib_reset,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="rt_meter",
		.get = dump_rtMeter_table,
		.set = dump_rtMeter_debugging_table ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="shaper",
		.get = dump_swShaper_table,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="ackDelay",
		.get = dump_sw_ackDelay,
		.set = NULL,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="rt_wanAccessLimitStatistic",
		.get = dump_wan_access_limit_statistic,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="dual_passthrough_flowMapping_table",
		.get = dump_dual_passthrough_flowMapping_table,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="ipv6_nat_mapping_table",
		.get = dump_ipv6_nat_mapping_table,
		.set = rtk_fc_reset_ipv6_nat_mapping_table ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="ipv6_mapt_mapping_table",
		.get = dump_ipv6_mapt_mapping_table,
		.set = rtk_fc_reset_ipv6_mapt_mapping_table ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="l2dual_info_table",
		.get = dump_l2dual_info_table,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
#if defined(CONFIG_FC_CA8277B_SERIES)	
	{
		.name="vxlan_extra_record",
		.get = dump_vxlan_extra_record,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif
#if defined(CONFIG_FC_RTL9607C_SERIES)
	{
		.name="vxlan_acc_mechanism",
		.get = _rtk_fc_vxlan_acceleration_mechanism_get,
		.set = _rtk_fc_vxlan_acceleration_mechanism_proc_set ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="nptv6_acc_mechanism",
		.get = _rtk_fc_nptv6_acceleration_mechanism_get,
		.set = _rtk_fc_nptv6_acceleration_mechanism_proc_set ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name= "vxlan_acc_mechanism_extraPmsk",
		.get = _rtk_fc_vxlan_acceleration_extraPmsk_get,
		.set = _rtk_fc_vxlan_acceleration_extraPmsk_proc_set,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name= "nptv6_acc_mechanism_lanMeter",
		.get = _rtk_fc_nptv6_acc_mechanism_lanMeter_get,
		.set = _rtk_fc_nptv6_acc_mechanism_lanMeter_set,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif
	{
		.name="flow_connection",
		.get = dump_flow_connection,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="mape_dst6_info",
		.get = dump_sw_mape_dst6_info,
		.set = NULL,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="meminfo",
		.get = dump_sw_memInfo,
		.set = NULL,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="memAllocStat",
		.get = dump_sw_memAllocStat,
		.set = NULL,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="vlanGroup_macLimit_statistics" ,
		.get = dump_vlanGroupMacLimitStatistics ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="mcToUcMacId" ,
		.get = dump_mc_macId_table ,
		.set = NULL ,
		.dir = PROC_DIR_SW_DUMP,
	},	
	{
		.name="dump_ct_hash_info_list" ,
		.get = dump_ct_hash_info_list ,
		.set = rtk_fc_ct_hash_info_list_flush ,
		.dir = PROC_DIR_SW_DUMP,
	},
#endif
	{
		.name="pe_crypto_connection" ,
		.get = dump_pe_crypto_connection ,
		.set = rtk_fc_proc_pe_crypto_test_set ,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_SW_DUMP,
	},
	{
		.name="pe_srv6_offload" ,
		.get = dump_pe_srv6_offload ,
		.set = rtk_fc_proc_pe_srv6_test_set ,
		.unlockBefortWrite=1,
		.dir = PROC_DIR_SW_DUMP,
	},
#if	defined(CONFIG_FC_RTL8277C_SERIES) && defined(CONFIG_FC_HASH_TEST)
	{
		.name="77C_hash_test" ,
		.get = rtk_fc_proc_hash_test ,
		.set = NULL,
		.dir = PROC_DIR_TRACE,
	},
#endif 
	// ==================== TRACE ====================
	{
		.name="debug_level" ,
		.get = rtk_fc_proc_debug_level_read ,
		.set = rtk_fc_proc_debug_level_write ,
		.dir = PROC_DIR_TRACE,
	},

	{
		.name="filter_level" ,
		.get = rtk_fc_proc_filter_level_read ,
		.set = rtk_fc_proc_filter_level_write ,
		.dir = PROC_DIR_TRACE,
	},

	{
		.name="trace_filter" ,
		.get = rtk_fc_proc_traceFilter_read ,
		.set = rtk_fc_proc_traceFilter_write ,
		.dir = PROC_DIR_TRACE,
	},

	{
		.name="debug_message_display_to_current_tty" ,
		.get = rtk_fc_proc_debug_message_display_to_tty_enable_read ,
		.set = rtk_fc_proc_debug_message_display_to_tty_enable_write ,
		.dir = PROC_DIR_TRACE,
	},
	{
		.name="manager_debug" ,
		.get = rtk_fc_proc_manager_debug_message_read ,
		.set = rtk_fc_proc_manager_debug_message_write ,
		.dir = PROC_DIR_TRACE,
	},
	{
		.name="flow_delete_track" ,
		.get = rtk_fc_proc_flow_delete_track_read ,
		.set = rtk_fc_proc_flow_delete_track_write ,
		.dir = PROC_DIR_TRACE,
	},
#if 0//defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		.name="ldma_test" ,
		.get = NULL,
		.set = rtk_fc_proc_ldma_test_set,
		.dir = PROC_DIR_CTRL,
		.unlockBefortWrite = 1,
		.unlockBeforeRead = 1,
	},
#endif
#if defined(CONFIG_RTK_PTOOL_CPU_PERF)
#if defined(CONFIG_FC_RTL9607C_SERIES)
	{
		.name="test_gdma4w" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_4WORD_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma8w" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_8WORD_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma16w" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_16WORD_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_0" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_0_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_1" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_1_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_2" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_2_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_3" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_3_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_01" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_01_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_02" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_02_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_03" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_03_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_12" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_12_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_13" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_13_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_23" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_23_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_012" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_012_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_013" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_013_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_023" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_023_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_gdma32w_123" ,
		.get = rtk_fc_proc_test_gdma_get,
		.set = rtk_fc_proc_test_gdma_32WORD_123_set,
		.dir = PROC_DIR_CTRL,
	},
	{
		.name="test_memcpy" ,
		.get = rtk_fc_proc_test_memcpy_get,
		.set = rtk_fc_proc_test_memcpy_set,
		.dir = PROC_DIR_CTRL,
	},
#endif
#endif
};

int _rtk_fc_proc_path_string_get(struct file *filep, char **path_str)
{
	// convert to string
	*path_str = d_path(&filep->f_path, &fc_db.cmd_proc_path[0], CMD_BUFF_PROC_LEN);
	if (IS_ERR(*path_str)) {
		printk("%s Failed to get path from d_path\n", __FUNCTION__);
		return FAIL;
	}

	return SUCCESS;
}

void _rtk_fc_proc_parsing_string_err_msg(void)
{
	rtlglue_printf("ERROR: invalid command.\n");
}

int _rtk_fc_proc_parsing_string_to_param_arrary(const char *buff, unsigned long length, char **param)
{
	char *strptr = NULL;
	unsigned char *tmpBuf = &fc_db.cmd_buf[0];
	int len = (length >= CMD_BUFF_SIZE) ? (int)CMD_BUFF_SIZE : (int)length;
	int i = 0;
	
	if (buff)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;

		/* parse all parameters*/
		for(i = 0; i < CMD_PARAM_COUNT; i++){

			*(param + i) = strsep(&strptr," ");
			
			if(*(param + i) == NULL){
				break;
			}
		}
	}

	if(*(param + 0) == NULL)
		return FAILED;
	else
		return SUCCESS;

}

int _rtk_fc_proc_parsing_string_to_integer(const char *buff,unsigned long len)
{
	char *tmpbuf;
	int ret;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	tmpbuf=&tmpBuf[0];

	if (buff)
	{
		/* copy data to the buffer */
		strscpy(tmpbuf, buff, CMD_BUFF_SIZE);
	}
	ret=simple_strtol(tmpbuf, NULL, 0);

	return ret;
}

unsigned char _rtk_rg_hex_to_byte(unsigned char hex)
{
	unsigned char ret=0;
	if((hex>='A')&&(hex<='F'))
		ret=hex-'A'+10;
	else if((hex>='a')&&(hex<='f'))
		ret=hex-'a'+10;
	else if((hex>='0')&&(hex<='9'))
		ret=hex-'0';
	return ret;
}
#if defined(CONFIG_FC_HASH_TEST)

void flowHashCalInfo_set(rtk_rg_asic_flow_hash_cal_info_t *flowHashCalInfo, int in_spa, int in_cvid, int in_cpri, int in_ccfi, int in_tcp, int in_dscp, int in_ecn, 
uint32 saddr, uint32 daddr, uint16 sport, uint16 dport)
{

	memset(flowHashCalInfo, 0, sizeof(rtk_rg_asic_flow_hash_cal_info_t));
	flowHashCalInfo->in_path = FB_PATH_5;

	{
		flowHashCalInfo->path5_key.orig_lspid		= in_spa;
		
		if(in_cvid>0)
		{
			flowHashCalInfo->path5_key.ctag_if		= TRUE;
			flowHashCalInfo->path5_key.cvlan_tpid	= ETH_P_8021Q;
			flowHashCalInfo->path5_key.cvlan_id		= in_cvid;
			flowHashCalInfo->path5_key.cvlan_pri		= in_cpri;
			flowHashCalInfo->path5_key.cvlan_cfi		= in_ccfi;
		}

		
		flowHashCalInfo->path5_key.ipv4_or_ipv6		= 0;		// 1: IPv6, 0: IPv4
		flowHashCalInfo->path5_key.l4proto			= in_tcp;	// 1: TCP, 0: UDP
		
		flowHashCalInfo->path5_key.ip_dscp			= in_dscp;
		flowHashCalInfo->path5_key.ip_ecn			= in_ecn;

		if(1)
		{
			flowHashCalInfo->path5_key.ip_sa[3] = saddr;
			flowHashCalInfo->path5_key.ip_da[3] = daddr;
		}

		flowHashCalInfo->path5_key.l4_src_port		=  sport;
		flowHashCalInfo->path5_key.l4_dst_port		=  dport;

	}
}

int rtk_fc_proc_hash_test(struct seq_file *s, void *v)
{
	rtk_rg_asic_flow_hash_crc_t flow_hash_crc;
	rtk_rg_asic_flow_hash_cal_info_t flowHashCalInfo;
	int k, collision = 0, total_flow = 0, baseHwFlowIdx_cnt = 0;
#if 1
	int i,j;
	uint32 base_ip = 0xc0a8000b, remote_ip = 0x4e257c73, onu_gateway= 0x4e257c6e;
	uint32 base_port = 35000;
#endif
	uint32 baseHwFlowIdx, search_index;

	memset(&tmp_flowTbl[0],0,sizeof(uint8)*(RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE) );
	memset(&tmp_flowTbl_bucket[0],0,sizeof(uint8)*(RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE));
	
#if 1
	flowHashCalInfo_set(&flowHashCalInfo, 0/*in_spa*/, 0/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
			0xc0a8010a/*saddr*/, 
			0xc0a80114/*daddr*/, 
			11111/*sport*/, 
			22222/*dport*/);
	rtk_rg_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);

	printk("fc_db.flowHashWayShift = %d\n",fc_db.flowHashWayShift);
	// US
	for(i = 0;i<10 ; i++)
	{
		//printk("Now sip: %x\n", base_ip+i);
		for(j = 0; j < 400 ; j++)
		{
			flowHashCalInfo_set(&flowHashCalInfo, 0/*in_spa*/, 0/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
				base_ip+i/*saddr*/, 
				remote_ip/*daddr*/, 
				base_port+i+10*j/*sport*/, 
				base_port+i+10*j/*dport*/);
			rtk_rg_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);

			baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);
			tmp_flowTbl_bucket[baseHwFlowIdx]++;
			//printk("baseHwFlowId : %d \n",  baseHwFlowIdx);
			for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
			{
				search_index = baseHwFlowIdx+k;

				if(tmp_flowTbl[search_index] ==0)
				{
					tmp_flowTbl[search_index]=1;
					break;
				}
				
				if(k == ((1<<fc_db.flowHashWayShift) -1) )
				{		
					collision++;
				}
			
			}
			//printk("Now port: %d\n", base_port+i+10*j);
			total_flow ++;
		}
	}

	// DS
	for(i = 0;i<10 ; i++)
	{
		//printk("Now sip: %x\n", base_ip+i);
		for(j = 0; j < 400 ; j++)
		{
			flowHashCalInfo_set(&flowHashCalInfo, 0/*in_spa*/, 0/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
				remote_ip/*saddr*/, 
				onu_gateway/*daddr*/, 
				base_port+i+10*j/*sport*/, 
				base_port+i+10*j/*dport*/);
			rtk_rg_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);
			baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);
			tmp_flowTbl_bucket[baseHwFlowIdx]++;
			//printk("baseHwFlowId : %d \n",  baseHwFlowIdx);
			for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
			{
				search_index = baseHwFlowIdx+k;

				if(tmp_flowTbl[search_index] ==0)
				{
					tmp_flowTbl[search_index]=1;
					break;
				}
				
				if(k == ((1<<fc_db.flowHashWayShift) -1) )
				{		
					collision++;
				}
			
			}
			//printk("Now port: %d\n", base_port+i+10*j);
			total_flow ++;
		}
	}
	for( i = 0 ; i < RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE ; i++)
	{
		if(tmp_flowTbl_bucket[i] == 0)
			continue;
		baseHwFlowIdx_cnt ++;
		//printk("baseHwFlowId[%d] : %d \n", i, tmp_flowTbl_bucket[i]);
	}
#else //Another test
	///LAN 0
	flowHashCalInfo_set(&flowHashCalInfo, 0/*in_spa*/, 0/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
				0x0a000001/*saddr 10.0.0.1*/, 
				0x64000001/*daddr 100.0.0.1*/, 
				1024/*sport*/, 
				1024/*dport*/);
	rtk_rg_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);
	baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);

	for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
	{
		search_index = baseHwFlowIdx+k;
		
		if(tmp_flowTbl[search_index]!=0)
		{
			if(k== ((1<<fc_db.flowHashWayShift)-1) )
				collision++;
			else
				continue;
		}
		else
		{
			tmp_flowTbl[search_index]=1;
			break;
		}
	}
	total_flow++;


	flowHashCalInfo_set(&flowHashCalInfo, 7/*in_spa*/, 51/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
				0x64000001/*saddr 10.0.0.1*/, 
				0x0a000001/*daddr 100.0.0.1*/, 
				1024/*sport*/, 
				1024/*dport*/);
	rtk_rg_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);
	baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);

	for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
	{
		search_index = baseHwFlowIdx+k;
		
		if(tmp_flowTbl[search_index]!=0)
		{
			if(k== ((1<<fc_db.flowHashWayShift)-1) )
				collision++;
			else
				continue;
		}
		else
		{
			tmp_flowTbl[search_index]=1;
			break;
		}
	}

	total_flow++;
	flowHashCalInfo_set(&flowHashCalInfo, 0/*in_spa*/, 52/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
				0x0a000002/*saddr 10.0.0.1*/, 
				0x64000002/*daddr 100.0.0.1*/, 
				1024/*sport*/, 
				1024/*dport*/);
	rtk_rg_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);
	baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);

	for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
	{
		search_index = baseHwFlowIdx+k;
		
		if(tmp_flowTbl[search_index]!=0)
		{
			if(k== ((1<<fc_db.flowHashWayShift)-1) )
				collision++;
			else
				continue;
		}
		else
		{
			tmp_flowTbl[search_index]=1;
			break;
		}
	}
	total_flow++;
	flowHashCalInfo_set(&flowHashCalInfo, 7/*in_spa*/, 52/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
					0x64000002/*saddr 10.0.0.1*/, 
					0x0a000002/*daddr 100.0.0.1*/, 
					1024/*sport*/, 
					1024/*dport*/);
		rtk_8277c_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);
		baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);
	
		for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
		{
			search_index = baseHwFlowIdx+k;
			
			if(tmp_flowTbl[search_index]!=0)
			{
				if(k== ((1<<fc_db.flowHashWayShift)-1) )
					collision++;
				else
					continue;
			}
			else
			{
				tmp_flowTbl[search_index]=1;
				break;
			}
		}
	total_flow++;

	//LAN 1
	flowHashCalInfo_set(&flowHashCalInfo, 1/*in_spa*/, 53/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
				0x0a000003/*saddr 10.0.0.1*/, 
				0x64000003/*daddr 100.0.0.1*/, 
				1024/*sport*/, 
				1024/*dport*/);
	rtk_8277c_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);
	baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);

	for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
	{
		search_index = baseHwFlowIdx+k;
		
		if(tmp_flowTbl[search_index]!=0)
		{
			if(k== ((1<<fc_db.flowHashWayShift)-1) )
				collision++;
			else
				continue;
		}
		else
		{
			tmp_flowTbl[search_index]=1;
			break;
		}
	}
	total_flow++;


	flowHashCalInfo_set(&flowHashCalInfo, 7/*in_spa*/, 53/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
				0x64000003/*saddr 10.0.0.1*/, 
				0x0a000003/*daddr 100.0.0.1*/, 
				1024/*sport*/, 
				1024/*dport*/);
	rtk_8277c_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);
	baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);

	for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
	{
		search_index = baseHwFlowIdx+k;
		
		if(tmp_flowTbl[search_index]!=0)
		{
			if(k== ((1<<fc_db.flowHashWayShift)-1) )
				collision++;
			else
				continue;
		}
		else
		{
			tmp_flowTbl[search_index]=1;
			break;
		}
	}
	total_flow++;
	flowHashCalInfo_set(&flowHashCalInfo, 1/*in_spa*/, 54/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
				0x0a000004/*saddr 10.0.0.1*/, 
				0x64000004/*daddr 100.0.0.1*/, 
				1024/*sport*/, 
				1024/*dport*/);
	rtk_8277c_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);
	baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);

	for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
	{
		search_index = baseHwFlowIdx+k;
		
		if(tmp_flowTbl[search_index]!=0)
		{
			if(k== ((1<<fc_db.flowHashWayShift)-1) )
				collision++;
			else
				continue;
		}
		else
		{
			tmp_flowTbl[search_index]=1;
			break;
		}
	}
	total_flow++;
	flowHashCalInfo_set(&flowHashCalInfo, 7/*in_spa*/, 54/*in_cvid*/, 0/*in_cpri*/, 0/*in_ccfi*/, 0/*in_tcp*/, 0/*in_dscp*/, 0/*in_ecn*/, 
					0x64000004/*saddr 10.0.0.1*/, 
					0x0a000004/*daddr 100.0.0.1*/, 
					1024/*sport*/, 
					1024/*dport*/);
		rtk_8277c_asic_flow_hash_crc_cal(&flowHashCalInfo, &flow_hash_crc);
		baseHwFlowIdx = (flow_hash_crc.crc16 & fc_db.flowEntIdxMask) / (RTK_FC_TABLESIZE_HASH_FLOW_MAX/RTK_FC_TABLESIZE_HASH_FLOW);
	
		for(k = 0; k <(1<<fc_db.flowHashWayShift) ; k++)
		{
			search_index = baseHwFlowIdx+k;
			
			if(tmp_flowTbl[search_index]!=0)
			{
				if(k== ((1<<fc_db.flowHashWayShift)-1) )
					collision++;
				else
					continue;
			}
			else
			{
				tmp_flowTbl[search_index]=1;
				break;
			}
		}
		total_flow++;
	
#endif	
	printk("=== collision: %d total flow = %d baseHwFlowIdx_cnt = %d===\n", collision, total_flow, baseHwFlowIdx_cnt);
	return SUCCESS;
}
#endif

#if 0//defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int rtk_fc_proc_ldma_test_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);
	
#if defined(CONFIG_FC_RTL9607F_SERIES)
{
#ifdef CONFIG_ARCH_HAS_PMEM_API
extern void arch_invalidate_pmem(void *addr, size_t size);
#define LDMA_TEST_DMA_MAP_ONCE (1)
#endif
#define LDMA_TEST_BYTE_NUM (2048)
#define LDMA_TEST_BUF_NUM (2)

		struct device *pDev = &(ni_info_data.pdev->dev);
		uint32 i, j, ret, testTimes=10000/*(val > 0) ? val : 10000*/;
		uint8 *uncache_ary_a=NULL, *uncache_ary_b=NULL, *test_vir_addr=NULL;
		uint8 *cache_test_ary[LDMA_TEST_BUF_NUM]={NULL}, tmpBuf[LDMA_TEST_BYTE_NUM];
		dma_addr_t ary_a_phy_addr, ary_b_phy_addr, test_phy_addr[LDMA_TEST_BUF_NUM];
		uint32 isCopyToTmp = 0, buf_gap=(val > 0) ? val : 0;

		uncache_ary_a = (uint8 *)dma_alloc_coherent(pDev, LDMA_TEST_BYTE_NUM, &ary_a_phy_addr,	GFP_ATOMIC | GFP_DMA); //GFP_KERNEL
		if(uncache_ary_a==NULL)
		{
			rtlglue_printf("[WARNING] uncache_ary_a=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		uncache_ary_b = (uint8 *)dma_alloc_coherent(pDev, LDMA_TEST_BYTE_NUM, &ary_b_phy_addr, GFP_ATOMIC | GFP_DMA);
		if(uncache_ary_b==NULL)
		{
			rtlglue_printf("[WARNING] uncache_ary_b=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		for(i=0; i<LDMA_TEST_BYTE_NUM; i++)
		{
			*(volatile uint8 *)(uncache_ary_a + i) = 0xa0 + (i&0xf);
			*(volatile uint8 *)(uncache_ary_b + i) = 0xb0 + (i&0xf);
		}
		test_vir_addr = (uint8 *)kmalloc((LDMA_TEST_BYTE_NUM+buf_gap) * LDMA_TEST_BUF_NUM, GFP_ATOMIC | GFP_DMA);
		if(test_vir_addr==NULL)
		{
			rtlglue_printf("test_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		for(i=0; i<LDMA_TEST_BUF_NUM; i++)
		{
			cache_test_ary[i] = test_vir_addr + ((LDMA_TEST_BYTE_NUM+buf_gap) * i);
#if defined(LDMA_TEST_DMA_MAP_ONCE)	
			//invalidate test ary cache value
			test_phy_addr[i] = dma_map_single(pDev, cache_test_ary[i], LDMA_TEST_BYTE_NUM, DMA_FROM_DEVICE);
#endif
		}
		rtlglue_printf("[Buf num %d][Buf size %d][Buf gap %d] Test start(test_vir_addr 0x%px) ... isCopyToTmp=%d\n", LDMA_TEST_BUF_NUM, LDMA_TEST_BYTE_NUM, buf_gap, test_vir_addr, isCopyToTmp);
		rtlglue_printf("Do dma_map_single once and mb \n");
		//test start
		aal_ldma_init();
		for(i=0; i<testTimes; i++)
		{
			for(j=0; j<LDMA_TEST_BUF_NUM; j++)
			{
				//test ary cache value is same as ary_a
				memcpy(cache_test_ary[j], uncache_ary_a, LDMA_TEST_BYTE_NUM);
				//invalidate test ary cache value
#if defined(LDMA_TEST_DMA_MAP_ONCE)
				arch_invalidate_pmem(cache_test_ary[j], LDMA_TEST_BYTE_NUM);
#else
				test_phy_addr[j] = dma_map_single(pDev, cache_test_ary[j], LDMA_TEST_BYTE_NUM, DMA_FROM_DEVICE);
#endif
			}
			for(j=0; j<LDMA_TEST_BUF_NUM; j++)
			{
				//ldma arb_b to test ary
				ret = aal_ldma_sync_copy(LDMA_RCPU_QUEUE_NUMBER_MIN, test_phy_addr[j], ary_b_phy_addr, LDMA_TEST_BYTE_NUM);
				if(ret != AAL_E_OK)
				{
					rtlglue_printf("\033[1;33;41m[WARNING] LDMA copy fail ... copySize=%u , ret=%d \033[0m\n", LDMA_TEST_BYTE_NUM, ret);
					goto ERR_RETURN;
				}
				mb();
				if(memcmp(cache_test_ary[j], uncache_ary_b, LDMA_TEST_BYTE_NUM))
				{
					rtlglue_printf("\033[1;33;41m[WARNING][1st memcmp] test ary(0x%px) is not same as ary_b ... loopTimes=%d bufIdx=%d \033[0m\n", cache_test_ary[j], i, j);
					dump_packet(cache_test_ary[j], LDMA_TEST_BYTE_NUM, "[1st memcmp] test_ary");
					//invalidate test ary cache value and compare again
#if defined(LDMA_TEST_DMA_MAP_ONCE)
					arch_invalidate_pmem(cache_test_ary[j], LDMA_TEST_BYTE_NUM);
#else					
					dma_unmap_single(pDev, test_phy_addr[j], LDMA_TEST_BYTE_NUM, DMA_FROM_DEVICE);
#endif
					if(memcmp(cache_test_ary[j], uncache_ary_b, LDMA_TEST_BYTE_NUM))
					{
						rtlglue_printf("\033[1;33;41m[WARNING][2st memcmp] test ary(0x%px) is not same as ary_b ... loopTimes=%d bufIdx=%d \033[0m\n", cache_test_ary[j], i, j);
						dump_packet(cache_test_ary[j], LDMA_TEST_BYTE_NUM, "[2st memcmp] test_ary");
					}
					else
					{
						rtlglue_printf("Invalidate test ary and compare again ... OK \n");
					}
					goto ERR_RETURN;
				}
#if !defined(LDMA_TEST_DMA_MAP_ONCE)
				dma_unmap_single(pDev, test_phy_addr[j], LDMA_TEST_BYTE_NUM, DMA_FROM_DEVICE);
#endif
				//copy to tmpBuf
				if(isCopyToTmp)
					memcpy(tmpBuf, cache_test_ary[j], LDMA_TEST_BYTE_NUM);
			}
		}
		rtlglue_printf("[Buf num %d][Buf size %d] Test %d times OK !! \n", LDMA_TEST_BUF_NUM, LDMA_TEST_BYTE_NUM, testTimes);
#if defined(LDMA_TEST_DMA_MAP_ONCE)
		for(i=0; i<LDMA_TEST_BUF_NUM; i++)
			dma_unmap_single(pDev, test_phy_addr[i], LDMA_TEST_BYTE_NUM, DMA_FROM_DEVICE);
#endif
}
#else
	if(val > 0)
	{
		int ret, i;
		uint32 copySize = val, avgTimes=10000;
		void *src_vir_addr=NULL, *dst_vir_addr=NULL;
		dma_addr_t src_phy_addr, dst_phy_addr;
		
		src_vir_addr = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		
		src_phy_addr = virt_to_phys(src_vir_addr);
		dst_phy_addr = virt_to_phys(dst_vir_addr);
		
		//rtlglue_printf("dst_phy_addr=0x%x src_phy_addr=0x%x \n", (uint32)dst_phy_addr, (uint32)src_phy_addr);
		aal_ldma_init();
		for(i=0; i<avgTimes; i++)
		{
			//PROFILE_START;
			ret = aal_ldma_sync_copy(LDMA_RCPU_QUEUE_NUMBER_MIN, dst_phy_addr, src_phy_addr, copySize);
			//PROFILE_END;
		}
		if(ret==AAL_E_OK)
			rtlglue_printf("LDMA copy done ... copySize=%u \n", copySize);
		else
			rtlglue_printf("LDMA copy fail ... copySize=%u , ret=%d\n", copySize, ret);
	}
	else
	{
		uint32 *src_uncache_val, *dst_uncache_val;
		uint32 src_phy_addr, dst_phy_addr;			
#if 0	
		struct device *dev = NULL;
		dev = &(ni_info_data.dev[0]->dev);		
		if(dev)
			rtlglue_printf("dev's init_name: %s \n", dev->init_name);
		else
			rtlglue_printf("[WARNING] dev is null !! \n");
		src_uncache_val = (uint32 *)dma_alloc_coherent(dev, sizeof(uint32), (dma_addr_t *)&src_phy_addr, GFP_ATOMIC);
		if(src_uncache_val==NULL)
		{
			rtlglue_printf("[WARNING] src_uncache_val is null !! \n");
			break;
		}
		dst_uncache_val = (uint32 *)dma_alloc_coherent(dev, sizeof(uint32), (dma_addr_t *)&dst_phy_addr, GFP_ATOMIC);
		if(dst_uncache_val==NULL)
		{
			rtlglue_printf("[WARNING] dst_uncache_val is null !! \n");
			break;
		}
#else
		ni_info_t *ni = &ni_info_data;

		src_uncache_val = (uint32 *)ni->swtxq[0][0].buf_base;
		src_phy_addr = (uint32)ni->swtxq[0][0].buf_dma_addr;
		dst_uncache_val = (uint32 *)ni->swtxq[0][1].buf_base;
		dst_phy_addr = (uint32)ni->swtxq[0][1].buf_dma_addr;
#endif
		rtlglue_printf("src_uncache_addr=%p src_phy_addr=0x%x \n", src_uncache_val, src_phy_addr);
		rtlglue_printf("dst_uncache_addr=%p dst_phy_addr=0x%x \n", dst_uncache_val, dst_phy_addr);
		
		*src_uncache_val = 5566;
		*dst_uncache_val = 7788;			
		rtlglue_printf("dst_uncache_val=%u src_uncache_val=%u \n", *dst_uncache_val, *src_uncache_val); 		
		aal_ldma_init();
		aal_ldma_sync_copy(LDMA_RCPU_QUEUE_NUMBER_MIN, dst_phy_addr, src_phy_addr, sizeof(uint32));
		rtlglue_printf("dst_uncache_val=%u src_uncache_val=%u \n", *dst_uncache_val, *src_uncache_val);
#if 0
		if(dev)
		{
			dma_free_noncoherent(dev, sizeof(uint32), (void *)src_uncache_val, (dma_addr_t)src_phy_addr);
			dma_free_noncoherent(dev, sizeof(uint32), (void *)dst_uncache_val, (dma_addr_t)dst_phy_addr);
		}
#endif		
	}
#endif	
ERR_RETURN:
	return count;
}
#endif

#if defined(CONFIG_RTK_PTOOL_CPU_PERF)
#if defined(CONFIG_FC_RTL9607C_SERIES)
#include <linux/ptool.h>
#include <asm/io.h>
#define REG32(reg) 	(*((volatile unsigned int *)(reg)))
#define LX0_BTG_BASED_ADDRESS 0xB8144000
#define LX1_BTG_BASED_ADDRESS 0xB800A000
#define LX2_BTG_BASED_ADDRESS 0xB814C000
#define LX3_BTG_BASED_ADDRESS 0xB8018000
int rtk_fc_proc_test_gdma_set(struct file *filp, const char *buff,unsigned long count, void *data, unsigned int burst_size)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc0000000|burst_size;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=4;
			while(1)//for(i=1; i<avgTimes;)
			{
				if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}
int rtk_fc_proc_test_gdma_4WORD_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	return rtk_fc_proc_test_gdma_set(filp, buff, count, data, 0x0);
}
int rtk_fc_proc_test_gdma_8WORD_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	return rtk_fc_proc_test_gdma_set(filp, buff, count, data, 0x40);
}
int rtk_fc_proc_test_gdma_16WORD_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	return rtk_fc_proc_test_gdma_set(filp, buff, count, data, 0x80);
}
int rtk_fc_proc_test_gdma_32WORD_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	return rtk_fc_proc_test_gdma_set(filp, buff, count, data, 0xc0);
}
int rtk_fc_proc_test_gdma_32WORD_0_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=1;
			while(1)//for(i=1; i<avgTimes;)
			{
				if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				/*if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_1_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			//REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=1;
			while(1)//for(i=1; i<avgTimes;)
			{
				/*if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				/*if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_2_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			//REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=1;
			while(1)//for(i=1; i<avgTimes;)
			{
				/*if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				/*if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_3_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			//REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=1;
			while(1)//for(i=1; i<avgTimes;)
			{
				/*if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_01_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=2;
			while(1)//for(i=1; i<avgTimes;)
			{
				if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				/*if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_02_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=2;
			while(1)//for(i=1; i<avgTimes;)
			{
				if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				/*if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}/*
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}


int rtk_fc_proc_test_gdma_32WORD_03_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=2;
			while(1)//for(i=1; i<avgTimes;)
			{
				if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				/*if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_12_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			//REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=2;
			while(1)//for(i=1; i<avgTimes;)
			{
				/*if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}/*
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}


int rtk_fc_proc_test_gdma_32WORD_13_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			//REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=2;
			while(1)//for(i=1; i<avgTimes;)
			{
				/*if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				/*if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_23_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			//REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=2;
			while(1)//for(i=1; i<avgTimes;)
			{
				/*if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}


int rtk_fc_proc_test_gdma_32WORD_012_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=3;
			while(1)//for(i=1; i<avgTimes;)
			{
				if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}/*
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_013_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=2;
			while(1)//for(i=1; i<avgTimes;)
			{
				if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				/*if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_023_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			//REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=3;
			while(1)//for(i=1; i<avgTimes;)
			{
				if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				/*if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_32WORD_123_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000, kick_off=0x0, reg_value;
		void *src_vir_addr0=NULL, *dst_vir_addr0=NULL;
		void *src_vir_addr1=NULL, *dst_vir_addr1=NULL;
		void *src_vir_addr2=NULL, *dst_vir_addr2=NULL;
		void *src_vir_addr3=NULL, *dst_vir_addr3=NULL;
		unsigned int src_uncache_addr[4], dst_uncache_addr[4];
		unsigned long flags;

		src_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr0==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr0 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr0==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr1==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr1 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr1==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr2==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr2 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr2==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		src_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr3==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr3 = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr3==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}

		src_uncache_addr[0] = ((u32)src_vir_addr0)|0xa0000000;
		dst_uncache_addr[0] = ((u32)dst_vir_addr0)|0xa0000000;
		src_uncache_addr[1] = ((u32)src_vir_addr1)|0xa0000000;
		dst_uncache_addr[1] = ((u32)dst_vir_addr1)|0xa0000000;
		src_uncache_addr[2] = ((u32)src_vir_addr2)|0xa0000000;
		dst_uncache_addr[2] = ((u32)dst_vir_addr2)|0xa0000000;
		src_uncache_addr[3] = ((u32)src_vir_addr3)|0xa0000000;
		dst_uncache_addr[3] = ((u32)dst_vir_addr3)|0xa0000000;

		//rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		//rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		
		reg_value=REG32(0xb8000600);
		REG32(0xb8000600)=reg_value|0x300;
		reg_value=REG32(0xb800063c);
		reg_value|=0xc0;
		REG32(0xb800063c)=reg_value&0x7fffffff;
		//lx0
		REG32(LX0_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX0_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX0_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx1
		REG32(LX1_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX1_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX1_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx2
		REG32(LX2_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX2_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX2_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		//lx3
		REG32(LX3_BTG_BASED_ADDRESS+0x20)=src_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x24)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x60)=dst_uncache_addr[0];
		REG32(LX3_BTG_BASED_ADDRESS+0x64)=val;
		REG32(LX3_BTG_BASED_ADDRESS+0x4)=0x80000000;	//enable interrupt
		REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
		kick_off=0xc00000c0;
		local_irq_save(flags);
		profile_start();
		do{
			PROFILE_START;
			//REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
			i=3;
			while(1)//for(i=1; i<avgTimes;)
			{
				/*if((REG32(LX0_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX0_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX0_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}*/
				if((REG32(LX1_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX1_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX1_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX2_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX2_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX2_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
				if((REG32(LX3_BTG_BASED_ADDRESS+0x8)&0x80000000)){
					REG32(LX3_BTG_BASED_ADDRESS+0x8)=0x80000000;	//clear interrput
					if(++i==avgTimes)break;
					REG32(LX3_BTG_BASED_ADDRESS)=kick_off;	//kick-off
				}
			}
			PROFILE_END;
		}while(0);
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("GDMA copy done ... copySize=%u \n", copySize);
	
ERR_RETURN:
		if(src_vir_addr0)kfree(src_vir_addr0);
		if(src_vir_addr1)kfree(src_vir_addr1);
		if(src_vir_addr2)kfree(src_vir_addr2);
		if(src_vir_addr3)kfree(src_vir_addr3);
		if(dst_vir_addr0)kfree(dst_vir_addr0);
		if(dst_vir_addr1)kfree(dst_vir_addr1);
		if(dst_vir_addr2)kfree(dst_vir_addr2);
		if(dst_vir_addr3)kfree(dst_vir_addr3);
	}
	return count;
}

int rtk_fc_proc_test_gdma_get(struct seq_file *s, void *v)
{
	rtlglue_printf("echo <copy_bytes> > /proc/fc/ctrl/test_gdma<burst_size>w;cat /proc/ptool/profile_dump\n");
	return 0;
}

int rtk_fc_proc_test_memcpy_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,count);

	if(val > 0)
	{
		int i;
		uint32 copySize = val, avgTimes=10000;
		void *src_vir_addr=NULL, *dst_vir_addr=NULL;
		unsigned int src_uncache_addr, dst_uncache_addr;
		unsigned long flags;

		src_vir_addr = kmalloc(copySize, GFP_ATOMIC);
		if(src_vir_addr==NULL)
		{
			rtlglue_printf("src_vir_addr=NULL, memory is not enough !! \n");
			goto ERR_RETURN;
		}
		dst_vir_addr = kmalloc(copySize, GFP_ATOMIC);
		if(dst_vir_addr==NULL)
		{
			rtlglue_printf("dst_vir_addr=NULL, memory is not enough !! \n");
			kfree(src_vir_addr);
			goto ERR_RETURN;
		}

		src_uncache_addr = ((u32)src_vir_addr)|0xa0000000;
		dst_uncache_addr = ((u32)dst_vir_addr)|0xa0000000;

		rtlglue_printf("dst_uncache_addr=0x%x src_uncache_addr=0x%x \n", dst_uncache_addr, src_uncache_addr);
		rtlglue_printf("dst_vir_addr=0x%x src_vir_addr=0x%x \n", dst_vir_addr, src_vir_addr);
		local_irq_save(flags);
		profile_start();
		for(i=0; i<avgTimes; i++)
		{
			PROFILE_START;
			dma_cache_wback_inv((unsigned long)dst_vir_addr,copySize);
			memcpy((void *)dst_vir_addr,(const void *)src_vir_addr,copySize);
			dma_cache_inv((unsigned long)src_vir_addr,copySize);
			PROFILE_END;
		}
		profile_stop();
		local_irq_restore(flags);
		rtlglue_printf("MEMCPY copy done ... copySize=%u \n", copySize);
		kfree(src_vir_addr);
		kfree(dst_vir_addr);
	}
ERR_RETURN:
	return count;
}
int rtk_fc_proc_test_memcpy_get(struct seq_file *s, void *v)
{
	rtlglue_printf("echo <copy_bytes> > /proc/fc/ctrl/test_memcpy;cat /proc/ptool/profile_dump\n");
	return 0;
}
#endif
#endif

int rtk_fc_proc_global_ctrl_get(struct seq_file *s, void *v)
{
	rtk_fc_proc_global_ctrl_flag_t i = 0;

	for(i = 0; i < FC_PROC_GLB_CTRL_MAX; i++) {

		PROC_PRINTF("%s: ", name_of_global_ctrl_flag[i]);
		switch(i) 
		{
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			case FC_PROC_GLB_CTRL_LAN_QUEUE_ABILITY_ENHANCE:
				PROC_PRINTF("%d ", fc_db.controlFuc.lan_queue_ability_enhance);		
				break;
			case FC_PROC_GLB_CTRL_ASIC_DRV_DEBUG:
				PROC_PRINTF("%d ", asic_debug);		
				break;
			case FC_PROC_GLB_CTRL_ACL_LAN_PORTMASK:
				PROC_PRINTF("0x%x ", fc_db.acl_lan_portmask);
				break;
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			case FC_PROC_GLB_CTRL_HASH_CRC_DEBUG:
				PROC_PRINTF("%d ", fc_db.controlFuc.hash_crc_debug);
				break;
			case FC_PROC_GLB_CTRL_TX_WITH_HDR_DEBUG:
				PROC_PRINTF("%d ", fc_db.controlFuc.tx_with_hdr_debug);
				break;
#endif
			case FC_PROC_GLB_CTRL_TX_BUSY_TO_PS:
				PROC_PRINTF("%d ", fc_db.controlFuc.tx_busy_to_ps);
				break;
#if defined(CONFIG_RTK_L34_XPON_PLATFORM) && defined(CONFIG_FC_RTL9607C_SERIES)
			case FC_PROC_GLB_CTRL_WIFI_RX_HASH:
				PROC_PRINTF("%d ", RTK_FC_HELPER_MGR_CHECK_CONFIG_SETTING(FC_MGR_GLB_CONFIG_WIFI_RX_HASH));
				break;
			case FC_PROC_GLB_CTRL_WIFI_RX_GMAC_AUTO_SEL:
				PROC_PRINTF("%d ", RTK_FC_HELPER_MGR_CHECK_CONFIG_SETTING(FC_MGR_GLB_CONFIG_WIFI_RX_GMAC_AUTO_SEL));
				break;
			case FC_PROC_GLB_CTRL_WIFI_TX_TRAP_HASH:
				PROC_PRINTF("%d ", fc_db.controlFuc.wifi_tx_trap_hash);
				break;
			case FC_PROC_GLB_CTRL_WIFI_TX_GMAC_AUTO_SEL:
				PROC_PRINTF("%d ", RTK_FC_HELPER_MGR_CHECK_CONFIG_SETTING(FC_MGR_GLB_CONFIG_WIFI_TX_GMAC_AUTO_SEL));
				break;
			case FC_PROC_GLB_CTRL_PON_DS_P7_LOOPBACK:
				PROC_PRINTF("%d ", RTK_FC_HELPER_MGR_CHECK_CONFIG_SETTING(FC_MGR_GLB_CONFIG_PON_DS_P7_LOOPBACK));
				break;
#endif
#if defined(CONFIG_RTK_L34_G3_PLATFORM) && defined(CONFIG_FC_RTL8277C_SERIES)
			case FC_PROC_GLB_CTRL_PON_PM_CNT_MIX_MODE:
				PROC_PRINTF("%d ", fc_db.controlFuc.pon_pm_cnt_mix_mode);
				break;
#endif
#if defined(FC_F_SHORTCUT_EARLYCHECK)
			case FC_PROC_GLB_CTRL_SHORTCUT_EARLYCHECK:
				PROC_PRINTF("%d ", fc_db.controlFuc.shortcut_earlycheck_en);
				break;
			case FC_PROC_GLB_CTRL_HW_SW_HYBRID_FWD:
				PROC_PRINTF("%d ", fc_db.controlFuc.hw_sw_hybrid_fwd);
				break;
			case FC_PROC_GLB_CTRL_HW_SW_HYBRID_FWD_TO_PE:
				PROC_PRINTF("%d ", fc_db.controlFuc.hw_sw_hybrid_fwd_to_pe);
				break;
#endif
			case FC_PROC_GLB_CTRL_ACL_PARA_DUMP:
				PROC_PRINTF("%d ", fc_db.controlFuc.acl_para_dump);
				break;
			case FC_PROC_GLB_CTRL_EVENT_REC_CTRL:
				PROC_PRINTF("%d ", fc_db.controlFuc.event_rec_ctrl);
				break;
			case FC_PROC_GLB_CTRL_MC2UC:
				PROC_PRINTF("%d ", fc_db.controlFuc.mc2uc_en);
				break;
			case FC_PROC_GLB_CTRL_BC_FLOW_DELAY:
				PROC_PRINTF("%d ", fc_db.controlFuc.bc_flowDelay_time_unit1ms);
				break;
			case FC_PROC_GLB_CTRL_WIFI_MULTIBAND_ACC:
				PROC_PRINTF("%d ", fc_db.controlFuc.wifi_multiband_acc);
				break;
			case FC_PROC_GLB_CTRL_ETHPORT_TRUNKING:
				PROC_PRINTF("%d \n", fc_db.controlFuc.ethport_trunking);
				PROC_PRINTF("1st_lanport_mask 0x%x, 2nd_lanport_mask 0x%x, 3rd_lanport_mask 0x%x, 4th_lanport_mask 0x%x, 5th_lanport_mask 0x%x\n", fc_db.controlFuc.lanport_trunking_port_1st_mask, fc_db.controlFuc.lanport_trunking_port_2nd_mask, fc_db.controlFuc.lanport_trunking_port_3rd_mask, fc_db.controlFuc.lanport_trunking_port_4th_mask, fc_db.controlFuc.lanport_trunking_port_5th_mask);
				PROC_PRINTF("1st_wanport_mask 0x%x, 2nd_wanport_mask 0x%x, 3rd_wanport_mask 0x%x, 4th_wanport_mask 0x%x, 5th_wanport_mask 0x%x\n", fc_db.controlFuc.wanport_trunking_port_1st_mask, fc_db.controlFuc.wanport_trunking_port_2nd_mask, fc_db.controlFuc.wanport_trunking_port_3rd_mask, fc_db.controlFuc.wanport_trunking_port_4th_mask, fc_db.controlFuc.wanport_trunking_port_5th_mask);
				break;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			case FC_PROC_GLB_CTRL_NETIFMIB_TIMER:
				PROC_PRINTF("%d ", fc_db.controlFuc.netifmib_timer);
				break;			
			case FC_PROC_GLB_CTRL_IPSEC_SKIP_CT:
				PROC_PRINTF("%d ", fc_db.controlFuc.ipsec_skip_ct);
				break;
			case FC_PROC_GLB_CTRL_PON_FRAG_SIZE:
				PROC_PRINTF("%d ", fc_db.controlFuc.pon_frag_size);
				break;
			case FC_PROC_GLB_CTRL_BR_FLOW_MTU_CHECK_EN:
				PROC_PRINTF("%d ", fc_db.controlFuc.br_flow_mtu_check_en);
				break;
#endif		
			case FC_PROC_GLB_CTRL_INTERNAL_DEBUG:
				PROC_PRINTF("0x%x (v2)", fc_db.controlFuc.internal_Debug);
				break;
			case FC_PROC_GLB_CTRL_L2GRE_HW_TUNNEL_LIMIT:
				PROC_PRINTF("%d ", fc_db.controlFuc.l2gre_tunnel_hwNetif_limit);
				break;

			default:
				PROC_PRINTF("N/A");
				break;
		}
		
		PROC_PRINTF("\n");
	}
	return SUCCESS;
}


int rtk_fc_proc_global_ctrl_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int length = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str, *cmd_str;
	int cmd_flag;
	
	if (buff)
	{
		//fc_db.debug_level=simple_strtoul(tmpBuf, NULL, 16);
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, length);

		strptr=tmpBuf;

		cmd_str=strsep(&strptr," ");//get flow_operation
		if(cmd_str==NULL){
			goto INVALID_PARA;
		}
		
		split_str=strsep(&strptr," ");	//get flow delete type
		if(split_str==NULL){
			goto INVALID_PARA;
		}
		cmd_flag = simple_strtol(split_str, NULL, 0);

#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_LAN_QUEUE_ABILITY_ENHANCE])==0)
		{
			fc_db.controlFuc.lan_queue_ability_enhance = cmd_flag;
		}		
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_ASIC_DRV_DEBUG])==0)
		{
			asic_debug = cmd_flag;
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_ACL_LAN_PORTMASK])==0)
		{
			if(fc_db.acl_lan_portmask)
				assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_ACL_LAN_PORTMASK_RESET));
			fc_db.acl_lan_portmask = cmd_flag;
			if(cmd_flag < 0){
				fc_db.acl_lan_portmask = RTK_FC_RT_ACL_PORTMASK_INVALID;
				assert_ok(_rtk_rg_aclAndCfReservedRuleAddSpecial(RTK_CLS_TYPE_USER_ACL_REARRANGE, NULL));
			}else if(cmd_flag > 0){
				rtlglue_printf("NOTE: reflash reserve acl only (not include user acl)\n");
				assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_ACL_LAN_PORTMASK_RESET, NULL));
			}
		}
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_HASH_CRC_DEBUG])==0)
		{
			fc_db.controlFuc.hash_crc_debug = cmd_flag;
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_TX_WITH_HDR_DEBUG])==0)
		{
			fc_db.controlFuc.tx_with_hdr_debug = cmd_flag;
		}
#endif
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_TX_BUSY_TO_PS])==0)
		{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			rtlglue_printf("ERROR: not support this function in this chip.\n");
#else
			fc_db.controlFuc.tx_busy_to_ps = cmd_flag;
#endif
		}
#if defined(CONFIG_RTK_L34_XPON_PLATFORM) && defined(CONFIG_FC_RTL9607C_SERIES)
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_WIFI_RX_HASH])==0)
		{
			RTK_FC_HELPER_MGR_GLB_CONFIG_SET(FC_MGR_GLB_CONFIG_WIFI_RX_HASH, (void *)&cmd_flag);
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_WIFI_RX_GMAC_AUTO_SEL])==0)
		{
			RTK_FC_HELPER_MGR_GLB_CONFIG_SET(FC_MGR_GLB_CONFIG_WIFI_RX_GMAC_AUTO_SEL, (void *)&cmd_flag);
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_WIFI_TX_TRAP_HASH])==0)
		{
			if(cmd_flag == 1 && RTK_FC_HELPER_MGR_CHECK_CONFIG_SETTING(FC_MGR_GLB_CONFIG_WIFI_TX_TRAP_HASH)==0 )
				rtlglue_printf("ERROR: please turn on FC_MGR_GLB_CONFIG_WIFI_TX_TRAP_HASH first.\n");
			else {
				fc_db.controlFuc.wifi_tx_trap_hash = cmd_flag;
				(void)rtk_trap_cpuTrapHashState_set( fc_db.controlFuc.wifi_tx_trap_hash ? ENABLED : DISABLED);
			}
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_WIFI_TX_GMAC_AUTO_SEL])==0)
		{
			if(cmd_flag == 1 && RTK_FC_HELPER_MGR_CHECK_CONFIG_SETTING(FC_PROC_GLB_CTRL_WIFI_TX_GMAC_AUTO_SEL)==0 )
				rtlglue_printf("ERROR: please turn on CONFIG_FC_WIFI_TX_GMAC_TRUNKING_SUPPORT first.\n");
			else {
				fc_db.controlFuc.wifi_tx_gmac_auto_sel_en = cmd_flag;
			}
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_PON_DS_P7_LOOPBACK])==0)
		{
			RTK_FC_HELPER_MGR_GLB_CONFIG_SET(FC_MGR_GLB_CONFIG_PON_DS_P7_LOOPBACK, (void *)&cmd_flag);
		}
#endif
#if defined(CONFIG_RTK_L34_G3_PLATFORM) && defined(CONFIG_FC_RTL8277C_SERIES)
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_PON_PM_CNT_MIX_MODE])==0)
		{
			if(fc_db.controlFuc.pon_pm_cnt_mix_mode == 0 && cmd_flag == 1) {
				fc_db.controlFuc.pon_pm_cnt_mix_mode = cmd_flag;
				// add reserved acl
				assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_CHT_MC_PATCH, NULL));
			}else if(fc_db.controlFuc.pon_pm_cnt_mix_mode == 1 && cmd_flag == 0) {
				fc_db.controlFuc.pon_pm_cnt_mix_mode = cmd_flag;
				// del reserved acl
				assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_CHT_MC_PATCH));
			}
		}
#endif
#if defined(FC_F_SHORTCUT_EARLYCHECK)
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_SHORTCUT_EARLYCHECK])==0)
		{
			fc_db.controlFuc.shortcut_earlycheck_en = cmd_flag;
		}		
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_HW_SW_HYBRID_FWD])==0)
		{
			fc_db.controlFuc.hw_sw_hybrid_fwd = cmd_flag;
			rtk_fc_flow_flush();
		}	
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_HW_SW_HYBRID_FWD_TO_PE])==0)
		{
			fc_db.controlFuc.hw_sw_hybrid_fwd_to_pe = cmd_flag;
			rtk_fc_flow_flush();
		}
#endif		
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_ACL_PARA_DUMP])==0)
		{
			fc_db.controlFuc.acl_para_dump = cmd_flag;
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_EVENT_REC_CTRL])==0)
		{
			fc_db.controlFuc.event_rec_ctrl = cmd_flag;
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_MC2UC])==0)
		{
			rtk_fc_multicast_flush_patten patten={0};	
			if(fc_db.controlFuc.mc2uc_en==1 && cmd_flag==0)
			{
				rtk_fc_multicast_flowFlush_strategy(MC_FLOW_FLUSH_BY_DMAC_TRANS_ACTION,&patten);
			}
			else if(fc_db.controlFuc.mc2uc_en==0 && cmd_flag==1)
			{
				rtk_fc_multicast_flowFlush_strategy(MC_FLOW_FLUSH_ALL,&patten);
			}
			fc_db.controlFuc.mc2uc_en = cmd_flag;
		}		
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_BC_FLOW_DELAY])==0)
		{
			fc_db.controlFuc.bc_flowDelay_time_unit1ms = cmd_flag;
		}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_NETIFMIB_TIMER])==0)
		{
			fc_db.controlFuc.netifmib_timer = cmd_flag;
		}
	
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_IPSEC_SKIP_CT])==0)
		{
			fc_db.controlFuc.ipsec_skip_ct = cmd_flag;
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_PON_FRAG_SIZE])==0)
		{
			fc_db.controlFuc.pon_frag_size = cmd_flag;
			if(cmd_flag <= 0){
				assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_PON_OVER_FRAG_SIZE_TRAP));
			}else if(cmd_flag > 0){
				assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_PON_OVER_FRAG_SIZE_TRAP, NULL));
			}
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_BR_FLOW_MTU_CHECK_EN])==0)
		{
			fc_db.controlFuc.br_flow_mtu_check_en = cmd_flag;
		}
#endif	

		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_INTERNAL_DEBUG])==0)
		{
			fc_db.controlFuc.internal_Debug = cmd_flag;
			if(fc_db.controlFuc.internal_Debug & 0x1)
			{
				if(fc_mgr_db._rtk_fc_wifi_event_handling_cb_register) 
				{
					rtk_fc_wifi_event_t event = {0};
					event.code = RTK_FC_WIFI_EVENT_DISABLE_ACC;
					fc_mgr_db._rtk_fc_wifi_event_handling_cb_register(&event);
				}
			}
			else if(fc_db.controlFuc.internal_Debug & 0x2)
			{
				if(fc_mgr_db._rtk_fc_wifi_event_handling_cb_register) 
				{				
					rtk_fc_wifi_event_t event = {0};
					event.code = RTK_FC_WIFI_EVENT_ENABLE_ACC;
					fc_mgr_db._rtk_fc_wifi_event_handling_cb_register(&event);
				}
			}
			
		}
		if(strcasecmp(cmd_str, name_of_global_ctrl_flag[FC_PROC_GLB_CTRL_L2GRE_HW_TUNNEL_LIMIT])==0)
		{
			fc_db.controlFuc.l2gre_tunnel_hwNetif_limit = cmd_flag;
		}
	}

	return len;
	
INVALID_PARA:

	rtlglue_printf("ERROR - supported setting:\n");
	rtk_fc_proc_global_ctrl_get(NULL, NULL);

	return len;
}

int rtk_fc_proc_acl_multiple_hit_cfg_get(struct seq_file *s, void *v)
{
	rtlglue_printf("Usage:\n");
	rtlglue_printf("0: disable trap and policer multiple hit function\n");
	rtlglue_printf("1: enable trap and policer multiple hit\n");
	PROC_PRINTF("Current status: %d\n", fc_db.controlFuc.acl_multiple_hit_cfg?1:0);
	return SUCCESS;
}


int rtk_fc_proc_acl_multiple_hit_cfg_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	int ret = len;
	if(val)
		val = 1;
	else
		val = 0;

	if(fc_db.controlFuc.acl_multiple_hit_cfg != val)
	{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM) || defined(CONFIG_FC_G3_G3LITE_SERIES)
		if(val == 1){
			WARNING("This chip not support this function");
			return ret;
		}else
#endif
		{
			fc_db.controlFuc.acl_multiple_hit_cfg = val;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			_rtk_rg_aclAndCfReservedRuleAddSpecial(RTK_CLS_TYPE_USER_ACL_REARRANGE, NULL);
#endif
		}
	}
	rtk_fc_proc_acl_multiple_hit_cfg_get(NULL,NULL);
	return ret;
}

int rtk_fc_proc_acl_redirect_open_ran_get(struct seq_file *s, void *v)
{
	rtlglue_printf("Redirect open ran function configuration:\n");
	PROC_PRINTF("\t status: %d (0: disable, else: bit 0 enable for downstream, bit 1 enable for upstream)\n", fc_db.controlFuc.acl_redirect_open_ran);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	PROC_PRINTF("[Downstream ACL rule is %s]\n", ((fc_db.controlFuc.acl_redirect_open_ran & 0x1)?"Enable":"Disable"));
	PROC_PRINTF("\t igr_vlan_vid: 0x%x (vaule 0 treat as untag)\n", fc_db.aclAndCfReservedRule.open_ran_redirect.igr_vlan_vid[0]);
	PROC_PRINTF("\t redirect_port: 0x%x\n", fc_db.aclAndCfReservedRule.open_ran_redirect.redirect_port_idx);
	PROC_PRINTF("\t egr_vlan_format: %d (0: keep original, 1: egress untag, 2: egress 1 tag with egr_vlan_vid)\n", fc_db.aclAndCfReservedRule.open_ran_redirect.vlan_format[0]);
	PROC_PRINTF("\t egr_vlan_vid: 0x%x (only valid when egr_vlan_format=2)\n", fc_db.aclAndCfReservedRule.open_ran_redirect.vlan_vid[0]);
	PROC_PRINTF("[Upstream ACL rule is %s]\n", ((fc_db.controlFuc.acl_redirect_open_ran & 0x2)?"Enable":"Disable"));
	PROC_PRINTF("\t us_igr_port_idx: 0x%x \n", fc_db.aclAndCfReservedRule.open_ran_redirect.us_igr_port_idx);
	PROC_PRINTF("\t us_igr_vlan_vid: 0x%x (vaule 0 treat as untag)\n", fc_db.aclAndCfReservedRule.open_ran_redirect.igr_vlan_vid[1]);
	PROC_PRINTF("\t us_redirect_stream: 0x%x\n", fc_db.aclAndCfReservedRule.open_ran_redirect.redirect_stream_idx);
	PROC_PRINTF("\t us_egr_vlan_format: %d (0: keep original, 1: egress untag, 2: egress 1 tag with egr_vlan_vid)\n", fc_db.aclAndCfReservedRule.open_ran_redirect.vlan_format[1]);
	PROC_PRINTF("\t us_egr_vlan_vid: 0x%x (only valid when egr_vlan_format=2)\n", fc_db.aclAndCfReservedRule.open_ran_redirect.vlan_vid[1]);
#endif

	return SUCCESS;
}


int rtk_fc_proc_acl_redirect_open_ran_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	char *strptr,*split_str;
	int value = 0;

	if(buff)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, CMD_BUFF_SIZE);

		strptr=tmpBuf;
		while(1)
		{
			split_str = strsep(&strptr," ");
			if(strcasecmp(split_str,"status") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				if(value < 0){
					goto exit;
				}
				fc_db.controlFuc.acl_redirect_open_ran = value;
			}
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			else if(strcasecmp(split_str,"igr_vlan_vid") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.aclAndCfReservedRule.open_ran_redirect.igr_vlan_vid[0] = value;
			}else if(strcasecmp(split_str,"redirect_port") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.aclAndCfReservedRule.open_ran_redirect.redirect_port_idx = value;
			}else if(strcasecmp(split_str,"egr_vlan_format") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.aclAndCfReservedRule.open_ran_redirect.vlan_format[0] = value;
			}else if(strcasecmp(split_str,"egr_vlan_vid") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.aclAndCfReservedRule.open_ran_redirect.vlan_vid[0] = value;
			}
			else if(strcasecmp(split_str,"us_igr_port_idx") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.aclAndCfReservedRule.open_ran_redirect.us_igr_port_idx = value;
			}else if(strcasecmp(split_str,"us_igr_vlan_vid") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.aclAndCfReservedRule.open_ran_redirect.igr_vlan_vid[1] = value;
			}else if(strcasecmp(split_str,"us_redirect_stream") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.aclAndCfReservedRule.open_ran_redirect.redirect_stream_idx = value;
			}else if(strcasecmp(split_str,"us_egr_vlan_format") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.aclAndCfReservedRule.open_ran_redirect.vlan_format[1] = value;
			}else if(strcasecmp(split_str,"us_egr_vlan_vid") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.aclAndCfReservedRule.open_ran_redirect.vlan_vid[1] = value;
			}
#endif
			else{
				rtlglue_printf("[ERROR] Wrong parameter\n");
				goto exit;
			}
			if(value < 0){
				goto exit;
			}
			if (strptr==NULL) break;
		}

#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		if(fc_db.controlFuc.acl_redirect_open_ran & 0x2){
			if(fc_db.streamidTbl[fc_db.aclAndCfReservedRule.open_ran_redirect.redirect_stream_idx].valid == 0)
				rtlglue_printf("[ERROR] Stream id table not ready...\n");
		}

		assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_OPEN_RAN_REDIRECT));

		if(fc_db.controlFuc.acl_redirect_open_ran)
			assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_OPEN_RAN_REDIRECT, NULL));
#endif
	}

exit:
	rtk_fc_proc_acl_redirect_open_ran_get(NULL,NULL);

	return len;
}

int _rtk_fc_proc_sendFromCpu_uasge(struct seq_file *s, void *v)
{
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("     echo \"[mode] [net_device] [packet content]\" > /proc/fc/ctrl/send_from_cpu \n");
	rtlglue_printf("[mode]:0: From CPU port to FC, 1: Local out, 2: Local out to PON\n");
	rtlglue_printf("Example:\n");
	rtlglue_printf("     Simulate from CPU port to FC: echo \"0 [net_device] [packet content]\" > /proc/fc/ctrl/send_from_cpu \n");
	rtlglue_printf("     Simulate Local out packet   : echo \"1 [net_device] [packet content]\" > /proc/fc/ctrl/send_from_cpu \n");
	rtlglue_printf("     Simulate Local out to PON   : echo \"2 [pon_sid] [packet content]\" > /proc/fc/ctrl/send_from_cpu \n");
	rtlglue_printf("\n");
	return 0;
}

//.unlockBefortWrite=1
int _rtk_fc_proc_sendFromCpu(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int i,j=0;
	struct sk_buff *new_skb;
	struct rt_skbuff *rtskb, rtNewSkb;
	char dev_name[16]={0};
	int offset=0;
	rtk_fc_pktHdr_t *pktHdr;
	uint8 tmpBuf[CMD_BUFF_SIZE] = {0};
	int mode = 0;
	bool is_found=FALSE, is_first=TRUE;
	uint32 pon_sid = 0;
	rtk_fc_egrInfo_t *p_egrExtraInfo;


#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	ca_eth_private_t *cep;
#endif

	pktHdr = RTK_FC_HELPER_FC_KMALLOC(sizeof(rtk_fc_pktHdr_t),GFP_ATOMIC);
	if(pktHdr == NULL){
		rtlglue_printf("[WARRING]pktHdr malloc failed\n");
		return len;
	}
	p_egrExtraInfo = RTK_FC_HELPER_FC_KMALLOC(sizeof(rtk_fc_egrInfo_t),GFP_ATOMIC);
	if(p_egrExtraInfo == NULL){
		rtlglue_printf("[WARRING]egrExtraInfo malloc failed\n");
		if(pktHdr) RTK_FC_HELPER_FC_KFREE(pktHdr, sizeof(rtk_fc_pktHdr_t));
		return len;
	}


	
	if((len>>1) > CMD_BUFF_SIZE){
		WARNING("max packet size is %d", CMD_BUFF_SIZE);
	}

	/* copy data to the buffer */
	strscpy(tmpBuf, buff, sizeof(tmpBuf));

	// fetch net_dev name or sid
	for(i=0;i<len;i++)
	{	
		if(i==0){
			if(tmpBuf[i]=='0'){
				rtlglue_printf(" - From CPU port to FC driver.\n");
				mode = 0;
			}else if(tmpBuf[i]=='1'){
				rtlglue_printf(" - Local out.\n");
				mode = 1;
			}else if(tmpBuf[i]=='2'){
				rtlglue_printf(" - Local out to pon port\n");
				mode = 2;
			}else{
				rtlglue_printf("[ERROR]Should decide send packet direction!");
				if(pktHdr) RTK_FC_HELPER_FC_KFREE(pktHdr, sizeof(rtk_fc_pktHdr_t));
				if(p_egrExtraInfo )RTK_FC_HELPER_FC_KFREE(p_egrExtraInfo, sizeof(*p_egrExtraInfo));
				return len;
			}
			
		}
			
		if(offset==0 && i!=0)
		{
			if(tmpBuf[i]!=' ')
			{
				if(i==16)
				{
					TRACE("CPU_DirectTX_ERROR: net_dev is not found.");
					if(pktHdr) RTK_FC_HELPER_FC_KFREE(pktHdr, sizeof(rtk_fc_pktHdr_t));
					if(p_egrExtraInfo )RTK_FC_HELPER_FC_KFREE(p_egrExtraInfo, sizeof(*p_egrExtraInfo));
					return len;
				}
				dev_name[j++]=tmpBuf[i];
			}else if(tmpBuf[i]==' ' && is_first==TRUE)
				is_first = FALSE;
			else
			{
				offset=i+1;
				break;
			}
		}
	}

	bzero(pktHdr,sizeof(rtk_fc_pktHdr_t));

	// alloc skb
	pktHdr->ingressPort.macPortIdx=RTK_FC_MAC_PORT_MAINCPU;

	RTK_FC_HOOK_PS_SKB_DEV_ALLOC_SKB(SKB_BUF_SIZE, &new_skb);
	if(new_skb==NULL) {
		RTK_FC_HELPER_FC_KFREE(pktHdr, sizeof(rtk_fc_pktHdr_t));
		RTK_FC_HELPER_FC_KFREE(p_egrExtraInfo, sizeof(*p_egrExtraInfo));
		return len;
	}

	rtskb = &rtNewSkb;
	RTK_FC_HOOK_CONVERTER_SKB(new_skb, &rtskb);

	for(i=offset;i<len;i+=2)
	{	
		RTSKB_DATA(rtskb)[(i-offset)>>1]=(_rtk_rg_hex_to_byte(tmpBuf[i])<<4)+_rtk_rg_hex_to_byte(tmpBuf[i+1]);
	}	
	RTK_FC_HOOK_PS_SKB_SKB_PUT(RTSKB_SKB(rtskb), (len-offset)>>1,&RTSKB_DATA(rtskb));

	// search net_device
	RTSKB_DEV(rtskb)= RTK_FC_HELPER_FIRST_NET_DEVICE_GET(&init_net);

	if(mode == 2) {
		pon_sid = simple_strtol(&dev_name[0], NULL, 0);
		DEBUG("send to pon port with sid %d", pon_sid);
	} else {
		while (RTSKB_DEV(rtskb)){
			if(strcmp(RTSKB_DEV(rtskb)->name,dev_name)==0){
				DEBUG("[SEND FROM CPU]Find net device: %s\n",dev_name);
				is_found = TRUE;
				break;	
			}
			RTSKB_DEV(rtskb) = RTK_FC_HELPER_NEXT_NET_DEVICE_GET(new_skb->dev);
		}
		if(is_found==FALSE){
			TRACE("Net device is not found!!");
			RTK_FC_HOOK_PS_SKB_DEV_KFREE_SKB_ANY(new_skb);
			if(pktHdr) RTK_FC_HELPER_FC_KFREE(pktHdr, sizeof(rtk_fc_pktHdr_t));
			if(p_egrExtraInfo )RTK_FC_HELPER_FC_KFREE(p_egrExtraInfo, sizeof(*p_egrExtraInfo));
			return len;
		}
	}
	
	if(mode==0){
		/*
			From CPU port to FC driver.
			E.g. wlan device 
		*/
		

		rtk_fc_wlan_devidx_t wlanDevIdx=RTK_FC_WLANX_NOT_FOUND;


		RTK_FC_HELPER_WLAN_DEV_TO_DEVID(RTSKB_DEV(rtskb),&wlanDevIdx);
		if(wlanDevIdx != RTK_FC_WLANX_NOT_FOUND)
		{
			int ret;
			unsigned char *pData;

			TRACE("[SEND FROM CPU]ready to wlan ingress.");
			RTK_FC_HOOK_PS_SKB_SKB_PULL(new_skb, 14, &pData);
			ret=RTK_FC_HELPER_MGR_WIFI_RX(new_skb);		//_rtk_fc_fastfwd_netif_rx, no need to handle return value an skb.
			/*if(ret==RE8670_RX_CONTINUE)
			{
					//to PS: free by PS
					RTK_FC_HOOK_PS_SKB_ETH_TYPE_TRANS(RTSKB_SKB(rtskb), RTSKB_DEV(rtskb));
					RTK_FC_HOOK_PS_SKB_NETIF_RX(RTSKB_SKB(rtskb));
			}
			else if(ret==RE8670_RX_STOP)
			{
					//drop: free at here
					RTK_FC_HOOK_PS_SKB_DEV_KFREE_SKB_ANY(new_skb);
					//rg_db.systemGlobal.statistic.perPortCnt_skb_free[rg_db.pktHdr->ingressPort]--;
			}
			else
			{
				//forward: don't do anything
			}*/
		}
		else if(wlanDevIdx == RTK_FC_WLANX_NOT_FOUND)
		{
			//to PS: free by PS			
			RTK_FC_HOOK_PS_SKB_ETH_TYPE_TRANS(RTSKB_SKB(rtskb), RTSKB_DEV(rtskb));
			RTK_FC_HOOK_PS_SKB_NETIF_RX(RTSKB_SKB(rtskb));

		}

	}else if(mode == 1){
		/*
			Simulate local out packet
			E.g. PPTP LCP packet
		*/
		TRACE("Local out Packet ready to egress learning");
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	p_egrExtraInfo->wlanDevIdx = RTK_FC_WLANX_NOT_FOUND;
	p_egrExtraInfo->portMask = ((struct re_dev_private*)(RTK_FC_HOOK_PS_DEV_GET_PRIV(RTSKB_DEV(rtskb))))->txPortMask;

	rtk_fc_egress_flowLearning(rtskb,RTSKB_DEV(rtskb),p_egrExtraInfo);
		
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
	{
		int ret;
		//ca_eth_private_t *cep;
		//rg_ca_skb_in(new_skb, TRUE);
		cep = netdev_priv(RTSKB_DEV(rtskb));
		p_egrExtraInfo->wlanDevIdx = RTK_FC_WLANX_NOT_FOUND;
		p_egrExtraInfo->portMask = (1<<cep->port_cfg.tx_ldpid);

		ret = rtk_fc_egress_flowLearning(rtskb, RTSKB_DEV(rtskb), p_egrExtraInfo);
		if(unlikely(ret == RTK_FC_RET_NIC_TX_BUSY_TO_PS)){
			if(unlikely(fc_db.fwdStatistic))
				atomic_inc(&fc_db.statistic.perPortCnt_Drop[RTK_FC_MAC_PORT_CPU][RTK_FC_RET_DROP_NIC_TX_BUSY & ~(RTK_FC_RET_DROP)]);
			RTK_FC_HOOK_PS_SKB_DEV_KFREE_SKB_ANY(RTSKB_SKB(rtskb));
		}
	}
#endif

		
	}else if(mode == 2){
		// mode 2 [sid] [raw data]
		RTK_FC_HELPER_MGR_NIC_TX_WITH_PON_SID(new_skb, pon_sid);
	}
	else
	{
		TRACE("ERROR OUT!");
		RTK_FC_HOOK_PS_SKB_DEV_KFREE_SKB_ANY(new_skb);
	}
	
	if(pktHdr) RTK_FC_HELPER_FC_KFREE(pktHdr, sizeof(rtk_fc_pktHdr_t));
	if(p_egrExtraInfo) RTK_FC_HELPER_FC_KFREE(p_egrExtraInfo, sizeof(*p_egrExtraInfo));

	return len;
	
}

//.unlockBefortWrite=1
int rtk_fc_proc_rstconntrack(struct file *file, const char *buffer, unsigned long count, void *data)
{
	RTK_FC_HOOK_PS_CT_FLUSH();

	return count;
}

void _rtk_fc_remarkBitsStatus_display(void)
{
	rtk_fc_skbmarkTarget_t i = 0;

	rtlglue_printf("\n");
	for(i=0; i<RTK_FC_SKBMARK_END; i++)
	{
		if(fc_db.skbmark[i].force)
		{
			rtlglue_printf(" - %-25s: -ForceAssign value:%d-\n", name_of_rg_remarkingBits[i],fc_db.skbmark[i].value);
		}
		else
		{
			if(fc_db.skbmark[i].startBit == RTK_FC_RMK_UNDEF)
				rtlglue_printf(" - %-25s: -disabled-\n", name_of_rg_remarkingBits[i]);
			else
				rtlglue_printf(" - %-25s: bits[%d:%d], using skb mark %s\n", name_of_rg_remarkingBits[i],  fc_db.skbmark[i].startBit, fc_db.skbmark[i].startBit+fc_db.skbmark[i].len-1, fc_db.skbmark[i].mark1or2?"2":"1");
		}
	}
}
// for skb->mark 1 check conflicts
int _rtk_fc_remarkBitsConflictCheck(rtk_fc_skbmarkTarget_t target, int startBit, int len)
{
	rtk_fc_skbmarkTarget_t i = 0;
	int ret = SUCCESS, usedBits = 0, wishBits = 0;

	if(startBit==-1) return SUCCESS;

	if(startBit+len==32){
		wishBits = (0xffffffff) & ~((1<<startBit)-1);
	}else
		wishBits = ((1<<(startBit+len))-1) & ~((1<<startBit)-1);

	for(i = 0; i < RTK_FC_SKBMARK_END; i++)
	{
		if(target == i) continue;

		if(fc_db.skbmark[i].startBit!=RTK_FC_RMK_UNDEF)
		{
			if (fc_db.skbmark[i].len+fc_db.skbmark[i].startBit==32)
				usedBits = (0xffffffff) & ~((1<<fc_db.skbmark[i].startBit)-1);
			else
				usedBits = ((1<<(fc_db.skbmark[i].len+fc_db.skbmark[i].startBit))-1) & ~((1<<fc_db.skbmark[i].startBit)-1);

			if((usedBits & wishBits) !=0 && fc_db.skbmark[i].mark1or2==0)
			{
				ret = FAILED;
				break;
			}
		}
	}

	return ret;
}


void _rtk_fc_proc_skbmark_usage(void){
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("     Mark1: echo [bit postition] > /proc/fc/ctrl/skbmark_[target] \n");
	rtlglue_printf("     Mark1: echo mark1 [bit postition] > /proc/fc/ctrl/skbmark_[target] \n");
	rtlglue_printf("     Mark2: echo mark2 [bit postition] > /proc/fc/ctrl/skbmark_[target] \n\n");
	rtlglue_printf("[target]:\"streamId/streamId_enable\", \"meterId_enable/meterId\", \"mibId_enable/mibId\", \"qid\", \"fwdByPs\", \"skip_fc_alg_check\", \"psFloodFwdAcc\", \"mapet_fmr\", \"forceIntpriToWifiPri\" ");
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	rtlglue_printf(", \"cvlan_vid\", \"cvlan_pri\", \"cvlan_action\", \"svlan_vid\", \"svlan_pri\", \"svlan_action\", \"svlan_tpid\"");
#endif
	rtlglue_printf("\n");
	rtlglue_printf("[bit postition]: mark1: 0 - 31, mark2: 0 - 63\n");
}

int _rtk_fc_skbmark2_BitsConflictCheck(rtk_fc_skbmarkTarget_t target, int startBit, int len)
{

	rtk_fc_skbmarkTarget_t i = 0;
	int ret = SUCCESS;
	__u64  usedBits = 0, wishBits = 0;
	if(startBit==-1) return SUCCESS;
	if(startBit+len==64){
		wishBits = (0xffffffffffffffff) & ~((1LL<<startBit)-1);
	}else
		wishBits = ((1LL<<(startBit+len))-1) & ~((1LL<<startBit)-1);

	for(i = 0; i < RTK_FC_SKBMARK_END; i++)
	{
		if(target == i) continue;

		if(fc_db.skbmark[i].startBit!=RTK_FC_RMK_UNDEF)
		{
			if (fc_db.skbmark[i].len+fc_db.skbmark[i].startBit==64)
				usedBits = (0xffffffffffffffff) & ~((1LL<<fc_db.skbmark[i].startBit)-1);
			else
				usedBits = ((1LL<<(fc_db.skbmark[i].len+fc_db.skbmark[i].startBit))-1) & ~((1LL<<fc_db.skbmark[i].startBit)-1);

			if((usedBits & wishBits) !=0 && fc_db.skbmark[i].mark1or2==1)
			{
				ret = FAILED;
				break;
			}
		}
	}

	return ret;
}

int _rtk_fc_skbmark_bit_config(int startbit, rtk_fc_skbmarkTarget_t target, int mark1or2)
{
	int ret = SUCCESS;

	if(startbit<0){
		startbit=-1;
		rtlglue_printf("Reset value to -1\n");
	}
	if(mark1or2==1){
		if(startbit>63){
			startbit=63;
			rtlglue_printf("out of range, try to assign with bit %d\n", startbit);
		}

		if((startbit!=RTK_FC_RMK_UNDEF) && ((startbit + fc_db.skbmark[target].len)>64))
		{
			rtlglue_printf("out of range, assignment was forbidden\n");
			return FAILED;
		}
	}else{
		if(startbit>32){
			startbit=32;
			rtlglue_printf("out of range, try to assign with bit %d\n", startbit);
		}

		if((startbit!=RTK_FC_RMK_UNDEF) && ((startbit + fc_db.skbmark[target].len)>32))
		{
			rtlglue_printf("out of range, assignment was forbidden\n");
			return FAILED;
		}
	}
	//bit confilct 64bit
	ret = (mark1or2==1)?_rtk_fc_skbmark2_BitsConflictCheck(target, startbit, fc_db.skbmark[target].len):_rtk_fc_remarkBitsConflictCheck(target, startbit, fc_db.skbmark[target].len);

	if(ret == SUCCESS)
	{
		fc_db.skbmark[target].startBit = startbit;
		fc_db.skbmark[target].mark1or2 =  (mark1or2==1)?1:0;
	}
	else
		rtlglue_printf("\nAssignment was conflict with other remarking function...keep original setting\n");

	return ret;
}

int _rtk_fc_check_mark2_enable(void){
	int ret = SUCCESS;
	struct sk_buff *new_skb;
	struct rt_skbuff *rtskb, rtNewSkb;

	RTK_FC_HOOK_PS_SKB_DEV_ALLOC_SKB(SKB_BUF_SIZE, &new_skb);
	if(new_skb==NULL)
		return FAILED;

	rtskb = &rtNewSkb;
	RTK_FC_HOOK_CONVERTER_SKB(new_skb, &rtskb);

	ret = RTSKB_MARK2_EXIST(rtskb)==TRUE?SUCCESS:FAILED;

	RTK_FC_HOOK_PS_SKB_DEV_KFREE_SKB_ANY(new_skb);

	return ret;
}
int _rtk_fc_skbmark_setting_decision(struct file *file, const char *buff, unsigned long count, void *data, rtk_fc_skbmarkTarget_t target)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	int mark2_enabled = FAILED, mark1or2 = 0; //mark1or2: 0-> mark1, 1 -> mark2
	if (buff)
	{
		char *strptr,*split_str, *split_str2;
		int bit_position;
		int forceValue;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;

		while(1)
		{
			split_str=strsep(&strptr," ");
			if(strcasecmp(split_str,"mark2")==0){
				/*
				 *    skb mark 2 (new one)
			     */
				mark2_enabled = _rtk_fc_check_mark2_enable();        //check if mark2 is enable.
				if(mark2_enabled==SUCCESS){
					split_str=strsep(&strptr," ");
					if(split_str){
						bit_position = simple_strtol(split_str, NULL, 0);
						mark1or2 = 1;
						_rtk_fc_skbmark_bit_config(bit_position, target, mark1or2);
					}else
						_rtk_fc_proc_skbmark_usage();
				}else
					rtlglue_printf("[Error]Mark 2 is not enabled!\n");
				break;

			}else if(strcasecmp(split_str,"mark1")==0){
				/*
				 *    skb mark 1 (new one)
			     */

				split_str=strsep(&strptr," ");
				if(split_str){
					bit_position = simple_strtol(split_str, NULL, 0);
					_rtk_fc_skbmark_bit_config(bit_position, target, mark1or2);
				}else
					_rtk_fc_proc_skbmark_usage();
				break;
			}else if(strcasecmp(split_str,"forceEn")==0){

				split_str=strsep(&strptr," ");
				if(split_str){
					forceValue = simple_strtol(split_str, NULL, 0);
					fc_db.skbmark[target].force = 1;
					fc_db.skbmark[target].value = forceValue;
				}else
					_rtk_fc_proc_skbmark_usage();
				break;
			}else if(strcasecmp(split_str,"forceDis")==0){
				fc_db.skbmark[target].force = 0;
				break;
			}else{

				split_str2=strsep(&strptr," ");
				if(split_str2){
					rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
					_rtk_fc_proc_skbmark_usage();
					break;
				}else{

					/*
					 *   Use skb mark 1 (old one).
				     */
					bit_position = simple_strtol(split_str, NULL, 0);
					
				    _rtk_fc_skbmark_bit_config(bit_position, target, 0);
					
					break;
				}
			}
		}
		return count;
	}else{
		_rtk_fc_proc_skbmark_usage();
	}
	return -EFAULT;
}

#if defined(CONFIG_CUSTOM_EXTERNAL_SWITCH)
int rtk_fc_proc_wanVidGroup_get(struct seq_file *s, void *v)
{
    int i, len=0;
    PROC_PRINTF("Dump WAN Vid Group:");
	for(i = 0; i < RTK_FC_MAX_WAN_NUM; i++)
    	PROC_PRINTF(" %d  ;",fc_db.wanVidGroup[i]);

	PROC_PRINTF("\n");

	return len;
}

int rtk_fc_proc_wanVidGroup_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	unsigned char tmpBuf[256] = {0};
	int i = 0;
	char *strptr,*split_str;
	unsigned int vid = 0;
	
	if (buff)
	{
		/* copy data to the buffer */
		strncpy(tmpBuf, buff, len);

		tmpBuf[len] = '\0';
		strptr=tmpBuf;

		split_str=strsep(&strptr," ");

		if (split_str==NULL)
		{
			goto PARSE_ERROR;
		}

		if(strncmp(split_str,"clear", 5)==0)
		{
			for(i = 0; i < RTK_FC_MAX_WAN_NUM; i++)
				fc_db.wanVidGroup[i] = 0;
		}
		else if(strcasecmp(split_str,"add")==0)
		{
			split_str=strsep(&strptr," ");	
			if(split_str)
				vid = simple_strtol(split_str, NULL, 0);
			else
				goto PARSE_ERROR;
			for(i = 0; i < RTK_FC_MAX_WAN_NUM; i++)
			{
				if(fc_db.wanVidGroup[i] == 0)
				{
					fc_db.wanVidGroup[i] = vid;
					break;
				}
			}
		}
		else if(strcasecmp(split_str,"del")==0)
		{
			split_str=strsep(&strptr," ");
			if(split_str)
				vid = simple_strtol(split_str, NULL, 0);
			else
				goto PARSE_ERROR;
			for(i = 0; i < RTK_FC_MAX_WAN_NUM; i++)
			{
				if(fc_db.wanVidGroup[i] == vid)
				{
					fc_db.wanVidGroup[i] = 0;
					break;
				}
			}
		}
		else 
			goto PARSE_ERROR;

		return len;
	}

PARSE_ERROR:
	printk("  Parse input parameter error\n");
	return -EFAULT;

}
#endif

static int rtk_fc_nullDebugGet(struct seq_file *s, void *v)
{
       return 0;
}

static int rtk_fc_nullDebugSingleOpen(struct inode *inode, struct file *file)
{
       return(single_open(file, rtk_fc_nullDebugGet, NULL));
}

static int rtk_fc_commonDebugSingleRead(struct seq_file *s, void *v)
{
	int i, ret = -1;

	for( i=0; i< (sizeof(fcProc)/sizeof(rtk_fc_proc_t)) ;i++)
	{
		//printk("common_single_read inode_id=%u i_ino=%u\n",fcProc[i].inode_id,(unsigned int)inode->i_ino);
		if(fcProc[i].inode_id==RTK_FC_HELPER_PROC_FILE_GET_INODE_ID(s))
		{
			//========================= Critical Section Start =========================//
			if (!fcProc[i].unlockBeforeRead) {
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
			}
			
			ret = fcProc[i].get(s, v);

			if (!fcProc[i].unlockBeforeRead) {
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();
			}
			//========================= Critical Section End ===========================//
	
			break;
		}
	}

	return ret;
}

static int rtk_fc_commonDebugSingleOpen(struct inode *inode, struct file *file)
{
	int i, ret = -1;

	//RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
	//RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
	//========================= Critical Section Start =========================//
	for( i=0; i< (sizeof(fcProc)/sizeof(rtk_fc_proc_t)) ;i++)
	{
		//printk("common_single_open inode_id=%u i_ino=%u\n",fcProc[i].inode_id,(unsigned int)inode->i_ino);
		if(fcProc[i].inode_id==(unsigned int)inode->i_ino)
		{
			if(!fcProc[i].overPageSizeDisplay)
				ret = RTK_FC_HELPER_PS_PROC_SINGLE_OPEN(file, rtk_fc_commonDebugSingleRead, NULL);
			else
				ret = RTK_FC_HELPER_PS_PROC_SINGLE_OPEN_SIZE(file, rtk_fc_commonDebugSingleRead, NULL, PAGE_SIZE<<3);
			break;
		}
	}
	//========================= Critical Section End =========================//
	//RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
	//RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();

	return ret;
}

// coverity[ +taint_sanitize : arg-*0 ]
bool rtk_fc_coverity_sanitize_string(const char *str) 
{
	//just avoid coverity issue	
	//checking string len, we want to sanitize procBuffer for coverity
	int i;
	int ret=true;

	for(i=0;i<CMD_BUFF_SIZE;i++)
	{
		if((str[i]<0x20 ||  str[i]==0x7f))
			ret=false;
	}
	if(!(strlen(str) < (CMD_BUFF_SIZE-1)))
		ret=false;

	return ret;
}


static ssize_t rtk_fc_commonDebugSingleWrite(struct file * file, const char __user * userbuf,
				size_t count, loff_t * off)
{
    int i, ret = -1;
	char procBuffer[CMD_BUFF_SIZE]={0};
	size_t len = (count >= (CMD_BUFF_SIZE))?(CMD_BUFF_SIZE):count;

	if(len==0)
		return -EFAULT;

	/* write data to the buffer */
	if ( RTK_FC_HOOK_PS_COPY_FROM_USER(procBuffer, userbuf, len) ) {
		return -EFAULT;
	}
	procBuffer[len-1] = '\0';	//echo string from user is  abc	=> 'a''b''c''\n' len=4 and no char '\0' , replace '\n' to '\0'
	
	rtk_fc_coverity_sanitize_string(procBuffer);

    for( i=0; i< (sizeof(fcProc)/sizeof(rtk_fc_proc_t)) ;i++)
    {

		//printk("common_single_write inode_id=%u i_ino=%u\n",fcProc[i].inode_id,(unsigned int)file->f_dentry->d_inode->i_ino);
        if(fcProc[i].inode_id==(unsigned int)file->f_inode->i_ino)
        {
			//========================= Critical Section Start =========================//
			if(!fcProc[i].unlockBefortWrite) {
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
			}
			ret = fcProc[i].set(file,&procBuffer[0],len,off);
			
			if(!fcProc[i].unlockBefortWrite) {
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();
			}
			//========================= Critical Section End =========================//
			
			break;
        }
    }


	return ret;      
}

int rtk_fc_proc_flow_natloopback(struct seq_file *s, void *v)
{
	int32 i, j;
	
	PROC_PRINTF(">>SW flow info natloopback Table:  %d buckect * %d way\n\n", 
		RTK_FC_TABLESIZE_NATLOOPBACK_BUCKET, RTK_FC_TABLESIZE_NATLOOPBACK_WAY);

	for(i=0; i<RTK_FC_TABLESIZE_NATLOOPBACK_BUCKET; i++)
	{
		for(j=0; j<RTK_FC_TABLESIZE_NATLOOPBACK_WAY; j++)
		{
			if(fc_db.flow_natloopbackTbl[i][j].vaild) 
			{		
				PROC_PRINTF(" - Bucket[%02d]Way[%d]: flowEntIdx %d key dip %pI4h ip %pI4h port %d\n", i, j, 
					fc_db.flow_natloopbackTbl[i][j].flow_ent_dx, 
					&fc_db.flow_natloopbackTbl[i][j].key_l3_dip, 
					&fc_db.flow_natloopbackTbl[i][j].l3_ip, 
					fc_db.flow_natloopbackTbl[i][j].l4_port);
			}
		}
	}
	
	return SUCCESS;
}



int rtk_fc_proc_netif_mib_reset(struct file *file, const char *buffer, unsigned long count, void *data)
{
	int32 rstIdx =_rtk_fc_proc_parsing_string_to_integer(buffer, count);

	if(rstIdx<-1 || rstIdx>=RTK_FC_TABLESIZE_INTF){
		rtlglue_printf("Invalid index %d, should be 0~%d.\n", rstIdx, RTK_FC_TABLESIZE_INTF);
		return count;
	}

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	if(rstIdx<0){
		int i;
		//reset all
		for(i=0;i<RTK_FC_TABLESIZE_INTF;i++){
			// hw
			ASSERT_EQ(rtk_rg_asic_netifMib_reset(i), SUCCESS);
			// sw
			if(fc_db.netifHwTblVaild[i].hwNetifValid)
				bzero(&fc_db.netifTbl[NETIF_HWTOSW(i)].intfMib, NR_CPUS*sizeof(rtk_rg_asic_netifMib_entry_t));
		}
	}else{
		// hw
		ASSERT_EQ(rtk_rg_asic_netifMib_reset(rstIdx), SUCCESS);
		// sw
		if(fc_db.netifHwTblVaild[rstIdx].hwNetifValid)
			bzero(&fc_db.netifTbl[NETIF_HWTOSW(rstIdx)].intfMib, NR_CPUS*sizeof(rtk_rg_asic_netifMib_entry_t));
	}

#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
	if(rstIdx<0){
		int i;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)		
		extern l3pe_cntr_tx_t last_if_tx_cntr[RTK_FC_TABLESIZE_INTF];
		extern l3pe_cntr_rx_t last_if_rx_cntr[RTK_FC_TABLESIZE_INTF];
#endif		
		//reset all
		for(i=0;i<RTK_FC_TABLESIZE_INTF;i++)
		{
			// hw
			{
				l3pe_cntr_tx_t tx_cntr;
				l3pe_cntr_rx_t rx_cntr;

				memset(&tx_cntr, 0, sizeof(tx_cntr));
				memset(&rx_cntr, 0, sizeof(rx_cntr));

				ASSERT_EQ(aal_l3pe_cntr_tx_set(G3_DEF_DEVID, i, &tx_cntr), AAL_E_OK);
				ASSERT_EQ(aal_l3pe_cntr_rx_set(G3_DEF_DEVID, i, &rx_cntr), AAL_E_OK);
#if defined(CONFIG_FC_CA8277B_SERIES)
				// clear MC netif counter
				ASSERT_EQ(aal_l3pe_cntr_tx_set(G3_DEF_DEVID, i + RTK_FC_MC_HW_NETIF_IDXSHIFT, &tx_cntr), AAL_E_OK);
				ASSERT_EQ(aal_l3pe_cntr_rx_set(G3_DEF_DEVID, i + RTK_FC_MC_HW_NETIF_IDXSHIFT, &rx_cntr), AAL_E_OK);
#elif defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
				

				// clear MC netif counter (care rx counter only)
				{
					aal_l3_te_pm_policer_t l3_pm_data;
					ASSERT_EQ(aal_l3_te_pm_policer_flow_get(G3_DEF_DEVID, i + G3_FLOW_POLICER_IDXSHIFT_MC_NETIF_RXMIB, &l3_pm_data), CA_E_OK); //read clear
				}
				if(fc_db.netifHwTblVaild[i].hwNetifValid)
					bzero(&fc_db.netifTbl[NETIF_HWTOSW(i)].hwMcNetifMib, sizeof(fc_db.netifTbl[NETIF_HWTOSW(i)].hwMcNetifMib));

				{
					int j;
					int stackingCount;
					int32 p_fcDevIdx[DEV_STACK_MAX]={0};

					stackingCount=rtk_fc_psIfidxStackKey_to_fcDevIdx(fc_db.netifTbl[NETIF_HWTOSW(i)].psIfidxStackKey,p_fcDevIdx);
					for(j = 0 ; j < stackingCount ; j ++)
					{
						if(fc_db.devGwMacTbl[p_fcDevIdx[j]].dev)
						{
							bzero(&fc_db.devGwMacTbl[p_fcDevIdx[j]].netifmib_wrapRnd_cnt, sizeof(rtk_fc_netif_mib_wrapRnd_t));
						}
						
					}
				}
				
				bzero(&fc_db.netifTbl[NETIF_HWTOSW(i)].hwMcNetifMib, sizeof(fc_db.netifTbl[NETIF_HWTOSW(i)].hwMcNetifMib));
				
				bzero(&last_if_tx_cntr[i], sizeof(l3pe_cntr_tx_t));
				bzero(&last_if_rx_cntr[i], sizeof(l3pe_cntr_rx_t));
#endif

			}
			// sw
			if(fc_db.netifHwTblVaild[i].hwNetifValid)
				bzero(&fc_db.netifTbl[NETIF_HWTOSW(i)].intfMib, NR_CPUS*sizeof(rtk_rg_asic_netifMib_entry_t));
		}
	}else{
		// hw
		{
			l3pe_cntr_tx_t tx_cntr;
			l3pe_cntr_rx_t rx_cntr;

			memset(&tx_cntr, 0, sizeof(tx_cntr));
			memset(&rx_cntr, 0, sizeof(rx_cntr));

			ASSERT_EQ(aal_l3pe_cntr_tx_set(G3_DEF_DEVID, rstIdx, &tx_cntr), AAL_E_OK);
			ASSERT_EQ(aal_l3pe_cntr_rx_set(G3_DEF_DEVID, rstIdx, &rx_cntr), AAL_E_OK);
#if defined(CONFIG_FC_CA8277B_SERIES)
			// clear MC netif counter
			ASSERT_EQ(aal_l3pe_cntr_tx_set(G3_DEF_DEVID, rstIdx + RTK_FC_MC_HW_NETIF_IDXSHIFT, &tx_cntr), AAL_E_OK);
			ASSERT_EQ(aal_l3pe_cntr_rx_set(G3_DEF_DEVID, rstIdx + RTK_FC_MC_HW_NETIF_IDXSHIFT, &rx_cntr), AAL_E_OK);
#elif defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			// clear MC netif counter (care rx counter only)
			{
				aal_l3_te_pm_policer_t l3_pm_data;
				ASSERT_EQ(aal_l3_te_pm_policer_flow_get(G3_DEF_DEVID, rstIdx + G3_FLOW_POLICER_IDXSHIFT_MC_NETIF_RXMIB, &l3_pm_data), CA_E_OK); //read clear
			}
			if(fc_db.netifHwTblVaild[rstIdx].hwNetifValid)
				bzero(&fc_db.netifTbl[NETIF_HWTOSW(rstIdx)].hwMcNetifMib, sizeof(fc_db.netifTbl[NETIF_HWTOSW(rstIdx)].hwMcNetifMib));

			{
				int j;
				int stackingCount;
				int32 p_fcDevIdx[DEV_STACK_MAX]={0};

				stackingCount=rtk_fc_psIfidxStackKey_to_fcDevIdx(fc_db.netifTbl[NETIF_HWTOSW(rstIdx)].psIfidxStackKey,p_fcDevIdx);
				for(j = 0 ; j < stackingCount ; j ++)
				{
					if(fc_db.devGwMacTbl[p_fcDevIdx[j]].dev)
					{
						bzero(&fc_db.devGwMacTbl[p_fcDevIdx[j]].netifmib_wrapRnd_cnt, sizeof(rtk_fc_netif_mib_wrapRnd_t));
					}
					
				}
			}
			{
				extern l3pe_cntr_tx_t last_if_tx_cntr[RTK_FC_TABLESIZE_INTF];
				extern l3pe_cntr_rx_t last_if_rx_cntr[RTK_FC_TABLESIZE_INTF];

				bzero(&last_if_tx_cntr[rstIdx], sizeof(l3pe_cntr_tx_t));
				bzero(&last_if_rx_cntr[rstIdx], sizeof(l3pe_cntr_rx_t));
			
			}
#endif
		}
		// sw
		if(fc_db.netifHwTblVaild[rstIdx].hwNetifValid)
			bzero(&fc_db.netifTbl[NETIF_HWTOSW(rstIdx)].intfMib, NR_CPUS*sizeof(rtk_rg_asic_netifMib_entry_t));
	}

#endif

	return count;
}

int rtk_fc_proc_hw_flow_mib_reset(struct file *file, const char *buffer, unsigned long count, void *data)
{
	uint32 rstIdx =_rtk_fc_proc_parsing_string_to_integer(buffer, count);

	if(rstIdx>=RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)){
		rtlglue_printf("Invalid index %d, should be 0~%d.\n", rstIdx, RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode));
		return count;
	}
	_rtk_fc_hwFlowMib_clear(rstIdx);

	return count;
}

int rtk_fc_proc_sw_flow_mib_reset(struct file *file, const char *buffer, unsigned long count, void *data)
{
	uint32 rstIdx =_rtk_fc_proc_parsing_string_to_integer(buffer, count);

	if(rstIdx>=RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)){
		rtlglue_printf("Invalid index %d, should be 0~%d.\n", rstIdx, RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode));
		return count;
	}

	memset(&fc_db.flowSWMib[rstIdx][0], 0, sizeof(fc_db.flowSWMib[rstIdx]));

	return count;
}

int rtk_fc_proc_hw_flow_mib2_reset(struct file *file, const char *buffer, unsigned long count, void *data)
{
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	uint32 rstIdx =_rtk_fc_proc_parsing_string_to_integer(buffer, count);

	if(rstIdx>=RT_FLOW_MIB2_SIZE_GET(fc_db.controlFuc.flow_mib_mode)){
		rtlglue_printf("Invalid index %d, should be 0~%d.\n", rstIdx, RT_FLOW_MIB2_SIZE_GET(fc_db.controlFuc.flow_mib_mode));
		return count;
	}
	_rtk_fc_hwFlowMib2_clear(rstIdx);
#else
	WARNING("Not support");
#endif
	return count;
}

int rtk_fc_proc_sw_flow_mib2_reset(struct file *file, const char *buffer, unsigned long count, void *data)
{
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	uint32 rstIdx =_rtk_fc_proc_parsing_string_to_integer(buffer, count);

	if(rstIdx>=RT_FLOW_MIB2_SIZE_GET(fc_db.controlFuc.flow_mib_mode)){
		rtlglue_printf("Invalid index %d, should be 0~%d.\n", rstIdx, RT_FLOW_MIB2_SIZE_GET(fc_db.controlFuc.flow_mib_mode));
		return count;
	}

	memset(&fc_db.flowSWMib2[rstIdx][0], 0, sizeof(fc_db.flowSWMib2[rstIdx]));
#else
	WARNING("Not support");
#endif
	return count;
}

void _rtk_fc_proc_fb_meter_usage(void){
	rtlglue_printf("usage:\n");
	rtlglue_printf("echo meterIdx ifgInclude rate > /proc/fc/ctrl/meter_assign \n");
	rtlglue_printf("meterIdx: 0~31\n");
	rtlglue_printf("ifgInclude: 0:DISABLE 1:ENABLED\n");
	rtlglue_printf("rate: %d~%d (unit: kbps)\n", METER_RATE_MIN, METER_RATE_MAX);
}


int rtk_fc_proc_fb_meter_get(struct seq_file *s, void *v)
{

	int i,ret,rt_ret;
	rtk_enable_t ifgInclude;
	uint32 rate;

	for(i=0;i<RTK_FC_TABLESIZE_FBMTR;i++)
	{
		int hwMeterIdx = 0;

		if(fc_db.controlFuc.rt_api_is_enabled){
			int virMeterIdx = i;
			rt_rate_meter_mapping_t meterMapping;
			//ret = rt_rate_shareMeterMappingHw_get(virMeterIdx, &meterMapping);
			rt_ret = RTK_FC_HELPER_RT_RATE_SHAREMETER_MAPPING_HW_GET(virMeterIdx, &meterMapping);
			if((rt_ret == 0) && (meterMapping.type == RT_METER_TYPE_FLOW))
				hwMeterIdx = meterMapping.hw_index;
			else
			{
				if(rt_ret == RT_ERR_FILTER_METER_ID)
					PROC_PRINTF("FB_Meter[%d]: Not be added by RT API.\n", i);
				else
					WARNING("get hw flow meter index failed, return value: %d, virtual meter type: %d", rt_ret, meterMapping.type);
				continue;
			}
		}
		else
		{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			hwMeterIdx = i;
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
			hwMeterIdx = i;
#endif
		}

		ret = _rtk_fc_l3Meter_get(RT_RATE_EXT_METER_TYPE_FLOW, hwMeterIdx, &rate,&ifgInclude);

		if(ret==RTK_FC_RET_OK)
		{
			PROC_PRINTF("FB_Meter[%d]: ifgInclude:%s  rate:%d(kbps) \n",i, (ifgInclude == ENABLED)?"ENABLED":"DISABLED",rate);
		}
		else
		{
			PROC_PRINTF("FB_Meter[%d]: ACCESS FAIL! (ret = %d) \n", i, ret);
		}

	}

	return SUCCESS;
}
int rtk_fc_proc_fb_meter_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	int ret;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	uint32 virMeterIdx, hwMeterIdx = 0;
	rtk_enable_t ifgInclude;
	uint32 rate;

	if (buffer)
	{
		//rg_kernel.debug_level=simple_strtoul(tmpBuf, NULL, 16);
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		strptr=tmpBuf;


		split_str=strsep(&strptr," ");
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_fb_meter_usage();
			return len;
		}
		virMeterIdx = simple_strtol(split_str, NULL, 0);
		if(virMeterIdx>=RTK_FC_TABLESIZE_FBMTR){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_fb_meter_usage();
			return -EFAULT;
		}

		split_str=strsep(&strptr," ");	//get ifgInclude
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_fb_meter_usage();
			return len;
		}
		ifgInclude = simple_strtol(split_str, NULL, 0);
		if(ifgInclude<0 ||  ifgInclude>2){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_fb_meter_usage();
			return -EFAULT;
		}

		split_str=strsep(&strptr," ");	//get rate
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_fb_meter_usage();
			return len;
		}
		rate = simple_strtol(split_str, NULL, 0);

		if(rate>METER_RATE_MAX)
		{
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_fb_meter_usage();
			return -EFAULT;
		}
		if(fc_db.controlFuc.rt_api_is_enabled)
		{
			rt_rate_meter_mapping_t meterMapping;
			//ret = rt_rate_shareMeterMappingHw_get(virMeterIdx, &meterMapping);
			ret = RTK_FC_HELPER_RT_RATE_SHAREMETER_MAPPING_HW_GET(virMeterIdx, &meterMapping);
			if((ret == 0) && (meterMapping.type == RT_METER_TYPE_FLOW))
			{
				hwMeterIdx = meterMapping.hw_index;
			}
			else
			{
				WARNING("fb meter idx decision Failed, return value: %d, virtual meter type: %d", ret, meterMapping.type);
				return count;

			}
		}else
		{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
		hwMeterIdx = virMeterIdx;
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
		hwMeterIdx = hwMeterIdx;
#endif
		}

		ret= _rtk_fc_l3Meter_set(RT_RATE_EXT_METER_TYPE_FLOW, hwMeterIdx,rate,ifgInclude);
#if defined(CONFIG_FC_CAG3_SERIES)
		/*reserved 32 policer for flow meter
		RT meter(virtual index)--->		RT meter(hw index)--->	real index (be shifted by FC)
		0								0							384
		1								1							385
		...
		31								31							415
		This solution is for flow_meter_mib_conf_dependence is enableing.
		policer counter may be impact by L2 policer index (or defaut value 0)*/
		ret= _rtk_fc_l3Meter_set(RT_RATE_EXT_METER_TYPE_FLOW, hwMeterIdx + G3_FLOW_POLICER_IDXSHIFT_FLOWMTR,rate,ifgInclude);
#endif

		if(ret==RT_ERR_RG_OK){
			rtk_fc_proc_fb_meter_get(NULL,NULL);
		}else{
			rtlglue_printf("[Error]set fb meter fail!!! \n");
		}

	}


	return count;
}

int rtk_fc_proc_igmp_unknown_flood_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Usage:\n");
	PROC_PRINTF("Enable unkown multicast data flooding:\n");
	PROC_PRINTF("echo 1 > proc/fc/ctrl/igmp_unkonwn_flood\n");
	PROC_PRINTF("Disable unkown multicast data flooding:\n");
	PROC_PRINTF("echo 0 > proc/fc/ctrl/igmp_unkonwn_flood\n");
	PROC_PRINTF("status:%s",fc_db.igmp_unknown_flood==0?"disable\n":"enable\n");
	return SUCCESS;
}
int rtk_fc_proc_igmp_unknown_flood_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	fc_db.igmp_unknown_flood = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	rtk_fc_proc_igmp_unknown_flood_get(NULL, NULL);
	return len;
}


int rtk_fc_proc_igmp_unknown_unTrack_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Usage:\n");
	PROC_PRINTF("Enable unkown multicast unTrack:\n");
	PROC_PRINTF("echo 1 > proc/fc/ctrl/igmp_unkonwn_unTrack\n");
	PROC_PRINTF("Disable unkown multicast data unTrack:\n");
	PROC_PRINTF("echo 0 > proc/fc/ctrl/igmp_unkonwn_unTrack\n");
	PROC_PRINTF("status:%s",fc_db.igmp_unknown_unTrack==0?"disable\n":"enable\n");
	return SUCCESS;
}
int rtk_fc_proc_igmp_unknown_unTrack_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	fc_db.igmp_unknown_unTrack = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	rtk_fc_proc_igmp_unknown_unTrack_get(NULL, NULL);
	return len;
}


int rtk_fc_proc_wan_access_limit_probeInterval_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Probe active host per %d sec\n",fc_db.wanAccessLimitProbeInterval_unit1s);
	return SUCCESS;
}
int rtk_fc_proc_wan_access_limit_probeInterval_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int time = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	if(time < 1)
	{
		rtlglue_printf("\ninvalid input, keep origrinal setting: %d s\n",fc_db.wanAccessLimitProbeInterval_unit1s);
		return len;
	}

	fc_db.wanAccessLimitProbeInterval_unit1s = time;

	rtk_fc_proc_wan_access_limit_probeInterval_get(NULL, NULL);

	return len;
}



int rtk_fc_proc_flowSyncPeriod_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Synchronize flow with linux conntrack per %d sec\n",fc_db.flowSyncPeriod_unit1s);

	PROC_PRINTF(" - house keeping timer scan %d groups within %d sec, %d flow buckets and %d l2 entries per tick\n", 
					fc_db.flowSync_groupCnt, fc_db.flowSyncPeriod_unit1s, fc_db.flowSync_GRANULARITY, L2HOUSE_KEEP_NUM);
	return SUCCESS;
}
int rtk_fc_proc_flowSyncPeriod_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int time = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	if(time < 1)
	{
		rtlglue_printf("\ninvalid input, keep origrinal setting: %d s\n",fc_db.flowSyncPeriod_unit1s);
		return len;
	}

	_rtk_fc_flow_chenkingTime_set(time);

	rtk_fc_proc_flowSyncPeriod_get(NULL, NULL);

	return len;
}
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
int rtk_fc_proc_amsdu_pkt_get(struct seq_file *s, void *v)
{
	unsigned int amsdu_pkt = 0;
	ca_uint16_t msg_size = 0;
	ca_ipc_pkt_t ipc_pkt;
	ca_status_t ret;

	msg_size = sizeof(unsigned int);
		
	ipc_pkt.session_id = CA_IPC_SESSION_WFO;
	ipc_pkt.dst_cpu_id = fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_WIFI_TX_AMSDU] + CA_IPC_CPU_PE0;
	ipc_pkt.priority = 0;
	ipc_pkt.msg_no = 6; //MAX_PKT_NUM_GETTING
	ipc_pkt.msg_data = &amsdu_pkt;			/* the message data to transmit. */
	ipc_pkt.msg_size = msg_size;
	ret = ca_ipc_msg_sync_send(&ipc_pkt, &amsdu_pkt, &msg_size);

	if (ret != CA_E_OK) {
		rtlglue_printf("fail to get amsdu_pkt num from pe, ret %d\n", ret);
	} else {
		rtlglue_printf("amsdu_pkt %u\n", amsdu_pkt);
	}
	
	return SUCCESS;
}
int rtk_fc_proc_amsdu_pkt_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int amsdu_pkt = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	ca_uint16_t msg_size = 0;
	ca_ipc_pkt_t ipc_pkt;
	ca_status_t ret;

	msg_size = sizeof(unsigned int);
		
	ipc_pkt.session_id = CA_IPC_SESSION_WFO;
	ipc_pkt.dst_cpu_id = fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_WIFI_TX_AMSDU] + CA_IPC_CPU_PE0;
	ipc_pkt.priority = 0;
	ipc_pkt.msg_no = 0; //MAX_PKT_NUM_SETTING
	ipc_pkt.msg_data = &amsdu_pkt;			/* the message data to transmit. */
	ipc_pkt.msg_size = msg_size;
	ret = ca_ipc_msg_sync_send(&ipc_pkt, &amsdu_pkt, &msg_size);

	if (ret != CA_E_OK) {
		rtlglue_printf("fail to get amsdu_pkt num from pe, ret %d\n", ret);
	} else {
		rtlglue_printf("amsdu_pkt %d\n",amsdu_pkt);
	}
	
	return len;
}

#define MAX_STA_NUM 41 //include mode
#define TC_MODE 0
#define WMM_MODE 1
#define DEBUG_MODE_ENABLE 2
int rtk_fc_proc_amsdu_pkt_size_get(struct seq_file *s, void *v)
{
	unsigned int amsdu_pkt_size[MAX_STA_NUM] = {0};
	unsigned int i = 0;
	ca_uint16_t msg_size = 0;
	ca_ipc_pkt_t ipc_pkt;
	ca_status_t ret;

	msg_size = sizeof(amsdu_pkt_size);
		
	ipc_pkt.session_id = CA_IPC_SESSION_WFO;
	ipc_pkt.dst_cpu_id = fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_WIFI_TX_AMSDU] + CA_IPC_CPU_PE0;
	ipc_pkt.priority = 0;
	ipc_pkt.msg_no = 7; //MAX_TX_PKT_SIZE_GETTING
	ipc_pkt.msg_data = &amsdu_pkt_size;			/* the message data to transmit. */
	ipc_pkt.msg_size = msg_size;
	ret = ca_ipc_msg_sync_send(&ipc_pkt, &amsdu_pkt_size, &msg_size);

	if (ret != CA_E_OK) {
		rtlglue_printf("fail to get amsdu_pkt_size from pe, ret %d\n", ret);
	} else {
		if (amsdu_pkt_size[40] == TC_MODE) {
			rtlglue_printf("TC_MODE\n");
			for (i = 0; i < (MAX_STA_NUM-1); i++) {
				rtlglue_printf("mac id %u amsdu_pkt_size %u\n", i + 1, amsdu_pkt_size[i]);
			}
		} else if (amsdu_pkt_size[40] == WMM_MODE) {
			rtlglue_printf("WMM_MODE\n");
			for (i = 0; i < (MAX_STA_NUM-1); i++) {
				rtlglue_printf("mac id %u cos %u amsdu_pkt_size %u\n", (i/4)+1, i%4, amsdu_pkt_size[i]);
			}
		} else if (amsdu_pkt_size[40] == DEBUG_MODE_ENABLE) {
			rtlglue_printf("DEBUG_MODE_ENABLE\n");
			for (i = 0; i < (MAX_STA_NUM-1); i++) {
				rtlglue_printf("mac id %u amsdu_pkt_size %u\n", i + 1, amsdu_pkt_size[i]);
			}
		}
	}
	
	return SUCCESS;
}
int rtk_fc_proc_amsdu_pkt_size_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int amsdu_pkt_size = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	ca_uint16_t msg_size = 0;
	ca_ipc_pkt_t ipc_pkt;
	ca_status_t ret;
		
	msg_size = sizeof(unsigned int);	
	ipc_pkt.session_id = CA_IPC_SESSION_WFO;
	ipc_pkt.dst_cpu_id = fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_WIFI_TX_AMSDU] + CA_IPC_CPU_PE0;
	ipc_pkt.priority = 0;
	ipc_pkt.msg_no = 1; //MAX_TX_PKT_SIZE_SETTING
	ipc_pkt.msg_data = &amsdu_pkt_size;			/* the message data to transmit. */
	ipc_pkt.msg_size = msg_size;
	ret = ca_ipc_msg_sync_send(&ipc_pkt, &amsdu_pkt_size, &msg_size);

	if (ret != CA_E_OK) {
		rtlglue_printf("fail to get amsdu_pkt_size from pe, ret %d\n", ret);
	} else {
		rtlglue_printf("amsdu_pkt_size %d\n",amsdu_pkt_size);
	}
	
	return len;
}

extern int rtk_fc_internal_amsdu_ofld_timeout_op(uint32 op, rt_pe_amsdu_ofld_tmorq_t *pReq, uint32 rq_num);
void rtk_fc_amsdu_ofld_timeout_usage(void)
{
	rtlglue_printf("1. set all macid timeout:\n");
	rtlglue_printf("   echo timeout VAL > /proc/fc/ctrl/wifi_amsdu_time_out_set\n");
	rtlglue_printf("2. set macid timeout:\n");
	rtlglue_printf("   echo macid VAL timeout VAL > /proc/fc/ctrl/wifi_amsdu_time_out_set\n");
	return;
}

int rtk_fc_proc_amsdu_time_out_get(struct seq_file *s, void *v)
{
	#define STANUM 40
	uint32 i = 0;
	rt_pe_amsdu_ofld_tmorq_t tmorq[STANUM] = {0};
	ca_status_t ret;

	ret = rtk_fc_internal_amsdu_ofld_timeout_op(TMORQ_OP_GET, tmorq, STANUM);

	if (ret != CA_E_OK) {
		rtlglue_printf("fail to get amsdu_time_out from pe, ret %d\n", ret);
	} 

	for (i = 0; i < STANUM; i++) {
		rtlglue_printf("Macid[%u]: %u\n", tmorq[i].macid, tmorq[i].timeout);
	}
	
	return SUCCESS;
}

int rtk_fc_proc_amsdu_time_out_set(struct file *filp, const char *buff, unsigned long len, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int length = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str, *cmd_str;
	uint32 val;
	rt_pe_amsdu_ofld_tmorq_t tmorq = {0};
	ca_status_t ret;

	//set value
	if (buff)
	{
		//fc_db.debug_level=simple_strtoul(tmpBuf, NULL, 16);
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, length);

		strptr=tmpBuf;

		cmd_str=strsep(&strptr," ");
		if(cmd_str==NULL){
			rtk_fc_amsdu_ofld_timeout_usage();
			return -EFAULT;
		}

		if(!strcasecmp(cmd_str, "timeout")) {
			split_str=strsep(&strptr," ");
			if(split_str==NULL){
				rtk_fc_amsdu_ofld_timeout_usage();
				return -EFAULT;
			}
			val = simple_strtol(split_str, NULL, 0);
			tmorq.op = TMORQ_OP_ALL;
			tmorq.macid = 0;
			tmorq.timeout = val;
			rtlglue_printf("set all macid timeout %u\n", tmorq.timeout);
		} 
		else if(!strcasecmp(cmd_str, "macid"))
		{
			//get mac id
			split_str=strsep(&strptr," ");
			if(split_str==NULL){
				rtk_fc_amsdu_ofld_timeout_usage();
				return -EFAULT;
			}
			val = simple_strtol(split_str, NULL, 0);
			tmorq.op = TMORQ_OP_MACID;
			tmorq.macid = val;

			//get timeout
			cmd_str=strsep(&strptr," ");
			if(cmd_str==NULL){
				rtk_fc_amsdu_ofld_timeout_usage();
				return -EFAULT;
			}

			if(!strcasecmp(cmd_str, "timeout")) {
				//get mac id
				split_str=strsep(&strptr," ");
				if(split_str==NULL){
					rtk_fc_amsdu_ofld_timeout_usage();
					return -EFAULT;
				}
				val = simple_strtol(split_str, NULL, 0);
				tmorq.timeout = val;
				rtlglue_printf("set macid %u timeout %u\n", tmorq.macid, tmorq.timeout);
			} else {
				rtk_fc_amsdu_ofld_timeout_usage();
				return -EFAULT;
			}
		}
		else
		{
			rtk_fc_amsdu_ofld_timeout_usage();
			return -EFAULT;
		}
	}

	//send ipc
	ret = rtk_fc_internal_amsdu_ofld_timeout_op(TMORQ_OP_SET, &tmorq, 1);
	if (ret != CA_E_OK) {
		rtlglue_printf("fail to set amsdu_time_out from pe, ret %d\n", ret);
	} 

	return len;
}

int rtk_fc_proc_pe_offload_wifi_amsdu_en_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Wifi AMSDU PE Offload Status : %s \n", fc_db.pe_offload_wifi_amsdu_en ? "enabled" : "disabled"); 
	return SUCCESS;
}

int rtk_fc_proc_pe_offload_wifi_amsdu_en_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int ret_val = 0;
	int pe_offload_wifi_amsdu_en = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	ret_val = rtk_fc_internal_wifi_amsdu_pe_offload_en(RTK_PE_AMSDU_MODE_DISABLE_FLOW, pe_offload_wifi_amsdu_en);

	if (ret_val == RTK_FC_RET_ERR) {
		rtlglue_printf("input is not 0/1\n"); 
	}

	//show the pe offload status
	rtlglue_printf("Wifi AMSDU PE Offload Status : %s \n", fc_db.pe_offload_wifi_amsdu_en ? "enabled" : "disabled"); 
    return len;
}

int rtk_fc_proc_pe_offload_wifi_amsdu_func_switching_get(struct seq_file *s, void *v)
{
	rtk_fc_proc_pe_offload_wifi_amsdu_en_get(NULL, NULL);
	return SUCCESS;
}

//extern void ca_ni_wait_tx_amsdu_pe_ofld_done(void);
int rtk_fc_proc_pe_offload_wifi_amsdu_func_switching_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
#if 0
	int i = 0;
	int msg_data = 0;
	ca_ipc_pkt_t ipc_pkt;
	rt_flow_ops_data_t flow_ops_data;
	
	//disable flow learning
	fc_db.pe_offload_wifi_amsdu_en = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	rtlglue_printf(" - Enable PE offload : %s \n",fc_db.pe_offload_wifi_amsdu_en?"enabled":"disabled"); 
	
	if (!fc_db.pe_offload_wifi_amsdu_en) {
		//delete flows
		memset(&flow_ops_data, 0 , sizeof(flow_ops_data));

		for(i = fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_start ; i <= fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_end ; i++)
			rtk_fc_internal_wifi_amsdu_pe_offload_mac_id_del(i);

		//wait tx amsdu rx done
		ca_ni_wait_tx_amsdu_pe_ofld_done();

		//notify pe to push all address to eq pool3
		ipc_pkt.session_id = CA_IPC_SESSION_WFO;
		ipc_pkt.dst_cpu_id = fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_WIFI_TX_AMSDU] + CA_IPC_CPU_PE0;
		ipc_pkt.priority = 0;
		ipc_pkt.msg_no = 4; //TX_AMSDU_RESET
		ipc_pkt.msg_data = &msg_data;			/* the message data to transmit. */
		ipc_pkt.msg_size = sizeof(unsigned int);
		ca_ipc_msg_async_send(&ipc_pkt);

		rtlglue_printf("Clear TX AMSDU Flow done\n"); 
	}
#else
	int ret_val = 0;
	int pe_offload_wifi_amsdu_en = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	ret_val = rtk_fc_internal_wifi_amsdu_pe_offload_en(RTK_PE_AMSDU_MODE_FUNC_SWITCHING, pe_offload_wifi_amsdu_en);

	if (ret_val == RTK_FC_RET_ERR) {
		rtlglue_printf("input is not 0/1\n"); 
	}

	//show the pe offload status
	rtlglue_printf("Wifi AMSDU PE Offload Status : %s \n", fc_db.pe_offload_wifi_amsdu_en ? "enabled" : "disabled"); 
#endif
    return len;
}

#define MAX_WLAN_IDX_NUM 40
int rtk_fc_proc_wifi_amsdu_wlan_idx_table_get(struct seq_file *s, void *v)
{
	unsigned int amsdu_wlan_idx_table[MAX_WLAN_IDX_NUM] = {0};
	unsigned int i = 0;
	ca_uint16_t msg_size = 0;
	ca_ipc_pkt_t ipc_pkt;
	ca_status_t ret;

	msg_size = sizeof(amsdu_wlan_idx_table);
		
	ipc_pkt.session_id = CA_IPC_SESSION_WFO;
	ipc_pkt.dst_cpu_id = fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_WIFI_TX_AMSDU] + CA_IPC_CPU_PE0;
	ipc_pkt.priority = 0;
	ipc_pkt.msg_no = 10; //MAX_TX_PKT_SIZE_GETTING
	ipc_pkt.msg_data = &amsdu_wlan_idx_table;			/* the message data to transmit. */
	ipc_pkt.msg_size = msg_size;
	ret = ca_ipc_msg_sync_send(&ipc_pkt, &amsdu_wlan_idx_table, &msg_size);

	if (ret != CA_E_OK) {
		rtlglue_printf("fail to get amsdu_wlan_idx_table from pe, ret %d\n", ret);
	} else {
		for (i = 0; i < MAX_WLAN_IDX_NUM; i++) {
			rtlglue_printf("mac id %u wlan_idx %u\n", i + 1, amsdu_wlan_idx_table[i]);
		}
	}
	return SUCCESS;
}
#endif
void _rtk_fc_proc_flow_operation_usage(void){
	rtlglue_printf("usage:\n");
	rtlglue_printf("echo operarion [operation_para_0] [operation_para_1] [operation_para_2]... > /proc/fc/ctrl/flow_operation \n");
	rtlglue_printf("operarion:\n");
	rtlglue_printf("  0: flow delete by flow key\n");
	rtlglue_printf("     case 1: by index");
	rtlglue_printf("     [operation_para_0]: INDEX\n");
	rtlglue_printf("     [operation_para_1]: flow_idx\n");
	rtlglue_printf("     e.g. echo 0 INDEX 100 > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     case 2: by pattern");
	rtlglue_printf("     [operation_para_0]: pattern_name (support pattern: SIP4/DIP4/SIP6/DIP6/SPORT/DPORT/MAC/SMAC/DMAC/L4PROTO/MSIP4/MDIP4/MSPORT/MDPORT/WLAN_DEV_IDX/IGR_WLAN_DEV_IDX/EGR_WLAN_DEV_IDX/SPA/PON_STREAM_ID/OUTER_VLAN/MOUTER_VLAN/IGR_IFNAME/EGR_IFNAME)\n");
	rtlglue_printf("     [operation_para_1]: pattern\n");
	rtlglue_printf("     ... \n");
	rtlglue_printf("     Note 1. supported L4PROTO \"TCP\" and \"UDP\"\n");
	rtlglue_printf("     Note 2. For OUTER_VLAN/MOUTER_VLAN , vlan range <0 - 4095>, 4096 for un-tagged\n");
	rtlglue_printf("     e.g. echo 0 SIP4 192.168.1.100 SPORT 200 > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     e.g.2. echo 0 SIP6 2001::44 SPORT 200 > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     e.g.3. echo 0 SIP4 192.168.1.100 SPORT 200 L4PROTO TCP > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     e.g.4. echo 0 SMAC f4:6d:2f:de:3e:8b > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     e.g.5. echo 0 SIP4 192.168.1.100 SPORT 200 SMAC f4:6d:2f:de:3e:8b > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     e.g.6. echo 0 SMAC f4:6d:2f:de:3e:8b WLAN_DEV_IDX 1 > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("  1: flush flow table, including SW and HW flows\n");
#if defined(CONFIG_RTK_FC_PER_HW_FLOW_MIB)
	rtlglue_printf("  2: get flow mib by flow index\n");
#else
	rtlglue_printf("  2: get flow mib by flow index (support in HWNAT_MODE_SW_ONLY mode only)\n");
#endif
	rtlglue_printf("     [operation_para_0]: flow_index\n");
	rtlglue_printf("  3: get flow group mib by flowmib index\n");
	rtlglue_printf("     [operation_para_0]: flow_group_mib_index [0~%d]\n", RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
	rtlglue_printf("  4: clear all mib, including flow mib and flow group mib\n");
#if defined(CONFIG_RTK_FC_PER_HW_FLOW_MIB)
	rtlglue_printf("  5: clear flow mib by flow index\n");
#else
	rtlglue_printf("  5: clear flow mib by flow index (support in HWNAT_MODE_SW_ONLY mode only)\n");
#endif
	rtlglue_printf("     [operation_para_0]: flow_index\n");
	rtlglue_printf("  6: clear flow group mib by flowmib index\n");
	rtlglue_printf("     [operation_para_0]: flow_group_mib_index [0~%d]\n", RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
	rtlglue_printf("  7: sync ct state to flow table immediately\n");
	rtlglue_printf("  8: high priority flow pattern action\n");
	rtlglue_printf("     case 1: add hiPriflowPtn[index]\n");
	rtlglue_printf("     [operation_para_0]: ACT\n");
	rtlglue_printf("     [operation_para_1]: ADD\n");
	rtlglue_printf("     [operation_para_2]: INDEX\n");
	rtlglue_printf("     [operation_para_3]: hiPriflowPtn index [0~%d]\n", RT_FLOW_HIGHPRIFLOW_PATTERN_SIZE-1);
	rtlglue_printf("     [operation_para_4]: pattern_name (support pattern: SIP4/DIP4/SIP6/DIP6/SPORT/DPORT/L4PROTO)\n");
	rtlglue_printf("     [operation_para_5]: pattern\n");
	rtlglue_printf("     ... \n");
	rtlglue_printf("     Note. supported L4PROTO \"TCP\" and \"UDP\"\n");
	rtlglue_printf("     e.g. echo 8 ACT ADD INDEX 0 SIP4 192.168.1.100 SPORT 200 > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     e.g.2. echo 8 ACT ADD INDEX 1 SIP6 2001::44 SPORT 200 > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     e.g.3. echo 8 ACT ADD INDEX 2 SIP4 192.168.1.100 SPORT 200 L4PROTO TCP > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     case 2: delete hiPriflowPtn[index]\n");
	rtlglue_printf("     [operation_para_0]: ACT\n");
	rtlglue_printf("     [operation_para_1]: DEL\n");
	rtlglue_printf("     [operation_para_2]: INDEX\n");
	rtlglue_printf("     [operation_para_3]: hiPriflowPtn index [0~%d]\n", RT_FLOW_HIGHPRIFLOW_PATTERN_SIZE-1);
	rtlglue_printf("     e.g. echo 8 ACT DEL INDEX 0 > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     case 3: get hiPriflowPtn[index]\n");
	rtlglue_printf("     [operation_para_0]: ACT\n");
	rtlglue_printf("     [operation_para_1]: GET\n");
	rtlglue_printf("     [operation_para_2]: INDEX\n");
	rtlglue_printf("     [operation_para_3]: hiPriflowPtn index [0~%d]\n", RT_FLOW_HIGHPRIFLOW_PATTERN_SIZE-1);
	rtlglue_printf("     e.g. echo 8 ACT GET INDEX 0 > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("     case 4: flush hiPriflowPtn table\n");
	rtlglue_printf("     [operation_para_0]: ACT\n");
	rtlglue_printf("     [operation_para_1]: FLUSH\n");
	rtlglue_printf("     e.g. echo 8 ACT FLUSH > /proc/fc/ctrl/flow_operation\n");
	rtlglue_printf("  9: get flow group mib2 by flowmib2 index\n");
	rtlglue_printf("     [operation_para_0]: flow_group_mib2_index [0~%d]\n", RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
	rtlglue_printf("  10: clear flow group mib2 by flowmib2 index\n");
}

//.unlockBefortWrite=1
int rtk_fc_proc_flow_operation_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	//char flow_operation_string[len];
	//int operation;
	//int flowIdx;
	//rt_stat_flow_mib_t flowCnt;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	rt_flow_ops_t flow_op;
	rt_flow_ops_data_t flow_ops_data;
	int ret = FAIL;

	memset(&flow_ops_data, 0 , sizeof(flow_ops_data));
	
	if (buff)
	{
		//fc_db.debug_level=simple_strtoul(tmpBuf, NULL, 16);
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, CMD_BUFF_SIZE);

		strptr=tmpBuf;

		split_str=strsep(&strptr," ");//get flow_operation
		if(split_str==NULL){
			goto INVALID_PARA;
		}
		flow_op = simple_strtol(split_str, NULL, 0);

		if(flow_op == RT_FLOW_OPS_DELETE_FLOW)
		{
			// delete flow by flow key
			bool valid_key = FALSE;
			split_str=strsep(&strptr," ");	//get flow delete type
			if(split_str==NULL){
				goto INVALID_PARA;
			}
			
			if(strcasecmp(split_str,"INDEX")==0)
			{
				split_str=strsep(&strptr," ");	//get flow index
				if(split_str!=NULL)
				{
					valid_key = TRUE;
					flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_INDEX;
					flow_ops_data.data_delFlow.flowKey.flowIdx = simple_strtol(split_str, NULL, 0);
				}				
			}
			else
			{
				while(1)
				{
					if(strcasecmp(split_str,"SIP4")==0)
					{
						char *ip_token,*split_ip_token,j;
						uint32 tempIP;
						
						split_str=strsep(&strptr," ");	//get SIP
						if(split_str==NULL)
							break;

						ip_token=split_str;
						tempIP=0;
						for(j=0;j<4;j++)
						{
							split_ip_token=strsep(&ip_token,".");
							tempIP|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
							if(ip_token==NULL) break;
						}
						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.sip4_valid = TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.sip4 = tempIP;

					}
					else if(strcasecmp(split_str,"DIP4")==0)
					{
						char *ip_token,*split_ip_token,j;
						uint32 tempIP;

						split_str=strsep(&strptr," ");	//get DIP
						if(split_str==NULL)
							break;

						ip_token=split_str;
						tempIP=0;
						for(j=0;j<4;j++)
						{
							split_ip_token=strsep(&ip_token,".");
							tempIP|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
							if(ip_token==NULL) break;
						}
						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.dip4_valid = TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.dip4 = tempIP;
					}
					else if(strcasecmp(split_str,"SIP6")==0)
					{
						split_str=strsep(&strptr," ");	//get SIP
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.sip6_valid = TRUE;
						in6_pton(split_str, -1, flow_ops_data.data_delFlow.flowKey.flowPattern.sip6, -1, NULL);
					}
					else if(strcasecmp(split_str,"DIP6")==0)
					{
						split_str=strsep(&strptr," ");	//get DIP
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.dip6_valid = TRUE;
						in6_pton(split_str, -1, flow_ops_data.data_delFlow.flowKey.flowPattern.dip6, -1, NULL);
					}
					else if(strcasecmp(split_str,"SPORT")==0)
					{
						split_str=strsep(&strptr," ");	//get SPORT
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.sport_valid= TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.sport = simple_strtol(split_str, NULL, 0);
					}
					else if(strcasecmp(split_str,"DPORT")==0)
					{
						split_str=strsep(&strptr," ");	//get DPORT
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.dport_valid= TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.dport = simple_strtol(split_str, NULL, 0);
					}
					else if(strcasecmp(split_str,"MAC")==0)
					{
						char *mac_token,split_mac_token[6]={0};

						split_str=strsep(&strptr," ");	//get mac
						if(split_str==NULL)
							break;

						mac_token=split_str;
						sscanf(mac_token,"%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",&split_mac_token[0],&split_mac_token[1],&split_mac_token[2],&split_mac_token[3],&split_mac_token[4],&split_mac_token[5]);
						memcpy(flow_ops_data.data_delFlow.flowKey.flowPattern.mac, split_mac_token, 6);

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.mac_valid = TRUE;
					}
					else if(strcasecmp(split_str,"SMAC")==0)
					{
						char *mac_token,split_mac_token[6]={0};

						split_str=strsep(&strptr," ");	//get mac
						if(split_str==NULL)
							break;

						mac_token=split_str;
						sscanf(mac_token,"%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",&split_mac_token[0],&split_mac_token[1],&split_mac_token[2],&split_mac_token[3],&split_mac_token[4],&split_mac_token[5]);
						memcpy(flow_ops_data.data_delFlow.flowKey.flowPattern.smac, split_mac_token, 6);

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.smac_valid = TRUE;
					}
					else if(strcasecmp(split_str,"DMAC")==0)
					{
						char *mac_token,split_mac_token[6]={0};

						split_str=strsep(&strptr," ");	//get mac
						if(split_str==NULL)
							break;

						mac_token=split_str;
						sscanf(mac_token,"%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",&split_mac_token[0],&split_mac_token[1],&split_mac_token[2],&split_mac_token[3],&split_mac_token[4],&split_mac_token[5]);
						memcpy(flow_ops_data.data_delFlow.flowKey.flowPattern.dmac, split_mac_token, 6);

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.dmac_valid = TRUE;
					}
					else if(strcasecmp(split_str,"L4PROTO")==0)
					{
						split_str=strsep(&strptr," ");	//get L4 prorocol
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.l4proto_valid= TRUE;
						if(strcasecmp(split_str,"UDP")==0)
						{
							flow_ops_data.data_delFlow.flowKey.flowPattern.l4proto = RT_FLOW_OP_FLOW_PATTERN_L4PROTO_UDP;
						}
						else if(strcasecmp(split_str,"TCP")==0)
						{
							flow_ops_data.data_delFlow.flowKey.flowPattern.l4proto = RT_FLOW_OP_FLOW_PATTERN_L4PROTO_TCP;
						}
						else if(strcasecmp(split_str,"MSIP4")==0)
						{
							char *ip_token,*split_ip_token,j;
							uint32 tempIP;
							
							split_str=strsep(&strptr," ");	//get MSIP
							if(split_str==NULL)
								break;

							ip_token=split_str;
							tempIP=0;
							for(j=0;j<4;j++)
							{
								split_ip_token=strsep(&ip_token,".");
								tempIP|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
								if(ip_token==NULL) break;
							}
							valid_key = TRUE;
							flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
							flow_ops_data.data_delFlow.flowKey.flowPattern.modified_sip4_valid = TRUE;
							flow_ops_data.data_delFlow.flowKey.flowPattern.modified_sip4 = tempIP;

						}
						else if(strcasecmp(split_str,"MDIP4")==0)
						{
							char *ip_token,*split_ip_token,j;
							uint32 tempIP;

							split_str=strsep(&strptr," ");	//get MDIP
							if(split_str==NULL)
								break;

							ip_token=split_str;
							tempIP=0;
							for(j=0;j<4;j++)
							{
								split_ip_token=strsep(&ip_token,".");
								tempIP|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
								if(ip_token==NULL) break;
							}
							valid_key = TRUE;
							flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
							flow_ops_data.data_delFlow.flowKey.flowPattern.modified_dip4_valid = TRUE;
							flow_ops_data.data_delFlow.flowKey.flowPattern.modified_dip4 = tempIP;
						}
						else if(strcasecmp(split_str,"MSPORT")==0)
						{
							split_str=strsep(&strptr," ");	//get MSPORT
							if(split_str==NULL)
								break;

							valid_key = TRUE;
							flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
							flow_ops_data.data_delFlow.flowKey.flowPattern.modified_sport_valid= TRUE;
							flow_ops_data.data_delFlow.flowKey.flowPattern.modified_sport = simple_strtol(split_str, NULL, 0);
						}
						else if(strcasecmp(split_str,"MDPORT")==0)
						{
							split_str=strsep(&strptr," ");	//get MDPORT
							if(split_str==NULL)
								break;

							valid_key = TRUE;
							flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
							flow_ops_data.data_delFlow.flowKey.flowPattern.modified_dport_valid= TRUE;
							flow_ops_data.data_delFlow.flowKey.flowPattern.modified_dport = simple_strtol(split_str, NULL, 0);
						}
						else
						{
							valid_key = FALSE;
							break;
						}
					}
					else if(strcasecmp(split_str,"WLAN_DEV_IDX")==0)
					{
						split_str=strsep(&strptr," ");	//get idx
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.wlan_dev_idx_valid= TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.wlan_dev_idx = simple_strtol(split_str, NULL, 0);
					}
					else if(strcasecmp(split_str,"IGR_WLAN_DEV_IDX")==0)
					{
						split_str=strsep(&strptr," ");	//get idx
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.igr_wlan_dev_idx_valid= TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.igr_wlan_dev_idx = simple_strtol(split_str, NULL, 0);
					}
					else if(strcasecmp(split_str,"EGR_WLAN_DEV_IDX")==0)
					{
						split_str=strsep(&strptr," ");	//get idx
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.egr_wlan_dev_idx_valid= TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.egr_wlan_dev_idx = simple_strtol(split_str, NULL, 0);
					}
					else if(strcasecmp(split_str,"SPA")==0)
					{
						split_str=strsep(&strptr," ");	//get idx
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.spa_valid= TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.spa = simple_strtol(split_str, NULL, 0);
					}
					else if(strcasecmp(split_str,"PON_STREAM_ID")==0)
					{
						split_str=strsep(&strptr," ");	//get idx
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.pon_streamid_valid= TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.pon_streamid = simple_strtol(split_str, NULL, 0);
					}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
					else if(strcasecmp(split_str,"OUTER_VLAN")==0)
					{
						split_str=strsep(&strptr," ");	//get idx
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.outer_vlan_valid= TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.outer_vlan = simple_strtol(split_str, NULL, 0);
					}
					else if(strcasecmp(split_str,"MOUTER_VLAN")==0)
					{
						split_str=strsep(&strptr," ");	//get idx
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.modified_outer_vlan_valid= TRUE;
						flow_ops_data.data_delFlow.flowKey.flowPattern.modified_outer_vlan = simple_strtol(split_str, NULL, 0);
					}
					else if(strcasecmp(split_str,"IGR_IFNAME")==0)
					{
						split_str=strsep(&strptr," ");	//get idx
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.igr_ifname_valid= TRUE;
						memcpy(&flow_ops_data.data_delFlow.flowKey.flowPattern.igr_ifname[0], split_str,strlen(split_str));
						
						
					}
					else if(strcasecmp(split_str,"EGR_IFNAME")==0)
					{
						split_str=strsep(&strptr," ");	//get idx
						if(split_str==NULL)
							break;

						valid_key = TRUE;
						flow_ops_data.data_delFlow.flowKeyType = RT_FLOW_OP_FLOW_KEY_TYPE_PATTERN;
						flow_ops_data.data_delFlow.flowKey.flowPattern.egr_ifname_valid= TRUE;
						memcpy(&flow_ops_data.data_delFlow.flowKey.flowPattern.egr_ifname[0], split_str,strlen(split_str));
						
					}
		 
#endif					
					if (strptr==NULL) break;

					split_str=strsep(&strptr," ");	//get next token
				}
			}
			
			if(!valid_key)
			{
				goto INVALID_PARA;
			}
			else
			{
				ret = rtk_fc_flow_operation_add(flow_op, &flow_ops_data);
				if(!ret)
				{
					if(flow_ops_data.data_delFlow.flowKeyType == RT_FLOW_OP_FLOW_KEY_TYPE_INDEX)
						rtlglue_printf("Delete flow by flow index %d!\n", flow_ops_data.data_delFlow.flowKey.flowIdx);
					else
					{
						rtlglue_printf("Delete flow by flow pattern:\n");
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.sip4_valid)
							rtlglue_printf("   SIP4: %pI4h\n", &(flow_ops_data.data_delFlow.flowKey.flowPattern.sip4));
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.dip4_valid)
							rtlglue_printf("   DIP4: %pI4h\n", &(flow_ops_data.data_delFlow.flowKey.flowPattern.dip4));
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.sip6_valid)
							rtlglue_printf("   SIP6: %pI6c\n", &(flow_ops_data.data_delFlow.flowKey.flowPattern.sip6[0]));
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.dip6_valid)
							rtlglue_printf("   DIP6: %pI6c\n", &(flow_ops_data.data_delFlow.flowKey.flowPattern.dip6[0]));
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.sport_valid)
							rtlglue_printf("   SPORT: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.sport);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.dport_valid)
							rtlglue_printf("   DPORT: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.dport);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.mac_valid)
							rtlglue_printf("   MAC: %pM\n", &(flow_ops_data.data_delFlow.flowKey.flowPattern.mac));
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.smac_valid)
							rtlglue_printf("   SMAC: %pM\n", &(flow_ops_data.data_delFlow.flowKey.flowPattern.smac));
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.dmac_valid)
							rtlglue_printf("   DMAC: %pM\n", &(flow_ops_data.data_delFlow.flowKey.flowPattern.dmac));
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.l4proto_valid)
							rtlglue_printf("   L4PROTO: %s\n", (flow_ops_data.data_delFlow.flowKey.flowPattern.l4proto == RT_FLOW_OP_FLOW_PATTERN_L4PROTO_UDP)? "UDP":"TCP");
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.modified_sip4_valid)
							rtlglue_printf("   MSIP4: %pI4h\n", &(flow_ops_data.data_delFlow.flowKey.flowPattern.modified_sip4));
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.modified_dip4_valid)
							rtlglue_printf("   MDIP4: %pI4h\n", &(flow_ops_data.data_delFlow.flowKey.flowPattern.modified_dip4));
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.modified_sport_valid)
							rtlglue_printf("   MSPORT: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.modified_sport);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.modified_dport_valid)
							rtlglue_printf("   MDPORT: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.modified_dport);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.wlan_dev_idx_valid)
							rtlglue_printf("   WLAN_DEV_IDX: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.wlan_dev_idx);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.igr_wlan_dev_idx_valid)
							rtlglue_printf("   IGR_WLAN_DEV_IDX: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.igr_wlan_dev_idx);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.egr_wlan_dev_idx_valid)
							rtlglue_printf("   EGR_WLAN_DEV_IDX: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.egr_wlan_dev_idx);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.spa_valid)
							rtlglue_printf("   SPA: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.spa);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.pon_streamid_valid)
							rtlglue_printf("   PON_STREAM_ID: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.pon_streamid);
#if defined(CONFIG_FC_RTL8277C_SERIES)||defined(CONFIG_FC_RTL9607F_SERIES)
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.igr_ifname_valid)
							rtlglue_printf("   IGR_IFNAME: %s\n", &flow_ops_data.data_delFlow.flowKey.flowPattern.igr_ifname[0]);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.egr_ifname_valid)
							rtlglue_printf("   EGR_IFNAME: %s\n", &flow_ops_data.data_delFlow.flowKey.flowPattern.egr_ifname[0]);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.outer_vlan_valid)
							rtlglue_printf("   OUTER_VLAN: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.outer_vlan);
						if(flow_ops_data.data_delFlow.flowKey.flowPattern.modified_outer_vlan_valid)
							rtlglue_printf("   MOUTER_CVLAN: %d\n", flow_ops_data.data_delFlow.flowKey.flowPattern.modified_outer_vlan);
#endif						
					}
				}

			}
			
		}
		else if(flow_op == RT_FLOW_OPS_FLUSH_FLOWTABLE)
		{
			// flush flow table, including SW and HW flows
			ret = rtk_fc_flow_operation_add(flow_op, NULL);
			if(!ret)
				rtlglue_printf("Flush flow table!\n");
		}
		else if(flow_op == RT_FLOW_OPS_GET_SW_FLOW_MIB_BY_IDX)
		{
			// get flow mib by flow index (support in HWNAT_MODE_SW_ONLY mode only)
			split_str=strsep(&strptr," ");	//get sw flow index
			if(split_str==NULL){
				goto INVALID_PARA;
			}
			flow_ops_data.data_getFlowMib.index = simple_strtol(split_str, NULL, 0);
			ret = rtk_fc_flow_operation_add(flow_op, &flow_ops_data);
			if(!ret)
			{
				rtlglue_printf("SW flow[%d]:\n", flow_ops_data.data_getFlowMib.index);
				rtlglue_printf("  ingress packet count: %u\n", flow_ops_data.data_getFlowMib.swFlowCnt.ingress_packet_count);
				rtlglue_printf("  ingress byte count: %llu\n", flow_ops_data.data_getFlowMib.swFlowCnt.ingress_byte_count);
			}
		}
		else if(flow_op == RT_FLOW_OPS_GET_FLOWGRP_MIB_BY_IDX)
		{
			// get flow group mib by flowmib index
			split_str=strsep(&strptr," ");	//get flowmib index
			if(split_str==NULL){
				goto INVALID_PARA;
			}
			flow_ops_data.data_getFlowGrpMib.index = simple_strtol(split_str, NULL, 0);
			ret = rtk_fc_flow_operation_add(flow_op, &flow_ops_data);
			if(!ret)
			{
				rtlglue_printf("flow group mib[%d]:\n", flow_ops_data.data_getFlowGrpMib.index);
				rtlglue_printf("  ingress packet count: %u\n", flow_ops_data.data_getFlowGrpMib.flowGrpCnt.ingress_packet_count);
				rtlglue_printf("  ingress byte count: %llu\n", flow_ops_data.data_getFlowGrpMib.flowGrpCnt.ingress_byte_count);
				rtlglue_printf("  egress packet count: %u\n", flow_ops_data.data_getFlowGrpMib.flowGrpCnt.egress_packet_count);
				rtlglue_printf("  egress byte count: %llu\n", flow_ops_data.data_getFlowGrpMib.flowGrpCnt.egress_byte_count);
				
			}
		}
		else if(flow_op == RT_FLOW_OPS_GET_FLOWGRP_MIB2_BY_IDX)
		{
			// get flow group mib2 by flowmib2 index
			split_str=strsep(&strptr," ");	//get flowmib2 index
			if(split_str==NULL){
				goto INVALID_PARA;
			}
			flow_ops_data.data_getFlowGrpMib2.index = simple_strtol(split_str, NULL, 0);
			ret = rtk_fc_flow_operation_add(flow_op, &flow_ops_data);
			if(!ret)
			{
				rtlglue_printf("flow group mib2[%d]:\n", flow_ops_data.data_getFlowGrpMib2.index);
				rtlglue_printf("  ingress packet count: %u\n", flow_ops_data.data_getFlowGrpMib2.flowGrpCnt.ingress_packet_count);
				rtlglue_printf("  ingress byte count: %llu\n", flow_ops_data.data_getFlowGrpMib2.flowGrpCnt.ingress_byte_count);
				rtlglue_printf("  egress packet count: %u\n", flow_ops_data.data_getFlowGrpMib2.flowGrpCnt.egress_packet_count);
				rtlglue_printf("  egress byte count: %llu\n", flow_ops_data.data_getFlowGrpMib2.flowGrpCnt.egress_byte_count);
			}
		}
		else if(flow_op == RT_FLOW_OPS_CLEAR_ALL_MIB)
		{
			// clear all mib, including flow mib and flow group mib
			ret = rtk_fc_flow_operation_add(flow_op, NULL);
			if(!ret)
				rtlglue_printf("clear all mib!\n");
		}
		else if(flow_op == RT_FLOW_OPS_CLEAR_SW_FLOW_MIB_BY_IDX)
		{
			// clear flow mib by flow index (support in HWNAT_MODE_SW_ONLY mode only)
			split_str=strsep(&strptr," ");	//get sw flow index
			if(split_str==NULL){
				goto INVALID_PARA;
			}
			flow_ops_data.data_clearFlowMib = simple_strtol(split_str, NULL, 0);
			ret = rtk_fc_flow_operation_add(flow_op, &flow_ops_data);
			if(!ret)
				rtlglue_printf("Clear SW flow mib, flow index %d!\n", flow_ops_data.data_clearFlowMib);
		}
		else if(flow_op == RT_FLOW_OPS_CLEAR_FLOWGRP_MIB_BY_IDX)
		{
			// clear flow group mib by flowmib index
			split_str=strsep(&strptr," ");	//get flowmib index
			if(split_str==NULL){
				goto INVALID_PARA;
			}
			flow_ops_data.data_clearFlowGrpMib = simple_strtol(split_str, NULL, 0);
			ret = rtk_fc_flow_operation_add(flow_op, &flow_ops_data);
			if(!ret)
				rtlglue_printf("Clear flow group mib, flow group index %d!\n", flow_ops_data.data_clearFlowGrpMib);
		}
		else if(flow_op == RT_FLOW_OPS_CLEAR_FLOWGRP_MIB2_BY_IDX)
		{
			// clear flow group mib2 by flowmib2 index
			split_str=strsep(&strptr," ");	//get flowmib2 index
			if(split_str==NULL){
				goto INVALID_PARA;
			}
			flow_ops_data.data_clearFlowGrpMib2 = simple_strtol(split_str, NULL, 0);
			ret = rtk_fc_flow_operation_add(flow_op, &flow_ops_data);
			if(!ret)
				rtlglue_printf("Clear flow group mib2, flow group2 index %d!\n", flow_ops_data.data_clearFlowGrpMib);
		}
		else if(flow_op == RT_FLOW_OPS_SYNC_CT_STATE)
		{	
			// sync ct state to flow table immediately
			ret = rtk_fc_flow_operation_add(flow_op, NULL);
			if(!ret)			
				rtlglue_printf("Sync CT state to flow table!\n");
		}
		else if(flow_op == RT_FLOW_OPS_HIPRIFLOW_PTN_ACT)
		{
			// high priority flow pattern action
			bool valid_para = FALSE;
			split_str=strsep(&strptr," ");	// high priority flow pattern action type
			if(split_str==NULL){
				goto INVALID_PARA;
			}

			if(strcasecmp(split_str,"ACT")==0)
			{
				split_str=strsep(&strptr," ");	// high priority flow pattern entry index
				if(split_str==NULL){
					goto INVALID_PARA;
				}
				if(strcasecmp(split_str,"ADD")==0)
				{
					split_str=strsep(&strptr," ");
					if(split_str==NULL){
						goto INVALID_PARA;
					}
					if(strcasecmp(split_str,"INDEX")==0)
					{
						split_str=strsep(&strptr," ");	//high priority flow pattern entry index
						if(split_str!=NULL)
						{
							flow_ops_data.data_hiPriflowPtnAct.hipriflow_ptn_act = RT_FLOW_OP_HIPRIFLOW_PTN_ACT_ADD_BY_IDX;
							flow_ops_data.data_hiPriflowPtnAct.index = simple_strtol(split_str, NULL, 0);
						}
						split_str=strsep(&strptr," ");
						while(1)
						{
							if(strcasecmp(split_str,"SIP4")==0)
							{
								char *ip_token,*split_ip_token,j;
								uint32 tempIP;

								split_str=strsep(&strptr," ");	//get SIP
								if(split_str==NULL)
									break;

								ip_token=split_str;
								tempIP=0;
								for(j=0;j<4;j++)
								{
									split_ip_token=strsep(&ip_token,".");
									tempIP|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
									if(ip_token==NULL) break;
								}
								valid_para = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip4_valid = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip4 = tempIP;

							}
							else if(strcasecmp(split_str,"DIP4")==0)
							{
								char *ip_token,*split_ip_token,j;
								uint32 tempIP;

								split_str=strsep(&strptr," ");	//get DIP
								if(split_str==NULL)
									break;

								ip_token=split_str;
								tempIP=0;
								for(j=0;j<4;j++)
								{
									split_ip_token=strsep(&ip_token,".");
									tempIP|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
									if(ip_token==NULL) break;
								}
								valid_para = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip4_valid = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip4 = tempIP;
							}
							else if(strcasecmp(split_str,"SIP6")==0)
							{
								split_str=strsep(&strptr," ");	//get SIP
								if(split_str==NULL)
									break;

								valid_para = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip6_valid = TRUE;
								in6_pton(split_str, -1, flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip6, -1, NULL);
							}
							else if(strcasecmp(split_str,"DIP6")==0)
							{
								split_str=strsep(&strptr," ");	//get DIP
								if(split_str==NULL)
									break;

								valid_para = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip6_valid = TRUE;
								in6_pton(split_str, -1, flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip6, -1, NULL);
							}
							else if(strcasecmp(split_str,"SPORT")==0)
							{
								split_str=strsep(&strptr," ");	//get SPORT
								if(split_str==NULL)
									break;

								valid_para = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.sport_valid= TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.sport= simple_strtol(split_str, NULL, 0);
							}
							else if(strcasecmp(split_str,"DPORT")==0)
							{
								split_str=strsep(&strptr," ");	//get DPORT
								if(split_str==NULL)
									break;

								valid_para = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.dport_valid= TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.dport= simple_strtol(split_str, NULL, 0);
							}
							else if(strcasecmp(split_str,"L4PROTO")==0)
							{
								split_str=strsep(&strptr," ");	//get L4 prorocol
								if(split_str==NULL)
									break;

								valid_para = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.flowPattern.l4proto_valid= TRUE;
								if(strcasecmp(split_str,"UDP")==0)
								{
									flow_ops_data.data_hiPriflowPtnAct.flowPattern.l4proto = RT_FLOW_OP_FLOW_PATTERN_L4PROTO_UDP;
								}
								else if(strcasecmp(split_str,"TCP")==0)
								{
									flow_ops_data.data_hiPriflowPtnAct.flowPattern.l4proto = RT_FLOW_OP_FLOW_PATTERN_L4PROTO_TCP;
								}
								else
								{
									valid_para = FALSE;
									break;
								}
							}
							if (strptr==NULL) break;

							split_str=strsep(&strptr," ");	//get next token
						}
					}
				}
				else if(strcasecmp(split_str,"DEL")==0)
				{
					split_str=strsep(&strptr," ");
					if(split_str!=NULL){
						if(strcasecmp(split_str,"INDEX")==0)
						{
							split_str=strsep(&strptr," ");	//high priority flow pattern entry index
							if(split_str!=NULL)
							{
								valid_para = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.hipriflow_ptn_act = RT_FLOW_OP_HIPRIFLOW_PTN_ACT_DEL_BY_IDX;
								flow_ops_data.data_hiPriflowPtnAct.index = simple_strtol(split_str, NULL, 0);
							}
						}
					}
				}
				else if(strcasecmp(split_str,"GET")==0)
				{
					split_str=strsep(&strptr," ");
					if(split_str!=NULL){
						if(strcasecmp(split_str,"INDEX")==0)
						{
							split_str=strsep(&strptr," ");	//high priority flow pattern entry index
							if(split_str!=NULL)
							{
								valid_para = TRUE;
								flow_ops_data.data_hiPriflowPtnAct.hipriflow_ptn_act = RT_FLOW_OP_HIPRIFLOW_PTN_ACT_GET_BY_IDX;
								flow_ops_data.data_hiPriflowPtnAct.index = simple_strtol(split_str, NULL, 0);
							}
						}
					}
				}
				else if(strcasecmp(split_str,"FLUSH")==0)
				{
					valid_para = TRUE;
					flow_ops_data.data_hiPriflowPtnAct.hipriflow_ptn_act = RT_FLOW_OP_HIPRIFLOW_PTN_ACT_FLUSH;
				}
			}
			if(!valid_para)
			{
				goto INVALID_PARA;
			}
			else
			{
				ret = rtk_fc_flow_operation_add(flow_op, &flow_ops_data);
				if(!ret)
				{
					if((flow_ops_data.data_hiPriflowPtnAct.hipriflow_ptn_act == RT_FLOW_OP_HIPRIFLOW_PTN_ACT_ADD_BY_IDX) || (flow_ops_data.data_hiPriflowPtnAct.hipriflow_ptn_act == RT_FLOW_OP_HIPRIFLOW_PTN_ACT_GET_BY_IDX))
					{
						rtlglue_printf("%s hiPriflowPtnAct[%d]:\n", (flow_ops_data.data_hiPriflowPtnAct.hipriflow_ptn_act == RT_FLOW_OP_HIPRIFLOW_PTN_ACT_ADD_BY_IDX)? "Add":"Get", flow_ops_data.data_hiPriflowPtnAct.index);
						rtlglue_printf("   %-15s %-5s", "SIP4_valid:", flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip4_valid?"TRUE":"FALSE");
						if(flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip4_valid)
							rtlglue_printf("   SIP4: %pI4h\n", &(flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip4));
						else
							rtlglue_printf("\n");
						rtlglue_printf("   %-15s %-5s", "DIP4_valid:", flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip4_valid?"TRUE":"FALSE");
						if(flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip4_valid)
							rtlglue_printf("   DIP4: %pI4h\n", &(flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip4));
						else
							rtlglue_printf("\n");
						rtlglue_printf("   %-15s %-5s", "SIP6_valid:", flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip6_valid?"TRUE":"FALSE");
						if(flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip6_valid)
							rtlglue_printf("   SIP6: %pI6c\n", &(flow_ops_data.data_hiPriflowPtnAct.flowPattern.sip6[0]));
						else
							rtlglue_printf("\n");
						rtlglue_printf("   %-15s %-5s", "DIP6_valid:", flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip6_valid?"TRUE":"FALSE");
						if(flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip6_valid)
							rtlglue_printf("   DIP6: %pI6c\n", &(flow_ops_data.data_hiPriflowPtnAct.flowPattern.dip6[0]));
						else
							rtlglue_printf("\n");
						rtlglue_printf("   %-15s %-5s", "SPORT_valid:", flow_ops_data.data_hiPriflowPtnAct.flowPattern.sport_valid?"TRUE":"FALSE");
						if(flow_ops_data.data_hiPriflowPtnAct.flowPattern.sport_valid)
							rtlglue_printf("   SPORT: %d\n", flow_ops_data.data_hiPriflowPtnAct.flowPattern.sport);
						else
							rtlglue_printf("\n");
						rtlglue_printf("   %-15s %-5s", "DPORT_valid:", flow_ops_data.data_hiPriflowPtnAct.flowPattern.dport_valid?"TRUE":"FALSE");
						if(flow_ops_data.data_hiPriflowPtnAct.flowPattern.dport_valid)
							rtlglue_printf("   DPORT: %d\n", flow_ops_data.data_hiPriflowPtnAct.flowPattern.dport);
						else
							rtlglue_printf("\n");
						rtlglue_printf("   %-15s %-5s", "L4PROTO_valid:", flow_ops_data.data_hiPriflowPtnAct.flowPattern.l4proto_valid?"TRUE":"FALSE");
						if(flow_ops_data.data_hiPriflowPtnAct.flowPattern.l4proto_valid)
							rtlglue_printf("   L4PROTO: %s\n", (flow_ops_data.data_hiPriflowPtnAct.flowPattern.l4proto == RT_FLOW_OP_FLOW_PATTERN_L4PROTO_UDP)? "UDP":"TCP");
						else
							rtlglue_printf("\n");
					}
					else if(flow_ops_data.data_hiPriflowPtnAct.hipriflow_ptn_act == RT_FLOW_OP_HIPRIFLOW_PTN_ACT_DEL_BY_IDX)
					{
						rtlglue_printf("Delete hiPriflowPtnAct[%d]:\n", flow_ops_data.data_hiPriflowPtnAct.index);
					}
					else if(flow_ops_data.data_hiPriflowPtnAct.hipriflow_ptn_act == RT_FLOW_OP_HIPRIFLOW_PTN_ACT_FLUSH)
					{
						rtlglue_printf("Flush hiPriflowPtnAct table:\n");
					}
				}
			}
		}
		else
		{
			goto INVALID_PARA;
		}
	}

	if(ret)
	{
		rtlglue_printf("flow_operation_add Fail\n");
		return count;
	}

	return count;
INVALID_PARA:
	rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
	_rtk_fc_proc_flow_operation_usage();
	return count;
}

int rtk_fc_proc_flow_operation_get(struct seq_file *s, void *v)
{
	_rtk_fc_proc_flow_operation_usage();
	return SUCCESS;
}


int rtk_fc_proc_nonfdbUc_timeout_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("non-fdb-unicast timeout %d sec\n",fc_db.ucTimeout_unit1s);
	return SUCCESS;
}
int rtk_fc_proc_nonfdbUc_timeout_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int time = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	if(time < 1)
	{
		rtlglue_printf("\ninvalid input, keep origrinal setting: %d s\n",fc_db.ucTimeout_unit1s);
		return len;
	}
	fc_db.ucTimeout_unit1s = time;
	rtk_fc_proc_nonfdbUc_timeout_get(NULL, NULL);

	return len;
}


int rtk_fc_proc_nonStaticMc_timeout_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("non-static-multicast timeout %d sec\n",fc_db.mcTimeout_unit1s);
	return SUCCESS;
}
int rtk_fc_proc_nonStaticMc_timeout_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int time = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	if(time < 1)
	{
		rtlglue_printf("\ninvalid input, keep origrinal setting: %d s\n",fc_db.mcTimeout_unit1s);
		return len;
	}
	fc_db.mcTimeout_unit1s = time;
	rtk_fc_proc_nonStaticMc_timeout_get(NULL, NULL);

	return len;
}


int rtk_fc_proc_nonCtFlowTimeoutSec_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Non-IP(IP Conntrack) flow timeout period: %d sec.\n",fc_db.flowIdleTimeout_unit1s);
	return SUCCESS;
}
int rtk_fc_proc_nonCtFlowTimeoutSec_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int time = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	if(time < 1 || time >= (1 << MAX_FLOW_IDLESEC_WIDTH))
	{
		rtlglue_printf("invalid input, keep origrinal setting: %d sec. (The value should be 1~%d.)\n",fc_db.flowIdleTimeout_unit1s, (1 << MAX_FLOW_IDLESEC_WIDTH)-1);
		return len;
	}
	fc_db.flowIdleTimeout_unit1s = time;
	rtk_fc_proc_nonCtFlowTimeoutSec_get(NULL, NULL);

	return len;
}

int32 _rtk_fc_switch_version_get(uint32 * pChipId, uint32 * pRev, uint32 * pSubtype)
{
	int ret;
	uint32 ChipId, Rev, Subtype;
	int32 rtk_switch_version_get(uint32 *pChipId, uint32 *pRev, uint32 *pSubtype);
	ret=rtk_switch_version_get(&ChipId, &Rev, &Subtype);
	*pChipId=ChipId;

	 *pRev=Rev;
	*pSubtype=Subtype;
	return ret;
}


#if defined(CONFIG_RTK_FC_IPSEC_FASTFWD)

char *name_of_ipsec_hash[]={ // mapping to rtk_fc_flow_proto_ctrl_t
	"NONE",
	"NONE",
	"MD5HMAC",
	"SHA1HMAC",
	"NONE",
	"SHA2_256HMAC",
	"SHA2_384HMAC",
	"SHA2_512HMAC",
	"RIPEMD160HMAC",
	"AES_XCBC_MAC",
};
char *name_of_ipsec_cipher[]={ // mapping to rtk_fc_flow_proto_ctrl_t
	"NONE",
	"NONE",
	"DESCBC",
	"3DESCBC",
	
	"NONE",
	"NONE",
	"CASTCBC",
	"BLOWFISHCBC",
	"NONE",
	"NONE",
	"NONE",
	"NONE",
	"AESCBC",
	"AESCTR",
	"AES_CCM_ICV8",
	"AES_CCM_ICV12",
	"AES_CCM_ICV16",
	"NONE",
	"AES_GCM_ICV8",
	"AES_GCM_ICV12",
	"AES_GCM_ICV16",
	"NONE",
	"CAMELLIACBC",
	"NULL_AES_GMAC",
};

void rtk_fc_proc_ipsec_hook_table_usage(void) 
{	
	rtlglue_printf("[Usage] echo {OPTION} > /proc/fc/sw_dump/ipsec_hook_table\n");
	
	rtlglue_printf("{OPTION}0: HASH_PADDING_TABLE\n");
	rtlglue_printf("        1: FC_HOOK_TABLE\n");
	//_rtk_fc_ipec_cbc_test();
}
int rtk_fc_proc_ipsec_hook_table_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int table = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	int i = 0, j, ivlen;
 	int HASH_PADDING_TABLE = 0;
	int FC_HOOK_TABLE = 1;
	int CLEAR_TABLE = 3;
	
	if(table < HASH_PADDING_TABLE || table > CLEAR_TABLE)
	{
		rtk_fc_proc_ipsec_hook_table_usage();
		return len;
	}
	else if(table == HASH_PADDING_TABLE)
	{
		rtlglue_printf("==========================Hash padding=====================================================\n");
		for(i = 0; i < 10 ; i ++)
		{
			
			rtlglue_printf("i = %d, encrypt len = %d\n",i, (i+1)*16);
			rtlglue_printf("hash_padlen = %d\n",fc_db.rtk_fc_ipsec_hash_padding_ary[i].hash_padlen);
			for(j = 0 ; j < fc_db.rtk_fc_ipsec_hash_padding_ary[i].hash_padlen ; j++)
			{
				rtlglue_printf("%x ",fc_db.rtk_fc_ipsec_hash_padding_ary[i].hash_padding[j]);
				if( (j+1)%16 ==0)
					rtlglue_printf("\n");
			}
			rtlglue_printf("\n");
		}
	}
	else if(table == FC_HOOK_TABLE)
	{
		rtlglue_printf("rtk_fc_cryptoEngine_baseaddr = %p\n",rtk_fc_cryptoEngine_baseaddr);
		rtlglue_printf("==========================FC hook table=====================================================\n");
		rtlglue_printf("==========================DS acl=====================================================\n");
		rtlglue_printf("isSet: %d index = 0x%x\n",fc_db.esp_ds_reserved_acl_isSet, fc_db.esp_ds_reserved_acl_index);
		rtlglue_printf("=====================================================================================\n");
		
		for(i = 0; i < RTK_FC_SPEC_XFRM_INFO_MAX_NUM ; i ++)
		{
			if(fc_db.fc_ipsec_info[i].valid ==0)
				continue;

			rtlglue_printf("============[%d]=============\n",i);
			rtlglue_printf("valid = %d\n",fc_db.fc_ipsec_info[i].valid);
			rtlglue_printf("hash_alg = %d (%s)\n",fc_db.fc_ipsec_info[i].aalgo, fc_db.fc_ipsec_info[i].aalgo > 250 ?"NONE":name_of_ipsec_hash[fc_db.fc_ipsec_info[i].aalgo]);
			rtlglue_printf("ciph_alg = %d (%s)\n",fc_db.fc_ipsec_info[i].ealgo, fc_db.fc_ipsec_info[i].ealgo > 250 ?"NONE":name_of_ipsec_cipher[fc_db.fc_ipsec_info[i].ealgo]);
			if(fc_db.fc_ipsec_info[i].ip_version == 0)
			{
				rtlglue_printf("SIP = %d.%d.%d.%d (%x)\n", (fc_db.fc_ipsec_info[i].saddr>>24)&0xff, 
															(fc_db.fc_ipsec_info[i].saddr>>16)&0xff, 
															(fc_db.fc_ipsec_info[i].saddr>>8)&0xff, 
															(fc_db.fc_ipsec_info[i].saddr)&0xff, fc_db.fc_ipsec_info[i].saddr);
				rtlglue_printf("DIP = %d.%d.%d.%d (%x)\n", (fc_db.fc_ipsec_info[i].daddr>>24)&0xff, 
															(fc_db.fc_ipsec_info[i].daddr>>16)&0xff, 
															(fc_db.fc_ipsec_info[i].daddr>>8)&0xff, 
															(fc_db.fc_ipsec_info[i].daddr)&0xff, fc_db.fc_ipsec_info[i].daddr);
			}
			else if(fc_db.fc_ipsec_info[i].ip_version == 1)
			{
				rtlglue_printf("SIP6 = %x %x %x %x\n",fc_db.fc_ipsec_info[i].saddr6.s6_addr32[0], fc_db.fc_ipsec_info[i].saddr6.s6_addr32[1], fc_db.fc_ipsec_info[i].saddr6.s6_addr32[2], fc_db.fc_ipsec_info[i].saddr6.s6_addr32[3]);
				rtlglue_printf("DIP6 = %x %x %x %x\n",fc_db.fc_ipsec_info[i].daddr6.s6_addr32[0], fc_db.fc_ipsec_info[i].daddr6.s6_addr32[1], fc_db.fc_ipsec_info[i].daddr6.s6_addr32[2], fc_db.fc_ipsec_info[i].daddr6.s6_addr32[3]);

			}
			rtlglue_printf("iv_len = %d\n",fc_db.fc_ipsec_info[i].iv_len);
			rtlglue_printf("direction = %d\n",fc_db.fc_ipsec_info[i].direction);
			rtlglue_printf("spi = %x (ntohl= %x)\n", fc_db.fc_ipsec_info[i].spi , ntohl(fc_db.fc_ipsec_info[i].spi));
			rtlglue_printf("block_size = %d\n",fc_db.fc_ipsec_info[i].block_size);
			rtlglue_printf("Seq_no = %d\n",fc_db.fc_ipsec_info[i].seq_no);
			rtlglue_printf("auth_len = %d\n",fc_db.fc_ipsec_info[i].auth_len);
			rtlglue_printf("header_len = %d\n",fc_db.fc_ipsec_info[i].header_len);
			rtlglue_printf("remote_lut_idx = %d\n",fc_db.fc_ipsec_info[i].remote_lut_idx);
			rtlglue_printf("ESN = %d\n",fc_db.fc_ipsec_info[i].is_ESN);
			rtlglue_printf("mark v = 0x%08x\n",fc_db.fc_ipsec_info[i].mark_v);
			rtlglue_printf("mark m = 0x%08x\n",fc_db.fc_ipsec_info[i].mark_m);
			rtlglue_printf("NAT-T = %d\n",fc_db.fc_ipsec_info[i].is_NATT);
			ivlen = fc_db.fc_ipsec_info[i].iv_len;
			for(j = 0 ; j < ivlen ; j ++)
			{	
				
				rtlglue_printf("%x ", fc_db.fc_ipsec_info[i].salt_in[j]);

			}
			rtlglue_printf("\n");
			rtlglue_printf("key_is_set = %d\n",fc_db.fc_ipsec_info[i].key_is_set);
			
			rtlglue_printf("key_size = %d\n",fc_db.fc_ipsec_info[i].key_sz);
			rtlglue_printf("key_index = %d\n",fc_db.fc_ipsec_info[i].key_index);
			rtlglue_printf("key = ");
			for(j = 0 ; j< fc_db.fc_ipsec_info[i].key_sz ; j++ )
			{
				rtlglue_printf("%02x ", fc_db.fc_ipsec_info[i].key[j] );
			}rtlglue_printf("\n");
			
			rtlglue_printf("hash key size = %d\n",fc_db.fc_ipsec_info[i].key_hash_sz);
			rtlglue_printf("hash key_index = %d\n",fc_db.fc_ipsec_info[i].hash_key_index);
			rtlglue_printf("hash key = ");
			for(j = 0 ; j< fc_db.fc_ipsec_info[i].key_hash_sz ; j++ )
			{
				rtlglue_printf("%02x ", fc_db.fc_ipsec_info[i].key_hash[j] );
			}rtlglue_printf("\n");
			rtlglue_printf("nonce = \n");
			for(j = 0 ; j< 4 ; j++ )
			{
				rtlglue_printf("%02x ", fc_db.fc_ipsec_info[i].nonce[j] );
			}rtlglue_printf("\n");
			rtlglue_printf("salt_in = \n");
			for(j = 0 ; j< fc_db.fc_ipsec_info[i].iv_len ; j++ )
			{
				rtlglue_printf("%02x ", fc_db.fc_ipsec_info[i].salt_in[j] );
			}rtlglue_printf("\n");
			rtlglue_printf("pe_crypto_sw_id = 0x%x, ", fc_db.fc_ipsec_info[i].pe_crypto_sw_id);
			if(fc_db.fc_ipsec_info[i].pe_crypto_sw_id==0)
				rtlglue_printf("Invalid\n");
			else	
				rtlglue_printf("[PE %s] connection_idx %u\n", (fc_db.fc_ipsec_info[i].pe_crypto_sw_id&RT_PE_IPSEC_SW_ID_DECRYPTION_BIT)?"Decrypt":"Encrypt", (fc_db.fc_ipsec_info[i].pe_crypto_sw_id&RT_PE_IPSEC_SW_ID_CONNECTION_IDX_MASK)-RT_PE_IPSEC_SW_ID_CONNECTION_IDX_BASE);
			
		
			if(fc_db.fc_ipsec_info[i].is_NATT)
			{
					rtlglue_printf("Content buffer:\n");
					rtk_fc_hexdump(&fc_db.fc_ipsec_info[i].contentBuffer[0], 20+8+8);
			}
			else
			{
					rtlglue_printf("Content buffer:\n");
					rtk_fc_hexdump(&fc_db.fc_ipsec_info[i].contentBuffer[0], 20+8);
			}
		}
	}
	else if(table == CLEAR_TABLE)
	{
		_rtk_fc_ipsec_saInfo_flush_all();
	}
	
	
	return len;
}
int rtk_fc_proc_ipsec_hook_table_get(struct seq_file *s, void *v)
{
	rtk_fc_proc_ipsec_hook_table_usage();
	//_rtk_fc_ipec_ipv6_aes_test();
	return SUCCESS;

}

int rtk_fc_proc_ipsec_ipi_info_get(struct seq_file *s, void *v)
{
	rtk_fc_ipsec_dump_ipi_info();
	return SUCCESS;

}

#endif

int rtk_fc_proc_devGwMac_get(struct seq_file *s, void *v)
{
	unsigned int privflags;
	int i,j;
	for(i=DEVIFIDX_VALID_MIN ; i<RTK_FC_DEV_GW_MAC_TBL ;i++ )
	{
		if(!fc_db.devGwMacTbl[i].dev)
			continue;
		PROC_PRINTF("[%d]Dev:%s Mac:%pM lutIdx:%d myMacIdx:%d \n",i,fc_db.devGwMacTbl[i].dev->name,fc_db.devGwMacTbl[i].dev->dev_addr,fc_db.devGwMacTbl[i].hwlutIdx,fc_db.devGwMacTbl[i].myMacIdx);
		PROC_PRINTF("  \t ifidx:%d	DevFlag:0x%x[%s%s%s]\n",fc_db.devGwMacTbl[i].dev->ifindex,fc_db.devGwMacTbl[i].dev->flags,
			fc_db.devGwMacTbl[i].dev->flags&IFF_UP?"/IFF_UP":"IFF_DOWN",
			fc_db.devGwMacTbl[i].dev->flags&IFF_POINTOPOINT?"/IFF_POINTOPOINT":"",
			fc_db.devGwMacTbl[i].dev->flags&IFF_NOARP?"/IFF_NOARP":"");

		RTK_FC_HOOK_PS_DEV_GET_PRIV_FLAGS(fc_db.devGwMacTbl[i].dev, &privflags);
		PROC_PRINTF("  \t privflags:0x%x%s%s\n", privflags,
			(RTK_FC_HOOK_PS_DEV_IS_PRIV_FLAGS_SET(fc_db.devGwMacTbl[i].dev, RTK_FC_NETDEV_PRIV_FLAG_TYPE_EBRIDGE))?"[TYPE_EBRIDGE]":"",
			(RTK_FC_HOOK_PS_DEV_IS_PRIV_FLAGS_SET(fc_db.devGwMacTbl[i].dev, RTK_FC_NETDEV_PRIV_FLAG_TYPE_DOMAIN_WAN))?"[TYPE_DOMAIN_WAN]":"");

		PROC_PRINTF("  \t macport:%d extmacport:%d wlanIdx:%d\n",fc_db.devGwMacTbl[i].macportidx,fc_db.devGwMacTbl[i].macextportidx,fc_db.devGwMacTbl[i].wlanidx);
		if(fc_db.devGwMacTbl[i].maptInfoValid)PROC_PRINTF("  \t draftVer:%d prefixLen:%d\n",fc_db.devGwMacTbl[i].maptDraftVer,fc_db.devGwMacTbl[i].maptPrefxLen);
		if(fc_db.devGwMacTbl[i].isXLAT464)PROC_PRINTF("  \t isXLAT464:1 dualIpv6HashMask:%x\n",fc_db.dualIpv6HashMask);
		for(j=0;j<MAX_SW_FRAGIPID_SIZE;j++)
		{
			if(fc_db.devGwMacTbl[i].psFragIPID[j].valid)
				PROC_PRINTF("  \t psFragIPID[%d]:ori %d new %d\n",j,fc_db.devGwMacTbl[i].psFragIPID[j].oriIPID,fc_db.devGwMacTbl[i].psFragIPID[j].newIPID);
		}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		PROC_PRINTF("\t[RX WRAP]uc:%llu, nuc:%llu, drop: %llu, error: %llu\n",fc_db.devGwMacTbl[i].netifmib_wrapRnd_cnt.rx_uc_pkt_wrap_cnt,fc_db.devGwMacTbl[i].netifmib_wrapRnd_cnt.rx_nuc_pkt_wrap_cnt,fc_db.devGwMacTbl[i].netifmib_wrapRnd_cnt.rx_drop_pkt_wrap_cnt,fc_db.devGwMacTbl[i].netifmib_wrapRnd_cnt.rx_error_pkt_wrap_cnt);
		PROC_PRINTF("\t[TX WRAP]uc:%llu, nuc:%llu, drop: %llu\n",fc_db.devGwMacTbl[i].netifmib_wrapRnd_cnt.tx_uc_pkt_wrap_cnt,fc_db.devGwMacTbl[i].netifmib_wrapRnd_cnt.tx_nuc_pkt_wrap_cnt,fc_db.devGwMacTbl[i].netifmib_wrapRnd_cnt.tx_drop_pkt_wrap_cnt);

#endif
	}
	return SUCCESS;
}


int rtk_fc_proc_devGwMac_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int dual_type,value;
	int ret;
	char devName[32];
	int strlen = (len >= (CMD_BUFF_SIZE-1)) ? (CMD_BUFF_SIZE-1) : len;
	rtk_fc_dual_devInfoConfig_t dual_config;

	memset(&dual_config,0,sizeof(dual_config));

	if (buff)
	{
		/* copy data to the buffer */
		strncpy(tmpBuf+1, buff, strlen);
		tmpBuf[strlen] = '\0';
		tmpBuf[0] = ' ';

		if(_rtk_fc_parseGetValue(tmpBuf,&devName,"DEVIFNAME",STRING) != SUCCESS)
		{
			printk("DEVIFNAME parser error\n");return len;
		}
	
		if(_rtk_fc_parseGetValue(tmpBuf,&dual_type,"DUAL_TYPE",UNSIGNED_LONG) != SUCCESS)
		{
			printk("DUAL_TYPE parser error\n");	return len;
		}

		if(dual_type == RTK_FC_API_DUALTYPE_XLAT464)
		{
			if(_rtk_fc_parseGetValue(tmpBuf,&value,"CLAT_PREFIX",UNSIGNED_LONG) == SUCCESS)
			{
				dual_config.xlat464_info.clat_prefixLen=value;
			}
			if(_rtk_fc_parseGetValue(tmpBuf,&value,"PLAT_PREFIX",UNSIGNED_LONG) == SUCCESS)
			{
				dual_config.xlat464_info.plat_prefixLen=value;
			}
			printk("dev:%s dualType:%d  plat_prefix:%d clat_prefix:%d \n",devName,dual_type,dual_config.xlat464_info.plat_prefixLen,dual_config.xlat464_info.clat_prefixLen);
		}

		ret = rtk_fc_dual_devInfoConfig(devName,dual_type,&dual_config);
		if(ret)
			printk("rtk_fc_dual_devInfoConfig config error:%d \n",ret);
		
	}

	return len;
}


int rtk_fc_proc_version_get(struct seq_file *s, void *v)
{
	unsigned int apolloChipId,apolloRev,apolloSubtype;

	//Check switch chip revision
	ASSERT_EQ(_rtk_fc_switch_version_get(&apolloChipId,&apolloRev,&apolloSubtype),RT_ERR_OK);

	PROC_PRINTF("FleetConntrackDrv(v%d): %s (0x%x 0x%x 0x%x)\n",
		FC_INTERNAL_VERSION, FC_GIT_VERSION, apolloChipId, apolloRev, apolloSubtype);
	PROC_PRINTF("ModuleName: %s\n", CONFIG_RTK_L34_MODULE_STRING);
	PROC_PRINTF("PhyPortStatus = 0x%llx\n", fc_db.systemGlobal.phyPortStatus);


	PROC_PRINTF("\n");

	PROC_PRINTF("%-16s: %s\n", "GIT SHA1", FC_GIT_SHA1);
	PROC_PRINTF("%-16s: %s\n", "GIT Date", FC_GIT_SHA1_DATE);
	PROC_PRINTF("%-16s: %s\n", "GIT Author", FC_GIT_AUTHOR);
	PROC_PRINTF("%-16s: %s\n", "Compile Date", FC_COMPILE_DATE);
	PROC_PRINTF("%-16s: %s\n", "Compile by", FC_COMPILE_BY);
	return SUCCESS;
}

int rtk_fc_proc_wlan_devmap_dump(struct seq_file *s, void *v)
{
	int ret = 0, i = 0, client_mode = 0;
	rtk_fc_wlan_devmap_t *devmap = NULL;
	struct net_device *wlanDev = NULL;

#if defined(RTK_FC_WLAN_HWLOOKUP_LSPID_FOR_BACKPRESSURE)
	PROC_PRINTF("Wifi Rx hw lookup lspid: 0x%x\n", RTK_FC_WLAN_HWLOOKUP_LSPID_FOR_BACKPRESSURE);
#endif

	PROC_PRINTF("\n%-6s %-16s %-8s %-8s %-8s %-4s %-12s %-8s %-4s %-8s\n", "DEVID", "DEV_NAME", "MAC_PORT", "EXTPORT", "PROPERTY", "BAND", "BLOCK_RELAY", "CLIENT", "CB", "DEV_PTR");
	for(i = 0; i < RTK_FC_WLANX_END_INTF; i++) {

		ret = RTK_FC_HELPER_WLAN_DEVMAP_GET(i, &devmap);
		if((ret == SUCCESS) && (devmap!=NULL) && (devmap->valid)) {
			ret = RTK_FC_HELPER_WLAN_DEVID_TO_DEV(i, &wlanDev);
			if(ret == SUCCESS) {
				PROC_PRINTF("%-6d %-16s %-2d(0x%02x) %-8d %-8s %-4d %-12d %-8d 0x%-2x %p\n", 
					i, RTK_FC_HELPER_WLAN_DEVMAP_DEVNAME_GET(devmap), RTK_FC_HELPER_WLAN_DEVMAP_MACPORTIDX_GET(devmap), RTK_FC_HELPER_WLAN_DEVMAP_MACPORTIDX_GET(devmap), RTK_FC_HELPER_WLAN_DEVMAP_MACEXTPORTIDX_GET(devmap), 
					RTK_FC_HELPER_WLAN_DEVMAP_SHAREEXTPORT_GET(devmap) ? "N-to-1" : "1-to-1", RTK_FC_HELPER_WLAN_DEVMAP_BAND_GET(devmap), 
					devmap->attr.block_relay, devmap->attr.client_mode, rtk_fc_wlan_attr_cb_mask_val(devmap), wlanDev);

				if(devmap->attr.client_mode)
					client_mode = 1;
			}
		}
	}

	if(RTK_FC_HELPER_WLAN_CLIENT_MODE_CB_IS_REGISTERED())
		PROC_PRINTF("client mode (NAT2.5) callback func is registered.\n");
	if(client_mode)
		PROC_PRINTF("client mode CB value:\n\t 0x1: NAT2.5; 0x2: UC2MC; 0x4: LOOPBACK_DETECT\n");
	
#if defined(CONFIG_FC_RTL9607C_SERIES)	
	if(fc_db.controlFuc.wifi_tx_gmac_auto_sel_en)
	{
		PROC_PRINTF("\n--------------------------------------------------------------------------------------\n");
		PROC_PRINTF("wifi_tx_gmac_sel table:   (gmac_trunking_num: %d)\n", fc_db.controlFuc.wifi_tx_gmac_trunking_num);
		PROC_PRINTF("%-8s %-13s %-8s\n", "SPA", "SEL_STATE", "SEL_MODE");
		for(i = RTK_FC_MAC_PORT0; i < RTK_FC_MAC_PORT_MAX; i++) {
			
			PROC_PRINTF("%-8d %-13d %-8s\n", i, fc_db.controlFuc.wifi_tx_gmac_sel[i], name_of_wifi_gmac_sel[fc_db.controlFuc.wifi_tx_gmac_sel[i]]);
		}
	}
#endif

	PROC_PRINTF("\n");
	return SUCCESS;
}
void rtk_fc_proc_wlan_devmap_set_usage(void){
	
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("    echo ?  > /proc/fc/... to dump current setting\n");
	rtlglue_printf("    echo add dev {devname} [port] {0x%x..0x%x} [band] {0|1|2} > /proc/fc/... \n", RTK_FC_MAC_PORT_CPU, RTK_FC_MAC_PORT_LASTCPU);
	rtlglue_printf("    echo del dev {devname} > /proc/fc/... \n");
	rtlglue_printf("    echo flush > /proc/fc/... \n");
#else
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("    echo ?  > /proc/fc/... to dump current setting\n");
#endif
}

//.unlockBefortWrite=1
int rtk_fc_proc_wlan_devmap_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	char *cmdParam[CMD_PARAM_COUNT] = {0};
	uint8 cmdPosi = 0;
	int ret = SUCCESS;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	uint8_t wlanDevIdx;
	rtk_fc_wlan_devMapCfg_t wlanDevMapCfg = {0};
	char devName[IFNAMSIZ];
	

	// cmd: echo [add|del|flush] idx <idx> dev <dev_str> port <macport_idx> > /proc/fc/...
	if((ret = _rtk_fc_proc_parsing_string_to_param_arrary(buff, len, &cmdParam[0])) != SUCCESS) {
		goto INVALID_CMD;
	}
	// show detail configuration
	if(strcasecmp(cmdParam[0], "?")==0) {
		rtk_fc_proc_wlan_devmap_set_usage();
		RTK_FC_HELPER_WLAN_DEVMAP_INITMAP_DUMP();
		return len;
	}else if(strcasecmp(cmdParam[0], "add")==0) {
		// add by dev name
		if(cmdParam[2]==NULL) {
			goto INVALID_CMD;
		}
		cmdPosi = 1;
		
		if(strcasecmp(cmdParam[cmdPosi++], "dev")==0) {	
			memcpy(devName, cmdParam[cmdPosi], IFNAMSIZ);
			cmdPosi++;
		}else{
			WARNING("error");
			goto INVALID_CMD;
		}

		// give initial setting
		wlanDevMapCfg.macPortIdx = RTK_FC_MAC_PORT_WLAN_MAINCPU;
		wlanDevMapCfg.initBand = RTK_FC_WLAN_INIT_ID_0;
		
		// parsing optional setting
		while(1) {
			if(cmdPosi+1>=CMD_PARAM_COUNT || cmdParam[cmdPosi]==NULL || cmdParam[cmdPosi+1]==NULL)
				break;
			
			if(strcasecmp(cmdParam[cmdPosi], "port")==0) {
				wlanDevMapCfg.macPortIdx = simple_strtol(cmdParam[++cmdPosi], NULL, 0);
			}
			
			if((strcasecmp(cmdParam[cmdPosi], "band")==0) && (cmdPosi+1<CMD_PARAM_COUNT)){
				wlanDevMapCfg.initBand = RTK_FC_WLAN_INIT_ID_0 + simple_strtol(cmdParam[++cmdPosi], NULL, 0);
			}
			
			cmdPosi++;
		}
	
		if(rtk_fc_wifi_wlanDevMap_add(devName, wlanDevMapCfg, &wlanDevIdx))
		{
			WARNING("add wlanDevMap error");
			goto INVALID_CMD;
		}
	}else if(strcasecmp(cmdParam[0], "del")==0) {
		// del by dev name
		if(cmdParam[2]==NULL) {
			goto INVALID_CMD;
		}
		cmdPosi = 1;
		
		if(strcasecmp(cmdParam[cmdPosi++], "dev")==0) {
			rtk_fc_wifi_wlanDevMap_del(cmdParam[cmdPosi++]);
		}else
			goto INVALID_CMD;
	}else if(strcasecmp(cmdParam[0], "flush")==0) {
		rtk_fc_wifi_wlanDevMap_flush();
	}else {
		goto INVALID_CMD;
	}


	return len;
	
INVALID_CMD:
	_rtk_fc_proc_parsing_string_err_msg();
	rtk_fc_proc_wlan_devmap_set_usage();
	return len;
	
#else

	cmdPosi = 0;

	// cmd: echo ? > /proc/fc/...
	if((ret = _rtk_fc_proc_parsing_string_to_param_arrary(buff, len, &cmdParam[0])) != SUCCESS) {
		_rtk_fc_proc_parsing_string_err_msg();
		rtk_fc_proc_wlan_devmap_set_usage();
		return len;
	}
	// show detail configuration
	if(strcasecmp(cmdParam[0], "?")==0) {
		RTK_FC_HELPER_WLAN_DEVMAP_INITMAP_DUMP();
		return len;
	}

	return len;
#endif

}

int32 rtk_fc_smpStatistic_get(struct seq_file *s, void *v)
{
	int len=0, i, j;
	rtk_fc_mgr_smp_static_t mgr_smp_stastic;
	atomic_t *pCnt;

#if defined(CONFIG_SMP)

	if(fc_db.smpStatistic == FC_SMP_STATISTICS_DUMP_MODE_DETAILED)
	{
		rtk_fc_mgr_ring_buf_state_t ring;

		RTK_FC_HELPER_MGR_RING_BUFFER_STATE_GET(RTK_FC_MGR_DISPATCH_NIC_RX, &ring);
	/*
		PROC_PRINTF("%-15s: freeIdx %-4d schedIdx %-4d waiting for processing %-4d\n",
			"NICRx hi ring", ring.hifree_idx, ring.hisched_idx, (ring.hifree_idx+MAX_FC_NIC_RX_HIQUEUE_SIZE-ring.hisched_idx)&(MAX_FC_NIC_RX_HIQUEUE_SIZE-1));
		PROC_PRINTF("%-15s: freeIdx %-4d schedIdx %-4d waiting for processing %-4d\n",
			"NICRx lo ring", ring.free_idx, ring.sched_idx, (ring.free_idx+MAX_FC_NIC_RX_QUEUE_SIZE-ring.sched_idx)&(MAX_FC_NIC_RX_QUEUE_SIZE-1));
	*/
		RTK_FC_HELPER_MGR_RING_BUFFER_STATE_GET(RTK_FC_MGR_DISPATCH_NIC_TX, &ring);
	/*
		PROC_PRINTF("%-15s: freeIdx %-4d schedIdx %-4d waiting for processing %-4d\n",
			"NICTx ring", ring.free_idx, ring.sched_idx, (ring.free_idx+MAX_FC_NIC_TX_QUEUE_SIZE-ring.sched_idx)&(MAX_FC_NIC_TX_QUEUE_SIZE-1));
	*/

		RTK_FC_HELPER_MGR_RING_BUFFER_STATE_GET(RTK_FC_MGR_DISPATCH_WLAN0_RX, &ring);
		//RTK_FC_HELPER_MGR_RING_BUFFER_STATE_GET(RTK_FC_MGR_DISPATCH_WLAN1_RX, &ring);
		//RTK_FC_HELPER_MGR_RING_BUFFER_STATE_GET(RTK_FC_MGR_DISPATCH_WLAN2_RX, &ring);

		RTK_FC_HELPER_MGR_RING_BUFFER_STATE_GET(RTK_FC_MGR_DISPATCH_WLAN0_TX, &ring);
		//RTK_FC_HELPER_MGR_RING_BUFFER_STATE_GET(RTK_FC_MGR_DISPATCH_WLAN1_TX, &ring);
		//RTK_FC_HELPER_MGR_RING_BUFFER_STATE_GET(RTK_FC_MGR_DISPATCH_WLAN2_TX, &ring);
		
		RTK_FC_HELPER_MGR_RING_BUFFER_STATE_GET(RTK_FC_MGR_DISPATCH_PS_NETIF_RX, &ring);
	}
#endif	
	

	if(fc_db.smpStatistic == FC_SMP_STATISTICS_DUMP_MODE_DISABLED)
	{
		PROC_PRINTF("%d: disabled, please \"echo MODE > proc/fc/sw_dump/smp_statistic\" to enable it.\n", fc_db.smpStatistic);
		PROC_PRINTF("MODE\tdescription\n");
		PROC_PRINTF("------------------------------\n");
		for(i = 0 ; i < FC_SMP_STATISTICS_DUMP_MODE_MAX ; i++)
		{
			PROC_PRINTF("%d\t%s\n", i, name_of_smp_statistic_mode[i]);
		}
		return len;
	}

#if defined(CONFIG_RTK_NIC_HWLOOKUP_DEAMSDU_OFFLOAD) && defined(CONFIG_FC_RTL8277C_SERIES)
	do{
		unsigned long cnt;
		int rtk_nic_amsdu_cnt_get(unsigned long *pAmsduCnt);
		rtk_nic_amsdu_cnt_get(&cnt);
		PROC_PRINTF("%30s","WIFI_AMSDU_DIVIDE_CNT");
		PROC_PRINTF("%12lu",cnt);
#if defined(CONFIG_RTK_NIC_HWLOOKUP_SNAP_TRANSFORM_BY_HW)
		PROC_PRINTF("\t(snap transform by HW)\n");
#elif defined(CONFIG_RTK_NIC_HWLOOKUP_SNAP_TRANSFORM_BY_TX_DESC)
		PROC_PRINTF("\t(snap transform by TX DESC)\n");
#else
		PROC_PRINTF("\n");
#endif
	}while(0);
	PROC_PRINTF("==========================================================================\n");
#endif

	PROC_PRINTF("%30s","SMP_JOBS_TYPE");
	for(i = 0 ; i < NR_CPUS ; i++)
		PROC_PRINTF("%11s%d","CPU", i);

	PROC_PRINTF("\n==========================================================================\n");
				
	for(i = 0 ; i < FC_SMP_STATISTICS_DUMP_TYPE_MAX ; i++)
	{
		switch(i)
		{
			/*Rx counter*/
			//wifi rx
			case FC_SMP_DUMP_WIFI_RX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_WIFI_RX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_RX");
				break;
#if defined(CONFIG_RTK_NIC_HWLOOKUP_DEAMSDU_OFFLOAD) || defined(CONFIG_RTK_FC_WIFI_DRIVER_OFFLOAD_BY_PE)
			case FC_SMP_DUMP_WIFI_AMSDU_RX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_WIFI_AMSDU_RX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_AMSDU_RX");
				break;
#endif
			//gmac rx skip FC
			case FC_SMP_DUMP_GMAC_RX_SKIP_FC:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC_RX_SKIP_FC);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","GMAC_RX_SKIP_FC");
				break;
			//gmac rx
			case FC_SMP_DUMP_GMAC_RX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC_RX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","GMAC_RX");
				break;
			//FYB_FWD
			case FC_SMP_DUMP_GMAC_FYB_FWD:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC_FYB_FWD);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","GMAC_HYB_FWD");
				break;
			//IPI to FC rx (IPI de-quque count)
			case FC_SMP_DUMP_GMAC_IPI_TO_FC_RX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_TO_FC_RX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","GMAC_IPI_TO_FC_RX");
				break;;
			//FC rx
			case FC_SMP_DUMP_GMAC_FC_RX:
				pCnt=&fc_db.smp_statistic[FC_SMP_FC_RX][0];
				PROC_PRINTF("%30s","FC_RX");
				break;
			//from PS skip FC
			case FC_SMP_DUMP_FROM_PS_RX_SKIP_FC:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_FROM_PS_RX_SKIP_FC);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","FROM_PS_RX_SKIP_FC");
				break;
			//from PS
			case FC_SMP_DUMP_FROM_PS_RX:
				pCnt=&fc_db.smp_statistic[FC_SMP_FROM_PS_RX][0];
				PROC_PRINTF("%30s","FROM_PS_RX");
				break;

			/*Rx drop counter*/
			case FC_SMP_DUMP_IPI_PS_NETIF_RX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_PS_NETIF_RX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","TO_PS_NETIF_RX");
				break;						
			case FC_SMP_DUMP_IPI_PS_NETIF_RX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_PS_NETIF_RX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","TO_PS_NETIF_RX_DROP");
				break;				
			//IPI to FC rx drop
			case FC_SMP_DUMP_IPI_TO_FC_RX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_TO_FC_RX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","GMAC_IPI_TO_FC_RX_DROP");
				break;
			//band0 IPI to HW lookup drop
			case FC_SMP_DUMP_IPI_BAND0_HW_LOOKUP_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_BAND0_HW_LOOKUP_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","BAND0_IPI_HW_LOOKUP_DROP");
				break;
			//band1 IPI to HW lookup drop
			case FC_SMP_DUMP_IPI_BAND1_HW_LOOKUP_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_BAND1_HW_LOOKUP_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","BAND1_IPI_HW_LOOKUP_DROP");
				break;
			//band2 IPI to HW lookup drop
			case FC_SMP_DUMP_IPI_BAND2_HW_LOOKUP_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_BAND2_HW_LOOKUP_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","BAND2_IPI_HW_LOOKUP_DROP");
				break;
			//band0 IPI to HW lookup almost full times
			case FC_SMP_DUMP_IPI_BAND0_HW_LOOKUP_AF:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_BAND0_HW_LOOKUP_AF);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","BAND0_IPI_HW_LOOKUP_AF");
				break;
			//band1 IPI to HW lookup almost full times
			case FC_SMP_DUMP_IPI_BAND1_HW_LOOKUP_AF:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_BAND1_HW_LOOKUP_AF);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","BAND1_IPI_HW_LOOKUP_AF");
				break;
			//band2 IPI to HW lookup almost full times
			case FC_SMP_DUMP_IPI_BAND2_HW_LOOKUP_AF:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_BAND2_HW_LOOKUP_AF);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","BAND2_IPI_HW_LOOKUP_AF");
				break;
			//wifi rx drop
			case FC_SMP_DUMP_WIFI_RX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_WIFI_RX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_RX_DROP");
				break;
			case FC_SMP_DUMP_WIFI_RX_RECOVER_DROP:
				pCnt = &fc_db.smp_statistic[FC_SMP_WIFI_RX_RECOVER_DROP][0];
				PROC_PRINTF("%30s","WIFI_RX_RECOVER_DROP");
				break;

			/*Tx counter*/
#ifdef CONFIG_RTK_L34_XPON_PLATFORM
			//GMAC9 tx
			case FC_SMP_DUMP_GMAC9_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC0_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","GMAC9_TX");
				break;
			//GMAC10 tx
			case FC_SMP_DUMP_GMAC10_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC1_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;;
				PROC_PRINTF("%30s","GMAC10_TX");
				break;
			//GMAC7 tx
			case FC_SMP_DUMP_GMAC7_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC2_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","GMAC7_TX");
				break;
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
			//PORT_0x10 tx
			case FC_SMP_DUMP_PORT_0x10_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC0_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","NIC_0x10_TX");
				break;
			//PORT_0x11 tx
			case FC_SMP_DUMP_PORT_0x11_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC1_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","NIC_0x11_TX");
				break;
			//PORT_0x12 tx
			case FC_SMP_DUMP_PORT_0x12_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC2_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","NIC_0x12_TX");
				break;
			//PORT_0x13 tx
			case FC_SMP_DUMP_PORT_0x13_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC3_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","NIC_0x13_TX");
				break;
			//PORT_0x14 tx
			case FC_SMP_DUMP_PORT_0x14_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC4_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","NIC_0x14_TX");
				break;
			//PORT_0x15 tx
			case FC_SMP_DUMP_PORT_0x15_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC5_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","NIC_0x15_TX");
				break;
			//PORT_0x16 tx
			case FC_SMP_DUMP_PORT_0x16_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC6_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","NIC_0x16_TX");
				break;
			//PORT_0x17 tx
			case FC_SMP_DUMP_PORT_0x17_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_GMAC7_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","NIC_0x17_TX");
				break;
#endif
			//wifi band0 tx
			case FC_SMP_DUMP_WIFI_BAND0_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BAND0_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_BAND0_TX");
				break;
			//wifi band1 tx
			case FC_SMP_DUMP_WIFI_BAND1_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BAND1_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_BAND1_TX");
				break;
			//wifi band2 tx
			case FC_SMP_DUMP_WIFI_BAND2_TX:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BAND2_TX);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_BAND2_TX");
				break;
			//wifi FF tx
			case FC_MGR_DUMP_WIFI_BANDx_TX_FF:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BANDx_TX_FF);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_FF_TX");
				break;
			//wifi FF tx
			case FC_MGR_DUMP_WIFI_BANDx_TX_FF_AGG:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BANDx_TX_FF_AGG);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_FF_AGG_TX");
				break;
			//to PS tx
			case FC_SMP_DUMP_TO_PS_TX:
				pCnt=&fc_db.smp_statistic[FC_SMP_TO_PS_TX][0];
				PROC_PRINTF("%30s","TO_PS_TX");
				break;

			/*Tx drop counter*/
			case FC_SMP_DUMP_NIC_TX_BUSY:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_NIC_TX_BUSY);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				if(fc_db.controlFuc.tx_busy_to_ps == 0)
					PROC_PRINTF("%30s","NIC_TX_BUSY_DROP");
				else
					PROC_PRINTF("%30s","NIC_TX_BUSY");
				break;
			//IPI to NIC tx drop
			case FC_SMP_DUMP_IPI_TO_NIC_TX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_FC_IPI_TO_NIC_TX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","FC_IPI_TO_NIC_TX_DROP");
				break;
			//IPI to wifi band0 tx drop
			case FC_SMP_DUMP_IPI_TO_WIFI_BAND0_TX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_TO_WIFI_BAND0_TX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","IPI_TO_WIFI_BAND0_DROP");
				break;
			//IPI to wifi band1 tx drop
			case FC_SMP_DUMP_IPI_TO_WIFI_BAND1_TX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_TO_WIFI_BAND1_TX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","IPI_TO_WIFI_BAND1_DROP");
				break;
			//IPI to wifi band2 tx drop
			case FC_SMP_DUMP_IPI_TO_WIFI_BAND2_TX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_STATIC_IPI_TO_WIFI_BAND2_TX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","IPI_TO_WIFI_BAND2_DROP");
				break;
			//wifi band0 tx drop
			case FC_SMP_DUMP_WIFI_BAND0_TX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BAND0_TX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_BAND0_TX_DROP");
				break;
			//wifi band1 tx drop
			case FC_SMP_DUMP_WIFI_BAND1_TX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BAND1_TX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_BAND1_TX_DROP");
				break;
			//wifi band2 tx drop
			case FC_SMP_DUMP_WIFI_BAND2_TX_DROP:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BAND2_TX_DROP);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_BAND2_TX_DROP");
				break;
			//wifi band0 TXQ stopped times
			case FC_SMP_DUMP_WIFI_BAND0_TXQ_STOPPED_TIMES:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BAND0_TXQ_STOPPED_TIMES);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_BAND0_TXQSTOP_TIMES");
				break;
			//wifi band1 TXQ stopped times
			case FC_SMP_DUMP_WIFI_BAND1_TXQ_STOPPED_TIMES:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BAND1_TXQ_STOPPED_TIMES);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_BAND1_TXQSTOP_TIMES");
				break;
			//wifi band2 TXQ stopped times
			case FC_SMP_DUMP_WIFI_BAND2_TXQ_STOPPED_TIMES:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_WIFI_BAND2_TXQ_STOPPED_TIMES);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","WIFI_BAND2_TXQSTOP_TIMES");
				break;
			case FC_SMP_DUMP_RPS_CPU_DISTRIBUTE:
				RTK_FC_HELPER_SMP_MGRTRX_STATIC_GET(&mgr_smp_stastic, FC_MGR_SMP_RPS_CPU_DISTRIBUTE);
				pCnt = (atomic_t *)&mgr_smp_stastic;
				PROC_PRINTF("%30s","RPS_CPU_DISTRIBUTION");
				break;
		}

		for(j = 0 ; j < NR_CPUS ; j++)
			PROC_PRINTF("%12d",atomic_read(&pCnt[j]));
				PROC_PRINTF("\n");
	}

	return len;
}

int rtk_fc_smpStatistic_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int i,j;

#if defined(CONFIG_RTK_NIC_HWLOOKUP_DEAMSDU_OFFLOAD) && defined(CONFIG_FC_RTL8277C_SERIES)
	int rtk_nic_amsdu_cnt_clear(void);
	rtk_nic_amsdu_cnt_clear();
#endif
	
	// set statistic flag and clear counter
	
	fc_db.smpStatistic = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	
	for(i=0;i<FC_SMP_JOBS_TYPE_MAX;i++)
		for(j=0;j<NR_CPUS;j++)
			atomic_set(&fc_db.smp_statistic[i][j], 0);
	if(fc_db.smpStatistic >= FC_SMP_STATISTICS_DUMP_MODE_MAX)
		fc_db.smpStatistic = FC_SMP_STATISTICS_DUMP_MODE_BRIEF;

	if(fc_db.smpStatistic == FC_SMP_STATISTICS_DUMP_MODE_DISABLED)
		RTK_FC_HELPER_SMP_MGRTRX_STATIC_SET(FALSE);
	else
		RTK_FC_HELPER_SMP_MGRTRX_STATIC_SET(TRUE);
	
	return len;
}

int rtk_fc_proc_flowFlush_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	rtk_fc_flow_flush();

	return len;
}

void rtk_fc_proc_pe_func_on_pe_cpu_num_usage(void)
{
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("echo [func number] [pe cpu number] > /proc/fc/ctrl/pe_func_on_pe_cpu_num\n");
	rtlglue_printf("	[func number] please refer to rt_pe_func_num_t in rt_pe_ext.h\n");
	rtlglue_printf("	[pe cpu number]  0~%u\n", RT_PE_MAX_CPU_NUM-1);
}

int rtk_fc_proc_pe_func_on_pe_cpu_num_get(struct seq_file *s, void *v)
{
	int i;

	PROC_PRINTF(">>PE functions run on pe cpu:\n\n");
	for(i=0; i<RT_PE_FUNC_NUM_MAX; i++)
	{
		switch(i)
		{
			case RT_PE_FUNC_NUM_WIFI_TX_AMSDU:
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
				PROC_PRINTF("[WIFI_TX_AMSDU] runs on PE%u\n", fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_WIFI_TX_AMSDU]);
#else
				PROC_PRINTF("[WIFI_TX_AMSDU] is disabled\n");
#endif
				break;
			case RT_PE_FUNC_NUM_HTTP:
#if defined(CONFIG_RTK_FC_HTTP_OFFLOAD_BY_PE)
				PROC_PRINTF("[HTTP/IPERF] runs on PE%u\n", fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_HTTP]);
#else
				PROC_PRINTF("[HTTP/IPERF] is disabled\n");
#endif

				break;
			case RT_PE_FUNC_NUM_CRYPTO:
#if defined(CONFIG_RTK_FC_CRYPTO_OFFLOAD_BY_PE)
				PROC_PRINTF("[CRYPTO] runs on PE%u\n", fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_CRYPTO]);
#else
				PROC_PRINTF("[CRYPTO] is disabled\n");
#endif
				break;
			case RT_PE_FUNC_NUM_WFO:
#if defined(CONFIG_RTK_FC_WIFI_DRIVER_OFFLOAD_BY_PE)
				PROC_PRINTF("[WFO] runs on PE%u\n", fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_WFO]);
#else
				PROC_PRINTF("[WFO] is disabled\n");
#endif
				break;
			case RT_PE_FUNC_NUM_SRV6:
#if defined(CONFIG_RTK_FC_SRV6_OFFLOAD_BY_PE)
				PROC_PRINTF("[SRv6] runs on PE%u\n", fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_SRV6]);
#else
				PROC_PRINTF("[SRv6] is disabled\n");
#endif
				break;
			case RT_PE_FUNC_NUM_PKT_QUEUE:
#if defined(CONFIG_RTK_FC_PKT_QUEUE_OFFLOAD_BY_PE)
				PROC_PRINTF("[PKT QUEUE] runs on PE%u\n", fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_PKT_QUEUE]);
#else
				PROC_PRINTF("[PKT QUEUE] is disabled\n");
#endif
				break;
			case RT_PE_FUNC_NUM_PKT_TC_QUEUE_0:
#if defined(CONFIG_RTK_FC_PKT_QUEUE_OFFLOAD_BY_PE)
				PROC_PRINTF("[PKT TC QUEUE 0] runs on PE%u\n", fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_PKT_TC_QUEUE_0]);
#else
				PROC_PRINTF("[PKT TC QUEUE 0] is disabled\n");
#endif
				break;
			case RT_PE_FUNC_NUM_PKT_TC_QUEUE_1:
#if defined(CONFIG_RTK_FC_PKT_QUEUE_OFFLOAD_BY_PE)
				PROC_PRINTF("[PKT TC QUEUE 1] runs on PE%u\n", fc_db.controlFuc.func_on_pe_cpu_num[RT_PE_FUNC_NUM_PKT_TC_QUEUE_1]);
#else
				PROC_PRINTF("[PKT TC QUEUE 1] is disabled\n");
#endif
				break;
			//default:
				//PROC_PRINTF("[PE func num %u] is unknown!!\n", i);
				//break;
		}	
	}
	
	return SUCCESS;
}


int rtk_fc_proc_pe_func_on_pe_cpu_num_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	int pe_func, pe_cpu;

	if (buff)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr = tmpBuf;
		
		if ((split_str = strsep(&strptr," ")) == NULL)
			goto SHOW_USAGE;
		pe_func = simple_strtol(split_str, NULL, 0);
		if(pe_func >= RT_PE_FUNC_NUM_MAX)
			goto SHOW_USAGE;

		if ((split_str = strsep(&strptr," ")) == NULL)
			goto SHOW_USAGE;
		pe_cpu = simple_strtol(split_str, NULL, 0);
		if(pe_cpu >= RT_PE_MAX_CPU_NUM)
			goto SHOW_USAGE;

		fc_db.controlFuc.func_on_pe_cpu_num[pe_func] = pe_cpu;
		rtk_fc_proc_pe_func_on_pe_cpu_num_get(NULL, NULL);

		return count;
	}

SHOW_USAGE:
	rtk_fc_proc_pe_func_on_pe_cpu_num_usage();
	return count;
}


int rtk_fc_proc_pe_crypto_test_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
#if defined(CONFIG_RTK_FC_CRYPTO_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
	rt_pe_crypto_request_t crypto_req;
	rt_pe_ret_t pe_ret;
	uint32 test_conn_idx=3, test_key_idx=0;
	int32 fc_saInfo_idx=FAIL;

	memset(&crypto_req, 0, sizeof(rt_pe_crypto_request_t));
	crypto_req.req_cmd = _rtk_fc_proc_parsing_string_to_integer(buff, count);
	rtlglue_printf("\n");
	switch(crypto_req.req_cmd)
	{
		case RT_PE_CRYPTO_ENGINE_CMD_ENABLE:
			rtlglue_printf("[PE Crypto] Enable\n");
			break;
		case RT_PE_CRYPTO_ENGINE_CMD_DISABLE:
			rtlglue_printf("[PE Crypto] Disable\n");
			break;
		case RT_PE_CRYPTO_ENGINE_CMD_ENCRYPT_CONNECTION_ADD:
			rtlglue_printf("[PE Crypto] Add encrypt connection %u\n", test_conn_idx);
			fc_saInfo_idx = 0;
			crypto_req.connection_idx = test_conn_idx;
			crypto_req.connection.encrypt_info.key_idx = test_key_idx;
			crypto_req.connection.encrypt_info.hash_key_idx = test_key_idx;
			crypto_req.connection.encrypt_info.cipher_mode = RT_PE_CRYPTO_EALG_AES_128;
			crypto_req.connection.encrypt_info.outer_dmac.octet[0]=0x0; crypto_req.connection.encrypt_info.outer_dmac.octet[1]=0xd1; crypto_req.connection.encrypt_info.outer_dmac.octet[2]=0xd2; crypto_req.connection.encrypt_info.outer_dmac.octet[3]=0xd3; crypto_req.connection.encrypt_info.outer_dmac.octet[4]=0xd4; crypto_req.connection.encrypt_info.outer_dmac.octet[5]=0x0;
			crypto_req.connection.encrypt_info.outer_smac.octet[0]=0x0; crypto_req.connection.encrypt_info.outer_smac.octet[1]=0xc1; crypto_req.connection.encrypt_info.outer_smac.octet[2]=0xc2; crypto_req.connection.encrypt_info.outer_smac.octet[3]=0xc3; crypto_req.connection.encrypt_info.outer_smac.octet[4]=0xc4; crypto_req.connection.encrypt_info.outer_smac.octet[5]=0x0;
			crypto_req.connection.encrypt_info.outer_isIPv4OrIpv6 = 0;
			crypto_req.connection.encrypt_info.outer_dip.ipv4_addr = 0xd0d0d0d0;
			crypto_req.connection.encrypt_info.outer_sip.ipv4_addr = 0xc0c0c0c0;
			crypto_req.connection.encrypt_info.esp_spi = 0x11223344;
			crypto_req.connection.encrypt_info.esp_seq_no = 0x1;
			crypto_req.connection.encrypt_info.iv[0] = 0x84; crypto_req.connection.encrypt_info.iv[1] = 0x24; crypto_req.connection.encrypt_info.iv[2] = 0xfd; crypto_req.connection.encrypt_info.iv[3] = 0x5b;
			crypto_req.connection.encrypt_info.iv[4] = 0x33; crypto_req.connection.encrypt_info.iv[5] = 0x5f; crypto_req.connection.encrypt_info.iv[6] = 0x2b; crypto_req.connection.encrypt_info.iv[7] = 0x6a;
			crypto_req.connection.encrypt_info.iv[8] = 0xe4; crypto_req.connection.encrypt_info.iv[9] = 0xf0; crypto_req.connection.encrypt_info.iv[10] = 0x0b; crypto_req.connection.encrypt_info.iv[11] = 0xa6;
			crypto_req.connection.encrypt_info.iv[12] = 0xdf; crypto_req.connection.encrypt_info.iv[13] = 0xee; crypto_req.connection.encrypt_info.iv[14] = 0x35; crypto_req.connection.encrypt_info.iv[15] = 0xbf;
			crypto_req.connection.encrypt_info.ldpid = 7;
			crypto_req.connection.encrypt_info.tag_info.external_used.pppoe_sid = FAIL;
			crypto_req.connection.encrypt_info.tag_info.external_used.svlan_vid = FAIL;
			crypto_req.connection.encrypt_info.tag_info.external_used.cvlan_vid = FAIL;
			crypto_req.connection.encrypt_info.tag_info.external_used.pon_streamId = FAIL;
			break;
		case RT_PE_CRYPTO_ENGINE_CMD_ENCRYPT_CONNECTION_DEL:
			rtlglue_printf("[PE Crypto] Del encrypt connection %u\n", test_conn_idx);
			crypto_req.connection_idx = test_conn_idx;
			break;
		case RT_PE_CRYPTO_ENGINE_CMD_DECRYPT_CONNECTION_ADD:
			rtlglue_printf("[PE Crypto] Add decrypt connection %u\n", test_conn_idx);
			fc_saInfo_idx = 1;
			crypto_req.connection_idx = test_conn_idx;
			crypto_req.connection.decrypt_info.key_idx = test_key_idx;
			crypto_req.connection.decrypt_info.hash_key_idx = test_key_idx;
			crypto_req.connection.decrypt_info.cipher_mode = RT_PE_CRYPTO_EALG_AES_128;
			crypto_req.connection.decrypt_info.outer_isIPv4OrIpv6 = 0;
			crypto_req.connection.decrypt_info.dmac.octet[0]=0x00; crypto_req.connection.decrypt_info.dmac.octet[1]=0x01; crypto_req.connection.decrypt_info.dmac.octet[2]=0x0f; crypto_req.connection.decrypt_info.dmac.octet[3]=0x03; crypto_req.connection.decrypt_info.dmac.octet[4]=0x03; crypto_req.connection.decrypt_info.dmac.octet[5]=0x03;
			crypto_req.connection.decrypt_info.smac.octet[0]=0x00; crypto_req.connection.decrypt_info.smac.octet[1]=0x01; crypto_req.connection.decrypt_info.smac.octet[2]=0x0f; crypto_req.connection.decrypt_info.smac.octet[3]=0x02; crypto_req.connection.decrypt_info.smac.octet[4]=0x02; crypto_req.connection.decrypt_info.smac.octet[5]=0x02;
			crypto_req.connection.decrypt_info.hwlookup_swId = 1 + RTK_FC_IPSEC_SWID_BASE;
			crypto_req.connection.decrypt_info.hwlookup_lspid = RT_PE_IPSEC_DMA_LSO_DECRYPT_HWLOOKUP_LSPID;
			break;
		case RT_PE_CRYPTO_ENGINE_CMD_DECRYPT_CONNECTION_DEL:
			rtlglue_printf("[PE Crypto] Del decrypt connection %u\n", test_conn_idx);
			crypto_req.connection_idx = test_conn_idx;
			break;
		case RT_PE_CRYPTO_ENGINE_CMD_STATUS_GET:
			rtlglue_printf("[PE Crypto] Get Status\n");
			break;	
		default:
			rtlglue_printf("\033[1;33;41m[WARNING] Unknown req_cmd %d \033[0m\n", crypto_req.req_cmd);
			return count;
	}
	pe_ret = rtk_fc_pe_crypto_test(crypto_req, fc_saInfo_idx, 1);
	if(crypto_req.req_cmd == RT_PE_CRYPTO_ENGINE_CMD_STATUS_GET)
	{
		rtlglue_printf("PE crypto engine status: ");
		if(pe_ret==RT_PE_RET_FAIL)
			rtlglue_printf("\033[1;33;41mPE crypto engine does not boot up!!\033[0m\n");
		else if(pe_ret==RT_PE_RET_EXISTED)
			rtlglue_printf("\033[1;33;40mEnable\033[0m\n");
		else if(pe_ret==RT_PE_RET_NOT_FOUND)
			rtlglue_printf("\033[1;33;40mDisable\033[0m\n");
		else
			rtlglue_printf("\033[1;33;41mUnknown\033[0m\n");
	}
	else if(pe_ret != RT_PE_RET_OK)
		rtlglue_printf("\033[1;33;41m[WARNING] pe_ret=%d \033[0m\n", pe_ret);
#endif
	return count;
}

int rtk_fc_proc_pe_srv6_test_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
#if defined(CONFIG_RTK_FC_SRV6_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
	rt_pe_srv6_request_t srv6_req;
	rt_pe_ret_t pe_ret;
	uint32 test_conn_idx=3;

	memset(&srv6_req, 0, sizeof(rt_pe_srv6_request_t));
	srv6_req.hwlookup_lspid = RTK_FC_SRV6_HWLOOKUP_LSPID;
	srv6_req.req_cmd = _rtk_fc_proc_parsing_string_to_integer(buff, count);
	rtlglue_printf("\n");
	switch(srv6_req.req_cmd)
	{
		case RT_PE_SRV6_OFFLOAD_CMD_ENABLE:
			rtlglue_printf("[PE SRv6] Enable\n");
			break;
		case RT_PE_SRV6_OFFLOAD_CMD_DISABLE:
			rtlglue_printf("[PE SRv6] Disable\n");
			break;
		case RT_PE_SRV6_OFFLOAD_CMD_ENCAP_CONNECTION_ADD:
			rtlglue_printf("[PE SRv6] Add encap connection %u\n", test_conn_idx);
			break;
		case RT_PE_SRV6_OFFLOAD_CMD_ENCAP_CONNECTION_DEL:
			rtlglue_printf("[PE SRv6] Del encap connection %u\n", test_conn_idx);
			break;
		case RT_PE_SRV6_OFFLOAD_CMD_DECAP_CONNECTION_ADD:
			rtlglue_printf("[PE SRv6] Add decap connection %u\n", test_conn_idx);
			break;
		case RT_PE_SRV6_OFFLOAD_CMD_DECAP_CONNECTION_DEL:
			rtlglue_printf("[PE SRv6] Del decap connection %u\n", test_conn_idx);
			break;
		case RT_PE_SRV6_OFFLOAD_CMD_STATUS_GET:
			rtlglue_printf("[PE SRv6] Get Status\n");
			break;
		default:
			rtlglue_printf("\033[1;33;41m[WARNING] Unknown req_cmd %d \033[0m\n", srv6_req.req_cmd);
			return count;
	}
	pe_ret = rtk_fc_pe_srv6_test(srv6_req, 1);
	if(srv6_req.req_cmd == RT_PE_SRV6_OFFLOAD_CMD_STATUS_GET)
	{
		rtlglue_printf("PE SRv6 status: ");
		if(pe_ret == RT_PE_RET_FAIL)
			rtlglue_printf("\033[1;33;41mPE SRv6 does not boot up!!\033[0m\n");
		else if(pe_ret == RT_PE_RET_EXISTED)
			rtlglue_printf("\033[1;33;40mEnable\033[0m\n");
		else if(pe_ret == RT_PE_RET_NOT_FOUND)
			rtlglue_printf("\033[1;33;40mDisable\033[0m\n");
		else
			rtlglue_printf("\033[1;33;41mUnknown\033[0m\n");
	}
	else if(pe_ret != RT_PE_RET_OK)
		rtlglue_printf("\033[1;33;41m[WARNING] pe_ret=%d \033[0m\n", pe_ret);
#endif
	return count;
}

#if defined(CONFIG_RTK_FC_CRYPTO_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
extern int rtk_fc_crypto_set_src_desc(unsigned int first_dw, unsigned int second_dw);
extern int rtk_fc_crypto_set_dst_desc(unsigned int first_dw, unsigned int second_dw, unsigned char start, unsigned char end);
extern int rtk_fc_crypto_ps_pop_done(void);
extern int rtk_fc_crypto_send_desc(void);
extern int rtk_fc_crypto_wait_put_desc_done(void);
extern int rtk_fc_crypto_register_hw_crypto_ready_cnt_callback(rtk_fc_crypto_hw_crypto_ready_cnt_callback cb_func);
#endif

#if defined(CONFIG_RTK_FC_PKT_QUEUE_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
void rtk_fc_proc_pe_offload_pkt_queue_usage(void)
{	
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("    echo [lan portmask] [pon stream id] > /proc/fc/ctrl/pe_offload_pkt_queue_portmask \n");
	
	rtlglue_printf("   Example: \n");
	rtlglue_printf("   - For upstream, from port 2 to wan with pon stream id 1 and redirect PE pakcet queue \n");
	rtlglue_printf("     echo 0x4 1 > /proc/fc/ctrl/pe_offload_pkt_queue_portmask \n");
	rtlglue_printf("   - For downstream, from wan to port 3 and redirect PE pakcet queue \n");
	rtlglue_printf("     echo 0x8 0 > /proc/fc/ctrl/pe_offload_pkt_queue_portmask \n");
	rtlglue_printf("   - Disable this function \n");
	rtlglue_printf("     echo 0 0 > /proc/fc/ctrl/pe_offload_pkt_queue_portmask \n");

	return;
}

int rtk_fc_proc_pe_offload_pkt_queue_get(struct seq_file *s, void *v)
{
	uint32 i, j;
	rt_pe_queue_test_request_t queue_test_req;
	rt_pe_ret_t pe_ret;
	
	if(fc_db.controlFuc.pe_offload_pkt_queue_portmask==0)
		PROC_PRINTF("[Disable] PE offload packet queue \n");
	else
	{		
		PROC_PRINTF("[Enable] PE offload upstream packet queue if packet's src port is in portmask 0x%x and to pon stream id %u\n", fc_db.controlFuc.pe_offload_pkt_queue_portmask, fc_db.controlFuc.pe_offload_pkt_queue_pon_streamId);
		if(fc_db.pe_queue_tc_mode)
		{
			for(i=0; i<fc_db.pe_tc_queue_pe_used_num; i++)
			{
				PROC_PRINTF("======= [PE TC QUEUE %u] ======= \n", i);
				for(j=0; j<MAX_PE_TC_QUEUE_TEST_CONNECTION_NUM; j++)
				{
					if(fc_db.pe_tc_queue_conn_data[i][j].flow_idx != FAIL)
					{
						PROC_PRINTF("  [idx %u] flow_idx %u cls_idx 0x%x ipv4_en %u ipv6_en %u tcp_en %u udp_en %u \n", 
									j, fc_db.pe_tc_queue_conn_data[i][j].flow_idx,
									fc_db.pe_tc_queue_conn_data[i][j].cls_idx,
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.ipv4_en,
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.ipv6_en,
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.tcp_en,
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.udp_en);
						PROC_PRINTF("          lspid 0x%x ldpid 0x%x cos 0x%x flowid 0x%x pol_grp_id 0x%x fe_bypass %u\n",
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.lspid,
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.ldpid,
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.cos,
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.flowid,
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.pol_grp_id,
									fc_db.pe_tc_queue_conn_data[i][j].conn_info.fe_bypass);
					}
				}
			}
		}
		fc_db.pe_tc_queue_ldma_delay_us = 0; //don't care
		memset(&queue_test_req, 0, sizeof(rt_pe_queue_test_request_t));
		queue_test_req.req_cmd = RT_PE_QUEUE_TEST_CMD_STATUS_GET;
		pe_ret = rtk_fc_pe_queue_test(queue_test_req);
		if(pe_ret != RT_PE_RET_OK)
			PROC_PRINTF("\033[1;33;41m[WARNING] PE queue engine does not boot up!!\033[0m\n");
	}
	return SUCCESS;
}

int rtk_fc_proc_pe_offload_pkt_queue_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	rt_pe_queue_test_request_t queue_test_req;
	rt_pe_ret_t pe_ret;
	uint32 portmask = 0, ori_portmask = fc_db.controlFuc.pe_offload_pkt_queue_portmask;
	uint32 pon_streamId = 0;

	if (buff)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr = tmpBuf;
		
		if ((split_str = strsep(&strptr," ")) == NULL)
			goto SHOW_USAGE;
		portmask = simple_strtol(split_str, NULL, 0);
		if(portmask & (~MAX_PE_QUEUE_TEST_LAN_PORT_MASK))
		{
			rtlglue_printf("\033[1;33;41m[WARNING] portmask 0x%x is out of range 0 ~ 0x%x \033[0m\n", portmask, MAX_PE_QUEUE_TEST_LAN_PORT_MASK);
			goto SHOW_USAGE;
		}

		if ((split_str = strsep(&strptr," ")) == NULL)
			goto SHOW_USAGE;
		pon_streamId = simple_strtol(split_str, NULL, 0);
		if(pon_streamId >= RTK_FC_TABLESIZE_STREAMID)
		{
			rtlglue_printf("\033[1;33;41m[WARNING] pon_streamId %d is out of range 0 ~ %d \033[0m\n", pon_streamId, RTK_FC_TABLESIZE_STREAMID-1);
			goto SHOW_USAGE;
		}

		if(portmask != ori_portmask)
		{	
			if(portmask==0)	//Disable
			{
				fc_db.controlFuc.pe_offload_pkt_queue_portmask = 0;
				_rtk_fc_pe_queue_test_free_resource();
				//delete flow that forwards to PE
				if(ori_portmask)
				{
					rtk_fc_flow_delete_by_lspidMask_ldpidMask(ori_portmask, fc_db.wanPortMask.portmask, TRUE);
				}
				
				memset(&queue_test_req, 0, sizeof(rt_pe_queue_test_request_t));
				queue_test_req.req_cmd = RT_PE_QUEUE_TEST_CMD_DISABLE;
				pe_ret = rtk_fc_pe_queue_test(queue_test_req);
				if(pe_ret == RT_PE_RET_NOT_FOUND)
					rtlglue_printf("\033[1;33;41m[WARNING] PE QUEUE is not existed!\033[0m\n");
			
				rtlglue_printf("\033[1;33;40mSuccess to disable PE QUEUE offlaod!\033[0m\n");
			}
			else			//Enable
			{	
				memset(&queue_test_req, 0, sizeof(rt_pe_queue_test_request_t));
				queue_test_req.req_cmd = RT_PE_QUEUE_TEST_CMD_ENABLE;
				queue_test_req.streamId.sid = FAIL;
				if(fc_db.pe_queue_tc_mode==0 && 0<=pon_streamId && pon_streamId<RTK_FC_TABLESIZE_STREAMID)
				{
					if(fc_db.streamidTbl[pon_streamId].valid==0)
					{
						rtlglue_printf("\033[1;33;41m[WARNING] Fail to enable PE QUEUE offlaod, pon_streamId %d is invalid !! \033[0m\n", pon_streamId);
						goto SHOW_USAGE;
					}
					queue_test_req.streamId.sid 	= pon_streamId;
					queue_test_req.streamId.ldpid 	= fc_db.streamidTbl[pon_streamId].ldpid;
					queue_test_req.streamId.cos 	= fc_db.streamidTbl[pon_streamId].cos;
					queue_test_req.streamId.flowid 	= fc_db.streamidTbl[pon_streamId].gemid;
				}
				pe_ret = rtk_fc_pe_queue_test(queue_test_req);
				if(pe_ret!=RT_PE_RET_OK && pe_ret!=RT_PE_RET_EXISTED)
				{
					rtlglue_printf("\033[1;33;41m[WARNING] Fail to enable PE QUEUE offlaod, pe_ret=%d \033[0m\n", pe_ret);
					fc_db.controlFuc.pe_offload_pkt_queue_portmask = 0;
					_rtk_fc_pe_queue_test_free_resource();
					_rtk_fc_pe_queue_test_free_page();
					//delete flow that forwards to PE
					if(ori_portmask)
					{
						rtk_fc_flow_delete_by_lspidMask_ldpidMask(ori_portmask, fc_db.wanPortMask.portmask, TRUE);
					}
				}
				else
				{		
					rtlglue_printf("\033[1;33;40mSuccess to enable PE QUEUE offlaod!\033[0m\n");
					fc_db.controlFuc.pe_offload_pkt_queue_portmask = portmask;
					fc_db.controlFuc.pe_offload_pkt_queue_pon_streamId = pon_streamId;
					_rtk_fc_pe_queue_test_alloc_resource();
					//delete flows that forwards to PE
					if(ori_portmask)
						rtk_fc_flow_delete_by_lspidMask_ldpidMask(ori_portmask, fc_db.wanPortMask.portmask, TRUE);
					//delete flows that forwards to port pe_offload_pkt_queue_portmask
					rtk_fc_flow_delete_by_lspidMask_ldpidMask(portmask, fc_db.wanPortMask.portmask, TRUE);
				}
			}
		}
		rtk_fc_proc_pe_offload_pkt_queue_get(NULL, NULL);
		return count;
	}
	
SHOW_USAGE:
	rtk_fc_proc_pe_offload_pkt_queue_usage();
	return count;	
}

int rtk_fc_proc_pe_offload_pkt_queue_downstream_tx_portmask_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("pe_offload_pkt_queue_downstream_tx_portmask 0x%x \n", fc_db.controlFuc.pe_offload_pkt_queue_downstream_tx_portmask);

	return SUCCESS;
}

int rtk_fc_proc_pe_offload_pkt_queue_downstream_tx_portmask_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	uint32 portmask = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(fc_db.controlFuc.pe_offload_pkt_queue_portmask == 0x0)
		fc_db.controlFuc.pe_offload_pkt_queue_downstream_tx_portmask = portmask & MAX_PE_QUEUE_TEST_LAN_PORT_MASK;
	else
		WARNING("[Fail] Please disable pe_offload_pkt_queue_portmask first !!");

	rtk_fc_proc_pe_offload_pkt_queue_downstream_tx_portmask_get(NULL, NULL);

	return count;
}

int rtk_fc_proc_pe_offload_pkt_queue_memory_cost_Mbytes_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("pe_queue_memory_cost_Mbytes %u (unit Mbytes), pe_queue_expected_ring_size %u\n", fc_db.pe_queue_memory_cost_Mbytes, fc_db.pe_queue_expected_ring_size);
	if(fc_db.pe_queue_tc_mode==0)
		PROC_PRINTF("pkt_buf_len %u \n", fc_db.pe_queue_ni_pkt_buf_len);

	return SUCCESS;
}

int rtk_fc_proc_pe_offload_pkt_queue_memory_cost_Mbytes_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);
	uint32 min_cost, max_cost;

	min_cost = (fc_db.pe_queue_tc_mode==0) ? MIN_PE_QUEUE_TEST_MBYTE_COST : MIN_PE_TC_QUEUE_TEST_MBYTE_COST ;
	max_cost = (fc_db.pe_queue_tc_mode==0) ? MAX_PE_QUEUE_TEST_MBYTE_COST : MAX_PE_TC_QUEUE_TEST_MBYTE_COST ;
	if(min_cost<=val && val<=max_cost)
	{
		fc_db.pe_queue_memory_cost_Mbytes = val;
		_rtk_fc_pe_queue_test_update_expected_ring_size();
	}
	else
	{
		rtlglue_printf("\033[1;33;41m[WARNING][tc_mode %u] Value is out of range %d ~ %d \033[0m\n", fc_db.pe_queue_tc_mode, min_cost, max_cost);
		rtlglue_printf("\nUsage:\n");
		rtlglue_printf("    echo [cost] > /proc/fc/ctrl/pe_offload_pkt_queue_memory_cost_Mbytes \n");
		rtlglue_printf("    cost: unit is Mbytes \n");
	}
	
	rtk_fc_proc_pe_offload_pkt_queue_memory_cost_Mbytes_get(NULL, NULL);
	return count;
}

int rtk_fc_proc_pe_offload_pkt_queue_tc_mode_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("pe_queue_tc_mode %u\n", fc_db.pe_queue_tc_mode);

	return SUCCESS;
}

int rtk_fc_proc_pe_offload_pkt_queue_tc_mode_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(0<=val && val<=1)
	{
		if(fc_db.pe_queue_tc_mode != val)
		{
			if(fc_db.controlFuc.pe_offload_pkt_queue_portmask)
			{
				rtlglue_printf("\033[1;33;41m[WARNING] Fail to change tc_mode ... please disable pe queue function first !! \033[0m\n");
			}
			else
			{
				fc_db.pe_queue_tc_mode = val;
				fc_db.pe_queue_memory_cost_Mbytes = (fc_db.pe_queue_tc_mode==0) ? DEF_PE_QUEUE_TEST_MBYTE_COST : DEF_PE_TC_QUEUE_TEST_MBYTE_COST ;
				_rtk_fc_pe_queue_test_update_expected_ring_size();
			}
		}
	}
	else
	{
		rtlglue_printf("\033[1;33;41m[WARNING] Value is out of range 0 ~ 1 \033[0m\n");
		rtlglue_printf("\nUsage:\n");
		rtlglue_printf("    echo [Value] > /proc/fc/ctrl/pe_offload_pkt_queue_tc_mode \n");
		rtlglue_printf("    Value: 0 for disabled, 1 for enabled \n");
	}

	rtk_fc_proc_pe_offload_pkt_queue_tc_mode_get(NULL, NULL);

	return count;
}

int rtk_fc_proc_pe_offload_pkt_queue_tc_mode_slave_portmask_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("pe_queue_tc_mode_slave_portmask 0x%x \n", fc_db.pe_queue_tc_mode_slave_portmask);

	return SUCCESS;
}

int rtk_fc_proc_pe_offload_pkt_queue_tc_mode_slave_portmask_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	uint32 portmask = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(fc_db.controlFuc.pe_offload_pkt_queue_portmask == 0x0)
		fc_db.pe_queue_tc_mode_slave_portmask = portmask & MAX_PE_QUEUE_TEST_LAN_PORT_MASK;
	else
		WARNING("[Fail] Please disable pe_offload_pkt_queue_portmask first !!");

	rtk_fc_proc_pe_offload_pkt_queue_tc_mode_slave_portmask_get(NULL, NULL);

	return count;
}

int rtk_fc_proc_pe_offload_pkt_queue_tc_mode_only_for_small_pkt_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("pe_queue_tc_mode_only_for_small_pkt %u\n", fc_db.pe_queue_tc_mode_only_for_small_pkt);

	return SUCCESS;
}

int rtk_fc_proc_pe_offload_pkt_queue_tc_mode_only_for_small_pkt_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(0<=val && val<=1)
	{
		if(fc_db.pe_queue_tc_mode_only_for_small_pkt != val)
		{
			if(fc_db.controlFuc.pe_offload_pkt_queue_portmask)
			{
				rtlglue_printf("\033[1;33;41m[WARNING] Fail to change this value ... please disable pe queue function first !! \033[0m\n");
			}
			else
			{
				fc_db.pe_queue_tc_mode_only_for_small_pkt = val;
			}
		}
	}
	else
	{
		rtlglue_printf("\033[1;33;41m[WARNING] Value is out of range 0 ~ 1 \033[0m\n");
		rtlglue_printf("\nUsage:\n");
		rtlglue_printf("    echo [Value] > /proc/fc/ctrl/pe_offload_pkt_queue_tc_mode_only_for_small_pkt \n");
		rtlglue_printf("    Value: 0 for disabled, 1 for enabled \n");
	}

	rtk_fc_proc_pe_offload_pkt_queue_tc_mode_only_for_small_pkt_get(NULL, NULL);

	return count;
}

int rtk_fc_proc_pe_offload_pkt_queue_tc_mode_ldma_delay_us_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);
	rt_pe_queue_test_request_t queue_test_req;
	rt_pe_ret_t pe_ret;

	if(val==-1)
	{
		int32 i, j;
		for(i=0; i<MAX_PE_TC_QUEUE_TEST_PE_USED_NUM; i++)
		{
			rtlglue_printf("======= [PE TC QUEUE %u] ======= \n", i);
			for(j=0; j<MAX_PE_TC_QUEUE_TEST_TC_SIGNATURE_PAGE_NUM; j++)
			{
				if(fc_db.pe_tc_queue_pageInfo_signature_buf[i][j].pPages==NULL)
					break;
				rtlglue_printf("  [idx %u] phy_addr 0x%x order %u \n", j, (uint32)__pa(page_address(fc_db.pe_tc_queue_pageInfo_signature_buf[i][j].pPages)), fc_db.pe_tc_queue_pageInfo_signature_buf[i][j].order);
			}
		}
	}
	else
	{
		fc_db.pe_tc_queue_ldma_delay_us = val;
		memset(&queue_test_req, 0, sizeof(rt_pe_queue_test_request_t));
		queue_test_req.req_cmd = RT_PE_QUEUE_TEST_CMD_STATUS_GET;
		pe_ret = rtk_fc_pe_queue_test(queue_test_req);
		if(pe_ret != RT_PE_RET_OK)
			rtlglue_printf("\033[1;33;41m[WARNING] PE queue engine does not boot up!!\033[0m\n");
	}
	return count;
}
#endif

#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
#if defined(CONFIG_REALTEK_IPC2RCPU)
#define PRE_ALLOC 1
extern int rtk_fc_internal_pe_de_amsdu_start(unsigned char preAlloc);
extern int rtk_fc_internal_pe_de_amsdu_end(void);
extern int rtk_fc_internal_pe_de_amsdu_debug(void);
#endif

int rtk_fc_pe_debug_mode_en_get(struct seq_file *s, void *v)
{
	uint8 cls_lspid, voq_id, voq_ldpid, voq_cos;
	uint32 i;
	PROC_PRINTF("amsdu_pe_dbg_rx_pmsk: 0x%x\n", fc_db.controlFuc.amsdu_pe_dbg_mode_rx_portmask);
	PROC_PRINTF("amsdu_pe_dbg_tx_pmsk: 0x%x\n", fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask);
	PROC_PRINTF("amsdu_pe_dbg_tx_tcont_id_msk: 0x%x\t([0]: 0x21, [1]: 0x22, ..., [31]: 0x3F)\n", fc_db.controlFuc.amsdu_pe_dbg_mode_tx_tcontIdmask);

	if(fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask)
	{
		PROC_PRINTF("\n");
		PROC_PRINTF("[AMSDU DEBUG MODE]: ON, here shows the info\n");
		if(fc_db.controlFuc.amsdu_pe_dbg_info_tbl)
		{
			PROC_PRINTF("amsdu_pe_dbg_info_tbl:\n");
			for(i = 0 ; i < AMSDU_PE_DBG_MODE_INFO_SIZE ; i++)
			{
				if(fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].valid)
				{
					if(_rtk_fc_amsdu_pe_dbg_mode_amsdu_pe_dbg_mode_lspid_get(fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_port, &cls_lspid))
					{
						PROC_PRINTF("[ERROR] lspid get failed, (port 0x%x, cos %u, pol_id %u)\n", fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_port, fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_cos, fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_pol_id);
						cls_lspid = 0;
					}
					if(_rtk_fc_amsdu_pe_dbg_mode_amsdu_pe_dbg_mode_voq_id_get(fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_port, fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_cos, fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_pol_id, &voq_id))
					{
						PROC_PRINTF("[ERROR] voq_id get failed (port 0x%x, cos %u, pol_id %u)\n", fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_port, fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_cos, fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_pol_id);
						voq_id = 0;
						voq_ldpid = 0;
						voq_cos = 0;
					}
					else
					{
						voq_ldpid = fc_db.wifi_amsdu_pe_offload_voq_tbl[voq_id].ldpid;
						voq_cos = fc_db.wifi_amsdu_pe_offload_voq_tbl[voq_id].cos;
					}

					PROC_PRINTF("FWD_ID[%-2d] egress_port 0x%02x, cos %u, pol_id %-3u, cls_lspid 0x%2x, amsdu_voq_id %-2u (ldpid 0x%2x, cos %u)\n",
						i, fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_port, fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_cos, fc_db.controlFuc.amsdu_pe_dbg_info_tbl[i].egress_pol_id,
						cls_lspid, voq_id, voq_ldpid, voq_cos);
				}
			}
		}
		else
			PROC_PRINTF("amsdu_pe_dbg_info_tbl: NULL\n");

		PROC_PRINTF("\n");
		PROC_PRINTF("amsdu_pe_dbg_mode_lspid2ldpid:\n");
		for(i = 0 ; i < AMSDU_PE_DBG_MODE_LDPID2LSPID_CNT ; i++)
		{
			if(fc_db.controlFuc.amsdu_pe_dbg_mode_lspid2ldpid[i].valid)
				PROC_PRINTF("[%d] egress_port 0x%02x, cls_lspid 0x%2x\n",
					i, fc_db.controlFuc.amsdu_pe_dbg_mode_lspid2ldpid[i].egress_port, fc_db.controlFuc.amsdu_pe_dbg_mode_lspid2ldpid[i].cls_lspid);
		}
	}
	return SUCCESS;
}

bool _rtk_fc_amsdu_pe_debug_mode_support_check(void)
{
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
	rtlglue_printf("Do not support AMSDU PE debug mode when CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB is enabled.\n");
	return false;
#elif defined(CONFIG_RTK_FC_WFO_INTEGRATE_QOS)
	rtlglue_printf("Do not support AMSDU PE debug mode when CONFIG_RTK_FC_WFO_INTEGRATE_QOS is enabled.\n");
	return false;
#else
	return true;
#endif
#else
	rtlglue_printf("Do not support AMSDU PE debug mode.\n");
	return false;
#endif
}

void rtk_fc_pe_debug_mode_set_usage(void)
{
	rtlglue_printf("1. enable pe_debug_mode:\n");
	rtlglue_printf("   echo 1 > /proc/fc/ctrl/pe_debug_mode_en\n");
	rtlglue_printf("2. disable pe_debug_mode:\n");
	rtlglue_printf("   echo 0 > /proc/fc/ctrl/pe_debug_mode_en\n");
	rtlglue_printf("3. change amsdu_pe_dbg_rx_pmsk:\n");
	rtlglue_printf("   echo amsdu_pe_dbg_rx_pmsk VAL > /proc/fc/ctrl/pe_debug_mode_en\n");
	rtlglue_printf("4. change amsdu_pe_dbg_tx_pmsk:\n");
	rtlglue_printf("   echo amsdu_pe_dbg_tx_pmsk VAL > /proc/fc/ctrl/pe_debug_mode_en\n");
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	rtlglue_printf("5. change amsdu_pe_dbg_tx_tcont_id_msk:\n");
	rtlglue_printf("   echo amsdu_pe_dbg_tx_tcont_id_msk VAL > /proc/fc/ctrl/pe_debug_mode_en\n");
#endif
	return ;
}

int rtk_fc_pe_debug_mode_en_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int length = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str, *cmd_str;
	int val;

	if (buff)
	{
		//fc_db.debug_level=simple_strtoul(tmpBuf, NULL, 16);
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, length);

		strptr=tmpBuf;

		cmd_str=strsep(&strptr," ");
		if(cmd_str==NULL){
			rtk_fc_pe_debug_mode_set_usage();
			return -EFAULT;
		}

		if(!strcasecmp(cmd_str, "1")) {
			rtlglue_printf("enable pe_debug_mode\n");
#if defined(CONFIG_REALTEK_IPC2RCPU)
			rtk_fc_internal_pe_de_amsdu_start(PRE_ALLOC);
#endif
		} 
		else if(!strcasecmp(cmd_str, "0")) {
			rtlglue_printf("disable pe_debug_mode\n");
#if defined(CONFIG_REALTEK_IPC2RCPU)
			rtk_fc_internal_pe_de_amsdu_end();
#endif
		}
		else
		{
			split_str=strsep(&strptr," ");
			if(split_str==NULL){
				rtk_fc_pe_debug_mode_set_usage();
				return -EFAULT;
			}
			val = simple_strtol(split_str, NULL, 0);
			if(!strcasecmp(cmd_str, "amsdu_pe_dbg_rx_pmsk"))
			{
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
				if(_rtk_fc_amsdu_pe_debug_mode_support_check() && (val != fc_db.controlFuc.amsdu_pe_dbg_mode_rx_portmask))
				{
					fc_db.controlFuc.amsdu_pe_dbg_mode_rx_portmask = val;
					rtk_fc_flow_flush();
					rtk_fc_internal_wifi_amsdu_pe_offload_mac_id_flow_flush(); // multicast WFO flow delete
				}
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();
			}
			else if(strcasecmp(cmd_str, "amsdu_pe_dbg_tx_pmsk")==0)
			{
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
				if(_rtk_fc_amsdu_pe_debug_mode_support_check() && (val != fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask))
				{
					if(val)
					{
						if(fc_db.controlFuc.amsdu_pe_dbg_info_tbl == NULL)
							fc_db.controlFuc.amsdu_pe_dbg_info_tbl = RTK_FC_HELPER_FC_KMALLOC(sizeof(rtk_fc_amsdu_pe_dbg_info_t) * AMSDU_PE_DBG_MODE_INFO_SIZE, GFP_ATOMIC);
						if(fc_db.controlFuc.amsdu_pe_dbg_info_tbl == NULL)
						{
							rtlglue_printf("Fail to allocate amsdu_pe_dbg_info_tbl. Set fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask to 0\n");
							val = 0;
						}
					}
					else
					{
						//set fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask to 0
						if(fc_db.controlFuc.amsdu_pe_dbg_info_tbl) RTK_FC_HELPER_FC_KFREE(fc_db.controlFuc.amsdu_pe_dbg_info_tbl, sizeof(rtk_fc_amsdu_pe_dbg_info_t) * AMSDU_PE_DBG_MODE_INFO_SIZE);
							fc_db.controlFuc.amsdu_pe_dbg_info_tbl = NULL;
					}
					_rtk_fc_amsdu_pe_dbg_mode_info_refresh(val, fc_db.controlFuc.amsdu_pe_dbg_mode_tx_tcontIdmask);

					fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask = val;
					rtk_fc_flow_flush();
					rtk_fc_internal_wifi_amsdu_pe_offload_mac_id_flow_flush(); // multicast WFO flow delete
				}
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();
			}
			else if(strcasecmp(cmd_str, "amsdu_pe_dbg_tx_tcont_id_msk")==0)
			{
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
				if(_rtk_fc_amsdu_pe_debug_mode_support_check() && (val != fc_db.controlFuc.amsdu_pe_dbg_mode_tx_tcontIdmask))
				{
	#if defined(CONFIG_RG_G3_WAN_PORT_INDEX)&&(CONFIG_RG_G3_WAN_PORT_INDEX==7)
					if(((1U<<0x7) & fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask) != 0x0)
					{
						_rtk_fc_amsdu_pe_dbg_mode_info_refresh(fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask, val);
					}
	#endif
					fc_db.controlFuc.amsdu_pe_dbg_mode_tx_tcontIdmask = val;
					rtk_fc_flow_flush();
					rtk_fc_internal_wifi_amsdu_pe_offload_mac_id_flow_flush(); // multicast WFO flow delete
				}
				RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
				RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();
			}
			else
			{
				rtk_fc_pe_debug_mode_set_usage();
				return -EFAULT;
			}
		}
	}

	return len;
}

#if defined(CONFIG_REALTEK_IPC2RCPU)
int rtk_fc_pe_debug_mode_pe_debug(struct seq_file *s, void *v)
{
	rtk_fc_internal_pe_de_amsdu_debug();
	return SUCCESS;
}
#endif
#endif
	


int rtk_fc_ct_hash_info_list_flush(struct file *filp, const char *buff,unsigned long len, void *data)
{
	printk("Ready to reset ct hash info\n");
	rtk_fc_ct_hash_info_flush();
	return len;
}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)

#if defined(CONFIG_RTK_FC_IPSEC_FASTFWD)

int rtk_fc_proc_ipsec_debug_get(struct seq_file *s, void *v)
{
	int val = _rtk_fc_ipsec_debug_get();
	PROC_PRINTF("IPSEC shortCut debug %s \n", val?"enable":"disable");

	return SUCCESS;
}

int rtk_fc_proc_ipsec_debug_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	
	_rtk_fc_ipsec_debug_set(val);
	

	return count;
}

int rtk_fc_proc_ipsec_en_shortCut_get(struct seq_file *s, void *v)
{
	
	PROC_PRINTF("IPSEC shortCut %s \n",fc_db.controlFuc.ipsec_en_shortCut?"enable":"disable");
	return SUCCESS;
}

int rtk_fc_proc_ipsec_en_shortCut_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(val == 1)
	{
		if(fc_db.controlFuc.ipsec_en_shortCut == 0)
		{
			fc_db.controlFuc.ipsec_en_shortCut = 1;
			__rtk_fc_ipsec_init();
		}
	}
	else if(val == 0)
	{
		if(fc_db.controlFuc.ipsec_en_shortCut == 1)
		{
			fc_db.controlFuc.ipsec_en_shortCut = 0;
			__rtk_fc_ipsec_remove();
		}
		
	}
	else
	{
		printk("echo 1: enable ; echo 0: disable\n");
	}

	return count;
}

int rtk_fc_proc_ipsec_en_pe_offload_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("IPSEC PE offload %s \n",fc_db.controlFuc.ipsec_pe_offload?"enable":"disable");

	return SUCCESS;
}

int rtk_fc_proc_ipsec_key_dump_get(struct seq_file *s, void *v)
{
	_rtk_fc_ipsec_dump_key_usage();
	return SUCCESS;
}



int rtk_fc_proc_ipsec_en_pe_offload_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
#if defined(CONFIG_RTK_FC_CRYPTO_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
	rt_pe_crypto_request_t crypto_req;
	rt_pe_ret_t pe_ret;
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(val == 1) //change from ipsec_pe_offload 0 to 1
	{
		if(fc_db.controlFuc.ipsec_pe_offload == 0)
		{
			__rtk_fc_ipsec_init_check();
			__rtk_fc_ipsec_intr_counting_mode_set(TRUE);
			
			memset(&crypto_req, 0, sizeof(rt_pe_crypto_request_t));
			crypto_req.req_cmd = RT_PE_CRYPTO_ENGINE_CMD_ENABLE;
			pe_ret = rtk_fc_pe_crypto_test(crypto_req, FAIL, 1);
			if(pe_ret != RT_PE_RET_OK)
			{
				if(pe_ret == RT_PE_RET_EXISTED)
					rtlglue_printf("\033[1;33;41m[WARNING] PE crypto engine is already existed!\033[0m\n");
				else
					rtlglue_printf("\033[1;33;41m[WARNING] Fail to enable PE IPSEC offlaod, pe_ret=%d \033[0m\n", pe_ret);
				__rtk_fc_ipsec_intr_counting_mode_set(FALSE);
			}
			else
			{		
				__rtk_fc_ipsec_pe_offload_init();
				rtlglue_printf("\033[1;33;40mSuccess to enable PE IPSEC offlaod!\033[0m\n");		
			}
		}
	}
	else if(val == 0) //change from ipsec_pe_offload 1 to 0
	{
		if(fc_db.controlFuc.ipsec_pe_offload == 1)
		{
			__rtk_fc_ipsec_pe_offload_remove();
			__rtk_fc_ipsec_intr_counting_mode_set(FALSE);
				
			memset(&crypto_req, 0, sizeof(rt_pe_crypto_request_t));
			crypto_req.req_cmd = RT_PE_CRYPTO_ENGINE_CMD_DISABLE;	
			pe_ret = rtk_fc_pe_crypto_test(crypto_req, FAIL, 1);
			if(pe_ret == RT_PE_RET_EXISTED)
				rtlglue_printf("\033[1;33;41m[WARNING] PE crypto engine is not existed!\033[0m\n");
			
			rtlglue_printf("\033[1;33;40mSuccess to disable PE IPSEC offlaod!\033[0m\n");	
		}
		
	}
	else
	{
		rtlglue_printf("echo 1: enable ; echo 0: disable\n");
	}

	rtk_fc_proc_ipsec_en_pe_offload_get(NULL, NULL);
#else
	rtlglue_printf("\033[1;33;41m[WARNING] No support!! Please check IC platform and compiler flag. \033[0m\n");
#endif
	return count;
}
#endif
#if defined(CONFIG_RTK_FC_SRV6_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
int rtk_fc_proc_pe_srv6_offload_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("SRv6 PE offload %s \n",fc_db.controlFuc.pe_srv6_offload?"enable":"disable");

	return SUCCESS;
}

int rtk_fc_proc_pe_srv6_offload_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	rt_pe_srv6_request_t srv6_req;
	rt_pe_ret_t pe_ret;

	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(val == 1) //change from ipsec_pe_offload 0 to 1
	{
		if(fc_db.controlFuc.pe_srv6_offload == 0)
		{
			memset(&srv6_req, 0, sizeof(rt_pe_srv6_request_t));
			srv6_req.hwlookup_lspid = RTK_FC_SRV6_HWLOOKUP_LSPID;
			srv6_req.req_cmd = RT_PE_SRV6_OFFLOAD_CMD_ENABLE;
			pe_ret = rtk_fc_pe_srv6_test(srv6_req, 1);
			if(pe_ret != RT_PE_RET_OK)
			{
				if(pe_ret == RT_PE_RET_EXISTED)
					rtlglue_printf("\033[1;33;41m[WARNING] PE SRv6 offload is already existed!\033[0m\n");
				else
					rtlglue_printf("\033[1;33;41m[WARNING] Fail to enable PE SRv6 offlaod, pe_ret=%d \033[0m\n", pe_ret);
			}
			else
			{
				rtlglue_printf("\033[1;33;40mSuccess to enable PE SRv6 offlaod!\033[0m\n");
				fc_db.controlFuc.pe_srv6_offload = 1;
			}
		}
	}
	else if(val == 0) //change from ipsec_pe_offload 1 to 0
	{
		if(fc_db.controlFuc.pe_srv6_offload == 1)
		{
			memset(&srv6_req, 0, sizeof(rt_pe_srv6_request_t));
			srv6_req.req_cmd = RT_PE_SRV6_OFFLOAD_CMD_DISABLE;
			pe_ret = rtk_fc_pe_srv6_test(srv6_req, 1);
			if(pe_ret != RT_PE_RET_OK)
			{
				if(pe_ret == RT_PE_RET_NOT_FOUND)
					rtlglue_printf("\033[1;33;41m[WARNING] PE SRv6 offload is not found!\033[0m\n");
				else
					rtlglue_printf("\033[1;33;41m[WARNING] Fail to disable PE SRv6 offlaod, pe_ret=%d \033[0m\n", pe_ret);
			}
			else
			{
				rtlglue_printf("\033[1;33;40mSuccess to disable PE SRv6 offlaod!\033[0m\n");
				fc_db.controlFuc.pe_srv6_offload = 0;
			}
		}
	}
	else if(val == 6) //get status
	{
		if(fc_db.controlFuc.pe_srv6_offload == 1)
		{
			memset(&srv6_req, 0, sizeof(rt_pe_srv6_request_t));
			srv6_req.req_cmd = RT_PE_SRV6_OFFLOAD_CMD_STATUS_GET;
			pe_ret = rtk_fc_pe_srv6_test(srv6_req, 1);
			if(pe_ret != RT_PE_RET_OK)
			{
				if(pe_ret == RT_PE_RET_NOT_FOUND)
					rtlglue_printf("\033[1;33;41m[WARNING] PE SRv6 offload is not found!\033[0m\n");
				else
					rtlglue_printf("\033[1;33;41m[WARNING] Fail to get PE SRv6 offlaod, pe_ret=%d \033[0m\n", pe_ret);
			}
		}
	}
	else
	{
		rtlglue_printf("echo 1: enable ; echo 0: disable ; echo 6: status\n");
	}

	rtk_fc_proc_pe_srv6_offload_get(NULL, NULL);

	return count;
}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int rtk_fc_proc_srv6NetIf_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	uint16 i, dev_idx;
	int8 first_invalid = -1;
	uint8 lip6[IPV6_ADDR_LEN] = {0};
	uint8 rip6[IPV6_ADDR_LEN] = {0};
	uint8 op[8] = {0};
	uint8 devName[IFNAMSIZ] = {0};

	//struct net_device *ip6tun_dev = NULL;

	if (buffer)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, CMD_BUFF_SIZE);

		if (_rtk_fc_parseGetValue(tmpBuf, devName, "DEVIFNAME", STRING) == FAILED)
		{
			printk("DEVIFNAME parser error\n");
			goto SRV6_ERR;
		}

		if (_rtk_fc_parseGetValue(tmpBuf, lip6, "LIP6", V6IP) == FAILED)
		{
			printk("LIP6 parser error\n");
			goto SRV6_ERR;
		}

		if (_rtk_fc_parseGetValue(tmpBuf, rip6, "RIP6", V6IP) == FAILED)
		{
			printk("RIP6 parser error\n");
			goto SRV6_ERR;
		}

		if (_rtk_fc_parseGetValue(tmpBuf, op, "OP", STRING) == FAILED)
		{
			printk("OP parser error\n");
			goto SRV6_ERR;
		}

		if (!memcmp(op, "add", strlen(op)))
		{
			for (i = 0 ; i < RTK_FC_DEV_GW_MAC_TBL ; i++)
			{
				if (!fc_db.devGwMacTbl[i].dev)
					continue;
				if (!memcmp(fc_db.devGwMacTbl[i].dev->name, devName, strlen(devName)))
					break;
			}
			if (i == RTK_FC_DEV_GW_MAC_TBL)
			{
				printk("can't find device %s in devGwMacTbl!!\n",devName);
				goto SRV6_ERR;
			}
			dev_idx = i;
			for (i = 0 ; i < RTK_FC_DEV_GW_MAC_TBL ; i++)
			{
				if (first_invalid < 0 && fc_db.rtk_pe_srv6_netif_info[i].sw_dev_id == 0)
				{
					first_invalid = i;
					continue;
				}
				if (!memcmp(fc_db.rtk_pe_srv6_netif_info[i].lip.ipv6_addr, lip6, IPV6_ADDR_LEN) &&
					!memcmp(fc_db.rtk_pe_srv6_netif_info[i].rip.ipv6_addr, rip6, IPV6_ADDR_LEN))
					break;
			}
			if (i == RTK_FC_DEV_GW_MAC_TBL && first_invalid != -1)
			{
				memcpy(fc_db.rtk_pe_srv6_netif_info[first_invalid].lip.ipv6_addr, lip6, IPV6_ADDR_LEN);
				memcpy(fc_db.rtk_pe_srv6_netif_info[first_invalid].rip.ipv6_addr, rip6, IPV6_ADDR_LEN);
				fc_db.rtk_pe_srv6_netif_info[first_invalid].sw_dev_id = dev_idx;
			}
		}
		else if (!memcmp(op, "del", strlen(op)))
		{
			for (i = 0 ; i < RTK_FC_DEV_GW_MAC_TBL ; i++)
			{
				if (!memcmp(fc_db.rtk_pe_srv6_netif_info[i].lip.ipv6_addr, lip6, IPV6_ADDR_LEN) &&
					!memcmp(fc_db.rtk_pe_srv6_netif_info[i].rip.ipv6_addr, rip6, IPV6_ADDR_LEN))
					break;
			}
			if (i == RTK_FC_DEV_GW_MAC_TBL)
			{
				printk("can't find device %s in devGwMacTbl!!\n",devName);
				goto SRV6_ERR;
			}
			memset(fc_db.rtk_pe_srv6_netif_info[i].lip.ipv6_addr, 0, IPV6_ADDR_LEN);
			memset(fc_db.rtk_pe_srv6_netif_info[i].rip.ipv6_addr, 0, IPV6_ADDR_LEN);
			fc_db.rtk_pe_srv6_netif_info[i].sw_dev_id = 0;
		}
		else
		{
			WARNING("wrong op:%s!!!",op);
			goto SRV6_ERR;
		}
	}

SRV6_ERR:
	return count;

}

int rtk_fc_proc_srv6NetIf_get(struct seq_file *s, void *v)
{
	int i;
	PROC_PRINTF("Usage:\n");
	PROC_PRINTF("Patten: [DEVIFNAME] [LOCALIP6] [REMOTEIP6] [OPERATION]\n");
	PROC_PRINTF("Example of addition:\n");
	PROC_PRINTF("\techo DEVIFNAME ip6tun0 LIP6 2001:0:1:2::1 RIP6 fcff:2::d2 OP add > /proc/fc/ctrl/srv6_netif\n");
	PROC_PRINTF("Example of deletion:\n");
	PROC_PRINTF("\techo DEVIFNAME ip6tun0 LIP6 2001:0:1:2::1 RIP6 fcff:2::d2 OP del > /proc/fc/ctrl/srv6_netif\n");

	PROC_PRINTF("SRV6 netif info table:\n");
	for (i = 0 ; i < RTK_FC_DEV_GW_MAC_TBL ; i++)
	{
		if (fc_db.rtk_pe_srv6_netif_info[i].sw_dev_id)
		{
			struct net_device *dev = fc_db.devGwMacTbl[fc_db.rtk_pe_srv6_netif_info[i].sw_dev_id].dev;
			PROC_PRINTF("\t[%d] lip=%pI6 rip=%pI6 devname=%s(%d)\n", i,
				fc_db.rtk_pe_srv6_netif_info[i].lip.ipv6_addr,
				fc_db.rtk_pe_srv6_netif_info[i].rip.ipv6_addr,
				dev ? dev->name : "NULL",
				fc_db.rtk_pe_srv6_netif_info[i].sw_dev_id);
		}
	}

	return SUCCESS;
}
#endif
#endif
#if defined(CONFIG_RTK_FC_HTTP_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
int rtk_fc_proc_pe_tcp_download_congestion_mode_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("pe_tcp_download_congestion_mode is %s \n", 
				(fc_db.pe_tcp_download_congestion_mode==FAIL)?"-1, decided by user api":((fc_db.pe_tcp_download_congestion_mode==0)?"0, non-congestion mode":"1, congestion mode"));

	return SUCCESS;
}

int rtk_fc_proc_pe_tcp_download_congestion_mode_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(-1<=val && val<=1)
		fc_db.pe_tcp_download_congestion_mode = val;
	else
	{
		rtlglue_printf("\033[1;33;41m[WARNING] Value is out of range -1 ~ 1 \033[0m\n");
		rtlglue_printf("\nUsage:\n");
		rtlglue_printf("    echo [mode] > /proc/fc/ctrl/pe_tcp_download_congestion_mode \n");
		rtlglue_printf("    mode: -1: decided by user api, 0: non-congestion mode, 1: congestion mode \n");
	}

	rtk_fc_proc_pe_tcp_download_congestion_mode_get(NULL, NULL);

	return count;
}

int rtk_fc_proc_pe_tcp_download_non_congestion_completely_done_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("pe_tcp_download_non_congestion_completely_done is %s \n", 
				(fc_db.pe_tcp_download_non_congestion_completely_done==FAIL)?"-1, decided by user api":((fc_db.pe_tcp_download_non_congestion_completely_done==0)?"0, normally done":"1, completely done"));

	return SUCCESS;
}

int rtk_fc_proc_pe_tcp_download_non_congestion_completely_done_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(-1<=val && val<=1)
		fc_db.pe_tcp_download_non_congestion_completely_done = val;
	else
	{
		rtlglue_printf("\033[1;33;41m[WARNING] Value is out of range -1 ~ 1 \033[0m\n");
		rtlglue_printf("\nUsage:\n");
		rtlglue_printf("    echo [mode] > /proc/fc/ctrl/pe_tcp_download_non_congestion_completely_done \n");
		rtlglue_printf("    mode: -1: decided by user api, 0: normally done, 1: completely done \n");
	}

	rtk_fc_proc_pe_tcp_download_non_congestion_completely_done_get(NULL, NULL);

	return count;
}
#endif
#endif


#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)


int rtk_fc_proc_dual_extra_cls_info_get(struct seq_file *s, void *v)
{
	int i;
	PROC_PRINTF("==================================================================\n");
	for(i = 0;i < RTK_FC_DUALTYPE_END ; i++)
	{
		if(fc_db.exra_cls_is_set[i].exra_cls_is_set)
		{
			PROC_PRINTF("[DUAL FRAG]Type %d Reserved CLS is set: %d, index; 0x%x\n", i, fc_db.exra_cls_is_set[i].exra_cls_is_set,fc_db.exra_cls_is_set[i].exra_cls_index);
		}
		
	}
	PROC_PRINTF("==================================================================\n");	
	

	return SUCCESS;
}


#endif

int rtk_fc_reset_ipv6_nat_mapping_table(struct file *filp, const char *buff,unsigned long len, void *data)
{
	rtk_fc_reset_v6NatTable();
	rtk_fc_flow_flush();

	return len;
}

int rtk_fc_reset_ipv6_mapt_mapping_table(struct file *filp, const char *buff,unsigned long len, void *data)
{
	rtk_fc_reset_v6MaptTable();
	rtk_fc_flow_flush();

	return len;
}

int rtk_fc_proc_flow_not_update_in_real_time_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.systemGlobal.flow_not_update_in_real_time);

	return SUCCESS;
}

int rtk_fc_proc_flow_not_update_in_real_time_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	fc_db.systemGlobal.flow_not_update_in_real_time = _rtk_fc_proc_parsing_string_to_integer(buff, len);
	rtk_fc_proc_flow_not_update_in_real_time_get(NULL,NULL);

	return len;
}
#if !(defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
int rtk_fc_proc_prehashptn_sport_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("0x%x\n", fc_db.systemGlobal.preHashPtn[FB_PREHASH_PTN_SPORT]);

	return SUCCESS;
}
int rtk_fc_proc_prehashptn_sport_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	uint32 ptn;

	ptn =_rtk_fc_proc_parsing_string_to_integer(buff,len);

	RTK_RG_ASIC_PREHASHPTN_SET(FB_PREHASH_PTN_SPORT, ptn);

	return len;
}
int rtk_fc_proc_prehashptn_dport_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("0x%x\n", fc_db.systemGlobal.preHashPtn[FB_PREHASH_PTN_DPORT]);

	return SUCCESS;
}
int rtk_fc_proc_prehashptn_dport_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	uint32 ptn;

	ptn =_rtk_fc_proc_parsing_string_to_integer(buff,len);

	RTK_RG_ASIC_PREHASHPTN_SET(FB_PREHASH_PTN_DPORT, ptn);

	return len;
}
int rtk_fc_proc_prehashptn_sip_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("0x%x\n", fc_db.systemGlobal.preHashPtn[FB_PREHASH_PTN_SIP]);

	return SUCCESS;
}
int rtk_fc_proc_prehashptn_sip_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	uint32 ptn;

	ptn =_rtk_fc_proc_parsing_string_to_integer(buff,len);

	RTK_RG_ASIC_PREHASHPTN_SET(FB_PREHASH_PTN_SIP, ptn);

	return len;
}
int rtk_fc_proc_prehashptn_dip_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("0x%x\n", fc_db.systemGlobal.preHashPtn[FB_PREHASH_PTN_DIP]);

	return SUCCESS;
}
int rtk_fc_proc_prehashptn_dip_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	uint32 ptn;

	ptn =_rtk_fc_proc_parsing_string_to_integer(buff,len);

	RTK_RG_ASIC_PREHASHPTN_SET(FB_PREHASH_PTN_DIP, ptn);

	return len;
}
#endif
int rtk_fc_proc_flowSessionAutoExtend_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.flow_sessionAutoExtend);

	return SUCCESS;
}
int rtk_fc_proc_flowSessionAutoExtend_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
#if 0	// not support since lots of packet format are not considered.
	int ret = 0;
	uint32 ptn;

	ptn =_rtk_fc_proc_parsing_string_to_integer(buff,len);

	fc_db.controlFuc.flow_sessionAutoExtend= ((ptn!=0)?TRUE:FALSE);

	rtk_fc_flow_flush();

	if(fc_db.controlFuc.flow_sessionAutoExtend){
		ret = rtk_fc_flow_dummyPktBuffer_alloc();
		if(ret != RTK_FC_RET_OK){
			fc_db.controlFuc.flow_sessionAutoExtend = FALSE;
			WARNING("change mode was fail");
		}else {
			if(fc_db.controlFuc.flow_skipAllPsTracking!= fc_db.controlFuc.flow_sessionAutoExtend) {
				fc_db.controlFuc.flow_skipAllPsTracking = fc_db.controlFuc.flow_sessionAutoExtend;
				WARNING("BE NOTICED: FC turn flow_skipAllPsTracking to %d automatically", fc_db.controlFuc.flow_skipAllPsTracking);		// turn on automaically
			}

		}
	}
	else
		ret = rtk_fc_flow_dummyPktBuffer_free();
#else
	
	rtlglue_printf("FC flow_sessionAutoExtend is not supported.\n");
#endif
	return len;
}

int rtk_fc_proc_flow_l2_skipPsTracking_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.l2_skipPsTracking);

	return SUCCESS;
}
int rtk_fc_proc_flow_l2_skipPsTracking_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	uint32 ptn;

	ptn =_rtk_fc_proc_parsing_string_to_integer(buff,len);

	fc_db.controlFuc.l2_skipPsTracking= ((ptn!=0)?TRUE:FALSE);

	return len;
}

int rtk_fc_proc_flowSkipAllPsTracking_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.flow_skipAllPsTracking);

	return SUCCESS;
}
int rtk_fc_proc_flowSkipAllPsTracking_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	uint32 ptn;

	ptn =_rtk_fc_proc_parsing_string_to_integer(buff,len);

	fc_db.controlFuc.flow_skipAllPsTracking = ((ptn!=0)?TRUE:FALSE);

	_rtk_fc_lut_nonStaticEntry_flush();

	return len;
}
int rtk_fc_proc_wifiSlowPathRxbyNic_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d: if wifi to nic flow miss, %s\n", fc_db.controlFuc.wifiSlowPathRxbyNic, fc_db.controlFuc.wifiSlowPathRxbyNic?"continue rx by NIC hook func":"do rx by netif_rx");

	return SUCCESS;
}
int rtk_fc_proc_wifiSlowPathRxbyNic_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	uint32 ptn;

	ptn =_rtk_fc_proc_parsing_string_to_integer(buff,len);

	fc_db.controlFuc.wifiSlowPathRxbyNic= ((ptn!=0)?TRUE:FALSE);

	return len;
}


void _rtk_fc_stag_tpid_usage(void){
	rtlglue_printf("\nUsage:\n");
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	rtlglue_printf("    echo [TPID 1] [TPID 2] [TPID 3] [TPID 4] > /proc/fc/ctrl/stag_tpid \n");
#else
	rtlglue_printf("    echo [TPID 1] [TPID 2] > /proc/fc/ctrl/stag_tpid \n");
#endif

	rtlglue_printf("   Example: \n");
	rtlglue_printf("   - set TPID_1 to 0x88A8 and disable TPID_2 \n");
	rtlglue_printf("     echo 0x88A8 > /proc/fc/ctrl/stag_tpid \n");
	rtlglue_printf("   - set TPID_1 to 0x88A8 and set TPID_2 to 0x8888 \n");
	rtlglue_printf("     echo 0x88A8 0x8888 > /proc/fc/ctrl/stag_tpid \n");
}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int _rtk_fc_stag_tpid_decision(const char *buff, unsigned long count)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	rtk_rg_asic_dmaAftTpid_t tpidConf;
	uint32_t stag_tpid_cnt = 0, top_tpid_en_mask = 0;
	int i;
	aal_l2_vlan_default_cfg_t cfg;
	aal_l2_vlan_default_cfg_mask_t mask;
	bool if_ctpid_as_stpid = false;

	bzero(&cfg,sizeof(cfg));
	bzero(&mask,sizeof(mask));

	if (buff)
	{
		char *strptr,*split_str;
		uint32 ptn;
		int vlan_register = 0;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;

		while(1)
		{
			split_str=strsep(&strptr," ");
			//printk("Now string: %s\n",split_str);
			if(split_str!=NULL)
			{
				ptn = simple_strtol(split_str, NULL, 0);
				ptn &= 0xffff;

				if(ptn!=0)
				{
					fc_db.systemGlobal.stagTPID[vlan_register] = ptn;
					vlan_register +=1;

					if(vlan_register==RTK_FC_TPID_SELECTION_MAX_NUM)
					{
						/* if stag tpid include 0x8100: support 4 stag tpid
						   else: support 3 stag tpid only*/
						for(i = 0 ; i < vlan_register ; i++)
						{
							if(fc_db.systemGlobal.stagTPID[i] == CVLAN_TPID)
							{
								if_ctpid_as_stpid = TRUE;
								break;
							}
						}
						if(if_ctpid_as_stpid)
						{
							rtlglue_printf("Set svlan tpid ptn = 0x%x to stag[%d]\n",ptn, vlan_register-1);
							rtlglue_printf("svlan tpid includes 0x%04x, Support at most 4 svlan tpid settings.\n", CVLAN_TPID);
							break;
						}
						else
						{
							rtlglue_printf("svlan tpid excludes 0x%04x, Support at most 3 svlan tpid settings.\n", CVLAN_TPID);
							vlan_register--;
							break;
						}
					}
					else
					{
						rtlglue_printf("Set svlan tpid ptn = 0x%x to stag[%d]\n",ptn, vlan_register-1);
					}
				}
				else
				{
					rtlglue_printf("[ERROR] Error setting! Skip TPID 0 to configuration");
				}
			}
			else
				break;

		}

		if(vlan_register == 0)
		{
			// no valid configuration
			_rtk_fc_stag_tpid_usage();
			return -EFAULT;
		}
		else
		{
			fc_db.systemGlobal.stagTPID_en_cnt = vlan_register;
		}

		top_tpid_en_mask = 0x1; //enable ETH_P_8021Q
		fc_db.systemGlobal.stagTPID_tx_not_sup_enc = 0;
		for(i = 0 ; i < fc_db.systemGlobal.stagTPID_en_cnt ; i++)
		{
			/*
				L3FE TPID configure:
				TPID[0] reserved for ETH_P_8021Q
				TPID[1] for stagTPID[0]
				TPID[2] for stagTPID[1]
				TPID[3] for stagTPID[2]

				DMA AFT TPID configure:
				TPID[0] is reserved for ETH_P_8021Q
				TPID[1] for stagTPID[0]
				TPID[2] for stagTPID[1]
				TPID[3] for stagTPID[2]

				Setup L2 TPID for fixing Multicast Svlan l2fe vlan default config & Fib table action
				L2_VLAN_TX_TPID[0] for stagTPID[0]
				L2_VLAN_TX_TPID[1] for stagTPID[1]
				L2_VLAN_TX_TPID[2] is reserved for ETH_P_8021Q
				L2_VLAN_TX_TPID[3] for stagTPID[2]
				L2_VLAN_CMP_TPID_SVLAN for stagTPID[0]
				L2_VLAN_CMP_TPID_CVLAN for ETH_P_8021Q
				L2_VLAN_CMP_TPID_OTHER for stagTPID[1]
				L2_VLAN_CMP_TPID_USER for  stagTPID[2]

				Note. If(stagTPID) contain CVLAN_TPID, the TPID already exists for CVID, skip it.
			*/
			if(fc_db.systemGlobal.stagTPID[i] != CVLAN_TPID)
			{
				stag_tpid_cnt++;
				top_tpid_en_mask |= (1 << stag_tpid_cnt);

				ASSERT_EQ(aal_l3fe_glb_pp_tpid_set(stag_tpid_cnt, fc_db.systemGlobal.stagTPID[i]), AAL_E_OK); //TPID[i] for stagTPID[i-1]

				// L3FE
				if(stag_tpid_cnt == 1)
				{
					// DMA AFT
					tpidConf.tpid_1			= fc_db.systemGlobal.stagTPID[i];
					// L2
					mask.s.tx_tpid0			= 1;
					cfg.tx_tpid0			= fc_db.systemGlobal.stagTPID[i];
					mask.s.cmp_tpid_svlan	= 1;
					cfg.cmp_tpid_svlan		= fc_db.systemGlobal.stagTPID[i];
				}
				else if(stag_tpid_cnt == 2)
				{
					// DMA AFT
					tpidConf.tpid_2			= fc_db.systemGlobal.stagTPID[i];
					// L2
					mask.s.tx_tpid1			= 1;
					cfg.tx_tpid1			= fc_db.systemGlobal.stagTPID[i];
					mask.s.cmp_tpid_other	= 1;
					cfg.cmp_tpid_other		= fc_db.systemGlobal.stagTPID[i];
				}
				else if(stag_tpid_cnt == 3)
				{
					// DMA AFT
					tpidConf.tpid_3			= fc_db.systemGlobal.stagTPID[i];
					// L2
					mask.s.tx_tpid3			= 1;
					cfg.tx_tpid3			= fc_db.systemGlobal.stagTPID[i];
					mask.s.cmp_tpid_user	= 1;
					cfg.cmp_tpid_user		= fc_db.systemGlobal.stagTPID[i];
					fc_db.systemGlobal.stagTPID_tx_not_sup_enc = i;
				}
			}
		}
		ASSERT_EQ(aal_l3fe_glb_pp_top_tpid_mask_set(top_tpid_en_mask), AAL_E_OK);
		rtk_rg_asic_dmaAftTpid_set(0x6, tpidConf);
		aal_l2_vlan_default_cfg_set(0, mask, &cfg);
		return count;
	}else{
		_rtk_fc_stag_tpid_usage();
	}
	return -EFAULT;
}
#else
//!(defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
int _rtk_fc_stag_tpid_decision(const char *buff, unsigned long count)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	uint32_t outerTPID[4] = {ETH_P_8021Q, 0x88a8, 0, 0};
	uint32_t innerTPID[4] = {ETH_P_8021Q, 0x88a8, 0, 0};
#if defined(CONFIG_FC_CA8277B_SERIES)
	rtk_rg_asic_dmaAftTpid_t tpidConf;
#endif
#endif
	
	if (buff)
	{
		char *strptr,*split_str;
		uint32 ptn;
		int vlan_register = 0;
		
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);
		
		strptr=tmpBuf;
		
		while(1)
		{
			split_str=strsep(&strptr," ");
			//printk("Now string: %s\n",split_str);
			if(split_str!=NULL)
			{
				ptn = simple_strtol(split_str, NULL, 0);
				ptn &= 0xffff;

				if(ptn!=0)
				{
					if(vlan_register==2)
					{
						rtlglue_printf("Support 2 Stag TPID only.");
						break;
					}
					rtlglue_printf("Set svlan tpid  ptn = 0x%x to stag[%d]\n",ptn, vlan_register);
					fc_db.systemGlobal.stagTPID[vlan_register] = ptn;

					vlan_register +=1;
				}
				else
				{
					rtlglue_printf("[ERROR] Error setting! Skip TPID 0 to configuration");
				}
			}
			else
				break;

		}

		if(vlan_register == 0)
		{
			// no valid configuration
			_rtk_fc_stag_tpid_usage();
			return -EFAULT;
		}
		else
		{
			fc_db.systemGlobal.stagTPID_en_cnt = vlan_register;
		}
		(void)rtk_svlan_tpidEnable_set(0, ENABLED);
		(void)rtk_svlan_tpidEntry_set(0, fc_db.systemGlobal.stagTPID[0]);
		(void)rtk_svlan_tpidEntry_set(1, fc_db.systemGlobal.stagTPID[1]);
		rtk_fc_gmac_svlan_tpid_enable(fc_db.systemGlobal.stagTPID[0], 0);
		rtk_fc_gmac_svlan_tpid_enable(fc_db.systemGlobal.stagTPID[1], 1);
		if(fc_db.systemGlobal.stagTPID_en_cnt == 2)
		{
			(void)rtk_svlan_tpidEnable_set(1, ENABLED);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			outerTPID[1] = fc_db.systemGlobal.stagTPID[0];
			outerTPID[2] = fc_db.systemGlobal.stagTPID[1];
			innerTPID[1] = fc_db.systemGlobal.stagTPID[0];
			innerTPID[2] = fc_db.systemGlobal.stagTPID[1];
			ASSERT_EQ(aal_l3fe_pp_tpid_set(3, outerTPID, 3, innerTPID), AAL_E_OK);

#if defined(CONFIG_FC_CA8277B_SERIES)
			tpidConf.tpid_1 = fc_db.systemGlobal.stagTPID[0];
			tpidConf.tpid_2 = fc_db.systemGlobal.stagTPID[1];
			rtk_rg_asic_dmaAftTpid_set(0x6, tpidConf);
#endif
#endif
		}
		else
		{
			//fc_db.systemGlobal.stagTPID_en_cnt = 1
			(void)rtk_svlan_tpidEnable_set(1, DISABLED);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			outerTPID[1] = fc_db.systemGlobal.stagTPID[0];
			innerTPID[1] = fc_db.systemGlobal.stagTPID[0];
			aal_l3fe_pp_tpid_set(2, outerTPID, 2, innerTPID);

#if defined(CONFIG_FC_CA8277B_SERIES)
			tpidConf.tpid_1 = fc_db.systemGlobal.stagTPID[0];
			rtk_rg_asic_dmaAftTpid_set(0x2, tpidConf);
#endif
#endif
		}
		return count;
	}else{
		_rtk_fc_stag_tpid_usage();
	}
	return -EFAULT;
}
#endif


int rtk_fc_proc_stagTPID_get(struct seq_file *s, void *v)
{
	int i;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	int max_stpid_cnt = RTK_FC_TPID_SELECTION_MAX_NUM;
#else
	int max_stpid_cnt = 2;
#endif
	PROC_PRINTF("stagTPID_en_cnt: %u\n", fc_db.systemGlobal.stagTPID_en_cnt);
	for(i = 0 ; i < max_stpid_cnt ; i++)
	{
		if(i < fc_db.systemGlobal.stagTPID_en_cnt)
			PROC_PRINTF("stagTPID[%d]: 0x%x\n", i, fc_db.systemGlobal.stagTPID[i]);
		else
			PROC_PRINTF("stagTPID[%d]: disabled\n", i);


	}

	return SUCCESS;
}
int rtk_fc_proc_stagTPID_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	 _rtk_fc_stag_tpid_decision(buff, len);
	return len;
}

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)


int rtk_fc_proc_fbmode_get(struct seq_file *s, void *v)
{
	rtk_rg_asic_fbMode_t fbmode = _rtk_rg_fbMode_get();

	PROC_PRINTF("Current FB Mode: %d\r\n", fbmode);
	PROC_PRINTF("(0: 4K SRAM, 1: 8K DRAM, 2: 16K DRAM, 3: 32K DRAM)\r\n");

	return SUCCESS;
}

//.unlockBefortWrite=1
int rtk_fc_proc_fbmode_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	int ret = SUCCESS;
	int tmp_hwnat_mode;
	int fbmode = _rtk_fc_proc_parsing_string_to_integer(buffer, count);

	if(fbmode < FB_MODE_4K || fbmode > FB_MODE_32K)
	{
		WARNING("Not supported!");
	}
	else{
		//========================= Critical Section Start =========================//
		RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
		RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
		
				
		//int driverState = atomic_read(&fc_db.trapToPs);
		tmp_hwnat_mode = fc_db.controlFuc.hwnat_mode;
		//atomic_set(&fc_db.trapToPs, 1);				// disable learning
		fc_db.controlFuc.hwnat_mode = RT_FLOW_HWNAT_MODE_DIS_ACC;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(RT_FLOW_HWNAT_MODE_DIS_ACC);
		assert_ok(rtk_fc_flow_flush_all());

		ret = _rtk_rg_fbMode_set(fbmode);
		ret = _rtk_fc_flowDataBase_init();

		rtk_fc_tableReset();

		//atomic_set(&fc_db.trapToPs, driverState);		// recover to original state
		fc_db.controlFuc.hwnat_mode = tmp_hwnat_mode;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(tmp_hwnat_mode);


		RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
		RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();
		//========================= Critical Section End =========================//

		// might sleep
		RTK_FC_HOOK_PS_CT_FLUSH();
	}


	//rtlglue_printf("Current FB Mode: %d\r\n", _rtk_rg_fbMode_get());
	//rtlglue_printf("(0: 4K SRAM, 1: 8K DRAM, 2: 16K DRAM, 3: 32K DRAM)\r\n");

	return count;
}

int rtk_fc_proc_prevent_control_packet_drop_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.prevent_control_packet_drop);

	return SUCCESS;
}
int rtk_fc_proc_prevent_control_packet_drop_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	uint32 status;
	rtk_port_macAbility_t cpuAbility_main;
	rtk_port_macAbility_t cpuAbility_last;

	status = (_rtk_fc_proc_parsing_string_to_integer(buff,len) !=0)?TRUE:FALSE;
	if(status != fc_db.controlFuc.prevent_control_packet_drop)
	{
		fc_db.controlFuc.prevent_control_packet_drop = status;

		if(RTK_PORT_MACFORCEABILITY_GET(RTK_FC_MAC_PORT_MAINCPU,&cpuAbility_main))WARNING("Change CPU port flow control failed!!!\n");
		if(RTK_PORT_MACFORCEABILITY_GET(RTK_FC_MAC_PORT_LASTCPU,&cpuAbility_last))WARNING("Change CPU port flow control failed!!!\n");
		if(fc_db.controlFuc.prevent_control_packet_drop == ENABLE)
		{

			cpuAbility_main.txFc=ENABLED;
			cpuAbility_main.rxFc=ENABLED;
			cpuAbility_last.txFc=ENABLED;
			cpuAbility_last.rxFc=ENABLED;

			re8686_set_flow_control(0, 1, ENABLE);

			rtk_rg_asic_unmatchedCpuPriority_set(RTK_FC_UNMATCH_FLOW_TRAP_PRI);

			re8686_set_pauseBySw(ENABLE);
		}
		else
		{
			cpuAbility_main.txFc=DISABLED;
			cpuAbility_main.rxFc=DISABLED;
			cpuAbility_last.txFc=DISABLED;
			cpuAbility_last.rxFc=DISABLED;

			re8686_set_flow_control(0, 1, DISABLE);

			rtk_rg_asic_unmatchedCpuPriority_set(RTK_FC_UNMATCH_FLOW_TRAP_PRI_DEFAULT);

			re8686_set_pauseBySw(DISABLE);
		}
		if(RTK_PORT_MACFORCEABILITY_SET(RTK_FC_MAC_PORT_MAINCPU,cpuAbility_main))WARNING("Change CPU port flow control failed!!!\n");
		if(RTK_PORT_MACFORCEABILITYSTATE_SET(RTK_FC_MAC_PORT_MAINCPU,ENABLED))WARNING("Change CPU port flow control failed!!!\n");
		if(RTK_PORT_MACFORCEABILITY_SET(RTK_FC_MAC_PORT_LASTCPU,cpuAbility_last))WARNING("Change CPU port flow control failed!!!\n");
		if(RTK_PORT_MACFORCEABILITYSTATE_SET(RTK_FC_MAC_PORT_LASTCPU,ENABLED))WARNING("Change CPU port flow control failed!!!\n");

	}

	return len;
}
int rtk_fc_proc_wifi_flow_ctrl_threshold_get(struct seq_file *s, void *v)
{
	rtk_fc_wifi_flow_crtl_info_t wifi_flow_crtl_info;
	RTK_FC_HELPER_MGR_WIFI_FLOW_CRTL_INFO_GET(&wifi_flow_crtl_info);

	PROC_PRINTF("wifi flow ctrl auto mechanism: %s\n", wifi_flow_crtl_info.wifi_flow_ctrl_auto_en?"ENABLE":"DISABLE");
	PROC_PRINTF("wlan0_flow_ctrl_on_threshold_mbps: %d\n", fc_db.controlFuc.wifi_flow_crtl_threshold.wlan0_flow_ctrl_on_threshold_mbps);
	PROC_PRINTF("wlan0_flow_ctrl_off_threshold_mbps %d\n", fc_db.controlFuc.wifi_flow_crtl_threshold.wlan0_flow_ctrl_off_threshold_mbps);
	PROC_PRINTF("wlan1_flow_ctrl_on_threshold_mbps: %d\n", fc_db.controlFuc.wifi_flow_crtl_threshold.wlan1_flow_ctrl_on_threshold_mbps);
	PROC_PRINTF("wlan1_flow_ctrl_off_threshold_mbps: %d\n", fc_db.controlFuc.wifi_flow_crtl_threshold.wlan1_flow_ctrl_off_threshold_mbps);
	return SUCCESS;
}

int rtk_fc_proc_wlan0_flow_ctrl_on_threshold_mbps_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff, len);

	fc_db.controlFuc.wifi_flow_crtl_threshold.wlan0_flow_ctrl_on_threshold_mbps = value;
	rtk_fc_wifi_flow_ctrl_onOff_check();
	rtk_fc_proc_wifi_flow_ctrl_threshold_get(NULL, NULL);

	return len;
}

int rtk_fc_proc_wlan0_flow_ctrl_off_threshold_mbps_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff, len);

	fc_db.controlFuc.wifi_flow_crtl_threshold.wlan0_flow_ctrl_off_threshold_mbps = value;
	rtk_fc_wifi_flow_ctrl_onOff_check();
	rtk_fc_proc_wifi_flow_ctrl_threshold_get(NULL, NULL);

	return len;
}

int rtk_fc_proc_wlan1_flow_ctrl_on_threshold_mbps_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff, len);

	fc_db.controlFuc.wifi_flow_crtl_threshold.wlan1_flow_ctrl_on_threshold_mbps = value;
	rtk_fc_wifi_flow_ctrl_onOff_check();
	rtk_fc_proc_wifi_flow_ctrl_threshold_get(NULL, NULL);

	return len;
}


int rtk_fc_proc_wlan1_flow_ctrl_off_threshold_mbps_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff, len);

	fc_db.controlFuc.wifi_flow_crtl_threshold.wlan1_flow_ctrl_off_threshold_mbps = value;
	rtk_fc_wifi_flow_ctrl_onOff_check();
	rtk_fc_proc_wifi_flow_ctrl_threshold_get(NULL, NULL);

	return len;
}
#endif

void rtk_fc_proc_wifi_tx_qos_mapping_usage(void)
{
	rtlglue_printf("\n\n");
	rtlglue_printf("usage:\n");
	rtlglue_printf("Change Wifi Tx QoS mapping state:\n");
	rtlglue_printf("- ENABLE : echo enable > /proc/fc/ctrl/wifi_tx_qos_mapping\n");
	rtlglue_printf("- DISABLE: echo disable > /proc/fc/ctrl/wifi_tx_qos_mapping\n\n");
	rtlglue_printf("Change mapping:\n");
	rtlglue_printf("- internal priority to WIFI_PRI mapping: echo INT_PRI WIFI_PRI > /proc/fc/ctrl/wifi_tx_qos_mapping\n");
	rtlglue_printf("  e.g. internal priority 7 mapping to wifi pri 3: echo intpri 7 3 > /proc/fc/ctrl/wifi_tx_qos_mapping\n");
	rtlglue_printf("- DSCP to WIFI_PRI mapping: echo INT_PRI WIFI_PRI > /proc/fc/ctrl/wifi_tx_qos_mapping\n");
	rtlglue_printf("  e.g. dscp 40 mapping to wifi pri 3: echo dscp 40 3 > /proc/fc/ctrl/wifi_tx_qos_mapping\n");
	
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	rtlglue_printf("- WIFI_PRI to AMSDU_Q mapping: echo amsduq WIFI_PRI AMSDU_Q > /proc/fc/ctrl/wifi_tx_qos_mapping\n");
	rtlglue_printf("  e.g. wifi pri 3 mapping to amsdu queue 0: echo amsduq 3 0 > /proc/fc/ctrl/wifi_tx_qos_mapping\n");
#endif
	rtlglue_printf("\n");
	rtlglue_printf("Dump all mapping table:\n");
	rtlglue_printf("- echo dump > /proc/fc/ctrl/wifi_tx_qos_mapping\n\n");
}
void rtk_fc_proc_wifi_tx_qos_dump_intpri_to_wifi_pri(void)
{
	int i = 0;
	uint8 wifi_pri = 0;

	rtlglue_printf("Wifi Tx: internal pri to wifi pri mapping:\n");
	rtlglue_printf("%-12s | %-12s\n", "int pri", "Wifi pri");
	rtlglue_printf("--------------------------------- \n");
	for(i = 0 ; i < 8 ; i++)
	{
		wifi_pri = fc_db.controlFuc.intPri_to_wifiPri[i];
		rtlglue_printf("%-12d | %-12d\n", i, wifi_pri);
	}
	rtlglue_printf("\n");
}

void rtk_fc_proc_wifi_tx_qos_dump_dscp_to_wifi_pri(void)
{
	int i = 0;
	uint8 wifi_pri = 0;
	uint8 start_idx = 0, end_idx = 0;

	rtlglue_printf("Wifi Tx: IP DSCP to wifi pri mapping:\n");
	rtlglue_printf("%-12s | %-12s\n", "IP DSCP", "Wifi pri");
	rtlglue_printf("--------------------------------- \n");
	start_idx = 0;
	wifi_pri = fc_db.controlFuc.dscp_to_wifiPri[start_idx];
	for(i = start_idx ; i < 64 ; i++)
	{
		end_idx = i;

		if(i == 63)
		{
			if(start_idx == end_idx)
				rtlglue_printf("%02d%-10s | %-12d\n", start_idx, " ", wifi_pri);
			else
				rtlglue_printf("%02d - %02d%-5s | %-12d\n", start_idx, end_idx, " ", wifi_pri);
		}
		else if(wifi_pri != fc_db.controlFuc.dscp_to_wifiPri[i+1])
		{
			if(start_idx == end_idx)
				rtlglue_printf("%02d%-10s | %-12d\n", start_idx, " ", wifi_pri);
			else
				rtlglue_printf("%02d - %02d%-5s | %-12d\n", start_idx, end_idx, " ", wifi_pri);
			start_idx = i+1;
			wifi_pri = fc_db.controlFuc.dscp_to_wifiPri[i+1];
		}
	}
	rtlglue_printf("\n");
}

int rtk_fc_proc_wifi_tx_qos_mapping_get(struct seq_file *s, void *v)
{
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	int i = 0, j = 0;
	uint8 wifi_pri = 0;
	char string_amsdu_pe_wmm_queue[RTK_FC_WIFI_AMSDU_PE_QUEUE_MAX_WMM_EPP64_MODE+1][20] = {{0}};
#endif

	if(fc_db.controlFuc.wifi_tx_qos_enable == WIFI_TX_QOS_MODE_ENABLE)
	{
		rtlglue_printf("Wifi Tx QoS mapping state: \033[1;33mON\033[0m\n\n");
		rtk_fc_proc_wifi_tx_qos_dump_intpri_to_wifi_pri();
		rtk_fc_proc_wifi_tx_qos_dump_dscp_to_wifi_pri();
	}
	else
	{
		rtlglue_printf("Wifi Tx QoS mapping state: \033[1;33mOFF\033[0m\n\n");
		rtlglue_printf("Wifi TX pri always = 0.\n\n");
	}

#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	PROC_PRINTF("\n\n");
	PROC_PRINTF("### wifi pri <=> AMSDU queue ###\n");
	PROC_PRINTF("%-12s | %-12s\n", "Wifi pri", "AMSDU queue");
	PROC_PRINTF("--------------------------------- \n");
	for(i=0 ; i<8 ; i++)
	{
		wifi_pri = i;
		if(RTK_FC_WIFI_AMSDU_PE_OFFLOAD_IF_EPP64_WMM_MODE(fc_db.wifi_amsdu_pe_offload_mode))
		{
			for(j = 0 ; j <= RTK_FC_WIFI_AMSDU_PE_QUEUE_MAX_WMM_EPP64_MODE ; j++)
			{
				if(j == RTK_FC_WIFI_AMSDU_PE_WMM_QUEUE_BK)
					strcpy(&string_amsdu_pe_wmm_queue[j][0], "WMM_QUEUE_BK");
				else if(j == RTK_FC_WIFI_AMSDU_PE_WMM_QUEUE_BE)
					strcpy(&string_amsdu_pe_wmm_queue[j][0], "WMM_QUEUE_BE");
				else if(j == RTK_FC_WIFI_AMSDU_PE_WMM_QUEUE_VI)
					strcpy(&string_amsdu_pe_wmm_queue[j][0], "WMM_QUEUE_VI");
				else if(j == RTK_FC_WIFI_AMSDU_PE_WMM_QUEUE_VO)
					strcpy(&string_amsdu_pe_wmm_queue[j][0], "WMM_QUEUE_VO");
			}
			PROC_PRINTF("%-12d | %-12d(%s)\n",	wifi_pri, fc_db.controlFuc.wifiPri_to_tx_asmduQ_mapping[wifi_pri], string_amsdu_pe_wmm_queue[fc_db.controlFuc.wifiPri_to_tx_asmduQ_mapping[wifi_pri]]);
		}
		else
			PROC_PRINTF("%-12d | %-12d\n",	wifi_pri, fc_db.controlFuc.wifiPri_to_tx_asmduQ_mapping[wifi_pri]);
	}
#endif

	return SUCCESS;
}

int rtk_fc_proc_wifi_tx_qos_mapping_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int length = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;
	int internal_pri, dscp, wifi_priority;
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	int8 amsdu_queue, max_amsdu_queue;
	uint mac_id;
	unsigned char l2Addr[ETH_ALEN];
	int16 lutIdx;
#endif

	if (buff)
	{
		//fc_db.debug_level=simple_strtoul(tmpBuf, NULL, 16);
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, length);

		strptr=tmpBuf;

		split_str=strsep(&strptr," ");
		if(split_str==NULL){
			rtk_fc_proc_wifi_tx_qos_mapping_usage();
			goto INVALID_PARA;
		}

		if(strcasecmp(split_str, "enable")==0) {
			if(fc_db.controlFuc.wifi_tx_qos_enable != WIFI_TX_QOS_MODE_ENABLE)
			{
				fc_db.controlFuc.wifi_tx_qos_enable = WIFI_TX_QOS_MODE_ENABLE;
				rtk_fc_flow_flush();
			}
		}else if (strcasecmp(split_str, "disable")==0) {
			if(fc_db.controlFuc.wifi_tx_qos_enable != WIFI_TX_QOS_MODE_DISABLE)
			{
				fc_db.controlFuc.wifi_tx_qos_enable = WIFI_TX_QOS_MODE_DISABLE;
				rtk_fc_flow_flush();
			}
		}else if (strcasecmp(split_str, "dump")==0) {
			rtk_fc_proc_wifi_tx_qos_dump_intpri_to_wifi_pri();
			rtk_fc_proc_wifi_tx_qos_dump_dscp_to_wifi_pri();
		}else if (strcasecmp(split_str, "intpri")==0) {
			split_str = strsep(&strptr," ");
			if(split_str==NULL){
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}
			internal_pri = simple_strtol(split_str, NULL, 0);
			if(internal_pri > 7)
			{
				rtlglue_printf("invalid intpri=%d \n", internal_pri);
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}

			split_str = strsep(&strptr," ");
			if(split_str==NULL){
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}
			wifi_priority = simple_strtol(split_str, NULL, 0);

			if(wifi_priority > 7)
			{
				rtlglue_printf("invalid wifi_priority=%d \n", wifi_priority);
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}
			rtlglue_printf("mapping internal_pri %d to wifi priority %d\n", internal_pri, wifi_priority);

			if(fc_db.controlFuc.intPri_to_wifiPri[internal_pri] != wifi_priority)
			{
				fc_db.controlFuc.intPri_to_wifiPri[internal_pri] = wifi_priority;
				rtk_fc_flow_flush();
			}
		}else if (strcasecmp(split_str, "dscp")==0) {
			split_str = strsep(&strptr," ");
			if(split_str==NULL){
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}
			dscp = simple_strtol(split_str, NULL, 0);
			if(dscp > 63)
			{
				rtlglue_printf("invalid dscp=%d \n", dscp);
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}

			split_str = strsep(&strptr," ");
			if(split_str==NULL){
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}
			wifi_priority = simple_strtol(split_str, NULL, 0);

			if(wifi_priority > 7)
			{
				rtlglue_printf("invalid wifi_priority=%d \n", wifi_priority);
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}
			rtlglue_printf("mapping dscp %d to wifi priority %d\n", dscp, wifi_priority);

			if(fc_db.controlFuc.dscp_to_wifiPri[dscp] != wifi_priority)
			{
				fc_db.controlFuc.dscp_to_wifiPri[dscp] = wifi_priority;
				rtk_fc_flow_flush();
			}
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
		}else if (strcasecmp(split_str, "amsduq")==0) {
			split_str = strsep(&strptr," ");
			if(split_str==NULL){
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}
			wifi_priority = simple_strtol(split_str, NULL, 0);

			if(wifi_priority > 7)
			{
				rtlglue_printf("invalid wifi_priority=%d \n", wifi_priority);
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}

			split_str = strsep(&strptr," ");
			if(split_str==NULL){
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}
			amsdu_queue = simple_strtol(split_str, NULL, 0);

			if(RTK_FC_WIFI_AMSDU_PE_OFFLOAD_IF_EPP64_TC_MODE(fc_db.wifi_amsdu_pe_offload_mode))
				max_amsdu_queue = RTK_FC_WIFI_AMSDU_PE_QUEUE_MAX_TC_EPP64_MODE;
			else if(RTK_FC_WIFI_AMSDU_PE_OFFLOAD_IF_EPP64_WMM_MODE(fc_db.wifi_amsdu_pe_offload_mode))
				max_amsdu_queue = RTK_FC_WIFI_AMSDU_PE_QUEUE_MAX_WMM_EPP64_MODE;
			else
			{
				//RTK_FC_WIFI_AMSDU_PE_OFFLOAD_MODE_TC_EPP256, RTK_FC_WIFI_AMSDU_PE_OFFLOAD_MODE_WMM_EPP256
				rtlglue_printf("WIFI_AMSDU_PE_OFFLOAD in EPP256 mode is not defined yet \n");
				goto INVALID_PARA;
			}

			if((amsdu_queue < RTK_FC_WIFI_AMSDU_PE_QUEUE0) || (amsdu_queue > max_amsdu_queue))
			{
				rtlglue_printf("invalid amsdu_queue=%d \n", amsdu_queue);
				rtk_fc_proc_wifi_tx_qos_mapping_usage();
				goto INVALID_PARA;
			}
			rtlglue_printf("mapping wifi priority %d to amsdu queue %d\n", wifi_priority, amsdu_queue);

			fc_db.controlFuc.wifiPri_to_tx_asmduQ_mapping[wifi_priority] = amsdu_queue;
			/* flush wifi amsdu flow */
			for(mac_id = 0 ; mac_id < RTK_FC_WIFI_PE_OFFLOAD_MAC_ID_SIZE ; mac_id++)
			{
				if(fc_db.wifi_amsdu_pe_offload_mac_id_tbl[mac_id].valid)
				{
					memcpy(l2Addr, fc_db.wifi_amsdu_pe_offload_mac_id_tbl[mac_id].l2Addr, sizeof(l2Addr));
					if(_rtk_fc_lut_find(l2Addr, &lutIdx) == RTK_FC_RET_OK)
					{
						assert(0 <= lutIdx && lutIdx < RTK_FC_TABLESIZE_LUT);
						rtk_fc_flow_update_for_amsdu_mac_id_update(lutIdx);
					}
				}
			}
#endif
		}
		else
		{
			rtlglue_printf("unknown configuration! \n");
			rtk_fc_proc_wifi_tx_qos_mapping_usage();
			goto INVALID_PARA;
		}

	}

INVALID_PARA:
	return len;
}

int rtk_fc_proc_debug_message_display_to_tty_enable_read(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.debug_message_display_to_tty);
	return SUCCESS;
}
int rtk_fc_proc_debug_message_display_to_tty_enable_write(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff, len);
	fc_db.controlFuc.debug_message_display_to_tty = (value!=0) ? TRUE : FALSE;

	if(fc_db.controlFuc.debug_message_display_to_tty == TRUE)
	{
		fc_db.controlFuc.tty_display_cur_sh_process = current;
		fc_db.controlFuc.tty_display_cur_sh_process_pid = current->pid;
	}

	else
		fc_db.controlFuc.tty_display_cur_sh_process = NULL;

	return len;
}

void rtk_fc_proc_manager_debug_message_usage(void)
{
	rtlglue_printf("usage:\n");
	rtlglue_printf("ENABLE: echo 1 > /proc/fc/trace/manager_debug\n");
	rtlglue_printf("DISABLE: echo 0 > /proc/fc/trace/manager_debug\n");

}

int rtk_fc_proc_manager_debug_message_read(struct seq_file *s, void *v)
{
	int fc_mgr_debug = 0;
	RTK_FC_HELPER_MGR_DEBUG_CONFIG_GET(&fc_mgr_debug);
	if(fc_mgr_debug > 0)
		PROC_PRINTF("FC Manager debug log is enabled\n");
	else
		PROC_PRINTF("FC Manager debug log is disabled\n");
	return SUCCESS;
}
int rtk_fc_proc_manager_debug_message_write(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff, len);

	if(value<0 || value > 2)
		rtk_fc_proc_manager_debug_message_usage();
	else
		RTK_FC_HELPER_MGR_DEBUG_CONFIG_SET(value);

	return len;
}

int rtk_fc_proc_flow_delete_track_read(struct seq_file *s, void *v)
{
	int i, cnt = 0;
	PROC_PRINTF("\n");
	for(i = 0 ; i < FLOWDELTRACE_MAX ; i++)
	{
		if(fc_db.flow_del_trace_index[i] == SIGNED_INVALID)
			break;
		PROC_PRINTF("flowIdx[%d]: %d\n", i, fc_db.flow_del_trace_index[i]);
		cnt++;
	}
	PROC_PRINTF("=== number of tracking flows: %d ===\n", cnt);

	PROC_PRINTF("usage:\n");
	PROC_PRINTF("echo operarion [flowIdx] > /proc/fc/trace/flow_delete_track\n");
	PROC_PRINTF("operarion:\n");
	PROC_PRINTF("  track: track flow[flowIdx] once it is deleted. Support maximum %d tracking flows.\n", FLOWDELTRACE_MAX);
	PROC_PRINTF("     e.g. echo track 100 > /proc/fc/trace/flow_delete_track\n");
	PROC_PRINTF("  untrack: untrack flow[flowIdx]\n");
	PROC_PRINTF("     e.g. echo untrack 100 > /proc/fc/trace/flow_delete_track\n");
	PROC_PRINTF("  flush: untrack all traking flows\n");
	PROC_PRINTF("     e.g. echo flush > /proc/fc/trace/flow_delete_track\n");
	return SUCCESS;
}
int rtk_fc_proc_flow_delete_track_write(struct file *filp, const char *buff, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	int32 flowIdx, i;

	if(buff)
	{
		char *strptr, *split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr = tmpBuf;

		split_str = strsep(&strptr, " ");//get flow_operation
		if(split_str == NULL){
			rtk_fc_proc_flow_delete_track_read(NULL, NULL);
			return len;
		}

		if(strcasecmp(split_str, "track")==0)
		{
			split_str = strsep(&strptr, " ");	//get flow indexed
			if(split_str != NULL)
			{
				flowIdx = simple_strtol(split_str, NULL, 0);
				for(i = 0 ; i < FLOWDELTRACE_MAX ; i++)
				{
					if(fc_db.flow_del_trace_index[i] == flowIdx)
						break;
					if(fc_db.flow_del_trace_index[i] == SIGNED_INVALID)
					{
						fc_db.flow_del_trace_index[i] = flowIdx;
						break;
					}
				}
				if(i == FLOWDELTRACE_MAX)
					rtlglue_printf("tracking flow is fulled.\n");
			}
		}
		else if(strcasecmp(split_str, "untrack")==0)
		{
			split_str = strsep(&strptr," ");	//get flow indexed
			if(split_str != NULL)
			{
				int j;
				flowIdx = simple_strtol(split_str, NULL, 0);
				if(flowIdx != SIGNED_INVALID)
				{
					for(i = 0 ; i < FLOWDELTRACE_MAX ; i++)
					{
						if(fc_db.flow_del_trace_index[i] == flowIdx)
							break;
					}
					if(i != FLOWDELTRACE_MAX)
					{
						for(j = i ; j < (FLOWDELTRACE_MAX-1); j++)
						{
							fc_db.flow_del_trace_index[j] = fc_db.flow_del_trace_index[j+1];
						}
						fc_db.flow_del_trace_index[j] = SIGNED_INVALID;
					}
				}
			}
		}
		else if(strcasecmp(split_str, "flush")==0)
		{
			for(i = 0 ; i < FLOWDELTRACE_MAX ; i++)
				fc_db.flow_del_trace_index[i] = SIGNED_INVALID;
		}
	}
	rtk_fc_proc_flow_delete_track_read(NULL, NULL);

	return len;
}

int rtk_fc_proc_wanPortMask_get(struct seq_file *s, void *v)
{
        int len=0;
        PROC_PRINTF("Dump WAN port mask:");
        PROC_PRINTF("0x%llx\n",fc_db.wanPortMask.portmask);

	if(fc_db.wanPortWifidevMask) {
        	PROC_PRINTF("Dump WAN port - wlan dev mask:");
		PROC_PRINTF("0x%llx\n",fc_db.wanPortWifidevMask);
	}
	return len;
}

int rtk_fc_proc_wanPortMask_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	rtk_fc_port_mask_t portmask = 0;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int cmdlen = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;
		
	/* copy data to the buffer */
	strscpy(tmpBuf, buff, cmdlen);

	strptr=tmpBuf;
	split_str=strsep(&strptr," ");

	// MAC port as wan
	portmask = simple_strtol(split_str, NULL, 0);
	rtk_fc_wan_portmask_set(portmask);

	// WLAN DEVIDX as wan
	split_str=strsep(&strptr," ");	//get detail portmask setting
	if(split_str!=NULL){
		fc_db.wanPortWifidevMask = simple_strtoll(split_str, NULL, 0);
	}
	

	return len;
}

int rtk_fc_proc_macPortMaskWithoutCpu_get(struct seq_file *s, void *v)
{
    int len=0;
    PROC_PRINTF("Dump MAC port mask(without cpu):");
    PROC_PRINTF("0x%x\n",RTK_FC_ALL_MAC_PORTMASK_WITHOUT_CPU);

	return len;
}

int rtk_fc_proc_macPortMaskWithoutCpu_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	unsigned int portmask = 0;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int cmdlen = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;

	/* copy data to the buffer */
	strscpy(tmpBuf, buff, cmdlen);

	strptr=tmpBuf;
	split_str=strsep(&strptr," ");

	portmask = simple_strtol(split_str, NULL, 0);
	fc_db.all_mac_portmask_without_cpu = portmask;
	rtk_fc_wan_portmask_set(fc_db.wanPortMask.portmask & portmask);

	return len;
}

void _rtk_fc_max_fc_wlan_rx_queue_size_usage(void){
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("    echo [MAX_FC_WLAN_RX_QUEUE_SIZE] [MAX_FC_WLAN_RX_QUEUE_SIZE_OFFSET]  > /proc/fc/ctrl/max_fc_wlan_rx_queue_size \n");
	rtlglue_printf("   Note. 0 means keep original.\n");

	rtlglue_printf("   Example: \n");
	rtlglue_printf("   - keep current max_fc_wlan_rx_queue_size and set max_fc_wlan_rx_queue_size_offset to 2 \n");
	rtlglue_printf("     echo 0 2 > /proc/fc/ctrl/max_fc_wlan_rx_queue_size \n");
	rtlglue_printf("   - set max_fc_wlan_rx_queue_size to 4096 and set max_fc_wlan_rx_queue_size_offset to 1 \n");
	rtlglue_printf("     echo 4096 1 > /proc/fc/ctrl/max_fc_wlan_rx_queue_size \n");
}

int rtk_fc_proc_max_fc_wlan_rx_queue_size_get(struct seq_file *s, void *v)
{
	uint32 rx_queue_size=0, rx_queue_size_offset=0;

	rtk_fc_max_fc_wlan_rx_queue_size_get(&rx_queue_size, &rx_queue_size_offset);
	if(rx_queue_size == 0 || rx_queue_size_offset == 0){
		rtlglue_printf("Failed to get parameters!\n");
		return -1;
	}
	else{
		rtlglue_printf("Dump max_fc_wlan_rx_queue_size: %d\n", rx_queue_size);
		rtlglue_printf("Dump max_fc_wlan_rx_queue_size_offset: %d\n", rx_queue_size_offset);
		rtlglue_printf("\nMax #dequeue in wlan rx IPI: \n");
		rtlglue_printf("%d(max_fc_wlan_rx_queue_size)/%d(max_fc_wlan_rx_queue_size_offset) = %d\n\n", rx_queue_size, rx_queue_size_offset, rx_queue_size/rx_queue_size_offset);
	}

	_rtk_fc_max_fc_wlan_rx_queue_size_usage();

	return 0;
}

int rtk_fc_proc_max_fc_wlan_rx_queue_size_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	uint32 rx_queue_size=0, rx_queue_size_offset=0;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int cmdlen = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;

	/* copy data to the buffer */
	strscpy(tmpBuf, buff, cmdlen);

	strptr=tmpBuf;
	split_str=strsep(&strptr," ");
	if(split_str==NULL)
	{
		_rtk_fc_max_fc_wlan_rx_queue_size_usage();
		return -1;
	}

	// get max_fc_wlan_rx_queue_size
	rx_queue_size = simple_strtol(split_str, NULL, 0);

	// get max_fc_wlan_rx_queue_size_offset
	split_str=strsep(&strptr," ");
	if(split_str==NULL)
	{
		_rtk_fc_max_fc_wlan_rx_queue_size_usage();
		return -1;
	}
	rx_queue_size_offset = simple_strtol(split_str, NULL, 0);

	rtk_fc_max_fc_wlan_rx_queue_size_set(rx_queue_size, rx_queue_size_offset);

	return len;
}

int rtk_fc_proc_l2_macEgrLearning_get(struct seq_file *s, void *v)
{
	
	if(fc_db.controlFuc.l2_macEgrLearning) {
		PROC_PRINTF("mode 1: enable l2 egress learning, enabled portmask 0x%llx  \n", 
			fc_db.controlFuc.l2_macEgrLearning);
	}else{
		PROC_PRINTF("mode 0: disable l2 egress learning\n");
	}
	
	PROC_PRINTF("\nUsage:\n");
	PROC_PRINTF("     echo [mode] [portmask] > /proc/fc/ctrl/l2_macEgrLearning \n");
	PROC_PRINTF("     [mode]:\n");
	PROC_PRINTF("       0: ingress learning, 1: egress learning \n");
	PROC_PRINTF("     [portmask]: \n");
	PROC_PRINTF("       egress learning portmask \n");


	return 0;
}

int rtk_fc_proc_l2_macEgrLearning_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int mode=0;
	rtk_fc_port_mask_t portmask = 0;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int cmdlen = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;
		
	//fc_db.controlFuc.l2_macEgrLearning = ((_rtk_fc_proc_parsing_string_to_integer(buff,len)==0) ? FALSE : TRUE);

	/* copy data to the buffer */
	strscpy(tmpBuf, buff, cmdlen);

	strptr=tmpBuf;
	split_str=strsep(&strptr," ");

	mode = simple_strtol(split_str, NULL, 0);

	if(mode == 0) {
		portmask = 0x0;
	}else if(mode == 1) {

		split_str=strsep(&strptr," ");	//get detail portmask setting
		if(split_str==NULL){
			portmask = RTK_FC_ALL_MAC_PORTMASK;
		}else{
			portmask = simple_strtol(split_str, NULL, 0);
		}
	}else {
		WARNING("not support");
		return cmdlen;
	}


	fc_db.controlFuc.l2_macEgrLearning = portmask;

	return cmdlen;
}


int rtk_fc_proc_l2_mac_portGroup_get(struct seq_file *s, void *v)
{
	int i = 0;
	
	if(fc_db.macAddr_portGroup.group.portmask) {
		
		PROC_PRINTF("port grouping is Enabled\n\n");
		
		for(i = 0; i <RTK_FC_MAC_PORT_MAX; i++) {
			if(((1<<i) & fc_db.macAddr_portGroup.group.portmask)) {
				PROC_PRINTF("grouping port[%d] limit count = %d\n", i, fc_db.macAddrLearningLimit[i].learningLimit_conf.learningLimitNumber);
			}	
		}

		PROC_PRINTF("portmask 0x%llx group limit count is %d current learning count is %d\n", 
			fc_db.macAddr_portGroup.group.portmask, 
			fc_db.macAddr_portGroup.limitCfg.learningLimit_conf.learningLimitNumber,
			atomic_read(&fc_db.macAddr_portGroup.limitCfg.learningCount));
		
	}else{
	
		PROC_PRINTF("port grouping is Disabled\n\n");
	}

	return 0;
}

int rtk_fc_proc_l2_mac_portGroup_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int i = 0;
	rtk_fc_port_mask_t portmask = 0;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int cmdlen = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;

	/* copy data to the buffer */
	strscpy(tmpBuf, buff, cmdlen);

	strptr=tmpBuf;
	split_str=strsep(&strptr," ");

	portmask = simple_strtol(split_str, NULL, 0);

	if(portmask==0 && fc_db.macAddr_portGroup.group.portmask!=0) {
		for(i = 0; i <RTK_FC_MAC_PORT_MAX; i++) {
			if((1<<i) & fc_db.macAddr_portGroup.group.portmask) {
				// init per port learning limit
				fc_db.macAddrLearningLimit[i].learningLimit_conf.learningLimitNumber = SIGNED_INVALID;
			}	
		}
	}

	rtk_fc_macAddr_portGroup_lrnCnt_config(portmask);

	return cmdlen;
}

int rtk_fc_proc_epon_llid_format_get(struct seq_file *s, void *v)
{
        int len=0;
        PROC_PRINTF("%d (0: remarking streamid is SID(llid+qid);  1: remarking streamid is LLID only, driver merge llid(3bits) and qid(4bit) as SID)\n", fc_db.controlFuc.epon_llid_format);
        return len;
}

int rtk_fc_proc_epon_llid_format_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	fc_db.controlFuc.epon_llid_format = ((_rtk_fc_proc_parsing_string_to_integer(buff,len)==0) ? FALSE : TRUE);
	RTK_FC_HELPER_MGR_EPON_LLID_FORMAT_SET(fc_db.controlFuc.epon_llid_format);

	return len;
}

int rtk_fc_proc_flow_tcp_learning_state_get(struct seq_file *s, void *v)
{
	int len=0;
	PROC_PRINTF("current mode: %d \n", fc_db.controlFuc.flow_tcp_learning_state);
	PROC_PRINTF("Usage:  decide the timing to learn tcp flow to HW\n");
	PROC_PRINTF("            0: learn at first syn and syn+ack (the 1st and 2nd TCP packet)\n");
	PROC_PRINTF("            1: learn at syn+ack and ack (the 2nd and 3rd TCP packet)\n");
	PROC_PRINTF("            2: learn at established (the 3rd and after)\n");
	
	return len;
}

int rtk_fc_proc_flow_tcp_learning_state_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	rtk_fc_tcp_lrn_state_t newstate = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	if(newstate < TCP_LEARN_END)
		fc_db.controlFuc.flow_tcp_learning_state = newstate;
	else
		rtlglue_printf("ERROE: available state is 0 to %d\n", (TCP_LEARN_END-1));

	return len;
}

int rtk_fc_proc_flow_tcp_bidirection_learning_get(struct seq_file *s, void *v)
{
        int len=0;
        PROC_PRINTF("%d (0: default learning by each tcp flow;  1: learn bidirection flow at same time (routing only).\n", fc_db.controlFuc.flow_tcp_bidirection_learning);
        return len;
}

int rtk_fc_proc_flow_tcp_bidirection_learning_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	fc_db.controlFuc.flow_tcp_bidirection_learning = ((_rtk_fc_proc_parsing_string_to_integer(buff,len)==0) ? FALSE : TRUE);

	if(fc_db.controlFuc.flow_tcp_bidirection_learning)
		RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_TRAP_TCP_SYN_ACK, DISABLED);
	else
		RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_TRAP_TCP_SYN_ACK, ENABLED);
		

	return len;
}

int rtk_fc_proc_pppoe_connectionAutoExtend_period_get(struct seq_file *s, void *v)
{
	if(fc_db.controlFuc.pppoe_connectionAutoExtend == TRUE)
		PROC_PRINTF("PPPoE connection auto extend period: %d sec.\n", fc_db.controlFuc.pppoe_connectionAutoExtend_period_unit1s);
	else
		PROC_PRINTF("PPPoE connection auto extend mechanism is disabled.\n");
	return SUCCESS;
}

int rtk_fc_proc_pppoe_connectionAutoExtend_period_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int time = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	if(time < 1)
	{
		rtlglue_printf("\ndisable PPPoE connection auto extend mechanism.\n");
		fc_db.controlFuc.pppoe_connectionAutoExtend = FALSE;
		if(RTK_FC_HELPER_TIMER_PENDING(fc_db.controlFuc.pppoe_connectionAutoExtend_timer))
			RTK_FC_HELPER_DEL_TIMER(fc_db.controlFuc.pppoe_connectionAutoExtend_timer);
		return len;
	}

	fc_db.controlFuc.pppoe_connectionAutoExtend = TRUE;
	fc_db.controlFuc.pppoe_connectionAutoExtend_period_unit1s = time;

	if(RTK_FC_HELPER_TIMER_PENDING(fc_db.controlFuc.pppoe_connectionAutoExtend_timer))
		RTK_FC_HELPER_DEL_TIMER(fc_db.controlFuc.pppoe_connectionAutoExtend_timer);
	RTK_FC_HELPER_INIT_TIMER(fc_db.controlFuc.pppoe_connectionAutoExtend_timer, _rtk_fc_pppoeConnectionAutoExtend_task);
	RTK_FC_HELPER_SETUP_TIMER(fc_db.controlFuc.pppoe_connectionAutoExtend_timer,_rtk_fc_pppoeConnectionAutoExtend_task,(unsigned long)NULL);
	RTK_FC_HELPER_MOD_TIMER(fc_db.controlFuc.pppoe_connectionAutoExtend_timer, jiffies + (fc_db.controlFuc.pppoe_connectionAutoExtend_period_unit1s* CONFIG_HZ));
 
	rtk_fc_proc_pppoe_connectionAutoExtend_period_get(NULL, NULL);
	return len;
}
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES)
int rtk_fc_proc_xgspon_ds_to_l3fe_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.p6_xfi_en?1:0);
	return SUCCESS;
}

int rtk_fc_proc_xgspon_ds_to_l3fe_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	int ret = len;
	if(val)
		val = ENABLED;
	else
		val = DISABLED;

	if(val) {
		fc_db.controlFuc.p6_xfi_en = TRUE;
		fc_db.controlFuc.p7_rxsel_l3fe = TRUE;

		RTK_FC_ASIC_PONRX_TO_L3FE_SET(fc_db.controlFuc.p7_rxsel_l3fe);
		
		_rtk_rg_aclAndCfReservedRuleDelSpecial(RTK_CA_CLS_TYPE_GENERIC_INTF_HASH_PROFILE_DECISION, NULL);
		_rtk_rg_aclAndCfReservedRuleAddSpecial(RTK_CA_CLS_TYPE_GENERIC_INTF_HASH_PROFILE_DECISION, NULL);
	}else {
		fc_db.controlFuc.p6_xfi_en = FALSE;
		fc_db.controlFuc.p7_rxsel_l3fe = FALSE;

		RTK_FC_ASIC_PONRX_TO_L3FE_SET(fc_db.controlFuc.p7_rxsel_l3fe);
		
		_rtk_rg_aclAndCfReservedRuleDelSpecial(RTK_CA_CLS_TYPE_GENERIC_INTF_HASH_PROFILE_DECISION, NULL);
		_rtk_rg_aclAndCfReservedRuleAddSpecial(RTK_CA_CLS_TYPE_GENERIC_INTF_HASH_PROFILE_DECISION, NULL);
	}

	return ret;
}
#endif

int rtk_fc_proc_flow_meter_mib_conf_dependence_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.flow_meter_mib_conf_dependence?1:0);

	return SUCCESS;
}


int rtk_fc_proc_flow_meter_mib_conf_dependence_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	int ret = len;
#if defined(CONFIG_FC_CA8277B_SERIES)
#else
	uint32 ori_acl_remap_cnt = RTK_FC_ACL_POLREMAP_CNT;
#endif

	if(val)
		val = ENABLED;
	else
		val = DISABLED;
#if defined(CONFIG_FC_CA8277B_SERIES)
	rtlglue_printf("support flow_meter_mib_conf_dependence is 1 only\n");
	ret = FAILED;
#else
	if(fc_db.controlFuc.flow_meter_mib_conf_dependence != val)
	{
		rtk_fc_flow_flush();
		fc_db.controlFuc.flow_meter_mib_conf_dependence = val;
		if(RTK_FC_ACL_POLREMAP_CNT != ori_acl_remap_cnt)
		{
			//rearrange ACL rule because RTK_FC_ACL_POLREMAP_CNT changed
			assert_ok(_rtk_rg_aclAndCfReservedRuleAddSpecial(RTK_CLS_TYPE_USER_ACL_REARRANGE, NULL));
		}
	}
#endif
	rtk_fc_proc_flow_meter_mib_conf_dependence_get(NULL,NULL);
	return ret;
}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int rtk_fc_proc_hw_error_pkt_check_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.hw_l3l4_error_pkt_check?1:0);
	return SUCCESS;
}

int rtk_fc_proc_hw_error_pkt_check_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	int ret = len;
	if(val)
		val = ENABLED;
	else
		val = DISABLED;

	if(fc_db.controlFuc.hw_l3l4_error_pkt_check != val)
	{
		static aal_l3_specpkt_err_fwd_ctrl_t err_fwd_ctrl;
		aal_l3_specpkt_err_fwd_ctrl_mask_t err_fwd_ctrl_mask;
		aal_l2_specpkt_detect_mask_t l2cfg_mask;
		aal_l2_specpkt_detect_t l2cfg;

		ASSERT_EQ(aal_l3_specpkt_err_fwd_ctrl_get(RTK_ASIC_DEVID, &err_fwd_ctrl),CA_E_OK);
		memset(&err_fwd_ctrl_mask, 0, sizeof(err_fwd_ctrl_mask));
		memset(&l2cfg_mask, 0, sizeof(l2cfg_mask));
		memset(&l2cfg, 0, sizeof(l2cfg));

		if(val)
		{
			//enable l3 l4 error check
			err_fwd_ctrl_mask.s.ipv4_hdr_cs_err = 1;
			err_fwd_ctrl.ipv4_hdr_cs_err = 0;				//drop

			err_fwd_ctrl_mask.s.ip_hdr_err = 1;
			err_fwd_ctrl.ip_hdr_err = 0;					//drop

			err_fwd_ctrl_mask.s.tcp_data_offset_err = 1;
			err_fwd_ctrl.tcp_data_offset_err = 0;			//drop

			err_fwd_ctrl_mask.s.udp_total_len_err = 1;
			err_fwd_ctrl.udp_total_len_err = 0;				//drop

			err_fwd_ctrl_mask.s.udp_lite_cs_cvrg_err = 1;
			err_fwd_ctrl.udp_lite_cs_cvrg_err = 0;			//drop

			err_fwd_ctrl_mask.s.rdp_hdr_len_err = 1;
			err_fwd_ctrl.rdp_hdr_len_err = 0;				//drop

			err_fwd_ctrl_mask.s.dsl_inner_ip_hdr_cs_err = 1;
			err_fwd_ctrl.dsl_inner_ip_hdr_cs_err = 0;		//drop

			err_fwd_ctrl_mask.s.inner_ip_hdr_err = 1;
			err_fwd_ctrl.inner_ip_hdr_err = 0;				//drop

			err_fwd_ctrl_mask.s.udp_zero_chksum_err = 1;
			err_fwd_ctrl.udp_zero_chksum_err = 0;			//drop

			err_fwd_ctrl_mask.s.pkt_l2_size_err = 1;
			err_fwd_ctrl.pkt_l2_size_err = 0;				//drop

#if defined(CONFIG_FC_RTL9607F_SERIES)
			/* 8277C: must set to forward */
			err_fwd_ctrl_mask.s.pkt_l3_total_len_err = 1;
			err_fwd_ctrl.pkt_l3_total_len_err = 0;			//drop

			err_fwd_ctrl_mask.s.pkt_inner_l3_total_len_err = 1;
			err_fwd_ctrl.pkt_inner_l3_total_len_err = 0;	//drop
#endif
			l2cfg_mask.s.ipv4_checksum_check_enable = 1;
			l2cfg.ipv4_checksum_check_enable = 1;			//enable check
		}
		else
		{
			//disable l3 l4 error check
			err_fwd_ctrl_mask.s.ipv4_hdr_cs_err = 1;
			err_fwd_ctrl.ipv4_hdr_cs_err = 2;				//Normal forward

			err_fwd_ctrl_mask.s.ip_hdr_err = 1;
			err_fwd_ctrl.ip_hdr_err = 2;					//Normal forward

			err_fwd_ctrl_mask.s.tcp_data_offset_err = 1;
			err_fwd_ctrl.tcp_data_offset_err = 2;			//Normal forward

			err_fwd_ctrl_mask.s.udp_total_len_err = 1;
			err_fwd_ctrl.udp_total_len_err = 2;				//Normal forward

			err_fwd_ctrl_mask.s.udp_lite_cs_cvrg_err = 1;
			err_fwd_ctrl.udp_lite_cs_cvrg_err = 2;			//Normal forward

			err_fwd_ctrl_mask.s.rdp_hdr_len_err = 1;
			err_fwd_ctrl.rdp_hdr_len_err = 2;				//Normal forward

			err_fwd_ctrl_mask.s.dsl_inner_ip_hdr_cs_err = 1;
			err_fwd_ctrl.dsl_inner_ip_hdr_cs_err = 2;		//Normal forward

			err_fwd_ctrl_mask.s.inner_ip_hdr_err = 1;
			err_fwd_ctrl.inner_ip_hdr_err = 2;				//Normal forward

			err_fwd_ctrl_mask.s.udp_zero_chksum_err = 1;
			err_fwd_ctrl.udp_zero_chksum_err = 2;			//Normal forward

			err_fwd_ctrl_mask.s.pkt_l2_size_err = 1;
			err_fwd_ctrl.pkt_l2_size_err = 2;				//Normal forward

#if defined(CONFIG_FC_RTL9607F_SERIES)
			/* 8277C: must set to forward */
			err_fwd_ctrl_mask.s.pkt_l3_total_len_err = 1;
			err_fwd_ctrl.pkt_l3_total_len_err = 2;			//Normal forward

			err_fwd_ctrl_mask.s.pkt_inner_l3_total_len_err = 1;
			err_fwd_ctrl.pkt_inner_l3_total_len_err = 2;	//Normal forward
#endif
			l2cfg_mask.s.ipv4_checksum_check_enable = 1;
			l2cfg.ipv4_checksum_check_enable = 0;			//disable check
		}

		ASSERT_EQ(aal_l3_specpkt_err_fwd_ctrl_set(RTK_ASIC_DEVID, err_fwd_ctrl_mask, &err_fwd_ctrl),CA_E_OK);
		ASSERT_EQ(aal_l2_specpkt_detect_set(RTK_ASIC_DEVID, l2cfg_mask, &l2cfg),CA_E_OK);

		fc_db.controlFuc.hw_l3l4_error_pkt_check = val;
	}

	rtk_fc_proc_hw_error_pkt_check_get(NULL,NULL);
	return ret;
}

int rtk_fc_proc_vlanParsingMode_get(struct seq_file *s, void *v)
{
	int i;
	char *mode_desc[FC_VLAN_PARSING_MODE_MAX] =
	{
		"default",
		"parsing outer vlan only",
	};
	PROC_PRINTF("vlan_parsing_mode: %u\n\n", fc_db.controlFuc.vlan_parsing_mode);
	PROC_PRINTF("%-4s\t| %-25s\n", "mode", "description");
	PROC_PRINTF("-----------------------------------\n");
	for(i = 0 ; i < FC_VLAN_PARSING_MODE_MAX ; i++)
		PROC_PRINTF("%-4d\t| %-25s\n", i, mode_desc[i]);

	PROC_PRINTF("\n");

	return SUCCESS;
}

int rtk_fc_proc_vlanParsingMode_write(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	int ret = len;
	uint32 tmp_hwnat_mode, tmp_flow_not_update_in_real_time;

	if(val)
		val = FC_VLAN_PARSING_MODE_OUTER_ONLY;
	else
		val = FC_VLAN_PARSING_MODE_DEFALT;

	if(fc_db.controlFuc.vlan_parsing_mode != val)
	{
		if(val == FC_VLAN_PARSING_MODE_OUTER_ONLY)
		{
			if(fc_db.controlFuc.bridge_5tuple_flow_accelerate_by_2tuple == 0)
			{
				WARNING("Please make sure bridge_5tuple_flow_accelerate_by_2tuple is set to 1");
				return ret;
			}
		}
		tmp_hwnat_mode = fc_db.controlFuc.hwnat_mode;
		fc_db.controlFuc.hwnat_mode = RT_FLOW_HWNAT_MODE_DIS_ACC; // disable learning
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(RT_FLOW_HWNAT_MODE_DIS_ACC);
		tmp_flow_not_update_in_real_time = fc_db.systemGlobal.flow_not_update_in_real_time;
		fc_db.systemGlobal.flow_not_update_in_real_time = DISABLED;

		rtk_fc_flow_flush();

		fc_db.controlFuc.vlan_parsing_mode = val;
		if(fc_db.controlFuc.vlan_parsing_mode == FC_VLAN_PARSING_MODE_OUTER_ONLY)
			aal_l3fe_glb_pp_inner_tpid_mask_set(0x0);	// disable inner vlan parsing
		else
			aal_l3fe_glb_pp_inner_tpid_mask_set(0x1);	// enable inner vlan parsing

		fc_db.systemGlobal.flow_not_update_in_real_time = tmp_flow_not_update_in_real_time;
		fc_db.controlFuc.hwnat_mode = tmp_hwnat_mode;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(tmp_hwnat_mode);
	}

	rtk_fc_proc_vlanParsingMode_get(NULL,NULL);
	return ret;
}
#endif
#endif

int rtk_fc_proc_bridge_5tuple_flow_accelerate_by_2tuple_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("bridge_5tuple_flow_accelerate_by_2tuple (UC):%d\n", fc_db.controlFuc.bridge_5tuple_flow_accelerate_by_2tuple);
	PROC_PRINTF("mc_5tuple_flow_accelerate_by_2tuple(MC):%d\n", fc_db.controlFuc.mc_5tuple_flow_accelerate_by_2tuple);	
	return SUCCESS;
}


int rtk_fc_proc_bridge_5tuple_flow_accelerate_by_2tuple_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	int ret = len;
	uint32 tmp_hwnat_mode, tmp_flow_not_update_in_real_time;
#if defined(CONFIG_RTK_L34_G3_PLATFORM) && defined(CONFIG_FC_CA8277B_SERIES)
	uint32 netif_idx = RTK_FC_TABLESIZE_INTF_ACC_ACL;
#endif

	if(fc_db.controlFuc.special_fast_forward_mode)
	{	
		printk("VXLAN/NPTv6 nic ACC enable, so disable 5tuple_flow_accelerate_by_2tuple.\n");
		return ret;
	}


	if(val)
		val = ENABLED;
	else
		val = DISABLED;

	if(fc_db.controlFuc.bridge_5tuple_flow_accelerate_by_2tuple != val)
	{
		tmp_hwnat_mode = fc_db.controlFuc.hwnat_mode;
		fc_db.controlFuc.hwnat_mode = RT_FLOW_HWNAT_MODE_DIS_ACC; // disable learning
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(RT_FLOW_HWNAT_MODE_DIS_ACC);
		tmp_flow_not_update_in_real_time = fc_db.systemGlobal.flow_not_update_in_real_time;
		fc_db.systemGlobal.flow_not_update_in_real_time = DISABLED;

#if defined(CONFIG_RTK_L34_G3_PLATFORM)

		if(val == ENABLED){
			assert_ok(_rtk_rg_aclAndCfReservedRuleAddSpecial(RTK_CA_CLS_TYPE_BRIDGE_5TUPLE_FLOW_ACCELERATE_BY_2TUPLE, NULL));
#if defined(CONFIG_FC_CA8277B_SERIES)
			assert_ok(_rtk_rg_aclAndCfReservedRuleAddSpecial(RTK_CA_CLS_TYPE_BRIDGE_5TUPLE_FLOW_ACCELERATE_BY_2TUPLE_GW_MAC_UPDATE, &netif_idx));
#endif
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
			if(RTK_FC_HELPER_WLAN_CLIENT_MODE_CB_IS_REGISTERED())
				assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_DHCPV4_TRAP, NULL));
#endif
		}else{
			assert_ok(_rtk_rg_aclAndCfReservedRuleDelSpecial(RTK_CA_CLS_TYPE_BRIDGE_5TUPLE_FLOW_ACCELERATE_BY_2TUPLE, NULL));
#if defined(CONFIG_FC_CA8277B_SERIES)
			assert_ok(_rtk_rg_aclAndCfReservedRuleDelSpecial(RTK_CA_CLS_TYPE_BRIDGE_5TUPLE_FLOW_ACCELERATE_BY_2TUPLE_GW_MAC_UPDATE, &netif_idx));
#endif
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
			if(RTK_FC_HELPER_WLAN_CLIENT_MODE_CB_IS_REGISTERED())
				assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_DHCPV4_TRAP));
#endif
		}
#else
		if(val == ENABLED)
		{
			RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_L2_FLOW_LOOKUP_BY_MAC, ENABLED);
		}
		else
		{
			RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_L2_FLOW_LOOKUP_BY_MAC, DISABLED);
		}
		fc_db.controlFuc.mc_5tuple_flow_accelerate_by_2tuple = fc_db.controlFuc.bridge_5tuple_flow_accelerate_by_2tuple;
#endif
		
		rtk_fc_flow_flush();
		fc_db.controlFuc.bridge_5tuple_flow_accelerate_by_2tuple = val;
		rtk_fc_abstrSwFlowPatternSet();

		fc_db.systemGlobal.flow_not_update_in_real_time = tmp_flow_not_update_in_real_time;
		fc_db.controlFuc.hwnat_mode = tmp_hwnat_mode;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(tmp_hwnat_mode);
	}
	rtk_fc_proc_bridge_5tuple_flow_accelerate_by_2tuple_get(NULL,NULL);
	return ret;
}

int rtk_fc_proc_mc_5tuple_flow_accelerate_by_2tuple_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int ret = len;
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	return rtk_fc_proc_bridge_5tuple_flow_accelerate_by_2tuple_set(filp,buff,len,data);
#else
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	val=val?TRUE:FALSE;

	if(fc_db.controlFuc.mc_5tuple_flow_accelerate_by_2tuple != val)
	{
		fc_db.controlFuc.mc_5tuple_flow_accelerate_by_2tuple=val;
		rtk_fc_abstrSwFlowPatternSet();
		assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP, NULL));
		rtk_fc_flow_tuple_init();
		assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP));
	}
#endif	

	return ret;
}


int rtk_fc_proc_bridge_2tuple_flow_ethtype_check_get(struct seq_file *s, void *v)
{
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	if(fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL]==FALSE)
		PROC_PRINTF("[disabled]\n");
	else
		PROC_PRINTF("[enable]\n");

	PROC_PRINTF("\nUsage:\n");
	PROC_PRINTF("     Enable 2tuple_flow to check ethertype or not\n");
	PROC_PRINTF("     e.g. echo 1 > /proc/fc/ctrl/bridge_2tuple_flow_ethtype_check \n");
#else
	int i = 0;
	
	if(fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL]==FALSE)
		PROC_PRINTF("[disabled]\n");
	else{
		for(i=0; i<RTK_FC_TABLESIZE_ETHERTYPE; i++) {
			if(fc_db.ethertypeTbl[i].valid) {
				PROC_PRINTF("sw[%d] hw[%d] 0x%04x ", i, fc_db.ethertypeTbl[i].hwIdx, fc_db.ethertypeTbl[i].ethType);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
				PROC_PRINTF("flow_ref[%d] acl_ref[%d]\n", fc_db.ethertypeTbl[i].flow_ref, fc_db.ethertypeTbl[i].acl_ref);
#else
				PROC_PRINTF("\n");
#endif
			}else
				PROC_PRINTF("sw[%d] N/A\n", i);
		}
	}
	
	PROC_PRINTF("\nUsage:\n");
	PROC_PRINTF("     set exist ethertype to clear, set new ethertype to add\n");
	PROC_PRINTF("     echo [ethertype] > /proc/fc/ctrl/bridge_2tuple_flow_ethtype_check \n");
	PROC_PRINTF("     [ethertype]:\n");
	PROC_PRINTF("       e.g. 0x0800, 0x8864, ... etc. \n");
#endif
	return SUCCESS;
}


int rtk_fc_proc_bridge_2tuple_flow_ethtype_check_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	uint8 oriState = fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL], newState;
	uint32 tmp_hwnat_mode;
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	newState = (val == 0)?FALSE:TRUE;

	if(oriState != newState)
	{
		// update FB_FLOW_CHECK_PATH12_PROTOCOL
		tmp_hwnat_mode = fc_db.controlFuc.hwnat_mode;
		fc_db.controlFuc.hwnat_mode = RT_FLOW_HWNAT_MODE_DIS_ACC;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(RT_FLOW_HWNAT_MODE_DIS_ACC);
		rtk_fc_flow_flush();

		fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL] = newState;

		rtk_fc_abstrSwFlowPatternSet();
		assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP, NULL));
		rtk_fc_flow_tuple_init();
		assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP));
		fc_db.controlFuc.hwnat_mode = tmp_hwnat_mode;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(tmp_hwnat_mode);	
	}
#else
	bool exist = FALSE;
	int i;
	uint16 ethertype = _rtk_fc_proc_parsing_string_to_integer(buff, len);
	uint8 oriState = fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL];
	uint32 tmp_hwnat_mode;

	if(ethertype<0x0600) {
		WARNING("0x%x not support, must > 0x0600", ethertype);
		return len;
	}

	tmp_hwnat_mode = fc_db.controlFuc.hwnat_mode;
	fc_db.controlFuc.hwnat_mode = RT_FLOW_HWNAT_MODE_DIS_ACC;
	RTK_FC_HELPER_MGR_HWNAT_MODE_SET(RT_FLOW_HWNAT_MODE_DIS_ACC);

	// find exist and clear
	for(i = 0; i < RTK_FC_TABLESIZE_ETHERTYPE; i++) {
		if(fc_db.ethertypeTbl[i].valid &&
			(fc_db.ethertypeTbl[i].ethType == ethertype)) {
			
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			if(fc_db.ethertypeTbl[i].flow_ref && fc_db.ethertypeTbl[i].acl_ref) {
				// clear	
				rtk_fc_flow_flush();				// no necessary to flush mc flow.
				WARNING("ethtype 0x%x is referenced by ACL, we must keep it in hw entry", ethertype);
				fc_db.ethertypeTbl[i].flow_ref=FALSE;
				goto REFRESH;
			}
			else if(!fc_db.ethertypeTbl[i].flow_ref && fc_db.ethertypeTbl[i].acl_ref) {exist = TRUE;
				// set		
				rtk_fc_flow_flush();				// no necessary to flush mc flow.
				//RTK_RG_ASIC_ETHTYPE_ADD(i, ethertype);
				fc_db.ethertypeTbl[i].flow_ref=TRUE;
				exist = TRUE;
				goto REFRESH;
			}else
#endif
			{
				// clear		
				rtk_fc_flow_flush();				// no necessary to flush mc flow.
				RTK_RG_ASIC_ETHTYPE_DEL(i);
				exist = TRUE;
				goto REFRESH;
			}
		}
	}

	// find empty and set
	if(!exist) {
		for(i = 0; i < RTK_FC_TABLESIZE_ETHERTYPE; i++) {
			if(fc_db.ethertypeTbl[i].valid) 
				continue;

			// set		
			rtk_fc_flow_flush();					// no necessary to flush mc flow.
			RTK_RG_ASIC_ETHTYPE_ADD(i, ethertype);
			fc_db.ethertypeTbl[i].flow_ref=TRUE;
			exist = TRUE;
			goto REFRESH;
		}
	}

	if(!exist) {
		fc_db.controlFuc.hwnat_mode = tmp_hwnat_mode;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(tmp_hwnat_mode);
		WARNING("table full, please delete one or request to enlarge table size ..");
		return len;
	}

REFRESH:
	fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL] = FALSE;
	for(i = 0; i < RTK_FC_TABLESIZE_ETHERTYPE; i++) {
		if(fc_db.ethertypeTbl[i].valid && fc_db.ethertypeTbl[i].flow_ref) {
			fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL] = TRUE;
			break;
		}
	}

	if(oriState != fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL]) {
		rtk_fc_abstrSwFlowPatternSet();
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP, NULL));
		rtk_fc_flow_tuple_init();
		assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP));
#endif
	}
	fc_db.controlFuc.hwnat_mode = tmp_hwnat_mode;
	RTK_FC_HELPER_MGR_HWNAT_MODE_SET(tmp_hwnat_mode);
#endif

	return len;
}


int rtk_fc_proc_bc_hwflow_accelerate_get(struct seq_file *s, void *v)
{
//	PROC_PRINTF("bcDomain_hwflow_accelerate:0x%x  (bit'0:BC   bit'1:L2MC  bit'2:UUC)\n", fc_db.controlFuc.bcDomain_hwflow_accelerate);
	PROC_PRINTF("bcDomain_hwflow_accelerate:0x%x  (bit'0:BC   bit'1:L2MC )\n", fc_db.controlFuc.bcDomain_hwflow_accelerate);
	PROC_PRINTF("BC_ACC:0x%x\n", fc_db.controlFuc.bcDomain_hwflow_accelerate&BC_ACC?1:0);
	PROC_PRINTF("L2MC_ACC:0x%x\n", fc_db.controlFuc.bcDomain_hwflow_accelerate&L2MC_ACC?1:0);
	//PROC_PRINTF("UUC_ACC:0x%x (UUC not support now)\n", fc_db.controlFuc.bcDomain_hwflow_accelerate&UUC_ACC?1:0);		
	return SUCCESS;
}


int rtk_fc_proc_bc_hwflow_accelerate_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	int ret = len;
	int retval;

	if(fc_db.controlFuc.bcDomain_hwflow_accelerate != val)
	{		
		//rtk_fc_flow_delete_by_l2Idx();
		fc_db.controlFuc.bcDomain_hwflow_accelerate = val;
	}
	if(val && fc_db.bcMacIdx==FAIL)
	{
		uint8 bcMac[ETH_ALEN]={0xff,0xff,0xff,0xff,0xff,0xff};
		retval=_rtk_fc_cpuSpa_macAdd(bcMac,TRUE,&fc_db.bcMacIdx);
	}
	else if(val==0)
	{
		_rtk_fc_lut_staticEntry_del((uint32)fc_db.bcMacIdx);
		fc_db.bcMacIdx=FAIL;
	}

	rtk_fc_flow_flush();
	//rtk_fc_abstrSwFlowPatternSet should be called after fc_db.controlFuc.bcDomain_hwflow_accelerate assigned
	rtk_fc_abstrSwFlowPatternSet();	
	
	rtk_fc_proc_bc_hwflow_accelerate_get(NULL,NULL);
	return ret;
}


#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
int rtk_fc_proc_dsliteHwAcceleration_disable_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.dsliteHwAcceleration_disable?1:0);
	return SUCCESS;
}


int rtk_fc_proc_dsliteHwAcceleration_disable_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	if(val)
		val = ENABLED;
	else
		val = DISABLED;

	if(fc_db.controlFuc.dsliteHwAcceleration_disable != val)
	{
		rtk_fc_flow_flush();
		fc_db.controlFuc.dsliteHwAcceleration_disable = val;
	}
	rtk_fc_proc_dsliteHwAcceleration_disable_get(NULL,NULL);
	return len;
}
#endif

int rtk_fc_proc_dsliteV6TosFromV4_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.dsliteV6TosFromV4?1:0);
	return SUCCESS;
}


int rtk_fc_proc_dsliteV6TosFromV4_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	if(val)
		val = ENABLED;
	else
		val = DISABLED;

	if(fc_db.controlFuc.dsliteV6TosFromV4 != val)
	{
		rtk_fc_flow_flush();
		fc_db.controlFuc.dsliteV6TosFromV4 = val;	
	}
	rtk_fc_proc_dsliteV6TosFromV4_get(NULL,NULL);
	return len;
}



int rtk_fc_proc_v6RDSwAcceleration_disable_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.controlFuc.v6RDSwAcceleration_disable?1:0);
	return SUCCESS;
}


int rtk_fc_proc_v6RDSwAcceleration_disable_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);
	if(val)
		val = ENABLED;
	else
		val = DISABLED;

	if(fc_db.controlFuc.v6RDSwAcceleration_disable != val)
	{
		rtk_fc_flow_flush();
		fc_db.controlFuc.v6RDSwAcceleration_disable = val;
	}

	if(fc_db.controlFuc.v6RDSwAcceleration_disable)
	{
		int i;
		for(i = 0; i < RTK_FC_TABLESIZE_INTF_SW; i++)
		{
			if(fc_db.netifTbl[i].dualType == RTK_FC_DUALTYPE_6RD)
			{
				rtk_fc_flow_delete(fc_db.netifTbl[i].outerHdrFlowIdx);
				fc_db.netifTbl[i].outerHdrFlowIdx = SIGNED_INVALID;
				fc_db.netifTbl[i].outerHdrExtratagIdx = SIGNED_INVALID;
			}
		}
	}

	rtk_fc_proc_v6RDSwAcceleration_disable_get(NULL,NULL);
	return len;
}

#if defined(CONFIG_RTK_PE)
int rtk_fc_proc_skbmark_pe_aware_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_pe_aware_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_PE_AWARE);
	return count;
}
#endif

int rtk_fc_proc_skbmark_streamId_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_streamId_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_STREAMID);
	RTK_FC_HELPER_MGR_SKBMARK_CONF_SET(FC_MGR_SKBMARK_STREAMID, fc_db.skbmark[RTK_FC_SKBMARK_STREAMID]);
	return count;
}

int rtk_fc_proc_skbmark_streamId_enable_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;

}

int rtk_fc_proc_skbmark_streamId_enable_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_STREAMID_EN);
	RTK_FC_HELPER_MGR_SKBMARK_CONF_SET(FC_MGR_SKBMARK_STREAMID_EN, fc_db.skbmark[RTK_FC_SKBMARK_STREAMID_EN]);
	return count;
}

int rtk_fc_proc_skbmark_swshaper_enable_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;

}

int rtk_fc_proc_skbmark_swshaper_enable_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SWSHAPER_EN);
	return count;
}


int rtk_fc_proc_skbmark_psFloodFwdAcc_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_psFloodFwdAcc_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_PSFLOODFWDACC);
	return count;
}


int rtk_fc_proc_skbmark_meterId_enable_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;

}

int rtk_fc_proc_skbmark_meterId_enable_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_METERIDX_EN);
	return count;
}

int rtk_fc_proc_skbmark_meterId_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;

}

int rtk_fc_proc_skbmark_meterId_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_METERIDX);
	return count;
}

int rtk_fc_proc_l2tp_v6_en_hwacc_get(struct seq_file *s, void *v)
{
	
	PROC_PRINTF("[%s] L2TP outer v6 hardware acceleration.\n",fc_db.controlFuc.en_l2tp_v6_hwacc?"Enable":"Disable");
	return SUCCESS;
}

int rtk_fc_proc_l2tp_v6_en_hwacc_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(val == 1)
		fc_db.controlFuc.en_l2tp_v6_hwacc = 1;
	else
		fc_db.controlFuc.en_l2tp_v6_hwacc = 0;

	return count;
}

int rtk_fc_proc_pptp_v6_en_hwacc_get(struct seq_file *s, void *v)
{
	
	PROC_PRINTF("[%s] PPTP inner v6 software acceleration.\n",fc_db.controlFuc.en_pptp_v6_hwacc?"Disable":"Enable");
	return SUCCESS;
}

int rtk_fc_proc_pptp_v6_en_hwacc_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(val == 1)
		fc_db.controlFuc.en_pptp_v6_hwacc = 1;
	else
		fc_db.controlFuc.en_pptp_v6_hwacc = 0;

	return count;
}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if !defined(CONFIG_FC_RTL9607F_SERIES)
//TODO: 9607F flow cache mib
int rtk_fc_proc_skbmark_flow_cache_mib_enable_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;

}

int rtk_fc_proc_skbmark_flow_cache_mib_enable_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_FLOW_CACHE_MIB_EN);
	return count;
}
#endif
int rtk_fc_proc_skbmark_mapet_fmr_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_mapet_fmr_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_MAPET_FMR);
	return count;
}

int rtk_fc_proc_vxlan_v6_en_hwacc_get(struct seq_file *s, void *v)
{
	
	PROC_PRINTF("VXLAN outer v6 %s hardward acceleration.\n",fc_db.controlFuc.en_vxlan_v6_hwacc?"enable":"disable");
	return SUCCESS;
}

int rtk_fc_proc_vxlan_v6_en_hwacc_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	if(val == 1)
		fc_db.controlFuc.en_vxlan_v6_hwacc = 1;
	else
		fc_db.controlFuc.en_vxlan_v6_hwacc = 0;

	return count;
}


#endif

int rtk_fc_proc_skbmark_mibId_enable_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;

}

int rtk_fc_proc_skbmark_mibId_enable_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_MIBIDX_EN);
	return count;
}

int rtk_fc_proc_skbmark_mibId_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_mibId_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_MIBIDX);
	return count;
}

int rtk_fc_proc_skbmark_mib2Id_enable_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_mib2Id_enable_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_MIB2IDX_EN);
#else
	WARNING("Not support!");
#endif
	return count;
}

int rtk_fc_proc_skbmark_mib2Id_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_mib2Id_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_MIB2IDX);
#else
	WARNING("Not support!");
#endif
	return count;
}

int rtk_fc_proc_skbmark_qid_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_qid_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_QID);
	RTK_FC_HELPER_MGR_SKBMARK_CONF_SET(FC_MGR_SKBMARK_QID, fc_db.skbmark[RTK_FC_SKBMARK_QID]);
	return count;
}

int rtk_fc_proc_skbmark_fwdByPs_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_fwdByPs_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_FWDBYPS);
	return count;
}

int rtk_fc_proc_skbmark_skipFcFunc_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_skipFcFunc_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SKIPFCFUNC);
	RTK_FC_HELPER_MGR_SKBMARK_CONF_SET(FC_MGR_SKBMARK_SKIPFCFUNC, fc_db.skbmark[RTK_FC_SKBMARK_SKIPFCFUNC]);
	return count;
}

int rtk_fc_proc_skbmark_wanDsLoopback_en_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_wanDsLoopback_en_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_WANDSLOOPBACK_EN);
	return count;
}

int rtk_fc_proc_skbmark_swIntfIdx_assign_en_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_swIntfIdx_assign_en_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SWINTFIDX_ASSIGN_EN);
	return count;
}

int rtk_fc_proc_skbmark_swIntfIdx_assign_inOut_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_swIntfIdx_assign_inOut_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SWINTFIDX_ASSIGN_INOUT);
	return count;
}

int rtk_fc_proc_skbmark_swIntfIdx_assign_idx_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_swIntfIdx_assign_idx_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SWINTFIDX_ASSIGN_IDX);
	return count;
}

int rtk_fc_proc_skbmark_skip_fc_alg_check_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_skip_fc_alg_check_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SKIP_FC_ALG_CHECK);
	return count;
}

int rtk_fc_proc_skbmark_forceIntpriToWifiPri_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();
	return SUCCESS;
}

int rtk_fc_proc_skbmark_forceIntpriToWifiPri_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_FORCEINTPRITOWIFIPRI);
	return count;
}


#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
int rtk_fc_proc_skbmark_cvlan_vid_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;
}

int rtk_fc_proc_skbmark_cvlan_vid_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_CVLAN_ID);
	return count;
}
int rtk_fc_proc_skbmark_cvlan_pri_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;
}

int rtk_fc_proc_skbmark_cvlan_pri_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_CVLAN_PRI);
	return count;
}
int rtk_fc_proc_skbmark_cvlan_action_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;
}

int rtk_fc_proc_skbmark_cvlan_action_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_CVLAN_ACTION);
	return count;
}

int rtk_fc_proc_skbmark_svlan_vid_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;

}

int rtk_fc_proc_skbmark_svlan_vid_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SVLAN_ID);
	return count;
}
int rtk_fc_proc_skbmark_svlan_pri_get(struct seq_file *s, void *v)
{

	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;

}

int rtk_fc_proc_skbmark_svlan_pri_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SVLAN_PRI);
	return count;
}
int rtk_fc_proc_skbmark_svlan_action_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;
}

int rtk_fc_proc_skbmark_svlan_action_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SVLAN_ACTION);
	return count;
}
int rtk_fc_proc_skbmark_svlan_tpid_get(struct seq_file *s, void *v)
{
	_rtk_fc_remarkBitsStatus_display();
	_rtk_fc_proc_skbmark_usage();

	return SUCCESS;
}

int rtk_fc_proc_skbmark_svlan_tpid_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	_rtk_fc_skbmark_setting_decision(filp, buff, count, data, RTK_FC_SKBMARK_SVLAN_TPID);
	return count;
}

#endif

void _rtk_fc_proc_hwnat_mode_selection_usage(struct seq_file *s, void *v)
{
	int i = 0;
	
	PROC_PRINTF("\nUsage:\n");
	PROC_PRINTF("echo [mode] > /proc/fc/ctrl/hwnat\n");
	PROC_PRINTF("mode | description\n");
	PROC_PRINTF("----------------------------------------------------------\n");
	for(i = 0; i < RT_FLOW_HWNAT_MODE_END; i++) {
		PROC_PRINTF("%-4d | %s \n", i, name_of_hwnat_mode[i]);
	}

}
void _rtk_fc_proc_hwnat_mode_selection_display(struct seq_file *s, void *v)
{
	if(fc_db.controlFuc.hwnat_mode < RT_FLOW_HWNAT_MODE_END)
		PROC_PRINTF("%d: %s\n", fc_db.controlFuc.hwnat_mode, name_of_hwnat_mode[fc_db.controlFuc.hwnat_mode]);
	else if (fc_db.controlFuc.hwnat_mode == RT_FLOW_HWNAT_MODE_SPEC_TEST)
		PROC_PRINTF("%d: block ALL netlink events and fc timers\n", fc_db.controlFuc.hwnat_mode);
}

int rtk_fc_proc_hwnat_mode_selection_get(struct seq_file *s, void *v)
{
	_rtk_fc_proc_hwnat_mode_selection_display(s, v);
	_rtk_fc_proc_hwnat_mode_selection_usage(s, v);
	return SUCCESS;
}

int rtk_fc_proc_hwnat_mode_selection_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	int idx = _rtk_fc_proc_parsing_string_to_integer(buff, count);

	// backward compatible
	if(idx == 100) idx = RT_FLOW_HWNAT_MODE_SPEC_TEST;
	
	if(idx >= RT_FLOW_HWNAT_MODE_END && idx != RT_FLOW_HWNAT_MODE_SPEC_TEST)
		goto ERR;

	if(idx == RT_FLOW_HWNAT_MODE_TRAP_ALL){	//trap by acl to make sure sw only
		assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP, NULL));
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		assert_ok(rtk_fc_asic_l2te_policer_enable_set(DISABLED));	//make sure not drop by L2 storm ctrl
#endif
	}else{
		assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP));
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		assert_ok(rtk_fc_asic_l2te_policer_enable_set(ENABLED));
#endif
	}

	
	fc_db.controlFuc.hwnat_mode = idx;
	RTK_FC_HELPER_MGR_HWNAT_MODE_SET(idx);
	
	if(idx!=RT_FLOW_HWNAT_MODE_SPEC_TEST)
		rtk_fc_flow_flush();//clear all flow

ERR:
	_rtk_fc_proc_hwnat_mode_selection_display(NULL, NULL);

	return count;
}
	



#if defined(CONFIG_SMP)

static void _rtk_fc_proc_dispatch_mode_usage(struct seq_file *s, int dispatch_point)
{
	PROC_PRINTF("\n====================\n");
	PROC_PRINTF("Usage:\n");

	if(dispatch_point == RTK_FC_MGR_DISPATCH_NIC_RX){
		PROC_PRINTF("\033[1;37;44m%-9s\033[0m%s\n", "NIC RX", ": echo [highpri] mode DISPATCH_MODE [OPTION] > /proc/fc/ctrl/smp_dispatch_nic_rx");
		PROC_PRINTF("%-9s  - e.g.  echo mode 1 cpu 2 > /proc/fc/ctrl/smp_dispatch_nic_rx\n", "");
		PROC_PRINTF("%-9s  - e.g2. echo highpri mode 1 cpu 3 > /proc/fc/ctrl/smp_dispatch_nic_rx\n", "");
		PROC_PRINTF("%-9s  - e.g3. echo mode 5 > /proc/fc/ctrl/smp_dispatch_nic_rx\n", "");
		PROC_PRINTF("%-9s  - e.g4. Dispatch to CPU 1, 2, 3, CPU_MASK = 2'b1110 (0xE), ***Only valid when NIC-RX using one cpu to interrupt***.\n", "");
		PROC_PRINTF("%-9s           echo mode 2 cpu 0xe > /proc/fc/ctrl/smp_dispatch_nic_rx\n", "");
		PROC_PRINTF("%-9s  - e.g5. Port 0 dispatch to CPU 1, port 1 dispatch to CPU 2, port 2 dispatch to CPU 3\n", "");
		PROC_PRINTF("%-9s           echo mode 6 port 0 cpu 1 port 1 cpu 2 port 2 cpu 3  > /proc/fc/ctrl/smp_dispatch_nic_rx\n", "");
	}else if(dispatch_point == RTK_FC_MGR_DISPATCH_NIC_TX){
		PROC_PRINTF("\033[1;37;44m%-9s\033[0m%s\n", "NIC TX", ": echo mode DISPATCH_MODE [OPTION] > /proc/fc/ctrl/smp_dispatch_nic_tx");
		PROC_PRINTF("%-9s  - e.g.  echo mode 1 cpu 2 > /proc/fc/ctrl/smp_dispatch_nic_tx\n", "");
	}else if(dispatch_point == RTK_FC_MGR_DISPATCH_WLAN0_RX){
		PROC_PRINTF("\033[1;37;44m%-9s\033[0m%s\n", "WIFI RX", ": echo wlan WLAN_IDX mode DISPATCH_MODE [OPTION] > /proc/fc/ctrl/smp_dispatch_wifi_rx");
		PROC_PRINTF("%-9s  - e.g.  wlan 0 echo mode 1 cpu 2 > /proc/fc/ctrl/smp_dispatch_wifi_rx\n", "");
	}else if(dispatch_point == RTK_FC_MGR_DISPATCH_WLAN0_TX){
		PROC_PRINTF("\033[1;37;44m%-9s\033[0m%s\n", "WIFI TX", ": echo wlan WLAN_IDX mode DISPATCH_MODE [OPTION] > /proc/fc/ctrl/smp_dispatch_wifi_tx");
		PROC_PRINTF("%-9s  - e.g.  echo wlan 0 mode 1 cpu 2 > /proc/fc/ctrl/smp_dispatch_wifi_tx\n", "");
		PROC_PRINTF("%-9s  - e.g2. echo wlan 0 mode 3 cpu 0xf > /proc/fc/ctrl/smp_dispatch_wifi_tx\n", "");
	}
	else if(dispatch_point == RTK_FC_MGR_DISPATCH_PS_NETIF_RX){
		PROC_PRINTF("\033[1;37;44m%-9s\033[0m%s\n", "NIC TX", ": echo mode DISPATCH_MODE [OPTION] > /proc/fc/ctrl/smp_dispatch_ps_netif_rx");
		PROC_PRINTF("%-9s  - e.g.  echo mode 1 cpu 2 > /proc/fc/ctrl/smp_dispatch_ps_netif_rx\n", "");
	}
	PROC_PRINTF("--------------------\n");
	if((dispatch_point == RTK_FC_MGR_DISPATCH_WLAN0_RX) || (dispatch_point == RTK_FC_MGR_DISPATCH_WLAN0_TX))
		PROC_PRINTF("%-15s: 0 - %d\n", "WLAN_IDX", WLAN_IFACE_NUM_MAX-1);

	PROC_PRINTF("%-15s: \n", "DISPATCH_MODE");
	PROC_PRINTF("%-15s	%d: %s\n", "", RTK_FC_DISPATCH_MODE_NONE, name_of_dispatch_mode[0]);

	PROC_PRINTF("%-15s	%d: %s, the operation will be dispatched to the CPU_CORE you assigned\n", "", RTK_FC_DISPATCH_MODE_IPI, name_of_dispatch_mode[RTK_FC_DISPATCH_MODE_IPI]);
	PROC_PRINTF("%-15s	   [OPTION]: cpu CPU_CORE\n", "");

	if(dispatch_point == RTK_FC_MGR_DISPATCH_NIC_RX){
		PROC_PRINTF("%-15s	%d: %s, the operation will be dispatched to the CPU according to the packet 5-tuple hash \n", "", RTK_FC_DISPATCH_MODE_RPS, name_of_dispatch_mode[RTK_FC_DISPATCH_MODE_RPS]);
		PROC_PRINTF("%-15s	   [OPTION]: cpu CPU_MASK\n", "");

	}
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	if(dispatch_point == RTK_FC_MGR_DISPATCH_WLAN0_TX){
		PROC_PRINTF("%-15s	%d: %s, the operation will be dispatched to the CPUs according to CPU_MASK\n", "", RTK_FC_DISPATCH_MODE_SMP_BY_HASH, name_of_dispatch_mode[RTK_FC_DISPATCH_MODE_SMP_BY_HASH]);
		PROC_PRINTF("%-15s	   [OPTION]: cpu CPU_MASK\n", "");
		PROC_PRINTF("%-15s	   note. implement by \033[1;37;44mHW\033[0m, and the user need to set smp_affinity at the same time\n", "");

		PROC_PRINTF("%-15s	%d: %s, the operation will be dispatched to the CPUs according to CPU_MASK\n", "", RTK_FC_DISPATCH_MODE_SMP_BY_RR, name_of_dispatch_mode[RTK_FC_DISPATCH_MODE_SMP_BY_RR]);
		PROC_PRINTF("%-15s	   [OPTION]: cpu CPU_MASK\n", "");
		PROC_PRINTF("%-15s	   note. implement by \033[1;37;44mHW\033[0m, and the user need to set smp_affinity at the same time\n", "");
	}
	if(dispatch_point == RTK_FC_MGR_DISPATCH_NIC_RX){
		PROC_PRINTF("%-15s	%d: %s, the operation will be dispatched to the CPUs according to the IP last bit\n", "", RTK_FC_DISPATCH_MODE_SMP_BY_ACL, name_of_dispatch_mode[RTK_FC_DISPATCH_MODE_SMP_BY_ACL]);
		PROC_PRINTF("%-15s	   note. implement by \033[1;37;44mHW\033[0m, the user need to set smp_affinity, and highpri mode will be disable at the same time\n", "");
	}
#endif
	if(dispatch_point == RTK_FC_MGR_DISPATCH_NIC_RX){
		PROC_PRINTF("%-15s	%d: %s, dispatch packet to the CPUs you assigned according to ingress port\n", "", RTK_FC_DISPATCH_MODE_SMP_BY_PORT, name_of_dispatch_mode[RTK_FC_DISPATCH_MODE_SMP_BY_PORT]);
		PROC_PRINTF("%-15s	   [OPTION]: cpu CPU_CORE port PORT_NUMBER\n", "");
	}

}

int rtk_fc_proc_smp_dispatch_nic_rx_get(struct seq_file *s, void *v)
{
	rtk_fc_dispatch_mode_t rx_dispatch_mode;

	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, RTK_FC_MGR_DISPATCH_NIC_RX);
	PROC_PRINTF("Mode: %d (%s) ", rx_dispatch_mode.mode, name_of_dispatch_mode[rx_dispatch_mode.mode]);
	if((rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE) || (rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_ACL))
		PROC_PRINTF("\n");
	else if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_RPS)
		PROC_PRINTF("\n");
	else if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_PORT)
		PROC_PRINTF("SMP dispatch by port\n");
	else
		PROC_PRINTF("cpu:%d\n", rx_dispatch_mode.smp_id);

	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, RTK_FC_MGR_DISPATCH_HIGHPRI_NIC_RX);
	PROC_PRINTF("Mode: %d (%s) ", rx_dispatch_mode.mode, name_of_dispatch_mode[rx_dispatch_mode.mode]);
	if((rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE) || (rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_ACL))
		PROC_PRINTF(" (highpri)\n");
	else if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_RPS)
		PROC_PRINTF("cpu bit mask:%x\n", rx_dispatch_mode.smp_id);
	else if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_PORT)
		PROC_PRINTF("SMP dispatch by port\n");
	else
		PROC_PRINTF("cpu:%d (highpri)\n", rx_dispatch_mode.smp_id);
	
	_rtk_fc_proc_dispatch_mode_usage(s, RTK_FC_MGR_DISPATCH_NIC_RX);

	return SUCCESS;
}

int rtk_fc_proc_smp_dispatch_nic_rx_set(struct file *filp, const char *buff,unsigned long count, void *data )
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	rtk_fc_dispatch_point_t dispatch_point = RTK_FC_MGR_DISPATCH_NIC_RX;

	if (buff)
	{
		char *strptr,*split_str;
		rtk_fc_dispatch_mode_t rx_dispatch_mode;
		rtk_fc_dispatch_mode_mask_t mask=RTK_FC_MGR_DISPATCH_MASK_NONE;
		int port_cpu_setting[RTK_FC_MAC_PORT_MAX];
		
		rx_dispatch_mode.mode = RTK_FC_DISPATCH_MODE_END;
		rx_dispatch_mode.smp_id = 0;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;
		while(1)
		{
			split_str=strsep(&strptr," ");
			if(strcasecmp(split_str,"highpri")==0)
			{
				dispatch_point = RTK_FC_MGR_DISPATCH_HIGHPRI_NIC_RX;
			}
			else if(strcasecmp(split_str,"mode")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
					rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!\n");
				else
				{
					rx_dispatch_mode.mode = simple_strtol(split_str, NULL, 0);
					if((1 << rx_dispatch_mode.mode) & RTK_FC_DISPATCH_MODEMSK_NIC_RX)
						mask |= RTK_FC_MGR_DISPATCH_MASK_MODE;
					else
						rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!\n");
				}

				if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_PORT)
				{
					int i = 0;
					for(i = 0;i < RTK_FC_MAC_PORT_MAX; i++)
					{
						port_cpu_setting[i] = -1;
					}
					
					while(1)
					{
						split_str=strsep(&strptr," ");
						if(split_str==NULL)
						{
							goto SET_DISPATCH;
						}
						else if(strcasecmp(split_str,"port")==0)
						{
							split_str=strsep(&strptr," ");
							if(split_str==NULL)
							{
								rtlglue_printf("[ERROR]Invalid RPS port bit mask!\n");
								return -EFAULT;
							}
							else
							{
								int port = 0;
								int cpu = 0;
								
								port  = simple_strtol(split_str, NULL, 0);
								if(port >= RTK_FC_MAC_PORT_MAX)
								{
									rtlglue_printf("[ERROR]Error port setting!\n");
									return -EFAULT;

								}
								
								split_str=strsep(&strptr," ");
								
								if(strcasecmp(split_str,"cpu")==0)
								{
									split_str=strsep(&strptr," ");
									cpu = simple_strtol(split_str, NULL, 0);
									if(cpu > NR_CPUS)
									{
										rtlglue_printf("[ERROR]None support CPU!\n");
										return -EFAULT;

									}
									port_cpu_setting[port] = cpu;
									smp_mb();
								}
								else
								{
									rtlglue_printf("[ERROR]Wrong parameter\n");
									return -EFAULT;
								}	
								
							}
						}
						else
						{
							// RESET
							rtlglue_printf("[ERROR]echo 6 port {PORT} cpu {CPU} to this proc\n");
								
							goto SET_DISPATCH;
						}
					}

				}
				else if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_IPSEC_SC)
				{
					int i = 0;
					for(i = 0;i < RTK_FC_MAC_PORT_MAX; i++)
					{
						port_cpu_setting[i] = -1;
					}
					
					while(1)
					{
						split_str=strsep(&strptr," ");
						if(split_str==NULL)
						{
							goto SET_DISPATCH;
						}
						else if(strcasecmp(split_str,"port")==0)
						{
							split_str=strsep(&strptr," ");
							if(split_str==NULL)
							{
								rtlglue_printf("[ERROR]Invalid RPS port bit mask!\n");
								return -EFAULT;
							}
							else
							{
								int port = 0;
								int cpu = 0;
								
								port  = simple_strtol(split_str, NULL, 0);
								if(port >= RTK_FC_MAC_PORT_MAX)
								{
									rtlglue_printf("[ERROR]Error port setting!\n");
									return -EFAULT;

								}
								
								split_str=strsep(&strptr," ");
								
								if(strcasecmp(split_str,"cpu")==0)
								{
									split_str=strsep(&strptr," ");
									cpu = simple_strtol(split_str, NULL, 0);
									if(cpu > NR_CPUS)
									{
										rtlglue_printf("[ERROR]None support CPU!\n");
										return -EFAULT;

									}
									port_cpu_setting[port] = cpu;
									smp_mb();
								}
								else
								{
									rtlglue_printf("[ERROR]Wrong parameter\n");
									return -EFAULT;
								}	
								
							}
						}
						else if(strcasecmp(split_str,"rps")==0)
						{
							split_str=strsep(&strptr," ");
							if(split_str==NULL)
								rtlglue_printf("[ERROR] invalid smp_id. CPU smp_id is not changed!");
							else
							{
								rx_dispatch_mode.smp_id  = simple_strtol(split_str, NULL, 0);
								mask |= RTK_FC_MGR_DISPATCH_MASK_CPU;
							}
						}
						else
						{
							// RESET
							rtlglue_printf("[ERROR]echo 6 port {PORT} cpu {CPU} to this proc\n");
								
							goto SET_DISPATCH;
						}
					}

				}
			}
			else if(strcasecmp(split_str,"cpu")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
					rtlglue_printf("[ERROR] invalid smp_id. CPU smp_id is not changed!");
				else
				{
					rx_dispatch_mode.smp_id  = simple_strtol(split_str, NULL, 0);
					mask |= RTK_FC_MGR_DISPATCH_MASK_CPU;
				}
			}
			
			if (strptr==NULL) break;
		}

SET_DISPATCH:
		if(dispatch_point == RTK_FC_MGR_DISPATCH_NIC_RX){
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(RTK_FC_MAINCPU_ONE_CPU_PORT)
			if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_ACL) {
				rtlglue_printf("[ERROR] not support.\n");
				return len;
			}
#elif defined(CONFIG_FC_CA8277AB_SERIES)
			if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_ACL)
				assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_SMP_DISPATCH_NIC_RX_BY_ACL, NULL));
			else
				assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_SMP_DISPATCH_NIC_RX_BY_ACL));
#else
			L3FE_GLB_LDPID_REMAP_CTRL_t remap;
			remap.wrd = rtk_ne_reg_read(L3FE_GLB_LDPID_REMAP_CTRL);
			if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_ACL)
				remap.bf.flw_mis_en = 1;
			else
				remap.bf.flw_mis_en = 0;				
			rtk_ne_reg_write(remap.wrd, L3FE_GLB_LDPID_REMAP_CTRL);
#endif
#endif
		}else if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_ACL){
			rtlglue_printf("[ERROR] fail to enable smp dispatch by acl due to only support nic rx.\n");
			return len;
		}
		
		RTK_FC_HELPER_MGR_DISPATCH_MODE_SET(rx_dispatch_mode, mask, dispatch_point, port_cpu_setting);
		RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, dispatch_point);
		if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_RPS || rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_PORT)
			rtlglue_printf("Dispatch mode: %d\n", rx_dispatch_mode.mode);
		else
			rtlglue_printf("Dispatch mode: %d, cpu: %d\n", rx_dispatch_mode.mode, rx_dispatch_mode.smp_id);

		if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_ACL){
			RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, RTK_FC_MGR_DISPATCH_HIGHPRI_NIC_RX);
			if(rx_dispatch_mode.mode != RTK_FC_DISPATCH_MODE_NONE){
				rx_dispatch_mode.mode = RTK_FC_DISPATCH_MODE_NONE;
				RTK_FC_HELPER_MGR_DISPATCH_MODE_SET(rx_dispatch_mode, RTK_FC_MGR_DISPATCH_MASK_MODE, RTK_FC_MGR_DISPATCH_HIGHPRI_NIC_RX, NULL);
				rtlglue_printf("Also disable smp dispatch highpri nic rx\n\n");
			}
		}

		return len;
	}
	_rtk_fc_proc_dispatch_mode_usage(NULL, RTK_FC_MGR_DISPATCH_NIC_RX);

	return len;
}

int rtk_fc_proc_smp_dispatch_nic_tx_get(struct seq_file *s, void *v)
{
	rtk_fc_dispatch_mode_t tx_dispatch_mode;

	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&tx_dispatch_mode, RTK_FC_MGR_DISPATCH_NIC_TX);
	
	{
		PROC_PRINTF("Mode: %d (%s) ", tx_dispatch_mode.mode, name_of_dispatch_mode[tx_dispatch_mode.mode]);
		if(tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
			PROC_PRINTF("\n");
		else
			PROC_PRINTF("cpu:%d\n", tx_dispatch_mode.smp_id);
	}
	
	_rtk_fc_proc_dispatch_mode_usage(s, RTK_FC_MGR_DISPATCH_NIC_TX);

	return SUCCESS;
}

int rtk_fc_proc_smp_dispatch_nic_tx_set(struct file *filp, const char *buff,unsigned long count, void *data )
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;

	if (buff)
	{
		char *strptr,*split_str;
		rtk_fc_dispatch_mode_t tx_dispatch_mode;
		rtk_fc_dispatch_mode_mask_t mask=RTK_FC_MGR_DISPATCH_MASK_NONE;
		
		tx_dispatch_mode.mode = RTK_FC_DISPATCH_MODE_END;
		tx_dispatch_mode.smp_id = 0;
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;
		while(1)
		{
			split_str=strsep(&strptr," ");
			if(strcasecmp(split_str,"mode")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
					rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!");
				else
				{
					tx_dispatch_mode.mode = simple_strtol(split_str, NULL, 0);
					if((1 << tx_dispatch_mode.mode) & RTK_FC_DISPATCH_MODEMSK_NIC_TX)
						mask |= RTK_FC_MGR_DISPATCH_MASK_MODE;
					else
						rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!");
				}
			}
			else if(strcasecmp(split_str,"cpu")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
					rtlglue_printf("[ERROR] invalid smp_id. CPU smp_id is not changed!");
				else
				{
					tx_dispatch_mode.smp_id  = simple_strtol(split_str, NULL, 0);
					mask |= RTK_FC_MGR_DISPATCH_MASK_CPU;
				}
			}

			if (strptr==NULL) break;
		}
		
		{
			RTK_FC_HELPER_MGR_DISPATCH_MODE_SET(tx_dispatch_mode, mask, RTK_FC_MGR_DISPATCH_NIC_TX, NULL);
			RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&tx_dispatch_mode, RTK_FC_MGR_DISPATCH_NIC_TX);
			rtlglue_printf("Dispatch mode: %d, cpu: %d\n", tx_dispatch_mode.mode, tx_dispatch_mode.smp_id);
		}
		
		return len;
	}
	_rtk_fc_proc_dispatch_mode_usage(NULL, RTK_FC_MGR_DISPATCH_NIC_TX);

	return len;
}
int rtk_fc_proc_smp_dispatch_wifi_rx_get(struct seq_file *s, void *v)
{
	rtk_fc_dispatch_mode_t rx_dispatch_mode;
	
	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLAN0_RX);
	PROC_PRINTF("WLAN0 - Mode: %d (%s) ", rx_dispatch_mode.mode, name_of_dispatch_mode[rx_dispatch_mode.mode]);
	if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else
		PROC_PRINTF("cpu:%d\n", rx_dispatch_mode.smp_id);
	
	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLAN1_RX);
	PROC_PRINTF("WLAN1 - Mode: %d (%s) ", rx_dispatch_mode.mode, name_of_dispatch_mode[rx_dispatch_mode.mode]);
	if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else
		PROC_PRINTF("cpu:%d\n", rx_dispatch_mode.smp_id);
	
	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLAN2_RX);
	PROC_PRINTF("WLAN2 - Mode: %d (%s) ", rx_dispatch_mode.mode, name_of_dispatch_mode[rx_dispatch_mode.mode]);
	if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else
		PROC_PRINTF("cpu:%d\n", rx_dispatch_mode.smp_id);

#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLANMLO_RX);
	PROC_PRINTF("WLANMLO - Mode: %d (%s) ", rx_dispatch_mode.mode, name_of_dispatch_mode[rx_dispatch_mode.mode]);
	if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else
		PROC_PRINTF("cpu:%d\n", rx_dispatch_mode.smp_id);

	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLANMLO_0_RX);
	PROC_PRINTF("WLANMLO_0 - Mode: %d (%s) ", rx_dispatch_mode.mode, name_of_dispatch_mode[rx_dispatch_mode.mode]);
	if(rx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else
		PROC_PRINTF("cpu:%d\n", rx_dispatch_mode.smp_id);
#endif

	_rtk_fc_proc_dispatch_mode_usage(s, RTK_FC_MGR_DISPATCH_WLAN0_RX);

	return SUCCESS;
}

int rtk_fc_proc_smp_dispatch_wifi_rx_set(struct file *filp, const char *buff,unsigned long count, void *data )
{
	int wlan_idx =SIGNED_INVALID;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;

	if (buff)
	{
		char *strptr,*split_str;
		rtk_fc_dispatch_mode_t rx_dispatch_mode;
		rtk_fc_dispatch_mode_mask_t mask=RTK_FC_MGR_DISPATCH_MASK_NONE;
		rx_dispatch_mode.mode = RTK_FC_DISPATCH_MODE_END;
		rx_dispatch_mode.smp_id = 0;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;
		while(1)
		{
			split_str=strsep(&strptr," ");
			if (split_str==NULL) 
				break;
			
			if(strcasecmp(split_str,"wlan")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
				{
					rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!");
					break;
				}
				else
				{
					wlan_idx = simple_strtol(split_str, NULL, 0);
					
				}
			}
			if(strcasecmp(split_str,"mode")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
				{
					rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!");
					break;
				}
				else
				{
					rx_dispatch_mode.mode = simple_strtol(split_str, NULL, 0);
					if((1 << rx_dispatch_mode.mode) & RTK_FC_DISPATCH_MODEMSK_WIFI_RX)
						mask |= RTK_FC_MGR_DISPATCH_MASK_MODE;
					else
						rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!");
				}
			}
			if(strcasecmp(split_str,"cpu")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
				{
					rtlglue_printf("[ERROR] invalid smp_id. CPU smp_id is not changed!");
					break;
				}
				else
				{
					rx_dispatch_mode.smp_id  = simple_strtol(split_str, NULL, 0);
					mask |= RTK_FC_MGR_DISPATCH_MASK_CPU;
				}
			}

			if (strptr==NULL) break;
		}

		if(mask == RTK_FC_MGR_DISPATCH_MASK_NONE)
			goto SHOW_USAGE;


		if((0 > wlan_idx) || (wlan_idx >= WLAN_IFACE_NUM_MAX)) {
			rtlglue_printf("[ERROR] invalid wlan index!");
		}

		if(wlan_idx==SIGNED_INVALID || mask == RTK_FC_MGR_DISPATCH_MASK_NONE)
			goto SHOW_USAGE;
			

		RTK_FC_HELPER_MGR_DISPATCH_MODE_SET(rx_dispatch_mode, mask, RTK_FC_MGR_DISPATCH_WLAN0_RX + wlan_idx, NULL);
		RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&rx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLAN0_RX + wlan_idx);
		rtlglue_printf("WLAN%d Dispatch mode: %d, cpu: %d\n", wlan_idx, rx_dispatch_mode.mode, rx_dispatch_mode.smp_id);
		
		return len;
	}

SHOW_USAGE:
	_rtk_fc_proc_dispatch_mode_usage(NULL, RTK_FC_MGR_DISPATCH_WLAN0_RX);

	return len;
}


int rtk_fc_proc_smp_dispatch_wifi_tx_get(struct seq_file *s, void *v)
{
	rtk_fc_dispatch_mode_t tx_dispatch_mode;

	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&tx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLAN0_TX);
	PROC_PRINTF("WLAN0 - Mode: %d (%s) ", tx_dispatch_mode.mode, name_of_dispatch_mode[tx_dispatch_mode.mode]);
	if(tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else if((tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_HASH) || (tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_RR))
		PROC_PRINTF("cpu (mask):0x%x\n", tx_dispatch_mode.smp_id);
	else
		PROC_PRINTF("cpu:%d\n", tx_dispatch_mode.smp_id);
	
	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&tx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLAN1_TX);
	PROC_PRINTF("WLAN1 - Mode: %d (%s) ", tx_dispatch_mode.mode, name_of_dispatch_mode[tx_dispatch_mode.mode]);
	if(tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else if((tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_HASH) || (tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_RR))
		PROC_PRINTF("cpu (mask):0x%x\n", tx_dispatch_mode.smp_id);
	else
		PROC_PRINTF("cpu:%d\n", tx_dispatch_mode.smp_id);
	
	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&tx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLAN2_TX);
	PROC_PRINTF("WLAN2 - Mode: %d (%s) ", tx_dispatch_mode.mode, name_of_dispatch_mode[tx_dispatch_mode.mode]);
	if(tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else if((tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_HASH) || (tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_RR))
		PROC_PRINTF("cpu (mask):0x%x\n", tx_dispatch_mode.smp_id);
	else
		PROC_PRINTF("cpu:%d\n", tx_dispatch_mode.smp_id);

#if defined(CONFIG_RTK_FC_WIFI7_SUPPORT)
	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&tx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLANMLO_TX);
	PROC_PRINTF("WLANMLO - Mode: %d (%s) ", tx_dispatch_mode.mode, name_of_dispatch_mode[tx_dispatch_mode.mode]);
	if(tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else if((tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_HASH) || (tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_RR))
		PROC_PRINTF("cpu (mask):0x%x\n", tx_dispatch_mode.smp_id);
	else
		PROC_PRINTF("cpu:%d\n", tx_dispatch_mode.smp_id);

	RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&tx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLANMLO_0_TX);
	PROC_PRINTF("WLANMLO_0 - Mode: %d (%s) ", tx_dispatch_mode.mode, name_of_dispatch_mode[tx_dispatch_mode.mode]);
	if(tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_NONE)
		PROC_PRINTF("\n");
	else if((tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_HASH) || (tx_dispatch_mode.mode == RTK_FC_DISPATCH_MODE_SMP_BY_RR))
		PROC_PRINTF("cpu (mask):0x%x\n", tx_dispatch_mode.smp_id);
	else
		PROC_PRINTF("cpu:%d\n", tx_dispatch_mode.smp_id);
#endif

	_rtk_fc_proc_dispatch_mode_usage(s, RTK_FC_MGR_DISPATCH_WLAN0_TX);

	return SUCCESS;
}

int rtk_fc_proc_smp_dispatch_wifi_tx_set(struct file *filp, const char *buff,unsigned long count, void *data )
{
	int wlan_idx =SIGNED_INVALID;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;

	if (buff)
	{
		char *strptr,*split_str;
		rtk_fc_dispatch_mode_t tx_dispatch_mode;
		rtk_fc_dispatch_mode_mask_t mask=RTK_FC_MGR_DISPATCH_MASK_NONE;
		tx_dispatch_mode.mode = RTK_FC_DISPATCH_MODE_END;
		tx_dispatch_mode.smp_id = 0;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;
		while(1)
		{
			split_str=strsep(&strptr," ");
			if(strcasecmp(split_str,"wlan")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
					rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!");
				else
				{
					wlan_idx = simple_strtol(split_str, NULL, 0);
					
				}
			}
			else if(strcasecmp(split_str,"mode")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
					rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!");
				else
				{
					tx_dispatch_mode.mode = simple_strtol(split_str, NULL, 0);
					if((1 << tx_dispatch_mode.mode) & RTK_FC_DISPATCH_MODEMSK_WIFI_TX)
						mask |= RTK_FC_MGR_DISPATCH_MASK_MODE;
					else
						rtlglue_printf("[ERROR] invalid dispatch mode. Dispatch mode is not changed!");
				}
			}
			else if(strcasecmp(split_str,"cpu")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
					rtlglue_printf("[ERROR] invalid smp_id. CPU smp_id is not changed!");
				else
				{
					tx_dispatch_mode.smp_id  = simple_strtol(split_str, NULL, 0);
					mask |= RTK_FC_MGR_DISPATCH_MASK_CPU;
				}
			}

			if (strptr==NULL) break;
		}

		if((0 > wlan_idx) || (wlan_idx >= WLAN_IFACE_NUM_MAX)) {
			rtlglue_printf("[ERROR] invalid wlan index!");
		}

		if(wlan_idx==SIGNED_INVALID || mask == RTK_FC_MGR_DISPATCH_MASK_NONE)
			goto SHOW_USAGE;
			

		RTK_FC_HELPER_MGR_DISPATCH_MODE_SET(tx_dispatch_mode, mask, RTK_FC_MGR_DISPATCH_WLAN0_TX + wlan_idx, NULL);
		RTK_FC_HELPER_MGR_DISPATCH_MODE_GET(&tx_dispatch_mode, RTK_FC_MGR_DISPATCH_WLAN0_TX + wlan_idx);
		rtlglue_printf("WLAN%d Dispatch mode: %d, cpu: %d\n", wlan_idx, tx_dispatch_mode.mode, tx_dispatch_mode.smp_id);
		return len;
	}

SHOW_USAGE:	
	_rtk_fc_proc_dispatch_mode_usage(NULL, RTK_FC_MGR_DISPATCH_WLAN0_TX);

	return len;
}



#if defined(CONFIG_FC_RTL9607C_SERIES)
int _rtk_fc_nptv6_acceleration_mechanism_get(struct seq_file *s, void *v)
{
	if(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_mechanism != RTK_FC_NPTV6_ACC_DISABLE)
	{
		int i;
		struct tx_info *pTxInfo;
		PROC_PRINTF("Enabled \n\n");
		for(i=1; i<MAX_NPTV6_ACC_UPSTREAM_SIZE+MAX_NPTV6_ACC_DOWNSTREAM_SIZE; i++)
		{
			if(fc_db.nptv6_flow_info[i].isSet==0)
				continue;
			PROC_PRINTF("fc_db.nptv6_flow_info[%d].outer_flow_index = %d\n", i ,fc_db.nptv6_flow_info[i].outer_flow_index);
			PROC_PRINTF("fc_db.nptv6_flow_info[%d].inner_flow_index = %d\n", i ,fc_db.nptv6_flow_info[i].inner_flow_index);
			PROC_PRINTF("fc_db.nptv6_flow_info[%d].sw_flow_index = %d\n", i ,fc_db.nptv6_flow_info[i].sw_flow_index);
		}
		for(i=0; i<4; i++){
			PROC_PRINTF("LAN[%d] meterIdx: %d aclIdx: %d\n", i, fc_db.meterIdx_fromLan[i], fc_db.meterACLIdx_fromLan[i]);
			
		}
		
		PROC_PRINTF("=========== Upstream ===========\n");
		PROC_PRINTF("Cpuport=%d Extport=%d Priority=%d Gmac=%d RxRingNum=%d TxRingNum=%d nicFlowNum=%d, Priority_rg=%d\n",
					fc_db_fastFwd_data.nptv6_acc.upstreamCpuport,
					fc_db_fastFwd_data.nptv6_acc.upstreamExtport,
					fc_db_fastFwd_data.nptv6_acc.upstreamPriority,
					fc_db_fastFwd_data.nptv6_acc.upstreamGmac,
					fc_db_fastFwd_data.nptv6_acc.upstreamRxRingNum,
					fc_db_fastFwd_data.nptv6_acc.upstreamTxRingNum,
					fc_db_fastFwd_data.nptv6_acc.upstreamNicFlowNum,
					fc_db_fastFwd_data.nptv6_acc.upstreamPriority_rg);
#ifdef HWNAT_CUSTOMIZE
		if(dynamic_sram_desc)
		{
			for(i=0; i<MAX_NPTV6_ACC_UPSTREAM_NIC_FLOW_NUM; i++)
			{
				if(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream[i].preallocated_outer_length)
				{
					PROC_PRINTF("Nic flow index[%d]:\n", i);
					PROC_PRINTF("\tSMAC=%pM DMAC=%pM SIPv6=%pI6 DIPv6=%pI6 isTcp=%d\n", 
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream[i].smac.octet,
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream[i].dmac.octet,
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream[i].sipv6.ipv6_addr,
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream[i].dipv6.ipv6_addr,
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream[i].isTcp);
					//dump_packet(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream[i].preallocated_outer, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream[i].preallocated_outer_length, "fs buffer:");
				}
			}
		}
#endif
		for(i=0; i<MAX_NPTV6_ACC_UPSTREAM_SIZE; i++)
		{
			if(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream[i].vaild==0)
				continue;
			pTxInfo = &fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream[i].txInfo;
			PROC_PRINTF("Index[%d]:\n", i);
			PROC_PRINTF("\tipv6_hdr_offset=%d ipv6_addr=%pI6\n", fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream[i].ipv6_hdr_offset, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream[i].ipv6_addr.ipv6_addr);
			PROC_PRINTF("\tdmac=%pM virtual_dmacL2Idx=%d\n", fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream[i].dmac.octet, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream[i].virtual_dmacL2Idx);
			PROC_PRINTF("\ttxInfo:\n");
			PROC_PRINTF("\topts1\t= 0x%08x Own=%d EOR=%d FS=%d LS=%d IPCS=%d L4CS=%d TPID_Sel=%d STAG_Aware=%d CRC=%d Len=%d\n", pTxInfo->opts1.dw,
				pTxInfo->opts1.bit.own,
				pTxInfo->opts1.bit.eor,
				pTxInfo->opts1.bit.fs,
				pTxInfo->opts1.bit.ls,
				pTxInfo->opts1.bit.ipcs,
				pTxInfo->opts1.bit.l4cs,
				pTxInfo->opts1.bit.tpid_sel,
				pTxInfo->opts1.bit.stag_aware,
				pTxInfo->opts1.bit.crc,
				pTxInfo->opts1.bit.data_length
			);
			PROC_PRINTF("\taddr\t= 0x%08x \n", pTxInfo->addr);
			PROC_PRINTF("\topts2\t= 0x%08x CPUTag=%d SVLANAct=%d CVLANAct=%d TxPmsk=0x%x CVLAN_VID=%d CVLAN_Pri=%d\n", pTxInfo->opts2.dw,
				pTxInfo->opts2.bit.cputag,			
				pTxInfo->opts2.bit.tx_svlan_action,
				pTxInfo->opts2.bit.tx_cvlan_action,
				pTxInfo->opts2.bit.tx_portmask,
				pTxInfo->opts2.bit.cvlan_vidh<<8|pTxInfo->opts2.bit.cvlan_vidl,
				pTxInfo->opts2.bit.cvlan_prio
			);
			PROC_PRINTF("\topts3\t= 0x%08x AsPri=%d CpuPri=%d Keep=%d DisLrn=%d Psel=%d L34Keep=%d extspa=%d Pppoe=%d SidIdx=%d PON_SID=%d\n", pTxInfo->opts3.dw,
				pTxInfo->opts3.bit.aspri,
				pTxInfo->opts3.bit.cputag_pri,
				pTxInfo->opts3.bit.keep,
				pTxInfo->opts3.bit.dislrn,
				pTxInfo->opts3.bit.cputag_psel,
				pTxInfo->opts3.bit.l34_keep,
				pTxInfo->opts3.bit.extspa,
				pTxInfo->opts3.bit.tx_pppoe_action,
				pTxInfo->opts3.bit.tx_pppoe_idx,
				pTxInfo->opts3.bit.tx_dst_stream_id
			);
			PROC_PRINTF("\topts4\t= 0x%08x LgsEn=%d LgMtu=%d SVLAN_VID=%d SVLAN_Pri=%d\n", pTxInfo->opts4.dw,
				pTxInfo->opts4.bit.lgsen,
				pTxInfo->opts4.bit.lgmtu,
				pTxInfo->opts4.bit.svlan_vidh<<8|pTxInfo->opts4.bit.svlan_vidl,
				pTxInfo->opts4.bit.svlan_prio
			);
		}

		if(fc_db_fastFwd_data.nptv6_acc.upstream0NicFlowNum)
		{
			PROC_PRINTF("=========== Upstream0 ===========\n");
			PROC_PRINTF("Cpuport=%d Extport=%d Priority=%d Gmac=%d RxRingNum=%d TxRingNum=%d nicFlowNum=%d, Priority_rg=%d\n",
						fc_db_fastFwd_data.nptv6_acc.upstream0Cpuport,
						fc_db_fastFwd_data.nptv6_acc.upstream0Extport,
						fc_db_fastFwd_data.nptv6_acc.upstream0Priority,
						fc_db_fastFwd_data.nptv6_acc.upstream0Gmac,
						fc_db_fastFwd_data.nptv6_acc.upstream0RxRingNum,
						fc_db_fastFwd_data.nptv6_acc.upstream0TxRingNum,
						fc_db_fastFwd_data.nptv6_acc.upstream0NicFlowNum,
						fc_db_fastFwd_data.nptv6_acc.upstream0Priority_rg);
#ifdef HWNAT_CUSTOMIZE
			if(dynamic_sram_desc)
			{
				for(i=0; i<MAX_NPTV6_ACC_UPSTREAM0_NIC_FLOW_NUM; i++)
				{
					if(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream0[i].preallocated_outer_length)
					{
						PROC_PRINTF("Nic flow index[%d]:\n", i);
						PROC_PRINTF("\tSMAC=%pM DMAC=%pM SIPv6=%pI6 DIPv6=%pI6 isTcp=%d\n", 
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream0[i].smac.octet,
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream0[i].dmac.octet,
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream0[i].sipv6.ipv6_addr,
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream0[i].dipv6.ipv6_addr,
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream0[i].isTcp);

						//dump_packet(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream0[i].preallocated_outer, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_upstream0[i].preallocated_outer_length, "fs buffer:");
					}
				}
			}
#endif
			for(i=0; i<MAX_NPTV6_ACC_UPSTREAM0_SIZE; i++)
			{
				if(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream0[i].vaild==0)
					continue;
				pTxInfo = &fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream0[i].txInfo;
				PROC_PRINTF("Index[%d]:\n", i);
				PROC_PRINTF("\tipv6_hdr_offset=%d ipv6_addr=%pI6\n", fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream0[i].ipv6_hdr_offset, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream0[i].ipv6_addr.ipv6_addr);
				PROC_PRINTF("\tdmac=%pM virtual_dmacL2Idx=%d\n", fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream0[i].dmac.octet, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_upstream0[i].virtual_dmacL2Idx);
				PROC_PRINTF("\ttxInfo:\n");
				PROC_PRINTF("\topts1\t= 0x%08x Own=%d EOR=%d FS=%d LS=%d IPCS=%d L4CS=%d TPID_Sel=%d STAG_Aware=%d CRC=%d Len=%d\n", pTxInfo->opts1.dw,
					pTxInfo->opts1.bit.own,
					pTxInfo->opts1.bit.eor,
					pTxInfo->opts1.bit.fs,
					pTxInfo->opts1.bit.ls,
					pTxInfo->opts1.bit.ipcs,
					pTxInfo->opts1.bit.l4cs,
					pTxInfo->opts1.bit.tpid_sel,
					pTxInfo->opts1.bit.stag_aware,
					pTxInfo->opts1.bit.crc,
					pTxInfo->opts1.bit.data_length
				);
				PROC_PRINTF("\taddr\t= 0x%08x \n", pTxInfo->addr);
				PROC_PRINTF("\topts2\t= 0x%08x CPUTag=%d SVLANAct=%d CVLANAct=%d TxPmsk=0x%x CVLAN_VID=%d CVLAN_Pri=%d\n", pTxInfo->opts2.dw,
					pTxInfo->opts2.bit.cputag,			
					pTxInfo->opts2.bit.tx_svlan_action,
					pTxInfo->opts2.bit.tx_cvlan_action,
					pTxInfo->opts2.bit.tx_portmask,
					pTxInfo->opts2.bit.cvlan_vidh<<8|pTxInfo->opts2.bit.cvlan_vidl,
					pTxInfo->opts2.bit.cvlan_prio
				);
				PROC_PRINTF("\topts3\t= 0x%08x AsPri=%d CpuPri=%d Keep=%d DisLrn=%d Psel=%d L34Keep=%d extspa=%d Pppoe=%d SidIdx=%d PON_SID=%d\n", pTxInfo->opts3.dw,
					pTxInfo->opts3.bit.aspri,
					pTxInfo->opts3.bit.cputag_pri,
					pTxInfo->opts3.bit.keep,
					pTxInfo->opts3.bit.dislrn,
					pTxInfo->opts3.bit.cputag_psel,
					pTxInfo->opts3.bit.l34_keep,
					pTxInfo->opts3.bit.extspa,
					pTxInfo->opts3.bit.tx_pppoe_action,
					pTxInfo->opts3.bit.tx_pppoe_idx,
					pTxInfo->opts3.bit.tx_dst_stream_id
				);
				PROC_PRINTF("\topts4\t= 0x%08x LgsEn=%d LgMtu=%d SVLAN_VID=%d SVLAN_Pri=%d\n", pTxInfo->opts4.dw,
					pTxInfo->opts4.bit.lgsen,
					pTxInfo->opts4.bit.lgmtu,
					pTxInfo->opts4.bit.svlan_vidh<<8|pTxInfo->opts4.bit.svlan_vidl,
					pTxInfo->opts4.bit.svlan_prio
				);
			}
		}
		
		PROC_PRINTF("\n=========== Downstream ===========\n");
		PROC_PRINTF("Cpuport=%d Extport=%d Priority=%d Gmac=%d RxRingNum=%d TxRingNum=%d nicFlowNum=%d, Priority_rg=%d\n",
					fc_db_fastFwd_data.nptv6_acc.downstreamCpuport,
					fc_db_fastFwd_data.nptv6_acc.downstreamExtport,
					fc_db_fastFwd_data.nptv6_acc.downstreamPriority,
					fc_db_fastFwd_data.nptv6_acc.downstreamGmac,
					fc_db_fastFwd_data.nptv6_acc.downstreamRxRingNum,
					fc_db_fastFwd_data.nptv6_acc.downstreamTxRingNum,
					fc_db_fastFwd_data.nptv6_acc.downstreamNicFlowNum,
					fc_db_fastFwd_data.nptv6_acc.downstreamPriority_rg);
#ifdef HWNAT_CUSTOMIZE
		if(dynamic_sram_desc)
		{
			for(i=0; i<MAX_NPTV6_ACC_DOWNSTREAM_NIC_FLOW_NUM; i++)
			{
				if(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream[i].preallocated_outer_length)
				{
					PROC_PRINTF("Nic flow index[%d]:\n", i);
					PROC_PRINTF("\tSMAC=%pM DMAC=%pM SIPv6=%pI6 DIPv6=%pI6 isTcp=%d\n", 
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream[i].smac.octet,
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream[i].dmac.octet,
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream[i].sipv6.ipv6_addr,
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream[i].dipv6.ipv6_addr,
						fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream[i].isTcp);
					//dump_packet(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream[i].preallocated_outer, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream[i].preallocated_outer_length, "fs buffer:");
				}
			}
		}
#endif
		for(i=0; i<MAX_NPTV6_ACC_DOWNSTREAM_SIZE; i++)
		{
			if(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream[i].vaild==0)
				continue;
			pTxInfo = &fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream[i].txInfo;
			PROC_PRINTF("Index[%d]:\n", i);
			PROC_PRINTF("\tipv6_hdr_offset=%d ipv6_addr=%pI6\n", fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream[i].ipv6_hdr_offset, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream[i].ipv6_addr.ipv6_addr);
			PROC_PRINTF("\tdmac=%pM virtual_dmacL2Idx=%d\n", fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream[i].dmac.octet, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream[i].virtual_dmacL2Idx);
			PROC_PRINTF("\ttxInfo:\n");
			PROC_PRINTF("\topts1\t= 0x%08x Own=%d EOR=%d FS=%d LS=%d IPCS=%d L4CS=%d TPID_Sel=%d STAG_Aware=%d CRC=%d Len=%d\n", pTxInfo->opts1.dw,
				pTxInfo->opts1.bit.own,
				pTxInfo->opts1.bit.eor,
				pTxInfo->opts1.bit.fs,
				pTxInfo->opts1.bit.ls,
				pTxInfo->opts1.bit.ipcs,
				pTxInfo->opts1.bit.l4cs,
				pTxInfo->opts1.bit.tpid_sel,
				pTxInfo->opts1.bit.stag_aware,
				pTxInfo->opts1.bit.crc,
				pTxInfo->opts1.bit.data_length
			);
			PROC_PRINTF("\taddr\t= 0x%08x \n", pTxInfo->addr);
			PROC_PRINTF("\topts2\t= 0x%08x CPUTag=%d SVLANAct=%d CVLANAct=%d TxPmsk=0x%x CVLAN_VID=%d CVLAN_Pri=%d\n", pTxInfo->opts2.dw,
				pTxInfo->opts2.bit.cputag,			
				pTxInfo->opts2.bit.tx_svlan_action,
				pTxInfo->opts2.bit.tx_cvlan_action,
				pTxInfo->opts2.bit.tx_portmask,
				pTxInfo->opts2.bit.cvlan_vidh<<8|pTxInfo->opts2.bit.cvlan_vidl,
				pTxInfo->opts2.bit.cvlan_prio
			);
			PROC_PRINTF("\topts3\t= 0x%08x AsPri=%d CpuPri=%d Keep=%d DisLrn=%d Psel=%d L34Keep=%d extspa=%d Pppoe=%d SidIdx=%d PON_SID=%d\n", pTxInfo->opts3.dw,
				pTxInfo->opts3.bit.aspri,
				pTxInfo->opts3.bit.cputag_pri,
				pTxInfo->opts3.bit.keep,
				pTxInfo->opts3.bit.dislrn,
				pTxInfo->opts3.bit.cputag_psel,
				pTxInfo->opts3.bit.l34_keep,
				pTxInfo->opts3.bit.extspa,
				pTxInfo->opts3.bit.tx_pppoe_action,
				pTxInfo->opts3.bit.tx_pppoe_idx,
				pTxInfo->opts3.bit.tx_dst_stream_id
			);
			PROC_PRINTF("\topts4\t= 0x%08x LgsEn=%d LgMtu=%d SVLAN_VID=%d SVLAN_Pri=%d\n", pTxInfo->opts4.dw,
				pTxInfo->opts4.bit.lgsen,
				pTxInfo->opts4.bit.lgmtu,
				pTxInfo->opts4.bit.svlan_vidh<<8|pTxInfo->opts4.bit.svlan_vidl,
				pTxInfo->opts4.bit.svlan_prio
			);
		}

		if(fc_db_fastFwd_data.nptv6_acc.downstream0NicFlowNum)
		{
			PROC_PRINTF("\n=========== Downstream0 ===========\n");
			PROC_PRINTF("Cpuport=%d Extport=%d Priority=%d Gmac=%d RxRingNum=%d TxRingNum=%d nicFlowNum=%d, Priority_rg=%d\n",
						fc_db_fastFwd_data.nptv6_acc.downstream0Cpuport,
						fc_db_fastFwd_data.nptv6_acc.downstream0Extport,
						fc_db_fastFwd_data.nptv6_acc.downstream0Priority,
						fc_db_fastFwd_data.nptv6_acc.downstream0Gmac,
						fc_db_fastFwd_data.nptv6_acc.downstream0RxRingNum,
						fc_db_fastFwd_data.nptv6_acc.downstream0TxRingNum,
						fc_db_fastFwd_data.nptv6_acc.downstream0NicFlowNum,
						fc_db_fastFwd_data.nptv6_acc.downstream0Priority_rg);
#ifdef HWNAT_CUSTOMIZE
			if(dynamic_sram_desc)
			{
				for(i=0; i<MAX_NPTV6_ACC_DOWNSTREAM0_NIC_FLOW_NUM; i++)
				{
					if(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream0[i].preallocated_outer_length)
					{
						PROC_PRINTF("Nic flow index[%d]:\n", i);
						PROC_PRINTF("\tSMAC=%pM DMAC=%pM SIPv6=%pI6 DIPv6=%pI6 isTcp=%d\n", 
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream0[i].smac.octet,
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream0[i].dmac.octet,
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream0[i].sipv6.ipv6_addr,
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream0[i].dipv6.ipv6_addr,
							fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream0[i].isTcp);
						//dump_packet(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream0[i].preallocated_outer, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_nic_flow_downstream0[i].preallocated_outer_length, "fs buffer:");
					}
				}
			}
#endif
			for(i=0; i<MAX_NPTV6_ACC_DOWNSTREAM0_SIZE; i++)
			{
				if(fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream0[i].vaild==0)
					continue;
				pTxInfo = &fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream0[i].txInfo;
				PROC_PRINTF("Index[%d]:\n", i);
				PROC_PRINTF("\tipv6_hdr_offset=%d ipv6_addr=%pI6\n", fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream0[i].ipv6_hdr_offset, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream0[i].ipv6_addr.ipv6_addr);
				PROC_PRINTF("\tdmac=%pM virtual_dmacL2Idx=%d\n", fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream0[i].dmac.octet, fc_db_fastFwd_data.nptv6_acc.nptv6_acceleration_downstream0[i].virtual_dmacL2Idx);
				PROC_PRINTF("\ttxInfo:\n");
				PROC_PRINTF("\topts1\t= 0x%08x Own=%d EOR=%d FS=%d LS=%d IPCS=%d L4CS=%d TPID_Sel=%d STAG_Aware=%d CRC=%d Len=%d\n", pTxInfo->opts1.dw,
					pTxInfo->opts1.bit.own,
					pTxInfo->opts1.bit.eor,
					pTxInfo->opts1.bit.fs,
					pTxInfo->opts1.bit.ls,
					pTxInfo->opts1.bit.ipcs,
					pTxInfo->opts1.bit.l4cs,
					pTxInfo->opts1.bit.tpid_sel,
					pTxInfo->opts1.bit.stag_aware,
					pTxInfo->opts1.bit.crc,
					pTxInfo->opts1.bit.data_length
				);
				PROC_PRINTF("\taddr\t= 0x%08x \n", pTxInfo->addr);
				PROC_PRINTF("\topts2\t= 0x%08x CPUTag=%d SVLANAct=%d CVLANAct=%d TxPmsk=0x%x CVLAN_VID=%d CVLAN_Pri=%d\n", pTxInfo->opts2.dw,
					pTxInfo->opts2.bit.cputag,			
					pTxInfo->opts2.bit.tx_svlan_action,
					pTxInfo->opts2.bit.tx_cvlan_action,
					pTxInfo->opts2.bit.tx_portmask,
					pTxInfo->opts2.bit.cvlan_vidh<<8|pTxInfo->opts2.bit.cvlan_vidl,
					pTxInfo->opts2.bit.cvlan_prio
				);
				PROC_PRINTF("\topts3\t= 0x%08x AsPri=%d CpuPri=%d Keep=%d DisLrn=%d Psel=%d L34Keep=%d extspa=%d Pppoe=%d SidIdx=%d PON_SID=%d\n", pTxInfo->opts3.dw,
					pTxInfo->opts3.bit.aspri,
					pTxInfo->opts3.bit.cputag_pri,
					pTxInfo->opts3.bit.keep,
					pTxInfo->opts3.bit.dislrn,
					pTxInfo->opts3.bit.cputag_psel,
					pTxInfo->opts3.bit.l34_keep,
					pTxInfo->opts3.bit.extspa,
					pTxInfo->opts3.bit.tx_pppoe_action,
					pTxInfo->opts3.bit.tx_pppoe_idx,
					pTxInfo->opts3.bit.tx_dst_stream_id
				);
				PROC_PRINTF("\topts4\t= 0x%08x LgsEn=%d LgMtu=%d SVLAN_VID=%d SVLAN_Pri=%d\n", pTxInfo->opts4.dw,
					pTxInfo->opts4.bit.lgsen,
					pTxInfo->opts4.bit.lgmtu,
					pTxInfo->opts4.bit.svlan_vidh<<8|pTxInfo->opts4.bit.svlan_vidl,
					pTxInfo->opts4.bit.svlan_prio
				);
			}
		}
	}
	else	
		PROC_PRINTF("Disabled \n");
	
	return SUCCESS;
}

int _rtk_fc_nptv6_acceleration_mechanism_proc_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff, len);

	if(fc_db.controlFuc.special_fast_forward_mode)
		_rtk_fc_nptv6_acceleration_mechanism_set(value);
	
	return len;
}

int _rtk_fc_vxlan_acceleration_mechanism_get(struct seq_file *s, void *v)
{
	if(fc_db_fastFwd_data.vxlan_acc.vxlan_acceleration_mechanism){
		PROC_PRINTF("Enabled: \n");
		if(fc_db.vxlan_recover_dev)
			PROC_PRINTF("\t recover dev: %s\n",fc_db.vxlan_recover_dev->name);
		
		PROC_PRINTF("\tvxlan_accelerate_downstreamL2Idx=%d\n",fc_db.vxlan_accelerate_downstreamL2Idx);
		PROC_PRINTF("\tvxlan_accelerate_extra_downstreamL2Idx=%d\n",fc_db.vxlan_accelerate_extra_downstreamL2Idx);
		PROC_PRINTF("\tvxlan_accelerate_upstreamL2Idx=%d\n",fc_db.vxlan_accelerate_upstreamL2Idx);
		PROC_PRINTF("\tvxlan_accelerate_extra_upstreamL2Idx=%d\n",fc_db.vxlan_accelerate_extra_upstreamL2Idx);
		PROC_PRINTF("\n");
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamGmac=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamGmac);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamRxRingNum=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamRxRingNum);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamTxRingNum=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamTxRingNum);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamPriority=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamPriority);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamInfo_prepared=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamInfo_prepared);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamRing_modified=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_upstreamRing_modified);
		PROC_PRINTF("\n");
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamGmac=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamGmac);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamRxRingNum=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamRxRingNum);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamTxRingNum=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamTxRingNum);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamPriority=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamPriority);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamInfo_prepared=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamInfo_prepared);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamRing_modified=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_downstreamRing_modified);
		PROC_PRINTF("\n");
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extraGmac=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extraGmac);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamRxRingNum=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamRxRingNum);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamTxRingNum=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamTxRingNum);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamPriority=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamPriority);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamInfo_prepared=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamInfo_prepared);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamRing_modified=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_upstreamRing_modified);
		PROC_PRINTF("\n");
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamRxRingNum=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamRxRingNum);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamTxRingNum=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamTxRingNum);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamPriority=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamPriority);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamInfo_prepared=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamInfo_prepared);		
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamRing_modified=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_extra_downstreamRing_modified);
		PROC_PRINTF("\n");
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_acceleration_extraPmsk=0x%04x\n",fc_db_fastFwd_data.vxlan_acc.vxlan_acceleration_extraPmsk);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_acceleration_extraMeter=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_acceleration_extraMeter);
		PROC_PRINTF("\n");
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_upstream_txAddrOffset=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_upstream_txAddrOffset);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_downstream_txAddrOffset=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_downstream_txAddrOffset);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_upstream_dmac=%02x%02x%02x%02x\n",fc_db_fastFwd_data.vxlan_acc.vxlan_upstream_dmac[0],fc_db_fastFwd_data.vxlan_acc.vxlan_upstream_dmac[1],fc_db_fastFwd_data.vxlan_acc.vxlan_upstream_dmac[2],fc_db_fastFwd_data.vxlan_acc.vxlan_upstream_dmac[3]);
		PROC_PRINTF("\n");
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_preallocated_outer_length=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_preallocated_outer_length);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_preallocated_outer_ip_length_offset=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_preallocated_outer_ip_length_offset);
		PROC_PRINTF("fc_db_fastFwd_data.vxlan_acc.vxlan_preallocated_outer_udp_length_offset=%d\n",fc_db_fastFwd_data.vxlan_acc.vxlan_preallocated_outer_udp_length_offset);
		dump_packet(fc_db_fastFwd_data.vxlan_acc.vxlan_preallocated_outer,64,"fc_db_fastFwd_data.vxlan_acc.vxlan_preallocated_outer");
		dump_packet(fc_db_fastFwd_data.vxlan_acc.vxlan_extra_preallocated_outer,64,"fc_db_fastFwd_data.vxlan_acc.vxlan_extra_preallocated_outer");		
	}else
		PROC_PRINTF("Disabled \n");
	
	return SUCCESS;
}
int _rtk_fc_vxlan_acceleration_mechanism_proc_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff, len);

	if(fc_db.controlFuc.special_fast_forward_mode)
		_rtk_fc_vxlan_acceleration_mechanism_set(value);
		

	return len;
}


int _rtk_fc_vxlan_acceleration_extraPmsk_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("extraPmsk: 0x%04x\n",fc_db_fastFwd_data.vxlan_acc.vxlan_acceleration_extraPmsk);
	return SUCCESS;
}
int _rtk_fc_vxlan_acceleration_extraPmsk_proc_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff, len);
	
	_rtk_fc_vxlan_acceleration_extraPmsk_set(value);
		
	_rtk_fc_vxlan_acceleration_extraPmsk_get(NULL,NULL);

	return len;
}

int _rtk_fc_nptv6_acc_mechanism_lanMeter_get(struct seq_file *s, void *v)
{
	int i;

	for(i=0; i<4; i++){
		PROC_PRINTF("LAN[%d] meterIdx: %d aclIdx: %d\n", i, fc_db.meterIdx_fromLan[i], fc_db.meterACLIdx_fromLan[i]);
		
	}
	
	return SUCCESS;
}
int _rtk_fc_nptv6_acc_mechanism_lanMeter_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	unsigned char tmpBuf[256] = {0};
	unsigned int length = (len > sizeof(tmpBuf))?sizeof(tmpBuf):len ;
	if(dynamic_sram_desc!=1)
	{
		rtlglue_printf("\033[1;33;41m[WARNING] Only valid when dynamic_sram_desc = 1 (nptv6 sram mode) \033[0m\n");
		goto errorOut;
	}

	if (buff)
	{
		char *strptr,*split_str;
		uint32 lanIdx;
		uint32 meter_bit_rate;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, length);

		strptr=tmpBuf;

		
		split_str=strsep(&strptr," ");
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter!"); 
			return len;
		}
		
		lanIdx = simple_strtoul(split_str, NULL, 0);	
		split_str=strsep(&strptr," ");
		
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter!"); 
			return len;
		}	
		meter_bit_rate = simple_strtol(split_str, NULL, 0);	

		if(lanIdx>=4)
		{
			rtlglue_printf("\033[1;33;41m[WARNING] lanIdx %d is out of range 0~3 \033[0m\n", lanIdx);
			goto errorOut;
		}

		if(meter_bit_rate> 1048568)
		{
			rtlglue_printf("\033[1;33;41m[WARNING] rate should be in 1 - 1048568 \033[0m\n");
			goto errorOut;
		}
		if(fc_db.meterIdx_fromLan[lanIdx] >= 0)
		{
			//rt_rate_meter_mapping_t meterMapping;
			rt_acl_filterAndQos_t acl_parameter;
			
			RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
			_rtk_fc_aclFilterAndQos_del(fc_db.meterACLIdx_fromLan[lanIdx]);
			(void)rt_rate_shareMeterType_del(fc_db.meterIdx_fromLan[lanIdx]);

			
			
			// rt_rate add to get index
			(void)rt_rate_shareMeterType_add(RT_METER_TYPE_ACL, &fc_db.meterIdx_fromLan[lanIdx]);
			//WARNING("fc_db_fastFwd_data.nptv6_acc.meterIdx_fromLan[%d] = %d rate = %d",i, fc_db_fastFwd_data.nptv6_acc.meterIdx_fromLan[i],rate);
			(void)rt_rate_shareMeterRate_set(fc_db.meterIdx_fromLan[lanIdx], meter_bit_rate);
			RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
			//RTK_FC_HELPER_RT_RATE_SHAREMETER_MAPPING_HW_GET(fc_db_fastFwd_data.nptv6_acc.meterIdx_fromLan[i], &meterMapping);

			//WARNING("ACL meter sw index: %d",fc_db_fastFwd_data.nptv6_acc.meterIdx_fromLan[i]);

			

			memset(&acl_parameter, 0, sizeof(rt_acl_filterAndQos_t));

			acl_parameter.filter_fields |= RT_ACL_INGRESS_PORT_MASK_BIT;
			acl_parameter.ingress_port_mask = 1<<lanIdx;
			
			acl_parameter.action_fields |= RT_ACL_ACTION_METER_GROUP_SHARE_METER_BIT;
			acl_parameter.action_meter_group_share_meter_index = fc_db.meterIdx_fromLan[lanIdx];

			acl_parameter.acl_weight = 65535;

			//acl_add_result = rt_acl_filterAndQos_add(acl_parameter, &acl_index);
			_rtk_fc_aclFilterAndQos_add(&acl_parameter, &fc_db.meterACLIdx_fromLan[lanIdx]);

			
			//_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_NPTV6_ACC_UPSTREAM_FROM_LAN0_ASSIGN_SHARE_METER+i, &nptv6_acc_upstream_assign_share_meter);
		}

	}
	else
	{
errorOut:	
		rtlglue_printf("usage:\n");
		rtlglue_printf("echo [lan port idx] [meter bit rate] > /proc/fc/sw_dump/nptv6_acc_mechanism_lanMeter\n");
	}
	return len;
}


#endif

#endif
void _rtk_fc_proc_flow_delay_usage(struct file *filep)
{
	char *path_str = NULL;

	if(_rtk_fc_proc_path_string_get(filep, &path_str)) {
		WARNING("get proc string fail");
		return;
	}
	
	rtlglue_printf("Usage:\n");
	rtlglue_printf("echo mode kernel > %s\n", path_str);
	rtlglue_printf("echo mode shortcut > %s\n", path_str);
	rtlglue_printf("echo count [# to hit] > %s\n", path_str);
	rtlglue_printf("echo protocol [tcp|other] [0|1] > %s\n", path_str);
	rtlglue_printf("[# to hit]: 0 - %d. (0: Default, also means disable the feature.)\n", RTK_FC_HIT_NUMBER_TO_DELAY_MAX);
	rtlglue_printf("[tcp|other]: tcp and other protocol control; set 0 to disable.\n");

}
void _rtk_fc_proc_external_switch_usage(void)
{
	rtlglue_printf("Usage:\n");
	rtlglue_printf("echo svlan {svlan_start} num {external_port_num} > /proc/fc/ctrl/external_switch_info\n");

}
void _rtk_fc_proc_sw_ackDelay_ctrl_usage(void)
{
	rtlglue_printf("Usage:\n");
	rtlglue_printf("echo status {0 or 1} > /proc/fc/ctrl/sw_ackDelay_ctrl\n");
	rtlglue_printf("Support field: pkt_num, jiffies, timer, acl_portmask, acl_pktlen_min, acl_pktlen_max, acl_trap_pri, hash_portmask \n");
}

int rtk_fc_proc_flow_delay_get(struct seq_file *s, void *v)
{
	if(fc_db.controlFuc.flow_delay_hit_num!=0) {
		PROC_PRINTF("Mode: %s\n", (fc_db.controlFuc.flow_delay_mode == RTK_FC_SHORTCUT_DELEY_MODE ? "shortcut delay to hw flow" : "kernel delay to hw/sw flow"));
		PROC_PRINTF("Delay after %d times hit.\n", fc_db.controlFuc.flow_delay_hit_num);
		PROC_PRINTF("Protocol Ctrl - TCP: %s ;Other: %s.\n", 
			fc_db.controlFuc.flow_delay_tcp_dis ? "OFF" : "ON", 
			fc_db.controlFuc.flow_delay_other_dis ? "OFF" : "ON");
	}
	else
		PROC_PRINTF("Feature disabled\n");
	
	return SUCCESS;
}

int rtk_fc_proc_flow_delay_set(struct file *filep, const char *buff,unsigned long len, void *data )
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int count = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	int hit_times, state;
	char *strptr,*split_str;

	if (buff) {
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, count);

		strptr = tmpBuf;

		if ((split_str = strsep(&strptr, " ")) == NULL)
			goto exit;

		if (strcasecmp(split_str, "mode") == 0) {
			if ((split_str = strsep(&strptr, " ")) == NULL)
				goto exit; 

			if (strcasecmp(split_str, "kernel") == 0) {
				fc_db.controlFuc.flow_delay_mode = RTK_FC_KERNEL_DELEY_MODE;
			}
			else if (strcasecmp(split_str, "shortcut") == 0) {
				fc_db.controlFuc.flow_delay_mode = RTK_FC_SHORTCUT_DELEY_MODE;
			}
			else {
				rtlglue_printf("[ERROR] invalid flow delay mode!\n");
				_rtk_fc_proc_flow_delay_usage(filep);
				goto exit;
			}
		}
		else if (strcasecmp(split_str, "count") == 0) {
			if ((split_str = strsep(&strptr, " ")) == NULL)
				goto exit;

			hit_times = simple_strtol(split_str, NULL, 0);
			if (hit_times < 0 || hit_times > RTK_FC_HIT_NUMBER_TO_DELAY_MAX) {
				rtlglue_printf("hit count should between 0 to %d!\n", RTK_FC_HIT_NUMBER_TO_DELAY_MAX);
				_rtk_fc_proc_flow_delay_usage(filep);
			}
			else {
				fc_db.controlFuc.flow_delay_hit_num = hit_times;
				//rtk_fc_proc_flow_delay_get(NULL,NULL);
			}
		}else if (strcasecmp(split_str, "protocol") == 0) {
			if ((split_str = strsep(&strptr, " ")) == NULL)
				goto exit;

			if (strcasecmp(split_str, "tcp") == 0) {
				if ((split_str = strsep(&strptr, " ")) == NULL)
					goto exit;
				state = simple_strtol(split_str, NULL, 0);
				fc_db.controlFuc.flow_delay_tcp_dis = state ? 0 : 1;
			}else if (strcasecmp(split_str, "other") == 0) {
				if ((split_str = strsep(&strptr, " ")) == NULL)
					goto exit;
				state = simple_strtol(split_str, NULL, 0);
				fc_db.controlFuc.flow_delay_other_dis = state ? 0 : 1;
			}else {
				rtlglue_printf("[ERROR] invalid flow delay type!\n");
				_rtk_fc_proc_flow_delay_usage(filep);
				goto exit;
			}
		}
		else {
			_rtk_fc_proc_flow_delay_usage(filep);
		}
	}

exit:
	return len;
}

int rtk_fc_proc_ethertype_bypass_get(struct seq_file *s, void *v)
{
	int i = 0;
	
	if(fc_db.systemGlobal.bypass_ethertypeProto_bitmap==0)
		PROC_PRINTF("none\n");
	else {
		for(i = 0; i < RTK_FC_ETHERTYPE_BYPASS_NUM; i++) {
			if(fc_db.systemGlobal.bypass_ethertypeProto_bitmap & (1<<i))
				PROC_PRINTF("[%d] 0x%x\n", i, fc_db.systemGlobal.bypass_ethertypeProto[i]);
			else
				PROC_PRINTF("[%d] N/A\n", i);
		}
	}
	PROC_PRINTF("\nUsage:\n");
	PROC_PRINTF("     set exist ethertype to clear, set new ethertype to add\n");
	PROC_PRINTF("     echo [ethertype] > /proc/fc/ctrl/ethertype_bypass \n");
	PROC_PRINTF("     [ethertype]:\n");
	PROC_PRINTF("       e.g. 0x888E, ... etc. \n");

	
	return SUCCESS;
}
int rtk_fc_proc_ethertype_bypass_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	bool exist = FALSE;
	int i;
	uint32 ethertype = _rtk_fc_proc_parsing_string_to_integer(buff, len);

	if(ethertype>0xffff) {
		WARNING("0x%x not support", ethertype);
		return len;
	}
	// find exist and clear
	for(i = 0; i < RTK_FC_ETHERTYPE_BYPASS_NUM; i++) {
		if((fc_db.systemGlobal.bypass_ethertypeProto_bitmap & (1<<i)) &&
			(fc_db.systemGlobal.bypass_ethertypeProto[i] == ethertype)) {
			exist = TRUE;
			// clear
			fc_db.systemGlobal.bypass_ethertypeProto[i] = 0;
			fc_db.systemGlobal.bypass_ethertypeProto_bitmap &= ~(1<<i);
			break;
		}
	}

	// find empty and set
	if(!exist) {
		for(i = 0; i < RTK_FC_ETHERTYPE_BYPASS_NUM; i++) {
			if(fc_db.systemGlobal.bypass_ethertypeProto_bitmap & (1<<i)) 
				continue;

			// set
			fc_db.systemGlobal.bypass_ethertypeProto[i] = (ethertype & 0xffff);
			fc_db.systemGlobal.bypass_ethertypeProto_bitmap |= (1<<i);
			exist = TRUE;
			break;
		}
	}

	if(!exist) {
		WARNING("table full, please delete one or request to enlarge table size ..");
	}
	

	return len;
}

void rtk_fc_proc_sw_shaperMib_update_mode_flow_usage(struct seq_file *s, void *v)
{
	rtk_fc_sw_shaperMib_update_modeType_t mode;
	
	PROC_PRINTF("\nUsage:\n"); ;
	PROC_PRINTF("     echo [sw_shaperMib_update_mode] > /proc/fc/ctrl/sw_shaperMib_update_mode_flow \n\n");
	
	for(mode = 0; mode < SW_SHAPER_UPDATE_END; mode++) {
		PROC_PRINTF("%s\n", name_of_sw_shaperMib_update_mode[mode]);
	}
}
int rtk_fc_proc_sw_shaperMib_update_mode_flow_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("[FLOW] SW shaper and mib update mode:\n");

	PROC_PRINTF("%s\n", name_of_sw_shaperMib_update_mode[fc_db.controlFuc.sw_shaperMib_update_mode_flow]);

	rtk_fc_proc_sw_shaperMib_update_mode_flow_usage(s, v);
	
	return SUCCESS;
}
int rtk_fc_proc_sw_shaperMib_update_mode_flow_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	rtk_fc_sw_shaperMib_update_modeType_t mode = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	if(mode < SW_SHAPER_UPDATE_END)
		fc_db.controlFuc.sw_shaperMib_update_mode_flow = mode;
		
	else {
		rtlglue_printf("\nnot support\n");
		rtk_fc_proc_sw_shaperMib_update_mode_flow_usage(NULL, NULL);
	}
	return len;
}

int rtk_fc_proc_sw_shaperMib_update_mode_host_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("[HOST] SW shaper and mib update mode:\n");
	if(fc_db.controlFuc.sw_shaperMib_update_mode_host)
		PROC_PRINTF("payload length mode, SW shaper and mib update by payload length (exclude L2/L3/L4 header length)) \n");
	else
		PROC_PRINTF("packet length mode, SW shaper and mib update by total packet length (include header length) \n");

	PROC_PRINTF("\nUsage:\n"); ;
	PROC_PRINTF("     echo [sw_shaperMib_update_mode] > /proc/fc/ctrl/sw_shaperMib_update_mode_host \n");
	PROC_PRINTF("     0: packet length mode, SW shaper and mib update by total packet length (include header length)\n");
	PROC_PRINTF("     1: payload length mode, SW shaper and mib update by payload length (exclude L2/L3/L4 header length)\n");

	return SUCCESS;
}

int rtk_fc_proc_sw_shaperMib_update_mode_host_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	fc_db.controlFuc.sw_shaperMib_update_mode_host = ((_rtk_fc_proc_parsing_string_to_integer(buff,len)==0) ? FALSE : TRUE);
	return len;
}

int rtk_fc_proc_sw_ackDelay_ctrl_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("ackDelayCtrl: status %d\n", fc_db.ackDelayCtrl.enable);
	PROC_PRINTF("\t Cfg: pkt_num %d, jiffies %d(1=10ms), timer %d(ms)\n", fc_db.ackDelayCtrl.pktNum_timeout, fc_db.ackDelayCtrl.jiffies_timeout, fc_db.ackDelayCtrl.timer_timeout);
	PROC_PRINTF("\t ACL: acl_portmask 0x%x, acl_pktlen_min %d, acl_pktlen_max %d, acl_trap_pri %d\n", fc_db.ackDelayCtrl.igr_portmask, fc_db.ackDelayCtrl.igr_pkt_len_min, fc_db.ackDelayCtrl.igr_pkt_len_max, fc_db.ackDelayCtrl.sw_fwd_acl_trap_pri);
	PROC_PRINTF("\t Hash: hash_portmask 0x%x\n", fc_db.ackDelayCtrl.egr_portmask);

	_rtk_fc_proc_sw_ackDelay_ctrl_usage();

	return SUCCESS;
}

int rtk_fc_proc_sw_ackDelay_ctrl_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	char *strptr,*split_str;
	uint8 update_acl = 0;
	int value = 0;

	if(buff)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, CMD_BUFF_SIZE);

		strptr=tmpBuf;
		while(1)
		{
			split_str = strsep(&strptr," ");
			if(strcasecmp(split_str,"status") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				if(value < 0){
					goto exit;
				}
				fc_db.ackDelayCtrl.enable = value;
				fc_db.ackDelayCtrl.pktNum_timeout = 120;
				fc_db.ackDelayCtrl.jiffies_timeout = 1;
				fc_db.ackDelayCtrl.timer_timeout = 10;
				fc_db.ackDelayCtrl.igr_portmask = 0x4;
				fc_db.ackDelayCtrl.igr_pkt_len_min = 0;
				fc_db.ackDelayCtrl.igr_pkt_len_max = 80;
				fc_db.ackDelayCtrl.sw_fwd_acl_trap_pri = 7;
				fc_db.ackDelayCtrl.egr_portmask = RTK_FC_ALL_MAC_WLANCPU_PORTMASK;
				update_acl = 1;
			}else if(strcasecmp(split_str,"pkt_num") == 0){
				if((split_str = strsep(&strptr, " "))==NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.ackDelayCtrl.pktNum_timeout = value;
			}else if(strcasecmp(split_str, "jiffies") == 0){
				if ((split_str = strsep(&strptr, " ")) == NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.ackDelayCtrl.jiffies_timeout = value;
			}else if(strcasecmp(split_str, "timer") == 0){
				if ((split_str = strsep(&strptr, " ")) == NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.ackDelayCtrl.timer_timeout = value;
			}else if(strcasecmp(split_str, "acl_portmask") == 0){
				if ((split_str = strsep(&strptr, " ")) == NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.ackDelayCtrl.igr_portmask = value;
				update_acl = 1;
			}else if(strcasecmp(split_str, "acl_pktlen_min") == 0){
				if ((split_str = strsep(&strptr, " ")) == NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.ackDelayCtrl.igr_pkt_len_min = value;
				update_acl = 1;
			}else if(strcasecmp(split_str, "acl_pktlen_max") == 0){
				if ((split_str = strsep(&strptr, " ")) == NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.ackDelayCtrl.igr_pkt_len_max = value;
				update_acl = 1;
			}else if(strcasecmp(split_str, "acl_trap_pri") == 0){
				if ((split_str = strsep(&strptr, " ")) == NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				if(value > 7){
					goto exit;
				}
				fc_db.ackDelayCtrl.sw_fwd_acl_trap_pri = value;
				update_acl = 1;
			}else if(strcasecmp(split_str, "hash_portmask") == 0){
				if ((split_str = strsep(&strptr, " ")) == NULL){
					goto exit;
				}
				value = simple_strtol(split_str, NULL, 0);
				fc_db.ackDelayCtrl.egr_portmask = value;
				if(value & RTK_FC_ALL_MAC_CPU_PORTMASK)
					fc_db.ackDelayCtrl.egr_portmask |= RTK_FC_ALL_MAC_WLANCPU_PORTMASK;
			}else{
				rtlglue_printf("[ERROR] Wrong parameter\n");
				goto exit;
			}
			if(value < 0){
				goto exit;
			}
			if (strptr==NULL) break;
		}

		fc_db.controlFuc.swAckDelay_en = fc_db.ackDelayCtrl.enable;
		if(update_acl){
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			if(fc_db.ackDelayCtrl.enable)
				assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_SMALL_TCP_ACK_TRAP, NULL));
			else
				assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_SMALL_TCP_ACK_TRAP));
#endif
		}
		if(fc_db.ackDelayCtrl.enable){
			rtlglue_printf("Also Force Configure Below Command: (please take care original default value by yourself)\n");
			rtlglue_printf("1. Disable 5tuple flow acc by 2tuple prevent hash unmatch\n");
			rtlglue_printf("echo 0 > proc/fc/ctrl/bridge_5tuple_flow_accelerate_by_2tuple\n");
			rtk_fc_proc_bridge_5tuple_flow_accelerate_by_2tuple_set(NULL, "0 \n", 20, NULL);
			rtlglue_printf("2. Enable acl trap support sw shortcut forward\n");
			snprintf(tmpBuf, CMD_BUFF_SIZE, "0x%x \n", 0x1<<fc_db.ackDelayCtrl.sw_fwd_acl_trap_pri);
			rtlglue_printf("echo %s > proc/fc/ctrl/flow_swFwded_aclTrapPri_mask", tmpBuf);
			rtk_fc_proc_flow_swFwded_aclTrapPri_mask_set(NULL, tmpBuf, 20, NULL);
			rtlglue_printf("\n\n");
			rtk_fc_flow_flush();
		}
	}

	return len;

exit:
	_rtk_fc_proc_sw_ackDelay_ctrl_usage();
	return len;
}


int rtk_fc_proc_flow_swFwded_aclTrapPri_mask_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int cmdlen = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;

	/* copy data to the buffer */
	strscpy(tmpBuf, buff, cmdlen);

	strptr=tmpBuf;
	split_str=strsep(&strptr," ");

	fc_db.controlFuc.flow_swFwded_aclTrapPri_mask = simple_strtol(split_str, NULL, 0);

	return len;
}

int rtk_fc_proc_flow_swFwded_aclTrapPri_mask_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("flow_swFwded_aclTrapPri_mask: 0x%02x\n", fc_db.controlFuc.flow_swFwded_aclTrapPri_mask);

	PROC_PRINTF("\nUsage:\n"); ;
	PROC_PRINTF("     echo [flow_swFwded_aclTrapPri_mask] > /proc/fc/ctrl/flow_swFwded_aclTrapPri_mask \n");
	PROC_PRINTF("     If the bit i of flow_swFwded_aclTrapPri_mask is 1, it means the packet, \n");
	PROC_PRINTF("     which is trapped by ACL with priority i, will be forwarded by a distinct SW shortcut flow.\n");

	return SUCCESS;
}
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)

static void _rtk_fc_proc_dynamic_prehashPattern_mode_usage(struct seq_file *s)
{
	PROC_PRINTF("\nUsage:\n");

	PROC_PRINTF("echo 1 > /proc/fc/ctrl/dynamic_prehashPattern\n:");

}


int rtk_fc_proc_dynamic_prehashPattern_set(struct file *filp, const char *buff,unsigned long len, void *data )
{

	int value = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	if(value == 1 || value == 0)
		fc_db.controlFuc.dynamic_calculate_prehash_pattern = value;
	else if (value >1)
		fc_db.dynamic_calculate_prehash_timeout_sec = value;

	
	return len;
}
int rtk_fc_proc_dynamic_prehashPattern_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Dynamic calculate prehash pattern: %s\n", fc_db.controlFuc.dynamic_calculate_prehash_pattern?"On":"Off");
	
	PROC_PRINTF("Dynamic calculate prehash timeout seconds: %d\n", fc_db.dynamic_calculate_prehash_timeout_sec);
	_rtk_fc_proc_dynamic_prehashPattern_mode_usage(NULL );
	return SUCCESS;
}
#endif
int rtk_fc_proc_house_keeping_flow_delete_immediatly_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	fc_db.controlFuc.house_keeping_flow_delete_immediatly = value;

	return len;
	
}
int rtk_fc_proc_house_keeping_flow_delete_immediatly_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Immediatly delte flow feature in house keeping is %s \n", (fc_db.controlFuc.house_keeping_flow_delete_immediatly>0)?"On":"Off");
	return SUCCESS;
}
int rtk_fc_proc_disable_port_port_moving_set(struct file *filp, const char *buff,unsigned long count, void *data )
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	

	if (buff)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;
		while(1)
		{
			split_str=strsep(&strptr," ");
			if(strcasecmp(split_str,"port")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
				{
					rtlglue_printf("[ERROR]Invalid port bit mask!\n");
					return -EFAULT;
				}
				else
				{
					int port = 0;
					
					port  = simple_strtol(split_str, NULL, 0);
					
					if(port >= RTK_FC_MAC_PORT_MAX)
					{
						rtlglue_printf("[ERROR]Error port setting!\n");
						return -EFAULT;

					}
				
					split_str=strsep(&strptr," ");
					
					if(strcasecmp(split_str,"disable")==0)
					{
						fc_db.disable_port_moving_port_bitmask |= 1<<port;
					}
					else if(strcasecmp(split_str,"enable")==0)
					{
						fc_db.disable_port_moving_port_bitmask &= ~(1<<port);
					}
					else
					{
						rtlglue_printf("[ERROR]Wrong parameter\n");
						return -EFAULT;
					}	
					
				}
			}else if(strcasecmp(split_str,"action")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
				{
					rtlglue_printf("[ERROR]Invalid cmd!\n");
					return -EFAULT;
				}
				else
				{					
					if(strcasecmp(split_str,"drop")==0)
					{
						fc_db.disable_port_moving_action_forward = 0;
					}
					else if(strcasecmp(split_str,"forward")==0)
					{
						fc_db.disable_port_moving_action_forward = 1;
					}
					else
					{
						rtlglue_printf("[ERROR]Wrong parameter\n");
						return -EFAULT;
					}	
				}
			}
			else
			{
				rtlglue_printf("[ERROR]Wrong parameter\n");
				return -EFAULT;
			}	
			if (strptr==NULL) break;
		}

	}

	rtlglue_printf("\n bit mask = 0x%x, action = %s\n", fc_db.disable_port_moving_port_bitmask, fc_db.disable_port_moving_action_forward ? "forward" : "drop");	

	return len;
}

int rtk_fc_proc_disable_port_port_moving_get(struct seq_file *s, void *v)
{
	int i ;
	PROC_PRINTF("ports disable port-moving:%s", " ");
	for(i = 0 ; i < 32 ;i ++)
	{
		if((fc_db.disable_port_moving_port_bitmask&(1<<i)) )
		{
			PROC_PRINTF("%d,", i);		
		}
	}
	PROC_PRINTF(" bit mask = 0x%x, action = %s \n-------------------\n", fc_db.disable_port_moving_port_bitmask, fc_db.disable_port_moving_action_forward ? "forward" : "drop");

	PROC_PRINTF("cmd: \n\techo port [port_num] [disable|enable] > /proc/fc/ctrl/disable_port_port_moving\n");
	PROC_PRINTF("\techo action [drop|forward] > /proc/fc/ctrl/disable_port_port_moving\n");
	return SUCCESS;
}

void _rtk_fc_proc_l2_port_moving_check_with_vlan_usage(struct file *filep)
{
	char *path_str = NULL;

	if(_rtk_fc_proc_path_string_get(filep, &path_str)) {
		WARNING("get proc string fail");
		return;
	}
	
	rtlglue_printf("Usage:\n");
	rtlglue_printf("echo [enable] [flow_flush_pmask] > %s\n", path_str);
	rtlglue_printf("[enable]: 0: disable; 1: enable\n");
	rtlglue_printf("[flow_flush_pmask]: 0x0 - 0x%x.\n", ((1<<RTK_FC_MAC_PORT_MAX)-1));

	return;
}

int rtk_fc_proc_l2_port_moving_check_with_vlan_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	int ret = SUCCESS;
	char *cmd_param[CMD_PARAM_COUNT] = {0};
	uint32 cfg;

	if((ret = _rtk_fc_proc_parsing_string_to_param_arrary(buff, len, &cmd_param[0])) != SUCCESS) {
		goto ERROR;
	}

	if(cmd_param[0]) {
		if(strcasecmp(cmd_param[0], "?")==0)
			goto ERROR;
		cfg = simple_strtol(cmd_param[0], NULL, 0);
		fc_db.l2_port_moving_check_with_vlan = (cfg==0) ? 0 : 1;
	} 
	if (cmd_param[1]) {
		cfg = simple_strtol(cmd_param[1], NULL, 0);
		fc_db.l2_port_moving_check_with_vlan_flow_flush = (cfg & ((1<<RTK_FC_MAC_PORT_MAX)-1));
	}

	return len;

ERROR:
	_rtk_fc_proc_l2_port_moving_check_with_vlan_usage(filp);
	return len;

}

int rtk_fc_proc_l2_port_moving_check_with_vlan_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("l2_learning_with_vlan: %s\n", (fc_db.l2_port_moving_check_with_vlan>0)?"On":"Off");	
	PROC_PRINTF("portmask to enable flow flush : 0x%x\n", fc_db.l2_port_moving_check_with_vlan_flow_flush);
	return SUCCESS;

}

int rtk_fc_proc_external_switch_info_set(struct file *filp, const char *buff,unsigned long len, void *data )
{

	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	
	if(buff)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, CMD_BUFF_SIZE);

		strptr=tmpBuf;
		while(1)
		{
			split_str=strsep(&strptr," ");
			if(strcasecmp(split_str,"num")==0)
			{
				split_str=strsep(&strptr," ");
				if(split_str==NULL)
				{
					
					_rtk_fc_proc_external_switch_usage();
					goto exit;
				}
				else
				{
					int port_num = 0;
					
					port_num  = simple_strtol(split_str, NULL, 0);
					
					if(port_num < 0)
					{
						
						_rtk_fc_proc_external_switch_usage();
						goto exit;
					}
					fc_db.external_switch_lan_port_num = port_num;
					
					
				}
			}
			else if(strcasecmp(split_str, "svlan") == 0)
			{
				int vlan;
				
				if ((split_str = strsep(&strptr, " ")) == NULL){
					
					_rtk_fc_proc_external_switch_usage();
					goto exit;
				}
				vlan = simple_strtol(split_str, NULL, 0);
				
				if(vlan < 0 || vlan > 4096)
				{
					
					_rtk_fc_proc_external_switch_usage();
					goto exit;
				}
				fc_db.external_switch_vlan_start = vlan;

			}
			else
			{
				rtlglue_printf("[ERROR]Wrong parameter\n");
				_rtk_fc_proc_external_switch_usage();
					goto exit;
			}	
			if (strptr==NULL) break;
		}

	}
	

exit:
	return len;
}
int rtk_fc_proc_external_switch_info_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("external_switch_vlan_start: %d external_switch_lan_port_num: %d\n", (fc_db.external_switch_vlan_start), (fc_db.external_switch_lan_port_num));
	_rtk_fc_proc_external_switch_usage();
	return SUCCESS;
}

int rtk_fc_proc_shortcut_flow_count_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%d\n", fc_db.shortcut_flow_count);
	return SUCCESS;
}
//.unlockBefortWrite=1
int rtk_fc_proc_shortcut_flow_count_set(struct file *filp, const char *buff, unsigned long count, void *data)
{
	uint32 ori_count = fc_db.shortcut_flow_count;
	uint32 shortcut_flow_count = _rtk_fc_proc_parsing_string_to_integer(buff, count);
	uint32 tmp_hwnat_mode, tmp_flow_not_update_in_real_time;


	//========================= Critical Section Start =========================//
	RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
	RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
		
	if(shortcut_flow_count > RTK_FC_MAX_SHORTCUT_FLOW_SIZE)
	{
		rtlglue_printf("over maximum shortcut flow count: %d, the shortcut flow count is not changed\n", RTK_FC_MAX_SHORTCUT_FLOW_SIZE);
	}
	else if((fc_db.controlFuc.hwnat_mode == RT_FLOW_HWNAT_MODE_SW_ONLY) && (shortcut_flow_count == 0))
	{
		rtlglue_printf("swflow_only is enabled, the shortcut flow count can not be set to 0. The shortcut flow count is not changed\n");
	}else {

		if(ori_count != shortcut_flow_count)
		{
			fc_db.shortcut_flow_count = shortcut_flow_count;

			tmp_hwnat_mode = fc_db.controlFuc.hwnat_mode;
			fc_db.controlFuc.hwnat_mode = RT_FLOW_HWNAT_MODE_DIS_ACC; // disable learning
			RTK_FC_HELPER_MGR_HWNAT_MODE_SET(RT_FLOW_HWNAT_MODE_DIS_ACC);

			tmp_flow_not_update_in_real_time = fc_db.systemGlobal.flow_not_update_in_real_time;
			fc_db.systemGlobal.flow_not_update_in_real_time = DISABLED;
			assert_ok(rtk_fc_flow_flush_all());
			
			fc_db.systemGlobal.flow_not_update_in_real_time = tmp_flow_not_update_in_real_time;

			assert_ok(_rtk_fc_flowDataBase_init());

			rtk_fc_tableReset();

			//atomic_set(&fc_db.trapToPs, driverState);		// recover to original state
			fc_db.controlFuc.hwnat_mode = tmp_hwnat_mode;
			RTK_FC_HELPER_MGR_HWNAT_MODE_SET(tmp_hwnat_mode);
		}

		rtk_fc_proc_shortcut_flow_count_get(NULL, NULL);
	}
	
	RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
	RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();
	//========================= Critical Section End =========================//

	// might sleep
	RTK_FC_HOOK_PS_CT_FLUSH();
	
	return count;
}
int rtk_fc_proc_flow_mib_mode_get(struct seq_file *s, void *v)
{
	char *str_flow_mib_mode[RT_STAT_FLOW_MIB_MODE_MAX]=
	{
		"default flow mib mode",
		"extra flow mib mode",
	};
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	int i;

	PROC_PRINTF("mode: %u\n\n", fc_db.controlFuc.flow_mib_mode);

	PROC_PRINTF("echo mode > /proc/fc/ctrl/flow_mib_mode\n");
	PROC_PRINTF("%-5s | %-35s | %-20s | %-20s | %-20s\n", "mode", "description", "flow_mib size", "flow_mib2 size", "acl_mib size");
	PROC_PRINTF("----------------------------------------------------------------------\n");
	for(i = 0 ; i < RT_STAT_FLOW_MIB_MODE_MAX ; i++)
		PROC_PRINTF("%5d | %-35s | %-20u | %-20u | %-20u\n", i, str_flow_mib_mode[i], RT_FLOW_MIB_SIZE_GET(i), RT_FLOW_MIB2_SIZE_GET(i), RT_ACL_MIB_SIZE_GET(i));
#else
	PROC_PRINTF("Support %s only:  flow_mib size %u, flow_mib2 size %u, acl_mib size %u\n", str_flow_mib_mode[RT_STAT_FLOW_MIB_MODE_DEFAULT], RT_FLOW_MIB_SIZE_GET(RT_STAT_FLOW_MIB_MODE_DEFAULT), RT_FLOW_MIB2_SIZE_GET(RT_STAT_FLOW_MIB_MODE_DEFAULT), RT_ACL_MIB_SIZE_GET(RT_STAT_FLOW_MIB_MODE_DEFAULT));
#endif

	return SUCCESS;
}

int rtk_fc_proc_flow_mib_mode_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
#if (defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
	int tmpVal = _rtk_fc_proc_parsing_string_to_integer(buff, count)?RT_STAT_FLOW_MIB_MODE_EXTRA:RT_STAT_FLOW_MIB_MODE_DEFAULT;
	uint32 tmp_hwnat_mode, idx, hw_idx = 0;
	uint32 ori_acl_remap_cnt = RTK_FC_ACL_POLREMAP_CNT;

	if(tmpVal != fc_db.controlFuc.flow_mib_mode)
	{
		if((tmpVal == RT_STAT_FLOW_MIB_MODE_EXTRA) && (fc_db.controlFuc.flow_miss_meter_idx != SIGNED_INVALID))
		{
			WARNING("[Error] do not support flow_mib_mode %d when flow miss meter is enabled\n", RT_STAT_FLOW_MIB_MODE_EXTRA);
			return -EFAULT;
		}

		tmp_hwnat_mode = fc_db.controlFuc.hwnat_mode;
		fc_db.controlFuc.hwnat_mode = RT_FLOW_HWNAT_MODE_DIS_ACC;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(RT_FLOW_HWNAT_MODE_DIS_ACC);

		//flush all flow
		rtk_fc_flow_flush_all();

		//Clear all HW and SW flow/ACL mib
		for(idx = 0 ; idx < RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode) ; idx++)
		{
			_rtk_fc_hwFlowMib_clear(idx);
			memset(&fc_db.flowSWMib[idx][0], 0, sizeof(fc_db.flowSWMib[idx]));
		}
		for(idx = 0 ; idx < RT_FLOW_MIB2_SIZE_GET(fc_db.controlFuc.flow_mib_mode) ; idx++)
		{
			_rtk_fc_hwFlowMib2_clear(idx);
			memset(&fc_db.flowSWMib2[idx][0], 0, sizeof(fc_db.flowSWMib2[idx]));

		}
		for(idx = 0 ; idx < RT_ACL_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode) ; idx++)
			_rtk_fc_acl_mib_clear(idx);

		{
			//[FLOW mib2][TODO] clear ACL rules with mib action
			for(idx = 0 ; idx < MAX_ACL_SW_ENTRY_SIZE; idx++)
			{
				if(fc_db.acl_SW_table_entry[idx].valid != ENABLED)
					continue;
				if(_rtk_fc_aclSWEntry_get(idx, &fc_db.aclSWEntry))
					return (RT_ERR_RG_ACL_SW_ENTRY_ACCESS_FAILED);
				if(((fc_db.aclSWEntry.acl_filter.action_fields) & RT_ACL_ACTION_LOGGING_GROUP_MIB_BIT))
				{
					if(_rtk_fc_aclFilterAndQos_del(idx))
					WARNING("Fail to force delete entry %d (Interface TRX counter enble)", idx);
				}
			}
		}

		{
			if(tmpVal == RT_STAT_FLOW_MIB_MODE_DEFAULT)
			{
				// sync flow meter setting to HW
				for(idx = 0 ; idx < RTK_FC_TABLESIZE_FBMTR ; idx++)
				{
					hw_idx = idx + G3_FLOW_POLICER_IDXSHIFT_FLOWMTR;
					if(_rtk_fc_g3L3Policer2_set(hw_idx, fc_db.l34Meter[idx].rate, fc_db.l34Meter[idx].ifgInclude))
						WARNING("Sync flow meter rate/ifgInclude from SW[%u] to hw policer[%u] fail", idx, hw_idx);
					if(_rtk_fc_g3L3Policer2BurstSize_set(hw_idx, fc_db.l34Meter[idx].bucketSize))
						WARNING("Sync flow meter bucketSize from SW[%u] to hw policer[%u] fail", idx, hw_idx);
					if(_rtk_fc_g3L3Policer2RateMode_set(hw_idx, fc_db.l34Meter[idx].meterMode))
						WARNING("Sync flow meterMode from SW[%u] to hw policer[%u] fail", idx, hw_idx);
				}
			}
			else
			{
				// clear flow meter setting to HW
				for(idx = 0 ; idx < RTK_FC_TABLESIZE_FBMTR ; idx++)
				{
					hw_idx = idx + G3_FLOW_POLICER_IDXSHIFT_FLOWMTR;
					if(_rtk_fc_g3L3Policer2_set(hw_idx, METER_RATE_MAX, ENABLED))
						WARNING("clear flow meter rate/ifgInclude to hw policer[%u] fail", hw_idx);
					if(_rtk_fc_g3L3Policer2BurstSize_set(hw_idx, METER_BURST_MAX))
						WARNING("Sync flow meter bucketSize to hw policer[%u] fail", hw_idx);
					if(_rtk_fc_g3L3Policer2RateMode_set(hw_idx, METER_MODE_BIT_RATE))
						WARNING("Sync flow meterMode to hw policer[%u] fail", hw_idx);
				}
			}
		}

		//update flow mib mode
		fc_db.controlFuc.flow_mib_mode = tmpVal;
		if(RTK_FC_ACL_POLREMAP_CNT != ori_acl_remap_cnt)
		{
			//rearrange ACL rule because RTK_FC_ACL_POLREMAP_CNT changed
			assert_ok(_rtk_rg_aclAndCfReservedRuleAddSpecial(RTK_CLS_TYPE_USER_ACL_REARRANGE, NULL));
		}

		fc_db.controlFuc.hwnat_mode = tmp_hwnat_mode;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(tmp_hwnat_mode);
	}
#else
	WARNING("Not support!");
#endif

	return count;
}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
void _rtk_fc_proc_flow_miss_meter_usage(void){
	rtlglue_printf("usage:\n");
	rtlglue_printf("  enable: FLOW_METER_IDX 0~%d\n", (RTK_FC_TABLESIZE_FBMTR-1));
	rtlglue_printf("  echo FLOW_METER_IDX > /proc/fc/ctrl/flow_miss_meter \n\n");
	rtlglue_printf("  disable: \n");
	rtlglue_printf("  echo %d > /proc/fc/ctrl/flow_miss_meter \n\n", SIGNED_INVALID);
}
int rtk_fc_proc_flow_miss_meter_get(struct seq_file *s, void *v)
{
	if((0 <= fc_db.controlFuc.flow_miss_meter_idx) && (fc_db.controlFuc.flow_miss_meter_idx < RTK_FC_TABLESIZE_FBMTR))
		PROC_PRINTF("enabled, use flow meter %d.\n", fc_db.controlFuc.flow_miss_meter_idx);
	else
		PROC_PRINTF("disabled.\n");
	return SUCCESS;
}

int rtk_fc_proc_flow_miss_meter_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int ret;
	int meterIdx = _rtk_fc_proc_parsing_string_to_integer(buff, count);
	rt_rate_meter_mapping_t meterMapping;

	if(meterIdx == SIGNED_INVALID)
	{
		rtk_rg_flow_default_action_update(RTK_ASIC_FLOW_PROFILE_FLOW_5TUPLE, RTK_ASIC_FLOW_DEFACT_TYPE_TRAP);
		rtk_rg_flow_default_action_update(RTK_ASIC_FLOW_PROFILE_FLOW_2TUPLE, RTK_ASIC_FLOW_DEFACT_TYPE_TRAP);
		
		rtlglue_printf("Disable flow miss meter.\n\n");
		fc_db.controlFuc.flow_miss_meter_idx = SIGNED_INVALID;
		return count;
	}
	else if((0 <= meterIdx) && (meterIdx < RTK_FC_TABLESIZE_FBMTR))
	{
		if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_EXTRA)
		{
			rtlglue_printf("[Error] do not support flow miss meter when flow_mib_mode is %d\n", RT_STAT_FLOW_MIB_MODE_EXTRA);
			return -EFAULT;
		}

		ret = RTK_FC_HELPER_RT_RATE_SHAREMETER_MAPPING_HW_GET(meterIdx, &meterMapping);
		if(ret != SUCCESS)
		{
			rtlglue_printf("[Error] Get rt meter %d failed!\n", meterIdx);
			return -EFAULT;
		}
		if(meterMapping.type != RT_METER_TYPE_FLOW)
		{
			rtlglue_printf("[Error] rt meter type %d is not FLOW!\n", meterMapping.type);
			return -EFAULT;
		}

		rtk_asic_flow_default_action_meter_update(RTK_ASIC_FLOW_DEFACT_TYPE_TRAP_WITH_METER, meterIdx);
		
		rtk_rg_flow_default_action_update(RTK_ASIC_FLOW_PROFILE_FLOW_5TUPLE, RTK_ASIC_FLOW_DEFACT_TYPE_TRAP_WITH_METER);
		rtk_rg_flow_default_action_update(RTK_ASIC_FLOW_PROFILE_FLOW_2TUPLE, RTK_ASIC_FLOW_DEFACT_TYPE_TRAP_WITH_METER);

		rtlglue_printf("Enable, flow miss meter use flow_meter[%d].\n\n", meterIdx);
		fc_db.controlFuc.flow_miss_meter_idx = meterIdx;
		return count;
	}
	else
		rtlglue_printf("Invalid configuration!\n\n");

	_rtk_fc_proc_flow_miss_meter_usage();
	return count;
}

void _rtk_fc_proc_umc_rate_limit_usage(void){
	rtlglue_printf("usage:\n");
	rtlglue_printf("Set limiting rate for unknown MC trap\n");
	rtlglue_printf("  echo rate RATE > /proc/fc/ctrl/umc_rate_limit \n");
	rtlglue_printf("  e.g. echo rate 100 > /proc/fc/ctrl/umc_rate_limit \n\n");
	rtlglue_printf("Set limiting mode for unknown MC trap (%d: METER_MODE_BIT_RATE, %d: METER_MODE_PACKET_RATE)\n", METER_MODE_BIT_RATE, METER_MODE_PACKET_RATE);
	rtlglue_printf("  e.g. echo mode 0 > /proc/fc/ctrl/umc_rate_limit \n\n");
}

int rtk_fc_proc_umc_rate_limit_get(struct seq_file *s, void *v)
{
	int ret;
	rtk_fc_asic_flow_defAct_type_t flow_empty_act, flow_aging_act;
	uint32 rate, burstSize;
	rtk_enable_t ifgIncludeTmp;
	rtk_rate_metet_mode_t mode;

	rtk_fc_asic_flow_default_action_get(RTK_ASIC_FLOW_PROFILE_FLOW_MC, &flow_empty_act, &flow_aging_act);
	rtlglue_printf("unknown MC action: ");
	if(flow_empty_act == RTK_ASIC_FLOW_DEFACT_TYPE_DROP)
		rtlglue_printf("DROP\n\n");
	else if(flow_empty_act == RTK_ASIC_FLOW_DEFACT_TYPE_TRAP_UMC_STORM)
	{
		rtlglue_printf("Trap with rate limit\n");
		ret = _rtk_fc_g3L3Policer_get(G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, &rate, &ifgIncludeTmp);
		if(ret)
		{
			rtlglue_printf("%-12d\terror:%s %d\n", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, "get rate failed, ret=", ret);
			return FAILED;
		}
		ret = _rtk_fc_g3L3PolicerBurstSize_get(G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, &burstSize);
		if(ret)
		{
			rtlglue_printf("%-12d\terror:%s %d\n", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, "get burstSize failed, ret=", ret);
			return FAILED;
		}
		ret = _rtk_fc_g3L3PolicerRateMode_get(G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, &mode);
		if(ret)
		{
			rtlglue_printf("%-12d\terror:%s %d\n", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, "get meterMode failed, ret=", ret);
			return FAILED;
		}
		rtlglue_printf("\033[1;33ml3_policer1[%d]: rate %d %s (burstSize %d, ifgInclude: %s)\n\n\033[0m", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, rate, (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec", burstSize, (ifgIncludeTmp==DISABLED)?"DISABLED":"ENABLED");
	}
	else
		rtlglue_printf("[ERROR] strange default action!!! (default action type %d)\n\n", flow_empty_act);

	_rtk_fc_proc_umc_rate_limit_usage();

	return SUCCESS;
}

int rtk_fc_proc_umc_rate_limit_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int length = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	char *strptr,*split_str;
	uint32_t rate;
	asic_pol_rateMode_t mode;

	if (buff)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buff, length);

		strptr=tmpBuf;

		split_str=strsep(&strptr," ");
		if(split_str==NULL){
			_rtk_fc_proc_umc_rate_limit_usage();
			goto INVALID_PARA;
		}

		if(strcasecmp(split_str, "rate")==0) {
			split_str = strsep(&strptr," ");
			if(split_str==NULL){
				_rtk_fc_proc_umc_rate_limit_usage();
				goto INVALID_PARA;
			}
			rate = simple_strtol(split_str, NULL, 0);
			if(rtk_rg_l3_flow_policer_rate_set(G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, rate, rate) != ASIC_RET_SUCCESS)
			{
				WARNING("[Error] set policer[%d] rate for MC hash default action TRAP with rate limit failed.", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP);
				goto INVALID_PARA;
			}
		}else if (strcasecmp(split_str, "mode")==0) {
			split_str = strsep(&strptr," ");
			if(split_str==NULL){
				_rtk_fc_proc_umc_rate_limit_usage();
				goto INVALID_PARA;
			}
			mode = simple_strtol(split_str, NULL, 0);

			if(rtk_rg_l3_flow_policer_rateMode_set(G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, mode) != ASIC_RET_SUCCESS)
			{
				WARNING("[Error] set policer[%d] mode for MC hash default action TRAP with rate limit failed.", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP);
				goto INVALID_PARA;
			}
		}
		else
		{
			rtlglue_printf("unknown configuration! \n");
			_rtk_fc_proc_umc_rate_limit_usage();
			goto INVALID_PARA;
		}
	}

INVALID_PARA:
	return count;
}

static void rtk_fc_proc_wan_ingress_shaper_usage(void)
{
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("    echo ?  > /proc/fc/... to dump current setting.\n");
	rtlglue_printf("    echo 1 [rate] > /proc/fc/... to enable shaper, rate should be 0-%dkbps.\n", RT_RATE_HW_EGR_RATE_MAX);
	rtlglue_printf("    echo 0  > /proc/fc/... to disable shaper.\n");
}

int rtk_fc_proc_wan_ingress_shaper_get(struct seq_file *s, void *v)
{
	if(fc_db.controlFuc.wan_ingress_shaper) {
		PROC_PRINTF("enabled with rate =  %d kbps.\n", fc_db.controlFuc.wan_ingress_shaper);
	}else
		PROC_PRINTF("disabled.\n");
	return SUCCESS;
}

int rtk_fc_proc_wan_ingress_shaper_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int ret = SUCCESS;
	char *cmdParam[CMD_PARAM_COUNT] = {0};
	aal_l2_te_shaper_tbc_cfg_msk_t profile_msk = {0};
	aal_l2_te_shaper_tbc_cfg_t profile = {0};
	uint32 i = 0, rate = 0, burst_size = 0;
	uint8 profileid = 0;

	if((ret = _rtk_fc_proc_parsing_string_to_param_arrary(buff, count, &cmdParam[0])) != SUCCESS) {
		goto INVALID_CMD;
	}
	// show detail configuration
	if(strcasecmp(cmdParam[0], "?")==0) {
		goto INVALID_CMD;
		
	}else if(strcasecmp(cmdParam[0], "1")==0) {
		// enable
		if(cmdParam[1]==NULL) {
			goto INVALID_CMD;
		}
		rate = simple_strtol(cmdParam[1], NULL, 0);
		
		if(rate > RT_RATE_HW_EGR_RATE_MAX)
			goto INVALID_CMD;
			
		burst_size = (rate/1000 + 511) >> 9;
		burst_size = (burst_size == 0) ? 1: burst_size;
		
		profile_msk.s.bs     = 1;
		profile_msk.s.rate_k = 1;
		profile_msk.s.rate_m = 1;
		profile_msk.s.mode   = 1;
		profile_msk.s.state  = 1;

		profile.bs     = burst_size;    /* in unit of 256 bytes */
		profile.rate_k = rate % 1000;
		profile.rate_m = rate / 1000;
		profile.mode   = /*CA_AAL_SHAPER_MODE_BPS*/ 0;
		profile.state  = /*enable ? CA_AAL_SHAPER_ADMIN_STATE_SHAPER : CA_AAL_SHAPER_ADMIN_STATE_FORWARD*/ 2;

		if((ret = aal_l2_te_shaper_port_tbc_set(0, AAL_PPORT_L3_WAN, profile_msk, &profile)) != SUCCESS)
			rtlglue_printf("aal config fail, ret = %d\n", ret);

		if((ret = aal_l2_tm_cb_voq_profile_sel_get(0, (AAL_PPORT_NI0<<3), &profileid)) != SUCCESS)
			rtlglue_printf("aal config fail, ret = %d\n", ret);

		// change to port 0 voq profile
		for(i = 0; i < 8; i ++) {
			if((ret = aal_l2_tm_cb_voq_profile_sel_set(0, ((AAL_PPORT_L3_WAN<<3) + i), profileid)) != SUCCESS)
				rtlglue_printf("aal config fail, ret = %d\n", ret);
		}
		
		fc_db.controlFuc.wan_ingress_shaper = rate;
	}else if(strcasecmp(cmdParam[0], "0")==0) {
		// disable
		profile_msk.s.state  = 1;
		profile.state  = 0;
		
		if((ret = aal_l2_te_shaper_port_tbc_set(0, 10, profile_msk, &profile)) != SUCCESS)
			rtlglue_printf("aal config fail, ret = %d\n", ret);

		if((ret = aal_l2_tm_cb_voq_profile_sel_get(0, (AAL_PPORT_L3_LAN<<3), &profileid)) != SUCCESS)
			rtlglue_printf("aal config fail, ret = %d\n", ret);

		// change to port 13 voq profile
		for(i = 0; i < 8; i ++) {
			if((ret = aal_l2_tm_cb_voq_profile_sel_set(0, ((AAL_PPORT_L3_WAN<<3) + i), profileid)) != SUCCESS)
				rtlglue_printf("aal config fail, ret = %d\n", ret);
		}
		
		fc_db.controlFuc.wan_ingress_shaper = 0;
	}

	return count;
	
INVALID_CMD:	
	rtk_fc_proc_wan_ingress_shaper_usage();

	return count;
}
#endif

int rtk_fc_proc_netlink_sync_lut_get(struct seq_file *s, void *v)
{
	if(fc_db.controlFuc.netlink_sync_lut) {
		PROC_PRINTF("enabled\n");
	}else
		PROC_PRINTF("disabled.\n");
	return SUCCESS;
}

int rtk_fc_proc_netlink_sync_lut_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	int val = _rtk_fc_proc_parsing_string_to_integer(buff, count);
	if(val)
		fc_db.controlFuc.netlink_sync_lut = 1;
	else
		fc_db.controlFuc.netlink_sync_lut = 0;
	
	return count;
}



void _rtk_fc_proc_flow_hashInputConfig_usage(void){
	rtlglue_printf("usage:\n");
	rtlglue_printf("- modify configuration:\n");
	rtlglue_printf("  echo [item] [if_enabled] > /proc/fc/ctrl/flow_hash_input_config \n");
	rtlglue_printf("  [item]: item name. (i.e. \"cpri\" , \"dscp\", \"cvid\", \"svid\", \"spri\", \"vlan_deicfi\", \"streamId\", \"5tuple_mac\")\n");
	rtlglue_printf("  [if_enabled]: 0:DISABLE 1:ENABLED\n");
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	rtlglue_printf("-  dump HW flow keys:\n");
	rtlglue_printf("   echo dump hw_key > /proc/fc/ctrl/flow_hash_input_config \n");
#endif
}

int rtk_fc_proc_flow_hashInputConfig_get(struct seq_file *s, void *v)
{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	bool if_hw_spri = FALSE, if_hw_vlan_deicfi = FALSE;
#else
	bool if_hw_spri = TRUE, if_hw_vlan_deicfi = TRUE;
#endif
#if defined(CONFIG_FC_RTL9607C_SERIES) || defined(CONFIG_FC_RTL9607C_RTL9603CVD_HYBRID)
	if(ASICDRIVERVER==0x1)
	{
		PROC_PRINTF("Not support!\n");
	}else
#endif
	{
		PROC_PRINTF("FB hash with %-15s: %-10s\n", "cpri", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CPRI]?"disable":"enable");
		PROC_PRINTF("FB hash with %-15s: %-10s\n", "dscp", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_DSCP]?"disable":"enable");
		PROC_PRINTF("FB hash with %-15s: %-10s\n", "cvid", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CVID]?"disable":"enable");
		PROC_PRINTF("FB hash with %-15s: %-10s\n", "svid", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_SVID]?"disable":"enable");
		PROC_PRINTF("FB hash with %-15s: %-10s \033[1;33m%s\033[0m\n", "spri", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_SPRI]?"disable":"enable", if_hw_spri?"":"(support only by SW flow)");
		PROC_PRINTF("FB hash with %-15s: %-10s \033[1;33m%s\033[0m\n", "vlan_deicfi", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_VLAN_DEICFI]?"disable":"enable", if_hw_vlan_deicfi?"":"(support only by SW flow)");
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
		PROC_PRINTF("FB hash with %-15s: %-10s \033[1;33m%s\033[0m\n", "streamId", fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_STREAM_IDX]?"enable":"disable", "(5 tuple: support only by SW flow, 2 tuple: support in both SW and HW flow)");
#else
		PROC_PRINTF("FB hash with %-15s: %-10s\n", "streamId", fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_STREAM_IDX]?"enable":"disable");
		PROC_PRINTF("FB hash with %-15s: %-10s\n", "5tuple_mac", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH345_SKIP_MAC]?"disable":"enable");

#endif
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		PROC_PRINTF("\n\n");
		PROC_PRINTF("\033[1;33mNote.   cpri/spri are global setting. Both configuration will be modified at the same time.\033[0m\n");
		PROC_PRINTF("\033[1;33mNote.2. cvid/svid are global setting. Both configuration will be modified at the same time.\033[0m\n");
#endif
	}
	return SUCCESS;
}

int _rtk_fc_flow_hashInputConfig_sw_status_update(hashPaternType_t type, int if_enable)
{
	rtk_enable_t if_skip = if_enable?DISABLED:ENABLED;
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	bool if_hw_spri = FALSE, if_hw_vlan_deicfi = FALSE;
#else
	bool if_hw_spri = TRUE, if_hw_vlan_deicfi = TRUE;
#endif

	switch(type)
	{
		case HASHPATERNTYPE_CPRI:
			if(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CPRI] != if_skip)
				rtk_fc_flow_flush_all();
			RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH12_SKIP_CPRI, if_skip);
			RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH34_UCBC_SKIP_CPRI, if_skip);
			RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH34_MC_SKIP_CPRI, if_skip);
			RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH5_SKIP_CPRI, if_skip);
			if(
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CPRI] != if_skip) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CPRI] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH34_UCBC_SKIP_CPRI]) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CPRI] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH34_MC_SKIP_CPRI]) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CPRI] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH5_SKIP_CPRI])
			)
				WARNING("Change configuration of \"CPRI\" as flow hash pattern failed!");
			DEBUG("FB hash with cpri: %s", if_enable?"ENABLE":"DISABLE");
			break;
		case HASHPATERNTYPE_DSCP:
			if(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_DSCP] != if_skip)
				rtk_fc_flow_flush_all();
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATHALL_SKIP_DSCP, if_skip), SUCCESS);
			fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_TOS] = if_enable;
			fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH34_TOS] = if_enable;
			fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH5_TOS] = if_enable;
			fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH6_TOS] = if_enable;

			if(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_DSCP] != if_skip)
				WARNING("Change configuration of \"DSCP\" as flow hash pattern failed!");
			DEBUG("FB hash with dscp: %s", if_enable?"ENABLE":"DISABLE");
			break;
		case HASHPATERNTYPE_CVID:
			if(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CVID] != if_skip)
				rtk_fc_flow_flush_all();

			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH12_SKIP_CVID, if_skip), SUCCESS);
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH34_UCBC_SKIP_CVID, if_skip), SUCCESS);
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH34_MC_SKIP_CVID, if_skip), SUCCESS);
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH5_SKIP_CVID, if_skip), SUCCESS);
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH6_SKIP_CVID, if_skip), SUCCESS);

			if(
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CVID] != if_skip) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CVID] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH34_UCBC_SKIP_CVID]) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CVID] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH34_MC_SKIP_CVID]) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CVID] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH5_SKIP_CVID]) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_CVID] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH6_SKIP_CVID])
			)
				WARNING("Change configuration of \"CVID\" as flow hash pattern failed!");
			DEBUG("FB hash with cvid: %s", if_enable?"ENABLE":"DISABLE");
			break;
		case HASHPATERNTYPE_SVID:
			if(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_SVID] != if_skip)
				rtk_fc_flow_flush_all();
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH12_SKIP_SVID, if_skip), SUCCESS);
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH34_UCBC_SKIP_SVID, if_skip), SUCCESS);
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH34_MC_SKIP_SVID, if_skip), SUCCESS);
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH5_SKIP_SVID, if_skip), SUCCESS);
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH6_SKIP_SVID, if_skip), SUCCESS);

			if(
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_SVID] != if_skip) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_SVID] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH34_UCBC_SKIP_SVID]) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_SVID] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH34_MC_SKIP_SVID]) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_SVID] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH5_SKIP_SVID]) ||
				(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH12_SKIP_SVID] != fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH6_SKIP_SVID])
			)
				WARNING("Change configuration of \"SVID\" as flow hash pattern failed!");
			DEBUG("FB hash with svid: %s", if_enable?"ENABLE":"DISABLE");
			break;
		case HASHPATERNTYPE_SPRI:
			if(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_SPRI] != if_skip)
				rtk_fc_flow_flush_all();
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATHALL_SKIP_SPRI, if_skip), SUCCESS);
			DEBUG("FB hash with spri: %s \033[1;33m%s\033[0m\n", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_SPRI]?"disable":"enable", if_hw_spri?"":"(support only by SW flow)");
			break;
		case HASHPATERNTYPE_VLAN_DEICFI:
			if(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_VLAN_DEICFI] != if_skip)
				rtk_fc_flow_flush_all();
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATHALL_SKIP_VLAN_DEICFI, if_skip), SUCCESS);
			DEBUG("FB hash with vlan_deicfi : %s \033[1;33m%s\033[0m\n", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_VLAN_DEICFI]?"disable":"enable", if_hw_vlan_deicfi?"":"(support only by SW flow)");
			break;
		case HASHPATERNTYPE_STREAMID:
			if(fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_STREAM_IDX] != if_enable)
				rtk_fc_flow_flush_all();
			fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_STREAM_IDX] = if_enable;
			fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH345_STREAM_IDX] = if_enable;
			DEBUG("FB hash with streamId: %s", if_enable?"ENABLE":"DISABLE");
			break;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		case HASHPATERNTYPE_MAC:
			if(fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH345_SKIP_MAC] != if_skip)
				rtk_fc_flow_flush_all();
			ASSERT_EQ(RTK_RG_ASIC_GLOBALSTATE_SET(FB_GLOBAL_PATH345_SKIP_MAC, if_skip), SUCCESS);
			DEBUG("FB hash with 5tuple_mac : %s \033[1;33m%s\033[0m\n", fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH345_SKIP_MAC]?"disable":"enable", if_hw_vlan_deicfi?"":"(support only by SW flow)");
			break;
#endif
		default:
			break;
	}
	return SUCCESS;
}

int rtk_fc_proc_flow_hashInputConfig_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	int if_enable = RTK_ENABLE_END;
	hashPaternType_t type = HASHPATERNTYPE_MAX;
	uint32 tmp_hwnat_mode;

#if defined(CONFIG_FC_RTL9607C_SERIES) || defined(CONFIG_FC_RTL9607C_RTL9603CVD_HYBRID)
	if(ASICDRIVERVER==0x1)
	{
		rtlglue_printf("Not support!\n");
		return count;
	}
#endif
	if (buff)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;

		split_str=strsep(&strptr," ");//get flow hash pattern
		if(split_str==NULL)
		{
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_flow_hashInputConfig_usage();
			return count;
		}

		if(strcasecmp(split_str,"CPRI")==0)
		{
			type = HASHPATERNTYPE_CPRI;
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
				return count;
			}
			if_enable = simple_strtol(split_str, NULL, 0);
		}
		else if(strcasecmp(split_str,"DSCP")==0)
		{
			type = HASHPATERNTYPE_DSCP;
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
				return count;
			}
			if_enable = simple_strtol(split_str, NULL, 0);
		}
		else if(strcasecmp(split_str,"CVID")==0)
		{
			type = HASHPATERNTYPE_CVID;
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
				return count;
			}
			if_enable = simple_strtol(split_str, NULL, 0);
		}
		else if(strcasecmp(split_str,"SVID")==0)
		{
			type = HASHPATERNTYPE_SVID;
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
				return count;
			}
			if_enable = simple_strtol(split_str, NULL, 0);
		}
		else if(strcasecmp(split_str,"SPRI")==0)
		{
			type = HASHPATERNTYPE_SPRI;
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
				return count;
			}
			if_enable = simple_strtol(split_str, NULL, 0);
		}
		else if(strcasecmp(split_str,"VLAN_DEICFI")==0)
		{
			type = HASHPATERNTYPE_VLAN_DEICFI;
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
				return count;
			}
			if_enable = simple_strtol(split_str, NULL, 0);
		}
		else if(strcasecmp(split_str,"streamId")==0)
		{
			type = HASHPATERNTYPE_STREAMID;
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
				return count;
			}
			if_enable = simple_strtol(split_str, NULL, 0);
		}

#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		else if(strcasecmp(split_str,"5tuple_mac")==0)
		{
			type = HASHPATERNTYPE_MAC;
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
				return count;
			}
			if_enable = simple_strtol(split_str, NULL, 0);
		}
		else if(strcasecmp(split_str,"DUMP")==0)
		{
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
				return count;
			}
			if(strcasecmp(split_str,"HW_KEY")==0)
			{
				dump_flow_key_conf();
				return count;
			}
			else
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_flow_hashInputConfig_usage();
			}
		}
#endif
		else{
			type = HASHPATERNTYPE_MAX;
			if_enable = DISABLED;
		}

		if(if_enable >= RTK_ENABLE_END)
		{
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_flow_hashInputConfig_usage();
			return count;
		}

		tmp_hwnat_mode = fc_db.controlFuc.hwnat_mode;
		fc_db.controlFuc.hwnat_mode = RT_FLOW_HWNAT_MODE_DIS_ACC;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(RT_FLOW_HWNAT_MODE_DIS_ACC);
		switch(type)
		{
			case HASHPATERNTYPE_DSCP:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_DSCP, if_enable));
				break;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			case HASHPATERNTYPE_CVID:
				/* fall through */
			case HASHPATERNTYPE_SVID:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_CVID, if_enable));
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_SVID, if_enable));
				break;
			case HASHPATERNTYPE_CPRI:
				/* fall through */
			case HASHPATERNTYPE_SPRI:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_CPRI, if_enable));
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_SPRI, if_enable));
				break;
			case HASHPATERNTYPE_MAC:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_MAC, if_enable));
				break;
#else
			case HASHPATERNTYPE_CVID:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_CVID, if_enable));
				break;
			case HASHPATERNTYPE_SVID:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_SVID, if_enable));
				break;
			case HASHPATERNTYPE_CPRI:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_CPRI, if_enable));
				break;
			case HASHPATERNTYPE_SPRI:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_SPRI, if_enable));
				break;
#endif
			case HASHPATERNTYPE_VLAN_DEICFI:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_VLAN_DEICFI, if_enable));
				break;
			case HASHPATERNTYPE_STREAMID:
				assert_ok(_rtk_fc_flow_hashInputConfig_sw_status_update(HASHPATERNTYPE_STREAMID, if_enable));
				break;
			default:
				break;
		}
		rtk_fc_abstrSwFlowPatternSet();
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		assert_ok(_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP, NULL));
		rtk_fc_flow_tuple_init();
		assert_ok(_rtk_rg_aclAndCfReservedRuleDel(RTK_FC_ACLANDCF_RESERVED_ALL_TRAP));
#endif
		fc_db.controlFuc.hwnat_mode = tmp_hwnat_mode;
		RTK_FC_HELPER_MGR_HWNAT_MODE_SET(tmp_hwnat_mode);
		return count;
	}
	return -EFAULT;
}

int rtk_fc_proc_host_policing_config_get(struct seq_file *s, void *v)
{
	int i;
	rtk_mac_t zeroMac = {{0}};
	uint32 tmpRate;
	rtk_enable_t tmpIfgInclude;
	int16 hostPolConfList_idx = SIGNED_INVALID;
	rtk_fc_hostPoliceConf_list_t *pHostPolicerConfEntry = NULL, *pNextHostPolicerConfEntry = NULL;
	uint8 pri_cnt = 0;

#if defined(CONFIG_FC_CA8277B_SERIES)
	PROC_PRINTF("Not support hw host rate limiting, hw host counting: l3_policer2\n");
#endif

	PROC_PRINTF("Host Policing entries: (Shows valid Entries only)\n");
	PROC_PRINTF("--------------------\n");
	for(i = 0 ; i < RT_RATE_HOSTPOLICING_TABLE_SIZE ; i++)
	{
		if(!memcmp(&zeroMac, fc_db.hostPoliceInfoTable[i].hostPoliceControl.mac_addr, sizeof(rtk_mac_t)))
			continue;
		hostPolConfList_idx = fc_db.hostPoliceInfoTable[i].hostPolConfList_idx;
		PROC_PRINTF("[%-2d] %pM\n", i, fc_db.hostPoliceInfoTable[i].hostPoliceControl.mac_addr);
		PROC_PRINTF("hostPolConfList_idx: %d\n", hostPolConfList_idx);
		PROC_PRINTF("rate limit:\n");
		PROC_PRINTF("  -ingress: %s", fc_db.hostPoliceInfoTable[i].hostPoliceControl.ingress_limit_control?"   Enable":"  Disable");
		if(fc_db.hostPoliceInfoTable[i].hostPoliceControl.ingress_limit_control)
		{
			if(hostPolConfList_idx == SIGNED_INVALID)
				PROC_PRINTF("  (error to get ingress rate limit info! invalid hostPolConfList_idx) ");
			else
			{
				if(fc_db.hostPoliceConfList[hostPolConfList_idx].ingress_limit_by_swShaper)
				{
					tmpRate = fc_db.swMeter[fc_db.hostPoliceConfList[hostPolConfList_idx].ingress_limit_meter_hwIndex].rate;
					PROC_PRINTF("  (meterType: %4s, meter index: %-2d, meter rate: %d (HW meter index:%d))", "SW", fc_db.hostPoliceInfoTable[i].hostPoliceControl.ingress_limit_meter_index, tmpRate, fc_db.hostPoliceConfList[hostPolConfList_idx].ingress_limit_meter_hwIndex);
				}
				else
				{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
					(void)rtk_rate_shareMeter_get(fc_db.hostPoliceConfList[hostPolConfList_idx].ingress_limit_meter_hwIndex, &tmpRate, &tmpIfgInclude);
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
					if(fc_db.controlFuc.loopbackMode_is_enabled)
						rtk_rate_shareMeter_get(fc_db.hostPoliceConfList[hostPolConfList_idx].ingress_limit_meter_hwIndex, &tmpRate, &tmpIfgInclude);//L2 policer
					else
						_rtk_fc_g3L3Policer_get(fc_db.hostPoliceConfList[hostPolConfList_idx].ingress_limit_meter_hwIndex, &tmpRate, &tmpIfgInclude);
#endif
					PROC_PRINTF("  (meterType: %4s, meter index: %-2d, meter rate: %d (HW meter index:%d))", "HOST", fc_db.hostPoliceInfoTable[i].hostPoliceControl.ingress_limit_meter_index, tmpRate, fc_db.hostPoliceConfList[hostPolConfList_idx].ingress_limit_meter_hwIndex);
				}
			}
		}
		PROC_PRINTF("\n  -egress:  %s", fc_db.hostPoliceInfoTable[i].hostPoliceControl.egress_limit_control?"   Enable":"  Disable");
		if(fc_db.hostPoliceInfoTable[i].hostPoliceControl.egress_limit_control)
		{
			if(hostPolConfList_idx == SIGNED_INVALID)
				PROC_PRINTF("  (error to get egress rate limit info! invalid hostPolConfList_idx) ");
			else
			{
				if(fc_db.hostPoliceConfList[hostPolConfList_idx].egress_limit_by_swShaper)
				{
					tmpRate = fc_db.swMeter[fc_db.hostPoliceConfList[hostPolConfList_idx].egress_limit_meter_hwIndex].rate;
					PROC_PRINTF("  (meterType: %4s, meter index: %-2d, meter rate: %d (HW meter index:%d))", "SW", fc_db.hostPoliceInfoTable[i].hostPoliceControl.egress_limit_meter_index, tmpRate, fc_db.hostPoliceConfList[hostPolConfList_idx].egress_limit_meter_hwIndex);
				}
				else
				{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
					rtk_rate_shareMeter_get(fc_db.hostPoliceConfList[hostPolConfList_idx].egress_limit_meter_hwIndex, &tmpRate, &tmpIfgInclude);
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
					if(fc_db.controlFuc.loopbackMode_is_enabled)
						rtk_rate_shareMeter_get(fc_db.hostPoliceConfList[hostPolConfList_idx].egress_limit_meter_hwIndex, &tmpRate, &tmpIfgInclude);
					else
						_rtk_fc_g3L3Policer_get(fc_db.hostPoliceConfList[hostPolConfList_idx].egress_limit_meter_hwIndex, &tmpRate, &tmpIfgInclude);
#endif
					PROC_PRINTF("  (meterType: %4s, meter index: %-2d, meter rate: %d (HW meter index:%d))", "HOST", fc_db.hostPoliceInfoTable[i].hostPoliceControl.egress_limit_meter_index, tmpRate, fc_db.hostPoliceConfList[hostPolConfList_idx].egress_limit_meter_hwIndex);
				}
			}

		}
		PROC_PRINTF("\nmib_count_control: %s\n", fc_db.hostPoliceInfoTable[i].hostPoliceControl.mib_count_control?" Enable":"Disable");
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		if(hostPolConfList_idx == SIGNED_INVALID)
			PROC_PRINTF("  (error to get mib info! invalid hostPolConfList_idx) ");
		else
		{
#if defined(FC_F_HW_POL_OPTIMIZE_MODE)
			PROC_PRINTF("loggingRx_policerIdx: %3d\n", fc_db.hostPoliceConfList[hostPolConfList_idx].loggingRx_policerIdx);
			PROC_PRINTF("loggingTx_policerIdx: %3d\n", fc_db.hostPoliceConfList[hostPolConfList_idx].loggingTx_policerIdx);
#else
			PROC_PRINTF("loggingRx_policerIdx: %3d", fc_db.hostPoliceConfList[hostPolConfList_idx].loggingRx_policerIdx);

			if((G3_FLOW_POLICER_IDXSHIFT_HPLOGRX <= fc_db.hostPoliceConfList[hostPolConfList_idx].loggingRx_policerIdx) && (fc_db.hostPoliceConfList[hostPolConfList_idx].loggingRx_policerIdx < G3_FLOW_POLICER_IDXSHIFT_HPLOGRX + G3_FLOW_POLICER_HOSTLOGGINGRX_SIZE))
				PROC_PRINTF("	  (flow policer group: HPLOGRX, index in group: %d)\n", fc_db.hostPoliceConfList[hostPolConfList_idx].loggingRx_policerIdx - G3_FLOW_POLICER_IDXSHIFT_HPLOGRX);
			else
				PROC_PRINTF("\n");
			PROC_PRINTF("loggingTx_policerIdx: %3d", fc_db.hostPoliceConfList[hostPolConfList_idx].loggingTx_policerIdx);
			if((G3_FLOW_POLICER_IDXSHIFT_HPLOGTX <= fc_db.hostPoliceConfList[hostPolConfList_idx].loggingTx_policerIdx) && (fc_db.hostPoliceConfList[hostPolConfList_idx].loggingTx_policerIdx < G3_FLOW_POLICER_IDXSHIFT_HPLOGTX + G3_FLOW_POLICER_HOSTLOGGINGTX_SIZE))
				PROC_PRINTF("	  (flow policer group: HPLOGTX, index in group: %d)\n", fc_db.hostPoliceConfList[hostPolConfList_idx].loggingTx_policerIdx - G3_FLOW_POLICER_IDXSHIFT_HPLOGTX);
			else
				PROC_PRINTF("\n");
#endif
		}
#endif
		PROC_PRINTF("--------------------\n");
	}

	PROC_PRINTF("\n\n");
	pri_cnt = 0;
	pHostPolicerConfEntry = NULL;
	pNextHostPolicerConfEntry = NULL;
	PROC_PRINTF("hostPoliceConfList table in hostPoliceConf_swHw_freeListHead:\n\t");
	// traverse hostPoliceConf_swHw_freeListHead
	if(!list_empty(&fc_db.hostPoliceConf_swHw_freeListHead))
	{
		//just return the first entry right behind of head
		list_for_each_entry_safe(pHostPolicerConfEntry, pNextHostPolicerConfEntry, &fc_db.hostPoliceConf_swHw_freeListHead, hostPoliceConf_list)
		{
			PROC_PRINTF(" %s[%02d]", (pri_cnt==0)?"  ":"->", pHostPolicerConfEntry->idx);
			if(((++pri_cnt)%10) == 0)
				PROC_PRINTF("\n\t");
		}
		PROC_PRINTF("\n");
	}
	else
		PROC_PRINTF("empty\n\n");

	pri_cnt = 0;
	pHostPolicerConfEntry = NULL;
	pNextHostPolicerConfEntry = NULL;
	PROC_PRINTF("hostPoliceConfList table in hostPoliceConf_swOnly_freeListHead:\n\t");
	// traverse hostPoliceConf_swOnly_freeListHead
	if(!list_empty(&fc_db.hostPoliceConf_swOnly_freeListHead))
	{
		list_for_each_entry_safe(pHostPolicerConfEntry, pNextHostPolicerConfEntry, &fc_db.hostPoliceConf_swOnly_freeListHead, hostPoliceConf_list)
		{
			PROC_PRINTF(" %s[%02d]", (pri_cnt==0)?"  ":"->", pHostPolicerConfEntry->idx);
			if(((++pri_cnt)%10) == 0)
				PROC_PRINTF("\n\t");
		}
		PROC_PRINTF("\n");
	}
	else
		PROC_PRINTF("empty\n\n");

	return SUCCESS;
}

void _rtk_fc_proc_host_policing_usage(void){
	rtlglue_printf("usage:\n");
	rtlglue_printf("echo hostIndex MAC mibCountCtrl ingressLimitCtrl ingressLimitMeterIdx egressLimitCtrl egressLimitMeterIdx [ingressLimitRate egressLimitRate] > /proc/fc/ctrl/host_policing_config \n");
	rtlglue_printf("hostIndex: 0~%d\n", RT_RATE_HOSTPOLICING_TABLE_SIZE-1);
	rtlglue_printf("mibCountCtrl: 0:DISABLE 1:ENABLED\n");
	rtlglue_printf("ingressLimitCtrl: 0:DISABLE 1:ENABLED\n");
	rtlglue_printf("ingressLimitMeterIdx\n");
	rtlglue_printf("egressLimitCtrl: 0:DISABLE 1:ENABLED\n");
	rtlglue_printf("egressLimitMeterIdx\n");
	rtlglue_printf("[optional parameters]:\n");
	rtlglue_printf("ingressLimitRate: -1 or %d~%d (-1 means do not change meter[ingressLimitMeterIdx] configuration)\n", METER_RATE_MIN, METER_RATE_MAX);
	rtlglue_printf("egressLimitRate: -1 or %d~%d (-1 means do not change meter[egressLimitMeterIdx] configuration)\n", METER_RATE_MIN, METER_RATE_MAX);
}

//.unlockBefortWrite=1
int rtk_fc_proc_host_policing_config_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	rt_rate_host_policer_control_t hostPoliceEntry;
	int hostIdx, tempEn, ret;
	rtk_mac_t tempMac;
	int32_t tempRate;

	memset(&hostPoliceEntry, 0, sizeof(hostPoliceEntry));
	hostPoliceEntry.ingress_limit_rate = RT_RATE_HOST_METER_RATE_UNCHANGE;
	hostPoliceEntry.egress_limit_rate = RT_RATE_HOST_METER_RATE_UNCHANGE;

	if (buff)
	{
		//fc_db.debug_level=simple_strtoul(tmpBuf, NULL, 16);
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, len);

		strptr=tmpBuf;

		split_str=strsep(&strptr," ");//get host polinging index
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return len;
		}
		hostIdx = simple_strtol(split_str, NULL, 0);
		if(hostIdx < 0 ||  hostIdx >= RT_RATE_HOSTPOLICING_TABLE_SIZE){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return -EFAULT;
		}

		split_str=strsep(&strptr," ");	//get host MAC
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return len;
		}
		_rtk_fc_str2mac(split_str, &tempMac);
		memcpy(hostPoliceEntry.mac_addr, tempMac.octet, sizeof(hostPoliceEntry.mac_addr));

		split_str=strsep(&strptr," ");	//get mibCount Control
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return len;
		}
		tempEn = simple_strtol(split_str, NULL, 0);
		if((tempEn < 0) ||  (tempEn > 2)){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return -EFAULT;
		}
		hostPoliceEntry.mib_count_control= tempEn;

		split_str=strsep(&strptr," ");	//get ingress rate limit control
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return len;
		}

		tempEn = simple_strtol(split_str, NULL, 0);
		if((tempEn < 0) ||  (tempEn > 2)){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return -EFAULT;
		}
		hostPoliceEntry.ingress_limit_control = tempEn;

		split_str=strsep(&strptr," ");	//get ingress rate limit meter index
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return len;
		}
		hostPoliceEntry.ingress_limit_meter_index= simple_strtol(split_str, NULL, 0);

		split_str=strsep(&strptr," ");	//get egress rate limit meter index
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return len;
		}
		tempEn = simple_strtol(split_str, NULL, 0);
		if((tempEn < 0) ||  (tempEn > 2)){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return -EFAULT;
		}
		hostPoliceEntry.egress_limit_control = tempEn;

		split_str=strsep(&strptr," ");	//get egress rate limit meter index
		if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return len;
		}
		hostPoliceEntry.egress_limit_meter_index = simple_strtol(split_str, NULL, 0);

		split_str=strsep(&strptr," ");	//get ingress and egress meter rate [OPTIONAL]
		if(split_str != NULL){
			tempRate = simple_strtol(split_str, NULL, 0);
			if(tempRate == -1)
				hostPoliceEntry.ingress_limit_rate = RT_RATE_HOST_METER_RATE_UNCHANGE;
			else if((METER_RATE_MIN <= tempRate) && (tempRate <= METER_RATE_MAX))
				hostPoliceEntry.ingress_limit_rate = tempRate;
			else
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_host_policing_usage();
				return -EFAULT;
			}

			split_str=strsep(&strptr," ");
			if(split_str==NULL){
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			_rtk_fc_proc_host_policing_usage();
			return len;
			}

			tempRate = simple_strtol(split_str, NULL, 0);
			if(tempRate == -1)
				hostPoliceEntry.egress_limit_rate = RT_RATE_HOST_METER_RATE_UNCHANGE;
			else if((METER_RATE_MIN <= tempRate) && (tempRate <= METER_RATE_MAX))
				hostPoliceEntry.egress_limit_rate = tempRate;
			else
			{
				rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
				_rtk_fc_proc_host_policing_usage();
				return -EFAULT;
			}
		}

		ret = rtk_fc_hostPoliceControl_set(hostIdx, hostPoliceEntry);

		if(ret != RT_ERR_RG_OK)
		{
			rtlglue_printf("[Error]set host policing entry [%d] fail!!! (ret=%d) \n", hostIdx, ret);
			_rtk_fc_proc_host_policing_usage();
		}
		return count;
	}
	return -EFAULT;
}

int rtk_fc_proc_hw_host_policing_mib_reset(struct file *file, const char *buffer, unsigned long count, void *data)
{
	int idx = _rtk_fc_proc_parsing_string_to_integer(buffer, count);
	int16 hostPolConfList_idx = SIGNED_INVALID;

	if(idx<0 || idx>=RT_RATE_HOSTPOLICING_TABLE_SIZE)
	{
		rtlglue_printf("[Error]invalid policing entry index!\n");
	}
	else
	{
		hostPolConfList_idx = fc_db.hostPoliceInfoTable[idx].hostPolConfList_idx;
		if(hostPolConfList_idx != SIGNED_INVALID)
		{
			_rtk_fc_hwHostPolingMib_clear(hostPolConfList_idx);
			rtlglue_printf("clear policing entry [%d] HW counter!\n", idx);
		}

	}

	return count;
}

int rtk_fc_proc_sw_host_policing_mib_reset(struct file *file, const char *buffer, unsigned long count, void *data)
{
	int idx = _rtk_fc_proc_parsing_string_to_integer(buffer, count);
	int16 hostPolConfList_idx = SIGNED_INVALID;

	if(idx<0 || idx>=RT_RATE_HOSTPOLICING_TABLE_SIZE)
	{
		rtlglue_printf("[Error]invalid policing entry index!\n");
	}
	else
	{
		hostPolConfList_idx = fc_db.hostPoliceInfoTable[idx].hostPolConfList_idx;
		if(hostPolConfList_idx != SIGNED_INVALID)
		{
			bzero(&fc_db.hostPoliceConfList[hostPolConfList_idx].swHostPoliceCounter, sizeof(fc_db.hostPoliceConfList[hostPolConfList_idx].swHostPoliceCounter));
			rtlglue_printf("clear policing entry [%d] SW counter!\n", idx);
		}
	}
	return count;
}

static int rtk_fc_proc_l2Ent_display(struct seq_file *s, void *v, uint32 hashidx, rtk_fc_lut_entry_t *lut)
{
	int32 timeOutsec=-1;
	
	if(lut->isStatic) {
		// !mc && static => gatewaymac
		timeOutsec = -1;
	}
	else {
		RTK_FC_HELPER_JIFFIES_TO_SECS((jiffies - lut->last_jiffies), &timeOutsec);
	}

#if defined(CONFIG_FC_G3_G3LITE_SERIES)
	PROC_PRINTF(" - hash[%d] swIdx[\033[1;33;40m%d\033[0m] hashliteIdx[%d] mac:%pM spa:%d extspa:%d wlanIdx:%d cvlan:%d fid:%d\n   isStatic:%d (refCnt:%d) syncPs:%d Idle(s)=%d hostPolIdx=%d wanPermit=%d\n",
	hashidx,lut->lutIdx,lut->daHashLiteIdx,lut->l2Addr,lut->spa,lut->extspa,lut->wlan_dev_idx,lut->CTAG_IF?lut->CVID:-1, lut->svl_fid,lut->isStatic,lut->staticRefCount,lut->isSync2PsEntry,timeOutsec, lut->hostPolIdx, lut->isPermitWanAccess);
#else
#if defined(CONFIG_RTL8198XB_SERIES) || defined(CONFIG_RTL8198E_SERIES)
	PROC_PRINTF(" - hash[%d] hwIdx[\033[1;33;40m%d\033[0m] mac:%pM spa:%d extspa:%d wlanIdx:%d cvlan:%d fid:%d spa2:%d spa3:%d spa4:%d spa5:%d wlanIdx2:%d\n   isStatic:%d (refCnt:%d) syncPs:%d Idle(s)=%d hostPolIdx=%d wanPermit=%d isUnknowUcMc=%d anyFlowEstablish=%d\n",
	hashidx,lut->lutIdx,lut->l2Addr,lut->spa,lut->extspa,lut->wlan_dev_idx,lut->CTAG_IF?lut->CVID:-1, lut->svl_fid,lut->spa2,lut->spa3,lut->spa4,lut->spa5,lut->wlan_dev_idx2,lut->isStatic,lut->staticRefCount,lut->isSync2PsEntry,timeOutsec, lut->hostPolIdx, lut->isPermitWanAccess,lut->isUnknowUcMc, lut->anyFlowEstablish);
#else
	PROC_PRINTF(" - hash[%d] hwIdx[\033[1;33;40m%d\033[0m] mac:%pM spa:%d extspa:%d wlanIdx:%d cvlan:%d fid:%d wlanIdx2:%d\n   isStatic:%d (refCnt:%d) syncPs:%d Idle(s)=%d hostPolIdx=%d wanPermit=%d isUnknowUcMc=%d anyFlowEstablish=%d\n",
	hashidx,lut->lutIdx,lut->l2Addr,lut->spa,lut->extspa,lut->wlan_dev_idx,lut->CTAG_IF?lut->CVID:-1, lut->svl_fid,lut->wlan_dev_idx2,lut->isStatic,lut->staticRefCount,lut->isSync2PsEntry,timeOutsec, lut->hostPolIdx, lut->isPermitWanAccess,lut->isUnknowUcMc, lut->anyFlowEstablish);
#endif
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	PROC_PRINTF(KERN_CONT"   wifiMacId:%d", lut->wifiMacId);
#endif
#if defined(CONFIG_RTL8198X_SERIES) && defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
	PROC_PRINTF(KERN_CONT"   extFlowmibIdx:%d", lut->ext_flowmib_idx);
#endif
#endif
	if(lut->isPortAggEntry)
		PROC_PRINTF(KERN_CONT"   isPortAggEntry:%d", lut->isPortAggEntry);

	// the end
	PROC_PRINTF("\n");

	if(fc_db.controlFuc.vlan_filtering) {
		uint32 i=0, s_offset=0;
		bool pvid_exist = FALSE;
		char *s_ptr = &fc_db.cmd_buf[0];

		bzero(s_ptr, sizeof(fc_db.cmd_buf));

		for(i=0 ; i<RTK_FC_TABLESIZE_LUT_IVL ; i++) {
			if(lut->ivl_sel[i].valid) {
				pvid_exist = TRUE;
				s_offset += snprintf(s_ptr+s_offset, sizeof(fc_db.cmd_buf)-s_offset, "[%d] vid:%d trf:%d\t", i, fc_db.lutIvlTbl[lut->ivl_sel[i].ivl_tbl_idx].ivl_pvid, lut->ivl_sel[i].traffic_bit);
			}
		}
		if(pvid_exist) {
			PROC_PRINTF("   pvid sel:%s\n", fc_db.cmd_buf);	
		}
	}



	return SUCCESS;
}

/* hwacc flows PROC*/
int rtk_fc_proc_flowLut_read(struct seq_file *s, void *v)
{
	int i=0, count=0;
	rtk_fc_lut_entry_t *lut;

	for(i=0;i<RTK_FC_LUT_BUCKET_SIZE;i++)
	{
		list_for_each_entry_rcu(lut,&fc_db.lut_hash_list_head[i],lutList)
		{
			count++;
			
			rtk_fc_proc_l2Ent_display(s, v, i, lut);

		}
	}

	PROC_PRINTF("\nTotal l2 learning count: %d\n", count);

	PROC_PRINTF("\nPORT\t");
	for(i=0;i<RTK_FC_MAC_PORT_MAX;i++) {
		if(atomic_read(&fc_db.portLinkupMask)&(1<<i))
		{
			PROC_PRINTF("%8d",i);
		}
	}	
	PROC_PRINTF("\nLimit\t");
	for(i=0;i<RTK_FC_MAC_PORT_MAX;i++) {
		if(atomic_read(&fc_db.portLinkupMask)&(1<<i))
		{
			PROC_PRINTF("%8d", fc_db.macAddrLearningLimit[i].learningLimit_conf.learningLimitNumber);
		}
	}
	PROC_PRINTF("\nCnt\t");
	for(i=0;i<RTK_FC_MAC_PORT_MAX;i++) {
		if(atomic_read(&fc_db.portLinkupMask)&(1<<i))
		{
			PROC_PRINTF("%8d", atomic_read(&fc_db.macAddrLearningLimit[i].learningCount));
		}
	}
	
	PROC_PRINTF("\n");
	return 0;
}

int rtk_fc_proc_flowLut_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	int i=0, l2idx = 0;
	rtk_mac_t l2addr;
	rtk_fc_lut_entry_t *lut;
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	char *strptr,*split_str;
	char *path_str = NULL;

	if(_rtk_fc_proc_path_string_get(file, &path_str)) {
		return len;
	}

	/* copy data to the buffer */
	strscpy(fc_db.cmd_buf, buffer, len);

	strptr=fc_db.cmd_buf;
	split_str=strsep(&strptr," ");

	if(strcasecmp(split_str,"idx")==0)
	{
		split_str=strsep(&strptr," ");
		if(!split_str) goto USAGE;
		
		l2idx = simple_strtoul(split_str,NULL,0);
		if(l2idx>=RTK_FC_TABLESIZE_LUT) goto USAGE;


		for(i=0;i<RTK_FC_LUT_BUCKET_SIZE;i++)
		{
			list_for_each_entry_rcu(lut, &fc_db.lut_hash_list_head[i],lutList)
			{
				if(lut->lutIdx == l2idx) {
	
					rtk_fc_proc_l2Ent_display(NULL, NULL, i, lut);
					
					return len;	
				}
			}
		}	
		
	}else if (strcasecmp(split_str,"mac")==0) {
				
		split_str=strsep(&strptr," ");
		if(!split_str) goto USAGE;
		_rtk_fc_str2mac(split_str, &l2addr);

		
		for(i=0;i<RTK_FC_LUT_BUCKET_SIZE;i++)
		{
			list_for_each_entry_rcu(lut, &fc_db.lut_hash_list_head[i],lutList)
			{
				if(!memcmp(&lut->l2Addr[0], &l2addr.octet[0], ETH_ALEN))
					rtk_fc_proc_l2Ent_display(NULL, NULL, i, lut);
			}
		}
	}else if(strcasecmp(split_str,"hash")==0)
	{
		split_str=strsep(&strptr," ");
		if(!split_str) goto USAGE;
		
		i = simple_strtoul(split_str,NULL,0);
		if(i>=RTK_FC_LUT_BUCKET_SIZE) goto USAGE;

		list_for_each_entry_rcu(lut, &fc_db.lut_hash_list_head[i],lutList)
		{
			rtk_fc_proc_l2Ent_display(NULL, NULL, i, lut);
		}
		
	}else if(strcasecmp(split_str,"port")==0)
	{
		split_str=strsep(&strptr," ");
		if(!split_str) goto USAGE;
		
		l2idx = simple_strtoul(split_str,NULL,0);
		if(l2idx>=RTK_FC_MAC_PORT_MAX) goto USAGE;

		for(i=0;i<RTK_FC_LUT_BUCKET_SIZE;i++) {
			list_for_each_entry_rcu(lut, &fc_db.lut_hash_list_head[i],lutList) {
				if(lut->spa == l2idx) {
					rtk_fc_proc_l2Ent_display(NULL, NULL, i, lut);
					return len;	
				}
			}
		}	
		
	}else if(strcasecmp(split_str,"count")==0)
	{
		for(i=0;i<RTK_FC_LUT_BUCKET_SIZE;i++) {
			list_for_each_entry_rcu(lut,&fc_db.lut_hash_list_head[i],lutList) {
				l2idx++;
			}
		}
		rtlglue_printf("\nTotal l2 learning count: %d\n", l2idx);
		
		rtlglue_printf("PORT\t");
		for(i=0;i<RTK_FC_MAC_PORT_MAX;i++) {
			if(atomic_read(&fc_db.portLinkupMask)&(1<<i))
			{
				rtlglue_printf(KERN_CONT"%8d",i);
			}
		}	
		rtlglue_printf("Limit\t");
		for(i=0;i<RTK_FC_MAC_PORT_MAX;i++) {
			if(atomic_read(&fc_db.portLinkupMask)&(1<<i))
			{
				rtlglue_printf(KERN_CONT"%8d", fc_db.macAddrLearningLimit[i].learningLimit_conf.learningLimitNumber);
			}
		}
		rtlglue_printf("Cnt\t");
		for(i=0;i<RTK_FC_MAC_PORT_MAX;i++) {
			if(atomic_read(&fc_db.portLinkupMask)&(1<<i))
			{
				rtlglue_printf(KERN_CONT"%8d", atomic_read(&fc_db.macAddrLearningLimit[i].learningCount));
			}
		}
	}else if(strcasecmp(split_str,"free_pool_info")==0){
		rtlglue_printf("\nlutEntry_pool_free: %u, fc_db.lutEntry_pool_max: %u (RTK_FC_LUTENTRY_POOL_SIZE: %u)\n", fc_db.lutEntry_pool_free, fc_db.lutEntry_pool_max, RTK_FC_LUTENTRY_POOL_SIZE);
		for(i = (fc_db.lutEntry_pool_max-1) ; i >= fc_db.lutEntry_pool_free ; i--)
		{
			rtlglue_printf("lutEntry_pool_ring[%d]: %pM (spa: %u, extspa: %u, wlan_dev_idx: %u)\n",
				i, fc_db.lutEntry_pool_ring[i]->l2Addr,
				fc_db.lutEntry_pool_ring[i]->spa, fc_db.lutEntry_pool_ring[i]->extspa, fc_db.lutEntry_pool_ring[i]->wlan_dev_idx);
		}
	}else {

USAGE:
		rtlglue_printf("\nusage: support idx/port/mac/hash to filter l2 table \n\t idx [num] \n\t port [num] \n\t mac [xx:xx:xx:xx:xx:xx] \n\t hash [num]\n");
		rtlglue_printf("       support learning count dump: \n\t echo count > %s \n\t", path_str);
		rtlglue_printf("       support free_pool_info dump:  \n\t echo free_pool_info > %s \n\t", path_str);
	}

	
	rtlglue_printf("\n");;

	return len;
}

int rtk_fc_proc_l2ivl_read(struct seq_file *s, void *v)
{	
	rtk_rg_err_code_t retval=0;
	int i;
	
	PROC_PRINTF(">>L2 IVL tablet: (table size:%d)\n", RTK_FC_TABLESIZE_LUT_IVL);
	for(i=0; i<RTK_FC_TABLESIZE_LUT_IVL; i++)
	{
		if(fc_db.lutIvlTbl[i].valid == TRUE ) {
			PROC_PRINTF(" - idx[%d] pvid:%d flow_ref_cnt:%d\n", i, fc_db.lutIvlTbl[i].ivl_pvid, fc_db.lutIvlTbl[i].flow_ref_cnt);
		}
	}
	
	return retval;
}

int dump_lutCam_list(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	int i;
	rtk_fc_lutCam_linkList_t *pLutCamEntry;

	PROC_PRINTF(">>lutCam list:\n");
	for(i=0; i<RTK_FC_LUT_BUCKET_SIZE; i++)
	{
		if(!list_empty(&fc_db.lutCam_hashListHead[i]))
		{
			PROC_PRINTF(" L2_Hash[%3d] : \n", i);
			list_for_each_entry(pLutCamEntry, &fc_db.lutCam_hashListHead[i], lut_list)
			{
				if(pLutCamEntry->lut_list.next!=&fc_db.lutCam_hashListHead[i])
					PROC_PRINTF("	 Flow idx[%d] ->\n", pLutCamEntry->idx);
				else
					PROC_PRINTF("	 Flow idx[%d]\n", pLutCamEntry->idx);
			}
			PROC_PRINTF("\n");
		}
	}
#if 0
	PROC_PRINTF(">>lutCam free list:\n");
	list_for_each_entry(pLutCamEntry, &fc_db.lutCam_freeListHead, lut_list)
	{
		if(pLutCamEntry->lut_list.next!=&fc_db.lutCam_freeListHead)
			PROC_PRINTF("	 Flow idx[%d] ->\n", pLutCamEntry->idx);
		else
			PROC_PRINTF("	 Flow idx[%d]\n", pLutCamEntry->idx);
	}
#endif
	PROC_PRINTF("\n");

	return retval;
}


int rtk_fc_proc_sw_flowStatistic_get(struct seq_file *s, void *data)
{
	int i = 0, j = 0;
	int p12Cnt, p34Cnt, p5Cnt, p6Cnt, p5tcp, p5udp;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	int p5other_l4Proto = 0;
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	int pMcWifiAmsduTxCnt = 0;
#endif
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	int pVxlanUsExtraTxCnt = 0;
#endif
	uint32 startTblIdx;

	p12Cnt=p34Cnt=p5Cnt=p6Cnt=p5tcp=p5udp=0;

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	startTblIdx = fc_db.flowHwTableSize;
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	startTblIdx = fc_db.flowHwTableSize;
#else
	startTblIdx = 0;
#endif
#endif

	for(i=startTblIdx; i<fc_db.flowSwTableSize; i++)
	{
#if defined(CONFIG_RTK_L34_CANDIDATE_FLOW)
		if(fc_db.flowTbl[i].candidateState!=CANDIDATE_STATE_NONE)
#else
		if(fc_db.flowTbl[i].pFlowEntry->path1.valid)
#endif
		{
#if defined(CONFIG_RTK_L34_G3_PLATFORM) && !(defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
		if(fc_db.flowTbl[i].mainHashIdx != G3_FLOWIDX_INVALID) continue; //HW flow entry
#endif

			switch(fc_db.flowTbl[i].pFlowEntry->path1.in_path)
			{
				case FB_PATH_12:
					p12Cnt++;
					break;
				case FB_PATH_34:
					p34Cnt++;
					break;
				case FB_PATH_5:
					p5Cnt++;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
					if(fc_db.flowTbl[i].pFlowEntry->path5.in_l4proto_num == IPPROTO_TCP)
						p5tcp++;
					else if(fc_db.flowTbl[i].pFlowEntry->path5.in_l4proto_num == IPPROTO_UDP)
						p5udp++;
					else
						p5other_l4Proto++;
#else
					if(fc_db.flowTbl[i].pFlowEntry->path5.in_l4proto == 1)
						p5tcp++;
					else
						p5udp++;
#endif
					break;
				case FB_PATH_6:
					p6Cnt++;
					break;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
				case FB_PATH_MC_WIFI_AMSDU_TX:
					pMcWifiAmsduTxCnt++;
					break;
#endif
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
				case FB_PATH_VXLAN_US_EXTRA_TX:
					pVxlanUsExtraTxCnt++;
					break;
#endif
			}
		}
	}
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
#if (defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)) && defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	PROC_PRINTF(" >> Sw flow usage: %d \n    path12: %d  path34: %d  path5: %d(tcp: %d udp: %d other: %d)  path6: %d, McWifiAmsduTx %d\n",
					p12Cnt+p34Cnt+p5Cnt+p6Cnt+pMcWifiAmsduTxCnt, p12Cnt, p34Cnt, p5Cnt, p5tcp, p5udp, p5other_l4Proto, p6Cnt, pMcWifiAmsduTxCnt);
#else
	PROC_PRINTF(" >> Sw flow usage: %d \n    path12: %d  path34: %d  path5: %d(tcp: %d udp: %d other: %d)  path6: %d\n",
					p12Cnt+p34Cnt+p5Cnt+p6Cnt, p12Cnt, p34Cnt, p5Cnt, p5tcp, p5udp, p5other_l4Proto, p6Cnt);
#endif
#else
	PROC_PRINTF(" >> Sw flow usage: %d \n    path12: %d  path34: %d  path5: %d(tcp: %d udp: %d)  path6: %d\n",
					p12Cnt+p34Cnt+p5Cnt+p6Cnt, p12Cnt, p34Cnt, p5Cnt, p5tcp, p5udp, p6Cnt);
#endif
#if (defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
	PROC_PRINTF("    VxlanUsExtraTxCnt:%d\n", pVxlanUsExtraTxCnt);
#endif

	if(fc_db.flowStatistic)
	{
		uint8 offset=16;
		uint32 flowAddTotalCnt = 0, flowCollision = 0;

		PROC_PRINTF("\n >> Flow hash usage:\n");
		PROC_PRINTF("Index/Offset\t");
		for(i=0; i<offset; i++)
		{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			PROC_PRINTF("%8d",i);
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
			PROC_PRINTF("%8d",i*2); //On G3 platform, the 64K index mapping to 32K index
#endif
		}
		PROC_PRINTF("\n");
		for(i=0; i<offset+2; i++)
			PROC_PRINTF("========");

		for(i=0; i<fc_db.flowHashBuckets; i++)
		{
			if((i%offset)==0x0)
			{
				uint8 skip = TRUE;
				for(j=i; j<(i+offset); j++)
				{
					if(fc_db.flowHashCount[j]>0)
					{
						skip = FALSE;
						break;
					}
				}
				if(skip)
				{
					i += (offset-1);
					continue;
				}
				else
				{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
					PROC_PRINTF("\nHash[%5d]:\t", i);
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
					PROC_PRINTF("\nHash[%5d]:\t", i*2); //On G3 platform, the 64K index mapping to 32K index
#endif
				}
			}
			PROC_PRINTF("%8d", fc_db.flowHashCount[i]);

			flowAddTotalCnt += fc_db.flowHashCount[i];
			if(fc_db.flowHashCount[i]>1)
				flowCollision += (fc_db.flowHashCount[i]-1);
		}

		PROC_PRINTF("\n >> flow hash collision rate = %d / %d \n", flowCollision, flowAddTotalCnt);

		flowCollision = 0;

		PROC_PRINTF("\n >> Flow hash hw collision count (hw entries of same flow hash are all used):\n");
		PROC_PRINTF("Index/Offset\t");
		for(i=0; i<offset; i++)
		{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			PROC_PRINTF("%8d",i);
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
			PROC_PRINTF("%8d",i*2); //On G3 platform, the 64K index mapping to 32K index
#endif
		}
		PROC_PRINTF("\n");
		for(i=0; i<offset+2; i++)
			PROC_PRINTF("========");

		for(i=0; i<fc_db.flowHashBuckets; i++)
		{
			if((i%offset)==0x0)
			{
				uint8 skip = TRUE;
				for(j=i; j<(i+offset); j++)
				{
					if(fc_db.flowHashHwCollisionCount[j]>0)
					{
						skip = FALSE;
						break;
					}
				}
				if(skip)
				{
					i += (offset-1);
					continue;
				}
				else
				{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
					PROC_PRINTF("\nHash[%5d]:\t", i);
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
					PROC_PRINTF("\nHash[%5d]:\t", i*2); //On G3 platform, the 64K index mapping to 32K index
#endif
				}
			}
			PROC_PRINTF("%8d", fc_db.flowHashHwCollisionCount[i]);
			flowCollision += fc_db.flowHashHwCollisionCount[i];
		}

		PROC_PRINTF("\n >> flow hash collision counts = %d \n", flowCollision);

		PROC_PRINTF("\n\n");
	}

	return 0;
}

int rtk_fc_proc_sw_flowStatistic_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	fc_db.flowStatistic = (_rtk_fc_proc_parsing_string_to_integer(buff,len)) == 0 ? FALSE : TRUE;
	memset(fc_db.flowHashCount, 0, sizeof(uint16)*fc_db.flowHashBuckets);
	memset(fc_db.flowHashHwCollisionCount, 0, sizeof(uint16)*fc_db.flowHashBuckets);
	rtlglue_printf("%d\n", fc_db.flowStatistic);
	return len;
}


int rtk_fc_proc_flowStatistic_read(struct seq_file *s, void *data)
{
	int i = 0, p12Cnt, p34Cnt, p5Cnt, p6Cnt, p5tcp, p5udp;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	int pMcWifiAmsduTxCnt = 0;
#endif
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	int pVxlanUsExtraTxCnt = 0;
#endif
	int cacheCnt, camCnt;
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	int fbmode = _rtk_rg_fbMode_get();
#endif
	uint32 checkTblSize;

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	checkTblSize = fc_db.flowHwTableSize;
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
	int p5other_l4Proto = 0;
#if defined(CONFIG_FC_RTL8277C_SERIES)
	int overFlowCnt = 0;
	checkTblSize = fc_db.flowHwTableSize;
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	checkTblSize = fc_db.flowHwTableSize;
#else
	checkTblSize = fc_db.flowSwTableSize;
#endif
#endif

	p12Cnt=p34Cnt=p5Cnt=p6Cnt=cacheCnt=camCnt=p5tcp=p5udp=0;


	for(i=0; i<checkTblSize; i++)
	{
#if defined(CONFIG_RTK_L34_CANDIDATE_FLOW)
		if(fc_db.flowTbl[i].candidateState!=CANDIDATE_STATE_NONE)
#else
		if(fc_db.flowTbl[i].pFlowEntry->path1.valid)
#endif
		{
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_RTL8277C_SERIES)
			if(RTK_FC_TABLESIZE_HASH_FLOW <= i) overFlowCnt++;
#elif defined(CONFIG_FC_RTL9607F_SERIES)
			// threre is no overFlow in CONFIG_FC_RTL9607F_SERIES
#else
			if(fc_db.flowTbl[i].mainHashIdx == G3_FLOWIDX_INVALID) continue; //SW flow entry
#endif
#endif
			switch(fc_db.flowTbl[i].pFlowEntry->path1.in_path)
			{
				case FB_PATH_12:
					p12Cnt++;
					break;
				case FB_PATH_34:
					p34Cnt++;
					break;
				case FB_PATH_5:
					p5Cnt++;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
					if(fc_db.flowTbl[i].pFlowEntry->path5.in_l4proto_num == IPPROTO_TCP)
						p5tcp++;
					else if(fc_db.flowTbl[i].pFlowEntry->path5.in_l4proto_num == IPPROTO_UDP)
						p5udp++;
					else
						p5other_l4Proto++;
#else
					if(fc_db.flowTbl[i].pFlowEntry->path5.in_l4proto == 1)
						p5tcp++;
					else
						p5udp++;
#endif
					break;
				case FB_PATH_6:
					p6Cnt++;
					break;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
				case FB_PATH_MC_WIFI_AMSDU_TX:
					pMcWifiAmsduTxCnt++;
					break;
#endif
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
				case FB_PATH_VXLAN_US_EXTRA_TX:
					pVxlanUsExtraTxCnt++;
					break;
#endif
			}
		}
	}
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	PROC_PRINTF(" >> Hw flow (%s) usage: %d \n    path12: %d  path34: %d  path5: %d(tcp: %d udp: %d)  path6: %d\n",
			(fbmode==FB_MODE_4K) ? "sram" : "dram", p12Cnt+p34Cnt+p5Cnt+p6Cnt, p12Cnt, p34Cnt, p5Cnt, p5tcp, p5udp, p6Cnt);

	if(fbmode!=FB_MODE_4K)
	{
		rtk_rg_asic_ccCheckAll_cmd(ENABLED, ENABLED);
		rtk_rg_asic_ccTblStatisticValidCnt_get(&cacheCnt, &camCnt);
		PROC_PRINTF(" >> Hw cache usage: 1st cache: %d  2nd cache: %d\n", cacheCnt, camCnt);
	}
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_RTL8277C_SERIES)
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	PROC_PRINTF(" >> Hw flow usage: %d \n    path12: %d  path34: %d  path5: %d(tcp: %d udp: %d other: %d)  path6: %d, McWifiAmsduTx %d (overFlowCnt: %d)\n",
		p12Cnt+p34Cnt+p5Cnt+p6Cnt+pMcWifiAmsduTxCnt, p12Cnt, p34Cnt, p5Cnt, p5tcp, p5udp, p5other_l4Proto, p6Cnt, pMcWifiAmsduTxCnt, overFlowCnt);
#else
	PROC_PRINTF(" >> Hw flow usage: %d \n    path12: %d  path34: %d  path5: %d(tcp: %d udp: %d other: %d)  path6: %d (overFlowCnt: %d)\n",
		p12Cnt+p34Cnt+p5Cnt+p6Cnt, p12Cnt, p34Cnt, p5Cnt, p5tcp, p5udp, p5other_l4Proto, p6Cnt, overFlowCnt);
#endif
#else
	// threre is no overFlow in CONFIG_FC_RTL9607F_SERIES
#if defined(CONFIG_FC_RTL9607F_SERIES) && defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	PROC_PRINTF(" >> Hw flow usage: %d \n    path12: %d  path34: %d  path5: %d(tcp: %d udp: %d other: %d)  path6: %d, McWifiAmsduTx %d\n",
		p12Cnt+p34Cnt+p5Cnt+p6Cnt+pMcWifiAmsduTxCnt, p12Cnt, p34Cnt, p5Cnt, p5tcp, p5udp, p5other_l4Proto, p6Cnt, pMcWifiAmsduTxCnt);
#else
	PROC_PRINTF(" >> Hw flow usage: %d \n    path12: %d  path34: %d  path5: %d(tcp: %d udp: %d other: %d)  path6: %d\n",
		p12Cnt+p34Cnt+p5Cnt+p6Cnt, p12Cnt, p34Cnt, p5Cnt, p5tcp, p5udp, p5other_l4Proto, p6Cnt);
#endif
#endif
#endif
#if (defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
	PROC_PRINTF("    VxlanUsExtraTxCnt: %d \n", pVxlanUsExtraTxCnt);
#endif

	return 0;
}


int rtk_fc_proc_flowStatistic_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	// do nothing

	return count;

}

/* debug level */

int rtk_fc_proc_debug_level_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	
	
	if (buffer)
	{
	
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);
		
		fc_db.debugLevel=simple_strtoul(tmpBuf, NULL, 16);
		
		rtk_fc_proc_debug_level_read(NULL,NULL);
		
		return count;
	}
	
	
	return -EFAULT;

}

int rtk_fc_proc_debug_level_read(struct seq_file *s, void *data)
{
	int bit = 0;
	char *name_of_debug_level[]={	//rtk_fc_debugLevel_t
		"DEBUG", "FIXME", "CALLBACK", "TRACE",
		"", "WARN", "TRACE_DUMP", "EVENT",
		"", "TABLE", "ALG", "IGMP",
		"ACL_RSV", "API", "TIMER", "PS",
		"IGR", "EGR", "WIFI", "ACL_CTRL",
		"DSLITE", "TOPS", "FRAGMENT", "SW_PATCH","IPSEC",
		"IPSEC_HOOK","TOPSTRACE","VXLAN"
	};
	
	PROC_PRINTF("FC Driver Debug level=0x%x ",fc_db.debugLevel);
	for(bit=0; bit<(sizeof(name_of_debug_level)/sizeof(name_of_debug_level[0])); bit++){
		if(strlen(name_of_debug_level[bit]))
			PROC_PRINTF("[0x%x:%s=%s]",(1<<bit), name_of_debug_level[bit], (fc_db.debugLevel&(1<<bit))?"\033[1;33mon\033[0m":"off");
	}
	PROC_PRINTF("\n");

	return 0;
}

int rtk_fc_proc_filter_level_read(struct seq_file *s, void *v)
{
	int len=0;
	int bit = 0;
	char *name_of_filter_level[]={	//part of rtk_fc_debugLevel_t, ex: ACL_CTRL not need filter level
		"DEBUG", "FIXME", "CALLBACK", "TRACE",
		"", "WARN", "TRACE_DUMP", "EVENT",
		"", "TABLE", "ALG", "IGMP",
		"", "API", "TIMER", "PS",
		"IGR", "EGR", "WIFI", "",
		"DSLITE", "TOPS", "FRAGMENT", "SW_PATCH","IPSEC",
		"IPSEC_HOOK","TOPSTRACE",
	};
	PROC_PRINTF("FC Driver Filter level=0x%x ",fc_db.filterLevel);
	for(bit=0; bit<(sizeof(name_of_filter_level)/sizeof(name_of_filter_level[0])); bit++){
		if(strlen(name_of_filter_level[bit]))
			PROC_PRINTF("[0x%x:%s=%s]",(1<<bit), name_of_filter_level[bit], (fc_db.filterLevel&(1<<bit))?"\033[1;33mon\033[0m":"off");
	}
	PROC_PRINTF("\n");

	return len;
}

int rtk_fc_proc_filter_level_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;

	if (buffer)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		fc_db.filterLevel=simple_strtoul(tmpBuf, NULL, 16);
		
		rtk_fc_proc_filter_level_read(NULL,NULL);
		return count;
	}
	return -EFAULT;
}



int rtk_fc_proc_traceFilter_read(struct seq_file *s, void *data)
{
	int i;
	int len=0;

	PROC_PRINTF("(Items: SPA,DA,SA,ETH,SIP,DIP,IP,L4PROTO,SPORT,DPORT,REASON,CVLAN,SVLAN,PPPOESSID,SIP6,DIP6,TIMES,DUMMYMC,WLANINDEX,FLOWINDEX,SIP_RANGE,DIP_RANGE,SIP6_RANGE,DIP6_RANGE,LEN_RANGE,TCP_FLAGS_MSK)\n");
	PROC_PRINTF("RULE MSK=%x\n",fc_db.traceFilterRuleMask);
	for(i=0;i<TRACFILTER_MAX;i++){
		PROC_PRINTF("FC Driver Trace Filter[%d]:0x%x \n",i,fc_db.trace_filter_bitmask[i]);
	}
	PROC_PRINTF("\nExample1(Dump TRACE_LOG when it is IPv4 unicast packet from CPU Port 9):\n");
	PROC_PRINTF("  echo \"SPA 9 DA 00:00:00:00:00:00 01:00:00:00:00:00  ETH 0800\" > /proc/fc/trace/trace_filter\n");
	PROC_PRINTF("  echo 8 > /proc/fc/trace/filter_level\n");
	PROC_PRINTF("  echo 8 > /proc/fc/trace/debug_level\n");
	PROC_PRINTF("Example2(Dump DEBUG_LOG when it is Broadcast ARP packet):\n");
	PROC_PRINTF("  echo \"DA FF:FF:FF:FF:FF:FF FF:FF:FF:FF:FF:FF  ETH 0806\" > /proc/fc/trace/trace_filter\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/filter_level\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/debug_level\n\n");
	PROC_PRINTF("Example3(rule option) select specific RULE from 0-3 (default used RULE 0) :\n");
	PROC_PRINTF("  echo \"RULE 3 SPA 2 SIP 192.168.1.1 DIP 8.8.8.8 \" > /proc/fc/trace/trace_filter\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/filter_level\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/debug_level\n\n");	
	PROC_PRINTF("Example4(Dump TRACE_LOG when IPv4 source address in the range 192.168.1.1~192.168.1.100) :\n");
	PROC_PRINTF("  echo \"SIP_RANGE 192.168.1.1 192.168.1.100 \" > /proc/fc/trace/trace_filter\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/filter_level\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/debug_level\n\n");
	PROC_PRINTF("Example5(Dump TRACE_LOG when packet len(w/o crc) in the range 100~200) :\n");
	PROC_PRINTF("  echo \"LEN_RANGE 100 200 \" > /proc/fc/trace/trace_filter\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/filter_level\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/debug_level\n\n");
	PROC_PRINTF("Example6(Dump TRACE_LOG when TCP flags exactly match 0x002(SYN):\n");
	PROC_PRINTF("  echo \"TCP_FLAGS_MSK 0xfff 0x002\" > /proc/rg/trace_filter\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/filter_level\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/debug_level\n\n");
	PROC_PRINTF("Example7(Dump TRACE_LOG when TCP flags have 0x002(SYN):\n");
	PROC_PRINTF("  echo \"TCP_FLAGS_MSK 0x002 0x002\" > /proc/fc/trace/trace_filter\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/filter_level\n");
	PROC_PRINTF("  echo 0x80000001 > /proc/fc/trace/debug_level\n\n");

	PROC_PRINTF("TCPFLAGS bits: \n");
	PROC_PRINTF("	000. .... .... = Reserved\n");
	PROC_PRINTF("	...N .... .... = Nonce\n");
	PROC_PRINTF("	.... C... .... = Congest Window Reduced(CWR)\n");
	PROC_PRINTF("	.... .E.. .... = ECN-Echo\n");
	PROC_PRINTF("	.... ..U. .... = Urgent\n");
	PROC_PRINTF("	.... ...A .... = Acknowledgment\n");
	PROC_PRINTF("	.... .... P... = Push\n");
	PROC_PRINTF("	.... .... .R.. = Reset\n");
	PROC_PRINTF("	.... .... ..S. = Syn\n");
	PROC_PRINTF("	.... .... ...F = Fin\n");

	for(i=0;i<TRACFILTER_MAX;i++){
		PROC_PRINTF("[RULE %d]:\n",i);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_SPA)
			PROC_PRINTF("SPA:%d\n",fc_db.trace_filter[i].spa);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_DA)
			PROC_PRINTF("DA:%02x:%02x:%02x:%02x:%02x:%02x MASK:%02x:%02x:%02x:%02x:%02x:%02x\n"
			,fc_db.trace_filter[i].dmac.octet[0]
			,fc_db.trace_filter[i].dmac.octet[1]
			,fc_db.trace_filter[i].dmac.octet[2]
			,fc_db.trace_filter[i].dmac.octet[3]
			,fc_db.trace_filter[i].dmac.octet[4]
			,fc_db.trace_filter[i].dmac.octet[5]
			,fc_db.trace_filter[i].dmac_mask.octet[0]
			,fc_db.trace_filter[i].dmac_mask.octet[1]
			,fc_db.trace_filter[i].dmac_mask.octet[2]
			,fc_db.trace_filter[i].dmac_mask.octet[3]
			,fc_db.trace_filter[i].dmac_mask.octet[4]
			,fc_db.trace_filter[i].dmac_mask.octet[5]);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_SA)
			PROC_PRINTF("SA:%02x:%02x:%02x:%02x:%02x:%02x MASK:%02x:%02x:%02x:%02x:%02x:%02x\n"
			,fc_db.trace_filter[i].smac.octet[0]
			,fc_db.trace_filter[i].smac.octet[1]
			,fc_db.trace_filter[i].smac.octet[2]
			,fc_db.trace_filter[i].smac.octet[3]
			,fc_db.trace_filter[i].smac.octet[4]
			,fc_db.trace_filter[i].smac.octet[5]
			,fc_db.trace_filter[i].smac_mask.octet[0]
			,fc_db.trace_filter[i].smac_mask.octet[1]
			,fc_db.trace_filter[i].smac_mask.octet[2]
			,fc_db.trace_filter[i].smac_mask.octet[3]
			,fc_db.trace_filter[i].smac_mask.octet[4]
			,fc_db.trace_filter[i].smac_mask.octet[5]);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_ETH)
			PROC_PRINTF("ETH:0x%04x\n",fc_db.trace_filter[i].ethertype);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_SIP)
			PROC_PRINTF("SIP:%d.%d.%d.%d\n"
			,(fc_db.trace_filter[i].sip>>24)&0xff
			,(fc_db.trace_filter[i].sip>>16)&0xff
			,(fc_db.trace_filter[i].sip>>8)&0xff
			,(fc_db.trace_filter[i].sip)&0xff);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_DIP)
			PROC_PRINTF("DIP:%d.%d.%d.%d\n"
			,(fc_db.trace_filter[i].dip>>24)&0xff
			,(fc_db.trace_filter[i].dip>>16)&0xff
			,(fc_db.trace_filter[i].dip>>8)&0xff
			,(fc_db.trace_filter[i].dip)&0xff);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_IP)
			PROC_PRINTF("IP:%d.%d.%d.%d\n"
			,(fc_db.trace_filter[i].ip>>24)&0xff
			,(fc_db.trace_filter[i].ip>>16)&0xff
			,(fc_db.trace_filter[i].ip>>8)&0xff
			,(fc_db.trace_filter[i].ip)&0xff);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_L4PROTO)
			PROC_PRINTF("L4PROTO:0x%04x\n",fc_db.trace_filter[i].l4proto);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_SPORT)
			PROC_PRINTF("SPORT:%d\n",fc_db.trace_filter[i].sport);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_DPORT)
			PROC_PRINTF("DPORT:%d\n",fc_db.trace_filter[i].dport);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_REASON)
			PROC_PRINTF("REASON:%d\n",fc_db.trace_filter[i].reason);

		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_CVLAN)
			PROC_PRINTF("CVLAN:%d\n",fc_db.trace_filter[i].cvlanid);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_SVLAN)
			PROC_PRINTF("SVLAN:%d\n",fc_db.trace_filter[i].svlanid);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_PPPOESESSIONID)
			PROC_PRINTF("PPPOE_SESSION:%d\n",fc_db.trace_filter[i].sessionid);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_V6SIP)
		{
			PROC_PRINTF("SIP6:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
			fc_db.trace_filter[i].sipv6[0],fc_db.trace_filter[i].sipv6[1],fc_db.trace_filter[i].sipv6[2],fc_db.trace_filter[i].sipv6[3],
			fc_db.trace_filter[i].sipv6[4],fc_db.trace_filter[i].sipv6[5],fc_db.trace_filter[i].sipv6[6],fc_db.trace_filter[i].sipv6[7],
			fc_db.trace_filter[i].sipv6[8],fc_db.trace_filter[i].sipv6[9],fc_db.trace_filter[i].sipv6[10],fc_db.trace_filter[i].sipv6[11],
			fc_db.trace_filter[i].sipv6[12],fc_db.trace_filter[i].sipv6[13],fc_db.trace_filter[i].sipv6[14],fc_db.trace_filter[i].sipv6[15]);
		}
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_V6DIP)
		{
			PROC_PRINTF("DIP6:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
			fc_db.trace_filter[i].dipv6[0],fc_db.trace_filter[i].dipv6[1],fc_db.trace_filter[i].dipv6[2],fc_db.trace_filter[i].dipv6[3],
			fc_db.trace_filter[i].dipv6[4],fc_db.trace_filter[i].dipv6[5],fc_db.trace_filter[i].dipv6[6],fc_db.trace_filter[i].dipv6[7],
			fc_db.trace_filter[i].dipv6[8],fc_db.trace_filter[i].dipv6[9],fc_db.trace_filter[i].dipv6[10],fc_db.trace_filter[i].dipv6[11],
			fc_db.trace_filter[i].dipv6[12],fc_db.trace_filter[i].dipv6[13],fc_db.trace_filter[i].dipv6[14],fc_db.trace_filter[i].dipv6[15]);
		}
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_SHOWNUMBEROFTIMES){
			//[FIXME]
			//PROC_PRINTF("TIMES is not support yet!!!");
			PROC_PRINTF("ShowNumberOfTimes:%d  counter=%d \n",fc_db.trace_filter[i].showNumberOfTimes,fc_db.trace_filter[i].showNumberOfTimesCounter);
		}
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_DUMMYMC){
			PROC_PRINTF("ShowDummyMcPkt:EN\n");
		}
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_WLAN_INDEX){
			PROC_PRINTF("INGRESS WLAN INDEX:%d\n",fc_db.trace_filter[i].wlan_index);
		}
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_FLOWINDEX){
			PROC_PRINTF("track flow index:%d\n",fc_db.trace_filter[i].flowindex);
		}
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_SIP_RANGE)
			PROC_PRINTF("SIP_RANGE:%d.%d.%d.%d~%d.%d.%d.%d\n"
			,(fc_db.trace_filter[i].sip_lowbound>>24)&0xff
			,(fc_db.trace_filter[i].sip_lowbound>>16)&0xff
			,(fc_db.trace_filter[i].sip_lowbound>>8)&0xff
			,(fc_db.trace_filter[i].sip_lowbound)&0xff
			,(fc_db.trace_filter[i].sip_highbound>>24)&0xff
			,(fc_db.trace_filter[i].sip_highbound>>16)&0xff
			,(fc_db.trace_filter[i].sip_highbound>>8)&0xff
			,(fc_db.trace_filter[i].sip_highbound)&0xff);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_DIP_RANGE)
			PROC_PRINTF("DIP_RANGE:%d.%d.%d.%d~%d.%d.%d.%d\n"
			,(fc_db.trace_filter[i].dip_lowbound>>24)&0xff
			,(fc_db.trace_filter[i].dip_lowbound>>16)&0xff
			,(fc_db.trace_filter[i].dip_lowbound>>8)&0xff
			,(fc_db.trace_filter[i].dip_lowbound)&0xff
			,(fc_db.trace_filter[i].dip_highbound>>24)&0xff
			,(fc_db.trace_filter[i].dip_highbound>>16)&0xff
			,(fc_db.trace_filter[i].dip_highbound>>8)&0xff
			,(fc_db.trace_filter[i].dip_highbound)&0xff);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_V6SIP_RANGE)
		{
			PROC_PRINTF("SIP6_RANGE:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x~%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
			fc_db.trace_filter[i].sipv6_lowbound[0],fc_db.trace_filter[i].sipv6_lowbound[1],fc_db.trace_filter[i].sipv6_lowbound[2],fc_db.trace_filter[i].sipv6_lowbound[3],
			fc_db.trace_filter[i].sipv6_lowbound[4],fc_db.trace_filter[i].sipv6_lowbound[5],fc_db.trace_filter[i].sipv6_lowbound[6],fc_db.trace_filter[i].sipv6_lowbound[7],
			fc_db.trace_filter[i].sipv6_lowbound[8],fc_db.trace_filter[i].sipv6_lowbound[9],fc_db.trace_filter[i].sipv6_lowbound[10],fc_db.trace_filter[i].sipv6_lowbound[11],
			fc_db.trace_filter[i].sipv6_lowbound[12],fc_db.trace_filter[i].sipv6_lowbound[13],fc_db.trace_filter[i].sipv6_lowbound[14],fc_db.trace_filter[i].sipv6_lowbound[15],
			fc_db.trace_filter[i].sipv6_highbound[0],fc_db.trace_filter[i].sipv6_highbound[1],fc_db.trace_filter[i].sipv6_highbound[2],fc_db.trace_filter[i].sipv6_highbound[3],
			fc_db.trace_filter[i].sipv6_highbound[4],fc_db.trace_filter[i].sipv6_highbound[5],fc_db.trace_filter[i].sipv6_highbound[6],fc_db.trace_filter[i].sipv6_highbound[7],
			fc_db.trace_filter[i].sipv6_highbound[8],fc_db.trace_filter[i].sipv6_highbound[9],fc_db.trace_filter[i].sipv6_highbound[10],fc_db.trace_filter[i].sipv6_highbound[11],
			fc_db.trace_filter[i].sipv6_highbound[12],fc_db.trace_filter[i].sipv6_highbound[13],fc_db.trace_filter[i].sipv6_highbound[14],fc_db.trace_filter[i].sipv6_highbound[15]);
		}
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_V6DIP_RANGE)
		{
			PROC_PRINTF("DIP6_RANGE:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x~%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
			fc_db.trace_filter[i].dipv6_lowbound[0],fc_db.trace_filter[i].dipv6_lowbound[1],fc_db.trace_filter[i].dipv6_lowbound[2],fc_db.trace_filter[i].dipv6_lowbound[3],
			fc_db.trace_filter[i].dipv6_lowbound[4],fc_db.trace_filter[i].dipv6_lowbound[5],fc_db.trace_filter[i].dipv6_lowbound[6],fc_db.trace_filter[i].dipv6_lowbound[7],
			fc_db.trace_filter[i].dipv6_lowbound[8],fc_db.trace_filter[i].dipv6_lowbound[9],fc_db.trace_filter[i].dipv6_lowbound[10],fc_db.trace_filter[i].dipv6_lowbound[11],
			fc_db.trace_filter[i].dipv6_lowbound[12],fc_db.trace_filter[i].dipv6_lowbound[13],fc_db.trace_filter[i].dipv6_lowbound[14],fc_db.trace_filter[i].dipv6_lowbound[15],
			fc_db.trace_filter[i].dipv6_highbound[0],fc_db.trace_filter[i].dipv6_highbound[1],fc_db.trace_filter[i].dipv6_highbound[2],fc_db.trace_filter[i].dipv6_highbound[3],
			fc_db.trace_filter[i].dipv6_highbound[4],fc_db.trace_filter[i].dipv6_highbound[5],fc_db.trace_filter[i].dipv6_highbound[6],fc_db.trace_filter[i].dipv6_highbound[7],
			fc_db.trace_filter[i].dipv6_highbound[8],fc_db.trace_filter[i].dipv6_highbound[9],fc_db.trace_filter[i].dipv6_highbound[10],fc_db.trace_filter[i].dipv6_highbound[11],
			fc_db.trace_filter[i].dipv6_highbound[12],fc_db.trace_filter[i].dipv6_highbound[13],fc_db.trace_filter[i].dipv6_highbound[14],fc_db.trace_filter[i].dipv6_highbound[15]);
		}
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_LEN_RANGE)
			PROC_PRINTF("LEN_RANGE:%d~%d\n"
			,fc_db.trace_filter[i].len_lowbound
			,fc_db.trace_filter[i].len_highbound);
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_TCP_FLAGS_MSK)
			PROC_PRINTF("TCP_FLAGS_MSK: enable 0x%x value 0x%x\n"
			,fc_db.trace_filter[i].tcp_flags_enable
			,fc_db.trace_filter[i].tcp_flags_value);
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		if(fc_db.trace_filter_bitmask[i]&FC_DEBUG_TRACE_FILTER_LDPID){
			PROC_PRINTF("LDPID:%d(Only for HeaderI filter timer)\n", fc_db.trace_filter[i].ldpid);
		}
#endif

	}
	return len;
}


int rtk_fc_proc_traceFilter_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;

	if (buffer)
	{
		//fc_db.debug_level=simple_strtoul(tmpBuf, NULL, 16);
		char *strptr,*split_str;
		int i=-1;

		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		strptr=tmpBuf;

		while(1)
		{
			split_str=strsep(&strptr," ");
			if(!split_str) break;
next_token:
			//printk("%d:%s\n",i++,split_str);

			if(i==-1){
				//MUST be the first parameter
				if(strcasecmp(split_str,"RULE")==0)
				{
					split_str=strsep(&strptr," ");
					if(!split_str) break;
					i=simple_strtol(split_str, NULL, 0);
					if(i>=TRACFILTER_MAX  || i<0)
						i=0;
				}else{
					i=0;
				}
				fc_db.trace_filter_bitmask[i]=0;
				memset(&fc_db.trace_filter[i],0,sizeof(rtk_fc_debugTraceFilter_t));
			}


			if(strcasecmp(split_str,"SPA")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_SPA;
				fc_db.trace_filter[i].spa=simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"DA")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_DA;
				_rtk_fc_str2mac(split_str,&fc_db.trace_filter[i].dmac);
				memset(fc_db.trace_filter[i].dmac_mask.octet,0xff,6);
				if(strptr==NULL)
					break;
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				if(strlen(split_str)<10) goto next_token;
				_rtk_fc_str2mac(split_str,&fc_db.trace_filter[i].dmac_mask);
			}
			else if(strcasecmp(split_str,"SA")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_SA;
				_rtk_fc_str2mac(split_str,&fc_db.trace_filter[i].smac);
				memset(fc_db.trace_filter[i].smac_mask.octet,0xff,6);
				if(strptr==NULL)
					break;
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				if(strlen(split_str)<10) goto next_token;
				_rtk_fc_str2mac(split_str,&fc_db.trace_filter[i].smac_mask);
			}
			else if(strcasecmp(split_str,"ETH")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_ETH;
				fc_db.trace_filter[i].ethertype=simple_strtol(split_str, NULL, 16);
			}
			else if(strcasecmp(split_str,"SIP")==0)
			{
				char *ip_token,*split_ip_token,j;
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_SIP;
				ip_token=split_str;
				fc_db.trace_filter[i].sip=0;
				for(j=0;j<4;j++)
				{
					split_ip_token=strsep(&ip_token,".");
					fc_db.trace_filter[i].sip|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
					if(ip_token==NULL) break;
				}
			}
			else if(strcasecmp(split_str,"DIP")==0)
			{
				char *ip_token,*split_ip_token,j;
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_DIP;
				ip_token=split_str;
				fc_db.trace_filter[i].dip=0;
				for(j=0;j<4;j++)
				{
					split_ip_token=strsep(&ip_token,".");
					fc_db.trace_filter[i].dip|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
					if(ip_token==NULL) break;
				}
			}
			else if(strcasecmp(split_str,"IP")==0)
			{
				char *ip_token,*split_ip_token,j;
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_IP;
				ip_token=split_str;
				fc_db.trace_filter[i].ip=0;
				for(j=0;j<4;j++)
				{
					split_ip_token=strsep(&ip_token,".");
					fc_db.trace_filter[i].ip|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
					if(ip_token==NULL) break;
				}
			}
			else if(strcasecmp(split_str,"L4PROTO")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_L4PROTO;
				fc_db.trace_filter[i].l4proto=simple_strtol(split_str, NULL, 16);
			}

			else if(strcasecmp(split_str,"SPORT")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_SPORT;
				fc_db.trace_filter[i].sport=simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"DPORT")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_DPORT;
				fc_db.trace_filter[i].dport=simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"REASON")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_REASON;
				fc_db.trace_filter[i].reason=simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"CVLAN")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_CVLAN;
				fc_db.trace_filter[i].cvlanid=simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"SVLAN")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_SVLAN;
				fc_db.trace_filter[i].svlanid=simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"PPPOESSID")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_PPPOESESSIONID;
				fc_db.trace_filter[i].sessionid=simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"TIMES")==0)
			{
				int32 showNumberOfTimes;
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_SHOWNUMBEROFTIMES;
				showNumberOfTimes=simple_strtol(split_str, NULL, 0);
				if(showNumberOfTimes<=0)
					showNumberOfTimes=1;
				fc_db.trace_filter[i].showNumberOfTimes=showNumberOfTimes;
				fc_db.trace_filter[i].showNumberOfTimesCounter=0;
			}
			else if(strcasecmp(split_str,"SIP6")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_V6SIP;
				in6_pton(split_str,-1,&(fc_db.trace_filter[i].sipv6[0]),-1,NULL);
			}
			else if(strcasecmp(split_str,"DIP6")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_V6DIP;
				in6_pton(split_str,-1,&(fc_db.trace_filter[i].dipv6[0]),-1,NULL);
			}
			else if(strcasecmp(split_str,"DUMMYMC")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_DUMMYMC;
			}
			else if(strcasecmp(split_str,"WLANINDEX")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_WLAN_INDEX;
				fc_db.trace_filter[i].wlan_index = simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"FLOWINDEX")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_FLOWINDEX;
				fc_db.trace_filter[i].flowindex = simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"SIP_RANGE")==0)
			{
				char *ip_token,*split_ip_token,j;
				split_str=strsep(&strptr," ");
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_SIP_RANGE;
				ip_token=split_str;
				fc_db.trace_filter[i].sip_lowbound=0;
				fc_db.trace_filter[i].sip_highbound=0;
				for(j=0;j<4;j++)
				{
					split_ip_token=strsep(&ip_token,".");
					fc_db.trace_filter[i].sip_lowbound|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
					if(ip_token==NULL) break;
				}
				split_str=strsep(&strptr," ");
				ip_token=split_str;
				for(j=0;j<4;j++)
				{
					split_ip_token=strsep(&ip_token,".");
					fc_db.trace_filter[i].sip_highbound|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
					if(ip_token==NULL) break;
				}
			}
			else if(strcasecmp(split_str,"DIP_RANGE")==0)
			{
				char *ip_token,*split_ip_token,j;
				split_str=strsep(&strptr," ");
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_DIP_RANGE;
				ip_token=split_str;
				fc_db.trace_filter[i].dip_lowbound=0;
				fc_db.trace_filter[i].dip_highbound=0;
				for(j=0;j<4;j++)
				{
					split_ip_token=strsep(&ip_token,".");
					fc_db.trace_filter[i].dip_lowbound|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
					if(ip_token==NULL) break;
				}
				split_str=strsep(&strptr," ");
				ip_token=split_str;
				for(j=0;j<4;j++)
				{
					split_ip_token=strsep(&ip_token,".");
					fc_db.trace_filter[i].dip_highbound|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
					if(ip_token==NULL) break;
				}
			}
			else if(strcasecmp(split_str,"SIP6_RANGE")==0)
			{
				split_str=strsep(&strptr," ");
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_V6SIP_RANGE;
				in6_pton(split_str,-1,&(fc_db.trace_filter[i].sipv6_lowbound[0]),-1,NULL);
				split_str=strsep(&strptr," ");
				in6_pton(split_str,-1,&(fc_db.trace_filter[i].sipv6_highbound[0]),-1,NULL);
			}
			else if(strcasecmp(split_str,"DIP6_RANGE")==0)
			{
				split_str=strsep(&strptr," ");
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_V6DIP_RANGE;
				in6_pton(split_str,-1,&(fc_db.trace_filter[i].dipv6_lowbound[0]),-1,NULL);
				split_str=strsep(&strptr," ");
				in6_pton(split_str,-1,&(fc_db.trace_filter[i].dipv6_highbound[0]),-1,NULL);
			}
			else if(strcasecmp(split_str,"LEN_RANGE")==0)
			{
				split_str=strsep(&strptr," ");
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_LEN_RANGE;
				fc_db.trace_filter[i].len_lowbound=simple_strtol(split_str, NULL, 0);
				split_str=strsep(&strptr," ");
				fc_db.trace_filter[i].len_highbound=simple_strtol(split_str, NULL, 0);
			}
			else if(strcasecmp(split_str,"TCP_FLAGS_MSK")==0)
			{
				split_str=strsep(&strptr," ");
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_TCP_FLAGS_MSK;
				fc_db.trace_filter[i].tcp_flags_enable=simple_strtol(split_str, NULL, 16)&0xfff;	//12bits
				split_str=strsep(&strptr," ");
				fc_db.trace_filter[i].tcp_flags_value=simple_strtol(split_str, NULL, 16)&0xfff; //12bits
			}
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			else if(strcasecmp(split_str,"ldpid")==0)
			{
				split_str=strsep(&strptr," ");
				if(!split_str) break;
				fc_db.trace_filter_bitmask[i]|=FC_DEBUG_TRACE_FILTER_LDPID;
				fc_db.trace_filter[i].ldpid = simple_strtol(split_str, NULL, 0);
			}
#endif


			if (strptr==NULL) break;
		}

		for(i=0 ;i<TRACFILTER_MAX ;i++)
		{
			if(fc_db.trace_filter_bitmask[i] && fc_db.trace_filter_bitmask[i]!=FC_DEBUG_TRACE_FILTER_SHOWNUMBEROFTIMES)
			{ 	//if any filter rule ,enable this rule (ingore only FILTER_SHOWNUMBEROFTIMES rule)
				fc_db.traceFilterRuleMask|=(1<<i);
			}
			else
			{ 	//disable rule[i]
				fc_db.traceFilterRuleMask&=(~(1<<i));
			}
		}
		if(fc_db.traceFilterRuleMask==0)//no any rule enable rule 0
			fc_db.traceFilterRuleMask=0x1;
				
		rtk_fc_proc_traceFilter_read(NULL,NULL);
		return count;
	}
	return -EFAULT;
}


extern int in4_pton(const char *src, int srclen, u8 *dst, int delim, const char **end);
extern int in6_pton(const char *src, int srclen, u8 *dst, int delim, const char **end);
extern char * strstr(const char *,const char *);

#define FC_TMP_BUF_ZIZE 48
int32 _rtk_fc_parseGetValue(char* stringBuff,void *parameterGetAddr,char* parameterName,enum rtk_rg_parseDataType dataType)
{
	char* strPtr;
	uint32 stringSize=0;
	char tmpbuf[FC_TMP_BUF_ZIZE]={0};
	int tmp_strSize=0;
	strPtr = stringBuff;

	while(1)
	{
		strPtr=strstr(strPtr,parameterName);
		if(strPtr==NULL) //string not find
		{
			//printk("stringBuff %s \n%s not find\n",stringBuff,parameterName);
			return FAILED;
		}
		//DEBUG("%s type=%d posDif=%d  sizeof:%d   [%d][%d]\n", parameterName,dataType, (int)(strPtr-stringBuff),(int)strlen(parameterName),((strPtr-stringBuff)!=0  && ((*(strPtr-1)!=' ') ||(*(strPtr-1)!=',') )  ),( (*(strPtr+strlen(parameterName)) != ' ') || (*(strPtr+strlen(parameterName)) != ',')));
		if(((strPtr-stringBuff)!=0  && ((*(strPtr-1)!=' ') && (*(strPtr-1)!=',') )  ) ||
			( (*(strPtr+strlen(parameterName)) != ' ') && (*(strPtr+strlen(parameterName)) != ',')))
		{
			//not this string maybe a sub-string goto find next
			strPtr+=strlen(parameterName);
			while((*strPtr==' ') || (*strPtr==',')) //ingore " " and ","
				strPtr++;
		}
		if( (*(strPtr+strlen(parameterName)) == ' ') || (*(strPtr+strlen(parameterName)) == ','))
			break;

	}
	strPtr+=strlen(parameterName);
	while((*strPtr==' ') || (*strPtr==',')) //ingore " " and ","
		strPtr++;


	if(*strPtr<0x20 ||  *strPtr==0x7f)//out of ascii char range
		return FAILED;

	//DEBUG("%s type=%d posDif=%d  sizeof:%d\n", parameterName,dataType, (int)(strPtr-stringBuff),(int)strlen(parameterName));
	while(1){
		if(*(strPtr+tmp_strSize)<=0x20 ||  *(strPtr+tmp_strSize)==0x7f)//out of ascii char range (include space)
			break;
		tmp_strSize++;
	}
	if(tmp_strSize<(FC_TMP_BUF_ZIZE-1))
	{
		memcpy(tmpbuf,strPtr,tmp_strSize);
		tmpbuf[tmp_strSize]='\0';
		//printk("GetStr:%s  strlen:%lu size:%d\n",tmpbuf,strlen(tmpbuf),tmp_strSize);
	}
	else
		return FAILED;


	switch(dataType)
	{
		case MAC:
			_rtk_fc_str2mac(tmpbuf,(rtk_mac_t*)(parameterGetAddr));
			DEBUG("MAC: %pM", (rtk_mac_t*)(parameterGetAddr));
			break;
		case V4IP:
			//*((uint32 *)parameterGetAddr) = (uint32)in_aton(strPtr);
			in4_pton(tmpbuf,-1,(uint8*)parameterGetAddr,-1,NULL);
			DEBUG("V4IP: %pI4", (uint8*)(parameterGetAddr));
			break;
		case V6IP:
			in6_pton(tmpbuf,-1,(uint8*)parameterGetAddr,-1,NULL);
			DEBUG("V6IP: %pI6", (uint8*)(parameterGetAddr));
			break;
		case UNSIGNED_LONG:
			*((uint32*)parameterGetAddr)= simple_strtoul(tmpbuf,NULL,0);
			DEBUG("UINT32: %u", *(uint32*)(parameterGetAddr));
			break;
		case SINGED_LONG:
			*((int32*)parameterGetAddr)= simple_strtol(tmpbuf,NULL,0);
			DEBUG("INT32: %d", *(int32*)(parameterGetAddr));
			break;
		case STRING_EN_DIS:
			if(memcmp(tmpbuf,"enable",sizeof("enable")-1)==0){
				*((uint32*)parameterGetAddr)=1;
			}else if(memcmp(tmpbuf,"disable",sizeof("disable")-1)==0){
				*((uint32*)parameterGetAddr)=0;
			}else
				return FAILED;
			
			DEBUG("EN: %d", *((uint32*)parameterGetAddr));
			break;
		case STRING:
			while(1){
				if(*(strPtr+stringSize)<=0x20 ||  *(strPtr+stringSize)==0x7f)//out of ascii char range (include space)
					break;
				stringSize++;
			}
			memcpy(parameterGetAddr,strPtr,stringSize);
			((char*)parameterGetAddr)[stringSize]='\0';
			DEBUG("STR: %s stringSize:%d", (char*)parameterGetAddr,stringSize);
			break;
		default:
			return FAILED;

	}

	return SUCCESS;
}

#if defined(CONFIG_RTK_SOC_RTL8198D)
int rtk_fc_proc_user_group_get(struct seq_file *s, void *v)
{ 
	int len=0;
	int i;

	for(i=0 ; i<MAX_USER_GROUP_NUMBER ;i++)
	{
		if(userGroupList[i].valid==0)
			continue;
		
		if(userGroupList[i].ipVersion)
			PROC_PRINTF("[%d] GIP: %pI6 \n",
			i,userGroupList[i].groupAddr);
		else
			PROC_PRINTF("[%d] GIP: %pI4h \n",
			i,&userGroupList[i].groupAddr[0]);
	}
   
    return len;
}

int rtk_fc_proc_user_group_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[256] = {0};
	int len = (count >= 255) ? 255 : count;
	int ret = -1 ;
	char *strptr,*split_str;
	int action = 0;
	unsigned short ipversion = 0;
	unsigned int DipAddr[4] = {0} ;
	unsigned int groupAddr[4]= {0};


	if (buffer)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		strptr=tmpBuf;

		split_str=strsep(&strptr," ");
		if(strcasecmp(split_str,"FLUSH")==0)
		{ 
			memset(userGroupList,0, sizeof(rtk_fc_userGroup_t)*MAX_USER_GROUP_NUMBER);
			return count;;
			
		}
		
		while(1)
		{
			if (strptr==NULL) 
				break;
			if(strcasecmp(split_str,"ADD")==0)
			{
				action = 1;
				split_str=strsep(&strptr," ");	
			}
			else if(strcasecmp(split_str,"DEL")==0)
			{
				action = 2;
				split_str=strsep(&strptr," ");
			}
			else if(strcasecmp(split_str,"GIP")==0)
			{
				split_str = strsep(&strptr," ");
				if (split_str==NULL)
				{
					goto PARSE_ERROR;
				}
				
				if(ipversion == 0)
				{
					sscanf(split_str, "%d.%d.%d.%d", &DipAddr[0], &DipAddr[1], &DipAddr[2], &DipAddr[3]);
					groupAddr[0]=(DipAddr[0]<<24)|(DipAddr[1]<<16)|(DipAddr[2]<<8)|(DipAddr[3]);
					//printk("%s:%d:DIp =%d.%d.%d.%d\n", __FUNCTION__,__LINE__,DipAddr[0],DipAddr[1],DipAddr[2],DipAddr[3]);
					if(action == 1)
					{
						 if(rtk_fc_get_user_group(groupAddr, ipversion)!= 0)
						 {
						 	_rtk_fc_add_lutMac(groupAddr ,ipversion);
						 	ret = rtk_fc_set_user_group(groupAddr, action,ipversion);
						 }
					}
					else if(action == 2)
					{
						 if(rtk_fc_get_user_group(groupAddr, ipversion)!= 0)
						 	ret = rtk_fc_set_user_group(groupAddr, action,ipversion);
					}	
					else
					{
						return -EFAULT;
					}
					if(ret!=0)
					{
						return -EFAULT;
					}
					else
					{
						return count;
					}
				}
			}
			else 
			{
				goto PARSE_ERROR;
			}

			if (strptr==NULL) 
				break;
		}

		return count;
	}

PARSE_ERROR:
	printk("  Parse input parameter error\n");
	return -EFAULT;

}

void _rtk_fc_proc_ext_flow_mib_usage(void){
	rtlglue_printf("usage:\n");
	rtlglue_printf("echo 0 > /proc/fc/sw_dump/ext_flowmib\n");
	rtlglue_printf("echo 1 > /proc/fc/sw_dump/ext_flowmib\n");

	rtlglue_printf("echo mode [mode] > /proc/fc/sw_dump/ext_flowmib\n");
	rtlglue_printf("\t [mode] 0-ipv4 based, 1-mac based\n");

	rtlglue_printf("\nipv4 based mode:\n");
	rtlglue_printf("\t echo add [ip] mask [mask] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo del [ip] mask [mask] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo clr [ip] mask [mask] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo get [type] [ip] mask [mask] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo del_ip_flow [ip] mask [mask] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t\t [ip] e.g. 192.168.1.100\n");
	rtlglue_printf("\t\t [mask] e.g. 255.255.255.0\n");
	rtlglue_printf("\t\t [is_wlan_sta] 0-eth, 1-wlan 5g, 2-wlan 2g\n");
	rtlglue_printf("\t\t [type] 0-HW, 1-SW, 2-Kernel\n");

	rtlglue_printf("\nmac based mode:\n");
	rtlglue_printf("\t echo add_mac [mac] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo del_mac [mac] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo clr_mac [mac] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo get_mac [type] [mac] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo del_mac_flow [mac] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t\t [mac] e.g. 00:66:77:88:88:88\n");
	rtlglue_printf("\t\t [is_wlan_sta] 0-eth, 1-wlan 5g, 2-wlan 2g\n");
	rtlglue_printf("\t\t [type] 0-HW, 1-SW, 2-Kernel\n");
}

//.unlockBefortWrite=1
int rtk_fc_proc_ext_flow_mib_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	uint32 op = 0U, ret = 0U;
	uint8 mode, cntr_type= 0U;
	uint32 rx_bytes = 0U, tx_bytes = 0U, rx_pkts = 0U, tx_pkts = 0U;

	if (buffer)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		strptr = tmpBuf;

		if ((split_str = strsep(&strptr," ")) == NULL) {
			goto SHOW_USAGE;
		}

		if (memcmp(split_str, "0", 1) == 0) {
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_CONTROL(0);
			return len;
		}
		else if (memcmp(split_str, "1", 1) == 0) {
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_CONTROL(1);
			return len;
		}
		else if (memcmp(split_str, "mode", 4) == 0) {
			if ((split_str = strsep(&strptr," ")) == NULL) {
				goto SHOW_USAGE;
			}

			mode = simple_strtol(split_str, NULL, 0);
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_CONTROL_MODE(mode);
			return len;
		}
		else if (memcmp(split_str, "debug_on", 8) == 0) {
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_DEBUG(1);
			return len;
		}
		else if (memcmp(split_str, "debug_off", 9) == 0) {
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_DEBUG(0);
			return len;
		}
		else if (memcmp(split_str, "debug_wfo_on", 12) == 0) {
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_DEBUG(3);
			return len;
		}
		else if (memcmp(split_str, "debug_wfo_off", 13) == 0) {
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_DEBUG(2);
			return len;
		}
		else if (memcmp(split_str, "wfo_test", 8) == 0) {
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_DEBUG(4);
			return len;
		}
		else if (memcmp(split_str, "wfo_wq_sleep", 12) == 0 || memcmp(split_str, "wfo_wq_cpu", 10) == 0) {
			if ((split_str = strsep(&strptr," ")) == NULL) {
				goto SHOW_USAGE;
			}

			op = simple_strtol(split_str, NULL, 0);
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_DEBUG(op);
			return len;
		}
		else {
			if (memcmp(split_str, "del_mac_flow", 12) == 0) {
				op = 15U;
			}
			else if (memcmp(split_str, "add_mac", 7) == 0) {
				op = 11U;
			}
			else if (memcmp(split_str, "del_mac", 7) == 0) {
				op = 12U;
			}
			else if (memcmp(split_str, "clr_mac", 7) == 0) {
				op = 13U;
			}
			else if (memcmp(split_str, "get_mac", 7) == 0) {
				op = 14U;
			}
			else if (memcmp(split_str, "del_ip_flow", 11) == 0) {
				op = 5U;
			}
			else if (memcmp(split_str, "add", 3) == 0) {
				op = 1U;
			}
			else if (memcmp(split_str, "del", 3) == 0) {
				op = 2U;
			}
			else if (memcmp(split_str, "clr", 3) == 0) {
				op = 3U;
			}
			else if (memcmp(split_str, "get", 3) == 0) {
				op = 4U;
			}
			else {
				goto SHOW_USAGE;
			}

			// get counter type first
			if (op == 4U || op == 14U) {
				if ((split_str = strsep(&strptr," ")) == NULL) {
					goto SHOW_USAGE;
				}

				cntr_type = simple_strtol(split_str, NULL, 0);
			}

			if (op < 10U) {	// ipv4 based
				rtk_fc_ext_flow_mib_host_mapping_t 	host;
				rtk_fc_ext_flow_mib_net_mapping_t	net;

				memset(&host, 0, sizeof(rtk_fc_ext_flow_mib_host_mapping_t));
				memset(&net, 0, sizeof(rtk_fc_ext_flow_mib_net_mapping_t));

				if ((split_str = strsep(&strptr," ")) == NULL) {
					goto SHOW_USAGE;
				}

				if (in4_pton(split_str, -1, (uint8*)&host.ip_addr, -1, NULL) == 0) {
					rtlglue_printf("[Error] Invalid ip, should be X.X.X.X!\n");
					goto SHOW_USAGE;
				}

				if ((split_str = strsep(&strptr," ")) == NULL) {
					goto SHOW_USAGE;
				}

				if (memcmp(split_str, "mask", strlen("mask")) == 0) {
					if ((split_str = strsep(&strptr," ")) == NULL) {
						goto SHOW_USAGE;
					}

					if (in4_pton(split_str, -1, (uint8*)&net.netmask, -1, NULL) == 0) {
						rtlglue_printf("[Error] Invalid netmask, should be X.X.X.X!\n");
						goto SHOW_USAGE;
					}

					if ((split_str = strsep(&strptr," ")) == NULL) {
						goto SHOW_USAGE;
					}

					if (memcmp(split_str, "is_wlan_sta", strlen("is_wlan_sta")) == 0) {
						if ((split_str = strsep(&strptr," ")) == NULL) {
							goto SHOW_USAGE;
						}
						host.is_wlan_sta = simple_strtol(split_str, NULL, 0);

						switch (op) {
						case 1U:
							if (host.is_wlan_sta) {
								if ((ret = RTK_FC_HELPER_ADD_EXT_FLOW_MIB_COUNT_BY_LAN_IP(&host, &net)) != 0) {
									rtlglue_printf("[Error] rtk_fc_add_ext_flow_mib_count_by_lan_ip err: %d!\n", ret);
								}
								else {
									rtlglue_printf("[Ok] rtk_fc_add_ext_flow_mib_count_by_lan_ip ok!\n");
								}
							}
							else {
								if ((ret = RTK_FC_HELPER_ADD_ETH_EXT_FLOW_MIB_COUNT_BY_LAN_IP(&host, &net)) != 0) {
									rtlglue_printf("[Error] rtk_fc_add_eth_ext_flow_mib_count_by_lan_ip err: %d!\n", ret);
								}
								else {
									rtlglue_printf("[Ok] rtk_fc_add_eth_ext_flow_mib_count_by_lan_ip ok!\n");
								}
							}
							break;
						case 2U:
							if ((ret = RTK_FC_HELPER_DEL_EXT_FLOW_MIB_COUNT_BY_LAN_IP(&host)) != 0) {
								rtlglue_printf("[Error] rtk_fc_delete_ext_flow_mib_count_by_lan_ip err: %d!\n", ret);
							}
							else {
								rtlglue_printf("[Ok] rtk_fc_delete_ext_flow_mib_count_by_lan_ip ok!\n");
							}
							break;
						case 3U:
							if ((ret = RTK_FC_HELPER_FLUSH_EXT_FLOW_MIB_COUNT_BY_LAN_IP(&host)) != 0) {
								rtlglue_printf("[Error] rtk_fc_flush_ext_flow_mib_count_by_lan_ip err: %d!\n", ret);
							}
							else {
								rtlglue_printf("[Ok] rtk_fc_flush_ext_flow_mib_count_by_lan_ip ok!\n");
							}
							break;
						case 4U:
							if ((ret = RTK_FC_HELPER_GET_EXT_FLOW_MIB_COUNT_BY_LAN_IP(host.ip_addr, host.is_wlan_sta, &rx_bytes, &tx_bytes, &rx_pkts, &tx_pkts, cntr_type)) != 0) {
								rtlglue_printf("[Error] rtk_fc_get_ext_flow_mib_count_by_lan_ip err: %d!\n", ret);
							}
							else {
								rtlglue_printf("[Ok] rtk_fc_get_ext_flow_mib_count_by_lan_ip ok!\n");
								rtlglue_printf("[%s] rx_bytes = %u, tx_bytes = %u, rx_pkts = %u, tx_pkts = %u\n", (cntr_type == 0 ? "HW" : (cntr_type == 1 ? "SW" : "Kernel")), 
									rx_bytes, tx_bytes, rx_pkts, tx_pkts);
							}
							break;
						case 5U:
							if ((ret = RTK_FC_HELPER_DEL_EXT_FLOW_MIB_COUNT_AND_FLOW_BY_LAN_IP(&host)) != 0) {
								rtlglue_printf("[Error] rtk_fc_delete_ext_flow_mib_count_and_flow_by_lan_ip err: %d!\n", ret);
							}
							else {
								rtlglue_printf("[Ok] rtk_fc_delete_ext_flow_mib_count_and_flow_by_lan_ip ok!\n");
							}
							break;
						//default:
						//	goto SHOW_USAGE;
						}
						return len;
					}
				}
			}
			else {	// mac based
				uint8 mac_addr[ETH_ALEN] = {0}, is_wlan_sta;

				if ((split_str = strsep(&strptr," ")) == NULL) {
					goto SHOW_USAGE;
				}

				if (sscanf(split_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &mac_addr[0], &mac_addr[1], &mac_addr[2], &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6) {
					goto SHOW_USAGE;
				}

				if ((split_str = strsep(&strptr," ")) == NULL) {
					goto SHOW_USAGE;
				}

				if (memcmp(split_str, "is_wlan_sta", strlen("is_wlan_sta")) == 0) {
					if ((split_str = strsep(&strptr," ")) == NULL) {
						goto SHOW_USAGE;
					}
					is_wlan_sta = simple_strtol(split_str, NULL, 0);

					switch (op) {
					case 11U:
						if ((ret = RTK_FC_HELPER_ADD_EXT_FLOW_MIB_COUNT_BY_LAN_MAC(mac_addr, is_wlan_sta)) != 0) {
							rtlglue_printf("[Error] rtk_fc_add_ext_flow_mib_count_by_lan_mac err: %d!\n", ret);
						}
						else {
							rtlglue_printf("[Ok] rtk_fc_add_ext_flow_mib_count_by_lan_mac ok!\n");
						}
						break;
					case 12U:
						if ((ret = RTK_FC_HELPER_DEL_EXT_FLOW_MIB_COUNT_BY_LAN_MAC(mac_addr, is_wlan_sta)) != 0) {
							rtlglue_printf("[Error] rtk_fc_del_ext_flow_mib_count_by_lan_mac err: %d!\n", ret);
						}
						else {
							rtlglue_printf("[Ok] rtk_fc_del_ext_flow_mib_count_by_lan_mac ok!\n");
						}
						break;
					case 13U:
						if ((ret = RTK_FC_HELPER_FLUSH_EXT_FLOW_MIB_COUNT_BY_LAN_MAC(mac_addr, is_wlan_sta)) != 0) {
							rtlglue_printf("[Error] rtk_fc_flush_ext_flow_mib_count_by_lan_mac err: %d!\n", ret);
						}
						else {
							rtlglue_printf("[Ok] rtk_fc_flush_ext_flow_mib_count_by_lan_mac ok!\n");
						}
						break;
					case 14U:
						if ((ret = RTK_FC_HELPER_GET_EXT_FLOW_MIB_COUNT_BY_LAN_MAC(mac_addr, is_wlan_sta, &rx_bytes, &tx_bytes, &rx_pkts, &tx_pkts, cntr_type)) != 0) {
							rtlglue_printf("[Error] rtk_fc_get_ext_flow_mib_count_by_lan_mac err: %d!\n", ret);
						}
						else {
							rtlglue_printf("[Ok] rtk_fc_get_ext_flow_mib_count_by_lan_mac ok!\n");
							rtlglue_printf("[%s] rx_bytes = %u, tx_bytes = %u, rx_pkts = %u, tx_pkts = %u\n", (cntr_type == 0 ? "HW" : (cntr_type == 1 ? "SW" : "Kernel")), 
								rx_bytes, tx_bytes, rx_pkts, tx_pkts);
						}
						break;
					case 15U:
						if ((ret = RTK_FC_HELPER_DEL_EXT_FLOW_MIB_COUNT_AND_FLOW_BY_LAN_MAC(mac_addr, is_wlan_sta)) != 0) {
							rtlglue_printf("[Error] rtk_fc_del_ext_flow_mib_count_and_flow_by_lan_mac err: %d!\n", ret);
						}
						else {
							rtlglue_printf("[Ok] rtk_fc_del_ext_flow_mib_count_and_flow_by_lan_mac ok!\n");
						}
						break;
					//default:
						//goto SHOW_USAGE;
					}
					return len;
				}
			}
		}
	}

SHOW_USAGE:
	_rtk_fc_proc_ext_flow_mib_usage();
	return count;
}

void rtk_fc_proc_flow_limit_usage(void)
{
	rtlglue_printf("\nusage:\n");
	rtlglue_printf("echo [type][path][limit] > /proc/fc/ctrl/flow_limit\n");
	rtlglue_printf("	[type]	HW/SW\n");
	rtlglue_printf("	[path]	path12/path34/path5/path6\n");
	rtlglue_printf("	[limit]	-1 unlimit, >=0 flow limit\n");
}

int rtk_fc_proc_flow_limit_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	int limit;
	bool swOnly;
	uint32 in_path;

	if (buffer)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		strptr = tmpBuf;

		if ((split_str = strsep(&strptr," ")) == NULL) {
			goto SHOW_USAGE;
		}

		if (!strncmp(split_str, "HW", strlen(split_str))) {
			swOnly = FALSE;
		}
		else if (!strncmp(split_str, "SW", strlen(split_str))) {
			swOnly = TRUE;
		}
		else
			goto SHOW_USAGE;

		if ((split_str = strsep(&strptr," ")) == NULL) {
			goto SHOW_USAGE;
		}

		if (!strncmp(split_str, "path12", strlen(split_str))) {
			in_path = FB_PATH_12;
		}
		else if (!strncmp(split_str, "path34", strlen(split_str))) {
			in_path = FB_PATH_34;
		}
		else if (!strncmp(split_str, "path5", strlen(split_str))) {
			in_path = FB_PATH_5;
		}
		else if (!strncmp(split_str, "path6", strlen(split_str))) {
			in_path = FB_PATH_6;
		}
		else
			goto SHOW_USAGE;


		if ((split_str = strsep(&strptr," ")) == NULL) {
			goto SHOW_USAGE;
		}
		limit = simple_strtol(split_str, NULL, 0);

		rtk_fc_flow_limit_set(swOnly, in_path, limit);

		dump_flow_limit(NULL, NULL);
	}

SHOW_USAGE:
	rtk_fc_proc_flow_limit_usage();
	return count;
}
#endif

#if defined(CONFIG_RTL8198X_SERIES)
void _rtk_fc_proc_ext_flow_mib_usage(void)
{
	rtlglue_printf("usage:\n");
	rtlglue_printf("echo 0 > /proc/fc/sw_dump/ext_flowmib\n");
	rtlglue_printf("echo 1 > /proc/fc/sw_dump/ext_flowmib\n");

	rtlglue_printf("\t echo add_mac [mac] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo del_mac [mac] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo clr_mac [mac] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t echo get_mac [mac] is_wlan_sta [is_wlan_sta] > /proc/fc/sw_dump/ext_flowmib \n");
	rtlglue_printf("\t\t [mac] e.g. 00:66:77:88:88:88\n");
	rtlglue_printf("\t\t [is_wlan_sta] 0-eth, 1-wlan\n");
}

//.unlockBefortWrite=1
int rtk_fc_proc_ext_flow_mib_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	uint32 op = 0U, ret = 0U;
	uint32 rx_bytes = 0U, tx_bytes = 0U, rx_pkts = 0U, tx_pkts = 0U;
	uint8 is_wlan_sta;

	if (buffer)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		strptr = tmpBuf;

		if ((split_str = strsep(&strptr," ")) == NULL) {
			goto SHOW_USAGE;
		}

		if (memcmp(split_str, "0", 1) == 0) {
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_CONTROL(0);
			return len;
		}
		else if (memcmp(split_str, "1", 1) == 0) {
			RTK_FC_HELPER_SET_EXT_FLOW_MIB_CONTROL(1);
			return len;
		}
		else {
			uint8 mac_addr[ETH_ALEN] = {0};

			if (memcmp(split_str, "add_mac", strlen(split_str)) == 0) {
				op = 11U;
			}
			else if (memcmp(split_str, "del_mac", strlen(split_str)) == 0) {
				op = 12U;
			}
			else if (memcmp(split_str, "clr_mac", strlen(split_str)) == 0) {
				op = 13U;
			}
			else if (memcmp(split_str, "get_mac", strlen(split_str)) == 0) {
				op = 14U;
			}
			else if (memcmp(split_str, "del_mac_flow", strlen(split_str)) == 0) {
				op = 15U;
			}
			else {
				goto SHOW_USAGE;
			}

			if ((split_str = strsep(&strptr," ")) == NULL) {
				goto SHOW_USAGE;
			}

			if (sscanf(split_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", &mac_addr[0], &mac_addr[1], &mac_addr[2], &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6) {
				goto SHOW_USAGE;
			}

			if ((split_str = strsep(&strptr," ")) == NULL) {
				goto SHOW_USAGE;
			}

			if (memcmp(split_str, "is_wlan_sta", strlen("is_wlan_sta")) == 0) {
				if ((split_str = strsep(&strptr," ")) == NULL) {
					goto SHOW_USAGE;
				}
				is_wlan_sta = simple_strtol(split_str, NULL, 0);

				switch (op) {
				case 11U:
					if ((ret = RTK_FC_HELPER_ADD_EXT_FLOW_MIB_COUNT_BY_LAN_MAC(mac_addr, is_wlan_sta)) != 0) {
						rtlglue_printf("[Error] rtk_fc_add_ext_flow_mib_count_by_lan_mac err: %d!\n", ret);
					}
					else {
						rtlglue_printf("[Ok] rtk_fc_add_ext_flow_mib_count_by_lan_mac ok!\n");
					}
					break;
				case 12U:
					if ((ret = RTK_FC_HELPER_DEL_EXT_FLOW_MIB_COUNT_BY_LAN_MAC(mac_addr, is_wlan_sta)) != 0) {
						rtlglue_printf("[Error] rtk_fc_del_ext_flow_mib_count_by_lan_mac err: %d!\n", ret);
					}
					else {
						rtlglue_printf("[Ok] rtk_fc_del_ext_flow_mib_count_by_lan_mac ok!\n");
					}
					break;
				case 13U:
					if ((ret = RTK_FC_HELPER_FLUSH_EXT_FLOW_MIB_COUNT_BY_LAN_MAC(mac_addr, is_wlan_sta)) != 0) {
						rtlglue_printf("[Error] rtk_fc_flush_ext_flow_mib_count_by_lan_mac err: %d!\n", ret);
					}
					else {
						rtlglue_printf("[Ok] rtk_fc_flush_ext_flow_mib_count_by_lan_mac ok!\n");
					}
					break;
				case 14U:
					if ((ret = RTK_FC_HELPER_GET_EXT_FLOW_MIB_COUNT_BY_LAN_MAC(mac_addr, is_wlan_sta, &rx_bytes, &tx_bytes, &rx_pkts, &tx_pkts)) != 0) {
						rtlglue_printf("[Error] rtk_fc_get_ext_flow_mib_count_by_lan_mac err: %d!\n", ret);
					}
					else {
						rtlglue_printf("[Ok] rtk_fc_get_ext_flow_mib_count_by_lan_mac ok!\n");
						rtlglue_printf("rx_bytes = %u, tx_bytes = %u, rx_pkts = %u, tx_pkts = %u\n", rx_bytes, tx_bytes, rx_pkts, tx_pkts);
					}
					break;
				case 15U:
					if ((ret = RTK_FC_HELPER_DEL_EXT_FLOW_MIB_COUNT_AND_FLOW_BY_LAN_MAC(mac_addr, is_wlan_sta)) != 0) {
						rtlglue_printf("[Error] rtk_fc_del_ext_flow_mib_count_and_flow_by_lan_mac err: %d!\n", ret);
					}
					else {
						rtlglue_printf("[Ok] rtk_fc_del_ext_flow_mib_count_and_flow_by_lan_mac ok!\n");
					}
					break;
				//default:
					//goto SHOW_USAGE;
				}
			}

			return len;
		}
	}

SHOW_USAGE:
	_rtk_fc_proc_ext_flow_mib_usage();
	return count;
}

#endif

#if defined(CONFIG_FC_RTL8198F_SERIES)
int rtk_fc_proc_dsliteUdpFrag_get(struct seq_file *s, void *v)
{
    int len=0;
	switch(atomic_read(&fc_db.dsliteUdpFrag))
	{
		case 0:
        	PROC_PRINTF("0x0: Disable\n");
			break;
		case 1:
			PROC_PRINTF("0x1: Do udp fragment for DS-Lite/MAP-E when len over mtu of upstream.\n");
			break;
	}
    return len;
}

int rtk_fc_proc_dsliteUdpFrag_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	atomic_set(&fc_db.dsliteUdpFrag, _rtk_fc_proc_parsing_string_to_integer(buff,len));

	return len;
}
#endif

int rtk_fc_proc_ip_frag_shortcut_get(struct seq_file *s, void *v)
{
	int i = 0;
	int max_hash_size = RTK_FC_GET_IPFRAG_MAX_HASH_SIZE();
	int max_hash_entry_size = RTK_FC_GET_IPFRAG_MAX_HASH_ENTRY_SIZE();
	rtk_fc_ipFrag_linkList_t *ipFragList;
	rtk_fc_negativeIpFrag_linkList_t *negativeIpFragList;
	
	PROC_PRINTF("enable: %d\n", fc_db.controlFuc.ip_frag_shortcut);

	PROC_PRINTF("max_hash_size: %d, max_hash_entry_size: %d\n\n", max_hash_size, max_hash_entry_size);
	PROC_PRINTF("Ip Frag Hash List\n");
	for (i = 0; i < max_hash_size; i++) {
		if (!list_empty(&fc_db.ipFrag_hashListHead[i])) {
			PROC_PRINTF("\t Frag Hash[%4d] : \n", i);
			list_for_each_entry(ipFragList, &fc_db.ipFrag_hashListHead[i], entry) {
				PROC_PRINTF("\t\t sip: 0x%x, dip: 0x%x\n", ipFragList->frag_info.src_ip, ipFragList->frag_info.dst_ip);
				PROC_PRINTF("\t\t id: 0x%x, proto: %d\n", ipFragList->frag_info.frag_id, ipFragList->frag_info.protocol);
				PROC_PRINTF("\t\t sport: %d, dport: %d\n", ipFragList->src_port, ipFragList->dst_port);
			}
			PROC_PRINTF("\n");
		}
	}
	PROC_PRINTF("\n");

	PROC_PRINTF("Negative Ip Frag Hash List\n");
	for (i = 0; i < max_hash_size; i++) {
		if (!list_empty(&fc_db.negativeIpFrag_hashListHead[i])) {
			PROC_PRINTF("\t Negative Frag Hash[%4d] : \n", i);
			list_for_each_entry(negativeIpFragList, &fc_db.negativeIpFrag_hashListHead[i], entry) {
				PROC_PRINTF("\t\t sip: 0x%x, dip: 0x%x\n", negativeIpFragList->frag_info.src_ip, negativeIpFragList->frag_info.dst_ip);
				PROC_PRINTF("\t\t id: 0x%x, proto: %d\n", negativeIpFragList->frag_info.frag_id, negativeIpFragList->frag_info.protocol);
			}
			PROC_PRINTF("\n");
		}
	}
	PROC_PRINTF("\n");
		
	return SUCCESS;
}

int rtk_fc_proc_ip_frag_shortcut_set(struct file *filp, const char *buff,unsigned long len, void *data )
{
	fc_db.controlFuc.ip_frag_shortcut = _rtk_fc_proc_parsing_string_to_integer(buff, len);
	return len;
}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int rtk_fc_proc_mapeNetifFmr_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	uint32 value;
	uint8 fmrv6Prefix[16]={0};
	rtk_fc_mapet_hwfmrInfo_t hwfmr;
	int ret;
	bzero(&hwfmr,sizeof(hwfmr));

	if (buffer)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, CMD_BUFF_SIZE);

		//default value
		hwfmr.draftVersion=1;

		if(_rtk_fc_parseGetValue(tmpBuf,hwfmr.devName,"DEVIFNAME",STRING) ==FAILED)
		{
			printk("DEVIFNAME parser error\n");
			goto MAPE_ERR;
		}

		if(_rtk_fc_parseGetValue(tmpBuf,fmrv6Prefix,"FMR6PREFIX",V6IP) ==SUCCESS)
		{
			memcpy(hwfmr.fmrv6Prefix,fmrv6Prefix,sizeof(hwfmr.fmrv6Prefix));
		}
		else
		{
			printk("FMR6PREFIX parser error\n");
			goto MAPE_ERR;
		}

		if(_rtk_fc_parseGetValue(tmpBuf,&value,"PREFIX_LEN",UNSIGNED_LONG) ==SUCCESS)
		{
			hwfmr.fmrPrefixLen=value;
		}
		else
		{
			printk("PREFIX_LEN parser error\n");
			goto MAPE_ERR;
		}

		if(_rtk_fc_parseGetValue(tmpBuf,&value,"PSID_STARTBIT",UNSIGNED_LONG) ==SUCCESS)
		{
			hwfmr.psidStartOffset=value;
		}
		else
		{
			printk("PSID_STARTBIT parser error\n");
			goto MAPE_ERR;
		}
		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"PSID_ENDBIT",UNSIGNED_LONG) ==SUCCESS)
		{
			hwfmr.psidEndOffset=value;
		}
		else
		{
			printk("PSID_ENDBIT parser error\n");
			goto MAPE_ERR;
		}


		if(_rtk_fc_parseGetValue(tmpBuf,&value,"DRAF",UNSIGNED_LONG) ==SUCCESS)
		{
			hwfmr.draftVersion=value;
		}

		ret = rtk_fc_mapet_hwfmr_add(&hwfmr);
		if(ret)
			printk("rtk_fc_mapet_hwfmr_add fail %d:\n",ret);
		
	}

MAPE_ERR:
	return count;

}


int rtk_fc_proc_mapeNetifFmr_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Usage:\n");
	PROC_PRINTF("Patten: [DEVIFNAME] [FMR6PREFIX] [PREFIX_LEN] [PSID_STARTBIT] [PSID_ENDBIT] [DRAF]\n");
	PROC_PRINTF("Example:\n");
	PROC_PRINTF("echo DEVIFNAME nas0_0 FMR6PREFIX 2001::2  PREFIX_LEN 40 PSID_STARTBIT 2 PSID_ENDBIT 9 DRAF 1 > proc/fc/ctrl/mape_fmr_netif\n");

	return SUCCESS;
}
#endif

int rtk_fc_proc_maptNetifDmr_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	uint32 value;
	uint8 devName[IFNAMSIZ]={0};
	uint8 draftVer;
	uint8 prefixLen;
	int i,deleting=0;

	if (buffer)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, CMD_BUFF_SIZE);

		if(_rtk_fc_parseGetValue(tmpBuf,devName,"DEVIFNAME",STRING) == FAILED)
		{
			printk("DEVIFNAME parser error\n");
			goto MAPT_ERR;
		}

		if(_rtk_fc_parseGetValue(tmpBuf,&value,"DRAF",UNSIGNED_LONG) == SUCCESS)
		{
			draftVer=value;
			if(draftVer>1)
			{
				printk("DRAF VALUE error:%d\n",draftVer);
				goto MAPT_ERR;
			}
			
			if(_rtk_fc_parseGetValue(tmpBuf,&value,"BR_PREFIX_LEN",UNSIGNED_LONG) == SUCCESS)
			{
				prefixLen=value;
				if(prefixLen>96)
				{
					printk("BR_PREFIX_LEN VALUE error:%d\n",prefixLen);
					goto MAPT_ERR;
				}
			}
			else
			{
				printk("BR_PREFIX_LEN parser error\n");
				goto MAPT_ERR;
			}
		}
		else
		{
			//delete!!
			deleting=1;
		}

		//check device
		for(i=DEVIFIDX_VALID_MIN ; i<RTK_FC_DEV_GW_MAC_TBL ;i++ )
		{
			if(!fc_db.devGwMacTbl[i].dev)
				continue;

			if(!strcmp(fc_db.devGwMacTbl[i].dev->name, devName))
			{
				//match!!setup DMR information
				if(deleting){
					fc_db.devGwMacTbl[i].maptInfoValid=0;
					fc_db.devGwMacTbl[i].maptPrefxLen=0;
					fc_db.devGwMacTbl[i].maptDraftVer=0;
					fc_db.dualIpv6HashMask&=(~DUAL_IPV6_HASHMASK_ALL);
				}else{
					fc_db.devGwMacTbl[i].maptInfoValid=1;
					fc_db.devGwMacTbl[i].maptPrefxLen=prefixLen;
					fc_db.devGwMacTbl[i].maptDraftVer=draftVer;
					fc_db.dualIpv6HashMask|=DUAL_IPV6_HASHMASK_ALL;
				}
				break;
			}
		}
	}

MAPT_ERR:
	return count;

}

int rtk_fc_proc_maptNetifDmr_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("Usage:\n");
	PROC_PRINTF("Patten: [DEVIFNAME] [PREFIX_LEN] [DRAF]\n");
	PROC_PRINTF("[DEVIFNAME] means mapt device.\n");
	PROC_PRINTF("[DRAF] means our CE's draft version, 0 or 1.\n");
	PROC_PRINTF("[BR_PREFIX_LEN] means BR's v6 prefix length.\n");
	PROC_PRINTF("Example:\n");
	PROC_PRINTF("Add mapt:\n");
	PROC_PRINTF("echo DEVIFNAME mapt_nas0_0 DRAF 1 BR_PREFIX_LEN 64 > /proc/fc/ctrl/mapt_dmr_netif\n");
	PROC_PRINTF("Del mapt:\n");
	PROC_PRINTF("echo DEVIFNAME mapt_nas0_0 > /proc/fc/ctrl/mapt_dmr_netif\n");

	return SUCCESS;
}


#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
char *str_wifi_pe_offload[] = {
	"TC mode (EPP64)",
	"WMM mode (EPP64)",
	"multi-STA TC mode (EPP64)",
	"multi-STA WMM mode (EPP64)",
	"DISABLE",
	"TC mode (EPP256)",
	"WMM mode (EPP256)",
	"multi-STA TC mode (EPP256)",
	"multi-STA WMM mode (EPP256)",
};

void rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage(void)
{
	uint8 i;
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("1. update wifi amdsu mode \n");
	rtlglue_printf("- echo mode MODE > /proc/fc/sw_dump/wifi_amsdu_pe_offload_mac_id_tbl \n");
	rtlglue_printf("  -- e.g. echo mode 1 > /proc/fc/sw_dump/wifi_amsdu_pe_offload_mac_id_tbl \n");
	rtlglue_printf("  MODE | description\n");
	rtlglue_printf("  ----------------------------------------------------------\n");
	for(i = 0; i < RTK_FC_WIFI_AMSDU_PE_OFFLOAD_MODE_END; i++) {
		rtlglue_printf("  %-4d | %s \n", i, str_wifi_pe_offload[i]);
	}
	rtlglue_printf("\n");

	rtlglue_printf("2. set mac_id entry \n");
	rtlglue_printf("- echo mac_id MAC_ID set [STA_INFO  STA_INFO_VAL] > /proc/fc/sw_dump/wifi_amsdu_pe_offload_mac_id_tbl \n");
	rtlglue_printf("  -- e.g. echo mac_id 7 set addr 00:00:00:00:00:33 > /proc/fc/sw_dump/wifi_amsdu_pe_offload_mac_id_tbl \n");
	rtlglue_printf("  -- e.g.2. echo mac_id 7 set addr 00:00:00:00:00:33 power_saving 1 > /proc/fc/sw_dump/wifi_amsdu_pe_offload_mac_id_tbl \n");
	rtlglue_printf("  mac_id: %u~%u\n", 0, RTK_FC_WIFI_PE_OFFLOAD_MAC_ID_SIZE-1);
	rtlglue_printf("  [STA_INFO]\n");
	rtlglue_printf("  addr: mac address\n");
	rtlglue_printf("  power_saving: power_saving status. 0 or 1.\n");
	rtlglue_printf("  wlandev_id: wlandev_id\n");
	rtlglue_printf("  wifi_pri_offld_en_bitmsk: wifi priority enable wifi amsdu offlad bitmask\n");
	rtlglue_printf("  voq_id: voq_id\n");
	rtlglue_printf("  sta_id: sta_id\n");
#if defined(CONFIG_REALTEK_IPC2RCPU)
	rtlglue_printf("  amsdu_pkt_size: AMSDU packet size.\n");
#endif
	rtlglue_printf("\n");

	rtlglue_printf("3. del mac_id entry \n");
	rtlglue_printf("- echo mac_id MAC_ID del [STA_INFO  STA_INFO_VAL] > /proc/fc/sw_dump/wifi_amsdu_pe_offload_mac_id_tbl \n");
	rtlglue_printf("  -- e.g. echo mac_id 7 del > /proc/fc/sw_dump/wifi_amsdu_pe_offload_mac_id_tbl \n");
	rtlglue_printf("\n");
	rtlglue_printf("4. dump mac_id to voq_id mapping.\n");
	rtlglue_printf("- echo dump table > /proc/fc/sw_dump/wifi_amsdu_pe_offload_mac_id_tbl \n");
	rtlglue_printf("\n");
}

int rtk_fc_wifi_amsdu_pe_offload_mode_macId_get(struct seq_file *s, void *v)
{
	uint i;
	uint8 ldpid, cos;

	PROC_PRINTF("wifi_amsdu_pe_offload_mode: %s\n", str_wifi_pe_offload[fc_db.wifi_amsdu_pe_offload_mode]);
	PROC_PRINTF("mac_id_start: %u, mac_id_end: %u, voq_count: %u\n\n",
		fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_start,
		fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_end,
		fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].voq_count);

	for(i = fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_start ; i <= fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_end ; i++)
	{
		if(fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].valid)
		{
			PROC_PRINTF("mac_id[%02d]: %pM,", i, fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].l2Addr);
			if(_rtk_fc_wifi_amsdu_pe_offload_voq_info_get(i, &ldpid, &cos))
				PROC_PRINTF(" No voq info!!!");
			else
			{
				if(fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.voq_id == RTK_WIFI_AMSDU_OFFLOAD_INVALID_VOQ_ID)
				{
					PROC_PRINTF(" voq_id: %02d (INVALID)            , sta_id: %02u,",
					fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.voq_id,
					fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.sta_id);
				}
				else
				{
					PROC_PRINTF(" voq_id: %02d (ldpid: 0x%x, cos: %u), sta_id: %02u,",
					fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.voq_id,
					ldpid, cos,
					fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.sta_id);
				}
				PROC_PRINTF(" power_saving: %u, wifi_pri_offld_en_bitmsk: 0x%02x",
					fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.power_saving,
					fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.wifi_pri_offld_en_bitmsk);
#if defined(CONFIG_REALTEK_IPC2RCPU)
				PROC_PRINTF(" amsdu_pkt_size: %u", fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.amsdu_pkt_size);
#endif
#if !defined(CONFIG_RTK_FC_WIFI_DRIVER_OFFLOAD_BY_PE)
				if(fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.p_wifi_dev)
				{
					rtk_fc_wlan_devidx_t wlan_dev_idx = 0;
					if(RTK_FC_HELPER_WLAN_DEV_TO_DEVID(fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.p_wifi_dev, &wlan_dev_idx) == SUCCESS)
						PROC_PRINTF(", wlan_dev_id: %d (p_wifi_dev: %p)", wlan_dev_idx, fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.p_wifi_dev);
					else
						PROC_PRINTF(", wlan_dev_id: ERROR (p_wifi_dev: %p)", fc_db.wifi_amsdu_pe_offload_mac_id_tbl[i].sta_info.p_wifi_dev);
				}
				else
				{
					PROC_PRINTF(", wlan_dev_id: N.A. (p_wifi_dev: NULL)");
				}
#endif
			}

			PROC_PRINTF("\n");
		}
	}

	return SUCCESS;
}

int rtk_fc_wifi_amsdu_pe_offload_mode_macId_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	uint8 mode;
	uint32 mac_id;
	uint i, j;

	if(buffer)
	{
		char *strptr, *split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		strptr = tmpBuf;

		split_str = strsep(&strptr, " ");
		if (strptr == NULL)
		{
			rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
			return count;
		}

		if(strcasecmp(split_str,"mode")==0)
		{
			split_str=strsep(&strptr," ");
			if(split_str==NULL)
				rtlglue_printf("[ERROR] invalid mode. Mode is not changed!\n");
			else
			{
				mode = simple_strtol(split_str, NULL, 0);
				if(mode >= RTK_FC_WIFI_AMSDU_PE_OFFLOAD_MODE_END)
				{
					WARNING("unknown PE_OFFLOAD_MODE");
					rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
					return count;
				}

				if(mode > RTK_FC_WIFI_AMSDU_PE_OFFLOAD_MODE_EPP64_END)
				{
					WARNING("EPP256 PE_OFFLOAD_MODE not support yet!");
					rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
					return count;
				}

				rtk_fc_wifi_amsdu_pe_offload_mode_set(mode);
				rtlglue_printf("Update wifi_amsdu_pe_offload_mode: %s\n", str_wifi_pe_offload[fc_db.wifi_amsdu_pe_offload_mode]);
			}
		}
		else if(strcasecmp(split_str, "mac_id")==0)
		{
			rtk_fc_wifi_amsdu_pe_offload_sta_conf_sel_t sta_conf_sel = {0};
			rtk_fc_wifi_amsdu_pe_offload_sta_info_t sta_conf = {0};
			rtk_mac_t tempMac = {0};
			split_str = strsep(&strptr, " ");
			if(split_str == NULL)
			{
				rtlglue_printf("[ERROR] invalid mac_id.");
				rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
				return count;
			}
			else
			{
				mac_id = simple_strtol(split_str, NULL, 0);
				memcpy(&tempMac, fc_db.wifi_amsdu_pe_offload_mac_id_tbl[mac_id].l2Addr, sizeof(tempMac));
			}

			split_str = strsep(&strptr, " ");
			if(split_str == NULL)
			{
				rtlglue_printf("[ERROR] invalid mac_id operation.");
				rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
				return count;
			}
			else
			{
				if(strcasecmp(split_str, "set") == 0)
				{
					while(1)
					{
						split_str = strsep(&strptr, " ");
						if(strcasecmp(split_str, "addr") == 0)
						{
							split_str=strsep(&strptr, " ");
							if(split_str == NULL)
							{
								rtlglue_printf("[ERROR] invalid addr.\n");
								rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
								return count;
							}
							else
								_rtk_fc_str2mac(split_str, &tempMac);
						}
						else if(strcasecmp(split_str, "power_saving") == 0)
						{
							uint32_t tmpVal;
							split_str = strsep(&strptr, " ");
							if(split_str == NULL)
							{
								rtlglue_printf("[ERROR] invalid power_saving config.\n");
								rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
								return count;
							}
							else
							{
								tmpVal = simple_strtol(split_str, NULL, 0);
								sta_conf_sel.power_saving = TRUE;
								sta_conf.power_saving = tmpVal & 0x1;
							}
						}
						else if(strcasecmp(split_str, "wlandev_id") == 0)
						{
							uint32_t tmpVal;
							split_str = strsep(&strptr, " ");
							if(split_str == NULL)
							{
								rtlglue_printf("[ERROR] invalid wlandev_id config.\n");
								rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
								return count;
							}
							else
							{
								struct net_device *pWifiDev = NULL;
								tmpVal = simple_strtol(split_str, NULL, 0);
								if(RTK_FC_HELPER_WLAN_DEVID_TO_DEV(tmpVal, &pWifiDev) == SUCCESS)
								{
									sta_conf_sel.p_wifi_dev = TRUE;
									sta_conf.p_wifi_dev = pWifiDev;
								}
								else
								{
									rtlglue_printf("[ERROR] get wlan dev fail by wlandev_id %u.\n", tmpVal);
									rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
									return count;
								}
							}
						}
						else if(strcasecmp(split_str, "wifi_pri_offld_en_bitmsk") == 0)
						{
							uint32_t tmpVal;
							split_str = strsep(&strptr, " ");
							if(split_str == NULL)
							{
								rtlglue_printf("[ERROR] invalid wifi_pri_offld_en_bitmsk config.\n");
								rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
								return count;
							}
							else
							{
								tmpVal = simple_strtol(split_str, NULL, 0);
								sta_conf_sel.wifi_pri_offld_en_bitmsk = TRUE;
								sta_conf.wifi_pri_offld_en_bitmsk = tmpVal & 0xff;
							}
						}
						else if(strcasecmp(split_str, "voq_id") == 0)
						{
							uint32_t tmpVal;
							split_str = strsep(&strptr, " ");
							if(split_str == NULL)
							{
								rtlglue_printf("[ERROR] invalid voq_id config.\n");
								rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
								return count;
							}
							else
							{
								tmpVal = simple_strtol(split_str, NULL, 0);
								sta_conf_sel.voq_id = TRUE;
								sta_conf.voq_id = tmpVal & 0xff;
							}
						}
						else if(strcasecmp(split_str, "sta_id") == 0)
						{
							uint32_t tmpVal;
							split_str = strsep(&strptr, " ");
							if(split_str == NULL)
							{
								rtlglue_printf("[ERROR] invalid sta_id config.\n");
								rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
								return count;
							}
							else
							{
								tmpVal = simple_strtol(split_str, NULL, 0);
								sta_conf_sel.sta_id = TRUE;
								sta_conf.sta_id = tmpVal & 0xff;
							}
						}
#if defined(CONFIG_REALTEK_IPC2RCPU)
						else if(strcasecmp(split_str, "amsdu_pkt_size") == 0)
						{
							uint32_t tmpVal;
							split_str = strsep(&strptr, " ");
							if(split_str == NULL)
							{
								rtlglue_printf("[ERROR] invalid amsdu_pkt_size config.\n");
								rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
								return count;
							}
							else
							{
								tmpVal = simple_strtol(split_str, NULL, 0);
								sta_conf_sel.amsdu_pkt_size = TRUE;
								sta_conf.amsdu_pkt_size = tmpVal;
							}
						}
#endif
						if (strptr==NULL) break;
					}
					rtlglue_printf("[mac_id set] mac_id %u: %pM ", mac_id, &tempMac);
					if(sta_conf_sel.power_saving)
						rtlglue_printf(" (power_saving update to %u)", sta_conf.power_saving);
#if defined(CONFIG_REALTEK_IPC2RCPU)
					if(sta_conf_sel.amsdu_pkt_size)
						rtlglue_printf(" (amsdu_pkt_size update to %u)", sta_conf.amsdu_pkt_size);
#endif
					rtlglue_printf("\n");
					if(!rtk_fc_wifi_amsdu_pe_offload_mac_id_set(mac_id, sta_conf_sel, sta_conf, &(tempMac.octet[0])))
						rtlglue_printf("set mac_id[%u] success!\n", mac_id);
					else
						rtlglue_printf("set mac_id[%u] fail!\n", mac_id);
				}
				else if(strcasecmp(split_str, "del") == 0)
				{
					rtlglue_printf("[mac_id del] mac_id %u \n", mac_id);
					if(!rtk_fc_wifi_amsdu_pe_offload_mac_id_del(mac_id))
						rtlglue_printf("del mac_id[%u] success!\n", mac_id);
					else
						rtlglue_printf("del mac_id[%u] fail!\n", mac_id);
				}
				else
				{
					rtlglue_printf("[ERROR] invalid mac_id operation.");
					rtk_fc_wifi_amsdu_pe_offload_mode_macId_usage();
					return count;
				}
			}
		}
		else if(strcasecmp(split_str, "dump")==0)
		{
			split_str = strsep(&strptr, " ");
			if(strcasecmp(split_str, "table") == 0)
			{
				rtlglue_printf("\nwifi_amsdu_pe_offload_mode: %s\n", str_wifi_pe_offload[fc_db.wifi_amsdu_pe_offload_mode]);
				rtlglue_printf("mac_id_start: %u, mac_id_end: %u, voq_count: %u\n\n",
					fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_start,
					fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_end,
					fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].voq_count);
				if(fc_db.wifi_amsdu_pe_offload_mode != RTK_FC_WIFI_AMSDU_PE_OFFLOAD_MODE_DISABLE)
				{

					rtlglue_printf(KERN_CONT "\033[1;33m[MAC_ID to VOQ_ID table]\033[0m\n");
					for(i = 0 ; i <= fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_end ; i = j)
					{
						rtlglue_printf(KERN_CONT "MAC_ID (if_valid) |");
						for(j = i ; (j <= fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_end) &&  (j < (i+8)) ; j++)
						{
							if(j < fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_start)
								rtlglue_printf(KERN_CONT "\t  ");
							else
								rtlglue_printf(KERN_CONT "\t%02u(%d)", j, fc_db.wifi_amsdu_pe_offload_mac_id_tbl[j].valid);
						}
						rtlglue_printf(KERN_CONT "\n");

						rtlglue_printf(KERN_CONT "VOQ_ID            |");
						for(j = i ; (j <= fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_end) &&  (j < (i+8)) ; j++)
						{
							if(j < fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_start)
								rtlglue_printf(KERN_CONT "\t  ");
							else if(fc_db.wifi_amsdu_pe_offload_mac_id_tbl[j].sta_info.voq_id == RTK_WIFI_AMSDU_OFFLOAD_INVALID_VOQ_ID)
								rtlglue_printf(KERN_CONT "\tINVALID");
							else
								rtlglue_printf(KERN_CONT "\t%02u", fc_db.wifi_amsdu_pe_offload_mac_id_tbl[j].sta_info.voq_id);
						}
						rtlglue_printf(KERN_CONT "\n");
						rtlglue_printf(KERN_CONT "------------------------------------------------------------------------------------------\n");
					}

					rtlglue_printf(KERN_CONT "\n\n");
				}
				rtlglue_printf(KERN_CONT "\033[1;33m[VOQ_ID table]\033[0m\n");
				for(i = 0 ; i < RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE ; i = j)
				{
					rtlglue_printf(KERN_CONT "VOQ_ID            |");
					for(j = i ; (j < RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE) &&  (j < (i+8)) ; j++)
					{
						rtlglue_printf(KERN_CONT "\t%02u", j);
					}
					rtlglue_printf(KERN_CONT "\n");

					rtlglue_printf(KERN_CONT "LDPID             |");
					for(j = i ; (j < RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE) &&  (j < (i+8)) ; j++)
					{
						rtlglue_printf(KERN_CONT "\t0x%x", fc_db.wifi_amsdu_pe_offload_voq_tbl[j].ldpid);
					}
					rtlglue_printf(KERN_CONT "\n");

					rtlglue_printf(KERN_CONT "COS               |");
					for(j = i ; (j < RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE) &&  (j < (i+8)) ; j++)
					{
						rtlglue_printf(KERN_CONT "\t%u", fc_db.wifi_amsdu_pe_offload_voq_tbl[j].cos);
					}
					rtlglue_printf(KERN_CONT "\n");
					rtlglue_printf(KERN_CONT "------------------------------------------------------------------------------------------\n");
				}
			}
		}
	}
	return count;
}

#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
int rtk_fc_proc_wfo_per_flow_share_cnt_hw_get(struct seq_file *s, void *v)
{
	int flowIdx;
	rt_stat_wfo_per_flow_cnt_t hw_cnt = {0};

	for(flowIdx = 0 ; flowIdx < fc_db.flowHwTableSize ; flowIdx++)
	{
#if defined(CONFIG_RTK_L34_CANDIDATE_FLOW)
		if(fc_db.flowTbl[flowIdx].candidateState != CANDIDATE_STATE_NONE)
#else
		if(fc_db.flowTbl[entIdx+i].pFlowEntry->path1.valid)
#endif
		{
			if((fc_db.flowTbl[flowIdx].lutEgrDaIdx != SIGNED_INVALID) && fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutEgrDaIdx] &&
			((1<<(fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutEgrDaIdx]->spa))&RTK_FC_ALL_MAC_WLANCPU_PORTMASK) &&
#if defined(CONFIG_RTK_FC_WIFI_MULTIBAND_ACC_BY_ONE_LUT)
			(fc_db.flowTbl[flowIdx].egrWlanDevIdx < RTK_FC_WLANX_END_INTF)
#else
			(fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutEgrDaIdx]->wlan_dev_idx < RTK_FC_WLANX_END_INTF)
#endif
			)
			{
				//wifi tx: get HW  counter
				_rtk_fc_wifi_amsdu_wfo_flow_mib_tbl_cntr_read(flowIdx, &hw_cnt);
				PROC_PRINTF("flow[%u]: wifi tx, pkt_cnt: %-10u, byte_cnt: %-u\n", flowIdx, hw_cnt.pkt_cnt, hw_cnt.byte_cnt);
			}
			else if((fc_db.flowTbl[flowIdx].lutIgrSaIdx != SIGNED_INVALID) && fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutIgrSaIdx] &&
			((1<<(fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutIgrSaIdx]->spa))&RTK_FC_ALL_MAC_WLANCPU_PORTMASK) &&
#if defined(CONFIG_RTK_FC_WIFI_MULTIBAND_ACC_BY_ONE_LUT)
			(fc_db.flowTbl[flowIdx].igrWlanDevIdx < RTK_FC_WLANX_END_INTF)
#else
			(fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutIgrSaIdx]->wlan_dev_idx < RTK_FC_WLANX_END_INTF)
#endif
			)
			{
				//wifi rx: get HW counter
				_rtk_fc_wifi_amsdu_wfo_flow_mib_tbl_cntr_read(flowIdx, &hw_cnt);
				PROC_PRINTF("flow[%u]: wifi rx, pkt_cnt: %-10u, byte_cnt: %-u\n", flowIdx, hw_cnt.pkt_cnt, hw_cnt.byte_cnt);
			}
		}
	}
	return SUCCESS;
}

int rtk_fc_proc_wfo_per_flow_share_cnt_hw_clear(struct file *file, const char *buffer, unsigned long count, void *data)
{
	uint32 flowIdx =_rtk_fc_proc_parsing_string_to_integer(buffer, count);

	if(flowIdx >= fc_db.flowHwTableSize)
	{
		rtlglue_printf("Invalid index %d, should be 0~%d.\n", flowIdx, fc_db.flowHwTableSize);
		return count;
	}
	_rtk_fc_wifi_amsdu_wfo_flow_mib_tbl_cntr_clear(flowIdx);

	return count;
}

int rtk_fc_proc_wfo_per_flow_share_cnt_sw_get(struct seq_file *s, void *v)
{
	int flowIdx;

	for(flowIdx = 0 ; flowIdx < fc_db.flowSwTableSize ; flowIdx++)
	{
#if defined(CONFIG_RTK_L34_CANDIDATE_FLOW)
		if(fc_db.flowTbl[flowIdx].candidateState != CANDIDATE_STATE_NONE)
#else
		if(fc_db.flowTbl[flowIdx].pFlowEntry->path1.valid)
#endif
		{
			if((fc_db.flowTbl[flowIdx].lutEgrDaIdx != SIGNED_INVALID) && fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutEgrDaIdx] &&
			((1<<(fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutEgrDaIdx]->spa))&RTK_FC_ALL_MAC_WLANCPU_PORTMASK) &&
#if defined(CONFIG_RTK_FC_WIFI_MULTIBAND_ACC_BY_ONE_LUT)
			(fc_db.flowTbl[flowIdx].egrWlanDevIdx < RTK_FC_WLANX_END_INTF)
#else
			(fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutEgrDaIdx]->wlan_dev_idx < RTK_FC_WLANX_END_INTF)
#endif
			)
			{
				//wifi tx: get SW  counter
				PROC_PRINTF("flow[%u]: wifi tx, pkt_cnt: %-10u, byte_cnt: %-u\n", flowIdx, fc_db.wfo_per_flow_cnt[flowIdx].pkt_cnt, fc_db.wfo_per_flow_cnt[flowIdx].byte_cnt);
			}
			else if((fc_db.flowTbl[flowIdx].lutIgrSaIdx != SIGNED_INVALID) && fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutIgrSaIdx] &&
			((1<<(fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutIgrSaIdx]->spa))&RTK_FC_ALL_MAC_WLANCPU_PORTMASK) &&
#if defined(CONFIG_RTK_FC_WIFI_MULTIBAND_ACC_BY_ONE_LUT)
			(fc_db.flowTbl[flowIdx].igrWlanDevIdx < RTK_FC_WLANX_END_INTF)
#else
			(fc_db.lutTbl[fc_db.flowTbl[flowIdx].lutIgrSaIdx]->wlan_dev_idx < RTK_FC_WLANX_END_INTF)
#endif
			)
			{
				//wifi rx: get SW counter
				PROC_PRINTF("flow[%u]: wifi rx, pkt_cnt: %-10u, byte_cnt: %-u\n", flowIdx, fc_db.wfo_per_flow_cnt[flowIdx].pkt_cnt, fc_db.wfo_per_flow_cnt[flowIdx].byte_cnt);
			}
		}
	}
	return SUCCESS;
}

int rtk_fc_proc_wfo_per_flow_share_cnt_sw_clear(struct file *file, const char *buffer, unsigned long count, void *data)
{
	uint32 flowIdx =_rtk_fc_proc_parsing_string_to_integer(buffer, count);

	if(flowIdx >= fc_db.flowSwTableSize)
	{
		rtlglue_printf("Invalid index %d, should be 0~%d.\n", flowIdx, fc_db.flowSwTableSize);
		return count;
	}

#if defined(CONFIG_RTL8198X_SERIES)
	_rtk_fc_wfo_update_ext_flow_mib_hisotry_count_by_flow(&(fc_db.flowTbl[flowIdx]), fc_db.wfo_per_flow_cnt[flowIdx].pkt_cnt, fc_db.wfo_per_flow_cnt[flowIdx].byte_cnt);
#endif
	memset(&fc_db.wfo_per_flow_cnt[flowIdx], 0, sizeof(fc_db.wfo_per_flow_cnt[flowIdx]));

	return count;
}

int rtk_fc_proc_wfo_per_hw_flow_share_mem_get(struct seq_file *s, void *v)
{
	int tbl_type, voqIdx, entIdx, i;
	uint8 *pData;
	uint32_t crc32;
	volatile rtk_fc_wfo_rx_cache_ent_t *rx_cache_data = NULL;
	rtk_fc_wfo_rx_cache_ent_t tmp_rx_cache_data = {0};
	volatile rtk_fc_wfo_tx_cache_ent_t *tx_cache_data = NULL;
	rtk_fc_wfo_tx_cache_ent_t tmp_tx_cache_data = {0};
	rtk_fc_wfo_non_cache_ent_t *non_cache_data = NULL;
	rtk_fc_wfo_non_cache_crc32_t *crc32_data = NULL;
	enum wfo_per_flow_mib_tbl
	{
		WFO_PER_FLOW_MIB_TBL_RX_CACHE = 0,
		WFO_PER_FLOW_MIB_TBL_TX_CACHE,
		WFO_PER_FLOW_MIB_TBL_NON_CACHE,
		WFO_PER_FLOW_MIB_TBL_MAX,
	};
	char *wfo_per_flow_mib_tbl_name[20] =
	{
		"[WFO rx cache]",
		"[WFO tx cache]",
		"[WFO non cache]",
	};
	bool if_tx;

	PROC_PRINTF("rx_entry_per_voq: %u\n", fc_db.wfo_per_flow_mib_info.rx_entry_per_voq);
	PROC_PRINTF("tx_entry_per_voq: %u\n\n", fc_db.wfo_per_flow_mib_info.tx_entry_per_voq);
	PROC_PRINTF("dmemWifiRxFlowMibCacheTbl_phyAddr: 0x%08x\n\n", fc_db.wfo_per_flow_mib_info.dmemWifiRxFlowMibCacheTbl_phyAddr);
	PROC_PRINTF("dmemWifiRxFlowMibCacheTbl_virAddr: %px\n\n", fc_db.dmemWifiRxFlowMibCacheTbl_virAddr);
	PROC_PRINTF("dmemWifiTxFlowMibCacheTbl_phyAddr: 0x%08x\n\n", fc_db.wfo_per_flow_mib_info.dmemWifiTxFlowMibCacheTbl_phyAddr);
	PROC_PRINTF("dmemWifiTxFlowMibCacheTbl_virAddr: %px\n\n", fc_db.dmemWifiTxFlowMibCacheTbl_virAddr);
	PROC_PRINTF("ddrWifiFlowMibNonCacheTbl_phyAddr: 0x%08x\n\n", fc_db.wfo_per_flow_mib_info.ddrWifiFlowMibNonCacheTbl_phyAddr);
	PROC_PRINTF("ddrWifiFlowMibNonCacheTbl_virAddr: %px\n\n", fc_db.ddrWifiFlowMibNonCacheTbl_virAddr);

	if(!fc_db.wfo_per_flow_mib_init_done)
	{
		PROC_PRINTF("[Note.] WFO per flow mib have not been initialized\n");
	}
	else
	{
		for(tbl_type = WFO_PER_FLOW_MIB_TBL_RX_CACHE ; tbl_type < WFO_PER_FLOW_MIB_TBL_MAX ; tbl_type++)
		{
			pData = NULL;

			/* get and print data */
			if(tbl_type == WFO_PER_FLOW_MIB_TBL_RX_CACHE)
			{
				if(fc_db.wfo_per_flow_mib_info.rx_entry_per_voq == 0)
					continue; // empty table, skip
				pData = fc_db.dmemWifiRxFlowMibCacheTbl_virAddr;
				PROC_PRINTF("%s: address %px\n", wfo_per_flow_mib_tbl_name[tbl_type], pData);
				if(pData)
				{
					rx_cache_data = (volatile rtk_fc_wfo_rx_cache_ent_t*)pData;
					PROC_PRINTF("%-4s\t%-12s\t%-12s\t%-6s\t%-7s\t%-4s\t%-25s\t%-15s\t%-15s\t%-15s\n",
						"idx",
						"PE port-voq", "per_voq_idx",
						"ldpid", "pol_id", "cos", "byte_cnt", "pkt_cnt", "byte_cnt_h", "byte_cnt_l");
					PROC_PRINTF("==================================================\n");
					for(voqIdx = 0 ; voqIdx < RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE ; voqIdx++)
					{
						for(entIdx = 0 ; entIdx < fc_db.wfo_per_flow_mib_info.rx_entry_per_voq ; entIdx++)
						{
							uint64_t byte_cnt;
							// It may be kernel panic if DMEM bit fields access
							tmp_rx_cache_data.pkt_cnt = rx_cache_data->pkt_cnt;
							tmp_rx_cache_data.byte_cnt_l = rx_cache_data->byte_cnt_l;
							tmp_rx_cache_data.byte_cnt_h = rx_cache_data->byte_cnt_h;
							tmp_rx_cache_data.bit32_field.raw_data = rx_cache_data->bit32_field.raw_data;

							byte_cnt = tmp_rx_cache_data.byte_cnt_h;
							byte_cnt = (byte_cnt << 32) + tmp_rx_cache_data.byte_cnt_l;
							PROC_PRINTF("%-4u\t0x%02x-%-7u\t%-12u\t0x%02x  \t%-7u\t%-4u\t%-25llu\t%-15u\t%-15u\t%-15u\n",
								(voqIdx*fc_db.wfo_per_flow_mib_info.rx_entry_per_voq)+entIdx,
								fc_db.wifi_amsdu_pe_offload_voq_tbl[voqIdx].ldpid, fc_db.wifi_amsdu_pe_offload_voq_tbl[voqIdx].cos, entIdx,
								tmp_rx_cache_data.bit32_field.bits.ldpid, tmp_rx_cache_data.bit32_field.bits.pol_id, tmp_rx_cache_data.bit32_field.bits.cos,
								byte_cnt, tmp_rx_cache_data.pkt_cnt,
								tmp_rx_cache_data.byte_cnt_h, tmp_rx_cache_data.byte_cnt_l);
							rx_cache_data++;
						}
					}
					PROC_PRINTF("\n\n");
				}
			}
			else if(tbl_type == WFO_PER_FLOW_MIB_TBL_TX_CACHE)
			{
				if(fc_db.wfo_per_flow_mib_info.tx_entry_per_voq == 0)
					continue;
				pData = fc_db.dmemWifiTxFlowMibCacheTbl_virAddr;
				PROC_PRINTF("%s: address %px\n", wfo_per_flow_mib_tbl_name[tbl_type], pData);
				if(pData)
				{
					tx_cache_data = (volatile rtk_fc_wfo_tx_cache_ent_t*)pData;
					PROC_PRINTF("%-4s\t%-12s\t%-12s\t%-7s\t%-9s\t%-25s\t%-15s\t%-15s\t%-15s\n",
						"idx", "PE port-voq", "per_voq_idx",
						"mac_id", "wifi_pri", "byte_cnt", "pkt_cnt", "byte_cnt_h", "byte_cnt_l");
					PROC_PRINTF("==================================================\n");
					for(voqIdx = 0 ; voqIdx < RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE ; voqIdx++)
					{
						for(entIdx = 0 ; entIdx < fc_db.wfo_per_flow_mib_info.tx_entry_per_voq ; entIdx++)
						{
							uint64_t byte_cnt;
							// It may be kernel panic if DMEM bit fields access
							tmp_tx_cache_data.pkt_cnt = tx_cache_data->pkt_cnt;
							tmp_tx_cache_data.byte_cnt_l = tx_cache_data->byte_cnt_l;
							tmp_tx_cache_data.byte_cnt_h = tx_cache_data->byte_cnt_h;
							tmp_tx_cache_data.bit32_field.raw_data = tx_cache_data->bit32_field.raw_data;

							byte_cnt = tmp_tx_cache_data.byte_cnt_h;
							byte_cnt = (byte_cnt << 32) + tmp_tx_cache_data.byte_cnt_l;
							PROC_PRINTF("%-4u\t0x%02x-%-7u\t%-12u\t%-7u\t%-9u\t%-25llu\t%-15u\t%-15u\t%-15u\n",
								(voqIdx*fc_db.wfo_per_flow_mib_info.tx_entry_per_voq)+entIdx,
								fc_db.wifi_amsdu_pe_offload_voq_tbl[voqIdx].ldpid, fc_db.wifi_amsdu_pe_offload_voq_tbl[voqIdx].cos, entIdx,
								tmp_tx_cache_data.bit32_field.bits.mac_id, tmp_tx_cache_data.bit32_field.bits.wifi_pri,
								byte_cnt, tmp_tx_cache_data.pkt_cnt,
								tmp_tx_cache_data.byte_cnt_h, tmp_tx_cache_data.byte_cnt_l);
							tx_cache_data++;
						}
					}
					PROC_PRINTF("\n\n");
				}
			}
			else if(tbl_type == WFO_PER_FLOW_MIB_TBL_NON_CACHE)
			{
				dma_addr_t dma_handle;
				size_t tbl_size = (sizeof(rtk_fc_wfo_non_cache_crc32_t)*(fc_db.flowHwTableSize/8) + (sizeof(rtk_fc_wfo_non_cache_ent_t)*fc_db.flowHwTableSize));
				pData = fc_db.ddrWifiFlowMibNonCacheTbl_virAddr;
				PROC_PRINTF("%s: address %px\n", wfo_per_flow_mib_tbl_name[tbl_type], pData);
				if(pData)
				{
					crc32_data = (rtk_fc_wfo_non_cache_crc32_t*)pData;
					non_cache_data = (rtk_fc_wfo_non_cache_ent_t*)(crc32_data + (fc_db.flowHwTableSize/8));

					PROC_PRINTF("Show valid entries only.\n");
					PROC_PRINTF("%-9s\t%-10s\t%-4s\t%-6s\t%-7s\t%-4s\t%-7s\t%-9s\t%-25s\t%-15s\t%-15s\t%-15s\n",
						"flow_idx", "CRC32", "dir",
						"ldpid", "pol_id", "cos",
						"mac_id", "wifi_pri",
						"byte_cnt", "pkt_cnt",
						"byte_cnt_h", "byte_cnt_l");
					PROC_PRINTF("==================================================\n");
					/* cache invalidate to DDR */
					dma_handle = dma_map_single(&(ni_info_data.pdev->dev), pData, tbl_size, DMA_FROM_DEVICE);
					dma_sync_single_for_cpu(&(ni_info_data.pdev->dev), dma_handle, tbl_size, DMA_FROM_DEVICE);

					for(entIdx = 0 ; entIdx < fc_db.flowHwTableSize ; entIdx += i)
					{
						for(i = 0 ; i < 8 ; i++)
						{
							uint64_t byte_cnt;
#if defined(CONFIG_RTK_L34_CANDIDATE_FLOW)
							if(fc_db.flowTbl[entIdx+i].candidateState != CANDIDATE_STATE_NONE)
#else
							if(fc_db.flowTbl[entIdx+i].pFlowEntry->path1.valid)
#endif
							{
								if(fc_db.flowTbl[entIdx+i].pFlowEntry->path1.out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_RX)
									if_tx = FALSE;
								else if(fc_db.flowTbl[entIdx+i].pFlowEntry->path1.out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_TX)
									if_tx = TRUE;
								else
									continue;

								switch(i)
								{
									case 0:
										crc32 = crc32_data->crc32_0;
										break;
									case 1:
										crc32 = crc32_data->crc32_1;
										break;
									case 2:
										crc32 = crc32_data->crc32_2;
										break;
									case 3:
										crc32 = crc32_data->crc32_3;
										break;
									case 4:
										crc32 = crc32_data->crc32_4;
										break;
									case 5:
										crc32 = crc32_data->crc32_5;
										break;
									case 6:
										crc32 = crc32_data->crc32_6;
										break;
									case 7:
										crc32 = crc32_data->crc32_7;
										break;
									default:
										break;
								}
								byte_cnt = (non_cache_data+(entIdx+i))->byte_cnt_h;
								byte_cnt = (byte_cnt << 32) + (non_cache_data+(entIdx+i))->byte_cnt_l;
								PROC_PRINTF("%-9d\t0x%08x\t%-4s\t%-6u\t%-7u\t%-4u\t%-7u\t%-9u\t%-25llu\t%-15u\t%-15u\t%-15u\n",
									entIdx+i, crc32, if_tx?"[TX]":"[RX]",
									(non_cache_data+(entIdx+i))->ldpid, (non_cache_data+(entIdx+i))->pol_id, (non_cache_data+(entIdx+i))->cos,
									(non_cache_data+(entIdx+i))->mac_id, (non_cache_data+(entIdx+i))->wifi_pri,
									byte_cnt, (non_cache_data+(entIdx+i))->pkt_cnt,
									(non_cache_data+(entIdx+i))->byte_cnt_h, (non_cache_data+(entIdx+i))->byte_cnt_l);
							}
						}
						crc32_data++;
					}
					dma_unmap_single(&(ni_info_data.pdev->dev), dma_handle, tbl_size, DMA_FROM_DEVICE);
					PROC_PRINTF("\n\n");
				}
			}
			else
			{
				PROC_PRINTF("Unknown table type: %u\n", tbl_type);
				continue;
			}
		}
	}

	return SUCCESS;
}

void rtk_fc_proc_wfo_per_hw_flow_share_mem_usage(void)
{
	rtlglue_printf("usage:\n");
	rtlglue_printf("-  dump trx cache list:\n");
	rtlglue_printf("   echo dump_cache_list > /proc/fc/sw_dump/wfo_per_hw_flow_share_mem \n");
	rtlglue_printf("   echo dump_cache_list rx > /proc/fc/sw_dump/wfo_per_hw_flow_share_mem \n");
	rtlglue_printf("   echo dump_cache_list tx > /proc/fc/sw_dump/wfo_per_hw_flow_share_mem \n");
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB_FC_TEST)
	rtlglue_printf("-  init by fc self memory test:\n");
	rtlglue_printf("   echo fc_self_mem_test > /proc/fc/sw_dump/wfo_per_hw_flow_share_mem \n");
	rtlglue_printf("-  clear fc self memory test status:\n");
	rtlglue_printf("   echo dis_fc_self_mem_test > /proc/fc/sw_dump/wfo_per_hw_flow_share_mem \n");
#endif
}

int rtk_fc_proc_wfo_per_hw_flow_share_mem_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int len = (count >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : count;
	int i;
	uint16 pri_cnt = 0;

	if(buffer)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		strptr=tmpBuf;

		split_str=strsep(&strptr," ");//get flow hash pattern
		if(split_str==NULL)
		{
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			rtk_fc_proc_wfo_per_hw_flow_share_mem_usage();
			return count;
		}

		if(strcasecmp(split_str,"dump_cache_list")==0)
		{
			bool if_dump_rx = FALSE, if_dump_tx = FALSE;
			rtk_fc_wfo_cache_mib_list_t *pWfoCacheMibListEntry, *pNextWfoCacheMibListEntry;

			split_str=strsep(&strptr," ");//get flow hash pattern
			if(split_str!=NULL)
			{
				if(strcasecmp(split_str,"rx")==0)
					if_dump_rx = TRUE;
				else if(strcasecmp(split_str,"tx")==0)
					if_dump_tx = TRUE;
				else
				{
					if_dump_rx = TRUE;
					if_dump_tx = TRUE;
				}
			}
			else
			{
				if_dump_rx = TRUE;
				if_dump_tx = TRUE;
			}

			RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
			// show entry in wfo_rx_cache_mib_freeListHead[i]
			if(if_dump_rx)
			{
				for( i = 0 ; i < RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE ; i++)
				{
					pri_cnt = 0;
					rtlglue_printf("\033[1;31mwfo_rx_cache_mib_freeListHead for voq %d\033[0m: \033[1;36mdump format [cacheIdx (voqIdx, perVoqIdx)]\033[0m\n\t", i);
					// traverse wfo_rx_cache_mib_freeListHead[i]
					if(!list_empty(&fc_db.wfo_rx_cache_mib_freeListHead[i]))
					{
						list_for_each_entry_safe(pWfoCacheMibListEntry, pNextWfoCacheMibListEntry, &fc_db.wfo_rx_cache_mib_freeListHead[i], wfoCacheMib_list)
						{
							rtlglue_printf(KERN_CONT"%s[%04u (%02u, %02u)]", (pri_cnt==0)?"  ":"->", pWfoCacheMibListEntry->cacheIdx, pWfoCacheMibListEntry->voqIdx, pWfoCacheMibListEntry->perVoqIdx);
							if(((++pri_cnt)%5) == 0)
								rtlglue_printf(KERN_CONT"\n\t");
						}
						rtlglue_printf("\n");
					}
					else
						rtlglue_printf(KERN_CONT"empty\n\n");
				}

				//show rx entry in use
				rtlglue_printf("\033[1;31mwfo_rx_cache_mib in use\033[0m: \033[1;36mdump format [cacheIdx (voqIdx, perVoqIdx)]\033[0m\n\t");
				pri_cnt = 0;
				for( i = 0 ; i < (RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE * fc_db.wfo_per_flow_mib_info.rx_entry_per_voq) ; i++)
				{
					if(list_empty(&fc_db.wfo_rx_cache_mib_list[i].wfoCacheMib_list))
					{
						rtlglue_printf(KERN_CONT"%s[%04u (%02u, %02u)]", (pri_cnt==0)?"  ":", ", fc_db.wfo_tx_cache_mib_list[i].cacheIdx, fc_db.wfo_tx_cache_mib_list[i].voqIdx, fc_db.wfo_tx_cache_mib_list[i].perVoqIdx);
						if(((++pri_cnt)%5) == 0)
							rtlglue_printf(KERN_CONT"\n\t");
					}
				}
				rtlglue_printf("\n");
			}

			if(if_dump_tx)
			{
				// show entry in wfo_tx_cache_mib_freeListHead[i]
				for( i = 0 ; i < RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE ; i++)
				{
					pri_cnt = 0;
					rtlglue_printf("\033[1;31mwfo_tx_cache_mib_freeListHead for voq %d\033[0m: \033[1;36mdump format [cacheIdx (voqIdx, perVoqIdx)]\033[0m\n\t", i);
					// traverse wfo_tx_cache_mib_freeListHead[i]
					if(!list_empty(&fc_db.wfo_tx_cache_mib_freeListHead[i]))
					{
						list_for_each_entry_safe(pWfoCacheMibListEntry, pNextWfoCacheMibListEntry, &fc_db.wfo_tx_cache_mib_freeListHead[i], wfoCacheMib_list)
						{
							rtlglue_printf(KERN_CONT"%s[%04u (%02u, %02u)]", (pri_cnt==0)?"  ":"->", pWfoCacheMibListEntry->cacheIdx, pWfoCacheMibListEntry->voqIdx, pWfoCacheMibListEntry->perVoqIdx);
							if(((++pri_cnt)%5) == 0)
								rtlglue_printf(KERN_CONT"\n\t");
						}
						rtlglue_printf("\n");
					}
					else
						rtlglue_printf(KERN_CONT"empty\n\n");
				}

				//show tx entry in use
				rtlglue_printf("\033[1;31mwfo_tx_cache_mib in use\033[0m: \033[1;36mdump format [cacheIdx (voqIdx, perVoqIdx)]\033[0m\n\t");
				pri_cnt = 0;
				for( i = 0 ; i < (RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE * fc_db.wfo_per_flow_mib_info.tx_entry_per_voq) ; i++)
				{
					if(list_empty(&fc_db.wfo_tx_cache_mib_list[i].wfoCacheMib_list))
					{
						rtlglue_printf(KERN_CONT"%s[%04u (%02u, %02u)]", (pri_cnt==0)?"  ":", ", fc_db.wfo_tx_cache_mib_list[i].cacheIdx, fc_db.wfo_tx_cache_mib_list[i].voqIdx, fc_db.wfo_tx_cache_mib_list[i].perVoqIdx);
						if(((++pri_cnt)%5) == 0)
							rtlglue_printf(KERN_CONT"\n\t");
					}
				}
				rtlglue_printf("\n");
			}
			RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
		}
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB_FC_TEST)
		else if(strcasecmp(split_str,"fc_self_mem_test")==0)
		{
			rtk_fc_wfo_per_flow_mib_info_t info = {0};

			info.rx_entry_per_voq = 31;
			info.tx_entry_per_voq = 31;

			if(fc_db.pDmemWifiRxFlowMibCacheTbl == NULL)
				fc_db.pDmemWifiRxFlowMibCacheTbl = RTK_FC_HELPER_FC_KMALLOC(RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE * info.rx_entry_per_voq * sizeof(rtk_fc_wfo_rx_cache_ent_t), GFP_ATOMIC);
			if(fc_db.pDmemWifiTxFlowMibCacheTbl == NULL)
				fc_db.pDmemWifiTxFlowMibCacheTbl = RTK_FC_HELPER_FC_KMALLOC(RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE * info.tx_entry_per_voq * sizeof(rtk_fc_wfo_tx_cache_ent_t), GFP_ATOMIC);
			if(fc_db.pDdrWifiFlowMibNonCacheTbl == NULL)
				fc_db.pDdrWifiFlowMibNonCacheTbl = RTK_FC_HELPER_FC_KMALLOC((sizeof(rtk_fc_wfo_non_cache_crc32_t)*(fc_db.flowHwTableSize/8) + (sizeof(rtk_fc_wfo_non_cache_ent_t)*fc_db.flowHwTableSize)), GFP_ATOMIC);
			if((fc_db.pDmemWifiRxFlowMibCacheTbl == NULL) || (fc_db.pDmemWifiTxFlowMibCacheTbl == NULL) || (fc_db.pDdrWifiFlowMibNonCacheTbl == NULL))
			{
				printk("[ERROR] allocate table failed!");
				return count;
			}

			/* For debug*/
			{
				rtlglue_printf("[FC internal allocate memory for test] show virtual memory\n");
				rtlglue_printf("dmemWifiRxFlowMibCacheTbl: %px\n", fc_db.pDmemWifiRxFlowMibCacheTbl);
				rtlglue_printf("dmemWifiTxFlowMibCacheTbl: %px\n", fc_db.pDmemWifiTxFlowMibCacheTbl);
				rtlglue_printf("ddrWifiFlowMibNonCacheTbl: %px\n", fc_db.pDdrWifiFlowMibNonCacheTbl);
			}

			info.dmemWifiRxFlowMibCacheTbl_phyAddr = virt_to_phys(fc_db.pDmemWifiRxFlowMibCacheTbl);
			info.dmemWifiTxFlowMibCacheTbl_phyAddr = virt_to_phys(fc_db.pDmemWifiTxFlowMibCacheTbl);
			info.ddrWifiFlowMibNonCacheTbl_phyAddr = virt_to_phys(fc_db.pDdrWifiFlowMibNonCacheTbl);

			rtk_fc_wfo_per_flow_mib_info_init(info);
		}
		else if(strcasecmp(split_str,"dis_fc_self_mem_test")==0)
		{
			uint8_t rx_entry_per_voq = fc_db.wfo_per_flow_mib_info.rx_entry_per_voq;
			uint8_t tx_entry_per_voq = fc_db.wfo_per_flow_mib_info.tx_entry_per_voq;
			rtk_fc_wfo_per_flow_mib_info_clear();

			if(fc_db.pDmemWifiRxFlowMibCacheTbl)
			{
				RTK_FC_HELPER_FC_KFREE(fc_db.pDmemWifiRxFlowMibCacheTbl, RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE * rx_entry_per_voq * sizeof(rtk_fc_wfo_rx_cache_ent_t));
				fc_db.pDmemWifiRxFlowMibCacheTbl = NULL;

			}
			if(fc_db.pDmemWifiTxFlowMibCacheTbl)
			{
				RTK_FC_HELPER_FC_KFREE(fc_db.pDmemWifiTxFlowMibCacheTbl, RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE * tx_entry_per_voq * sizeof(rtk_fc_wfo_tx_cache_ent_t));
				fc_db.pDmemWifiTxFlowMibCacheTbl = NULL;

			}
			if(fc_db.pDdrWifiFlowMibNonCacheTbl)
			{
				RTK_FC_HELPER_FC_KFREE(fc_db.pDdrWifiFlowMibNonCacheTbl, (sizeof(rtk_fc_wfo_non_cache_crc32_t)*(fc_db.flowHwTableSize/8) + (sizeof(rtk_fc_wfo_non_cache_ent_t)*fc_db.flowHwTableSize)));
				fc_db.pDdrWifiFlowMibNonCacheTbl = NULL;
			}

			/* For debug*/
			{
				rtlglue_printf("[FC internal free memory for test] show virtual memory\n");
				rtlglue_printf("dmemWifiRxFlowMibCacheTbl: %px\n", fc_db.pDmemWifiRxFlowMibCacheTbl);
				rtlglue_printf("dmemWifiTxFlowMibCacheTbl: %px\n", fc_db.pDmemWifiTxFlowMibCacheTbl);
				rtlglue_printf("ddrWifiFlowMibNonCacheTbl: %px\n", fc_db.pDdrWifiFlowMibNonCacheTbl);
			}
		}
#endif
		else
		{
			rtlglue_printf("[Error]Invalid parameter, please reference the usage! \n");
			rtk_fc_proc_wfo_per_hw_flow_share_mem_usage();
			return count;
		}
	}

	return count;
}
#endif
#endif

int rtk_fc_igmp_mcExtFlowIdxTbl_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	uint32 value;
	uint32 sendAllDummy=0;
	
	if (buffer)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, CMD_BUFF_SIZE);

		if(_rtk_fc_parseGetValue(tmpBuf,&value,"SEND",UNSIGNED_LONG) == SUCCESS)
		{
			sendAllDummy=value;
		}
		if(sendAllDummy)
			rtk_fc_igmp_sendAllMcDummyPktDetector(0);
		
	}

	return count;

}


int rtk_fc_proc_abstrSwFlowTemplateType_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	uint32 value;
	int add;		//add:1 del:0
	uint8 pri=0;
	uint32 typeMsk=0;
	
	if (buffer)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, CMD_BUFF_SIZE);


		if(_rtk_fc_parseGetValue(tmpBuf,&add,"ADD",UNSIGNED_LONG)==SUCCESS)
			add=1;
		else if(_rtk_fc_parseGetValue(tmpBuf,&add,"DEL",UNSIGNED_LONG)==SUCCESS)
			add=0;
		else
		{
			printk("ADD/DEL parser error\n");
			goto ABSTR_TYPE_ERR;
		}

		if(_rtk_fc_parseGetValue(tmpBuf,&pri,"TYPE_PRI",UNSIGNED_LONG)==FAIL)
		{
			printk("TYPE_PRI parser error\n");
			goto ABSTR_TYPE_ERR;
		}
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_PHY_LSPID",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_PHY_LSPID);
		}
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_PHY_STREAMID",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_PHY_STREAMID);
		}
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_DMAC",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_DMAC);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_SMAC",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_SMAC);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_ETH",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_ETH);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_CVLANTAG",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_CVLANTAG);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_CVLAN",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_CVLAN);
		}		

		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_CPRI",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_CPRI);
		}		

		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_CDEI",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_CDEI);
		}		

		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_SVLANTAG",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_SVLANTAG);
		}		

		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_SVLAN",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_SVLAN);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_SPRI",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_SPRI);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_SDEI",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_SDEI);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_PPPOETAG",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_PPPOETAG);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L2_PPPOESID",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L2_PPPOESID);
		}	
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L3_IP_VLD",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L3_IP_VLD);
		}			
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L3_IPVER46",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L3_IPVER46);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L3_DIP",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L3_DIP);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L3_SIP",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L3_SIP);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L3_TOS",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L3_TOS);
		}				
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L4_PROTO",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L4_PROTO);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L4_DPORT",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L4_DPORT);
		}		
		if(_rtk_fc_parseGetValue(tmpBuf,&value,"FLOW_L4_SPORT",UNSIGNED_LONG)==SUCCESS)
		{
			typeMsk |= TYPETOMSK(FLOW_L4_SPORT);
		}		


		if(add)
			rtk_fc_abstrSwFlowSetTemplateType(typeMsk,pri);
		else
			rtk_fc_abstrSwFlowDelTemplateType(pri);
		
	}

ABSTR_TYPE_ERR:
	return count;

}



int rtk_fc_proc_abstrSwFlowTemplateType_get(struct seq_file *s, void *v)
{
	int type;
	PROC_PRINTF("Usage:\n");
	PROC_PRINTF("Patten: [ADD 1]/[DEL 1] [TYPE_PRI] option:[FLOW_PHY_LSPID] [FLOW_PHY_STREAMID] [FLOW_L2_DMAC] [FLOW_L2_SMAC] [FLOW_L2_ETH] [FLOW_L2_CVLANTAG] [FLOW_L2_CPRI] [FLOW_L2_CDEI]\n");
	PROC_PRINTF("[FLOW_L2_SVLANTAG] [FLOW_L2_SVLAN] [FLOW_L2_SPRI] [FLOW_L2_SDEI] [FLOW_L2_PPPOETAG] [FLOW_L2_PPPOESID][FLOW_L3_IP_VLD] [FLOW_L3_IPVER46] \n");
	PROC_PRINTF("[FLOW_L3_DIP] [FLOW_L3_SIP] [FLOW_L3_TOS] [FLOW_L4_PROTO] [FLOW_L4_DPORT] [FLOW_L4_SPORT]  \n");
	PROC_PRINTF("Example:\n");
	PROC_PRINTF("echo ADD 1  TYPE_PRI 1 FLOW_PHY_LSPID 1 FLOW_PHY_STREAMID 1 FLOW_L2_DMAC 1 FLOW_L2_SMAC 1 > proc/fc/ctrl/abstrSwFlowTmpType\n");

	for(type=0; type<ABSTR_SWFLOW_TYPE_SIZE; type++)
	{
		if(!fc_db.abstrSwFlowType[type].valid)
			continue;
		dump_abstrSwflowType_table(s,v,type);
	}


	return SUCCESS;
}



int rtk_fc_proc_mcSetMode_get(struct seq_file *s, void *v)
{
    int len=0;
	if(fc_db.mcSetMode==RT_IGMP_MULTICAST_SYNC_AUTO)
		PROC_PRINTF("mcSetMode :[%d] RT_IGMP_MULTICAST_SYNC_AUTO\n",fc_db.mcSetMode);
	else if (fc_db.mcSetMode==RT_IGMP_MULTICAST_SYNC_MODE_USER)
		PROC_PRINTF("mcSetMode :[%d] RT_IGMP_MULTICAST_SYNC_MODE_USER\n",fc_db.mcSetMode);
	else if (fc_db.mcSetMode==RT_IGMP_MULTICAST_SYNC_MODE_KERNEL)
		PROC_PRINTF("mcSetMode :[%d] RT_IGMP_MULTICAST_SYNC_MODE_KERNEL\n",fc_db.mcSetMode);
		
    return len;
}

//.unlockBefortWrite=1
int rtk_fc_proc_mcSetMode_set(struct file *filp, const char *buff,unsigned long len, void *data)
{

	uint32 mcSetMode = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	rtk_fc_igmp_mcMode(mcSetMode);

	rtk_fc_proc_mcSetMode_get(NULL,NULL);
	return len;
}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)

int rtk_fc_dynamic_pe_cfg_get(struct seq_file *s, void *v)
{
	if(fc_db.controlFuc.dynamic_pe_cfg.enable)
	{
		PROC_PRINTF("DYNAMIC PE CONFIG:\n");
		PROC_PRINTF("\t PE status:%s",fc_db.controlFuc.dynamic_pe_cfg.status?"PE ON":"PE OFF");		
		PROC_PRINTF("\t start_signal:%d stop_signal:%d processId:%d detect_timeout:%d SEC (minConfig of detect_timeout is %d)\n",
			fc_db.controlFuc.dynamic_pe_cfg.start_signal,fc_db.controlFuc.dynamic_pe_cfg.stop_signal,fc_db.controlFuc.dynamic_pe_cfg.processId,fc_db.controlFuc.dynamic_pe_cfg.detect_timeout,fc_db.flowSyncPeriod_unit1s);
		PROC_PRINTF("\t detect_period_time:%d MSEC tc_packet_cfg:%d tc_packet_cnt:%d last_tcPkt_jiffies:%llu \n",
			fc_db.controlFuc.dynamic_pe_cfg.detect_period_time,fc_db.controlFuc.dynamic_pe_cfg.tc_packet_cfg,fc_db.controlFuc.dynamic_pe_cfg.tc_packet_cnt,fc_db.controlFuc.dynamic_pe_cfg.last_tcPkt_jiffies);
	}
	else
	{
		PROC_PRINTF("dynamic_pe function disable \n");
		PROC_PRINTF("usage: \n");
		PROC_PRINTF("[DYNAMIC_PE_EN][START_SIG][STOP_SIG][PROCESS_ID][TIMEOUT][TC_PKT_CFG][DETECT_PERIOD] \n");
	}


	return 0;
}
int rtk_fc_dynamic_pe_cfg_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[128] = {0};
	int len = (count >= 128) ? 128 : count;
	int enable=FAIL,start_signal=FAIL,stop_signal=FAIL,detect_timeout=FAIL,process_id=FAIL;
	uint32 tc_packet_cfg=20,detect_period_time=200;
	
	if (buffer)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		if(_rtk_fc_parseGetValue(tmpBuf,&enable,"DYNAMIC_PE_EN",UNSIGNED_LONG)!=SUCCESS)
			;
		if(_rtk_fc_parseGetValue(tmpBuf,&start_signal,"START_SIG",UNSIGNED_LONG)!=SUCCESS)
			;
		if(_rtk_fc_parseGetValue(tmpBuf,&stop_signal,"STOP_SIG",UNSIGNED_LONG)!=SUCCESS)
			;
		if(_rtk_fc_parseGetValue(tmpBuf,&detect_timeout,"TIMEOUT",UNSIGNED_LONG)!=SUCCESS)
			;
		if(_rtk_fc_parseGetValue(tmpBuf,&process_id,"PROCESS_ID",UNSIGNED_LONG)!=SUCCESS)
			;
		if(_rtk_fc_parseGetValue(tmpBuf,&tc_packet_cfg,"TC_PKT_CFG",UNSIGNED_LONG)!=SUCCESS)
			;
		if(_rtk_fc_parseGetValue(tmpBuf,&detect_period_time,"DETECT_PERIOD",UNSIGNED_LONG)!=SUCCESS)
			;
		

		
		if(enable==FAIL || start_signal==FAIL || stop_signal==FAIL || detect_timeout==FAIL || process_id==FAIL)	
		{
			printk("Parser FAIL enable:%d start_signal:%d stop_signal:%d detect_timeout:%d process_id:%d\n",enable,start_signal,stop_signal,detect_timeout,process_id);
			return count;
		}
		fc_db.controlFuc.dynamic_pe_cfg.enable=enable&0xff;
		fc_db.controlFuc.dynamic_pe_cfg.start_signal=start_signal&0xff;
		fc_db.controlFuc.dynamic_pe_cfg.stop_signal=stop_signal&0xff;
		fc_db.controlFuc.dynamic_pe_cfg.detect_timeout=detect_timeout <fc_db.flowSyncPeriod_unit1s ? fc_db.flowSyncPeriod_unit1s:detect_timeout;
		fc_db.controlFuc.dynamic_pe_cfg.processId = process_id<=0?0:process_id;
		fc_db.controlFuc.dynamic_pe_cfg.tc_packet_cfg=tc_packet_cfg;
		fc_db.controlFuc.dynamic_pe_cfg.detect_period_time=detect_period_time;

	}
	return len;
}
#endif

#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)	
int rtk_fc_proc_mc_amsdu_flow_get(struct seq_file *s, void *v)
{

	int i;
	for(i=0;i<WLAN_MC_TO_UC_MAC_POOL_MAX ;i++)
	{
		if(fc_db.amsdu2PeFlowRef[i].valid)
		{
			PROC_PRINTF("[%d] flowIdx:%d  key_macid:%d key_lutIdx:%d refCnt:%d \n",i,
				fc_db.amsdu2PeFlowRef[i].flowIdx,fc_db.amsdu2PeFlowRef[i].key_macid,fc_db.amsdu2PeFlowRef[i].key_lutIdx,fc_db.amsdu2PeFlowRef[i].refCnt);
		}
	}
	return 0;
}
#endif

int rtk_fc_proc_mc_set_get(struct seq_file *s, void *v)
{
	int i=0;
	rtk_fc_table_mcConfigTbl_t *pMcConfig;	
	PROC_PRINTF("==============================\n");
	PROC_PRINTF("CONFIG LIST:\n");
	
	list_for_each_entry(pMcConfig,&fc_db.listHead_mcCfgTbl,lsMcConfigEntry)
	{
		int j;
		struct net_device *dev=NULL;
		rtk_fc_realdev_t rdev;
		
		if(pMcConfig->isIpv6)
		{
			PROC_PRINTF("[%d]GroupAddress [%pI6]   <== (carebit:%d)SourceAddr [%pI6]  (carebit:%d)IngressCvid[%d]  (carebit:%d)IngressCvid[%d] %s\n",
				i,pMcConfig->confGroup,pMcConfig->careSource, pMcConfig->confSource,pMcConfig->careIngressCvid,pMcConfig->confIgrCvid,pMcConfig->careIngressSvid,pMcConfig->confIgrSvid,pMcConfig->copy2cpu?"COPY2CPU":"");
		}
		else
		{
			PROC_PRINTF("[%d]GroupAddress [%pI4h]   <== (carebit:%d)SourceAddr [%pI4h]  (carebit:%d)IngressCvid[%d]  (carebit:%d)IngressCvid[%d] %s\n",
				i,&pMcConfig->confGroup,pMcConfig->careSource,&pMcConfig->confSource,pMcConfig->careIngressCvid,pMcConfig->confIgrCvid,pMcConfig->careIngressSvid,pMcConfig->confIgrSvid,pMcConfig->copy2cpu?"COPY2CPU":"");
		}


		PROC_PRINTF("\t");
		for(j=0;j<pMcConfig->cnt_egrInfo;j++)
		{

			if(pMcConfig->egrInfo[j].ifidx!=FAIL)
			{
				RTK_FC_HOOK_PS_DEV_GET_BY_INDEX(&init_net,pMcConfig->egrInfo[j].ifidx,&dev);
				if(dev)
				{
					RTK_FC_HOOK_PS_DEV_GET_REALDEV_PHYPORT(dev,&rdev);
					if(pMcConfig->egrInfo[j].isWlan)
						PROC_PRINTF(" [WlanPort:%d(%s ifidx=%d)] ",pMcConfig->egrInfo[j].portId,dev->name,pMcConfig->egrInfo[j].ifidx);
					else
						PROC_PRINTF(" [Port:%d(%s ifidx=%d)] ",pMcConfig->egrInfo[j].portId,dev->name,pMcConfig->egrInfo[j].ifidx);

				}
			}
			else
			{
				if(pMcConfig->egrInfo[j].isWlan)
					PROC_PRINTF(" [WlanPort:%d] ",pMcConfig->egrInfo[j].portId);
				else
					PROC_PRINTF(" [Port:%d] ",pMcConfig->egrInfo[j].portId);
			}
		}
		PROC_PRINTF("\n");

		i++;
	}


return 0;
}
void _rtk_fc_proc_mc_set_set_cfg(uint32 flushtable, uint8 isIpv6, uint32 *GIP, int GIP_size, uint32 *SIP, int SIP_size, uint32 groupknown, uint32 groupunknown,
										uint32 careSip, uint32 careCvid, uint32 ingressCtagIf, int32 cvid, uint32 careSvid, uint32 ingressStagIf, int32 svid, uint32 first_act_portmask, int32 first_act_cvid,uint32 first_act_cpri,
										uint32 act1_mask,int32  second_act_cvid, uint32 second_act_cpri)
{
	
	rt_igmp_multicast_group_cfg_t *mc_cfg = NULL;

	mc_cfg = RTK_FC_HELPER_FC_KMALLOC(sizeof(rt_igmp_multicast_group_cfg_t), GFP_ATOMIC);
	if(mc_cfg == NULL){
		rtlglue_printf("[WARRING]mc_cfg malloc failed\n");
		return ;
	}
	bzero(mc_cfg,sizeof(rt_igmp_multicast_group_cfg_t));
	
	if(!flushtable)
	{
		mc_cfg->is_ipv6=isIpv6;
		if(isIpv6)
		{
			memcpy(mc_cfg->group_addr.ipv6,GIP,sizeof(GIP_size));
			memcpy(mc_cfg->source_addr.ipv6,SIP,sizeof(SIP_size));
		}
		else
		{
			mc_cfg->group_addr.ipv4=ntohl(GIP[0]);
			mc_cfg->source_addr.ipv4=ntohl(SIP[0]);
		}

		if(groupknown || groupunknown)
		{
			if(groupknown)
				mc_cfg->groupBehavior = RT_MC_BEHAVIOR_GROUP_AS_KNOW;
			else
				mc_cfg->groupBehavior = RT_MC_BEHAVIOR_GROUP_AS_UNKNOW;
		}
		else
		{
			mc_cfg->careSourceAddress= careSip;
			
			mc_cfg->careIngressCvid = careCvid;
			mc_cfg->ingress_ctagif = ingressCtagIf;
			mc_cfg->ingress_cvid = cvid;
			
			mc_cfg->careIngressSvid = careSvid;
			mc_cfg->ingress_stagif = ingressStagIf;
			mc_cfg->ingress_svid = svid;

			mc_cfg->first_act_portmask	= first_act_portmask;
			mc_cfg->first_act_ctagif = first_act_cvid==-1?0:1;
			if(first_act_cvid!=-1){ // Fixing coverity: overflowed constant
				mc_cfg->first_act_cvid	= first_act_cvid;
			}
			mc_cfg->first_act_cpri	= first_act_cpri;

			mc_cfg->second_act_portmask	= act1_mask;
			mc_cfg->second_act_ctagif	= second_act_cvid==-1?0:1;
			mc_cfg->second_act_cvid	= (unsigned int)(second_act_cvid&0xff);
			mc_cfg->second_act_cpri	= second_act_cpri;
		}
	}

	rtk_fc_igmp_mcGroupSet(*mc_cfg);

	RTK_FC_HELPER_FC_KFREE(mc_cfg, sizeof(rt_igmp_multicast_group_cfg_t));
}

//.unlockBefortWrite=1
int rtk_fc_proc_mc_set_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[128] = {0};
	int len = (count >= 128) ? 128 : count;

	uint32 GIP[4]={0};
	uint32 SIP[4]={0};
	uint32 first_act_portmask=0,act1_mask=0;
	uint8 isIpv6=0;
	uint32 flushtable=0,groupknown=0,groupunknown=0;
	int32  second_act_cvid=-1,first_act_cvid=-1;
	uint32 second_act_cpri=0,first_act_cpri=0;
	uint32 careSip=0;
	uint32 careCvid=0,ingressCtagIf=0;
	int32 cvid=-1;
	uint32 careSvid=0,ingressStagIf=0;
	int32 svid=-1;


	if(fc_db.mcSetMode != RT_IGMP_MULTICAST_SYNC_MODE_USER)
	{
		rtlglue_printf("[WARRING]MCMOD NOT USER MODE!!!\n");
	}

	if (buffer)
	{
		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, len);

		if((_rtk_fc_parseGetValue(tmpBuf,&flushtable,"FLUSH",UNSIGNED_LONG)) == SUCCESS)
			;
		if(flushtable)
		{
			rtlglue_printf("FLUSH ALL Table\n");
			goto CHANGE_TABLE;
		}
		if(_rtk_fc_parseGetValue(tmpBuf,&GIP,"GIP4",V4IP)==SUCCESS)
		{
			if(_rtk_fc_parseGetValue(tmpBuf,&SIP,"SIP4",V4IP)==SUCCESS)
				careSip=1;
			isIpv6=0;
		}
		else if(_rtk_fc_parseGetValue(tmpBuf,&GIP,"GIP6",V6IP)==SUCCESS)
		{
			if(_rtk_fc_parseGetValue(tmpBuf,&SIP,"SIP6",V6IP)==SUCCESS)
				careSip=1;
			isIpv6=1;
		}
		else
		{
			rtlglue_printf("Can't find GIP4/GIP6 Error\n");
			goto PARSE_ERROR;
		}
		
		if((_rtk_fc_parseGetValue(tmpBuf,&groupknown,"GROUP_AS_KNOWN",UNSIGNED_LONG))==SUCCESS)
			;
		if((_rtk_fc_parseGetValue(tmpBuf,&groupunknown,"GROUP_AS_UNKNOWN",UNSIGNED_LONG))==SUCCESS)
			;
		if(groupknown || groupunknown)
		{
			goto CHANGE_TABLE;
		}
		
		if(_rtk_fc_parseGetValue(tmpBuf,&first_act_portmask,"PORTMASK",UNSIGNED_LONG)!=SUCCESS)
		{
			rtlglue_printf("Can't find PORTMASK Error\n");
			goto PARSE_ERROR;

		}

		//option for vlan setting
		if((_rtk_fc_parseGetValue(tmpBuf,&act1_mask,"ACT1_MASK",UNSIGNED_LONG))==SUCCESS)
			;
		if(_rtk_fc_parseGetValue(tmpBuf,&second_act_cvid,"second_act_cvid",SINGED_LONG)==SUCCESS)
		{
			if((_rtk_fc_parseGetValue(tmpBuf,&second_act_cpri,"second_act_cpri",UNSIGNED_LONG))==SUCCESS)
				;
			if( 7 < second_act_cpri)
			{
				rtlglue_printf("Error config second_act_cpri=%d  \n",second_act_cpri);
				second_act_cpri=0;
			}
		}
		else if(_rtk_fc_parseGetValue(tmpBuf,&second_act_cvid,"ACT1_CVID",SINGED_LONG)==SUCCESS)
		{
			if((_rtk_fc_parseGetValue(tmpBuf,&second_act_cpri,"ACT1_CPRI",UNSIGNED_LONG))==SUCCESS)
				;
			if( 7 < second_act_cpri)
			{
				rtlglue_printf("Error config second_act_cpri=%d  \n",second_act_cpri);
				second_act_cpri=0;
			}
		}

		//option for vlan1 setting  syn Pmsk with "PORTMASK"
		if(_rtk_fc_parseGetValue(tmpBuf,&first_act_cvid,"CVID",SINGED_LONG)==SUCCESS)
		{
			if((_rtk_fc_parseGetValue(tmpBuf,&first_act_cpri,"CPRI",UNSIGNED_LONG))==SUCCESS)
				;
			if( 7 < first_act_cpri)
			{
				rtlglue_printf("Error config first_act_cpri=%d  \n",first_act_cpri);
				first_act_cpri=0;
			}

		}

		if(_rtk_fc_parseGetValue(tmpBuf,&cvid,"INGRESS_CVID",SINGED_LONG)==SUCCESS)
		{
			careCvid=1;
			if(cvid<0 || cvid>=4096)
			{
				cvid=FAIL;
			}
			else
				ingressCtagIf=1;
				
		}
		if(_rtk_fc_parseGetValue(tmpBuf,&svid,"INGRESS_SVID",SINGED_LONG)==SUCCESS)
		{
			careSvid=1;
			if(svid<0 || svid>=4096)
			{
				svid=FAIL;
			}
			else
				ingressStagIf=1;
				
		}


		if(first_act_cvid<0 || first_act_portmask==0)
		{
			//rtlglue_printf("Error config first_act_cvid=%d  \n",first_act_cvid);
			first_act_cvid=-1;
		}

		if(second_act_cvid<0 || act1_mask==0)
		{
			//rtlglue_printf("Error config second_act_cvid=%d act1_mask=%d \n",second_act_cvid,act1_mask);
			second_act_cvid=-1;
		}




CHANGE_TABLE:
		_rtk_fc_proc_mc_set_set_cfg(flushtable, isIpv6, GIP, (int)sizeof(GIP)/sizeof(GIP[0]), SIP, (int)sizeof(SIP)/sizeof(SIP[0]), groupknown, groupunknown,careSip,careCvid, ingressCtagIf, cvid,careSvid, ingressStagIf, svid,
			first_act_portmask,first_act_cvid,first_act_cpri,act1_mask,second_act_cvid, second_act_cpri);
		

		return count;
	}

PARSE_ERROR:

	rtlglue_printf("ADD GROUP:\n");
	rtlglue_printf("echo GROUP_AS_KNOWN 1  GIP4 224.1.2.3  > proc/fc/ctrl/mc_set \n");	
	rtlglue_printf("DEL GROUP:\n");
	rtlglue_printf("echo GROUP_AS_UNKNOWN 1  GIP4 224.1.2.3 > proc/fc/ctrl/mc_set \n"); 
	rtlglue_printf("ADD GROUP FORWARD PORTS [GIP4][GIP6][PORTMASK] OPTIONS:[SIP4][SIP6][INGRESS_CVID]\n");
	rtlglue_printf("echo GIP4 224.1.2.3 PORTMASK 0x3  > proc/fc/ctrl/mc_set \n"); 
	rtlglue_printf("DEL GROUP FORWARD PORTS:\n");
	rtlglue_printf("echo GIP4 224.1.2.3 PORTMASK 0x0  > proc/fc/ctrl/mc_set \n"); 
	rtlglue_printf("FLUSH ALL TABLE:\n");
	rtlglue_printf("echo FLUSH 1 > proc/fc/ctrl/mc_set \n"); 

	return -EFAULT;
}

int rtk_fc_proc_wan_access_limit_IP_list_reset(struct file *filp, const char *buff,unsigned long len, void *data)
{
	int i=0;
	rtk_fc_wan_access_limit_IP_list_t *pList, *pNext;
	struct list_head *pHead;

	for(i=0;i<RTK_FC_WAN_ACCESS_IP_BUCKET_SIZE;i++)
	{
		pHead=&fc_db.wanAccessLimitIP_head[i];
		if(!list_empty(pHead))
		{
			list_for_each_entry_safe(pList, pNext, pHead, accessIP_list)
			{
				list_del(&pList->accessIP_list);
				RTK_FC_HELPER_FC_KFREE(pList,sizeof(rtk_fc_wan_access_limit_IP_list_t));
				atomic_dec(&fc_db.wanAccessLimit.learningCount);
			}
		}
	}
	rtlglue_printf("done.\n");
	return len;
}

int rtk_fc_proc_lanhost_routing_logging_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%u\n", fc_db.controlFuc.lanhost_routing_logging);

	return SUCCESS;
}

int rtk_fc_proc_lanhost_routing_logging_set(struct file *filp, const char *buff,unsigned long count, void *data)
{
	fc_db.controlFuc.lanhost_routing_logging = (_rtk_fc_proc_parsing_string_to_integer(buff,count)) == 0 ? 0U : 1U;

	return count;
}

#if defined(CONFIG_RTK_SOC_RTL8198D) || defined(CONFIG_RTL8198X_SERIES)
int rtk_fc_proc_skip_acc_port_mask_get(struct seq_file *s, void *v)
{
	int len = 0;
	PROC_PRINTF("skip FC acceleration port mask:");
	PROC_PRINTF("0x%llx\n",fc_db.skipAccPortMask.portmask);

	return len;
}

int rtk_fc_proc_skip_acc_port_mask_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	rtk_fc_port_mask_t portmask = 0;
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int cmdlen = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;

	/* copy data to the buffer */
	strscpy(tmpBuf, buff, cmdlen);

	strptr=tmpBuf;
	split_str=strsep(&strptr," ");

	portmask = simple_strtol(split_str, NULL, 0);
	fc_db.skipAccPortMask.portmask = portmask;

	return len;
}
#endif

#if defined(CONFIG_RTL8198XB_SERIES) || defined(CONFIG_RTL8198E_SERIES)
int rtk_fc_improve_mc_priority(uint8 enable, uint32 uc_rate_limit)
{
	aal_l2_cf_key_t tmp_rule_key;
	aal_l2_cf_rule_t bypass_rule;
	aal_l2_cf_action_mask_t bypass_mask;
	ca_uint32_t l2_cf_entry_id;
	ca_status_t ret;
	ca_uint8_t profileid;
	aal_l2_cls_fib_t cls_fib;
	aal_l2_cls_fib_mask_t cls_mask;
	ca_uint32_t fib_id;

	if ((enable != fc_db.improve_mc_priority) || (uc_rate_limit != fc_db.uc_rate_limit)) {
		memset(&tmp_rule_key, 0, sizeof(aal_l2_cf_key_t));
		memset(&bypass_rule, 0, sizeof(aal_l2_cf_rule_t));
		memset(&bypass_mask, 0, sizeof(aal_l2_cf_action_mask_t));

		/* precheck l2 cf rules */
		l2_cf_entry_id = 17;
		if ((ret = aal_l2_cf_rule_get(G3_DEF_DEVID, l2_cf_entry_id, &bypass_rule)) != CA_E_OK) {
			WARNING("aal_l2_cf_rule_get %d fail, 0x%x", l2_cf_entry_id, ret);
			return 0;
		}

		tmp_rule_key.care_bits.valid = 1;
		tmp_rule_key.data_bits.valid = 1;
		tmp_rule_key.care_bits.ldpid = 0x3f;
		tmp_rule_key.data_bits.ldpid = AAL_LPORT_L3_WAN;

		if (memcmp(&(tmp_rule_key), &(bypass_rule.key), sizeof(aal_l2_cf_key_t))) {
			WARNING("l2 cf key unmatch, please check l2 cf rule index %d!", l2_cf_entry_id);
			return 0;
		}

		/* precheck l2 cls rules */
		if (L2_CLS_TBL_ID(fc_db.l2_cls_mc_forward_l3fe_rule_index[0]) != L2_CLS_TBL_ID(fc_db.l2_cls_mc_forward_l3fe_rule_index[1])) {
			WARNING("unexpected l2 cls index, please check [0] 0x%x [1] 0x%x!", fc_db.l2_cls_mc_forward_l3fe_rule_index[0], fc_db.l2_cls_mc_forward_l3fe_rule_index[1]);
			return 0;
		}

		memset(&cls_fib, 0, sizeof(aal_l2_cls_fib_t));
		fib_id = L2_CLS_TBL_ID(fc_db.l2_cls_mc_forward_l3fe_rule_index[0]);
		if ((ret = aal_l2_cls_fib_get(G3_DEF_DEVID, fib_id, &cls_fib)) != CA_E_OK) {
			WARNING("aal_l2_cls_fib_get %d fail, 0x%x!", fib_id, ret);
			return 0;
		}

		if (enable) {
			/* L2 CF skip cos assign */
			bypass_mask.s.cf_cos_act = 1U;
			bypass_rule.action.cf_cos_act = AAL_L2_CF_ACTION_COS_ACT_NOP;
			if ((ret = aal_l2_cf_action_set(G3_DEF_DEVID, l2_cf_entry_id, bypass_mask, &(bypass_rule.action))) != CA_E_OK) {
				WARNING("aal_l2_cf_action_set %d fail, 0x%x", l2_cf_entry_id, ret);
				return 0;
			}

			/* [TODO] assign mc cos 7 */
			memset(&cls_mask, 0, sizeof(aal_l2_cls_fib_mask_t));
			cls_mask.s.cos_valid_0 = 1U;
			cls_mask.s.cos_valid_1 = 1U;
			cls_mask.s.cos_0 = 1U;
			cls_mask.s.cos_1 = 1U;
			cls_fib.cos_valid_0 = TRUE;
			cls_fib.cos_valid_1 = TRUE;
			cls_fib.cos_0 = 7U;
			cls_fib.cos_1 = 7U;
			if ((ret = aal_l2_cls_fib_set(G3_DEF_DEVID, fib_id, cls_mask, &cls_fib)) != CA_E_OK) {
				WARNING("aal_l2_cls_fib_set %d fail, 0x%x", fib_id, ret);
				return 0;
			}

			/* change L3_WAN voq0 to port0 voq0 profile */
			if ((ret = aal_l2_tm_cb_voq_profile_sel_get(G3_DEF_DEVID, (AAL_PPORT_NI0 << 3), &profileid)) != CA_E_OK) {
				WARNING("aal_l2_tm_cb_voq_profile_sel_get port %d queue 0 fail, 0x%x", AAL_PPORT_NI0, ret);
				return 0;
			}

			if ((ret = aal_l2_tm_cb_voq_profile_sel_set(G3_DEF_DEVID, (AAL_PPORT_L3_WAN << 3), profileid)) != CA_E_OK) {
				WARNING("aal_l2_tm_cb_voq_profile_sel_set port %d queue 0 fail, 0x%x", AAL_PPORT_L3_WAN, ret);
				return 0;
			}

			/* rate limit L3_WAN voq0 */
			if (rtk_rate_egrQueueBwCtrlRate_set(AAL_LPORT_L3_WAN, 0, uc_rate_limit) != RT_ERR_OK) {
				WARNING("rtk_rate_egrQueueBwCtrlRate_set lport %d queue 0 rate %d fail", AAL_LPORT_L3_WAN, uc_rate_limit);
				return 0;
			}
		}
		else {
			/* L2 CF do cos assign */
			bypass_mask.s.cf_cos_act = 1U;
			bypass_rule.action.cf_cos_act = AAL_L2_CF_ACTION_COS_ACT_ASSIGN;
			if ((ret = aal_l2_cf_action_set(G3_DEF_DEVID, l2_cf_entry_id, bypass_mask, &(bypass_rule.action))) != CA_E_OK) {
				WARNING("aal_l2_cf_action_set %d fail, 0x%x", l2_cf_entry_id, ret);
				return 0;
			}

			/* [TODO] skip mc cos assign */
			memset(&cls_mask, 0, sizeof(aal_l2_cls_fib_mask_t));
			cls_mask.s.cos_valid_0 = 1U;
			cls_mask.s.cos_valid_1 = 1U;
			cls_fib.cos_valid_0 = 0U;
			cls_fib.cos_valid_1 = 0U;

			cls_mask.s.cos_valid_0 = 1U;
			cls_mask.s.cos_valid_1 = 1U;
			cls_mask.s.cos_0 = 1U;
			cls_mask.s.cos_1 = 1U;
			cls_fib.cos_valid_0 = FALSE;
			cls_fib.cos_valid_1 = FALSE;
			cls_fib.cos_0 = 0U;
			cls_fib.cos_1 = 0U;

			if ((ret = aal_l2_cls_fib_set(G3_DEF_DEVID, fib_id, cls_mask, &cls_fib)) != CA_E_OK) {
				WARNING("aal_l2_cls_fib_set %d fail, 0x%x", fib_id, ret);
				return 0;
			}

			/* change L3_WAN voq0 to L3_LAN voq7 profile (L3_LAN voq0-voq6 profile may updated by flow control) */
			if ((ret = aal_l2_tm_cb_voq_profile_sel_get(G3_DEF_DEVID, (AAL_PPORT_L3_LAN << 3) + 7, &profileid)) != CA_E_OK) {
				WARNING("aal_l2_tm_cb_voq_profile_sel_get port %d queue 7 fail, 0x%x", AAL_PPORT_L3_LAN, ret);
				return 0;
			}

			if ((ret = aal_l2_tm_cb_voq_profile_sel_set(G3_DEF_DEVID, (AAL_PPORT_L3_WAN << 3), profileid)) != CA_E_OK) {
				WARNING("aal_l2_tm_cb_voq_profile_sel_set port %d queue 0 fail, 0x%x", AAL_PPORT_L3_WAN, ret);
				return 0;
			}

			/* remove rate limit L3_WAN voq0 */
			if (rtk_rate_egrQueueBwCtrlRate_set(AAL_LPORT_L3_WAN, 0, RT_RATE_HW_EGR_RATE_MAX) != RT_ERR_OK) {
				WARNING("rtk_rate_egrQueueBwCtrlRate_set lport %d queue 0 rate %d fail", AAL_LPORT_L3_WAN, RT_RATE_HW_EGR_RATE_MAX);
				return 0;
			}
		}

		fc_db.improve_mc_priority = enable;
		fc_db.uc_rate_limit = uc_rate_limit;
	}

	return 0;
}

int rtk_fc_proc_improve_mc_priority_get(struct seq_file *s, void *v)
{
	if (fc_db.improve_mc_priority) {
		PROC_PRINTF("enabled.\n\n");
		PROC_PRINTF("uc rate limit to %uKbps.\n", fc_db.uc_rate_limit);
		PROC_PRINTF("l2_cls_mc_forward_l3fe_rule[0] TBL/KEY_IDX: %04d / %d\n", L2_CLS_TBL_ID(fc_db.l2_cls_mc_forward_l3fe_rule_index[0]), L2_CLS_KEY_ID(fc_db.l2_cls_mc_forward_l3fe_rule_index[0]));
		PROC_PRINTF("l2_cls_mc_forward_l3fe_rule[1] TBL/KEY_IDX: %04d / %d\n", L2_CLS_TBL_ID(fc_db.l2_cls_mc_forward_l3fe_rule_index[1]), L2_CLS_KEY_ID(fc_db.l2_cls_mc_forward_l3fe_rule_index[1]));
	}
	else {
		PROC_PRINTF("disabled.\n");
	}

	return 0;
}

int rtk_fc_proc_improve_mc_priority_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int cmdlen = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;
	uint8 enable;
	uint32 rate = 0U;

	/* copy data to the buffer */
	strscpy(tmpBuf, buff, cmdlen);

	strptr = tmpBuf;
	if ((split_str = strsep(&strptr," ")) == NULL) {
		goto show_usage;
	}

	if (memcmp(split_str, "?", 1) == 0) {
		goto show_usage;
	}
	else if (memcmp(split_str, "0", 1) == 0) {
		enable = 0U;
	}
	else if (memcmp(split_str, "1", 1) == 0) {
		if ((split_str = strsep(&strptr," ")) == NULL) {
			goto show_usage;
		}

		rate = simple_strtol(split_str, NULL, 0);
		if (rate > RT_RATE_HW_EGR_RATE_MAX) {
			goto show_usage;
		}
		enable = 1U;
	}
	else {
		goto show_usage;
	}

	rtk_fc_improve_mc_priority(enable, rate);

	return cmdlen;

show_usage:
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("    echo ?  > /proc/fc/... for help.\n");
	rtlglue_printf("    echo 1 [uc_rate_limit] > /proc/fc/... to enable shaper, rate should be 0-%dkbps.\n", RT_RATE_HW_EGR_RATE_MAX);
	rtlglue_printf("    echo 0  > /proc/fc/... to disable shaper.\n");

	return cmdlen;
}
#endif

#if defined(CONFIG_FC_RTL9607F_SERIES)
int rtk_fc_proc_dmalso_backpressure_rule_get(struct seq_file *s, void *v)
{
	rtk_asic_dmalso_backpressure_rule_show();

	return 0;
}

int rtk_fc_proc_dmalso_backpressure_rule_set(struct file *filp, const char *buff,unsigned long len, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int cmdlen = (len >= CMD_BUFF_SIZE) ? CMD_BUFF_SIZE : len;
	char *strptr,*split_str;
	uint8 del, rule_idx, i = 0U;
	rtk_asic_dmalso_bp_conf_t vp_bp_conf;

	/* copy data to the buffer */
	strscpy(tmpBuf, buff, cmdlen);

	strptr = tmpBuf;
	if ((split_str = strsep(&strptr," ")) == NULL)
		goto show_usage;

	if (!memcmp(split_str, "add", 3))
		del = FALSE;
	else if (!memcmp(split_str, "del", 3))
		del = TRUE;
	else
		goto show_usage;

	if ((split_str = strsep(&strptr," ")) == NULL)
		goto show_usage;

	rule_idx = (uint8)simple_strtol(split_str, NULL, 0);
	if (rule_idx >= ASIC_DMALSO_BP_RULE_END)
		goto show_usage;

	if (del) {
		if (rtk_asic_dmalso_backpressure_rule_del(rule_idx))
			rtlglue_printf("rtk_asic_dmalso_backpressure_rule_del failure!\n");

		return cmdlen;
	}

	memset(&vp_bp_conf, 0, sizeof(vp_bp_conf));
	vp_bp_conf.rule_idx = rule_idx;

	if ((split_str = strsep(&strptr," ")) == NULL)
		goto show_usage;

	if (memcmp(split_str, "threshold_hi", 12))
		goto show_usage;

	if ((split_str = strsep(&strptr," ")) == NULL)
		goto show_usage;

	vp_bp_conf.threshold_hi = (uint16)simple_strtol(split_str, NULL, 0);

	if ((split_str = strsep(&strptr," ")) == NULL)
		goto show_usage;

	if (memcmp(split_str, "threshold_lo", 12))
		goto show_usage;

	if ((split_str = strsep(&strptr," ")) == NULL)
		goto show_usage;

	vp_bp_conf.threshold_lo = (uint16)simple_strtol(split_str, NULL, 0);

	if ((split_str = strsep(&strptr," ")) == NULL)
		goto show_usage;

	while (1) {
		if (memcmp(split_str, "lspid", 5))
			goto show_usage;

		if ((split_str = strsep(&strptr," ")) == NULL)
			goto show_usage;

		vp_bp_conf.lspid_vld[i] = TRUE;
		vp_bp_conf.lspid[i] = (uint8)simple_strtol(split_str, NULL, 0);

		if ((split_str = strsep(&strptr," ")) == NULL)
			goto show_usage;

		if (memcmp(split_str, "vp", 2))
			goto show_usage;

		if ((split_str = strsep(&strptr," ")) == NULL)
			goto show_usage;

		vp_bp_conf.vpId_vld[i] = TRUE;
		vp_bp_conf.vpId[i] = (uint8)simple_strtol(split_str, NULL, 0);
		if (vp_bp_conf.vpId[i] >= DMA_SEC_DMA_LSO_VP_COUNT)
			goto show_usage;

		if ((split_str = strsep(&strptr," ")) == NULL)
			break;

		if (i++ == ASIC_DMALSO_BP_SPID_MAX)
			break;
	}

	if (rtk_asic_dmalso_backpressure_rule_add(&vp_bp_conf))
		rtlglue_printf("rtk_asic_dmalso_backpressure_rule_add failure!\n");

	return cmdlen;

show_usage:
	rtlglue_printf("\nUsage:\n");
	rtlglue_printf("    echo del [rule_idx] > /proc/fc/...\n");
	rtlglue_printf("    echo add [rule_idx] threshold_hi [hi] threshold_lo [lo] lspid [lspid_1] vp [vp_1] lspid [lspid_2] vp [vp_2] ... > /proc/fc/...\n");
	rtlglue_printf("        [rule_idx] 1-7; [hi][lo] 0-0xffff; [lspid_x] 16-63; [vp_x] 0-11; [x] 1-8;\n");

	return cmdlen;
}
#endif

void rtk_fc_proc_init(void)
{
	int i = 0;
	struct proc_dir_entry *p=NULL;

	/* create proc directories  */
	if(rtk_fc_proc_dir==NULL){
		rtk_fc_proc_dir = RTK_FC_HELPER_PS_PROC_MKDIR("fc", NULL);
	}

	if(rtk_rg_hw_dump_proc_dir==NULL){
		rtk_rg_hw_dump_proc_dir = RTK_FC_HELPER_PS_PROC_MKDIR("hw_dump", rtk_fc_proc_dir);
	}

	if(rtk_rg_sw_dump_proc_dir==NULL){
		rtk_rg_sw_dump_proc_dir = RTK_FC_HELPER_PS_PROC_MKDIR("sw_dump", rtk_fc_proc_dir);
	}

	if(rtk_rg_ctrl_proc_dir==NULL){
		rtk_rg_ctrl_proc_dir = RTK_FC_HELPER_PS_PROC_MKDIR("ctrl", rtk_fc_proc_dir);
	}

	if(rtk_rg_trace_proc_dir==NULL){
		rtk_rg_trace_proc_dir = RTK_FC_HELPER_PS_PROC_MKDIR("trace", rtk_fc_proc_dir);
	}

	for( i=0; i< (sizeof(fcProc)/sizeof(rtk_fc_proc_t)) ;i++)
	{
		struct proc_dir_entry *tmpDir=NULL;

		fcProc[i].pProc_fops = RTK_FC_HELPER_MGR_PROC_FOPS_KMALLOC();
		
		if(fcProc[i].get==NULL)
			RTK_FC_HELPER_MGR_PROC_FOPS_OPEN_SET(fcProc[i].pProc_fops, rtk_fc_nullDebugSingleOpen);
		else
			RTK_FC_HELPER_MGR_PROC_FOPS_OPEN_SET(fcProc[i].pProc_fops, rtk_fc_commonDebugSingleOpen);

		if(fcProc[i].set==NULL)
			RTK_FC_HELPER_MGR_PROC_FOPS_WRITE_SET(fcProc[i].pProc_fops, NULL);
		else
			RTK_FC_HELPER_MGR_PROC_FOPS_WRITE_SET(fcProc[i].pProc_fops, rtk_fc_commonDebugSingleWrite);

		RTK_FC_HELPER_MGR_PROC_FOPS_READ_SET(fcProc[i].pProc_fops, seq_read);
		RTK_FC_HELPER_MGR_PROC_FOPS_LSEEK_SET(fcProc[i].pProc_fops, seq_lseek);
		RTK_FC_HELPER_MGR_PROC_FOPS_RELEASE_SET(fcProc[i].pProc_fops, single_release);

		switch(fcProc[i].dir)
		{
			case PROC_DIR_SW_DUMP:
				tmpDir=rtk_rg_sw_dump_proc_dir;
				break;
			case PROC_DIR_HW_DUMP:
				tmpDir=rtk_rg_hw_dump_proc_dir;
				break;
			case PROC_DIR_TRACE:
				tmpDir=rtk_rg_trace_proc_dir;
				break;
			case PROC_DIR_CTRL:
				tmpDir=rtk_rg_ctrl_proc_dir;
				break;
			default:
				break;
		}

		p = RTK_FC_HELPER_MGR_PROC_CREATE_DATA(fcProc[i].name, S_IRUGO, tmpDir, fcProc[i].pProc_fops, NULL);
		if(!p){
			printk("create proc %s failed!\n",fcProc[i].name);
		}
		else{
			fcProc[i].inode_id = RTK_FC_HELPER_MGR_PROC_INODE_ID_GET(p);
		}
	}
}
void rtk_fc_proc_exit(void)
{
	int i = 0;

	for( i=0; i< (sizeof(fcProc)/sizeof(rtk_fc_proc_t)) ;i++)
	{
		struct proc_dir_entry *tmpDir=NULL;

		if(fcProc[i].pProc_fops)
		{
			RTK_FC_HELPER_MGR_PROC_FOPS_KFREE(fcProc[i].pProc_fops);
			fcProc[i].pProc_fops = NULL;
		}
		switch(fcProc[i].dir)
		{
			case PROC_DIR_SW_DUMP:
				tmpDir=rtk_rg_sw_dump_proc_dir;
				break;
			case PROC_DIR_HW_DUMP:
				tmpDir=rtk_rg_hw_dump_proc_dir;
				break;
			case PROC_DIR_TRACE:
				tmpDir=rtk_rg_trace_proc_dir;
				break;
			case PROC_DIR_CTRL:
				tmpDir=rtk_rg_ctrl_proc_dir;
				break;
			default:
				break;
		}

		remove_proc_entry(fcProc[i].name, tmpDir);
	}

	proc_remove(rtk_rg_hw_dump_proc_dir);
	proc_remove(rtk_rg_sw_dump_proc_dir);
	proc_remove(rtk_rg_ctrl_proc_dir);
	proc_remove(rtk_fc_proc_dir);
}

