#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>

#include <asm/stacktrace.h> //for dump_stack()
#include <linux/stdarg.h>
#include <ioal/mem32.h>


#include <rtk_igmp_hook.h>
#include <rtk_igmp_debug.h>


#define COLOR_Y "\033[1;33m"
#define COLOR_NM "\033[0m"
#define COLOR_H "\033[1;37;41m"
#define COLOR_G "\033[1;32m"

#define rtlglue_printf printk
#define rtlglue_print_hex_dump(arg...) print_hex_dump(KERN_EMERG, ##arg)

char rtk_mt_watch_tmp[512];

void _rtk_igmp_dump_stack(void)
{
	dump_stack();
}

int igmp_assert_eq(int func_return,int expect_return,const char *func,int line)
{
	if (func_return != expect_return)
	{
		rtlglue_printf("\033[31;43m%s(%d): func_return=0x%x expect_return=0x%x, fail, so abort!\033[m\n",func, line,func_return,expect_return);
		_rtk_igmp_dump_stack();
		return func_return;
	}
	return 0;
}


void igmp_common_dump(rtk_igmp_debug_level_t level,const char *funcs,int line, char *comment,...)
{
	int show=1;
	char string[16]={0};
	int color=31, bgcolor=40;
	va_list a_list;
	va_start(a_list,comment);

	if(show==1)
	{
		int mt_trace_i;

		vsprintf( rtk_mt_watch_tmp, comment,a_list);
		for(mt_trace_i=1;mt_trace_i<512;mt_trace_i++)
		{
			if(rtk_mt_watch_tmp[mt_trace_i]==0)
			{
				if(rtk_mt_watch_tmp[mt_trace_i-1]=='\n') rtk_mt_watch_tmp[mt_trace_i-1]=' ';
				else break;
			}
		}

		switch(level)
		{
			case RTK_IGMP_DEBUG_LEVEL_IGMPCTRL:
				sprintf(string,"IGMP_CTRL"); color=35; bgcolor=40;
				break;
			case RTK_IGMP_DEBUG_LEVEL_FLOWDATA:
				sprintf(string,"DATAFLOW"); color=32; bgcolor=40;
				break;
			case RTK_IGMP_DEBUG_LEVEL_HWCB:
				sprintf(string,"HWCB"); color=33; bgcolor=40;
				break;
			case RTK_IGMP_DEBUG_LEVEL_DUMP:
				sprintf(string,"DUMP"); color=36; bgcolor=40;
				break;
			case RTK_IGMP_DEBUG_LEVEL_WARNING:
				sprintf(string,"WARNING"); color=31; bgcolor=41;
				break;
			case RTK_IGMP_DEBUG_LEVEL_RATE_LIMIT_DROP:
				sprintf(string,"RATE LIMIT DROP"); color=37; bgcolor=40;
				break;
			case RTK_IGMP_DEBUG_LEVEL_HOOK_POINT:
				sprintf(string,"HOOK"); color=38; bgcolor=40;
				break;
			case RTK_IGMP_DEBUG_LEVEL_PARSER:
				sprintf(string,"PARSER"); color=39; bgcolor=40;
				break;		
			case RTK_IGMP_DEBUG_LEVEL_API:
				sprintf(string,"API"); color=39; bgcolor=40;
				break;				
		}
		printk("\033[1;%d;%dm[%s] %s\033[1;30;40m @ %s(%d)\033[0m\n",color,bgcolor,string,rtk_mt_watch_tmp,funcs,line);
	}
	va_end (a_list);
}


void igmp_dump_packet(u8 *pkt,u32 size,char *memo)
{
	int off;
	u8 protocol=0;
	int i;
	int pppoeif=0;
	u8 splitLine[80]={0};
	memset(splitLine,'=',79);
	rtlglue_printf("%s\n",splitLine);

	if(size==0)
	{
		rtlglue_printf("%s\npacket_length=0\n",memo);
		return;
	}

	rtlglue_printf("%s\n",memo);
	rtlglue_print_hex_dump("", DUMP_PREFIX_ADDRESS, 16, 1, pkt, size, 1);
	rtlglue_printf("\n" COLOR_Y "DA" COLOR_NM ":[%02X-%02X-%02X-%02X-%02X-%02X]\t" COLOR_Y "SA" COLOR_NM ":[%02X-%02X-%02X-%02X-%02X-%02X]\n",pkt[0],pkt[1],pkt[2],pkt[3],pkt[4],pkt[5]
		,pkt[6],pkt[7],pkt[8],pkt[9],pkt[10],pkt[11]);
	off=12;
	if((pkt[off]==0x88)&&(pkt[off+1]==0x99))
	{

		if(((pkt[off+8]==0x88)&&(pkt[off+9]==0xa8))||((pkt[off+8]==0x81)&&(pkt[off+9]==0x00))||((pkt[off+8]==0x88)&&((pkt[off+9]==0x63)||(pkt[off+9]==0x64)))||
		((pkt[off+8]==0x86)&&(pkt[off+9]==0xdd))||((pkt[off]==0x08)&&(pkt[off+1]==0x00)))
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

{

	if ((pkt[off]==0x88)&&(pkt[off+1]==0xa8))
	{
		rtlglue_printf("SVLAN:[" COLOR_Y "Pri" COLOR_NM "=%d][" COLOR_Y "DEI" COLOR_NM "=%d][" COLOR_Y "VID" COLOR_NM "=%d]\n",pkt[off+2]>>5,(pkt[off+2]>>4)&1,((pkt[off+2]&0xf)<<8)|(pkt[off+3]));
		off+=4;
	}
}
	if((pkt[off]==0x81)&&(pkt[off+1]==0x00))
	{
		rtlglue_printf("CVLAN:[" COLOR_Y "Pri" COLOR_NM "=%d][" COLOR_Y "CFI" COLOR_NM "=%d][" COLOR_Y "VID" COLOR_NM "=%d]\n",pkt[off+2]>>5,(pkt[off+2]>>4)&1,((pkt[off+2]&0xf)<<8)|(pkt[off+3]));
		off+=4;
	}

	if((pkt[off]==0x88)&&((pkt[off+1]==0x63)||(pkt[off+1]==0x64))) //PPPoE
	{
		rtlglue_printf("PPPoE:[" COLOR_Y "Code" COLOR_NM "=0x%02x][" COLOR_Y "SessionID" COLOR_NM "=0x%04x][" COLOR_Y "Len" COLOR_NM "=%d]\n",
			pkt[off+3],((u32)pkt[off+4]<<8)|pkt[off+5],((u32)pkt[off+6]<<8)|pkt[off+7]);
		off+=8;
		pppoeif=1;
	}

	if(((pkt[off]==0x86)&&(pkt[off+1]==0xdd)) || ((pkt[off]==0x00)&&(pkt[off+1]==0x57)))		//IPv6 or IPv6 with PPPoE
	{
		rtlglue_printf("IPv6:[" COLOR_Y "Ver" COLOR_NM "=%d][" COLOR_Y "TC" COLOR_NM "=%02x][" COLOR_Y "FL" COLOR_NM "=%02x%02x%x][" COLOR_Y "Len" COLOR_NM "=%d][" COLOR_Y "NxHdr" COLOR_NM "=%d][" COLOR_Y "HopLimit" COLOR_NM "=%d]\n"
			,pkt[off+2]>>4, (pkt[off+2]&0xf)+(pkt[off+3]>>4), (pkt[off+3]&0xf)+(pkt[off+4]>>4), (pkt[off+4]&0xf)+(pkt[off+5]>>4), (pkt[off+5]&0xf), (pkt[off+6]<<8)+pkt[off+7], pkt[off+8], pkt[off+9]);
		rtlglue_printf("     [" COLOR_Y "SIP" COLOR_NM "=%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]\n"
			,pkt[off+10], pkt[off+11], pkt[off+12], pkt[off+13], pkt[off+14], pkt[off+15], pkt[off+16], pkt[off+17]
			,pkt[off+18], pkt[off+19], pkt[off+20], pkt[off+21], pkt[off+22], pkt[off+23], pkt[off+24], pkt[off+25]);
		rtlglue_printf("     [" COLOR_Y "DIP" COLOR_NM "=%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]\n"
			,pkt[off+26], pkt[off+27], pkt[off+28], pkt[off+29], pkt[off+30], pkt[off+31], pkt[off+32], pkt[off+33]
			,pkt[off+34], pkt[off+35], pkt[off+36], pkt[off+37], pkt[off+38], pkt[off+39], pkt[off+40], pkt[off+41]);

		protocol=pkt[off+8];
		if(protocol==0)	//hop-by-hop
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

	if(protocol==0x6) //TCP
	{
		rtlglue_printf("TCP:[" COLOR_Y "SPort" COLOR_NM "=%d][" COLOR_Y "DPort" COLOR_NM "=%d][" COLOR_Y "Seq" COLOR_NM "=0x%x][" COLOR_Y "Ack" COLOR_NM "=0x%x][" COLOR_Y "HLen" COLOR_NM "=%d]\n"
			,(pkt[off]<<8)|(pkt[off+1]),(pkt[off+2]<<8)|(pkt[off+3]),(pkt[off+4]<<24)|(pkt[off+5]<<16)|(pkt[off+6]<<8)|(pkt[off+7]<<0)
			,(pkt[off+8]<<24)|(pkt[off+9]<<16)|(pkt[off+10]<<8)|(pkt[off+11]<<0),pkt[off+12]>>4<<2);
		rtlglue_printf("    [" COLOR_Y "URG" COLOR_NM "=%d][" COLOR_Y "ACK" COLOR_NM "=%d][" COLOR_Y "PSH" COLOR_NM "=%d][" COLOR_Y "RST" COLOR_NM "=%d][" COLOR_Y "SYN" COLOR_NM "=%d][" COLOR_Y "FIN" COLOR_NM "=%d][" COLOR_Y "Win" COLOR_NM "=%d]\n"
			,(pkt[off+13]>>5)&1,(pkt[off+13]>>4)&1,(pkt[off+13]>>3)&1,(pkt[off+13]>>2)&1,(pkt[off+13]>>1)&1,(pkt[off+13]>>0)&1
			,(pkt[off+14]<<8)|pkt[off+15]);
		rtlglue_printf("    [" COLOR_Y "CHM" COLOR_NM "=0x%x][" COLOR_Y "Urg" COLOR_NM "=0x%x]\n",(pkt[off+16]<<8)|(pkt[off+17]<<0),(pkt[off+18]<<8)|(pkt[off+19]<<0));
	}
	else if(protocol==0x11) //UDP
	{
		rtlglue_printf("UDP:[" COLOR_Y "SPort" COLOR_NM "=%d][" COLOR_Y "DPort" COLOR_NM "=%d][" COLOR_Y "Len" COLOR_NM "=%d][" COLOR_Y "CHM" COLOR_NM "=0x%x]\n",(pkt[off]<<8)|(pkt[off+1]),(pkt[off+2]<<8)|(pkt[off+3])
			,(pkt[off+4]<<8)|(pkt[off+5]),(pkt[off+6]<<8)|(pkt[off+7]));

	}

}


#define MAX_PRINT_SIZE	1024
char proc_igmp_print_buf[MAX_PRINT_SIZE];

char *proc_igmp_printf(struct seq_file *s, char *fmt, ...)
{
    int n;
    int size = MAX_PRINT_SIZE;     /* Guess we need no more than 512 bytes */
    va_list ap;

    while (1) {
        va_start(ap, fmt);
        n = vsnprintf(proc_igmp_print_buf, size, fmt, ap);

		if((s==NULL))
			{printk("%s",proc_igmp_print_buf);}
		else
			{seq_puts(s,proc_igmp_print_buf);}

        va_end(ap);

		if (n < 0)
		    return NULL;

		if (n < size)
		    return proc_igmp_print_buf;

		size = n + 1;

    }
	return NULL;
}



