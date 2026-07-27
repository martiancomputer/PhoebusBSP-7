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

#include <linux/proc_fs.h>
#include <asm/stacktrace.h> //for dump_stack()

#include "rtk_fc_struct.h"
#include "rtk_fc_define.h"
#include "rtk_fc_debug.h"
#include "rtk_fc_internal.h"
#include "rtk_fc_proc.h"
#include "rtk_fc_acl.h"
#include "rtk_fc_mappingAPI.h"
#include "rtk_fc_external.h"


#include <linux/sched.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
#include <linux/sched/signal.h>
#endif
#include <linux/tty.h>


#include <rtk/l2.h>
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
#include <rtk/switch.h>
#include <rtk/acl.h>
#include <rtk/classify.h>
#include <rtk/l34.h>
#include <rtk/qos.h>
#include <rtk/svlan.h>
#include <rtk/debug.h>
#include <ioal/mem32.h>

#include "rtk_fc_callback.h"

#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
#include "aal_l2_vlan.h"
#include "aal_l3fe.h"
#include "aal_l2_cls.h"
#include "aal_l3_cls.h"
#include "aal_l3_cam.h"
#include "classifier.h"
#include <aal_l3_pe.h>
#include <aal_l2_te.h>
#include <aal_l2_qm.h>
#include <aal_l2_tm_cb.h>
#include <aal_l3_te.h>
#include <aal_l3_te_cb.h>

#include <rtk/rt/rt_ponmisc.h>
#include <ca_aal_proc.h>

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#include "aal_l2_classification.h"
#endif
#endif




#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
char *name_of_rg_netifPPPoEAct[]={
	"Keep",
	"Add",
	"Modify",
	"Remove",
};
#endif


#if defined(CONFIG_RTK_L34_G3_PLATFORM)

int tracefilterRULE0_headri_period_time=0;
int initTimer=0;
int latchMode_lastState = 0;
rtk_fc_timer_list_t *dumpheaderI_timer = NULL;

#define L4PROTO_NUM_TO_STR(l4proto_num)	(l4proto_num==IPPROTO_TCP)?"TCP":((l4proto_num==IPPROTO_UDP)?"UDP":((l4proto_num==IPPROTO_UDPLITE)?"UDP-lite":((l4proto_num==0x1B)?"RDP":"Other")))
#endif

char *name_of_rg_remarkingBits[]={ //mappint to rtk_fc_acl_cvlan_tagif_decision_t
	"qid",
	"streamid",
	"streamId en",
	"swShaper_en",
	"meterIdx",
	"meterIdx en",
	"fwd by PS",
	"mibIdx",
	"mibIdx en",
	"mib2Idx",
	"mib2Idx en",
	"skipFcFunction",
	"cvlan id",
	"cvlan pri",
	"cvlan action",
	"svlan id",
	"svlan pri",
	"svlan action",
	"svlan tpid",
	"wanDsLoopback_en",
	"swIntfIdx_assign_en",
	"swIntfIdx_assign_inOut",
	"swIntfIdx_assign_idx",
	"skip_fc_alg_check",
	"flow_cache_mib_en",
	"psFloodFwdAcc",
	"mapet_fmr",
	"forceIntpriToWifiPri",
	"pe_aware",
	"END",
};

char *name_of_dualHdrType[]={
	"RTK_FC_DUALTYPE_NONE",
	"RTK_FC_DUALTYPE_IP4INIP6",
	"RTK_FC_DUALTYPE_PPTP",
	"RTK_FC_DUALTYPE_L2TP",
	"RTK_FC_DUALTYPE_IPSEC",
	"RTK_FC_DUALTYPE_GRE_ETH_BR",
	"RTK_FC_DUALTYPE_VXLAN",
	"RTK_FC_DUALTYPE_6RD",
	"RTK_FC_DUALTYPE_MAPT",
	"RTK_FC_DUALTYPE_XLAT464",
	"RTK_FC_DUALTYPE_SRV6",
	"RTK_FC_DUALTYPE_END",
	
};

char *name_of_drop_type[]={ //mappint to rtk_fc_acl_cvlan_tagif_decision_t
	"DROP",
	"DROP_PORT_MOVING",
	"DROP_PORT_LRN_LIMIT",
	"DROP_GROUP_LRN_LIMIT",
	"DROP_VLAN_GROUP_LEARNING_LIMIT",
	"DROP_WIFI_LRN_LIMIT",
	"DROP_WIFI_BLOCK_RELAY",
	"DROP_WAN_ACC_LIMIT",
	"DROP_SHAPING_LIMIT",
	"DROP_CALLBACK_FUNC",
	"DROP_SW_RATE_LIMIT",
	"DROP_DUMMY",
	"DROP_INVLD_STREAMID",
	"DROP_NO_MEMORY",
	"DROP_MTU_FAIL_NO_SW_ID_IFNO",
	"DROP_IPSEC_SC_FIFO_FULL",
	"DROP_IPSEC_SC_NAT_T_HDRERR",
	"DROP_IPSEC_SC_IPI_FULL",
	"DROP_IPSEC_SLOW_IPI_FULL",
	"DROP_IPSEC_SLOW_IPI_DMAALLOC_FAIL",
	"DROP_NIC_TX_BUSY",
	"DROP_END",
};


char *name_of_flow_proto_ctrl[]={ // mapping to rtk_fc_flow_proto_ctrl_t
	"NONE",
	"L3_DUAL_Passthrough",
	"NAPTv6_NAT66",
	"NPTV6_SpecFF",
	"VXLAN_ADD",
	"VXLAN_REMOVE",
	"LOOPBACK",
	"ICMP",
	"MAPE",
	"MAPT",
	"L2_DUAL",
	"L2_DUAL_EXTRA",
	"IPSEC",
};

void _rtk_fc_dump_stack(void)
{
#if 0//defined(CONFIG_MIPS) && IS_BUILTIN(CONFIG_RTK_L34_FC_KERNEL_MODULE)
	struct pt_regs regs;
	unsigned long sp,ra,pc;
//	prepare_frametrace(&regs);

	if(!(fc_db.debugLevel&FC_DEBUG_LEVEL_WARN))
	{
		return;
	}

    memset(&regs, 0, sizeof(regs));

	__asm__ __volatile__(
	".set push\n\t"
	".set noat\n\t"
	"1: la $1, 1b\n\t"
	"sw $1, %0\n\t"
	"sw $29, %1\n\t"
	"sw $31, %2\n\t"
	".set pop\n\t"
	: "=m" (regs.cp0_epc),
	"=m" (regs.regs[29]), "=m" (regs.regs[31])
	: : "memory");


	sp = regs.regs[29];
	ra = regs.regs[31];
	pc = regs.cp0_epc;

	if (!__kernel_text_address(pc))
	{
		return;
	}

	rtlglue_printf("\033[1;33;41m");
	pc = unwind_stack(current, &sp, pc, &ra);
	while(1)
	{
		if(!pc) break;
		pc = unwind_stack(current, &sp, pc, &ra);
		if(!pc) break;
		rtlglue_printf("[%p][%pS]\n", (void *)pc, (void *)pc);
		//printk("[%p:%pS]\n", (void *) pc, (void *) pc);
	}
	rtlglue_printf("\033[0m\n");
#else
	dump_stack();
#endif
}


void _rtk_fc_dump_mem(unsigned char *ptitle, unsigned char *pbuf, int len)
{
	char tmpbuf[100];
	int i, n = 0;

	if (ptitle)
		snprintf(tmpbuf, sizeof(tmpbuf), "%s", ptitle);
	else
		tmpbuf[0] = '\0';

	for (i = 0; i < len; ++i ) {
		if (!(i & 0x0f)) {
			printk("%s\n", tmpbuf);
			n = sprintf(tmpbuf, "%03X:\t", i);
		}
		n += snprintf((tmpbuf+n), sizeof(tmpbuf)-n, " %02X", pbuf[i]);
	}
	printk("%s\n", tmpbuf);
}


void dump_packet(unsigned char *pkt,unsigned int size,char *memo)
{
	int off;
	unsigned char protocol=0U;
	int i;
	int pppoeif=0;
	unsigned char splitLine[80]={0};
	memset(splitLine,'=',79);
	rtlglue_printf("%s\n",splitLine);

	if(size==0U)
	{
		rtlglue_printf("%s\npacket_length=0\n",memo);
		return;
	}

	rtlglue_printf("%s - len %d\n",memo, size);
	//if(size>96) size = 96;						// print 16 bytes * 6 for header reference
	rtlglue_print_hex_dump("", DUMP_PREFIX_ADDRESS, 16, 1, pkt, size, 1);
	rtlglue_printf("\n" COLOR_Y "DA" COLOR_NM ":[%02X-%02X-%02X-%02X-%02X-%02X]\t" COLOR_Y "SA" COLOR_NM ":[%02X-%02X-%02X-%02X-%02X-%02X]\n",pkt[0],pkt[1],pkt[2],pkt[3],pkt[4],pkt[5]
		,pkt[6],pkt[7],pkt[8],pkt[9],pkt[10],pkt[11]);
	off=12;
	if((pkt[off]==0x88)&&(pkt[off+1]==0x99))
	{

		if(((pkt[off+8]==0x88)&&(pkt[off+9]==0xa8))||((pkt[off+8]==0x81)&&(pkt[off+9]==0x00))||((pkt[off+8]==0x88)&&((pkt[off+9]==0x63)||(pkt[off+9]==0x64)))||
		((pkt[off+8]==0x86)&&(pkt[off+9]==0xdd))||(pkt[off+1]==0x00))
		{
			//TO CPU
			rtlglue_printf("CPU:[" COLOR_Y "Protocol" COLOR_NM "=%d][" COLOR_Y "Res" COLOR_NM "=0x%x][" COLOR_Y "Pri" COLOR_NM "=%d][" COLOR_Y "TTL_1" COLOR_NM "=0x%x][" COLOR_Y "L3R" COLOR_NM "=%d][" COLOR_Y "ORG" COLOR_NM "=%d][" COLOR_Y "SPA" COLOR_NM "=%d][" COLOR_Y "EPMSK" COLOR_NM "=0x%x]\n"
				,pkt[off+2],pkt[off+3],pkt[off+4]>>5,pkt[off+4]&0x1f
				,pkt[off+5]>>7,(pkt[off+5]>>6)&1,pkt[off+5]&7,pkt[off+7]&0x3f);
			off+=8;
		}
		else
		{
			//FROM CPU
			rtlglue_printf("CPU:[" COLOR_Y "Proto" COLOR_NM "=%d][" COLOR_Y "L3CS" COLOR_NM "=%d][" COLOR_Y "L4CS" COLOR_NM "=%d][" COLOR_Y "TxPortMask" COLOR_NM "=0x%x][" COLOR_Y "EFID_EN" COLOR_NM "=%d][" COLOR_Y "EFID" COLOR_NM "=%d][" COLOR_Y "Priority" COLOR_NM "=%d]\n"
				,pkt[off+2],(pkt[off+3]>>7)&1,(pkt[off+3]>>6)&1,pkt[off+3]&0x3f,pkt[off+4]>>5,(pkt[off+4]>>3)&3,pkt[off+4]&7);
			rtlglue_printf("    [" COLOR_Y "Keep" COLOR_NM "=%d][" COLOR_Y "VSEL" COLOR_NM "=%d][" COLOR_Y "DisLrn" COLOR_NM "=%d][" COLOR_Y "PSEL" COLOR_NM "=%d][" COLOR_Y "Rsv1" COLOR_NM "=%d][" COLOR_Y "Rsv0" COLOR_NM "=%d][" COLOR_Y "L34Keep" COLOR_NM "=%d][" COLOR_Y "QSEL" COLOR_NM "=%d]\n"
				,pkt[off+5]>>7,(pkt[off+5]>>6)&1,(pkt[off+5]>>5)&1,(pkt[off+5]>>4)&1,(pkt[off+5]>>3)&1,(pkt[off+5]>>2)&1,(pkt[off+5]>>1)&1,pkt[off+5]&1);
			rtlglue_printf("    [" COLOR_Y "ExtSPA" COLOR_NM "=%d][" COLOR_Y "PPPoEAct" COLOR_NM "=%d][" COLOR_Y "PPPoEIdx" COLOR_NM "=%d][" COLOR_Y "L2BR" COLOR_NM "=%d][" COLOR_Y "QID" COLOR_NM "=%d]\n"
				,(pkt[off+6]>>5)&7,(pkt[off+6]>>3)&3,pkt[off+6]&7,(pkt[off+7]>>7)&1,pkt[off+6]&0x7f);
			off+=12;
		}
	}

	if( ((pkt[off]==0x88)&&(pkt[off+1]==0xa8)) || ((pkt[off]==0x88)&&(pkt[off+1]==0x88)) )
	{
		rtlglue_printf("SVLAN:[" COLOR_Y "Pri" COLOR_NM "=%d][" COLOR_Y "DEI" COLOR_NM "=%d][" COLOR_Y "VID" COLOR_NM "=%d]\n",pkt[off+2]>>5,(pkt[off+2]>>4)&1,((pkt[off+2]&0xf)<<8)|(pkt[off+3]));
		off+=4;
	}

	if((pkt[off]==0x81)&&(pkt[off+1]==0x00))
	{
		rtlglue_printf("CVLAN:[" COLOR_Y "Pri" COLOR_NM "=%d][" COLOR_Y "CFI" COLOR_NM "=%d][" COLOR_Y "VID" COLOR_NM "=%d]\n",pkt[off+2]>>5,(pkt[off+2]>>4)&1,((pkt[off+2]&0xf)<<8)|(pkt[off+3]));
		off+=4;
	}

	if((pkt[off]==0x88)&&((pkt[off+1]==0x63)||(pkt[off+1]==0x64))) //PPPoE
	{
		rtlglue_printf("PPPoE:[" COLOR_Y "Code" COLOR_NM "=0x%02x][" COLOR_Y "SessionID" COLOR_NM "=0x%04x]\n",
			pkt[off+3],((unsigned int)pkt[off+4]<<8)|pkt[off+5]);
		off+=8;
		pppoeif=1;
	}

	if(((pkt[off]==0x86)&&(pkt[off+1]==0xdd)) || ((pkt[off]==0x00)&&(pkt[off+1]==0x57)))		//IPv6 or IPv6 with PPPoE
	{
		if(pkt[off+2]>>4 !=6)
			return;
		rtlglue_printf("IPv6:[" COLOR_Y "Ver" COLOR_NM "=%d][" COLOR_Y "TC" COLOR_NM "=%02x][" COLOR_Y "FL" COLOR_NM "=%02x%02x%x][" COLOR_Y "Len" COLOR_NM "=%d][" COLOR_Y "NxHdr" COLOR_NM "=%d][" COLOR_Y "HopLimit" COLOR_NM "=%d]\n"
			,pkt[off+2]>>4, (pkt[off+2]&0xf)+(pkt[off+3]>>4), (pkt[off+3]&0xf)+(pkt[off+4]>>4), (pkt[off+4]&0xf)+(pkt[off+5]>>4), (pkt[off+5]&0xf), (pkt[off+6]<<8)+pkt[off+7], pkt[off+8], pkt[off+9]);
		
		rtlglue_printf("     [" COLOR_Y "SIP" COLOR_NM "=%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]\n"
			,pkt[off+10], pkt[off+11], pkt[off+12], pkt[off+13], pkt[off+14], pkt[off+15], pkt[off+16], pkt[off+17]
			,pkt[off+18], pkt[off+19], pkt[off+20], pkt[off+21], pkt[off+22], pkt[off+23], pkt[off+24], pkt[off+25]);
		
		rtlglue_printf("     [" COLOR_Y "DIP" COLOR_NM "=%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]\n"
			,pkt[off+26], pkt[off+27], pkt[off+28], pkt[off+29], pkt[off+30], pkt[off+31], pkt[off+32], pkt[off+33]
			,pkt[off+34], pkt[off+35], pkt[off+36], pkt[off+37], pkt[off+38], pkt[off+39], pkt[off+40], pkt[off+41]);

		protocol=pkt[off+8];
		if(protocol==0U && size > 64U)	//hop-by-hop
		{
			rtlglue_printf("Hop-By-Hop:[" COLOR_Y "NxHdr" COLOR_NM "=%d][" COLOR_Y "Length" COLOR_NM "=%d]\n"
				,pkt[off+42], pkt[off+43]);
			rtlglue_printf("          [" COLOR_Y "Option" COLOR_NM "=%02x %02x %02x %02x %02x %02x]\n"
				,pkt[off+44], pkt[off+45], pkt[off+46], pkt[off+47], pkt[off+48], pkt[off+49]);
			for(i=0; i<pkt[off+43]; i++)
			{
				rtlglue_printf("         [" COLOR_Y "Option" COLOR_NM "=%02x %02x %02x %02x %02x %02x %02x %02x]\n"
					,pkt[off+50+i*8], pkt[off+51+i*8], pkt[off+52+i*8], pkt[off+53+i*8]
					,pkt[off+54+i*8], pkt[off+55+i*8], pkt[off+56+i*8], pkt[off+57+i*8]);
			}

			protocol=pkt[off+42];
			off+=(50+pkt[off+43]*8);
		}
		else
			off+=42;
	}

	if(((pkt[off]==0x08)&&(pkt[off+1]==0x00))||((pkt[off]==0x00)&&(pkt[off+1]==0x21)))
	{
		if(pkt[off+2]>>4 !=4)
			return;
		rtlglue_printf("IPv4:[" COLOR_Y "Ver" COLOR_NM "=%d][" COLOR_Y "HLen" COLOR_NM "=%d][" COLOR_Y "TOS" COLOR_NM "=%d(DSCP=%d)][" COLOR_Y "Len" COLOR_NM "=%d][" COLOR_Y "ID" COLOR_NM "=%d][" COLOR_Y "R" COLOR_NM "=%d," COLOR_Y "DF" COLOR_NM "=%d," COLOR_Y "MF" COLOR_NM "=%d]\n"
			,pkt[off+2]>>4,(pkt[off+2]&0xf)*4,pkt[off+3],pkt[off+3]>>2,(pkt[off+4]<<8)|pkt[off+5],(pkt[off+6]<<8)|pkt[off+7]
			,(pkt[off+8]>>7)&1,(pkt[off+8]>>6)&1,(pkt[off+8]>>5)&1);
		rtlglue_printf("     [" COLOR_Y "FrgOff" COLOR_NM "=%d][" COLOR_Y "TTL" COLOR_NM "=%d][" COLOR_Y "PROTO" COLOR_NM "=%d][" COLOR_Y "CHM" COLOR_NM "=0x%x]\n"
			,((pkt[off+8]&0x1f)<<8)|pkt[off+9],pkt[off+10],pkt[off+11],(pkt[off+12]<<8)|pkt[off+13]);
		rtlglue_printf("     [" COLOR_Y "SIP" COLOR_NM "=%d.%d.%d.%d][" COLOR_Y "DIP" COLOR_NM "=%d.%d.%d.%d]\n"
			,pkt[off+14],pkt[off+15],pkt[off+16],pkt[off+17],pkt[off+18],pkt[off+19],pkt[off+20],pkt[off+21]);

		protocol=pkt[off+11];
		off+=(pkt[off+2]&0xf)*4+2;
	}

	if(protocol==0x6U) //TCP
	{
		rtlglue_printf("TCP:[" COLOR_Y "SPort" COLOR_NM "=%d][" COLOR_Y "DPort" COLOR_NM "=%d][" COLOR_Y "Seq" COLOR_NM "=0x%x][" COLOR_Y "Ack" COLOR_NM "=0x%x][" COLOR_Y "HLen" COLOR_NM "=%d]\n"
			,(pkt[off]<<8)|(pkt[off+1]),(pkt[off+2]<<8)|(pkt[off+3]),(pkt[off+4]<<24)|(pkt[off+5]<<16)|(pkt[off+6]<<8)|(pkt[off+7]<<0)
			,(pkt[off+8]<<24)|(pkt[off+9]<<16)|(pkt[off+10]<<8)|(pkt[off+11]<<0),pkt[off+12]>>4<<2);
		rtlglue_printf("    [" COLOR_Y "URG" COLOR_NM "=%d][" COLOR_Y "ACK" COLOR_NM "=%d][" COLOR_Y "PSH" COLOR_NM "=%d][" COLOR_Y "RST" COLOR_NM "=%d][" COLOR_Y "SYN" COLOR_NM "=%d][" COLOR_Y "FIN" COLOR_NM "=%d][" COLOR_Y "Win" COLOR_NM "=%d]\n"
			,(pkt[off+13]>>5)&1,(pkt[off+13]>>4)&1,(pkt[off+13]>>3)&1,(pkt[off+13]>>2)&1,(pkt[off+13]>>1)&1,(pkt[off+13]>>0)&1
			,(pkt[off+14]<<8)|pkt[off+15]);
		rtlglue_printf("    [" COLOR_Y "CHM" COLOR_NM "=0x%x][" COLOR_Y "Urg" COLOR_NM "=0x%x]\n",(pkt[off+16]<<8)|(pkt[off+17]<<0),(pkt[off+18]<<8)|(pkt[off+19]<<0));
	}
	else if(protocol==0x11U) //UDP
	{
		rtlglue_printf("UDP:[" COLOR_Y "SPort" COLOR_NM "=%d][" COLOR_Y "DPort" COLOR_NM "=%d][" COLOR_Y "Len" COLOR_NM "=%d][" COLOR_Y "CHM" COLOR_NM "=0x%x]\n",(pkt[off]<<8)|(pkt[off+1]),(pkt[off+2]<<8)|(pkt[off+3])
			,(pkt[off+4]<<8)|(pkt[off+5]),(pkt[off+6]<<8)|(pkt[off+7]));

	}
}

void _rtk_fc_rtlglue_printf(char* fmt, ...)
{
	va_list aList;
	char tty_display_char_temp[512];

	va_start(aList, fmt);
	vsprintf(tty_display_char_temp, fmt, aList);
	va_end (aList);

	if(fc_db.controlFuc.debug_message_display_to_tty == TRUE)
	{
		int len;
		struct tty_struct *display_tty = NULL;
		struct task_struct *task_temp = NULL;
		struct pid *pPid = NULL;
		/*The used /bin/sh process doesn't exist anymore or the tty structure of the used /bin/sh process is NULL*/
		RTK_FC_HOOK_FIND_VPID(&pPid, fc_db.controlFuc.tty_display_cur_sh_process_pid);
		RTK_FC_HOOK_PID_TASK(&task_temp, pPid, PIDTYPE_PID);
		if((task_temp != NULL) && (strncmp(RTK_FC_HOOK_TASK_COMM_GET(task_temp), "sh", 2) == 0))
		{
			if((fc_db.controlFuc.tty_display_cur_sh_process != NULL) && (RTK_FC_HOOK_TASK_SIGNAL_GET(fc_db.controlFuc.tty_display_cur_sh_process) != NULL))
				display_tty = RTK_FC_HOOK_TASK_SIGNAL_TTY_GET(fc_db.controlFuc.tty_display_cur_sh_process);

			if(display_tty != NULL)
			{
				//remove the '\n' character  at the end of the string
				len = strlen(tty_display_char_temp);
				if(len >= 512)
				{
					tty_display_char_temp[511]= '\0';
				}
				else
				{
					if(tty_display_char_temp[len-1] == '\n')
						tty_display_char_temp[len-1]=' ';
				}

				//wite to tty
				(display_tty->driver->ops->write)(
				display_tty,	  // The tty itself
				tty_display_char_temp,				 // String
				strlen(tty_display_char_temp)); 	 // Length

				//replace '\n'
				(display_tty->driver->ops->write)(display_tty, "\015\012", 2);
				return;
			}
		}
		printk("\033[1;33;40m[DEBUG] Unable to print debug message to current tty, turn off proc/fc/trace/debug_message_display_to_current_tty automatically!\033[1;30;40m @ %s(%d)\033[0m\n",__FUNCTION__,__LINE__); //Can not use DEBUG here (because it call _rtk_fc_rtlglue_printf)
		fc_db.controlFuc.tty_display_cur_sh_process = NULL;
		fc_db.controlFuc.debug_message_display_to_tty = FALSE;
	}
	//use printk to print debug message
	printk(tty_display_char_temp);

	return;
}

void _rtk_fc_tracelog_printf(int bitmask,int color, int bgcolor, char *string, const char *func, int line, char *dbgtmp, ...)
{
#define TRACELOG_LEN	512
	int mt_trace_i;
	va_list aList;
	char char_temp[TRACELOG_LEN]={0};

	va_start(aList, dbgtmp);
	vsprintf(char_temp, dbgtmp, aList);
	va_end (aList);

	
	for(mt_trace_i=1;mt_trace_i<TRACELOG_LEN;mt_trace_i++) 
	{ 
		if(char_temp[mt_trace_i]==0)
		{
			if(char_temp[mt_trace_i-1]=='\n') 
				char_temp[mt_trace_i-1]=' ';
			else 
				break;
		}
	}
	if(bitmask == FC_DEBUG_LEVEL_WARN)
	{
		if(printk_ratelimit())
			rtlglue_printf("\033[1;%d;%dm[%s] %s \033[1;30;40m@%s(%d)\033[0m\n",color,bgcolor,string,char_temp,func,line);
	}
	else
	{
		rtlglue_printf("\033[1;%d;%dm[%s] %s \033[1;30;40m@%s(%d)\033[0m\n",color,bgcolor,string,char_temp,func,line);
	}
}


#define MAX_PROC_PRINT_SIZE	1024
char proc_print_buf[MAX_PROC_PRINT_SIZE];
char *proc_printf(struct seq_file *s, char *fmt, ...)
{
    int n;
    int size = MAX_PROC_PRINT_SIZE;     /* Guess we need no more than 512 bytes */
    va_list ap;

    while (1) {
        va_start(ap, fmt);
        n = vsnprintf(proc_print_buf, size, fmt, ap);

		if((s==NULL)/*||(rg_db.systemGlobal.proc_to_pipe==0)*/)
			{rtlglue_printf("%s",proc_print_buf);}
		else
			{seq_puts(s,proc_print_buf);}

        va_end(ap);

		if (n < 0)
		    return NULL;

		if (n < size)
		    return proc_print_buf;

		size = n + 1;

    }
	return NULL;
}

int32 dump_lut_display(struct seq_file *s, void *v, uint32 idx, rtk_l2_addr_table_t *data)
{
	PROC_PRINTF("--------------- LUT TABLE (%d)----------------\n",idx);

        if(data->entryType==RTK_LUT_L2UC)
        {

			PROC_PRINTF("[P1] mac=%02x:%02x:%02x:%02x:%02x:%02x cvid=%d ctagif=%d l3lookup=%d ivl=%d\n"
	                           ,data->entry.l2UcEntry.mac.octet[0]
	                           ,data->entry.l2UcEntry.mac.octet[1]
	                           ,data->entry.l2UcEntry.mac.octet[2]
	                           ,data->entry.l2UcEntry.mac.octet[3]
	                           ,data->entry.l2UcEntry.mac.octet[4]
	                           ,data->entry.l2UcEntry.mac.octet[5]
	                           ,data->entry.l2UcEntry.vid
	                           ,(data->entry.l2UcEntry.flags&RTK_L2_UCAST_FLAG_CTAG_IF)?1:0
	                           ,0
	                           ,(data->entry.l2UcEntry.flags&RTK_L2_UCAST_FLAG_IVL)?1:0);


            PROC_PRINTF("fid=%d spa=%d age=%d auth1x=%d sablock=%d\n"
                           ,data->entry.l2UcEntry.fid
                           ,data->entry.l2UcEntry.port
                           ,data->entry.l2UcEntry.age
                           ,data->entry.l2UcEntry.auth
                           ,(data->entry.l2UcEntry.flags&RTK_L2_UCAST_FLAG_SA_BLOCK)?1:0);

            PROC_PRINTF("dablock=%d ext_spa=%d arp_used=%d static=%d\n"
                           ,(data->entry.l2UcEntry.flags&RTK_L2_UCAST_FLAG_DA_BLOCK)?1:0
                           ,data->entry.l2UcEntry.ext_port
                           ,(data->entry.l2UcEntry.flags&RTK_L2_UCAST_FLAG_ARP_USED)?1:0
                           ,(data->entry.l2UcEntry.flags&RTK_L2_UCAST_FLAG_STATIC)?1:0);

        }
        else if(data->entryType==RTK_LUT_L2MC)
        {
            PROC_PRINTF("[P2] mac=%02x:%02x:%02x:%02x:%02x:%02x ivl=%d vid=%d fid=%d l3lookup=%d\n"
                           ,data->entry.l2McEntry.mac.octet[0]
                           ,data->entry.l2McEntry.mac.octet[1]
                           ,data->entry.l2McEntry.mac.octet[2]
                           ,data->entry.l2McEntry.mac.octet[3]
                           ,data->entry.l2McEntry.mac.octet[4]
                           ,data->entry.l2McEntry.mac.octet[5]
                           ,(data->entry.l2McEntry.flags&RTK_L2_MCAST_FLAG_IVL)?1:0
                           ,data->entry.l2McEntry.vid
                           ,data->entry.l2McEntry.fid
                           ,0);

            PROC_PRINTF("mbr=0x%x extmbr=0x%x static=%d\n"
                           ,data->entry.l2McEntry.portmask.bits[0]
                           ,data->entry.l2McEntry.ext_portmask.bits[0],(data->entry.l2McEntry.flags&RTK_L2_IPMCAST_FLAG_STATIC)?1:0);


        }
        else if(data->entryType==RTK_LUT_L3MC)
        {
            if(!(data->entry.ipmcEntry.flags&RTK_L2_IPMCAST_FLAG_DIP_ONLY))
            {
                PROC_PRINTF("[P3] gip=%d.%d.%d.%d\n",((data->entry.ipmcEntry.dip>>24)&0xff)|0xe0,(data->entry.ipmcEntry.dip>>16)&0xff,(data->entry.ipmcEntry.dip>>8)&0xff,(data->entry.ipmcEntry.dip)&0xff);
                PROC_PRINTF("sip=%d.%d.%d.%d cvid=%d\n",(data->entry.ipmcEntry.sip>>24)&0xff,(data->entry.ipmcEntry.sip>>16)&0xff,(data->entry.ipmcEntry.sip>>8)&0xff,(data->entry.ipmcEntry.sip)&0xff,data->entry.ipmcEntry.vid);
                PROC_PRINTF("mbr=0x%x extmbr=0x%x\n",data->entry.ipmcEntry.portmask.bits[0],data->entry.ipmcEntry.ext_portmask.bits[0]);
                PROC_PRINTF("lutpri=%d fwdpri_en=%d\n"
                               ,data->entry.ipmcEntry.priority
                               ,(data->entry.ipmcEntry.flags&RTK_L2_IPMCAST_FLAG_FWD_PRI)?1:0);
            }
            else
            {
                PROC_PRINTF("[P4] gip=%d.%d.%d.%d\n",((data->entry.ipmcEntry.dip>>24)&0xff)|0xe0,(data->entry.ipmcEntry.dip>>16)&0xff,(data->entry.ipmcEntry.dip>>8)&0xff,(data->entry.ipmcEntry.dip)&0xff);
                PROC_PRINTF("mbr=0x%x extmbr=0x%x l3trans=0x%x\n",data->entry.ipmcEntry.portmask.bits[0],data->entry.ipmcEntry.ext_portmask.bits[0],data->entry.ipmcEntry.l3_trans_index);
                PROC_PRINTF("lutpri=%d fwdpri_en=%d dip_only=%d ext_fr=%d wan_sa=%d static=%d\n"
                               ,data->entry.ipmcEntry.priority
                               ,(data->entry.ipmcEntry.flags&RTK_L2_IPMCAST_FLAG_FWD_PRI)?1:0
                               ,(data->entry.ipmcEntry.flags&RTK_L2_IPMCAST_FLAG_DIP_ONLY)?1:0
                               ,(data->entry.ipmcEntry.flags&RTK_L2_IPMCAST_FLAG_FORCE_EXT_ROUTE)?1:0
                               ,(data->entry.ipmcEntry.flags&RTK_L2_IPMCAST_FLAG_L3MC_ROUTE_EN)?1:0
                               ,(data->entry.ipmcEntry.flags&RTK_L2_IPMCAST_FLAG_STATIC)?1:0
                              );
            }

        }

	return SUCCESS;

}



int32 dump_lut_table(struct seq_file *s, void *v)
{
	int len=0, count=0;

	int i = 0;
	int r = 0;
	rtk_l2_addr_table_t l2table;
	do
	{
		bzero(&l2table, sizeof(rtk_l2_addr_table_t));
		i =	r;
		if(RT_ERR_OK ==	(rtk_l2_nextValidEntry_get(&r, &l2table)))
		{
			/* Wrap	around */
			if(r < i)
			{
				break;
			}
			dump_lut_display(s, v, r, &l2table);
			r++;
			count++;
		}
		else
		{
			break;
		}
	} while(1);

	
	PROC_PRINTF("total l2 count: %d\n", count);


    return len;
}

int32 dump_flow_by_rawdata(struct seq_file *s, int32 idx, void *pFlowData)
{

	rtk_rg_asic_path1_entry_t *pP1Data = (rtk_rg_asic_path1_entry_t *)pFlowData;
	
	if(pP1Data->in_path == 0U)
	{
		if(pP1Data->in_multiple_act == 0U)
			dump_flow_p1Rawdata(s, idx, (void*)pP1Data);
		else
			dump_flow_p2Rawdata(s, idx, (void*)pP1Data);
	}else if (pP1Data->in_path == 1U)
	{
		if(pP1Data->in_multiple_act == 0U)
			dump_flow_p3Rawdata(s, idx, (void*)pP1Data);
		else
			dump_flow_p4Rawdata(s, idx, (void*)pP1Data);
	}else if (pP1Data->in_path == 2U)
	{
			dump_flow_p5Rawdata(s, idx, (void*)pP1Data);
	}else if (pP1Data->in_path == 3U)
	{
			dump_flow_p6Rawdata(s, idx, (void*)pP1Data);
	}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	else if (pP1Data->in_path == FB_PATH_MC_WIFI_AMSDU_TX)
	{
			dump_flow_pMcWifiAmsduTxRawdata(s, idx, (void*)pP1Data);
	}
#endif
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	else if (pP1Data->in_path == FB_PATH_VXLAN_US_EXTRA_TX)
	{
			dump_flow_pVxlanUsFragTxRawdata(s, idx, (void*)pP1Data);
	}
#if defined(CONFIG_RTK_FC_IPSEC_FASTFWD)
	else if (pP1Data->in_path == FB_PATH_ESP_SPI_DS)
	{
			dump_flow_pESP_SPI_Rawdata(s, idx, (void*)pP1Data);
	}
#endif
#endif

	return SUCCESS;
}

int32 dump_flow_p1Rawdata(struct seq_file *s, int32 flowIdx, void *pFlowData)
{
	rtk_rg_asic_path1_entry_t *p1Data = (rtk_rg_asic_path1_entry_t *)pFlowData;
	if(p1Data==NULL) return FAIL;

	PROC_PRINTF(" -- Flow["COLOR_Y"%d"COLOR_NM"] [P1] valid: %d --\n", flowIdx, p1Data->valid);

	PROC_PRINTF(">>ingress pattern\n");
	PROC_PRINTF("intf: %d spachk:%d\n", p1Data->in_intf_idx, p1Data->in_spa_check);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("iphdrif:%d\n", p1Data->in_iphdrif);
#endif
	PROC_PRINTF("dscpchk: %d tos: %d\n", p1Data->in_tos_check, p1Data->in_tos);
	PROC_PRINTF("stagif: %d ctagif: %d ctagpri: %d pppoeif: %d\n", p1Data->in_stagif, p1Data->in_ctagif, p1Data->in_cvlan_pri, p1Data->in_pppoeif);
#if defined(CONFIG_FC_RTL8277C_SERIES)
	PROC_PRINTF("smacidx: %d dmacidx: %d in_ethertype: 0x%04x (%s), in_len_encoded: %u\n", p1Data->in_smac_lut_idx, p1Data->in_dmac_lut_idx, ((p1Data->in_ethertype_msb<<8) | (p1Data->in_ethertype_lsb7_4<<4) | p1Data->in_ethertype_lsb3_0), fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL]?"care":"don't care", p1Data->in_len_encoded);
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("smacidx: %d dmacidx: %d in_ethertype: 0x%04x (%s)\n", p1Data->in_smac_lut_idx, p1Data->in_dmac_lut_idx, p1Data->in_ethertype, fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL]?"care":"don't care");
#else
	if(p1Data->in_protocol==RTK_FC_ETHERTYPE_DONT_CARE)
		PROC_PRINTF("smacidx: %d dmacidx: %d inproto: %d (%s)\n", p1Data->in_smac_lut_idx, p1Data->in_dmac_lut_idx, p1Data->in_protocol, "don't care");
	else {
		int i = 0;
		int enttryidx = p1Data->in_protocol;
		uint16 ethtype = 0U;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		enttryidx-=1;
#endif
		for(i = 0; i<RTK_FC_TABLESIZE_ETHERTYPE; i++){
			if(fc_db.ethertypeTbl[i].valid && fc_db.ethertypeTbl[i].hwIdx == enttryidx) {
				ethtype = fc_db.ethertypeTbl[i].ethType;
				break;
			}
		}
		PROC_PRINTF("smacidx: %d dmacidx: %d inproto: %d (0x%04x)\n", p1Data->in_smac_lut_idx, p1Data->in_dmac_lut_idx, p1Data->in_protocol, ethtype);
	}
#endif
	PROC_PRINTF("svlan: %d cvlan: %d spa: %d extspa: %d\n", p1Data->in_svlan_id, p1Data->in_cvlan_id, p1Data->in_spa, p1Data->in_ext_spa);
	PROC_PRINTF("pppoeidchk: %d pppoesid: %d\n\n", p1Data->in_pppoe_sid_check, p1Data->in_pppoe_sid);
	if(p1Data->in_spa==RTK_FC_MAC_PORT_PON) {
		PROC_PRINTF("streamcheck: %d streamidx: %d\n", p1Data->in_out_stream_idx_check_act, p1Data->in_out_stream_idx);	
	}

	PROC_PRINTF(">>egress action\n");
	if(p1Data->in_spa!=RTK_FC_MAC_PORT_PON) {
		PROC_PRINTF("streamact: %d streamidx: %d\n", p1Data->in_out_stream_idx_check_act, p1Data->in_out_stream_idx);
	}
	PROC_PRINTF("mtact: %d mtidx: %d exttagidx: %d\n", p1Data->out_share_meter_act, p1Data->out_share_meter_idx, p1Data->out_extra_tag_index);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("sw_shaper_en: %d\n", p1Data->sw_shaper_en);
#endif
	PROC_PRINTF("intf: %d : dmacidx: %d dmactrans: %d\n", p1Data->out_intf_idx, p1Data->out_dmac_idx, p1Data->out_dmac_trans);
	PROC_PRINTF("stagfmt: %d svidfmtact: %d sprifmtact: %d\n", p1Data->out_stag_format_act, p1Data->out_svid_format_act, p1Data->out_spri_format_act);
	PROC_PRINTF("egsvidact: %d svid: %d spri: %d\n", p1Data->out_egress_svid_act, p1Data->out_svlan_id, p1Data->out_spri);
	PROC_PRINTF("ctagfmt: %d cvidfmtact: %d cprifmtact: %d\n", p1Data->out_ctag_format_act, p1Data->out_cvid_format_act, p1Data->out_cpri_format_act);
	PROC_PRINTF("egcvidact: %d cvid: %d cpri: %d\n", p1Data->out_egress_cvid_act, p1Data->out_cvlan_id, p1Data->out_cpri);
	PROC_PRINTF("egport2vid:%s\n", (p1Data->out_egress_port_to_vid_act==1)?"sp2c":((p1Data->out_egress_port_to_vid_act==2)?"sp2s":((p1Data->out_egress_port_to_vid_act==3)?"cp2c":"none")));
	PROC_PRINTF("usrpriact: %d usrpri: %d\n", p1Data->out_user_pri_act, p1Data->out_user_priority);
	PROC_PRINTF("dscpact: %d dscp: %d\n", p1Data->out_dscp_act, p1Data->out_dscp);
	PROC_PRINTF("pmask: 0x%x extpmaskidx: %d\n", p1Data->out_portmask, p1Data->out_ext_portmask_idx);
	PROC_PRINTF("flowmibact: %d flowmibidx: %d\n", p1Data->out_flow_counter_act, p1Data->out_flow_counter_idx);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_EXTRA)
		PROC_PRINTF("flowmib2act: %d flowmib2idx: %d\n", p1Data->out_flow_counter2_act, p1Data->out_flow_counter2_idx);
#endif
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
	PROC_PRINTF("out_wfo_flow_mib_idx: %u ", p1Data->out_wfo_flow_mib_idx);
	if(p1Data->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_DISABLE)
		PROC_PRINTF("(DISABLE)\n");
	else if((RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_BASE <= p1Data->out_wfo_flow_mib_idx)  && (p1Data->out_wfo_flow_mib_idx <= RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_MAX))
		PROC_PRINTF("(RX CACHE, cache idx: %u)\n", (p1Data->out_wfo_flow_mib_idx - RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_BASE));
	else if((RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_BASE <= p1Data->out_wfo_flow_mib_idx)  && (p1Data->out_wfo_flow_mib_idx <= RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_MAX))
		PROC_PRINTF("(TX CACHE, cache idx: %u)\n", (p1Data->out_wfo_flow_mib_idx - RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_BASE));
	else if(p1Data->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_RX)
		PROC_PRINTF("(NON CACHE RX)\n");
	else if(p1Data->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_TX)
		PROC_PRINTF("(NON CACHE TX)\n");
	else
		PROC_PRINTF("(UNKNOWN)\n");
#endif
#if defined(CONFIG_RTK_FC_PKT_QUEUE_OFFLOAD_BY_PE)
	PROC_PRINTF("pe_tc_queue_valid: %u pe_tc_queue_idx %u pe_tc_queue_connection_idx %u\n", p1Data->pe_tc_queue_valid, p1Data->pe_tc_queue_idx, p1Data->pe_tc_queue_connection_idx);
#endif
	PROC_PRINTF("drop: %d multiact: %d uclut: %d smactrans: %d\n\n", p1Data->out_drop, p1Data->out_multiple_act, p1Data->out_uc_lut_lookup, p1Data->out_smac_trans);

	return SUCCESS;
}
int32 dump_flow_p2Rawdata(struct seq_file *s, int32 flowIdx, void *pFlowData)
{
	rtk_rg_asic_path2_entry_t *p2Data = (rtk_rg_asic_path2_entry_t *)pFlowData;
	if(p2Data==NULL) return FAIL;

	PROC_PRINTF(" -- Flow["COLOR_Y"%d"COLOR_NM"] [P2] valid: %d --\n", flowIdx, p2Data->valid);

	PROC_PRINTF(">>ingress pattern\n");
	PROC_PRINTF("spachk:%d\n", p2Data->in_spa_check);
	PROC_PRINTF("dscpchk: %d tos: %d\n", p2Data->in_tos_check, p2Data->in_tos);
	PROC_PRINTF("stagif: %d ctagif: %d ctagpri: %d pppoeif: %d\n", p2Data->in_stagif, p2Data->in_ctagif, p2Data->in_cvlan_pri, p2Data->in_pppoeif);
#if defined(CONFIG_FC_RTL8277C_SERIES)
	PROC_PRINTF("smacidx: %d dmacidx: %d in_ethertype: 0x%04x (%s), in_len_encoded: %u\n", p2Data->in_smac_lut_idx, p2Data->in_dmac_lut_idx, ((p2Data->in_ethertype_msb<<8) | (p2Data->in_ethertype_lsb7_4<<4) | p2Data->in_ethertype_lsb3_0), fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL]?"care":"don't care", p2Data->in_len_encoded);
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("smacidx: %d dmacidx: %d in_ethertype: 0x%04x (%s)\n", p2Data->in_smac_lut_idx, p2Data->in_dmac_lut_idx, p2Data->in_ethertype, fc_db.systemGlobal.flowCheckState[FB_FLOW_CHECK_PATH12_PROTOCOL]?"care":"don't care");
#else
	PROC_PRINTF("smacidx: %d dmacidx: %d inproto: %d\n", p2Data->in_smac_lut_idx, p2Data->in_dmac_lut_idx, p2Data->in_protocol);
#endif
	PROC_PRINTF("svlan: %d cvlan: %d spa: %d extspa: %d\n", p2Data->in_svlan_id, p2Data->in_cvlan_id, p2Data->in_spa, p2Data->in_ext_spa);
	PROC_PRINTF("pppoesidchk: %d pppoesid: %d\n", p2Data->in_pppoe_sid_check, p2Data->in_pppoe_sid);
	if(p2Data->in_spa==RTK_FC_MAC_PORT_PON) {
		PROC_PRINTF("streamcheck: %d streamidx: %d\n", p2Data->in_stream_idx_check, p2Data->in_stream_idx);	
	}

	PROC_PRINTF(">>egress action\n");
	PROC_PRINTF("intf: %d\n", p2Data->out_intf_idx);
	PROC_PRINTF("stagfmt: %d svidfmtact: %d sprifmtact: %d\n", p2Data->out_stag_format_act, p2Data->out_svid_format_act, p2Data->out_spri_format_act);
	PROC_PRINTF("egsvidact: %d svid: %d spri: %d\n", p2Data->out_egress_svid_act, p2Data->out_svlan_id, p2Data->out_spri);
	PROC_PRINTF("ctagfmt: %d cvidfmtact: %d cprifmtact: %d\n", p2Data->out_ctag_format_act, p2Data->out_cvid_format_act, p2Data->out_cpri_format_act);
	PROC_PRINTF("egcvidact: %d cvid: %d cpri: %d\n", p2Data->out_egress_cvid_act, p2Data->out_cvlan_id, p2Data->out_cpri);
	PROC_PRINTF("egport2vid:%s\n", (p2Data->out_egress_port_to_vid_act==1)?"sp2c":((p2Data->out_egress_port_to_vid_act==2)?"sp2s":((p2Data->out_egress_port_to_vid_act==3)?"cp2c":"none")));
	PROC_PRINTF("usrpriact: %d usrpri: %d\n", p2Data->out_user_pri_act, p2Data->out_user_priority);
	PROC_PRINTF("dscpact: %d dscp: %d\n", p2Data->out_dscp_act, p2Data->out_dscp);
	PROC_PRINTF("pmask: 0x%x extpmaskidx: %d\n", p2Data->out_portmask, p2Data->out_ext_portmask_idx);
	PROC_PRINTF("smactrans: %d\n\n", p2Data->out_smac_trans);

	return SUCCESS;
}
int32 dump_flow_p3Rawdata(struct seq_file *s, int32 flowIdx, void *pFlowData)
{
	rtk_rg_asic_path3_entry_t *p3Data = (rtk_rg_asic_path3_entry_t *)pFlowData;
	if(p3Data==NULL) return FAIL;

	PROC_PRINTF(" -- Flow["COLOR_Y"%d"COLOR_NM"] [P3] valid: %d --\n", flowIdx, p3Data->valid);

	PROC_PRINTF(">>ingress pattern\n");
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	PROC_PRINTF("intf: %d v6:%d l4proto_num: 0x%02x(%s)\n", p3Data->in_intf_idx, p3Data->in_ipv4_or_ipv6, p3Data->in_l4proto_num, L4PROTO_NUM_TO_STR(p3Data->in_l4proto_num));
#else
	PROC_PRINTF("intf: %d v6:%d l4proto: %d\n", p3Data->in_intf_idx, p3Data->in_ipv4_or_ipv6, p3Data->in_l4proto);
#endif
	PROC_PRINTF("dscpchk: %d tos: %d\n", p3Data->in_tos_check, p3Data->in_tos);
	PROC_PRINTF("stagif: %d ctagif: %d ctagpri: %d pppoeif: %d pppoesidchk: %d\n", p3Data->in_stagif, p3Data->in_ctagif, p3Data->in_cvlan_pri, p3Data->in_pppoeif, p3Data->in_pppoe_sid_check);
	if(!p3Data->in_ipv4_or_ipv6){
	PROC_PRINTF("srcv4ip: %d.%d.%d.%d dstv4ip: %d.%d.%d.%d\n",
		(p3Data->in_src_ipv4_addr>>24)&0xff, (p3Data->in_src_ipv4_addr>>16)&0xff, (p3Data->in_src_ipv4_addr>>8)&0xff, p3Data->in_src_ipv4_addr&0xff,
		(p3Data->in_dst_ipv4_addr>>24)&0xff, (p3Data->in_dst_ipv4_addr>>16)&0xff, (p3Data->in_dst_ipv4_addr>>8)&0xff, p3Data->in_dst_ipv4_addr&0xff);
	}else{
	PROC_PRINTF("srcv6ip: 0x%x dstv6ip: 0x%x\n", p3Data->in_src_ipv6_addr_hash, p3Data->in_dst_ipv6_addr_hash);
	}
	PROC_PRINTF("srcport: %d dstport: %d\n\n", p3Data->in_l4_src_port, p3Data->in_l4_dst_port);

	PROC_PRINTF(">>egress action\n");
	PROC_PRINTF("streamact: %d streamidx: %d\n", p3Data->out_stream_idx_act, p3Data->out_stream_idx);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	if(p3Data->out_change_l4_port_only)
		PROC_PRINTF("out_change_l4_port_only: %u (change %s only, new port 0x%04x)\n", p3Data->out_change_l4_port_only, (p3Data->out_change_l4_port_only==1)?"SPORT":"DPOTR", p3Data->out_change_l4_port);
#endif
	PROC_PRINTF("mtact: %d mtidx: %d exttagidx: %d\n", p3Data->out_share_meter_act, p3Data->out_share_meter_idx, p3Data->out_extra_tag_index);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("sw_shaper_en: %d\n", p3Data->sw_shaper_en);
#endif
	PROC_PRINTF("intf: %d : dmacidx: %d dmactrans: %d\n", p3Data->out_intf_idx, p3Data->out_dmac_idx, p3Data->out_dmac_trans);
	PROC_PRINTF("stagfmt: %d svidfmtact: %d sprifmtact: %d\n", p3Data->out_stag_format_act, p3Data->out_svid_format_act, p3Data->out_spri_format_act);
	PROC_PRINTF("egsvidact: %d svid: %d spri: %d\n", p3Data->out_egress_svid_act, p3Data->out_svlan_id, p3Data->out_spri);
	PROC_PRINTF("ctagfmt: %d cvidfmtact: %d cprifmtact: %d\n", p3Data->out_ctag_format_act, p3Data->out_cvid_format_act, p3Data->out_cpri_format_act);
	PROC_PRINTF("egcvidact: %d cvid: %d cpri: %d\n", p3Data->out_egress_cvid_act, p3Data->out_cvlan_id, p3Data->out_cpri);
	PROC_PRINTF("egport2vid:%s\n", (p3Data->out_egress_port_to_vid_act==1)?"sp2c":((p3Data->out_egress_port_to_vid_act==2)?"sp2s":((p3Data->out_egress_port_to_vid_act==3)?"cp2c":"none")));
	PROC_PRINTF("usrpriact: %d usrpri: %d\n", p3Data->out_user_pri_act, p3Data->out_user_priority);
	PROC_PRINTF("dscpact: %d dscp: %d\n", p3Data->out_dscp_act, p3Data->out_dscp);
	PROC_PRINTF("pmask: 0x%x extpmaskidx: %d\n", p3Data->out_portmask, p3Data->out_ext_portmask_idx);
	PROC_PRINTF("flowmibact: %d flowmibidx: %d\n", p3Data->out_flow_counter_act, p3Data->out_flow_counter_idx);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_EXTRA)
		PROC_PRINTF("flowmib2act: %d flowmib2idx: %d\n", p3Data->out_flow_counter2_act, p3Data->out_flow_counter2_idx);
#endif
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
	PROC_PRINTF("out_wfo_flow_mib_idx: %u ", p3Data->out_wfo_flow_mib_idx);
	if(p3Data->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_DISABLE)
		PROC_PRINTF("(DISABLE)\n");
	else if((RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_BASE <= p3Data->out_wfo_flow_mib_idx)  && (p3Data->out_wfo_flow_mib_idx <= RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_MAX))
		PROC_PRINTF("(RX CACHE, cache idx: %u)\n", (p3Data->out_wfo_flow_mib_idx - RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_BASE));
	else if((RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_BASE <= p3Data->out_wfo_flow_mib_idx)  && (p3Data->out_wfo_flow_mib_idx <= RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_MAX))
		PROC_PRINTF("(TX CACHE, cache idx: %u)\n", (p3Data->out_wfo_flow_mib_idx - RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_BASE));
	else if(p3Data->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_RX)
		PROC_PRINTF("(NON CACHE RX)\n");
	else if(p3Data->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_TX)
		PROC_PRINTF("(NON CACHE TX)\n");
	else
		PROC_PRINTF("(UNKNOWN)\n");
#endif	
#if defined(CONFIG_RTK_FC_PKT_QUEUE_OFFLOAD_BY_PE)
	PROC_PRINTF("pe_tc_queue_valid: %u pe_tc_queue_idx %u pe_tc_queue_connection_idx %u\n", p3Data->pe_tc_queue_valid, p3Data->pe_tc_queue_idx, p3Data->pe_tc_queue_connection_idx);
#endif
	PROC_PRINTF("drop: %d multiact: %d uclut: %d smactrans: %d\n\n", p3Data->out_drop, p3Data->out_multiple_act, p3Data->out_uc_lut_lookup, p3Data->out_smac_trans);

	return SUCCESS;
}
int32 dump_flow_p4Rawdata(struct seq_file *s, int32 flowIdx, void *pFlowData)
{
	rtk_rg_asic_path4_entry_t *p4Data = (rtk_rg_asic_path4_entry_t *)pFlowData;
	if(p4Data==NULL) return FAIL;

	PROC_PRINTF(" -- Flow["COLOR_Y"%d"COLOR_NM"] [P4] valid: %d --\n", flowIdx, p4Data->valid);

	PROC_PRINTF(">>ingress pattern\n");
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	PROC_PRINTF("intf: %d v6:%d l4proto_num: 0x%02x(%s)\n", p4Data->in_intf_idx, p4Data->in_ipv4_or_ipv6, p4Data->in_l4proto_num, L4PROTO_NUM_TO_STR(p4Data->in_l4proto_num));
#else
	PROC_PRINTF("intf: %d v6:%d l4proto: %d\n", p4Data->in_intf_idx, p4Data->in_ipv4_or_ipv6, p4Data->in_l4proto);
#endif
	PROC_PRINTF("dscpchk: %d tos: %d\n", p4Data->in_tos_check, p4Data->in_tos);
	PROC_PRINTF("stagif: %d ctagif: %d ctagpri: %d pppoeif: %d pppoesidchk: %d\n", p4Data->in_stagif, p4Data->in_ctagif, p4Data->in_cvlan_pri, p4Data->in_pppoeif, p4Data->in_pppoe_sid_check);
	if(!p4Data->in_ipv4_or_ipv6){
	PROC_PRINTF("srcv4ip: %d.%d.%d.%d dstv4ip: %d.%d.%d.%d\n",
		(p4Data->in_src_ipv4_addr>>24)&0xff, (p4Data->in_src_ipv4_addr>>16)&0xff, (p4Data->in_src_ipv4_addr>>8)&0xff, p4Data->in_src_ipv4_addr&0xff,
		(p4Data->in_dst_ipv4_addr>>24)&0xff, (p4Data->in_dst_ipv4_addr>>16)&0xff, (p4Data->in_dst_ipv4_addr>>8)&0xff, p4Data->in_dst_ipv4_addr&0xff);
	}else{
	PROC_PRINTF("srcv6ip: 0x%x dstv6ip: 0x%x\n", p4Data->in_src_ipv6_addr_hash, p4Data->in_dst_ipv6_addr_hash);
	}
	PROC_PRINTF("srcport: %d dstport: %d\n\n", p4Data->in_l4_src_port, p4Data->in_l4_dst_port);

	PROC_PRINTF(">>egress action\n");
	PROC_PRINTF("intf: %d\n", p4Data->out_intf_idx);
	PROC_PRINTF("stagfmt: %d svidfmtact: %d sprifmtact: %d\n", p4Data->out_stag_format_act, p4Data->out_svid_format_act, p4Data->out_spri_format_act);
	PROC_PRINTF("egsvidact: %d svid: %d spri: %d\n", p4Data->out_egress_svid_act, p4Data->out_svlan_id, p4Data->out_spri);
	PROC_PRINTF("ctagfmt: %d cvidfmtact: %d cprifmtact: %d\n", p4Data->out_ctag_format_act, p4Data->out_cvid_format_act, p4Data->out_cpri_format_act);
	PROC_PRINTF("egcvidact: %d cvid: %d cpri: %d\n", p4Data->out_egress_cvid_act, p4Data->out_cvlan_id, p4Data->out_cpri);
	PROC_PRINTF("egport2vid:%s\n", (p4Data->out_egress_port_to_vid_act==1)?"sp2c":((p4Data->out_egress_port_to_vid_act==2)?"sp2s":((p4Data->out_egress_port_to_vid_act==3)?"cp2c":"none")));
	PROC_PRINTF("usrpriact: %d usrpri: %d\n", p4Data->out_user_pri_act, p4Data->out_user_priority);
	PROC_PRINTF("dscpact: %d dscp: %d\n", p4Data->out_dscp_act, p4Data->out_dscp);
	PROC_PRINTF("pmask: 0x%x extpmaskidx: %d\n", p4Data->out_portmask, p4Data->out_ext_portmask_idx);
	PROC_PRINTF("smactrans: %d\n\n", p4Data->out_smac_trans);

	return SUCCESS;
}
int32 dump_flow_p5Rawdata(struct seq_file *s, int32 flowIdx, void *pFlowData)
{
	rtk_rg_asic_path5_entry_t *p5Data = (rtk_rg_asic_path5_entry_t *)pFlowData;
	if(p5Data==NULL) return FAIL;

	PROC_PRINTF(" -- Flow["COLOR_Y"%d"COLOR_NM"] [P5] valid: %d --\n", flowIdx, p5Data->valid);

	PROC_PRINTF(">>ingress pattern\n");
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	PROC_PRINTF("intf: %d v6:%d l4proto_num: 0x%02x(%s)\n", p5Data->in_intf_idx, p5Data->in_ipv4_or_ipv6, p5Data->in_l4proto_num, L4PROTO_NUM_TO_STR(p5Data->in_l4proto_num));
#else
	PROC_PRINTF("intf: %d v6:%d l4proto: %d\n", p5Data->in_intf_idx, p5Data->in_ipv4_or_ipv6, p5Data->in_l4proto);
#endif
	PROC_PRINTF("dscpchk: %d tos: %d\n", p5Data->in_tos_check, p5Data->in_tos);
	PROC_PRINTF("stagif: %d ctagif: %d ctagpri: %d pppoeif: %d\n", p5Data->in_stagif, p5Data->in_ctagif, p5Data->in_cvlan_pri, p5Data->in_pppoeif);
	if(!p5Data->in_ipv4_or_ipv6){

		if(p5Data->out_l4_act && !p5Data->out_l4_direction)
		{
		#if 0 //needs mapping to sw netif table
			PROC_PRINTF("srcv4ip: %d.%d.%d.%d dstv4ip: %d.%d.%d.%d\n",
				(p5Data->in_src_ipv4_addr>>24)&0xff, (p5Data->in_src_ipv4_addr>>16)&0xff, (p5Data->in_src_ipv4_addr>>8)&0xff, p5Data->in_src_ipv4_addr&0xff,
				(fc_db.netif[p5Data->in_intf_idx].rtk_netif.ipAddr>>24)&0xff, (rg_db.netif[p5Data->in_intf_idx].rtk_netif.ipAddr>>16)&0xff, (rg_db.netif[p5Data->in_intf_idx].rtk_netif.ipAddr>>8)&0xff, rg_db.netif[p5Data->in_intf_idx].rtk_netif.ipAddr&0xff);
		#else
			PROC_PRINTF("srcv4ip: %d.%d.%d.%d dstv4ip=netif[%d] ip address\n",
				(p5Data->in_src_ipv4_addr>>24)&0xff, (p5Data->in_src_ipv4_addr>>16)&0xff, (p5Data->in_src_ipv4_addr>>8)&0xff, p5Data->in_src_ipv4_addr&0xff,
				p5Data->in_intf_idx);
		#endif
		}
		else
		{
			PROC_PRINTF("srcv4ip: %d.%d.%d.%d dstv4ip: %d.%d.%d.%d\n",
				(p5Data->in_src_ipv4_addr>>24)&0xff, (p5Data->in_src_ipv4_addr>>16)&0xff, (p5Data->in_src_ipv4_addr>>8)&0xff, p5Data->in_src_ipv4_addr&0xff,
				(p5Data->in_dst_ipv4_addr>>24)&0xff, (p5Data->in_dst_ipv4_addr>>16)&0xff, (p5Data->in_dst_ipv4_addr>>8)&0xff, p5Data->in_dst_ipv4_addr&0xff);
		}


	}else{
		PROC_PRINTF("srcv6ip: 0x%x dstv6ip: 0x%x\n", p5Data->in_src_ipv6_addr_hash, p5Data->in_dst_ipv6_addr_hash);
	}
	PROC_PRINTF("srcport: %d dstport: %d\n\n", p5Data->in_l4_src_port, p5Data->in_l4_dst_port);

	PROC_PRINTF(">>egress action\n");
  	if(!p5Data->in_ipv4_or_ipv6 && p5Data->out_l4_act)
  	{
		#if 0 //FIXME
  		if(p5Data->out_l4_direction)
			PROC_PRINTF("out_srcv4ip: %d.%d.%d.%d\n",
				(rg_db.netif[p5Data->out_intf_idx].rtk_netif.ipAddr>>24)&0xff, (rg_db.netif[p5Data->out_intf_idx].rtk_netif.ipAddr>>16)&0xff, (rg_db.netif[p5Data->out_intf_idx].rtk_netif.ipAddr>>8)&0xff, rg_db.netif[p5Data->out_intf_idx].rtk_netif.ipAddr&0xff);
		else
		#endif
			PROC_PRINTF("out_destv4ip: %d.%d.%d.%d\n",
				(p5Data->out_dst_ipv4_addr>>24)&0xff, (p5Data->out_dst_ipv4_addr>>16)&0xff, (p5Data->out_dst_ipv4_addr>>8)&0xff, p5Data->out_dst_ipv4_addr&0xff);
  	}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("fmract: %d fmridx: %d\n", p5Data->out_fmr_idx_act, p5Data->out_fmr_idx);
#endif
	PROC_PRINTF("streamact: %d streamidx: %d\n", p5Data->out_stream_idx_act, p5Data->out_stream_idx);
	PROC_PRINTF("mtact: %d mtidx: %d exttagidx: %d\n", p5Data->out_share_meter_act, p5Data->out_share_meter_idx, p5Data->out_extra_tag_index);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("sw_shaper_en: %d\n", p5Data->sw_shaper_en);
#endif
	PROC_PRINTF("intf: %d dmacidx: %d\n", p5Data->out_intf_idx, p5Data->out_dmac_idx);
	PROC_PRINTF("l4act: %d l4dir: %d(%s) l4port: %d\n", p5Data->out_l4_act, p5Data->out_l4_direction, p5Data->out_l4_direction?"outbound":"inbound", p5Data->out_l4_port);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	if(p5Data->out_change_l4_port_only)
		PROC_PRINTF("out_change_l4_port_only: %u (change %s only, new port 0x%04x)\n", p5Data->out_change_l4_port_only, (p5Data->out_change_l4_port_only==1)?"SPORT":"DPOTR", p5Data->out_l4_port);
#endif
	PROC_PRINTF("stagfmt: %d svidfmtact: %d sprifmtact: %d\n", p5Data->out_stag_format_act, p5Data->out_svid_format_act, p5Data->out_spri_format_act);
	PROC_PRINTF("egsvidact: %d svid: %d spri: %d\n", p5Data->out_egress_svid_act, p5Data->out_svlan_id, p5Data->out_spri);
	PROC_PRINTF("ctagfmt: %d cvidfmtact: %d cprifmtact: %d\n", p5Data->out_ctag_format_act, p5Data->out_cvid_format_act, p5Data->out_cpri_format_act);
	PROC_PRINTF("egcvidact: %d cvid: %d cpri: %d\n", p5Data->out_egress_cvid_act, p5Data->out_cvlan_id, p5Data->out_cpri);
	PROC_PRINTF("egport2vid: %s\n", (p5Data->out_egress_port_to_vid_act==1)?"sp2c":((p5Data->out_egress_port_to_vid_act==2)?"sp2s":((p5Data->out_egress_port_to_vid_act==3)?"cp2c":"none")));
	PROC_PRINTF("usrpriact: %d usrpri: %d\n", p5Data->out_user_pri_act, p5Data->out_user_priority);
	PROC_PRINTF("dscpact: %d dscp: %d\n", p5Data->out_dscp_act, p5Data->out_dscp);
	PROC_PRINTF("flowmibact: %d flowmibidx: %d\n", p5Data->out_flow_counter_act, p5Data->out_flow_counter_idx);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_EXTRA)
		PROC_PRINTF("flowmib2act: %d flowmib2idx: %d\n", p5Data->out_flow_counter2_act, p5Data->out_flow_counter2_idx);
#endif
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
	PROC_PRINTF("out_wfo_flow_mib_idx: %u ", p5Data->out_wfo_flow_mib_idx);
	if(p5Data->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_DISABLE)
		PROC_PRINTF("(DISABLE)\n");
	else if((RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_BASE <= p5Data->out_wfo_flow_mib_idx)  && (p5Data->out_wfo_flow_mib_idx <= RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_MAX))
		PROC_PRINTF("(RX CACHE, cache idx: %u)\n", (p5Data->out_wfo_flow_mib_idx - RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_BASE));
	else if((RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_BASE <= p5Data->out_wfo_flow_mib_idx)  && (p5Data->out_wfo_flow_mib_idx <= RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_MAX))
		PROC_PRINTF("(TX CACHE, cache idx: %u)\n", (p5Data->out_wfo_flow_mib_idx - RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_BASE));
	else if(p5Data->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_RX)
		PROC_PRINTF("(NON CACHE RX)\n");
	else if(p5Data->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_TX)
		PROC_PRINTF("(NON CACHE TX)\n");
	else
		PROC_PRINTF("(UNKNOWN)\n");
#endif	
#if defined(CONFIG_RTK_FC_PKT_QUEUE_OFFLOAD_BY_PE)
	PROC_PRINTF("pe_tc_queue_valid: %u pe_tc_queue_idx %u pe_tc_queue_connection_idx %u\n", p5Data->pe_tc_queue_valid, p5Data->pe_tc_queue_idx, p5Data->pe_tc_queue_connection_idx);
#endif
	PROC_PRINTF("drop: %d\n\n", p5Data->out_drop);

	return SUCCESS;
}
int32 dump_flow_p6Rawdata(struct seq_file *s, int32 flowIdx, void *pFlowData)
{
	rtk_rg_asic_path6_entry_t *p6Data = (rtk_rg_asic_path6_entry_t *)pFlowData;
	if(p6Data==NULL) return FAIL;

	PROC_PRINTF(" -- Flow["COLOR_Y"%d"COLOR_NM"] [P6] valid: %d --\n", flowIdx, p6Data->valid);

	PROC_PRINTF(">>ingress pattern\n");
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("intf: %d\n", p6Data->in_intf_idx);
#else
	PROC_PRINTF("intf: %d proto: %d\n", p6Data->in_intf_idx, p6Data->in_protocol);
#endif
	PROC_PRINTF("stagif: %d ctagif: %d ctagpri: %d pppoeif: %d\n", p6Data->in_stagif, p6Data->in_ctagif, p6Data->in_cvlan_pri, p6Data->in_pppoeif);
	PROC_PRINTF("pptpif: %d l2tpif: %d dslite: %d 6rd: %d greEthBrif: %d\n", p6Data->in_pptpif, p6Data->in_l2tpif, p6Data->in_dsliteif,p6Data->rsvd_in_6rdif, p6Data->in_greEthBrif);
	if(p6Data->in_pptpif)
	{
		PROC_PRINTF("gre callidchk: %d callid: 0x%x\n", p6Data->in_gre_call_id_check, p6Data->in_gre_call_id);
	}
	if(p6Data->in_l2tpif)
	{
		PROC_PRINTF("l2tp tunnelidchk: %d sessionidchk: %d\n", p6Data->in_l2tp_tunnel_id_check, p6Data->in_l2tp_session_id_check);
		PROC_PRINTF("l2tp tunnelid: 0x%x sessionid: 0x%x\n", p6Data->in_l2tp_tunnel_id, p6Data->in_l2tp_session_id);
	}
	PROC_PRINTF("smachk: %d dmacchk: %d\n", p6Data->in_src_mac_check, p6Data->in_dst_mac_check);
	PROC_PRINTF("pppoesidchk: %d pppoesid: %u\n", p6Data->in_pppoe_sid_check, p6Data->in_pppoe_sid);
	PROC_PRINTF("sipchk: %d dipchk: %d\n", p6Data->in_src_ip_check, p6Data->in_dst_ip_check);
	PROC_PRINTF("sportchk: %d dportchk: %d\n", p6Data->in_l4_src_port_check, p6Data->in_l4_dst_port_check);
	PROC_PRINTF("dscpchk: %d tos: %d\n", p6Data->in_tos_check, p6Data->in_tos);

	PROC_PRINTF("smacidx: %d dmacidx: %d\n", p6Data->in_smac_lut_idx, p6Data->in_dmac_lut_idx);
	if(!p6Data->in_dsliteif){
	PROC_PRINTF("srcv4ip: %d.%d.%d.%d dstv4ip: %d.%d.%d.%d\n",
		(p6Data->in_src_ipv4_addr>>24)&0xff, (p6Data->in_src_ipv4_addr>>16)&0xff, (p6Data->in_src_ipv4_addr>>8)&0xff, p6Data->in_src_ipv4_addr&0xff,
		(p6Data->in_dst_ipv4_addr>>24)&0xff, (p6Data->in_dst_ipv4_addr>>16)&0xff, (p6Data->in_dst_ipv4_addr>>8)&0xff, p6Data->in_dst_ipv4_addr&0xff);
	}else{
	PROC_PRINTF("srcv6ip: 0x%x dstv6ip: 0x%x\n", p6Data->in_src_ipv6_addr_hash, p6Data->in_dst_ipv6_addr_hash);
	}
	PROC_PRINTF("srcport: %d dstport: %d\n\n", p6Data->in_l4_src_port, p6Data->in_l4_dst_port);

	PROC_PRINTF(">>egress action\n");
	PROC_PRINTF("drop: %d\n\n", p6Data->out_drop);

	return SUCCESS;
}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
int32 dump_flow_pMcWifiAmsduTxRawdata(struct seq_file *s, int32 flowIdx, void *pFlowData)
{
	rtk_rg_asic_pathMc_wifi_amsdu_tx_entry_t *pMcWifiAmsduTxDate = (rtk_rg_asic_pathMc_wifi_amsdu_tx_entry_t *)pFlowData;
	if(pMcWifiAmsduTxDate==NULL) return FAIL;

	PROC_PRINTF(" -- Flow["COLOR_Y"%d"COLOR_NM"] [PMcWifiAmsduTxDate] valid: %d --\n", flowIdx, pMcWifiAmsduTxDate->valid);

	PROC_PRINTF(">>ingress pattern\n");
	PROC_PRINTF("in_dmac_lut_idx: %u in_wifi_mac_id: %u\n\n", pMcWifiAmsduTxDate->in_dmac_lut_idx, pMcWifiAmsduTxDate->in_wifi_mac_id);

	PROC_PRINTF(">>egress action\n");
	PROC_PRINTF("out_dmac_lut_idx: %u out_dmac_lut_act: %u\n", pMcWifiAmsduTxDate->out_dmac_lut_idx, pMcWifiAmsduTxDate->out_dmac_lut_act);
	PROC_PRINTF("out_ldpid: 0x%x out_ldpid_act: %u\n", pMcWifiAmsduTxDate->out_ldpid, pMcWifiAmsduTxDate->out_ldpid_act);
	PROC_PRINTF("out_cos: %u out_cos_act: %u\n", pMcWifiAmsduTxDate->out_cos, pMcWifiAmsduTxDate->out_cos_act);
	PROC_PRINTF("out_sw_id: %u out_sw_id_act: %u\n\n", pMcWifiAmsduTxDate->out_sw_id, pMcWifiAmsduTxDate->out_sw_id_act);
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
	PROC_PRINTF("out_wfo_flow_mib_idx: %u ", pMcWifiAmsduTxDate->out_wfo_flow_mib_idx);
	if(pMcWifiAmsduTxDate->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_DISABLE)
		PROC_PRINTF("(DISABLE)\n");
	else if((RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_BASE <= pMcWifiAmsduTxDate->out_wfo_flow_mib_idx)  && (pMcWifiAmsduTxDate->out_wfo_flow_mib_idx <= RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_MAX))
		PROC_PRINTF("(RX CACHE, cache idx: %u)\n", (pMcWifiAmsduTxDate->out_wfo_flow_mib_idx - RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_RX_BASE));
	else if((RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_BASE <= pMcWifiAmsduTxDate->out_wfo_flow_mib_idx)  && (pMcWifiAmsduTxDate->out_wfo_flow_mib_idx <= RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_MAX))
		PROC_PRINTF("(TX CACHE, cache idx: %u)\n", (pMcWifiAmsduTxDate->out_wfo_flow_mib_idx - RTK_ASIC_WFO_PER_FLOW_MIB_CACHE_IDX_TX_BASE));
	else if(pMcWifiAmsduTxDate->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_RX)
		PROC_PRINTF("(NON CACHE RX)\n");
	else if(pMcWifiAmsduTxDate->out_wfo_flow_mib_idx == RTK_ASIC_WFO_PER_FLOW_MIB_NON_CACHE_IDX_TX)
		PROC_PRINTF("(NON CACHE TX)\n");
	else
		PROC_PRINTF("(UNKNOWN)\n");
#endif
#if defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("out_l2format_act_vld: %u out_l2format_act: %u\n\n", pMcWifiAmsduTxDate->out_l2format_act_vld, pMcWifiAmsduTxDate->out_l2format_act);
#endif

	return SUCCESS;
}
#endif
#endif


#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int32 dump_flow_pVxlanUsFragTxRawdata(struct seq_file *s, int32 flowIdx, void *pFlowData)
{
	rtk_rg_asic_pathVxlan_up_extra_tx_entry_t *pVxlanUsExtraTxData = (rtk_rg_asic_pathVxlan_up_extra_tx_entry_t *)pFlowData;
	if(pVxlanUsExtraTxData==NULL) return FAIL;

	PROC_PRINTF(" -- Flow["COLOR_Y"%d"COLOR_NM"] [P5] valid: %d --\n", flowIdx, pVxlanUsExtraTxData->valid);

	PROC_PRINTF(">>ingress pattern\n");
	PROC_PRINTF("lspid: 0x%x\n", pVxlanUsExtraTxData->lspid);
	PROC_PRINTF("netif_id(pol_grp_id): 0x%x\n", pVxlanUsExtraTxData->netif_id);


	PROC_PRINTF(">>egress action\n");
	PROC_PRINTF("streamact: %d streamidx: %d\n", pVxlanUsExtraTxData->out_stream_idx_act, pVxlanUsExtraTxData->out_stream_idx);
	PROC_PRINTF("intf: %d, ldpid: %d \n", pVxlanUsExtraTxData->out_intf_idx, pVxlanUsExtraTxData->ldpid);
	PROC_PRINTF("usrpriact: %d usrpri: %d\n", pVxlanUsExtraTxData->out_user_pri_act, pVxlanUsExtraTxData->out_user_priority);
	PROC_PRINTF("out_vxlan_sport_update_en: %d out_vxlan_sport: %d\n", pVxlanUsExtraTxData->out_vxlan_sport_update_en, pVxlanUsExtraTxData->out_vxlan_sport);
	return SUCCESS;
}
#if defined(CONFIG_RTK_FC_IPSEC_FASTFWD)
int32 dump_flow_pESP_SPI_Rawdata(struct seq_file *s, int32 flowIdx, void *pFlowData)
{
	rtk_rg_asic_esp_ds_spi_entry_t *p5Data = (rtk_rg_asic_esp_ds_spi_entry_t *)pFlowData;
	if(p5Data==NULL) return FAIL;

	PROC_PRINTF(" -- Flow["COLOR_Y"%d"COLOR_NM"] [ESP SPI] valid: %d --\n", flowIdx, p5Data->valid);

	PROC_PRINTF(">>ingress pattern\n");
	PROC_PRINTF("SPI: 0x%x)\n", p5Data->spi);
	PROC_PRINTF("intf: %d v6:%d l4proto_num: 0x%02x(%s)\n", p5Data->in_intf_idx, p5Data->in_ipv4_or_ipv6, p5Data->in_l4proto_num, L4PROTO_NUM_TO_STR(p5Data->in_l4proto_num));

	PROC_PRINTF("dscpchk: %d tos: %d\n", p5Data->in_tos_check, p5Data->in_tos);
	PROC_PRINTF("stagif: %d ctagif: %d ctagpri: %d pppoeif: %d\n", p5Data->in_stagif, p5Data->in_ctagif, p5Data->in_cvlan_pri, p5Data->in_pppoeif);
	if(!p5Data->in_ipv4_or_ipv6){

		if(p5Data->out_l4_act && !p5Data->out_l4_direction)
		{
		#if 0 //needs mapping to sw netif table
			PROC_PRINTF("srcv4ip: %d.%d.%d.%d dstv4ip: %d.%d.%d.%d\n",
				(p5Data->in_src_ipv4_addr>>24)&0xff, (p5Data->in_src_ipv4_addr>>16)&0xff, (p5Data->in_src_ipv4_addr>>8)&0xff, p5Data->in_src_ipv4_addr&0xff,
				(fc_db.netif[p5Data->in_intf_idx].rtk_netif.ipAddr>>24)&0xff, (rg_db.netif[p5Data->in_intf_idx].rtk_netif.ipAddr>>16)&0xff, (rg_db.netif[p5Data->in_intf_idx].rtk_netif.ipAddr>>8)&0xff, rg_db.netif[p5Data->in_intf_idx].rtk_netif.ipAddr&0xff);
		#else
			PROC_PRINTF("srcv4ip: %d.%d.%d.%d dstv4ip=netif[%d] ip address\n",
				(p5Data->in_src_ipv4_addr>>24)&0xff, (p5Data->in_src_ipv4_addr>>16)&0xff, (p5Data->in_src_ipv4_addr>>8)&0xff, p5Data->in_src_ipv4_addr&0xff,
				p5Data->in_intf_idx);
		#endif
		}
		else
		{
			PROC_PRINTF("srcv4ip: %d.%d.%d.%d dstv4ip: %d.%d.%d.%d\n",
				(p5Data->in_src_ipv4_addr>>24)&0xff, (p5Data->in_src_ipv4_addr>>16)&0xff, (p5Data->in_src_ipv4_addr>>8)&0xff, p5Data->in_src_ipv4_addr&0xff,
				(p5Data->in_dst_ipv4_addr>>24)&0xff, (p5Data->in_dst_ipv4_addr>>16)&0xff, (p5Data->in_dst_ipv4_addr>>8)&0xff, p5Data->in_dst_ipv4_addr&0xff);
		}


	}else{
		PROC_PRINTF("srcv6ip: 0x%x dstv6ip: 0x%x\n", p5Data->in_src_ipv6_addr_hash, p5Data->in_dst_ipv6_addr_hash);
	}
	PROC_PRINTF("srcport: %d dstport: %d\n\n", p5Data->in_l4_src_port, p5Data->in_l4_dst_port);

	PROC_PRINTF(">>egress action\n");
  	if(!p5Data->in_ipv4_or_ipv6 && p5Data->out_l4_act)
  	{
		#if 0 //FIXME
  		if(p5Data->out_l4_direction)
			PROC_PRINTF("out_srcv4ip: %d.%d.%d.%d\n",
				(rg_db.netif[p5Data->out_intf_idx].rtk_netif.ipAddr>>24)&0xff, (rg_db.netif[p5Data->out_intf_idx].rtk_netif.ipAddr>>16)&0xff, (rg_db.netif[p5Data->out_intf_idx].rtk_netif.ipAddr>>8)&0xff, rg_db.netif[p5Data->out_intf_idx].rtk_netif.ipAddr&0xff);
		else
		#endif
			PROC_PRINTF("out_destv4ip: %d.%d.%d.%d\n",
				(p5Data->out_dst_ipv4_addr>>24)&0xff, (p5Data->out_dst_ipv4_addr>>16)&0xff, (p5Data->out_dst_ipv4_addr>>8)&0xff, p5Data->out_dst_ipv4_addr&0xff);
  	}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("fmract: %d fmridx: %d\n", p5Data->out_fmr_idx_act, p5Data->out_fmr_idx);
#endif
	PROC_PRINTF("streamact: %d streamidx: %d\n", p5Data->out_stream_idx_act, p5Data->out_stream_idx);
	PROC_PRINTF("mtact: %d mtidx: %d exttagidx: %d\n", p5Data->out_share_meter_act, p5Data->out_share_meter_idx, p5Data->out_extra_tag_index);
#if defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("sw_shaper_en: %d\n", p5Data->sw_shaper_en);
#endif
	PROC_PRINTF("intf: %d dmacidx: %d\n", p5Data->out_intf_idx, p5Data->out_dmac_idx);
	PROC_PRINTF("l4act: %d l4dir: %d(%s) l4port: %d\n", p5Data->out_l4_act, p5Data->out_l4_direction, p5Data->out_l4_direction?"outbound":"inbound", p5Data->out_l4_port);

	PROC_PRINTF("stagfmt: %d svidfmtact: %d sprifmtact: %d\n", p5Data->out_stag_format_act, p5Data->out_svid_format_act, p5Data->out_spri_format_act);
	PROC_PRINTF("egsvidact: %d svid: %d spri: %d\n", p5Data->out_egress_svid_act, p5Data->out_svlan_id, p5Data->out_spri);
	PROC_PRINTF("ctagfmt: %d cvidfmtact: %d cprifmtact: %d\n", p5Data->out_ctag_format_act, p5Data->out_cvid_format_act, p5Data->out_cpri_format_act);
	PROC_PRINTF("egcvidact: %d cvid: %d cpri: %d\n", p5Data->out_egress_cvid_act, p5Data->out_cvlan_id, p5Data->out_cpri);
	PROC_PRINTF("egport2vid: %s\n", (p5Data->out_egress_port_to_vid_act==1)?"sp2c":((p5Data->out_egress_port_to_vid_act==2)?"sp2s":((p5Data->out_egress_port_to_vid_act==3)?"cp2c":"none")));
	PROC_PRINTF("usrpriact: %d usrpri: %d\n", p5Data->out_user_pri_act, p5Data->out_user_priority);
	PROC_PRINTF("dscpact: %d dscp: %d\n", p5Data->out_dscp_act, p5Data->out_dscp);
	PROC_PRINTF("drop: %d\n\n", p5Data->out_drop);

	return SUCCESS;
}
#endif
#endif


//#define diag_util_inet_mactoa mactoa
int8 *diag_util_inet_mactoa (const uint8 *mac)
{
        static int8 str[6*sizeof "123"];

    if (NULL == mac)
    {
        sprintf(str,"NULL");
        return str;
    }

    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return str;
} /* end of diag_util_mac2str */

/*IPv4 address to string*/
int8 *diag_util_inet_ntoa(uint32 ina)
{
	static int8 buf[4*sizeof "123"];
	sprintf(buf, "%d.%d.%d.%d", ((ina>>24)&0xff), ((ina>>16)&0xff), ((ina>>8)&0xff), (ina&0xff));
	return (buf);
}

/*IPv6 address to string*/
int8 *diag_util_inet_n6toa(const uint8 *ipv6)
{
	static int8 buf[8*sizeof "FFFF:"];
    uint32  i;
    uint16  ipv6_ptr[8] = {0};

    for (i = 0; i < 8 ;i++)
    {
        ipv6_ptr[i] = ipv6[i*2+1];
        ipv6_ptr[i] |=  ipv6[i*2] << 8;
    }

    sprintf(buf, "%X:%X:%X:%X:%X:%X:%X:%X", ipv6_ptr[0], ipv6_ptr[1], ipv6_ptr[2], ipv6_ptr[3]
    , ipv6_ptr[4], ipv6_ptr[5], ipv6_ptr[6], ipv6_ptr[7]);
	return (buf);
}


#if defined(CONFIG_RTK_L34_XPON_PLATFORM)

#define diag_util_printf(fmt, args...)    printk( fmt, ## args)
#include <diag_display.h>


rtk_hsb_t rawHsb;
rtk_hsa_t rawHsa;
rtk_hsa_debug_t rawHsd;

rtk_rg_asic_hsb_entry_t rawL34Hsb;
rtk_rg_asic_hsa_entry_t rawL34Hsa;

int32 dump_hs(struct seq_file *s, void *v)
{
	int32 ret,value=0;
	bool show = 1;
	char acl_dbg_info[64];

	ret=0;
	memset((void*)&rawHsb,0,sizeof(rawHsb));
	memset((void*)&rawHsa,0,sizeof(rawHsa));
	memset((void*)&rawHsd,0,sizeof(rawHsd));

	//disable hsa latch ,stop hab update (config as non-latch mode)
	rtk_rg_asic_l2HsbaLatchMode_set(DISABLED);

#if defined(CONFIG_RTL9607C_SERIES) && defined(CONFIG_RTL9603CVD_SERIES)
	if(ASIC_CHIP_ID==RTL9607C_CHIP_ID)
	{
		ret = rtl9607c_hsbData_get(&rawHsb);
		ASSERT_EQ(ret,RT_ERR_OK);

		ret = rtl9607c_hsbPar_get(&rawHsb);
		ASSERT_EQ(ret,RT_ERR_OK);

		ret = rtl9607c_hsaData_get(&rawHsa);
		ASSERT_EQ(ret,RT_ERR_OK);

		ret = rtl9607c_hsdData_get(&rawHsd);
		ASSERT_EQ(ret,RT_ERR_OK);
		
		if(rawHsa.dbghsa_oq_fb){ // via FB
			ret = rtl9607c_fbData_get(&rawHsa);
			ASSERT_EQ(ret,RT_ERR_OK);
			value = 1;
		}
	}
	else
	{
		ret = rtl9603cvd_hsbData_get(&rawHsb);
		ASSERT_EQ(ret,RT_ERR_OK);

		ret = rtl9603cvd_hsbPar_get(&rawHsb);
		ASSERT_EQ(ret,RT_ERR_OK);

		ret = rtl9603cvd_hsaData_get(&rawHsa);
		ASSERT_EQ(ret,RT_ERR_OK);

		ret = rtl9603cvd_hsdData_get(&rawHsd);
		ASSERT_EQ(ret,RT_ERR_OK);

		if(rawHsa.dbghsa_oq_fb){ // via FB
			ret = rtl9603cvd_fbData_get(&rawHsa);
			ASSERT_EQ(ret,RT_ERR_OK);
			value = 1;
		}
	}

#elif defined(CONFIG_RTL9607C_SERIES)
	ret = rtl9607c_hsbData_get(&rawHsb);
	ASSERT_EQ(ret,RT_ERR_OK);

	ret = rtl9607c_hsbPar_get(&rawHsb);
	ASSERT_EQ(ret,RT_ERR_OK);

	ret = rtl9607c_hsaData_get(&rawHsa);
	ASSERT_EQ(ret,RT_ERR_OK);

	ret = rtl9607c_hsdData_get(&rawHsd);
	ASSERT_EQ(ret,RT_ERR_OK);
	
	if(rawHsa.dbghsa_oq_fb){ // via FB
		ret = rtl9607c_fbData_get(&rawHsa);
		ASSERT_EQ(ret,RT_ERR_OK);
		value = 1;
	}
#elif defined(CONFIG_RTL9603CVD_SERIES)
	ret = rtl9603cvd_hsbData_get(&rawHsb);
	ASSERT_EQ(ret,RT_ERR_OK);

	ret = rtl9603cvd_hsbPar_get(&rawHsb);
	ASSERT_EQ(ret,RT_ERR_OK);

	ret = rtl9603cvd_hsaData_get(&rawHsa);
	ASSERT_EQ(ret,RT_ERR_OK);

	ret = rtl9603cvd_hsdData_get(&rawHsd);
	ASSERT_EQ(ret,RT_ERR_OK);

	if(rawHsa.dbghsa_oq_fb){ // via FB
		ret = rtl9603cvd_fbData_get(&rawHsa);
		ASSERT_EQ(ret,RT_ERR_OK);
		value = 1;
	}
#else
#error "please add "
#endif


	if((rawHsb.bgdsc != rawHsa.dbghsa_bgdsc) || (rawHsb.endsc != rawHsa.dbghsa_oq_endsc))
	{
		rtlglue_printf("HSB/HSA not match! please try again ...\n");
		show = 0;
	}

	//enable hab update (config as all-latch mode)
	rtk_rg_asic_l2HsbaLatchMode_set(ENABLED);


	if(show){
		/* HSB display */
		rtlglue_printf("---- "COLOR_Y "[HSB:]" COLOR_NM "------------------------------------\n");
		_diag_debug_hsb_display(&rawHsb);
		rtlglue_printf("----------------------------------------------\n");

		/* HSA display */
		memset(acl_dbg_info, 0, sizeof(acl_dbg_info));
		if(rawHsa.dbghsa_epcom_fwdrsn == CPU_REASON_ACL)
			_rtk_fc_aclHeaderInfo_get(acl_dbg_info, sizeof(acl_dbg_info), (rawHsa.dbghsa_grp0_cvlan_info&RXINFO_REF_ACL_RSN_BIT));
		rtlglue_printf("---- "COLOR_Y "[HSA: (by %s)]" COLOR_NM COLOR_G "%s" COLOR_NM "------------------------------------\n", value?"FB":"switch", acl_dbg_info);
		_diag_debug_hsa_display(&rawHsa);
		rtlglue_printf("----------------------------------------------\n");

		/* HSD display */
		rtlglue_printf("---- "COLOR_Y "[HSD:]" COLOR_NM "------------------------------------\n");
		_diag_debug_hsd_display(&rawHsd);
		rtlglue_printf("----------------------------------------------\n");
	}

    return SUCCESS;

}

void dump_l34hsb_display(rtk_rg_asic_hsb_entry_t *l34hsbData)
{
	rtlglue_printf("spa: %d extspa: %d ponIdx: %d l2payloadLen: %d diff: %d\n",
		l34hsbData->SPA,
		l34hsbData->SPA_EXT,
		l34hsbData->STM_IDX,
		l34hsbData->L2_LEN, l34hsbData->L2_LEN_DIFF);
	rtlglue_printf("gmacChk: %d ethType: 0x%x\n",
		l34hsbData->GMAC_CHK,
		l34hsbData->ETH_TYPE);
	rtlglue_printf("daIdx: %d saIdx: %d dual: %d dualErr: %d gre: %d l2tp: %d\n",
		l34hsbData->DA_IDX,
		l34hsbData->SA_IDX,
		l34hsbData->DUAL_HDR,
		l34hsbData->DUAL_FAIL,
		l34hsbData->GRE,
		l34hsbData->L2TP);
	rtlglue_printf("stagif: %d svid: %d ",
		l34hsbData->STAG_IF,
		l34hsbData->SVLAN_ID);
	rtlglue_printf("ctagif: %d cvid: %d\n",
		l34hsbData->CTAG_IF,
		l34hsbData->CVLAN_ID);
	rtlglue_printf("pppoeif: %d pppoesid: %d\n",
		l34hsbData->PPPOE_IF,
		l34hsbData->PPPOE_SID);

	rtlglue_printf("ip: %d v6: %d tos: 0x%x ttl: %d option: %d ipmf: %d l3cs: %d\n",
		l34hsbData->IP,
		l34hsbData->IPV4_6,
		l34hsbData->TOS,
		l34hsbData->TTL_ST,
		l34hsbData->IP_OPTION,
		l34hsbData->IPMF,
		l34hsbData->L3_CSOK);
	rtlglue_printf("sipv4: %s ", diag_util_inet_ntoa(l34hsbData->SIP_V4));
	rtlglue_printf("dipv4: %s\n", diag_util_inet_ntoa(l34hsbData->DIP_V4));
	rtlglue_printf("siphash: 0x%x ", l34hsbData->SIP_HSH);
	rtlglue_printf("diphash: 0x%x\n", l34hsbData->DIP_HSH);

	rtlglue_printf("l4: %d tcp: %d l4cs: %d sport: %d dport: %d\n",
		l34hsbData->L4_TYPE,
		l34hsbData->L4_PTC,
		l34hsbData->L4_CSOK,
		l34hsbData->SPORT,
		l34hsbData->DPORT);
	if(l34hsbData->L4_TYPE && l34hsbData->L4_PTC)
		rtlglue_printf("tcp flag ack: %d syn: %d rst: %d fin: %d\n", (l34hsbData->TCP_FLAG>>3)&0x1, (l34hsbData->TCP_FLAG>>2)&0x1, (l34hsbData->TCP_FLAG>>1)&0x1, l34hsbData->TCP_FLAG&0x1);
	if(l34hsbData->L4_TYPE && (0==l34hsbData->L4_PTC))
		rtlglue_printf("udp nocs: %d\n", l34hsbData->UDP_NOCS);
	if(l34hsbData->GRE)
		rtlglue_printf("GRE call id: 0x%x, seq: 0x%x\n", l34hsbData->L2TP_SESSION, l34hsbData->GRE_SEQ_NUM);
	if(l34hsbData->L2TP)
		rtlglue_printf("L2TP session id: 0x%x tunnel id: 0x%x\n", l34hsbData->L2TP_SESSION, l34hsbData->L2TP_ID);

//	if(l34hsbData->DUAL_HDR)
	{
		rtlglue_printf("[outer info] (dual: %d)\n", l34hsbData->DUAL_HDR);
		rtlglue_printf("v6: %d tos: 0x%x mf: %d ttl: %d option: %d l3cs: %d \n",
			l34hsbData->IPV6_OUT,
			l34hsbData->OUT_TOS,
			l34hsbData->OUT_IPMF,
			l34hsbData->OUT_TTL_ST,
			l34hsbData->OUT_IP_OPTION,
			l34hsbData->OUT_L3_CSOK);
		rtlglue_printf("l4: %d tcp: %d sport: %d dport: %d l4cs: %d\n",
			l34hsbData->OUT_L4_TYPE,
			l34hsbData->OUT_L4_PTC,
			l34hsbData->OUT_SPORT,
			l34hsbData->OUT_DPORT,
			l34hsbData->OUT_L4_CSOK);
	}

}

void dump_l34hsa_display(rtk_rg_asic_hsa_entry_t *l34hsaData)
{

	rtlglue_printf("act: %s", (l34hsaData->HSA_ACT == FB_ACTION_FORWARD)?"fwd":((l34hsaData->HSA_ACT == FB_ACTION_TRAP2CPU)?"trap":((l34hsaData->HSA_ACT == FB_ACTION_DROP)?"drop":"UNKNOWN")));

	if(l34hsaData->HSA_ACT != FB_ACTION_FORWARD)
	{
		rtlglue_printf("\t rsn: %d\n", l34hsaData->HSA_RSN);

		rtlglue_printf("prien: %d pri: %d\n", l34hsaData->HSA_PRI_EN, l34hsaData->HSA_PRI);
		rtlglue_printf("fbi: %d hashidx: %d\n", l34hsaData->HSA_HID_VLD, l34hsaData->HSA_HID);

	}else if(l34hsaData->HSA_ACT == FB_ACTION_FORWARD)
	{
		rtlglue_printf("\t routing: %d\n", l34hsaData->S1_P5);

		rtlglue_printf("dmact: %d dmacidx: %d\n", l34hsaData->HSA_DMAC_T, l34hsaData->HSA_DMAC_IDX);
		rtlglue_printf("streamact: %d streamidx: %d\n", l34hsaData->HSA_STREAM_ACT, l34hsaData->HSA_STREAM_IDX);
		rtlglue_printf("lutlookup: %d igrnetif: %d\n", l34hsaData->HSA_UC_LUT_LUP, l34hsaData->HSA_I_IF_IDX);
		rtlglue_printf("dual: %d\n", l34hsaData->HSA_HIT_DUAL);
		rtlglue_printf("flowmibact: %d flowmibidx: %d\n", l34hsaData->HSA_FLOW_COUNTER_ACT, l34hsaData->HSA_FLOW_COUNTER_IDX);

		rtlglue_printf("s1 info\n");
		rtlglue_printf("smact: %d extag: %d\n", l34hsaData->S1_SMAC_T, l34hsaData->S1_EX_TAG_IDX);
		rtlglue_printf("pmask: 0x%x extpmask: 0x%x\n", l34hsaData->S1_PMASK, l34hsaData->S1_EXTP_MASK);
		rtlglue_printf("pppact: %d pppsid: %d egrnetif: %d\n", l34hsaData->S1_PP_ACT, l34hsaData->S1_PP_SID, l34hsaData->S1_O_IF_IDX);
		rtlglue_printf("priact: %d pri: %d\n", l34hsaData->S1_USER_PRI_ACT, l34hsaData->S1_USER_PRI);
		rtlglue_printf("dscpact: %d dscp: %d\n", l34hsaData->S1_DSCP_ACT, l34hsaData->S1_DSCP);
		rtlglue_printf("stagact: %d svidact: %d spriact: %d\n", l34hsaData->S1_STAG_ACT, l34hsaData->S1_SVID_ACT, l34hsaData->S1_SPRI_ACT);
		rtlglue_printf("svidegract: %d svid: %d spri: %d\n", l34hsaData->S1_EGS_SVID_ACT, l34hsaData->S1_SVID, l34hsaData->S1_SPRI);
		rtlglue_printf("ctagact: %d cvidact: %d cpriact: %d\n", l34hsaData->S1_CTAG_ACT, l34hsaData->S1_CVID_ACT, l34hsaData->S1_CPRI_ACT);
		rtlglue_printf("cvidegract: %d cvid: %d cpri: %d\n", l34hsaData->S1_EGS_CVID_ACT, l34hsaData->S1_CVID, l34hsaData->S1_CPRI);
		rtlglue_printf("vid2s: %d vid2c: %d\n", l34hsaData->S1_VID2S_ACT, l34hsaData->S1_VID2C_ACT);

		if(l34hsaData->S1_P5){
			rtlglue_printf("l34 info\n");
			rtlglue_printf("l3cs: 0x%x l4cs: 0x%x\n", l34hsaData->S1_L3_CS, l34hsaData->S1_L4_CS);
			rtlglue_printf("l4act: %d l4dir: %d\n", l34hsaData->S1_L4_ACT, l34hsaData->S1_L4_DIR);
			rtlglue_printf("transip: %s transport:%d\n", diag_util_inet_ntoa(l34hsaData->S1_IP), l34hsaData->S1_PORT);
		}
#if 0 		// show all hsa for verification
		if(l34hsaData->S2_ACT)
#else
		else
#endif
		{
			diag_util_printf("s2 info (act: %d)\n", l34hsaData->S2_ACT);
			diag_util_printf("smact: %d\n", l34hsaData->S2_SMAC_T);
			diag_util_printf("pmask: 0x%x extpmask: 0x%x\n", l34hsaData->S2_PMASK, l34hsaData->S2_EXTP_MASK);
			diag_util_printf("pppact: %d pppsid: %d egrnetif: %d\n", l34hsaData->S2_PP_ACT, l34hsaData->S2_PP_SID, l34hsaData->S2_O_IF_IDX);
			diag_util_printf("priact: %d pri: %d\n", l34hsaData->S2_USER_PRI_ACT, l34hsaData->S2_USER_PRI);
			diag_util_printf("dscpact: %d dscp: %d\n", l34hsaData->S2_DSCP_ACT, l34hsaData->S2_DSCP);
			diag_util_printf("stagact: %d svidact: %d spriact: %d\n", l34hsaData->S2_STAG_ACT, l34hsaData->S2_SVID_ACT, l34hsaData->S2_SPRI_ACT);
			diag_util_printf("svidegract: %d svid: %d spri: %d\n", l34hsaData->S2_EGS_SVID_ACT, l34hsaData->S2_SVID, l34hsaData->S2_SPRI);
			diag_util_printf("ctagact: %d cvidact: %d cpriact: %d\n", l34hsaData->S2_CTAG_ACT, l34hsaData->S2_CVID_ACT, l34hsaData->S2_CPRI_ACT);
			diag_util_printf("cvidegract: %d cvid: %d cpri: %d\n", l34hsaData->S2_EGS_CVID_ACT, l34hsaData->S2_CVID, l34hsaData->S2_CPRI);
			diag_util_printf("vid2s: %d vid2c: %d\n", l34hsaData->S2_VID2S_ACT, l34hsaData->S2_VID2C_ACT);
		}
	}
}


int32 dump_l34hs(struct seq_file *s, void *v)
{
	memset((void*)&rawL34Hsb,0,sizeof(rawL34Hsb));
	memset((void*)&rawL34Hsa,0,sizeof(rawL34Hsa));

	ASSERT_EQ(rtk_rg_asic_hsbData_get(&rawL34Hsb), RT_ERR_RG_OK);
	ASSERT_EQ(rtk_rg_asic_hsaData_get(&rawL34Hsa), RT_ERR_RG_OK);
	
	rtlglue_printf("---- "COLOR_Y "[L34HSB:]" COLOR_NM "------------------------------------\n");
	dump_l34hsb_display(&rawL34Hsb);
	PROC_PRINTF("----------------------------------------------\n");
	rtlglue_printf("---- "COLOR_Y "[L34HSA:]" COLOR_NM "------------------------------------\n");
	dump_l34hsa_display(&rawL34Hsa);
	rtlglue_printf("----------------------------------------------\n");

	return SUCCESS;
}


int dump_acl(struct seq_file *s, void *v)
{
	int i,j;
	uint8 debug_once = 0U;
	uint32 val;
	rtk_acl_ingress_entry_t aclRule;
	rtk_acl_template_t aclTemplate;
	rtk_acl_debug_reason_t pDbgReason;
	char* actionString;

	/*H/W ACL debug string*/
	char *name_of_acl_field[]={
		"", "",
		"DMAC0[15:0]", // 2
		"DMAC1[31:16]",
		"DMAC2[47:32]",
		"",
		"SMAC0[15:0]",	//6
		"SMAC1[31:16]",
		"SMAC2[47:32]",
		"ETHERTYPE", //9
		"CTAG", //a
		"STAG", //b
		"GEMIDX/LLIDX", 	//0xc
		"FRAME/TAGS",	//0xd
		"",
		"IP4SIP[15:0]", //0xf
		"IP4SIP[31:16]", //0x10
		"",
		"IP4DIP[15:0]",
		"IP4DIP[31:16]",
		"IP4(TOS+PROTO)", //0x14
		"", //0x15
		"",
		"INNER_IP4SIP[15:0]",//0X17
		"INNER_IP4SIP[31:16]",
		"",
		"INNER_IP4DIP[15:0]", //0x1a
		"INNER_IP4DIP[31:16]",
		"INNER_IP4(TOS+PROTO)", //0X1c
		"",//0x1d
		"IP6SIP[15:0]", //0x1e
		"IP6SIP[31:16]",
		"IP6SIP[47:32]",
		"IP6SIP[63:48]",
		"IP6SIP[79:64]",
		"IP6SIP[95:80]",
		"IP6SIP[111:96]",
		"IP6SIP[127:112]", //0x25
		"",
		"IP6DIP[15:0]", //0x27
		"IP6DIP[31:16]",
		"IP6DIP[47:32]",
		"IP6DIP[63:48]",
		"IP6DIP[79:64]",
		"IP6DIP[95:80]",
		"IP6DIP[111:96]",
		"IP6DIP[127:112]",//0x2e
		"IP6(TC+NH)", //0x2f
		"TCPSPORT",//x0x30
		"TCPDPORT",
		"UDPSPORT",
		"UDPDPORT",
		"VIDRANGE",//x0x34
		"IPRANGE",
		"PORTRANGE",
		"PKTLENRANGE",
		"","","","",//0x38~0x3b
		"EXT_PORT_MASK",
		"FIELD_VALID",//3d
		"FIELD_SEL0",//0x3e
		"FIELD_SEL1",
		"FIELD_SEL2",
		"FIELD_SEL3",
		"FIELD_SEL4",
		"FIELD_SEL5",
		"FIELD_SEL6",
		"FIELD_SEL7",
		"FIELD_SEL8",
		"FIELD_SEL9",
		"FIELD_SEL10",
		"FIELD_SEL11",
		"FIELD_SEL12",
		"FIELD_SEL13",
		"FIELD_SEL14",
		"FIELD_SEL15"
	};

	i=0;j=0;val=0;

	PROC_PRINTF("+++ wanPortMask = 0x%llx, lanPortMask = 0x%llx +++\n", fc_db.wanPortMask.portmask, fc_db.lanPortMask.portmask);
	if(fc_db.hypridPPTP.portmask)
		PROC_PRINTF("+++ hyprid pptp pmsk = 0x%llx +++\n", fc_db.hypridPPTP.portmask);
	if(fc_db.aclAndCfReservedRule.hybrid_pptp_func.intf_count){
		PROC_PRINTF("+++ intf count = %d, ", fc_db.aclAndCfReservedRule.hybrid_pptp_func.intf_count);
		for(i=0; i<fc_db.aclAndCfReservedRule.hybrid_pptp_func.intf_count; i++){
			PROC_PRINTF("MAC[%d] %pM, ", i, &fc_db.aclAndCfReservedRule.hybrid_pptp_func.gateway_mac_addr[i].octet[0]);
		}
		PROC_PRINTF(" +++\n");
	}

	PROC_PRINTF("--------------- ACL TABLES ----------------\n");
	for(i=0; i<MAX_ACL_ENTRY_SIZE; i++)
	{
		memset(&aclRule,0,sizeof(aclRule));
		aclRule.index=i;
		ASSERT_EQ(rtk_acl_igrRuleEntry_get(&aclRule),RT_ERR_OK);
		if(aclRule.valid == ENABLED)
		{
			int tag_care=0;
			PROC_PRINTF("  --- ACL TABLE[%d] ---\n",i);
			PROC_PRINTF("\tvalid:%x\n",aclRule.valid);

			for(j=0; j<8; j++)
			{
				if(aclRule.readField.fieldRaw[j].mask)
				{
					PROC_PRINTF("\tfield[%d]:0x%04x  mask[%d]:0x%04x\t",j,aclRule.readField.fieldRaw[j].value,j,aclRule.readField.fieldRaw[j].mask);
					memset(&aclTemplate,0,sizeof(aclTemplate));
					aclTemplate.index=aclRule.templateIdx;
					ASSERT_EQ(rtk_acl_template_get(&aclTemplate),RT_ERR_OK);
					PROC_PRINTF("{0x%02x:%s}\n",aclTemplate.fieldType[j],name_of_acl_field[aclTemplate.fieldType[j]]);
					if((aclTemplate.fieldType[j] == ACL_FIELD_FRAME_TYPE_TAGS) && (debug_once == 0)){
						PROC_PRINTF("\t(bit 2:8863/8864,4:8864,5:outterV4,6:v6,7:innerV4,9:udp,10:tcp,11:igmp/mld,15:l2tp)\n");
						debug_once = 1U;
					}
				}
			}

			PROC_PRINTF("\tactive portmask:0x%x\n",aclRule.activePorts.bits[0]);

			if(aclRule.careTag.tags[2].mask) tag_care|=(1<<2);
			if(aclRule.careTag.tags[1].mask) tag_care|=(1<<1);
			if(tag_care)
			{
				PROC_PRINTF("\ttag_care:");
				if(tag_care&(1<<2))PROC_PRINTF("%s",aclRule.careTag.tags[2].value?"[Stag:O]":"[Stag:X]");
				if(tag_care&(1<<1))PROC_PRINTF("%s",aclRule.careTag.tags[1].value?"[Ctag:O]":"[Ctag:X]");
				PROC_PRINTF("\n");
			}

			PROC_PRINTF("\ttemplateIdx:%x\n",aclRule.templateIdx);

			{
				PROC_PRINTF("\taction bits:");

				if(aclRule.act.enableAct[5] == ENABLED) PROC_PRINTF("[INT/CF]");
				if(aclRule.act.enableAct[4] == ENABLED) PROC_PRINTF("[FWD]");
				if(aclRule.act.enableAct[3] == ENABLED) PROC_PRINTF("[POLICY/LOG]");
				if(aclRule.act.enableAct[2] == ENABLED) PROC_PRINTF("[PRI]");
				if(aclRule.act.enableAct[1] == ENABLED) PROC_PRINTF("[SVLAN]");
				if(aclRule.act.enableAct[0] == ENABLED) PROC_PRINTF("[CVLAN]");
				PROC_PRINTF("\n");

				if(aclRule.act.enableAct[ACL_IGR_CVLAN_ACT] == ENABLED)
				{
					switch(aclRule.act.cvlanAct.act)
					{
						case ACL_IGR_CVLAN_IGR_CVLAN_ACT: actionString="Ingress CVLAN action"; break;
						case ACL_IGR_CVLAN_EGR_CVLAN_ACT: actionString="Egress CVLAN action";break;
						case ACL_IGR_CVLAN_DS_SVID_ACT: actionString="Using SVID";break;
						case ACL_IGR_CVLAN_POLICING_ACT: actionString="Policing";break;
						case ACL_IGR_CVLAN_MIB_ACT: actionString="Logging";break;
						case ACL_IGR_CVLAN_1P_REMARK_ACT: actionString="1P remark";break;
						case ACL_IGR_CVLAN_BW_METER_ACT: actionString="Bandwidth Metering"; break;
						default: actionString="unKnown action"; break;
					}
					if(aclRule.act.cvlanAct.act == ACL_IGR_CVLAN_MIB_ACT)
						PROC_PRINTF("\t[CVLAN_ACTIDX:%x(%s)] mib:%d\n",aclRule.act.cvlanAct.act,actionString,aclRule.act.cvlanAct.mib);
					else
						PROC_PRINTF("\t[CVLAN_ACTIDX:%x(%s)] cvid:%d dot1p:%x\n",aclRule.act.cvlanAct.act,actionString,aclRule.act.cvlanAct.cvid,aclRule.act.cvlanAct.dot1p);
				}

				if(aclRule.act.enableAct[ACL_IGR_SVLAN_ACT] == ENABLED)
				{
					switch(aclRule.act.svlanAct.act)
					{
						case ACL_IGR_SVLAN_IGR_SVLAN_ACT: actionString="Ingress SVLAN action"; break;
						case ACL_IGR_SVLAN_EGR_SVLAN_ACT:  actionString="Egress SVLAN action"; break;
						case ACL_IGR_SVLAN_US_CVID_ACT:  actionString="Using CVID"; break;
						case ACL_IGR_SVLAN_POLICING_ACT:  actionString="Policing"; break;
						case ACL_IGR_SVLAN_MIB_ACT: actionString="Logging";break;
						case ACL_IGR_SVLAN_1P_REMARK_ACT:  actionString="1P remark"; break;
						case ACL_IGR_SVLAN_DSCP_REMARK_ACT:
							actionString="DSCP remark";
							//pPktHdr->egressDSCPRemarking = ENABLED_DSCP_REMARK_AND_SRC_FROM_ACL;
							//pPktHdr->egressDSCP = pPktHdr->aclDecision.action_dscp_remarking_pri;
							break;
						case ACL_IGR_SVLAN_BW_METER_ACT:  actionString="Bandwidth Metering"; break;
						default: actionString="unKnown action"; break;
					}
					PROC_PRINTF("\t[SVLAN_ACTIDX:%x(%s)] svid:%d dot1p:%x dscp:%d nexthop:%x\n",aclRule.act.svlanAct.act,actionString,aclRule.act.svlanAct.svid,aclRule.act.svlanAct.dot1p,aclRule.act.svlanAct.dscp,aclRule.act.svlanAct.nexthop);
				}

				if(aclRule.act.enableAct[ACL_IGR_FORWARD_ACT] == ENABLED)
				{
					switch(aclRule.act.forwardAct.act)
					{
						case ACL_IGR_FORWARD_EGRESSMASK_ACT: actionString="Forward frame with ACLPMSK only (& filtering)"; break;
						case ACL_IGR_FORWARD_REDIRECT_ACT: actionString="Redirect frame with ACLPMSK"; break;
						case ACL_IGR_FORWARD_IGR_MIRROR_ACT: actionString="Ingress mirror to ACLPMSK"; break;
						case ACL_IGR_FORWARD_TRAP_ACT: actionString="Trap to CPU"; break;
						case ACL_IGR_FORWARD_TRAP2SLAVE_ACT: actionString="Trap to SLAVECPU"; break;
						default: actionString="unKnown action"; break;
					}

					PROC_PRINTF("\t[FWD_ACTIDX:%x(%s)] portMask:0x%x\n",aclRule.act.forwardAct.act,actionString,aclRule.act.forwardAct.portMask.bits[0]);
				}

				if(aclRule.act.enableAct[ACL_IGR_PRI_ACT] == ENABLED)
				{
					switch(aclRule.act.priAct.act)
					{
						case ACL_IGR_PRI_ACL_PRI_ASSIGN_ACT: actionString="ACL Priority";break;
						case ACL_IGR_PRI_DSCP_REMARK_ACT:
							actionString="DSCP Remarking";
							//pPktHdr->egressDSCPRemarking = ENABLED_DSCP_REMARK_AND_SRC_FROM_ACL;
							//pPktHdr->egressDSCP = pPktHdr->aclDecision.action_dscp_remarking_pri;
							break;
						case ACL_IGR_PRI_1P_REMARK_ACT: actionString="1P Remarking";break;
						case ACL_IGR_PRI_POLICING_ACT: actionString="Policing";break;
						case ACL_IGR_PRI_MIB_ACT: actionString="Logging";break;
						case ACL_IGR_PRI_BW_METER_ACT: actionString="Bandwidth Metering";break;
						case ACL_IGR_PRI_TOS_REMARK_ACT: actionString="ToS remarking";break;
						default: actionString="unKnown action"; break;
					}
					PROC_PRINTF("\t[PRI_ACTIDX:%x(%s)] aclPri:%x dot1p:%x dscp:%d Tos:%d\n",aclRule.act.priAct.act,actionString,aclRule.act.priAct.aclPri,aclRule.act.priAct.dot1p,aclRule.act.priAct.dscp,aclRule.act.priAct.tos);
				}

				if(aclRule.act.enableAct[ACL_IGR_LOG_ACT] == ENABLED)
				{
					switch(aclRule.act.logAct.act)
					{
						case ACL_IGR_LOG_POLICING_ACT: actionString="Policing"; break;
						case ACL_IGR_LOG_MIB_ACT: actionString="Logging"; break;
						case ACL_IGR_LOG_BW_METER_ACT: actionString="Bandwidth Metering"; break;
						case ACL_IGR_LOG_1P_REMARK_ACT: actionString="1P remark"; break;
						default: actionString="unKnown action"; break;
					}
					PROC_PRINTF("\t[POLICY/LOG_ACTIDX:%x(%s)] meteridx:%d\n",aclRule.act.logAct.act,actionString, aclRule.act.logAct.act==ACL_IGR_LOG_MIB_ACT? aclRule.act.logAct.mib:aclRule.act.logAct.meter);
				}

				if(aclRule.act.enableAct[ACL_IGR_INTR_ACT] == ENABLED)
				{
					switch(aclRule.act.extendAct.act)
					{
						case ACL_IGR_EXTEND_NONE_ACT: actionString="None"; break;
						case ACL_IGR_EXTEND_SID_ACT: actionString="Stream ID assign"; break;
						case ACL_IGR_EXTEND_LLID_ACT: actionString="LLID"; break;
						case ACL_IGR_EXTEND_EXT_ACT: actionString="Ext Act(not support in apolloFE)"; break;
						case ACL_IGR_EXTEND_1P_REMARK_ACT: actionString="1P Remarking"; break;
						default: actionString="unKnown action"; break;
					}
					PROC_PRINTF("\t[INT/CF_ACTIDX:%x(%s)] CFHITLATCH:%x INT:%x index(stream_id or llid):0x%x, pmask:0x%x\n",aclRule.act.extendAct.act,actionString,aclRule.act.aclLatch, aclRule.act.aclInterrupt,aclRule.act.extendAct.index,aclRule.act.extendAct.portMask.bits[0]);
				}

			}

	   }
	}


	//use the ASIC API
	PROC_PRINTF("--------------- ACL HIT OINFO----------------\n");
	memset(&pDbgReason,0,sizeof(pDbgReason));
	ASSERT_EQ(rtk_acl_dbgHitReason_get(&pDbgReason),RT_ERR_OK);

	PROC_PRINTF("[CACT:%s]: hit rule %d\n", (pDbgReason.hitAct[ACL_IGR_CVLAN_ACT] == ENABLED) ?"O":"X",pDbgReason.index[ACL_IGR_CVLAN_ACT]);
	PROC_PRINTF("[SACT:%s]: hit rule %d\n", (pDbgReason.hitAct[ACL_IGR_SVLAN_ACT] == ENABLED) ?"O":"X",pDbgReason.index[ACL_IGR_SVLAN_ACT]);
	PROC_PRINTF("[PRI:%s]: hit rule %d\n", (pDbgReason.hitAct[ACL_IGR_PRI_ACT] == ENABLED) ?"O":"X",pDbgReason.index[ACL_IGR_PRI_ACT]);
	PROC_PRINTF("[POLICE:%s]: hit rule %d\n", (pDbgReason.hitAct[ACL_IGR_LOG_ACT] == ENABLED) ?"O":"X",pDbgReason.index[ACL_IGR_LOG_ACT]);
	PROC_PRINTF("[FWD:%s]: hit rule %d\n", (pDbgReason.hitAct[ACL_IGR_FORWARD_ACT] == ENABLED) ?"O":"X",pDbgReason.index[ACL_IGR_FORWARD_ACT]);
	PROC_PRINTF("[INT:%s]: hit rule %d\n", (pDbgReason.hitAct[ACL_IGR_INTR_ACT] == ENABLED) ?"O":"X",pDbgReason.index[ACL_IGR_INTR_ACT]);
	return SUCCESS;
}

int dump_acl_by_index(struct file *file, const char *buffer, unsigned long len, void *data)
{
	rtlglue_printf("Not support.\n");
	return SUCCESS;
}

int dump_acl_range_table(struct seq_file *s, void *v)
{
    int i;
    rtk_acl_rangeCheck_ip_t ipRangeEntry;
    rtk_acl_rangeCheck_l4Port_t portRangeEntry;
	rtk_acl_rangeCheck_pktLength_t pktLengthRangeEntry;

	char *name_of_acl_iprange_type[]={
		"UNUSED",
		"IPV4_SIP",
		"IPV4_DIP",
		"IPV6_SIP",
		"IPV6_DIP",
		"IPV4_SIP_INNER",
		"IPV4_DIP_INNER",
	};
	char *name_of_acl_portrange_type[]={
		"UNUSED",
		"SPORT",
		"DPORT",
	};

    PROC_PRINTF("------------ ACL IP RANGE USED TABLES [SIZE: %d] -------------\n", MAX_ACL_IPRANGETABLE_SIZE);
    for(i=0; i<MAX_ACL_IPRANGETABLE_SIZE; i++)
    {
        memset(&ipRangeEntry,0,sizeof(ipRangeEntry));
        ipRangeEntry.index=i;
        if(rtk_acl_ipRange_get(&ipRangeEntry))
			continue;
		if(ipRangeEntry.type!=IPRANGE_UNUSED)
	        PROC_PRINTF("\tIPRANGE[%d] upper:0x%x lower:0x%x type:%s\n",i,ipRangeEntry.upperIp,ipRangeEntry.lowerIp,name_of_acl_iprange_type[ipRangeEntry.type]);
    }

    PROC_PRINTF("------------ ACL PORT RANGE USED TABLES [SIZE: %d] -------------\n", MAX_ACL_PORTRANGETABLE_SIZE);
    for(i=0; i<MAX_ACL_PORTRANGETABLE_SIZE; i++)
    {
        memset(&portRangeEntry,0,sizeof(portRangeEntry));
        portRangeEntry.index=i;
        if(rtk_acl_portRange_get(&portRangeEntry))
			continue;
		if(portRangeEntry.type!=PORTRANGE_UNUSED)
	        PROC_PRINTF("\tPORTRANGE[%d] upper:%d lower:%d type:%s\n",i,portRangeEntry.upper_bound,portRangeEntry.lower_bound,name_of_acl_portrange_type[portRangeEntry.type]);
    }

    PROC_PRINTF("------------ ACL PACKET LENGTH RANGE USED TABLES [SIZE: %d] -------------\n", MAX_ACL_PACKETRANGETABLE_SIZE);
    for(i=0; i<MAX_ACL_PACKETRANGETABLE_SIZE; i++)
    {
        memset(&pktLengthRangeEntry,0,sizeof(pktLengthRangeEntry));
        pktLengthRangeEntry.index=i;
        if(rtk_acl_packetLengthRange_get(&pktLengthRangeEntry))
			continue;
		if(pktLengthRangeEntry.upper_bound||pktLengthRangeEntry.lower_bound)
	        PROC_PRINTF("\tPKTLENRANGE[%d] upper:%d lower:%d reverse_opt:%d\n",i,pktLengthRangeEntry.upper_bound,pktLengthRangeEntry.lower_bound,pktLengthRangeEntry.type);
    }

	return SUCCESS;
}


int tracefilterRULE0_hs_period_time=0;
int initTimer=0;
rtk_fc_timer_list_t *dumphs_timer = NULL;

int _pasring_proc_string_to_integer(const char *buff,unsigned long len)
{
	unsigned char tmpbuf[256] = {0};
	unsigned int count = (len >= 255U) ? 255U : len;
	int ret;

	if (buff)
	{
		/* copy data to the buffer */
		strscpy(tmpbuf, buff, count);
	}
	ret=simple_strtol(tmpbuf, NULL, 0);

	return ret;
}

void tracefilterRULE0_dump_hs(unsigned long data )
{
	int ret=0,value=0;
	unsigned long int msec2jiffies=0UL;
		
	rtk_rg_asic_hsb_entry_t hsb_r;
	rtk_rg_asic_hsa_entry_t hsa_r;
	char acl_dbg_info[64]={0};


	if(tracefilterRULE0_hs_period_time==0)
	{
		RTK_FC_HELPER_DEL_TIMER(dumphs_timer);
		initTimer=0;
		return ;
	}


	if(fc_db.traceFilterRuleMask&0x1U)
	{

		if(fc_db.trace_filter_bitmask[0]==FC_DEBUG_TRACE_FILTER_SHOWNUMBEROFTIMES || fc_db.trace_filter_bitmask[0]==0)
			goto NEXTTIMER;

		memset((void*)&rawHsb,0,sizeof(rawHsb));
		memset((void*)&rawHsa,0,sizeof(rawHsa));
		memset((void*)&rawHsd,0,sizeof(rawHsd));
		memset(&hsb_r,0,sizeof(hsb_r));
		memset(&hsa_r,0,sizeof(hsa_r));

		//disable hsa latch ,stop hab update (config as non-latch mode)
		rtk_rg_asic_l2HsbaLatchMode_set(DISABLED);
#if defined(CONFIG_FC_RTL9607C_RTL9603CVD_HYBRID)
		if(CHIP_ID_9607C) {
			ret = rtl9607c_hsbData_get(&rawHsb);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

			ret = rtl9607c_hsbPar_get(&rawHsb);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

			ret = rtl9607c_hsaData_get(&rawHsa);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

			ret = rtl9607c_hsdData_get(&rawHsd);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}
			
			if(rawHsa.dbghsa_oq_fb){ // via FB
				ret = rtl9607c_fbData_get(&rawHsa);
				if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}
				value = 1;
			}
		}else {
			ret = rtl9603cvd_hsbData_get(&rawHsb);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

			ret = rtl9603cvd_hsbPar_get(&rawHsb);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

			ret = rtl9603cvd_hsaData_get(&rawHsa);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

			ret = rtl9603cvd_hsdData_get(&rawHsd);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}
			
			if(rawHsa.dbghsa_oq_fb){ // via FB
				ret = rtl9603cvd_fbData_get(&rawHsa);
				if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}
				value = 1;
			}
		}
		
#elif defined(CONFIG_FC_RTL9607C_SERIES)
		ret = rtl9607c_hsbData_get(&rawHsb);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

		ret = rtl9607c_hsbPar_get(&rawHsb);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

		ret = rtl9607c_hsaData_get(&rawHsa);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

		ret = rtl9607c_hsdData_get(&rawHsd);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}
		
		if(rawHsa.dbghsa_oq_fb){ // via FB
			ret = rtl9607c_fbData_get(&rawHsa);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}
			value = 1;
		}
#elif defined(CONFIG_FC_RTL9603CVD_SERIES) 
		ret = rtl9603cvd_hsbData_get(&rawHsb);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

		ret = rtl9603cvd_hsbPar_get(&rawHsb);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

		ret = rtl9603cvd_hsaData_get(&rawHsa);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

		ret = rtl9603cvd_hsdData_get(&rawHsd);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}
		
		if(rawHsa.dbghsa_oq_fb){ // via FB
			ret = rtl9603cvd_fbData_get(&rawHsa);
			if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}
			value = 1;
		}
#endif

		if((rawHsb.bgdsc != rawHsa.dbghsa_bgdsc) || (rawHsb.endsc != rawHsa.dbghsa_oq_endsc))
		{
			rtlglue_printf("HSB/HSA not match! please wait next timer expired ...\n");
			goto NEXTTIMER;
		}

		ret = rtk_rg_asic_hsbData_get(&hsb_r);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}
		ret = rtk_rg_asic_hsaData_get(&hsa_r);
		if(ret!=RT_ERR_OK) {rtlglue_printf("ret failed %s:%d\n",__FUNCTION__, __LINE__); goto NEXTTIMER;}

		//enable hab update
		rtk_rg_asic_l2HsbaLatchMode_set(ENABLED);


		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_SPA)
		{
				if(fc_db.trace_filter[0].spa!=rawHsb.spa) goto NEXTTIMER;

		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_DA)
		{
			if(memcmp(&fc_db.trace_filter[0].dmac.octet[0],&rawHsb.da.octet[0],6)!=0) goto NEXTTIMER;
		}

		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_SA)
		{
			if(memcmp(&fc_db.trace_filter[0].smac.octet[0],&rawHsb.sa.octet[0],6)!=0) goto NEXTTIMER;
		}

		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_SIP )
		{
			if(fc_db.trace_filter[0].sip!=rawHsb.sip)
				goto NEXTTIMER;
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_DIP )
		{
			if(fc_db.trace_filter[0].dip!=rawHsb.dip)
				goto NEXTTIMER;
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_V6SIP)
		{

			if(memcmp(&fc_db.trace_filter[0].sipv6[12],&rawHsb.sip,4)!=0)
				goto NEXTTIMER;
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_V6DIP)
		{
			if(memcmp(&fc_db.trace_filter[0].dipv6[12],&rawHsb.dip,4)!=0)
				goto NEXTTIMER;
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_CVLAN )
		{
			if(rawHsb.ctag_if)
			{
				if( fc_db.trace_filter[0].cvlanid != rawHsb.ctag)
					goto NEXTTIMER;
			}else
				goto NEXTTIMER;
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_SVLAN )
		{
			if(rawHsb.stag_if)
			{
				if( fc_db.trace_filter[0].svlanid != rawHsb.stag)
					goto NEXTTIMER;
			}else
				goto NEXTTIMER;
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_PPPOESESSIONID )
		{
			if(rawHsb.pppoe_if)
			{
				if( fc_db.trace_filter[0].sessionid!= rawHsb.pppoe_session)
					goto NEXTTIMER;
			}else
				goto NEXTTIMER;
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_ETH )
		{
			if( fc_db.trace_filter[0].ethertype!= rawHsb.ether_type)
				goto NEXTTIMER;
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_REASON )
		{
			if( fc_db.trace_filter[0].reason!= rawHsa.dbghsa_epcom_fwdrsn)
				goto NEXTTIMER;
		}



		rtlglue_printf("---- "COLOR_Y "[HSB:]" COLOR_NM "------------------------------------\n");
		_diag_debug_hsb_display(&rawHsb);
		rtlglue_printf("----------------------------------------------\n");
		memset(acl_dbg_info, 0, sizeof(acl_dbg_info));
		if(rawHsa.dbghsa_epcom_fwdrsn == CPU_REASON_ACL)
			_rtk_fc_aclHeaderInfo_get(acl_dbg_info, sizeof(acl_dbg_info), (rawHsa.dbghsa_grp0_cvlan_info&RXINFO_REF_ACL_RSN_BIT));
		rtlglue_printf("---- "COLOR_Y "[HSA: (by %s)]" COLOR_NM COLOR_G "%s" COLOR_NM "------------------------------------\n", value?"FB":"switch", acl_dbg_info);
		_diag_debug_hsa_display(&rawHsa);
		rtlglue_printf("----------------------------------------------\n");
		rtlglue_printf("---- "COLOR_Y "[HSD:]" COLOR_NM "------------------------------------\n");
		_diag_debug_hsd_display(&rawHsd);
		rtlglue_printf("----------------------------------------------\n\n");

		rtlglue_printf("---- "COLOR_Y "[L4HSB:]" COLOR_NM "------------------------------------\n");
		dump_l34hsb_display(&hsb_r);
		rtlglue_printf("----------------------------------------------\n");
		rtlglue_printf("---- "COLOR_Y "[L4HSA:]" COLOR_NM "------------------------------------\n");
		dump_l34hsa_display(&hsa_r);
		rtlglue_printf("----------------------------------------------\n");



		RTK_FC_HELPER_DEL_TIMER(dumphs_timer);
		initTimer=0;

		return ;

	}

NEXTTIMER:

	//enable hab update
	rtk_rg_asic_l2HsbaLatchMode_set(ENABLED);
	RTK_FC_HELPER_MSECS_TO_JIFFIES(tracefilterRULE0_hs_period_time, &msec2jiffies);
	RTK_FC_HELPER_MOD_TIMER(dumphs_timer,jiffies+msec2jiffies);

	return ;

}

int tracefilterRULE0_dump_hs_timer( struct file *filp, const char *buff,unsigned long len, void *data )
{

	unsigned long int msec2jiffies=0UL;
		
	tracefilterRULE0_hs_period_time=_pasring_proc_string_to_integer(buff,len);
	if(tracefilterRULE0_hs_period_time<0)
		tracefilterRULE0_hs_period_time=1;

	rtlglue_printf("SUPPORT tracefilterRULE0  SPA ETHTYPE CVLAN SVLAN PPPOESSID DA/48 SA/48 DIP/32 SIP/32 DIP6/32 SIP6/32 \n");

	if(tracefilterRULE0_hs_period_time==0)
	{
		rtlglue_printf("disable get dump hs period\n");
		return len;
	}
	else
	{
		rtlglue_printf("dumpHs period timer=%d (ms)\n",tracefilterRULE0_hs_period_time);
		if(initTimer==0){
			dumphs_timer = RTK_FC_HELPER_MGR_TIMER_LIST_KMALLOC();
			RTK_FC_HELPER_SETUP_TIMER(dumphs_timer,tracefilterRULE0_dump_hs,0);
			RTK_FC_HELPER_MSECS_TO_JIFFIES(tracefilterRULE0_hs_period_time, &msec2jiffies);
			RTK_FC_HELPER_MOD_TIMER(dumphs_timer,jiffies+msec2jiffies);
			initTimer=1;
			rtlglue_printf("TIMER INIT SUCCESS\n");
		}
	}

	return len;
}




int32 _display_flowEntry_byIdx(struct seq_file *s, uint32 idx,int filter)
{
	uint32 maxTableSize=0U;
	rtk_rg_asic_fbMode_t fbMode = _rtk_rg_fbMode_get();
	rtk_rg_asic_path1_entry_t p1data;
	rtk_rg_asic_path1_entry_t *pP1Data = &p1data;
	rtk_rg_asic_camTag_entry_t camTag;
	int dumpThisEntry=0;

	maxTableSize = (RTK_FC_TABLESIZE_FLOWSRAM+RTK_FC_TABLESIZE_FLOWTCAM);

	if (idx<maxTableSize)
	{
		// 4K mode, we check valid bit saved in rgprodb.
		// DDR mode, we check valid bit by reading flow entry saved in sram directly. (we didn't maintain validbit in DDR mode because sram entries are controlled by cache controller.)
		if( (fbMode!= FB_MODE_4K) || ((fbMode== FB_MODE_4K) && (_rtk_rg_flowEntryValidBit_get(idx) == TRUE)))
		{
			if((fbMode== FB_MODE_4K) || ((fbMode!= FB_MODE_4K) && (idx < RTK_FC_TABLESIZE_FLOWSRAM)))
			{
				// Force to read SRAM flow entries
				rtk_rg_asic_sramFlowEntry_get(idx, (void*)pP1Data);
			}else
			{
				// DRAM mode, read cam entries
				rtk_rg_asic_camTagTable_get(idx-RTK_FC_TABLESIZE_FLOWSRAM, &camTag);
				if(!camTag.valid)
				{
					return 0;
				}
				rtk_rg_asic_flowPath1_get(camTag.hsahIdx, pP1Data);
			}

			//if(!pP1Data->valid) continue;
			if(!pP1Data->valid)
			{
				return 0;
			}
			if(pP1Data->in_path == 0U)
			{
				if(pP1Data->in_multiple_act == 0U)
				{
					dump_flow_p1Rawdata(s, idx, (void*)pP1Data);
					dumpThisEntry=1;
				}
				else
				{
					dump_flow_p2Rawdata(s, idx, (void*)pP1Data);
					dumpThisEntry=1;
				}
			}else if (pP1Data->in_path == 1U)
			{
				if(pP1Data->in_multiple_act == 0U)
				{
					rtk_rg_asic_path3_entry_t *p3Data = (rtk_rg_asic_path3_entry_t *)pP1Data;
					if((filter==-1)||(filter==p3Data->in_l4_src_port)||(filter==p3Data->in_l4_dst_port))
					{
						dump_flow_p3Rawdata(s, idx, (void*)pP1Data);
						dumpThisEntry=1;
					}
				}
				else
				{
					rtk_rg_asic_path4_entry_t *p4Data = (rtk_rg_asic_path4_entry_t *)pP1Data;
					if((filter==-1)||(filter==p4Data->in_l4_src_port)||(filter==p4Data->in_l4_dst_port))
					{
						dump_flow_p4Rawdata(s, idx, (void*)pP1Data);
						dumpThisEntry=1;
					}
				}
			}else if (pP1Data->in_path == 2U)
			{
					rtk_rg_asic_path5_entry_t *p5Data = (rtk_rg_asic_path5_entry_t *)pP1Data;
					if((filter==-1)||(filter==p5Data->out_l4_port)||(filter==p5Data->in_l4_src_port)||(filter==p5Data->in_l4_dst_port))
					{
						dump_flow_p5Rawdata(s, idx, (void*)pP1Data);
						dumpThisEntry=1;
					}

			}else if (pP1Data->in_path == 3U)
			{
					dump_flow_p6Rawdata(s, idx, (void*)pP1Data);
			}

			if((fbMode!= FB_MODE_4K)&&(dumpThisEntry==1))
			{
				if(idx < RTK_FC_TABLESIZE_FLOWSRAM)
				{
					// SRAM cached flow
					rtk_rg_asic_flowTag_entry_t flowTag;
					bzero(&flowTag, sizeof(flowTag));
					rtk_rg_asic_flowTagTable_get(idx, &flowTag);

					PROC_PRINTF(">>flowtag - msb:%d\tttl:%d DDR_IDX:%d\n\n", flowTag.hashIdxMsb, flowTag.TTL,(flowTag.hashIdxMsb<<12)|idx);
				}else
				{
					// CAM cached flow
					rtk_rg_asic_camTag_entry_t camTag;
					rtk_rg_asic_camTagTable_get(idx-RTK_FC_TABLESIZE_FLOWSRAM, &camTag);

					PROC_PRINTF(">>camtag - entryIdx:%d\tlock:%d\n\n", camTag.hsahIdx, camTag.lock);
				}
			}
		}
	}


	return 0;

}

int32 dump_flow_table(struct seq_file *s, void *v)
{
	uint32 idx=0U, maxTableSize=0U;
	rtk_rg_err_code_t retval=0;
	
	maxTableSize = (RTK_FC_TABLESIZE_FLOWSRAM+RTK_FC_TABLESIZE_FLOWTCAM);
	PROC_PRINTF(">>ASIC Flow (SRAM) Table:\n");
	for(idx=0; idx<maxTableSize; idx++)
	{
		retval = _display_flowEntry_byIdx(s, idx,-1);
	}
	PROC_PRINTF("----------------------------------------------\n");

	return 0;
}


int32 dump_flow_table_by_filter(struct file *file, const char *buffer, unsigned long len, void *data)
{
	uint32 idx=0U, maxTableSize=0U;
	rtk_rg_err_code_t retval=0;
	int port=_rtk_fc_proc_parsing_string_to_integer(buffer,len);

	maxTableSize = (RTK_FC_TABLESIZE_FLOWSRAM+RTK_FC_TABLESIZE_FLOWTCAM);
	rtlglue_printf(">>ASIC Flow (SRAM) Table:\n");
	for(idx=0; idx<maxTableSize; idx++)
	{
		retval = _display_flowEntry_byIdx(NULL, idx,port);
	}
	rtlglue_printf("----------------------------------------------\n");


	return len;
}

int32 dump_flowdram_table(struct seq_file *s, void *v)
{
	uint32 idx=0U, entryNum= _rtk_rg_flowEntryNum_get();
	rtk_rg_err_code_t retval=0;
	rtk_rg_asic_fbMode_t fbMode = _rtk_rg_fbMode_get();
	rtk_rg_asic_path1_entry_t *pP1Data = NULL;

	if(fbMode == FB_MODE_4K)
	{
		PROC_PRINTF("Not Support in 4K mode.\n");
		return SUCCESS;
	}

	pP1Data = RTK_FC_HELPER_FC_KMALLOC(sizeof(rtk_rg_asic_path1_entry_t), GFP_ATOMIC);
	

	PROC_PRINTF(">>Main Memory Flow Table:\n");
	for(idx=0; idx<entryNum; idx++)
	{
		// Read DDR flow entries
		retval = rtk_rg_asic_flowPath1_get(idx, pP1Data);

		if(!pP1Data->valid) continue;

		dump_flow_by_rawdata(s, idx, (void *)pP1Data);
	}
	PROC_PRINTF("----------------------------------------------\n");

	RTK_FC_HELPER_FC_KFREE(pP1Data,sizeof(rtk_rg_asic_path1_entry_t));

	return retval;
}

int dump_flowdram_table_by_filter(struct file *file, const char *buffer, unsigned long len, void *data)
{
	int port=_rtk_fc_proc_parsing_string_to_integer(buffer,len);
	uint32 idx=0U, entryNum= _rtk_rg_flowEntryNum_get();
	rtk_rg_err_code_t retval=0;
	rtk_rg_asic_fbMode_t fbMode = _rtk_rg_fbMode_get();
	rtk_rg_asic_path1_entry_t *pP1Data = NULL;

	if(fbMode == FB_MODE_4K)
	{
		rtlglue_printf("Not Support in 4K mode.\n");
		return SUCCESS;
	}

	pP1Data = RTK_FC_HELPER_FC_KMALLOC(sizeof(rtk_rg_asic_path1_entry_t), GFP_ATOMIC);
	

	rtlglue_printf(">>Main Memory Flow Table:\n");
	for(idx=0; idx<entryNum; idx++)
	{
		// Read DDR flow entries
		retval = rtk_rg_asic_flowPath1_get(idx, pP1Data);

		if(!pP1Data->valid) continue;

		if(pP1Data->in_path == 0U)
		{
			if(pP1Data->in_multiple_act == 0U)
				dump_flow_p1Rawdata(NULL, idx, (void*)pP1Data);
			else
				dump_flow_p2Rawdata(NULL, idx, (void*)pP1Data);
		}else if (pP1Data->in_path == 1U)
		{
			if(pP1Data->in_multiple_act == 0U)
			{
				rtk_rg_asic_path3_entry_t *p3Data = (rtk_rg_asic_path3_entry_t *)pP1Data;
				if((port==p3Data->in_l4_src_port)||(port==p3Data->in_l4_dst_port))
				{
					dump_flow_p3Rawdata(NULL, idx, (void*)pP1Data);
				}
			}
			else
			{
				rtk_rg_asic_path4_entry_t *p4Data = (rtk_rg_asic_path4_entry_t *)pP1Data;
				if((port==p4Data->in_l4_src_port)||(port==p4Data->in_l4_dst_port))
				{
					dump_flow_p4Rawdata(NULL, idx, (void*)pP1Data);
				}
			}
		}else if (pP1Data->in_path == 2U)
		{
			rtk_rg_asic_path5_entry_t *p5Data = (rtk_rg_asic_path5_entry_t *)pP1Data;
			if((port==p5Data->out_l4_port)||(port==p5Data->in_l4_src_port)||(port==p5Data->in_l4_dst_port))
			{
				dump_flow_p5Rawdata(NULL, idx, (void*)pP1Data);
			}
		}else if (pP1Data->in_path == 3U)
		{
			dump_flow_p6Rawdata(NULL, idx, (void*)pP1Data);
		}
	}
	rtlglue_printf("----------------------------------------------\n");

	RTK_FC_HELPER_FC_KFREE(pP1Data, sizeof(rtk_rg_asic_path1_entry_t));
	return len;


}

int dump_sw_flowTcam_list(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	int i;
	rtk_fc_flowTcam_linkList_t *pFlowEntry;

	if(_rtk_rg_fbMode_get()!=FB_MODE_4K)
	{
		PROC_PRINTF("Not support\n");
		return retval;
	}

	PROC_PRINTF(">>flow Tcam list:\n");
	for(i=0; i<fc_db.flowHashBuckets; i++)
	{
		if(!list_empty(&fc_db.flowTcam_hashListHead[i]))
		{
			PROC_PRINTF(" Flow_Hash[%3d] : \n", i);
			list_for_each_entry(pFlowEntry, &fc_db.flowTcam_hashListHead[i], flowTcam_list)
			{
				if(pFlowEntry->flowTcam_list.next!=&fc_db.flowTcam_hashListHead[i])
					PROC_PRINTF("	 Flow idx[%d] ->\n", pFlowEntry->idx);
				else
					PROC_PRINTF("	 Flow idx[%d]\n", pFlowEntry->idx);
			}
			PROC_PRINTF("\n");
		}
	}
#if 0
	PROC_PRINTF(">>flow Tcam free list:\n");
	list_for_each_entry(pFlowEntry, &fc_db.flowTcam_freeListHead, flowTcam_list)
	{
		if(pFlowEntry->flowTcam_list.next!=&fc_db.flowTcam_freeListHead)
			PROC_PRINTF("	 Flow idx[%d] ->\n", pFlowEntry->idx);
		else
			PROC_PRINTF("	 Flow idx[%d]\n", pFlowEntry->idx);
	}
#endif
	PROC_PRINTF("\n");

	return retval;
}



int32 dump_flowtag_table(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	uint32 idx=0U, counter=0U;
	rtk_rg_asic_fbMode_t fbMode = _rtk_rg_fbMode_get();
	rtk_rg_asic_flowTag_entry_t flowTag;
	rtk_rg_asic_path1_entry_t flowEnt;
	bzero(&flowTag, sizeof(flowTag));

	if(fbMode == FB_MODE_4K)
	{
		PROC_PRINTF("Not Support in 4K mode.\n");
		return SUCCESS;
	}

	PROC_PRINTF(">>ASIC FlowTag Table:\n");
	for(idx = 0; idx < RTK_FC_TABLESIZE_FLOWTAG; idx++)
	{
		{
			rtk_rg_asic_flowTagTable_get(idx, &flowTag);
			rtk_rg_asic_sramFlowEntry_get(idx, &flowEnt);
			if(flowEnt.valid)
			{
				counter++;
				PROC_PRINTF("[%d] msb:%d\tttl:%d\n", idx, flowTag.hashIdxMsb, flowTag.TTL);
			}
		}
	}
	PROC_PRINTF("\nusage: %d / %d\n", counter, RTK_FC_TABLESIZE_FLOWTAG);
	PROC_PRINTF("----------------------------------------------\n");

	return retval;
}

int32 dump_camtag_table(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	uint32 idx=0U;
	rtk_rg_asic_fbMode_t fbMode = _rtk_rg_fbMode_get();
	rtk_rg_asic_camTag_entry_t camTag;
	bzero(&camTag, sizeof(camTag));

	if(fbMode == FB_MODE_4K)
	{
		PROC_PRINTF("Not Support in 4K mode.\n");
		return SUCCESS;
	}

	PROC_PRINTF(">>ASIC CamTag Table:\n");
	for(idx = 0; idx < RTK_FC_TABLESIZE_CAMTAG; idx++)
	{
		rtk_rg_asic_camTagTable_get(idx, &camTag);
		if(camTag.valid)
			PROC_PRINTF("[%d] hashidx:%d\tlck:%d\n", idx, camTag.hsahIdx, camTag.lock);
	}
	PROC_PRINTF("----------------------------------------------\n");


	return retval;
}

int32 dump_flowtrf_table(struct seq_file *s, void *v)
{
	uint32 idx=0U, entryNum= _rtk_rg_flowEntryNum_get();
	rtk_rg_err_code_t retval=0;
	rtk_rg_asic_fbMode_t fbMode = _rtk_rg_fbMode_get();
	int retVal = 0;
	int tmp_func_return;

	uint32 *pflowTrfvalids = RTK_FC_HELPER_FC_KMALLOC(sizeof(uint32) * 1024, GFP_ATOMIC);
	uint32 *pflowTrfbits = RTK_FC_HELPER_FC_KMALLOC(sizeof(uint32) * 1024, GFP_ATOMIC);

	memset(pflowTrfvalids, 1, (sizeof(uint32) * 1024));

	PROC_PRINTF("\n>>ASIC flow traffic table - FB mode: %d (%d entries)\n", fbMode, entryNum);
	tmp_func_return = rtk_rg_asic_flowTraffic_get(&pflowTrfvalids[0], &pflowTrfbits[0]);
	ASSERT_EQ_WITH_RET(tmp_func_return, SUCCESS, retVal);

	if (retVal == -1)
	{
	        RTK_FC_HELPER_FC_KFREE(pflowTrfvalids,sizeof(uint32) * 1024);
	        RTK_FC_HELPER_FC_KFREE(pflowTrfbits,sizeof(uint32) * 1024);
		return tmp_func_return;	
	}

	entryNum >>= 5;
	for(idx = 0U; idx < entryNum; idx++)
	{
		if(pflowTrfbits[idx]!=0x0U)
			PROC_PRINTF("FlowEntry [%d~%d]: 0x%08x\r\n", idx<<5, ((idx+1)<<5)-1, pflowTrfbits[idx]);
	}


	PROC_PRINTF("----------------------------------------------\n");

	RTK_FC_HELPER_FC_KFREE(pflowTrfvalids,sizeof(uint32) * 1024);
	RTK_FC_HELPER_FC_KFREE(pflowTrfbits,sizeof(uint32) * 1024);

	return retval;
}

int32 dump_indmac_table(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	uint32 idx=0U;
	rtk_rg_asic_indirectMac_entry_t indMac;

	PROC_PRINTF(">>ASIC Mac Indirect Access Table:\n");
	for(idx = 0; idx < RTK_FC_TABLESIZE_INDMAC; idx++)
	{
		if(fc_db.indMacTbl[idx].valid){
			rtk_rg_asic_indirectMacTable_get(idx, &indMac);
			PROC_PRINTF("[%d] lut index: %d (sw idx: %d, refcnt: %d)\n", idx, indMac.l2_idx, fc_db.indMacTbl[idx].indMac.l2_idx, fc_db.indMacTbl[idx].indMacRefCount);
		}

	}
	PROC_PRINTF("----------------------------------------------\n");

	return retval;
}

int32 dump_extpmask_table(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	uint32 idx=0U;
	rtk_rg_asic_extPortMask_entry_t *pExtPMask = NULL;
	pExtPMask = RTK_FC_HELPER_FC_KMALLOC(sizeof(rtk_rg_asic_extPortMask_entry_t), GFP_ATOMIC);

	PROC_PRINTF(">>ASIC ExtPortMask Table:\n");
	for(idx = 0; idx < RTK_FC_TABLESIZE_EXTPORT; idx++)
	{
		rtk_rg_asic_extPortMaskTable_get(idx, pExtPMask);
		if(pExtPMask->extpmask != 0x0)
			PROC_PRINTF("[%d] extpmask: 0x%x refCout=%d\n", idx, pExtPMask->extpmask,fc_db.extPortTbl[idx].extPortRefCount);
	}
	PROC_PRINTF("----------------------------------------------\n");

	RTK_FC_HELPER_FC_KFREE(pExtPMask, sizeof(rtk_rg_asic_extPortMask_entry_t));

	return retval;
}

int32 _rtk_fc_displayNetifEntryByIdx(uint32 idx)
{
	rtk_rg_asic_netif_entry_t intf;
	rtk_rg_asic_netifMib_entry_t intfMib;
	uint8	*mac;
	int retval  = 0;

	memset(&intf,0,sizeof(rtk_rg_asic_netif_entry_t));
	retval = rtk_rg_asic_netifTable_get(idx, &intf);

	memset(&intfMib,0,sizeof(rtk_rg_asic_netifMib_entry_t));
	retval = rtk_rg_asic_netifMib_get(idx, &intfMib);


	if (intf.valid)
	{
		//HW fields + SW fields
		mac = (uint8 *)&intf.gateway_mac_addr.octet[0];
		rtlglue_printf("  [%d]- %02x:%02x:%02x:%02x:%02x:%02x    IP: %d.%d.%d.%d\n",
			idx, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			(intf.gateway_ipv4_addr>>24)&0xff, (intf.gateway_ipv4_addr>>16)&0xff, (intf.gateway_ipv4_addr>>8)&0xff, intf.gateway_ipv4_addr&0xff);

		rtlglue_printf("      MTU check: %d, MTU %d Bytes\n", intf.intf_mtu_check, intf.intf_mtu);
		rtlglue_printf("      IgrAct: %d, EgrAct: %d, denyv4: %d, denyv6: %d\n", intf.ingress_action, intf.egress_action, intf.deny_ipv4, intf.deny_ipv6);
		rtlglue_printf("      Allow IgrPMask: 0x%x, IgrExtPmask: 0x%x\n", intf.allow_ingress_portmask.bits[0], intf.allow_ingress_ext_portmask.bits[0]);
		rtlglue_printf("      Out PPPoE Act: %d(%s), sid: 0x%x\n", intf.out_pppoe_act, name_of_rg_netifPPPoEAct[intf.out_pppoe_act], intf.out_pppoe_sid);

		rtlglue_printf("      Igr pkt count : uc(%u), mc(%u), bc(%u)\n", intfMib.in_intf_uc_packet_cnt, intfMib.in_intf_mc_packet_cnt, intfMib.in_intf_bc_packet_cnt);
		rtlglue_printf("      Igr byte count: uc(%llu), mc(%llu), bc(%llu)\n", intfMib.in_intf_uc_byte_cnt, intfMib.in_intf_mc_byte_cnt, intfMib.in_intf_bc_byte_cnt);

		rtlglue_printf("      Egr pkt count : uc(%u), mc(%u), bc(%u)\n", intfMib.out_intf_uc_packet_cnt, intfMib.out_intf_mc_packet_cnt, intfMib.out_intf_bc_packet_cnt);
		rtlglue_printf("      Egr byte count: uc(%llu), mc(%llu), bc(%llu)\n", intfMib.out_intf_uc_byte_cnt, intfMib.out_intf_mc_byte_cnt, intfMib.out_intf_bc_byte_cnt);

		rtlglue_printf("\n\n");
	}


	return SUCCESS;
}


int32 dump_netif(struct seq_file *s, void *v)
{

	// CONFIG_RTK_L34_XPON_PLATFORM
	int32	i;
	rtlglue_printf(">>ASIC Netif Table:\n\n");

	for(i=0; i<RTK_FC_TABLESIZE_INTF; i++)
		_rtk_fc_displayNetifEntryByIdx(i);

	
	return SUCCESS;
}
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
void _rtk_ca_cls_rule_dump(ca_uint32_t hw_index, ca_uint32_t priority, ca_int32_t index, ca_uint8_t is_l2)
{
	char string_buf[8];
	char* string_print;

	if(index > 0)
		snprintf(string_buf, sizeof(string_buf), " SW[%2d]", index);
	else
		snprintf(string_buf, sizeof(string_buf), "AAL[%2d]", ~index+1);	//hwnat rsv acl

	if(is_l2){
		switch(L2_CLS_KEY_TYPE(hw_index)) {
			case AAL_L2_CLS_RULE_KEY_TYPE_L2R: string_print = "L2R(1/2)"; break;
			case AAL_L2_CLS_RULE_KEY_TYPE_L3R: string_print = "L3R(1/2)"; break;
			case AAL_L2_CLS_RULE_KEY_TYPE_CMR: string_print = "CMR(1/2)"; break;
			case AAL_L2_CLS_RULE_KEY_TYPE_IPV4R: string_print = "IPV4R(1)"; break;
			case AAL_L2_CLS_RULE_KEY_TYPE_IPV6: string_print = "IPV6(1)"; break;
			case AAL_L2_CLS_RULE_KEY_TYPE_IPV6_LONG: string_print = "IPV6L(2)"; break;
			default: string_print = "UNKNOWN"; break;
		}
		printk("|%s-L2: DEV_IDX %d | P%2d | PRI %2d | TBL/KEY_IDX: %04d / %d | %s\n", 
							string_buf, L2_CLS_DEV_ID(hw_index), L2_CLS_RUL_PORT(hw_index), priority, L2_CLS_TBL_ID(hw_index), L2_CLS_KEY_ID(hw_index), string_print);
		return;
	}

	switch(CLS_KEY_TYPE(hw_index)) {
		case CL_IF_ID_KEY: string_print = "IF_ID_KEY(1/4)"; break;
		case CL_IPV4_TUNNEL_ID_KEY: string_print = "IPV4_TUNNEL_ID_KEY(1)"; break;
		case CL_IPV4_SHORT_KEY: string_print = "IPV4_SHORT_KEY(1/2)"; break;
		case CL_IPV6_SHORT_KEY: string_print = "IPV6_SHORT_KEY(1)"; break;
		case CL_SPCL_PKT_KEY: string_print = "SPCL_PKT_KEY(1/2)"; break;
		case CL_MCST_KEY: string_print = "MCST_KEY(1)"; break;
		case CL_FULL_KEY: string_print = "FULL_KEY(2)"; break;
#if !defined(CONFIG_FC_G3_G3LITE_SERIES)
		case CL_FLOW_KEY: string_print = "FLOW_KEY(2)"; break;
#endif
		default: string_print = "UNKNOWN"; break;
	}
	printk("|%s-L3: DEV_IDX %d | %s | PRI %2d | TBL/KEY_IDX: %04d / %d | %s\n", 
						string_buf, CLS_DEV_ID(hw_index), CLS_TBL_ID(hw_index)<(L3_CLS_KEY_TBL_ENTRY_MAX/2)?"WAN":"LAN", priority, CLS_TBL_ID(hw_index), CLS_KEY_ID(hw_index), string_print);

	return;
}

#if defined(CONFIG_RTK_L34_G3_PLATFORM) && defined(FC_USER_ACL_CA_CLS_SUPPORT)
void _rtk_ca_port_dump(struct seq_file *s, ca_port_id_t cfg, ca_uint8_t ca_port_cls[][3], int ca_port_type)
{
	ca_uint32_t port_id;
	ca_uint32_t port_type;

	enum{
		CA_PORT_TYPE_ORIG_SRC_PORT=0,
		CA_PORT_TYPE_SRC_PORT=1,
		CA_PORT_TYPE_DEST_PORT=2,
		CA_SW_CLS_USAGE_L2_IGR=0,
		CA_SW_CLS_USAGE_L2_EGR=1,
		CA_SW_CLS_USAGE_L3=2,
	};

	port_id = PORT_ID(cfg);
	port_type = PORT_TYPE(cfg);

	PROC_PRINTF("0x%x", port_id);
	switch (port_type) {
		case CA_PORT_TYPE_ETHERNET:
			PROC_PRINTF("(L2 rule, TYPE_ETHERNET)");
			break;
		case CA_PORT_TYPE_CPU:
			PROC_PRINTF("(L2 rule, TYPE_CPU)");
			break;
		case CA_PORT_TYPE_L2RP:
			PROC_PRINTF("("COLOR_H"L3"COLOR_NM" rule, TYPE_L2RP)");
			break;
		case CA_PORT_TYPE_L3:
			PROC_PRINTF("("COLOR_H"L3"COLOR_NM" rule, TYPE_L3)");
			break;
		default:
			PROC_PRINTF("(port_type=0x%x)", port_type);
			break;
	}

	if(ca_port_type > 0)
	{	//only record src_port or dest_port
		switch (port_type) {
			case CA_PORT_TYPE_ETHERNET:
			case CA_PORT_TYPE_CPU:
				//src_port add at l2_igr; dest_port add at l2_egr
				ca_port_cls[port_id][ca_port_type-1]++;
				break;
			case CA_PORT_TYPE_L2RP:
			case CA_PORT_TYPE_L3:
				ca_port_cls[port_id][CA_SW_CLS_USAGE_L3]++;
				break;
		}
	}

	PROC_PRINTF(", ");
}

void _rtk_ca_vlan_tag_dump(struct seq_file *s, ca_classifier_vlan_mask_t cfg_mask, ca_classifier_vlan_t cfg)
{
	if(cfg_mask.tpid)	PROC_PRINTF("tpid = 0x%x, ", cfg.tpid);
	if(cfg_mask.vid)	PROC_PRINTF("vid = 0x%x, ", cfg.vid);
	if(cfg_mask.dei)	PROC_PRINTF("dei = 0x%x, ", cfg.dei);
	if(cfg_mask.pri)	PROC_PRINTF("pri = 0x%x, ", cfg.pri);
}

void _rtk_ca_ip_address_dump(struct seq_file *s, ca_ip_address_t cfg)
{
	//int i;

	//PROC_PRINTF("afi = %#x, ", cfg.afi);

	//for(i=0;i<4;i++)
	//	PROC_PRINTF("addr[%d] = 0x%x, ", i, cfg.ip_addr.addr[i]);

	if(cfg.afi == CA_IPV4)
		PROC_PRINTF("ipv4_addr = %pI4h, ", &cfg.ip_addr.ipv4_addr);
	else
		PROC_PRINTF("ipv6_addr = %x:%x:%x:%x, ", cfg.ip_addr.ipv6_addr[0], cfg.ip_addr.ipv6_addr[1], cfg.ip_addr.ipv6_addr[2], cfg.ip_addr.ipv6_addr[3]);

	PROC_PRINTF("addr_len = %#x", cfg.addr_len);
}

void _rtk_ca_rule_info_get(int index, char* debug_str, int debug_str_size)
{
	int sw_index;
	int ret;

	memset(debug_str, 0, debug_str_size);
	if(fc_db.ca_cls_rule_record[index].ruleType < RTK_FC_ACLANDCF_RESERVED_TAIL_END)
	{
		snprintf(debug_str, debug_str_size, "%s", fc_db.nameRsvAclType[fc_db.ca_cls_rule_record[index].ruleType]);
	}
	else
	{
		bzero(&fc_db.aclSWEntry,sizeof(fc_db.aclSWEntry));
		sw_index = fc_db.ca_cls_rule_record[index].ruleType-RTK_FC_ACLANDCF_RESERVED_TAIL_END;
		ret = _rtk_fc_aclSWEntry_get(sw_index,&fc_db.aclSWEntry);
		if(ret == RT_ERR_RG_OK)
			snprintf(debug_str, debug_str_size, "RT_ACL[%d],portmask=0x%x,total=%d", sw_index, fc_db.aclSWEntry.hw_aclEntry_port.bits[0],fc_db.aclSWEntry.hw_aclEntry_count);
		else
			snprintf(debug_str, debug_str_size, "RT_ACL[%d]", sw_index);
	}

}

int dump_acl_ca(struct seq_file *s, void *v)
{
	int i, j;
	uint32 index;
	uint32 priority;
	ca_classifier_key_t key;
	ca_classifier_key_mask_t key_mask;
	ca_classifier_action_t action;
	char* string_print;
	char string_buf[64];
	ca_uint8_t ca_port_cls[RTK_FC_MAC_PORT_MAX][3]; //each port for L2 IGR/L2EGR/L3
	int l3_cls_profile_lan = 0;
	int l3_cls_profile_wan = 0;

	enum{
		CA_PORT_TYPE_ORIG_SRC_PORT=0,
		CA_PORT_TYPE_SRC_PORT=1,
		CA_PORT_TYPE_DEST_PORT=2,
		CA_SW_CLS_USAGE_L2_IGR=0,
		CA_SW_CLS_USAGE_L2_EGR=1,
		CA_SW_CLS_USAGE_L3=2,
	};

	bzero(&ca_port_cls, sizeof(ca_port_cls));

	if(fc_db.aclAndCfReservedRule.acl_wanPortMask != fc_db.wanPortMask.portmask)
		PROC_PRINTF("+++ acl_wanPortMask = 0x%llx, wanPortMask = 0x%llx, lanPortMask = 0x%llx +++\n", fc_db.aclAndCfReservedRule.acl_wanPortMask, fc_db.wanPortMask.portmask, fc_db.lanPortMask.portmask);
	else
		PROC_PRINTF("+++ wanPortMask = 0x%llx, lanPortMask = 0x%llx +++\n", fc_db.wanPortMask.portmask, fc_db.lanPortMask.portmask);
	PROC_PRINTF("--------------- ACL TABLES ----------------(TOTAL: %d)\n", fc_db.ca_cls_used_count);
	for(index=0; index<MAX_ACL_CA_CLS_RULE_SIZE; index++)
	{
		if(ca_classifier_rule_get(G3_DEF_DEVID, index, &priority, &key, &key_mask, &action) != CA_E_OK)
			continue;
		if(fc_db.ca_cls_rule_record[index].valid == ENABLED)
		{
			_rtk_ca_rule_info_get(index, string_buf, sizeof(string_buf));
			PROC_PRINTF("  --- ACL TABLE[%d] ---(RTK-Priority=%d, RTK-RuleType=%d[%s])\n", index, fc_db.ca_cls_rule_record[index].priority, fc_db.ca_cls_rule_record[index].ruleType, string_buf);
		}
		else
			PROC_PRINTF("  --- ACL TABLE[%d] ---\n", index);

		PROC_PRINTF("\tPriority:0x%x\n", priority);
		PROC_PRINTF("\tKey: ");
		if(key_mask.orig_src_port) {
			PROC_PRINTF("orig_src_port = ");
			_rtk_ca_port_dump(s, key.orig_src_port, ca_port_cls, CA_PORT_TYPE_ORIG_SRC_PORT);
		}
		if(key_mask.src_port) {
			PROC_PRINTF("src_port = ");
			_rtk_ca_port_dump(s, key.src_port, ca_port_cls, CA_PORT_TYPE_SRC_PORT);
		}
		if(key_mask.dest_port) {
			PROC_PRINTF("dest_port = ");
			_rtk_ca_port_dump(s, key.dest_port, ca_port_cls, CA_PORT_TYPE_DEST_PORT);
		}
		if(key_mask.src_intf)				PROC_PRINTF("src_intf = 0x%x, ",key.src_intf);
		if(key_mask.dst_intf)				PROC_PRINTF("dst_intf = 0x%x, ",key.dst_intf);

		if(key_mask.merge_prio)				PROC_PRINTF("merge_prio = 0x%x, ",key.merge_prio);
		if(key_mask.packet_length)				PROC_PRINTF("packet_length_low = %d, packet_length_high = %d, ",key.packet_length_low, key.packet_length_high);
		if(key_mask.ingress_lan)				PROC_PRINTF("ingress_lan = 0x%x, ",key.ingress_lan);

		if(	(index<(MAX_ACL_CA_CLS_RULE_SIZE-1))
			&& (fc_db.ca_cls_rule_record[index].ruleType == fc_db.ca_cls_rule_record[index+1].ruleType) && fc_db.ca_cls_rule_record[index+1].valid == ENABLED
			&& (fc_db.ca_cls_rule_record[index].ruleType >= RTK_FC_ACLANDCF_RESERVED_TAIL_END))
		{
			PROC_PRINTF("\n");
			PROC_PRINTF("\t\t\t\t ...... Check key & action from next entry ...... \n");
			continue;
		}

		if(key_mask.l2) {
			PROC_PRINTF("\n");
			PROC_PRINTF("\tL2 Field: ");
			if(key_mask.l2_mask.ethertype)	PROC_PRINTF("ethertype = 0x%x, ",key.l2.ethertype);
			if(key_mask.l2_mask.subtype)	PROC_PRINTF("subtype = 0x%x, ",key.l2.subtype);
			if(key_mask.l2_mask.vlan_count)	PROC_PRINTF("vlan_count = 0x%x, ",key.l2.vlan_count);
			if(key_mask.l2_mask.is_multicast)	PROC_PRINTF("is_multicast = 0x%x, ",key.l2.is_multicast);
			if(key_mask.l2_mask.is_length)	PROC_PRINTF("is_length = 0x%x, ",key.l2.is_length);
			if(key_mask.l2_mask.cfm_opcode)	PROC_PRINTF("cfm_opcode = 0x%x, ",key.l2.cfm_opcode);
			if(key_mask.l2_mask.mac_sa) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tmac_sa(min): %pM; mac_sa(max): %pM", key.l2.mac_sa.mac_min, key.l2.mac_sa.mac_max);
			}
			if(key_mask.l2_mask.mac_da) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tmac_da(min): %pM; mac_da(max): %pM", key.l2.mac_da.mac_min, key.l2.mac_da.mac_max);
			}
			if(key_mask.l2_mask.vlan_otag) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tvlan_otag(min): ");
				_rtk_ca_vlan_tag_dump(s, key_mask.l2_mask.vlan_otag_mask, key.l2.vlan_otag.vlan_min);
				PROC_PRINTF(";vlan_otag(max): ");
				_rtk_ca_vlan_tag_dump(s, key_mask.l2_mask.vlan_otag_mask, key.l2.vlan_otag.vlan_max);
			}
			if(key_mask.l2_mask.vlan_itag) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tvlan_itag(min): ");
				_rtk_ca_vlan_tag_dump(s, key_mask.l2_mask.vlan_itag_mask, key.l2.vlan_itag.vlan_min);
				PROC_PRINTF(";vlan_itag(max): ");
				_rtk_ca_vlan_tag_dump(s, key_mask.l2_mask.vlan_itag_mask, key.l2.vlan_itag.vlan_max);
			}
		}

		if(key_mask.ip) {
			PROC_PRINTF("\n");
			PROC_PRINTF("\tIP Field: ");
			if(key_mask.ip_mask.ip_valid)			PROC_PRINTF("ip_valid = 0x%x, ",key.ip.ip_valid);
			if(key_mask.ip_mask.ip_version)		PROC_PRINTF("ip_version = 0x%x, ",key.ip.ip_version);
			if(key_mask.ip_mask.ip_protocol)		PROC_PRINTF("ip_protocol = 0x%x, ",key.ip.ip_protocol);
			if(key_mask.ip_mask.dscp)			PROC_PRINTF("dscp = 0x%x, ",key.ip.dscp);
			if(key_mask.ip_mask.ecn)			PROC_PRINTF("ecn = 0x%x, ",key.ip.ecn);
			if(key_mask.ip_mask.fragment)		PROC_PRINTF("fragment = 0x%x, ",key.ip.fragment);
			if(key_mask.ip_mask.have_options)	PROC_PRINTF("have_options = 0x%x, ",key.ip.have_options);
			if(key_mask.ip_mask.flow_label)		PROC_PRINTF("flow_label = 0x%x, ",key.ip.flow_label);
			if(key_mask.ip_mask.ext_header)		PROC_PRINTF("ext_header = 0x%x, ",key.ip.ext_header);
			if(key_mask.ip_mask.icmp_type)		PROC_PRINTF("icmp_type = 0x%x, ",key.ip.icmp_type);
			if(key_mask.ip_mask.igmp_type)		PROC_PRINTF("igmp_type = 0x%x, ",key.ip.igmp_type);
			if(key_mask.ip_mask.is_multicast)	PROC_PRINTF("is_multicast = 0x%x, ",key.ip.is_multicast);
			if(key_mask.ip_mask.ip_sa) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tip_sa: ");
				_rtk_ca_ip_address_dump(s, key.ip.ip_sa);
			}
			if(key_mask.ip_mask.ip_sa_max) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tip_sa_max: ");
				_rtk_ca_ip_address_dump(s, key.ip.ip_sa_max);
			}
			if(key_mask.ip_mask.ip_da) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tip_da: ");
				_rtk_ca_ip_address_dump(s, key.ip.ip_da);
			}
			if(key_mask.ip_mask.ip_da_max) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tip_da_max: ");
				_rtk_ca_ip_address_dump(s, key.ip.ip_da_max);
			}
		}

		if(key_mask.l4) {
			PROC_PRINTF("\n");
			PROC_PRINTF("\tL4 Field: ");
			if(key_mask.l4_mask.l4_valid)		PROC_PRINTF("l4_valid = 0x%x, ",key.l4.l4_valid);
			if(key_mask.l4_mask.tcp_flags)	PROC_PRINTF("tcp_flags = 0x%x, ",key.l4.tcp_flags);
			if(key_mask.l4_mask.src_port) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tsrc_port: min = %d, max = %d", key.l4.src_port.min, key.l4.src_port.max);
			}
			if(key_mask.l4_mask.dst_port) {
				PROC_PRINTF("\n");
				PROC_PRINTF("\t\tdst_port: min = %d, max = %d", key.l4.dst_port.min, key.l4.dst_port.max);
			}
		}

		if(key_mask.ext) {
			PROC_PRINTF("\n");
			PROC_PRINTF("\tEXT Field: ");
			PROC_PRINTF("offset = 0x%x, ", key.ext.offset);
			PROC_PRINTF("length = 0x%x, ", key.ext.length);
			PROC_PRINTF("\n");
			PROC_PRINTF("\t\tdata: ");
			for(i = 0; i < CA_MAX_DATA_EXTRACTION_LEN;i++) {
				PROC_PRINTF("[%d] = 0x%x, ", i, key.ext.data[i]);
			}
			PROC_PRINTF("\n");
			PROC_PRINTF("\t\tmask: ");
			for(i = 0; i < CA_MAX_DATA_EXTRACTION_LEN;i++) {
				PROC_PRINTF("[%d] = 0x%x, ", i, key.ext.mask[i]);
			}
		}

		if(fc_db.ca_cls_rule_record[index].aal_customize > 0) {
			PROC_PRINTF("\n");
			PROC_PRINTF("\tHW Field: ");
			for(i=CA_CLASSIFIER_AAL_NA; (0x1<<i)<CA_CLASSIFIER_AAL_END; i++) {
				if(!(fc_db.ca_cls_rule_record[index].aal_customize&(0x1<<i)))
					continue;
				switch(0x1<<i) {
					case CA_CLASSIFIER_AAL_L3_PPP_LCP:
						PROC_PRINTF("ppp_protocol_enc = 0xC021, ip_vld = 0, ");
						break;
					case CA_CLASSIFIER_AAL_L3_PPP_IPCP:
						PROC_PRINTF("ppp_protocol_enc = 0x8021, ip_vld = 0, ");
						break;
					case CA_CLASSIFIER_AAL_L3_PPP_IP6CP:
						PROC_PRINTF("ppp_protocol_enc = 0x8057, ip_vld = 0, ");
						break;
					case CA_CLASSIFIER_AAL_L3_IGNORE_SRC_PORT:
						PROC_PRINTF("ignore src_port, ");
						if((1<<PORT_ID(key.src_port)) == CA_L3_CLS_PROFILE_LAN)
							l3_cls_profile_lan++;
						else if((1<<PORT_ID(key.src_port)) == CA_L3_CLS_PROFILE_WAN)
							l3_cls_profile_wan++;
						else
							PROC_PRINTF("<<Unknown Profile>>");
						break;
					case CA_CLASSIFIER_AAL_L3_NOT_BROADCAST:
						PROC_PRINTF("mac_da_bc = 0, ");
						break;
					case CA_CLASSIFIER_AAL_L3_PKT_LEN_RANGE_IDX0:
						PROC_PRINTF("pkt_len_range_idx = 0, ");
						break;
					case CA_CLASSIFIER_AAL_L3_PPP_IPV4:
						PROC_PRINTF("ppp_protocol_enc = 0x0021");
						break;
					case CA_CLASSIFIER_AAL_L3_PPP_IPV6:
						PROC_PRINTF("ppp_protocol_enc = 0x0057");
						break;
					case CA_CLASSIFIER_AAL_L3_KEEP_ORIG:
						PROC_PRINTF("keep_orig_pkt = 1, ");
						break;
					case CA_CLASSIFIER_AAL_L3_PKT_LEN_RANGE_IDX1:
						PROC_PRINTF("pkt_len_range_idx = 1, cls_rslt_type = 0");
						break;
					case CA_CLASSIFIER_AAL_L3_SPEC_PKT_L4_L2TP_PORT:
						PROC_PRINTF("(l2tp)l4_sport = 1701, l4_dport = 1701, ");
						break;
					case CA_CLASSIFIER_AAL_L3_PKT_LEN_RANGE_IDX2:
						PROC_PRINTF("pkt_len_range_idx = 2, ");
						break;
					case CA_CLASSIFIER_AAL_L3_MAC_DA_IP_MC:
						PROC_PRINTF("mac_da_ip_mc = 1, ");
						break;
					case CA_CLASSIFIER_AAL_L3_MAC_DA_RSVD:
						PROC_PRINTF("mac_da_rsvd = 1, ");
						break;
					case CA_CLASSIFIER_AAL_L3_PPPOE_SESID_0:
						PROC_PRINTF("pppoe_session_id = 0, ");
						break;
					case CA_CLASSIFIER_AAL_L3_MAC_DA_AN_HIT:
						PROC_PRINTF("mac_da_an_hit = 1, ");
						break;
					case CA_CLASSIFIER_AAL_L3_POL_REMAP_GRP:
						PROC_PRINTF("t2_ctrl = 0x4, pol_remap_grp = 1, ");
						break;
					default:
						break;
				}
			}
		}

		PROC_PRINTF("\n");
		switch(action.forward) {
			case CA_CLASSIFIER_FORWARD_DENY:
				PROC_PRINTF("\tAction: CA_CLASSIFIER_FORWARD_DENY, drop = 0x%x", action.dest.drop);
				break;
			case CA_CLASSIFIER_FORWARD_FE:
				if(action.dest.fe == CA_CLASSIFIER_FORWARD_FE_L2FE)
					string_print = "L2FE";
				else if(action.dest.fe == CA_CLASSIFIER_FORWARD_FE_L3FE)
					string_print = "L3FE";
				else
					string_print = "NONE";
				PROC_PRINTF("\tAction: CA_CLASSIFIER_FORWARD_FE, fe = 0x%x(%s)", action.dest.fe, string_print);
				break;
			case CA_CLASSIFIER_FORWARD_INTERFACE:
				PROC_PRINTF("\tAction: CA_CLASSIFIER_FORWARD_INTERFACE, intf = 0x%x", action.dest.intf);
				break;
			case CA_CLASSIFIER_FORWARD_PORT:
				PROC_PRINTF("\tAction: CA_CLASSIFIER_FORWARD_PORT, port = 0x%x", action.dest.port);
				break;
			default:
				PROC_PRINTF("\tAction: Unknown!!");
				break;
		}

		PROC_PRINTF("\n");
		PROC_PRINTF("\tAction Option: ");
		if(action.options.masks.action_handle) {
			PROC_PRINTF("flow_id = 0x%x, ", action.options.action_handle.flow_id);
			PROC_PRINTF("gem_index = 0x%x, ", action.options.action_handle.gem_index);
			PROC_PRINTF("llid_cos_index = 0x%x, ", action.options.action_handle.llid_cos_index);
		}
		if(action.options.masks.priority)		PROC_PRINTF("priority = 0x%x, ", action.options.priority);
		if(action.options.masks.dscp)			PROC_PRINTF("dscp = 0x%x, ", action.options.dscp);
		if(action.options.masks.inner_dot1p)	PROC_PRINTF("inner_dot1p = 0x%x, ", action.options.inner_dot1p);
		if(action.options.masks.inner_tpid)		PROC_PRINTF("inner_tpid = 0x%x, ", action.options.inner_tpid);
		if(action.options.masks.inner_dei)		PROC_PRINTF("inner_dei = 0x%x, ", action.options.inner_dei);
		if(action.options.masks.inner_vlan_act) {
			PROC_PRINTF("inner_vlan_act = 0x%x, ", action.options.inner_vlan_act);
			PROC_PRINTF("inner_vid = 0x%x, ", action.options.inner_vid);
		}
		if(action.options.masks.outer_dot1p)	PROC_PRINTF("outer_dot1p = 0x%x, ", action.options.outer_dot1p);
		if(action.options.masks.outer_tpid)		PROC_PRINTF("outer_tpid = 0x%x, ", action.options.outer_tpid);
		if(action.options.masks.outer_dei)		PROC_PRINTF("outer_dei = 0x%x, ", action.options.outer_dei);
		if(action.options.masks.outer_vlan_act) {
			PROC_PRINTF("outer_vlan_act = 0x%x, ", action.options.outer_vlan_act);
			PROC_PRINTF("outer_vid = 0x%x, ", action.options.outer_vid);
		}
		if(action.options.masks.sw_shaper_id)	PROC_PRINTF("sw_shaper_id = 0x%x, ", action.options.sw_shaper_id);
		if(action.options.masks.sw_id) {
			PROC_PRINTF("\n");
			PROC_PRINTF("\t\tsw_id: ");
			for(i = 0; i < 4;i++)
				PROC_PRINTF("[%d] = 0x%x, ", i, action.options.sw_id[i]);
		}
		if(action.options.masks.mac_da) {
			PROC_PRINTF("\n");
			PROC_PRINTF("\t\tmac_da(mask=0x%x): %pM", action.options.masks.mac_da, action.options.mac_da);
		}

		PROC_PRINTF("\n");
	}

	PROC_PRINTF("--------------- CA SW CLS Usage ----------------\n");
	for(j = CA_SW_CLS_USAGE_L2_IGR; j <= CA_SW_CLS_USAGE_L3; j++)
	{
		if(j == CA_SW_CLS_USAGE_L2_IGR)
			PROC_PRINTF("L2 IGR:\t");
		else if(j == CA_SW_CLS_USAGE_L2_EGR)
			PROC_PRINTF("L2 EGR:\t");
		else
			PROC_PRINTF("L3 CLS:\t");
		for(i = 0; i < RTK_FC_MAC_PORT_MAX; i++) {
			if(ca_port_cls[i][j] > 0) {
				if(j == CA_SW_CLS_USAGE_L3) {
					if((1<<i) == CA_L3_CLS_PROFILE_LAN) {
						PROC_PRINTF(" [LAN]%d ", l3_cls_profile_lan);
						PROC_PRINTF(" [%02x]%d ", i, ca_port_cls[i][j]-l3_cls_profile_lan);
					}
					else if((1<<i) == CA_L3_CLS_PROFILE_WAN) {
						PROC_PRINTF(" [%02x]%d ", i, ca_port_cls[i][j]-l3_cls_profile_wan);
						PROC_PRINTF(" [WAN]%d ", l3_cls_profile_wan);
					}
					else
						PROC_PRINTF(" [%02x]%d ", i, ca_port_cls[i][j]);
				}
				else
					PROC_PRINTF(" [%02x]%d ", i, ca_port_cls[i][j]);
			}
		}
		PROC_PRINTF("\n");
	}

	return SUCCESS;
}

int dump_acl_ca_by_index(struct file *file, const char *buffer, unsigned long len, void *data)
{
	int ca_cls_index=_rtk_fc_proc_parsing_string_to_integer(buffer,len);
	uint32 rule_info = CA_UINT32_INVALID;
	char string_buf[64];
	int rule_priority = -1;

	if(fc_db.ca_cls_rule_record[ca_cls_index].valid == ENABLED)
	{
		rule_priority = fc_db.ca_cls_rule_record[ca_cls_index].priority;
		_rtk_ca_rule_info_get(ca_cls_index, string_buf, sizeof(string_buf));
		rtlglue_printf("--- FC Rule Type %d [ %s ] --- \n", fc_db.ca_cls_rule_record[ca_cls_index].ruleType, string_buf);
	}
	ca_classifier_l3_cls_rule_idx_get(G3_DEF_DEVID, ca_cls_index, &rule_info);
	_rtk_ca_cls_rule_dump(rule_info, rule_priority, ca_cls_index, FALSE);
	aal_used_entry_print(AAL_TABLE_L3_CLS_KEY, CLS_TBL_ID(rule_info), (CLS_KEY_TYPE(rule_info)==CL_FULL_KEY)?2:1);

	return len;
}
#endif

int dump_acl(struct seq_file *s, void *v)
{
	uint32 rule_index = 0;
	uint8 dump_name[RTK_FC_ACLANDCF_RESERVED_TAIL_END];
#if defined(CONFIG_FC_CA8277B_SERIES)
	rtk_rg_asic_netif_ref_t asicNetif;
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	aal_ilpb_cfg_t	ilpb_cfg = {0};
#endif

	rtlglue_printf("+++ wanPortMask = 0x%llx, lanPortMask = 0x%llx +++\n", fc_db.wanPortMask.portmask, fc_db.lanPortMask.portmask);
	if(fc_db.acl_lan_portmask)
		rtlglue_printf("+++ RT_ACL user portmask = 0x%x +++\n", fc_db.acl_lan_portmask);
	if(fc_db.acl_lan_portmask_hw)
		rtlglue_printf("+++ HW support lan portmask = 0x%x +++\n", fc_db.acl_lan_portmask_hw);
	if(fc_db.hypridPPTP.portmask)
		rtlglue_printf("+++ hyprid pptp pmsk = 0x%llx +++\n", fc_db.hypridPPTP.portmask);
	if(fc_db.aclAndCfReservedRule.dslite_intf_count)
		rtlglue_printf("+++ dslite intf count = %d +++\n", fc_db.aclAndCfReservedRule.dslite_intf_count);
	for(rule_index = 0; rule_index < RTK_FC_TABLESIZE_INTF_MC_ACL; rule_index++){
		if(!(fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].gateway_mac_addr.octet[0]|fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].gateway_mac_addr.octet[1]|
				fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].gateway_mac_addr.octet[2]|fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].gateway_mac_addr.octet[3]|
				fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].gateway_mac_addr.octet[4]|fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].gateway_mac_addr.octet[5]))
			continue;
		rtlglue_printf("+++ generic intf for netif[%d] with mc dmac[%d] = %pM (TBL/KEY_IDX: %d / %d): AAL[%d] +++\n", fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].netif_idx, rule_index, fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].gateway_mac_addr.octet, 
			CLS_TBL_ID(fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].acl_info), CLS_KEY_ID(fc_db.aclAndCfReservedRule.generic_intf_mc_dmac[rule_index].acl_info), RTK_CA_CLS_TYPE_GENERIC_INTF_MC_PROFILE_WITH_DMAC);
	}
#if defined(CONFIG_FC_CA8277B_SERIES)
	for(rule_index = 0; rule_index < RTK_FC_TABLESIZE_INTF_ACC_ACL; rule_index++){
		memset(&asicNetif, 0x0, sizeof(asicNetif));
		if(rtk_rg_asic_netifTable_get(rule_index, &asicNetif))
			continue;
		if((asicNetif.valid != 0) && (asicNetif.acl_info_flow_acc != CA_UINT32_INVALID))
			rtlglue_printf("+++ bridge 5to2 tuple flow acc for netif[%d] with gw dmac = %pM, check AAL[%d] and AAL[%d] +++\n", rule_index, &asicNetif.netif_mac_addr.octet[0],
				RTK_CA_CLS_TYPE_BRIDGE_5TUPLE_FLOW_ACCELERATE_BY_2TUPLE, RTK_CA_CLS_TYPE_BRIDGE_5TUPLE_FLOW_ACCELERATE_BY_2TUPLE_GW_MAC_UPDATE);
	}
#endif

	printk("|===== Classifier SW & HW Mapping Table ==============================|\n");

	memset(&dump_name, 0, sizeof(dump_name));
	for(rule_index = 0; rule_index < MAX_ACL_CA_AAL_CLS_RULE_SIZE; rule_index++)
	{
		if(!fc_db.ca_aal_cls_rule_record[rule_index].valid)
			continue;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		if(fc_db.ca_aal_cls_rule_record[rule_index].ruleType >= RTK_FC_ACLANDCF_RESERVED_TAIL_END)
			continue;	//skip user acl
#endif
		if((fc_db.ca_aal_cls_rule_record[rule_index].ruleType < RTK_FC_ACLANDCF_RESERVED_TAIL_END) && (dump_name[fc_db.ca_aal_cls_rule_record[rule_index].ruleType] == 0)){
			printk("|AAL[%2d]-%s -> %s\n", fc_db.ca_aal_cls_rule_record[rule_index].ruleType, fc_db.ca_aal_cls_rule_record[rule_index].is_l2_rule?"L2":"L3", fc_db.nameRsvAclType[fc_db.ca_aal_cls_rule_record[rule_index].ruleType]);	//to print with other ca debug message
			dump_name[fc_db.ca_aal_cls_rule_record[rule_index].ruleType] = 1;
		}
		_rtk_ca_cls_rule_dump(fc_db.ca_aal_cls_rule_record[rule_index].rslt_idx, fc_db.ca_aal_cls_rule_record[rule_index].priority, 0-fc_db.ca_aal_cls_rule_record[rule_index].ruleType, fc_db.ca_aal_cls_rule_record[rule_index].is_l2_rule);
	}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	printk(KERN_INFO "|===== L2 IGR Classifier Table ===========================================|\n");
	rtlglue_printf("[ILPBstart/len: LANportmask 0x%x, WANportmask 0x%x] ", fc_db.cls_l2_port_info[0], fc_db.cls_l2_port_info[1]);
	for(rule_index = 0; rule_index <= RTK_FC_MAC_PORT_NI_MAX; rule_index++)
	{
		if(CA_E_OK == aal_port_ilpb_cfg_get(RTK_ASIC_DEVID, rule_index, &ilpb_cfg))
			rtlglue_printf("P%d-%d/%d,", rule_index, ilpb_cfg.cls_start, ilpb_cfg.cls_length);
	}
	rtlglue_printf("\n");
	aal_l2_igr_cls_used_entry_print();
	printk(KERN_INFO "|===== L2 EGR Classifier Table ===========================================|\n");
	aal_l2_cf_entry_get(G3_DEF_DEVID, 512);	//AAL_L2_CF_MAX_RULE_ID
	aal_used_entry_print(AAL_TABLE_L3_CLS_KEY, 0, L3_CLS_KEY_TBL_ENTRY_MAX);
	if(L3_CLS_FIB_TBL_DEF_ENTRY_MAX > 0)
		aal_used_entry_print(AAL_TABLE_L3_CLS_FIB, (L3_CLS_FIB_TBL_ENTRY_MAX-L3_CLS_FIB_TBL_DEF_ENTRY_MAX), (L3_CLS_FIB_TBL_DEF_ENTRY_MAX/2));
#else
	ca_used_aal_entry_print(G3_DEF_DEVID);
#endif

	return SUCCESS;
}

int dump_acl_by_index(struct file *file, const char *buffer, unsigned long len, void *data)
{

	int cls_hw_index=_rtk_fc_proc_parsing_string_to_integer(buffer,len);
	int ca_cls_count=2;

#if defined(FC_USER_ACL_CA_CLS_SUPPORT)
	int ca_cls_index=0;
	uint32 rule_info = CA_UINT32_INVALID;
	char string_buf[64];

	for(ca_cls_index = 0; ca_cls_index < MAX_ACL_CA_CLS_RULE_SIZE; ca_cls_index++)
	{
		if(fc_db.ca_cls_rule_record[ca_cls_index].valid != ENABLED)
			continue;
		ca_classifier_l3_cls_rule_idx_get(G3_DEF_DEVID, ca_cls_index, &rule_info);
		if(CLS_TBL_ID(rule_info) != cls_hw_index)
			continue;
		_rtk_ca_rule_info_get(ca_cls_index, string_buf, sizeof(string_buf));
		rtlglue_printf("--- FC Rule Type %d [ %s ] --- \n", fc_db.ca_cls_rule_record[ca_cls_index].ruleType, string_buf);
		_rtk_ca_cls_rule_dump(rule_info, fc_db.ca_cls_rule_record[ca_cls_index].priority, ca_cls_index, FALSE);
		break;
	}
#endif

	if(CLS_KEY_TYPE(cls_hw_index) < CL_FULL_KEY)
		ca_cls_count = 1;
	aal_used_entry_print(AAL_TABLE_L3_CLS_KEY, cls_hw_index, ca_cls_count);

	return len;
}

int dump_acl_range_table(struct seq_file *s, void *v)
{
	aal_entry_print(AAL_TABLE_L3_CAM_PKT_LEN, 0, L3_CAM_PKT_LEN_TBL_ENTRY_MAX);
	aal_entry_print(AAL_TABLE_L3_CAM_ETHERTYPE, 0, 4);
	aal_entry_print(AAL_TABLE_L3_CAM_SPORT, 0, 4);
	aal_entry_print(AAL_TABLE_L3_CAM_DPORT, 0, 4);
	aal_entry_print(AAL_TABLE_L3_CAM_SPCL_HDR, 0, 4);

	return SUCCESS;
}

int32 dump_flow_table(struct seq_file *s, void *v)
{
	uint32 idx = 0;
#if 0
	uint32 groupIdx = 0, bitIdx = 0;
	uint32 agingTime = 0;
#endif
	int retval  = 0;

	aal_hash_cache_count_get(G3_DEF_DEVID, &idx);

	idx = rtk_ne_reg_read(L3FE_FE_L3E_HS_CACHE_CNT);

#if 0
	PROC_PRINTF(">>ASIC MainHash Flow Table:\n\n");
	for(groupIdx = 0; groupIdx < (fc_db.flowHwTableSize>>5); groupIdx++){
		if(fc_db.g3_mHashTbl_validBitsArray[groupIdx]==0)
			continue;

		for(bitIdx = 0; bitIdx < (1<<5); bitIdx++){
			if(!(fc_db.g3_mHashTbl_validBitsArray[groupIdx] & (1<<bitIdx)))
				continue;

			idx = (groupIdx<<5) + bitIdx;
			retval = ca_flow_age_get(G3_DEF_DEVID, idx, &agingTime);
			//ca_flow_get(CA_IN ca_device_id_t device_id, CA_IN ca_uint32_t index, CA_OUT ca_flow_t * p_flow_config)

			if(retval)
				PROC_PRINTF(" - Flow[%d] agingTime = ERROR ret %d\n", idx, retval);
			else
				PROC_PRINTF(" - Flow[%d] agingTime = %d \n", idx, agingTime);
		}
	}
#else
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF(">>ASIC MainHash HW Entry: \n");
	aal_hash_flow_entry_show_all();
#else
	PROC_PRINTF(">>ASIC MainHash Cached Entry: %d\n\n", idx);
	flow_table_dump();
#endif
#endif


	return retval;
}

int32 dump_flow_table_by_filter(struct file *file, const char *buffer, unsigned long len, void *data)
{

	return len;
}

void dump_hash_mask_by_idx(int hash_mask_idx)
{
	aal_hash_mask_t hash_mask;

	if(aal_entry_get_by_idx(AAL_TABLE_HASH_MASK_TBL, hash_mask_idx, &hash_mask) == AAL_E_OK)
	{
		/* Hash Control */
		if((hash_mask.table_id != 1) || (hash_mask.ctrl_set_id != 1) || (hash_mask.hkey_id != 1))
		{
			rtlglue_printf("\t[Hash Control]\n");
			if(hash_mask.table_id != 1)
				rtlglue_printf("\t- table_id\n");
			if(hash_mask.ctrl_set_id != 1)
				rtlglue_printf("\t- ctrl_set_id\n");
			if(hash_mask.hkey_id != 1)
				rtlglue_printf("\t- hkey_id\n");
		}

		/* Source Port ID */
		if((hash_mask.lspid != 1) || (hash_mask.orig_lspid != 1) || (hash_mask.mc_idx_vld != 1))
		{
			rtlglue_printf("\t[Source Port ID]\n");
			if(hash_mask.lspid != 1)
				rtlglue_printf("\t- lspid\n");
			if(hash_mask.orig_lspid != 1)
				rtlglue_printf("\t- orig_lspid\n");
			if(hash_mask.mc_idx_vld != 1)
				rtlglue_printf("\t- mc_idx_vld\n");
		}

		/* Dest Port ID */
		if((hash_mask.mc != 1) || (hash_mask.mcgid != 0x3ff))
		{
			rtlglue_printf("\t[Dest Port ID]\n");
			if(hash_mask.mc != 1)
				rtlglue_printf("\t- mc\n");
			if(hash_mask.mcgid != 0x3ff)
				rtlglue_printf("\t- mcgid \033[2;35;40m(bitmask 0x%x)\033[0m\n", (~hash_mask.mcgid)&0x3ff);
		}

		/* COS */
		if(hash_mask.cos != 1)
		{
			rtlglue_printf("\t[cos]\n");
			rtlglue_printf("\t- cos\n");
		}

		/* POL ID */
		if((hash_mask.pol_id != 1) || (hash_mask.pol_grp_id != 1) || (hash_mask.qos_premark != 1))
		{
			rtlglue_printf("\t[POL ID]\n");
			if(hash_mask.pol_id != 1)
				rtlglue_printf("\t- pol_id\n");
			if(hash_mask.pol_grp_id != 1)
				rtlglue_printf("\t- pol_grp_id\n");
			if(hash_mask.qos_premark != 1)
				rtlglue_printf("\t- qos_premark\n");
		}

		/* MDATA */
		if(hash_mask.mdata != 0xffffffffffffffff)
		{
			rtlglue_printf("\t[MDATA]\n");
			rtlglue_printf("\t- mdata \033[2;35;40m(bitmask 0x%016llx)\033[0m\n", (~hash_mask.mdata)&0xffffffffffffffff);
		}

		/* special packet */
		if((hash_mask.spcl_pkt_enc != 1) || (hash_mask.spcl_pkt_hdr_mtch != 0xff))
		{
			rtlglue_printf("\t[special packet]\n");
			if(hash_mask.spcl_pkt_enc != 1)
				rtlglue_printf("\t- spcl_pkt_enc\n");
			if(hash_mask.spcl_pkt_hdr_mtch != 0xff)
				rtlglue_printf("\t- spcl_pkt_hdr_mtch \033[2;35;40m(bitmask 0x%x)\033[0m\n", (~hash_mask.spcl_pkt_hdr_mtch)&0xff);
		}

		/* L2 */
		if((hash_mask.mac_da != 0x3f) || (hash_mask.mac_da_an_sel != 1) || (hash_mask.mac_da_ip_mc != 1) ||
			(hash_mask.mac_da_rng != 1) || (hash_mask.mac_da_rsvd != 1) || (hash_mask.mac_sa != 0x3f) ||
			(hash_mask.ethertype != 1) || (hash_mask.ethertype_enc != 1))
		{
			rtlglue_printf("\t[L2]\n");
			if(hash_mask.mac_da != 0x3f)
				rtlglue_printf("\t- mac_da \033[2;35;40m(bitmask %x:%x:%x:%x:%x:%x)\033[0m\n",
				(hash_mask.mac_da>>5)&0x1?00:0xff,
				(hash_mask.mac_da>>4)&0x1?00:0xff,
				(hash_mask.mac_da>>3)&0x1?00:0xff,
				(hash_mask.mac_da>>2)&0x1?00:0xff,
				(hash_mask.mac_da>>1)&0x1?00:0xff,
				(hash_mask.mac_da>>0)&0x1?00:0xff
				);
			if(hash_mask.mac_da_an_sel != 1)
				rtlglue_printf("\t- mac_da_an_sel\n");
			if(hash_mask.mac_da_ip_mc != 1)
				rtlglue_printf("\t- mac_da_ip_mc\n");
			if(hash_mask.mac_da_rng != 1)
				rtlglue_printf("\t- mac_da_rng\n");
			if(hash_mask.mac_da_rsvd != 1)
				rtlglue_printf("\t- mac_da_rsvd\n");
			if(hash_mask.mac_sa != 0x3f)
				rtlglue_printf("\t- mac_sa \033[2;35;40m(bitmask %x:%x:%x:%x:%x:%x)\033[0m\n",
				(hash_mask.mac_sa>>5)&0x1?00:0xff,
				(hash_mask.mac_sa>>4)&0x1?00:0xff,
				(hash_mask.mac_sa>>3)&0x1?00:0xff,
				(hash_mask.mac_sa>>2)&0x1?00:0xff,
				(hash_mask.mac_sa>>1)&0x1?00:0xff,
				(hash_mask.mac_sa>>0)&0x1?00:0xff
				);
			if(hash_mask.ethertype != 1)
				rtlglue_printf("\t- ethertype\n");
			if(hash_mask.ethertype_enc != 1)
				rtlglue_printf("\t- ethertype_enc\n");
		}

		/* L2 Format */
		if((hash_mask.len_encoded != 1) || (hash_mask.pktlen_rng_match_vec != 0xf) || (hash_mask.llc_snap != 1) || (hash_mask.llc_type_enc != 1))
		{
			rtlglue_printf("\t[L2 Format]\n");
			if(hash_mask.len_encoded != 1)
				rtlglue_printf("\t- len_encoded\n");
			if(hash_mask.pktlen_rng_match_vec != 0xf)
				rtlglue_printf("\t- pktlen_rng_match_vec \033[2;35;40m(bitmask 0x%x)\033[0m\n", (~hash_mask.pktlen_rng_match_vec)&0xf);
			if(hash_mask.llc_snap != 1)
				rtlglue_printf("\t- llc_snap\n");
			if(hash_mask.llc_type_enc != 1)
				rtlglue_printf("\t- llc_type_enc\n");
		}

		/* VLAN */
		if((hash_mask.vlan_cnt != 1) || (hash_mask.top_tpid_enc != 1) || (hash_mask.top_vid != 1) ||
			(hash_mask.top_8021p != 1) || (hash_mask.top_dei != 1) || (hash_mask.inner_tpid_enc != 1) ||
			(hash_mask.inner_vid != 1) || (hash_mask.inner_8021p != 1) || (hash_mask.inner_dei != 1))
		{
			rtlglue_printf("\t[VLAN]\n");
			if(hash_mask.vlan_cnt != 1)
				rtlglue_printf("\t- vlan_cnt\n");
			if(hash_mask.top_tpid_enc != 1)
				rtlglue_printf("\t- top_tpid_enc\n");
			if(hash_mask.top_vid != 1)
				rtlglue_printf("\t- top_vid\n");
			if(hash_mask.top_8021p != 1)
				rtlglue_printf("\t- top_8021p\n");
			if(hash_mask.top_dei != 1)
				rtlglue_printf("\t- top_dei\n");
			if(hash_mask.inner_tpid_enc != 1)
				rtlglue_printf("\t- inner_tpid_enc\n");
			if(hash_mask.inner_vid != 1)
				rtlglue_printf("\t- inner_vid\n");
			if(hash_mask.inner_8021p != 1)
				rtlglue_printf("\t- inner_8021p\n");
			if(hash_mask.inner_dei != 1)
				rtlglue_printf("\t- inner_dei\n");
		}

		/* PPP / PPPoE */
		if((hash_mask.pppoe_type != 1) || (hash_mask.pppoe_code_enc != 1) || (hash_mask.pppoe_session_id != 1) || (hash_mask.ppp_protocol_enc != 1))
		{
			rtlglue_printf("\t[PPP/PPPoE]\n");
			if(hash_mask.pppoe_type != 1)
				rtlglue_printf("\t- pppoe_type\n");
			if(hash_mask.pppoe_code_enc != 1)
				rtlglue_printf("\t- pppoe_code_enc\n");
			if(hash_mask.pppoe_session_id != 1)
				rtlglue_printf("\t- pppoe_session_id\n");
			if(hash_mask.ppp_protocol_enc != 1)
				rtlglue_printf("\t- ppp_protocol_enc\n");
		}

		/* L3 */
		if((hash_mask.ip_vld != 1) || (hash_mask.ip_ver != 1) || (hash_mask.ip_dscp != 0x3f) || (hash_mask.ip_ecn != 0x3) ||
			(hash_mask.ip_protocol != 1) || (hash_mask.ip_l4_type != 1) || ((hash_mask.ip_sa & 0xff) != 0) ||
			((hash_mask.ip_da & 0xff) != 0) || (hash_mask.ipv6_flow_lbl != 1) || (hash_mask.ip_ttl == 2) || (hash_mask.ip_ttl == 3) ||
			(hash_mask.ip_options != 1) || (hash_mask.ip_da_sa_equal != 1) || (hash_mask.ip_fragment_flag != 1) || (hash_mask.ipv6_hbh != 1) ||
			(hash_mask.ipv6_rh != 1) || (hash_mask.ipv6_doh != 1) || (hash_mask.icmp_vld != 1) || (hash_mask.icmp_type != 1) ||
			(hash_mask.spi_vld != 1) || (hash_mask.spi != 1) || (hash_mask.l3_chksum_err != 1))
		{
			rtlglue_printf("\t[L3]\n");
			if(hash_mask.ip_vld != 1)
				rtlglue_printf("\t- ip_vld\n");
			if(hash_mask.ip_ver != 1)
				rtlglue_printf("\t- ip_ver\n");
			if(hash_mask.ip_dscp != 0x3f)
				rtlglue_printf("\t- ip_dscp \033[2;35;40m(bitmask 0x%x)\033[0m\n", (~hash_mask.ip_dscp)&0x3f);
			if(hash_mask.ip_ecn != 0x3)
				rtlglue_printf("\t- ip_ecn \033[2;35;40m(bitmask 0x%x)\033[0m\n", (~hash_mask.ip_ecn)&0x3);
			if(hash_mask.ip_protocol != 1)
				rtlglue_printf("\t- ip_protocol\n");
			if(hash_mask.ip_l4_type != 1)
				rtlglue_printf("\t- ip_l4_type\n");
			if((hash_mask.ip_sa & 0xff) != 0)
			{
				if((hash_mask.ip_sa >> 8) & 0x1)
					rtlglue_printf("\t- ip_sa \033[2;35;40m(suffix %d bits)\033[0m\n", (hash_mask.ip_sa & 0xff));
				else
					rtlglue_printf("\t- ip_sa \033[2;35;40m(prefix %d bits)\033[0m\n", (hash_mask.ip_sa & 0xff));
			}
			if((hash_mask.ip_da & 0xff) != 0)
			{
				if((hash_mask.ip_da >> 8) & 0x1)
					rtlglue_printf("\t- ip_da \033[2;35;40m(suffix %d bits)\033[0m\n", (hash_mask.ip_da & 0xff));
				else
					rtlglue_printf("\t- ip_da \033[2;35;40m(prefix %d bits)\033[0m\n", (hash_mask.ip_da & 0xff));
			}
			if(hash_mask.ipv6_flow_lbl != 1)
				rtlglue_printf("\t- ipv6_flow_lbl\n");
			if(hash_mask.ip_ttl == 2)
				rtlglue_printf("\t- ip_ttl \033[2;35;40m(exact TTL value)\033[0m\n");
			else if(hash_mask.ip_ttl == 3)
				rtlglue_printf("\t- ip_ttl \033[2;35;40m(TTL range)\033[0m\n");
			if(hash_mask.ip_options != 1)
				rtlglue_printf("\t- ip_options\n");
			if(hash_mask.ip_da_sa_equal != 1)
				rtlglue_printf("\t- ip_da_sa_equal\n");
			if(hash_mask.ip_fragment_flag != 1)
				rtlglue_printf("\t- ip_fragment_flag\n");
			if(hash_mask.ipv6_hbh != 1)
				rtlglue_printf("\t- ipv6_hbh\n");
			if(hash_mask.ipv6_rh != 1)
				rtlglue_printf("\t- ipv6_rh\n");
			if(hash_mask.ipv6_doh != 1)
				rtlglue_printf("\t- ipv6_doh\n");
			if(hash_mask.icmp_vld != 1)
				rtlglue_printf("\t- icmp_vld\n");
			if(hash_mask.icmp_type != 1)
				rtlglue_printf("\t- icmp_type\n");
			if(hash_mask.spi_vld != 1)
				rtlglue_printf("\t- spi_vld\n");
			if(hash_mask.spi != 1)
				rtlglue_printf("\t- spi\n");
			if(hash_mask.l3_chksum_err != 1)
				rtlglue_printf("\t- l3_chksum_err\n");
		}

		/* L4 */
		if(((hash_mask.l4_sp_exact_range & 0xffff) != 0xffff) || ((hash_mask.l4_dp_exact_range & 0xffff) != 0xffff) ||
			(hash_mask.tcp_rdp_ctrl != 0x1ff) || (hash_mask.l4_chksum_zero != 1))
		{
			rtlglue_printf("\t[L4]\n");
			if((hash_mask.l4_sp_exact_range & 0xffff) != 0xffff)
			{
				rtlglue_printf("\t- l4_sp_exact_range \033[2;35;40m(bitmask 0x%x with %s)\033[0m\n", (~hash_mask.l4_sp_exact_range)&0xffff, ((hash_mask.l4_sp_exact_range>>16)&0x1)?"L4_port_range_vec":"L4 port");
			}
			if((hash_mask.l4_dp_exact_range & 0xffff) != 0xffff)
			{
				rtlglue_printf("\t- l4_dp_exact_range \033[2;35;40m(bitmask 0x%x with %s)\033[0m\n", (~hash_mask.l4_dp_exact_range)&0xffff, ((hash_mask.l4_dp_exact_range>>16)&0x1)?"L4_port_range_vec":"L4 port");
			}
			if(hash_mask.tcp_rdp_ctrl != 0x1ff)
			{
				rtlglue_printf("\t- tcp_rdp_ctrl \033[2;35;40m(bitmask 0x%x (N: %u, C: %u, E: %u, U: %u, A: %u, P: %u, R: %u, S: %u, F: %u))\033[0m\n",
					(~hash_mask.tcp_rdp_ctrl)&0x1ff,
					(((~hash_mask.tcp_rdp_ctrl)&0x1ff)>>8)&0x1,
					(((~hash_mask.tcp_rdp_ctrl)&0x1ff)>>7)&0x1,
					(((~hash_mask.tcp_rdp_ctrl)&0x1ff)>>6)&0x1,
					(((~hash_mask.tcp_rdp_ctrl)&0x1ff)>>5)&0x1,
					(((~hash_mask.tcp_rdp_ctrl)&0x1ff)>>4)&0x1,
					(((~hash_mask.tcp_rdp_ctrl)&0x1ff)>>3)&0x1,
					(((~hash_mask.tcp_rdp_ctrl)&0x1ff)>>2)&0x1,
					(((~hash_mask.tcp_rdp_ctrl)&0x1ff)>>1)&0x1,
					(((~hash_mask.tcp_rdp_ctrl)&0x1ff)>>0)&0x1
					);
			}
			if(hash_mask.l4_chksum_zero != 1)
				rtlglue_printf("\t- l4_chksum_zero\n");
		}
	}
}
int dump_flow_key_conf(void)
{
	L3FE_FE_L3E_HS_PROFILE0_INI_t profile_conf;
	L3FE_FE_L3E_HS_PROFILE0_TUPLE0_t tuple_conf;
	uint32 tuple_id = 0, profile_id = 0;

#if defined(CONFIG_FC_RTL8277C_SERIES)
	char * profile_string[RTK_ASIC_FLOW_PROFILE_MAX] =
	{
		"FLOW_5TUPLE",
		"FLOW_2TUPLE",
		"FLOW_MC",
		"FLOW_5TUPLE_TCP_FLAG0",
		"DEFAULT_DROP",
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
		"WIFI_TX_AMSDU_OFLD",
#endif
	};
	char * def_act_string[RTK_ASIC_FLOW_DEFACT_TYPE_MAX] =
	{
		"DEFACT_TYPE_TRAP",
		"DEFACT_TYPE_DROP",
		"DEFACT_TYPE_TRAP_WITH_METER",
		"DEFACT_TYPE_TRAP_UMC_STORM",
	};
	rtk_fc_asic_flow_defAct_type_t flow_empty_act, flow_aging_act;

	for(profile_id = 0 ; profile_id < RTK_ASIC_FLOW_PROFILE_MAX ; profile_id++)
	{
		profile_conf.wrd = rtk_ne_reg_read(L3FE_FE_L3E_HS_PROFILE0_INI + (profile_id * L3FE_FE_L3E_HS_PROFILE0_INI_STRIDE));
		rtk_fc_asic_flow_default_action_get(profile_id, &flow_empty_act, &flow_aging_act);
		for(tuple_id = 0 ; tuple_id < profile_conf.bf.tpl_num ; tuple_id++)
		{
			rtlglue_printf(COLOR_Y"<PROFILE %d/TUPLE %d> %s (empty_miss_act: %s, aging_miss_act: %s)"COLOR_NM, profile_id, tuple_id, profile_string[profile_id], def_act_string[flow_empty_act], def_act_string[flow_aging_act]);
			rtlglue_printf("\n");
			tuple_conf.wrd = rtk_ne_reg_read(L3FE_FE_L3E_HS_PROFILE0_TUPLE0 + (profile_id * L3FE_FE_L3E_HS_PROFILE0_TUPLE0_STRIDE) + (tuple_id * 4));
			dump_hash_mask_by_idx(tuple_conf.bf.maskptr);
		}
	}
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	char * profile_string[RTK_ASIC_FLOW_PROFILE_MAX] =
	{
		"FLOW_5TUPLE",
		"FLOW_2TUPLE",
		"FLOW_MC",
		"DEFAULT_DROP",
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
		"WIFI_TX_AMSDU_OFLD",
#endif
	};
	char * def_act_string[RTK_ASIC_FLOW_DEFACT_TYPE_MAX] =
	{
		"DEFACT_TYPE_TRAP",
		"DEFACT_TYPE_DROP",
		"DEFACT_TYPE_TRAP_WITH_METER",
		"DEFACT_TYPE_TRAP_UMC_STORM",
	};
	rtk_fc_asic_flow_defAct_type_t flow_empty_act, flow_aging_act;

	for(profile_id = 0 ; profile_id < RTK_ASIC_FLOW_PROFILE_MAX ; profile_id++)
	{
		profile_conf.wrd = rtk_ne_reg_read(L3FE_FE_L3E_HS_PROFILE0_INI + (profile_id * L3FE_FE_L3E_HS_PROFILE0_INI_STRIDE));
		rtk_fc_asic_flow_default_action_get(profile_id, &flow_empty_act, &flow_aging_act);
		for(tuple_id = 0 ; tuple_id < profile_conf.bf.tpl_num ; tuple_id++)
		{
			rtlglue_printf(COLOR_Y"<PROFILE %d/TUPLE %d> %s (empty_miss_act: %s, aging_miss_act: %s)"COLOR_NM, profile_id, tuple_id, profile_string[profile_id], def_act_string[flow_empty_act], def_act_string[flow_aging_act]);
			rtlglue_printf("\n");
			tuple_conf.wrd = rtk_ne_reg_read(L3FE_FE_L3E_HS_PROFILE0_TUPLE0 + (profile_id * L3FE_FE_L3E_HS_PROFILE0_TUPLE0_STRIDE) + (tuple_id * 4));
			dump_hash_mask_by_idx(tuple_conf.bf.maskptr);
		}
	}
#else
	int total_profile_count = L3FE_FE_L3E_HS_PROFILE0_INI_COUNT;

	char * profile_string[L3FE_FE_L3E_HS_PROFILE0_INI_COUNT] =
	{
		/*
		ref:
		ca_flow_key_profile_mapping_set(G3_DEF_DEVID, RG_CA_FLOW_UC5TUPLE_DS,			HASH_PROFILE_0);
		ca_flow_key_profile_mapping_set(G3_DEF_DEVID, RG_CA_FLOW_UC5TUPLE_US,			HASH_PROFILE_1);
		ca_flow_key_profile_mapping_set(G3_DEF_DEVID, RG_CA_FLOW_MC, 					HASH_PROFILE_2);
		ca_flow_key_profile_mapping_set(G3_DEF_DEVID, RG_CA_FLOW_UC2TUPLE_BRIDGE,		HASH_PROFILE_3);
		ca_flow_key_profile_mapping_set(G3_DEF_DEVID, RG_CA_FLOW_MC_FORWARD,			HASH_PROFILE_2);
#if defined(CONFIG_FC_G3_G3LITE_SERIES)
		ca_flow_key_profile_mapping_set(G3_DEF_DEVID, RG_CA_FLOW_CLS_DOS_POL,			HASH_PROFILE_4);
#elif defined(CONFIG_CA8277B_SERIES)
		ca_flow_key_profile_mapping_set(G3_DEF_DEVID, RG_CA_FLOW_CLS_TRAP_GRPPOL,		HASH_PROFILE_4);
#endif
		*/
		"FLOW_UC5TUPLE_DS",
		"UC5TUPLE_US",
		"FLOW_MC",
		"FLOW_UC2TUPLE_BRIDGE",
#if defined(CONFIG_FC_G3_G3LITE_SERIES)
		"FLOW_CLS_DOS_POL",
#elif defined(CONFIG_CA8277B_SERIES)
		"FLOW_CLS_TRAP_GRPPOL"
#endif
	};
	for(profile_id = 0 ; profile_id < total_profile_count ; profile_id++)
	{
		profile_conf.wrd = rtk_ne_reg_read(L3FE_FE_L3E_HS_PROFILE0_INI + (profile_id * L3FE_FE_L3E_HS_PROFILE0_INI_STRIDE));
		for(tuple_id = 0 ; tuple_id < profile_conf.bf.tpl_num ; tuple_id++)
		{
			rtlglue_printf(COLOR_Y"<PROFILE %d/TUPLE %d> %s"COLOR_NM, profile_id, tuple_id, profile_string[profile_id]);
			rtlglue_printf("\n");
			tuple_conf.wrd = rtk_ne_reg_read(L3FE_FE_L3E_HS_PROFILE0_TUPLE0 + (profile_id * L3FE_FE_L3E_HS_PROFILE0_TUPLE0_STRIDE) + (tuple_id * 4));
			dump_hash_mask_by_idx(tuple_conf.bf.maskptr);
		}
	}
#endif
	return SUCCESS;
}

int32 dump_netif(struct seq_file *s, void *v)
{

	int32	i;
	PROC_PRINTF(">>ASIC Netif Table:\n\n");

	for(i=0; i<RTK_FC_TABLESIZE_INTF; i++){

		if (fc_db.netifHwTblVaild[i].hwNetifValid)
		{
			PROC_PRINTF("[%d]HwNetif reference to SwNetif[%d]\n",i,fc_db.netifHwTblVaild[i].swNetifIdx);
		}
	}

	return SUCCESS;
}

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
typedef enum rtk_fc_headeri_phase_e
{
	RTK_FC_HEADERI_PHASE_L3PP_STG0 = 0,
	RTK_FC_HEADERI_PHASE_STG0_STG1,
	RTK_FC_HEADERI_PHASE_STG1_STG2,
	RTK_FC_HEADERI_PHASE_STG2_PE,
}rtk_fc_headeri_phase_t;
static void dump_l3fe_hdr_i(struct seq_file *s, void *hdr, rtk_fc_headeri_phase_t phase)
{
	#define SWIDSTRLEN	64
	uint32 address_src[4];
	uint32 address_dest[4];
	char swid_str[SWIDSTRLEN]={0};
#if defined(CONFIG_FC_RTL8277C_SERIES)
	char *name_t2_ctrl[RTK_ASIC_FLOW_PROFILE_MAX]={"(5tup)","(2tup)","(MC)","(5tupTCP)","(ACLdrop)"};
	char *sw_reason_code_str[]={
		/*reaseon #00*/"N/A",
		/*reaseon #01-RXINFO_REF_TRAP_RSN_ACL*/COLOR_H"ACL Trap and keep CRC"COLOR_NM,
		/*reaseon #02-RXINFO_REF_TRAP_RSN_DUAL_DS*/COLOR_H"ACL Pop DS Dual"COLOR_NM,
		/*reaseon #03-RXINFO_REF_TRAP_RSN_ACL_RSN*/COLOR_H"ACL Trap and keep CRC and include RSN"COLOR_NM,
		/*reaseon #04-RXINFO_REF_TRAP_RSN_ACL_TO_PS*/COLOR_H"ACL Trap to PS"COLOR_NM,
		/*reaseon #05~15*/"N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A"};
	char *reason_code_str[]={
		/*reaseon #00*/"other trap",
		/*reaseon #01*/"pp/stag0 trap",
		/*reaseon #02*/"acl trap",
		/*reaseon #03*/"flow miss",
		/*reaseon #04*/"ttl equal to 0",
		/*reaseon #05*/"dmac lookup miss",
		/*reaseon #06*/"over mtu",
		/*reaseon #07~14*/"N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A",
		/*reaseon #15*/"flow hit"
	};
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	char *name_t2_ctrl[RTK_ASIC_FLOW_PROFILE_MAX]={"(5tup)","(2tup)","(MC)","(ACLdrop)"};
	char *sw_reason_code_str[]={
		/*reaseon #00~01*/"N/A","N/A",
		/*reaseon #02-RXINFO_REF_TRAP_RSN_DUAL_DS*/COLOR_H"ACL Pop DS Dual"COLOR_NM,
		/*reaseon #03-RXINFO_REF_TRAP_RSN_ACL_RSN*/COLOR_H"ACL Trap and keep CRC and include RSN"COLOR_NM,
		/*reaseon #04~15*/"N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A","N/A"};
	char *reason_code_str[]={
		/*reaseon #00*/"other trap",
		/*reaseon #01*/"pp/stag0 trap",
		/*reaseon #02*/"acl trap",
		/*reaseon #03*/"flow miss",
		/*reaseon #04*/"ttl equal to 0",
		/*reaseon #05*/"dmac lookup miss",
		/*reaseon #06*/"over mtu",
		/*reaseon #07~13*/"N/A","N/A","N/A","N/A","N/A","N/A","N/A",
		/*reaseon #14*/"acl trap with crc"
		/*reaseon #15*/"flow hit"
	};
#endif

	snprintf(swid_str, SWIDSTRLEN, "%s", "---");

	if(phase == RTK_FC_HEADERI_PHASE_STG1_STG2){
		PROC_PRINTF("<Table Control>\n");
		PROC_PRINTF("	t0_ctrl: %u, t1_ctrl(4): 0x%X, mainHash(4): 0x%X%s\n", HDR_I_HW_FIELD(hdr, t0_ctrl), HDR_I_HW_FIELD(hdr, t1_ctrl), HDR_I_HW_FIELD(hdr, t2_ctrl), ((HDR_I_HW_FIELD(hdr, t2_ctrl) == 0xf)? COLOR_H"(bypass)"COLOR_NM : name_t2_ctrl[HDR_I_HW_FIELD(hdr, t2_ctrl)]));
		PROC_PRINTF("<MDATA>\n");
		PROC_PRINTF("	mdata_h(32): 0x%08X, mdata_l(32): 0x%08X\n", HDR_I_HW_FIELD(hdr, mdata_h), HDR_I_HW_FIELD(hdr, mdata_l));
		if(HAS_ACL_RSN(MDATAL_REASON(HDR_I_HW_FIELD(hdr, mdata_l)), MDATAL_CLS_RSN(HDR_I_HW_FIELD(hdr, mdata_l)))){
			_rtk_fc_aclHeaderInfo_get(swid_str, sizeof(swid_str), MDATAL_CLS_RSN(HDR_I_HW_FIELD(hdr, mdata_l)));
			PROC_PRINTF("	  sw_reason_code(4): 0x%X (%s), reason_code(4): 0x%X (%s), acl_rsn(8): 0x%02X %s", 
				MDATAL_REASON_SW(HDR_I_HW_FIELD(hdr, mdata_l)), sw_reason_code_str[MDATAL_REASON_SW(HDR_I_HW_FIELD(hdr, mdata_l))], (HDR_I_HW_FIELD(hdr, mdata_l) >> 8) & 0xf, reason_code_str[(HDR_I_HW_FIELD(hdr, mdata_l) >> 8) & 0xf], MDATAL_CLS_RSN(HDR_I_HW_FIELD(hdr, mdata_l)), swid_str);
		}else
			PROC_PRINTF("	  sw_reason_code(4): 0x%X (%s), reason_code(4): 0x%X (%s), %s(8): 0x%02X", 
				MDATAL_REASON_SW(HDR_I_HW_FIELD(hdr, mdata_l)), sw_reason_code_str[MDATAL_REASON_SW(HDR_I_HW_FIELD(hdr, mdata_l))], (HDR_I_HW_FIELD(hdr, mdata_l) >> 8) & 0xf, reason_code_str[(HDR_I_HW_FIELD(hdr, mdata_l) >> 8) & 0xf],  swid_str, MDATAL_CLS_RSN(HDR_I_HW_FIELD(hdr, mdata_l)));
		PROC_PRINTF("\n");
		return;
	}

	PROC_PRINTF("<Table Control>\n");
	PROC_PRINTF("	t0_ctrl: %u, t1_ctrl(4): 0x%X, mainHash(4): 0x%X%s\n", HDR_I_HW_FIELD(hdr, t0_ctrl), HDR_I_HW_FIELD(hdr, t1_ctrl), HDR_I_HW_FIELD(hdr, t2_ctrl), ((HDR_I_HW_FIELD(hdr, t2_ctrl) == 0xf)? "" : name_t2_ctrl[HDR_I_HW_FIELD(hdr, t2_ctrl)]));
	PROC_PRINTF("	igr_l3_if_idx(6): %u, egr_l3_if_idx(6): %u, l3_if_counter_en: %u\n",	HDR_I_HW_FIELD(hdr, igr_l3_if_idx), HDR_I_HW_FIELD(hdr, egr_l3_if_idx), HDR_I_HW_FIELD(hdr, l3_if_counter_en));

	if (HDR_I_HW_FIELD(hdr, stage2_ctrl) == 0) {
		PROC_PRINTF("	stage2_ctrl: 0 [Merge type-0 and type-1 result of Hash Engine]\n");
	} else {
		PROC_PRINTF("	stage2_ctrl: 1 [Do NOT merge type-0 result of Hash Engine]\n");
	}

	PROC_PRINTF("	modify_vlan_only: %u\n", HDR_I_HW_FIELD(hdr, modify_vlan_only));

	PROC_PRINTF("	keep_org_pkt: %u, keep_ts: %u, hdr_ptp: %u\n", HDR_I_HW_FIELD(hdr, keep_org_pkt), HDR_I_HW_FIELD(hdr, keep_ts), HDR_I_HW_FIELD(hdr, hdr_ptp));
#if defined(CONFIG_FC_RTL8277C_SERIES)
	PROC_PRINTF("	flow_type: %u (%s)\n", HDR_I_HW_FIELD(hdr, flow_type), (HDR_I_HW_FIELD(hdr, flow_type) == 0)?"small":((HDR_I_HW_FIELD(hdr, flow_type) == 1)?"middle":"big"));
#endif
	// --------------------------- //

	PROC_PRINTF("<Info for PE module>\n");
#if defined(CONFIG_FC_RTL9607F_SERIES)
	if(ASICDRIVERVER != CHIP_REV_ID_A)
	{
		PROC_PRINTF("	orig_packet_len(14): 0x%04X(%d), l4_offset(9): 0x%03X, l4_inner_offset(9): 0x%03X, l3_offset(9): 0x%03X, l3_inner_offset(9): 0x%03X\n",
			HDR_I_HW_FIELD(hdr, orig_packet_len), HDR_I_HW_FIELD(hdr, orig_packet_len), HDR_I_HW_FIELD(hdr, l4_offset), HDR_I_HW_FIELD(hdr, l4_inner_offset), HDR_I_HW_FIELD(hdr, l3_offset), HDR_I_HW_FIELD(hdr, l3_inner_offset));
	}
	else
#endif
	{
		PROC_PRINTF("	orig_packet_len(14): 0x%04X(%d), l4_offset(8): 0x%02X, l4_inner_offset(8): 0x%02X, l3_offset(8): 0x%02X, l3_inner_offset(8): 0x%02X\n",
			HDR_I_HW_FIELD(hdr, orig_packet_len), HDR_I_HW_FIELD(hdr, orig_packet_len), HDR_I_HW_FIELD(hdr, l4_offset), HDR_I_HW_FIELD(hdr, l4_inner_offset), HDR_I_HW_FIELD(hdr, l3_offset), HDR_I_HW_FIELD(hdr, l3_inner_offset));
	}

	PROC_PRINTF("	l4_rdp_hdr_len(8): 0x%02X, ori_pppoe_tag: %u, ori_inner_pppoe_tag: %u\n",
		HDR_I_HW_FIELD(hdr, l4_rdp_hdr_len), HDR_I_HW_FIELD(hdr, ori_pppoe_tag), HDR_I_HW_FIELD(hdr, ori_inner_pppoe_tag));
	PROC_PRINTF("	l3_outer_popped: %u, drop_src(6): 0x%02X\n",
		HDR_I_HW_FIELD(hdr, l3_outer_popped), HDR_I_HW_FIELD(hdr, drop_src));

	// --------------------------- //

	PROC_PRINTF("<CPU Header>\n");
	PROC_PRINTF("	fwd_vld(9): 0x%03X\n", HDR_I_HW_FIELD(hdr, fwd_vld));
	PROC_PRINTF("	  [CLS: %s]\n", (HDR_I_HW_FIELD(hdr, fwd_vld) & 0x1) ? COLOR_H "Hit" COLOR_NM : "NO Hit");
	PROC_PRINTF("	  [Hash(type-0): ");
	switch ((HDR_I_HW_FIELD(hdr, fwd_vld) >> 1) & 0x3) {
	case 0: PROC_PRINTF("NO Hit"); break;
	case 1: PROC_PRINTF(COLOR_H"Hit Overflow Entry"COLOR_NM); break;
	case 2: PROC_PRINTF(COLOR_H"Hit Cache Hash Entry"COLOR_NM); break;
	case 3: PROC_PRINTF(COLOR_H"Hit Main Hash Entry"COLOR_NM); break;
	}
	PROC_PRINTF(", Hash(type-1): ");
	switch ((HDR_I_HW_FIELD(hdr, fwd_vld) >> 3) & 0x3) {
	case 0: PROC_PRINTF("NO Hit"); break;
	case 1: PROC_PRINTF(COLOR_H"Hit Overflow Entry"COLOR_NM); break;
	case 2: PROC_PRINTF(COLOR_H"Hit Cache Hash Entry"COLOR_NM); break;
	case 3: PROC_PRINTF(COLOR_H"Hit Main Hash Entry"COLOR_NM); break;
	}
	PROC_PRINTF("]\n");
	PROC_PRINTF("	  [cos remarking hit: %s]\n", ((HDR_I_HW_FIELD(hdr, fwd_vld) >> 5) & 0x1) ? COLOR_H"Hit"COLOR_NM : "NO Hit");
	PROC_PRINTF("	  [ldpid remapping hit: %s]\n", ((HDR_I_HW_FIELD(hdr, fwd_vld) >> 6) & 0x1) ? COLOR_H"Hit"COLOR_NM : "NO Hit");
#if defined(CONFIG_FC_RTL8277C_SERIES)
	PROC_PRINTF("	cls_action(12): 0x%03X [std_fib: %u, fib_index: %u]\n",
		HDR_I_HW_FIELD(hdr, cls_action), (HDR_I_HW_FIELD(hdr, cls_action) & 0x400) >> 10, HDR_I_HW_FIELD(hdr, cls_action) & 0x3ff);
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("	cls_action(10): 0x%03X [std_fib: %u, fib_index: %u]\n",
		HDR_I_HW_FIELD(hdr, cls_action), (HDR_I_HW_FIELD(hdr, cls_action) & 0x200) >> 9, HDR_I_HW_FIELD(hdr, cls_action) & 0x1ff);
#endif
	PROC_PRINTF("	hash_dbl_chk_fail(3): 0x%X\n", HDR_I_HW_FIELD(hdr, hash_dbl_chk_fail));
	if (HDR_I_HW_FIELD(hdr, hash_dbl_chk_fail) & 0x1) {
		PROC_PRINTF("	  ["COLOR_H"Hash Engine double check fail (type-0)"COLOR_NM"]\n");
	}
	if (HDR_I_HW_FIELD(hdr, hash_dbl_chk_fail) & 0x2) {
		PROC_PRINTF("	  ["COLOR_H"Hash Engine double check fail (type-1)"COLOR_NM"]\n");
	}

	PROC_PRINTF("	hash_idx(21): 0x%06X, cache_way(5): 0x%02X\n", HDR_I_HW_FIELD(hdr, hash_idx), HDR_I_HW_FIELD(hdr, cache_way));
	if (((HDR_I_HW_FIELD(hdr, fwd_vld) >> 1) & 0x3) == 1) {
		PROC_PRINTF("	  [overflow_hash_idx(6): 0x%02X]\n", HDR_I_HW_FIELD(hdr, hash_idx) & 0x3f);
	}
	else if (((HDR_I_HW_FIELD(hdr, fwd_vld) >> 1) & 0x3) == 2) {
		PROC_PRINTF("	  [cache_hash_idx(11): 0x%03X, hash_profile(4): 0x%X]\n", HDR_I_HW_FIELD(hdr, hash_idx) & 0x7ff, (HDR_I_HW_FIELD(hdr, hash_idx) >> 16) & 0xf);
	}
	else if (((HDR_I_HW_FIELD(hdr, fwd_vld) >> 1) & 0x3) == 3) {
		PROC_PRINTF("	  [hash_idx(16): 0x%04X, hash_profile(4): 0x%X]\n", HDR_I_HW_FIELD(hdr, hash_idx) & 0xffff, (HDR_I_HW_FIELD(hdr, hash_idx) >> 16) & 0xf);
	}

	// --------------------------- //

	/* Some HDR_A fields filled by L2FE/NI are manipulated by L3FE.PP temporarily:
	 *	 HDR_A.ldpid --> HDR_I.lspid
	 *	 HDR_A.lspid --> HDR_I.o_lspid
	 *	 HDR_A.mcgid --> HDR_I.mcgid(ldpid)
	 * These manipulated fields are restored to proper fields by STG1.
	 *
	 * If L3FE_STG0_CTRL.lpb_idx_mode is 0, PP uses HDR_A.ldpid to index reg L3FE_STG0_LDPID_MAP.
	 * If L3FE_STG0_CTRL.lpb_idx_mode is 1, PP uses HDR_A.lspid to index reg L3FE_STG0_LPB_IDX_TBL.
	 */
	PROC_PRINTF("<Source Port ID>\n");
	PROC_PRINTF("	pspid(4): 0x%X, lspid(6): 0x%02X, o_lspid(6): 0x%02X\n", HDR_I_HW_FIELD(hdr, pspid), HDR_I_HW_FIELD(hdr, lspid), HDR_I_HW_FIELD(hdr, o_lspid));

	// --------------------------- //

	PROC_PRINTF("<Destination Port ID>\n");
	PROC_PRINTF("	dpid_pri: %u, mc: %u, mcgid/ldpid(10): 0x%03X, deepq: %u, no_drop: %u, mrr_en: %u\n",
		HDR_I_HW_FIELD(hdr, dpid_pri), HDR_I_HW_FIELD(hdr, mc), HDR_I_HW_FIELD(hdr, mcgid), HDR_I_HW_FIELD(hdr, deepq), HDR_I_HW_FIELD(hdr, no_drop), HDR_I_HW_FIELD(hdr, mrr_en));
#if defined(CONFIG_FC_RTL8277C_SERIES)
	if(fc_db.controlFuc.p7_rxsel_l3fe && HDR_I_HW_FIELD(hdr, lspid) == RTK_FC_MAC_PORT_PON_LSPID_TO_L2FE &&  
									HDR_I_HW_FIELD(hdr, o_lspid) == RTK_FC_MAC_PORT_PON  && 
									(HDR_I_HW_FIELD(hdr, mcgid) == 0x10 || HDR_I_HW_FIELD(hdr, mcgid) == 0x11) && HDR_I_HW_FIELD(hdr, deepq) == 0) {
		PROC_PRINTF("	[L3FE trap] pon to cpu, no %s\n", "L2FE");
	}
	else if(fc_db.controlFuc.p7_rxsel_l3fe && HDR_I_HW_FIELD(hdr, lspid) == RTK_FC_MAC_PORT_PON_LSPID_TO_L2FE &&  
										HDR_I_HW_FIELD(hdr, o_lspid) == RTK_FC_MAC_PORT_PON && 
										HDR_I_HW_FIELD(hdr, mcgid) == 0x11 && HDR_I_HW_FIELD(hdr, deepq) == 1) {
		PROC_PRINTF("	[L3FE round#1] pon to cpu, redirect to %s first\n", COLOR_H"L2FE"COLOR_NM);
	}
	else if(fc_db.controlFuc.p7_rxsel_l3fe && HDR_I_HW_FIELD(hdr, lspid) == RTK_FC_MAC_PORT_PON_LSPID_TO_L2FE &&  
										HDR_I_HW_FIELD(hdr, o_lspid) == RTK_FC_MAC_PORT_PON_LSPID_TO_L2FE && 
										HDR_I_HW_FIELD(hdr, mcgid) == 0x11 && HDR_I_HW_FIELD(hdr, deepq) == 0) {
		PROC_PRINTF("	[L3FE round#2] pon to cpu \n");
	}
#endif
	// --------------------------- //

	PROC_PRINTF("<COS>\n");
	PROC_PRINTF("	cos: %u\n", HDR_I_HW_FIELD(hdr, cos));

	// --------------------------- //

	PROC_PRINTF("<Policer ID>\n");
	PROC_PRINTF("	pol_grp_id(3): 0x%X, pol_id(8): 0x%02X, pol_en(2): %u, pol_all_bypass: %u, qos_premark: %u\n",
		HDR_I_HW_FIELD(hdr, pol_grp_id), HDR_I_HW_FIELD(hdr, pol_id), HDR_I_HW_FIELD(hdr, pol_en), HDR_I_HW_FIELD(hdr, pol_all_bypass), HDR_I_HW_FIELD(hdr, qos_premark));
	PROC_PRINTF("	pol2_id_msb(3): 0x%X, pol2_en: %u, pol3_id(6): 0x%02X, pol3_en: %u\n",
		HDR_I_HW_FIELD(hdr, pol2_id_msb), HDR_I_HW_FIELD(hdr, pol2_en), HDR_I_HW_FIELD(hdr, pol3_id), HDR_I_HW_FIELD(hdr, pol3_en));

	// --------------------------- //

	if((HDR_I_HW_FIELD(hdr, keep_org_pkt)==0)&&(HDR_I_HW_FIELD(hdr, mcgid)>=0x10)&&(HDR_I_HW_FIELD(hdr, mcgid)<=0x17))
		snprintf(swid_str, SWIDSTRLEN, "%s", "to_wifi");
	else if((HDR_I_HW_FIELD(hdr, keep_org_pkt)==1)&&(HDR_I_HW_FIELD(hdr, o_lspid)!=0x7)&&(HDR_I_HW_FIELD(hdr, mdata_l) & 0xff))
		snprintf(swid_str, SWIDSTRLEN, "%s", "from_wifi");
	else if(HDR_I_HW_FIELD(hdr, o_lspid)==0x7)
		snprintf(swid_str, SWIDSTRLEN, "%s", "from_pon");

	PROC_PRINTF("<MDATA>\n");
	PROC_PRINTF("	mdata_h(32): 0x%08X, mdata_l(32): 0x%08X\n", HDR_I_HW_FIELD(hdr, mdata_h), HDR_I_HW_FIELD(hdr, mdata_l));
	PROC_PRINTF("	  sw_reason_code(4): 0x%X (%s), reason_code(4): 0x%X (%s), sw_id(%s)(8): 0x%02X", 
		MDATAL_REASON_SW(HDR_I_HW_FIELD(hdr, mdata_l)), sw_reason_code_str[MDATAL_REASON_SW(HDR_I_HW_FIELD(hdr, mdata_l))], (HDR_I_HW_FIELD(hdr, mdata_l) >> 8) & 0xf, reason_code_str[(HDR_I_HW_FIELD(hdr, mdata_l) >> 8) & 0xf],  swid_str, HDR_I_HW_FIELD(hdr, mdata_l) & 0xff);
	PROC_PRINTF("\n");

	if(phase < RTK_FC_HEADERI_PHASE_STG2_PE ) // before main hash fib resolution
	{
		//rtlglue_printf("	  [Before MainHash]\n");
#if defined(CONFIG_FC_RTL9607F_SERIES)
		PROC_PRINTF("	  pkt_type(4): 0x%X", (HDR_I_HW_FIELD(hdr, mdata_h) >> 16) & 0xff);
		switch((HDR_I_HW_FIELD(hdr, mdata_h) >> 16) & 0xff) {
#else
		PROC_PRINTF("	  pkt_type(4): 0x%X", (HDR_I_HW_FIELD(hdr, mdata_h) >> 16) & 0xf);
		switch((HDR_I_HW_FIELD(hdr, mdata_h) >> 16) & 0xf) {
#endif
			case 1: PROC_PRINTF("[AH]"); break;
			case 2: PROC_PRINTF("[ESP]"); break;
			case 3: PROC_PRINTF("[L2TP_over_IP(ctrl)]"); break;
			case 4: PROC_PRINTF("[L2TP_over_IP(data)]"); break;
			case 5: PROC_PRINTF("[[L2TP_over_UDP(ctrl)]]"); break;
			case 6: PROC_PRINTF("[[L2TP_over_UDP(data)]]"); break;
			case 7: PROC_PRINTF("[VxLAN_IP]"); break;
			case 8: PROC_PRINTF("[PPTP GRE]"); break;
			case 9: PROC_PRINTF("[IPv4 in IPv6]"); break;
			case 10: PROC_PRINTF("[IPv6 in IPv4]"); break;
			case 11: PROC_PRINTF("[IPv4 only]"); break;
			case 12: PROC_PRINTF("[IPv6 only]"); break;
			case 13: PROC_PRINTF("[VxLAN non IP]"); break;
#if defined(CONFIG_FC_RTL9607F_SERIES)
			case 14: PROC_PRINTF("[SRv6_IP]"); break;
			case 15: PROC_PRINTF("[SRv6_NON_IP]"); break;
			case 16: PROC_PRINTF("[L2GRE_IP]"); break;
			case 17: PROC_PRINTF("[L2GRE_NON_IP]"); break;
			case 18: PROC_PRINTF("%s", (ASICDRIVERVER!=CHIP_REV_ID_A)?"[rsvd]":"[IPv6 in IPv6]"); break;
			case 19: PROC_PRINTF("%s", (ASICDRIVERVER!=CHIP_REV_ID_A)?"[rsvd]":"[SRv6_IP_TNL]"); break;
			case 20: PROC_PRINTF("%s", (ASICDRIVERVER!=CHIP_REV_ID_A)?"[rsvd]":"[SRv6_INLINE]"); break;
			case 23: PROC_PRINTF("%s", (ASICDRIVERVER!=CHIP_REV_ID_A)?"[rsvd]":"[Eth_in_IPv4_IP_vld]"); break;
			case 24: PROC_PRINTF("%s", (ASICDRIVERVER!=CHIP_REV_ID_A)?"[rsvd]":"[Eth_in_IPv4_non_IP]"); break;
			case 25: PROC_PRINTF("%s", (ASICDRIVERVER!=CHIP_REV_ID_A)?"[rsvd]":"[IPv4_In_IPv4]"); break;
#endif
			default: PROC_PRINTF("[rsvd]"); break;
		}
		PROC_PRINTF(", dual_hdr_info(32): 0x%08X", ((HDR_I_HW_FIELD(hdr, mdata_h) & 0xffff) << 16) | ((HDR_I_HW_FIELD(hdr, mdata_h) >> 16) & 0xffff));
		switch((HDR_I_HW_FIELD(hdr, mdata_h) >> 16) & 0xf) {
			case 1: PROC_PRINTF("[Security Parameter Index(32)]\n"); break;
			case 2: PROC_PRINTF("[Security Parameter Index(32)]\n"); break;
			case 3: PROC_PRINTF("[Session ID(32)]\n"); break;
			case 4: PROC_PRINTF("[Session ID(32)]\n"); break;
			case 5: PROC_PRINTF("[Session ID(16), Tunnel ID(16)]\n"); break;
			case 6: PROC_PRINTF("[Session ID(16), Tunnel ID(16)]\n"); break;
			case 7: PROC_PRINTF("[rsvd(8), VNI(24)]\n"); break;
			case 8: PROC_PRINTF("[rsvd(16), call ID(16)]\n"); break;
			default: PROC_PRINTF("[rsvd(32)]\n"); break;
		}
	}else {
		//rtlglue_printf("	  [After MainHash]\n");
		PROC_PRINTF("	  CRC32(32): 0x%08X, CRC16(16): 0x%04X\n", HDR_I_HW_FIELD(hdr, mdata_h), (HDR_I_HW_FIELD(hdr, mdata_l) >> 16) & 0xffff);
	}

	// --------------------------- //

	PROC_PRINTF( "<Special Packet>\n");
#if defined(CONFIG_FC_RTL8277C_SERIES)
	PROC_PRINTF("	spcl_pkt_enc(6): 0x%02X, spcl_pkt_hdr_mtch(8): 0x%02X\n", HDR_I_HW_FIELD(hdr, spcl_pkt_enc), HDR_I_HW_FIELD(hdr, spcl_pkt_hdr_mtch));
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("	spcl_pkt_hdr_mtch(8): 0x%02X\n", HDR_I_HW_FIELD(hdr, spcl_pkt_hdr_mtch));
#endif
	// --------------------------- //


	PROC_PRINTF("<L2>\n");
	PROC_PRINTF("	l2_pkt_type: %u ", HDR_I_HW_FIELD(hdr, l2_pkt_type));
	switch (HDR_I_HW_FIELD(hdr, l2_pkt_type)) {
		case 0: PROC_PRINTF("[L2 Unicast]\n"); break;
		case 1: PROC_PRINTF("[L2 Multicast]\n"); break;
		case 2: PROC_PRINTF("[L2 Broadcast]\n"); break;
		default: PROC_PRINTF("[Unknown Unicast/Multicast]\n"); break;
	}

	PROC_PRINTF("	mac_da_ip_mc: %u, mac_da_rng: %u, mac_da_rsvd: %u\n",
		HDR_I_HW_FIELD(hdr, mac_da_ip_mc), HDR_I_HW_FIELD(hdr, mac_da_rng), HDR_I_HW_FIELD(hdr, mac_da_rsvd));
	PROC_PRINTF("	mac_da: %02X-%02X-%02X-%02X-%02X-%02X, mac_da_an_sel(4): 0x%X\n",
		HDR_I_HW_FIELD(hdr, mac_da_5), HDR_I_HW_FIELD(hdr, mac_da_4), HDR_I_HW_FIELD(hdr, mac_da_3), HDR_I_HW_FIELD(hdr, mac_da_2), HDR_I_HW_FIELD(hdr, mac_da_1), HDR_I_HW_FIELD(hdr, mac_da_0), HDR_I_HW_FIELD(hdr, mac_da_an_sel));
#if defined(CONFIG_FC_RTL8277C_SERIES)
	PROC_PRINTF("	mac_sa: %02X-%02X-%02X-%02X-%02X-%02X, ethertype(16): 0x%04X, ethertype_enc(6): 0x%02X\n",
		HDR_I_HW_FIELD(hdr, mac_sa_5), HDR_I_HW_FIELD(hdr, mac_sa_4), HDR_I_HW_FIELD(hdr, mac_sa_3), HDR_I_HW_FIELD(hdr, mac_sa_2), HDR_I_HW_FIELD(hdr, mac_sa_1), HDR_I_HW_FIELD(hdr, mac_sa_0),
		HDR_I_HW_FIELD(hdr, ethertype), HDR_I_HW_FIELD(hdr, ethertype_enc));

	PROC_PRINTF("	llc_snap(2): %u, len_encoded: %u, pktlen_rng_match_vec(4): 0x%X, llc_type_enc(2): %u, pad_len(6): 0x%02X\n",
		HDR_I_HW_FIELD(hdr, llc_snap), HDR_I_HW_FIELD(hdr, len_encoded), HDR_I_HW_FIELD(hdr, pktlen_rng_match_vec), HDR_I_HW_FIELD(hdr, llc_type_enc), HDR_I_HW_FIELD(hdr, pad_len));
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("	mac_sa: %02X-%02X-%02X-%02X-%02X-%02X, ethertype(16): 0x%04X, ethertype_enc(4): 0x%02X\n",
		HDR_I_HW_FIELD(hdr, mac_sa_5), HDR_I_HW_FIELD(hdr, mac_sa_4), HDR_I_HW_FIELD(hdr, mac_sa_3), HDR_I_HW_FIELD(hdr, mac_sa_2), HDR_I_HW_FIELD(hdr, mac_sa_1), HDR_I_HW_FIELD(hdr, mac_sa_0),
		HDR_I_HW_FIELD(hdr, ethertype), HDR_I_HW_FIELD(hdr, ethertype_enc));

	PROC_PRINTF("	llc_snap(2): %u, len_encoded: %u, pktlen_rng_match_vec(4): 0x%X, llc_type_enc(2): %u, pad_len(6): 0x%02X, vlan_snap_format(2): 0x%X\n",
		HDR_I_HW_FIELD(hdr, llc_snap), HDR_I_HW_FIELD(hdr, len_encoded), HDR_I_HW_FIELD(hdr, pktlen_rng_match_vec), HDR_I_HW_FIELD(hdr, llc_type_enc), HDR_I_HW_FIELD(hdr, pad_len), HDR_I_HW_FIELD(hdr, vlan_snap_format));
#endif


	// --------------------------- //

	PROC_PRINTF("<VLAN>\n");
	PROC_PRINTF("	vlan_cnt: %u\n", HDR_I_HW_FIELD(hdr, vlan_cnt));
	PROC_PRINTF("	top_tpid_enc(3): %u, top_vid(12): 0x%03X, top_dei: %u\n", HDR_I_HW_FIELD(hdr, top_tpid_enc), HDR_I_HW_FIELD(hdr, top_vid), HDR_I_HW_FIELD(hdr, top_dei));
	PROC_PRINTF("	inner_tpid_enc(3): %u, inner_vid(12): 0x%03X, inner_dei: %u\n", HDR_I_HW_FIELD(hdr, inner_tpid_enc), HDR_I_HW_FIELD(hdr, inner_vid), HDR_I_HW_FIELD(hdr, inner_dei));

	// --------------------------- //

	PROC_PRINTF("<802.1p>\n");
	PROC_PRINTF("	top_802_1p(3): %u, inner_802_1p(3): %u\n", HDR_I_HW_FIELD(hdr, top_802_1p), HDR_I_HW_FIELD(hdr, inner_802_1p));

	// --------------------------- //

	PROC_PRINTF("<PPP / PPPoE>\n");
#if defined(CONFIG_FC_RTL8277C_SERIES)
	PROC_PRINTF("	pppoe_type: %u [%s], pppoe_code_enc(4): 0x%X, pppoe_session_id(16): 0x%04X, ppp_protocol_enc(4): 0x%X\n",
		HDR_I_HW_FIELD(hdr, pppoe_type),
		(HDR_I_HW_FIELD(hdr, pppoe_type) == 0) ? "INVALID" : ((HDR_I_HW_FIELD(hdr, pppoe_type) == 1) ? "PPPoE Discovery" : "PPPoE Session"),
		HDR_I_HW_FIELD(hdr, pppoe_code_enc), HDR_I_HW_FIELD(hdr, pppoe_session_id), HDR_I_HW_FIELD(hdr, ppp_protocol_enc));
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("	pppoe_type: %u [%s], pppoe_session_id(16): 0x%04X, ppp_protocol_enc(4): 0x%X\n",
		HDR_I_HW_FIELD(hdr, pppoe_type),
		(HDR_I_HW_FIELD(hdr, pppoe_type) == 0) ? "INVALID" : ((HDR_I_HW_FIELD(hdr, pppoe_type) == 1) ? "PPPoE Discovery" : "PPPoE Session"),
		HDR_I_HW_FIELD(hdr, pppoe_session_id), HDR_I_HW_FIELD(hdr, ppp_protocol_enc));
#endif

	// --------------------------- //

	PROC_PRINTF("<L3>\n");
#if defined(CONFIG_FC_RTL8277C_SERIES)
	PROC_PRINTF("	ip_vld: %u, ip_ver: %u [%s], ip_mtu_enc(4): %u, ip_mtu_en: %u\n",
		HDR_I_HW_FIELD(hdr, ip_vld), HDR_I_HW_FIELD(hdr, ip_ver),
		(HDR_I_HW_FIELD(hdr, ip_ver) == 0) ? "IPv4" : "IPv6",
		HDR_I_HW_FIELD(hdr, ip_mtu_enc), HDR_I_HW_FIELD(hdr, ip_mtu_en));
#elif defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("	ip_vld: %u, ip_ver: %u [%s]\n",
		HDR_I_HW_FIELD(hdr, ip_vld), HDR_I_HW_FIELD(hdr, ip_ver),
		(HDR_I_HW_FIELD(hdr, ip_ver) == 0) ? "IPv4" : "IPv6");
#endif

	PROC_PRINTF("	ip_protocol(8): 0x%02X, ip_ihl(4): 0x%X\n", HDR_I_HW_FIELD(hdr, ip_protocol), HDR_I_HW_FIELD(hdr, ip_ihl));
	PROC_PRINTF("	ip_l4_type(3): %u ", HDR_I_HW_FIELD(hdr, ip_l4_type));
	switch (HDR_I_HW_FIELD(hdr, ip_l4_type)) {
		case 1: PROC_PRINTF("[TCP]\n"); break;
		case 2: PROC_PRINTF("[UDP]\n"); break;
		case 3: PROC_PRINTF("[UDP-Lite]\n"); break;
		case 4: PROC_PRINTF("[RDPv1]\n"); break;
		case 5: PROC_PRINTF("[RDPv2]\n"); break;
		default: PROC_PRINTF("[L4 INVALID]\n"); break;
	}

	if(HDR_I_HW_FIELD(hdr, ip_ver) == 0){
		address_src[0] = HDR_I_HW_FIELD(hdr, ip_sa_0);
		address_dest[0] = HDR_I_HW_FIELD(hdr, ip_da_0);
		PROC_PRINTF("	ip_sa: %08X-%08X-%08X-%08X (%pI4h)\n",
			HDR_I_HW_FIELD(hdr, ip_sa_3), HDR_I_HW_FIELD(hdr, ip_sa_2), HDR_I_HW_FIELD(hdr, ip_sa_1), HDR_I_HW_FIELD(hdr, ip_sa_0), &address_src[0]);
		PROC_PRINTF("	ip_da: %08X-%08X-%08X-%08X (%pI4h), ip_da_mc: %u, ip_da_sa_equal: %u\n",
			HDR_I_HW_FIELD(hdr, ip_da_3), HDR_I_HW_FIELD(hdr, ip_da_2), HDR_I_HW_FIELD(hdr, ip_da_1), HDR_I_HW_FIELD(hdr, ip_da_0), &address_dest[0], HDR_I_HW_FIELD(hdr, ip_da_mc), HDR_I_HW_FIELD(hdr, ip_da_sa_equal));
	}else{
		address_src[0] = ntohl(HDR_I_HW_FIELD(hdr, ip_sa_3));
		address_dest[0] = ntohl(HDR_I_HW_FIELD(hdr, ip_da_3));
		address_src[1] = ntohl(HDR_I_HW_FIELD(hdr, ip_sa_2));
		address_dest[1] = ntohl(HDR_I_HW_FIELD(hdr, ip_da_2));
		address_src[2] = ntohl(HDR_I_HW_FIELD(hdr, ip_sa_1));
		address_dest[2] = ntohl(HDR_I_HW_FIELD(hdr, ip_da_1));
		address_src[3] = ntohl(HDR_I_HW_FIELD(hdr, ip_sa_0));
		address_dest[3] = ntohl(HDR_I_HW_FIELD(hdr, ip_da_0));
		PROC_PRINTF("	ip_sa: %08X-%08X-%08X-%08X (%pI6c)\n",
			HDR_I_HW_FIELD(hdr, ip_sa_3), HDR_I_HW_FIELD(hdr, ip_sa_2), HDR_I_HW_FIELD(hdr, ip_sa_1), HDR_I_HW_FIELD(hdr, ip_sa_0), address_src);
		PROC_PRINTF("	ip_da: %08X-%08X-%08X-%08X (%pI6c), ip_da_mc: %u, ip_da_sa_equal: %u\n",
			HDR_I_HW_FIELD(hdr, ip_da_3), HDR_I_HW_FIELD(hdr, ip_da_2), HDR_I_HW_FIELD(hdr, ip_da_1), HDR_I_HW_FIELD(hdr, ip_da_0), address_dest, HDR_I_HW_FIELD(hdr, ip_da_mc), HDR_I_HW_FIELD(hdr, ip_da_sa_equal));
	}
	PROC_PRINTF("	ipv6_flow_lbl(20): 0x%05X, ip_ttl(8): 0x%02X, ip_options: %u, ip_fragment: %u\n",
		HDR_I_HW_FIELD(hdr, ipv6_flow_lbl), HDR_I_HW_FIELD(hdr, ip_ttl), HDR_I_HW_FIELD(hdr, ip_options), HDR_I_HW_FIELD(hdr, ip_fragment));
	PROC_PRINTF("	ipv6_ndp: %u, ipv6_hbh: %u, ipv6_rh: %u, ipv6_doh: %u\n",
		HDR_I_HW_FIELD(hdr, ipv6_ndp), HDR_I_HW_FIELD(hdr, ipv6_hbh), HDR_I_HW_FIELD(hdr, ipv6_rh), HDR_I_HW_FIELD(hdr, ipv6_doh));

	PROC_PRINTF("	icmp_vld(3): %u ", HDR_I_HW_FIELD(hdr, icmp_vld));
	switch (HDR_I_HW_FIELD(hdr, icmp_vld)) {
		case 3: PROC_PRINTF("[ICMPv6 NDP]"); break;
		case 4: PROC_PRINTF("[ICMPv4]"); break;
		case 5: PROC_PRINTF("[ICMPv6]"); break;
		case 6: PROC_PRINTF("[IGMP]"); break;
		case 7: PROC_PRINTF("[MLD]"); break;
		default: PROC_PRINTF("[INVALID]"); break;
	}
	PROC_PRINTF(", icmp_type(8): 0x%02X\n", HDR_I_HW_FIELD(hdr, icmp_type));

	PROC_PRINTF("	gre_seq_vld: %u , gre_seq(32): 0x%08X\n", HDR_I_HW_FIELD(hdr, gre_seq_vld), HDR_I_HW_FIELD(hdr, gre_seq));
	if(phase >= RTK_FC_HEADERI_PHASE_STG2_PE) // after main hash fib resolution
	{
		PROC_PRINTF("	  [After MainHash]\n");
		PROC_PRINTF("	  === dual upstream info ===\n");
		PROC_PRINTF("	  {MAP-E/MAP-T} FMR_IDX(2): %u\n", HDR_I_HW_FIELD(hdr, gre_seq) & 0x3);
		PROC_PRINTF("	  {6RD}         6rd_dip_remap(1): %u\n", HDR_I_HW_FIELD(hdr, gre_seq) & 0x1);
		PROC_PRINTF("	  {VXLAN}       vxlan_sport_update_en(1): %u,  vxlan_sport(16): 0x%04X\n", (HDR_I_HW_FIELD(hdr, gre_seq) >> 15) & 0x1, (HDR_I_HW_FIELD(hdr, gre_seq) >> 16) & 0xFFFF);
		PROC_PRINTF("	  === dual downstream info ===\n");
		PROC_PRINTF("	  content_hdr_format(2): %d\n", (HDR_I_HW_FIELD(hdr, gre_seq) >> 9)&0x3);
		PROC_PRINTF("	  l2tp_type: %u %s\n", (HDR_I_HW_FIELD(hdr, gre_seq) >> 13)&0x1, ((HDR_I_HW_FIELD(hdr, gre_seq) >> 13)&0x1)?"(L2TP over UDP)":" ");
		PROC_PRINTF("	  === other info ===\n");
		PROC_PRINTF("	  pppoe_pad_ctrl: %u\n", (HDR_I_HW_FIELD(hdr, gre_seq) >> 14)&0x1);
		PROC_PRINTF("	  ======================\n");
	}
	else if(phase >= RTK_FC_HEADERI_PHASE_STG1_STG2)// after Stage-1
	{
		PROC_PRINTF("	  [After Stage-1] dual downstream info\n");
		PROC_PRINTF("	  dual_outer_DSCP/TOS/TC(8): 0x%02X, mainHash dscp/ecn_identify:%s, content_hdr_format(2): %d\n",
			HDR_I_HW_FIELD(hdr, gre_seq) & 0xFF, (HDR_I_HW_FIELD(hdr, gre_seq) >> 8)&0x1?"by dual_outer_DSCP/TOS/TC":"by header-I", (HDR_I_HW_FIELD(hdr, gre_seq) >> 9)&0x3);
		PROC_PRINTF("	  lookup_fmr_idx(2): %u %s\n", (HDR_I_HW_FIELD(hdr, gre_seq) >> 11)&0x3, (((HDR_I_HW_FIELD(hdr, gre_seq) >> 11)&0x3) == 0x3)?"(un-matched(DMR))":" ");
		PROC_PRINTF("	  l2tp_type: %u\n", (HDR_I_HW_FIELD(hdr, gre_seq) >> 13)&0x1);

	}
	PROC_PRINTF("	l3_chksum(16): 0x%04X, l3_chksum_err: %u\n", HDR_I_HW_FIELD(hdr, l3_chksum), HDR_I_HW_FIELD(hdr, l3_chksum_err));
	PROC_PRINTF("	l3_total_len(14): 0x%04X(%d)\n", HDR_I_HW_FIELD(hdr, l3_total_len), HDR_I_HW_FIELD(hdr, l3_total_len));

	// --------------------------- //

	PROC_PRINTF("<QoS DSCP>\n");
	PROC_PRINTF("	ip_dscp(6): 0x%02X, ip_dscp_markdown_en: %u, ip_dscp_marked_down(6): 0x%02X, ip_ecn(2): %u, ip_ecn_en: %u\n",
		HDR_I_HW_FIELD(hdr, ip_dscp), HDR_I_HW_FIELD(hdr, ip_dscp_markdown_en), HDR_I_HW_FIELD(hdr, ip_dscp_marked_down), HDR_I_HW_FIELD(hdr, ip_ecn), HDR_I_HW_FIELD(hdr, ip_ecn_en));

	// --------------------------- //
	PROC_PRINTF("<L4>\n");
	PROC_PRINTF("	l4_sp(16): 0x%04X(%d), l4_dp(16): 0x%04X(%d), l4_port_rng_match_vec(32): 0x%08X\n",
		HDR_I_HW_FIELD(hdr, l4_sp), HDR_I_HW_FIELD(hdr, l4_sp), HDR_I_HW_FIELD(hdr, l4_dp), HDR_I_HW_FIELD(hdr, l4_dp), HDR_I_HW_FIELD(hdr, l4_port_rng_match_vec));
	PROC_PRINTF("	tcp_rdp_ctrl(9): 0x%03X", HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl));
	switch (HDR_I_HW_FIELD(hdr, ip_l4_type)) {
		case 1: PROC_PRINTF("[TCP] N: %u, C: %u, E: %u, U: %u, A: %u, P: %u, R: %u, S: %u, F: %u\n",
			(HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 8)&0x1, (HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 7)&0x1, (HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 6)&0x1, (HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 5)&0x1, (HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 4)&0x1,
			(HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 3)&0x1, (HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 2)&0x1, (HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 1)&0x1, HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl)&0x1);
		break;
		case 4:
		case 5: PROC_PRINTF("[RDP S: %u, A: %u, E: %u, R: %u, N: %u, VER: %u]\n",
			(HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 7)&0x1, (HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 6)&0x1, (HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 5)&0x1, (HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 4)&0x1,
			(HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl) >> 3)&0x1, HDR_I_HW_FIELD(hdr, tcp_rdp_ctrl)&0x3);
		break;
		default: PROC_PRINTF("\n"); break;
	}
	PROC_PRINTF("	l4_chksum_zero: %u, l4_chksum(32): 0x%08X\n", HDR_I_HW_FIELD(hdr, l4_chksum_zero), HDR_I_HW_FIELD(hdr, l4_chksum));
}

#else
static void dump_l3fe_hdr_i(struct seq_file *s, L3FE_HDR_I_t *hdr)
{
	char *name_mdata_h_trap_rsn[RXINFO_REF_RSN_MAX]={"0x0","ACL","FLOWMISS","UNKNOWN_DA","ACL to PS","","","",""};	//refer rxinfo_ref_trap_rsn
#if defined(CONFIG_FC_CAG3_SERIES)
	char *name_t2_ctrl[CA_FLOW_TYPE_MAX]={"(UC5_DS)","(UC5_US)","(MC)","(UC2)","(CLS_DOS_POL)","","",""};	//refer rg_ca_flow_profile_keytype_t
	char *name_t4_ctrl[16]={"UUC","UUC","UUC","UUC","UUC","UUC","UUC","UUC","UUC","UUC","UUC","UUC","UUC","PPPoE_RM","RSVD","Bypass"};
#elif defined(CONFIG_FC_CA8277B_SERIES)
	char *name_t2_ctrl[CA_FLOW_TYPE_MAX]={"(UC5_DS)","(UC5_US)","(MC)","(UC2)","(CLS_TRAP_GRPPOL)","","",""};
	char *name_t4_ctrl[16]={"SMAC","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","Bypass"};
#else
	char *name_t2_ctrl[CA_FLOW_TYPE_MAX]={"(UC5_DS)","(UC5_US)","(MC)","(UC2)","","","",""};
	char *name_t4_ctrl[16]={"RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","RSVD","Bypass"};
#endif

	PROC_PRINTF("<Table Control>\n");
	PROC_PRINTF("  cls: 0x%X mainhash: 0x%X%s hashlite: 0x%X(%s)",
		hdr->t1_ctrl, hdr->t2_ctrl, ((hdr->t2_ctrl == 0xF)? "" : name_t2_ctrl[hdr->t2_ctrl]), hdr->t4_ctrl, hdr->t4_ctrl<16?name_t4_ctrl[hdr->t4_ctrl]:"ERR");
	if((hdr->t1_ctrl == 0xF) && (hdr->t2_ctrl == 0xF) && (hdr->t3_ctrl == 0xF) && (hdr->t4_ctrl == 0xF))
		PROC_PRINTF(COLOR_H"(may hit special packet)"COLOR_NM);
	PROC_PRINTF("\n");
	PROC_PRINTF("  orig_packet_len: %d\n", hdr->orig_packet_len);

	// --------------------------- //

	PROC_PRINTF("<CPU Header>\n");
	PROC_PRINTF("    [CLS: %s]\n", (hdr->fwd_vld & 0x1) ?  COLOR_H "Hit" COLOR_NM : "NO Hit");
	PROC_PRINTF("    [Hash(type-0): ");
	switch ((hdr->fwd_vld >> 1) & 0x3) {
	case 0: PROC_PRINTF("NO Hit"); break;
	case 1: PROC_PRINTF(COLOR_H"Hit Overflow Entry"COLOR_NM); break;
	case 2: PROC_PRINTF(COLOR_H"Hit Cache Hash Entry"COLOR_NM); break;
	case 3: PROC_PRINTF(COLOR_H"Hit Main Hash Entry"COLOR_NM); break;
	default: PROC_PRINTF("UNKNOWN"); break;
	}
	PROC_PRINTF(", Hash(type-1): ");
	switch ((hdr->fwd_vld >> 3) & 0x3) {
	case 0: PROC_PRINTF("NO Hit"); break;
	case 1: PROC_PRINTF(COLOR_H"Hit Overflow Entry"COLOR_NM); break;
	case 2: PROC_PRINTF(COLOR_H"Hit Cache Hash Entry"COLOR_NM); break;
	case 3: PROC_PRINTF(COLOR_H"Hit Main Hash Entry"COLOR_NM); break;
	default: PROC_PRINTF("UNKNOWN"); break;
	}
	PROC_PRINTF("]\n");
	PROC_PRINTF("    [HashLite(type-0): ");
	switch ((hdr->fwd_vld >> 6) & 0x3) {
	case 0:
		PROC_PRINTF("NO Hit"); break;
	case 1:
		PROC_PRINTF(COLOR_H"Hit Overflow Entry"COLOR_NM); break;
	case 2:
	case 3:
		PROC_PRINTF(COLOR_H"Hit Hash Entry"COLOR_NM); break;
	default:
		PROC_PRINTF("UNKNOWN"); break;
	}
	PROC_PRINTF(", HashLite(type-1): %s]\n", (hdr->fwd_vld & 0x100) ? COLOR_H"Hit"COLOR_NM : "NO Hit");

	PROC_PRINTF("  cls_action: 0x%03X [std_fib: %u, fib_index: %u]\n",
		hdr->cls_action, (hdr->cls_action & 0x400) >> 10, hdr->cls_action & 0x3ff);

	if (hdr->hash_dbl_chk_fail & 0x1) {
		PROC_PRINTF("    [Hash Engine double check fail (type-0)]\n");
	}
	if (hdr->hash_dbl_chk_fail & 0x2) {
		PROC_PRINTF("    [Hash Engine double check fail (type-1)]\n");
	}
	if (hdr->hash_dbl_chk_fail & 0x4) {
		PROC_PRINTF("    [HashLite Engine double check fail]\n");
	}

	PROC_PRINTF("  hash_idx: %d  cache_way: %d\n", hdr->hash_idx, hdr->cache_way);
	if (((hdr->fwd_vld >> 1) & 0x3) == 1) {
		PROC_PRINTF("    [overflow_hash_idx: %d]\n", hdr->hash_idx & 0x3f);
	}
	if (((hdr->fwd_vld >> 1) & 0x2) == 2) {
		PROC_PRINTF("    [hash_idx: %d hash_profile: %d]\n", hdr->hash_idx & 0xffff, (hdr->hash_idx >> 16) & 0xf);
	}
	if ((hdr->fwd_vld >> 5) & 0x1) {
#ifdef CONFIG_ARCH_CORTINA_G3LITE
		PROC_PRINTF("    [lpm_idx: %d lpm_sw_idx: %d]\n",
			hdr->hash_idx & 0x3ff,
			((hdr->hash_idx & 0x3e0) >> 1) | (hdr->hash_idx & 0xf));
#else
		PROC_PRINTF("    [lpm_idx: %d]\n", hdr->hash_idx & 0x3ff);
#endif
	}
	if (((hdr->fwd_vld >> 6) & 0x3) == 1) {
#ifdef CONFIG_ARCH_CORTINA_G3LITE
		PROC_PRINTF("    [hashlite_overflow_idx: 0x%02X]\n", (hdr->hash_idx >> 10) & 0xff);
#else
		PROC_PRINTF("    [hashlite_overflow_idx): 0x%03X]\n", (hdr->hash_idx >> 10) & 0x7ff);
#endif
	}
	if (((hdr->fwd_vld >> 6) & 0x2) == 2) {
#ifdef CONFIG_ARCH_CORTINA_G3LITE
		PROC_PRINTF("    [hashlite_idx: 0x%02X]\n", (hdr->hash_idx >> 10) & 0xff);
#else
		PROC_PRINTF("    [hashlite_idx): 0x%03X]\n", (hdr->hash_idx >> 10) & 0x7ff);
#endif
	}

	// --------------------------- //

	/* Some HDR_A fields filled by L2FE/NI are manipulated by L3FE.PP temporarily:
	 *   HDR_A.ldpid --> HDR_I.lspid
	 *   HDR_A.lspid --> HDR_I.o_lspid
	 *   HDR_A.mcgid --> HDR_I.mcgid(ldpid)
	 * These manipulated fields are restored to proper fields by STG1.
	 *
	 * If L3FE_STG0_CTRL.lpb_idx_mode is 0, PP uses HDR_A.ldpid to index reg L3FE_STG0_LDPID_MAP.
	 * If L3FE_STG0_CTRL.lpb_idx_mode is 1, PP uses HDR_A.lspid to index reg L3FE_STG0_LPB_IDX_TBL.
	 */
	PROC_PRINTF("<Source Port ID>\n");
	PROC_PRINTF("  pspid: 0x%X lspid: 0x%02X o_lspid: 0x%02X\n", hdr->pspid, hdr->lspid, hdr->o_lspid);

	// --------------------------- //

	PROC_PRINTF("<Destination Port ID>\n");
	PROC_PRINTF("  dpid_pri: %u mc: %u mcgid/ldpid: 0x%03X deepq: %u no_drop: %u mrr_en: %u\n",
		hdr->dpid_pri, hdr->mc, hdr->mcgid, hdr->deepq, hdr->no_drop, hdr->mrr_en);

	// --------------------------- //

	PROC_PRINTF("<QOS>\n");
	PROC_PRINTF("  cos: %u  pol_grp_id: 0x%X pol_id: 0x%03X pol_en: %u pol_all_bypass: %u qos_premark: %u\n", hdr->cos,
		hdr->pol_grp_id, hdr->pol_id, hdr->pol_en, hdr->pol_all_bypass, hdr->qos_premark);
#if defined(CONFIG_FC_CA8277B_SERIES)
	PROC_PRINTF("  pol2_id_msb(9): 0x%03X, pol2_en: %u, pol3_id(9): 0x%03X, pol3_en: %u\n",
		hdr->pol2_id_msb, hdr->pol2_en, hdr->pol3_id, hdr->pol3_en);
#endif

	// --------------------------- //

	PROC_PRINTF("<MDATA>\n");
	PROC_PRINTF("  mdata_h: 0x%08X, mdata_l: 0x%08X\n", hdr->mdata_h, hdr->mdata_l);
#ifdef CONFIG_ARCH_CORTINA_G3LITE
	PROC_PRINTF("    [spcl_pkt_enc(6): 0x%02X, L3_ingress_if_id(8): 0x%02X, L3_egress_if_id(8): 0x%02X, lpm_result(12): 0x%03X, mc_fib_id(8): 0x%02X]\n",
		(hdr->mdata_h >> 26) & 0x3f, (hdr->mdata_l >> 24) & 0xff, (hdr->mdata_l >> 16) & 0xff, hdr->mdata_l & 0xfff, hdr->mdata_l & 0xff);
#else
	PROC_PRINTF("  [ingress_if_id: 0x%02X, egress_if_id: 0x%02X, sw_id: %d]\n",
		(hdr->mdata_l >> 24) & 0xff, (hdr->mdata_l >> 16) & 0xff, hdr->mdata_l & 0xffff);
#endif
	PROC_PRINTF("  [trap_rsn: %s, acl_rsn: 0x%03X]\n", name_mdata_h_trap_rsn[(hdr->mdata_h&RXINFO_REF_TRAP_RSN_BIT)>>RXINFO_REF_TRAP_RSN_SHIFT_BIT], (hdr->mdata_h&RXINFO_REF_ACL_RSN_BIT));

	// --------------------------- //

	if(hdr->spcl_pkt_enc != 0) {
		PROC_PRINTF("<Special Packet>\n");
		PROC_PRINTF("  spcl_pkt_enc(6): 0x%02X, spcl_pkt_hdr_mtch(8): 0x%02X\n", hdr->spcl_pkt_enc, hdr->spcl_pkt_hdr_mtch);
	}

	// --------------------------- //

	PROC_PRINTF("<L2>\n");
	PROC_PRINTF("  l2_pkt_type: %u ", hdr->l2_pkt_type);
	switch (hdr->l2_pkt_type) {
	case 0: PROC_PRINTF("[L2 Unicast]\n"); break;
	case 1: PROC_PRINTF("[L2 Multicast]\n"); break;
	case 2: PROC_PRINTF("[L2 Broadcast]\n"); break;
	default: PROC_PRINTF("[Unknown Unicast/Multicast]\n"); break;
	}
	PROC_PRINTF("  mac_da: %02X-%02X-%02X-%02X-%02X-%02X, mac_da_an_sel(4): 0x%X\n",
		hdr->mac_da_5, hdr->mac_da_4, hdr->mac_da_3, hdr->mac_da_2, hdr->mac_da_1, hdr->mac_da_0, hdr->mac_da_an_sel);
	PROC_PRINTF("  mac_sa: %02X-%02X-%02X-%02X-%02X-%02X, ethertype(16): 0x%04X\n",
		hdr->mac_sa_5, hdr->mac_sa_4, hdr->mac_sa_3, hdr->mac_sa_2, hdr->mac_sa_1, hdr->mac_sa_0,
		hdr->ethertype);

	// --------------------------- //

	PROC_PRINTF("<VLAN>\n");
	PROC_PRINTF("  vlan_cnt: %u\n", hdr->vlan_cnt);
	PROC_PRINTF("  top_tpid_enc: %u top_vid: 0x%03X top_1p: %u top_dei: %u\n", hdr->top_tpid_enc, hdr->top_vid, hdr->top_802_1p, hdr->top_dei);
	PROC_PRINTF("  inner_tpid_enc: %u inner_vid: 0x%03X inner_802_1p: %u inner_dei: %u\n", hdr->inner_tpid_enc, hdr->inner_vid, hdr->inner_802_1p, hdr->inner_dei);

	// --------------------------- //

	PROC_PRINTF("<PPP / PPPoE>\n");
	PROC_PRINTF("  type: %u [%s] code: 0x%X session_id: 0x%04X protocol_enc(4): 0x%X\n",
		hdr->pppoe_type,
		(hdr->pppoe_type == 0) ? "INVALID" : ((hdr->pppoe_type == 1) ? "PPPoE Discovery" : "PPPoE Session"),
		hdr->pppoe_code_enc, hdr->pppoe_session_id, hdr->ppp_protocol_enc);

	// --------------------------- //

	PROC_PRINTF("<L3>\n");
	PROC_PRINTF("  ip_vld: %u ip_ver: %u [%s] ip_mtu_enc: %u ip_mtu_en: %u\n",
		hdr->ip_vld, hdr->ip_ver,
		(hdr->ip_ver == 0) ? "IPv4" : "IPv6",
		hdr->ip_mtu_enc, hdr->ip_mtu_en);

	// --------------------------- //

	PROC_PRINTF("<IP>\n");
	PROC_PRINTF("  ip_protocol: 0x%02X ip_l4_type: %u", hdr->ip_protocol, hdr->ip_l4_type);
	switch (hdr->ip_l4_type) {
	case 1: PROC_PRINTF("[TCP]\n"); break;
	case 2: PROC_PRINTF("[UDP]\n"); break;
	case 3: PROC_PRINTF("[UDP-Lite]\n"); break;
	case 4: PROC_PRINTF("[RDPv1]\n"); break;
	case 5: PROC_PRINTF("[RDPv2]\n"); break;
	default: PROC_PRINTF("[L4 INVALID]\n"); break;
	}

	PROC_PRINTF("  ip_sa: %08X-%08X-%08X-%08X, ip_sa_enc: 0x%X\n",
		hdr->ip_sa_3, hdr->ip_sa_2, hdr->ip_sa_1, hdr->ip_sa_0, hdr->ip_sa_enc);
	PROC_PRINTF("  ip_da: %08X-%08X-%08X-%08X, ip_da_enc: 0x%X, da_mc: %u, da_sa_equal: %u\n",
		hdr->ip_da_3, hdr->ip_da_2, hdr->ip_da_1, hdr->ip_da_0, hdr->ip_da_enc, hdr->ip_da_mc, hdr->ip_da_sa_equal);
	PROC_PRINTF("  ip_dscp: 0x%02X markdown_en: %u marked_down: 0x%02X ip_ecn: %u ip_ecn_en: %u\n",
		hdr->ip_dscp, hdr->ip_dscp_markdown_en, hdr->ip_dscp_marked_down, hdr->ip_ecn, hdr->ip_ecn_en);
	PROC_PRINTF("  ipv6_flow_lbl: 0x%05X ip_ttl: 0x%02X ip_options: %u ip_fragment: %u\n",
		hdr->ipv6_flow_lbl, hdr->ip_ttl, hdr->ip_options, hdr->ip_fragment);
	PROC_PRINTF("  ipv6_ndp: %u ipv6_hbh: %u ipv6_rh: %u ipv6_doh: %u\n",
		hdr->ipv6_ndp, hdr->ipv6_hbh, hdr->ipv6_rh, hdr->ipv6_doh);

	PROC_PRINTF("  icmp_vld: %u ", hdr->icmp_vld);
	switch (hdr->icmp_vld) {
	case 3: PROC_PRINTF("[ICMPv6 NDP]"); break;
	case 4: PROC_PRINTF("[ICMPv4]"); break;
	case 5: PROC_PRINTF("[ICMPv6]"); break;
	case 6: PROC_PRINTF("[IGMP]"); break;
	case 7: PROC_PRINTF("[MLD]"); break;
	default: PROC_PRINTF("[INVALID]"); break;
	}
	PROC_PRINTF(" icmp_type: 0x%02X\n", hdr->icmp_type);

	PROC_PRINTF("  spi_vld: %u ", hdr->spi_vld);
	switch (hdr->spi_vld) {
	case 1: PROC_PRINTF("[IPSec AH]"); break;
	case 2: PROC_PRINTF("[IPSec ESP]"); break;
	case 3: PROC_PRINTF("[L2TP over IP, control]"); break;
	case 4: PROC_PRINTF("[L2TP over IP, data]"); break;
	case 5: PROC_PRINTF("[L2TP over UDP, control]"); break;
	case 6: PROC_PRINTF("[L2TP over UDP, data]"); break;
	default: PROC_PRINTF("[INVALID]"); break;
	}
	PROC_PRINTF(" spi/l2tp_session_id: 0x%08X\n", hdr->spi);

	PROC_PRINTF("  l3_chksum: 0x%04X l3_chksum_err: %u\n", hdr->l3_chksum, hdr->l3_chksum_err);

	// --------------------------- //

	PROC_PRINTF("<L4>\n");
	PROC_PRINTF("  l4_sp: 0x%04X l4_dp: 0x%04X port_rng_match_vec: 0x%08X tcp_rdp_ctrl: 0x%03X\n",
		hdr->l4_sp, hdr->l4_dp, hdr->l4_port_rng_match_vec, hdr->tcp_rdp_ctrl);
	PROC_PRINTF("  l4_chksum_zero: %u l4_chksum: 0x%08X\n", hdr->l4_chksum_zero, hdr->l4_chksum);
}
#endif

#if !(defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
static void dump_l2fe_hdr_a(aal_l2_fe_heada_t *hdr)
{
	rtlglue_printf("cpu_flg: %u deep_q: %u\n", hdr->cpu_flag, hdr->deep_q);
	rtlglue_printf("pol_grp_id: 0x%X, pol_id: 0x%03X, pol_en: %u ", hdr->policy_group_id, hdr->policy_id, hdr->policy_ena);
	switch (hdr->policy_ena) {
	case 0: rtlglue_printf("[Disabled]\n"); break;
	case 1: rtlglue_printf("[L2 Enabled]\n"); break;
	case 2: rtlglue_printf("[L3 Enabled]\n"); break;
	case 3: rtlglue_printf("[L2 & L3 Enabled]\n"); break;
	default: rtlglue_printf("[INVALID]\n"); break;
	}

	rtlglue_printf("mark: %u mirror: %u no_drop: %u drop_code: %u\n",
		hdr->marked, hdr->mirror, hdr->no_drop, hdr->drop_code);

	rtlglue_printf("rx_pkt_type: %u ", hdr->rx_packet_type);
	switch (hdr->rx_packet_type) {
	case 0: rtlglue_printf("[Unicast or Disable L2 Learning]\n"); break;
	case 1: rtlglue_printf("[Broadcast]\n"); break;
	case 2: rtlglue_printf("[Multicast]\n"); break;
	case 3: rtlglue_printf("[Unknown Unicast or Enable L2 Learning]\n"); break;
	}

	rtlglue_printf("hdr_type: %u ", hdr->header_type);
	switch (hdr->header_type) {
	case 0: rtlglue_printf("[Generic Unicast HDR]\n"); break;
	case 1: rtlglue_printf("[Multicast HDR, MCGID is valid]\n"); break;
	case 2: rtlglue_printf("[CPU bound packet, MCGID contains L2 learning info]\n"); break;
	case 3: rtlglue_printf("[PTP Packet]\n"); break;
	default: rtlglue_printf("[INVALID]\n"); break;
	}

	rtlglue_printf("mcgid: 0x%02X fe_bypass: %u pkt_size: 0x%04X\n",
		hdr->mc_group_id, hdr->fe_bypass, hdr->packet_size);
	rtlglue_printf("lspid: 0x%02X ldpid: 0x%02X cos: %u\n", hdr->logic_spid, hdr->logic_dpid, hdr->cos);
}

static void dump_l3fe_hdr_a(L3FE_HDR_A_t *hdr)
{
	rtlglue_printf("cpu_flg: %u deep_q: %u\n", hdr->cpu_flg, hdr->deep_q);
	rtlglue_printf("pol_grp_id: 0x%X, pol_id: 0x%03X, pol_en: %u ", hdr->pol_grp_id, hdr->pol_id, hdr->pol_en);
	switch (hdr->pol_en) {
	case 0: rtlglue_printf("[Disabled]\n"); break;
	case 1: rtlglue_printf("[L2 Enabled]\n"); break;
	case 2: rtlglue_printf("[L3 Enabled]\n"); break;
	case 3: rtlglue_printf("[L2 & L3 Enabled]\n"); break;
	default: rtlglue_printf("[INVALID]\n"); break;
	}

	rtlglue_printf("mark: %u mirror: %u no_drop: %u drop_code: %u\n",
		hdr->mark, hdr->mirror, hdr->no_drop, hdr->drop_code);

	rtlglue_printf("rx_pkt_type: %u ", hdr->rx_pkt_type);
	switch (hdr->rx_pkt_type) {
	case 0: rtlglue_printf("[Unicast or Disable L2 Learning]\n"); break;
	case 1: rtlglue_printf("[Broadcast]\n"); break;
	case 2: rtlglue_printf("[Multicast]\n"); break;
	case 3: rtlglue_printf("[Unknown Unicast or Enable L2 Learning]\n"); break;
	}

	rtlglue_printf("hdr_type: %u ", hdr->hdr_type);
	switch (hdr->hdr_type) {
	case 0: rtlglue_printf("[Generic Unicast HDR]\n"); break;
	case 1: rtlglue_printf("[Multicast HDR, MCGID is valid]\n"); break;
	case 2: rtlglue_printf("[CPU bound packet, MCGID contains L2 learning info]\n"); break;
	case 3: rtlglue_printf("[PTP Packet]\n"); break;
	default: rtlglue_printf("[INVALID]\n"); break;
	}

	rtlglue_printf("mcgid: 0x%02X fe_bypass: %u pkt_size: 0x%04X\n",
		hdr->mcgid, hdr->fe_bypass, hdr->pkt_size);
	rtlglue_printf("lspid: 0x%02X ldpid: 0x%02X cos: %u\n", hdr->lspid, hdr->ldpid, hdr->cos);
}
#endif 
int dump_headera(struct seq_file *s, void *v)
{
	ca_status_t ret = CA_E_OK;
#if !(defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
	aal_l2_fe_heada_t l2HdrA_before;
	aal_l2_fe_heada_t l2HdrA_after;
	L3FE_HDR_A_t l3HdrA_before;
	L3FE_HDR_A_t l3HdrA_after;
	L3FE_HDR_A_t l3HdrA_qmrx;
	int i, count;
	uint32_t *val = NULL;
	L3FE_GLB_DBG_IDX_t dbgVal;


	/* Get L2 HDR_A */
	ASSERT_EQ((ret = aal_l2_fe_pp_heada_get(G3_DEF_DEVID, &l2HdrA_before)), CA_E_OK);

	/* Get L2 HDR_A */
	ASSERT_EQ((ret = aal_l2_fe_pe_heada_get(G3_DEF_DEVID, &l2HdrA_after)), CA_E_OK);

	/* Get L3 HDR_A Before */
	//ASSERT_EQ((ret = aal_l3fe_pp_header_a_get(&l3HdrA)), CA_E_OK);
	val = (uint32_t *)&l3HdrA_before;
	*val = rtk_ne_reg_read(L3FE_PP_HEADER_A_LOW);
	val++;
	*val = rtk_ne_reg_read(L3FE_PP_HEADER_A_HI);

	/* Get L3 HDR_A After */
	//if ((ret = aal_l3fe_glb_dbg_get(14, &hdr_a, sizeof(hdr_a))) != AAL_E_OK)
	val = (uint32_t *)&l3HdrA_after;
	/* total read count is ceil(size/sizeof(int)) */
	count = (sizeof(L3FE_HDR_A_t) + sizeof(*val) - 1) / sizeof(*val);
	for (i = 0; i < count; i++) {
		/* idx[9:5] is debug vector, idx[4:0] chooses data bit range */
		dbgVal.bf.idx = (DBG_HDR_A_PE << 5) | i;
		rtk_ne_reg_write(dbgVal.wrd, L3FE_GLB_DBG_IDX);
		val[i] = rtk_ne_reg_read(L3FE_GLB_DBG_DAT);
	}

	val = (uint32_t *)&l3HdrA_qmrx;
	*val = rtk_ne_reg_read(QM_QM_RMU0_RX_PACKET_HEADER_INFO0);
	val++;
	*val = rtk_ne_reg_read(QM_QM_RMU0_RX_PACKET_HEADER_INFO1);

	/* Display L2 HDR_A Before */
	rtlglue_printf("---- "COLOR_Y "[L2 HDR_A - PktParser]" COLOR_NM "-------------------------\n");
	dump_l2fe_hdr_a(&l2HdrA_before);

	/* Display L2 HDR_A After */
	rtlglue_printf("---- "COLOR_Y "[L2 HDR_A - PktEditor]" COLOR_NM "--------------------------\n");
	dump_l2fe_hdr_a(&l2HdrA_after);
	
	rtlglue_printf("====================================================\n");

	/* Display L3 HDR_A Before */
	rtlglue_printf("---- "COLOR_Y "[L3 HDR_A - PktParser]" COLOR_NM "--------------------------\n");
	dump_l3fe_hdr_a(&l3HdrA_before);

	/* Display L3 HDR_A After */
	rtlglue_printf("---- "COLOR_Y "[L3 HDR_A - PktEditor]" COLOR_NM "---------------------------\n");
	dump_l3fe_hdr_a(&l3HdrA_after);

	rtlglue_printf("====================================================\n");
	
	/* Display L3QM HDR_A Rx  */
	rtlglue_printf("---- "COLOR_Y "[L3 HDR_A - QM Rx]" COLOR_NM "-----------------------------\n");
	dump_l3fe_hdr_a(&l3HdrA_qmrx);
#else
	dump_headera_show(s, v);
#endif	
	return ret;
}
#if defined(CONFIG_RTK_L34_G3_PLATFORM)


void tracefilterRULE0_dump_headerI(unsigned long data)
{
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	int count, i;
	unsigned long int msec2jiffies=0;
	uint32_t *val = NULL;
#if defined(CONFIG_FC_RTL9607F_SERIES)
	L3FE_HDR_I_DATA_t l3HdrI_before;
	L3FE_HDR_I_DATA_t l3HdrI_after;
#else
	L3FE_HDR_I_t l3HdrI_before;
	L3FE_HDR_I_t l3HdrI_after;
#endif
	L3FE_DBG_STG3_INFO_t stag3_info;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	cls_dbg_fib_hit_t cls_fib_dbg;
#if defined(CONFIG_FC_RTL9607F_SERIES)
	L3FE_HDR_I_DATA_t l3HdrI_CLS;
#else
	L3FE_HDR_I_t l3HdrI_CLS;
#endif
#else
	cls_fib_dbg_t cls_fib_dbg;
#endif
	char acl_dbg_info[64];


	if(tracefilterRULE0_headri_period_time==0)
	{
		RTK_FC_HELPER_DEL_TIMER(dumpheaderI_timer);
		initTimer=0;
		return ;
	}


	if(fc_db.traceFilterRuleMask&0x1)
	{

		if(fc_db.trace_filter_bitmask[0]==FC_DEBUG_TRACE_FILTER_SHOWNUMBEROFTIMES || fc_db.trace_filter_bitmask[0]==0)
			goto NEXTTIMER;

		

		memset(&l3HdrI_before,0,sizeof(l3HdrI_before));
		memset(&l3HdrI_after,0,sizeof(l3HdrI_after));
		
		{
			L3FE_GLB_L3FE_MONITOR_CTRL_t dbgVal;

			// latch mode
			val =  (uint32_t *)&l3HdrI_before;
			count = (sizeof(l3HdrI_before) + sizeof(*val) - 1) / sizeof(*val);
			//printk("~~~~~~count = %d\n",count);
			for (i = 0; i < count; i++) {
				/* CTRL[7:5] is monitor vector, CTRL[4:0] chooses data bit range */
				dbgVal.bf.bus_sel = (LATCH_MONITOR_HDR_I_PP_STG0 << 5) | i;
				rtk_ne_reg_write(dbgVal.wrd, L3FE_GLB_L3FE_MONITOR_CTRL);
				val[i] = rtk_ne_reg_read(L3FE_GLB_L3FE_MONITOR_RETURN);
			}

			val =  (uint32_t *)&l3HdrI_after;
			count = (sizeof(l3HdrI_after) + sizeof(*val) - 1) / sizeof(*val);
			//printk("~~~~~~count = %d\n",count);
			for (i = 0; i < count; i++) {
				/* CTRL[7:5] is monitor vector, CTRL[4:0] chooses data bit range */
				dbgVal.bf.bus_sel = (LATCH_MONITOR_HDR_I_BEFORE_PE << 5) | i;
				rtk_ne_reg_write(dbgVal.wrd, L3FE_GLB_L3FE_MONITOR_CTRL);
				val[i] = rtk_ne_reg_read(L3FE_GLB_L3FE_MONITOR_RETURN);
			}

			val = (uint32_t *)&stag3_info;
			count = (sizeof(L3FE_DBG_STG3_INFO_t) + sizeof(*val) - 1) / sizeof(*val);
			for (i = 0; i < count; i++) {
				/* CTRL[7:5] is monitor vector, CTRL[4:0] chooses data bit range */
				dbgVal.bf.bus_sel = (LATCH_MONITOR_DROP_CODE_PE << 5) | i;
				rtk_ne_reg_write(dbgVal.wrd, L3FE_GLB_L3FE_MONITOR_CTRL);
				val[i] = rtk_ne_reg_read(L3FE_GLB_L3FE_MONITOR_RETURN);
			}

			aal_l3fe_glb_dbg_latch_trigger_set(ENABLED); // triger next through Packet Parser packet lookup result latched.
		}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		{
			L3FE_GLB_CLS_STG_MONITOR_CTRL_t dbgVal_cls;
	
			val =  (uint32_t *)&l3HdrI_CLS;
			count = (sizeof(l3HdrI_CLS) + sizeof(*val) - 1) / sizeof(*val);
			for (i = 0; i < count; i++) {
				/* bus_sel[7:5] is monitor vector, bus_sel[4:0] chooses data bit range */
				dbgVal_cls.bf.enable = 1;
				dbgVal_cls.bf.bus_sel = (MONITOR_HDR_I_STG1_STG2 << 5) | i;
				rtk_ne_reg_write(dbgVal_cls.wrd, L3FE_GLB_CLS_STG_MONITOR_CTRL);
				val[i] = rtk_ne_reg_read(L3FE_GLB_CLS_STG_MONITOR_RETURN);
			}
	
			val =  (uint32_t *)&cls_fib_dbg;
			count = (sizeof(cls_dbg_fib_hit_t) + sizeof(*val) - 1) / sizeof(*val);
			for (i = 0; i < count; i++) {
				/* bus_sel[7:5] is monitor vector, bus_sel[4:0] chooses data bit range */
				dbgVal_cls.bf.enable = 1;
				dbgVal_cls.bf.bus_sel = (4 << 5) | i;	//ref: __l3fe_debug_proc_write
				rtk_ne_reg_write(dbgVal_cls.wrd, L3FE_GLB_CLS_STG_MONITOR_CTRL);
				val[i] = rtk_ne_reg_read(L3FE_GLB_CLS_STG_MONITOR_RETURN);
			}
		}
#else
		{
			L3FE_GLB_CLS_STG_MONITOR_CTRL_t dbgVal_clsfib;
	
			val =  (uint32_t *)&cls_fib_dbg;
			count = (sizeof(cls_fib_dbg_t) + sizeof(*val) - 1) / sizeof(*val);
			for (i = 0; i < count; i++) {
				/* bus_sel[7:5] is monitor vector, bus_sel[4:0] chooses data bit range */
				dbgVal_clsfib.bf.enable = 1;
				dbgVal_clsfib.bf.bus_sel = (MONITOR_CLS_RESULT << 5) | i;
				rtk_ne_reg_write(dbgVal_clsfib.wrd, L3FE_GLB_CLS_STG_MONITOR_CTRL);
				val[i] = rtk_ne_reg_read(L3FE_GLB_CLS_STG_MONITOR_RETURN);
			}
		}
#endif

		if(HDR_I_HW_FIELD(&l3HdrI_after, pspid) ==0 || HDR_I_HW_FIELD(&l3HdrI_before, pspid) ==0)
			goto NEXTTIMER;

		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_SPA)
		{
			//printk("fc_db.trace_filter[0].spa = %d (%d) (%d)", fc_db.trace_filter[0].spa, HDR_I_HW_FIELD(&l3HdrI_before, o_lspid), HDR_I_HW_FIELD(&l3HdrI_after, o_lspid);
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if(fc_db.trace_filter[0].spa!= HDR_I_HW_FIELD(&l3HdrI_after, o_lspid)  ) goto NEXTTIMER;
			}
			else
			{
				if(fc_db.trace_filter[0].spa!=HDR_I_HW_FIELD(&l3HdrI_before, o_lspid)) goto NEXTTIMER;
			}

		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_DA)
		{
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if(((fc_db.trace_filter[0].dmac.octet[0] != HDR_I_HW_FIELD(&l3HdrI_after, mac_da_5)) ||
				 (fc_db.trace_filter[0].dmac.octet[1] != HDR_I_HW_FIELD(&l3HdrI_after, mac_da_4)) ||
				 (fc_db.trace_filter[0].dmac.octet[2] != HDR_I_HW_FIELD(&l3HdrI_after, mac_da_3)) ||
				 (fc_db.trace_filter[0].dmac.octet[3] != HDR_I_HW_FIELD(&l3HdrI_after, mac_da_2)) ||
				 (fc_db.trace_filter[0].dmac.octet[4] != HDR_I_HW_FIELD(&l3HdrI_after, mac_da_1)) ||
				 (fc_db.trace_filter[0].dmac.octet[5] != HDR_I_HW_FIELD(&l3HdrI_after, mac_da_0))
				)) goto NEXTTIMER;
			}
			else
			{
				if(((fc_db.trace_filter[0].dmac.octet[0] != HDR_I_HW_FIELD(&l3HdrI_before, mac_da_5)) ||
				 (fc_db.trace_filter[0].dmac.octet[1] != HDR_I_HW_FIELD(&l3HdrI_before, mac_da_4)) ||
				 (fc_db.trace_filter[0].dmac.octet[2] != HDR_I_HW_FIELD(&l3HdrI_before, mac_da_3)) ||
				 (fc_db.trace_filter[0].dmac.octet[3] != HDR_I_HW_FIELD(&l3HdrI_before, mac_da_2)) ||
				 (fc_db.trace_filter[0].dmac.octet[4] != HDR_I_HW_FIELD(&l3HdrI_before, mac_da_1)) ||
				 (fc_db.trace_filter[0].dmac.octet[5] != HDR_I_HW_FIELD(&l3HdrI_before, mac_da_0))
				)) goto NEXTTIMER;
			}
			
		}

		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_SA)
		{
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if((fc_db.trace_filter[0].smac.octet[0] != HDR_I_HW_FIELD(&l3HdrI_after, mac_sa_5)) ||
				 (fc_db.trace_filter[0].smac.octet[1] != HDR_I_HW_FIELD(&l3HdrI_after, mac_sa_4)) ||
				 (fc_db.trace_filter[0].smac.octet[2] != HDR_I_HW_FIELD(&l3HdrI_after, mac_sa_3)) ||
				 (fc_db.trace_filter[0].smac.octet[3] != HDR_I_HW_FIELD(&l3HdrI_after, mac_sa_2)) ||
				 (fc_db.trace_filter[0].smac.octet[4] != HDR_I_HW_FIELD(&l3HdrI_after, mac_sa_1)) ||
				 (fc_db.trace_filter[0].smac.octet[5] != HDR_I_HW_FIELD(&l3HdrI_after, mac_sa_0))
				)goto NEXTTIMER;
			}
			else
			{
				if(((fc_db.trace_filter[0].smac.octet[0] != HDR_I_HW_FIELD(&l3HdrI_before, mac_sa_5)) ||
				 (fc_db.trace_filter[0].smac.octet[1] != HDR_I_HW_FIELD(&l3HdrI_before, mac_sa_4)) ||
				 (fc_db.trace_filter[0].smac.octet[2] != HDR_I_HW_FIELD(&l3HdrI_before, mac_sa_3)) ||
				 (fc_db.trace_filter[0].smac.octet[3] != HDR_I_HW_FIELD(&l3HdrI_before, mac_sa_2)) ||
				 (fc_db.trace_filter[0].smac.octet[4] != HDR_I_HW_FIELD(&l3HdrI_before, mac_sa_1)) ||
				 (fc_db.trace_filter[0].smac.octet[5] != HDR_I_HW_FIELD(&l3HdrI_before, mac_sa_0))
				)) goto NEXTTIMER;
			}
			
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_LDPID)
		{
			//printk("%d %d\n",HDR_I_HW_FIELD(&l3HdrI_after, mcgid),fc_db.trace_filter[0].ldpid);
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if((fc_db.trace_filter[0].ldpid != HDR_I_HW_FIELD(&l3HdrI_after, mcgid)))goto NEXTTIMER;
			}
			else
			{
				if((fc_db.trace_filter[0].ldpid != HDR_I_HW_FIELD(&l3HdrI_before, mcgid))) goto NEXTTIMER;
			}
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_SIP )
		{
			//printk("SIP: %x %x\n",fc_db.trace_filter[0].sip, (HDR_I_HW_FIELD(&l3HdrI_before, ip_sa_0)));
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if(fc_db.trace_filter[0].sip!= (HDR_I_HW_FIELD(&l3HdrI_after, ip_sa_0)))
					goto NEXTTIMER;
			}
			else
			{
				if(fc_db.trace_filter[0].sip!= (HDR_I_HW_FIELD(&l3HdrI_before, ip_sa_0)))
					goto NEXTTIMER;
			}
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_DIP )
		{
			//printk("DIP: %x %x\n",fc_db.trace_filter[0].dip, (HDR_I_HW_FIELD(&l3HdrI_before, ip_da_0)));
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if(fc_db.trace_filter[0].dip!= (HDR_I_HW_FIELD(&l3HdrI_after, ip_da_0)))
					goto NEXTTIMER;
			}
			else
			{
				if(fc_db.trace_filter[0].dip!= (HDR_I_HW_FIELD(&l3HdrI_before, ip_da_0)))
					goto NEXTTIMER;
			}
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_V6SIP)
		{

			ca_uint32_t	tmp_sa_1, tmp_sa_2, tmp_sa_3, tmp_sa_4;
			tmp_sa_1 = (fc_db.trace_filter[0].sipv6[0]<<24) |(fc_db.trace_filter[0].sipv6[1]<<16) | (fc_db.trace_filter[0].sipv6[2]<<8)| (fc_db.trace_filter[0].sipv6[3]);
			tmp_sa_2 = (fc_db.trace_filter[0].sipv6[4]<<24) |(fc_db.trace_filter[0].sipv6[5]<<16) | (fc_db.trace_filter[0].sipv6[6]<<8)| (fc_db.trace_filter[0].sipv6[7]);
			tmp_sa_3 = (fc_db.trace_filter[0].sipv6[8]<<24) |(fc_db.trace_filter[0].sipv6[9]<<16) | (fc_db.trace_filter[0].sipv6[10]<<8)| (fc_db.trace_filter[0].sipv6[11]);
			tmp_sa_4 = (fc_db.trace_filter[0].sipv6[12]<<24) |(fc_db.trace_filter[0].sipv6[13]<<16) | (fc_db.trace_filter[0].sipv6[14]<<8)| (fc_db.trace_filter[0].sipv6[15]);

			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if(( (HDR_I_HW_FIELD(&l3HdrI_after, ip_sa_3) != tmp_sa_1)|| (HDR_I_HW_FIELD(&l3HdrI_after, ip_sa_2) != tmp_sa_2) ||(HDR_I_HW_FIELD(&l3HdrI_after, ip_sa_1) != tmp_sa_3) ||(HDR_I_HW_FIELD(&l3HdrI_after, ip_sa_0) != tmp_sa_4) ))
					goto NEXTTIMER;
			}
			else
			{
				if(( (HDR_I_HW_FIELD(&l3HdrI_before, ip_sa_3) != tmp_sa_1)|| (HDR_I_HW_FIELD(&l3HdrI_before, ip_sa_2) != tmp_sa_2) ||(HDR_I_HW_FIELD(&l3HdrI_before, ip_sa_1) != tmp_sa_3) ||(HDR_I_HW_FIELD(&l3HdrI_before, ip_sa_0) != tmp_sa_4) ))
					goto NEXTTIMER;
			}
		
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_V6DIP)
		{
			ca_uint32_t	tmp_da_1, tmp_da_2, tmp_da_3, tmp_da_4;
			
			tmp_da_1 = (fc_db.trace_filter[0].dipv6[0]<<24) |(fc_db.trace_filter[0].dipv6[1]<<16) | (fc_db.trace_filter[0].dipv6[2]<<8)| (fc_db.trace_filter[0].dipv6[3]);
			tmp_da_2 = (fc_db.trace_filter[0].dipv6[4]<<24) |(fc_db.trace_filter[0].dipv6[5]<<16) | (fc_db.trace_filter[0].dipv6[6]<<8)| (fc_db.trace_filter[0].dipv6[7]);
			tmp_da_3 = (fc_db.trace_filter[0].dipv6[8]<<24) |(fc_db.trace_filter[0].dipv6[9]<<16) | (fc_db.trace_filter[0].dipv6[10]<<8)| (fc_db.trace_filter[0].dipv6[11]);
			tmp_da_4 = (fc_db.trace_filter[0].dipv6[12]<<24) |(fc_db.trace_filter[0].dipv6[13]<<16) | (fc_db.trace_filter[0].dipv6[14]<<8)| (fc_db.trace_filter[0].dipv6[15]);
			if(( (HDR_I_HW_FIELD(&l3HdrI_before, ip_da_3) != tmp_da_1)|| (HDR_I_HW_FIELD(&l3HdrI_before, ip_da_2) != tmp_da_2) ||(HDR_I_HW_FIELD(&l3HdrI_before, ip_da_1) != tmp_da_3) ||(HDR_I_HW_FIELD(&l3HdrI_before, ip_da_0) != tmp_da_4) ) && 
				( (HDR_I_HW_FIELD(&l3HdrI_after, ip_da_3) != tmp_da_1)|| (HDR_I_HW_FIELD(&l3HdrI_after, ip_da_2) != tmp_da_2) ||(HDR_I_HW_FIELD(&l3HdrI_after, ip_da_1) != tmp_da_3) ||(HDR_I_HW_FIELD(&l3HdrI_after, ip_da_0) != tmp_da_4) )
			)
				goto NEXTTIMER;

			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if( (HDR_I_HW_FIELD(&l3HdrI_after, ip_da_3) != tmp_da_1)|| (HDR_I_HW_FIELD(&l3HdrI_after, ip_da_2) != tmp_da_2) ||(HDR_I_HW_FIELD(&l3HdrI_after, ip_da_1) != tmp_da_3) ||(HDR_I_HW_FIELD(&l3HdrI_after, ip_da_0) != tmp_da_4) )
					goto NEXTTIMER;
			}
			else
			{
				if( (HDR_I_HW_FIELD(&l3HdrI_before, ip_da_3) != tmp_da_1)|| (HDR_I_HW_FIELD(&l3HdrI_before, ip_da_2) != tmp_da_2) ||(HDR_I_HW_FIELD(&l3HdrI_before, ip_da_1) != tmp_da_3) ||(HDR_I_HW_FIELD(&l3HdrI_before, ip_da_0) != tmp_da_4) )
					goto NEXTTIMER;
			}
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_CVLAN )
		{
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if(!(( HDR_I_HW_FIELD(&l3HdrI_after, vlan_cnt)==1 && fc_db.trace_filter[0].cvlanid ==HDR_I_HW_FIELD(&l3HdrI_after, top_vid)) ||
				   ( HDR_I_HW_FIELD(&l3HdrI_after, vlan_cnt)==2 && fc_db.trace_filter[0].svlanid ==HDR_I_HW_FIELD(&l3HdrI_after, top_vid) && fc_db.trace_filter[0].cvlanid ==HDR_I_HW_FIELD(&l3HdrI_after, inner_vid)) )
				  ) goto NEXTTIMER;
			}
			else
			{
				if(!(( HDR_I_HW_FIELD(&l3HdrI_before, vlan_cnt)==1 && fc_db.trace_filter[0].cvlanid ==HDR_I_HW_FIELD(&l3HdrI_before, top_vid)) ||
				   ( HDR_I_HW_FIELD(&l3HdrI_before, vlan_cnt)==2 && fc_db.trace_filter[0].svlanid ==HDR_I_HW_FIELD(&l3HdrI_before, top_vid) && fc_db.trace_filter[0].cvlanid ==HDR_I_HW_FIELD(&l3HdrI_before, inner_vid)))
				   ) goto NEXTTIMER;
			}
			
			
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_SVLAN )
		{
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if( fc_db.trace_filter[0].svlanid != HDR_I_HW_FIELD(&l3HdrI_after, top_vid) )
					goto NEXTTIMER;
			}
			else
			{
				if( fc_db.trace_filter[0].svlanid != HDR_I_HW_FIELD(&l3HdrI_before, top_vid) )
					goto NEXTTIMER;
			}
			
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_PPPOESESSIONID )
		{
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if( HDR_I_HW_FIELD(&l3HdrI_after, pppoe_type) )
				{
					if(	fc_db.trace_filter[0].sessionid!= HDR_I_HW_FIELD(&l3HdrI_after, pppoe_session_id))
						goto NEXTTIMER;
				}
				else
				{
					goto NEXTTIMER;
				}
			}
			else
			{
				
				if(HDR_I_HW_FIELD(&l3HdrI_before, pppoe_type) )
				{
					if( fc_db.trace_filter[0].sessionid!= HDR_I_HW_FIELD(&l3HdrI_before, pppoe_session_id))
						goto NEXTTIMER;
				}
				else
				{
					goto NEXTTIMER;
				}
			}
			
			
		}
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_ETH )
		{
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if( fc_db.trace_filter[0].ethertype!= HDR_I_HW_FIELD(&l3HdrI_after, ethertype) )
					goto NEXTTIMER;
			}
			else
			{
				if( fc_db.trace_filter[0].ethertype!= HDR_I_HW_FIELD(&l3HdrI_before, ethertype) )
					goto NEXTTIMER;
			}
		}

		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_L4PROTO )
		{
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if( fc_db.trace_filter[0].l4proto!= HDR_I_HW_FIELD(&l3HdrI_after, ip_protocol) )
					goto NEXTTIMER;
			}
			else
			{
				if( fc_db.trace_filter[0].l4proto!= HDR_I_HW_FIELD(&l3HdrI_before, ip_protocol) )
					goto NEXTTIMER;
			}
		}

		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_SPORT )
		{
		
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if( fc_db.trace_filter[0].sport!= HDR_I_HW_FIELD(&l3HdrI_after, l4_sp) )
					goto NEXTTIMER;
			}
			else
			{
				if( fc_db.trace_filter[0].sport!= HDR_I_HW_FIELD(&l3HdrI_before, l4_sp) )
					goto NEXTTIMER;
			}
		}

		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_DPORT )
		{
		
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if( fc_db.trace_filter[0].dport!= HDR_I_HW_FIELD(&l3HdrI_after, l4_dp) )
					goto NEXTTIMER;
			}
			else
			{
				if(fc_db.trace_filter[0].dport!= HDR_I_HW_FIELD(&l3HdrI_before, l4_dp) )
					goto NEXTTIMER;
			}
		}

		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_REASON )
		{
		
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if( fc_db.trace_filter[0].reason!= ((HDR_I_HW_FIELD(&l3HdrI_after, mdata_l) >> 8) & 0xf) )
					goto NEXTTIMER;
			}
			else
			{
				if(fc_db.trace_filter[0].reason!= ((HDR_I_HW_FIELD(&l3HdrI_before, mdata_l) >> 8) & 0xf) )
					goto NEXTTIMER;
			}
		}
			
			
		if(fc_db.trace_filter_bitmask[0]&FC_DEBUG_TRACE_FILTER_WLAN_INDEX )
		{
		
			if(fc_db.controlFuc.headeri_check_afterPE)
			{
				if(fc_db.trace_filter[0].wlan_index!= (HDR_I_HW_FIELD(&l3HdrI_after, mdata_l) & 0xff) )
					goto NEXTTIMER;
			}
			else
			{
				if(fc_db.trace_filter[0].wlan_index!= (HDR_I_HW_FIELD(&l3HdrI_before, mdata_l) & 0xff) )
					goto NEXTTIMER;
			}
		}
			
		
		
		rtlglue_printf(COLOR_H"headeri_latch_mode: ON"COLOR_NM"\n");
		rtlglue_printf("---- "COLOR_Y "[additional drop code]" COLOR_NM "------------------------------------\n");
		rtlglue_printf("stage3_pe_hdr_a_mark: %d\n", stag3_info.hdr_a_mark);
		rtlglue_printf("stage3_pe_hdr_a_drop_code: %d\n", stag3_info.hdr_a_drop_code);
		
		/* Display L3 HDR_I Before */
		rtlglue_printf("---- "COLOR_Y "[L3 PP - STG0]" COLOR_NM "------------------------------------\n");
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		dump_l3fe_hdr_i(NULL, &l3HdrI_before, RTK_FC_HEADERI_PHASE_L3PP_STG0);
#else
		dump_l3fe_hdr_i(NULL, &l3HdrI_before);
#endif
		rtlglue_printf("----------------------------------------------\n");

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		rtlglue_printf("---- "COLOR_Y "[L3 STG1 - STG2]" COLOR_NM "------------------------------------\n");
		dump_l3fe_hdr_i(NULL, &l3HdrI_CLS, RTK_FC_HEADERI_PHASE_STG1_STG2);
#endif

		/* Display L3 HDR_I After */
		rtlglue_printf("---- "COLOR_Y "[L3 HDR_I - TE_PE]" COLOR_NM "------------------------------------\n");
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		dump_l3fe_hdr_i(NULL, &l3HdrI_after, RTK_FC_HEADERI_PHASE_STG2_PE);
#else
		dump_l3fe_hdr_i(NULL, &l3HdrI_after);
#endif
		rtlglue_printf("----------------------------------------------\n");

		if(HDR_I_HW_FIELD(&l3HdrI_after, fwd_vld) & 0x1)
		{
			memset(acl_dbg_info, 0, sizeof(acl_dbg_info));
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			if(HAS_ACL_RSN(MDATAL_REASON(HDR_I_HW_FIELD(&l3HdrI_CLS, mdata_l)), MDATAL_CLS_RSN(HDR_I_HW_FIELD(&l3HdrI_CLS, mdata_l))))
				_rtk_fc_aclHeaderInfo_get(acl_dbg_info, sizeof(acl_dbg_info), MDATAL_CLS_RSN(HDR_I_HW_FIELD(&l3HdrI_CLS, mdata_l)));
			rtlglue_printf("---- "COLOR_Y "[L3 HIT CLS - HW info and SW idx]" COLOR_NM COLOR_G "%s" COLOR_NM "------------------------------------\n", acl_dbg_info);
			if(cls_fib_dbg.cls_hit_0)
				aal_used_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_0);
			if(cls_fib_dbg.cls_hit_1)
				aal_used_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_1);
			if(cls_fib_dbg.cls_hit_2)
				aal_used_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_2);
			if(cls_fib_dbg.cls_hit_3)
				aal_used_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_3);
#else
			if(HDR_I_HW_FIELD(&l3HdrI_after, mdata_h) & RXINFO_REF_ACL_RSN_BIT /*mdata_w_2*/)
				_rtk_fc_aclHeaderInfo_get(acl_dbg_info, sizeof(acl_dbg_info), (HDR_I_HW_FIELD(&l3HdrI_after, mdata_h)&RXINFO_REF_ACL_RSN_BIT));
			rtlglue_printf("---- "COLOR_Y "[L3 HIT CLS - HW info and SW idx]" COLOR_NM COLOR_G "%s" COLOR_NM "------------------------------------\n", acl_dbg_info);
			if(cls_fib_dbg.cls_hit_0)
				ca_used_aal_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_0);
			if(cls_fib_dbg.cls_hit_1)
				ca_used_aal_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_1);
			if(cls_fib_dbg.cls_hit_2)
				ca_used_aal_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_2);
			if(cls_fib_dbg.cls_hit_3)
				ca_used_aal_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_3);
#endif
			rtlglue_printf("---- [RSLT hit %d/%d/%d/%d, FIB idx %3d/%3d/%3d/%3d] -------------------------\n", 
				cls_fib_dbg.cls_hit_0, cls_fib_dbg.cls_hit_1, cls_fib_dbg.cls_hit_2, cls_fib_dbg.cls_hit_3, cls_fib_dbg.cls_hit_0?cls_fib_dbg.cls_hit_addr_0:-1, 
				cls_fib_dbg.cls_hit_1?cls_fib_dbg.cls_hit_addr_1:-1, cls_fib_dbg.cls_hit_2?cls_fib_dbg.cls_hit_addr_2:-1, cls_fib_dbg.cls_hit_3?cls_fib_dbg.cls_hit_addr_3:-1);
		}

		
		RTK_FC_HELPER_DEL_TIMER(dumpheaderI_timer);
		initTimer=0;
		fc_db.controlFuc.headeri_latch_mode = latchMode_lastState;
		aal_l3fe_glb_dbg_latch_trigger_set(latchMode_lastState);

		
		return ;

	}

NEXTTIMER:


	RTK_FC_HELPER_MSECS_TO_JIFFIES(tracefilterRULE0_headri_period_time, &msec2jiffies);
	RTK_FC_HELPER_MOD_TIMER(dumpheaderI_timer,jiffies+msec2jiffies);

	return ;
#endif
}
void _rtk_fc_headerI_dump_info(void)
{
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)

	rtlglue_printf("Header-I info dumping\n");
	rtlglue_printf("[Usage] 1. Turn on latch mode: \n");
	rtlglue_printf("		   - echo latch 1 > /proc/fc/hw_dump/headeri\n");
	rtlglue_printf("        2. Turn off latch mode: \n");
	rtlglue_printf("		   - echo latch 0 > /proc/fc/hw_dump/headeri\n");
	rtlglue_printf("        3. Turn on filtering dumping mode: \n");
	rtlglue_printf("		   - echo {time_interval} {Check_state} > /proc/fc/hw_dump/headeri\n");
	rtlglue_printf("		   - {time_internal} unit: 1 msec\n");
	rtlglue_printf("		   - {Check_state} 0: Before modify(stage PP), 1: after modify(Before PE)\n");
	rtlglue_printf("        4. Turn off filtering dumping mode: \n");
	rtlglue_printf("		   - echo 0 > /proc/fc/hw_dump/headeri   or \n");
	rtlglue_printf("		   - echo -1 > /proc/fc/hw_dump/headeri   \n");
#endif
}

int rtk_fc_headeri_latchMode_set(struct file *filp, const char *buff,unsigned long length, void *data)
{
	
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	//int val = _rtk_fc_proc_parsing_string_to_integer(buff,len);

	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	int32 time, latchMode;
	unsigned long int msec2jiffies=0;
	uint8 check_state = 0 ;
	if(buff)
	{
		char *strptr, *split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buff, CMD_BUFF_SIZE);

		strptr = tmpBuf;

		split_str = strsep(&strptr, " ");
		if(split_str == NULL){
			rtlglue_printf("Setting error\n");
			_rtk_fc_headerI_dump_info();
			return length;
		}
		if(strcasecmp(split_str, "latch")==0)
		{
			split_str = strsep(&strptr," ");
			if(split_str != NULL)
			{
				latchMode = simple_strtol(split_str, NULL, 0);
				if(latchMode==1)
				{
					rtlglue_printf("Latch Mode On\n");
					fc_db.controlFuc.headeri_latch_mode = ENABLED;
					aal_l3fe_glb_dbg_latch_trigger_set(ENABLED);
				}
				else
				{
					rtlglue_printf("Latch Mode Off\n");
					fc_db.controlFuc.headeri_latch_mode = DISABLED;
					aal_l3fe_glb_dbg_latch_trigger_set(DISABLED);
				}
			}
		}
		else
		{
	
			if(strcasecmp(split_str, "?")==0)
			{
				_rtk_fc_headerI_dump_info();
			}
			else
			{
				time = simple_strtol(split_str, NULL, 0);
				if(time <= 0)
				{
					tracefilterRULE0_headri_period_time=0;

					rtlglue_printf("disable headeri filter timer period\n");

				}
				else
				{
					extern int latchMode_lastState;
					
					split_str = strsep(&strptr," ");
					if(split_str !=NULL)
						check_state = simple_strtol(split_str, NULL, 0);
					else
						check_state = 0;
					
					rtlglue_printf(" headeri filter timer period=%d (ms)  check_state = %d\n",time, check_state);
					fc_db.controlFuc.headeri_check_afterPE = check_state;
					aal_l3fe_glb_dbg_latch_trigger_set(ENABLED); // triger next through Packet Parser packet lookup result latched.
					tracefilterRULE0_headri_period_time=time;
					
					latchMode_lastState = fc_db.controlFuc.headeri_latch_mode;
					if(initTimer==0){
						dumpheaderI_timer = RTK_FC_HELPER_MGR_TIMER_LIST_KMALLOC();
						RTK_FC_HELPER_SETUP_TIMER(dumpheaderI_timer,tracefilterRULE0_dump_headerI,0);
						RTK_FC_HELPER_MSECS_TO_JIFFIES(tracefilterRULE0_headri_period_time, &msec2jiffies);
						RTK_FC_HELPER_MOD_TIMER(dumpheaderI_timer,jiffies+msec2jiffies);
						initTimer=1;
						rtlglue_printf("TIMER INIT SUCCESS\n");
					}
				}

			}
		}
	}
#else
	// do nothing
#endif
	return length;
}

#endif

#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int dump_headeri(struct seq_file *s, void *v)
{
	ca_status_t ret = CA_E_OK;
#if defined(CONFIG_FC_RTL9607F_SERIES)
	L3FE_HDR_I_DATA_t l3HdrI_before;
	L3FE_HDR_I_DATA_t l3HdrI_after;
#else
	L3FE_HDR_I_t l3HdrI_before;
	L3FE_HDR_I_t l3HdrI_after;
#endif
	L3FE_DBG_STG3_INFO_t stag3_info;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	cls_dbg_fib_hit_t cls_fib_dbg;
#if defined(CONFIG_FC_RTL9607F_SERIES)
	L3FE_HDR_I_DATA_t l3HdrI_CLS;
#else
	L3FE_HDR_I_t l3HdrI_CLS;
#endif
#else
	cls_fib_dbg_t cls_fib_dbg;
#endif
	int i, count;
	uint32_t *val = NULL;
	char acl_dbg_info[64];

	s = NULL;	//temporary disable this finction due to some log will missing if system is busy

	//aal_l3fe_glb_cls_stg_monitor_get(l3fe_glb_monitor_vector_e where, void * data, size_t size)
	if(fc_db.controlFuc.headeri_latch_mode)
	{
		L3FE_GLB_L3FE_MONITOR_CTRL_t dbgVal;

		// latch mode
		val =  (uint32_t *)&l3HdrI_before;
		count = (sizeof(l3HdrI_before) + sizeof(*val) - 1) / sizeof(*val);
		for (i = 0; i < count; i++) {
			/* CTRL[7:5] is monitor vector, CTRL[4:0] chooses data bit range */
			dbgVal.bf.bus_sel = (LATCH_MONITOR_HDR_I_PP_STG0 << 5) | i;
			rtk_ne_reg_write(dbgVal.wrd, L3FE_GLB_L3FE_MONITOR_CTRL);
			val[i] = rtk_ne_reg_read(L3FE_GLB_L3FE_MONITOR_RETURN);
		}

		val =  (uint32_t *)&l3HdrI_after;
		count = (sizeof(l3HdrI_after) + sizeof(*val) - 1) / sizeof(*val);
		for (i = 0; i < count; i++) {
			/* CTRL[7:5] is monitor vector, CTRL[4:0] chooses data bit range */
			dbgVal.bf.bus_sel = (LATCH_MONITOR_HDR_I_BEFORE_PE << 5) | i;
			rtk_ne_reg_write(dbgVal.wrd, L3FE_GLB_L3FE_MONITOR_CTRL);
			val[i] = rtk_ne_reg_read(L3FE_GLB_L3FE_MONITOR_RETURN);
		}

		val = (uint32_t *)&stag3_info;
		count = (sizeof(L3FE_DBG_STG3_INFO_t) + sizeof(*val) - 1) / sizeof(*val);
		for (i = 0; i < count; i++) {
			/* CTRL[7:5] is monitor vector, CTRL[4:0] chooses data bit range */
			dbgVal.bf.bus_sel = (LATCH_MONITOR_DROP_CODE_PE << 5) | i;
			rtk_ne_reg_write(dbgVal.wrd, L3FE_GLB_L3FE_MONITOR_CTRL);
			val[i] = rtk_ne_reg_read(L3FE_GLB_L3FE_MONITOR_RETURN);
		}

		aal_l3fe_glb_dbg_latch_trigger_set(ENABLED); // triger next through Packet Parser packet lookup result latched.
	}
	else
	{
		L3FE_GLB_CLS_STG_MONITOR_CTRL_t dbgVal_before;
		L3FE_GLB_DBG_IDX_t dbgVal_after;

		val =  (uint32_t *)&l3HdrI_before;
		count = (sizeof(l3HdrI_before) + sizeof(*val) - 1) / sizeof(*val);
		for (i = 0; i < count; i++) {
			/* bus_sel[7:5] is monitor vector, bus_sel[4:0] chooses data bit range */
			dbgVal_before.bf.enable = 1;
			dbgVal_before.bf.bus_sel = (MONITOR_HDR_I_PP_STG0 << 5) | i;
			rtk_ne_reg_write(dbgVal_before.wrd, L3FE_GLB_CLS_STG_MONITOR_CTRL);
			val[i] = rtk_ne_reg_read(L3FE_GLB_CLS_STG_MONITOR_RETURN);
		}


		val =  (uint32_t *)&l3HdrI_after;
		count = (sizeof(l3HdrI_after) + sizeof(*val) - 1) / sizeof(*val);
		for (i = 0; i < count; i++) {
			/* idx[9:5] is debug vector, idx[4:0] chooses data bit range */
			dbgVal_after.bf.idx = (DBG_HDR_I_TE_PE << 5) | i;
			rtk_ne_reg_write(dbgVal_after.wrd, L3FE_GLB_DBG_IDX);
			val[i] = rtk_ne_reg_read(L3FE_GLB_DBG_DAT);
		}
	}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	{
		L3FE_GLB_CLS_STG_MONITOR_CTRL_t dbgVal_cls;

		val =  (uint32_t *)&l3HdrI_CLS;
		count = (sizeof(l3HdrI_CLS) + sizeof(*val) - 1) / sizeof(*val);
		for (i = 0; i < count; i++) {
			/* bus_sel[7:5] is monitor vector, bus_sel[4:0] chooses data bit range */
			dbgVal_cls.bf.enable = 1;
			dbgVal_cls.bf.bus_sel = (MONITOR_HDR_I_STG1_STG2 << 5) | i;
			rtk_ne_reg_write(dbgVal_cls.wrd, L3FE_GLB_CLS_STG_MONITOR_CTRL);
			val[i] = rtk_ne_reg_read(L3FE_GLB_CLS_STG_MONITOR_RETURN);
		}

		val =  (uint32_t *)&cls_fib_dbg;
		count = (sizeof(cls_dbg_fib_hit_t) + sizeof(*val) - 1) / sizeof(*val);
		for (i = 0; i < count; i++) {
			/* bus_sel[7:5] is monitor vector, bus_sel[4:0] chooses data bit range */
			dbgVal_cls.bf.enable = 1;
			dbgVal_cls.bf.bus_sel = (4 << 5) | i;	//ref: __l3fe_debug_proc_write
			rtk_ne_reg_write(dbgVal_cls.wrd, L3FE_GLB_CLS_STG_MONITOR_CTRL);
			val[i] = rtk_ne_reg_read(L3FE_GLB_CLS_STG_MONITOR_RETURN);
		}
	}
#else
	{
		L3FE_GLB_CLS_STG_MONITOR_CTRL_t dbgVal_clsfib;

		val =  (uint32_t *)&cls_fib_dbg;
		count = (sizeof(cls_fib_dbg_t) + sizeof(*val) - 1) / sizeof(*val);
		for (i = 0; i < count; i++) {
			/* bus_sel[7:5] is monitor vector, bus_sel[4:0] chooses data bit range */
			dbgVal_clsfib.bf.enable = 1;
			dbgVal_clsfib.bf.bus_sel = (MONITOR_CLS_RESULT << 5) | i;
			rtk_ne_reg_write(dbgVal_clsfib.wrd, L3FE_GLB_CLS_STG_MONITOR_CTRL);
			val[i] = rtk_ne_reg_read(L3FE_GLB_CLS_STG_MONITOR_RETURN);
		}
	}
#endif

	if(fc_db.controlFuc.headeri_latch_mode)
	{
		PROC_PRINTF(COLOR_H"headeri_latch_mode: ON"COLOR_NM"\n");
		PROC_PRINTF("---- "COLOR_Y "[additional drop code]" COLOR_NM "------------------------------------\n");
		PROC_PRINTF("stage3_pe_hdr_a_mark: %d\n", stag3_info.hdr_a_mark);
		PROC_PRINTF("stage3_pe_hdr_a_drop_code: %d\n", stag3_info.hdr_a_drop_code);
	}
	else
		PROC_PRINTF(COLOR_H"headeri_latch_mode: OFF"COLOR_NM"\n");

	/* Display L3 HDR_I Before */
	PROC_PRINTF("---- "COLOR_Y "[L3 PP - STG0]" COLOR_NM "------------------------------------\n");
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	dump_l3fe_hdr_i(s, &l3HdrI_before, RTK_FC_HEADERI_PHASE_L3PP_STG0);
#else
	dump_l3fe_hdr_i(s, &l3HdrI_before);
#endif
	PROC_PRINTF("----------------------------------------------\n");

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("---- "COLOR_Y "[L3 STG1 - STG2]" COLOR_NM "------------------------------------\n");
	dump_l3fe_hdr_i(s, &l3HdrI_CLS, RTK_FC_HEADERI_PHASE_STG1_STG2);
#endif

	/* Display L3 HDR_I After */
	PROC_PRINTF("---- "COLOR_Y "[L3 HDR_I - TE_PE]" COLOR_NM "------------------------------------\n");
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	dump_l3fe_hdr_i(s, &l3HdrI_after, RTK_FC_HEADERI_PHASE_STG2_PE);
#else
	dump_l3fe_hdr_i(s, &l3HdrI_after);
#endif
	PROC_PRINTF("----------------------------------------------\n");


	if(HDR_I_HW_FIELD(&l3HdrI_after, fwd_vld) & 0x1)
	{
		memset(acl_dbg_info, 0, sizeof(acl_dbg_info));
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		if(HAS_ACL_RSN(MDATAL_REASON(HDR_I_HW_FIELD(&l3HdrI_CLS, mdata_l)), MDATAL_CLS_RSN(HDR_I_HW_FIELD(&l3HdrI_CLS, mdata_l))))
			_rtk_fc_aclHeaderInfo_get(acl_dbg_info, sizeof(acl_dbg_info), MDATAL_CLS_RSN(HDR_I_HW_FIELD(&l3HdrI_CLS, mdata_l)));
		rtlglue_printf("---- "COLOR_Y "[L3 HIT CLS - HW info and SW idx]" COLOR_NM COLOR_G "%s" COLOR_NM "------------------------------------\n", acl_dbg_info);
		if(cls_fib_dbg.cls_hit_0)
			aal_used_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_0);
		if(cls_fib_dbg.cls_hit_1)
			aal_used_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_1);
		if(cls_fib_dbg.cls_hit_2)
			aal_used_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_2);
		if(cls_fib_dbg.cls_hit_3)
			aal_used_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_3);
#else
		if(HDR_I_HW_FIELD(&l3HdrI_after, mdata_h) & RXINFO_REF_ACL_RSN_BIT /*mdata_w_2*/)
			_rtk_fc_aclHeaderInfo_get(acl_dbg_info, sizeof(acl_dbg_info), (HDR_I_HW_FIELD(&l3HdrI_after, mdata_h)&RXINFO_REF_ACL_RSN_BIT));
		rtlglue_printf("---- "COLOR_Y "[L3 HIT CLS - HW info and SW idx]" COLOR_NM COLOR_G "%s" COLOR_NM "------------------------------------\n", acl_dbg_info);
		if(cls_fib_dbg.cls_hit_0)
			ca_used_aal_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_0);
		if(cls_fib_dbg.cls_hit_1)
			ca_used_aal_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_1);
		if(cls_fib_dbg.cls_hit_2)
			ca_used_aal_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_2);
		if(cls_fib_dbg.cls_hit_3)
			ca_used_aal_entry_print_by_fib_idx(G3_DEF_DEVID, cls_fib_dbg.cls_hit_addr_3);
#endif
		rtlglue_printf("---- [RSLT hit %d/%d/%d/%d, FIB idx %3d/%3d/%3d/%3d] -------------------------\n", 
			cls_fib_dbg.cls_hit_0, cls_fib_dbg.cls_hit_1, cls_fib_dbg.cls_hit_2, cls_fib_dbg.cls_hit_3, cls_fib_dbg.cls_hit_0?cls_fib_dbg.cls_hit_addr_0:-1, 
			cls_fib_dbg.cls_hit_1?cls_fib_dbg.cls_hit_addr_1:-1, cls_fib_dbg.cls_hit_2?cls_fib_dbg.cls_hit_addr_2:-1, cls_fib_dbg.cls_hit_3?cls_fib_dbg.cls_hit_addr_3:-1);
	}

	return ret;
}







#else
int dump_headeri(struct seq_file *s, void *v)
{
	ca_status_t ret = CA_E_OK;
	L3FE_HDR_I_t l3HdrI_before;
	L3FE_HDR_I_t l3HdrI_after;
	int i, count;
	uint32_t *val = NULL;
	L3FE_GLB_DBG_IDX_t dbgVal;
	char acl_dbg_info[64];

	s = NULL;	//temporary disable this finction due to some log will missing if system is busy

	//aal_l3fe_glb_cls_stg_monitor_get(l3fe_glb_monitor_vector_e where, void * data, size_t size)
	val =  (uint32_t *)&l3HdrI_before;
	count = (sizeof(L3FE_HDR_I_t) + sizeof(*val) - 1) / sizeof(*val);
	for (i = 0; i < count; i++) {
		/* idx[9:5] is debug vector, idx[4:0] chooses data bit range */
		dbgVal.bf.idx = (DBG_HDR_I_T1_T2 << 5) | i;
		rtk_ne_reg_write(dbgVal.wrd, L3FE_GLB_DBG_IDX);
		val[i] = rtk_ne_reg_read(L3FE_GLB_DBG_DAT);
	}


	val =  (uint32_t *)&l3HdrI_after;
	count = (sizeof(L3FE_HDR_I_t) + sizeof(*val) - 1) / sizeof(*val);
	for (i = 0; i < count; i++) {
		/* idx[9:5] is debug vector, idx[4:0] chooses data bit range */
		dbgVal.bf.idx = (DBG_HDR_I_TE_PE << 5) | i;
		rtk_ne_reg_write(dbgVal.wrd, L3FE_GLB_DBG_IDX);
		val[i] = rtk_ne_reg_read(L3FE_GLB_DBG_DAT);
	}


	/* Display L3 HDR_I Before */
	PROC_PRINTF("---- "COLOR_Y "[L3 HDR_I - CLS_HASH]" COLOR_NM "------------------------------------\n");
	dump_l3fe_hdr_i(s, &l3HdrI_before);
	PROC_PRINTF("----------------------------------------------\n");


	/* Display L3 HDR_I After */
	PROC_PRINTF("---- "COLOR_Y "[L3 HDR_I - TE_PE]" COLOR_NM "------------------------------------\n");
	dump_l3fe_hdr_i(s, &l3HdrI_after);
	PROC_PRINTF("----------------------------------------------\n");

	if(HDR_I_HW_FIELD(&l3HdrI_after, fwd_vld) & 0x1) {
		memset(acl_dbg_info, 0, sizeof(acl_dbg_info));
		if(HDR_I_HW_FIELD(&l3HdrI_after, mdata_h) & RXINFO_REF_ACL_RSN_BIT /*mdata_w_2*/)
			_rtk_fc_aclHeaderInfo_get(acl_dbg_info, sizeof(acl_dbg_info), (HDR_I_HW_FIELD(&l3HdrI_after, mdata_h)&RXINFO_REF_ACL_RSN_BIT));
		rtlglue_printf("---- "COLOR_Y "[L3 HIT CLS - HW info and SW idx]" COLOR_NM COLOR_G "%s" COLOR_NM "------------------------------------\n", acl_dbg_info);
		ca_used_aal_entry_print_by_fib_idx(G3_DEF_DEVID, HDR_I_HW_FIELD(&l3HdrI_after, cls_action) & 0x3ff);
		rtlglue_printf("----------------------------------------------\n");
	}

	return ret;
}
#endif

char CounterDesc[L3PE_CNTR_DROP_SRC_ENTRY_MAX][80] = {
	"PP: L2 pkt size illigal (L2_pkt_size < L2_hdr_size + L3_total_len)", // 0
	"PP: L3L4 header offset exceeds 254",
	"PP: IPv4 header checksum validation error",
	"PP: UDP checksum zero",
	"PP: Other detected errors",
	"PP: More than 2 VLAN ID", // 5
	"LPB: MRU check fail",
	"LPB: MyMAC filtering",
	"LPB: default drop",
	"Special packet drop",
	"reserved", // 10
	"DSLite/6RD: Decapsulate ECN check fail",
	"DSLite/6RD: Outer IPDA and IPSA reverse path of forwarding check fail",
	"6RD: outer and inner IPSA consistency check fail",
	"6RD: Decapsulate 6RD IPDA delegated prefix check fail",
	"6RD: Decapsulate 6RD IPMC drop", // 15
	"6RD: outer IPMC with inner IPUC packet",
	"DSLite: IPMC addr prefix check fail",
	"DSLite: IPMC embedded IPv4 addr check fail",
	"DSLite: outer IPMC with inner IPUC packet",
	"DSLite/6RD: IF ID check fail", // 20
	"DSLite: Decapsulate DSLite IPMC drop",
	"Hash Engine double check fail",
	"LPM search miss",
	"HashLite Engine double check fail",
	"T5: ILPB filter drop", // 25
	"T5: MC drop",
	"T5: SA deny",
	"T5: DA deny",
	"T5: software learning deny",
	"T5: unknown VLAN", // 30
	"T5: VLAN membership check fail",
	"T5: STP mode block drop",
	"T5: STP mode learn only and SMAC hit drop",
	"T5: VE deny",
	"T5: VE MC VLAN drop", // 35
	"T5: VE FIB drop",
	"T5: L2E FIB drop",
	"TE: reserved1",
	"TE: reserved2",
	"TE: unmarked WRED drop", // 40
	"TE: marked WRED drop",
	"TE: tail drop",
	"TE: policer drop",
	"TE: reserved7",
	"PE: TCP/UDP/RDPv1/RDPv2 checksum error", // 45
	"PE: L2 CRC error",
	"PE: MTU check fail",
	"STG1: L3 IF table",
	"STG1: FIB LDPID is blackhole",
	"STG1: TTL is 0", // 50
	"STG1: CR other drop",
	"STG2: L3 IF table",
	"STG2: FIB LDPID is blackhole",
	"STG2: TTL is 0",
	"STG2: CR other drop", // 55
	"STG3.U1: L3 IF table",
	"STG3.U1: FIB LDPID is blackhole",
	"STG3.U1: TTL is 0",
	"STG3.U1: CR other drop",
	"STG3.U2: L3 IF table", // 60
	"STG3.U2: FIB LDPID is blackhole",
	"STG3.U2: TTL is 0",
	"STG3.U2: CR other drop",
};
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int dump_dropcount(struct seq_file *s, void *v)
{
	dump_dropcount_show(s, v);

	PROC_PRINTF("===== "COLOR_Y"Note."COLOR_NM" =====\n");
	PROC_PRINTF("flow pol_1 idx for HASH default action DROP: \033[1;37;41m%d\033[0m\n\n", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_DROP);
	return 0;
}
int dump_dropcount_opcode(struct file *file, const char *buffer, unsigned long count, void *data)
{
	loff_t my_data=1;	 // to indicate buffer is already kernel space.
	dump_dropcount_write(file, buffer, count, &my_data);
	return count;
}
#if RTK_FC_TABLESIZE_OVERFLOW_FLOW
int dump_sw_flowOverFlow_list_list(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	rtk_fc_overFlowHash_linkList_t *pOverFlowEntry;

	PROC_PRINTF(">>flow overflow list:\n");

	if(!list_empty(&fc_db.overFlowHash_inUseListHead))
	{
		PROC_PRINTF(" overFlowHash_inUseListHead : \n");
		list_for_each_entry(pOverFlowEntry, &fc_db.overFlowHash_inUseListHead, flow_list)
		{
			if(pOverFlowEntry->flow_list.next!=&fc_db.overFlowHash_inUseListHead)
				PROC_PRINTF("	 Flow idx[%d] ->\n", pOverFlowEntry->idx);
			else
				PROC_PRINTF("	 Flow idx[%d]\n", pOverFlowEntry->idx);
		}
		PROC_PRINTF("\n");
	}

	PROC_PRINTF(">>flow overflow free list:\n");
	if(!list_empty(&fc_db.overFlowHash_freeListHead))
	{
		list_for_each_entry(pOverFlowEntry, &fc_db.overFlowHash_freeListHead, flow_list)
		{
			if(pOverFlowEntry->flow_list.next!=&fc_db.overFlowHash_freeListHead)
				PROC_PRINTF("	 Flow idx[%d] ->\n", pOverFlowEntry->idx);
			else
				PROC_PRINTF("	 Flow idx[%d]\n", pOverFlowEntry->idx);
		}
	}

	PROC_PRINTF("system overflow count: %d\n", RTK_FC_TABLESIZE_OVERFLOW_FLOW);

	PROC_PRINTF("\n");

	return retval;
}
#endif

#else
int dump_dropcount(struct seq_file *s, void *v)
{
	ca_status_t ret = CA_E_OK;
	ca_uint16_t num16 = 0;
	uint32 regvalue;
	uint8 showepon = FALSE;
	int i =0, j =0;
	uint64 cntPkt;
	aal_l2_fe_drop_source_t l2fedrop;
	aal_l2_te_pm_egress_t pm_stats;
	aal_l2_te_pm_stats_t pm_stat_stats;
	aal_l2_te_pm_policer_t	pm_pol;
	aal_l3_te_pm_egress_t l3pm_stats;
	aal_l3_te_pm_stats_t l3pm_stat_stats;
	aal_l3_te_pm_policer_t l3pm_pol;
	
	QM_QM_INT_SRC_t			int_src;
	QM_QM_RMU0_RX_PKT_CNTR_t	rx_pkt_cntr;
 	QM_QM_TX_PKT_CNTR_t 		tx_pkt_schedule_cntr;
	QM_QM_TX_PKT_CNTR_ALL_NI_t tx_pkt_ni_cntr;
	QM_QM_TX_PKT_CNTR_CPU0_t tx_pkt_cpu_cntr;
	QM_QM_RMU0_NO_BUF_DROP_PKT_INFO_t	no_buf_drop_info;
	QM_QM_RMU0_NO_BUF_DROP_PKT_CNTR_t	no_buf_drop_cntr;
#if !defined(CONFIG_FC_G3_G3LITE_SERIES)
	QM_QM_RMU0_FE_DROP_PKT_CNTR_t	drop_cntr;
	QM_QM_RX_EOP_DROP_CNTR_t eop_drop_cntr;
	QM_QM_RX_LEN_CHK_ERROR_CNTR_t rx_len_err_cntr;
	QM_QM_RX_L2TE_TAIL_DROP_CNTR_t qm_l2te_tail_drop_cntr;
#endif
	//printk("re-entrant check @%s(%d)\n", __FUNCTION__, __LINE__);

#if (defined(CONFIG_FC_CAG3_SERIES) || defined(CONFIG_FC_CA8277B_SERIES)) && !defined(CONFIG_RTL8198X_SERIES)
	//2 PON mib 
	{
		extern ca_status_t aal_gpon_gems_mib_get(ca_device_id_t device_id);
		extern ca_status_t aal_xgpon_xgem_mibs_get(ca_device_id_t device_id);
		rt_ponmisc_ponMode_t ponmode;
		rt_ponmisc_ponSpeed_t ponspeed;

		rt_ponmisc_modeSpeed_get(&ponmode, &ponspeed);
				

		if(ponmode==RT_GPON_MODE) {	
			rtlglue_printf("===== "COLOR_Y"PON counter"COLOR_NM" ===== (read-and-clear)\n");
			rtlglue_printf("Notice: please run \"omcicli set pm stop\" if needed\n\n");
			
			if(ponspeed == RT_1G25G_SPEED) {
   				rtlglue_printf("PON_MODE = %s\n", "2.5G GPON");

				aal_gpon_gems_mib_get(G3_DEF_DEVID);
			}
			else {
				if(ponspeed == RT_DN10G_SPEED)
   					rtlglue_printf("PON_MODE = %s\n", "10G XGPON");
				else if (ponspeed == RT_BOTH10G_SPEED)
   					rtlglue_printf("PON_MODE = %s\n", "10G XGSPON");

				aal_xgpon_xgem_mibs_get(G3_DEF_DEVID);
			}
		}else if(ponmode==RT_EPON_MODE){
			showepon = TRUE;
		}else {
   			PROC_PRINTF("PON_MODE = %s\n", "UNKNOWN");
		}
	}
	PROC_PRINTF("\n");
#endif 

#if 1
	PROC_PRINTF("usage:\t cat THISPROC before and after your test to show if any hw drop happens.\n");
	PROC_PRINTF("\t echo 0 > THISPROC to clear voq max buffer count.\n");
	PROC_PRINTF("\t echo [OpCode] > THISPROC to show page usage.\n");
	PROC_PRINTF("\t\t OpCode_bit 0 (0x00000001): Port Free Buffer Count and High/Low Threshold\n");
	PROC_PRINTF("\t\t OpCode_bit 1 (0x00000002): Voq NON-ZERO Used Pages and High/Low Threshold\n");
	PROC_PRINTF("\t\t OpCode_bit 2 (0x00000004): Voq ALL      Used Pages and High/Low Threshold\n");
	PROC_PRINTF("\t\t OpCode_bit 3 (0x00000008): L2TM and L3QM page size and numbers\n");
	PROC_PRINTF("\t\t OpCode_bit 4 (0x00000010): QM active and inactive counter\n");
	PROC_PRINTF("\t\t OpCode_bit 5 (0x00000020): NE module ready signals\n\n");
#endif

#if !defined(CONFIG_FC_G3_G3LITE_SERIES)
	//2 NI drop cnt
	PROC_PRINTF("===== "COLOR_Y"NI counter"COLOR_NM" ===== (read-and-clear)\n");
	{
		PROC_PRINT_REG_WITH_NAME(NI_HV_INTPT_TX_PKT_CNT, 		"NI_HV_INTPT_TX_PKT_CNT_L3FE");
		PROC_PRINT_REG_WITH_NAME(NI_HV_INTPT_RX_PKT_CNT, 		"NI_HV_INTPT_RX_PKT_CNT_L3FE");
		
		PROC_PRINT_REG_WITH_NAME(NI_HV_INTPT_TX_PKT_CNT+0x40, 	"NI_HV_INTPT_TX_PKT_CNT_L3QM");
		PROC_PRINT_REG_WITH_NAME(NI_HV_INTPT_RX_PKT_CNT+0x40, 	"NI_HV_INTPT_RX_PKT_CNT_L3QM");
		
		PROC_PRINT_REG_WITH_NAME(NI_HV_INTPT_TX_PKT_CNT+0x80, 	"NI_HV_INTPT_TX_PKT_CNT_MCE");
		PROC_PRINT_REG_WITH_NAME(NI_HV_INTPT_RX_PKT_CNT+0x80, 	"NI_HV_INTPT_RX_PKT_CNT_MCE");
		
		PROC_PRINT_REG_WITH_NAME(NI_HV_INTPT_RX_PKT_CNT+0xc0, 	"NI_HV_INTPT_RX_PKT_CNT_DMA");

		cntPkt = rtk_ne_reg_read(NI_HV_INTPT_TX_PKT_BYTE_HI_CNT);
		cntPkt = ((cntPkt << 32) | rtk_ne_reg_read(NI_HV_INTPT_TX_PKT_BYTE_LO_CNT));
		if(cntPkt!=0) PROC_PRINTF("%-40s: %llu\n", "NI_HV_INTPT_TX_PKT_BYTE_L3FE", cntPkt);
		cntPkt = rtk_ne_reg_read(NI_HV_INTPT_RX_PKT_BYTE_HI_CNT);
		cntPkt = ((cntPkt << 32) | rtk_ne_reg_read(NI_HV_INTPT_RX_PKT_BYTE_LO_CNT));
		if(cntPkt!=0) PROC_PRINTF("%-40s: %llu\n", "NI_HV_INTPT_RX_PKT_BYTE_L3FE", cntPkt);
		
		cntPkt = rtk_ne_reg_read(NI_HV_INTPT_TX_PKT_BYTE_HI_CNT+0x40);
		cntPkt = ((cntPkt << 32) | rtk_ne_reg_read(NI_HV_INTPT_TX_PKT_BYTE_LO_CNT+0x40));
		if(cntPkt!=0) PROC_PRINTF("%-40s: %llu\n", "NI_HV_INTPT_TX_PKT_BYTE_L3QM", cntPkt);
		cntPkt = rtk_ne_reg_read(NI_HV_INTPT_RX_PKT_BYTE_HI_CNT+0x40);
		cntPkt = ((cntPkt << 32) | rtk_ne_reg_read(NI_HV_INTPT_RX_PKT_BYTE_LO_CNT+0x40));
		if(cntPkt!=0) PROC_PRINTF("%-40s: %llu\n", "NI_HV_INTPT_RX_PKT_BYTE_L3QM", cntPkt);		

		regvalue = rtk_ne_reg_read(NI_HV_INTPT_RX_MISSING_SOP_EOP_CNT);
		if(regvalue!=0) PROC_PRINTF("%-40s: sop %d eop %d\n", "NI_HV_INTPT_RX_MISSING_SOP_EOP_CNT_L3FE", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_RX_MISSING_SOP_EOP_CNT+0x40);
		if(regvalue!=0) PROC_PRINTF("%-40s: sop %d eop %d\n", "NI_HV_INTPT_RX_MISSING_SOP_EOP_CNT_L3QM", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_RX_MISSING_SOP_EOP_CNT+0x80);
		if(regvalue!=0) PROC_PRINTF("%-40s: sop %d eop %d\n", "NI_HV_INTPT_RX_MISSING_SOP_EOP_CNT_MCE", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_RX_MISSING_SOP_EOP_CNT+0xc0);
		if(regvalue!=0) PROC_PRINTF("%-40s: sop %d eop %d\n", "NI_HV_INTPT_RX_MISSING_SOP_EOP_CNT_DMA", regvalue>>16 & 0xffff, regvalue & 0xffff);
		
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_RX_SHORT_ERR_CNT);
		if(regvalue!=0) PROC_PRINTF("%-40s: short %d err %d\n", "NI_HV_INTPT_RX_SHORT_ERR_CNT_L3FE", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_RX_SHORT_ERR_CNT+0x40);
		if(regvalue!=0) PROC_PRINTF("%-40s: short %d err %d\n", "NI_HV_INTPT_RX_SHORT_ERR_CNT_L3QM", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_RX_SHORT_ERR_CNT+0x80);
		if(regvalue!=0) PROC_PRINTF("%-40s: short %d err %d\n", "NI_HV_INTPT_RX_SHORT_ERR_CNT_MCE", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_RX_SHORT_ERR_CNT+0xc0);
		if(regvalue!=0) PROC_PRINTF("%-40s: short %d err %d\n", "NI_HV_INTPT_RX_SHORT_ERR_CNT_DMA", regvalue>>16 & 0xffff, regvalue & 0xffff);
		
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_TX_MISSING_SOP_EOP_CNT);
		if(regvalue!=0) PROC_PRINTF("%-40s: sop %d eop %d\n", "NI_HV_INTPT_TX_MISSING_SOP_EOP_CNT_L3FE", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_TX_MISSING_SOP_EOP_CNT+0x40);
		if(regvalue!=0) PROC_PRINTF("%-40s: sop %d eop %d\n", "NI_HV_INTPT_TX_MISSING_SOP_EOP_CNT_L3QM", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_TX_MISSING_SOP_EOP_CNT+0x80);
		if(regvalue!=0) PROC_PRINTF("%-40s: sop %d eop %d\n", "NI_HV_INTPT_TX_MISSING_SOP_EOP_CNT_MCE", regvalue>>16 & 0xffff, regvalue & 0xffff);
		
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_TX_SHORT_ERR_CNT);
		if(regvalue!=0) PROC_PRINTF("%-40s: short %d err %d\n", "NI_HV_INTPT_TX_SHORT_ERR_CNT_L3FE", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_TX_SHORT_ERR_CNT+0x40);
		if(regvalue!=0) PROC_PRINTF("%-40s: short %d err %d\n", "NI_HV_INTPT_TX_SHORT_ERR_CNT_L3QM", regvalue>>16 & 0xffff, regvalue & 0xffff);
		regvalue = rtk_ne_reg_read(NI_HV_INTPT_TX_SHORT_ERR_CNT+0x80);
		if(regvalue!=0) PROC_PRINTF("%-40s: short %d err %d\n", "NI_HV_INTPT_TX_SHORT_ERR_CNT_MCE", regvalue>>16 & 0xffff, regvalue & 0xffff);
	 	
	}
	PROC_PRINTF("\n");
#endif


	//2 L2FE cnt
	PROC_PRINTF("===== "COLOR_Y"L2FE counter"COLOR_NM" ===== (read-and-clear)\n");
	{
		PROC_PRINT_REG(L2FE_PP_NI_INTF_DROP_CNT);
		regvalue = rtk_ne_reg_read(L2FE_PP_NI_INTF_PKT_CNT);
		if(regvalue!=0) PROC_PRINTF("%-40s: sop %d eop %d\n", "L2FE_PP_NI_INTF_PKT_CNT (16bits)", regvalue>>16 & 0xffff, regvalue & 0xffff);

		// clear
		rtk_ne_reg_write(0, L2FE_PP_NI_INTF_PKT_CNT);
		
		if((ret =aal_l2_fe_drop_source_cnt_get(G3_DEF_DEVID, &l2fedrop))!= AAL_E_OK){
			PROC_PRINTF("ERROR! Fail to get l2fe drop count,  ret=%d)\n", ret);

		}else {
			if(l2fedrop.drop_source_cnt_00 != 0) PROC_PRINTF("%-40s: %u\n", "drop_source_cnt_00", l2fedrop.drop_source_cnt_00);
			if(l2fedrop.ipv4_cs_chk_drp_01 != 0) PROC_PRINTF("%-40s: %u\n", "ipv4_cs_chk_drp_01", l2fedrop.ipv4_cs_chk_drp_01);
			if(l2fedrop.dpid_blackhole_drp_02 != 0) PROC_PRINTF("%-40s: %u\n", "dpid_blackhole_drp_02", l2fedrop.dpid_blackhole_drp_02);
			if(l2fedrop.ilpb_stp_chk_drp_03 != 0) PROC_PRINTF("%-40s: %u\n", "ilpb_stp_chk_drp_03", l2fedrop.ilpb_stp_chk_drp_03);
			if(l2fedrop.vlan_stp_chk_drp_04 != 0) PROC_PRINTF("%-40s: %u\n", "vlan_stp_chk_drp_04", l2fedrop.vlan_stp_chk_drp_04);
			if(l2fedrop.ilpb_vlan_type_drp_05 != 0) PROC_PRINTF("%-40s: %u\n", "ilpb_vlan_type_drp_05", l2fedrop.ilpb_vlan_type_drp_05);
			if(l2fedrop.igr_cls_pmt_stp_drp_06 != 0) PROC_PRINTF("%-40s: %u\n", "igr_cls_pmt_stp_drp_06", l2fedrop.igr_cls_pmt_stp_drp_06);
			if(l2fedrop.vid4095_drp_07 != 0) PROC_PRINTF("%-40s: %u\n", "vid4095_drp_07", l2fedrop.vid4095_drp_07);
			if(l2fedrop.unknown_vlan_drp_08 != 0) PROC_PRINTF("%-40s: %u\n", "unknown_vlan_drp_08", l2fedrop.unknown_vlan_drp_08);
			if(l2fedrop.ve_deny_mc_flag_drp_09 != 0) PROC_PRINTF("%-40s: %u\n", "ve_deny_mc_flag_drp_09", l2fedrop.ve_deny_mc_flag_drp_09);
			if(l2fedrop.l2e_da_deny_drp_10 != 0) PROC_PRINTF("%-40s: %u\n", "l2e_da_deny_drp_10", l2fedrop.l2e_da_deny_drp_10);
			if(l2fedrop.non_std_sa_drp_11 != 0) PROC_PRINTF("%-40s: %u\n", "non_std_sa_drp_11", l2fedrop.non_std_sa_drp_11);
			if(l2fedrop.sw_learn_err_drp_12 != 0) PROC_PRINTF("%-40s: %u\n", "sw_learn_err_drp_12", l2fedrop.sw_learn_err_drp_12);
			if(l2fedrop.l2e_sa_deny_drp_13 != 0) PROC_PRINTF("%-40s: %u\n", "l2e_sa_deny_drp_13", l2fedrop.l2e_sa_deny_drp_13);
			if(l2fedrop.sa_mis_or_sa_limit_drp_14 != 0) PROC_PRINTF("%-40s: %u\n", "sa_mis_or_sa_limit_drp_14", l2fedrop.sa_mis_or_sa_limit_drp_14);
			if(l2fedrop.uuc_umc_bc_drp_15 != 0) PROC_PRINTF("%-40s: %u\n", "uuc_umc_bc_drp_15", l2fedrop.uuc_umc_bc_drp_15);
			if(l2fedrop.spid_dpid_not_in_rx_vlan_mem_drp_16 != 0) PROC_PRINTF("%-40s: %u\n", "spid_dpid_not_in_rx_vlan_mem_drp_16", l2fedrop.spid_dpid_not_in_rx_vlan_mem_drp_16);
			if(l2fedrop.dpid_not_in_port_mem_drp_17 != 0) PROC_PRINTF("%-40s: %u\n", "dpid_not_in_port_mem_drp_17", l2fedrop.dpid_not_in_port_mem_drp_17);
			if(l2fedrop.spid_dpid_not_in_tx_vlan_mem_drp_18 != 0) PROC_PRINTF("%-40s: %u\n", "spid_dpid_not_in_tx_vlan_mem_drp_18", l2fedrop.spid_dpid_not_in_tx_vlan_mem_drp_18);
			if(l2fedrop.tx_vlan_stp_drp_19 != 0) PROC_PRINTF("%-40s: %u\n", "tx_vlan_stp_drp_19", l2fedrop.tx_vlan_stp_drp_19);
			if(l2fedrop.egr_cls_deny_drp_20 != 0) PROC_PRINTF("%-40s: %u\n", "egr_cls_deny_drp_20", l2fedrop.egr_cls_deny_drp_20);
			if(l2fedrop.loopback_drp_21 != 0) PROC_PRINTF("%-40s: %u\n", "loopback_drp_21", l2fedrop.loopback_drp_21);
			if(l2fedrop.elpb_stp_drp_22 != 0) PROC_PRINTF("%-40s: %u\n", "elpb_stp_drp_22", l2fedrop.elpb_stp_drp_22);
			if(l2fedrop.drop_source_cnt_23 != 0) PROC_PRINTF("%-40s: %u\n", "drop_source_cnt_23", l2fedrop.drop_source_cnt_23);
			if(l2fedrop.drop_source_cnt_24 != 0) PROC_PRINTF("%-40s: %u\n", "drop_source_cnt_24", l2fedrop.drop_source_cnt_24);
			if(l2fedrop.mcfib_vlan_drp_25 != 0) PROC_PRINTF("%-40s: %u\n", "mcfib_vlan_drp_25", l2fedrop.mcfib_vlan_drp_25);
			if(l2fedrop.drop_source_cnt_26 != 0) PROC_PRINTF("%-40s: %u\n", "drop_source_cnt_26", l2fedrop.drop_source_cnt_26);
			if(l2fedrop.mcfib_dpid_equal_spid_drp_27 != 0) PROC_PRINTF("%-40s: %u\n", "mcfib_dpid_equal_spid_drp_27", l2fedrop.mcfib_dpid_equal_spid_drp_27);
			if(l2fedrop.drop_source_cnt_28 != 0) PROC_PRINTF("%-40s: %u\n", "drop_source_cnt_28", l2fedrop.drop_source_cnt_28);
			if(l2fedrop.drop_source_cnt_29 != 0) PROC_PRINTF("%-40s: %u\n", "drop_source_cnt_29", l2fedrop.drop_source_cnt_29);
			if(l2fedrop.drop_source_cnt_30 != 0) PROC_PRINTF("%-40s: %u\n", "drop_source_cnt_30", l2fedrop.drop_source_cnt_30);
			if(l2fedrop.drop_source_cnt_31 != 0) PROC_PRINTF("%-40s: %u\n", "drop_source_cnt_31", l2fedrop.drop_source_cnt_31);
		}
	}
	PROC_PRINTF("\n");

	//2 L3FE cnt
	PROC_PRINTF("===== "COLOR_Y"L3FE counter"COLOR_NM" ===== (read-and-clear)\n");
	{
		L3FE_PP_SPCL_PKT_DETECTION_CFG_t l3spcl_cfg;

		l3spcl_cfg.wrd = rtk_ne_reg_read(L3FE_PP_SPCL_PKT_DETECTION_CFG);
		if(l3spcl_cfg.bf.ni2fe_pkt_cnt_wrap_en==0) {
			l3spcl_cfg.bf.ni2fe_pkt_cnt_wrap_en = 1;
			rtk_ne_reg_write(l3spcl_cfg.wrd, L3FE_PP_SPCL_PKT_DETECTION_CFG);
		}
			
		regvalue = rtk_ne_reg_read(L3FE_PP_NI_INTF_PKT_CNT);
		if(regvalue!=0) PROC_PRINTF("%-40s: sop %u eop %un", "L3FE_PP_NI_INTF_PKT_CNT (16bits)", regvalue>>16 & 0xffff, regvalue & 0xffff);
		
		regvalue = rtk_ne_reg_read(L3FE_PP_PARSING_STTS_0);
		if(regvalue!=0) PROC_PRINTF("%-40s: 0x%x\n", "L3FE_PP_PARSING_STTS_0_L4ERR", regvalue);
		PROC_PRINT_REG(L3FE_PP_L4_CS_ERR);
		regvalue = rtk_ne_reg_read(L3FE_PP_PARSING_STTS_1);
		if(regvalue!=0) PROC_PRINTF("%-40s: 0x%x\n", "L3FE_PP_PARSING_STTS_1_PKTERR", regvalue);

		// clear
		rtk_ne_reg_write(0, L3FE_PP_PARSING_STTS_0);
		rtk_ne_reg_write(0, L3FE_PP_PARSING_STTS_1);
		rtk_ne_reg_write(0, L3FE_PP_L4_CS_ERR);
		
		for (i = 0; i < L3PE_CNTR_DROP_SRC_ENTRY_MAX; i++) {
			if ((ret = aal_l3pe_cntr_drop_src_pkt_get(G3_DEF_DEVID, i, &cntPkt)) != AAL_E_OK) {
				PROC_PRINTF("ERROR! Fail to get l3fe drop count. (counter index=%lu, ret=%d)\n", i, ret);
				continue;
			}
			if(cntPkt != 0) {
				PROC_PRINTF("%-40s: %u\n", CounterDesc[i], cntPkt);
			}
		}
		aal_l3pe_cntr_drop_src_pkt_clear(G3_DEF_DEVID);
	}
	PROC_PRINTF("\n");

	//2 L2TE cnt
	PROC_PRINTF("===== "COLOR_Y"L2TE counter"COLOR_NM" ===== (read-and-clear)\n");
	PROC_PRINTF("%-13s  %-13s  %-13s  %-13s  %-13s  %-13s  %-13s\r\n", "TotalPkt", "PolDropPkt", "TailDropPkt", "WredYellow", "WredGreen", "Bypass", "BypassFlow");
	PROC_PRINTF("-----------------------------------------------------------------------------------\r\n");
	{
		memset(&pm_stat_stats, 0, sizeof(pm_stat_stats));
		ret = aal_l2_te_pm_stats_get(G3_DEF_DEVID, &pm_stat_stats);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l2 te stats counter. (ret=%d)\n", ret);
		}else {
			if(pm_stat_stats.total_cnt!=0) {
				PROC_PRINTF("%-13u  %-13u  %-13u  %-13u  %-13u  %-13u  %-13u\r\n",
					pm_stat_stats.total_cnt, pm_stat_stats.policer_drop_cnt, pm_stat_stats.tail_drop_cnt, pm_stat_stats.wred_yellow_drop_cnt, pm_stat_stats.wred_green_drop_cnt, 
					pm_stat_stats.bypass_cnt, pm_stat_stats.bypass_flow_cnt);
			}
		}
	}

	PROC_PRINTF("\n");

	// l2te dpid
	//PROC_PRINTF("===== "COLOR_Y"L3TE voq  counter"COLOR_NM" ===== (read-and-clear)\n");
	PROC_PRINTF("%-4s %-8s  %-13s  %-13s  %-13s  %-13s\r\n", "DPID", "Alias", "TotalPkt", "TailDropPkt", "WredMarkPkt", "WredUnMarkPkt");
	PROC_PRINTF("-----------------------------------------------------------------------------------\r\n");
	for (i = 0; i < CA_AAL_MAX_PORT_ID; i++) {
		char alias[16] = {0};
		memset(&pm_stats, 0, sizeof(pm_stats));
		ret = aal_l2_te_pm_egress_port_get(G3_DEF_DEVID, i, &pm_stats);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l2 te port counter. (voq index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if (0 == pm_stats.total_pkt + pm_stats.td_drop_pkt + pm_stats.wred_mark_pkt + pm_stats.wred_unmark_pkt)
			continue;

		switch(i) {
			case 0 ... 7:
   				sprintf(alias, "%s", "Phy");
				break;
			case 8:
   				sprintf(alias, "%s", "DQ-NI");
				break;
			case 9:
   				sprintf(alias, "%s", "DQ-CPU");
				break;
			case 10:
   				sprintf(alias, "%s", "L3FE-WAN");
				break;
			case 13:
   				sprintf(alias, "%s", "L3FE-LAN");
				break;
			case 11:
   				sprintf(alias, "%s", "MC-ENG");
				break;
			default:
   				sprintf(alias, "%s", "---");
				break;
		}
		

		PROC_PRINTF("%-4u %-8s  %-13u  %-13u  %-13u  %-13u\r\n",
			i, alias, pm_stats.total_pkt, pm_stats.td_drop_pkt, pm_stats.wred_mark_pkt, pm_stats.wred_unmark_pkt);
	}
	PROC_PRINTF("\n");
	
	// l2te voq
	//PROC_PRINTF("===== "COLOR_Y"L2TE voq  counter"COLOR_NM" ===== (read-and-clear)\n");
	PROC_PRINTF("%-4s %-8s  %-13s  %-13s  %-13s  %-13s  %-13s\r\n", "Voq", "Port-Voq", "TotalPkt", "TailDropPkt", "WredMarkPkt", "WredUnMarkPkt", "Max-Buf");
	PROC_PRINTF("-----------------------------------------------------------------------------------\r\n");
	for (i = 0; i <= CA_AAL_MAX_VOQ_ID; i++) {
		memset(&pm_stats, 0, sizeof(pm_stats));
		ret = aal_l2_te_pm_egress_voq_get(G3_DEF_DEVID, i, &pm_stats);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l2 te voq counter. (voq index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if (0 == pm_stats.total_pkt + pm_stats.td_drop_pkt + pm_stats.wred_mark_pkt + pm_stats.wred_unmark_pkt)
			continue;

		ret = aal_l2_tm_cb_voq_max_buf_get(G3_DEF_DEVID, i, &num16);

		PROC_PRINTF("%-4u %2u-%-5u  %-13u  %-13u  %-13u  %-13u  %-13u\r\n",
			i, i/8, i%8,pm_stats.total_pkt, pm_stats.td_drop_pkt, pm_stats.wred_mark_pkt, pm_stats.wred_unmark_pkt, num16);
	}
	PROC_PRINTF("\n");
	
	// l2te pol - Flow/AgrFlow/Port/PktType
	//PROC_PRINTF("===== "COLOR_Y"L2TE pol  counter"COLOR_NM" ===== (read-and-clear)\n");
	PROC_PRINTF("%-8s %-4s  %-13s  %-13s  %-13s  %-13s\r\n", "Policer", "Idx", "TotalPkt", "RedPkt(drop)", "YellowPkt", "GreenPkt");
	PROC_PRINTF("-----------------------------------------------------------------------------------\r\n");
	for (i = 0; i < CA_AAL_MAX_FLOW_ID; i++) {
		memset(&pm_pol, 0, sizeof(pm_pol));
		ret = aal_l2_te_pm_policer_flow_get(G3_DEF_DEVID, i, &pm_pol);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l2 te Flow pol counter. (pol index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if ((cntPkt = (pm_pol.red_pkt + pm_pol.yellow_pkt + pm_pol.green_pkt)) == 0)
			continue;

		PROC_PRINTF("%-8s %-4u  %-13llu  %-13u  %-13u  %-13u\r\n",
			"Flow", i, cntPkt, pm_pol.red_pkt, pm_pol.yellow_pkt, pm_pol.green_pkt);
	}
	for (i = 0; i <= CA_AAL_MAX_AGR_FLOW_ID; i++) {
		memset(&pm_pol, 0, sizeof(pm_pol));
		ret = aal_l2_te_pm_policer_agr_flow_get(G3_DEF_DEVID, i, &pm_pol);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l2 te AgrFlow pol counter. (pol index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if ((cntPkt = (pm_pol.red_pkt + pm_pol.yellow_pkt + pm_pol.green_pkt)) == 0)
			continue;

		PROC_PRINTF("%-8s %-4u  %-13llu  %-13u  %-13u  %-13u\r\n",
			"AgrFlow", i, cntPkt, pm_pol.red_pkt, pm_pol.yellow_pkt, pm_pol.green_pkt);
	}
	for (i = 0; i <= CA_AAL_MAX_PORT_ID; i++) {
		memset(&pm_pol, 0, sizeof(pm_pol));
		ret = aal_l2_te_pm_policer_port_get(G3_DEF_DEVID, i, &pm_pol);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l2 te Port pol counter. (pol index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if ((cntPkt = (pm_pol.red_pkt + pm_pol.yellow_pkt + pm_pol.green_pkt)) == 0)
			continue;

		PROC_PRINTF("%-8s %-4u  %-13llu  %-13u  %-13u  %-13u\r\n",
			"SrcPort", i, cntPkt, pm_pol.red_pkt, pm_pol.yellow_pkt, pm_pol.green_pkt);
	}
	for (i = 0; i < CA_AAL_POLICER_PKT_TYPE_END; i++) {
		memset(&pm_pol, 0, sizeof(pm_pol));
		ret = aal_l2_te_pm_policer_pkt_type_get(G3_DEF_DEVID, i, &pm_pol);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l2 te PKTTYPE pol counter. (pol index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if ((cntPkt = (pm_pol.red_pkt + pm_pol.yellow_pkt + pm_pol.green_pkt)) == 0)
			continue;

		PROC_PRINTF("%-8s %-4u  %-13llu  %-13u  %-13u  %-13u\r\n",
			"PktType", i, cntPkt, pm_pol.red_pkt, pm_pol.yellow_pkt, pm_pol.green_pkt);
	}
	PROC_PRINTF("\n");


	//2 L2TM cnt
	PROC_PRINTF("===== "COLOR_Y"L2TM counter"COLOR_NM" ===== (read-and-clear)\n");
	{	
		PROC_PRINT_REG(L2TM_L2TM_BM_RX_DQ_PCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_RX_DQ_DPCNT);
#if !defined(CONFIG_FC_G3_G3LITE_SERIES)
		PROC_PRINT_REG(L2TM_L2TM_BM_RX_DQ_ABR_DPCNT);
#endif
		PROC_PRINT_REG(L2TM_L2TM_BM_RX_PCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_TX_PCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_SB_DPCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_HDR_DPCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_TE_DPCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_ERR_DPCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_FC_MIRROR0_DPCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_FC_MIRROR1_DPCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_FC_MIRROR2_DPCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_FC_MIRROR3_DPCNT);
		PROC_PRINT_REG(L2TM_L2TM_BM_RX_DPCNT);
#if !defined(CONFIG_FC_G3_G3LITE_SERIES)
		PROC_PRINT_REG(L2TM_L2TM_BM_RX_ABR_DPCNT);
#endif
		PROC_PRINT_REG(L2TM_L2TM_BM_NOBUF_DPCNT);
	}
	PROC_PRINTF("\n");

	//2 L3TE cnt
	PROC_PRINTF("===== "COLOR_Y"L3TE counter"COLOR_NM" ===== (read-and-clear)\n");
	PROC_PRINTF("%-13s  %-13s  %-13s  %-13s  %-13s  %-13s  %-13s\r\n", "TotalPkt", "PolDropPkt", "TailDropPkt", "WredYellow", "WredGreen", "Bypass", "BypassFlow");
	PROC_PRINTF("-----------------------------------------------------------------------------------\r\n");
	{
		memset(&l3pm_stat_stats, 0, sizeof(l3pm_stat_stats));
		ret = aal_l3_te_pm_stats_get(G3_DEF_DEVID, &l3pm_stat_stats);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l2 te stats counter. (ret=%d)\n", ret);
		}else {
			if(l3pm_stat_stats.total_cnt!=0) {
				PROC_PRINTF("%-13u  %-13u  %-13u  %-13u  %-13u  %-13u  %-13u\r\n",
					l3pm_stat_stats.total_cnt, l3pm_stat_stats.policer_drop_cnt, l3pm_stat_stats.tail_drop_cnt, l3pm_stat_stats.wred_yellow_drop_cnt, l3pm_stat_stats.wred_green_drop_cnt, 
					l3pm_stat_stats.bypass_cnt, l3pm_stat_stats.bypass_flow_cnt);
			}
		}
	}
	PROC_PRINTF("\n");


	// l3te dpid
	//PROC_PRINTF("===== "COLOR_Y"L3TE voq  counter"COLOR_NM" ===== (read-and-clear)\n");
	PROC_PRINTF("%-4s %-8s  %-13s  %-13s  %-13s  %-13s\r\n", "DPID", "Alias", "TotalPkt", "TailDropPkt", "WredMarkPkt", "WredUnMarkPkt");
	PROC_PRINTF("-----------------------------------------------------------------------------------\r\n");
	for (i = 0; i <= CA_AAL_MAX_PORT_ID; i++) {
		memset(&l3pm_stats, 0, sizeof(l3pm_stats));
		ret = aal_l3_te_pm_egress_port_get(G3_DEF_DEVID, i, &l3pm_stats);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l3 te port counter. (voq index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if (0 == l3pm_stats.total_pkt + l3pm_stats.td_drop_pkt + l3pm_stats.wred_mark_pkt + l3pm_stats.wred_unmark_pkt)
			continue;

		PROC_PRINTF("%-4u %s%-4u  %-13u  %-13u  %-13u  %-13u\r\n",
			i, i<8?"cpu ":"ni  ", (i<8)?(i):(i-8), l3pm_stats.total_pkt, l3pm_stats.td_drop_pkt, l3pm_stats.wred_mark_pkt, l3pm_stats.wred_unmark_pkt);
	}
	PROC_PRINTF("\n");


	// l3te voq
	//PROC_PRINTF("===== "COLOR_Y"L3TE voq  counter"COLOR_NM" ===== (read-and-clear)\n");
	PROC_PRINTF("%-4s %-8s  %-13s  %-13s  %-13s  %-13s  %-13s\r\n", "Voq", "Port-Voq", "TotalPkt", "TailDropPkt", "WredMarkPkt", "WredUnMarkPkt", "Max-Buf");
	PROC_PRINTF("-----------------------------------------------------------------------------------\r\n");
	for (i = 0; i <= CA_AAL_MAX_VOQ_ID; i++) {
		memset(&l3pm_stats, 0, sizeof(l3pm_stats));
		ret = aal_l3_te_pm_egress_voq_get(G3_DEF_DEVID, i, &l3pm_stats);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l3 te voq counter. (voq index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if (0 == l3pm_stats.total_pkt + l3pm_stats.td_drop_pkt + l3pm_stats.wred_mark_pkt + l3pm_stats.wred_unmark_pkt)
			continue;

		ret = aal_l3_te_cb_voq_max_buf_get(G3_DEF_DEVID, i, &num16);

		PROC_PRINTF("%-4u %s%u-%-2u  %-13u  %-13u  %-13u  %-13u  %-13u\r\n",
			i, i<64?"cpu ":"ni  ", (i<64)?(i/8):(i/8-8), i%8, l3pm_stats.total_pkt, l3pm_stats.td_drop_pkt, l3pm_stats.wred_mark_pkt, l3pm_stats.wred_unmark_pkt, num16);
	}
	PROC_PRINTF("\n");

	// l3te pol
	//PROC_PRINTF("===== "COLOR_Y"L3TE pol  counter"COLOR_NM" ===== (read-and-clear)\n");
	PROC_PRINTF("%-8s %-4s  %-13s  %-13s  %-13s  %-13s\r\n", "Policer", "Idx", "TotalPkt", "RedPkt(drop)", "YellowPkt", "GreenPkt");
	PROC_PRINTF("-----------------------------------------------------------------------------------\r\n");
	for (i = 0; i <= CA_AAL_L3_MAX_FLOW_ID; i++) {
		memset(&l3pm_pol, 0, sizeof(l3pm_pol));
		ret = aal_l3_te_pm_policer_flow_get(G3_DEF_DEVID, i, &l3pm_pol);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l3 te pol counter. (pol index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if ((cntPkt = (l3pm_pol.red_pkt + l3pm_pol.yellow_pkt + l3pm_pol.green_pkt)) == 0)
			continue;

		PROC_PRINTF("%-8s %-4u  %-13llu  %-13u  %-13u  %-13u\r\n",
			"Flow", i, cntPkt, l3pm_pol.red_pkt, l3pm_pol.yellow_pkt, l3pm_pol.green_pkt);
	}
#if !defined(CONFIG_FC_G3_G3LITE_SERIES)
	for (i = 0; i <= CA_AAL_L3_MAX_FLOW2_ID; i++) {
		memset(&l3pm_pol, 0, sizeof(l3pm_pol));
		ret = aal_l3_te_pm_policer_agr_flow_get(G3_DEF_DEVID, i, &l3pm_pol);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l3 te AgrFlow pol counter. (pol index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if ((cntPkt = (l3pm_pol.red_pkt + l3pm_pol.yellow_pkt + l3pm_pol.green_pkt)) == 0)
			continue;

		PROC_PRINTF("%-8s %-4u  %-13llu  %-13u  %-13u  %-13u\r\n",
			"Flow2", i, cntPkt, l3pm_pol.red_pkt, l3pm_pol.yellow_pkt, l3pm_pol.green_pkt);
	}
	for (i = 0; i <= CA_AAL_L3_MAX_FLOW3_ID; i++) {
		memset(&l3pm_pol, 0, sizeof(l3pm_pol));
		ret = aal_l3_te_pm_policer_flow3_get(G3_DEF_DEVID, i, &l3pm_pol);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l3 te Flow3 pol counter. (pol index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if ((cntPkt = (l3pm_pol.red_pkt + l3pm_pol.yellow_pkt + l3pm_pol.green_pkt)) == 0)
			continue;

		PROC_PRINTF("%-8s %-4u  %-13llu  %-13u  %-13u  %-13u\r\n",
			"Flow3", i, cntPkt, l3pm_pol.red_pkt, l3pm_pol.yellow_pkt, l3pm_pol.green_pkt);
	}
#endif
	for (i = 0; i <= (CA_AAL_L3_MAX_PORT_ID+1); i++) {
		memset(&l3pm_pol, 0, sizeof(l3pm_pol));
		ret = aal_l3_te_pm_policer_port_get(G3_DEF_DEVID, i, &l3pm_pol);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l3 te Port pol counter. (pol index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if ((cntPkt = (l3pm_pol.red_pkt + l3pm_pol.yellow_pkt + l3pm_pol.green_pkt)) == 0)
			continue;

		PROC_PRINTF("%-8s %-4u  %-13llu  %-13u  %-13u  %-13u\r\n",
			(i<=CA_AAL_L3_MAX_PORT_ID) ? "SrcPort" : "SrcPortx", i, cntPkt, l3pm_pol.red_pkt, l3pm_pol.yellow_pkt, l3pm_pol.green_pkt);
	}
	for (i = 0; i <= CA_AAL_POLICER_PKT_TYPE_RSV2; i++) {
		memset(&l3pm_pol, 0, sizeof(l3pm_pol));
		ret = aal_l3_te_pm_policer_pkt_type_get(G3_DEF_DEVID, i, &l3pm_pol);
		if (CA_E_OK != ret) {
			PROC_PRINTF("ERROR! Fail to get l3 te PKTTYPE pol counter. (pol index=%lu, ret=%d)\n", i, ret);
			continue;
		}
		if ((cntPkt = (l3pm_pol.red_pkt + l3pm_pol.yellow_pkt + l3pm_pol.green_pkt)) == 0)
			continue;

		PROC_PRINTF("%-8s %-4u  %-13llu  %-13u  %-13u  %-13u\r\n",
			"PktType", i, cntPkt, l3pm_pol.red_pkt, l3pm_pol.yellow_pkt, l3pm_pol.green_pkt);
	}

	PROC_PRINTF("\n");
	
	//2 L3QM cnt 
	// ref: aal_l3qm_dump_rmu_fe_drop_counter
	PROC_PRINTF("===== "COLOR_Y"L3QM counter"COLOR_NM" ===== (read-and-clear)\n");

	rx_pkt_cntr.wrd = rtk_ne_reg_read(QM_QM_RMU0_RX_PKT_CNTR);
	tx_pkt_schedule_cntr.wrd = rtk_ne_reg_read(QM_QM_TX_PKT_CNTR);
	tx_pkt_ni_cntr.wrd = rtk_ne_reg_read(QM_QM_TX_PKT_CNTR_ALL_NI);
	
	if(rx_pkt_cntr.bf.cntr!=0)
		PROC_PRINTF("%-30s = %u\n", "RX_PKT_CNTR (good pkts)", rx_pkt_cntr.bf.cntr);
	if(tx_pkt_schedule_cntr.bf.cntr!=0) {
		PROC_PRINTF("%-30s = %u\n", "TX_PKT_CNTR (scheduled pkts)", tx_pkt_schedule_cntr.bf.cntr);
		PROC_PRINTF("%-30s = %u\n", "TX_PKT_CNTR_NI", tx_pkt_ni_cntr.bf.cntr);
		for(i = 0; i<QM_QM_TX_PKT_CNTR_CPU0_COUNT; i++) {
			tx_pkt_cpu_cntr.wrd = rtk_ne_reg_read(QM_QM_TX_PKT_CNTR_CPU0 + (i*QM_QM_TX_PKT_CNTR_CPU0_STRIDE));
			if(tx_pkt_cpu_cntr.wrd!=0)
				PROC_PRINTF("%s[%d]%-12s = %u\n", "TX_PKT_CNTR_CPU",  i, " ", tx_pkt_cpu_cntr.bf.cntr);
		}

		
		// clear qm counter manually
		rtk_ne_reg_write(0, QM_QM_RMU0_RX_PKT_CNTR);
		rtk_ne_reg_write(0, QM_QM_TX_PKT_CNTR);
		rtk_ne_reg_write(0, QM_QM_TX_PKT_CNTR_ALL_NI);
		for(i = 0; i<QM_QM_TX_PKT_CNTR_CPU0_COUNT; i++) {
			rtk_ne_reg_write(0, (QM_QM_TX_PKT_CNTR_CPU0 + (i*QM_QM_TX_PKT_CNTR_CPU0_STRIDE)));
		}
	}
	
	
	int_src.wrd = rtk_ne_reg_read(QM_QM_INT_SRC);
	
	no_buf_drop_info.wrd = rtk_ne_reg_read(QM_QM_RMU0_NO_BUF_DROP_PKT_INFO);
	no_buf_drop_cntr.wrd = rtk_ne_reg_read(QM_QM_RMU0_NO_BUF_DROP_PKT_CNTR);
#if !defined(CONFIG_FC_G3_G3LITE_SERIES)
	drop_cntr.wrd = rtk_ne_reg_read(QM_QM_RMU0_FE_DROP_PKT_CNTR);
	eop_drop_cntr.wrd = rtk_ne_reg_read(QM_QM_RX_EOP_DROP_CNTR);
	rx_len_err_cntr.wrd = rtk_ne_reg_read(QM_QM_RX_LEN_CHK_ERROR_CNTR);
	qm_l2te_tail_drop_cntr.wrd = rtk_ne_reg_read(QM_QM_RX_L2TE_TAIL_DROP_CNTR);
#endif
	
	if(int_src.wrd!= 0) {
		PROC_PRINTF("QM_QM_INT_SRC: 0x%x\n", int_src.wrd);
		PROC_PRINTF(" - rmu0_no_buffer_drop =%d (NO_BUF_DROP)\n", int_src.bf.rmu0_no_buffer_drop_int_src);
		PROC_PRINTF(" - rmu0_check_error =%d\n",	int_src.bf.rmu0_check_error_int_src);
		PROC_PRINTF(" - rmu0_fifo_error =%d, \n", int_src.bf.rmu0_fifo_error_int_src);
		PROC_PRINTF(" - pkt_lenght_error =%d \n",	int_src.bf.pkt_lenght_error_int_src);
		PROC_PRINTF(" - dqm_fe_drop =%d\n", int_src.bf.dqm_fe_drop_int_src);
		PROC_PRINTF(" - sop_eop_error =%d\n", int_src.bf.sop_eop_error_int_src);
	}


	if(no_buf_drop_cntr.bf.cntr!=0) {
		PROC_PRINTF("NO_BUF_DROP_CNTR =%d\n", no_buf_drop_cntr.bf.cntr);
		PROC_PRINTF("eqid=%d, voqid=%d\n", no_buf_drop_info.bf.eqid, no_buf_drop_info.bf.voqid);
	}

#if !defined(CONFIG_FC_G3_G3LITE_SERIES)
	if(drop_cntr.bf.cntr!=0)
		PROC_PRINTF("%-30s = %u\n", "RMU0_FE_DROP_PKT_CNTR", drop_cntr.bf.cntr);

	if(eop_drop_cntr.bf.cntr!=0)
		PROC_PRINTF("%-30s = %u\n", "RX_EOP_DROP_CNTR", eop_drop_cntr.bf.cntr);
	
	if(rx_len_err_cntr.bf.cntr!=0)
		PROC_PRINTF("%-30s = %u\n", "RX_LEN_CHK_ERROR_CNTR", rx_len_err_cntr.bf.cntr);

	if(qm_l2te_tail_drop_cntr.bf.cntr)
		PROC_PRINTF("%-30s = %u\n", "RX_L2TE_TAIL_DROP_CNTR", qm_l2te_tail_drop_cntr.bf.cntr);
#endif

	rtk_ne_reg_write(0xFFFFFFFF, QM_QM_INT_SRC);	// write 1 clear
	
	
	PROC_PRINTF("\n");

	PROC_PRINTF("===== "COLOR_Y"DMALSO counter"COLOR_NM" =====\n");

	for(i = 0; i < CA_NI_DMA_LSO_VP_NUM; i++)
	{
		for(j = 0; j < CA_NI_DMA_LSO_TXQ_NUM; j++)
		{
			ca_uint32_t access_val = 0, data_val = 0;
			unsigned int tx_cnt;
			int timeout_counter;

			// read VP[i] txq[j] transmit packet counter
			access_val = 1 << 31;
			access_val |= j << 0;	// txq id
			rtk_dma_lso_reg_write(access_val, DMA_SEC_DMA_LSO_VP_TXQ_PKTCNT_ACCESS + (i * DMA_SEC_DMA_LSO_VP_STRIDE));

			timeout_counter = 0;
			do {
				access_val = rtk_dma_lso_reg_read(DMA_SEC_DMA_LSO_VP_TXQ_PKTCNT_ACCESS + (i * DMA_SEC_DMA_LSO_VP_STRIDE));
			} while (((1 << 31) & access_val) && (++timeout_counter < 1000));
			if (timeout_counter >= 1000)
				PROC_PRINTF("VP[%02d] txq[%d]                  = register access failed (read) !\n", i, j);

			data_val = rtk_dma_lso_reg_read(DMA_SEC_DMA_LSO_VP_TXQ_PKTCNT_DATA + (i * DMA_SEC_DMA_LSO_VP_STRIDE));
			tx_cnt = data_val & 0xFFFFFF;

			if(tx_cnt)
			{
				PROC_PRINTF("VP[%02d] txq[%d]                  = %d\n", i, j, tx_cnt);
#if 0	// these counters can not be cleared
				// clear VP[i] txq[j] transmit packet counter
				data_val = 0;
				CA_DMA_LSO_REG_WRITE(data_val, DMA_SEC_DMA_LSO_VP_TXQ_PKTCNT_DATA + (i * DMA_SEC_DMA_LSO_VP_STRIDE));

				access_val = 1 << 31;
				access_val |= 1 << 30;	//operation: write
				access_val |= j << 0;	// txq id
				CA_DMA_LSO_REG_WRITE(access_val, DMA_SEC_DMA_LSO_VP_TXQ_PKTCNT_ACCESS + (i * DMA_SEC_DMA_LSO_VP_STRIDE));

				timeout_counter = 0;
				do {
					access_val = CA_DMA_LSO_REG_READ(DMA_SEC_DMA_LSO_VP_TXQ_PKTCNT_ACCESS + (i * DMA_SEC_DMA_LSO_VP_STRIDE));
				} while (((1 << 31) & access_val) && (++timeout_counter < 1000));
				if (timeout_counter >= 1000)
					PROC_PRINTF("VP[%02d] txq[%d]                  = register access failed (write) !\n", i, j);
#endif
			}
		}
	}
	PROC_PRINTF("\n");

	if(showepon) {
		
		PROC_PRINTF("===== "COLOR_Y"PON counter"COLOR_NM" ===== (read-and-clear)\n");
		PROC_PRINTF("PON_MODE = %s\n", "EPON");

		PROC_PRINTF("please cat /proc/ca_rtk_epon/port_stats if needed\n");
	}
	
	return ret;
}
int dump_dropcount_opcode(struct file *file, const char *buffer, unsigned long count, void *data)
{
	ca_status_t ret = CA_E_OK;
	int i =0, opcode = 0;
	aal_l2_qm_eq_cfg_t l2qmeq_cfg0, l2qmeq_cfg1;
	aal_l2_tm_cb_free_buf_cnt_t free_buf_cnt_l2;
	aal_l3_te_cb_free_buf_cnt_t free_buf_cnt_l3;
	aal_l2_tm_cb_comb_threshold_t tecbportthresh_l2;
	aal_l2_tm_cb_threshold_t qmschportthresh_l2;
	aal_l3_te_cb_comb_threshold_t tecbportthresh_l3;
	aal_l3_te_cb_threshold_t qmschportthresh_l3;
    	TE_CB_EQ_PROFILE_THRSH0_t  pool_thrsh;
	ca_uint8_t profileid = 0;
	ca_uint32_t reg_off = 0, value = 0;
	int len = (count > 15) ? 15 : count;

	opcode = _rtk_fc_proc_parsing_string_to_integer(buffer, len);

	// TM/QM page size & buffer size
	// port/queue threshold(H/L) & port current(free) buffer

	if(opcode == 0){
		rtlglue_printf(COLOR_Y"clear voq max buffer count ...\r\n"COLOR_NM);

		for (i = 0; i < CA_AAL_MAX_VOQ_ID; i++) {
			ret = aal_l2_tm_cb_voq_max_buf_set(G3_DEF_DEVID, i, 0);
			ret = aal_l3_te_cb_voq_max_buf_set(G3_DEF_DEVID, i, 0);
		}
	}
	if(opcode & (1<<0)){
		rtlglue_printf(COLOR_Y"0x1: Port Free Buffer Count and High/Low Threshold ..."COLOR_NM"\r\n");

		ret = aal_l2_qm_eq_glb_free_cnt_get(G3_DEF_DEVID, &value);
		rtlglue_printf("L2TM run-time shared free buffer cnt: %d\r\n", value);

		rtlglue_printf("%-9s  %-6s  %-6s  %-11s  %-11s | %-10s  %-6s  %-6s  %-10s  %-11s\r\n",
			"L2TE-Port", "Pool0", "Pool1", "b.p. Thrsh", "drop Thrsh", "L3TE-Port", "Pool0", "Pool1", "b.p. Thrsh", "drop Thrsh");

		for (i = 0; i <= CA_AAL_MAX_PORT_ID; i++) {
			if(i < CA_AAL_L3_TE_NI_PORT_OFFSET) {
				// phy port 0~7
				ret = aal_l2_tm_cb_port_free_buf_cnt_get(G3_DEF_DEVID, i, &free_buf_cnt_l2);	//port_free_buf_cnt counts shred buffers
				ret = aal_l3_te_cb_port_free_buf_cnt_get(G3_DEF_DEVID, i+CA_AAL_L3_TE_NI_PORT_OFFSET, &free_buf_cnt_l3);

				ret = aal_l2_tm_cb_port_profile_sel_get(G3_DEF_DEVID, i, &profileid);
				ret = aal_l2_tm_cb_port_threshold_profile_get(G3_DEF_DEVID, profileid, &tecbportthresh_l2);
				
				ret = aal_l2_tm_cb_deep_q_port_profile_sel_get(G3_DEF_DEVID, i, &profileid);
				ret = aal_l2_tm_cb_deep_q_port_threshold_profile_get(G3_DEF_DEVID, profileid, &qmschportthresh_l2);

				ret = aal_l3_te_cb_port_profile_sel_get(G3_DEF_DEVID, i+CA_AAL_L3_TE_NI_PORT_OFFSET, &profileid);
				ret = aal_l3_te_cb_port_threshold_profile_get(G3_DEF_DEVID, profileid, &tecbportthresh_l3);

				profileid = 0;
				ret = aal_l3_te_cb_dqsch_port_thrsh_profile_sel_get(G3_DEF_DEVID, i, &profileid);				// actually CA didn't implement profile sel, profile index is 0 by default.
				ret = aal_l3_te_cb_dqsch_port_thrsh_profile_get(G3_DEF_DEVID, profileid, &qmschportthresh_l3);	// L3QM only support hi threshold, there is no low threshold in asic design.


				rtlglue_printf("%-9u  %-6u  %-6u  %-4u / %-4u  %-4u / %-4u | %-4u(ni_%u)  %-6u  %-6u  %-10u  %-4u / %-4u \r\n",
					i, free_buf_cnt_l2.cnt0, free_buf_cnt_l2.cnt1, qmschportthresh_l2.threshold_hi, qmschportthresh_l2.threshold_lo, tecbportthresh_l2.threshold_hi, tecbportthresh_l2.threshold_lo,
					i+CA_AAL_L3_TE_NI_PORT_OFFSET, i, free_buf_cnt_l3.cnt0, free_buf_cnt_l3.cnt1, qmschportthresh_l3.threshold_hi, tecbportthresh_l3.threshold_hi, tecbportthresh_l3.threshold_lo);
			}else {
				// others: l2te 8~15 are physical ports;
				//		l3te 0~7 are cpu ports;
				ret = aal_l2_tm_cb_port_free_buf_cnt_get(G3_DEF_DEVID, i, &free_buf_cnt_l2);
				ret = aal_l3_te_cb_port_free_buf_cnt_get(G3_DEF_DEVID, i-CA_AAL_L3_TE_NI_PORT_OFFSET, &free_buf_cnt_l3);

				ret = aal_l2_tm_cb_port_profile_sel_get(G3_DEF_DEVID, i, &profileid);
				ret = aal_l2_tm_cb_port_threshold_profile_get(G3_DEF_DEVID, profileid, &tecbportthresh_l2);
				
				ret = aal_l2_tm_cb_deep_q_port_profile_sel_get(G3_DEF_DEVID, i, &profileid);
				ret = aal_l2_tm_cb_deep_q_port_threshold_profile_get(G3_DEF_DEVID, profileid, &qmschportthresh_l2);

				ret = aal_l3_te_cb_port_profile_sel_get(G3_DEF_DEVID, i-CA_AAL_L3_TE_NI_PORT_OFFSET, &profileid);
				ret = aal_l3_te_cb_port_threshold_profile_get(G3_DEF_DEVID, profileid, &tecbportthresh_l3);

				//profileid = 0;
				//ret = aal_l3_te_cb_dqsch_port_thrsh_profile_sel_get(G3_DEF_DEVID, i, &profileid);				// actually CA didn't implement profile sel, profile index is 0 by default.
				//ret = aal_l3_te_cb_dqsch_port_thrsh_profile_get(G3_DEF_DEVID, profileid, &qmschportthresh_l3);	// L3QM only support hi threshold, there is no low threshold in asic design.

				rtlglue_printf("%-9u  %-6u  %-6u  %-4u / %-4u  %-4u / %-4u | %-3u(cpu_%u)  %-6u  %-6u  %-10s  %-4u / %-4u \r\n",
					i, free_buf_cnt_l2.cnt0, free_buf_cnt_l2.cnt1, qmschportthresh_l2.threshold_hi, qmschportthresh_l2.threshold_lo, tecbportthresh_l2.threshold_hi, tecbportthresh_l2.threshold_lo,
					i-CA_AAL_L3_TE_NI_PORT_OFFSET, i-CA_AAL_L3_TE_NI_PORT_OFFSET, free_buf_cnt_l3.cnt0, free_buf_cnt_l3.cnt1, "N/A", tecbportthresh_l3.threshold_hi, tecbportthresh_l3.threshold_lo);
			}
		}

		rtlglue_printf("-----------------------------------------------------------------------------------\r\n");
	}

	if((opcode & (1<<1)) || (opcode & (1<<2))){
		#define RG_CA_AAL_L3_TE_NI_VOQ_OFFSET ((CA_AAL_MAX_VOQ_ID+1)/2)
		aal_l2_tm_cb_free_buf_cnt_t free_buf_cnt_l2;
		aal_l3_te_cb_free_buf_cnt_t free_buf_cnt_l3;

		if(!(opcode & (1<<2)))
			rtlglue_printf(COLOR_Y"0x2: Voq \"NON-ZERO\" Used Pages and High/Low Threshold ..."COLOR_NM"\r\n");
		else
			rtlglue_printf(COLOR_Y"0x4: Voq \"ALL\"            Used Pages and High/Low Threshold ..."COLOR_NM"\r\n");

		ret = aal_l2_qm_eq_glb_free_cnt_get(G3_DEF_DEVID, &value);
		rtlglue_printf("L2TM run-time shared free buffer cnt: %d\r\n", value);

		rtlglue_printf("%-10s  %-6s  %-6s  %-11s  %-11s | %-10s  %-6s  %-6s  %-10s  %-11s\r\n",
					"L2Port-Voq", "Pool0", "Pool1", "b.p. Thrsh", "drop Thrsh", "L3Port-Voq", "Pool0", "Pool1", "b.p. Thrsh", "drop Thrsh");
		for (i = 0; i <= CA_AAL_MAX_VOQ_ID; i++) {
			if(i < RG_CA_AAL_L3_TE_NI_VOQ_OFFSET) {
				// phy port 0~7
				ret = aal_l2_tm_cb_voq_buf_cnt_get(G3_DEF_DEVID, i, &free_buf_cnt_l2);		// voq_buf_cnt counts shared and private buffers
				ret = aal_l3_te_cb_voq_buf_cnt_get(G3_DEF_DEVID, i+RG_CA_AAL_L3_TE_NI_VOQ_OFFSET, &free_buf_cnt_l3);

				if(!(opcode & (1<<2)) && ((free_buf_cnt_l2.cnt0+free_buf_cnt_l2.cnt1+free_buf_cnt_l3.cnt0+free_buf_cnt_l3.cnt1) == 0))
					continue;

				ret = aal_l2_tm_cb_voq_profile_sel_get(G3_DEF_DEVID, i, &profileid);
				ret = aal_l2_tm_cb_voq_threshold_profile_get(G3_DEF_DEVID, profileid, &tecbportthresh_l2);
				
				ret = aal_l2_tm_cb_deep_q_voq_profile_sel_get(G3_DEF_DEVID, i, &profileid);
				ret = aal_l2_tm_cb_deep_q_voq_threshold_profile_get(G3_DEF_DEVID, profileid, &qmschportthresh_l2);

				ret = aal_l3_te_cb_voq_profile_sel_get(G3_DEF_DEVID, i+RG_CA_AAL_L3_TE_NI_VOQ_OFFSET, &profileid);
				ret = aal_l3_te_cb_voq_threshold_profile_get(G3_DEF_DEVID, profileid, &tecbportthresh_l3);
				
				profileid = 0;
				ret = aal_l3_te_cb_dqsch_voq_thrsh_profile_sel_get(G3_DEF_DEVID, i, &profileid);				// actually CA didn't implement profile sel, profile index is 0 by default.
				ret = aal_l3_te_cb_dqsch_voq_thrsh_profile_get(G3_DEF_DEVID, profileid, &qmschportthresh_l3);	// L3QM only support hi threshold, there is no low threshold in asic design.

				rtlglue_printf("%2u-%-7u  %-6u  %-6u  %-4u / %-4u  %-4u / %-4u | ni_%u-%-5u  %-6u  %-6u  %-10u  %-4u / %-4u \r\n",
					i/8, i%8, free_buf_cnt_l2.cnt0, free_buf_cnt_l2.cnt1, qmschportthresh_l2.threshold_hi, qmschportthresh_l2.threshold_lo, tecbportthresh_l2.threshold_hi, tecbportthresh_l2.threshold_lo,
					i/8, i%8, free_buf_cnt_l3.cnt0, free_buf_cnt_l3.cnt1, qmschportthresh_l3.threshold_lo, tecbportthresh_l3.threshold_hi, tecbportthresh_l3.threshold_lo);
			}else {
				// others: l2te 8~15 are physical ports;
				//		l3te 0~7 are cpu ports;
				ret = aal_l2_tm_cb_voq_buf_cnt_get(G3_DEF_DEVID, i, &free_buf_cnt_l2);
				ret = aal_l3_te_cb_voq_buf_cnt_get(G3_DEF_DEVID, i-RG_CA_AAL_L3_TE_NI_VOQ_OFFSET, &free_buf_cnt_l3);

				if(!(opcode & (1<<2)) && ((free_buf_cnt_l2.cnt0+free_buf_cnt_l2.cnt1+free_buf_cnt_l3.cnt0+free_buf_cnt_l3.cnt1) == 0))
					continue;

				ret = aal_l2_tm_cb_voq_profile_sel_get(G3_DEF_DEVID, i, &profileid);
				ret = aal_l2_tm_cb_voq_threshold_profile_get(G3_DEF_DEVID, profileid, &tecbportthresh_l2);
				
				ret = aal_l2_tm_cb_deep_q_voq_profile_sel_get(G3_DEF_DEVID, i, &profileid);
				ret = aal_l2_tm_cb_deep_q_voq_threshold_profile_get(G3_DEF_DEVID, profileid, &qmschportthresh_l2);

				ret = aal_l3_te_cb_voq_profile_sel_get(G3_DEF_DEVID, i-RG_CA_AAL_L3_TE_NI_VOQ_OFFSET, &profileid);
				ret = aal_l3_te_cb_voq_threshold_profile_get(G3_DEF_DEVID, profileid, &tecbportthresh_l3);
				
				//profileid = 0;
				//ret = aal_l3_te_cb_dqsch_voq_thrsh_profile_sel_get(G3_DEF_DEVID, i, &profileid);				// actually CA didn't implement profile sel, profile index is 0 by default.
				//ret = aal_l3_te_cb_dqsch_voq_thrsh_profile_get(G3_DEF_DEVID, profileid, &qmschportthresh_l3);	// L3QM only support hi threshold, there is no low threshold in asic design.

				rtlglue_printf("%2u-%-7u  %-6u  %-6u  %-4u / %-4u  %-4u / %-4u | cpu_%u-%-4u  %-6u  %-6u  %-10s  %-4u / %-4u \r\n",
					i/8, i%8, free_buf_cnt_l2.cnt0, free_buf_cnt_l2.cnt1, qmschportthresh_l2.threshold_hi, qmschportthresh_l2.threshold_lo, tecbportthresh_l2.threshold_hi, tecbportthresh_l2.threshold_lo,
					(i-RG_CA_AAL_L3_TE_NI_VOQ_OFFSET)/8, i%8, free_buf_cnt_l3.cnt0, free_buf_cnt_l3.cnt1, "N/A", tecbportthresh_l3.threshold_hi, tecbportthresh_l3.threshold_lo);
			}
		}


		rtlglue_printf("-----------------------------------------------------------------------------------\r\n");
	}
	if(opcode & (1<<3)){
		uint32 portidx = 0;
		QM_QM_DEST_PORT0_EQ_CFG_t dest_port_eq_cfg;
		QM_QM_EQ_PROFILE0_t eq_profile;
		QM_QM_CFG1_EQ0_t cfg1_eq0, cfg1_eq1;
		QM_QM_CFG2_EQ0_t cfg2_eq0, cfg2_eq1;

#if defined(CONFIG_ARCH_CORTINA_G3LITE)
		#define L2TM_PAGE_SIZE 32
#else
		#define L2TM_PAGE_SIZE 64
#endif

		rtlglue_printf(COLOR_Y"0x8: L2TM and L3QM page size and numbers ..."COLOR_NM"\r\n");

		ret = aal_l2_qm_eq_cfg_get(G3_DEF_DEVID, 0, &l2qmeq_cfg0);
		if(l2qmeq_cfg0.eq_ena == FALSE) {
			rtlglue_printf("L2TM Pool0 is Disabled!\r\n");
		}else {

			rtlglue_printf("L2TM Pool0 total buffer %d (priv num %d + shared num %d), %d Bytes per 1 Buffer\r\n\n",
				l2qmeq_cfg0.buff_num * 1024, l2qmeq_cfg0.prvt_buff_num * 16, l2qmeq_cfg0.buff_num * 1024 - l2qmeq_cfg0.prvt_buff_num * 16, L2TM_PAGE_SIZE);
		}

		ret = aal_l2_qm_eq_cfg_get(G3_DEF_DEVID, 1, &l2qmeq_cfg1);
		if(l2qmeq_cfg1.eq_ena == FALSE) {
			rtlglue_printf("L2TM Pool1 is Disabled!\r\n\n");
		}else {

			rtlglue_printf("L2TM Pool1 total buffer %d (priv cnt %d + shared cnt %d), %d Bytes per 1 Buffer\r\n\n",
				l2qmeq_cfg1.buff_num * 1024, l2qmeq_cfg1.prvt_buff_num * 16, l2qmeq_cfg1.buff_num * 1024 - l2qmeq_cfg1.prvt_buff_num * 16, L2TM_PAGE_SIZE);
		}

		rtlglue_printf("%-10s  %-10s  %-10s  %-10s  %-10s  %-10s  %-10s  %-10s  %-11s\r\n",
			"L3TE-Port", "EQ_Profile", "Pool0_ID", "Pool1_ID", "Pool0_Num", "Pool0_Size", "Pool1_Num", "Pool1_Size", "Profile_Thrsh");

		for (i = 0; i <= CA_AAL_MAX_PORT_ID; i++) {

			if(i < CA_AAL_L3_TE_NI_PORT_OFFSET)
				portidx = i + CA_AAL_L3_TE_NI_PORT_OFFSET;
			else
				portidx = i - CA_AAL_L3_TE_NI_PORT_OFFSET;

			reg_off = QM_QM_DEST_PORT0_EQ_CFG + (QM_QM_DEST_PORT0_EQ_CFG_STRIDE * portidx);
			dest_port_eq_cfg.wrd = rtk_ne_reg_read(reg_off);

			reg_off = QM_QM_EQ_PROFILE0 + (QM_QM_EQ_PROFILE0_STRIDE * dest_port_eq_cfg.bf.profile_sel);
			eq_profile.wrd = rtk_ne_reg_read(reg_off);

			reg_off = QM_QM_CFG1_EQ0 + (QM_QM_CFG1_EQ0_STRIDE * eq_profile.bf.eqp0);
			cfg1_eq0.wrd = rtk_ne_reg_read(reg_off);

			reg_off = QM_QM_CFG1_EQ0 + (QM_QM_CFG1_EQ0_STRIDE * eq_profile.bf.eqp1);
			cfg1_eq1.wrd = rtk_ne_reg_read(reg_off);

			reg_off = QM_QM_CFG2_EQ0 + (QM_QM_CFG2_EQ0_STRIDE * eq_profile.bf.eqp0);
			cfg2_eq0.wrd = rtk_ne_reg_read(reg_off);

			reg_off = QM_QM_CFG2_EQ0 + (QM_QM_CFG2_EQ0_STRIDE * eq_profile.bf.eqp1);
			cfg2_eq1.wrd = rtk_ne_reg_read(reg_off);

			
            		// refer to: aal_l3_te_cb_pool_profile_get // rule 0: chck th0 only
			reg_off = TE_CB_EQ_PROFILE_THRSH0 + (TE_CB_EQ_PROFILE_THRSH0_STRIDE * dest_port_eq_cfg.bf.profile_sel);
			pool_thrsh.wrd  = rtk_ne_reg_read(reg_off);

				rtlglue_printf("%-4u%-3s(%u)  %-10u  %-10u  %-10u  %-10u  %-10u  %-10u  %-10u  %-4u \r\n",
					portidx,  portidx<CA_AAL_L3_TE_NI_PORT_OFFSET?"cpu":"ni", portidx%8, dest_port_eq_cfg.bf.profile_sel, eq_profile.bf.eqp0, eq_profile.bf.eqp1,
					cfg1_eq0.bf.total_buffer_num, 64<<cfg2_eq0.bf.buffer_size, cfg1_eq1.bf.total_buffer_num, 64<<cfg2_eq1.bf.buffer_size, pool_thrsh.bf.th0);

		}

		rtlglue_printf("-----------------------------------------------------------------------------------\r\n");
	}


	if(opcode & (1<<4)){
		extern ca_uint32_t aal_l3qm_dump_monitor_eq(ca_device_id_t device_id);
		
		rtlglue_printf(COLOR_Y"0x10: QM active and inactive counter ..."COLOR_NM"\r\n");
		
		aal_l3qm_dump_monitor_eq(CA_DEF_DEVID);
		
		rtlglue_printf("-----------------------------------------------------------------------------------\r\n");
	}

	if(opcode & (1<<5)){
		ca_uint32_t regvalue;

		rtlglue_printf(COLOR_Y"0x20: NE module ready signals ..."COLOR_NM"\r\n");
		rtlglue_printf("==================== %s ====================\n", COLOR_Y"NI"COLOR_NM);
		{
			unsigned char toTmRxFifoState = 0, rdy_Tm2NiL3FeQm = 0, rdy_nirx2Qm = 0, rdy_nirx2L3fe = 0, toL2FeRxFifoState = 0;
			unsigned char rdy_niQm2L3fe = 0 , rdy_niWan2L3fe = 0, rdy_niRxrmu2L3fe = 0;
			unsigned char rdy_l3egrMrr_niQm2L3fe = 0, rdy_l3egrMrr_niWan2L3fe = 0, rdy_l3egrMrr_niL2Fe2L3fe = 0;
			unsigned char rdy_ni2TmPort[16] = {0};
			char temp_str[10];
			int idx;
			uint32 bit_pos;

			regvalue = rtk_ne_reg_read(NI_HV_GLB_NIRX_L3QM_STS);
			rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "NI_HV_GLB_NIRX_L3QM_STS", regvalue);
			bit_pos = 2;
			toTmRxFifoState = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, to TM RXFIFO status, L3FE to TM and L3QM to TM share the same FIFO\n", bit_pos, toTmRxFifoState);
			rtlglue_printf("\t       (FIFO usage %s NI_HV_GLB_RXFIFO_THRESHOLD_MISC_CFG.l3qm_rxfifo_hi[6:0])\n", toTmRxFifoState?"<=":">");
			rtlglue_printf("\t                           %12s -----%c-----> %s\n", "L3FE", toTmRxFifoState?'O':'X', "TM");
			rtlglue_printf("\t                           %12s -----%c-----> %s\n", "QM", toTmRxFifoState?'O':'X', "TM");
			bit_pos = 16;
			rdy_nirx2Qm = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_nirx2Qm, "QM", rdy_nirx2Qm?'O':'X', "NIRX");
			bit_pos = 17;
			rdy_Tm2NiL3FeQm = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_Tm2NiL3FeQm, "NI(L3FE/QM)", rdy_Tm2NiL3FeQm?'O':'X', "TM");

			regvalue = rtk_ne_reg_read(NI_HV_GLB_NITX_STS);
			rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "NI_HV_GLB_NITX_STS", regvalue);
			for(idx = 0 ; i < 16 ; i++)
			{
				bit_pos = i;
				rdy_ni2TmPort[i] = (regvalue>>bit_pos)&0x1;
				snprintf(temp_str, 10, "TM_Port%d", i);
				rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_ni2TmPort[i], temp_str, rdy_ni2TmPort[i]?'O':'X', "NI");
			}

			regvalue = rtk_ne_reg_read(NI_HV_GLB_NIRX_L3FE_STS);
			rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "NI_HV_GLB_NIRX_L3FE_STS", regvalue);
			bit_pos = 4;
			rdy_l3egrMrr_niQm2L3fe = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\t(for L3FE egr mirror packet)\n", bit_pos, rdy_l3egrMrr_niQm2L3fe, "L3FE", rdy_l3egrMrr_niQm2L3fe?'O':'X', "NI(QM)");
			bit_pos = 5;
			rdy_l3egrMrr_niWan2L3fe = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\t(for L3FE egr mirror packet)\n", bit_pos, rdy_l3egrMrr_niWan2L3fe, "L3FE", rdy_l3egrMrr_niWan2L3fe?'O':'X', "NI(WAN)");
			bit_pos = 6;
			rdy_l3egrMrr_niL2Fe2L3fe = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\t(for L3FE egr mirror packet)\n", bit_pos, rdy_l3egrMrr_niL2Fe2L3fe, "L3FE", rdy_l3egrMrr_niL2Fe2L3fe?'O':'X', "NI(L2FE)");
			bit_pos = 7;
			rdy_nirx2L3fe = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_nirx2L3fe, "L3FE", rdy_nirx2L3fe?'O':'X', "NIRX");
			bit_pos = 10;
			rdy_niQm2L3fe = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_niQm2L3fe, "L3FE", rdy_niQm2L3fe?'O':'X', "NI(QM)");
			bit_pos = 11;
			rdy_niWan2L3fe = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_niWan2L3fe, "L3FE", rdy_niWan2L3fe?'O':'X', "NI(WAN)");
			bit_pos = 12;
			rdy_niRxrmu2L3fe = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> NIRX (FIFO) ---> RXMUX(SRAM) ---> L2FE\n", bit_pos, rdy_niRxrmu2L3fe, "L3FE", rdy_niRxrmu2L3fe?'O':'X');
			bit_pos = 28;
			toL2FeRxFifoState = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, L3FE to RXMUX/L2FE FIFO status\n", bit_pos, toL2FeRxFifoState);
			rtlglue_printf("\t       (FIFO usage %s NI_HV_GLB_RXFIFO_THRESHOLD_MISC_CFG.l3fe_rxfifo_hi[6:0])\n", toL2FeRxFifoState?"<=":">");
		}

		rtlglue_printf("==================== %s ====================\n", COLOR_Y"L2FE"COLOR_NM);
		{
			unsigned char rdy_l2fe2Ni = 0;
			uint32 bit_pos;
			regvalue = rtk_ne_reg_read(L2FE_PP_NI_INTF_STS);
			rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "L2FE_PP_NI_INTF_STS", regvalue);
			bit_pos = 0;
			rdy_l2fe2Ni = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_l2fe2Ni, "NI", rdy_l2fe2Ni?'O':'X', "L2FE");
		}

		rtlglue_printf("==================== %s ====================\n", COLOR_Y"L2TM"COLOR_NM);
		{
			unsigned char rdy_l2tm2L2fe = 0;
			uint32 bit_pos;
			regvalue = rtk_ne_reg_read(L2TM_L2TM_BM_STS);
			rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "L2TM_L2TM_BM_STS", regvalue);
			bit_pos = 1;
			rdy_l2tm2L2fe = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_l2tm2L2fe, "L2FE", rdy_l2tm2L2fe?'O':'X', "L2TM");
		}

		rtlglue_printf("==================== %s ====================\n", COLOR_Y"L3QM"COLOR_NM);
		{
			unsigned char rdy_qm2Nitx = 0, rdy_nirx2Qm = 0, /*rdy_rmuAxiW2Qm = 0, */rdy_rmuAxiR2Qm = 0;
			unsigned char vld_qmNirx = 0, vld_nitxQm = 0;
			uint32 bit_pos;
			regvalue = rtk_ne_reg_read(QM_QM_PHY_PORT_STS);
			rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "QM_QM_PHY_PORT_STS", regvalue);
			rtlglue_printf("\t-[%2d:%2d]: 0x%02x, nirx_qm_port_rdy[7:0] \"qm_rmu\" module FSM status\n", 7, 0, (regvalue>>0)&0xff);
			rtlglue_printf("\t-[%2d:%2d]: 0x%02x, te_qm_es_ni_ok[7:0]   \"qm_rmu\" module FSM status\n", 15, 8, (regvalue>>8)&0xff);
			rtlglue_printf("\t-[%2d:%2d]: 0x%02x, te_qm_es_cpu_ok[7:0]  \"qm_rmu\" module FSM status\n", 23, 16, (regvalue>>16)&0xff);

			bit_pos = 24;
			rdy_qm2Nitx = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_qm2Nitx, "NITX", rdy_qm2Nitx?'O':'X', "QM");
			bit_pos = 25;
			vld_nitxQm = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, valid signal     %12s -----%s--> %s\n", bit_pos, vld_nitxQm, "NITX", vld_nitxQm?"vld1":"vld0", "QM");
			bit_pos = 26;
			rdy_nirx2Qm = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_nirx2Qm, "QM", rdy_nirx2Qm?'O':'X', "NIRX");
			bit_pos = 27;
			vld_qmNirx = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, valid signal     %12s -----%s--> %s\n", bit_pos, vld_qmNirx, "QM", vld_qmNirx?"vld1":"vld0", "NIRX");
#if 0 // not ready signal, no need to show
			bit_pos = 28;
			rdy_rmuAxiW2Qm = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, if data trans    %12s -----%s-> %s\n", bit_pos, rdy_rmuAxiW2Qm, "QM", rdy_rmuAxiW2Qm?"data-":"empty", "RMU-AXI-W");
#endif
			bit_pos = 29;
			rdy_rmuAxiR2Qm = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s (%s)\n", bit_pos, rdy_rmuAxiR2Qm, "RMU-AXI-R", rdy_rmuAxiR2Qm?'O':'X', "QM", rdy_rmuAxiR2Qm?"there is space in QM fifo to receive data":"there is NO space in QM fifo to receive data");
		}
#if 0	// L3FE dedug is support in 8277C only
		rtlglue_printf("==================== %s ====================\n", COLOR_Y"L3FE"COLOR_NM);
		{
			unsigned char rdy_nirx2L3fe = 0, rdy_l3fe2Nitx = 0;
			uint32 bit_pos;
			rtk_ne_reg_write(0x000001E0, L3FE_GLB_DBG_IDX);
			rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (WRITE)\n", "L3FE_GLB_DBG_IDX", 0x000001E0);
			regvalue = rtk_ne_reg_read(L3FE_GLB_DBG_DAT);
			rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "L3FE_GLB_DBG_DAT", regvalue);
			bit_pos = 14;
			rdy_l3fe2Nitx = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_l3fe2Nitx, "NITX", rdy_l3fe2Nitx?'O':'X', "L3FE");
			bit_pos = 30;
			rdy_nirx2L3fe = (regvalue>>bit_pos)&0x1;
			rtlglue_printf("\t-[%02d]: %u, ready signal for %12s -----%c-----> %s\n", bit_pos, rdy_nirx2L3fe, "L3FE", rdy_nirx2L3fe?'O':'X', "NIRX");
		}
#endif
		rtlglue_printf("==================== %s ====================\n", COLOR_Y"DMALSO"COLOR_NM);
		rtlglue_printf("\tThe following registers show the FSM status in DMALSO (0: normal)\n");
		regvalue = rtk_dma_lso_reg_read(DMA_SEC_DMA_GLB_DMA_LSO_DEBUG0);
		rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "DMA_SEC_DMA_GLB_DMA_LSO_DEBUG0", regvalue);
		regvalue = rtk_dma_lso_reg_read(DMA_SEC_DMA_GLB_DMA_LSO_DEBUG1);
		rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "DMA_SEC_DMA_GLB_DMA_LSO_DEBUG1", regvalue);
		regvalue = rtk_dma_lso_reg_read(DMA_SEC_DMA_GLB_DMA_LSO_DEBUG2);
		rtlglue_printf("\t%-30s: \033[1;37;41m0x%08x\033[0m (READ)\n", "DMA_SEC_DMA_GLB_DMA_LSO_DEBUG2", regvalue);

		rtlglue_printf("-----------------------------------------------------------------------------------\r\n");
	}

	return count;
}
#endif

int dump_ponStreamid_table(struct seq_file *s, void *v)
{
#if defined(CONFIG_RTK_L34_G3_PLATFORM) && !defined(CONFIG_FC_G3_G3LITE_SERIES)
	int i = 0, voqid = 0;

	for(i=0; i<RTK_FC_TABLESIZE_STREAMID; i++) {
		if(fc_db.streamidTbl[i].valid == TRUE) {
			voqid = (fc_db.streamidTbl[i].ldpid - ASIC_LPORT_PON_US_0) * 8 + fc_db.streamidTbl[i].cos;
			PROC_PRINTF("sid[%3d] ldpid(tcont): 0x%x cos: %d flowid(gemid): %-3d flowRefCnt: %d\n", 
				i, fc_db.streamidTbl[i].ldpid, fc_db.streamidTbl[i].cos, fc_db.streamidTbl[i].gemid, fc_db.ponVoqRefTbl[voqid]);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			{
				aal_ni_dma_lso_stream_id_tbl_cfg_t stream_tbl_cfg;
				aal_ni_dma_lso_get_streamid_tbl(i, &stream_tbl_cfg);
				if((fc_db.streamidTbl[i].valid != stream_tbl_cfg.en_flag) || (fc_db.streamidTbl[i].ldpid != stream_tbl_cfg.ldpid) 
					|| (fc_db.streamidTbl[i].cos != stream_tbl_cfg.cos) || (fc_db.streamidTbl[i].gemid != stream_tbl_cfg.pol_id))
					PROC_PRINTF("hw_sid[%3d] ldpid(tcont): 0x%x cos: %d flowid(gemid): %d deep_q: %ds\n", i, stream_tbl_cfg.ldpid, stream_tbl_cfg.cos, stream_tbl_cfg.pol_id, stream_tbl_cfg.deep_q);
			}
#endif
		}
	}
	
	PROC_PRINTF("PON currect used voq cnt = %d\n", fc_db.ponVoqCurUsedCnt);
#endif

	return SUCCESS;
}


#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int32 dump_dmaAftAction_table(struct seq_file *s, void *v)
{
	int i;
	rtk_rg_asic_dmaAftFib_t fib;
	rtk_rg_asic_dmaAftTpid_t aftTpid;

	PROC_PRINTF(">>ASIC DMA After TPID conf:\n");
	rtk_rg_asic_dmaAftTpid_get(&aftTpid);
	PROC_PRINTF("\tTPID_0: 0x%x\n", aftTpid.tpid_0);
	PROC_PRINTF("\tTPID_1: 0x%x\n", aftTpid.tpid_1);
	PROC_PRINTF("\tTPID_2: 0x%x\n", aftTpid.tpid_2);
	PROC_PRINTF("\tTPID_3: 0x%x\n", aftTpid.tpid_3);
	PROC_PRINTF(">>ASIC DMA LSO not support tpid enc %d", (fc_db.systemGlobal.stagTPID_tx_not_sup_enc)?(fc_db.systemGlobal.stagTPID_tx_not_sup_enc):(-1));

	PROC_PRINTF(">>ASIC DMA After Action Table:\n");
	for(i = 0 ; i < RTK_FC_TABLESIZE_DMAAFTACTION ; i++)
	{
		rtk_rg_asic_dmaAftFib_get(i, &fib);
		PROC_PRINTF("--- index: %d ---\n", i);
		PROC_PRINTF("\t[VLAN]\n");
		PROC_PRINTF("\tvlan_vld: %d\t\t(0: VLAN stacking operation mode, 1: VLAN set mode)\n", fib.vlan_vld);
		PROC_PRINTF("\tvlan_cnt: %d\t\t(redefined as TOP_VLAN_CMD[1:0] under VLAN stacking operation mode\n", fib.vlan_cnt);
		PROC_PRINTF("\tinner_vlan_cmd: %d\t(Undefined in VLAN set mode)\n", fib.inner_vlan_cmd);

		if(fib.top_tpid_enc && (fib.top_tpid_enc < 5))
			PROC_PRINTF("\ttop_tpid_enc: %d\t\t(TPID_%d)\n", fib.top_tpid_enc, fib.top_tpid_enc-1);
		else
			PROC_PRINTF("\ttop_tpid_enc: %d\t\t(0: any other value or no vlan, others: reserved)\n", fib.top_tpid_enc);
		
		PROC_PRINTF("\ttop_tpid_sel: %d\t\t(0: no-op, 1: top_tpid_enc, 2: inner_tpid_enc, 3: fib.top_tpid_enc)\n", fib.top_tpid_sel);

		PROC_PRINTF("\ttop_vid: %d\n", fib.top_vid);
		PROC_PRINTF("\ttop_dei: %d\n", fib.top_dei);
		PROC_PRINTF("\ttop_dei_sel: %d\t\t(0: no-op, 1: top_dei, 2: inner_dei, 3: fib.top_dei)\n", fib.top_dei_sel);
		PROC_PRINTF("\ttop_802_1p: %d\n", fib.top_802_1p);
		PROC_PRINTF("\ttop_1p_sel: %d\t\t(0: no-op, 1: top_802_1p, 2: inner_802_1p, 3: fib.top_802_1p)\n", fib.top_1p_sel);

		if(fib.inner_tpid_enc && (fib.inner_tpid_enc < 5))
			PROC_PRINTF("\tinner_tpid_enc: %d\t(TPID_%d)\n", fib.inner_tpid_enc, fib.inner_tpid_enc-1);
		else
			PROC_PRINTF("\tinner_tpid_enc: %d\t(0: any other value or no vlan, others: reserved)\n", fib.inner_tpid_enc);

		PROC_PRINTF("\tinner_tpid_sel: %d\t(0: no-op, 1: inner_tpid_enc, 2: top_tpid_enc, 3: fib.inner_tpid_enc)\n", fib.inner_tpid_sel);
		PROC_PRINTF("\tinner_vid: %d\n", fib.inner_vid);

		PROC_PRINTF("\tinner_dei: %d\n", fib.inner_dei);
		PROC_PRINTF("\tinner_dei_sel: %d\t(0: no-op, 1: inner_dei, 2: top_dei, 3: fib.inner_dei)\n", fib.inner_dei_sel);
		PROC_PRINTF("\tinner_802_1p: %d\n", fib.inner_802_1p);
		PROC_PRINTF("\tinner_1p_sel: %d\t\t(0: no-op, 1: inner_802_1p, 2: top_802_1p, 3: fib.inner_802_1p)\n", fib.inner_1p_sel);
		
		PROC_PRINTF("\t[PPPoE]\n");
		PROC_PRINTF("\tpppoe_cmd: %d\t\t(0: no op, 1: push, 2: pop, 3 reserved)\n", fib.pppoe_cmd);
		PROC_PRINTF("\tpppoe_session_id: %d\n", fib.session_id);
	}		
		
	PROC_PRINTF("----------------------------------------------\n");
	return SUCCESS;
}

int32 dump_dmaAftAction_table_by_idx(struct file *file, const char *buffer, unsigned long count, void *data)
{
	rtk_rg_asic_dmaAftFib_t fib;
	rtk_rg_asic_dmaAftTpid_t aftTpid;

	int idx = _rtk_fc_proc_parsing_string_to_integer(buffer, count);

	rtlglue_printf(">>ASIC DMA After TPID conf:\n");
	rtk_rg_asic_dmaAftTpid_get(&aftTpid);
	rtlglue_printf("\tTPID_0: 0x%x\n", aftTpid.tpid_0);
	rtlglue_printf("\tTPID_1: 0x%x\n", aftTpid.tpid_1);
	rtlglue_printf("\tTPID_2: 0x%x\n", aftTpid.tpid_2);
	rtlglue_printf("\tTPID_3: 0x%x\n", aftTpid.tpid_3);

	rtlglue_printf(">>ASIC DMA After Action Table:\n");

	rtk_rg_asic_dmaAftFib_get(idx, &fib);
	rtlglue_printf("--- index: %d ---\n", idx);
	rtlglue_printf("\t[VLAN]\n");
	rtlglue_printf("\tvlan_vld: %d\t\t(0: VLAN stacking operation mode, 1: VLAN set mode)\n", fib.vlan_vld);
	rtlglue_printf("\tvlan_cnt: %d\t\t(redefined as TOP_VLAN_CMD[1:0] under VLAN stacking operation mode\n", fib.vlan_cnt);
	rtlglue_printf("\tinner_vlan_cmd: %d\t(Undefined in VLAN set mode)\n", fib.inner_vlan_cmd);

	if(fib.top_tpid_enc && (fib.top_tpid_enc < 5))
		rtlglue_printf("\ttop_tpid_enc: %d\t\t(TPID_%d)\n", fib.top_tpid_enc, fib.top_tpid_enc-1);
	else
		rtlglue_printf("\ttop_tpid_enc: %d\t\t(0: any other value or no vlan, others: reserved)\n", fib.top_tpid_enc);

	rtlglue_printf("\ttop_tpid_sel: %d\t\t(0: no-op, 1: top_tpid_enc, 2: inner_tpid_enc, 3: fib.top_tpid_enc)\n", fib.top_tpid_sel);

	rtlglue_printf("\ttop_vid: %d\n", fib.top_vid);
	rtlglue_printf("\ttop_dei: %d\n", fib.top_dei);
	rtlglue_printf("\ttop_dei_sel: %d\t\t(0: no-op, 1: top_dei, 2: inner_dei, 3: fib.top_dei)\n", fib.top_dei_sel);
	rtlglue_printf("\ttop_802_1p: %d\n", fib.top_802_1p);
	rtlglue_printf("\ttop_1p_sel: %d\t\t(0: no-op, 1: top_802_1p, 2: inner_802_1p, 3: fib.top_802_1p)\n", fib.top_1p_sel);

	if(fib.inner_tpid_enc && (fib.inner_tpid_enc < 5))
		rtlglue_printf("\tinner_tpid_enc: %d\t(TPID_%d)\n", fib.inner_tpid_enc, fib.inner_tpid_enc-1);
	else
		rtlglue_printf("\tinner_tpid_enc: %d\t(0: any other value or no vlan, others: reserved)\n", fib.inner_tpid_enc);

	rtlglue_printf("\tinner_tpid_sel: %d\t(0: no-op, 1: inner_tpid_enc, 2: top_tpid_enc, 3: fib.inner_tpid_enc)\n", fib.inner_tpid_sel);
	rtlglue_printf("\tinner_vid: %d\n", fib.inner_vid);

	rtlglue_printf("\tinner_dei: %d\n", fib.inner_dei);
	rtlglue_printf("\tinner_dei_sel: %d\t(0: no-op, 1: inner_dei, 2: top_dei, 3: fib.inner_dei)\n", fib.inner_dei_sel);
	rtlglue_printf("\tinner_802_1p: %d\n", fib.inner_802_1p);
	rtlglue_printf("\tinner_1p_sel: %d\t\t(0: no-op, 1: inner_802_1p, 2: top_802_1p, 3: fib.inner_802_1p)\n", fib.inner_1p_sel);

	rtlglue_printf("\t[PPPoE]\n");
	rtlglue_printf("\tpppoe_cmd: %d\t\t(0: no op, 1: push, 2: pop, 3 reserved)\n", fib.pppoe_cmd);
	rtlglue_printf("\tpppoe_session_id: %d\n", fib.session_id);

	rtlglue_printf("----------------------------------------------\n");
	return count;
}


int32 dump_sw_dmaAftAction_table(struct seq_file *s, void *v)
{
	int i;
#if defined(CONFIG_FC_RTL9607F_SERIES)
	int loop_idx;
#endif
	PROC_PRINTF(">>DMA After Action Table: show valid (ref_cont>0) enrties only\n");

	for(i = 0 ; i < RTK_FC_TABLESIZE_DMAAFTACTION ; i++)
	{
		rtk_rg_asic_dmaAftFib_t *pFib = &fc_db.dmaAftActionTbl[i].fib;
		if(fc_db.dmaAftActionTbl[i].refCount)
		{
			PROC_PRINTF("--- index: %d ---\n", i);
			PROC_PRINTF("\tref_count: %d\n", fc_db.dmaAftActionTbl[i].refCount);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			PROC_PRINTF("\trsyncRsvEntry: %d, syncRsvEntryIdx: %d\n", fc_db.dmaAftActionTbl[i].syncRsvEntry, fc_db.dmaAftActionTbl[i].syncRsvEntryIdx);
#if defined(CONFIG_FC_RTL9607F_SERIES)
			PROC_PRINTF("\treference by DMAAFT MAP table idx: ");
			for(loop_idx = 0 ; loop_idx < RTK_FC_TABLESIZE_DMAAFTMAP ; loop_idx++)
			{
				if(fc_db.dmaAftMapTbl[loop_idx].refCount && fc_db.dmaAftMapTbl[loop_idx].aft_en && fc_db.dmaAftMapTbl[loop_idx].aft_fib_idx)
					PROC_PRINTF("%d(lspid 0x%x), ", loop_idx, fc_db.dmaAftMapTbl[loop_idx].lspid);
			}
			PROC_PRINTF("\n");
#endif
#endif
			PROC_PRINTF("\t[VLAN]\n");
			PROC_PRINTF("\tvlan_vld: %d\t\t(0: VLAN stacking operation mode, 1: VLAN set mode)\n", pFib->vlan_vld);
			PROC_PRINTF("\tvlan_cnt: %d\t\t(redefined as TOP_VLAN_CMD[1:0] under VLAN stacking operation mode\n", pFib->vlan_cnt);
			PROC_PRINTF("\tinner_vlan_cmd: %d\t(Undefined in VLAN set mode)\n", pFib->inner_vlan_cmd);

			if(pFib->top_tpid_enc && (pFib->top_tpid_enc < 5))
				PROC_PRINTF("\ttop_tpid_enc: %d\t\t(TPID_%d)\n", pFib->top_tpid_enc, pFib->top_tpid_enc-1);
			else
				PROC_PRINTF("\ttop_tpid_enc: %d\t\t(0: any other value or no vlan, others: reserved)\n", pFib->top_tpid_enc);

			PROC_PRINTF("\ttop_tpid_sel: %d\t\t(0: no-op, 1: top_tpid_enc, 2: inner_tpid_enc, 3: fib.top_tpid_enc)\n", pFib->top_tpid_sel);

			PROC_PRINTF("\ttop_vid: %d\n", pFib->top_vid);
			PROC_PRINTF("\ttop_dei: %d\n", pFib->top_dei);
			PROC_PRINTF("\ttop_dei_sel: %d\t\t(0: no-op, 1: top_dei, 2: inner_dei, 3: fib.top_dei)\n", pFib->top_dei_sel);
			PROC_PRINTF("\ttop_802_1p: %d\n", pFib->top_802_1p);
			PROC_PRINTF("\ttop_1p_sel: %d\t\t(0: no-op, 1: top_802_1p, 2: inner_802_1p, 3: fib.top_802_1p)\n", pFib->top_1p_sel);

			if(pFib->inner_tpid_enc && (pFib->inner_tpid_enc < 5))
				PROC_PRINTF("\tinner_tpid_enc: %d\t(TPID_%d)\n", pFib->inner_tpid_enc, pFib->inner_tpid_enc-1);
			else
				PROC_PRINTF("\tinner_tpid_enc: %d\t(0: any other value or no vlan, others: reserved)\n", pFib->inner_tpid_enc);

			PROC_PRINTF("\tinner_tpid_sel: %d\t(0: no-op, 1: inner_tpid_enc, 2: top_tpid_enc, 3: fib.inner_tpid_enc)\n", pFib->inner_tpid_sel);
			PROC_PRINTF("\tinner_vid: %d\n", pFib->inner_vid);

			PROC_PRINTF("\tinner_dei: %d\n", pFib->inner_dei);
			PROC_PRINTF("\tinner_dei_sel: %d\t(0: no-op, 1: inner_dei, 2: top_dei, 3: fib.inner_dei)\n", pFib->inner_dei_sel);
			PROC_PRINTF("\tinner_802_1p: %d\n", pFib->inner_802_1p);
			PROC_PRINTF("\tinner_1p_sel: %d\t\t(0: no-op, 1: inner_802_1p, 2: top_802_1p, 3: fib.inner_802_1p)\n", pFib->inner_1p_sel);

			PROC_PRINTF("\t[PPPoE]\n");
			PROC_PRINTF("\tpppoe_cmd: %d\t\t(0: no op, 1: push, 2: pop, 3 reserved)\n", pFib->pppoe_cmd);
			PROC_PRINTF("\tpppoe_session_id: %d\n", pFib->session_id);
		}
	}
	PROC_PRINTF("----------------------------------------------\n");
	return SUCCESS;
}

int32 dump_sw_dmaAftAction_table_by_idx(struct file *file, const char *buffer, unsigned long count, void *data)
{
	int idx = _rtk_fc_proc_parsing_string_to_integer(buffer, count);
	rtk_rg_asic_dmaAftFib_t *pFib = &fc_db.dmaAftActionTbl[idx].fib;

	rtlglue_printf(">>DMA After Action Table: \n");

	rtlglue_printf("--- index: %d ---\n", idx);
	rtlglue_printf("\tref_count: %d\n", fc_db.dmaAftActionTbl[idx].refCount);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	rtlglue_printf("\trsyncRsvEntry: %d, syncRsvEntryIdx: %d\n", fc_db.dmaAftActionTbl[idx].syncRsvEntry, fc_db.dmaAftActionTbl[idx].syncRsvEntryIdx);
#endif
	rtlglue_printf("\t[VLAN]\n");
	rtlglue_printf("\tvlan_vld: %d\t\t(0: VLAN stacking operation mode, 1: VLAN set mode)\n", pFib->vlan_vld);
	rtlglue_printf("\tvlan_cnt: %d\t\t(redefined as TOP_VLAN_CMD[1:0] under VLAN stacking operation mode\n", pFib->vlan_cnt);
	rtlglue_printf("\tinner_vlan_cmd: %d\t(Undefined in VLAN set mode)\n", pFib->inner_vlan_cmd);

	if(pFib->top_tpid_enc && (pFib->top_tpid_enc < 5))
		rtlglue_printf("\ttop_tpid_enc: %d\t\t(TPID_%d)\n", pFib->top_tpid_enc, pFib->top_tpid_enc-1);
	else
		rtlglue_printf("\ttop_tpid_enc: %d\t\t(0: any other value or no vlan, others: reserved)\n", pFib->top_tpid_enc);

	rtlglue_printf("\ttop_tpid_sel: %d\t\t(0: no-op, 1: top_tpid_enc, 2: inner_tpid_enc, 3: fib.top_tpid_enc)\n", pFib->top_tpid_sel);

	rtlglue_printf("\ttop_vid: %d\n", pFib->top_vid);
	rtlglue_printf("\ttop_dei: %d\n", pFib->top_dei);
	rtlglue_printf("\ttop_dei_sel: %d\t\t(0: no-op, 1: top_dei, 2: inner_dei, 3: fib.top_dei)\n", pFib->top_dei_sel);
	rtlglue_printf("\ttop_802_1p: %d\n", pFib->top_802_1p);
	rtlglue_printf("\ttop_1p_sel: %d\t\t(0: no-op, 1: top_802_1p, 2: inner_802_1p, 3: fib.top_802_1p)\n", pFib->top_1p_sel);

	if(pFib->inner_tpid_enc && (pFib->inner_tpid_enc < 5))
		rtlglue_printf("\tinner_tpid_enc: %d\t(TPID_%d)\n", pFib->inner_tpid_enc, pFib->inner_tpid_enc-1);
	else
		rtlglue_printf("\tinner_tpid_enc: %d\t(0: any other value or no vlan, others: reserved)\n", pFib->inner_tpid_enc);

	rtlglue_printf("\tinner_tpid_sel: %d\t(0: no-op, 1: inner_tpid_enc, 2: top_tpid_enc, 3: fib.inner_tpid_enc)\n", pFib->inner_tpid_sel);
	rtlglue_printf("\tinner_vid: %d\n", pFib->inner_vid);

	rtlglue_printf("\tinner_dei: %d\n", pFib->inner_dei);
	rtlglue_printf("\tinner_dei_sel: %d\t(0: no-op, 1: inner_dei, 2: top_dei, 3: fib.inner_dei)\n", pFib->inner_dei_sel);
	rtlglue_printf("\tinner_802_1p: %d\n", pFib->inner_802_1p);
	rtlglue_printf("\tinner_1p_sel: %d\t\t(0: no-op, 1: inner_802_1p, 2: top_802_1p, 3: fib.inner_802_1p)\n", pFib->inner_1p_sel);

	rtlglue_printf("\t[PPPoE]\n");
	rtlglue_printf("\tpppoe_cmd: %d\t\t(0: no op, 1: push, 2: pop, 3 reserved)\n", pFib->pppoe_cmd);
	rtlglue_printf("\tpppoe_session_id: %d\n", pFib->session_id);

	rtlglue_printf("----------------------------------------------\n");
	return count;
}

#if defined(CONFIG_FC_RTL9607F_SERIES)
int32 dump_dmaAftMap_table(struct seq_file *s, void *v)
{
	int i;
	aal_ni_dma_lso_dmaaft_map_tbl_cfg_t cfg;
	aal_ni_dma_lso_lspid_map_tbl_cfg_t lspid_cfg;
	uint8 lspid_map_vld[CA_NI_DMA_LSO_LSPID_MAP_TBL_SIZE];

	memset(&lspid_map_vld,0,sizeof(lspid_map_vld));

	PROC_PRINTF(">>ASIC DMA After MAP Table:\n");
	for(i = 0; i < CA_NI_DMA_LSO_DMAAFT_MAP_TBL_SIZE; i++) {
		if (aal_ni_dma_lso_get_dmaaft_map_tbl(i, &cfg) != CA_E_OK)
			continue;
		if(cfg.en_flag == 0)
			continue;
		PROC_PRINTF("[IDX %2d] vld %d, lspid_map 0x%x, DMAAFT_en %d, fib_id %2d\t", i, cfg.en_flag, cfg.lspid_map, cfg.dmaaft_en, cfg.dmaaft_fib);
		lspid_map_vld[cfg.lspid_map] = 1;
		if(i % 2)
			PROC_PRINTF("\n");
	}

	PROC_PRINTF("\n");
	PROC_PRINTF(">>ASIC DMA LSO LSPID MAP Table:\n");
	for(i = 0; i < CA_NI_DMA_LSO_LSPID_MAP_TBL_SIZE; i++) {
		if(lspid_map_vld[cfg.lspid_map] == 0)
			continue;
		if (aal_ni_dma_lso_get_lspid_map_tbl(i, &lspid_cfg) != CA_E_OK)
			continue;
		if(lspid_cfg.en_flag == 0)
			continue;
		PROC_PRINTF("[IDX %2d] vld %d, lspid 0x%x, FBM en %d, pool_id %d, buf_mask %#llx \n", i, lspid_cfg.en_flag, lspid_cfg.lspid, lspid_cfg.fbm_en, lspid_cfg.fbm_pool_id, lspid_cfg.fbm_buf_mask);
	}

	return SUCCESS;
}

int32 dump_sw_dmaAftMap_table(struct seq_file *s, void *v)
{
	int i;
	PROC_PRINTF(">>DMA After Map Table: show valid enrties only\n");

	for(i = 0 ; i < RTK_FC_TABLESIZE_DMAAFTMAP ; i++)
	{
		if(fc_db.dmaAftMapTbl[i].refCount == 0)
			continue;
		PROC_PRINTF("[IDX %2d, refCount %2d] lspid 0x%x(lspid_map %d), DMAAFT_en %d, fib_id %2d \n", i, fc_db.dmaAftMapTbl[i].refCount, fc_db.dmaAftMapTbl[i].lspid, DMALSO_LSPID_MAP_IDX(fc_db.dmaAftMapTbl[i].lspid), fc_db.dmaAftMapTbl[i].aft_en, fc_db.dmaAftMapTbl[i].aft_fib_idx);
	}

	PROC_PRINTF("----------------------------------------------\n");
	return SUCCESS;
}
#endif

#endif

#endif

int32 dump_ethtype_table(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	uint32 idx=0U;

	PROC_PRINTF(">>ASIC EtherType Table:\n");
	for(idx = 0; idx < RTK_FC_TABLESIZE_ETHERTYPE; idx++)
	{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
		rtk_rg_asic_etherType_entry_t ethType={0};
		retval = rtk_rg_asic_etherTypeTable_get(idx, &ethType);
		if(!retval && ethType.ethertype != 0x0)
			PROC_PRINTF("[%d] ethertype: 0x04%x\n", idx, ethType.ethertype);
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
		l3_cam_ethertype_tbl_entry_t ethType={0};
		retval = aal_entry_get_by_idx(AAL_TABLE_L3_CAM_ETHERTYPE, idx, &ethType);
		if(!retval && ethType.vld)
			PROC_PRINTF("[%d] ethertype: 0x%04x\n", idx, ethType.ethertype);
#endif
	}
	PROC_PRINTF("----------------------------------------------\n");

	return retval;
}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
int dump_extratag_table(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	uint32 list=0U, idx=0U, base_addr = 0U;
	unsigned char contentBuf[RTK_FC_DUAL_CONTENT_BUF_ENT_OFFSET];		// dump content per entry
	rtk_rg_asic_extraTagAction_t *pExtraTag, extraTag;
	char tmpStr[32];

	pExtraTag = &extraTag;

	PROC_PRINTF(">>ASIC dual control/content:\n");

	for(list = RTK_FC_TABLESIZE_EXTRATAG_LISTMIN; list <= RTK_FC_TABLESIZE_EXTRATAG_LISTMAX; list++)
	{
		PROC_PRINTF(" -- Action List[%d] --\n", list);
		for(idx = 0; idx < RTK_FC_TABLESIZE_EXTRATAG_ACTIONS; idx++)
		{
			RTK_RG_ASIC_EXTRATAGACTION_GET(list, idx, pExtraTag);
			if(pExtraTag->type1.act_bit==FB_EXTG_ACTBIT_NOACTION)
				break;
			else{
				switch(pExtraTag->type1.act_bit)
				{
					case FB_EXTG_ACTBIT_1:
						PROC_PRINTF("\tacttype[%d]: inserttag bufoff:%d len:%d\n", pExtraTag->type1.act_bit, pExtraTag->type1.src_addr_offset, pExtraTag->type1.length);
						break;
					case FB_EXTG_ACTBIT_2:
						PROC_PRINTF("\tacttype[%d]: ethtran ethertype:0x%04x\n", pExtraTag->type2.act_bit, pExtraTag->type2.ethertype);
						break;
					case FB_EXTG_ACTBIT_3:
						PROC_PRINTF("\tacttype[%d]: updatelen pktoff:%d width:%d value:%d op:%d\n", pExtraTag->type3.act_bit, pExtraTag->type3.pkt_buff_offset, pExtraTag->type3.length, pExtraTag->type3.value, pExtraTag->type3.operation);
						break;
					case FB_EXTG_ACTBIT_4:
						if(pExtraTag->type4.data_src_type==1)
							PROC_PRINTF("\tacttype[%d]: ipv4id pktoff:%d, intfidx:%d\n", pExtraTag->type4.act_bit, pExtraTag->type4.pkt_buff_offset, pExtraTag->type4.seq_ack_reg_idx);
						else
							PROC_PRINTF("\tacttype[%d]: greseqack pktoff:%d, intfidx:%d, reduceseq:%d, reduceack:%d\n", pExtraTag->type4.act_bit, pExtraTag->type4.pkt_buff_offset, pExtraTag->type4.seq_ack_reg_idx, pExtraTag->type4.reduce_seq, pExtraTag->type4.reduce_ack);
						break;
					case FB_EXTG_ACTBIT_5:
						PROC_PRINTF("\tacttype[%d]: l3chksum pktoff:%d\n", pExtraTag->type5.act_bit, pExtraTag->type5.pkt_buff_offset);
						break;
					case FB_EXTG_ACTBIT_6:
						PROC_PRINTF("\tacttype[%d]: l4chksum pktoff:%d\n", pExtraTag->type6.act_bit, pExtraTag->type6.pkt_buff_offset);
						break;
					case FB_EXTG_ACTBIT_7:
						PROC_PRINTF("\tacttype[%d]: outer_tag_len:%u, outer_udp_offset:%u, outer_ppp_offset:%u, is_l2dual:%u, outer_is_v6:%u, outer_ip_offset:%u\n",
							pExtraTag->type7.act_bit, pExtraTag->type7.outer_tag_len, pExtraTag->type7.outer_udp_offset, pExtraTag->type7.outer_ppp_offset, pExtraTag->type7.is_l2dual, pExtraTag->type7.outer_is_v6, pExtraTag->type7.outer_ip_offset);
						break;
				}
			}
		}

	}
	for(base_addr = 0, idx = 0 ; base_addr < RTK_FC_DUAL_CONTENT_BUF_SIZE ; base_addr = base_addr + RTK_FC_DUAL_CONTENT_BUF_ENT_OFFSET)
	{
		bzero(&contentBuf, sizeof(contentBuf));
		sprintf(tmpStr, "[Extra Tag Content Buffer %d]", idx++);
		RTK_RG_ASIC_EXTRATAGCONTENTBUFFER_GET(base_addr, RTK_FC_DUAL_CONTENT_BUF_ENT_OFFSET, (char *)&contentBuf);
		_rtk_fc_dump_mem(tmpStr, &contentBuf[0], RTK_FC_DUAL_CONTENT_BUF_ENT_OFFSET);
	}

	PROC_PRINTF("\n----------------------------------------------\n");
	return retval;
}
#else
int dump_extratag_table(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	uint32 list=0, idx=0, notEmpty=0;
	unsigned char contentBuf[RTK_FC_DUAL_CONTENT_BUF_SIZE];		// dump content buffer all
	rtk_rg_asic_extraTagAction_t *pExtraTag, extraTag;

	pExtraTag = &extraTag;

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	PROC_PRINTF(">>ASIC ExtraTag Table:\n");
#endif

	for(list = RTK_FC_TABLESIZE_EXTRATAG_LISTMIN; list <= RTK_FC_TABLESIZE_EXTRATAG_LISTMAX; list++)
	{
		notEmpty = 0;
		PROC_PRINTF(" -- Action List[%d] --\n", list);
		for(idx = 0; idx < RTK_FC_TABLESIZE_EXTRATAG_ACTIONS; idx++)
		{
			RTK_RG_ASIC_EXTRATAGACTION_GET(list, idx, pExtraTag);
			if(pExtraTag->type1.act_bit==FB_EXTG_ACTBIT_NOACTION)
				break;
			else{
				switch(pExtraTag->type1.act_bit)
				{
					case FB_EXTG_ACTBIT_1:
						PROC_PRINTF("\tacttype[%d]: inserttag bufoff:%d len:%d\n", pExtraTag->type1.act_bit, pExtraTag->type1.src_addr_offset, pExtraTag->type1.length);
						notEmpty = 1;
						break;
					case FB_EXTG_ACTBIT_2:
						PROC_PRINTF("\tacttype[%d]: ethtran ethertype:0x%04x\n", pExtraTag->type2.act_bit, pExtraTag->type2.ethertype);
						break;
					case FB_EXTG_ACTBIT_3:
						PROC_PRINTF("\tacttype[%d]: updatelen pktoff:%d width:%d value:%d op:%d\n", pExtraTag->type3.act_bit, pExtraTag->type3.pkt_buff_offset, pExtraTag->type3.length, pExtraTag->type3.value, pExtraTag->type3.operation);
						break;
					case FB_EXTG_ACTBIT_4:
						if(pExtraTag->type4.data_src_type==1)
							PROC_PRINTF("\tacttype[%d]: ipv4id pktoff:%d, intfidx:%d\n", pExtraTag->type4.act_bit, pExtraTag->type4.pkt_buff_offset, pExtraTag->type4.seq_ack_reg_idx);
						else
							PROC_PRINTF("\tacttype[%d]: greseqack pktoff:%d, intfidx:%d, reduceseq:%d, reduceack:%d\n", pExtraTag->type4.act_bit, pExtraTag->type4.pkt_buff_offset, pExtraTag->type4.seq_ack_reg_idx, pExtraTag->type4.reduce_seq, pExtraTag->type4.reduce_ack);
						break;
					case FB_EXTG_ACTBIT_5:
						PROC_PRINTF("\tacttype[%d]: l3chksum pktoff:%d\n", pExtraTag->type5.act_bit, pExtraTag->type5.pkt_buff_offset);
						break;
					case FB_EXTG_ACTBIT_6:
						PROC_PRINTF("\tacttype[%d]: l4chksum pktoff:%d\n", pExtraTag->type6.act_bit, pExtraTag->type6.pkt_buff_offset);
						break;
					case FB_EXTG_ACTBIT_7:
						PROC_PRINTF("\tacttype[%d]: outer_tag_len:%u, outer_udp_offset:%u, outer_ppp_offset:%u, is_l2dual:%u, outer_is_v6:%u, outer_ip_offset:%u\n",
							pExtraTag->type7.act_bit, pExtraTag->type7.outer_tag_len, pExtraTag->type7.outer_udp_offset, pExtraTag->type7.outer_ppp_offset, pExtraTag->type7.is_l2dual, pExtraTag->type7.outer_is_v6, pExtraTag->type7.outer_ip_offset);
						break;
				}
			}
		}
		if(notEmpty)
		{
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			uint32 len = 0;
			rtk_rg_asic_extraTagInsertHdrLen_get(list, &len);
			PROC_PRINTF("\t>>IncresedLen = %d\n", len);
#endif
		}

	}
	bzero(&contentBuf, sizeof(contentBuf));
	RTK_RG_ASIC_EXTRATAGCONTENTBUFFER_GET(0, RTK_FC_DUAL_CONTENT_BUF_SIZE, (char *)&contentBuf);
	_rtk_fc_dump_mem("[Extra Tag Content Buffer]", &contentBuf[0], sizeof(contentBuf));


	PROC_PRINTF("\n----------------------------------------------\n");


	return retval;
}
#endif

int dump_acl_reserved_info(struct seq_file *s, void *v){
	//record current fc_db.debugLevel and fc_db.filterLevel
	rtk_fc_debugLevel_t debug_level, filter_level;
	int filter_en;
	uint32 tracefilter_show;

	debug_level = fc_db.debugLevel;
	fc_db.debugLevel |= FC_DEBUG_LEVEL_ACL_RRESERVED;

	if(fc_db.filterLevel){
		filter_level = fc_db.filterLevel;
		tracefilter_show = fc_db.tracefilterShow;
		fc_db.filterLevel = FC_DEBUG_LEVEL_ACL_RRESERVED;
		fc_db.tracefilterShow = 1U;
		filter_en = TRUE;
	}else
		filter_en = FALSE;

	//show reserved ACL info
	_rtk_rg_aclAndCfReservedRuleAdd(RTK_FC_ACLANDCF_RESERVED_TAIL_END, NULL);

	//roll-back fc_db.debugLevel
	fc_db.debugLevel = debug_level;

	//roll-back fc_db.filterLevel
	if(filter_en){
		fc_db.filterLevel = filter_level;
		fc_db.tracefilterShow = tracefilter_show;
	}

	return SUCCESS;
}

int rtk_fc_dump_fc_acl_entry_content(struct seq_file *s, rtk_fc_aclFilterEntry_t *acl_entry)
{
	rt_acl_filterAndQos_t acl_parameter = acl_entry->acl_filter;
	int i;

	PROC_PRINTF("  acl_weight: %d\n", acl_parameter.acl_weight);
	PROC_PRINTF("[Patterns] \n");

	if(acl_parameter.filter_fields & RT_ACL_INGRESS_PORT_MASK_BIT)
		PROC_PRINTF("  ingress_port_mask: 0x%x", acl_parameter.ingress_port_mask);
	else if(acl_parameter.ingress_port_mask != 0x0)
		PROC_PRINTF("  ingress_port_mask: 0x%x (user unconfigure, use default value)", acl_parameter.ingress_port_mask);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	if((fc_db.acl_lan_portmask != 0) && ((acl_parameter.ingress_port_mask&RT_ACL_PORT_ALL_LAN_BIT) == fc_db.acl_lan_portmask))
		PROC_PRINTF(" (acl_lan_portmask 0x%x)", fc_db.acl_lan_portmask);
#endif
	if(fc_db.hypridPPTP.portmask && (acl_parameter.filter_fields & RT_ACL_CARE_PORT_UNITYPE_PPTP_BIT))
		PROC_PRINTF(" (care unitype pptp portmask 0x%llx)\n", fc_db.hypridPPTP.portmask);
	else if(acl_parameter.ingress_port_mask & fc_db.hypridPPTP.portmask)
		PROC_PRINTF(" (not care unitype pptp portmask 0x%llx)\n", fc_db.hypridPPTP.portmask);
	else
		PROC_PRINTF("\n");

	if(acl_parameter.filter_fields & RT_ACL_INGRESS_WLAN_MBSSID_MASK_BIT) {
		for(i=0; i<RT_WLAN_DEVICE_MAX; i++)
			if(acl_parameter.ingress_wlan_mbssid_mask[i] > RT_WLAN_MBSSID_NONE)
				PROC_PRINTF("  ingress_wlan%d_mbssid_mask: 0x%x %s\n", i, acl_parameter.ingress_wlan_mbssid_mask[i],
					(acl_parameter.ingress_wlan_mbssid_mask[i]==RT_WLAN_MBSSID_ALL_BIT)?"(ALL)":"");
	}

	if(acl_parameter.filter_fields & RT_ACL_INGRESS_SMAC_BIT) {
		PROC_PRINTF("  ingress_smac: %s", diag_util_inet_mactoa(acl_parameter.ingress_smac));
		PROC_PRINTF("  ingress_smac_mask: %s\n", diag_util_inet_mactoa(acl_parameter.ingress_smac_mask));
	}
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_DMAC_BIT) {
		PROC_PRINTF("  ingress_dmac: %s", diag_util_inet_mactoa(acl_parameter.ingress_dmac));
		PROC_PRINTF("  ingress_dmac_mask: %s\n", diag_util_inet_mactoa(acl_parameter.ingress_dmac_mask));
	}

	if(acl_parameter.filter_fields & RT_ACL_INGRESS_ETHERTYPE_BIT)
		PROC_PRINTF("  ingress_ethertype: 0x%x  ingress_ethertype_mask: 0x%x\n", acl_parameter.ingress_ethertype, acl_parameter.ingress_ethertype_mask);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_STAGIF_BIT)
		PROC_PRINTF("  ingress_stagif: %s\n", acl_parameter.ingress_stagif?"Must have Stag":"Must not have Stag");
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_STAG_VID_BIT)
		PROC_PRINTF("  ingress_stag_vid: %d\n", acl_parameter.ingress_stag_vid);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_STAG_PRI_BIT)
		PROC_PRINTF("  ingress_stag_pri: %d\n", acl_parameter.ingress_stag_pri);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_STAG_DEI_BIT)
		PROC_PRINTF("  ingress_stag_dei: %d\n", acl_parameter.ingress_stag_dei);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_CTAGIF_BIT)
		PROC_PRINTF("  ingress_ctagif: %s\n", acl_parameter.ingress_ctagif?"Must have Ctag":"Must not have Ctag");
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_CTAG_VID_BIT)
		PROC_PRINTF("  ingress_ctag_vid: %d\n", acl_parameter.ingress_ctag_vid);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_CTAG_PRI_BIT)
		PROC_PRINTF("  ingress_ctag_pri: %d\n", acl_parameter.ingress_ctag_pri);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_CTAG_CFI_BIT)
		PROC_PRINTF("  ingress_ctag_cfi: %d\n", acl_parameter.ingress_ctag_cfi);

	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV4_TAGIF_BIT)
		PROC_PRINTF("  ingress_ipv4_tagif: %s\n", acl_parameter.ingress_ipv4_tagif?"Must be IPv4":"Must not be IPv4");
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV4_SIP_RANGE_BIT) {
		PROC_PRINTF("  ingress_src_ipv4_addr_start: %s", diag_util_inet_ntoa(acl_parameter.ingress_src_ipv4_addr_start));
		PROC_PRINTF("  ingress_src_ipv4_addr_end: %s\n", diag_util_inet_ntoa(acl_parameter.ingress_src_ipv4_addr_end));
	}
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV4_DIP_RANGE_BIT) {
		PROC_PRINTF("  ingress_dest_ipv4_addr_start: %s", diag_util_inet_ntoa(acl_parameter.ingress_dest_ipv4_addr_start));
		PROC_PRINTF("  ingress_dest_ipv4_addr_end: %s\n", diag_util_inet_ntoa(acl_parameter.ingress_dest_ipv4_addr_end));
	}
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV4_DSCP_BIT)
		PROC_PRINTF("  ingress_ipv4_dscp: %d\n", acl_parameter.ingress_ipv4_dscp);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV4_TOS_BIT)
		PROC_PRINTF("  ingress_ipv4_tos: %d\n", acl_parameter.ingress_ipv4_tos);

	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV6_TAGIF_BIT)
		PROC_PRINTF("  ingress_ipv6_tagif: %s\n", acl_parameter.ingress_ipv6_tagif?"Must be IPv6":"Must not be IPv6");
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV6_SIP_BIT) {
		PROC_PRINTF("  ingress_src_ipv6_addr: %s", diag_util_inet_n6toa(&acl_parameter.ingress_src_ipv6_addr[0]));
		PROC_PRINTF("  ingress_src_ipv6_addr_mask: %s\n", diag_util_inet_n6toa(&acl_parameter.ingress_src_ipv6_addr_mask[0]));
	}
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV6_DIP_BIT) {
		PROC_PRINTF("  ingress_dest_ipv6_addr: %s", diag_util_inet_n6toa(&acl_parameter.ingress_dest_ipv6_addr[0]));
		PROC_PRINTF("  ingress_dest_ipv6_addr_mask: %s\n", diag_util_inet_n6toa(&acl_parameter.ingress_dest_ipv6_addr_mask[0]));
	}
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV6_DSCP_BIT)
		PROC_PRINTF("  ingress_ipv6_dscp: %d\n", acl_parameter.ingress_ipv6_dscp);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_IPV6_TC_BIT)
		PROC_PRINTF("  ingress_ipv6_tc: %d\n", acl_parameter.ingress_ipv6_tc);

	if(acl_parameter.filter_fields & RT_ACL_INGRESS_L4_TCP_BIT) {
		PROC_PRINTF("  ingress_l4_protocal: tcp \n");
	}else if(acl_parameter.filter_fields & RT_ACL_INGRESS_L4_UDP_BIT) {
		PROC_PRINTF("  ingress_l4_protocal: udp \n");
	}else if(acl_parameter.filter_fields & RT_ACL_INGRESS_L4_ICMP_BIT) {
		PROC_PRINTF("  ingress_l4_protocal: icmp \n");
	}else if(acl_parameter.filter_fields & RT_ACL_INGRESS_L4_ICMPV6_BIT) {
		PROC_PRINTF("  ingress_l4_protocal: icmpv6 \n");
	}else if(acl_parameter.filter_fields & RT_ACL_INGRESS_L4_POROTCAL_VALUE_BIT) {
		PROC_PRINTF("  ingress_l4_protocal: %d\n", acl_parameter.ingress_l4_protocal);
	}
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_L4_SPORT_RANGE_BIT)
		PROC_PRINTF("  ingress_src_l4_port_start: %d  ingress_src_l4_port_end: %d\n", acl_parameter.ingress_src_l4_port_start, acl_parameter.ingress_src_l4_port_end);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_L4_DPORT_RANGE_BIT)
		PROC_PRINTF("  ingress_dest_l4_port_start: %d  ingress_dest_l4_port_end: %d\n", acl_parameter.ingress_dest_l4_port_start, acl_parameter.ingress_dest_l4_port_end);

	if(acl_parameter.filter_fields & RT_ACL_INGRESS_STREAM_ID_BIT)
		PROC_PRINTF("  ingress_stream_id: %d  ingress_stream_id_mask: 0x%x\n", acl_parameter.ingress_stream_id, acl_parameter.ingress_stream_id_mask);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_TCP_FLAGS_BIT)
		PROC_PRINTF("  ingress_tcp_flags: 0x%x  ingress_tcp_flags_mask: 0x%x\n", acl_parameter.ingress_tcp_flags, acl_parameter.ingress_tcp_flags_mask);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_PKT_LEN_RANGE_BIT)
		PROC_PRINTF("  ingress_packet_length_start: %d  ingress_packet_length_end: %d\n", acl_parameter.ingress_packet_length_start, acl_parameter.ingress_packet_length_end);
	if(acl_parameter.filter_fields & RT_ACL_INGRESS_ARP_TARGET_IP_BIT)
		PROC_PRINTF("  ingress_arp_target_ip: %s (packet offset %d)\n", diag_util_inet_ntoa(acl_parameter.ingress_arp_target_ip), (fc_db.controlFuc.acl_arp_targetip_offset + ARP_TARGET_IP_BYTE_OFFSET - 1));

	if(acl_parameter.filter_fields == 0x0)
		PROC_PRINTF("  NULL\n");

	PROC_PRINTF("[Actions] \n");
	if(acl_parameter.action_fields & RT_ACL_ACTION_FORWARD_GROUP_DROP_BIT)
		PROC_PRINTF("  FORWARD: drop \n");
	if(acl_parameter.action_fields & RT_ACL_ACTION_FORWARD_GROUP_PERMIT_BIT)
		PROC_PRINTF("  FORWARD: permit \n");
	if(acl_parameter.action_fields & RT_ACL_ACTION_FORWARD_GROUP_TRAP_BIT)
		PROC_PRINTF("  FORWARD: trap \n");
	if(acl_parameter.action_fields & RT_ACL_ACTION_FORWARD_GROUP_TRAP_SLAVECPU_BIT)
		PROC_PRINTF("  FORWARD: trap to slave cpu \n");
	if(acl_parameter.action_fields & RT_ACL_ACTION_FORWARD_GROUP_TRAP_TO_PS_BIT)
		PROC_PRINTF("  FORWARD: trap to ps \n");
	if(acl_parameter.action_fields & RT_ACL_ACTION_FORWARD_GROUP_REDIRECT_BIT)
		PROC_PRINTF("  FORWARD: redirect to port %d \n", acl_parameter.action_forward_group_redirect_port_idx);
	if(acl_parameter.action_fields & RT_ACL_ACTION_PRIORITY_GROUP_TRAP_PRIORITY_BIT)
		PROC_PRINTF("  PRIORITY: trap priority %d \n", acl_parameter.action_priority_group_trap_priority);
	if(acl_parameter.action_fields & RT_ACL_ACTION_PRIORITY_GROUP_ACL_PRIORITY_BIT)
		PROC_PRINTF("  PRIORITY: acl priority %d \n", acl_parameter.action_priority_group_acl_priority);
	if(acl_parameter.action_fields & RT_ACL_ACTION_METER_GROUP_SHARE_METER_BIT) {
		if(acl_entry->hw_share_meter_index != acl_parameter.action_meter_group_share_meter_index)
			PROC_PRINTF("  METER: shere meter with meter index %d (hw meter index %d)\n", acl_parameter.action_meter_group_share_meter_index, acl_entry->hw_share_meter_index);
		else
			PROC_PRINTF("  METER: shere meter with meter index %d \n", acl_parameter.action_meter_group_share_meter_index);
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
		PROC_PRINTF("  (Note: meter function may not work if packet also hit other HW ACL rule with TRAP action.)\n");
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
		if(fc_db.controlFuc.acl_multiple_hit_cfg == 0)
			PROC_PRINTF("  (Note: meter function may not work if packet also hit other ACL with TRAP action.)\n");
		else if((acl_parameter.action_fields & RT_ACL_ACTION_FORWARD_GROUP_TRAP_BIT) == 0x0)
			PROC_PRINTF("  (Note: meter function may not work if packet also hit other flow with METER action.)\n");
#endif
	}
	if(acl_parameter.action_fields & RT_ACL_ACTION_LOGGING_GROUP_MIB_BIT)
		PROC_PRINTF("  LOGGING: mib index %d (hw mib index %d) \n", acl_parameter.action_logging_group_mib_index, acl_entry->hw_mib_index);

	if(acl_parameter.action_fields == 0x0)
		PROC_PRINTF("  NULL\n");

	return SUCCESS;
}

int dump_sw_acl(struct seq_file *s, void *v)
{
	int i;

	PROC_PRINTF("acl_SW_table_entry_size:%d\n",fc_db.acl_SW_table_entry_size);

	PROC_PRINTF("aclSW rule index sorting:\n");
	for(i=0;i<MAX_ACL_SW_ENTRY_SIZE;i++){
		if(fc_db.acl_SWindex_sorting_by_weight[i]==-1)
			break;
		PROC_PRINTF("ACL[%d]:w(%d)",fc_db.acl_SWindex_sorting_by_weight[i],
			fc_db.acl_SW_table_entry[(fc_db.acl_SWindex_sorting_by_weight[i])].acl_filter.acl_weight);
		if(i+1!=MAX_ACL_SW_ENTRY_SIZE && fc_db.acl_SWindex_sorting_by_weight[i+1]>=0)
			PROC_PRINTF(" > ");
	}
	PROC_PRINTF("\n");

	for(i=0;i<MAX_ACL_SW_ENTRY_SIZE;i++){

		if(fc_db.acl_SW_table_entry[i].valid==ENABLED){
			PROC_PRINTF("========================RT_ACL[%d]===========================\n",i);
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			PROC_PRINTF("[hw_acl_start:%d(continue:%d)] \n"
				,fc_db.acl_SW_table_entry[i].hw_aclEntry_start
				,fc_db.acl_SW_table_entry[i].hw_aclEntry_size);

			PROC_PRINTF("[Using range tables]: \n");
			if(fc_db.acl_SW_table_entry[i].hw_used_table&RTK_FC_ACL_USED_INGRESS_SIP4TABLE) PROC_PRINTF("ACL_SIP4_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[i].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_SIP4TABLE_INDEX]);
			if(fc_db.acl_SW_table_entry[i].hw_used_table&RTK_FC_ACL_USED_INGRESS_DIP4TABLE) PROC_PRINTF("ACL_DIP4_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[i].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_DIP4TABLE_INDEX]);
			if(fc_db.acl_SW_table_entry[i].hw_used_table&RTK_FC_ACL_USED_INGRESS_SIP6TABLE) PROC_PRINTF("ACL_SIP6_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[i].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_SIP6TABLE_INDEX]);
			if(fc_db.acl_SW_table_entry[i].hw_used_table&RTK_FC_ACL_USED_INGRESS_DIP6TABLE) PROC_PRINTF("ACL_DIP6_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[i].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_DIP6TABLE_INDEX]);
			if(fc_db.acl_SW_table_entry[i].hw_used_table&RTK_FC_ACL_USED_INGRESS_SPORTTABLE) PROC_PRINTF("ACL_SPORT_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[i].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_SPORTTABLE_INDEX]);
			if(fc_db.acl_SW_table_entry[i].hw_used_table&RTK_FC_ACL_USED_INGRESS_DPORTTABLE) PROC_PRINTF("ACL_DPORT_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[i].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_DPORTTABLE_INDEX]);
			if(fc_db.acl_SW_table_entry[i].hw_used_table&RTK_FC_ACL_USED_INGRESS_PKTLENTABLE) PROC_PRINTF("ACL_PKTLEN_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[i].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_PKTLENTABLE_INDEX]);
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
			PROC_PRINTF("[hw_acl_index:%s(port:0x%x, total:%d)] \n"
				,fc_db.acl_SW_table_entry[i].hw_aclEntry_index
				,fc_db.acl_SW_table_entry[i].hw_aclEntry_port.bits[0]
				,fc_db.acl_SW_table_entry[i].hw_aclEntry_count);
#endif
			rtk_fc_dump_fc_acl_entry_content(s, &fc_db.acl_SW_table_entry[i]);

		}

	}

	return SUCCESS;
}

int dump_sw_acl_by_index(struct file *file, const char *buffer, unsigned long len, void *data)
{
	int sw_index=_rtk_fc_proc_parsing_string_to_integer(buffer,len);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	unsigned char tmpBuf[CMD_BUFF_SIZE];
	unsigned char tmpBuf2[CMD_BUFF_SIZE];
	char *strptr,*split_str;
	int value = 0;
	int value_pre = -1;
#endif

	if(sw_index >= MAX_ACL_SW_ENTRY_SIZE)
		return len;

	if(fc_db.acl_SW_table_entry[sw_index].valid != ENABLED)
		return len;

	{
		rtlglue_printf("========================RT_ACL[%d]===========================\n",sw_index);
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
		rtlglue_printf("[hw_acl_start:\033[1;33m%d\033[0m(continue:%d)] \n"
			,fc_db.acl_SW_table_entry[sw_index].hw_aclEntry_start
			,fc_db.acl_SW_table_entry[sw_index].hw_aclEntry_size);

		rtlglue_printf("[Using range tables]: \n");
		if(fc_db.acl_SW_table_entry[sw_index].hw_used_table&RTK_FC_ACL_USED_INGRESS_SIP4TABLE) rtlglue_printf("ACL_SIP4_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[sw_index].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_SIP4TABLE_INDEX]);
		if(fc_db.acl_SW_table_entry[sw_index].hw_used_table&RTK_FC_ACL_USED_INGRESS_DIP4TABLE) rtlglue_printf("ACL_DIP4_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[sw_index].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_DIP4TABLE_INDEX]);
		if(fc_db.acl_SW_table_entry[sw_index].hw_used_table&RTK_FC_ACL_USED_INGRESS_SIP6TABLE) rtlglue_printf("ACL_SIP6_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[sw_index].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_SIP6TABLE_INDEX]);
		if(fc_db.acl_SW_table_entry[sw_index].hw_used_table&RTK_FC_ACL_USED_INGRESS_DIP6TABLE) rtlglue_printf("ACL_DIP6_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[sw_index].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_DIP6TABLE_INDEX]);
		if(fc_db.acl_SW_table_entry[sw_index].hw_used_table&RTK_FC_ACL_USED_INGRESS_SPORTTABLE) rtlglue_printf("ACL_SPORT_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[sw_index].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_SPORTTABLE_INDEX]);
		if(fc_db.acl_SW_table_entry[sw_index].hw_used_table&RTK_FC_ACL_USED_INGRESS_DPORTTABLE) rtlglue_printf("ACL_DPORT_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[sw_index].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_DPORTTABLE_INDEX]);
		if(fc_db.acl_SW_table_entry[sw_index].hw_used_table&RTK_FC_ACL_USED_INGRESS_PKTLENTABLE) rtlglue_printf("ACL_PKTLEN_RANGE_TABLE[%d]  \n",fc_db.acl_SW_table_entry[sw_index].hw_used_table_index[RTK_FC_ACL_USED_INGRESS_PKTLENTABLE_INDEX]);
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
		rtlglue_printf("[hw_acl_index:\033[1;33m%s\033[0m(port:0x%x, total:%d)] \n"
			,fc_db.acl_SW_table_entry[sw_index].hw_aclEntry_index
			,fc_db.acl_SW_table_entry[sw_index].hw_aclEntry_port.bits[0]
			,fc_db.acl_SW_table_entry[sw_index].hw_aclEntry_count);
#endif
		rtk_fc_dump_fc_acl_entry_content(NULL, &fc_db.acl_SW_table_entry[sw_index]);
	}

#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	strscpy(tmpBuf, fc_db.acl_SW_table_entry[sw_index].hw_aclEntry_index, CMD_BUFF_SIZE);
	strptr=tmpBuf;
	while(1)
	{	//ex: 26-0,90-0
		split_str = strsep(&strptr,"-");
		if (strptr==NULL) break;
		value = simple_strtol(split_str, NULL, 0);
		if((value != value_pre) && (value >= 0) && (value <= MAX_ACL_CA_AAL_CLS_RULE_SIZE)){
			rtlglue_printf("========================HW_ACL[%d]===========================\n",value);
			snprintf(tmpBuf2, sizeof(tmpBuf2), "%d", value);
			dump_acl_by_index(NULL, tmpBuf2, sizeof(tmpBuf2), NULL);
			value_pre = value;
		}
		split_str = strsep(&strptr,",");
		if (strptr==NULL) break;
		if (split_str==NULL) continue; //fix coverity UNUSED_VALUE
	}
#endif

	return len;
}


int32 dump_sw_netif_dualInfo(struct seq_file *s, void *v,int swNetifIdx)
{


	if(fc_db.netifTbl[swNetifIdx].dualType)
	{
		PROC_PRINTF("  \tdualUniInfo_syncedEncap:%d dualUniInfo_syncedDecap:%d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo_syncedEncap,fc_db.netifTbl[swNetifIdx].dualUniInfo_syncedDecap);
		PROC_PRINTF("  \tDualType: %d (%s)	dualHash:0x%08x\n", fc_db.netifTbl[swNetifIdx].dualType, name_of_dualHdrType[fc_db.netifTbl[swNetifIdx].dualType],fc_db.netifTbl[swNetifIdx].dualHashKey);
		PROC_PRINTF("  \touterHdrExtratagIdx: %d	outerHdrFlowIdx: %d\n", fc_db.netifTbl[swNetifIdx].outerHdrExtratagIdx, fc_db.netifTbl[swNetifIdx].outerHdrFlowIdx);
		if( fc_db.netifTbl[swNetifIdx].dualHdr_downstream_earlyChk)
			PROC_PRINTF("  \tdualHdr_outer_ipversion:%d dualHdr_outer_L3_Offset:%d dualHdr_outer_length:%d\n", fc_db.netifTbl[swNetifIdx].dualHdr_outer_ipversion, fc_db.netifTbl[swNetifIdx].dualHdr_outer_L3_Offset,fc_db.netifTbl[swNetifIdx].dualHdr_outer_length);
	}
	if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_VXLAN)
	{
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		PROC_PRINTF("  \textra_valid: %d\n",fc_db.netifTbl[swNetifIdx].extra_available);
		PROC_PRINTF("  \textra_netifId_map: %d\n",fc_db.netifTbl[swNetifIdx].extra_netifId_map);
#endif	
		PROC_PRINTF("  \tVXLAN vni: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.vxlan_vni);
		PROC_PRINTF("  \touter_ctag_if: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_ctag_if);
		PROC_PRINTF("  \touter_cvlan_id: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_cvlan_id);
		PROC_PRINTF("  \touter_ctag_pri: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_ctag_pri);
		PROC_PRINTF("  \touter_stag_if: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_stag_if);
		PROC_PRINTF("  \touter_svlan_id: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_svlan_id);
		PROC_PRINTF("  \touter_stag_pri: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_stag_pri);
		PROC_PRINTF("  \touter_tag_len: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_tag_len);
		PROC_PRINTF("  \touter_pppoe_tag: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_pppoe_tag);
		PROC_PRINTF("  \touter_pppoe_sid: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_pppoe_sid);
		PROC_PRINTF("  \tvxlan_vni: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.vxlan_vni);
		PROC_PRINTF("  \touter_is_v6: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_is_v6);
		if(fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_is_v6)
			PROC_PRINTF("  \touter_ip: %x\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_ip);
		else
			PROC_PRINTF("  \touter_v6Ip: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.vxlan_netif_info.outer_v6Ip.s6_addr32[0]);
	}
	else if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_MAPT)
	{
		PROC_PRINTF("  \tlocalV6Ip: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.maptInfo.localV6Ip.s6_addr32[0]);
		PROC_PRINTF("  \tremoteV6Ip: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.maptInfo.remoteV6Ip.s6_addr32[0]);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)		
		PROC_PRINTF("  \tprefixLen %d draftVer %d\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.maptInfo.hwPrefxLen, fc_db.netifTbl[swNetifIdx].dualUniInfo.maptInfo.hwDraftVer);
#endif
		PROC_PRINTF("  \tfc_db.dualIpv6HashMask:%x\n",fc_db.dualIpv6HashMask);
	}
	else if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_XLAT464)
	{
		PROC_PRINTF("  \tremote6Addr: %pI6c  prefixType:%x\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.xlatInfo.Ipv6Addr_remote.s6_addr32[0],fc_db.netifTbl[swNetifIdx].dualUniInfo.xlatInfo.Ipv6Addr_remotePreifx);
		PROC_PRINTF("  \tlocal6Addr:  %pI6c prefixType:%x\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.xlatInfo.Ipv6Addr_local.s6_addr32[0],fc_db.netifTbl[swNetifIdx].dualUniInfo.xlatInfo.Ipv6Addr_localPreifx);
		PROC_PRINTF("  \tfc_db.dualIpv6HashMask:%x\n",fc_db.dualIpv6HashMask);
	}
	else if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_IP4INIP6)
	{
		PROC_PRINTF("  \tremote6Addr: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.ip4Inip6_info.remote6Addr.s6_addr32[0]);
		PROC_PRINTF("  \tlocal6Addr: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.ip4Inip6_info.local6Addr.s6_addr32[0]);
		PROC_PRINTF("  \tisMape:%d\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.ip4Inip6_info.isMape);		
	}
	else if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_6RD)
	{
		uint32 rip,lip;
		rip=htonl(fc_db.netifTbl[swNetifIdx].dualUniInfo.ip6Inip4_info.remoteAddr.s_addr);
		lip=htonl(fc_db.netifTbl[swNetifIdx].dualUniInfo.ip6Inip4_info.localAddr.s_addr);
		PROC_PRINTF("  \tremoteAddr: %pI4\n", &rip);
		PROC_PRINTF("  \tlocalAddr: %pI4\n", &lip);
	}
	else if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_PPTP)
	{
		PROC_PRINTF("  \t greCallID: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.call_id);
		PROC_PRINTF("  \t peerCallID: %d\n",fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.peer_call_id);
		if(fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.outer_ipversion)
		{
			PROC_PRINTF("  \tIPv6\n");
			PROC_PRINTF("  \t remote6Addr: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.remoteV6Ip.s6_addr32[0]);
			PROC_PRINTF("  \t local6Addr: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.localV6Ip.s6_addr32[0]);
		}
		else
		{
			uint32 rip,lip;
			rip=htonl(fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.remoteIp);
			lip=htonl(fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.localIp);
			PROC_PRINTF("  \tIPv4\n");
			//PROC_PRINTF("  \tremoteAddr: %pI4n\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.remoteIp);
			//PROC_PRINTF("  \tlocalAddr: %pI4h\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.localIp);
			PROC_PRINTF("  \t remoteAddr: %pI4\n", &rip);
			PROC_PRINTF("  \t localAddr: %pI4\n", &lip);
		}
	}
	else if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_L2TP)
	{
		PROC_PRINTF("  \t tunnel ID: %d session ID: %d\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.tunnel_id, fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.session_id);
		PROC_PRINTF("  \t [Peer] tunnel ID: %d session ID: %d\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.peer_tunnel_id, fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.peer_session_id);	
		if(fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.outer_ipversion)
		{
			PROC_PRINTF("  \tIPv6\n");
			PROC_PRINTF("  \t remote6Addr: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.remoteV6Ip.s6_addr32[0]);
			PROC_PRINTF("  \t local6Addr: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.localV6Ip.s6_addr32[0]);
		}
		else
		{
			uint32 rip,lip;
			rip=htonl(fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.remoteIp);
			lip=htonl(fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.localIp);
			PROC_PRINTF("  \tIPv4\n");
			//PROC_PRINTF("  \tremoteAddr: %pI4n\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.remoteIp);
			//PROC_PRINTF("  \tlocalAddr: %pI4h\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.psGreInfo.localIp);
			PROC_PRINTF("  \t remoteAddr: %pI4\n", &rip);
			PROC_PRINTF("  \t localAddr: %pI4\n", &lip);
		}
		PROC_PRINTF("  \tUDP\n");
		PROC_PRINTF("  \t local_l4port: %u\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.local_l4port);
		PROC_PRINTF("  \t remote_l4port: %u\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.l2tpInfo.remote_l4port);
	}
	else if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_GRE_ETH_BR)
	{
		PROC_PRINTF("  \touterTag_len: %u\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.greEthBrInfo.outerTag_len);
		if(fc_db.netifTbl[swNetifIdx].dualUniInfo.greEthBrInfo.outer_ipversion)
			PROC_PRINTF("  \t[outer IPv6] remoteV6Addr: %pI6c, localV6Addr: %pI6c\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.greEthBrInfo.remoteV6Ip, &fc_db.netifTbl[swNetifIdx].dualUniInfo.greEthBrInfo.localV6Ip);
		else
			PROC_PRINTF("  \t[outer IPv4] remoteAddr: %pI4n, localAddr: %pI4n\n", &fc_db.netifTbl[swNetifIdx].dualUniInfo.greEthBrInfo.remoteIp, &fc_db.netifTbl[swNetifIdx].dualUniInfo.greEthBrInfo.localIp);
	}
	else if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_SRV6)
	{
		PROC_PRINTF("  \tSRv6 type: %u\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.srv6_info.srv6_type);
		PROC_PRINTF("  \t outer offset: %u\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.srv6_info.srv6_offset);
		PROC_PRINTF("  \t outer tag_len: %u\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.srv6_info.outer_tag_len);
		PROC_PRINTF("  \t outer srh_len: %u\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.srv6_info.outer_srh_len);
		if(fc_db.netifTbl[swNetifIdx].dualUniInfo.srv6_info.srv6_encap_conn_idx_valid)
			PROC_PRINTF("  \t encap conn idx: %u\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.srv6_info.srv6_encap_conn_idx);
		if(fc_db.netifTbl[swNetifIdx].dualUniInfo.srv6_info.srv6_decap_conn_idx_valid)
			PROC_PRINTF("  \t decap conn idx: %u\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.srv6_info.srv6_decap_conn_idx);
	}
	else if(fc_db.netifTbl[swNetifIdx].dualType == RTK_FC_DUALTYPE_IPSEC)
	{
		PROC_PRINTF("  \tvalid: %d\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.ipsec_info.valid);
		PROC_PRINTF("  \t[EN]hook table index: %d\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.ipsec_info.en_hook_table_idx);
		PROC_PRINTF("  \t[DE]hook table index: %d\n", fc_db.netifTbl[swNetifIdx].dualUniInfo.ipsec_info.de_hook_table_idx);
	}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	if(fc_db.netifTbl[swNetifIdx].dualType)
	{
		if(fc_db.netifTbl[swNetifIdx].hwEntryNum == 1)
			PROC_PRINTF("  \thwEntryNum: %d, hwIdx %d fmrhwIdx %d extra_available: %d extra_netifId_map: %d extraFlowIdx %d hwIdx_for_pe %d\n", fc_db.netifTbl[swNetifIdx].hwEntryNum, fc_db.netifTbl[swNetifIdx].hwIdx,fc_db.netifTbl[swNetifIdx].hwFmrIdx,fc_db.netifTbl[swNetifIdx].extra_available,fc_db.netifTbl[swNetifIdx].extra_netifId_map,fc_db.netifTbl[swNetifIdx].dualHdr_extra_flowIdx,fc_db.netifTbl[swNetifIdx].hwIdx_for_pe);
		else
			PROC_PRINTF("  \thwEntryNum: %d, hwIdx %d ~ %d fmrhwIdx %d  extra_available: %d extra_netifId_map: %d extraFlowIdx %d hwIdx_for_pe %d\n", fc_db.netifTbl[swNetifIdx].hwEntryNum, fc_db.netifTbl[swNetifIdx].hwIdx, fc_db.netifTbl[swNetifIdx].hwIdx+fc_db.netifTbl[swNetifIdx].hwEntryNum-1,fc_db.netifTbl[swNetifIdx].hwFmrIdx,fc_db.netifTbl[swNetifIdx].extra_available,fc_db.netifTbl[swNetifIdx].extra_netifId_map,fc_db.netifTbl[swNetifIdx].dualHdr_extra_flowIdx,fc_db.netifTbl[swNetifIdx].hwIdx_for_pe);

		PROC_PRINTF("  \tdualHdr_HWState: 0x%x dualHdr_ds_clsIdx: 0x%x \n", fc_db.netifTbl[swNetifIdx].dualHdr_HWState, fc_db.netifTbl[swNetifIdx].dualHdr_ds_clsIdx);

	}
	
#endif

	return SUCCESS;
}

int32 dump_sw_netif(struct seq_file *s, void *v)
{
	int32	i;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	int32 j;
#endif
	struct net_device *p_PScacheDev[DEV_STACK_MAX]={NULL,NULL,NULL,NULL};
	PROC_PRINTF(">>SW Netif Table:\n\n");


	for(i=0; i<RTK_FC_TABLESIZE_INTF_SW; i++)
	{
		if (fc_db.netifTbl[i].intf.valid)
		{
			rtk_fc_psIfidxStackKey_to_dev(fc_db.netifTbl[i].psIfidxStackKey,p_PScacheDev);
				
			PROC_PRINTF("swNetif[%d]  MAC: %pM    IP: %d.%d.%d.%d    MTU: %d Bytes   \n",i,
				&fc_db.netifTbl[i].intf.gateway_mac_addr.octet[0],
				(fc_db.netifTbl[i].intf.gateway_ipv4_addr>>24)&0xff, (fc_db.netifTbl[i].intf.gateway_ipv4_addr>>16)&0xff, (fc_db.netifTbl[i].intf.gateway_ipv4_addr>>8)&0xff, fc_db.netifTbl[i].intf.gateway_ipv4_addr&0xff,
				fc_db.netifTbl[i].intf.intf_mtu);

			PROC_PRINTF("  \tpsIfidxStackKey:%08x [%s(%02x)]->[%s(%02x)]->[%s(%02x)]->[%s(%02x)]\n",fc_db.netifTbl[i].psIfidxStackKey,
				p_PScacheDev[0]?p_PScacheDev[0]->name:"NULL",p_PScacheDev[0]?rtk_fc_devGwMacIdx_get(p_PScacheDev[0]):DEVIFIDX_INVALID_MIN,
				p_PScacheDev[1]?p_PScacheDev[1]->name:"NULL",p_PScacheDev[1]?rtk_fc_devGwMacIdx_get(p_PScacheDev[1]):DEVIFIDX_INVALID_MIN,
				p_PScacheDev[2]?p_PScacheDev[2]->name:"NULL",p_PScacheDev[2]?rtk_fc_devGwMacIdx_get(p_PScacheDev[2]):DEVIFIDX_INVALID_MIN,
				p_PScacheDev[3]?p_PScacheDev[3]->name:"NULL",p_PScacheDev[3]?rtk_fc_devGwMacIdx_get(p_PScacheDev[3]):DEVIFIDX_INVALID_MIN);

			PROC_PRINTF("  \tnetif_hwIdx:%d\n",fc_db.netifTbl[i].hwIdx) ;

			if((fc_db.netifTbl[i].hwIdx!= -1) && (fc_db.netifTbl[i].intf.out_pppoe_act==FB_NETIFPPPOE_ACT_ADD))
				PROC_PRINTF("  \tPPPoE sid: 0x%x  pppoeGwLutIdx:%d\n", fc_db.netifTbl[i].intf.out_pppoe_sid,fc_db.netifTbl[i].pppoeGwLutIdx);
			
			PROC_PRINTF("  \tLut index: %d\n", fc_db.netifTbl[i].lutIdx);
			if(fc_db.netifTbl[i].flowCntRef)
				PROC_PRINTF("  \tflowCntRef: %d\n", fc_db.netifTbl[i].flowCntRef);

			dump_sw_netif_dualInfo(s,v,i);


		}

	}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	for(i=0; i<RTK_FC_DUALTYPE_END; i++) {
		for(j = 0; j < RTK_FC_DUALHDR_EXTRA_TUNNEL_MAX_NUM;j++)
		if(fc_db.dual_extra_tunnel_valid[i][j])
			PROC_PRINTF("\nDUAL[%d] extra tunnel[%d] used.\n", i,j);
	}
#endif
	return SUCCESS;

}


int dump_sw_netifmib(struct seq_file *s, void *v)
{
	int retval  = 0;
	int i = 0;
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
	rtk_rg_asic_netifMib_entry_t intfMib;
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
	l3pe_cntr_tx_t tx_cntr;
	l3pe_cntr_rx_t rx_cntr;
#if defined(CONFIG_FC_CA8277B_SERIES)
	l3pe_cntr_tx_t mc_tx_cntr;
	l3pe_cntr_rx_t mc_rx_cntr;
#endif
#endif

	rtlglue_printf("Usage: Reset mib by \"echo [idx] > /proc/fc/[PATH]/[NAME]\"\r\n");
	rtlglue_printf("       Reset All mib by \"echo -1 > /proc/fc/[PATH]/[NAME]\"\r\n");
	rtlglue_printf(">>\033[1;35mHW\033[0m Netif MIB:\n\n");

	for(i=0; i<RTK_FC_TABLESIZE_INTF_SW; i++)
	{
		if (fc_db.netifTbl[i].intf.valid != FALSE && fc_db.netifTbl[i].hwIdx!=SIGNED_INVALID)
		{
			int32 hwIdx=fc_db.netifTbl[i].hwIdx;
			rtlglue_printf("  [%d]", hwIdx);

#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			memset(&intfMib,0,sizeof(intfMib));
			retval = rtk_rg_asic_netifMib_get(hwIdx, &intfMib);

			rtlglue_printf("  \t--HW--\n");
			rtlglue_printf("  \tIgr pkt count : uc(%u), mc(%u), bc(%u)\n", intfMib.in_intf_uc_packet_cnt, intfMib.in_intf_mc_packet_cnt, intfMib.in_intf_bc_packet_cnt);
			rtlglue_printf("  \tIgr byte count: uc(%llu), mc(%llu), bc(%llu)\n", intfMib.in_intf_uc_byte_cnt, intfMib.in_intf_mc_byte_cnt, intfMib.in_intf_bc_byte_cnt);
			rtlglue_printf("  \tEgr pkt count : uc(%u), mc(%u), bc(%u)\n", intfMib.out_intf_uc_packet_cnt, intfMib.out_intf_mc_packet_cnt, intfMib.out_intf_bc_packet_cnt);
			rtlglue_printf("  \tEgr byte count: uc(%llu), mc(%llu), bc(%llu)\n", intfMib.out_intf_uc_byte_cnt, intfMib.out_intf_mc_byte_cnt, intfMib.out_intf_bc_byte_cnt);

			rtlglue_printf("  \t--SW--\n");
			rtlglue_printf("  \tIgr pkt count : uc(%llu), mc(%llu), bc(%llu)\n", _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_UC_PKT_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_MC_PKT_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_BC_PKT_CNT));
			rtlglue_printf("  \tIgr byte count: uc(%llu), mc(%llu), bc(%llu)\n", _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_UC_BYTE_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_MC_BYTE_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_BC_BYTE_CNT));
			rtlglue_printf("  \tEgr pkt count : uc(%llu), mc(%llu), bc(%llu)\n", _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_UC_PKT_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_MC_PKT_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_BC_PKT_CNT));
			rtlglue_printf("  \tEgr byte count: uc(%llu), mc(%llu), bc(%llu)\n", _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_UC_BYTE_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_MC_BYTE_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_BC_BYTE_CNT));
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
			memset(&tx_cntr,0,sizeof(tx_cntr));
			memset(&rx_cntr,0,sizeof(rx_cntr));
			aal_l3pe_cntr_tx_get(G3_DEF_DEVID, hwIdx, &tx_cntr);
			aal_l3pe_cntr_rx_get(G3_DEF_DEVID, hwIdx, &rx_cntr);
#if defined(CONFIG_FC_CA8277B_SERIES)
			memset(&mc_tx_cntr,0,sizeof(mc_tx_cntr));
			memset(&mc_rx_cntr,0,sizeof(mc_rx_cntr));
			aal_l3pe_cntr_tx_get(G3_DEF_DEVID, hwIdx+RTK_FC_MC_HW_NETIF_IDXSHIFT, &mc_tx_cntr);
			aal_l3pe_cntr_rx_get(G3_DEF_DEVID, hwIdx+RTK_FC_MC_HW_NETIF_IDXSHIFT, &mc_rx_cntr);

			rtlglue_printf("  \t--HW--\n");
			rtlglue_printf("  \tIgr pkt count : uc(%llu),\tmc(%llu),\t(drop count: %llu, csum err: %llu)\n", ((long long unsigned int)rx_cntr.uc_pkt + (long long unsigned int)rx_cntr.nuc_pkt), ((long long unsigned int)mc_rx_cntr.uc_pkt + (long long unsigned int)mc_rx_cntr.nuc_pkt), (long long unsigned int)rx_cntr.drop_pkt+(long long unsigned int)mc_rx_cntr.drop_pkt, (long long unsigned int)rx_cntr.error_pkt+(long long unsigned int)mc_rx_cntr.error_pkt);
			rtlglue_printf("  \tIgr byte count: uc(%llu),\tmc(%llu),\t(drop byte count: %llu)\n", rx_cntr.byte + (4 * (rx_cntr.uc_pkt + rx_cntr.nuc_pkt))/*CRC*/, mc_rx_cntr.byte + (4 * (mc_rx_cntr.uc_pkt + mc_rx_cntr.nuc_pkt))/*CRC*/, rx_cntr.drop_byte + (4 * rx_cntr.drop_pkt) + mc_rx_cntr.drop_byte + (4 * mc_rx_cntr.drop_pkt));
			rtlglue_printf("  \tEgr pkt count : uc(%llu),\tmc(%llu),\t(drop count: %llu)\n", ((long long unsigned int)tx_cntr.uc_pkt + (long long unsigned int)tx_cntr.nuc_pkt), ((long long unsigned int)mc_tx_cntr.uc_pkt + (long long unsigned int)mc_tx_cntr.nuc_pkt), (long long unsigned int)tx_cntr.drop_pkt + (long long unsigned int)mc_tx_cntr.drop_pkt);
			rtlglue_printf("  \tEgr byte count: uc(%llu),\tmc(%llu),\t(drop byte count: %llu)\n", tx_cntr.byte + (4 * (tx_cntr.uc_pkt + tx_cntr.nuc_pkt)), mc_tx_cntr.byte + (4 * (mc_tx_cntr.uc_pkt + mc_tx_cntr.nuc_pkt)), tx_cntr.drop_byte + (4 * tx_cntr.drop_pkt) + mc_tx_cntr.drop_byte + (4 * mc_tx_cntr.drop_pkt));
#elif defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			{
				int rx_uc_pkt_wrapRnd_cnt = 0, rx_nuc_pkt_wrapRnd_cnt = 0, rx_drop_pkt_wrapRnd_cnt = 0, rx_error_pkt_wrapRnd_cnt = 0;
				int tx_uc_pkt_wrapRnd_cnt = 0, tx_nuc_pkt_wrapRnd_cnt = 0, tx_drop_pkt_wrapRnd_cnt = 0;
				uint64 UINT32_MAX_VALUE = 0x00000000ffffffff;
				{
					// get MC netif counter (care rx only)
					// policer counter is read clear, stored in HW after read
					aal_l3_te_pm_policer_t l3_pm_data;
					aal_l3_te_pm_policer_flow_get(G3_DEF_DEVID, hwIdx + G3_FLOW_POLICER_IDXSHIFT_MC_NETIF_RXMIB, &l3_pm_data);
					fc_db.netifTbl[i].hwMcNetifMib.in_intf_mc_packet_cnt += l3_pm_data.total_pkt;
					fc_db.netifTbl[i].hwMcNetifMib.in_intf_mc_byte_cnt += l3_pm_data.total_bytes;
				}
				{
					
					int cnt;
					int stackingCount;
					int32 p_fcDevIdx[DEV_STACK_MAX]={0};

					stackingCount=rtk_fc_psIfidxStackKey_to_fcDevIdx(fc_db.netifTbl[i].psIfidxStackKey,p_fcDevIdx);
					
					for(cnt=0;cnt < stackingCount;cnt++)
					{
						if(fc_db.devGwMacTbl[p_fcDevIdx[cnt]].dev)
						{
							//Just take the first one's data
							rx_uc_pkt_wrapRnd_cnt = fc_db.devGwMacTbl[p_fcDevIdx[cnt]].netifmib_wrapRnd_cnt.rx_uc_pkt_wrap_cnt;
							rx_nuc_pkt_wrapRnd_cnt = fc_db.devGwMacTbl[p_fcDevIdx[cnt]].netifmib_wrapRnd_cnt.rx_nuc_pkt_wrap_cnt;
							rx_drop_pkt_wrapRnd_cnt = fc_db.devGwMacTbl[p_fcDevIdx[cnt]].netifmib_wrapRnd_cnt.rx_drop_pkt_wrap_cnt;
							rx_error_pkt_wrapRnd_cnt = fc_db.devGwMacTbl[p_fcDevIdx[cnt]].netifmib_wrapRnd_cnt.rx_error_pkt_wrap_cnt;
							tx_uc_pkt_wrapRnd_cnt = fc_db.devGwMacTbl[p_fcDevIdx[cnt]].netifmib_wrapRnd_cnt.tx_uc_pkt_wrap_cnt;
							tx_nuc_pkt_wrapRnd_cnt = fc_db.devGwMacTbl[p_fcDevIdx[cnt]].netifmib_wrapRnd_cnt.tx_nuc_pkt_wrap_cnt;
							tx_drop_pkt_wrapRnd_cnt = fc_db.devGwMacTbl[p_fcDevIdx[cnt]].netifmib_wrapRnd_cnt.tx_drop_pkt_wrap_cnt;
							break;
						}
					}
				}
				rtlglue_printf("  \t--HW--\n");
				rtlglue_printf("  \tIgr pkt count : uc(%llu),\tmc(%llu),\t(drop count: %llu, csum err: %llu)\n", ((long long unsigned int)rx_cntr.uc_pkt + (long long unsigned int)rx_cntr.nuc_pkt) + (long long unsigned int)(rx_uc_pkt_wrapRnd_cnt * UINT32_MAX_VALUE)+ (long long unsigned int)(rx_nuc_pkt_wrapRnd_cnt * UINT32_MAX_VALUE), fc_db.netifTbl[i].hwMcNetifMib.in_intf_mc_packet_cnt, (long long unsigned int)rx_cntr.drop_pkt + (long long unsigned int)(rx_drop_pkt_wrapRnd_cnt * UINT32_MAX_VALUE) , (long long unsigned int)rx_cntr.error_pkt+ (long long unsigned int)(rx_error_pkt_wrapRnd_cnt * UINT32_MAX_VALUE));
				rtlglue_printf("  \tIgr byte count: uc(%llu),\tmc(%llu),\t(drop byte count: %llu)\n", rx_cntr.byte + (4 * (rx_cntr.uc_pkt + rx_cntr.nuc_pkt + (long long unsigned int)(rx_uc_pkt_wrapRnd_cnt * UINT32_MAX_VALUE)+ (long long unsigned int)(rx_nuc_pkt_wrapRnd_cnt * UINT32_MAX_VALUE) ))/*CRC*/, fc_db.netifTbl[i].hwMcNetifMib.in_intf_mc_byte_cnt, rx_cntr.drop_byte + (4 * (rx_cntr.drop_pkt + (long long unsigned int)(rx_drop_pkt_wrapRnd_cnt * UINT32_MAX_VALUE)) ));
				rtlglue_printf("  \tEgr pkt count : uc(%llu),\tmc(%llu),\t(drop count: %llu)\n", ((long long unsigned int)tx_cntr.uc_pkt + (long long unsigned int)tx_cntr.nuc_pkt + (long long unsigned int)(tx_uc_pkt_wrapRnd_cnt * UINT32_MAX_VALUE)+ (long long unsigned int)(tx_nuc_pkt_wrapRnd_cnt * UINT32_MAX_VALUE)), (long long unsigned int)0x0, (long long unsigned int)tx_cntr.drop_pkt + (long long unsigned int)(tx_drop_pkt_wrapRnd_cnt * UINT32_MAX_VALUE));
				rtlglue_printf("  \tEgr byte count: uc(%llu),\tmc(%llu),\t(drop byte count: %llu)\n", tx_cntr.byte + (4 * (tx_cntr.uc_pkt + tx_cntr.nuc_pkt + (long long unsigned int)(tx_uc_pkt_wrapRnd_cnt * UINT32_MAX_VALUE)+ (long long unsigned int)(tx_nuc_pkt_wrapRnd_cnt * UINT32_MAX_VALUE))), (long long unsigned int)0x0, tx_cntr.drop_byte + (4 * (tx_cntr.drop_pkt + (long long unsigned int)(tx_drop_pkt_wrapRnd_cnt * UINT32_MAX_VALUE))));
			

			}
				
#else
			rtlglue_printf("  \t--HW--\n");
			rtlglue_printf("  \tIgr pkt count : %llu, drop count: %llu, csum err: %llu\n", ((long long unsigned int)rx_cntr.uc_pkt + (long long unsigned int)rx_cntr.nuc_pkt), (long long unsigned int)rx_cntr.drop_pkt, (long long unsigned int)rx_cntr.error_pkt);
			rtlglue_printf("  \tIgr byte count: %llu drop byte count: %llu\n", rx_cntr.byte + (4 * (rx_cntr.uc_pkt + rx_cntr.nuc_pkt))/*CRC*/, rx_cntr.drop_byte + (4 * rx_cntr.drop_pkt));
			rtlglue_printf("  \tEgr pkt count : %llu, drop count: %llu\n", ((long long unsigned int)tx_cntr.uc_pkt + (long long unsigned int)tx_cntr.nuc_pkt), (long long unsigned int)tx_cntr.drop_pkt);
			rtlglue_printf("  \tEgr byte count: %llu drop byte count: %llu\n", tx_cntr.byte + (4 * (tx_cntr.uc_pkt + tx_cntr.nuc_pkt)), tx_cntr.drop_byte + (4 * tx_cntr.drop_pkt));
#endif
			rtlglue_printf("  \t--SW--\n");
			rtlglue_printf("  \tIgr pkt count : uc(%llu),\tmc(%llu)\n", _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_UC_PKT_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_MC_PKT_CNT));
			rtlglue_printf("  \tIgr byte count: uc(%llu),\tmc(%llu)\n", _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_UC_BYTE_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, IN_MC_BYTE_CNT));
			rtlglue_printf("  \tEgr pkt count : uc(%llu),\tmc(%llu)\n", _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_UC_PKT_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_MC_PKT_CNT));
			rtlglue_printf("  \tEgr byte count: uc(%llu),\tmc(%llu)\n", _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_UC_BYTE_CNT), _rtk_fc_sw_netifMib_all_cpu_get( hwIdx, OUT_MC_BYTE_CNT));
#endif
		}

	}
	return retval;

}
int32 dump_flow_mib(struct seq_file *s, void *v)
{
	uint32 idx=0U;
	rtk_rg_err_code_t retval=0;
	rtk_fc_flowOrHostmib_counter_t *pFlowMIB = RTK_FC_HELPER_FC_KMALLOC(sizeof(rtk_fc_flowOrHostmib_counter_t), GFP_ATOMIC);
	

	PROC_PRINTF(">>ASIC Flow MIB Table: (Show Non Zero Entries)\n");
	PROC_PRINTF("     [Ingress]        \t[Egress]\r\n");
	PROC_PRINTF("     pktCnt  byteCnt\tpktCnt  byteCnt\r\n");
	for(idx=0; idx<RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode); idx++)
	{
		_rtk_fc_hwFlowMib_get(idx, pFlowMIB); // hw only

		// skip empty entries
		if(!(pFlowMIB->in_packet_cnt||pFlowMIB->in_byte_cnt||pFlowMIB->out_packet_cnt||pFlowMIB->out_byte_cnt))
			continue;

		PROC_PRINTF("[%2d] %-6u  %-7llu\t%-6u  %-7llu\r\n", idx, pFlowMIB->in_packet_cnt, pFlowMIB->in_byte_cnt, pFlowMIB->out_packet_cnt, pFlowMIB->out_byte_cnt);
	}
	PROC_PRINTF("----------------------------------------------\n");

	PROC_PRINTF("Usage: Reset mib by \"echo [idx] > /proc/fc/[PATH]/[NAME]\"\r\n");

	RTK_FC_HELPER_FC_KFREE(pFlowMIB,sizeof(rtk_fc_flowOrHostmib_counter_t));

	return retval;
}

int32 dump_sw_flow_mib(struct seq_file *s, void *v)
{
	uint32 idx=0U;
	rtk_rg_err_code_t retval=0;
	rtk_fc_flowOrHostmib_counter_t flowGroupMib;

	PROC_PRINTF(">>Flow SW MIB Table: \n");
	PROC_PRINTF("     [Ingress]        \t[Egress]\r\n");
	PROC_PRINTF("     pktCnt  byteCnt\tpktCnt  byteCnt\r\n");
	for(idx = 0 ; idx < RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode) ; idx++)
	{
		flowGroupMib.in_packet_cnt = _rtk_fc_sw_flowMib_all_cpu_get(idx, IN_PKT_CNT);
		flowGroupMib.in_byte_cnt = _rtk_fc_sw_flowMib_all_cpu_get(idx, IN_BYTE_CNT);
		flowGroupMib.out_packet_cnt = _rtk_fc_sw_flowMib_all_cpu_get(idx, OUT_PKT_CNT);
		flowGroupMib.out_byte_cnt = _rtk_fc_sw_flowMib_all_cpu_get(idx, OUT_BYTE_CNT);
	
		// skip empty entries
		if(!(flowGroupMib.in_packet_cnt || flowGroupMib.in_byte_cnt ||flowGroupMib.out_packet_cnt || flowGroupMib.out_byte_cnt))
			continue;

		PROC_PRINTF("[%2d] %-6u  %-7llu\t%-6u  %-7llu\r\n", idx, flowGroupMib.in_packet_cnt, flowGroupMib.in_byte_cnt, flowGroupMib.out_packet_cnt, flowGroupMib.out_byte_cnt);
	}
	PROC_PRINTF("----------------------------------------------\n");

	PROC_PRINTF("Usage: Reset mib by \"echo [idx] > /proc/fc/[PATH]/[NAME]\"\r\n");

	return retval;
}

int32 dump_flow_mib2(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	uint32 idx=0U;
	rtk_fc_flowOrHostmib_counter_t *pFlowMIB = RTK_FC_HELPER_FC_KMALLOC(sizeof(rtk_fc_flowOrHostmib_counter_t), GFP_ATOMIC);
	

	PROC_PRINTF(">>ASIC Flow MIB2 Table: (Show Non Zero Entries)\n");
	PROC_PRINTF("     [Ingress]        \t[Egress]\r\n");
	PROC_PRINTF("     pktCnt  byteCnt\tpktCnt  byteCnt\r\n");
	for(idx=0; idx<RT_FLOW_MIB2_SIZE_GET(fc_db.controlFuc.flow_mib_mode); idx++)
	{
		_rtk_fc_hwFlowMib2_get(idx, pFlowMIB); // hw only

		// skip empty entries
		if(!(pFlowMIB->in_packet_cnt||pFlowMIB->in_byte_cnt||pFlowMIB->out_packet_cnt||pFlowMIB->out_byte_cnt))
			continue;

		PROC_PRINTF("[%2d] %-6u  %-7llu\t%-6u  %-7llu\r\n", idx, pFlowMIB->in_packet_cnt, pFlowMIB->in_byte_cnt, pFlowMIB->out_packet_cnt, pFlowMIB->out_byte_cnt);
	}
	PROC_PRINTF("----------------------------------------------\n");

	PROC_PRINTF("Usage: Reset mib by \"echo [idx] > /proc/fc/[PATH]/[NAME]\"\r\n");

	RTK_FC_HELPER_FC_KFREE(pFlowMIB,sizeof(rtk_fc_flowOrHostmib_counter_t));
#else
	WARNING("Not support");
#endif
	return retval;
}

int32 dump_sw_flow_mib2(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	uint32 idx=0U;
	rtk_fc_flowOrHostmib_counter_t flowGroupMib;

	PROC_PRINTF(">>Flow SW MIB2 Table: \n");
	PROC_PRINTF("     [Ingress]        \t[Egress]\r\n");
	PROC_PRINTF("     pktCnt  byteCnt\tpktCnt  byteCnt\r\n");
	for(idx = 0 ; idx < RT_FLOW_MIB2_SIZE_GET(fc_db.controlFuc.flow_mib_mode) ; idx++)
	{
		flowGroupMib.in_packet_cnt = _rtk_fc_sw_flowMib2_all_cpu_get(idx, IN_PKT_CNT);
		flowGroupMib.in_byte_cnt = _rtk_fc_sw_flowMib2_all_cpu_get(idx, IN_BYTE_CNT);
		flowGroupMib.out_packet_cnt = _rtk_fc_sw_flowMib2_all_cpu_get(idx, OUT_PKT_CNT);
		flowGroupMib.out_byte_cnt = _rtk_fc_sw_flowMib2_all_cpu_get(idx, OUT_BYTE_CNT);
	
		// skip empty entries
		if(!(flowGroupMib.in_packet_cnt || flowGroupMib.in_byte_cnt ||flowGroupMib.out_packet_cnt || flowGroupMib.out_byte_cnt))
			continue;

		PROC_PRINTF("[%2d] %-6u  %-7llu\t%-6u  %-7llu\r\n", idx, flowGroupMib.in_packet_cnt, flowGroupMib.in_byte_cnt, flowGroupMib.out_packet_cnt, flowGroupMib.out_byte_cnt);
	}
	PROC_PRINTF("----------------------------------------------\n");

	PROC_PRINTF("Usage: Reset mib by \"echo [idx] > /proc/fc/[PATH]/[NAME]\"\r\n");
#else
	WARNING("Not support");
#endif
	return retval;
}


int rtk_fc_dump_collisionFlows(rtk_fc_tableFlowEntry_t *pOriFlow,rtk_fc_tableFlowEntry_t *pNewFlow)
{
	// show patterns
	int oriPathNum = _rtk_fc_flowPath_get(pOriFlow);
	int newPathNum = _rtk_fc_flowPath_get(pNewFlow);

	if(oriPathNum==6){
		FIXME("ori: path[%d] sip[0x%x] dip[0x%x] stagif[%d] ctagif[%d]", oriPathNum, pOriFlow->path6.in_src_ipv4_addr, pOriFlow->path6.in_dst_ipv4_addr, pOriFlow->path6.in_stagif, pOriFlow->path6.in_ctagif);
	}else if(oriPathNum<=2){
		FIXME("ori: path[%d] saidx[%d] daidx[%d] svlan[%d] cvlan[%d]", oriPathNum, pOriFlow->path1.in_smac_lut_idx, pOriFlow->path1.in_dmac_lut_idx, pOriFlow->path1.in_svlan_id, pOriFlow->path1.in_cvlan_id);
	}else{
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		FIXME("ori: path[%d] sip[0x%x] dip[0x%x] sport[%d] dport[%d] protocol[0x%2x]", oriPathNum, pOriFlow->path5.in_src_ipv4_addr, pOriFlow->path5.in_dst_ipv4_addr, pOriFlow->path5.in_l4_src_port, pOriFlow->path5.in_l4_dst_port, pOriFlow->path5.in_l4proto_num);
#else
		FIXME("ori: path[%d] sip[0x%x] dip[0x%x] sport[%d] dport[%d] protocol[%d]", oriPathNum, pOriFlow->path5.in_src_ipv4_addr, pOriFlow->path5.in_dst_ipv4_addr, pOriFlow->path5.in_l4_src_port, pOriFlow->path5.in_l4_dst_port, pOriFlow->path5.in_l4proto);
#endif
	}


	if(newPathNum==6){
		FIXME("new: path[%d] sip[0x%x] dip[0x%x] stagif[%d] ctagif[%d]", newPathNum, pNewFlow->path6.in_src_ipv4_addr, pNewFlow->path6.in_dst_ipv4_addr, pNewFlow->path6.in_stagif, pNewFlow->path6.in_ctagif);
	}else if(newPathNum<=2){
		FIXME("new: path[%d] saidx[%d] daidx[%d] svlan[%d] cvlan[%d]", newPathNum, pNewFlow->path1.in_smac_lut_idx, pNewFlow->path1.in_dmac_lut_idx, pNewFlow->path1.in_svlan_id, pNewFlow->path1.in_cvlan_id);
	}else{
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
		FIXME("new: path[%d] sip[0x%x] dip[0x%x] sport[%d] dport[%d] protocol[0x%2x]", newPathNum, pNewFlow->path5.in_src_ipv4_addr, pNewFlow->path5.in_dst_ipv4_addr, pNewFlow->path5.in_l4_src_port, pNewFlow->path5.in_l4_dst_port, pNewFlow->path5.in_l4proto_num);
#else
		FIXME("new: path[%d] sip[0x%x] dip[0x%x] sport[%d] dport[%d] protocol[%d]", newPathNum, pNewFlow->path5.in_src_ipv4_addr, pNewFlow->path5.in_dst_ipv4_addr, pNewFlow->path5.in_l4_src_port, pNewFlow->path5.in_l4_dst_port, pNewFlow->path5.in_l4proto);
#endif
	}

	return SUCCESS;
}



int dump_abstrSwflowType_table(struct seq_file *s, void *v,int type)
{
	rtk_fc_abstrSwFlowType_entry_t *flowType;	
	if(!fc_db.abstrSwFlowType[type].valid)
		return SUCCESS;
	
	flowType=&fc_db.abstrSwFlowType[type];

	PROC_PRINTF("flowType[%d] :\n\t",type);
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_PHY_LSPID))
		PROC_PRINTF("LSPID / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_PHY_STREAMID))
		PROC_PRINTF("STREAMID / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_DMAC))
		PROC_PRINTF("DMAC / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SMAC))
		PROC_PRINTF("SMAC / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_ETH))
		PROC_PRINTF("ETH / ");	
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CVLANTAG))
		PROC_PRINTF("CVLANTAG / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CVLAN_TPID))
		PROC_PRINTF("CVLANTPID / ");	
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CVLAN))
		PROC_PRINTF("CVLAN / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CPRI))
		PROC_PRINTF("CPRI / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CDEI))
		PROC_PRINTF("CDEI / ");	
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SVLANTAG))
		PROC_PRINTF("SVLANTAG / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SVLAN_TPID))
		PROC_PRINTF("SVLANTPID / ");		
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SVLAN))
		PROC_PRINTF("SVLAN / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SPRI))
		PROC_PRINTF("SPRI / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SDEI))
		PROC_PRINTF("SDEI / ");	
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_PPPOETAG))
		PROC_PRINTF("PPPOETAG / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_PPPOESID))
		PROC_PRINTF("PPPOESID / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_IP_VLD))
		PROC_PRINTF("IP_VLD / ");	
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_IPVER46))
		PROC_PRINTF("IPVER46 / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_DIP))
		PROC_PRINTF("DIP / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_SIP))
		PROC_PRINTF("SIP / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_TOS))
		PROC_PRINTF("TOS / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L4_PROTO))
		PROC_PRINTF("PROTO / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L4_DPORT))
		PROC_PRINTF("DPORT / ");
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L4_SPORT))
		PROC_PRINTF("SPORT / ");
	PROC_PRINTF("\n");
	return SUCCESS;

}

extern uint16 patternSizeMapping[FLOW_PATTERN_MAX];	
int dump_abstrSwFlowPatten_table(struct seq_file *s, void *v,rtk_fc_abstrSwFlowList_entry_t *pAbstrSwFlowEt)
{

	rtk_fc_abstrSwFlowType_entry_t *flowType;
	uint8 isIpv6;
	rtk_fc_abstrSwFlowPattenField_entry_t pattenField;

	if(pAbstrSwFlowEt==NULL)
	{
		PROC_PRINTF("pAbstrSwFlowEt=NULL Point Error \n");
		return FAIL;
	}

	isIpv6 =pAbstrSwFlowEt->swFlowKey.bits.isIpv6;
	flowType=&fc_db.abstrSwFlowType[pAbstrSwFlowEt->swFlowKey.bits.flowtype];
	bzero(&pattenField,sizeof(pattenField));
	PROC_PRINTF("\t*PATTERN: \n");
	PROC_PRINTF("\t  abstrSwFlowType:%d ",pAbstrSwFlowEt->swFlowKey.bits.flowtype);
	PROC_PRINTF("ctagif:%d ",pAbstrSwFlowEt->swFlowKey.bits.ctagif);
	PROC_PRINTF("stagif:%d ",pAbstrSwFlowEt->swFlowKey.bits.stagif);
	PROC_PRINTF("pppoetagif:%d ",pAbstrSwFlowEt->swFlowKey.bits.pppoetagif);
	PROC_PRINTF("isIpv6:%d ",pAbstrSwFlowEt->swFlowKey.bits.isIpv6);
	PROC_PRINTF("\n\t");

	rtk_fc_parseAbstrSwPattenField(pAbstrSwFlowEt->swFlowKey.bits.isIpv6,&pAbstrSwFlowEt->swFlowKey,&pattenField);


	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_PHY_LSPID))
	{
		PROC_PRINTF("  LSPID:%d ",*pattenField.pLspid);
	}
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_PHY_STREAMID))
	{
		PROC_PRINTF("  STREAMID:%d ",*pattenField.pPonStreaId);		
	}
	PROC_PRINTF("\n\t");
	
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_DMAC))
	{

		PROC_PRINTF("  DMAC:%pM ",pattenField.pDmac);
		PROC_PRINTF("\n\t");
	}
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SMAC))
	{

		PROC_PRINTF("  SMAC:%pM ",pattenField.pSmac);
		PROC_PRINTF("\n\t");
	}
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_ETH))
	{

		PROC_PRINTF("  Ethtype:0x%04x ",*pattenField.pEth);
		PROC_PRINTF("\n\t");
	}

	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CVLANTAG))
	{

		PROC_PRINTF("  CVLANTAG / ");
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CVLAN_TPID))
		{
			PROC_PRINTF("CVLAN_TPID:0x%x / ",(*pattenField.pCvlanTPID));
		}
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CVLAN))
		{
			PROC_PRINTF("CVLAN:%d / ",(*pattenField.pCvlanTCI)&VLAN_VID_MASK);
		}
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CPRI))
		{
			PROC_PRINTF("CPRI:%d",((*pattenField.pCvlanTCI)&VLAN_PRIO_MASK)>>VLAN_PRIO_SHIFT);
		}
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_CDEI))
		{
			PROC_PRINTF("CDEI:%d",((*pattenField.pCvlanTCI)&VLAN_CFI_MASK)>>VLAN_CFI_SHIFT);
		}		
		PROC_PRINTF("\n\t");
	}

	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SVLANTAG))
	{

		PROC_PRINTF("  SVLANTAG / ");
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SVLAN_TPID))
		{
			PROC_PRINTF("SVLAN_TPID:0x%x / ",(*pattenField.pSvlanTPID));
		}	
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SVLAN))
		{
			PROC_PRINTF("SVLAN:%d / ",(*pattenField.pSvlanTCI)&VLAN_VID_MASK);
		}
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SPRI))
		{
			PROC_PRINTF("SPRI:%d",((*pattenField.pSvlanTCI)&VLAN_PRIO_MASK)>>VLAN_PRIO_SHIFT);
		}
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SDEI))
		{
			PROC_PRINTF("SDEI:%d",((*pattenField.pSvlanTCI)&VLAN_CFI_MASK)>>VLAN_CFI_SHIFT);
		}		
		PROC_PRINTF("\n\t");
	}

	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_PPPOETAG))
	{
		PROC_PRINTF("  PPPOETAG / ");
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_PPPOESID))
		{		
			PROC_PRINTF("PPPOESID:%d ",(*pattenField.pPppoeSid));
		}
		PROC_PRINTF("\n\t");
	}

	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_IP_VLD))
	{
		PROC_PRINTF("  IP_VLD: %d/ ",pAbstrSwFlowEt->swFlowKey.bits.ip_vld);
	}
	
	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_IPVER46))
	{
		PROC_PRINTF("  IPVER46: %s/ ",isIpv6?"v6":"v4");
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_DIP))
		{

			if(isIpv6)
				PROC_PRINTF(" DIPv6Hash:%x / ",(pattenField.pDip ? *pattenField.pDip : 0));
				//PROC_PRINTF("DIP:%pI6 ",(pattenField.pDip));
			else
				PROC_PRINTF(" DIP:%pI4h / ",(pattenField.pDip));
		}
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_SIP))
		{

			if(isIpv6)
				PROC_PRINTF(" SIPv6Hash:%x / ",(pattenField.pSip ? *pattenField.pSip : 0));
				//PROC_PRINTF("SIP:%pI6 ",(pattenField.pSip));
			else
				PROC_PRINTF("SIP:%pI4h ",(pattenField.pSip));
		}
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_TOS))
		{
		
			PROC_PRINTF("TOS:%x / ",(*pattenField.pTos));
		}
		PROC_PRINTF("\n\t");
	}

	if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L4_PROTO))
	{
		
		PROC_PRINTF("  PROTO:%x / ",(*pattenField.pL4Porto));

		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L4_DPORT))
		{

			PROC_PRINTF("DPORT:%d / ",(*pattenField.pL4Dport));
		}
		if(flowType->patternFlagMsk & TYPETOMSK(FLOW_L4_SPORT))
		{
			PROC_PRINTF("SPORT:%d ",(*pattenField.pL4Sport));
		}
		PROC_PRINTF("\n\t");
	}
	PROC_PRINTF("\n");

#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	PROC_PRINTF("\t*KeyPrivateField [hwacc:%d hwHashIdx:%d swMcgid:%d hwMcgid:%d]\n",pAbstrSwFlowEt->g3KeyHwPriv.hwacc,pAbstrSwFlowEt->g3KeyHwPriv.hwHashIdx,pAbstrSwFlowEt->g3KeyHwPriv.swMcgid,pAbstrSwFlowEt->g3KeyHwPriv.hwMcgid);
#else
	PROC_PRINTF("\t*KeyPrivateField [hwacc:%d path13Idx=%d]\n",pAbstrSwFlowEt->xponKeyHwPriv.hwacc,pAbstrSwFlowEt->xponKeyHwPriv.flowP13hwIdx);
#endif


	PROC_PRINTF("\t*ACTION: \n");
	if(pAbstrSwFlowEt->copy2cpu)
		PROC_PRINTF("\t--COPY to CPU FLOW-- \n");

	{
		rtk_fc_abstrSwFlowActionList_entry_t *pTmpSwFlowAction;
		rtk_fc_abstrSwFlowLdpid_entry_t *pLdpid=NULL;
		uint32 cnt=0;
		rtk_fc_abstrSwFlowActionField_entry_t field;

		list_for_each_entry(pTmpSwFlowAction, &pAbstrSwFlowEt->swFlowActionHdr, swFlowActionList)
		{
			PROC_PRINTF("\t  ACTION[%d]: \n\t  ",cnt);
			PROC_PRINTF("\t  IgrIntf:%d EgrIntf:%d \n\t  ",pTmpSwFlowAction->igrIntf,pTmpSwFlowAction->egrIntf);
			if(pTmpSwFlowAction->flowMibEn)
				PROC_PRINTF("\t  flowMibEn:%d meter_idx:%d \n\t  ",pTmpSwFlowAction->flowMibEn,pTmpSwFlowAction->flowMibIdx);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			if(pTmpSwFlowAction->flowMib2En)
				PROC_PRINTF("\t  flowMib2En:%d mib2_idx:%d \n\t  ",pTmpSwFlowAction->flowMibEn,pTmpSwFlowAction->flowMib2Idx);
#endif
			rtk_fc_parseAbstrSwActionField(isIpv6,&pTmpSwFlowAction->swFlowAction,&field);

			
			if(pTmpSwFlowAction->swFlowAction.bits.dmacTrans)
				PROC_PRINTF(" dmacTrans: %pM /",field.pDmac);
			if(pTmpSwFlowAction->swFlowAction.bits.smacTrans)
				PROC_PRINTF(" smacTrans: %pM /",field.pSmac);

			if(pTmpSwFlowAction->swFlowAction.bits.stagCmd==SWFLOW_EGACT_UNTAG)
				PROC_PRINTF(" stagCmd:UNTAG /");
			else
				PROC_PRINTF(" stagCmd:TAG TPID:0x%x Vid=%d Vpri=%d Vdei=%d /",(*field.pSvlanTpid),(*field.pSvlanTCI)&VLAN_VID_MASK,((*field.pSvlanTCI)&VLAN_PRIO_MASK)>>VLAN_PRIO_SHIFT,((*field.pSvlanTCI)&VLAN_CFI_MASK)>>VLAN_CFI_SHIFT);

			if(pTmpSwFlowAction->swFlowAction.bits.ctagCmd==SWFLOW_EGACT_UNTAG)
				PROC_PRINTF(" ctagCmd:UNTAG /");
			else
				PROC_PRINTF(" ctagCmd:TAG TPID:0x%x Vid=%d Vpri=%d Vdei=%d /",(*field.pCvlanTpid),(*field.pCvlanTCI)&VLAN_VID_MASK,((*field.pCvlanTCI)&VLAN_PRIO_MASK)>>VLAN_PRIO_SHIFT,((*field.pCvlanTCI)&VLAN_CFI_MASK)>>VLAN_CFI_SHIFT);
			
			if(pTmpSwFlowAction->swFlowAction.bits.pppoeCmd==SWFLOW_EGACT_UNTAG)
				PROC_PRINTF(" pppoeCmd:UNTAG /");
			else if (pTmpSwFlowAction->swFlowAction.bits.pppoeCmd==SWFLOW_EGACT_TAG)
				PROC_PRINTF(" pppoeCmd:TAG  SESSIONID:%d /",*field.pPppoeSid);
			if(pTmpSwFlowAction->swFlowAction.bits.dipTrans)
			{
				if(isIpv6)
					PROC_PRINTF(" dipTrans  DIP:%pI6 /",field.pDip);
				else
					PROC_PRINTF(" dipTrans  DIP:%pI4 /",field.pDip);
			}
			if(pTmpSwFlowAction->swFlowAction.bits.sipTrans)
			{
				if(isIpv6)
					PROC_PRINTF(" sipTrans  SIP:%pI6 /",field.pSip);
				else
					PROC_PRINTF(" sipTrans  SIP:%pI4 /",field.pSip);
			}
			if(pTmpSwFlowAction->swFlowAction.bits.dscpCmd)
				PROC_PRINTF(" dscpCmd: DSPC:%d /",((*field.pTos)&FC_IP_DSCP_MASK)>>FC_IP_DSCP_SHIFT);
			if(pTmpSwFlowAction->swFlowAction.bits.ecnCmd)
				PROC_PRINTF(" ecnCmd: ENC:%d /",((*field.pTos)&FC_IP_ECN_MASK));
			if(pTmpSwFlowAction->swFlowAction.bits.l4dportCmd)
				PROC_PRINTF(" l4dportCmd  DPORT:%d /",*field.pL4Dport);
			if(pTmpSwFlowAction->swFlowAction.bits.l4sportCmd)
				PROC_PRINTF(" l4sportCmd  SPORT:%d /",*field.pL4Sport);
			if(pTmpSwFlowAction->swFlowAction.bits.userPriCmd)
				PROC_PRINTF(" userPriCmd USERPRI:%d /",*field.pUserPri);
			if(pTmpSwFlowAction->swFlowAction.bits.ponStreamIdCmd)
				PROC_PRINTF(" ponStreamIdCmd  STREAMID:%d /",*field.pPonStreaId);
			PROC_PRINTF("\n\t");	
			
			PROC_PRINTF(" LDPID LIST: \n\t  ");
			list_for_each_entry(pLdpid, &pTmpSwFlowAction->ldpidListHdr, ldpidList)
			{
				if(pLdpid->isWlan)
					PROC_PRINTF(" wlanldpid:%d(devIfIdx=%d) ",pLdpid->flowLdpid,pLdpid->devIfIdx);
				else
					PROC_PRINTF(" ldpid:%d(devIfIdx=%d) ",pLdpid->flowLdpid,pLdpid->devIfIdx);
				if(pLdpid->ldpidPriEn)
					PROC_PRINTF(" ldpidPri:%d ",pLdpid->ldpidPri);
				if(pLdpid->igrShorcutSendbit)
					PROC_PRINTF(" IgrSC");
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
				if(pLdpid->g3PortPriv.hwFibHashAcc)
					PROC_PRINTF(" hwFibHashIdx:%d ",pLdpid->g3PortPriv.hwFibHashIdx);
				if(pLdpid->g3PortPriv.swMacIdEn)
					PROC_PRINTF(" swMacId:%d ",pLdpid->g3PortPriv.swMacId);
				if(pLdpid->g3PortPriv.hwFibHashAcc_tmp)
					PROC_PRINTF(" hwFibHashIdx_tmp:%d ",pLdpid->g3PortPriv.hwFibHashIdx_tmp);
				if(pLdpid->g3PortPriv.swMacIdEn_tmp)
					PROC_PRINTF(" swMacId_tmp:%d ",pLdpid->g3PortPriv.swMacId_tmp);
#endif
				PROC_PRINTF("/");

			}
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			PROC_PRINTF("\n\t *PrivateField [hwacc:%d hwIdx:%d isPath24:%d] /",pTmpSwFlowAction->xponActPriv.hwacc,pTmpSwFlowAction->xponActPriv.hwIdx,pTmpSwFlowAction->xponActPriv.isPath24);
#endif
			PROC_PRINTF("\n");	
			cnt++;

		}
	}


	return SUCCESS;

}

int _dump_abstrSwFlow_table(struct seq_file *s, void *v,int32 swidx,int32 flowType)
{

	
	if(swidx<fc_db.flowHwTableSize || fc_db.flowSwTableSize<swidx)
		return SUCCESS;

	if(!fc_db.flowTbl[swidx].pAbstrSwFlowEt)
		return SUCCESS;
	if( 0<flowType && flowType<ABSTR_SWFLOW_TYPE_SIZE && fc_db.flowTbl[swidx].pAbstrSwFlowEt->swFlowKey.bits.flowtype != flowType)
		return SUCCESS;

	PROC_PRINTF("["COLOR_Y"%d"COLOR_NM"]abstrSwFlow:\n",swidx);
	
	dump_abstrSwFlowPatten_table(s,v,fc_db.flowTbl[swidx].pAbstrSwFlowEt);

	return SUCCESS;
}


int dump_abstrSwFlow_table(struct seq_file *s, void *v)
{
	uint32 idx=0U,type=0U;

	for(type=0; type<ABSTR_SWFLOW_TYPE_SIZE; type++)
	{
		if(!fc_db.abstrSwFlowType[type].valid)
			continue;
		
		dump_abstrSwflowType_table(s,v,type);

		for(idx=fc_db.flowHwTableSize; idx<fc_db.flowSwTableSize; idx++)
		{
			if(!fc_db.flowTbl[idx].pAbstrSwFlowEt)
				continue;
			if(fc_db.flowTbl[idx].pAbstrSwFlowEt->swFlowKey.bits.flowtype != type)
				continue;

			_dump_abstrSwFlow_table(s,v,idx,type);
		}
	}
	return 0;
}


void dump_sw_flow_fields(struct seq_file *s, void *v, rtk_fc_tableFlow_t *pFlowTbl, uint32 flowIdx)
{
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	if(pFlowTbl->pFlowEntry->path3.in_path == FB_PATH_MC_WIFI_AMSDU_TX)
	{
		uint32 agingTime = 0U;
		rtk_rg_err_code_t retval=0;
		bool if_aging_miss = FALSE;

		if(flowIdx < fc_db.flowHwTableSize)
		{
			retval = rtk_rg_asic_flow_age_get(flowIdx, &agingTime);
			if((agingTime == 0U) && (fc_db.g3_mHashTbl_validBitsArray[(flowIdx >> 5)]&(0x1 << (flowIdx&0x1f))))
				if_aging_miss = TRUE;
		}

		PROC_PRINTF("mainHash Idx: %d, agingTime: %d %s\n", ((agingTime == 0U)&&(if_aging_miss==FALSE))?SIGNED_INVALID:flowIdx, agingTime, if_aging_miss?"(age-out miss)":"");
		PROC_PRINTF("CRC16: 0x%04x, CRC32: 0x%08x\n", pFlowTbl->crc16, pFlowTbl->crc32);
		PROC_PRINTF("\n");

		PROC_PRINTF("flow extra info: 0x%x\n", pFlowTbl->flow_extra_info);
		PROC_PRINTF("candidate: %d, lock: %u, idle: %u, static: %u, isMc:%d, swOnly: %u, skipCT: %d, aclPriFwd: %d, duplicate: %d\r\n",
			pFlowTbl->candidateState, pFlowTbl->pFlowEntry->path1.lock, pFlowTbl->idleSecs,
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_STATIC_ENTRY),
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_MC_ENTRY),
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_SOFTWARE_ONLY), 
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_SKIP_CT), 
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_ACL_PRI_FWD), 
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_DUPLICATE_EXIST));
		return;
	}
#endif
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	if(pFlowTbl->pFlowEntry->path3.in_path == FB_PATH_VXLAN_US_EXTRA_TX)
	{
		uint32 agingTime = 0U;
		rtk_rg_err_code_t retval=0;
		bool if_aging_miss = FALSE;

		if(flowIdx < fc_db.flowHwTableSize)
		{
			retval = rtk_rg_asic_flow_age_get(flowIdx, &agingTime);
			if((agingTime == 0U) && (fc_db.g3_mHashTbl_validBitsArray[(flowIdx >> 5)]&(0x1 << (flowIdx&0x1f))))
				if_aging_miss = TRUE;
		}

		PROC_PRINTF("mainHash Idx: %d, agingTime: %d %s\n", ((agingTime == 0U)&&(if_aging_miss==FALSE))?SIGNED_INVALID:flowIdx, agingTime, if_aging_miss?"(age-out miss)":"");
		PROC_PRINTF("CRC16: 0x%04x, CRC32: 0x%08x\n", pFlowTbl->crc16, pFlowTbl->crc32);
		PROC_PRINTF("\n");

		PROC_PRINTF("flow extra info: 0x%x\n", pFlowTbl->flow_extra_info);
		PROC_PRINTF("candidate: %d, lock: %u, idle: %u, static: %u, isMc:%d, swOnly: %u, skipCT: %d, aclPriFwd: %d, duplicate: %d\r\n",
			pFlowTbl->candidateState, pFlowTbl->pFlowEntry->path1.lock, pFlowTbl->idleSecs,
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_STATIC_ENTRY),
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_MC_ENTRY),
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_SOFTWARE_ONLY), 
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_SKIP_CT), 
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_ACL_PRI_FWD), 
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_DUPLICATE_EXIST));

		PROC_PRINTF("IgrSaIdx = %d, EgrDaIdx = %d\n", pFlowTbl->lutIgrSaIdx, pFlowTbl->lutEgrDaIdx);
		PROC_PRINTF("flow extra info: 0x%x, pppoe_sid: %d, pon_stream_id: %d\n", pFlowTbl->flow_extra_info, pFlowTbl->pppoe_sid, pFlowTbl->pon_stream_id);

		if(!fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_SPRI])
			PROC_PRINTF("svlan_pri: %u\n", pFlowTbl->svlan_pri);
		if(!fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_VLAN_DEICFI])
			PROC_PRINTF("svlan_dei: %u, cvlan_cfi: %u\n", pFlowTbl->igr_svlan_dei, pFlowTbl->igr_cvlan_cfi);
		if(fc_db.systemGlobal.stagTPID_en_cnt > 1)
			PROC_PRINTF("igr_stpid_sel: 0x%x\n", pFlowTbl->igr_stpid_sel);
		PROC_PRINTF("candidate: %d, lock: %u, idle: %u, static: %u, isMc:%d, swOnly: %u, skipCT: %d, aclPriFwd: %d, duplicate: %d\r\n",
			pFlowTbl->candidateState, pFlowTbl->pFlowEntry->pathVxlan_up_extra_tx.lock, pFlowTbl->idleSecs,
										FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_STATIC_ENTRY),
										FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_MC_ENTRY),
										FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_SOFTWARE_ONLY), 
										FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_SKIP_CT), 
										FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_ACL_PRI_FWD), 
										FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_DUPLICATE_EXIST));
		PROC_PRINTF("flow check SMAC by index \033[1;35m%d\033[0m, check DMAC by index \033[1;35m%d\033[0m\n", pFlowTbl->lutIgrSaIdx, pFlowTbl->lutEgrDaIdx);
		return;
	}
#endif
#endif
	{
		uint32 agingTime = 0U;
		rtk_rg_err_code_t retval=0;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		bool if_aging_miss = FALSE;

		if(flowIdx < fc_db.flowHwTableSize)
		{
			retval = rtk_rg_asic_flow_age_get(flowIdx, &agingTime);
			if((agingTime == 0U) && (fc_db.g3_mHashTbl_validBitsArray[(flowIdx >> 5)]&(0x1 << (flowIdx&0x1f))))
				if_aging_miss = TRUE;
		}

		PROC_PRINTF("mainHash Idx: %d, agingTime: %d %s\n", ((agingTime == 0U)&&(if_aging_miss==FALSE))?SIGNED_INVALID:flowIdx, agingTime, if_aging_miss?"(age-out miss)":"");
#else
		retval = ca_flow_age_get(G3_DEF_DEVID, pFlowTbl->mainHashIdx, &agingTime);
		PROC_PRINTF("mainHash Idx: %d, agingTime: %d\n", pFlowTbl->mainHashIdx, agingTime);
#endif

		if(pFlowTbl->ingressSaHostPolIdx != SIGNED_INVALID || pFlowTbl->egressDaHostPolIdx != SIGNED_INVALID)
			PROC_PRINTF("SMAC hostPolicing Idx: %d, DMAC hostPolicing Idx: %d\n", pFlowTbl->ingressSaHostPolIdx, pFlowTbl->egressDaHostPolIdx);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		PROC_PRINTF("forceDisDmaAft: %u\n", pFlowTbl->forceDisDmaAft);
#endif
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES)
		PROC_PRINTF("DMA AFT En: %d, FibIdx: %d\n", pFlowTbl->dmaAftFibCtrlEn, pFlowTbl->dmaAftFibIdx);
#endif
#if defined(CONFIG_FC_RTL9607F_SERIES)
		PROC_PRINTF("DMA AFT Map En: %d, MapIdx: %d(DMA AFT En: %d, FibIdx: %d)\n", pFlowTbl->dmaAftMapCtrlEn, pFlowTbl->dmaAftMapIdx, pFlowTbl->dmaAftMapCtrlEn?fc_db.dmaAftMapTbl[pFlowTbl->dmaAftMapIdx].aft_en:0, pFlowTbl->dmaAftMapCtrlEn?fc_db.dmaAftMapTbl[pFlowTbl->dmaAftMapIdx].aft_fib_idx:0);
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		PROC_PRINTF("CRC16: 0x%04x, CRC32: 0x%08x\n", pFlowTbl->crc16, pFlowTbl->crc32);
#if !defined(CONFIG_FC_RTL9607F_SERIES)
		//TODO: 9607F flow cache mib
		PROC_PRINTF("flow_cache_mib_en: %d, flow_cache_mib_idx_or_in_outerVlan: %d\n", 
						pFlowTbl->pFlowEntry->path1.flow_cache_mib_en, pFlowTbl->pFlowEntry->path1.flow_cache_mib_idx_or_in_outerVlan);
		if(pFlowTbl->pFlowEntry->path1.flow_cache_mib_en)
		{
			int aal_ret;
			l3fe_hash_aqm_flow_mib_tbl_entry_t aqm_flow_mib_entry;
			aal_ret = aal_l3fe_hash_aqm_flow_mib_entry_get(pFlowTbl->pFlowEntry->path1.flow_cache_mib_idx_or_in_outerVlan, &aqm_flow_mib_entry);
			if(aal_ret)
				PROC_PRINTF("Fail to get aqm flow mib entry[%d], ret = %d", pFlowTbl->pFlowEntry->path1.flow_cache_mib_idx_or_in_outerVlan, aal_ret);
			else
			{
				PROC_PRINTF("[flow_cache_mib] pktCnt : %7u\n", aqm_flow_mib_entry.pktCnt);
				PROC_PRINTF("[flow_cache_mib] byteCnt: %7u\n", aqm_flow_mib_entry.byteCnt);
			}
		}
#endif
#endif
		PROC_PRINTF("\n");
	}
#endif

	PROC_PRINTF("igr lutidx = %d, spa = %d, wlandevidx = %d\n", pFlowTbl->lutIgrSaIdx, pFlowTbl->igrspa, pFlowTbl->igrWlanDevIdx);
	PROC_PRINTF("egr lutidx = %d, dpa = %d, wlandevidx = %d\n", pFlowTbl->lutEgrDaIdx, pFlowTbl->egrdpa, pFlowTbl->egrWlanDevIdx);

	PROC_PRINTF("flow extra info: 0x%x, pppoe_sid: %d, pon_stream_id: %d\n", pFlowTbl->flow_extra_info, pFlowTbl->pppoe_sid, pFlowTbl->pon_stream_id);

	if(pFlowTbl->swShaperEn)
		PROC_PRINTF("swShaping: Flow %s, SMAC_HOST %s, DMAC_HOST %s\n", (pFlowTbl->swShaperEn & (1<<SWSHAPER_TYPE_FLOW))?"ON":"off",
																		(pFlowTbl->swShaperEn & (1<<SWSHAPER_TYPE_SMAC_HOST))?"ON":"off",
																		(pFlowTbl->swShaperEn & (1<<SWSHAPER_TYPE_DMAC_HOST))?"ON":"off");
	if(!fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_SPRI])
		PROC_PRINTF("svlan_pri: %u\n", pFlowTbl->svlan_pri);
	if(!fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATHALL_SKIP_VLAN_DEICFI])
		PROC_PRINTF("svlan_dei: %u, cvlan_cfi: %u\n", pFlowTbl->igr_svlan_dei, pFlowTbl->igr_cvlan_cfi);
	if(fc_db.systemGlobal.stagTPID_en_cnt > 1)
		PROC_PRINTF("igr_stpid_sel: 0x%x, egr_stpid_sel: 0x%x\n", pFlowTbl->igr_stpid_sel, pFlowTbl->egr_stpid_sel);
	if(pFlowTbl->egr_tos_ecn_remark)
		PROC_PRINTF("egr_tos_ecn_remark: %u, egr_tos_ecn: 0x%x\n", pFlowTbl->egr_tos_ecn_remark, pFlowTbl->egr_tos_ecn);

	if(pFlowTbl->is_TC_Flow)
		PROC_PRINTF("is_TC_Flow:%d \n",pFlowTbl->is_TC_Flow);
	
	PROC_PRINTF("candidate: %d, lock: %u, idle: %u, static: %u, isMc:%d, swOnly: %u, skipCT: %d, aclPriFwd: %d, duplicate: %d, force_to_bridge_flow: %d\r\n",
		pFlowTbl->candidateState, pFlowTbl->pFlowEntry->path1.lock, pFlowTbl->idleSecs,
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_STATIC_ENTRY),
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_MC_ENTRY),
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_SOFTWARE_ONLY), 
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_SKIP_CT), 
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_ACL_PRI_FWD), 
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_DUPLICATE_EXIST),
									FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_FORCE_TO_BRIDGE_FLOW));
	

	if(pFlowTbl->protoCtrl) {
		if((pFlowTbl->protoCtrl == FLOW_PROTO_CTRL_VXLAN_ADD) )
		{
			PROC_PRINTF("%s ", "VXLAN Upstream");
		}
		else if((pFlowTbl->protoCtrl == FLOW_PROTO_CTRL_VXLAN_REMOVE))
		{
			PROC_PRINTF("%s ", "VXLAN Downstream");

		}
		else
		{
			PROC_PRINTF("%s: ", name_of_flow_proto_ctrl[pFlowTbl->protoCtrl]);
		}
		PROC_PRINTF("protoCtrl: %d\n", pFlowTbl->protoCtrl);
			
		switch(pFlowTbl->protoCtrl) 
		{
			case FLOW_PROTO_CTRL_MAPE_ACC:
				PROC_PRINTF("mape_out_dst6_idx:%d mape_tun_ipv4_addr:%pI4h\n",pFlowTbl->mapeInfo.mape_out_dst6_idx,&pFlowTbl->mapeInfo.mape_tun_ipv4_addr);
				break;
			case FLOW_PROTO_CTRL_DUAL_PT:					
				PROC_PRINTF("dual_pt_flowMapTbl_idx: %d\n", pFlowTbl->dual_pt_flowMapTbl_idx);
				break;
			case FLOW_PROTO_CTRL_V6NAT:
				PROC_PRINTF("IPv6 NAT Table index: %d\n", pFlowTbl->ipv6_nat_indirect_mapping_index);
				break;
			case FLOW_PROTO_CTRL_LOOPBACK_ACC:
				PROC_PRINTF("loopbackRevFlowIdx: %d\n", pFlowTbl->loopbackRevFlowIdx);
				break;
			case FLOW_PROTO_CTRL_ICMP_ACC:
				PROC_PRINTF("ingress_icmp_id = %d, egress_icmp_id = %d, icmp_seq = %d\n", ntohs(pFlowTbl->icmpInfo.ingress_icmp_id), ntohs(pFlowTbl->icmpInfo.egress_icmp_id), ntohs(pFlowTbl->icmpInfo.icmp_seqence_num) );
				break;
			case FLOW_PROTO_CTRL_MAPT_ACC:
					PROC_PRINTF("IPv6 MAPT Table index:sipv6 = %d, dipv6 = %d\n", pFlowTbl->maptInfo.sip_indirect_mapping_index, pFlowTbl->maptInfo.dip_indirect_mapping_index);
				break;
			case FLOW_PROTO_CTRL_XLAT_ACC:
					PROC_PRINTF("IPv6 XLAT464 Table index:sipv6 = %d, dipv6 = %d\n", pFlowTbl->maptInfo.sip_indirect_mapping_index, pFlowTbl->maptInfo.dip_indirect_mapping_index);
				break;			
			case FLOW_PROTO_CTRL_VXLAN_ADD:
			case FLOW_PROTO_CTRL_VXLAN_REMOVE:
				break;
#if defined(CONFIG_RTK_FC_IPSEC_FASTFWD)			
			case FLOW_PROTO_CTRL_IPSEC_ENDPOINT:
				PROC_PRINTF("IPSEC Table index:%d\n", pFlowTbl->ipsec_flow_info.ipsec_shortCut_info_table_index);
				PROC_PRINTF("IPSEC spi:%x\n", pFlowTbl->ipsec_flow_info.spi);
				PROC_PRINTF("IPSEC ipsec_direction:%d\n", pFlowTbl->ipsec_flow_info.ipsec_direction);
				break;
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			case FLOW_PROTO_CTRL_VXLAN_EXTRA:
				PROC_PRINTF("VXLAN EXTRA flow\n");
				break;
#endif				
			default:
				PROC_PRINTF("ERROR\n");
				break;
		}
	}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)

	if(pFlowTbl->pFlowEntry->path3.in_path == FB_PATH_5)
	{
		if(!fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH345_SKIP_MAC])
			PROC_PRINTF("flow check SMAC by index \033[1;35m%d\033[0m, check DMAC by index \033[1;35m%d\033[0m\n", pFlowTbl->lutIgrSaIdx, fc_db.netifTbl[NETIF_HWTOSW(pFlowTbl->pFlowEntry->path5.in_intf_idx)].lutIdx);
	}
	else if(pFlowTbl->pFlowEntry->path3.in_path == FB_PATH_34)
	{
		if(!fc_db.systemGlobal.fbGlobalState[FB_GLOBAL_PATH345_SKIP_MAC])
			PROC_PRINTF("flow check SMAC by index \033[1;35m%d\033[0m, check DMAC by index \033[1;35m%d\033[0m\n", pFlowTbl->lutIgrSaIdx, pFlowTbl->lutEgrDaIdx);
		if(fc_db.controlFuc.bridge_5tuple_flow_keep_mac_addr_mode)
			PROC_PRINTF("bridge_5tuple_flow_keep_mac_addr_mode: 1\n");
	}
#endif
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	// TODO: CONFIG_RTK_FC_WIFI_MULTIBAND_ACC_BY_ONE_LUT
	if((pFlowTbl->lutEgrDaIdx!= SIGNED_INVALID) && fc_db.lutTbl[pFlowTbl->lutEgrDaIdx] && (fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask == 0))
	{
		uint mac_id = fc_db.lutTbl[pFlowTbl->lutEgrDaIdx]->wifiMacId;
		uint wlan_dev_idx = fc_db.lutTbl[pFlowTbl->lutEgrDaIdx]->wlan_dev_idx;
		if((fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_start <= mac_id) && (mac_id <= fc_db.wifi_amsdu_pe_offload_mode_info[fc_db.wifi_amsdu_pe_offload_mode].mac_id_end))
		{
			PROC_PRINTF("[TO WIFI STA] wifiMacId: %u, wlanDevIdx: %u, power_saving: %u", mac_id,wlan_dev_idx, fc_db.wifi_amsdu_pe_offload_mac_id_tbl[mac_id].sta_info.power_saving);
#if defined(CONFIG_RTK_FC_WIFI_DRIVER_OFFLOAD_BY_PE)
			PROC_PRINTF(" (wifi driver on PE, tx_sw_id: wifi_pri)\n");
#else
			PROC_PRINTF(" (wifi driver on ARM, tx_sw_id: wifi_pri)\n");
#endif
		}
	}

	if((pFlowTbl->lutIgrSaIdx != SIGNED_INVALID) && fc_db.lutTbl[pFlowTbl->lutIgrSaIdx] && ((1U << fc_db.lutTbl[pFlowTbl->lutIgrSaIdx]->spa) & fc_db.controlFuc.amsdu_pe_dbg_mode_rx_portmask) &&
			(pFlowTbl->lutEgrDaIdx != SIGNED_INVALID) && fc_db.lutTbl[pFlowTbl->lutEgrDaIdx] && ((1U << fc_db.lutTbl[pFlowTbl->lutEgrDaIdx]->spa) & fc_db.controlFuc.amsdu_pe_dbg_mode_tx_portmask))
	{
		if(FLOW_INFO_IS_SET(pFlowTbl, FLOW_INFO_US_TO_PON))
		{
			if(fc_db.streamidTbl[pFlowTbl->pon_stream_id].valid && ((1U << (fc_db.streamidTbl[pFlowTbl->pon_stream_id].ldpid-0x20)) & fc_db.controlFuc.amsdu_pe_dbg_mode_tx_tcontIdmask))
				PROC_PRINTF(COLOR_Y"This flow should in \"AMSDU debug mode\"\n"COLOR_NM);
		}
		else
			PROC_PRINTF(COLOR_Y"This flow should in \"AMSDU debug mode\"\n"COLOR_NM);
	}
#endif
}

int dump_sw_flow_table(struct seq_file *s, void *v)
{
	uint32 idx=0U;
	rtk_rg_asic_path1_entry_t p1Data;

	PROC_PRINTF(">>Software Flow Table:\n");
	for(idx=0U; idx<fc_db.flowSwTableSize; idx++)
	{

		if(fc_db.flowTbl[idx].pAbstrSwFlowEt)
		{
			if(idx < fc_db.flowHwTableSize)
			{
				PROC_PRINTF("\n["COLOR_Y"%d"COLOR_NM"]flow:\n",idx);
				PROC_PRINTF("##################################################\n");
				PROC_PRINTF("####### occupy by abstrSwMcFlow ["COLOR_Y"%d"COLOR_NM"] #########\n",fc_db.flowTbl[idx].pAbstrSwFlowEt->swFlowIdx);
				PROC_PRINTF("##################################################\n");
				dump_sw_flow_fields(s, v, &fc_db.flowTbl[idx], idx);
			}
			else
			{
				PROC_PRINTF("\n["COLOR_Y"%d"COLOR_NM"]abstrSwMcFlow:\n",idx);
				dump_abstrSwFlowPatten_table(s,v,fc_db.flowTbl[idx].pAbstrSwFlowEt);
				dump_sw_flow_fields(s, v, &fc_db.flowTbl[idx], idx);
			}
			continue;
		}

	
#if defined(CONFIG_RTK_L34_CANDIDATE_FLOW)
		if(fc_db.flowTbl[idx].candidateState!=CANDIDATE_STATE_NONE)
#else
		if(fc_db.flowTbl[idx].pFlowEntry->path1.valid)
#endif
		{		
			memcpy(&p1Data, &fc_db.flowTbl[idx].pFlowEntry->path1, sizeof(rtk_rg_asic_path1_entry_t));
			// In order to share an image between 9607C and 9603CVD
			_rtk_fc_sharing_image_flow_structure_convert(&p1Data);

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)

			if(fc_db.flowTbl[idx].candidateState==CANDIDATE_STATE_NEW && fc_db.flowTbl[idx].pFlowEntry->path6.in_path == FB_PATH_6 )
			{	
				int i;
				for(i=0;i<RTK_FC_TABLESIZE_MCFLOW;i++)
				{
					if(fc_db.mcGroupTbl[i].valid && fc_db.mcGroupTbl[i].hwIdx==idx)
						break;
				}
				PROC_PRINTF("\n["COLOR_Y"%d"COLOR_NM"]flow:\n",idx);
				PROC_PRINTF("##################################################\n");
				PROC_PRINTF("###### occupy by mainhash knownGroupTrap #########\n");
				if(i<RTK_FC_TABLESIZE_MCFLOW)
				{
					if(fc_db.mcGroupTbl[i].isIpv6)
						PROC_PRINTF("####### Group= %pI6 #########\n",fc_db.mcGroupTbl[i].mcGroup);
					else
						PROC_PRINTF("####### Group= %pI4h #########\n",fc_db.mcGroupTbl[i].mcGroup);
				}
				PROC_PRINTF("##################################################\n");

			}
			else
				dump_flow_by_rawdata(s, idx, (void *)&p1Data);
#else
			dump_flow_by_rawdata(s, idx, (void *)&p1Data);
#endif
			dump_sw_flow_fields(s, v, &fc_db.flowTbl[idx], idx);
		}
	}
	PROC_PRINTF("\n----------------------------------------------\n");

	return 0;
}


int dump_sw_flow_table_by_filter(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmpBuf[CMD_BUFF_SIZE] = {0};
	uint32 idx=0U;
	rtk_rg_asic_path1_entry_t p1Data;
	uint8 spa_check=0U, smac_check=0U, dmac_check=0U, sip_check=0U, dip_check=0U, ip_check=0U, sipV6_check=0U, dipV6_check=0U, ipV6_check=0U, l4proto_check=0U, sport_check=0U, dport_check=0U;
	rtk_fc_mac_port_idx_t spa=0;
	rtk_mac_t smac, dmac;
	uint32 sip=0U, dip=0U, ip=0U;
	rtk_ipv6_addr_t sipV6, dipV6, ipV6;
	uint32 sipV6_hash=0U, dipV6_hash=0U, ipV6_srcHash=0U, ipV6_dstHash=0U;
	uint16 l4proto=0U, sport=0U, dport=0U;
	uint8 flowIdx_check = 0U;
	uint32 flowIdx = 0U;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
	uint32 mainhashIdx = 0U;
	uint8 mainhashIdx_check = 0U;
#endif
#if defined(CONFIG_RTL8198E_SERIES)
	uint8 isMc_check = 0U, isMc = 0U;
	uint8 in_path_check = 0U, in_path = 0U;
#endif

	if (buffer)
	{
		char *strptr,*split_str;

		/* copy data to the buffer */
		strscpy(tmpBuf, buffer, CMD_BUFF_SIZE);

		strptr=tmpBuf;

		while(1)
		{
			split_str=strsep(&strptr," ");
			if(strptr==NULL) goto ERROR_PARAMETER;
#if 0
			if(strcasecmp(split_str,"SPA")==0)
			{
				split_str=strsep(&strptr," ");
				spa=simple_strtol(split_str, NULL, 0);
				spa_check=1;
			}
			else if(strcasecmp(split_str,"DA")==0)
			{
				split_str=strsep(&strptr," ");
				_rtk_rg_str2mac(split_str, &dmac);
				dmac_check=1;
			}
			else if(strcasecmp(split_str,"SA")==0)
			{
				split_str=strsep(&strptr," ");
				_rtk_rg_str2mac(split_str,&smac);
				smac_check=1;
			}
#endif
			if(strcasecmp(split_str,"SIP")==0)
			{
				char *ip_token, *split_ip_token, j;

				split_str=strsep(&strptr," ");
				ip_token=split_str;
				for(j=0, sip=0U; j<4; j++)
				{
					if(ip_token==NULL) goto ERROR_PARAMETER;
					split_ip_token=strsep(&ip_token,".");
					sip|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
				}
				sip_check=1U;
			}
			else if(strcasecmp(split_str,"DIP")==0)
			{
				char *ip_token, *split_ip_token, j;

				split_str=strsep(&strptr," ");
				ip_token=split_str;
				for(j=0, dip=0U; j<4; j++)
				{
					if(ip_token==NULL) goto ERROR_PARAMETER;
					split_ip_token=strsep(&ip_token,".");
					dip|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
				}
				dip_check=1U;
			}
			else if(strcasecmp(split_str,"IP")==0)
			{
				char *ip_token, *split_ip_token, j;

				split_str=strsep(&strptr," ");
				ip_token=split_str;
				for(j=0, ip=0U; j<4; j++)
				{
					if(ip_token==NULL) goto ERROR_PARAMETER;
					split_ip_token=strsep(&ip_token,".");
					ip|=(simple_strtol(split_ip_token, NULL, 0)<<((3-j)<<3));
				}
				ip_check=1U;
			}
			else if(strcasecmp(split_str,"L4PROTO")==0)
			{
				split_str=strsep(&strptr," ");
				l4proto=simple_strtol(split_str, NULL, 16);
				l4proto_check=1U;
			}

			else if(strcasecmp(split_str,"SPORT")==0)
			{
				split_str=strsep(&strptr," ");
				sport=simple_strtol(split_str, NULL, 0);
				sport_check=1U;
			}
			else if(strcasecmp(split_str,"DPORT")==0)
			{
				split_str=strsep(&strptr," ");
				dport=simple_strtol(split_str, NULL, 0);
				dport_check=1U;
			}
			else if(strcasecmp(split_str,"SIP6")==0)
			{
				split_str=strsep(&strptr," ");
				in6_pton(split_str, -1, &(sipV6.ipv6_addr[0]), -1, NULL);
				sipV6_check=1U;
				sipV6_hash = _rtk_rg_flowHashIPv6SrcAddr_get(sipV6.ipv6_addr);
			}
			else if(strcasecmp(split_str,"DIP6")==0)
			{
				split_str=strsep(&strptr," ");
				in6_pton(split_str,-1,&(dipV6.ipv6_addr[0]),-1,NULL);
				dipV6_check=1U;
				dipV6_hash = _rtk_rg_flowHashIPv6DstAddr_get(dipV6.ipv6_addr);
			}
			else if(strcasecmp(split_str,"IP6")==0)
			{
				split_str=strsep(&strptr," ");
				in6_pton(split_str,-1,&(ipV6.ipv6_addr[0]),-1,NULL);
				ipV6_check=1U;
				ipV6_srcHash = _rtk_rg_flowHashIPv6SrcAddr_get(ipV6.ipv6_addr);
				ipV6_dstHash = _rtk_rg_flowHashIPv6DstAddr_get(ipV6.ipv6_addr);
			}
			else if(strcasecmp(split_str,"idx")==0 || strcasecmp(split_str,"index")==0)
			{
				split_str=strsep(&strptr," ");
				flowIdx=simple_strtol(split_str, NULL, 0);
				flowIdx_check = 1U;
			}
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			else if((strcasecmp(split_str,"mainhash"))==0 || strcasecmp(split_str,"mainhashidx")==0)
			{
				split_str=strsep(&strptr," ");
				mainhashIdx=simple_strtol(split_str, NULL, 0);
				mainhashIdx_check = 1U;
			}
#endif
#if defined(CONFIG_RTL8198E_SERIES)
			else if(strcasecmp(split_str,"isMc")==0)
			{
				split_str=strsep(&strptr," ");
				isMc=simple_strtol(split_str, NULL, 0);
				isMc_check = 1U;
			}
			else if(strcasecmp(split_str,"in_path")==0)
			{
				split_str=strsep(&strptr," ");
				in_path=simple_strtol(split_str, NULL, 0);
				in_path_check = 1U;
			}
#endif
			else
			{
				goto ERROR_PARAMETER;
			}

			if (strptr==NULL) break;
		}
	}

	rtlglue_printf(">>Software Flow Table (filter by the following patterns):\n");
	if(spa_check) rtlglue_printf("SPA:%d\n", spa);
	if(dmac_check) rtlglue_printf("DA:%02x:%02x:%02x:%02x:%02x:%02x\n", dmac.octet[0], dmac.octet[1], dmac.octet[2], dmac.octet[3], dmac.octet[4], dmac.octet[5]);
	if(smac_check) rtlglue_printf("SA:%02x:%02x:%02x:%02x:%02x:%02x\n", smac.octet[0], smac.octet[1], smac.octet[2], smac.octet[3], smac.octet[4], smac.octet[5]);
	if(sip_check) rtlglue_printf("SIP:%d.%d.%d.%d\n", (sip>>24)&0xff, (sip>>16)&0xff, (sip>>8)&0xff, (sip)&0xff);
	if(dip_check) rtlglue_printf("DIP:%d.%d.%d.%d\n", (dip>>24)&0xff, (dip>>16)&0xff, (dip>>8)&0xff, (dip)&0xff);
	if(ip_check) rtlglue_printf("IP:%d.%d.%d.%d\n", (ip>>24)&0xff, (ip>>16)&0xff, (ip>>8)&0xff, (ip)&0xff);
	if(l4proto_check) rtlglue_printf("L4PROTO:0x%04x\n", l4proto);
	if(sport_check) rtlglue_printf("SPORT:%d\n", sport);
	if(dport_check) rtlglue_printf("DPORT:%d\n", dport);
	if(sipV6_check) rtlglue_printf("SIP6:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x (srcHash:0x%x)\n",
						sipV6.ipv6_addr[0], sipV6.ipv6_addr[1], sipV6.ipv6_addr[2], sipV6.ipv6_addr[3],
						sipV6.ipv6_addr[4], sipV6.ipv6_addr[5], sipV6.ipv6_addr[6], sipV6.ipv6_addr[7],
						sipV6.ipv6_addr[8], sipV6.ipv6_addr[9], sipV6.ipv6_addr[10], sipV6.ipv6_addr[11],
						sipV6.ipv6_addr[12], sipV6.ipv6_addr[13], sipV6.ipv6_addr[14], sipV6.ipv6_addr[15], sipV6_hash);
	if(dipV6_check) rtlglue_printf("DIP6:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x (dstHash:0x%x)\n",
						dipV6.ipv6_addr[0], dipV6.ipv6_addr[1], dipV6.ipv6_addr[2], dipV6.ipv6_addr[3],
						dipV6.ipv6_addr[4], dipV6.ipv6_addr[5], dipV6.ipv6_addr[6], dipV6.ipv6_addr[7],
						dipV6.ipv6_addr[8], dipV6.ipv6_addr[9], dipV6.ipv6_addr[10], dipV6.ipv6_addr[11],
						dipV6.ipv6_addr[12], dipV6.ipv6_addr[13], dipV6.ipv6_addr[14], dipV6.ipv6_addr[15], dipV6_hash);
	if(ipV6_check) rtlglue_printf("IP6:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x (srcHash:0x%x, dstHash:0x%x)\n",
						ipV6.ipv6_addr[0], ipV6.ipv6_addr[1], ipV6.ipv6_addr[2], ipV6.ipv6_addr[3],
						ipV6.ipv6_addr[4], ipV6.ipv6_addr[5], ipV6.ipv6_addr[6], ipV6.ipv6_addr[7],
						ipV6.ipv6_addr[8], ipV6.ipv6_addr[9], ipV6.ipv6_addr[10], ipV6.ipv6_addr[11],
						ipV6.ipv6_addr[12], ipV6.ipv6_addr[13], ipV6.ipv6_addr[14], ipV6.ipv6_addr[15], ipV6_srcHash, ipV6_dstHash);
#if defined(CONFIG_RTL8198E_SERIES)
	if(isMc_check) rtlglue_printf("isMc:%u\n", isMc);
	if(in_path_check) rtlglue_printf("in_path:%u\n", in_path);
#endif

	rtlglue_printf("\n");
	for(idx=0U; idx<fc_db.flowSwTableSize; idx++)
	{
#if defined(CONFIG_RTK_L34_CANDIDATE_FLOW)
		if(fc_db.flowTbl[idx].candidateState==CANDIDATE_STATE_NONE) continue;
#else
		if(fc_db.flowTbl[idx].pFlowEntry->path1.valid==0U) continue;
#endif

		if(flowIdx_check && flowIdx!=idx) continue;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		if(mainhashIdx_check && (idx >= fc_db.flowHwTableSize)) break;
		if(mainhashIdx_check && mainhashIdx!=idx) continue;
#else
		if(mainhashIdx_check && mainhashIdx!=fc_db.flowTbl[idx].mainHashIdx) continue;
#endif
#endif
#if defined(CONFIG_RTL8198E_SERIES)
		if (isMc_check && (isMc != FLOW_INFO_IS_SET(&fc_db.flowTbl[idx], FLOW_INFO_MC_ENTRY))) continue;
		if (in_path_check && (in_path != fc_db.flowTbl[idx].pFlowEntry->path1.in_path)) continue;
#endif
		
		memcpy(&p1Data, &fc_db.flowTbl[idx].pFlowEntry->path1, sizeof(rtk_rg_asic_path1_entry_t));
		// In order to share an image between 9607C and 9603CVD
		_rtk_fc_sharing_image_flow_structure_convert(&p1Data);

		if(fc_db.flowTbl[idx].pAbstrSwFlowEt)
		{
			rtk_fc_abstrSwFlowType_entry_t *flowType;
			rtk_fc_abstrSwFlowPattenField_entry_t pattenField;
			flowType=&fc_db.abstrSwFlowType[fc_db.flowTbl[idx].pAbstrSwFlowEt->swFlowKey.bits.flowtype];

			rtk_fc_parseAbstrSwPattenField(fc_db.flowTbl[idx].pAbstrSwFlowEt->swFlowKey.bits.isIpv6,&fc_db.flowTbl[idx].pAbstrSwFlowEt->swFlowKey,&pattenField);


			if((flowType->patternFlagMsk & TYPETOMSK(FLOW_PHY_LSPID)) && spa_check && *pattenField.pLspid != spa)
				continue;
			if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_DMAC)) && dmac_check && memcmp(pattenField.pDmac,&dmac,sizeof(dmac)))
				continue;
			if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L2_SMAC)) && smac_check && memcmp(pattenField.pSmac,&smac,sizeof(smac)))
				continue;
			if(fc_db.flowTbl[idx].pAbstrSwFlowEt->swFlowKey.bits.isIpv6	)
			{
				if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_SIP)) && sipV6_check && memcmp(pattenField.pSip,&sip,sizeof(sip)))
					continue;
				if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_SIP)) && ipV6_check && memcmp(pattenField.pSip,&ipV6,sizeof(ipV6)))
					continue;			
				if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_DIP)) && dipV6_check && memcmp(pattenField.pDip,&dipV6,sizeof(dipV6)))
					continue;
				if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_DIP)) && ipV6_check && memcmp(pattenField.pDip,&ipV6,sizeof(ipV6)))
					continue;
			}
			else
			{
				if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_SIP)) && sip_check && memcmp(pattenField.pSip,&sip,sizeof(sip)))
					continue;
				if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_SIP)) && ip_check && memcmp(pattenField.pSip,&ip,sizeof(ip)))
					continue;			
				if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_DIP)) && dip_check && memcmp(pattenField.pDip,&dip,sizeof(dip)))
					continue;
				if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L3_DIP)) && ip_check && memcmp(pattenField.pDip,&ip,sizeof(ip)))
					continue;
			}
			if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L4_PROTO)) && l4proto_check && *pattenField.pL4Porto != l4proto)
				continue;	
			if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L4_DPORT)) && dport_check && *pattenField.pL4Dport != dport)
				continue;				
			if((flowType->patternFlagMsk & TYPETOMSK(FLOW_L4_SPORT)) && sport_check && *pattenField.pL4Sport != sport)
				continue;				
		}
		else
		{
			switch(p1Data.in_path)
			{
				case FB_PATH_12:
				{
					//skip ip/port/l4protocol checking
					if(sip_check || dip_check || ip_check || sipV6_check || dipV6_check || ipV6_check || l4proto_check || sport_check || dport_check)
						continue;
					break;
				}
				case FB_PATH_34:
				{
					rtk_rg_asic_path3_entry_t *pP3Data = (rtk_rg_asic_path3_entry_t *)&p1Data;
					if(pP3Data->in_ipv4_or_ipv6==0U) //ipv4
					{
						if(sipV6_check || dipV6_check || ipV6_check)
							continue;
						if(sip_check && pP3Data->in_src_ipv4_addr!=sip)
							continue;
						if(dip_check && pP3Data->in_dst_ipv4_addr!=dip)
							continue;
						if(ip_check && pP3Data->in_src_ipv4_addr!=ip && pP3Data->in_dst_ipv4_addr!=ip)
							continue;
					}
					else	//ipv6
					{
						if(sip_check || dip_check || ip_check)
							continue;
						if(sipV6_check && pP3Data->in_src_ipv6_addr_hash!=sipV6_hash)
							continue;
						if(dipV6_check && pP3Data->in_dst_ipv6_addr_hash!=dipV6_hash)
							continue;
						if(ipV6_check && pP3Data->in_src_ipv6_addr_hash!=ipV6_srcHash && pP3Data->in_dst_ipv6_addr_hash!=ipV6_dstHash)
							continue;
					}
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
					if(l4proto_check && (pP3Data->in_l4proto_num != l4proto))
						continue;
#else
					if(l4proto_check && ((pP3Data->in_l4proto==1U && l4proto!=0x6U) || (pP3Data->in_l4proto==0U && l4proto!=0x11U)))
						continue;
#endif
					if(sport_check && pP3Data->in_l4_src_port!=sport)
						continue;
					if(dport_check && pP3Data->in_l4_dst_port!=dport)
						continue;
					break;
				}
				case FB_PATH_5:
				{
					rtk_rg_asic_path5_entry_t *pP5Data = (rtk_rg_asic_path5_entry_t *)&p1Data;
					if(pP5Data->in_ipv4_or_ipv6==0) //ipv4
					{
						uint32 checkDip;
						checkDip = (pP5Data->out_l4_act==1U && pP5Data->out_l4_direction==0U)?(fc_db.netifTbl[NETIF_HWTOSW(pP5Data->in_intf_idx)].intf.gateway_ipv4_addr):(pP5Data->in_dst_ipv4_addr);

						if(sipV6_check || dipV6_check || ipV6_check)
							continue;
						if(sip_check && pP5Data->in_src_ipv4_addr!=sip)
							continue;
						if(dip_check && checkDip!=dip)
							continue;
						if(ip_check && pP5Data->in_src_ipv4_addr!=ip && checkDip!=ip)
							continue;
					}
					else	//ipv6
					{
						if(sip_check || dip_check || ip_check)
							continue;
						if(sipV6_check && pP5Data->in_src_ipv6_addr_hash!=sipV6_hash)
							continue;
						if(dipV6_check && pP5Data->in_dst_ipv6_addr_hash!=dipV6_hash)
							continue;
						if(ipV6_check && pP5Data->in_src_ipv6_addr_hash!=ipV6_srcHash && pP5Data->in_dst_ipv6_addr_hash!=ipV6_dstHash)
							continue;
					}
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
					if(l4proto_check && (pP5Data->in_l4proto_num != l4proto))
						continue;
#else
					if(l4proto_check && ((pP5Data->in_l4proto==1U && l4proto!=0x6U) || (pP5Data->in_l4proto==0U && l4proto!=0x11U)))
						continue;
#endif
					if(sport_check && pP5Data->in_l4_src_port!=sport)
						continue;
					if(dport_check && pP5Data->in_l4_dst_port!=dport)
						continue;
					break;
				}
				case FB_PATH_6:
				{
					rtk_rg_asic_path6_entry_t *pP6Data = (rtk_rg_asic_path6_entry_t *)&p1Data;
					if(pP6Data->in_dsliteif==0U) //ipv4
					{
						if(sipV6_check || dipV6_check || ipV6_check)
							continue;
						if(sip_check && pP6Data->in_src_ipv4_addr!=sip)
							continue;
						if(dip_check && pP6Data->in_dst_ipv4_addr!=dip)
							continue;
						if(ip_check && pP6Data->in_src_ipv4_addr!=ip && pP6Data->in_dst_ipv4_addr!=ip)
							continue;
					}
					else	//ipv6
					{
						if(sip_check || dip_check || ip_check)
							continue;
						if(sipV6_check && pP6Data->in_src_ipv6_addr_hash!=sipV6_hash)
							continue;
						if(dipV6_check && pP6Data->in_dst_ipv6_addr_hash!=dipV6_hash)
							continue;
						if(ipV6_check && pP6Data->in_src_ipv6_addr_hash!=ipV6_srcHash && pP6Data->in_dst_ipv6_addr_hash!=ipV6_dstHash)
							continue;
					}
					//skip port/l4protocol checking
					if(l4proto_check || sport_check || dport_check)
						continue;
					break;
				}
#if defined(CONFIG_RTL8198E_SERIES) && defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
				case FB_PATH_MC_WIFI_AMSDU_TX:
					break;
#endif
				default:
					continue;
			}
		}

		if(fc_db.flowTbl[idx].pAbstrSwFlowEt)
		{
			rtlglue_printf("\n["COLOR_Y"%d"COLOR_NM"]abstrSwMcFlow:\n",idx);
			dump_abstrSwFlowPatten_table(NULL,NULL,fc_db.flowTbl[idx].pAbstrSwFlowEt);
			dump_sw_flow_fields(NULL, NULL, &fc_db.flowTbl[idx], idx);
		}
		else		
			dump_flow_by_rawdata(NULL, idx, (void *)&p1Data);

		dump_sw_flow_fields(NULL, NULL, &fc_db.flowTbl[idx], idx);
	}

	rtlglue_printf("\n----------------------------------------------\n");
	return count;

ERROR_PARAMETER:
	//rtlglue_printf("Accepted parameters: SPA, DA, SA, SIP, DIP, IP, L4PROTO(tcp: 0x6, udp: 0x11), SPORT, DPORT, SIP6, DIP6\n");
	rtlglue_printf("Accepted parameters: SIP, DIP, IP, L4PROTO(tcp: 0x6, udp: 0x11), SPORT, DPORT, SIP6, DIP6, idx, mainhash\n");
	rtlglue_printf("Example: (Dump flows according to their sip and dip)\n");
	rtlglue_printf("  echo \"SIP 192.168.1.2 DIP 10.10.10.1\" > /proc/fc/sw_dump/flow\n");
	return count;
}

int dump_sw_flow_list(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	int i;
	rtk_fc_swFlow_linkList_t *pFlowEntry;

	if(fc_db.shortcut_flow_count == 0U)
	{
		PROC_PRINTF("shortcut_flow_count is 0. No entry in Sw flow list.\n");
		return retval;
	}

	PROC_PRINTF(">>Sw flow list:\n");
	for(i=0; i<RTK_SW_FLOW_LIST_HEAD_COUNT; i++)
	{
		if(!SWFLOW_LIST_EMPTY(&fc_db.swFlow_hashListHead[i]))
		{
			PROC_PRINTF(" Flow_Hash[%3d] : \n", i);
			SWFLOW_LIST_FOR_EACH_ENTRY(pFlowEntry, &fc_db.swFlow_hashListHead[i], flow_list)
			{
				if(pFlowEntry->flow_list.next!= SWFLOW_LIST_END)
					PROC_PRINTF("	 Flow idx[%d] ->\n", pFlowEntry->idx);
				else
					PROC_PRINTF("	 Flow idx[%d]\n", pFlowEntry->idx);
			}
			PROC_PRINTF("\n");
		}
	}
#if 0
	PROC_PRINTF(">>Sw flow free list:\n");
	SWFLOW_LIST_FOR_EACH_ENTRY(pFlowEntry, &fc_db.swFlow_freeListHead, flow_list)
	{
		if(pFlowEntry->flow_list.next!=&fc_db.swFlow_freeListHead)
			PROC_PRINTF("	 Flow idx[%d] ->\n", pFlowEntry->idx);
		else
			PROC_PRINTF("	 Flow idx[%d]\n", pFlowEntry->idx);
	}
#endif
	PROC_PRINTF("\n");

	return retval;
}

/*
* MAP-E:
*	Dump all the IPv6 dst addresses' info used in upstream
*/
int dump_sw_mape_dst6_info(struct seq_file *s, void *v){
	rtk_rg_err_code_t retval=0;
	rtk_fc_mape_fmr_dst6_t *mape_fmr;
	struct list_head *p=NULL, *n=NULL;
	int i;
	
	PROC_PRINTF(">>MAP-E upstream IPv6 dst list:\n");
	for(i=0; i<RTK_FC_TABLESIZE_INTF; i++)
	{
		if (fc_db.mapeDst6s[i].in_used != 0){
			PROC_PRINTF("- hwIdx[%d]\n", i); 
			
			if (!list_empty(&fc_db.mapeDst6s[i].used_list)){
				list_for_each_safe(p, n, &(fc_db.mapeDst6s[i].used_list)) {
					mape_fmr = list_entry(p, rtk_fc_mape_fmr_dst6_t, list);	
					PROC_PRINTF("	 idx:[%d] addr6:%s ref_cnt:%d\n", mape_fmr->index, diag_util_inet_n6toa(&(mape_fmr->d_addr6.s6_addr[0])),mape_fmr->ref_cnt); 
				}
			}
			
			PROC_PRINTF("\n");
		}
	}

	return retval;
}
int dump_sw_memAllocStat(struct seq_file *s, void *v)
{
	struct list_head memAllocDumpList;
	rtk_rg_err_code_t retval=0;
	unsigned long flags;

	if(fc_db.controlFuc.mem_leak_debug)
	{
		
		INIT_LIST_HEAD(&memAllocDumpList);
		spin_lock_irqsave(&(fc_db.lock_memDebug),flags);
		if (!list_empty(&(fc_db.memAlloc))){	
			rtk_fc_mem_alloc_debug *memalloc;
			list_for_each_entry(memalloc, &(fc_db.memAlloc), mem_alloc_list) {
				rtk_fc_mem_alloc_debug_dump *memallocdump;
				unsigned char is_exist=0U;
				if (!list_empty(&memAllocDumpList)){
					list_for_each_entry(memallocdump, &memAllocDumpList, mem_alloc_list) {
						if((memalloc->alloc_func_addr == memallocdump->alloc_func_addr) && 
							(memalloc->alloc_size == memallocdump->alloc_size)&&
							(memalloc->alloc_ret_addr == memallocdump->alloc_ret_addr) )
						{
							(memallocdump->alloc_num)++;
							is_exist = 1U;
							break;
						}
					}
				}
				if(is_exist == 0U)
				{
					memallocdump = (rtk_fc_mem_alloc_debug_dump*)kmalloc(sizeof(rtk_fc_mem_alloc_debug_dump), GFP_ATOMIC);
					if (memallocdump) {
						memallocdump->alloc_func_addr = memalloc->alloc_func_addr;
						memallocdump->alloc_ret_addr = memalloc->alloc_ret_addr;
						memallocdump->alloc_size = memalloc->alloc_size;
						memallocdump->alloc_num = 1;
						list_add_tail(&(memallocdump->mem_alloc_list), &memAllocDumpList);
					}
				}
			}
		}
		spin_unlock_irqrestore(&(fc_db.lock_memDebug),flags);
		PROC_PRINTF("%-10s %-10s %-60s %-60s\n","allocnum","allocsize","allocfunc","retfunc");
		if (!list_empty(&memAllocDumpList)){
			rtk_fc_mem_alloc_debug_dump *memallocdump,*tmpmemallocdump;
			list_for_each_entry_safe(memallocdump, tmpmemallocdump, &memAllocDumpList, mem_alloc_list)
			{
				PROC_PRINTF("%-10u %-10u %p %-50pS %p %-50pS\n",memallocdump->alloc_num, memallocdump->alloc_size, 
					memallocdump->alloc_func_addr,memallocdump->alloc_func_addr,memallocdump->alloc_ret_addr,memallocdump->alloc_ret_addr);
				list_del(&(memallocdump->mem_alloc_list));
				kfree(memallocdump);
			}
		}
	}
	else
	{
		PROC_PRINTF("Please enable the %s config\n","CONFIG_RTK_FC_MEMLEAK_DEBUG");
	}
	return retval;
}
int dump_sw_memInfo(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
#if defined(CONFIG_RG_RTL9607C_SERIES) || defined(CONFIG_RG_RTL9603CVD_SERIES)
	int fc_core_mod_size = (int)(fc_db.memUsage + sizeof(fc_db) + sizeof(rgpro_db));
#else
	int fc_core_mod_size = (int)(fc_db.memUsage + sizeof(fc_db));
#endif
	int ipFrag_max_hash_size = RTK_FC_GET_IPFRAG_MAX_HASH_SIZE();
	int ipFrag_max_hash_entry_size = RTK_FC_GET_IPFRAG_MAX_HASH_ENTRY_SIZE();

	/* FC mgr module */
	{
		rtlglue_printf("%-50s\n","\033[1;33m[FC manager module]\033[0m");
		RTK_FC_HELPER_SMP_MGR_MEMINFO_DUMP();
	}

	/* FC core module */
	{
		rtlglue_printf("\033[1;33m");
		rtlglue_printf("%-50s%12d (%6d KB)\n","[FC core module]                     total", fc_core_mod_size, fc_core_mod_size/1024);
		rtlglue_printf("\033[0m");

		/* malloc */
		{
			rtlglue_printf("%-50s%12d (%6d KB)\n","1. malloc:", fc_db.memUsage, fc_db.memUsage/1024U);
			// kmalloc used in _rtk_fc_flowTable_init() and _rtk_fc_flowDataBase_init()
			{
				// flow related
				rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "flow related");
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
				if(_rtk_rg_fbMode_get()==FB_MODE_4K)
				{
					rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flowTcam_hashListHead:", (int)sizeof(struct list_head) * fc_db.flowHashBuckets, (int)sizeof(struct list_head), fc_db.flowHashBuckets);
					rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flowTcamList:", (int)sizeof(rtk_fc_flowTcam_linkList_t) * RTK_FC_TABLESIZE_FLOWTCAM, (int)sizeof(rtk_fc_flowTcam_linkList_t), RTK_FC_TABLESIZE_FLOWTCAM);
				}
#endif
#if defined(CONFIG_FC_RTL8277C_SERIES)
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     overFlowHashList:", (int)sizeof(rtk_fc_overFlowHash_linkList_t) * RTK_FC_TABLESIZE_OVERFLOW_FLOW, (int)sizeof(rtk_fc_overFlowHash_linkList_t), RTK_FC_TABLESIZE_OVERFLOW_FLOW);
#endif
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     pureSwFlows:", (int)sizeof(rtk_fc_tableFlowEntry_t)*(fc_db.flowSwTableSize - fc_db.flowHwTableSize), (int)sizeof(rtk_fc_tableFlowEntry_t), (fc_db.flowSwTableSize - fc_db.flowHwTableSize));
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     swFlow_hashListHead:", (int)sizeof(SWFLOW_LIST_HEAD_t) * RTK_SW_FLOW_LIST_HEAD_COUNT, (int)sizeof(SWFLOW_LIST_HEAD_t), RTK_SW_FLOW_LIST_HEAD_COUNT);
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     swFlowList:", (int)sizeof(rtk_fc_swFlow_linkList_t) * (fc_db.flowSwTableSize-fc_db.flowHwTableSize), (int)sizeof(rtk_fc_swFlow_linkList_t), SWFLOW_LIST_ENT_CNT);
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     fb_hwFlow_validBitsArray:", (int)sizeof(int)*(fc_db.flowHwTableSize>>5), (int)sizeof(int), (fc_db.flowHwTableSize>>5));
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     fb_hwFlow_tmpValidBitsArray:", (int)sizeof(int)*(fc_db.flowHwTableSize>>5), (int)sizeof(int), (fc_db.flowHwTableSize>>5));
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     fb_hwFlow_trfBitsArray:", (int)sizeof(int)*(fc_db.flowHwTableSize>>5), (int)sizeof(int), (fc_db.flowHwTableSize>>5));
#endif
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flowHashCount:", (int)sizeof(uint16) * fc_db.flowHashBuckets, (int)sizeof(uint16), fc_db.flowHashBuckets);
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flowHashHwCollisionCount:", (int)sizeof(uint16) * fc_db.flowHashBuckets, (int)sizeof(uint16), fc_db.flowHashBuckets);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     g3_mHashTbl_validBitsArray:", (int)sizeof(int)*(fc_db.flowHwTableSize>>5), (int)sizeof(int), (fc_db.flowHwTableSize>>5));
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     g3_mHash_trfBitsArray:", (int)sizeof(int)*(fc_db.flowHwTableSize>>5), (int)sizeof(int), (fc_db.flowHwTableSize>>5));
#endif

				// ipFrag related
				rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "ipFrag relateds");
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ipFrag_hashListHead:", (int)sizeof(struct list_head) * ipFrag_max_hash_size, (int)sizeof(struct list_head), ipFrag_max_hash_size);
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     negativeIpFrag_hashListHead:", (int)sizeof(struct list_head) * ipFrag_max_hash_size, (int)sizeof(struct list_head), ipFrag_max_hash_size);
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ipFragList:", (int)sizeof(rtk_fc_ipFrag_linkList_t) * ipFrag_max_hash_size * ipFrag_max_hash_entry_size, (int)sizeof(rtk_fc_ipFrag_linkList_t), ipFrag_max_hash_size * ipFrag_max_hash_entry_size);
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     negativeIpFragList:", (int)sizeof(rtk_fc_negativeIpFrag_linkList_t) * ipFrag_max_hash_size * ipFrag_max_hash_entry_size, (int)sizeof(rtk_fc_negativeIpFrag_linkList_t), ipFrag_max_hash_size * ipFrag_max_hash_entry_size);
			}

			// kmalloc used in _rtk_fc_lutEntry_pool_init()
			{
				rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "lut related");
				rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     lutEntry_pool_ring:", (int)sizeof(rtk_fc_lut_entry_t) * RTK_FC_LUTENTRY_POOL_SIZE, (int)sizeof(rtk_fc_lut_entry_t), RTK_FC_LUTENTRY_POOL_SIZE);
			}
		 }
		 /* fc_db */
		 {
			rtlglue_printf("%-50s%12d (%6d KB)\n","2. fc_db:", (int)sizeof(fc_db), (int)sizeof(fc_db)/1024);
			rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "system related");
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     controlFuc:", (int)sizeof(fc_db.controlFuc), (int)sizeof(fc_db.controlFuc), 1);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     systemGlobal:", (int)sizeof(fc_db.systemGlobal), (int)sizeof(fc_db.systemGlobal), 1);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     smp_pktHdr:", (int)sizeof(fc_db.smp_pktHdr), (int)sizeof(fc_db.smp_pktHdr[0]), 1, NR_CPUS);
#if defined(CONFIG_RTK_L34_XPON_PLATFORM) || defined(CONFIG_RTK_L34_G3_PLATFORM)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     rtnlJobs:", (int)sizeof(fc_db.rtnlJobs), (int)sizeof(fc_db.rtnlJobs), 1);
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if defined(FC_F_HW_POL_OPTIMIZE_MODE)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     swMeterIfUsed_32bitmap:", (int)sizeof(fc_db.swMeterIfUsed_32bitmap), (int)sizeof(fc_db.swMeterIfUsed_32bitmap[0]), RTK_FC_SWMETER_USED_BITMAP32_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     l34MeterIfUsed_32bitmap:", (int)sizeof(fc_db.l34MeterIfUsed_32bitmap), (int)sizeof(fc_db.l34MeterIfUsed_32bitmap[0]), RTK_FC_L34METER_USED_BITMAP32_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     pol1ACLIfUsed_32bitmap:", (int)sizeof(fc_db.pol1ACLIfUsed_32bitmap), (int)sizeof(fc_db.pol1ACLIfUsed_32bitmap[0]), RTK_FC_HW_POL1_ACL_USED_BITMAP32_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     pol1HostIfUsed_info:", (int)sizeof(fc_db.pol1HostIfUsed_info), (int)sizeof(fc_db.pol1HostIfUsed_info[0]), RTK_FC_HW_POL1_HOST_SIZE);
			// no need CLS policer remapping table
#else
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     hwPolRemap:", (int)sizeof(fc_db.hwPolRemap), (int)sizeof(fc_db.hwPolRemap[0]), RTK_FC_TABLESIZE_ACL_POLREMAP_MAX);
#endif
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     swMeter:", (int)sizeof(fc_db.swMeter), (int)sizeof(fc_db.swMeter[0]), RTK_FC_TABLESIZE_SW_SHAREMTR);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     swRateLimit:", (int)sizeof(fc_db.swRateLimit), (int)sizeof(fc_db.swRateLimit[0]), RT_RATE_SW_RATE_LIMIT_TYPE_MAX);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     l34Meter:", (int)sizeof(fc_db.l34Meter), (int)sizeof(fc_db.l34Meter[0]), RTK_FC_TABLESIZE_FBMTR);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     shapingCtrl:", (int)sizeof(fc_db.shapingCtrl), (int)sizeof(fc_db.shapingCtrl[0]), RTK_FC_TABLESIZE_SW_SHAPING);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     wanAccessLimitIP_head:", (int)sizeof(fc_db.wanAccessLimitIP_head), (int)sizeof(fc_db.wanAccessLimitIP_head[0]), RTK_FC_WAN_ACCESS_IP_BUCKET_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     extraTagList:", (int)sizeof(fc_db.extraTagList), (int)sizeof(fc_db.extraTagList[0]), RTK_FC_TABLESIZE_EXTRATAG);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     extraTagContentBuffer:", (int)sizeof(fc_db.extraTagContentBuffer), (int)sizeof(fc_db.extraTagContentBuffer[0]), RTK_FC_DUAL_CONTENT_BUF_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     smp_statistic:", (int)sizeof(fc_db.smp_statistic), (int)sizeof(fc_db.smp_statistic[0][0]), FC_SMP_JOBS_TYPE_MAX, NR_CPUS);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     highPriFlowPattern:", (int)sizeof(fc_db.highPriFlowPattern), (int)sizeof(fc_db.highPriFlowPattern[0]), RT_FLOW_HIGHPRIFLOW_PATTERN_SIZE);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     skbmark:", (int)sizeof(fc_db.skbmark), (int)sizeof(fc_db.skbmark[0]), RTK_FC_SKBMARK_END);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     statistic:", (int)sizeof(fc_db.statistic), (int)sizeof(fc_db.statistic), 1);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     trace_filter_bitmask:", (int)sizeof(fc_db.trace_filter_bitmask), (int)sizeof(fc_db.trace_filter_bitmask[0]), TRACFILTER_MAX);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     trace_filter:", (int)sizeof(fc_db.trace_filter), (int)sizeof(fc_db.trace_filter[0]), TRACFILTER_MAX);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     tracefilterCpuOwner:", (int)sizeof(fc_db.tracefilterCpuOwner), (int)sizeof(fc_db.tracefilterCpuOwner[0]), 1, NR_CPUS);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     cmd_buf:", (int)sizeof(fc_db.cmd_buf), (int)sizeof(fc_db.cmd_buf[0]), CMD_BUFF_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     cmd_proc_path:", (int)sizeof(fc_db.cmd_proc_path), (int)sizeof(fc_db.cmd_proc_path[0]), CMD_BUFF_PROC_LEN);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flow_del_trace_index:", (int)sizeof(fc_db.flow_del_trace_index), (int)sizeof(fc_db.flow_del_trace_index[0]), FLOWDELTRACE_MAX);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     uucStorm:", (int)sizeof(fc_db.uucStorm), (int)sizeof(fc_db.uucStorm[0]), RTK_FC_MAC_PORT_MAX);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     hostPoliceInfoTable:", (int)sizeof(fc_db.hostPoliceInfoTable), (int)sizeof(fc_db.hostPoliceInfoTable[0]), RT_RATE_HOSTPOLICING_TABLE_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     hostPoliceConfList:", (int)sizeof(fc_db.hostPoliceConfList), (int)sizeof(fc_db.hostPoliceConfList[0]), RT_RATE_HOSTPOLICING_TABLE_SIZE);
#if defined(CONFIG_RTK_SOC_RTL8198D) || defined(CONFIG_FC_RTL8198F_SERIES) || defined(CONFIG_RTL8198X_SERIES)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     defaultRouteInfo:", (int)sizeof(fc_db.defaultRouteInfo), (int)sizeof(fc_db.defaultRouteInfo[0]), MAX_DEFAULT_ROUTE_INFO_ENTRY_SIZE);
#endif

#if defined(CONFIG_RTK_L34_XPON_PLATFORM) || defined(CONFIG_RTK_L34_G3_PLATFORM)
			rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "flow related");
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys + %6d align)\n","     pureHwFlows:", (int)FLOWDATAPOOL_SIZE, (int)sizeof(char) * (int)sizeof(rtk_fc_tableFlowEntry_t), RTK_FC_TABLESIZE_HW_FLOW, RTK_FC_FLOWENT_ALIGNBUF);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flowTbl:", (int)sizeof(fc_db.flowTbl), (int)sizeof(fc_db.flowTbl[0]), (RTK_FC_TABLESIZE_HW_FLOW + RTK_FC_MAX_SHORTCUT_FLOW_SIZE));
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     abstrSwFlowType:", (int)sizeof(fc_db.abstrSwFlowType), (int)sizeof(fc_db.abstrSwFlowType[0]), ABSTR_SWFLOW_TYPE_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     listHead_mcExtraFlowIdxHashTbl:", (int)sizeof(fc_db.listHead_mcExtraFlowIdxHashTbl), (int)sizeof(fc_db.listHead_mcExtraFlowIdxHashTbl[0]), RTK_FC_TABLESIZE_MCFLOW_HASH);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     mcExtraFlowIdxTbl:", (int)sizeof(fc_db.mcExtraFlowIdxTbl), (int)sizeof(fc_db.mcExtraFlowIdxTbl[0]), RTK_FC_TABLESIZE_MCFLOW);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ipv6_nat_mappingTbl:", (int)sizeof(fc_db.ipv6_nat_mappingTbl), (int)sizeof(fc_db.ipv6_nat_mappingTbl[0]), RTK_FC_TABLESIZE_I6NAT_MAPPING_TABLE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ipv6_mapt_mappingTbl:", (int)sizeof(fc_db.ipv6_mapt_mappingTbl), (int)sizeof(fc_db.ipv6_mapt_mappingTbl[0]), RTK_FC_TABLESIZE_I6MAPT_MAPPING_TABLE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     l2DualTbl:", (int)sizeof(fc_db.l2DualTbl), (int)sizeof(fc_db.l2DualTbl[0]), RTK_FC_TABLESIZE_L2DUAL_TABLE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     extPortTbl:", (int)sizeof(fc_db.extPortTbl), (int)sizeof(fc_db.extPortTbl[0]), RTK_FC_TABLESIZE_EXTPORT);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     mcGroupTbl:", (int)sizeof(fc_db.mcGroupTbl), (int)sizeof(fc_db.mcGroupTbl[0]), RTK_FC_TABLESIZE_MCFLOW);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flowHWMib:", (int)sizeof(fc_db.flowHWMib), (int)sizeof(fc_db.flowHWMib[0]), RT_STAT_FLOWMIB_TABLE_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     flowSWMib:", (int)sizeof(fc_db.flowSWMib), (int)sizeof(fc_db.flowSWMib[0][0]), RT_STAT_FLOWMIB_TABLE_SIZE, NR_CPUS);
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flowHWMib2:", (int)sizeof(fc_db.flowHWMib2), (int)sizeof(fc_db.flowHWMib2[0]), RT_STAT_FLOWMIB2_TABLE_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     flowSWMib2:", (int)sizeof(fc_db.flowSWMib2), (int)sizeof(fc_db.flowSWMib2[0][0]), RT_STAT_FLOWMIB2_TABLE_SIZE, NR_CPUS);
#endif
#elif defined(CONFIG_RTK_L34_XPON_PLATFORM)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     flowSWMib:", (int)sizeof(fc_db.flowSWMib), (int)sizeof(fc_db.flowSWMib[0][0]), RT_STAT_FLOWMIB_TABLE_SIZE, NR_CPUS);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flow_natloopbackTbl:", (int)sizeof(fc_db.flow_natloopbackTbl), (int)sizeof(fc_db.flow_natloopbackTbl[0][0]), RTK_FC_TABLESIZE_NATLOOPBACK_BUCKET * RTK_FC_TABLESIZE_NATLOOPBACK_WAY);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     path5_downStream_flowList:", (int)sizeof(fc_db.path5_downStream_flowList), (int)sizeof(fc_db.path5_downStream_flowList[0]), RTK_FC_PATH5_DS_FLOWLIST_BUCKET_MAX_NUM);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     greIpidInfo:", (int)sizeof(fc_db.greIpidInfo), (int)sizeof(fc_db.greIpidInfo[0]), RTK_FC_TABLESIZE_INTF);
#if defined(CONFIG_RTK_L34_G3_PLATFORM) && !defined(CONFIG_FC_G3_G3LITE_SERIES)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     streamidTbl:", (int)sizeof(fc_db.streamidTbl), (int)sizeof(fc_db.streamidTbl[0]), RTK_FC_TABLESIZE_STREAMID);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ponVoqRefTbl:", (int)sizeof(fc_db.ponVoqRefTbl), (int)sizeof(fc_db.ponVoqRefTbl[0]), RTK_FC_TABLESIZE_PON_VOQ);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     dmaAftActionTbl:", (int)sizeof(fc_db.dmaAftActionTbl), (int)sizeof(fc_db.dmaAftActionTbl[0]), RTK_FC_TABLESIZE_DMAAFTACTION);
#if defined(CONFIG_FC_RTL9607F_SERIES)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     dmaAftMapTbl:", (int)sizeof(fc_db.dmaAftMapTbl), (int)sizeof(fc_db.dmaAftMapTbl[0]), RTK_FC_TABLESIZE_DMAAFTMAP);
#endif
#endif
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     gemFilter_conf:", (int)sizeof(fc_db.gemFilter_conf), (int)sizeof(fc_db.gemFilter_conf[0]), RT_STAT_GEMMIB_TABLE_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys * %6d cpu cores)\n","     gemFilter_mib:", (int)sizeof(fc_db.gemFilter_mib), (int)sizeof(fc_db.gemFilter_mib[0][0]), RT_STAT_GEMMIB_TABLE_SIZE, NR_CPUS);			
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     dual_pt_flowMapTbl:", (int)sizeof(fc_db.dual_pt_flowMapTbl), (int)sizeof(fc_db.dual_pt_flowMapTbl[0]), RTK_FC_DUAL_PASSTHROUGH_FLOWMAPPING_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     dualCallBackFunc:", (int)sizeof(fc_db.dualCallBackFunc), (int)sizeof(fc_db.dualCallBackFunc[0]), RTK_FC_DUALTYPE_END);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     mapeDst6s:", (int)sizeof(fc_db.mapeDst6s), (int)sizeof(fc_db.mapeDst6s[0]), RTK_FC_TABLESIZE_INTF);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ethertypeTbl:", (int)sizeof(fc_db.ethertypeTbl), (int)sizeof(fc_db.ethertypeTbl[0]), RTK_FC_TABLESIZE_ETHERTYPE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ct_hash_info_ListHead:", (int)sizeof(fc_db.ct_hash_info_ListHead), (int)sizeof(fc_db.ct_hash_info_ListHead[0]), RTK_FC_CT_HASH_INFO_BUCKET_SIZE);

			rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n","netif related");
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     netifTbl:", (int)sizeof(fc_db.netifTbl), (int)sizeof(fc_db.netifTbl[0]), RTK_FC_TABLESIZE_INTF_SW);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     netifHwTblVaild:", (int)sizeof(fc_db.netifHwTblVaild), (int)sizeof(fc_db.netifHwTblVaild[0]), RTK_FC_TABLESIZE_INTF);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     netifDummyPacket:", (int)sizeof(fc_db.netifDummyPacket), (int)sizeof(fc_db.netifDummyPacket[0]), RTK_FC_TABLESIZE_INTF);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     netifTrfBit:", (int)sizeof(fc_db.netifTrfBit), (int)sizeof(fc_db.netifTrfBit[0]), (RTK_FC_TABLESIZE_INTF/32)+1);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     devGwMacTbl:", (int)sizeof(fc_db.devGwMacTbl), (int)sizeof(fc_db.devGwMacTbl[0]), RTK_FC_DEV_GW_MAC_TBL);
#endif
			rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "lut related");
#if defined(CONFIG_RTK_L34_XPON_PLATFORM) || defined(CONFIG_RTK_L34_G3_PLATFORM)

			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     lutTbl:", (int)sizeof(fc_db.lutTbl), (int)sizeof(fc_db.lutTbl[0]), RTK_FC_TABLESIZE_LUT);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     lutCam_hashListHead:", (int)sizeof(fc_db.lutCam_hashListHead), (int)sizeof(fc_db.lutCam_hashListHead[0]), RTK_FC_LUT_BUCKET_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     lutCamList:", (int)sizeof(fc_db.lutCamList), (int)sizeof(fc_db.lutCamList[0]), LUTTABLE_BCAM_SIZE);
#if !(defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)) // 8277C/9607F no need indMac, pFlowPath5->out_dmac_idx is meaningless
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     indMacTbl:", (int)sizeof(fc_db.indMacTbl), (int)sizeof(fc_db.indMacTbl[0]), RTK_FC_TABLESIZE_INDMAC);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     listHead_indMacHash:", (int)sizeof(fc_db.listHead_indMacHash), (int)sizeof(fc_db.listHead_indMacHash[0]), RTK_FLOWBASE_BUCKETSIZE_INDMAC);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     lutIvlTbl:", (int)sizeof(fc_db.lutIvlTbl), (int)sizeof(fc_db.lutIvlTbl[0]), RTK_FC_TABLESIZE_LUT_IVL);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     macAddrLearningLimit:", (int)sizeof(fc_db.macAddrLearningLimit), (int)sizeof(fc_db.macAddrLearningLimit[0]), RTK_FC_MAC_PORT_MAX);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     macAddr_portGroup:", (int)sizeof(fc_db.macAddr_portGroup), (int)sizeof(fc_db.macAddr_portGroup), 1);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     wlanMacAddrLearningLimit:", (int)sizeof(fc_db.wlanMacAddrLearningLimit), (int)sizeof(fc_db.wlanMacAddrLearningLimit[0]), RTK_FC_WLANX_END_INTF);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     lutEntry_pool_ring (ptr):", (int)sizeof(fc_db.lutEntry_pool_ring), (int)sizeof(fc_db.lutEntry_pool_ring[0]), RTK_FC_LUTENTRY_POOL_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     lut_hash_list_head:", (int)sizeof(fc_db.lut_hash_list_head), (int)sizeof(fc_db.lut_hash_list_head[0]), RTK_FC_LUT_BUCKET_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     lut_quickhash_list_head:", (int)sizeof(fc_db.lut_quickhash_list_head), (int)sizeof(fc_db.lut_quickhash_list_head[0]), RTK_FC_LUT_BUCKET_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     vlanGroupMACLimit_macHead:", (int)sizeof(fc_db.vlanGroupMACLimit_macHead), (int)sizeof(fc_db.vlanGroupMACLimit_macHead[0]), RTK_FC_MAX_LUT_HW_HASH_HEAD);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     vlanGroupMACLimit_group:", (int)sizeof(fc_db.vlanGroupMACLimit_group), (int)sizeof(fc_db.vlanGroupMACLimit_group[0]), MAX_VLAN_GROUP_MAC_LIMIT_NUMBER);

			rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "acl related");
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     acl_SW_table_entry:", (int)sizeof(fc_db.acl_SW_table_entry), (int)sizeof(fc_db.acl_SW_table_entry[0]), MAX_ACL_SW_ENTRY_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     aclSWEntry:", (int)sizeof(fc_db.aclSWEntry), (int)sizeof(fc_db.aclSWEntry), 1);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     aclSWEntry_temp:", (int)sizeof(fc_db.aclSWEntry_temp), (int)sizeof(fc_db.aclSWEntry_temp), 1);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     acl_rt_table_entry:", (int)sizeof(fc_db.acl_filter_temp), (int)sizeof(fc_db.acl_filter_temp[0]), MAX_ACL_SW_ENTRY_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     acl_rt_table_valid:", (int)sizeof(fc_db.acl_filter_temp_valid), (int)sizeof(fc_db.acl_filter_temp_valid[0]), (MAX_ACL_SW_ENTRY_SIZE/32)+1);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     acl_SWindex_sorting_by_weight:", (int)sizeof(fc_db.acl_SWindex_sorting_by_weight), (int)sizeof(fc_db.acl_SWindex_sorting_by_weight[0]), MAX_ACL_SW_ENTRY_SIZE);
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     aclField:", (int)sizeof(fc_db.aclField), (int)sizeof(fc_db.aclField[0]), GLOBAL_ACL_FIELD_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     aclRule:", (int)sizeof(fc_db.aclRule), (int)sizeof(fc_db.aclRule[0]), GLOBAL_ACL_RULE_SIZE);
#endif
#if defined(CONFIG_FC_G3_G3LITE_SERIES)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     acl_remap_hash_idx:", (int)sizeof(fc_db.acl_remap_hash_idx), (int)sizeof(fc_db.acl_remap_hash_idx[0]), RTK_FC_ACL_HASH_TCP_TYPE_MAX * RTK_FC_MAC_PORT_CPU);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     aclAndCfReservedRule:", (int)sizeof(fc_db.aclAndCfReservedRule), (int)sizeof(fc_db.aclAndCfReservedRule), 1);
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     nameRsvAclType:", (int)sizeof(fc_db.nameRsvAclType), (int)sizeof(fc_db.nameRsvAclType[0]), RTK_FC_ACLANDCF_RESERVED_TAIL_END);
#if defined(FC_USER_ACL_CA_CLS_SUPPORT)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     hw_cls_rule_record:", (int)sizeof(fc_db.ca_cls_rule_record), (int)sizeof(fc_db.ca_cls_rule_record[0]), MAX_ACL_CA_CLS_RULE_SIZE);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     hw_aal_cls_rule_record:", (int)sizeof(fc_db.ca_aal_cls_rule_record), (int)sizeof(fc_db.ca_aal_cls_rule_record[0]), MAX_ACL_CA_AAL_CLS_RULE_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     hw_cls_template:", (int)sizeof(fc_db.ca_cls_template), (int)sizeof(fc_db.ca_cls_template[0]), RTK_ACL_TEMPLATE_CA_END);
#endif
#if defined(CONFIG_RTK_L34_G3_PLATFORM) && (defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES))
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     acl_mib:", (int)sizeof(fc_db.acl_mib), (int)sizeof(fc_db.acl_mib[0]), RT_STAT_ACLMIB_TABLE_SIZE);
#endif

			rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "vxlan related");
#if defined(CONFIG_FC_CA8277B_SERIES)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     vxlan_l3cls_info:", (int)sizeof(fc_db.vxlan_l3cls_info), (int)sizeof(fc_db.vxlan_l3cls_info), 1);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     vxlan_fastFwd_RoundRobin_cpuPort_array:", (int)sizeof(fc_db.vxlan_fastFwd_RoundRobin_cpuPort_array), (int)sizeof(fc_db.vxlan_fastFwd_RoundRobin_cpuPort_array[0]), 4);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     vxlan_downStream_record:", (int)sizeof(fc_db.vxlan_downStream_record), (int)sizeof(fc_db.vxlan_downStream_record[0]), 4);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     vxlan_upStream_record:", (int)sizeof(fc_db.vxlan_upStream_record), (int)sizeof(fc_db.vxlan_upStream_record[0]), 4);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     vxlan_us_SpecialFastFwdTbl:", (int)sizeof(fc_db.vxlan_us_SpecialFastFwdTbl), (int)sizeof(fc_db.vxlan_us_SpecialFastFwdTbl[0]), 4);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     vxlan_l3cls_upstream_isSet:", (int)sizeof(fc_db.vxlan_l3cls_upstream_isSet), (int)sizeof(fc_db.vxlan_l3cls_upstream_isSet[0]), 4);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     vxlan_l3cls_downstream_isSet:", (int)sizeof(fc_db.vxlan_l3cls_downstream_isSet), (int)sizeof(fc_db.vxlan_l3cls_downstream_isSet[0]), 4);

#if	defined(CONFIG_FC_RTL9607C_SERIES)
			rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "NPTV6 related");
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     nptv6_acc_upstream_assign_share_meter:", (int)sizeof(fc_db.nptv6_acc_upstream_assign_share_meter), (int)sizeof(fc_db.nptv6_acc_upstream_assign_share_meter[0]), 4);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     nptv6_flow_info:", (int)sizeof(fc_db.nptv6_flow_info), (int)sizeof(fc_db.nptv6_flow_info[0]), MAX_NPTV6_ACC_UPSTREAM_SIZE+MAX_NPTV6_ACC_DOWNSTREAM_SIZE);
#endif
#if defined(CONFIG_RTK_FC_WFO_PER_HW_FLOW_MIB)
			rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "WFO_PER_HW_FLOW_MIB");
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     wfo_rx_cache_mib_freeListHead:", (int)sizeof(fc_db.wfo_rx_cache_mib_freeListHead), (int)sizeof(fc_db.wfo_rx_cache_mib_freeListHead[0]), RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE);
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     wfo_tx_cache_mib_freeListHead:", (int)sizeof(fc_db.wfo_tx_cache_mib_freeListHead), (int)sizeof(fc_db.wfo_tx_cache_mib_freeListHead[0]), RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE);
#endif
		 }
#if defined(CONFIG_RG_RTL9607C_SERIES) || defined(CONFIG_RG_RTL9603CVD_SERIES)
		 /* rgpro_db */
		 {
			rtlglue_printf("%-50s%12d (%6d KB)\n","3. rgpro_db:", (int)sizeof(rgpro_db), (int)sizeof(rgpro_db)/1024);
#if defined(CONFIG_RG_FLOW_8K_MODE)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ddrMemBlock:", (int)sizeof(rgpro_db.ddrMemBlock), (int)sizeof(rgpro_db.ddrMemBlock[0]), (32<<13) + 1023);
#elif defined(CONFIG_RG_FLOW_16K_MODE)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ddrMemBlock:", (int)sizeof(rgpro_db.ddrMemBlock), (int)sizeof(rgpro_db.ddrMemBlock[0]), (32<<14) + 1023);
#elif defined(CONFIG_RG_FLOW_32K_MODE) || defined(CONFIG_APOLLOPRO_FPGA)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     ddrMemBlock:", (int)sizeof(rgpro_db.ddrMemBlock), (int)sizeof(rgpro_db.ddrMemBlock[0]), (32<<15) + 1023);
#endif
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     flowEntryValidBits:", (int)sizeof(rgpro_db.flowEntryValidBits), (int)sizeof(rgpro_db.flowEntryValidBits[0]), (FLOWBASED_TABLESIZE_FLOWSRAM_ARYSIZE+FLOWBASED_TABLESIZE_FLOWTCAM_ARYSIZE)>>5);
		 }
#endif
	}

	rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "struct size related");
	/* extra info */
	{
		rtlglue_printf("%s %s(%d) %s(%d)\n", "     sk_buff skb struct size:",
			"sk_buff",			(int)sizeof(struct sk_buff), 
			"fc_cached_data_size",	(int)sizeof(rtk_fc_ingress_data_t) );
	}
	/* offset tracking */
	{
		rtlglue_printf("%s %s(%d) %s(%d) %s(%d)\n", "     flowEntry member offset:", 
			"flow_extra_info", 		(int)offsetof(rtk_fc_tableFlow_t, flow_extra_info),
			"pppoe_sid", 			(int)offsetof(rtk_fc_tableFlow_t, pppoe_sid), 
			"protoCtrl", 			(int)offsetof(rtk_fc_tableFlow_t, protoCtrl) );
		
		rtlglue_printf("%s %s(%d) %s(%d) %s(%d)\n", "     LutEntry member offset:", 
			"wlan_dev_idx", 		(int)offsetof(rtk_fc_lut_entry_t, wlan_dev_idx),
			"staticRefCount", 		(int)offsetof(rtk_fc_lut_entry_t, staticRefCount), 
			"lutRcuHead", 			(int)offsetof(rtk_fc_lut_entry_t, lutRcuHead) );
	}
#if defined(CONFIG_RTK_FC_WIFI_AMSDU_OFFLOAD_BY_PE)
	rtlglue_printf("     +++++++++++++++ %-20s +++++++++++++++\n", "AMSDU offload related");
	{
		rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     wifi_amsdu_pe_offload_mac_id_tbl:", (int)sizeof(fc_db.wifi_amsdu_pe_offload_mac_id_tbl), (int)sizeof(fc_db.wifi_amsdu_pe_offload_mac_id_tbl[0]), RTK_FC_WIFI_PE_OFFLOAD_MAC_ID_SIZE);
		rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     wifi_amsdu_pe_offload_voq_tbl:", (int)sizeof(fc_db.wifi_amsdu_pe_offload_voq_tbl), (int)sizeof(fc_db.wifi_amsdu_pe_offload_voq_tbl[0]), RTK_FC_WIFI_PE_OFFLOAD_VOQ_SIZE);
		rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     wifi_amsdu_pe_offload_mode_info:", (int)sizeof(fc_db.wifi_amsdu_pe_offload_mode_info), (int)sizeof(fc_db.wifi_amsdu_pe_offload_mode_info[0]), RTK_FC_WIFI_AMSDU_PE_OFFLOAD_MODE_END);
		if(fc_db.controlFuc.amsdu_pe_dbg_info_tbl)
			rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     amsdu_pe_dbg_info_tbl:", (int)(sizeof(rtk_fc_amsdu_pe_dbg_info_t) * AMSDU_PE_DBG_MODE_INFO_SIZE), (int)sizeof(rtk_fc_amsdu_pe_dbg_info_t), AMSDU_PE_DBG_MODE_INFO_SIZE);
		rtlglue_printf("%-50s%12d (%6d btyes * %6d entrys)\n","     amsdu_pe_dbg_mode_lspid2ldpid:", (int)(sizeof(fc_db.controlFuc.amsdu_pe_dbg_mode_lspid2ldpid)), (int)sizeof(fc_db.controlFuc.amsdu_pe_dbg_mode_lspid2ldpid[0]), AMSDU_PE_DBG_MODE_LDPID2LSPID_CNT);
	}
#endif
	return retval;
}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)

int dump_mc_macId_table(struct seq_file *s, void *v)
{
	int i;

	for(i=0;i<WLAN_MC_TO_UC_MAC_POOL_MAX;i++)
	{
		if(fc_db.mcToUcMacId[i].valid)
			PROC_PRINTF("[%d] LutIdx=%d ref:%d\n",i,fc_db.mcToUcMacId[i].lutIdx,fc_db.mcToUcMacId[i].refCnt);
	}
	
	return SUCCESS;
}

#endif


int dump_ct_hash_info_list(struct seq_file *s, void *v)
{
	int i, count = 0;
	rtk_fc_ct_hash_info_t *pTmp_ct_hash_list, *pTmp_next_ct_hash_list;
	
	for(i = 0; i < RTK_FC_CT_HASH_INFO_BUCKET_SIZE ; i++)
	{
		if(!list_empty(&fc_db.ct_hash_info_ListHead[i]))
		{
			list_for_each_entry_safe(pTmp_ct_hash_list, pTmp_next_ct_hash_list, &fc_db.ct_hash_info_ListHead[i], ct_hash_list)	//just return the first entry right behind of head
			{
				PROC_PRINTF("============Hash: %d============\n",i);
				PROC_PRINTF("upstream_flow_index: %d\n",pTmp_ct_hash_list->upstream_flow_index);
				PROC_PRINTF("downstream_flow_index: %d\n",pTmp_ct_hash_list->downstream_flow_index);
				PROC_PRINTF("ct: %p\n\n",pTmp_ct_hash_list->ct);
				
				
				count++;
			}
		}
	}
	
	PROC_PRINTF("Total list count:%d\n",count);
	return SUCCESS;
}

int dump_pe_crypto_connection(struct seq_file *s, void *v)
{
#if defined(CONFIG_RTK_FC_CRYPTO_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)	
	int i, j;
	rt_pe_ret_t pe_ret;
	rt_pe_crypto_request_t crypto_req;

	memset(&crypto_req, 0, sizeof(rt_pe_crypto_request_t));
	crypto_req.req_cmd = RT_PE_CRYPTO_ENGINE_CMD_STATUS_GET;
	RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
	RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();
	pe_ret = rtk_fc_pe_crypto_test(crypto_req, FAIL, 1);
	RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
	RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
	PROC_PRINTF("PE crypto engine status: ");
	if(pe_ret==RT_PE_RET_FAIL)
		PROC_PRINTF("\033[1;33;41mPE crypto engine does not boot up!!\033[0m\n");
	else if(pe_ret==RT_PE_RET_EXISTED)
		PROC_PRINTF("\033[1;33;40mEnable\033[0m\n");
	else if(pe_ret==RT_PE_RET_NOT_FOUND)
		PROC_PRINTF("\033[1;33;40mDisable\033[0m\n");
	else
		PROC_PRINTF("\033[1;33;41mUnknown\033[0m\n");

	PROC_PRINTF("\n>>PE Encryption Table:\n");
	for(i=0; i<MAX_PE_IPSEC_ENCRYPTION_CONNECTION_NUM; i++)
	{
		if(fc_db.pe_encrypt_info[i].fc_saInfo_idx == FAIL)
			continue;
		PROC_PRINTF("===== Encrypt connection[%d], fc_saInfo_idx[%d] =====\n", i, fc_db.pe_encrypt_info[i].fc_saInfo_idx);
		PROC_PRINTF("  cipher_mode %d hash_mode %d\n", fc_db.pe_encrypt_info[i].pe_data.cipher_mode, fc_db.pe_encrypt_info[i].pe_data.hash_mode);
		PROC_PRINTF("  iv_len %d hash_icv_len %d\n", fc_db.pe_encrypt_info[i].pe_data.iv_len, fc_db.pe_encrypt_info[i].pe_data.hash_icv_len);
		PROC_PRINTF("  key_idx %d hash_key_idx %d\n", fc_db.pe_encrypt_info[i].pe_data.key_idx, fc_db.pe_encrypt_info[i].pe_data.hash_key_idx);
		PROC_PRINTF("  outer_dmac %pM outer_smac %pM\n", fc_db.pe_encrypt_info[i].pe_data.outer_dmac.octet, fc_db.pe_encrypt_info[i].pe_data.outer_smac.octet); 	
		PROC_PRINTF("  outer_isIPv4OrIpv6 %u ldpid %u\n", fc_db.pe_encrypt_info[i].pe_data.outer_isIPv4OrIpv6, fc_db.pe_encrypt_info[i].pe_data.ldpid);
		if(fc_db.pe_encrypt_info[i].pe_data.outer_isIPv4OrIpv6)
			PROC_PRINTF("  outer_sipv6 %pI6 outer_dipv6 %pI6\n", fc_db.pe_encrypt_info[i].pe_data.outer_sip.ipv6_addr.ipv6_addr, fc_db.pe_encrypt_info[i].pe_data.outer_dip.ipv6_addr.ipv6_addr);
		else
		{
			uint32 ipv4_saddr = htonl(fc_db.pe_encrypt_info[i].pe_data.outer_sip.ipv4_addr);
			uint32 ipv4_daddr = htonl(fc_db.pe_encrypt_info[i].pe_data.outer_dip.ipv4_addr);
			PROC_PRINTF("  outer_sipv4 %pI4 outer_dipv4 %pI4\n", &ipv4_saddr, &ipv4_daddr);
		}
		PROC_PRINTF("  is_NATT %u encap_sport %u encap_dport %u\n", fc_db.pe_encrypt_info[i].pe_data.is_NATT, fc_db.pe_encrypt_info[i].pe_data.encap_sport, fc_db.pe_encrypt_info[i].pe_data.encap_dport);
		PROC_PRINTF("  esp_spi 0x%x esp_seq_no 0x%x\n", fc_db.pe_encrypt_info[i].pe_data.esp_spi, fc_db.pe_encrypt_info[i].pe_data.esp_seq_no);
		PROC_PRINTF("  iv");
		for(j=0; j<RT_PE_IPSEC_IV_LEN; j++)
			PROC_PRINTF(" 0x%x", fc_db.pe_encrypt_info[i].pe_data.iv[j]);
		PROC_PRINTF("\n");
		PROC_PRINTF("  dma_aft_idx %d pon_streamId %d\n", fc_db.pe_encrypt_info[i].pe_data.tag_info.internal_used.dma_aft_idx, fc_db.pe_encrypt_info[i].pe_data.tag_info.internal_used.streamId.sid);	
	}

	PROC_PRINTF("\n>>PE Decryption Table:\n");
	for(i=0; i<MAX_PE_IPSEC_DECRYPTION_CONNECTION_NUM; i++)
	{
		if(fc_db.pe_decrypt_info[i].fc_saInfo_idx == FAIL)
			continue;
		PROC_PRINTF("===== Decrypt connection[%d], fc_saInfo_idx[%d] =====\n", i, fc_db.pe_decrypt_info[i].fc_saInfo_idx);
		PROC_PRINTF("  cipher_mode %d hash_mode %d\n", fc_db.pe_decrypt_info[i].pe_data.cipher_mode, fc_db.pe_decrypt_info[i].pe_data.hash_mode);
		PROC_PRINTF("  iv_len %d hash_icv_len %d\n", fc_db.pe_decrypt_info[i].pe_data.iv_len, fc_db.pe_decrypt_info[i].pe_data.hash_icv_len);
		PROC_PRINTF("  key_idx %d hash_key_idx %d\n", fc_db.pe_decrypt_info[i].pe_data.key_idx, fc_db.pe_decrypt_info[i].pe_data.hash_key_idx);
		PROC_PRINTF("  outer_isIPv4OrIpv6 %u is_NATT %u hwlookup_swId %u\n", fc_db.pe_decrypt_info[i].pe_data.outer_isIPv4OrIpv6, fc_db.pe_decrypt_info[i].pe_data.is_NATT, fc_db.pe_decrypt_info[i].pe_data.hwlookup_swId);
		PROC_PRINTF("  Dmac %pM Smac %pM\n", fc_db.pe_decrypt_info[i].pe_data.dmac.octet, fc_db.pe_decrypt_info[i].pe_data.smac.octet);
	}
#else
	PROC_PRINTF("\033[1;33;41m[WARNING] No support!! Please check IC platform and compiler flag. \033[0m\n");
#endif

	return SUCCESS;
}

int dump_pe_srv6_offload(struct seq_file *s, void *v)
{
#if defined(CONFIG_RTK_FC_SRV6_OFFLOAD_BY_PE) && defined(CONFIG_REALTEK_IPC2RCPU)
	int i;
	rt_pe_ret_t pe_ret;
	rt_pe_srv6_request_t srv6_req;

	memset(&srv6_req, 0, sizeof(rt_pe_srv6_request_t));
	srv6_req.req_cmd = RT_PE_SRV6_OFFLOAD_CMD_STATUS_GET;
	RTK_FC_HELPER_MGR_GLOBAL_SPIN_UNLOCK_BH();
	RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_UNLOCK_BH();
	pe_ret = rtk_fc_pe_srv6_test(srv6_req, 1);
	RTK_FC_HELPER_MGR_TRACEFILTER_SPIN_LOCK_BH();
	RTK_FC_HELPER_MGR_GLOBAL_SPIN_LOCK_BH();
	rtlglue_printf("PE SRv6 offload status: ");

	if(pe_ret==RT_PE_RET_EXISTED) {
		rtlglue_printf("\033[1;33;40mEnable\033[0m\n");
		for(i=0;i<MAX_PE_SRV6_ENCAP_CONNECTION_NUM;i++){
			if(fc_db.pe_encap_info[i].valid){
				rtlglue_printf("[%d] encap conn type=%d outer_len=%d\n",i,fc_db.pe_encap_info[i].pe_data.srv6_type,fc_db.pe_encap_info[i].pe_data.outer_len);
				dump_packet(&fc_db.pe_encap_info[i].pe_data.outer_hdr[0], fc_db.pe_encap_info[i].pe_data.outer_len, "SRv6");
			}
		}
		for(i=0;i<MAX_PE_SRV6_DECAP_CONNECTION_NUM;i++){
			if(fc_db.pe_decap_info[i].valid)
				rtlglue_printf("[%d] decap conn type=%d outer_len=%d\n",i,fc_db.pe_decap_info[i].pe_data.srv6_type,fc_db.pe_decap_info[i].pe_data.outer_len);
		}
	} else if(pe_ret==RT_PE_RET_FAIL) {
		rtlglue_printf("\033[1;33;41mPE SRv6 offload does not boot up!!\033[0m\n");
	} else if(pe_ret==RT_PE_RET_NOT_FOUND) {
		rtlglue_printf("\033[1;33;40mDisable\033[0m\n");
	} else {
		rtlglue_printf("\033[1;33;41mUnknown\033[0m\n");
	}

#else
	rtlglue_printf("\033[1;33;41m[WARNING] No support!! Please check IC platform and compiler flag. \033[0m\n");
#endif

	return SUCCESS;
}

int dump_vlanGroupMacLimitStatistics(struct seq_file *s, void *v)
{
	int i,j,k,len=0;
	PROC_PRINTF(">>vlan group MAC limit statistics:\n");
	//per port, per group information, showing all learned MAC if avalable
	for(i=0;i<RTK_FC_MAC_PORT_MAX;i++)
	{
		PROC_PRINTF("PORT[%d]:\n",i);
		for(j=0;j<MAX_VLAN_GROUP_MAC_LIMIT_NUMBER;j++)
		{
			if(fc_db.vlanGroupMACLimit_group[j].group_info.valid && fc_db.vlanGroupMACLimit_group[j].group_info.port==i)
			{
				PROC_PRINTF("  Group Index[%d] Count:%d Limit:%d\n",j,atomic_read(&fc_db.vlanGroupMACLimit_group[j].group_info.mac_count),fc_db.vlanGroupMACLimit_group[j].group_info.mac_limit_number);

				if(fc_db.vlanGroupMACLimit_group[j].group_info.untag)
				{
					PROC_PRINTF("    Untag MAC:\n");
					if(!list_empty(&fc_db.vlanGroupMACLimit_group[j].mac_head))
					{
						int cnt=1;
						rtk_fc_vlanGroupMacLimit_mac_t *pMacEntry;
						list_for_each_entry(pMacEntry, &fc_db.vlanGroupMACLimit_group[j].mac_head, group_list)
						{
							if(pMacEntry->vlanId==-1)
							{
								PROC_PRINTF("        %d. %02x:%02x:%02x:%02x:%02x:%02x\n",cnt++,
									pMacEntry->mac.octet[0],pMacEntry->mac.octet[1],pMacEntry->mac.octet[2],
									pMacEntry->mac.octet[3],pMacEntry->mac.octet[4],pMacEntry->mac.octet[5]);
							}
						}
					}
				}
				for(k=0;k<MAX_VLAN_HW_TABLE_SIZE;k++)
				{
					if(rtk_fc_test_bit(k&0x1f,(void *)&fc_db.vlanGroupMACLimit_group[j].group_info.vlanMask[k>>5]))
					{
						PROC_PRINTF("    Vlan %d MAC:\n",k);
						if(!list_empty(&fc_db.vlanGroupMACLimit_group[j].mac_head))
						{
							int cnt=1;
							rtk_fc_vlanGroupMacLimit_mac_t *pMacEntry;
							list_for_each_entry(pMacEntry, &fc_db.vlanGroupMACLimit_group[j].mac_head, group_list)
							{
								if(pMacEntry->vlanId==k)
								{
									PROC_PRINTF("        %d. %02x:%02x:%02x:%02x:%02x:%02x\n",cnt++,
										pMacEntry->mac.octet[0],pMacEntry->mac.octet[1],pMacEntry->mac.octet[2],
										pMacEntry->mac.octet[3],pMacEntry->mac.octet[4],pMacEntry->mac.octet[5]);
								}
							}
						}
					}
				}
			}
		}
	}

	return len;
}


#define PRINTF_BY_PLATFORM_ATOMIC( port , p_arr)	do { \
	if((port>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<port))) \
	{ \
		PROC_PRINTF("%8d",atomic_read(p_arr+(uint32)port)); \
	} \
}while(0)

#define PRINTF_BY_PLATFORM( port , p_arr)	do { \
	if((port>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<port))) \
	{ \
		PROC_PRINTF("%8d",*(p_arr+(uint32)port)); \
	} \
}while(0)

#define PRINTF_BY_PLATFORM_2D_ARRAY( port , p_arr, twodarr_size)	\
do { \
	if((port>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<port))) \
	{ \
		uint32 arr_idx = 0; \
		uint32 sum = 0; \
		for(arr_idx=0;arr_idx<twodarr_size;arr_idx++) { \
			sum+=p_arr[port][arr_idx];\
		} \
		PROC_PRINTF("%8d", sum); \
	} \
}while(0)
#define PRINTF_BY_PLATFORM_2D_ARRAY_ATOMIC( port , p_arr, twodarr_size)	\
			do { \
				if((port>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<port))) \
				{ \
					uint32 arr_idx = 0; \
					uint32 sum = 0; \
					for(arr_idx=0;arr_idx<twodarr_size;arr_idx++) { \
						sum+=atomic_read(&p_arr[port][arr_idx]);\
					} \
					PROC_PRINTF("%8d", sum); \
				} \
			}while(0)


int rtk_fc_proc_fwdStatistic_set(struct file *filp, const char *buff,unsigned long len, void *data)
{

	fc_db.fwdStatistic = (_rtk_fc_proc_parsing_string_to_integer(buff,len))==0 ? FALSE : TRUE;
	memset(&fc_db.statistic,0,sizeof(fc_db.statistic));
	rtlglue_printf("%d\n", fc_db.fwdStatistic);
	return len;
}

int32 rtk_fc_proc_fwdStatistic_get(struct seq_file *s, void *v)
{
	int i,j;
	int len=0;

	//PROC_PRINTF("rg_db size: %d bytes\n", sizeof(rg_db));
	//PROC_PRINTF("rgpro_db size: %d bytes\n", sizeof(rgpro_db));

	//PROC_PRINTF("%d\n",fc_db.fwdStatistic);
	if(fc_db.fwdStatistic==0)
	{
		rtlglue_printf("%d: disabled, please echo 1 to enable it.\n",fc_db.fwdStatistic);
		return 0;
	}
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
		PROC_PRINTF(" - mem usage %d KB (malloc:%d	fc_db:%d pro_db:%d)\n", (fc_db.memUsage+sizeof(fc_db)+sizeof(rgpro_db))/1024, fc_db.memUsage,  sizeof(fc_db), sizeof(rgpro_db));
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
		PROC_PRINTF(" - mem usage %d KB (malloc:%d	fc_db:%d )\n", (int)(fc_db.memUsage+sizeof(fc_db))/1024, fc_db.memUsage,  (int)sizeof(fc_db));
		PROC_PRINTF(" - WIFI FF Cnt From MC Port(0x1B):%d\n",atomic_read(&fc_db.statistic.mcPortCnt_WIFI_FF_TX));
		PROC_PRINTF(" - WIFI FF Cnt by nicRx hook (EPP64 has no SPA info):%d\n",atomic_read(&fc_db.statistic.ucEpp64Cnt_WIFI_FF_TX));
#endif


	PROC_PRINTF("Ingress PORT\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("%8d",i);
		}
	}


	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("========");
		}
	}
	PROC_PRINTF("\nWIFI FF\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			//if(i==RTK_RG_PORT_EXT0_0)PROC_PRINTF("       X");
			PROC_PRINTF("%8d",atomic_read(&fc_db.statistic.perPortCnt_MWFF_TX[i]));
		}
	}
	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("========");
		}
	}
	//------------------------------------------------------------------------------------------------------------------------------------

	PROC_PRINTF("\nBC\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_broadcast);

	PROC_PRINTF("\nMC\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_multicast);

	PROC_PRINTF("\nUC\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_unicast);

	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

/*
	PROC_PRINTF("\nARP Request\t");
	for(i=0;i<RTK_RG_PORT_EXT1_OTHER;i++)
		PRINTF_BY_PLATFORM(i,fc_db.statistic.perPortCnt_ARP_request);


	PROC_PRINTF("\nARP Reply\t");
	for(i=0;i<RTK_RG_PORT_EXT1_OTHER;i++)
		PRINTF_BY_PLATFORM(i,fc_db.statistic.perPortCnt_ARP_reply);

	PROC_PRINTF("\nNB Sol\t\t");
	for(i=0;i<RTK_RG_PORT_EXT1_OTHER;i++)
		PRINTF_BY_PLATFORM(i,fc_db.statistic.perPortCnt_NB_solicitation);

	PROC_PRINTF("\nNB Adv\t\t");
	for(i=0;i<RTK_RG_PORT_EXT1_OTHER;i++)
		PRINTF_BY_PLATFORM(i,fc_db.statistic.perPortCnt_NB_advertisement);
*/
	PROC_PRINTF("\nUDP\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_UDP);

	PROC_PRINTF("\nTCP\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_TCP);

	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}


	PROC_PRINTF("\nSYN\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_SYN);


	PROC_PRINTF("\nSYN_ACK\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_SYN_ACK);

	PROC_PRINTF("\nFIN\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_FIN);

	PROC_PRINTF("\nFIN_ACK\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_FIN_ACK);

	PROC_PRINTF("\nFIN_PSH_ACK\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_FIN_PSH_ACK);

	PROC_PRINTF("\nRST\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_RST);

	PROC_PRINTF("\nRST_ACK\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_RST_ACK);
	
	PROC_PRINTF("\nPUSH_ACK\t");
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_PUSH_ACK);

	PROC_PRINTF("\nACK\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ACK);

	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	PROC_PRINTF("\nFragment\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_fragment);
	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	PROC_PRINTF("\nSC UDP\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_UDP);
	
	PROC_PRINTF("\nSC TCP\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_TCP);

	PROC_PRINTF("\nSC SYN\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_sc_SYN);


	PROC_PRINTF("\nSC SYN_ACK\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_sc_SYN_ACK);

	PROC_PRINTF("\nSC FIN\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_sc_FIN);

	PROC_PRINTF("\nSC FIN_ACK\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_sc_FIN_ACK);

	PROC_PRINTF("\nSC FIN_PSH_ACK\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_sc_FIN_PSH_ACK);

	PROC_PRINTF("\nSC RST\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_sc_RST);

	PROC_PRINTF("\nSC RST_ACK\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_sc_RST_ACK);
	
	PROC_PRINTF("\nSC PUSH_ACK\t");
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_sc_PUSH_ACK);

	PROC_PRINTF("\nSC ACK\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_sc_ACK);

	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	
	PROC_PRINTF("\nEGR UDP\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_UDP);
	
	PROC_PRINTF("\nEGR TCP\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_TCP);


	PROC_PRINTF("\nEGR SYN\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_SYN);


	PROC_PRINTF("\nEGR SYN_ACK\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_SYN_ACK);

	PROC_PRINTF("\nEGR FIN\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_FIN);

	PROC_PRINTF("\nEGR FIN_ACK\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_FIN_ACK);

	PROC_PRINTF("\nEGR FIN_PSH_ACK\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_FIN_PSH_ACK);

	PROC_PRINTF("\nEGR RST\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_RST);

	PROC_PRINTF("\nEGRs RST_ACK\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_RST_ACK);
	
	PROC_PRINTF("\nEGR PUSH_ACK\t");
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_PUSH_ACK);

	PROC_PRINTF("\nEGR ACK\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_egress_ACK);

	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	PROC_PRINTF("\nShortcut\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut);

	PROC_PRINTF("\nShortcut_v6\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcutV6);

	PROC_PRINTF("\nShortcut_icmpv4\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_icmp4);

	
	PROC_PRINTF("\nShortcut_icmpv6\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_icmp6);

	
	PROC_PRINTF("\nSC_esp_encrypt\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_encrypt);
	
	PROC_PRINTF("\nSC_esp_decrypt\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_decrypt);
	
	PROC_PRINTF("\nIPSEC hwlookup\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_decrypt_hwlookup);
	
	PROC_PRINTF("\nIPSEC ipi enq\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_ipi_enqueue);
	

#if defined(FC_F_SHORTCUT_EARLYCHECK)
	PROC_PRINTF("\nSC_earlychk\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_earlycheck);
#endif

#if 0
	PROC_PRINTF("\nSC_esp_encrypt\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_encrypt);
	
	PROC_PRINTF("\nSC_esp_decrypt\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_decrypt);
	
	PROC_PRINTF("\nIPSEC hwlookup\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_decrypt_hwlookup);
#endif
	
	PROC_PRINTF("\nNPTv6_FF_TX\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_NPTv6_FF_TX);
	
	PROC_PRINTF("\nNPTv6_NIC_FF_TX\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_NPTv6_NIC_FF_TX);

	

	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	PROC_PRINTF("\nL2Fwd\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_L2FWD);


	PROC_PRINTF("\nIPv4 L3Fwd\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_IPv4_L3FWD);

	PROC_PRINTF("\nIPv6 L3Fwd\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_IPv6_L3FWD);

	PROC_PRINTF("\nL4Fwd\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_L4FWD);

	PROC_PRINTF("\nDrop\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_2D_ARRAY_ATOMIC(i,fc_db.statistic.perPortCnt_Drop, (RTK_FC_RET_DROP_END-RTK_FC_RET_DROP));


	PROC_PRINTF("\nESP IPI Drop\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_ipi_drop);

	PROC_PRINTF("\nESP desc Drop\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_desc_not_enough);

	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	PROC_PRINTF("\nTo PS(BC)\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ToPS_broadcast);

	PROC_PRINTF("\nTo PS(MC)\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ToPS_multicast);

	PROC_PRINTF("\nTo PS(UC)\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ToPS_unicast);

	PROC_PRINTF("\nFrom PS(BC)\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_FromPS_broadcast);

	PROC_PRINTF("\nFrom PS(MC)\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_FromPS_multicast);

	PROC_PRINTF("\nFrom PS(UC)\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_FromPS_unicast);

	if(unlikely(fc_db.controlFuc.tx_busy_to_ps == 1)){
		PROC_PRINTF("\nFrom PS(TXbusy)\t");
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_FromPS_txbusy);
	}

	PROC_PRINTF("\nLocal out\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_LocalOut);


	PROC_PRINTF("\nESP hw-LU miss\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipsec_decrypt_hwlookup_miss);
	
	PROC_PRINTF("\nSeq_num changed\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ipsec_change_seq_num);

	PROC_PRINTF("\nSQN sync drop\t");
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ipsec_change_seq_num_drop);



	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	PROC_PRINTF("\nDummyPkt\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_dummpPkt);

	PROC_PRINTF("\ndummpPktAlloc\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_dummpPktAlloc);
	PROC_PRINTF("\n");
	
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	PROC_PRINTF("\nL2 LRU\t\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_L2LRU);
	
	PROC_PRINTF("\nFlow LRU\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_FlowLRU);

	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	for(j=0;j<256;j++)
	{
		int show=0;
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			if(atomic_read(&fc_db.statistic.perPortCnt_Reason[j][i])!=0)
			{
				show=1;
				break;
			}

		if(show==1)
		{
			switch(j)
			{
				case CPU_REASON_L34_FWD:
					PROC_PRINTF("\nRSN:%2d (L34_FWD)",j);
					break;
				case CPU_REASON_ACL:
					PROC_PRINTF("\nRSN:%2d (ACL_TRP)",j);
					break;
				case CPU_REASON_FLOWMISS:
					PROC_PRINTF("\nRSN:%2d (FLW_MIS)",j);
					break;
				case CPU_REASON_MTU:
					PROC_PRINTF("\nRSN:%2d (OVR_MTU)",j);
					break;
				case CPU_REASON_TTL:
					PROC_PRINTF("\nRSN:%2d (TTL_EQ0)",j);
					break;
				case CPU_REASON_HW_SW_HYBRID_FWD:
					PROC_PRINTF("\nRSN:%2d (HYB_FWD)",j);
					break;
				default:
					PROC_PRINTF("\nRSN:%2d\t\t",j);
			}
			for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
				PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_Reason[j]);
		}
	}
	PROC_PRINTF("\n");

	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	PROC_PRINTF("\nIpFrag Pkt\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ipFrag);

	PROC_PRINTF("\nIpFrag Cached\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ipFrag_cached);

	PROC_PRINTF("\nHit Cache\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_hit_ipFrag_cache);

	PROC_PRINTF("\nCache Full\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ipFrag_cache_tbl_full);

	PROC_PRINTF("\nShortcut\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipFrag);

	PROC_PRINTF("\nShortcut Fail\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_ipFrag_fail);
	
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)

	PROC_PRINTF("\nDual SC IpFrag\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_shortcut_dual_ipFrag);
#endif

	PROC_PRINTF("\n");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("--------");
		}
	}

	PROC_PRINTF("\nNegFrag Cached\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_negative_ipFrag_cached);

	PROC_PRINTF("\nHit Neg Cache\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_hit_negative_ipFrag_cache);

	PROC_PRINTF("\nNeg Cache Full\t");
	for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
		PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_negative_ipFrag_cache_tbl_full);
	PROC_PRINTF("\n");

	if(fc_db.ackDelayCtrl.enable){
		PROC_PRINTF("\n");
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU+2;i++)
		{
			if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
			{
				PROC_PRINTF("--------");
			}
		}

		PROC_PRINTF("\nAck Delay Enque\t");
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ackDelayEQ);

		PROC_PRINTF("\nAck Delay Free\t");
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ackDelayFree);

		PROC_PRINTF("\nAck Delay Deque\t");
		for(i=0;i<RTK_FC_MAC_PORT_LASTCPU;i++)
			PRINTF_BY_PLATFORM_ATOMIC(i,fc_db.statistic.perPortCnt_ackDelayDQ);
		PROC_PRINTF("\n");
	}

	return len;
}

int32 rtk_fc_proc_fwdStatistic_drop_get(struct seq_file *s, void *v)
{
	int i,j;
	int len=0;

	if(fc_db.fwdStatistic==0)
	{
		rtlglue_printf("%d: disabled, please echo 1 to fwdStatistic to enable it.\n",fc_db.fwdStatistic);
		return 0;
	}

	PROC_PRINTF("%-35s", "Ingress PORT");
	for(i=0;i<RTK_FC_MAC_PORT_MAX;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("%-4d",i);
		}
	}


	PROC_PRINTF("\n================");
	for(i=0;i<RTK_FC_MAC_PORT_MAX;i++)
	{
		if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
		{
			PROC_PRINTF("====");
		}
	}

	PROC_PRINTF("\n");
	for(j=0; j < (RTK_FC_RET_DROP_END-RTK_FC_RET_DROP); j++) {

		PROC_PRINTF("%-35s", name_of_drop_type[j]);

		for(i=0;i<RTK_FC_MAC_PORT_MAX;i++)
		{
			if((i>=RTK_FC_MAC_PORT_MAINCPU)||(atomic_read(&fc_db.portLinkupMask)&(1<<i)))
			{
				int per_port_drop_cnt = atomic_read(&fc_db.statistic.perPortCnt_Drop[i][j]);
				if(per_port_drop_cnt != 0 )
					PROC_PRINTF(COLOR_Y"%-4d"COLOR_NM, per_port_drop_cnt);
				else
					PROC_PRINTF("%-4d",per_port_drop_cnt);
			}
		}
		PROC_PRINTF("\n");
	}

	return len;
}

int32 dump_hw_host_policing_mib(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	uint64 rxCnt, txCnt;
#if defined(CONFIG_RTL8198X_SERIES)
	uint32 rxPkt, txPkt;
#endif
	rtk_mac_t zeroMac;
	int idx;
	int16 hostPolConfList_idx = SIGNED_INVALID;

	memset(&zeroMac, 0 , sizeof(rtk_mac_t));
	PROC_PRINTF(">>ASIC host policing MIB Table: (Shows valid Entries only)\n");
#if defined(CONFIG_RTL8198X_SERIES)
	PROC_PRINTF("                      \t                   [Rx Bytes]\t                [Tx Bytes]\t             [Rx Pkts]\t              [Tx Pkts]\n");
#else
	PROC_PRINTF("                      \t                [Rx]\t                [Tx]\n");
#endif
	PROC_PRINTF("----------------------------------------\n");
	for(idx = 0 ; idx < RT_RATE_HOSTPOLICING_TABLE_SIZE ; idx++)
	{
		if(!memcmp(&zeroMac, fc_db.hostPoliceInfoTable[idx].hostPoliceControl.mac_addr, sizeof(rtk_mac_t)))
			continue;
		hostPolConfList_idx = fc_db.hostPoliceInfoTable[idx].hostPolConfList_idx;
		if(hostPolConfList_idx != SIGNED_INVALID)
		{
#if defined(CONFIG_RTL8198X_SERIES)
			_rtk_fc_hwHostPolingMib_get(hostPolConfList_idx, &rxCnt, &txCnt, &rxPkt, &txPkt);
			PROC_PRINTF("[%2d]%pM\t%25llu\t%25llu%25u%25u\n", idx, fc_db.hostPoliceInfoTable[idx].hostPoliceControl.mac_addr, rxCnt, txCnt, rxPkt, txPkt);
#else
			_rtk_fc_hwHostPolingMib_get(hostPolConfList_idx, &rxCnt, &txCnt);
			PROC_PRINTF("[%2d]%pM\t%20llu\t%20llu\n", idx, fc_db.hostPoliceInfoTable[idx].hostPoliceControl.mac_addr, rxCnt, txCnt);
#endif
		}
		else
			PROC_PRINTF("[Error] [%2d]: invalid hostPolConfList_idx\n", idx);

	}
	return retval;
}

int32 dump_sw_host_policing_mib(struct seq_file *s, void *v)
{
	rtk_rg_err_code_t retval=0;
	rtk_mac_t zeroMac;
	int idx;
	int16 hostPolConfList_idx = SIGNED_INVALID;

	memset(&zeroMac, 0 , sizeof(rtk_mac_t));
	PROC_PRINTF(">>SW host policing MIB Table: (Shows valid Entries only)\n");
#if defined(CONFIG_RTL8198X_SERIES)
	PROC_PRINTF("                      \t                   [Rx Bytes]\t                [Tx Bytes]\t             [Rx Pkts]\t              [Tx Pkts]\n");
#else
	PROC_PRINTF("                      \t                [Rx]\t                [Tx]\n");
#endif
	PROC_PRINTF("----------------------------------------\n");
	for(idx = 0 ; idx < RT_RATE_HOSTPOLICING_TABLE_SIZE ; idx++)
	{
		if(!memcmp(&zeroMac, fc_db.hostPoliceInfoTable[idx].hostPoliceControl.mac_addr, sizeof(rtk_mac_t)))
			continue;
		hostPolConfList_idx = fc_db.hostPoliceInfoTable[idx].hostPolConfList_idx;
		if(hostPolConfList_idx != SIGNED_INVALID)
		{
#if defined(CONFIG_RTL8198X_SERIES)
			PROC_PRINTF("[%2d]%pM\t%25llu\t%25llu%25llu%25llu\n", idx, fc_db.hostPoliceInfoTable[idx].hostPoliceControl.mac_addr, _rtk_fc_sw_hostMib_all_cpu_get(hostPolConfList_idx, RX_BYTE_CNT), _rtk_fc_sw_hostMib_all_cpu_get(hostPolConfList_idx, TX_BYTE_CNT), _rtk_fc_sw_hostMib_all_cpu_get(hostPolConfList_idx, RX_PACKET_CNT), _rtk_fc_sw_hostMib_all_cpu_get(hostPolConfList_idx, TX_PACKET_CNT));
#else
			PROC_PRINTF("[%2d]%pM\t%20llu\t%20llu\n", idx, fc_db.hostPoliceInfoTable[idx].hostPoliceControl.mac_addr, _rtk_fc_sw_hostMib_all_cpu_get(hostPolConfList_idx, RX_BYTE_CNT), _rtk_fc_sw_hostMib_all_cpu_get(hostPolConfList_idx, TX_BYTE_CNT));
#endif
		}
		else
			PROC_PRINTF("[Error] [%2d]: invalid hostPolConfList_idx\n", idx);
	}
	return retval;
}
int dump_rtMeter_table(struct seq_file *s, void *v)
{
	int vir_index, ret;
	rt_rate_meter_mapping_t meterMapping;
	char typeString[25];
	uint32 rate, burstSize;
	rtk_enable_t ifgIncludeTmp;
	rtk_rate_metet_mode_t mode;
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
#if !defined(FC_F_HW_POL_OPTIMIZE_MODE)
	rtk_rate_metet_mode_t mode_tmp;
#endif
#endif
	uint32 virMeterSize = RT_RATE_V_IDX_MAX_NUM;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	int i, j;
	enum fc_inter_meter_type_e
	{
		FC_INTER_METER_TYPE_FLOW_START = 0,
		FC_INTER_METER_TYPE_FLOW_DROP = FC_INTER_METER_TYPE_FLOW_START,
		FC_INTER_METER_TYPE_FLOW_TRAP_UMC_STORM,
		FC_INTER_METER_TYPE_FLOW_END,
	};
#endif

	char meterType[15], meterMode[25];

	PROC_PRINTF(">>Dump valid entries only:\n\n");
	PROC_PRINTF("%-9s\t%-20s\t%-9s\t%-10s\t%-10s\t%-10s\t%-15s\t%-25s\n", "rt meter","rt meter type","HW index","rate", "burst_size", "ifgInclude", "hwMeterType", "meterMode");
	PROC_PRINTF("======================================================================\n");

	if(!fc_db.controlFuc.rt_api_is_enabled)
	{
		PROC_PRINTF("Not support! (RT API is not enabled!)");
		return SUCCESS;
	}

	for(vir_index = 0 ; vir_index < virMeterSize ; vir_index++)
	{
		if(RTK_FC_HELPER_RT_RATE_SHAREMETER_MAPPING_HW_GET(vir_index, &meterMapping))
			continue;
		if(meterMapping.type == RT_METER_TYPE_STORM)
		{
			strcpy(typeString,"RT_METER_TYPE_STORM");
			//L2 meter
			ret = rtk_rate_shareMeter_get(meterMapping.hw_index, &rate, &ifgIncludeTmp);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get rate failed, ret=", ret);
				continue;
			}
			ret = rtk_rate_shareMeterBucket_get(meterMapping.hw_index, &burstSize);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get burstSize failed, ret=", ret);
				continue;
			}
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			strcpy(meterType,"switch_meter");
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
			strcpy(meterType,"l2_policer");
#endif
			ret = rtk_rate_shareMeterMode_get(meterMapping.hw_index, &mode);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get meterMode failed, ret=", ret);
				continue;
			}

			sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");
		}
		else if(meterMapping.type == RT_METER_TYPE_HOST)
		{
			strcpy(typeString,"RT_METER_TYPE_HOST");
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			//L2 meter
			ret = rtk_rate_shareMeter_get(meterMapping.hw_index, &rate, &ifgIncludeTmp);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get rate failed, ret=", ret);
				continue;
			}

			ret = rtk_rate_shareMeterBucket_get(meterMapping.hw_index, &burstSize);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get burstSize failed, ret=", ret);
				continue;
			}
			strcpy(meterType,"switch_meter");

			ret = rtk_rate_shareMeterMode_get(meterMapping.hw_index, &mode);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get meterMode failed, ret=", ret);
				continue;
			}
			sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");

#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
			//L3 meter (loopback mode disable), L2 meter (loopback mode enable),
			if(fc_db.controlFuc.loopbackMode_is_enabled)
				ret = rtk_rate_shareMeter_get(meterMapping.hw_index, &rate, &ifgIncludeTmp);
			else
				ret = _rtk_fc_l3Meter_get(RT_RATE_EXT_METER_TYPE_HOST, meterMapping.hw_index, &rate, &ifgIncludeTmp);
			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get rate failed, ret=", ret);
				continue;
			}

			if(fc_db.controlFuc.loopbackMode_is_enabled)
				ret = rtk_rate_shareMeterBucket_get(meterMapping.hw_index, &burstSize);
			else
				ret = _rtk_fc_l3MeterBucket_get(RT_RATE_EXT_METER_TYPE_HOST, meterMapping.hw_index, &burstSize);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get burstSize failed, ret=", ret);
				continue;
			}
			if(fc_db.controlFuc.loopbackMode_is_enabled)
				strcpy(meterType,"l2_policer");
			else
				strcpy(meterType,"l3_policer1");

			if(fc_db.controlFuc.loopbackMode_is_enabled)
				ret = rtk_rate_shareMeterMode_get(meterMapping.hw_index, &mode);
			else
				ret = _rtk_fc_l3MeterRateMode_get(RT_RATE_EXT_METER_TYPE_HOST, meterMapping.hw_index, &mode);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get meterMode failed, ret=", ret);
				continue;
			}
			sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");
#endif
		}
		else if(meterMapping.type == RT_METER_TYPE_FLOW)
		{
			strcpy(typeString,"RT_METER_TYPE_FLOW");
			//L3 meter
			ret = _rtk_fc_l3Meter_get(RT_RATE_EXT_METER_TYPE_FLOW, meterMapping.hw_index, &rate, &ifgIncludeTmp);
			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get rate failed, ret=", ret);
				continue;
			}

			ret = _rtk_fc_l3MeterBucket_get(RT_RATE_EXT_METER_TYPE_FLOW, meterMapping.hw_index, &burstSize);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get burstSize failed, ret=", ret);
				continue;
			}
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			strcpy(meterType,"fb_meter");
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_EXTRA)
				strcpy(meterType,"sw only");
			else
				strcpy(meterType,"l3_policer2");
#else
			strcpy(meterType,"l3_policer1");
#endif
#endif
			ret = _rtk_fc_l3MeterRateMode_get(RT_RATE_EXT_METER_TYPE_FLOW, meterMapping.hw_index, &mode);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get meterMode failed, ret=", ret);
				continue;
			}
			sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");
		}
		else if(meterMapping.type == RT_METER_TYPE_ACL)
		{
			strcpy(typeString,"RT_METER_TYPE_ACL");
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
			//L2 meter
			ret = rtk_rate_shareMeter_get(meterMapping.hw_index, &rate, &ifgIncludeTmp);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get rate failed, ret=", ret);
				continue;
			}

			ret = rtk_rate_shareMeterBucket_get(meterMapping.hw_index, &burstSize);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get burstSize failed, ret=", ret);
				continue;
			}
			strcpy(meterType,"switch_meter");

			ret = rtk_rate_shareMeterMode_get(meterMapping.hw_index, &mode);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get meterMode failed, ret=", ret);
				continue;
			}
			sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");
#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
			//L3 meter
			ret = _rtk_fc_l3Meter_get(RT_RATE_EXT_METER_TYPE_ACL, meterMapping.hw_index, &rate, &ifgIncludeTmp);
			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get rate failed, ret=", ret);
				continue;
			}

			ret = _rtk_fc_l3MeterBucket_get(RT_RATE_EXT_METER_TYPE_ACL, meterMapping.hw_index, &burstSize);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get burstSize failed, ret=", ret);
				continue;
			}
			strcpy(meterType,"l3_policer1");

			ret = _rtk_fc_l3MeterRateMode_get(RT_RATE_EXT_METER_TYPE_ACL, meterMapping.hw_index, &mode);

			if(ret)
			{
				PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "get meterMode failed, ret=", ret);
				continue;
			}
#if defined(FC_F_HW_POL_OPTIMIZE_MODE)
			// no need CLS policer remapping table
			sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");
#else
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			// _rtk_fc_l3MeterRateMode_get is get from polRemapping, get pol1 HW here
			ret = _rtk_fc_g3L3PolicerRateMode_get(meterMapping.hw_index, &mode_tmp);

			if(ret)
			{
				PROC_PRINTF("HW %-9d\terror:%s %d\n", meterMapping.hw_index, "get HW meterMode failed, ret=", ret);
				continue;
			}
			sprintf(meterMode, "%s(pol1:%s)", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec", (mode_tmp == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");
#else
			sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");
#endif
#endif
#endif

		}
		else if(meterMapping.type == RT_METER_TYPE_SW)
		{
			strcpy(typeString,"RT_METER_TYPE_SW");
			rate = fc_db.swMeter[meterMapping.hw_index].rate;
			burstSize = 0;
			ifgIncludeTmp = fc_db.swMeter[meterMapping.hw_index].ifgInclude;
			strcpy(meterType,"sw_meter");
			strcpy(meterMode,"bits/sec"); // support bps only now.
		}
		else
		{
			PROC_PRINTF("%-9d\terror:%s %d\n", vir_index, "invalid meter type", meterMapping.type);
			continue;
		}
#if defined(CONFIG_FC_CAG3_SERIES) || defined(CONFIG_FC_CA8277B_SERIES)
		//8277: sync to policer[hwidx+G3_FLOW_POLICER_IDXSHIFT_FLOWMTR], 8277B: set to policer3[hwidx+G3_FLOW_POLICER_IDXSHIFT_FLOWMTR]
		if(meterMapping.type == RT_METER_TYPE_FLOW)
			PROC_PRINTF("%-9d\t%-20s\t%-9d\t%-10d\t%-10d\t%-10s\t%-15s\t%-25s (FC shift hw_index to %d)\n", vir_index, typeString, meterMapping.hw_index, rate, burstSize, (ifgIncludeTmp==DISABLED)?"DISABLED":"ENABLED", meterType, meterMode, meterMapping.hw_index+G3_FLOW_POLICER_IDXSHIFT_FLOWMTR);
		else
#endif
		{
			PROC_PRINTF("%-9d\t%-20s\t%-9d\t%-10d\t%-10d\t%-10s\t%-15s\t%-25s\n", vir_index, typeString, meterMapping.hw_index, rate, burstSize, (ifgIncludeTmp==DISABLED)?"DISABLED":"ENABLED", meterType, meterMode);
		}
	}
#if defined(FC_F_HW_POL_OPTIMIZE_MODE)
	// no need CLS policer remapping table
#else
#if defined(CONFIG_FC_CA8277B_SERIES) || defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("\n");
	PROC_PRINTF("%-12s\t%-20s\t%-9s\t%-10s\t%-10s\t%-10s\t%-15s\t%-25s\n", "remap meter","rt meter type","HW index","rate", "burst_size", "ifgInclude", "hwMeterType", "meterMode");
	PROC_PRINTF("======================================================================\n");

	for(vir_index = 0, virMeterSize = RTK_FC_TABLESIZE_ACL_POLREMAP_MAX; vir_index < virMeterSize ; vir_index++)
	{
		if(!fc_db.hwPolRemap[vir_index].valid)
			continue;
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		strcpy(meterType,"l3_policer3");
		ret = _rtk_fc_g3L3Policer3_get(vir_index, &rate, &ifgIncludeTmp);
#else
		strcpy(meterType,"l3_policer2");
		ret = _rtk_fc_g3L3Policer2_get(vir_index, &rate, &ifgIncludeTmp);
#endif
		if(ret)
		{
			PROC_PRINTF("%-12d\terror:%s %d\n", vir_index, "get rate failed, ret=", ret);
			continue;
		}
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		ret = _rtk_fc_g3L3Policer3BurstSize_get(vir_index, &burstSize);
#else
		ret = _rtk_fc_g3L3Policer2BurstSize_get(vir_index, &burstSize);
#endif
		if(ret)
		{
			PROC_PRINTF("%-12d\terror:%s %d\n", vir_index, "get burstSize failed, ret=", ret);
			continue;
		}
		strcpy(typeString,"RT_METER_TYPE_ACL");

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		ret = _rtk_fc_g3L3Policer3RateMode_get(vir_index, &mode);
#else
		ret = _rtk_fc_g3L3Policer2RateMode_get(vir_index, &mode);
#endif

		if(ret)
		{
			PROC_PRINTF("%-12d\terror:%s %d\n", vir_index, "get meterMode failed, ret=", ret);
			continue;
		}
		sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");

		PROC_PRINTF("%-12d\t%-20s\t%-9d\t%-10d\t%-10d\t%-10s\t%-15s\t%-25s\n", vir_index, typeString, fc_db.hwPolRemap[vir_index].pol_id, rate, burstSize, (ifgIncludeTmp==DISABLED)?"DISABLED":"ENABLED", meterType, meterMode);
	}
#endif
#endif

#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
	PROC_PRINTF("\n");
	PROC_PRINTF("%-32s\t%-9s\t%-10s\t%-10s\t%-10s\t%-15s\t%-25s\n", "fc inter meter","HW index","rate", "burst_size", "ifgInclude", "hwMeterType", "meterMode");
	PROC_PRINTF("======================================================================\n");

	for(i = FC_INTER_METER_TYPE_FLOW_START ; i < FC_INTER_METER_TYPE_FLOW_END ; i++)
	{
		int count = 0, idx_shift = 0;;
		switch(i)
		{
			case FC_INTER_METER_TYPE_FLOW_DROP:
				count = G3_FLOW_POLICER_HASH_DFT_DROP_SIZE;
				idx_shift = G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_DROP;
				strncpy(typeString,"INTER_METER_FLOW_DROP", sizeof(typeString));
				strncpy(meterType,"l3_policer1", sizeof(meterType));
				for(j = 0 ; j < G3_FLOW_POLICER_HASH_DFT_DROP_SIZE ; j++)
				{
					ret = _rtk_fc_g3L3Policer_get(idx_shift+j, &rate, &ifgIncludeTmp);
					if(ret)
					{
						PROC_PRINTF("%-12d\terror:%s %d\n", idx_shift+j, "get rate failed, ret=", ret);
						continue;
					}
					ret = _rtk_fc_g3L3PolicerBurstSize_get(idx_shift+j, &burstSize);
					if(ret)
					{
						PROC_PRINTF("%-12d\terror:%s %d\n", idx_shift+j, "get burstSize failed, ret=", ret);
						continue;
					}

					ret = _rtk_fc_g3L3PolicerRateMode_get(idx_shift+j, &mode);
					if(ret)
					{
						PROC_PRINTF("%-12d\terror:%s %d\n", idx_shift+j, "get meterMode failed, ret=", ret);
						continue;
					}

					sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");
					PROC_PRINTF("%-32s\t%-9d\t%-10d\t%-10d\t%-10s\t%-15s\t%-25s\n", typeString, idx_shift+j, rate, burstSize, (ifgIncludeTmp==DISABLED)?"DISABLED":"ENABLED", meterType, meterMode);
				}
				break;
			case FC_INTER_METER_TYPE_FLOW_TRAP_UMC_STORM:
				count = G3_FLOW_POLICER_HASH_DFT_UMC_TRAP_SIZE;
				idx_shift = G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP;
				strncpy(typeString,"INTER_METER_FLOW_UMC_STM", sizeof(typeString));
				strncpy(meterType,"l3_policer1", sizeof(meterType));
				for(j = 0 ; j < G3_FLOW_POLICER_HASH_DFT_UMC_TRAP_SIZE ; j++)
				{
					ret = _rtk_fc_g3L3Policer_get(idx_shift+j, &rate, &ifgIncludeTmp);
					if(ret)
					{
						PROC_PRINTF("%-12d\terror:%s %d\n", idx_shift+j, "get rate failed, ret=", ret);
						continue;
					}
					ret = _rtk_fc_g3L3PolicerBurstSize_get(idx_shift+j, &burstSize);
					if(ret)
					{
						PROC_PRINTF("%-12d\terror:%s %d\n", idx_shift+j, "get burstSize failed, ret=", ret);
						continue;
					}

					ret = _rtk_fc_g3L3PolicerRateMode_get(idx_shift+j, &mode);
					if(ret)
					{
						PROC_PRINTF("%-12d\terror:%s %d\n", idx_shift+j, "get meterMode failed, ret=", ret);
						continue;
					}

					sprintf(meterMode, "%s", (mode == METER_MODE_BIT_RATE)?"bits/sec":"pkts/sec");
					PROC_PRINTF("%-32s\t%-9d\t%-10d\t%-10d\t%-10s\t%-15s\t%-25s\n", typeString, idx_shift+j, rate, burstSize, (ifgIncludeTmp==DISABLED)?"DISABLED":"ENABLED", meterType, meterMode);
				}
				break;
			//default:
				//count = 0;
				//break;
		}
	}

	PROC_PRINTF("\nfc_db.controlFuc.flow_mib_mode: %s\n", (fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_DEFAULT)?"FLOW_MIB_MODE_DEFAULT":"FLOW_MIB_MODE_EXTRA");
	if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_DEFAULT)
		PROC_PRINTF("flow meter configuration: set to both HW and SW.l34meter\n\n");
	else if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_DEFAULT)
		PROC_PRINTF("flow meter configuration: set to both HW and SW.l34meter\n\n");

#endif

	return SUCCESS;
}

int dump_rtMeter_debugging_table(struct file *file, const char *buffer, unsigned long count, void *data)
{
	int idx = _rtk_fc_proc_parsing_string_to_integer(buffer, count);
	if(idx != 1)
	{
		rtlglue_printf("\nUsage:\n");
		rtlglue_printf("     HW meter detail: echo 1 > /proc/fc/sw_dump/rt_meter \n");
	}
	else
	{
		if(!fc_db.controlFuc.rt_api_is_enabled)
		{
			rtlglue_printf("Not support! (RT API is not enabled!)");
			return count;
		}
		rtlglue_printf("===== "COLOR_Y"Meter Usage:"COLOR_NM" =====\n");
#if defined(CONFIG_RTK_L34_XPON_PLATFORM)
		rtlglue_printf("L2 (switch) meter size:     %-3d\n", RTK_FC_TABLESIZE_SHAREMTR);
		rtlglue_printf("[%03d]-[%03d]: storm control, ACL rate limit, host policing rate limiting (HW policing)\n", 0, RTK_FC_TABLESIZE_SHAREMTR-1);
		rtlglue_printf("\n");

		rtlglue_printf("L3 (fb) meter size:         %-3d\n", RTK_FC_TABLESIZE_FBMTR);
		rtlglue_printf("[%03d]-[%03d]: flow meter. (HW policing and FC SW shaping)\n", 0, RTK_FC_TABLESIZE_FBMTR-1);
		rtlglue_printf("\n");

#elif defined(CONFIG_RTK_L34_G3_PLATFORM)
#if defined(CONFIG_FC_CA8277B_SERIES)
		rtlglue_printf("L2 policer size:            %-3d\n", G3_L2_FLOW_POLICER_SIZE);
		rtlglue_printf("\tstorm control. (with L2 packet type policer)\n");
		rtlglue_printf("\n");

		rtlglue_printf("L3 policer1 size:           %-3d\n", G3_L3_FLOW_POLICER_SIZE);
		rtlglue_printf("\t[%03d]-[%03d]: for RT meter use. (Reserved. (policer for PON gemID assignment))\n", 0, G3_L3_FLOW_POLICER_SIZE_METER-1);
		rtlglue_printf("\n");

		rtlglue_printf("L3 policer2 size:           %-3d\n", G3_L3_FLOW_POLICER2_SIZE);
		rtlglue_printf("\t[%03d]-[%03d]: ACL rate limiting.\n", 0, RTK_FC_ACL_POLREMAP_CNT-1);
		rtlglue_printf("\t[%03d]-[%03d]: flow meter (HW policing and FC SW shaping) + flow mib. (flow_meter_mib_conf_dependence always 1)\n", G3_FLOW_POLICER_IDXSHIFT_FLOWMTR, G3_FLOW_POLICER_IDXSHIFT_FLOWMTR+RTK_FC_TABLESIZE_FBMTR-1);
		rtlglue_printf("\n");

		rtlglue_printf("L3 policer3 size:           %-3d\n", G3_L3_FLOW_POLICER3_SIZE);
		rtlglue_printf("\t[%03d]-[%03d]: host rx logging. (do not support hw host rate limiting)\n", G3_FLOW_POLICER_IDXSHIFT_HPLOGRX, G3_FLOW_POLICER_IDXSHIFT_HPLOGRX+RTK_FC_HOSTPOLICING_TABLE_HW_SIZE-1);
		rtlglue_printf("\t[%03d]-[%03d]: host tx logging. (do not support hw host rate limiting)\n", G3_FLOW_POLICER_IDXSHIFT_HPLOGTX, G3_FLOW_POLICER_IDXSHIFT_HPLOGTX+RTK_FC_HOSTPOLICING_TABLE_HW_SIZE-1);
		rtlglue_printf("\n");

#elif defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
		rtlglue_printf("L2 policer size:            %-3d\n", 0);
		rtlglue_printf("\tstorm control. (with L2 packet type policer)\n");
		rtlglue_printf("\n");
#if defined(FC_F_HW_POL_OPTIMIZE_MODE)
		rtlglue_printf("L3 policer1 size:           %-3d\n", G3_L3_FLOW_POLICER_SIZE);
		rtlglue_printf("\t[%03d]-[%03d]: for RT host use. (host policing rate limiting (HW policing) or host logging)\n", 0, RTK_FC_HW_POL1_HOST_SIZE-1);
		rtlglue_printf("\t[%03d]-[%03d]: Hash default action drop.\n", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_DROP, G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_DROP+G3_FLOW_POLICER_HASH_DFT_DROP_SIZE-1);
		rtlglue_printf("\t[%03d]-[%03d]: Hash default action umc trap with rate limit.\n", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP+G3_FLOW_POLICER_HASH_DFT_UMC_TRAP_SIZE-1);
		rtlglue_printf("\t[%03d]-[%03d]: mc netif rx mib.\n", G3_FLOW_POLICER_IDXSHIFT_MC_NETIF_RXMIB, G3_FLOW_POLICER_IDXSHIFT_MC_NETIF_RXMIB+G3_FLOW_POLICER_MC_NETIF_RXMIB_SIZE-1);
		rtlglue_printf("\t[%03d]-[%03d]: ACL rate limiting\n", RTK_FC_HW_POL_OPTIMIZE_ACL_HWIDX_BASE, RTK_FC_HW_POL_OPTIMIZE_ACL_HWIDX_BASE+RTK_FC_HW_POL1_ACL_SIZE-1);
		rtlglue_printf("\n");

		if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_DEFAULT)
		{
			rtlglue_printf("L3 policer2 size:           %-3d\n", G3_L3_FLOW_POLICER2_SIZE);
			rtlglue_printf("\t[%03d]-[%03d]: %s\n", G3_FLOW_POLICER_IDXSHIFT_FLOWMTR, G3_FLOW_POLICER_IDXSHIFT_FLOWMTR+RTK_FC_TABLESIZE_FBMTR-1, (fc_db.controlFuc.flow_meter_mib_conf_dependence)?"flow meter (HW policing and FC SW shaping) + flow mib.":"flow meter (HW policing and FC SW shaping).");
			rtlglue_printf("\t[%03d]-[%03d]: ACL mib.\n", G3_FLOW_POLICER_IDXSHIFT_ACLMIB,G3_FLOW_POLICER_IDXSHIFT_ACLMIB+RT_ACL_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
			rtlglue_printf("\n");

			rtlglue_printf("L3 policer3 size:           %-3d\n", G3_L3_FLOW_POLICER3_SIZE);
			if(!fc_db.controlFuc.flow_meter_mib_conf_dependence)
				rtlglue_printf("\t[%03d]-[%03d]: flow mib.\n", G3_FLOW_POLICER_IDXSHIFT_FLOWMIB, G3_FLOW_POLICER_IDXSHIFT_FLOWMIB+RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
			rtlglue_printf("\n");
		}
		else if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_EXTRA)
		{
			rtlglue_printf("L3 policer2 size:           %-3d\n", G3_L3_FLOW_POLICER2_SIZE);
			if(!fc_db.controlFuc.flow_meter_mib_conf_dependence)
				rtlglue_printf("\t[%03d]-[%03d]: flow mib.\n", G3_FLOW_POLICER_IDXSHIFT_FLOWMIB, G3_FLOW_POLICER_IDXSHIFT_FLOWMIB+RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
			rtlglue_printf("\n");

			rtlglue_printf("L3 policer3 size:           %-3d\n", G3_L3_FLOW_POLICER3_SIZE);
			rtlglue_printf("\t[%03d]-[%03d]: flow mib2.\n", G3_FLOW_POLICER_IDXSHIFT_FLOWMIB2, G3_FLOW_POLICER_IDXSHIFT_FLOWMIB2+RT_FLOW_MIB2_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
			rtlglue_printf("\n");
		}
		else
			WARNING("Strange flow_mib_mode %u", fc_db.controlFuc.flow_mib_mode);
#else
		rtlglue_printf("L3 policer1 size:           %-3d\n", G3_L3_FLOW_POLICER_SIZE);
		rtlglue_printf("\t[%03d]-[%03d]: for RT meter use. (host policing rate limiting (HW policing))\n", 0, G3_L3_FLOW_POLICER_SIZE_METER-1);
		rtlglue_printf("\t[%03d]-[%03d]: Hash default action drop.\n", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_DROP, G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_DROP+G3_FLOW_POLICER_HASH_DFT_DROP_SIZE-1);
		rtlglue_printf("\t[%03d]-[%03d]: Hash default action umc trap with rate limit.\n", G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP, G3_FLOW_POLICER_IDXSHIFT_HASH_DFT_UMC_TRAP+G3_FLOW_POLICER_HASH_DFT_UMC_TRAP_SIZE-1);
		rtlglue_printf("\t[%03d]-[%03d]: mc netif rx mib.\n", G3_FLOW_POLICER_IDXSHIFT_MC_NETIF_RXMIB, G3_FLOW_POLICER_IDXSHIFT_MC_NETIF_RXMIB+G3_FLOW_POLICER_MC_NETIF_RXMIB_SIZE-1);
		rtlglue_printf("\t[%03d]-[%03d]: host rx logging.\n", G3_FLOW_POLICER_IDXSHIFT_HPLOGRX, G3_FLOW_POLICER_IDXSHIFT_HPLOGRX+RTK_FC_HOSTPOLICING_TABLE_HW_SIZE-1);
		rtlglue_printf("\t[%03d]-[%03d]: host tx logging.\n", G3_FLOW_POLICER_IDXSHIFT_HPLOGTX, G3_FLOW_POLICER_IDXSHIFT_HPLOGTX+RTK_FC_HOSTPOLICING_TABLE_HW_SIZE-1);
		rtlglue_printf("\n");

		if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_DEFAULT)
		{
			rtlglue_printf("L3 policer2 size:           %-3d\n", G3_L3_FLOW_POLICER2_SIZE);
			rtlglue_printf("\t[%03d]-[%03d]: %s\n", G3_FLOW_POLICER_IDXSHIFT_FLOWMTR, G3_FLOW_POLICER_IDXSHIFT_FLOWMTR+RTK_FC_TABLESIZE_FBMTR-1, (fc_db.controlFuc.flow_meter_mib_conf_dependence)?"flow meter (HW policing and FC SW shaping) + flow mib.":"flow meter (HW policing and FC SW shaping).");
			rtlglue_printf("\t[%03d]-[%03d]: ACL mib.\n", G3_FLOW_POLICER_IDXSHIFT_ACLMIB,G3_FLOW_POLICER_IDXSHIFT_ACLMIB+RT_ACL_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
			rtlglue_printf("\n");

			rtlglue_printf("L3 policer3 size:           %-3d\n", G3_L3_FLOW_POLICER3_SIZE);
			rtlglue_printf("\t[%03d]-[%03d]: ACL rate limiting\n", 0, RTK_FC_ACL_POLREMAP_CNT-1);
			if(!fc_db.controlFuc.flow_meter_mib_conf_dependence)
				rtlglue_printf("\t[%03d]-[%03d]: flow mib.\n", G3_FLOW_POLICER_IDXSHIFT_FLOWMIB, G3_FLOW_POLICER_IDXSHIFT_FLOWMIB+RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
			rtlglue_printf("\n");
		}
		else if(fc_db.controlFuc.flow_mib_mode == RT_STAT_FLOW_MIB_MODE_EXTRA)
		{
			rtlglue_printf("L3 policer2 size:           %-3d\n", G3_L3_FLOW_POLICER2_SIZE);
			if(!fc_db.controlFuc.flow_meter_mib_conf_dependence)
				rtlglue_printf("\t[%03d]-[%03d]: flow mib.\n", G3_FLOW_POLICER_IDXSHIFT_FLOWMIB, G3_FLOW_POLICER_IDXSHIFT_FLOWMIB+RT_FLOW_MIB_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
			rtlglue_printf("\n");

			rtlglue_printf("L3 policer3 size:           %-3d\n", G3_L3_FLOW_POLICER3_SIZE);
			rtlglue_printf("\t[%03d]-[%03d]: ACL rate limiting\n", 0, RTK_FC_ACL_POLREMAP_CNT-1);
			rtlglue_printf("\t[%03d]-[%03d]: flow mib2.\n", G3_FLOW_POLICER_IDXSHIFT_FLOWMIB2, G3_FLOW_POLICER_IDXSHIFT_FLOWMIB2+RT_FLOW_MIB2_SIZE_GET(fc_db.controlFuc.flow_mib_mode)-1);
			rtlglue_printf("\n");
		}
		else
			WARNING("Strange flow_mib_mode %u", fc_db.controlFuc.flow_mib_mode);
#endif
#else
		rtlglue_printf("do not be displayed here!!\n");
#endif
#endif
		rtlglue_printf("SW meter size:              %-3d\n", RTK_FC_TABLESIZE_SW_SHAREMTR);
		rtlglue_printf("\t[%03d]-[%03d]: host rate limiting (FC SW shaping).\n", 0, RTK_FC_TABLESIZE_SW_SHAREMTR-1);
		rtlglue_printf("\n");

#if defined(FC_F_HW_POL_OPTIMIZE_MODE)
		{
			int i, j, count;

			rtlglue_printf("============================================================\n");
			rtlglue_printf("SW meter is in used:\n\t");
			count = 0;
			for(i = 0 ; i < RTK_FC_SWMETER_USED_BITMAP32_SIZE ; i++)
			{
				for(j = 0 ; j < 32 ; j++)
				{
					if((fc_db.swMeterIfUsed_32bitmap[i] >> j)&0x1)
					{
						rtlglue_printf("[%03d] ", (i<<5)|j);
						if(((++count)%10) == 0)
							rtlglue_printf("\n\t");
					}
				}
			}
			if(!count)rtlglue_printf("none.");
			rtlglue_printf("\n");
			rtlglue_printf("flow meter (policer2) is in used:\n\t");
			count = 0;
			for(i = 0 ; i < RTK_FC_L34METER_USED_BITMAP32_SIZE ; i++)
			{
				for(j = 0 ; j < 32 ; j++)
				{
					if((fc_db.l34MeterIfUsed_32bitmap[i] >> j)&0x1)
					{
						rtlglue_printf("[%03d] ", (i<<5)|j);
						if(((++count)%10) == 0)
							rtlglue_printf("\n\t");
					}
				}
			}
			if(!count)rtlglue_printf("none.");
			rtlglue_printf("\n");
			rtlglue_printf("ACL meter (policer1) is in used:\n\t");
			count = 0;
			for(i = 0 ; i < RTK_FC_HW_POL1_ACL_USED_BITMAP32_SIZE ; i++)
			{
				for(j = 0 ; j < 32 ; j++)
				{
					if((fc_db.pol1ACLIfUsed_32bitmap[i] >> j)&0x1)
					{
						rtlglue_printf("[%03d] ", ((i<<5)|j)+RTK_FC_HW_POL_OPTIMIZE_ACL_HWIDX_BASE);
						if(((++count)%10) == 0)
							rtlglue_printf("\n\t");
					}
				}
			}
			if(!count)rtlglue_printf("none.");
			rtlglue_printf("\n");
			rtlglue_printf("host meter/mib (policer1) is in used:\n\t");
			count = 0;
			for(i = 0 ; i < RTK_FC_HW_POL1_HOST_SIZE ; i++)
			{
				if(fc_db.pol1HostIfUsed_info[i].if_used)
				{
					rtlglue_printf("[%03d]:%s ", i, (fc_db.pol1HostIfUsed_info[i].used_type == POL1HOSTHWINUSETYPE_RT_METER)?"RT_METER":"HOST_MIB");
					if(((++count)%5) == 0)
						rtlglue_printf("\n\t");
				}
			}
			rtlglue_printf("\n");
		}
#endif

	}
	return count;
}


int dump_swShaper_table(struct seq_file *s, void *v)
{
	int i = 0;

	for(i = 0; i < RTK_FC_TABLESIZE_SW_SHAPING; i++){
		
		if((fc_db.shapingCtrl[i].enququcnt+fc_db.shapingCtrl[i].pausetxcnt) != 0U) {
			if(i < RTK_FC_SW_SHAPING_CTRL_IDX_OFFSET_SWMETER)
			{
				//SW flow meter shaping
				PROC_PRINTF("shaper[%d]: free:%d sched:%d cong:%d enqcnt:%d deqcnt:%d\n",
							i, atomic_read(&fc_db.shapingCtrl[i].free_idx), atomic_read(&fc_db.shapingCtrl[i].sched_idx), 
							atomic_read(&fc_db.shapingCtrl[i].congestion), fc_db.shapingCtrl[i].enququcnt, fc_db.shapingCtrl[i].pausetxcnt);

				PROC_PRINTF("\tuse flow meter[%d] rate:%d kbps\n", i, fc_db.l34Meter[i].rate);
			}
			else
			{
				//SW host shaping
				PROC_PRINTF("shaper[%d]: free:%d sched:%d cong:%d enqcnt:%d deqcnt:%d\n",
							i, atomic_read(&fc_db.shapingCtrl[i].free_idx), atomic_read(&fc_db.shapingCtrl[i].sched_idx), 
							atomic_read(&fc_db.shapingCtrl[i].congestion), fc_db.shapingCtrl[i].enququcnt, fc_db.shapingCtrl[i].pausetxcnt);

				PROC_PRINTF("\tuse sw meter[%d] rate:%d kbps\n", i-RTK_FC_SW_SHAPING_CTRL_IDX_OFFSET_SWMETER, fc_db.swMeter[i-RTK_FC_SW_SHAPING_CTRL_IDX_OFFSET_SWMETER].rate);
			}
		}
	}
	return SUCCESS;
}

int dump_sw_ackDelay(struct seq_file *s, void *v)
{
#if defined(CONFIG_RTK_FC_SW_ACK_DELAY_WIDTH) && (CONFIG_RTK_FC_SW_ACK_DELAY_WIDTH > 5)
	int i = 0;
	rtk_fc_ackDelay_linkList_t *pAckDelay = NULL;

	PROC_PRINTF("Current jiffies: %lu\n", jiffies);

	for(i=0; i<RTK_FC_ACKDELAY_ENTRY_SIZE; i++) {
		if(fc_db.ackDelayList[i].valid == 0)
			continue;
		pAckDelay = &fc_db.ackDelayList[i];
		PROC_PRINTF("[ACKdelay][%d(hash %d)] crc16 0x%x, crc32 0x%x, l4_offset %d, seq_no %d, expireJiffies %lu, pktNum %d, skb %p\n", pAckDelay->list_idx, RTK_FC_ACKDELAY_HASH_IDX(pAckDelay->crc16), pAckDelay->crc16, pAckDelay->crc32, pAckDelay->l4_offset, pAckDelay->seq_no, pAckDelay->expireJiffies, pAckDelay->pktNum, pAckDelay->skb);
	}
#endif

	return SUCCESS;
}

int dump_dual_passthrough_flowMapping_table(struct seq_file *s, void *v)
{
	int i = 0;

	PROC_PRINTF("[0]: reserved entry\n");

	for(i = 1; i < RTK_FC_DUAL_PASSTHROUGH_FLOWMAPPING_SIZE; i++)
	{
		
		if(fc_db.dual_pt_flowMapTbl[i].dual_type == RTK_FC_DUALTYPE_NONE)
			continue; //invavid, skip
		else if(fc_db.dual_pt_flowMapTbl[i].dual_type == RTK_FC_DUALTYPE_PPTP)
		{
			PROC_PRINTF("[%d]: PPTP (flowIdx: %d)\n",i, fc_db.dual_pt_flowMapTbl[i].refFlowIdx);
			PROC_PRINTF("   -In-callId: %d Out-callId: %d\n",  fc_db.dual_pt_flowMapTbl[i].pptp_flowMapping.in_gre_call_id, fc_db.dual_pt_flowMapTbl[i].pptp_flowMapping.out_gre_call_id);
		}
		else if(fc_db.dual_pt_flowMapTbl[i].dual_type == RTK_FC_DUALTYPE_L2TP)
		{
			PROC_PRINTF("[%d]: L2TP (flowIdx: %d\n", i, fc_db.dual_pt_flowMapTbl[i].refFlowIdx);
			PROC_PRINTF("   - tunnelID: %d, sesseionID: %d\n", fc_db.dual_pt_flowMapTbl[i].l2tp_flowMapping.in_l2tp_tunnel_id, fc_db.dual_pt_flowMapTbl[i].l2tp_flowMapping.in_l2tp_session_id);
		}
		else if(fc_db.dual_pt_flowMapTbl[i].dual_type == RTK_FC_DUALTYPE_IPSEC)
		{
			PROC_PRINTF("[%d]: IPsec (flowIdx: %d\n", i, fc_db.dual_pt_flowMapTbl[i].refFlowIdx);
			PROC_PRINTF("   - spi: 0x%x\n", fc_db.dual_pt_flowMapTbl[i].ipsec_flowMapping.esp_spi);
		}
	}

	return SUCCESS;
}
int dump_ipv6_nat_mapping_table(struct seq_file *s, void *v)
{
	int i = 0;

	PROC_PRINTF("-------------------------------------\n");
	PROC_PRINTF("[0]: reserved entry\n");

	for(i = 1; i < RTK_FC_TABLESIZE_I6NAT_MAPPING_TABLE; i++)
	{
		
		if(atomic_read(&fc_db.ipv6_nat_mappingTbl[i].refCount )!=0 )
		{
			PROC_PRINTF("-------------------------------------\n");
			PROC_PRINTF("[%d]: V6 Address: %pI6c (refCnt: %d)\n", i, &fc_db.ipv6_nat_mappingTbl[i].addr, atomic_read(&fc_db.ipv6_nat_mappingTbl[i].refCount));
			PROC_PRINTF("      - direction %d \n", fc_db.ipv6_nat_mappingTbl[i].direction);
			PROC_PRINTF("      - Original-CVID %d \n", fc_db.ipv6_nat_mappingTbl[i].oriCVID);
			PROC_PRINTF("      - Original-CPRI %d \n", fc_db.ipv6_nat_mappingTbl[i].oriCPRI);
			PROC_PRINTF("      - Original-SVID %d \n", fc_db.ipv6_nat_mappingTbl[i].oriSVID);
			PROC_PRINTF("      - Original-SPRI %d \n", fc_db.ipv6_nat_mappingTbl[i].oriSPRI);	
			PROC_PRINTF("      - isNPTv6 %d \n", fc_db.ipv6_nat_mappingTbl[i].isNPTv6);	
			PROC_PRINTF("      - wan_prefix_len %d \n", fc_db.ipv6_nat_mappingTbl[i].wan_prefix_len);	
			PROC_PRINTF("      - lan_prefix_len %d \n", fc_db.ipv6_nat_mappingTbl[i].lan_prefix_len);	
#if defined(CONFIG_FC_RTL8277C_SERIES) || defined(CONFIG_FC_RTL9607F_SERIES)
			PROC_PRINTF("      - prefix_idx %d \n", fc_db.ipv6_nat_mappingTbl[i].prefix_idx);	
#endif
			
		}

		
	}

	return SUCCESS;
}

int dump_ipv6_mapt_mapping_table(struct seq_file *s, void *v)
{
	int i = 0;

	PROC_PRINTF("-------------------------------------\n");

	for(i = RTK_FC_I6MAPT_TABLE_START_INDEX; i < RTK_FC_TABLESIZE_I6MAPT_MAPPING_TABLE; i++)
	{
		if(atomic_read(&fc_db.ipv6_mapt_mappingTbl[i].refCount )!=0 )
		{
			PROC_PRINTF("-------------------------------------\n");
			if(fc_db.ipv6_mapt_mappingTbl[i].isHash)
				PROC_PRINTF("[%d]: V6 hash: 0x%x (refCnt: %d)\n", i, fc_db.ipv6_mapt_mappingTbl[i].v6_hash_value, atomic_read(&fc_db.ipv6_mapt_mappingTbl[i].refCount));
			else
				PROC_PRINTF("[%d]: V6 Address: %pI6c (refCnt: %d)\n", i, &fc_db.ipv6_mapt_mappingTbl[i].addr, atomic_read(&fc_db.ipv6_mapt_mappingTbl[i].refCount));
		}		
	}

	return SUCCESS;
}
#if defined(CONFIG_FC_CA8277B_SERIES)

int dump_vxlan_extra_record(struct seq_file *s, void *v)
{
	int i ;
	if(fc_db.controlFuc.special_fast_forward_mode==1)
	{
		PROC_PRINTF("EXTRA VXLAN special fast-forward upstream number :%d\n",fc_db.vxlan_us_extra_fastForward_num);
		PROC_PRINTF("EXTRA VXLAN special fast-forward downstream number :%d\n",fc_db.vxlan_ds_extra_fastForward_num);

		PROC_PRINTF("-----------------[VXLAN EXTRA UPSTREAM]--------------------\n");	
		for(i = 0; i < RTK_FC_VXLAN_EXTRA_MAX_NUM ; i++)
		{
			if(fc_db.vxlan_extra_upStream_record[i].isSet!=1)
				continue;


			PROC_PRINTF("[%d] 	- outer flow index: %d (Hash: %d)\n",i, fc_db.vxlan_extra_upStream_record[i].outer_flow_index, fc_db.flowTbl[fc_db.vxlan_extra_upStream_record[i].outer_flow_index].mainHashIdx);	
			PROC_PRINTF("           - inner flow index: %d (Hash: %d)\n", fc_db.vxlan_extra_upStream_record[i].inner_flow_index, fc_db.flowTbl[fc_db.vxlan_extra_upStream_record[i].inner_flow_index].mainHashIdx);	
			PROC_PRINTF("           - sw flow index: %d\n", fc_db.vxlan_extra_upStream_record[i].sw_flow_index);
			PROC_PRINTF("           - cpuPort: %x\n", fc_db.vxlan_extra_upStream_record[i].cpuPort);	
			PROC_PRINTF("           - isSet: %x\n", fc_db.vxlan_extra_upStream_record[i].isSet);
			PROC_PRINTF("           - voq: %x\n", (fc_db.vxlan_extra_upStream_record[i].extraVoq_index==0)?5:6);
			PROC_PRINTF("     		- direction: Upstream \n");
			PROC_PRINTF("-------------------------------------\n");	
		}


		PROC_PRINTF("-----------------[VXLAN EXTRA DOWNSTREAM]--------------------\n");
		
		PROC_PRINTF("           - 68 bytes acl info: 0x%x, [AAL-L3] %s | TBL/KEY_IDX: %04d / %d\n", fc_db.vxlan_ds_extraCLS_Info,
					CLS_TBL_ID(fc_db.vxlan_ds_extraCLS_Info)<(L3_CLS_KEY_TBL_ENTRY_MAX/2)?"WAN":"LAN", CLS_TBL_ID(fc_db.vxlan_ds_extraCLS_Info), CLS_KEY_ID(fc_db.vxlan_ds_extraCLS_Info));
		for(i = 0; i < RTK_FC_VXLAN_EXTRA_MAX_NUM ; i++)
		{
			if(fc_db.vxlan_extra_downStream_record[i].isSet!=1)
				continue;


			PROC_PRINTF("[%d] 		- outer flow index: %d (Hash: %d)\n",i, fc_db.vxlan_extra_downStream_record[i].outer_flow_index, fc_db.flowTbl[fc_db.vxlan_extra_downStream_record[i].outer_flow_index].mainHashIdx);	
			PROC_PRINTF("           - inner flow index: %d (Hash: %d\n", fc_db.vxlan_extra_downStream_record[i].inner_flow_index, fc_db.flowTbl[fc_db.vxlan_extra_downStream_record[i].inner_flow_index].mainHashIdx);	
			PROC_PRINTF("           - sw flow index: %d\n", fc_db.vxlan_extra_downStream_record[i].sw_flow_index);
			PROC_PRINTF("           - cpuPort: %x\n", fc_db.vxlan_extra_downStream_record[i].cpuPort);	
			PROC_PRINTF("           - isSet: %x\n", fc_db.vxlan_extra_downStream_record[i].isSet);
			PROC_PRINTF("     		- direction: Upstream \n");
			PROC_PRINTF("-------------------------------------\n");	
		}

	}
	return SUCCESS;

}
#endif
int dump_l2dual_info_table(struct seq_file *s, void *v)
{
	int i = 0,j;
	
	if(fc_db.controlFuc.special_fast_forward_mode==1U)
	{
		PROC_PRINTF("-------------------------------------\n");
		PROC_PRINTF("VXLAN upstream special fast-forward egress interface index :%d\n\n",fc_db.vxlan_us_fastForward_wan_intf_id);
		PROC_PRINTF("VXLAN special fast-forward upstream number :%d\n",fc_db.vxlan_us_fastForward_num);
		
		PROC_PRINTF("===============Upstream FastFwd Information=================\n");
#if defined(CONFIG_FC_RTL9607C_SERIES)
		PROC_PRINTF(" vxlan_accelerate_upstreamL2Idx: %d\n", fc_db.vxlan_accelerate_upstreamL2Idx);	
		PROC_PRINTF(" vxlan_accelerate_extra_upstreamL2Idx: %d\n", fc_db.vxlan_accelerate_extra_upstreamL2Idx);	
		PROC_PRINTF(" vxlan_upstream_set_ok: %d\n", fc_db.vxlan_upstream_set_ok); 

#endif
		for(i = 0; i < 4 ; i++)
		{
			if(fc_db.vxlan_upStream_record[i].isSet!=1U)
				continue;
#if defined(CONFIG_FC_RTL9607C_SERIES)

			PROC_PRINTF("[port %d] - outer flow index: %d\n",i, fc_db.vxlan_upStream_record[i].outer_flow_index);	
			PROC_PRINTF("           - inner flow index: %d\n", fc_db.vxlan_upStream_record[i].inner_flow_index);	
			PROC_PRINTF("           - sw flow index: %d\n", fc_db.vxlan_upStream_record[i].sw_flow_index);
			PROC_PRINTF("           - cpuPort: %x\n", fc_db.vxlan_upStream_record[i].cpuPort);	
			PROC_PRINTF("           - isSet: %x\n", fc_db.vxlan_upStream_record[i].isSet);
#elif defined(CONFIG_FC_CA8277B_SERIES)
			PROC_PRINTF("[%d] - outer flow index(MainHashIdx): %d \n",i, fc_db.flowTbl[fc_db.vxlan_upStream_record[i].outer_flow_index].mainHashIdx);	
			PROC_PRINTF("	  - inner flow index(MainHashIdx): %d \n", fc_db.flowTbl[fc_db.vxlan_upStream_record[i].inner_flow_index].mainHashIdx);	
			PROC_PRINTF("	  - sw flow index: %d\n", fc_db.vxlan_upStream_record[i].sw_flow_index);
			PROC_PRINTF("	  - cpuPort: %x\n", fc_db.vxlan_upStream_record[i].cpuPort);	
			PROC_PRINTF("	  - isSet: %x\n", fc_db.vxlan_upStream_record[i].isSet);

			if(fc_db.vxlan_l3cls_info.pktlen_us_68_aclInfo[i] != CA_UINT32_INVALID){
				PROC_PRINTF("           - 68 bytes acl info: 0x%x, [AAL-L3] %s | TBL/KEY_IDX: %04d / %d\n", fc_db.vxlan_l3cls_info.pktlen_us_68_aclInfo[i],
					CLS_TBL_ID(fc_db.vxlan_l3cls_info.pktlen_us_68_aclInfo[i])<(L3_CLS_KEY_TBL_ENTRY_MAX/2)?"WAN":"LAN", CLS_TBL_ID(fc_db.vxlan_l3cls_info.pktlen_us_68_aclInfo[i]), CLS_KEY_ID(fc_db.vxlan_l3cls_info.pktlen_us_68_aclInfo[i]));
				PROC_PRINTF("           - 128 bytes acl info: 0x%x  [AAL-L3] %s | TBL/KEY_IDX: %04d / %d\n", fc_db.vxlan_l3cls_info.pktlen_us_128_aclInfo[i],
					CLS_TBL_ID(fc_db.vxlan_l3cls_info.pktlen_us_128_aclInfo[i])<(L3_CLS_KEY_TBL_ENTRY_MAX/2)?"WAN":"LAN", CLS_TBL_ID(fc_db.vxlan_l3cls_info.pktlen_us_128_aclInfo[i]), CLS_KEY_ID(fc_db.vxlan_l3cls_info.pktlen_us_128_aclInfo[i]));
			}else{
				PROC_PRINTF("           - 68 bytes acl info: 0x%x\n", fc_db.vxlan_l3cls_info.pktlen_us_68_aclInfo[i]);
			}
#endif
			PROC_PRINTF("           - direction: Upstream \n");
			PROC_PRINTF("-------------------------------------\n");	
		}
	
		PROC_PRINTF("\n");

		PROC_PRINTF("VXLAN special fast-forward downstream number :%d\n",fc_db.vxlan_ds_fastForward_num);
		PROC_PRINTF("=============Downstream FastFwd Information=================\n");
#if defined(CONFIG_FC_RTL9607C_SERIES)
		PROC_PRINTF(" vxlan_accelerate_downstreamL2Idx: %d\n", fc_db.vxlan_accelerate_downstreamL2Idx);	
		PROC_PRINTF(" vxlan_accelerate_extra_downstreamL2Idx: %d\n", fc_db.vxlan_accelerate_extra_downstreamL2Idx);	
		PROC_PRINTF(" vxlan_downstream_set_ok: %d\n", fc_db.vxlan_downstream_set_ok); 
#endif

		for(i = 0; i < 4 ; i++)
		{
			if(fc_db.vxlan_downStream_record[i].isSet!=1U)
				continue;
#if defined(CONFIG_FC_RTL9607C_SERIES)			
			PROC_PRINTF("[%d] - outer flow index: %d\n",i, fc_db.vxlan_downStream_record[i].outer_flow_index);	
			PROC_PRINTF("     - iner flow index: %d\n", fc_db.vxlan_downStream_record[i].inner_flow_index);	
			PROC_PRINTF("     - sw flow index: %d\n", fc_db.vxlan_downStream_record[i].sw_flow_index);	
			PROC_PRINTF("     - cpuPort: %x\n", fc_db.vxlan_downStream_record[i].cpuPort);
#elif defined(CONFIG_FC_CA8277B_SERIES)			
			PROC_PRINTF("[%d] - outer flow index: %d\n",i,fc_db.flowTbl[fc_db.vxlan_downStream_record[i].outer_flow_index].mainHashIdx );	
			PROC_PRINTF("     - iner flow index: %d\n", fc_db.flowTbl[fc_db.vxlan_downStream_record[i].inner_flow_index].mainHashIdx);	
			PROC_PRINTF("     - sw flow index: %d\n", fc_db.vxlan_downStream_record[i].sw_flow_index);	
			PROC_PRINTF("     - cpuPort: %x\n", fc_db.vxlan_downStream_record[i].cpuPort);
			if(fc_db.vxlan_l3cls_info.pktlen_ds_68_aclInfo[i] != CA_UINT32_INVALID){
				PROC_PRINTF("     - 68 bytes acl info: 0x%x, [AAL-L3] %s | TBL/KEY_IDX: %04d / %d\n", fc_db.vxlan_l3cls_info.pktlen_ds_68_aclInfo[i],
					CLS_TBL_ID(fc_db.vxlan_l3cls_info.pktlen_ds_68_aclInfo[i])<(L3_CLS_KEY_TBL_ENTRY_MAX/2)?"WAN":"LAN", CLS_TBL_ID(fc_db.vxlan_l3cls_info.pktlen_ds_68_aclInfo[i]), CLS_KEY_ID(fc_db.vxlan_l3cls_info.pktlen_ds_68_aclInfo[i]));
				PROC_PRINTF("     - others bytes acl info: 0x%x, [AAL-L3] %s | TBL/KEY_IDX: %04d / %d\n", fc_db.vxlan_l3cls_info.pktlen_ds_others_aclInfo[i],
					CLS_TBL_ID(fc_db.vxlan_l3cls_info.pktlen_ds_others_aclInfo[i])<(L3_CLS_KEY_TBL_ENTRY_MAX/2)?"WAN":"LAN", CLS_TBL_ID(fc_db.vxlan_l3cls_info.pktlen_ds_others_aclInfo[i]), CLS_KEY_ID(fc_db.vxlan_l3cls_info.pktlen_ds_others_aclInfo[i]));
			}else{
				PROC_PRINTF("     - 68 bytes acl info: 0x%x\n", fc_db.vxlan_l3cls_info.pktlen_ds_68_aclInfo[i]);
				PROC_PRINTF("     - others bytes acl info: 0x%x\n", fc_db.vxlan_l3cls_info.pktlen_ds_others_aclInfo[i]);
			}
#endif
		}
	}
	
	PROC_PRINTF("================================================\n");
	PROC_PRINTF("[0]: reserved entry\n");
	PROC_PRINTF("================================================\n");

	for(i = 1; i < RTK_FC_TABLESIZE_L2DUAL_TABLE; i++)
	{
		if(atomic_read(&fc_db.l2DualTbl[i].refCount)!=0)
		{
			if((fc_db.l2DualTbl[i].type == RTK_FC_L2DUAL_TYPE_VXLAN) && (fc_db.l2DualTbl[i].action == RTK_FC_L2DUAL_ACT_ADD))
			{
				PROC_PRINTF("[%d] type: VXLAN (refCount: %d) action: ADD, outer_tag_len: %d\n", i, atomic_read(&fc_db.l2DualTbl[i].refCount), fc_db.l2DualTbl[i].vxlan_us.outer_tag_len);
				PROC_PRINTF("\tVNI: %d \n", ntohs(fc_db.l2DualTbl[i].vxlan_us.vni)>>8);
				PROC_PRINTF("\touter udp sport: %d \n", ntohs(fc_db.l2DualTbl[i].vxlan_us.outer_udph_sport));
				PROC_PRINTF("\touter_ppph_tag_off: %d \n", fc_db.l2DualTbl[i].vxlan_us.outer_ppph_tag_off);
				PROC_PRINTF("\touter_iph_tag_off: %d \n", fc_db.l2DualTbl[i].vxlan_us.outer_iph_tag_off);
				PROC_PRINTF("\touter_udph_tag_off: %d \n", fc_db.l2DualTbl[i].vxlan_us.outer_udph_tag_off);
				PROC_PRINTF("\tstreamid_en: %d \n", fc_db.l2DualTbl[i].vxlan_us.streamid_en);
				PROC_PRINTF("\tstreamid: %d \n", fc_db.l2DualTbl[i].vxlan_us.streamid);
				PROC_PRINTF("\tdmacL2Idx: %d \n", fc_db.l2DualTbl[i].vxlan_us.dmacL2Idx);
				PROC_PRINTF("\touter_is_ipv6: %d \n", fc_db.l2DualTbl[i].vxlan_us.outer_is_ipv6);
				PROC_PRINTF("\tinner_cvlan_tagif: %d \n", fc_db.l2DualTbl[i].vxlan_us.inner_cvlan_tagif);
				PROC_PRINTF("\tinner_cvlan_cvid: %d \n", fc_db.l2DualTbl[i].vxlan_us.inner_cvlan_cvid);
				PROC_PRINTF("\tingress_intf_idx: %d \n", fc_db.l2DualTbl[i].vxlan_us.ingress_intf_idx);
				PROC_PRINTF("\tegress_intf_idx: %d \n", fc_db.l2DualTbl[i].vxlan_us.egress_intf_idx);
				

				PROC_PRINTF("\tContent Buffer = \n");
				PROC_PRINTF("\t");
				for(j = 0 ; j <fc_db.l2DualTbl[i].vxlan_us.outer_tag_len ; j++)
				{
					
					PROC_PRINTF("%02x ",fc_db.l2DualTbl[i].vxlan_us.contentBuffer[j]);
					if((j+1)%16==0)
						PROC_PRINTF("\n\t");
						
				}
				PROC_PRINTF("\n");
			}
			if((fc_db.l2DualTbl[i].type == RTK_FC_L2DUAL_TYPE_VXLAN) && (fc_db.l2DualTbl[i].action == RTK_FC_L2DUAL_ACT_REMOVE))
			{
				PROC_PRINTF("[%d] type: VXLAN (refCount: %d) action: REMOVE, outer_tag_len: %d\n", i, atomic_read(&fc_db.l2DualTbl[i].refCount), fc_db.l2DualTbl[i].vxlan_ds.outer_tag_len);
				PROC_PRINTF("\tVNI: %d \n", ntohl(fc_db.l2DualTbl[i].vxlan_ds.vni)>>8);
			}

			else
			{
				PROC_PRINTF("[%d] type: %d (unknown) (refCount: %d)\n", i, fc_db.l2DualTbl[i].type, atomic_read(&fc_db.l2DualTbl[i].refCount));
			}
			PROC_PRINTF("================================================\n");
		}
	}

	return SUCCESS;
}


int dump_wan_access_limit_statistic(struct seq_file *s, void *v)
{
	int len=0;

	PROC_PRINTF("Wan Access Limit:\n");
	if(fc_db.wanAccessLimit.accessLimitNumber>=0){
		PROC_PRINTF("  Portmask:0x%llx\n",fc_db.wanAccessLimit.portMask);
		PROC_PRINTF("  Wlanmask:0x%llx\n",fc_db.wanAccessLimit.wlanMask);
		PROC_PRINTF("  Limit:%d\n",fc_db.wanAccessLimit.accessLimitNumber);
		PROC_PRINTF("  Count:%d\n",atomic_read(&fc_db.wanAccessLimit.learningCount));
		if(fc_db.wanAccessLimit.limitField==RT_MISC_WAN_ACCESS_LIMIT_BY_MAC)
		{
			PROC_PRINTF("  LimitField:BY MAC\n");
		}
		else
		{
			int i=0;
			rtk_fc_wan_access_limit_IP_list_t *pList;
			struct list_head *pHead;
			PROC_PRINTF("  LimitField:BY IP\n");
			for(i=0;i<RTK_FC_WAN_ACCESS_IP_BUCKET_SIZE;i++)
			{
				pHead=&fc_db.wanAccessLimitIP_head[i];
				if(!list_empty(pHead))
				{
					PROC_PRINTF("   - hash[\033[1;33;40m%d\033[0m]:\n",i);
					list_for_each_entry(pList, pHead, accessIP_list)
					{
						PROC_PRINTF("\t-> %pI4 \n",&pList->sip);
					}
				}
			}
		}
	}
	else
		PROC_PRINTF("  Limit:unlimit\n");

	return len;
}

int dump_flow_connection(struct seq_file *s, void *v)
{
	uint32 idx=0U;
	int dmacIdx;
	uint8 l4_proto_num;

	for(idx=0U; idx<fc_db.flowSwTableSize; idx++)
	{
#if defined(CONFIG_RTK_L34_CANDIDATE_FLOW)
		if(fc_db.flowTbl[idx].candidateState!=CANDIDATE_STATE_NONE)
#else
		if(fc_db.flowTbl[idx].pFlowEntry->path1.valid)
#endif
		{
			dmacIdx = fc_db.flowTbl[idx].lutEgrDaIdx;
			if(fc_db.flowTbl[idx].pFlowEntry->path3.in_path == FB_PATH_34)
			{
				rtk_rg_asic_path3_entry_t *p3Data;
				p3Data = (rtk_rg_asic_path3_entry_t *)fc_db.flowTbl[idx].pFlowEntry;

#if defined(CONFIG_RTK_L34_G3_PLATFORM)
				l4_proto_num =  p3Data->in_l4proto_num;
#else
				l4_proto_num =  p3Data->in_l4proto?IPPROTO_TCP:IPPROTO_UDP;
#endif

				if(!p3Data->in_ipv4_or_ipv6){
					//IPv4
					uint32 sip = p3Data->in_src_ipv4_addr, dip = p3Data->in_dst_ipv4_addr;

					PROC_PRINTF("[%u] DMAC:%pM SIP:%pI4h DIP:%pI4h SPORT:%u DPROT:%u PROTO:0x%02x IDLE_TIME:%u\n",
						idx, fc_db.lutTbl[dmacIdx]->l2Addr , &sip, &dip, p3Data->in_l4_src_port, p3Data->in_l4_dst_port, l4_proto_num, fc_db.flowTbl[idx].idleSecs);
				}
				else
				{
					//IPv6
					struct nf_conn * pCT = NULL;
					struct rt_nfconn *rtct, rt_ct;
					pCT = fc_db.flowTbl[idx].cachedCt;

					if(pCT!=NULL)
					{
						rtct = &rt_ct;
						RTK_FC_HOOK_CONVERTER_CT(pCT, rtct);
						if(atomic_read(&rtct->ct_general->use))
						{
							rt_ip_conntrack_dir_t ct_dir = RT_IP_CT_DIR_MAX;
							rt_nf_5tuple_info_t rt_nf_tuple_info;
							RTK_FC_HELPER_PS_CT_RT_5TUPLE_INFO_GET(pCT, &rt_nf_tuple_info);

							if(p3Data->in_src_ipv6_addr_hash == (_rtk_rg_flowHashIPv6SrcAddr_get((uint8 *)(&rt_nf_tuple_info.tuple[RT_IP_CT_DIR_ORIGINAL].src_ip.v6_addr))))
								ct_dir = RT_IP_CT_DIR_ORIGINAL;
							else if(p3Data->in_src_ipv6_addr_hash == (_rtk_rg_flowHashIPv6SrcAddr_get((uint8 *)(&rt_nf_tuple_info.tuple[RT_IP_CT_DIR_REPLY].src_ip.v6_addr))))
								ct_dir = RT_IP_CT_DIR_REPLY;

							if(ct_dir != RT_IP_CT_DIR_MAX)
							{
								PROC_PRINTF("[%u] DMAC:%pM SIP:%pI6n DIP:%pI6n SPORT:%u DPROT:%u PROTO:0x%02x IDLE_TIME:%u\n",
									idx, fc_db.lutTbl[dmacIdx]->l2Addr, &(rt_nf_tuple_info.tuple[ct_dir].src_ip.v6_addr), &(rt_nf_tuple_info.tuple[ct_dir].dest_ip.v6_addr), p3Data->in_l4_src_port, p3Data->in_l4_dst_port, l4_proto_num, fc_db.flowTbl[idx].idleSecs);
							}
						}
					}
				}
			}
			else if(fc_db.flowTbl[idx].pFlowEntry->path3.in_path == FB_PATH_5)
			{
				rtk_rg_asic_path5_entry_t *p5Data;

				p5Data = (rtk_rg_asic_path5_entry_t *)fc_db.flowTbl[idx].pFlowEntry;
#if defined(CONFIG_RTK_L34_G3_PLATFORM)
				l4_proto_num =  p5Data->in_l4proto_num;
#else
				l4_proto_num =  p5Data->in_l4proto?IPPROTO_TCP:IPPROTO_UDP;
#endif
				if(!p5Data->in_ipv4_or_ipv6){
					//IPv4
					uint32 sip = p5Data->in_src_ipv4_addr, dip;
					if(p5Data->out_l4_act && !p5Data->out_l4_direction)
					{
						dip = fc_db.netifTbl[NETIF_HWTOSW(p5Data->in_intf_idx)].intf.gateway_ipv4_addr;
						PROC_PRINTF("[%u] DMAC:%pM SIP:%pI4h DIP:%pI4h SPORT:%u DPROT:%u PROTO:0x%02x IDLE_TIME:%u\n",
							idx, fc_db.lutTbl[dmacIdx]->l2Addr , &sip, &dip, p5Data->in_l4_src_port, p5Data->in_l4_dst_port, l4_proto_num, fc_db.flowTbl[idx].idleSecs);
					}
					else
					{
						dip = p5Data->in_dst_ipv4_addr;
						PROC_PRINTF("[%u] DMAC:%pM SIP:%pI4h DIP:%pI4h SPORT:%u DPROT:%u PROTO:0x%02x IDLE_TIME:%u\n",
							idx, fc_db.lutTbl[dmacIdx]->l2Addr , &sip, &dip, p5Data->in_l4_src_port, p5Data->in_l4_dst_port, l4_proto_num, fc_db.flowTbl[idx].idleSecs);
					}
				}
				else
				{
					//IPv6
					struct nf_conn * pCT = NULL;
					struct rt_nfconn *rtct, rt_ct;
					pCT = fc_db.flowTbl[idx].cachedCt;

					if(pCT!=NULL)
					{
						rtct = &rt_ct;
						RTK_FC_HOOK_CONVERTER_CT(pCT, rtct);
						if(atomic_read(&rtct->ct_general->use))
						{
							rt_ip_conntrack_dir_t ct_dir = RT_IP_CT_DIR_MAX;
							rt_nf_5tuple_info_t rt_nf_tuple_info;
							RTK_FC_HELPER_PS_CT_RT_5TUPLE_INFO_GET(pCT, &rt_nf_tuple_info);

							if(p5Data->in_src_ipv6_addr_hash == (_rtk_rg_flowHashIPv6SrcAddr_get((uint8 *)(&rt_nf_tuple_info.tuple[RT_IP_CT_DIR_ORIGINAL].src_ip.v6_addr))))
								ct_dir = RT_IP_CT_DIR_ORIGINAL;
							else if(p5Data->in_src_ipv6_addr_hash == (_rtk_rg_flowHashIPv6SrcAddr_get((uint8 *)(&rt_nf_tuple_info.tuple[RT_IP_CT_DIR_REPLY].src_ip.v6_addr))))
								ct_dir = RT_IP_CT_DIR_REPLY;

							if(ct_dir != RT_IP_CT_DIR_MAX)
							{
								PROC_PRINTF("[%u] DMAC:%pM SIP:%pI6n DIP:%pI6n SPORT:%u DPROT:%u PROTO:0x%02x IDLE_TIME:%u\n",
									idx, fc_db.lutTbl[dmacIdx]->l2Addr, &(rt_nf_tuple_info.tuple[ct_dir].src_ip.v6_addr), &(rt_nf_tuple_info.tuple[ct_dir].dest_ip.v6_addr), p5Data->in_l4_src_port, p5Data->in_l4_dst_port, l4_proto_num, fc_db.flowTbl[idx].idleSecs);
							}
						}
					}
				}
			}
		}
	}
	return 0;
}

#if defined(CONFIG_RTK_SOC_RTL8198D)
int32 dump_flow_limit(struct seq_file *s, void *v)
{
	PROC_PRINTF("Dump flow limit (-1 unlimit) and count:\n\n");

	PROC_PRINTF("[HW]\n");
	PROC_PRINTF("     hw_path12_limit:	%d,	hw_path12_count:	%u\n", fc_db.flow_limit.hw_path12_limit, atomic_read(&fc_db.flow_limit.hw_path12_count));
	PROC_PRINTF("     hw_path34_limit:	%d,	hw_path34_count:	%u\n", fc_db.flow_limit.hw_path34_limit, atomic_read(&fc_db.flow_limit.hw_path34_count));
	PROC_PRINTF("     hw_path5_limit:	%d,	hw_path5_count:		%u\n", fc_db.flow_limit.hw_path5_limit, atomic_read(&fc_db.flow_limit.hw_path5_count));
	PROC_PRINTF("     hw_path6_limit:	%d,	hw_path6_count:		%u\n", fc_db.flow_limit.hw_path6_limit, atomic_read(&fc_db.flow_limit.hw_path6_count));

	PROC_PRINTF("[SW]\n");
	PROC_PRINTF("     sw_path12_limit:	%d,	sw_path12_count:	%u\n", fc_db.flow_limit.sw_path12_limit, atomic_read(&fc_db.flow_limit.sw_path12_count));
	PROC_PRINTF("     sw_path34_limit:	%d,	sw_path34_count:	%u\n", fc_db.flow_limit.sw_path34_limit, atomic_read(&fc_db.flow_limit.sw_path34_count));
	PROC_PRINTF("     sw_path5_limit:	%d,	sw_path5_count:		%u\n", fc_db.flow_limit.sw_path5_limit, atomic_read(&fc_db.flow_limit.sw_path5_count));
	PROC_PRINTF("     sw_path6_limit:	%d,	sw_path6_count:		%u\n", fc_db.flow_limit.sw_path6_limit, atomic_read(&fc_db.flow_limit.sw_path6_count));

	return 0;
}
#endif

#if defined(CONFIG_RTK_SOC_RTL8198D) || defined(CONFIG_RTL8198X_SERIES)
int32 dump_ext_flow_mib(struct seq_file *s, void *v)
{
	RTK_FC_HELPER_PRINT_EXT_FLOW_MIB_COUNT_ALL();
	return 0;
}
#endif

#if defined(CONFIG_RTK_SOC_RTL8198D) || defined(CONFIG_FC_RTL8198F_SERIES) || defined(CONFIG_RTL8198X_SERIES)
void _rtk_fc_clear_shortcut_tcp_in_window_statistic(void)
{
	atomic_set(&fc_db.statistic.totalCnt_tcp_in_window, 0);
	atomic_set(&fc_db.statistic.okCnt_tcp_in_window, 0);
	atomic_set(&fc_db.statistic.failCnt_tcp_in_window, 0);
	atomic_set(&fc_db.statistic.abnormalCnt_tcp_in_window, 0);
	
	return;
}

int rtk_fc_tcp_in_window_get(struct seq_file *s, void *v)
{
	char *str_action = "none";

	switch(fc_db.tcp_in_window_shortcut_fail_action) {
	case RTK_FC_TCP_IN_WINDOW_FAIL_CONTINUE_SHORTCUT:
		str_action = RTK_FC_STR_FAIL_CONTINUE_SHORTCUT;
		break;
	case RTK_FC_TCP_IN_WINDOW_FAIL_FREE_SKB:
		str_action = RTK_FC_STR_FAIL_FREE_SKB;
		break;
	case RTK_FC_TCP_IN_WINDOW_FAIL_TO_PS:
		str_action = RTK_FC_STR_FAIL_TO_PS;
		break;
	default:
		break;
	}
	
	PROC_PRINTF(">>Dump fc shortcut tcp_in_window:\n\n");
	
	PROC_PRINTF("     shortcut_check:		%u\n", fc_db.tcp_in_window_shortcut_check);
	PROC_PRINTF("     shortcut_fail_action:	%s\n", str_action);
	
	PROC_PRINTF("     totalCnt_tcp_check:	%u\n", atomic_read(&fc_db.statistic.totalCnt_tcp_in_window));
	PROC_PRINTF("     okCnt_tcp_check:		%u\n", atomic_read(&fc_db.statistic.okCnt_tcp_in_window));
	PROC_PRINTF("     failCnt_tcp_check:		%u\n", atomic_read(&fc_db.statistic.failCnt_tcp_in_window));
	PROC_PRINTF("     abnormalCnt_tcp_in_window:	%u\n", atomic_read(&fc_db.statistic.abnormalCnt_tcp_in_window));
	
	return SUCCESS;
}

int rtk_fc_tcp_in_window_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned char tmp_buf[32] = {0};
	int len = (count >= 31) ? 31 : count;
	unsigned int act;
	char *strptr, *cmdptr;
	
	if(buffer) {
		/* copy data to the buffer */
		strscpy(tmp_buf, buffer, len);

		if(!strncmp(tmp_buf, "help", strlen(tmp_buf))) {
			rtlglue_printf("format:\n");
			rtlglue_printf("	echo 0 > /proc/fc/sw_dump/tcp_in_window\n");
			rtlglue_printf("	echo 1 > /proc/fc/sw_dump/tcp_in_window\n");
			rtlglue_printf("	echo clear_cnt > /proc/fc/sw_dump/tcp_in_window\n");
			rtlglue_printf("	echo fail_act $act > /proc/fc/sw_dump/tcp_in_window.\n");
			rtlglue_printf("		$act	0:%s\n", RTK_FC_STR_FAIL_CONTINUE_SHORTCUT);
			rtlglue_printf("		$act	1:%s\n", RTK_FC_STR_FAIL_FREE_SKB);
			rtlglue_printf("		$act	2:%s\n", RTK_FC_STR_FAIL_TO_PS);

			return count;
		}
		else if(!strncmp(tmp_buf, "1", strlen(tmp_buf))) {
			fc_db.tcp_in_window_shortcut_check = 1U;
			
			rtlglue_printf("fc flow flush...\n");
			rtk_fc_flow_flush();

			_rtk_fc_clear_shortcut_tcp_in_window_statistic();
		}
		else if(!strncmp(tmp_buf, "0", strlen(tmp_buf))) {
			fc_db.tcp_in_window_shortcut_check = 0U;
			
			rtlglue_printf("fc flow flush...\n");
			rtk_fc_flow_flush();

			_rtk_fc_clear_shortcut_tcp_in_window_statistic();
		}
		else if(!strncmp(tmp_buf, "clear_cnt", strlen(tmp_buf))){
			_rtk_fc_clear_shortcut_tcp_in_window_statistic();
		}
		else {
			strptr = tmp_buf;
			if((cmdptr = strsep(&strptr," ")) == NULL) {
				goto PARSE_ERROR;
			}

			if(strncmp(cmdptr, "fail_act", strlen(cmdptr)) == 0) {
				if((cmdptr = strsep(&strptr," ")) == NULL) {
					goto PARSE_ERROR;
				}

				if((act = simple_strtol(cmdptr, NULL, 10)) > RTK_FC_TCP_IN_WINDOW_FAIL_TO_PS) {
					rtlglue_printf("	tcp_in_window_shortcut_fail_action %d invalid\n", act);
					goto PARSE_ERROR;
				}
				fc_db.tcp_in_window_shortcut_fail_action = act;
			}
			else {
				goto PARSE_ERROR;
			}
		}

		rtk_fc_tcp_in_window_get(NULL, NULL);
		return count;
	}
	
PARSE_ERROR:
	return -EFAULT;	
}

int rtk_fc_default_route_info_get(struct seq_file *s, void *v)
{
	int i;

	PROC_PRINTF(">>Dump default route info:\n\n");
	for (i = 0; i < MAX_DEFAULT_ROUTE_INFO_ENTRY_SIZE; i++) {
		if (fc_db.defaultRouteInfo[i].valid) {
			PROC_PRINTF("[%d]	dev: %s		rt_tbl_id: %u, gw: %pI4\n", i, 
				fc_db.defaultRouteInfo[i].dev_name, fc_db.defaultRouteInfo[i].rt_tbl_id, &fc_db.defaultRouteInfo[i].gw_addr);
		}
	}

	return SUCCESS;
}

void rtk_fc_sw_check_hwflow_usage(void)
{
	rtlglue_printf("usage:\n");
	rtlglue_printf("ENABLE: echo 1 > /proc/fc/sw_dump/sw_check_hwflow\n");
	rtlglue_printf("DISABLE: echo 0 > /proc/fc/sw_dump/sw_check_hwflow\n");
}

int rtk_fc_sw_check_hwflow_get(struct seq_file *s, void *v)
{
	PROC_PRINTF("%u\n", fc_db.controlFuc.sw_check_hwflow);
	return SUCCESS;
}

int rtk_fc_sw_check_hwflow_set(struct file *file, const char *buffer, unsigned long count, void *data)
{
	int value = _rtk_fc_proc_parsing_string_to_integer(buffer, count);

	if (value < 0 || value > 1)
		rtk_fc_sw_check_hwflow_usage();
	else {
		fc_db.controlFuc.sw_check_hwflow = value;
		RTK_FC_HELPER_MGR_SW_CHECK_HWFLOW_SET(value);
	}

	return count;
}
#endif
