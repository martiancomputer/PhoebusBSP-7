/* re8670.c: A Linux Ethernet driver for the RealTek 8670 chips. */

#define DRV_NAME		"8686"
#define DRV_VERSION		"0.0.1"
#define DRV_RELDATE		"Aug 17, 2016"
#define DRV_DUALBAND	"Dual Band Disable"


#include <generated/autoconf.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/netdevice.h>
#include <net/hotdata.h>
#include <linux/platform_device.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/in.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <net/xfrm.h>
#include <linux/proc_fs.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/sctp.h>

#ifdef CONFIG_RTK_WFOAX
#define WFO_GMAC_NO 2
#endif
/* to be removed */
#define BSP_EN_GMAC  (1<<1)
#define BSP_EN_GMAC2 (1<<20)
#define BSP_EN_GMAC1 (1<<19)
#define WRITE_MEM32(addr, val)   (*(volatile unsigned int *)   (addr)) = (val)
#define READ_MEM32(addr)         (*(volatile unsigned int *)   (addr))

#include <bspchip.h>


#ifdef CONFIG_RTL865X_ETH_PRIV_SKB
#include "re_privskb.h"
#endif
#ifdef CONFIG_RTL_ETH_RECYCLED_SKB
#include "re_recycleskb.h"
#endif
#ifdef CONFIG_RTL865X_ETH_PRIV_SKB_ADV
#include "re_privskb_adv.h"
#include "brg_shortcut.h"
#endif

#include "../../ethctl_implement.h"
#include "re8686_rtl9607c.h"

#undef __IRAM_NIC
#define __IRAM_NIC __attribute__ ((__section__(".iram-fwd")))

#ifdef CONFIG_RTL8686_SWITCH
//#include "../sdk/include/dal/apollo/raw/apollo_raw_port.h"
#include <rtk/l2.h>
#include <dal/apollomp/dal_apollomp_l2.h>
#include <rtk/switch.h>
#include <rtk/acl.h>
#include <rtk/trap.h>
#include <rtk/gponv2.h> 
#include <rtk/epon.h>
#include <common/type.h>
#include <rtk/intr.h>
#include <module/intr_bcaster/intr_bcaster.h>
#endif

#if defined(CONFIG_RTK_SOC_RTL8198D)
#include <rtk/rate.h>
#include <rtk/stat.h>
#include <rtk/port.h>
#include "hal/common/halctrl.h"
#endif

#ifdef CONFIG_RTK_PTOOL
#include <linux/ptool.h>
#endif

#ifdef CONFIG_RTL_MULTI_LAN_DEV
#ifdef CONFIG_COMMON_RT_API
#include "rt_stat.h"
#include "rt_stat_ext.h"
#include "rt_gpon.h"
#include <common/error.h>
#endif
#endif

#ifdef CONFIG_RG_DEBUG
#include <rtk_rg_profile.h>
#else

#ifdef CONFIG_RTK_SOC_RTL8198D
#include "../../../arch/mips/rtl8198d/bspchip.h"
#endif

#define MIPS_PERF_NIC_RX 0
inline void mips_perf_measure_entrance(int index)
{
}

inline void mips_perf_measure_exit(int index)
{
}
#endif

static inline void kick_tx(unsigned int gmac, int ring_num);
static inline void kick_tx_hw(unsigned int gmac, int ring_num);

#if defined(CONFIG_RTW_MEMPOOL)
extern int rtw_remove_mem_pool_ex(char *name);
extern struct sk_buff *rtw_mem_pool_alloc_skb(int size);
extern void rtw_create_mem_pool_ex(char *name, int max, int min, int size);
#endif

#if defined(CONFIG_APOLLO_ROMEDRIVER)
int re8670_start_xmit_check(struct sk_buff *skb, struct net_device *dev);
#elif defined(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE) && IS_BUILTIN(CONFIG_RTK_L34_FC_KERNEL_MODULE)
extern int rtk_fc_skb_tx(struct sk_buff *skb, struct net_device *dev);
int re8670_start_xmit_fc(struct sk_buff *skb, struct net_device *dev);
#endif
extern int rtk_dev_update_stats(struct net_device *dev, struct rtnl_link_stats64 *stats);
/*static*/ int re8670_start_xmit (struct sk_buff *skb, struct net_device *dev);

/* Jonah + for FASTROUTE */
struct net_device *eth_net_dev[MAX_GMAC_NUM];
struct tasklet_struct *eth_rx_tasklets[MAX_GMAC_NUM]={0};

#define WITH_NAPI		""

/* These identify the driver base version and may not be removed. */
static char version[] = KERN_INFO DRV_NAME " Ethernet driver v" DRV_VERSION " (" DRV_RELDATE ")["DRV_DUALBAND"]" WITH_NAPI "\n";

MODULE_AUTHOR("Sam Hsu <sam_hsu@realtek.com>");
MODULE_DESCRIPTION("RealTek RTL-8686 series 10/100/1000 Ethernet driver");
MODULE_LICENSE("GPL");

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC.  */
static int multicast_filter_limit = 32;
module_param(multicast_filter_limit, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC (multicast_filter_limit, "8686 maximum number of filtered multicast addresses");

#ifdef TX_KICK_RING_USING_POLLING
static int tx_ring_active_poll_sec = TX_RING_ACTIVE_POLLING_SECONDS;
module_param(tx_ring_active_poll_sec, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC (tx_ring_active_poll_sec, "8686 detect Tx ring active time, unit second");

static int tx_ring_active_poll_packet_num = TX_RING_ACTIVE_POLLING_PACKET_NUM;
module_param(tx_ring_active_poll_packet_num, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC (tx_ring_active_poll_packet_num, "8686 detect Tx ring active packet count");

static int tx_tdu_kick_ring_10msec = TX_KICK_RING_POLLING_10MSECONDS;
module_param(tx_tdu_kick_ring_10msec, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC (tx_tdu_kick_ring_10msec, "8686 kick Tx time, unit 10msecond");
#endif

#ifndef CONFIG_RTK_L34_ENABLE
static unsigned int	SWITCH_MODE	= RTL8686_Switch_Mode_Normal;
#endif


#define PFX			DRV_NAME ": "
#define CP_DEF_MSG_ENABLE	(NETIF_MSG_DRV		| \
		NETIF_MSG_PROBE 	| \
		NETIF_MSG_LINK)
#define CP_REGS_SIZE		(0xff + 1)

#define DESC_ALIGN		0x100
#define UNCACHE_MASK		0xa0000000	
#define UNCACHE_ADDR(x)  	(((u32)x)|UNCACHE_MASK)
/*add 1 desc for dummy desc : we will allocate one more desc even if the RING_SIZE is 0, 
this can let the ring which we disable has a dummy desc in its FDP, RD says our NIC need this...*/
#define RE8670_RXRING_BYTES(RING_SIZE)	( (sizeof(struct dma_rx_desc) * (RING_SIZE+1)) + DESC_ALIGN)
#define RE8670_TXRING_BYTES(RING_SIZE)	( (sizeof(struct dma_tx_desc) * (RING_SIZE+1)) + DESC_ALIGN)

#define NEXT_TX(N,RING_SIZE)		(((N) + 1) & (RING_SIZE - 1))
#define NEXT_RX(N,RING_SIZE)		(((N) + 1) & (RING_SIZE - 1))

#define NOT_IN_BITMAP(map, num) (!((map)&(1<<(num))))

static inline int idx_sw2hw(int ring_num) {
	return (MAX_TXRING_NUM-1)-ring_num;
}

static inline int idx_hw2sw(int ring_num) {
	return (MAX_TXRING_NUM-1)-ring_num;
}

//#define PKT_BUF_SZ		1536	/* Size of each temporary Rx buffer.*/
#ifndef CONFIG_RTL865X_ETH_PRIV_SKB_ADV
#define RX_OFFSET		2	//move to re_privskb_adv.h
#endif

/* The following settings are log_2(bytes)-4:  0 == 16 bytes .. 6==1024, 7==end of packet. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT		(3*HZ)
/* hardware minimum and maximum for a single frame's data payload */
#define CP_MIN_MTU		60	/* TODO: allow lower, but pad */
#define CP_MAX_MTU		4096

#define TOTAL_RTL8686_DEV_NUM	((sizeof(rtl8686_dev_table))/(sizeof(struct rtl8686_dev_table_entry)))

#define RLE0787_W32(idx, reg, value)			(*(volatile u32*)((u32)re_private_data[idx].base+reg)) = (u32)value
#define RLE0787_W16(idx, reg, value)			(*(volatile u16*)((u32)re_private_data[idx].base+reg)) = (u16)value
#define RLE0787_W8(idx, reg, value)			(*(volatile u8*)((u32)re_private_data[idx].base+reg)) = (u8)value
#define RLE0787_R32(idx, reg)				(*(volatile u32*)((u32)re_private_data[idx].base+reg))
#define RLE0787_R16(idx, reg)				(*(volatile u16*)((u32)re_private_data[idx].base+reg))
#define RLE0787_R8(idx, reg)				(*(volatile u8*)((u32)re_private_data[idx].base+reg))
#define RLE0787_SET_BIT32(idx, reg, bits)		(RLE0787_W32(idx, reg, (RLE0787_R32(idx, reg) | (bits))))
#define RLE0787_CLR_BIT32(idx, reg, bits)		(RLE0787_W32(idx, reg, (RLE0787_R32(idx, reg) & ~(bits))))

#define TX_HQBUFFS_AVAIL(CP,ring_num)					\
		(((CP)->tx_Mhqtail[ring_num] - (CP)->tx_Mhqhead[ring_num] + (CP)->re8670_tx_ring_size[ring_num] - 1)&((CP)->re8670_tx_ring_size[ring_num] - 1))		

#define RX_HQBUFFS_EMPTY(CP,ring_num)					\
		(!ring_num ? ((CP)->rx_Mtail[ring_num] == RLE0787_R16((CP)->gmac, RxCDO)) : ((CP)->rx_Mtail[ring_num] == RLE0787_R16((CP)->gmac, RxCDO2+(ADDR_OFFSET*(ring_num-1)))))
		
#ifdef RX_MRING_INT_SPLIT/*plz add into both define and not define area*/
#define en_rx_mring_int_split(idx) RLE0787_W32(idx, CONFIG_REG, (RLE0787_R32(idx, CONFIG_REG) | En_int_split))
#define set_rring_route(idx) {RLE0787_W32(idx, RRING_ROUTING1, 0x65432111);RLE0787_W32(idx, RRING_ROUTING2, 0x65432111);RLE0787_W32(idx, RRING_ROUTING3, 0x65432111);RLE0787_W32(idx, RRING_ROUTING4, 0x65432111);RLE0787_W32(idx, RRING_ROUTING5, 0x65432111);RLE0787_W32(idx, RRING_ROUTING6, 0x65432111);RLE0787_W32(idx, RRING_ROUTING7, 0x65432111);}
#define retrieve_isr1_status(idx) RLE0787_R32(idx, ISR1)
#define assigne_cpisr_status(x, y) (x) = (y)
#define assigne_cpisr1_status(x, y) (x) = (y)
#define gather_rx_isr(x,y) (x) = (x)|(!!(y & 0x3f))
#define gather_tx_isr(x,y) (x) = (x)|((!!(y & 0x1f0000)) << 16)|((!!(y & 0x1f000000)) << 24)
#define CLEAR_ISR1(idx, x) RLE0787_W32(idx, ISR1, (x))
#ifdef TX_INTR_HANDLE
#define MASK_IMR1_TXALL(idx) RLE0787_R32(idx, IMR1)&=~((re_private_data[idx].tx_multiring_bitmap<<16)|(re_private_data[idx].tx_multiring_bitmap<<24))
#define UNMASK_IMR1_TXALL(idx) RLE0787_R32(idx, IMR1)|=((re_private_data[idx].tx_multiring_bitmap<<16)|(re_private_data[idx].tx_multiring_bitmap<<24))
#define UNMASK_IMR1_TOK(idx, tx_ring_bitmap) RLE0787_R32(idx, IMR1)|=tx_ring_bitmap
#define UNMASK_IMR1_TDU(idx, tx_ring_bitmap) RLE0787_R32(idx, IMR1)|=tx_ring_bitmap
#define MASK_IMR0_TXALL(idx) RLE0787_R32(idx, IMR0)&=~((re_private_data[idx].tx_multiring_bitmap<<16)|(re_private_data[idx].tx_multiring_bitmap<<24))
#define MASK_IMR0_TOK(idx, tx_ring_bitmap) RLE0787_R32(idx, IMR0)&=~tx_ring_bitmap
#define MASK_IMR0_TDU(idx, tx_ring_bitmap) RLE0787_R32(idx, IMR0)&=~tx_ring_bitmap
#define UNMASK_IMR0_TXALL(idx) RLE0787_R32(idx, IMR0)|=((re_private_data[idx].tx_multiring_bitmap<<16)|(re_private_data[idx].tx_multiring_bitmap<<24))
#define UNMASK_IMR0_TOK(idx, tx_ring_bitmap) RLE0787_R32(idx, IMR0)|=tx_ring_bitmap
#define CLEAR_ISR1_TOK(idx, tx_ring_bitmap) RLE0787_W32(idx, ISR1, tx_ring_bitmap)
#define UNMASK_IMR0_TDU(idx, tx_ring_bitmap) RLE0787_R32(idx, IMR0)|=tx_ring_bitmap
#define CLEAR_ISR1_TDU(idx, tx_ring_bitmap) RLE0787_W32(idx, ISR1, tx_ring_bitmap)
#endif
#define MASK_IMR0_RXALL(idx) RLE0787_R32(idx, IMR0)&=~(re_private_data[idx].rx_multiring_bitmap)
#define UNMASK_IMR0_RXALL(idx) RLE0787_R32(idx, IMR0)|=(re_private_data[idx].rx_multiring_bitmap)
#else
#define en_rx_mring_int_split(idx)
#define set_rring_route(idx)
#define retrieve_isr1_status(idx) 0
#define assigne_cpisr_status(x, y)
#define assigne_cpisr1_status(x, y)
#define gather_rx_isr(x, y)
#define CLEAR_ISR1(idx, x) 
#define MASK_IMR0_RXALL(idx)
#define UNMASK_IMR0_RXALL(idx)
#endif

#if defined(CONFIG_RTL9607C_SERIES)
uint32 sramSizeMappingArray[RTK_DYNAMIC_SRAM_MAX_BYTES] = {0, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
uint32 previous_dynamic_sram_desc = 0;
uint32 dynamic_sram_desc = 0;
uint32 dynamic_mapping_buffer[4]={NULL,NULL,NULL,NULL};
uint32 dynamic_mapping_buffer_addr_current;
uint32 dynamic_mapping_buffer_size_idx[4]={0,0,0,0};

#if defined(HWNAT_CUSTOMIZE) && defined(CONFIG_RTL9607C_SERIES)
#define MAX_HWNAT_CUSTOMIZED_TYPE_NUM  4
uint32 hwnat_customized_version = 2;

uint32 hwnat_customized_up_gmac = 1;
uint32 hwnat_customized_up0_gmac = 1;
uint32 hwnat_customized_down_gmac = 2;
uint32 hwnat_customized_down0_gmac = 0;

uint32 hwnat_customized_up_flowNum = 4;
uint32 hwnat_customized_up_rx_ringNum = 4;
uint32 hwnat_customized_up_tx_ringNum = 3;

uint32 hwnat_customized_up0_flowNum = 4;
uint32 hwnat_customized_up0_rx_ringNum = 4;
uint32 hwnat_customized_up0_tx_ringNum = 3;

uint32 hwnat_customized_down_flowNum = 2;
uint32 hwnat_customized_down_rx_ringNum = 4;
uint32 hwnat_customized_down_tx_ringNum = 3;
uint32 hwnat_customized_extra_gmac = 0;
uint32 hwnat_customized_extra_up_rx_ringNum = 4;
uint32 hwnat_customized_extra_up_tx_ringNum = 3;
uint32 hwnat_customized_extra_down_rx_ringNum = 3;
uint32 hwnat_customized_extra_down_tx_ringNum = 2;

uint32 hwnat_customized_down0_flowNum = 2;
uint32 hwnat_customized_down0_rx_ringNum = 4;
uint32 hwnat_customized_down0_tx_ringNum = 3;


#define CHECK_TX_OWN_BIT_INTERVAL_NUM 1 // must be power of 2, e.g., 1, 2, 4, 8, ...
uint32 hwnat_customized_check_tx_done = 1;

#if 1 //Wen: For sync VXLAN/NPTv6 fastforward issue, sync from luna_pro_cmcc_cu: [JIM][revision:39486]fix lock wait problem for nic acc data path.
static char re8686_customized_rx_and_tx_used[MAX_GMAC_NUM] = { 0, 0, 0 };
#endif
//RX
static customized_rxHook_t re8686_rx_ring_customized_func[MAX_GMAC_NUM][MAX_RXRING_NUM][CUSTOMIZE_TYPE_MAX-1] = 
{
	{{NULL}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}},
	{{NULL}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}},
	{{NULL}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}}
};

static char re8686_rx_ring_customized_preLen[MAX_GMAC_NUM][MAX_RXRING_NUM] = 
{
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
};

static unsigned int re8686_rx_ring_customized_tx_ringNum[MAX_GMAC_NUM][MAX_RXRING_NUM] = 
{
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}
};

typedef struct re8686_customized_tx_descAddr_s
{
	volatile DMA_TX_DESC *fs_descAddr;
	volatile DMA_TX_DESC *ls_descAddr;
}re8686_customized_tx_descAddr_t;

static volatile re8686_customized_tx_descAddr_t* re8686_rx_descIdx_customized_tx_descAddr[MAX_GMAC_NUM][MAX_RXRING_NUM] = 
{
	{NULL, NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL}
};

static unsigned char* re8686_rx_ring_data_buffer[MAX_GMAC_NUM][MAX_RXRING_NUM] = 
{
	{NULL, NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL}
};

static unsigned char re8686_rx_ring_fc_state[MAX_GMAC_NUM][MAX_RXRING_NUM] = 
{
	{ON, ON, ON, ON, ON, ON},
	{ON, ON, ON, ON, ON, ON},
	{ON, ON, ON, ON, ON, ON}
};

static int re8686_rx_ring_previousDesc[MAX_GMAC_NUM][MAX_RXRING_NUM][CHECK_TX_OWN_BIT_INTERVAL_NUM];


static unsigned int re8686_rx_ring_ext_pmsk[MAX_GMAC_NUM][MAX_RXRING_NUM][CUSTOMIZE_TYPE_MAX-1] = 
{
	{{0}, {0}, {0}, {0}, {0}, {0}},
	{{0}, {0}, {0}, {0}, {0}, {0}},
	{{0}, {0}, {0}, {0}, {0}, {0}}
};
//TX
static unsigned int re8686_tx_ring_customized[MAX_GMAC_NUM][MAX_TXRING_NUM] = 
{
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0}
};

static unsigned char* re8686_tx_ring_hdr_buffer[MAX_GMAC_NUM][MAX_RXRING_NUM] = 
{
	{NULL, NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL}
};

static unsigned char* re8686_tx_ring_hdr_buffer_sram_aligned[MAX_GMAC_NUM][MAX_RXRING_NUM] = 
{
	{NULL, NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL}
};

static customized_txHook_t re8686_tx_ring_customized_func[MAX_GMAC_NUM][MAX_TXRING_NUM] = 
{
	{NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL}
};

static char re8686_tx_ring_addr_offset[MAX_GMAC_NUM][MAX_RXRING_NUM][CUSTOMIZE_TYPE_MAX-1] = 
{
	{{0}, {0}, {0}, {0}, {0}, {0}},
	{{0}, {0}, {0}, {0}, {0}, {0}},
	{{0}, {0}, {0}, {0}, {0}, {0}}
};

static struct tx_info re8686_tx_ring_txInfo[MAX_GMAC_NUM][MAX_RXRING_NUM][CUSTOMIZE_TYPE_MAX-1] = 
{
	{{{0}},{{0}},{{0}},{{0}},{{0}},{{0}}},
	{{{0}},{{0}},{{0}},{{0}},{{0}},{{0}}},
	{{{0}},{{0}},{{0}},{{0}},{{0}},{{0}}}
};	
#endif
#endif

static struct re_private_root re_private_data_root = {0};
static struct re_private re_private_data[MAX_GMAC_NUM] = {{0}};

static int str_valid(const char *s)
{
	const char *p;

	for (p = s; *p != '\0' && *p != '\r' && *p != '\n'; p++) {
		if ((*p < 32) || (*p > 126))
			return 0;
	}
	return 1;
}

void memDump(void *start, u32 size, char *strHeader, char format)
{
	int row, column, index, index2, max;
//	uint32 buffer[5];
	u8 *buf, *line, ascii[17];
	char empty = '.';

	if(!start ||(size==0))
		return;
	line = (u8*)start;

	/*
	16 bytes per line
	*/
	if (strHeader)
		printk(KERN_CONT "%s", strHeader);
	column = size % 16;
	row = (size / 16) + 1;
	for (index = 0; index < row; index++, line += 16) 
	{
		buf = line;

		memset (ascii, 0, 17);

		max = (index == row - 1) ? column : 16;
		if ( max==0 ) break; /* If we need not dump this line, break it. */

		if(format & RTL8686_MEM_DUMP_FORMAT_ADDR)
			printk(KERN_CONT "\n%08x ", (u32) line);
		else
			printk(KERN_CONT "\n ");
		
		//Hex
		if(format & RTL8686_MEM_DUMP_FORMAT_HEX)
		{
			for (index2 = 0; index2 < max; index2++)
			{
				if (index2 == 8)
					printk(KERN_CONT "  ");
				printk(KERN_CONT "%02x ", (u8) buf[index2]);
				ascii[index2] = (((u8) buf[index2] < 32) || ((u8) buf[index2] >= 128)) ? empty : buf[index2];
			}

			if (max != 16)
			{
				if (max < 8)
					printk(KERN_CONT "  ");
				for (index2 = 16 - max; index2 > 0; index2--)
					printk(KERN_CONT "   ");
			}
		}
		else
		{
			for (index2 = 0; index2 < max; index2++)
			{
				ascii[index2] = (((u8) buf[index2] < 32) || ((u8) buf[index2] >= 128)) ? empty : buf[index2];
			}
		}

		//ASCII
		if(format & RTL8686_MEM_DUMP_FORMAT_TEXT)
			printk(KERN_CONT "  %s", ascii);
	}
	printk(KERN_CONT "\n");
	return;
}

#undef ETH_DBG
#define ETH_DBG
#ifdef ETH_DBG
static void skb_debug(const struct sk_buff *skb, int enable, int times, int flag)
{
#define NUM2PRINT 1518
	if (unlikely(times && (enable&flag))) {
		if (flag == RTL8686_SKB_RX)
			printk(KERN_CONT "\nI: ");
		else
			printk(KERN_CONT "\nO: ");
		printk(KERN_CONT "eth len = %d eth name %s", skb->len,skb->dev?skb->dev->name:"");
		memDump(skb->data, (skb->len > NUM2PRINT)?NUM2PRINT : skb->len, "", RTL8686_MEM_DUMP_FORMAT_ALL);
		if(skb->len > NUM2PRINT){
			printk(KERN_CONT "........");
		}
		printk(KERN_CONT "\n");
	}
}
//int32 dump_hs (void);
//int32 dump_lut_table (void);

void rxinfo_debug(struct re_private *cp, struct rx_info *pRxInfo, int ring_num)
{
	if (unlikely(cp->debug_times && (cp->debug_enable&RTL8686_RXINFO))) {
		printk(KERN_CONT "rxInfo[ring%d]:\n", ring_num);
		printk(KERN_CONT "opts1\t= 0x%08x Own=%d EOR=%d FS=%d LS=%d CRCErr=%d IP4CSErr=%d L4CSErr=%d RCDF=%d IPFrag=%d PPPoETag=%d RWT=%d Len=%d\n", pRxInfo->opts1.dw,
			pRxInfo->opts1.bit.own,
			pRxInfo->opts1.bit.eor,
			pRxInfo->opts1.bit.fs,
			pRxInfo->opts1.bit.ls,
			pRxInfo->opts1.bit.crcerr,
			pRxInfo->opts1.bit.ipv4csf,
			pRxInfo->opts1.bit.l4csf,
			pRxInfo->opts1.bit.rcdf,
			pRxInfo->opts1.bit.ipfrag,			
			pRxInfo->opts1.bit.pppoetag,
			pRxInfo->opts1.bit.rwt,
			pRxInfo->opts1.bit.data_length
		);
		printk(KERN_CONT "addr\t= 0x%08x\n", pRxInfo->addr);
		printk(KERN_CONT "opts2\t= 0x%08x CpuTag=%d PTPCpuTagEst=%d SVLAN_Est=%d Reason=%d CtagVa=%d CvlanTag=%d\n", pRxInfo->opts2.dw,
			pRxInfo->opts2.bit.cputag,	
			pRxInfo->opts2.bit.ptp_in_cpu_tag_exist,
			pRxInfo->opts2.bit.svlan_tag_exist,
			pRxInfo->opts2.bit.reason,
			pRxInfo->opts2.bit.ctagva,
			pRxInfo->opts2.bit.cvlan_tag
		);		
		printk(KERN_CONT "opts3\t= 0x%08x IntPri=%d PONSID_or_EXTSPA=%d L3Rout=%d ORG=%d SrcPortNum=%d FBI=%d FBHSH_or_DstPortmsk=0x%x\n", pRxInfo->opts3.dw,
			pRxInfo->opts3.bit.internal_priority,	
			pRxInfo->opts3.bit.pon_sid_or_extspa,	
			pRxInfo->opts3.bit.l3routing,	
			pRxInfo->opts3.bit.origformat,	
			pRxInfo->opts3.bit.src_port_num,
			pRxInfo->opts3.bit.fbi,
			pRxInfo->opts3.bit.fb_hash_or_dst_portmsk
		);
	}
}

void txinfo_debug(struct re_private *cp, struct tx_info *pTxInfo)
{
	if (unlikely(cp->debug_times && (cp->debug_enable&RTL8686_TXINFO))) {
		printk(KERN_CONT "txInfo:\n");
		printk(KERN_CONT "opts1\t= 0x%08x Own=%d EOR=%d FS=%d LS=%d IPCS=%d L4CS=%d TPID_Sel=%d STAG_Aware=%d CRC=%d Len=%d\n", pTxInfo->opts1.dw,
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
		printk(KERN_CONT "addr\t= 0x%08x \n", pTxInfo->addr);
		printk(KERN_CONT "opts2\t= 0x%08x CPUTag=%d SVLANAct=%d CVLANAct=%d TxPmsk=0x%x CVLAN_VID=%d CVLAN_Pri=%d\n", pTxInfo->opts2.dw,
			pTxInfo->opts2.bit.cputag,			
			pTxInfo->opts2.bit.tx_svlan_action,
			pTxInfo->opts2.bit.tx_cvlan_action,
			pTxInfo->opts2.bit.tx_portmask,
			pTxInfo->opts2.bit.cvlan_vidh<<8|pTxInfo->opts2.bit.cvlan_vidl,
			pTxInfo->opts2.bit.cvlan_prio
		);
		printk(KERN_CONT "opts3\t= 0x%08x AsPri=%d CpuPri=%d Keep=%d DisLrn=%d Psel=%d L34Keep=%d extspa=%d Pppoe=%d SidIdx=%d PON_SID=%d\n", pTxInfo->opts3.dw,
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
		printk(KERN_CONT "opts4\t= 0x%08x LgsEn=%d LgMtu=%d SVLAN_VID=%d SVLAN_Pri=%d\n", pTxInfo->opts4.dw,
			pTxInfo->opts4.bit.lgsen,
			pTxInfo->opts4.bit.lgmtu,
			pTxInfo->opts4.bit.svlan_vidh<<8|pTxInfo->opts4.bit.svlan_vidl,
			pTxInfo->opts4.bit.svlan_prio
		);	
	}
}

static char mt_watch_tmp[512];

#define OTHERS_TRACE(gmac, enable, times, comment, arg...) \
do {\
		if(unlikely(times && (enable&RTL8686_OTHERS)))\
		{\
			u8 mt_trace_head_str[]="[NIC OTHERS]\033[1;36m"; \
			int mt_trace_i;\
			sprintf( mt_watch_tmp, comment,## arg);\
			for(mt_trace_i=0;mt_trace_i<512;mt_trace_i++) \
			{\
				if(mt_watch_tmp[mt_trace_i]==0) break; \
			}\
			if(mt_watch_tmp[mt_trace_i-1]=='\n') mt_watch_tmp[mt_trace_i-1]=' ';\
			printk(KERN_CONT "%s %s \033[1;30m@%s:%d\033[0m\n",mt_trace_head_str,mt_watch_tmp,__FILE__,__LINE__);\
		}\
} while(0)

#define RX_TRACE(gmac, enable, times, comment, arg...) \
do {\
		if(unlikely(times && (enable&RTL8686_RX_TRACE)))\
		{\
			u8 mt_trace_head_str[]="[NIC RX]\033[1;36m"; \
			int mt_trace_i;\
			sprintf( mt_watch_tmp, comment,## arg);\
			for(mt_trace_i=0;mt_trace_i<512;mt_trace_i++) \
			{\
				if(mt_watch_tmp[mt_trace_i]==0) break; \
			}\
			if(mt_watch_tmp[mt_trace_i-1]=='\n') mt_watch_tmp[mt_trace_i-1]=' ';\
			printk(KERN_CONT "%s %s \033[1;30m@%s:%d\033[0m\n",mt_trace_head_str,mt_watch_tmp,__FILE__,__LINE__);\
		}\
} while(0)

#define TX_TRACE(gmac, enable, times, comment, arg...) \
do {\
		if(unlikely(times && (enable&RTL8686_TX_TRACE)))\
		{\
			u8 mt_trace_head_str[]="[NIC TX]\033[1;36m"; \
			int mt_trace_i;\
			sprintf( mt_watch_tmp, comment,## arg);\
			for(mt_trace_i=0;mt_trace_i<512;mt_trace_i++) \
			{\
				if(mt_watch_tmp[mt_trace_i]==0) break; \
			}\
			if(mt_watch_tmp[mt_trace_i-1]=='\n') mt_watch_tmp[mt_trace_i-1]=' ';\
			printk(KERN_CONT "%s %s \033[1;30m@%s:%d\033[0m\n",mt_trace_head_str,mt_watch_tmp,__FILE__,__LINE__);\
		}\
} while(0)

#define RX_WARN(gmac, enable, times, comment, arg...) \
do {\
		if(unlikely(times && (enable&RTL8686_RX_WARN)))\
		{\
			int mt_trace_i;\
			sprintf( mt_watch_tmp, comment,## arg);\
			for(mt_trace_i=0;mt_trace_i<512;mt_trace_i++) \
			{\
				if(mt_watch_tmp[mt_trace_i]==0) break; \
			}\
			if(mt_watch_tmp[mt_trace_i-1]=='\n') mt_watch_tmp[mt_trace_i-1]=' ';\
			printk(KERN_CONT "%s\n",mt_watch_tmp);\
		}\
} while(0)

#define TX_WARN(gmac, enable, times, comment, arg...) \
do {\
		if(unlikely(times && (enable&RTL8686_TX_WARN)))\
		{\
			int mt_trace_i;\
			sprintf( mt_watch_tmp, comment,## arg);\
			for(mt_trace_i=0;mt_trace_i<512;mt_trace_i++) \
			{\
				if(mt_watch_tmp[mt_trace_i]==0) break; \
			}\
			if(mt_watch_tmp[mt_trace_i-1]=='\n') mt_watch_tmp[mt_trace_i-1]=' ';\
			printk(KERN_CONT "%s\n",mt_watch_tmp);\
		}\
} while(0)

#define ETHDBG_PRINT(gmac, enable, times, flag, fmt, args...)  if(unlikely(times && (enable&flag))) printk(fmt, ##args)
#define SKB_DBG(args...) skb_debug(args)
#define RXINFO_DBG(CP, args...) rxinfo_debug(CP, args)
#define TXINFO_DBG(CP, args...) txinfo_debug(CP, args)
#define RXDBG_TIMES_UPDATE(CP) \
do { \
		if(unlikely((CP->debug_times>0) && (CP->debug_enable&RTL8686_RX_ALL))) \
		{ \
			CP->debug_times--; \
		} \
} while(0)
#define TXDBG_TIMES_UPDATE(CP) \
do { \
		if(unlikely((CP->debug_times>0) && (CP->debug_enable&RTL8686_TX_ALL))) \
		{ \
			CP->debug_times--; \
		} \
} while(0)
#else
#define RX_TRACE(gmac, enable, times, args...)
#define TX_TRACE(gmac, enable, times, args...)
#define RX_WARN(gmac, enable, times, comment, arg...)
#define TX_WARN(gmac, enable, times, comment, arg...)
#define ETHDBG_PRINT(gmac, enable, times, flag, fmt, args...)
#define SKB_DBG(args...) 
#define RXINFO_DBG(CP, args...)
#define TXINFO_DBG(CP, args...)
#define RXDBG_TIMES_UPDATE(CP)
#define TXDBG_TIMES_UPDATE(CP)
#endif

struct ring_info {
	struct sk_buff		*skb;
	dma_addr_t		mapping;
	unsigned		frag;
};

static int dev_num=0;

#define ROOTDEV	(re_private_data_root.dev)
#define ROOTPRIV2PRIV(idx)  ((struct re_private*)re_private_data_root.re_private_data_ptr[idx])
#define PRIV2DEV(priv)  (((struct re_private_root*)priv)->dev)
#define DEV2CP(dev)  (((struct re_dev_private*)netdev_priv(dev))->pCp)
#define DEVPRIV(dev)  ((struct re_dev_private*)netdev_priv(dev))
#define VTAG2DESC(d) (((((d) & 0x00ff)<<8) | (((d) & 0xff00)>>8)) & 0x0000ffff)
/*warning! +1 for smux.................................................*/
#define VTAG2VLANTCI(v) (( (((v) & 0xff00)>>8) | (((v) & 0x00ff)<<8) ) + 1) 

static void __re8670_set_rx_mode(unsigned int gmac);
static void re8670_tx(struct re_private *cp, int ring_num, int from_hw);

static void re8670_clean_rings(struct re_private *cp);
static void re8670_tx_timeout(struct net_device *dev, unsigned int);
static int change_dev_port_mapping(int port_num ,char* name);
static void sync_dev_port_mapping(void);
int re8670_reset(void);

/*================================================
			GMAC used Global Variable
================================================*/ 

#if defined(CONFIG_RTL865X_ETH_PRIV_SKB)
extern int eth_skb_free_num;
extern int eth_skb_alloc_num;
extern int lowest_eth_skb_free_num;
extern int critical_eth_skb_free_num;
extern int dynamic_alloc_skb_num;
#endif

static struct rtl8686_dev_table_entry rtl8686_dev_table[] = {	
	//ifname, ifflag, vid, phyPort, dev_instant
	{"eth0",		RTL8686_ELAN, 0, CPU_PORT0, 0, NULL}, // root dev eth0 must be first
	{"eth0.2",		RTL8686_ELAN, 0, LAN_PORT1, 1, NULL},
	{"eth0.3",		RTL8686_ELAN, 0, LAN_PORT2, 1, NULL},
	{"eth0.4",		RTL8686_ELAN, 0, LAN_PORT3, 1, NULL},
	{"eth0.5",		RTL8686_ELAN, 0, LAN_PORT4, 1, NULL},
	{"eth0.6",		RTL8686_ELAN, 0, LAN_PORT5, 1, NULL},
	{"eth0.7",		RTL8686_ELAN, 0, LAN_PORT6, 1, NULL},
#if !defined(CONFIG_ETHWAN_USE_USB_SGMII) && !defined(CONFIG_ETHWAN_USE_PCIE1_SGMII)
	{"eth0.8",		RTL8686_ELAN, 0, APOLLOPRO_SGMII0_PORT, 1, NULL},
	{"eth0.9",		RTL8686_ELAN, 0, APOLLOPRO_SGMII1_PORT, 1, NULL},
#elif !defined(CONFIG_ETHWAN_USE_USB_SGMII)
	{"eth0.8",		RTL8686_ELAN, 0, APOLLOPRO_SGMII0_PORT, 1, NULL},
#elif !defined(CONFIG_ETHWAN_USE_PCIE1_SGMII)
	{"eth0.8",		RTL8686_ELAN, 0, APOLLOPRO_SGMII1_PORT, 1, NULL},
#endif
	{"nas0",			RTL8686_WAN, 0, WAN_PORT, 0, NULL},
#if defined(CONFIG_RTL_MULTI_PHY_ETH_WAN)
	{"ifprobe",		RTL8686_WAN, 0, LAN_PORT6, 0, NULL}, 
#endif
};

#ifdef CONFIG_RTL8686_SWITCH
struct netif_carrier_dev_mapping
{
	unsigned char 	ifname[IFNAMSIZ];
	struct net_device *phy_dev;
	unsigned char status; // 0 : disable , 1 : enable
	int linkchangetimes;
};

// global variable for link change interrupt device mapping

struct netif_carrier_dev_mapping LCDev_mapping[SW_PORT_NUM];

static void gmacintr_notifier_link_change(intrBcasterMsg_t	*pMsgData)
{
#if 0
    printk(KERN_CONT "intrType	= %d\n", pMsgData->intrType);
    printk(KERN_CONT "intrSubType	= %u\n", pMsgData->intrSubType);
    printk(KERN_CONT "intrBitMask	= %u\n", pMsgData->intrBitMask);
    printk(KERN_CONT "intrStatus	= %d\n", pMsgData->intrStatus);
#endif

	if(!IS_LAN_PORT(pMsgData->intrBitMask))
		return;

	if(pMsgData->intrType == MSG_TYPE_LINK_CHANGE) // Link Change Interrupt
	{
		if(pMsgData->intrBitMask < SW_PORT_NUM)
		{
			if(pMsgData->intrSubType == 1U && !netif_carrier_ok(LCDev_mapping[pMsgData->intrBitMask].phy_dev)) // Link Up
			{
				netif_carrier_on(LCDev_mapping[pMsgData->intrBitMask].phy_dev);
				LCDev_mapping[pMsgData->intrBitMask].status = (u8)1;
			}
			else if (pMsgData->intrSubType == 2U && netif_carrier_ok(LCDev_mapping[pMsgData->intrBitMask].phy_dev)) //Link Down 
			{
				netif_carrier_off(LCDev_mapping[pMsgData->intrBitMask].phy_dev);
				LCDev_mapping[pMsgData->intrBitMask].status = (u8)0;
			}
			else printk(KERN_CONT "something error ?!\n");
		}
	}
}
// Handle Link Change Interrupt for Netlink 
static intrBcasterNotifier_t GMAClinkChangeNotifier = {
    .notifyType = MSG_TYPE_LINK_CHANGE,
    .notifierCb = gmacintr_notifier_link_change,
};

#if defined(CONFIG_ETHWAN_USE_USB_SGMII) || defined(CONFIG_ETHWAN_USE_PCIE1_SGMII)
static void gmacintr_notifier_ethwan_link_change(intrBcasterMsg_t	*pMsgData)
{
#if 0
    printk(KERN_CONT "intrType	= %d\n", pMsgData->intrType);
    printk(KERN_CONT "intrSubType	= %u\n", pMsgData->intrSubType);
    printk(KERN_CONT "intrBitMask	= %u\n", pMsgData->intrBitMask);
    printk(KERN_CONT "intrStatus	= %d\n", pMsgData->intrStatus);
#endif

	if(!IS_WAN_PORT(pMsgData->intrBitMask))
		return;

	if(pMsgData->intrType == MSG_TYPE_LINK_CHANGE) // Link Change Interrupt
	{
		if(pMsgData->intrBitMask < SW_PORT_NUM && pMsgData->intrBitMask >= 0)
		{
			if(pMsgData->intrSubType == 1U && !netif_carrier_ok(LCDev_mapping[pMsgData->intrBitMask].phy_dev) // Link Up
			{
				netif_carrier_on(LCDev_mapping[pMsgData->intrBitMask].phy_dev);
				LCDev_mapping[pMsgData->intrBitMask].status = 1;
			}
			else if (pMsgData->intrSubType == 2U && netif_carrier_ok(LCDev_mapping[pMsgData->intrBitMask].phy_dev)) //Link Down 
			{
				netif_carrier_off(LCDev_mapping[pMsgData->intrBitMask].phy_dev);
				LCDev_mapping[pMsgData->intrBitMask].status = 0;
			}
			else printk("something error ?!\n");
		}
	}
}
// Handle Link Change Interrupt for Netlink 
static intrBcasterNotifier_t GMACethwanStateChangeNotifier = {
    .notifyType = MSG_TYPE_LINK_CHANGE,
    .notifierCb = gmacintr_notifier_ethwan_link_change,
};
#else
static void gmacintr_notifier_onu_state_change(intrBcasterMsg_t	*pMsgData)
{
#if 0
    printk(KERN_CONT "intrType	= %d\n", pMsgData->intrType);
    printk(KERN_CONT "intrSubType	= %u\n", pMsgData->intrSubType);
    printk(KERN_CONT "intrBitMask	= %u\n", pMsgData->intrBitMask);
    printk(KERN_CONT "intrStatus	= %d\n", pMsgData->intrStatus);
#endif

	if(!IS_PON_PORT(pMsgData->intrBitMask))
		return;

	if(pMsgData->intrType == MSG_TYPE_ONU_STATE) // ONU state Change event
	{
		if(pMsgData->intrBitMask < SW_PORT_NUM)
		{
			if(pMsgData->intrSubType == GPON_STATE_O5 && !netif_carrier_ok(LCDev_mapping[pMsgData->intrBitMask].phy_dev)) // Link Up
			{
				netif_carrier_on(LCDev_mapping[pMsgData->intrBitMask].phy_dev);
				LCDev_mapping[pMsgData->intrBitMask].status = 1;
			}
			else if (pMsgData->intrSubType != GPON_STATE_O5 && netif_carrier_ok(LCDev_mapping[pMsgData->intrBitMask].phy_dev)) //Link Down
			{
				netif_carrier_off(LCDev_mapping[pMsgData->intrBitMask].phy_dev);
				LCDev_mapping[pMsgData->intrBitMask].status = 0;
			}
		}
	}
}
// Handle Link Change Interrupt for Netlink
static intrBcasterNotifier_t GMAConuStateChangeNotifier = {
    .notifyType = MSG_TYPE_ONU_STATE,
    .notifierCb = gmacintr_notifier_onu_state_change,
};

static void gmacintr_notifier_epon_state_change(intrBcasterMsg_t	*pMsgData)
{
#if 0
    printk(KERN_CONT "intrType	= %d\n", pMsgData->intrType);
    printk(KERN_CONT "intrSubType	= %u\n", pMsgData->intrSubType);
    printk(KERN_CONT "intrBitMask	= %u\n", pMsgData->intrBitMask);
    printk(KERN_CONT "intrStatus	= %d\n", pMsgData->intrStatus);
#endif

	if (pMsgData->intrType == MSG_TYPE_EPON_STATE)
	{
		if(pMsgData->intrSubType == RTK_EPON_STATE_OAM_AUTH_SUCC && !netif_carrier_ok(LCDev_mapping[APOLLOPRO_PON_PORT].phy_dev)) // Link Up
		{
			netif_carrier_on(LCDev_mapping[APOLLOPRO_PON_PORT].phy_dev);
			LCDev_mapping[APOLLOPRO_PON_PORT].status = 1;
		}
		else if (pMsgData->intrSubType != RTK_EPON_STATE_OAM_AUTH_SUCC && netif_carrier_ok(LCDev_mapping[APOLLOPRO_PON_PORT].phy_dev)) //Link Down
		{
			netif_carrier_off(LCDev_mapping[APOLLOPRO_PON_PORT].phy_dev);
			LCDev_mapping[APOLLOPRO_PON_PORT].status = 0;
		}
	}
}

// Handle EPON Link Change Interrupt for Netlink
static intrBcasterNotifier_t GMACoamStateChangeNotifier = {
    .notifyType = MSG_TYPE_EPON_STATE,
    .notifierCb = gmacintr_notifier_epon_state_change,
};

#endif
#endif
static int re8670_set_mac_addr(struct net_device *dev, void *addr_p)
{
	struct re_private_root *root_cp = DEV2CP(dev);
	struct re_private *cp;
	unsigned int gmac;
	struct sockaddr *addr = addr_p;

	if (netif_running(dev))
        return -EBUSY;

    if (!is_valid_ether_addr(addr->sa_data))
    	return -EADDRNOTAVAIL;

	eth_hw_addr_set(dev, addr->sa_data);
	
	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		cp = root_cp->re_private_data_ptr[gmac];
		
		if(cp->gmac_enabled != GMAC_TRUE)
			continue;
	
		RLE0787_W32(cp->gmac, IDR0 , (dev->dev_addr[0] << 24) | (dev->dev_addr[1] << 16) | 
				(dev->dev_addr[2] << 8) | (dev->dev_addr[3] << 0));
		RLE0787_W32(cp->gmac, IDR4 , (dev->dev_addr[4] << 24) | (dev->dev_addr[5] << 16));
	}
    return 0;
}

static int config_rx_sideband(u32 gmac, u8 enabled)
{
	if(enabled)
		RLE0787_SET_BIT32(gmac, CONFIG_REG, (1<<22)|(1<<23));
	else
		RLE0787_CLR_BIT32(gmac, CONFIG_REG, (1<<22)|(1<<23));

	return 0;
}

static int config_tx_jumbo(u32 gmac, u8 enabled)
{
	if(enabled)
	{
		RLE0787_SET_BIT32(gmac, TCR, (1<<15));
		RLE0787_SET_BIT32(gmac, IO_CMD, (1<<28));
		RLE0787_SET_BIT32(gmac, CONFIG_REG, (1<<16));
	}
	else
	{
		RLE0787_CLR_BIT32(gmac, TCR, (1<<15));
		RLE0787_CLR_BIT32(gmac, IO_CMD, (1<<28));
		RLE0787_CLR_BIT32(gmac, CONFIG_REG, (1<<16));
	}

	return 0;
}

#ifdef CONFIG_REALTEK_HW_LSO
static int re8670_set_mtu(struct net_device *dev, int new_mtu)
{
	//printk(KERN_CONT "%s-%d: dev=%s new_mtu=%d\n", __func__, __LINE__, dev->name, new_mtu);
	
	if (new_mtu < 68 || new_mtu > RE8686_ETH_DATA_LEN)
	{
		return -EINVAL;
	}

	dev->mtu = new_mtu;
	return 0;
}

static int re8670_init_mtu(void)
{
	unsigned int totalDev = TOTAL_RTL8686_DEV_NUM;
	int i;
	
	for(i=0;i<totalDev;i++)
	{
		re8670_set_mtu(rtl8686_dev_table[i].dev_instant, CP_LS_MTU);
	}

	return 0;
}

static unsigned int re8670_skb_gso_transport_seglen(const struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	unsigned int thlen = 0;

	if (skb->encapsulation) {
		thlen = skb_inner_transport_header(skb) -
			skb_transport_header(skb);

		if (likely(shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6)))
			thlen += inner_tcp_hdrlen(skb);
	} else if (likely(shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))) {
		thlen = tcp_hdrlen(skb);
	} else if (unlikely(skb_is_gso_sctp(skb))) {
		thlen = sizeof(struct sctphdr);
	} else if (shinfo->gso_type & SKB_GSO_UDP_L4) {
		thlen = sizeof(struct udphdr);
	}
	/* UFO sets gso_size to the size of the fragmentation
	 * payload, i.e. the size of the L4 (UDP) header is already
	 * accounted for.
	 */
	return thlen + shinfo->gso_size;
}

static unsigned int re8670_skb_gso_network_seglen(const struct sk_buff *skb)
{
	unsigned int hdr_len = skb_transport_header(skb) -
			       skb_network_header(skb);
	return hdr_len + re8670_skb_gso_transport_seglen(skb);
}

static int re8670_get_mtu(struct sk_buff *skb, struct re_private *cp)
{
	int mtu;

	if (skb->protocol == htons(ETH_P_IPV6))
		return 0;

	if(skb_is_gso(skb))
	{
		if (re8670_skb_gso_network_seglen(skb) && re8670_skb_gso_network_seglen(skb) <= skb->dev->mtu)
			mtu = re8670_skb_gso_network_seglen(skb);
		else
			mtu = skb->dev->mtu;

		if (mtu > RE8670_MAX_ETH_FRAME_SIZE)
			mtu = RE8670_MAX_ETH_FRAME_SIZE;

		TX_TRACE(cp->gmac, cp->debug_enable, cp->debug_times, "mtu=%d\n", mtu);
		return mtu;
	}
	else if (skb_shinfo(skb)->nr_frags)
	{
		mtu = skb->dev->mtu;

		if (mtu > RE8670_MAX_ETH_FRAME_SIZE)
			mtu = RE8670_MAX_ETH_FRAME_SIZE;

		TX_TRACE(cp->gmac, cp->debug_enable, cp->debug_times, "mtu=%d\n", mtu);
		return mtu;
	}
	else
		return 0;
}
#endif

static int re8670_set_features(struct net_device *dev, netdev_features_t features)
{
	printk(KERN_CONT "%s %d: enter\n", __func__, __LINE__);
	return 0;
}

#ifdef TX_INTR_HANDLE
static inline u32 read_imr0(struct re_private *cp){
	return RLE0787_R32(cp->gmac, IMR0);
}

static inline u32 read_imr1(struct re_private *cp){
	return RLE0787_R32(cp->gmac, IMR1);
}

static inline u32 read_isr_status(struct re_private *cp){
	u32 gmac = cp->gmac;
	u32 isr_status = (RLE0787_R16(gmac, ISR) & RX_ALL(gmac));
	u32 isr1_status = retrieve_isr1_status(gmac);

	/*rx mring split int*/
	assigne_cpisr_status(cp->isr_status, isr_status);
	assigne_cpisr1_status(cp->isr1_status, (isr1_status & cp->rx_multiring_bitmap));
	gather_rx_isr(isr_status, cp->isr1_status);
	assigne_cpisr1_status(cp->isr1_status_tx, (isr1_status & ((cp->tx_multiring_bitmap<<16)|(cp->tx_multiring_bitmap<<24))));
	///--cp->isr1_status_tx&=read_imr0(cp);
	gather_tx_isr(isr_status, cp->isr1_status_tx);
	return isr_status;
}
#else
static inline u16 read_isr_status(struct re_private *cp){
	u32 gmac = cp->gmac;
	u16 isr_status = (RLE0787_R16(gmac, ISR) & RX_ALL(gmac));
	u32 ring_mask;

	/*rx mring split int*/
	assigne_cpisr_status(cp->isr_status, isr_status);
	ring_mask = retrieve_isr1_status(gmac) & cp->rx_multiring_bitmap;
	assigne_cpisr1_status(cp->isr1_status, ring_mask);
	gather_rx_isr(isr_status, cp->isr1_status);
	return isr_status;
}
#endif

static inline void mask_rx_int(unsigned int gmac)
{
	RLE0787_R16(gmac, IMR)&=(u16)(~RX_ALL(gmac));
	MASK_IMR0_RXALL(gmac);
}

static inline void unmask_rx_int(unsigned int gmac)
{
	RLE0787_R16(gmac, IMR)|=(u16)(RX_ALL(gmac));//we still open imr when rx_work==0 for a quickly schedule
	UNMASK_IMR0_RXALL(gmac);
}

#ifdef TX_INTR_HANDLE
static inline void mask_tx_int(unsigned int gmac)
{
	RLE0787_R16(gmac, IMR)&=(u16)(~(IMR_TOK_TI|IMR_TDU_EN));
	MASK_IMR0_TXALL(gmac);
}

static inline void mask_tx_tok_int(unsigned int gmac, unsigned int tx_ring_bitmap)
{
	RLE0787_R16(gmac, IMR)&=(u16)(~IMR_TOK_TI);
	MASK_IMR0_TOK(gmac, tx_ring_bitmap);
}

static inline void mask_tx_tdu_int(unsigned int gmac, unsigned int tx_ring_bitmap)
{
	RLE0787_R16(gmac, IMR)&=(u16)(~IMR_TDU_EN);
	MASK_IMR0_TDU(gmac, tx_ring_bitmap);
}

static inline void unmask_tx_tok_int(unsigned int gmac, unsigned int tx_ring_bitmap)
{
	RLE0787_R16(gmac, IMR)|=(u16)(IMR_TOK_TI);
	UNMASK_IMR0_TOK(gmac, tx_ring_bitmap);
}

static inline void clear_tx_tok_int(unsigned int gmac, unsigned int tx_ring_bitmap)
{
	CLEAR_ISR1_TOK(gmac, tx_ring_bitmap);
}

static inline void unmask_tx_tdu_int(unsigned int gmac, unsigned int tx_ring_bitmap)
{
	RLE0787_R16(gmac, IMR)|=(u16)(IMR_TDU_EN);
	UNMASK_IMR0_TDU(gmac, tx_ring_bitmap);
}

static inline void clear_tx_tdu_int(unsigned int gmac, unsigned int tx_ring_bitmap)
{
	CLEAR_ISR1_TDU(gmac, tx_ring_bitmap);
}
#endif

static inline void clear_isr(struct re_private *cp, u32 status)
{
	unsigned int gmac = cp->gmac;
	
	RLE0787_W16(gmac, ISR, (u16)status);
#ifdef RX_MRING_INT_SPLIT
	CLEAR_ISR1(gmac, (cp->isr1_status&~(cp->rx_multiring_bitmap)));
#else
	CLEAR_ISR1(gmac, cp->isr1_status);
#endif
}

#include <linux/vmstat.h>
extern int min_free_kbytes;
//#define MIN_LIMIT_PAGES (20*1024) //20M
//static unsigned long min_limit_pages = MIN_LIMIT_PAGES;
// default set 0, let userspace to adjust it
static unsigned long min_limit_pages = 0;
module_param(min_limit_pages, ulong, 0644);
static unsigned int mem_usage_status; /*0: normal, 1: no memory*/

#define K(x) ((x) << (PAGE_SHIFT - 10))
static int check_memory_avaliable(int size)
{
	unsigned long free_pages, limit_pages;
	//printk(KERN_CONT "===> totalram %lu, freeram %lu, totalhigh %lu, freehigh %lu\n",
	//		val.totalram, val.freeram, val.totalhigh, val.freehigh);
	limit_pages = (min_free_kbytes > min_limit_pages) ? min_free_kbytes : min_limit_pages; 
	limit_pages += (size/512);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	free_pages = global_zone_page_state(NR_FREE_PAGES);
#else
	free_pages = global_page_state(NR_FREE_PAGES);
#endif
#ifdef CONFIG_HIGHMEM
	free_pages = free_pages - nr_free_highpages();
#endif
	free_pages = K(free_pages);
	
	if (free_pages > limit_pages) {
		if (mem_usage_status == 1) {
			mem_usage_status = 0;
			queue_broadcast(MSG_TYPE_NIC_EVENT, NIC_EVENT_TYPE_MEM_RECOVERD, 0, ENABLED);
		}
		return 1;
	}

	if (mem_usage_status == 0) {
		mem_usage_status = 1;
		queue_broadcast(MSG_TYPE_NIC_EVENT, NIC_EVENT_TYPE_NO_MEM, 0, ENABLED);
	}

	return 0;
}

__IRAM_NIC
struct sk_buff *re8670_getAlloc(unsigned int size)
{	
	struct re_private_root *root_cp = &re_private_data_root;
	struct sk_buff *skb=NULL;
	unsigned long free_page;
	
#if defined(CONFIG_RTL865X_ETH_PRIV_SKB) || defined(CONFIG_RTL_ETH_RECYCLED_SKB)
#if defined(CONFIG_RTL_ETH_RECYCLED_SKB)
	skb = dev_alloc_skb_recy_eth(size, root_cp->recycle_skb_pool_id);
#else
	skb = dev_alloc_skb_priv_eth(size);
#endif

	if (skb == NULL && !root_cp->skb_dynamic_allocate_disable)
	{
		if (check_memory_avaliable(size))
		{
#if defined(CONFIG_RTL865X_ETH_PRIV_SKB)
			dynamic_alloc_skb_num++;
#endif
			skb = dev_alloc_skb(size);
			if(skb && (unsigned int)skb->data&0xf) {
				skb->data = (((unsigned int)skb->data)&0xfffffff0)+0x10;
			}
		}
#if 0
		else
		{
			printk_ratelimited(KERN_CONT "[%s %d] %lu, %lu\n", __func__, __LINE__, K(free_page), min_free_kbytes);
		}
#endif
	}
#else
	if (check_memory_avaliable(size))
	{
		skb = dev_alloc_skb(size);
		if(skb && (unsigned int)skb->data&0xf) {
			skb->data = (((unsigned int)skb->data)&0xfffffff0)+0x10;
		}
	}
#if 0
	else
	{
		printk_ratelimited(KERN_CONT "[%s %d] %lu, %lu\n", __func__, __LINE__, K(free_page), min_free_kbytes);
	}
#endif
#endif

	return skb;
}

__IRAM_NIC
struct sk_buff *re8670_getMcAlloc(unsigned int size)
{	
	return re8670_getAlloc(size);
}
	
__IRAM_NIC
struct sk_buff *re8670_getBcAlloc(unsigned int size)
		{
	return re8670_getAlloc(size);
	}

__IRAM_NIC
struct sk_buff *re8670_getCriticalAlloc(unsigned int size)
{
	struct sk_buff *skb=NULL;

#if defined(CONFIG_RTL865X_ETH_PRIV_SKB)
	skb = dev_alloc_critical_skb_priv_eth(size);
#else
#ifdef CONFIG_RTW_MEMPOOL
	skb = rtw_mem_pool_alloc_skb(size);
#else
	skb = dev_alloc_skb(size);
#endif
#endif

	return skb;
}

static inline void rtk_gmac_set_rxbufsize (struct re_private_root *root_cp)
{
	unsigned int mtu = root_cp->dev->mtu;
	struct re_private *cp;	
	unsigned int gmac;

	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		cp = root_cp->re_private_data_ptr[gmac];

		if(cp->gmac_enabled != GMAC_TRUE)
			continue;
	
		if (mtu > ETH_DATA_LEN)
			/* MTU + ethernet header + FCS + optional VLAN tag */
			cp->rx_buf_sz = mtu + ETH_HLEN + 8;
		else
			cp->rx_buf_sz = cp->rx_buff_size;
	}
}


int re8686_register_txfunc(tfunc_t pfunc){
	struct re_private_root *root_cp = &re_private_data_root;

	root_cp->txfunc = pfunc;
	
	return 0;
}

atomic_t lock_tx_tail = ATOMIC_INIT(0);

int re8686_register_rxfunc_by_port(unsigned int gmac, unsigned int port, p2rfunc_t pfunc)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];	

	if(port >= SW_PORT_NUM || !pfunc)
		return -EINVAL;
	cp->port2rxfunc[port] = pfunc;
	return 0;
}

int re8686_register_rxfunc_all_port(p2rfunc_t pfunc)
{
	unsigned int gmac;
	unsigned int i;
	int ret;
	
	if(!pfunc)
		return -EINVAL;

	for(gmac=0;gmac<MAX_GMAC_NUM;gmac++) {
		for(i=0;i<SW_PORT_NUM;i++) {
			ret = re8686_register_rxfunc_by_port(gmac, i, pfunc);
			if(ret){
				return ret;
			}
		}
	}

	return 0;
}

void re8686_reset_rxfunc_to_default(unsigned int gmac)
{
	unsigned int i;
	for(i=0;i<SW_PORT_NUM;i++){
		re8686_register_rxfunc_by_port(gmac, i, re8670_rx_skb);
	}
}

static inline void _tx_additional_setting(struct sk_buff *skb, struct net_device *dev, struct tx_info *pTxInfo)
{
	pTxInfo->opts2.bit.cputag = 1;

#if defined(CONFIG_SFU)
	//the txPmask and VLAN information should be decided in fwdEngine_xmit and recorded in txInfo,
	//so we do not need these check code here
#else
	if(pTxInfo->opts2.bit.tx_portmask == 0){
		pTxInfo->opts2.bit.tx_portmask = DEVPRIV(dev)->txPortMask;
	}
	
	//luke:20130411, patch for protocol stack broadcast hardware lookup packet to LAN will fail 
	//this patch should be remove when fwdEngine Tx module is ready.
	if((pTxInfo->opts2.bit.tx_portmask == 0) && ((skb->data[0]&1)==1))
	{

			//luke:20130506, patch for broadcast packet from WAN port will return to WAN port, cause spanning tree protocol treats it as loop
			if(skb->from_dev && (skb->from_dev->rtk_priv_flags & (RTK_IFF_DOMAIN_ELAN|RTK_IFF_DOMAIN_WAN)))
			{
				//printk(KERN_CONT "skb is from %s %x, did not tx to wan port\n",skb->from_dev->name,DEVPRIV(skb->from_dev)->txPortMask);
			}
			else
			{
				//printk("broadcast!! the dev name is %s, txportmask is %x\n",dev->name,DEVPRIV(dev)->txPortMask);
				//force do DirectTx to all port except CPU port
				pTxInfo->opts2.bit.tx_portmask=0x2f;
			}
			//printk(KERN_CONT "the txpmsk is %x\n",pTxInfo->opts3.bit.tx_portmask);

	}
#endif

	if(skb->from_dev){
		if(skb->from_dev->rtk_priv_flags & (RTK_IFF_DOMAIN_ELAN|RTK_IFF_DOMAIN_WAN)){
			pTxInfo->opts3.bit.dislrn = 1; //disable cpu port learning
		}
	}
#if defined(CONFIG_RTK_L34_ENABLE) && defined(CONFIG_RG_API_RLE0371)
	//tysu patch for 0371
	pTxInfo->opts2.bit.tx_portmask=0;
#endif
	
	//20130724: If NIC TX send by HWLOOKUP, L34 Keep will cause un-except problem.
	if(pTxInfo->opts2.bit.tx_portmask!=0) pTxInfo->opts3.bit.l34_keep = 1;	//ensure switch won't modify or filter packet
	//20141104LUKE: when L34Keep is on, Keep is also needed for gpon.
	if(pTxInfo->opts3.bit.l34_keep)pTxInfo->opts3.bit.keep = 1;

	//20131028: If ipv4 packet length <=60, then recaculate data_length in txInfo. Otherwise GMAC will cause L4chksum error
	//20131029: OMCI packet may cause data_length too big, so check streamID to filter them!!
	if(skb->len==60
#ifdef CONFIG_GPON_FEATURE
		&& pTxInfo->opts3.bit.tx_dst_stream_id!=0x40
#endif
		)
	{
		u16 off=12;
		u32 xlen=0;
	
		if((*(u16*)(skb->data+off))==htons(0x8100))//CTAG
	        off+=4;
		if(((*(u16*)(skb->data+off))==htons(0x8863))||((*(u16*)(skb->data+off))==htons(0x8864)))//PPPoE
			off+=8;
		if(((*(u16*)(skb->data+off))==htons(0x0800))||((*(u16*)(skb->data+off))==htons(0x0021)))//IPv4 or IPv4 with PPPoE
		{
			xlen=(off+2+((skb->data[off+4]<<8)|skb->data[off+5]))&0x1ffff;	//l3offset + IPv4 total length
			if(xlen<=60)
				pTxInfo->opts1.bit.data_length=xlen;
		}
			
	}
}

static 
__IRAM_NIC
void	tx_additional_setting(struct sk_buff *skb, struct net_device *dev, struct tx_info *pTxInfo)
{
#ifndef RE8686_VERIFY
	_tx_additional_setting(skb, dev, pTxInfo);
#else
	tx_additional_setting_verify_version(skb, dev, pTxInfo);
#endif
}

//#define RE8686_VERIFY
#ifdef RE8686_VERIFY
#include "./re8686_verify.c"
#else
#define INVERIFYMODE 0
#endif

struct net_device* nic_decide_rx_device_by_spa(unsigned int phy_src_port)
{
	unsigned int num = (phy_src_port >= SW_PORT_NUM) ? (0) : phy_src_port ;
	struct re_private *cp = ROOTPRIV2PRIV(0);

	return cp->port2dev[num];
}

__IRAM_NIC
#if !defined(CONFIG_RG_WMUX_SUPPORT) && !defined(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE)
static
#endif
struct net_device* decideRxDevice(struct re_private *cp, struct rx_info *pRxInfo)
{
	unsigned int num = (pRxInfo->opts3.bit.src_port_num >= SW_PORT_NUM) ? 
		(0) : pRxInfo->opts3.bit.src_port_num ;

	return cp->port2dev[num];
}

#if defined(CONFIG_RG_WMUX_SUPPORT) || defined(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE)
#if !IS_BUILTIN(CONFIG_RTK_L34_FC_KERNEL_MODULE)

EXPORT_SYMBOL(decideRxDevice);
EXPORT_SYMBOL(dynamic_sram_desc);
#endif

#endif

static inline void updateRxStatic(struct re_private *cp, struct sk_buff *skb)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,10,0)
	skb->dev->last_rx = jiffies;
#endif
	cp->cp_stats.rx_sw_num++;
	DEVPRIV(skb->dev)->net_stats.rx_packets++;
	DEVPRIV(skb->dev)->net_stats.rx_bytes += skb->len;
}

__IRAM_NIC
int re8670_rx_skb(struct re_private *cp, struct sk_buff *skb, struct rx_info *pRxInfo)
{
#ifdef RX_NAPI_MODE
	struct net_device *dev;
#endif
	unsigned int gmac = cp->gmac;
	struct net_device *net_device_record = NULL;
	int ret;

#ifdef CONFIG_RTL_ETH_RECYCLED_SKB
	//allocate new skb, swap skb, enqueue old_skb back to queue, continue to protocol stack
	if(skb->recyclable)     skb=recycle_skb_swap(skb);
	if(skb==NULL)return RE8670_RX_STOP_SKBNOFREE;
#endif

	skb->dev = decideRxDevice(cp, pRxInfo);
#ifdef RX_NAPI_MODE
	dev = skb->dev;
#endif
	net_device_record = skb->dev;

	skb->vlan_tci = (pRxInfo->opts2.bit.ctagva) ? VTAG2VLANTCI(pRxInfo->opts2.bit.cvlan_tag) : 0;
	ETHDBG_PRINT(gmac, cp->debug_enable, cp->debug_times, RTL8686_SKB_RX, "This packet comes from %s(vlan %u)\n", skb->dev->name, skb->vlan_tci);

	skb->from_dev=skb->dev;

	/* switch_port is patched for iptables and ebtables rule matching */
	//skb->switch_port = NULL;
#if 0 //remove it, it will influence FC to assign GPON SID to mark 
	skb->mark = (skb->vlan_tci & 0xFFF);
#endif
	//printk(KERN_CONT "%s %d switch_port: %s vlan_tci=0x%x mark=0x%x\n",
	//		__func__, __LINE__, skb->switch_port, skb->vlan_tci, skb->mark);

	skb->protocol = eth_type_trans (skb, skb->dev);
	
	//do we need any wan dev rx hacking here?(before pass to netif_rx)
	
	updateRxStatic(cp, skb);
	SKB_DBG(skb, cp->debug_enable, cp->debug_times, RTL8686_SKB_RX);
#ifdef RX_NAPI_MODE
	if(dev->features & NETIF_F_GRO)
	{
		ret = napi_gro_receive(&cp->napi, skb);
		if(ret == GRO_DROP)
			DEVPRIV(net_device_record)->net_stats.rx_dropped++;
#ifdef RX_NAPI_MODE_DEBUG
		switch(ret)
		{
			case GRO_NORMAL:
				cp->cp_stats.rx_napi_gro_normal++;
				break;
			case GRO_DROP:
				cp->cp_stats.rx_napi_gro_drop++;
				break;
			case GRO_MERGED_FREE:
				cp->cp_stats.rx_napi_gro_merged_free++;
				break;
			case GRO_HELD:
				cp->cp_stats.rx_napi_gro_held++;
				break;
			case GRO_MERGED:
				cp->cp_stats.rx_napi_gro_merged++;
				break;
			default:
				break;
		}
#endif
	}
	else
	{
		if(netif_rx(skb) == NET_RX_DROP)
			DEVPRIV(net_device_record)->net_stats.rx_dropped++;
	}
#else
	if (netif_rx(skb) == NET_RX_DROP)
		DEVPRIV(net_device_record)->net_stats.rx_dropped++;
#endif

	return RE8670_RX_STOP_SKBNOFREE;
}

__IRAM_NIC
static void re8670_rx_software (struct re_private *cp, struct sk_buff *skb, struct rx_info *pRxInfo)
{
	unsigned int num = (pRxInfo->opts3.bit.src_port_num >= SW_PORT_NUM) ? (0) : pRxInfo->opts3.bit.src_port_num ;
	unsigned int gmac = cp->gmac;

#ifdef CONFIG_RTK_CONTROL_PACKET_PROTECTION
	if(cp->rx_save_int_prio_to_skb == GMAC_ON)
	{
		skb->priority = pRxInfo->opts3.bit.internal_priority;
		RX_TRACE(gmac, cp->debug_enable, cp->debug_times, "priority=%d\n", skb->priority);
	}
#ifdef CONFIG_RTL_ETH_RECYCLED_SKB	
	else	
		skb->priority = 0;
#endif
#endif
#if defined(CONFIG_RTL_ETH_RECYCLED_SKB) && (defined(CONFIG_RTL8192CD) || defined(LOCAL_SERVICE_ACCELERATE))	
	//clear cb[0] & cb[1] here, for skip fc
	*(unsigned int *)(skb->cb)=0x0;
#endif
#ifdef CONFIG_RTL8192CD
	if(unlikely(cp->wifi_tx_qos_enable == GMAC_ON))
	{
		skb->cb[0] = cp->wifi_tx_qos_mapping[pRxInfo->opts3.bit.internal_priority];
	}
#endif
#ifdef LOCAL_SERVICE_ACCELERATE
	if(cp->local_service_acc_enable && pRxInfo->opts3.bit.internal_priority == GMAC_PRIORITY_LOCAL_SERVICE_ACC)
	{
		skb->cb[1] = 1;
	}
#endif

	RX_TRACE(gmac, cp->debug_enable, cp->debug_times, "port2rxfunc=0x%p, internal_priority=%d, skb->cb[0]=%d, skb->cb[1]=%d\n", cp->port2rxfunc[num], pRxInfo->opts3.bit.internal_priority, skb->cb[0], skb->cb[1]);
	cp->port2rxfunc[num](cp, skb, pRxInfo);
}


static 
__IRAM_NIC
unsigned int re8670_rx_csum_ok (struct rx_info *rxInfo)
{
#if 0
	unsigned int protocol = rxInfo->opts1.bit.pkttype;

	switch(protocol){
		case RxProtoTCP:
		case RxProtoUDP:
		case RxProtoICMP:
		case RxProtoIGMP:/*we check both l4cs and ipv4cs when protocol is ipv4 l4*/
			if(likely((!rxInfo->opts1.bit.l4csf) && (!rxInfo->opts1.bit.ipv4csf))){
				return 1;
			}
			break;
		case RxProtoTCPv6:
		case RxProtoUDPv6:
		case RxProtoICMPv6:/*when protocol is ipv6, we only check l4cs*/
			if(likely(!rxInfo->opts1.bit.l4csf)){
				return 1;
			}
			break;
		default:/*no guarantee when the protocol is only ipv4*/
			break;
	}
#endif
	return 0;
}
unsigned char eth_close[MAX_GMAC_NUM] = {1,1,1};

static inline void retriveRxInfo(DMA_RX_DESC *desc, struct rx_info *pRxInfo){
	pRxInfo->opts1.dw = desc->opts1;
	pRxInfo->addr= desc->addr;
	pRxInfo->opts2.dw = desc->opts2;
	pRxInfo->opts3.dw = desc->opts3;
}

static inline void updateGmacFlowControl(unsigned int gmac,unsigned rx_tail,int ring_num)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];	
	unsigned int new_cpu_desc_num;		

	if(cp->re8670_rx_flow_control_status[ring_num] != GMAC_TRUE)
		return;
	
	if(ring_num==0)
	{    
		new_cpu_desc_num = RLE0787_R32(gmac, EthrntRxCPU_Des_Num);
		new_cpu_desc_num &= 0x00FFFF0F; // clear
		new_cpu_desc_num |= (((rx_tail&0xFF)<<24)|(((rx_tail>>8)&0xF)<<4)); // update
		RLE0787_R32(gmac, EthrntRxCPU_Des_Num) = new_cpu_desc_num;
	}
	else
	{
		new_cpu_desc_num = RLE0787_R32(gmac, EthrntRxCPU_Des_Num2+ADDR_OFFSET*(ring_num-1));
		new_cpu_desc_num &= 0xfffff000; // clear
		new_cpu_desc_num |= (((rx_tail&0xFF)|(((rx_tail>>8)&0xF)<<4))); // update
		RLE0787_W32(gmac, EthrntRxCPU_Des_Num2+ADDR_OFFSET*(ring_num-1), new_cpu_desc_num);
	}
}

void vlan_detag(unsigned int gmac, int onoff){
	switch(onoff){
		case GMAC_ON:
			RLE0787_W8(gmac, CMD, RLE0787_R8(gmac, CMD)|RxVLAN_Detag);
			break;
		case GMAC_OFF:
			RLE0787_W8(gmac, CMD, RLE0787_R8(gmac, CMD)&~RxVLAN_Detag);
			break;
		default:
			printk(KERN_CONT "%s %d: wrong arg %d\n", __func__, __LINE__, onoff);
			break;
	}
}

void SetTxRingPrioInRR(unsigned int gmac)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];	
	
	cp->iocmd1_reg |= TX_RR_scheduler;
	RLE0787_W32(gmac, IO_CMD1, cp->iocmd1_reg);
}

static inline unsigned char getRxRingBitMap(struct re_private *cp)
{
	unsigned char result = (unsigned char)((cp->isr1_status)|(RX_RDU_CHECK(cp->isr_status))|((cp->isr_status&SW_INT)?cp->rx_multiring_bitmap:0));
		
	cp->isr_status=0;	
	cp->isr1_status=0;
	return result;
}

#ifdef CONFIG_RTL8686_SWITCH
void re8686_set_pauseBySw(unsigned char enable)
{
	re_private_data_root.rx_pause_by_software_enable = (enable) ? GMAC_TRUE : GMAC_FALSE;
	return;
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
static void setSoftwareInterrupt(unsigned long data)
#else
static void setSoftwareInterrupt(struct timer_list *t)
#endif
{
#if defined (CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE)
	struct softnet_data *sd;
	unsigned long flag;
	unsigned int qlen;

	sd = &per_cpu(softnet_data, smp_processor_id());
	local_irq_save(flag);
	spin_lock(&sd->input_pkt_queue.lock);

	qlen = skb_queue_len(&sd->input_pkt_queue);
#endif
	//linux input queue is empty and skb is enough=>trigger NIC to receive packet 
	if (
#ifdef CONFIG_RTL865X_ETH_PRIV_SKB
		(eth_skb_alloc_num < RE8670_MAX_ALLOC_RXSKB_NUM)
#else
		GMAC_TRUE
#endif
#if defined (CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE)		
		&&(qlen <= net_hotdata.max_backlog)
#endif		
		)
	{
		//Gmac 0
		if(re_private_data_root.rx_pause_by_software_bitmap & (1<<0))
			RLE0787_W32(0, SWINT_REG, 1<<24);
		//Gmac 1
		if(re_private_data_root.rx_pause_by_software_bitmap & (1<<1))
			RLE0787_W32(1, SWINT_REG, 1<<24);
	}	
	else
	{
		mod_timer(&re_private_data_root.rx_pause_by_software_interrupt_timer, jiffies+1);
	}
#if defined (CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE)		
	spin_unlock(&sd->input_pkt_queue.lock);
	local_irq_restore(flag);
#endif	
	return ;
}
#endif

#ifdef CONFIG_RTL865X_ETH_PRIV_SKB_ADV
/*
 * RETURN VAL: 	1 fast forwarding successfully
 *                    	0 should go normal path
 */
static int re8670_ff_enter(struct re_private *cp, struct sk_buff *skb, struct rx_info *pRxInfo)
{
	extern void reinit_skbhdr(struct sk_buff *skb, 
						void (*prealloc_cb)(struct sk_buff *, unsigned));
	extern int rteFastForwarding(struct sk_buff *skb);

	skb->dev = decideRxDevice(cp, pRxInfo);
	if ( !ether_addr_equal(skb->data, skb->dev->dev_addr))
	{
		int dir=DIR_LAN;
		
		if (skb->dev->rtk_priv_flags & RTK_IFF_DOMAIN_WAN) // wan
			dir = DIR_WAN;
		
		if (brgFastForwarding((struct sk_buff *)skb, dir))
		{
			return 1;
		}
	}

	skb_reset_mac_header(skb);
	skb->protocol = ((unsigned short *)(skb->data))[6];
		
	skb_pull(skb, ETH_HLEN);
	skb->dst = NULL;
	
	if (NET_RX_SUCCESS == rteFastForwarding(skb))
	{
		return 1;
	}
	skb_push(skb, ETH_HLEN);

	reinit_skbhdr(skb, rtl865x_free_eth_priv_buf);
	
	return 0;
}
#endif //end of CONFIG_RTL865X_ETH_PRIV_SKB_ADV


#if defined (CONFIG_RTK_VOIP_QOS)
extern int ( *check_voip_channel_loading )( void );
#endif

/*
	we can use this map to decide what sequence we want, that means, queue's priority.
	also, we can use this to decrease some iterations when we split rx interrupt. 
	if u only open rx ring1 and ring6, u can set pri_map = {0,5,1,2,3,4}, and 
	rx_ring_bitmap_current will only has these two bits as well, so we don't need to run "for"
	6 times.
	*/
#ifdef RX_MRING_INT_SPLIT
	unsigned char pri_map[MAX_GMAC_NUM][MAX_RXRING_NUM] = {{5,0,1,2,3,4}, {5,0,1,2,3,4}, {5,0,1,2,3,4}};
	unsigned char higherq[MAX_GMAC_NUM][MAX_RXRING_NUM] = {{0x3e,0x3c,0x38,0x30,0x20,0x00}, {0x3e,0x3c,0x38,0x30,0x20,0x00}, {0x3e,0x3c,0x38,0x30,0x20,0x00}};
#endif
static int loop_wdt=0;

static unsigned int re8670_cpu_own_get(struct re_private *cp, int ring_index)
{
	DMA_RX_DESC *rxd;
	int i, cpuOwn=0;

	for(i=0;i<cp->re8670_rx_ring_size[ring_index];i++)
	{
		rxd = (DMA_RX_DESC *)((u32)&cp->rx_Mring[ring_index][i]|0xa0000000);
		if ((rxd->opts1&0x80000000) == 0)
			cpuOwn++;
	}

	return cpuOwn;
}

__IRAM_NIC
#ifdef RX_NAPI_MODE
static unsigned int re8670_rx(struct re_private *cp)
#else
static void re8670_rx(struct re_private *cp)
#endif
{
	unsigned int gmac = cp->gmac, rx_ring_size;
	int rx_work;
	int ring_num=0;
	unsigned rx_Mtail;
	unsigned long expiry = jiffies + (2 * HZ);

#ifdef RX_NAPI_MODE
	unsigned int rx_packets_cnt = 0;
#endif

#if defined (CONFIG_RTK_VOIP_QOS)
	unsigned long start_time = jiffies;
	int pkt_rcv_cnt = 0;
#endif

#ifdef RX_MRING_INT_SPLIT
	static unsigned char rx_ring_backup = 0;
	unsigned char rx_ring_bitmap_current;
	int i = 0, max_rx_ring_num;
#endif	
#ifdef HWNAT_CUSTOMIZE	
		u32 len;	
		struct sk_buff *skb, *new_skb;	
#ifdef CONFIG_RG_JUMBO_FRAME	
		struct sk_buff *orig_skb;	
#endif	
		DMA_RX_DESC *desc;	
		unsigned buflen;	
		struct rx_info rxInfo;	
		int rx_previousTail;	
		unsigned customized_buflen=cp->rx_buf_sz;	
		//u32 customized_opts1,customized_opts2,customized_opts3;	
		u32 customized_opts1, customized_opts3;	
#endif
	u32 skb_buf_size = 0;
	
	GMAC_SPIN_LOCK(&cp->rx_lock);

	//protect eth rx while reboot
	if(unlikely(cp->eth_close == GMAC_TRUE)){
#ifndef RX_NAPI_MODE
		unmask_rx_int(gmac);
#endif
		GMAC_SPIN_UNLOCK(&cp->rx_lock);
#ifdef RX_NAPI_MODE
		return rx_packets_cnt;
#else
		return;
#endif
	}
	
#ifdef RX_MRING_INT_SPLIT
	rx_ring_bitmap_current=getRxRingBitMap(cp);	
	rx_ring_bitmap_current|=rx_ring_backup;
	rx_ring_backup=0;
	max_rx_ring_num = cp->rx_not_only_ring1?MAX_RXRING_NUM:1;
	for(i=0 ; rx_ring_bitmap_current && i<max_rx_ring_num ; i++){
		if(cp->rx_not_only_ring1) {
			ring_num = pri_map[gmac][i];
			if(NOT_IN_BITMAP(rx_ring_bitmap_current, ring_num)){
				continue;
			}
			rx_ring_bitmap_current&=~(1<<ring_num);
			if(RX_HQBUFFS_EMPTY(cp, ring_num))
			{
				CLEAR_ISR1(gmac, (1<<ring_num));
			}
		}
#endif		
		rx_Mtail = cp->rx_Mtail[ring_num];	
		rx_ring_size = cp->re8670_rx_ring_size[ring_num];

#if defined(HWNAT_CUSTOMIZE) && defined(CONFIG_RTL9607C_SERIES)	
		if(dynamic_sram_desc !=0)
			rx_work = 4096;	
		else
#endif
		{
			rx_work = cp->re8670_rx_ring_size[ring_num];
		}
#if defined(CONFIG_RTK_VOIP_QOS)
		start_time = jiffies;
		pkt_rcv_cnt = 0;
#endif

		while (rx_work--)
		{	
#ifndef HWNAT_CUSTOMIZE
			u32 len;
			struct sk_buff *skb, *new_skb;
#ifdef CONFIG_RG_JUMBO_FRAME
			struct sk_buff *orig_skb;
#endif
			DMA_RX_DESC *desc;
			unsigned buflen;
			struct rx_info rxInfo;	
#else	
			if(re8686_rx_ring_data_buffer[gmac][ring_num]){	
				customized_opts1 = cp->rx_Mring[ring_num][rx_Mtail].opts1;	
				//customized_opts2 = cp->rx_Mring[ring_num][rx_Mtail].opts2;	
					
				//retriveRxInfo(&cp->rx_Mring[ring_num][rx_Mtail], &rxInfo);	
				//RXINFO_DBG(gmac, &rxInfo, ring_num);if(debug_enable[gmac]&RTL8686_SKB_RX)memDump(cp->rx_Mring[ring_num][rx_Mtail].addr,80,"rx packet");	
				
				if (unlikely(customized_opts1&DescOwn))	
				{					
					cp->cp_stats.rx_customized_rx_owned++;	
					break;	
				}	
				len = (customized_opts1 & 0x0fff) - 4;		//minus CRC 4 bytes	
				/*if (unlikely(customized_opts1&RCDF)){		//DMA error	
					cp->cp_stats.rcdf++;	
					goto BYPASS_TX;	
				}	
				if (unlikely(customized_opts1&CRCErr)){ 	//CRC error	
					cp->cp_stats.crcerr++;	
					goto BYPASS_TX;	
				}	
				if (unlikely((customized_opts1 & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag))){	
					cp->cp_stats.frag++;	
					goto BYPASS_TX;	
				}	
				if ((customized_opts2&CPU_REASON_FWD)||(customized_opts2&CPU_REASON_ACL_TRAP))*/	
				{	
					if(dynamic_sram_desc>0){	
						//do the tx function	
			
						len+=re8686_rx_ring_customized_preLen[gmac][ring_num];	
							
						if(re8686_tx_ring_hdr_buffer[gmac][ring_num])	
							re8686_customized_dualTx(cp, len, re8686_rx_ring_customized_tx_ringNum[gmac][ring_num]);	
						else	
							re8686_customized_quickTx(cp, len, re8686_rx_ring_customized_tx_ringNum[gmac][ring_num]);	
					}else{	
#if 1 //Wen: For sync VXLAN/NPTv6 fastforward issue, sync from luna_pro_cmcc_cu: [JIM][revision 39486]fix lock wait problem for nic acc data path.	
						if(re8686_customized_rx_and_tx_used[gmac])	
						{	
							// Forcibly break if re8686_customized_rx_and_tx is used	
							rx_work = 0;	
						}	
#endif	
						desc = &cp->rx_Mring[ring_num][rx_Mtail];	
						customized_opts3 = *((unsigned int *)(desc)+3);	
						//check type by extport mask	
						if(customized_opts3&re8686_rx_ring_ext_pmsk[gmac][ring_num][0]){	
								
							//call rx hook func, if have	
							if(re8686_rx_ring_customized_func[gmac][ring_num][0])	
								len=(re8686_rx_ring_customized_func[gmac][ring_num][0])(cp, (struct rx_info *)desc, len);	
								
							re8686_customized_tx(cp, len, re8686_rx_ring_customized_tx_ringNum[gmac][ring_num], &re8686_tx_ring_txInfo[gmac][ring_num][0], re8686_tx_ring_addr_offset[gmac][ring_num][0]);	
						}else if(customized_opts3&re8686_rx_ring_ext_pmsk[gmac][ring_num][1]){	
							//call rx hook func, if have	
								
							if(re8686_rx_ring_customized_func[gmac][ring_num][1])	
								len=(re8686_rx_ring_customized_func[gmac][ring_num][1])(cp, (struct rx_info *)desc, len);	
								
							re8686_customized_tx(cp, len, re8686_rx_ring_customized_tx_ringNum[gmac][ring_num], &re8686_tx_ring_txInfo[gmac][ring_num][1], re8686_tx_ring_addr_offset[gmac][ring_num][1]);	
						}else if(customized_opts3&re8686_rx_ring_ext_pmsk[gmac][ring_num][2]){	
							//call rx hook func, if have	
								
							if(re8686_rx_ring_customized_func[gmac][ring_num][2])	
								len=(re8686_rx_ring_customized_func[gmac][ring_num][2])(cp, (struct rx_info *)desc, len);		
								
							re8686_customized_tx(cp, len, re8686_rx_ring_customized_tx_ringNum[gmac][ring_num], &re8686_tx_ring_txInfo[gmac][ring_num][2], re8686_tx_ring_addr_offset[gmac][ring_num][2]);	
						}else{	
							//call rx hook func, if have	
								
							if(re8686_rx_ring_customized_func[gmac][ring_num][3])	
								len=(re8686_rx_ring_customized_func[gmac][ring_num][3])(cp, (struct rx_info *)desc, len);	
								
							re8686_customized_tx(cp, len, re8686_rx_ring_customized_tx_ringNum[gmac][ring_num], &re8686_tx_ring_txInfo[gmac][ring_num][3], re8686_tx_ring_addr_offset[gmac][ring_num][3]);	
						}	
							
					}	
BYPASS_TX:	
					rx_previousTail=re8686_rx_ring_previousDesc[gmac][ring_num][rx_Mtail%CHECK_TX_OWN_BIT_INTERVAL_NUM];	
					if(likely(rx_previousTail>=0))	
					{	
						if(hwnat_customized_check_tx_done && re8686_rx_descIdx_customized_tx_descAddr[gmac][ring_num] && re8686_rx_descIdx_customized_tx_descAddr[gmac][ring_num][rx_previousTail].fs_descAddr)	
						{	
							while(unlikely(re8686_rx_descIdx_customized_tx_descAddr[gmac][ring_num][rx_previousTail].fs_descAddr->opts1&DescOwn))	
							{	
								cp->cp_stats.rx_customized_tx_own_waiting_times++;	
							}	
						}	
						if(!dynamic_sram_desc) //DRAM mode	
							cp->tx_Mhqring[re8686_rx_ring_customized_tx_ringNum[gmac][ring_num]][rx_previousTail].addr=(u32)(re8686_rx_ring_data_buffer[gmac][ring_num]+rx_previousTail*cp->rx_buff_size) | UNCACHE_MASK;	
						cp->rx_Mring[ring_num][rx_previousTail].opts1 = (DescOwn | customized_buflen) | ((rx_previousTail == (rx_ring_size - 1))?RingEnd:0);	
					}	
					re8686_rx_ring_previousDesc[gmac][ring_num][rx_Mtail%CHECK_TX_OWN_BIT_INTERVAL_NUM]=rx_Mtail;	
					cp->cp_stats.rx_customized_forward++;	
					rx_Mtail = NEXT_RX(rx_Mtail, rx_ring_size);	
					continue;	
				}	
				//copy to original skb 	
				//memcpy(cp->rx_skb[ring_num][rx_Mtail].skb->data+(customized_opts1 & FirstFrag?RX_OFFSET:0), cp->rx_Mring[ring_num][rx_Mtail].addr, customized_opts1 & 0x0fff);	
				//cp->cp_stats.rx_customized_copied++;	
			}	
#endif	
#if 1 // Wen: For sync VXLAN/NPTv6 fastforward issue, sync from luna_pro_cmcc_cu: [LUKE][revision 39160]: fix normal path do not kick watchdog problem.	
			loop_wdt++;	
			if (unlikely(loop_wdt > 1024 )) {	
#ifdef CONFIG_LUNA_WDT_KTHREAD	
				extern void luna_watchdog_kick(void);	
				luna_watchdog_kick();	
				loop_wdt = 0;	
#endif	
			}	
			if (unlikely(time_is_before_jiffies(expiry))) {	
				printk_once(KERN_CONT "nic-rx: rx time exceed %lu/%lu\n",expiry, jiffies);
				break;	
			}	
#endif
#ifdef CONFIG_RTL8686_SWITCH
			if(re_private_data_root.rx_pause_by_software_enable)
			{
#if defined (CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE)			
				struct softnet_data *sd;
				unsigned long flag;
				unsigned int qlen;
				//The packet will be dropped by Linux PS when it is sent to PS and PS input queue is full, so pause receiving packet to SW.
			
				sd = &per_cpu(softnet_data, smp_processor_id());
				local_irq_save(flag);
				spin_lock(&sd->input_pkt_queue.lock);

				qlen = skb_queue_len(&sd->input_pkt_queue);
				
				if (qlen > net_hotdata.max_backlog)
				{
					cp->pauseBySwRingBitmap |= (1<<ring_num);
					re_private_data_root.rx_pause_by_software_bitmap |= (1<<gmac);
					if(!timer_pending(&re_private_data_root.rx_pause_by_software_interrupt_timer))
						mod_timer(&re_private_data_root.rx_pause_by_software_interrupt_timer, jiffies+1);
					spin_unlock(&sd->input_pkt_queue.lock);
					local_irq_restore(flag);
					break;
				}
				spin_unlock(&sd->input_pkt_queue.lock);
				local_irq_restore(flag);
#endif			
				cp->pauseBySwRingBitmap &= ~(1<<ring_num);
				re_private_data_root.rx_pause_by_software_bitmap &= ~(1<<gmac);
			}
#endif
#if defined(CONFIG_RTK_VOIP_QOS)
			if( (pkt_rcv_cnt++ > 100 || (jiffies - start_time) >= 1)&& check_voip_channel_loading && check_voip_channel_loading() > 0)
			{
				break; 
			}
#endif

#ifdef RX_MRING_INT_SPLIT
			/* if higher queue has packet, quit and restart */
			if (cp->rx_not_only_ring1) {
				if ((retrieve_isr1_status(gmac) & higherq[gmac][ring_num])) {
					rx_ring_backup = (1 << ring_num) | rx_ring_bitmap_current;
					rx_ring_bitmap_current |= rx_ring_backup;
					/* i = MAX_RXRING_NUM; */
					break;
				}
			}
#endif 
			desc = &cp->rx_Mring[ring_num][rx_Mtail];	
			retriveRxInfo(desc, &rxInfo);

			if (rxInfo.opts1.bit.own)
			{
				break;
			}

			mips_perf_measure_entrance(MIPS_PERF_NIC_RX);

            cp->cp_stats.rx_hw_num++;
            skb = cp->rx_skb[ring_num][rx_Mtail].skb;
            if (unlikely(!skb))
            	BUG();

			RXINFO_DBG(cp, &rxInfo, ring_num);
			
			len = rxInfo.opts1.bit.data_length & 0x0fff;		//minus CRC 4 bytes later
	
			if (unlikely(rxInfo.opts1.bit.rcdf)){		//DMA error
#ifdef CONFIG_RG_JUMBO_FRAME
				if(cp->jumboLength>0)		//flush jumbo skb
				{
					if(cp->jumboFrame) 
						dev_kfree_skb_any(cp->jumboFrame);
					cp->jumboLength=0;
					cp->jumboFrame=NULL;
				}
#endif
				cp->cp_stats.rcdf++;
				goto rx_next;
			}
			if (unlikely(rxInfo.opts1.bit.crcerr)){		//CRC error
#ifdef CONFIG_RG_JUMBO_FRAME
				if(cp->jumboLength>0)		//flush jumbo skb
				{
					if(cp->jumboFrame) 
						dev_kfree_skb_any(cp->jumboFrame);
					cp->jumboLength=0;
					cp->jumboFrame=NULL;
				}
#endif
				cp->cp_stats.crcerr++;
				goto rx_next;
			}
			
			skb_buf_size = SKB_BUF_SIZE;

			buflen = cp->rx_buf_sz + RX_OFFSET;
#ifdef CONFIG_RG_JUMBO_FRAME
			if(rxInfo.opts1.bit.fs==1)	//FS=1
			{
				if(rxInfo.opts1.bit.ls==1) ////FS=1, LS=1
				{
					orig_skb=NULL;
					goto rx_to_software;
				}
				else //FS=1, LS=0
				{				
					orig_skb=skb;
					//printk(KERN_CONT "FS=1, LS=0, first frag skb is %d\n",len);
					//memDump(skb->data+RX_OFFSET,64,"skb_start_64", RTL8686_MEM_DUMP_FORMAT_ALL);
					//memDump(skb->data+RX_OFFSET+len-16,16,"skb_last_16", RTL8686_MEM_DUMP_FORMAT_ALL);

					if(cp->jumboLength>0)		//flush jumbo skb
					{
						if(cp->jumboFrame) 
							dev_kfree_skb_any(cp->jumboFrame);
						cp->jumboLength=0;
						cp->jumboFrame=NULL;
					}
					
					//Init global variables, copy the skb into jumboFrame and free the frag one
					cp->jumboFrame = dev_alloc_skb(JUMBO_SKB_BUF_SIZE);
					if (unlikely(!cp->jumboFrame)) {
						printk(KERN_CONT "there is no memory for jumbo frame\n");
						cp->cp_stats.rx_no_mem++;
						dma_cache_wback_inv((unsigned long)skb->data,(u32)(skb->end-skb->data));
						cp->jumboLength=0;
						goto rx_next;
					}
					cp->jumboLength=len;
					
					//Copy skb into jumboFrame
					memcpy(cp->jumboFrame->data+RX_OFFSET,skb->data+RX_OFFSET,len);
					dma_cache_wback_inv((unsigned long)skb->data,(u32)(skb->end-skb->data));
				}
			}
			else 	//FS=0
			{
				orig_skb=skb; //keep
				//if the length is zero, JumboFrame is not allocated...
				if(unlikely(cp->jumboLength==0))
				{
					printk(KERN_CONT "the first skb is not entered or error...\n");
					cp->cp_stats.frag++;
					dma_cache_wback_inv((unsigned long)skb->data,(u32)(skb->end-skb->data));
				}
				else
				{
					if(unlikely(cp->jumboLength+len>=JUMBO_SKB_BUF_SIZE))
					{
						if(cp->jumboFrame) 
							dev_kfree_skb_any(cp->jumboFrame);
						cp->jumboLength=0;
						cp->jumboFrame=NULL;
						cp->cp_stats.toobig++;
						dma_cache_wback_inv((unsigned long)skb->data,(u32)(skb->end-skb->data));
					}
					else
					{
						//Copy into jumboFrame, free the skb						
						memcpy(cp->jumboFrame->data+RX_OFFSET+cp->jumboLength,skb->data,len);
						dma_cache_wback_inv((unsigned long)skb->data,(u32)(skb->end-skb->data));
						cp->jumboLength+=len;
						
						if(rxInfo.opts1.bit.ls==1)		//FS=0, LS=1
						{
							//printk("FS=0, LS=1, last frag skb is %d\n",len);
							//memDump(skb->data,64,"skb_start_64", RTL8686_MEM_DUMP_FORMAT_ALL);
							//memDump(skb->data+len-16,16,"skb_last_16", RTL8686_MEM_DUMP_FORMAT_ALL);
							//dev_kfree_skb_any(skb);							
							skb=cp->jumboFrame;
							len=cp->jumboLength;							
							rxInfo.opts1.bit.data_length=cp->jumboLength & 0x3fff;		//at most 14 bits
							cp->jumboLength=0;
							cp->jumboFrame=NULL;
							rxInfo.opts1.bit.fs=1; //in order to do skb_reserve RX_OFFSET
							goto rx_to_software;
						}
						else
						{
							//printk(KERN_CONT "FS=0, LS=0 frag skb is %d\n",len);
							//memDump(skb->data,64,"skb_start_64", RTL8686_MEM_DUMP_FORMAT_ALL);
							//memDump(skb->data+len-16,16,"skb_last_16", RTL8686_MEM_DUMP_FORMAT_ALL);							
						}
					}
				}
			}
			goto rx_next;
rx_to_software:
#else
			if (unlikely((rxInfo.opts1.dw & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag))) {
				cp->cp_stats.frag++;
				goto rx_next;
			}
#endif

			new_skb = NULL;
#ifdef CONFIG_RG_JUMBO_FRAME
			if(orig_skb == NULL)
#endif
			{
				if(rxInfo.opts3.bit.internal_priority == GMAC_PRIORITY_CRITICAL || 
					(ring_num == MAX_RXRING_NUM-1)) 
				{
					cp->cp_stats.rx_critical_num++;
					new_skb=re8670_getCriticalAlloc(skb_buf_size);
					if (!new_skb) {
						new_skb=re8670_getAlloc(skb_buf_size);
 						if (!new_skb) {
 							cp->cp_stats.rx_critical_drop_num++;
 						}
					}
				} else {
					new_skb=re8670_getAlloc(skb_buf_size);
 				}

				if (unlikely(!new_skb)) {
					cp->cp_stats.rx_no_mem++;
					//printk(KERN_CONT "nic rx new_skb alloc failed no mem %d\n",eth_skb_alloc_num);
					//dma_cache_wback_inv((unsigned long)skb->data,buflen);
					dma_cache_wback_inv((unsigned long)skb->data,(u32)(skb->end-skb->data));
					//goto rx_next;
					//The packet will be dropped when run out of skb, so pause receiving packet to SW.
#ifdef CONFIG_RTL8686_SWITCH
					if(re_private_data_root.rx_pause_by_software_enable)
					{		
						cp->pauseBySwRingBitmap |= (1<<ring_num);
						re_private_data_root.rx_pause_by_software_bitmap |= (1<<gmac);
						if(!timer_pending(&re_private_data_root.rx_pause_by_software_interrupt_timer))
							mod_timer(&re_private_data_root.rx_pause_by_software_interrupt_timer, jiffies+1);
						break;
					}
					else
#endif
					{
						goto rx_next;
					}
				}
			}

#if defined(CONFIG_RTL_ETH_RECYCLED_SKB)
			extern unsigned int switch_off_recycle_skb;
			if(unlikely(switch_off_recycle_skb && skb->recyclable)){
				void recycle_skb_clean(struct sk_buff *skb);
				skb->recyclable=0;
				recycle_skb_clean(skb);
			}
#endif

			/* Handle checksum offloading for incoming packets. */
			if (re8670_rx_csum_ok(&rxInfo))
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			else
				skb->ip_summed = CHECKSUM_NONE;

			if(rxInfo.opts1.bit.fs==1)
			{
				skb_reserve(skb, RX_OFFSET); // HW DMA start at 4N+2 only in FS.
			}
			len-=4;	//minus CRC 4 bytes here			
			skb->len=0;
			skb_put(skb, len);
			
			SKB_DBG(skb, cp->debug_enable, cp->debug_times, RTL8686_SKB_RX);
			RX_TRACE(gmac, cp->debug_enable, cp->debug_times, "SKB[%x] DA=%02x:%02x:%02x:%02x:%02x:%02x SA=%02x:%02x:%02x:%02x:%02x:%02x ethtype=%04x len=%d\n",(u32)skb&0xffff
			,skb->data[0],skb->data[1],skb->data[2],skb->data[3],skb->data[4],skb->data[5]
			,skb->data[6],skb->data[7],skb->data[8],skb->data[9],skb->data[10],skb->data[11]			
			,(skb->data[12]<<8)|skb->data[13],len);
#ifdef HWNAT_CUSTOMIZE	
			if(re8686_rx_ring_data_buffer[gmac][ring_num]==NULL)	
#endif

			if(new_skb)
			{
				cp->rx_Mring[ring_num][rx_Mtail].addr = CPHYSADDR(new_skb->data);
				cp->rx_skb[ring_num][rx_Mtail].skb = new_skb;
				
				dma_cache_inv((unsigned long)new_skb->data,(u32)new_skb->end-(u32)new_skb->data);
				//dma_cache_inv((unsigned long)skb->data,skb->len);
			}
			/*fastpath enter here.*/
#ifndef CONFIG_RTK_L34_ENABLE
	#ifdef CONFIG_RTL865X_ETH_PRIV_SKB_ADV
			if (re8670_ff_enter(cp, skb, &rxInfo))
			{
				goto rx_next;
			}
			//printk(KERN_CONT "go normal path\n");
	#endif
#endif//end of CONFIG_RTK_L34_ENABLE
			re8670_rx_software(cp, skb, &rxInfo);
rx_next:
			desc->opts1 = (DescOwn | skb_buf_size) | ((rx_Mtail == (cp->re8670_rx_ring_size[ring_num] - 1))?RingEnd:0);
			//desc->opts1 = (DescOwn | cp->rx_buf_sz) | ((rx_Mtail == (cp->re8670_rx_ring_size[ring_num] - 1))?RingEnd:0);
			updateGmacFlowControl(gmac, rx_Mtail, ring_num);
			RXDBG_TIMES_UPDATE(cp);
			rx_Mtail = NEXT_RX(rx_Mtail, cp->re8670_rx_ring_size[ring_num]);
#ifdef RX_NAPI_MODE
			if(++rx_packets_cnt >= cp->napi_budget)
			{
				cp->rx_Mtail[ring_num] = rx_Mtail;
				GMAC_SPIN_UNLOCK(&cp->rx_lock);
				return rx_packets_cnt;
			}
#endif
			mips_perf_measure_exit(MIPS_PERF_NIC_RX);
		}
		cp->rx_Mtail[ring_num] = rx_Mtail;

#ifndef RX_NAPI_MODE
		if(rx_work <= 0)
		{
			tasklet_hi_schedule(&cp->rx_tasklets);
		}
#else
#if defined(HWNAT_CUSTOMIZE) && defined(CONFIG_RTL9607C_SERIES)
		if(rx_work <= 0)
		{
			napi_schedule(&cp->napi);
		}
#endif
#endif

#ifdef RX_MRING_INT_SPLIT
	}
#endif
#ifndef RX_NAPI_MODE
	unmask_rx_int(gmac);
#endif
	GMAC_SPIN_UNLOCK(&cp->rx_lock); 
#ifdef RX_NAPI_MODE
	return rx_packets_cnt;
#endif
}

static void __re8670_rx(unsigned long data) {
	struct re_private *cp = (struct re_private *)data;
	re8670_rx(cp);
}

#ifdef RX_NAPI_MODE
#ifdef RX_NAPI_MODE_DEBUG
#define MAX_NAPI_STATISTIC_NUM	2000
unsigned int napi_statistic[MAX_NAPI_STATISTIC_NUM] = {0};
#endif

unsigned int re8670_rx_napi_gmac_index_get(struct napi_struct *napi)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp;
	int i;

	for(i=0 ; i<MAX_GMAC_NUM ; i++)
	{
		cp = root_cp->re_private_data_ptr[i];
		if(cp->gmac_enabled != GMAC_TRUE)
			continue;
		
		if(&cp->napi == napi)
			return i;
	}
	return NULL;
}

static int re8670_poll(struct napi_struct *napi, int budget)
{
	struct re_private_root *root_cp = &re_private_data_root;	
	struct re_private *cp;
	unsigned int gmac;
	int total_received_pkts = 0;
	u16 status;

	gmac = re8670_rx_napi_gmac_index_get(napi);
	
	cp = root_cp->re_private_data_ptr[gmac];
	total_received_pkts = re8670_rx(cp);
#ifdef RX_NAPI_MODE_DEBUG
	napi_statistic[total_received_pkts]++;
#endif
	if(total_received_pkts < budget) 
	{
		napi_complete_done(napi, total_received_pkts);
		/* do not diable interrupt if we got bad packet */
		unmask_rx_int(gmac);
		cp->cp_stats.rx_napi_interrupt++;
		return 0;
	}
	else
	{
		cp->cp_stats.rx_napi_poll++;
		status = read_isr_status(cp);
	}

	return total_received_pkts;
}
#endif

#ifdef TX_INTR_HANDLE
__IRAM_NIC void re8670_tdu_kick(struct re_private *cp, int ring_num);
#endif

__IRAM_NIC
static irqreturn_t re8670_interrupt(int irq, void *re_private_ptr, struct pt_regs *regs)
{
	struct re_private *cp = re_private_ptr;
	unsigned int gmac = cp->gmac;
	u32 status = read_isr_status(cp);
	
	if (!status)  
	{
		//printk(KERN_CONT "%s: no status indicated in interrupt, weird!\n", __func__);	//shlee 2.6
		return IRQ_RETVAL(IRQ_NONE);
	}
	
#ifdef TX_INTR_HANDLE
	if(status & TX_ALL(gmac))
	{
		if(status & ISR1_TDU)
		{
			///--printk(KERN_CONT "TDU\n");
			cp->cp_stats.tdu++;
			if (cp->isr1_status_tx&ISR1_TDU5) {
				//mask_tx_tdu_int(gmac, IMR0_TDU5);
				clear_tx_tdu_int(gmac, ISR1_TDU5);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 4);
#endif
			}
			if (cp->isr1_status_tx&ISR1_TDU4) {
				//mask_tx_tdu_int(gmac, IMR0_TDU4);
				clear_tx_tdu_int(gmac, ISR1_TDU4);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 3);
#endif
			}
			if (cp->isr1_status_tx&ISR1_TDU3) {
				//mask_tx_tdu_int(gmac, IMR0_TDU3);
				clear_tx_tdu_int(gmac, ISR1_TDU3);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 2);
#endif
			}
			if (cp->isr1_status_tx&ISR1_TDU2) {
				//mask_tx_tdu_int(gmac, IMR0_TDU2);
				clear_tx_tdu_int(gmac, ISR1_TDU2);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 1);
#endif
			}
			if (cp->isr1_status_tx&ISR1_TDU) {
				//mask_tx_tdu_int(gmac, IMR0_TDU);
				clear_tx_tdu_int(gmac, ISR1_TDU);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 0);
#endif
			}
		}
		if(status & ISR1_TOK)
		{
			///--printk(KERN_CONT "TOK\n");
			cp->cp_stats.tok++;
			if (cp->isr1_status_tx&ISR1_TOK5) {
				mask_tx_tok_int(gmac, IMR0_TOK5);
				tasklet_hi_schedule(&cp->tx_tok_tasklets_ring[4]);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 4);
#endif
				clear_tx_tok_int(gmac, ISR1_TOK5);
			}
			if (cp->isr1_status_tx&ISR1_TOK4) {
				mask_tx_tok_int(gmac, IMR0_TOK4);
				tasklet_hi_schedule(&cp->tx_tok_tasklets_ring[3]);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 3);
#endif
				clear_tx_tok_int(gmac, ISR1_TOK4);
			}
			if (cp->isr1_status_tx&ISR1_TOK3) {
				mask_tx_tok_int(gmac, IMR0_TOK3);
				tasklet_hi_schedule(&cp->tx_tok_tasklets_ring[2]);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 2);
#endif
				clear_tx_tok_int(gmac, ISR1_TOK3);
			}
			if (cp->isr1_status_tx&ISR1_TOK2) {
				mask_tx_tok_int(gmac, IMR0_TOK2);
				tasklet_hi_schedule(&cp->tx_tok_tasklets_ring[1]);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 1);
#endif
				clear_tx_tok_int(gmac, ISR1_TOK2);
			}
			if (cp->isr1_status_tx&ISR1_TOK) {
				mask_tx_tok_int(gmac, IMR0_TOK);
				tasklet_hi_schedule(&cp->tx_tok_tasklets_ring[0]);
#ifdef TX_KICK_RING_USING_TDU_INT
				re8670_tdu_kick(cp, 0);
#endif
				clear_tx_tok_int(gmac, ISR1_TOK);
			}
		}
	}
#endif

	if (status & RX_ALL(gmac)) 
	{
		if(status & RER_RUNT)
		{
			cp->cp_stats.rer_runt++;
		}
		if(status & RER_OVF)
		{
			cp->cp_stats.rer_ovf++;
		}
		if(status & RDU_ALL)
		{
			mask_rx_int(gmac);
			cp->cp_stats.rdu++;
#ifdef RX_NAPI_MODE
			napi_schedule(&cp->napi);
#else
			tasklet_hi_schedule(&cp->rx_tasklets);
#endif
		}
		if(status & (RX_OK | SW_INT))
		{
			mask_rx_int(gmac);
#ifdef RX_NAPI_MODE
			napi_schedule(&cp->napi);
#else
			tasklet_hi_schedule(&cp->rx_tasklets);
#endif
		}
	}
	
	clear_isr(cp, status);

	return IRQ_RETVAL(IRQ_HANDLED);
}

static inline void updateTxStatic(struct sk_buff *skb){
	DEVPRIV(skb->dev)->net_stats.tx_packets++;
	DEVPRIV(skb->dev)->net_stats.tx_bytes += skb->len;
}

#ifdef TX_RECYCLE_SKB_USING_TOK_INT
atomic_t re8670_tx_lock=ATOMIC_INIT(0);
#endif

void nic_tx_ring_dump(unsigned int gmac, struct seq_file *m);
void nic_rx_ring_dump(unsigned int gmac, struct seq_file *m);

__IRAM_NIC void re8670_tx(struct re_private *cp, int ring_num, int from_hw)
{
	unsigned tx_tail = cp->tx_Mhqtail[ring_num];
	u32 status;
	struct sk_buff *skb;

	if(unlikely(cp->eth_close == GMAC_TRUE))
		return;
	
	while (!((status = (cp->tx_Mhqring[ring_num][tx_tail].opts1))& DescOwn)) {
		if (tx_tail == cp->tx_Mhqhead[ring_num])
			break;

		skb = cp->tx_skb[ring_num][tx_tail].skb;

#if defined(CONFIG_REALTEK_HW_LSO) || defined(TX_BURST_PACKET_SEND)
		//printk(KERN_CONT "skb=%x tx_tail=%d\n",(u32)skb,tx_tail);
		while((u32)skb==0xffffffff) //skb is last_frag in mfrag_skb.
		{
			cp->tx_skb[ring_num][tx_tail].skb=NULL;
#ifdef TX_BURST_PACKET_SEND
			if (tx_tail == cp->tx_Mhqhead[ring_num]){
				skb=NULL;
				break;
			}
#endif
			tx_tail = NEXT_TX(tx_tail,cp->re8670_tx_ring_size[ring_num]); //tysu: this skb is many frags skb, just free once.
			skb = cp->tx_skb[ring_num][tx_tail].skb;
			//printk(KERN_CONT "skb=%x tx_tail=%d\n",(u32)skb,tx_tail);
			status = cp->tx_Mhqring[ring_num][tx_tail].opts1;			
			if(status & DescOwn)
			{
				break;
			}
		}

		if(status & DescOwn) break;
		if(skb==NULL) break;
#else
		if (unlikely(!skb))   
			break;
#endif
		updateTxStatic(skb);	

		dev_kfree_skb_any(skb);
#if defined(TX_RECYCLE_SKB_USING_TOK_INT) || defined(TX_RECYCLE_SKB_USING_POLLING)
		if(from_hw) {
			cp->cp_stats.tok_free_skb[ring_num]++;
		}
#endif
		cp->tx_skb[ring_num][tx_tail].skb = NULL;		
		
		tx_tail = NEXT_TX(tx_tail, cp->re8670_tx_ring_size[ring_num]);
	}
	cp->tx_Mhqtail[ring_num]=tx_tail;

	if (netif_queue_stopped(ROOTDEV) && (TX_HQBUFFS_AVAIL(cp, ring_num) > (MAX_SKB_FRAGS + 1)))
		netif_wake_queue(ROOTDEV);
}

#ifdef TX_KICK_RING_USING_POLLING
static inline unsigned int re8670_tx_ring_active_check(struct re_private *cp, int ring_num)
{
	int ret;

	ret=(cp->cp_stats.tx_hw_num_ring[ring_num]-cp->last_tx_hw_num_ring[ring_num] > tx_ring_active_poll_packet_num) ? 1:0;
	cp->last_tx_hw_num_ring[ring_num] = cp->cp_stats.tx_hw_num_ring[ring_num];

	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
__IRAM_NIC void re8670_tx_ring_active_poll(struct re_private *cp)
{
	int ring_num;
	DMA_TX_DESC *txd;

	for (ring_num=0 ; ring_num<MAX_TXRING_NUM ; ring_num++) {

		if (!cp->re8670_tx_ring_size[ring_num])
			continue;

		if (re8670_tx_ring_active_check(cp, ring_num))
		{
			if(cp->tx_tdu_poll_status[ring_num] == 1)
				continue;

			if(timer_pending(&cp->tx_tdu_kick_polling_timer))
				timer_delete(&cp->tx_tdu_kick_polling_timer);
			mod_timer(&cp->tx_tdu_kick_polling_timer, jiffies+tx_tdu_kick_ring_10msec);
			cp->tx_tdu_poll_status[ring_num] = 1;
		}
		else
		{
			if(cp->tx_tdu_poll_status[ring_num] == 0)
				continue;

			if(timer_pending(&cp->tx_tdu_kick_polling_timer))
				timer_delete(&cp->tx_tdu_kick_polling_timer);
			cp->tx_tdu_poll_status[ring_num] = 0;
		}
	}

	mod_timer(&cp->tx_ring_active_polling_timer, jiffies+(tx_ring_active_poll_sec*
HZ));

	return;
}

__IRAM_NIC void re8670_tx_tdu_kick_poll(struct re_private *cp)
{
	int hw_ring_num, ring_num;
	DMA_TX_DESC *txd;

	for (ring_num=0 ; ring_num<MAX_TXRING_NUM ; ring_num++) {

		if (!cp->re8670_tx_ring_size[ring_num])
			continue;

		if (!cp->tx_tdu_poll_status[ring_num])
			continue;

		cp->cp_stats.tdu_poll[ring_num]++;
		hw_ring_num = idx_sw2hw(ring_num);
		txd = (DMA_TX_DESC *)((u32)&cp->tx_Mhqring[ring_num][RLE0787_R16(cp->gmac, TxCDO1+(ADDR_OFFSET*hw_ring_num))]|0xa0000000);
		if (!(txd->opts1&0x80000000)){
			goto tx_tdu_kick_poll_exit;
		}
		kick_tx(cp->gmac, ring_num);
		cp->cp_stats.tdu_kick_ring_poll[ring_num]++;
	}

tx_tdu_kick_poll_exit:
	mod_timer(&cp->tx_tdu_kick_polling_timer, jiffies+tx_tdu_kick_ring_10msec);
	return;
}
#else
__IRAM_NIC static void re8670_tx_ring_active_poll_gmac0(struct timer_list *timer)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[0];
	int hw_ring_num, ring_num;
	DMA_TX_DESC *txd;

	for (ring_num=0 ; ring_num<MAX_TXRING_NUM ; ring_num++) {

		if (!cp->re8670_tx_ring_size[ring_num])
			continue;

		if (re8670_tx_ring_active_check(cp, ring_num))
		{
			if(cp->tx_tdu_poll_status[ring_num] == 1)
				continue;

			printk(KERN_CONT "%s %d: switch ON\n", __func__, __LINE__);
			if(timer_pending(&cp->tx_tdu_kick_polling_timer))
				timer_delete(&cp->tx_tdu_kick_polling_timer);
			mod_timer(&cp->tx_tdu_kick_polling_timer, jiffies+tx_tdu_kick_ring_10msec);
			cp->tx_tdu_poll_status[ring_num] = 1;
		}
		else
		{
			if(cp->tx_tdu_poll_status[ring_num] == 0)
				continue;

			printk(KERN_CONT "%s %d: switch OFF\n", __func__, __LINE__);
			if(timer_pending(&cp->tx_tdu_kick_polling_timer))
				timer_delete(&cp->tx_tdu_kick_polling_timer);
			cp->tx_tdu_poll_status[ring_num] = 0;
		}
	}

	mod_timer(&cp->tx_ring_active_polling_timer, jiffies+(tx_ring_active_poll_sec*
HZ));

	return;
}

__IRAM_NIC static void re8670_tx_ring_active_poll_gmac1(struct timer_list *timer)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[1];
	int hw_ring_num, ring_num;
	DMA_TX_DESC *txd;

	for (ring_num=0 ; ring_num<MAX_TXRING_NUM ; ring_num++) {

		if (!cp->re8670_tx_ring_size[ring_num])
			continue;

		if (re8670_tx_ring_active_check(cp, ring_num))
		{
			if(cp->tx_tdu_poll_status[ring_num] == 1)
				continue;

			printk(KERN_CONT "%s %d: switch ON\n", __func__, __LINE__);
			if(timer_pending(&cp->tx_tdu_kick_polling_timer))
				timer_delete(&cp->tx_tdu_kick_polling_timer);
			mod_timer(&cp->tx_tdu_kick_polling_timer, jiffies+tx_tdu_kick_ring_10msec);
			cp->tx_tdu_poll_status[ring_num] = 1;
		}
		else
		{
			if(cp->tx_tdu_poll_status[ring_num] == 0)
				continue;

			printk(KERN_CONT "%s %d: switch OFF\n", __func__, __LINE__);
			if(timer_pending(&cp->tx_tdu_kick_polling_timer))
				timer_delete(&cp->tx_tdu_kick_polling_timer);
			cp->tx_tdu_poll_status[ring_num] = 0;
		}
	}

	mod_timer(&cp->tx_ring_active_polling_timer, jiffies+(tx_ring_active_poll_sec*
HZ));

	return;
}

__IRAM_NIC static void re8670_tx_ring_active_poll_gmac2(struct timer_list *timer)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[2];
	int hw_ring_num, ring_num;
	DMA_TX_DESC *txd;

	for (ring_num=0 ; ring_num<MAX_TXRING_NUM ; ring_num++) {

		if (!cp->re8670_tx_ring_size[ring_num])
			continue;

		if (re8670_tx_ring_active_check(cp, ring_num))
		{
			if(cp->tx_tdu_poll_status[ring_num] == 1)
				continue;

			printk(KERN_CONT "%s %d: switch ON\n", __func__, __LINE__);
			if(timer_pending(&cp->tx_tdu_kick_polling_timer))
				timer_delete(&cp->tx_tdu_kick_polling_timer);
			mod_timer(&cp->tx_tdu_kick_polling_timer, jiffies+tx_tdu_kick_ring_10msec);
			cp->tx_tdu_poll_status[ring_num] = 1;
		}
		else
		{
			if(cp->tx_tdu_poll_status[ring_num] == 0)
				continue;

			printk(KERN_CONT "%s %d: switch OFF\n", __func__, __LINE__);
			if(timer_pending(&cp->tx_tdu_kick_polling_timer))
				timer_delete(&cp->tx_tdu_kick_polling_timer);
			cp->tx_tdu_poll_status[ring_num] = 0;
		}
	}

	mod_timer(&cp->tx_ring_active_polling_timer, jiffies+(tx_ring_active_poll_sec*
HZ));

	return;
}

__IRAM_NIC static void re8670_tx_tdu_kick_poll_gmac0(struct timer_list *timer)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[0];
	int hw_ring_num, ring_num;
	DMA_TX_DESC *txd;

	for (ring_num=0 ; ring_num<MAX_TXRING_NUM ; ring_num++) {

		if (!cp->re8670_tx_ring_size[ring_num])
			continue;

		if (!cp->tx_tdu_poll_status[ring_num])
			continue;

		cp->cp_stats.tdu_poll[ring_num]++;
		hw_ring_num = idx_sw2hw(ring_num);
		txd = (DMA_TX_DESC *)((u32)&cp->tx_Mhqring[ring_num][RLE0787_R16(cp->gmac, TxCDO1+(ADDR_OFFSET*hw_ring_num))]|0xa0000000);
		if (!(txd->opts1&0x80000000)){
			goto tx_tdu_kick_poll_exit;
			return;
		}
		kick_tx(cp->gmac, ring_num);
		cp->cp_stats.tdu_kick_ring_poll[ring_num]++;
	}

tx_tdu_kick_poll_exit:
	mod_timer(&cp->tx_tdu_kick_polling_timer, jiffies+tx_tdu_kick_ring_10msec);
	return;
}

__IRAM_NIC static void re8670_tx_tdu_kick_poll_gmac1(struct timer_list *timer)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[1];
	int hw_ring_num, ring_num;
	DMA_TX_DESC *txd;

	for (ring_num=0 ; ring_num<MAX_TXRING_NUM ; ring_num++) {

		if (!cp->re8670_tx_ring_size[ring_num])
			continue;

		if (!cp->tx_tdu_poll_status[ring_num])
			continue;

		cp->cp_stats.tdu_poll[ring_num]++;
		hw_ring_num = idx_sw2hw(ring_num);
		txd = (DMA_TX_DESC *)((u32)&cp->tx_Mhqring[ring_num][RLE0787_R16(cp->gmac, TxCDO1+(ADDR_OFFSET*hw_ring_num))]|0xa0000000);
		if (!(txd->opts1&0x80000000)){
			goto tx_tdu_kick_poll_exit;
			return;
		}
		kick_tx(cp->gmac, ring_num);
		cp->cp_stats.tdu_kick_ring_poll[ring_num]++;
	}

tx_tdu_kick_poll_exit:
	mod_timer(&cp->tx_tdu_kick_polling_timer, jiffies+tx_tdu_kick_ring_10msec);
	return;
}

__IRAM_NIC static void re8670_tx_tdu_kick_poll_gmac2(struct timer_list *timer)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[2];
	int hw_ring_num, ring_num;
	DMA_TX_DESC *txd;

	for (ring_num=0 ; ring_num<MAX_TXRING_NUM ; ring_num++) {

		if (!cp->re8670_tx_ring_size[ring_num])
			continue;

		if (!cp->tx_tdu_poll_status[ring_num])
			continue;

		cp->cp_stats.tdu_poll[ring_num]++;
		hw_ring_num = idx_sw2hw(ring_num);
		txd = (DMA_TX_DESC *)((u32)&cp->tx_Mhqring[ring_num][RLE0787_R16(cp->gmac, TxCDO1+(ADDR_OFFSET*hw_ring_num))]|0xa0000000);
		if (!(txd->opts1&0x80000000)){
			goto tx_tdu_kick_poll_exit;
			return;
		}
		kick_tx(cp->gmac, ring_num);
		cp->cp_stats.tdu_kick_ring_poll[ring_num]++;
	}

tx_tdu_kick_poll_exit:
	mod_timer(&cp->tx_tdu_kick_polling_timer, jiffies+tx_tdu_kick_ring_10msec);
	return;
}
#endif
#endif

#ifdef TX_INTR_HANDLE
#ifdef TX_KICK_RING_USING_TDU_INT
__IRAM_NIC void re8670_tdu_kick_all(struct re_private *cp)
{
	int ring_num;
	int sw_ring_num;
	DMA_TX_DESC *txd;

	for (ring_num=(MAX_TXRING_NUM-1) ; ring_num>=0 ; ring_num--)
	{
		sw_ring_num = idx_hw2sw(ring_num);
		txd = (DMA_TX_DESC *)((u32)&cp->tx_Mhqring[sw_ring_num][RLE0787_R16(cp->gmac, TxCDO1+(ADDR_OFFSET*ring_num))]|0xa0000000);
		if (!(txd->opts1&0x80000000)){
			continue;
		}
		kick_tx_hw(cp->gmac, ring_num);
		cp->cp_stats.tdu_kick_ring_retry[sw_ring_num]++;
		tasklet_hi_schedule(&cp->tx_tdu_kick_tasklets);
	}

	return;
}

__IRAM_NIC void re8670_tdu_kick(struct re_private *cp, int ring_num)
{
	unsigned int gmac = cp->gmac;
	int sw_ring_num;
	DMA_TX_DESC *txd;

	sw_ring_num = idx_hw2sw(ring_num);
	txd = (DMA_TX_DESC *)((u32)&cp->tx_Mhqring[sw_ring_num][RLE0787_R16(gmac, TxCDO1+(ADDR_OFFSET*ring_num))]|0xa0000000);
	if (!(txd->opts1&0x80000000)){
		///--clear_tx_tdu_int(gmac, (ISR1_TDU<<ring_num));
		///--unmask_tx_tdu_int(gmac, (IMR0_TDU<<(ring_num)));
		return;
	}
	kick_tx_hw(gmac, ring_num);
	cp->cp_stats.tdu_kick_ring_intr[sw_ring_num]++;
	tasklet_hi_schedule(&cp->tx_tdu_kick_tasklets);
	///--clear_tx_tdu_int(gmac, (ISR1_TDU<<ring_num));
	///--unmask_tx_tdu_int(gmac, (IMR0_TDU<<(ring_num)));
	return;
}
#endif

__IRAM_NIC void re8670_tx_tok_ring0(struct re_private *cp)
{
	unsigned int gmac = cp->gmac;
	int ring_num=0;
	int sw_ring_num;

	sw_ring_num = idx_hw2sw(ring_num);
#ifdef TX_RECYCLE_SKB_USING_TOK_INT
	GMAC_SPIN_LOCK(&cp->tx_lock);
	re8670_tx(cp, sw_ring_num, GMAC_TRUE);
	GMAC_SPIN_UNLOCK(&cp->tx_lock);
#endif
	///--clear_tx_tok_int(gmac, (ISR1_TOK<<ring_num));
	unmask_tx_tok_int(gmac, (IMR0_TOK<<(ring_num)));

	return;
}

__IRAM_NIC void re8670_tx_tok_ring1(struct re_private *cp)
{
	unsigned int gmac = cp->gmac;
	int ring_num=1;
	int sw_ring_num;

	sw_ring_num = idx_hw2sw(ring_num);
#ifdef TX_RECYCLE_SKB_USING_TOK_INT
	GMAC_SPIN_LOCK(&cp->tx_lock);
	re8670_tx(cp, sw_ring_num, GMAC_TRUE);
	GMAC_SPIN_UNLOCK(&cp->tx_lock);
#endif
	///--clear_tx_tok_int(gmac, (ISR1_TOK<<ring_num));
	unmask_tx_tok_int(gmac, (IMR0_TOK<<(ring_num)));

	return;
}

__IRAM_NIC void re8670_tx_tok_ring2(struct re_private *cp)
{
	unsigned int gmac = cp->gmac;
	int ring_num=2;
	int sw_ring_num;

	sw_ring_num = idx_hw2sw(ring_num);
#ifdef TX_RECYCLE_SKB_USING_TOK_INT
	GMAC_SPIN_LOCK(&cp->tx_lock);
	re8670_tx(cp, sw_ring_num, GMAC_TRUE);
	GMAC_SPIN_UNLOCK(&cp->tx_lock);
#endif
	///--clear_tx_tok_int(gmac, (ISR1_TOK<<ring_num));
	unmask_tx_tok_int(gmac, (IMR0_TOK<<(ring_num)));

	return;
}

__IRAM_NIC void re8670_tx_tok_ring3(struct re_private *cp)
{
	unsigned int gmac = cp->gmac;
	int ring_num=3;
	int sw_ring_num;

	sw_ring_num = idx_hw2sw(ring_num);
#ifdef TX_RECYCLE_SKB_USING_TOK_INT
	GMAC_SPIN_LOCK(&cp->tx_lock);
	re8670_tx(cp, sw_ring_num, GMAC_TRUE);
	GMAC_SPIN_UNLOCK(&cp->tx_lock);
#endif
	///--clear_tx_tok_int(gmac, (ISR1_TOK<<ring_num));
	unmask_tx_tok_int(gmac, (IMR0_TOK<<(ring_num)));

	return;
	}

__IRAM_NIC void re8670_tx_tok_ring4(struct re_private *cp)
{
	unsigned int gmac = cp->gmac;
	int ring_num=4;
	int sw_ring_num;

	sw_ring_num = idx_hw2sw(ring_num);
#ifdef TX_RECYCLE_SKB_USING_TOK_INT
	GMAC_SPIN_LOCK(&cp->tx_lock);
	re8670_tx(cp, sw_ring_num, GMAC_TRUE);
	GMAC_SPIN_UNLOCK(&cp->tx_lock);
#endif
	///--clear_tx_tok_int(gmac, (ISR1_TOK<<ring_num));
	unmask_tx_tok_int(gmac, (IMR0_TOK<<(ring_num)));

	return;
}
#elif defined(TX_RECYCLE_SKB_USING_POLLING)
__IRAM_NIC void re8670_tx_all(struct re_private *cp)
{
	int i;
	if(cp->gmac_enabled == GMAC_FALSE)
	{
		goto tx_all_exit;
	}

	if(cp->eth_close == GMAC_TRUE)
	{
		goto tx_all_exit;
	}
	
	GMAC_SPIN_LOCK(&cp->tx_lock);	
	for (i=0 ; i<MAX_TXRING_NUM ; i++) {
		re8670_tx(cp, i, GMAC_TRUE);
		kick_tx(cp->gmac, i);
	}
	GMAC_SPIN_UNLOCK(&cp->tx_lock);

tx_all_exit:
	mod_timer(&cp->tok_polling_timer, jiffies+TX_RECYCLE_SKB_POLLING_10MSECONDS);
	return;
}
#endif

__IRAM_NIC void checkTXDesc(int ring_num, unsigned int gmac)
{
#if !defined(TX_RECYCLE_SKB_USING_TOK_INT) && !defined(TX_RECYCLE_SKB_USING_POLLING)
	struct re_private_root *root_cp = &re_private_data_root;	
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	
	if (likely(!atomic_read(&lock_tx_tail)) && cp!=NULL) {
		atomic_set(&lock_tx_tail, 1);
        re8670_tx(cp, ring_num, GMAC_FALSE);
		atomic_set(&lock_tx_tail, 0);
	}
#else
	return;
#endif
}

#ifdef CONFIG_REALTEK_HW_LSO
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
void re8670_mFrag_xmit(struct sk_buff *skb, struct re_private *cp, unsigned *entry, struct tx_info* ptxInfo, int ring_num)
{
	struct re_private_root *root_cp = &re_private_data_root;
	int addi_l2_len = 0;
	unsigned first_entry;
	u32 first_len;
	struct sk_buff *new_skb;
	unsigned int gmac = cp->gmac;
	struct tx_info* first_txd;
	struct tx_info* txd;
	int frag;
#ifdef HW_LSO_ENABLE
	int mtu;
#endif

	struct ethhdr *eth = eth_hdr(skb);
	if (eth && eth->h_proto == htons(0x8100))
		addi_l2_len += VLAN_HLEN;

	if (__vlan_get_protocol(skb, skb->protocol, NULL) == htons(ETH_P_PPP_SES))
		addi_l2_len += RE8670_PPPOE_HDR_SIZE;

	if (likely((skb->len - skb->data_len) != (RE8670_HW_LSO_FS_TX_STUCK_LEN+addi_l2_len) 
		&& (skb->len - skb->data_len) != RE8670_HW_LSO_FS_TX_STUCK_LEN_1))
	{
#ifdef CONFIG_REALTEK_HW_LSO			
		cp->tx_skb[ring_num][*entry].skb = (struct sk_buff *)0xffffffff;
#else
		cp->tx_skb[ring_num][*entry].skb = skb;
#endif
		cp->tx_skb[ring_num][*entry].mapping = (dma_addr_t)skb->data;
		cp->tx_skb[ring_num][*entry].frag = 1;

		first_txd = &cp->tx_Mhqring[ring_num][*entry];
		if(ptxInfo)
		{
			memcpy(first_txd, ptxInfo, sizeof(struct tx_info));
		}

		first_txd->addr = (skb->data);
#ifdef HW_LSO_ENABLE
		mtu = re8670_get_mtu(skb, cp);
		if(mtu && (skb->len-14) > mtu)
		{
			first_txd->opts4.bit.lgsen = 1;
			if (__vlan_get_protocol(skb, skb->protocol, NULL) == htons(ETH_P_PPP_SES))
				mtu -= RE8670_PPPOE_HDR_SIZE;
			first_txd->opts4.bit.lgmtu = mtu; //real MTU
		}
		else
		{
			first_txd->opts4.bit.lgsen = 0;
			first_txd->opts4.bit.lgmtu = 0;
		}
#endif
		dma_cache_wback_inv((unsigned long)skb->data, (skb->len - skb->data_len));
		first_txd->opts1.bit.fs = 1;
		first_txd->opts1.bit.ls = 0;
		first_txd->opts1.bit.crc = 1;
		first_txd->opts1.bit.ipcs = 1;
		first_len = (skb->len - skb->data_len);
		first_txd->opts1.bit.data_length = first_len;	
		first_txd->opts1.bit.eor = (*entry == (cp->re8670_tx_ring_size[ring_num] - 1)) ? 1 : 0;
		if(first_txd->opts2.bit.tx_portmask&CPU_PORT_MASK_ALL
			&& !(first_txd->opts3.bit.tx_dst_stream_id & 0x7F))
		{
			first_txd->opts3.bit.tx_dst_stream_id |= 0x1;
		}
		first_txd->opts1.bit.own = 0;
		first_entry = *entry;
#ifdef TX_RING_DEBUG
		if(cp->tx_ring_backup_debug)
		{
			DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ring_num].txDescriptor[*entry];
			memset(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
			memcpy(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], skb->data, (skb->len - skb->data_len));
			apply_to_txdesc(dbg_txd, first_txd);
			dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry];
		}
#endif
		dma_cache_wback_inv(first_txd, sizeof(struct tx_info)); 

		*entry = NEXT_TX(*entry, cp->re8670_tx_ring_size[ring_num]);
		for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
			skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];
			u32 len, mapping, new_skb_len;

#if defined(LINUX_SG_ENABLE) && defined(NIC_DESC_ACCELERATE_FOR_SG)
			u32 first_data_mapping;
			u32 next_mapping;
			skb_frag_t *next_frag = &skb_shinfo(skb)->frags[frag+1];
			if((frag+1) == skb_shinfo(skb)->nr_frags) next_frag=NULL;

			len=0;
			mapping = (u32)page_address(skb_frag_page(this_frag))+skb_frag_off(this_frag);
			first_data_mapping=mapping;

			while(next_frag)
			{
				next_mapping=(u32)page_address(skb_frag_page(next_frag))+skb_frag_off(next_frag);
				if(next_mapping==skb_frag_size(this_frag)+mapping)
				{
					len += skb_frag_size(this_frag);
					this_frag=next_frag;
					mapping=next_mapping;
					++frag;
					next_frag=&skb_shinfo(skb)->frags[frag+1];
					if((frag+1) == skb_shinfo(skb)->nr_frags) next_frag=NULL;
				}
				else //the data is not continuously
				{
					break;
				}
			}
			len+=skb_frag_size(this_frag);
			//first_data_mapping |= UNCACHE_MASK; //tysu: disable uncache
			mapping=first_data_mapping;
#else
			len = skb_frag_size(this_frag);
			//mapping = (u32)page_address(this_frag->page)+skb_frag_off(this_frag)|UNCACHE_MASK;
			struct page *this_frag_page = skb_frag_page(this_frag);
			mapping = (u32)page_address(this_frag_page->p)+skb_frag_off(this_frag); //tysu: disable uncache
#endif
			txd = &cp->tx_Mhqring[ring_num][*entry];
			if(ptxInfo)
			{
				memcpy(txd, ptxInfo, sizeof(struct tx_info));
			}
			txd->addr = (mapping);

#ifdef HW_LSO_ENABLE
			if(mtu && (skb->len-14) > mtu)
			{
				txd->opts4.bit.lgsen = 1;
				txd->opts4.bit.lgmtu = mtu; //real MTU
			}
			else
			{
				txd->opts4.bit.lgsen = 0;
				txd->opts4.bit.lgmtu = 0;
			}
#endif
			dma_cache_wback_inv((unsigned long)mapping, len);
			txd->opts1.bit.fs = 0;
			if(frag != skb_shinfo(skb)->nr_frags-1)
			{
				if (!frag && ((mtu+ETH_HLEN) > first_len) && (len <= ((mtu+ETH_HLEN) - first_len)))
				{
					new_skb = dev_alloc_skb(skb->len);
					if(new_skb)
					{
						new_skb->dev = skb->dev;
						cp->tx_skb[ring_num][first_entry].skb = new_skb;
						//first_data_mapping = (new_skb->data);
						txd->addr = (new_skb->data);
						new_skb_len = len;
						this_frag = &skb_shinfo(skb)->frags[frag];
						memcpy((new_skb->data), (u32)page_address(skb_frag_page(this_frag))+skb_frag_off(this_frag), skb_frag_size(this_frag));
						while(new_skb_len <= ((mtu+ETH_HLEN) - first_len))
						{
							frag++;
							this_frag = &skb_shinfo(skb)->frags[frag];
							memcpy((new_skb->data + new_skb_len), (u32)page_address(skb_frag_page(this_frag))+skb_frag_off(this_frag), skb_frag_size(this_frag));
							new_skb_len += skb_frag_size(this_frag);
							if (frag != skb_shinfo(skb)->nr_frags-1)
							{
								txd->opts1.bit.ls = 0;
								cp->tx_skb[ring_num][*entry].skb = (struct sk_buff *)0xffffffff;
							}
							else
							{
								txd->opts1.bit.ls = 1;
								cp->tx_skb[ring_num][*entry].skb = skb;
								break;
							}
						}
						skb_put(new_skb, new_skb_len);
						len = new_skb_len;
						dma_cache_wback_inv(new_skb->data, new_skb->len);
					}
					else
					{
						txd->opts1.bit.ls = 0;
						cp->tx_skb[ring_num][*entry].skb = (struct sk_buff *)0xffffffff;
					}
				}
				else
				{
					txd->opts1.bit.ls = 0;
					cp->tx_skb[ring_num][*entry].skb = (struct sk_buff *)0xffffffff;
				}
			}
			else
			{
				txd->opts1.bit.ls = 1;
				cp->tx_skb[ring_num][*entry].skb = skb;
			}
			txd->opts1.bit.crc = 1;
			txd->opts1.bit.ipcs = 1;
			txd->opts1.bit.data_length = len;
			txd->opts1.bit.eor = (*entry == (cp->re8670_tx_ring_size[ring_num] - 1)) ? 1 : 0;
			if(txd->opts2.bit.tx_portmask&CPU_PORT_MASK_ALL
				&& !(txd->opts3.bit.tx_dst_stream_id & 0x7F))
			{
				txd->opts3.bit.tx_dst_stream_id |= 0x1;
			}
			txd->opts1.bit.own = 1;
#ifdef TX_RING_DEBUG
			if(cp->tx_ring_backup_debug)
			{
				DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ring_num].txDescriptor[*entry];
				memset(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
				memcpy(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], mapping, len);
				apply_to_txdesc(dbg_txd, txd);
				dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry];
			}
#endif
			dma_cache_wback_inv(txd, sizeof(struct tx_info));

			//wmb();
			//msg_queue_print("i=%x %x s=%x %x l=%d\n",*(u8*)(txd->addr+0x12),*(u8*)(txd->addr+0x13),*(u8*)(txd->addr+0x32),*(u8*)(txd->addr+0x33),len);

#if defined(LINUX_SG_ENABLE) && defined(NIC_DESC_ACCELERATE_FOR_SG)			
			cp->tx_skb[ring_num][*entry].mapping = (dma_addr_t)first_data_mapping;
#else
			cp->tx_skb[ring_num][*entry].mapping = (dma_addr_t)mapping;
#endif
			cp->tx_skb[ring_num][*entry].frag = frag + 1;
			*entry = NEXT_TX(*entry, cp->re8670_tx_ring_size[ring_num]);
		}

		first_txd->opts1.bit.own = 1;
		dma_cache_wback_inv(&(first_txd->opts1), sizeof(u32));
	}
	else
	{
		new_skb = dev_alloc_skb(skb->len);
		if(new_skb)
		{
			new_skb->dev = skb->dev;
			cp->tx_skb[ring_num][*entry].skb = new_skb;
			first_txd = &cp->tx_Mhqring[ring_num][*entry];
			if(ptxInfo)
			{
				memcpy(first_txd, ptxInfo, sizeof(struct tx_info));
			}

			first_txd->addr = (new_skb->data);
			memcpy(new_skb->data, skb->data, (skb->len - skb->data_len));
#ifdef HW_LSO_ENABLE
			mtu = re8670_get_mtu(skb, cp);
			if(mtu && (skb->len-14) > mtu)
			{
				first_txd->opts4.bit.lgsen = 1;
				if (__vlan_get_protocol(skb, skb->protocol, NULL) == htons(ETH_P_PPP_SES))
					mtu -= RE8670_PPPOE_HDR_SIZE;
				first_txd->opts4.bit.lgmtu = mtu; //real MTU
			}
			else
			{
				first_txd->opts4.bit.lgsen = 0;
				first_txd->opts4.bit.lgmtu = 0;
			}
#endif
			first_txd->opts1.bit.fs = 1;
			first_txd->opts1.bit.ls = 1;
			first_txd->opts1.bit.crc = 1;
			first_txd->opts1.bit.ipcs = 1;
			first_len = (skb->len - skb->data_len);
			first_txd->opts1.bit.data_length = first_len;
			first_txd->opts1.bit.eor = (*entry == (cp->re8670_tx_ring_size[ring_num] - 1)) ? 1 : 0;
			if(first_txd->opts2.bit.tx_portmask&CPU_PORT_MASK_ALL
				&& !(first_txd->opts3.bit.tx_dst_stream_id & 0x7F))
			{
				first_txd->opts3.bit.tx_dst_stream_id |= 0x1;
			}

			for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
				skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];

				memcpy(new_skb->data + first_txd->opts1.bit.data_length, (u32)page_address(skb_frag_page(this_frag))+skb_frag_off(this_frag), skb_frag_size(this_frag));
				first_txd->opts1.bit.data_length += skb_frag_size(this_frag);
			}
			dma_cache_wback_inv((unsigned long)new_skb->data, first_txd->opts1.bit.data_length);
			first_txd->opts1.bit.own = 1;

			if(!first_txd->opts4.bit.lgsen && first_txd->opts1.bit.data_length > RE8670_MAX_ETH_FRAME_SIZE)
			{
				if(!cp->tx_jumbo_frame_enabled || (first_txd->opts1.bit.data_length > RE8670_MAX_ETH_JUMBO_FRAME_SIZE))
				{
					dev_kfree_skb_any(new_skb);
					cp->tx_skb[ring_num][*entry].skb = skb;
					*entry = NEXT_TX(*entry, cp->re8670_tx_ring_size[ring_num]);
					return;
				}
				else
				{
					first_txd->opts1.dw &= ~(IPCS|L4CS);
				}
			}

#ifdef TX_RING_DEBUG
			if(cp->tx_ring_backup_debug)
			{
				DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ring_num].txDescriptor[*entry];
				memset(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
				memcpy(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], skb->data, (skb->len - skb->data_len));
				apply_to_txdesc(dbg_txd, first_txd);
				dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry];
			}
#endif
			dma_cache_wback_inv(first_txd, sizeof(struct tx_info));
			dev_kfree_skb_any(skb);
		}
		else
		{
			cp->tx_skb[ring_num][*entry].skb = skb;
		}
		*entry = NEXT_TX(*entry, cp->re8670_tx_ring_size[ring_num]);
	}
}

#else
void re8670_mFrag_xmit(struct sk_buff *skb, struct re_private *cp, unsigned *entry, struct tx_info* ptxInfo, int ring_num)
{
	struct re_private_root *root_cp = &re_private_data_root;
	int addi_l2_len = 0;
	unsigned first_entry;
	u32 first_len;
	struct sk_buff *new_skb;
	unsigned int gmac = cp->gmac;
	struct tx_info* first_txd;
	struct tx_info* txd;
	int frag;
#ifdef HW_LSO_ENABLE
	int mtu;
#endif

	struct ethhdr *eth = eth_hdr(skb);
	if (eth && eth->h_proto == htons(0x8100))
		addi_l2_len += VLAN_HLEN;

	if (__vlan_get_protocol(skb, skb->protocol, NULL) == htons(ETH_P_PPP_SES))
		addi_l2_len += RE8670_PPPOE_HDR_SIZE;

	if (likely((skb->len - skb->data_len) != (RE8670_HW_LSO_FS_TX_STUCK_LEN+addi_l2_len) 
		&& (skb->len - skb->data_len) != RE8670_HW_LSO_FS_TX_STUCK_LEN_1))
	{
#ifdef CONFIG_REALTEK_HW_LSO			
		cp->tx_skb[ring_num][*entry].skb = (struct sk_buff *)0xffffffff;
#else
		cp->tx_skb[ring_num][*entry].skb = skb;
#endif
		cp->tx_skb[ring_num][*entry].mapping = (dma_addr_t)skb->data;
		cp->tx_skb[ring_num][*entry].frag = 1;

		first_txd = &cp->tx_Mhqring[ring_num][*entry];
		if(ptxInfo)
		{
			memcpy(first_txd, ptxInfo, sizeof(struct tx_info));
		}

		first_txd->addr = (skb->data);
#ifdef HW_LSO_ENABLE
		mtu = re8670_get_mtu(skb, cp);
		if(mtu && (skb->len-14) > mtu)
		{
			first_txd->opts4.bit.lgsen = 1;
			if (__vlan_get_protocol(skb, skb->protocol, NULL) == htons(ETH_P_PPP_SES))
				mtu -= RE8670_PPPOE_HDR_SIZE;
			first_txd->opts4.bit.lgmtu = mtu; //real MTU
		}
		else
		{
			first_txd->opts4.bit.lgsen = 0;
			first_txd->opts4.bit.lgmtu = 0;
		}
#endif
		dma_cache_wback_inv((unsigned long)skb->data, (skb->len - skb->data_len));
		first_txd->opts1.bit.fs = 1;
		first_txd->opts1.bit.ls = 0;
		first_txd->opts1.bit.crc = 1;
		first_txd->opts1.bit.ipcs = 1;
		first_len = (skb->len - skb->data_len);
		first_txd->opts1.bit.data_length = first_len;	
		first_txd->opts1.bit.eor = (*entry == (cp->re8670_tx_ring_size[ring_num] - 1)) ? 1 : 0;
		if(first_txd->opts2.bit.tx_portmask&CPU_PORT_MASK_ALL
			&& !(first_txd->opts3.bit.tx_dst_stream_id & 0x7F))
		{
			first_txd->opts3.bit.tx_dst_stream_id |= 0x1;
		}
		first_txd->opts1.bit.own = 0;
		first_entry = *entry;
#ifdef TX_RING_DEBUG
		if(cp->tx_ring_backup_debug)
		{
			DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ring_num].txDescriptor[*entry];
			memset(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
			memcpy(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], skb->data, (skb->len - skb->data_len));
			apply_to_txdesc(dbg_txd, first_txd);
			dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry];
		}
#endif
		dma_cache_wback_inv(first_txd, sizeof(struct tx_info)); 

		*entry = NEXT_TX(*entry, cp->re8670_tx_ring_size[ring_num]);
		for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
			skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];
			u32 len, mapping, new_skb_len;

#if defined(LINUX_SG_ENABLE) && defined(NIC_DESC_ACCELERATE_FOR_SG)
			u32 first_data_mapping;
			u32 next_mapping;
			skb_frag_t *next_frag = &skb_shinfo(skb)->frags[frag+1];
			if((frag+1) == skb_shinfo(skb)->nr_frags) next_frag=NULL;

			len=0;
			mapping = (u32)page_address(skb_frag_page(this_frag))+(u32)this_frag->page_offset;
			first_data_mapping=mapping;

			while(next_frag)
			{
				next_mapping=(u32)page_address(skb_frag_page(next_frag))+(u32)next_frag->page_offset;
				if(next_mapping==this_frag->size+mapping)
				{
					len += this_frag->size;
					this_frag=next_frag;
					mapping=next_mapping;
					++frag;
					next_frag=&skb_shinfo(skb)->frags[frag+1];
					if((frag+1) == skb_shinfo(skb)->nr_frags) next_frag=NULL;
				}
				else //the data is not continuously
				{
					break;
				}
			}
			len+=this_frag->size;
			//first_data_mapping |= UNCACHE_MASK; //tysu: disable uncache
			mapping=first_data_mapping;
#else
			len = this_frag->size;
			//mapping = (u32)page_address(this_frag->page)+(u32)this_frag->page_offset|UNCACHE_MASK;
			mapping = (u32)page_address(this_frag->page.p)+(u32)this_frag->page_offset; //tysu: disable uncache
#endif
			txd = &cp->tx_Mhqring[ring_num][*entry];
			if(ptxInfo)
			{
				memcpy(txd, ptxInfo, sizeof(struct tx_info));
			}
			txd->addr = (mapping);

#ifdef HW_LSO_ENABLE
			if(mtu && (skb->len-14) > mtu)
			{
				txd->opts4.bit.lgsen = 1;
				txd->opts4.bit.lgmtu = mtu; //real MTU
			}
			else
			{
				txd->opts4.bit.lgsen = 0;
				txd->opts4.bit.lgmtu = 0;
			}
#endif
			dma_cache_wback_inv((unsigned long)mapping, len);
			txd->opts1.bit.fs = 0;
			if(frag != skb_shinfo(skb)->nr_frags-1)
			{
				if (!frag && ((mtu+ETH_HLEN) > first_len) && (len <= ((mtu+ETH_HLEN) - first_len)))
				{
					new_skb = dev_alloc_skb(skb->len);
					if(new_skb)
					{
						new_skb->dev = skb->dev;
						cp->tx_skb[ring_num][first_entry].skb = new_skb;
						//first_data_mapping = (new_skb->data);
						txd->addr = (new_skb->data);
						new_skb_len = len;
						this_frag = &skb_shinfo(skb)->frags[frag];
						memcpy((new_skb->data), (u32)page_address(skb_frag_page(this_frag))+(u32)this_frag->page_offset, this_frag->size);
						while(new_skb_len <= ((mtu+ETH_HLEN) - first_len))
						{
							frag++;
							this_frag = &skb_shinfo(skb)->frags[frag];
							memcpy((new_skb->data + new_skb_len), (u32)page_address(skb_frag_page(this_frag))+(u32)this_frag->page_offset, this_frag->size);
							new_skb_len += this_frag->size;
							if (frag != skb_shinfo(skb)->nr_frags-1)
							{
								txd->opts1.bit.ls = 0;
								cp->tx_skb[ring_num][*entry].skb = (struct sk_buff *)0xffffffff;
							}
							else
							{
								txd->opts1.bit.ls = 1;
								cp->tx_skb[ring_num][*entry].skb = skb;
								break;
							}
						}
						skb_put(new_skb, new_skb_len);
						len = new_skb_len;
						dma_cache_wback_inv(new_skb->data, new_skb->len);
					}
					else
					{
						txd->opts1.bit.ls = 0;
						cp->tx_skb[ring_num][*entry].skb = (struct sk_buff *)0xffffffff;
					}
				}
				else
				{
					txd->opts1.bit.ls = 0;
					cp->tx_skb[ring_num][*entry].skb = (struct sk_buff *)0xffffffff;
				}
			}
			else
			{
				txd->opts1.bit.ls = 1;
				cp->tx_skb[ring_num][*entry].skb = skb;
			}
			txd->opts1.bit.crc = 1;
			txd->opts1.bit.ipcs = 1;
			txd->opts1.bit.data_length = len;
			txd->opts1.bit.eor = (*entry == (cp->re8670_tx_ring_size[ring_num] - 1)) ? 1 : 0;
			if(txd->opts2.bit.tx_portmask&CPU_PORT_MASK_ALL
			&& !(txd->opts3.bit.tx_dst_stream_id & 0x7F))
			{
				txd->opts3.bit.tx_dst_stream_id |= 0x1;
			}
			txd->opts1.bit.own = 1;
#ifdef TX_RING_DEBUG
			if(cp->tx_ring_backup_debug)
			{
				DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ring_num].txDescriptor[*entry];
				memset(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
				memcpy(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], mapping, len);
				apply_to_txdesc(dbg_txd, txd);
				dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry];
			}
#endif
			dma_cache_wback_inv(txd, sizeof(struct tx_info));

			//wmb();
			//msg_queue_print("i=%x %x s=%x %x l=%d\n",*(u8*)(txd->addr+0x12),*(u8*)(txd->addr+0x13),*(u8*)(txd->addr+0x32),*(u8*)(txd->addr+0x33),len);

#if defined(LINUX_SG_ENABLE) && defined(NIC_DESC_ACCELERATE_FOR_SG)			
			cp->tx_skb[ring_num][*entry].mapping = (dma_addr_t)first_data_mapping;
#else
			cp->tx_skb[ring_num][*entry].mapping = (dma_addr_t)mapping;
#endif
			cp->tx_skb[ring_num][*entry].frag = frag + 1;
			*entry = NEXT_TX(*entry, cp->re8670_tx_ring_size[ring_num]);
		}

		first_txd->opts1.bit.own = 1;
		dma_cache_wback_inv(&(first_txd->opts1), sizeof(u32));
	}
	else
	{
		new_skb = dev_alloc_skb(skb->len);
		if(new_skb)
		{
			new_skb->dev = skb->dev;
			cp->tx_skb[ring_num][*entry].skb = new_skb;
			first_txd = &cp->tx_Mhqring[ring_num][*entry];
			if(ptxInfo)
			{
				memcpy(first_txd, ptxInfo, sizeof(struct tx_info));
			}

			first_txd->addr = (new_skb->data);
			memcpy(new_skb->data, skb->data, (skb->len - skb->data_len));
#ifdef HW_LSO_ENABLE
			mtu = re8670_get_mtu(skb, cp);
			if(mtu && (skb->len-14) > mtu)
			{
				first_txd->opts4.bit.lgsen = 1;
				if (__vlan_get_protocol(skb, skb->protocol, NULL) == htons(ETH_P_PPP_SES))
					mtu -= RE8670_PPPOE_HDR_SIZE;
				first_txd->opts4.bit.lgmtu = mtu; //real MTU
			}
			else
			{
				first_txd->opts4.bit.lgsen = 0;
				first_txd->opts4.bit.lgmtu = 0;
			}
#endif
			first_txd->opts1.bit.fs = 1;
			first_txd->opts1.bit.ls = 1;
			first_txd->opts1.bit.crc = 1;
			first_txd->opts1.bit.ipcs = 1;
			first_len = (skb->len - skb->data_len);
			first_txd->opts1.bit.data_length = first_len;
			first_txd->opts1.bit.eor = (*entry == (cp->re8670_tx_ring_size[ring_num] - 1)) ? 1 : 0;
			if(first_txd->opts2.bit.tx_portmask&CPU_PORT_MASK_ALL
				&& !(first_txd->opts3.bit.tx_dst_stream_id & 0x7F))
			{
				first_txd->opts3.bit.tx_dst_stream_id |= 0x1;
			}

			for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
				skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];

				memcpy(new_skb->data + first_txd->opts1.bit.data_length, (u32)page_address(skb_frag_page(this_frag))+(u32)this_frag->page_offset, this_frag->size);
				first_txd->opts1.bit.data_length += this_frag->size;
			}
			dma_cache_wback_inv((unsigned long)new_skb->data, first_txd->opts1.bit.data_length);
			first_txd->opts1.bit.own = 1;

			if(!first_txd->opts4.bit.lgsen && first_txd->opts1.bit.data_length > RE8670_MAX_ETH_FRAME_SIZE)
			{
				if(!cp->tx_jumbo_frame_enabled || (first_txd->opts1.bit.data_length > RE8670_MAX_ETH_JUMBO_FRAME_SIZE))
				{
					dev_kfree_skb_any(new_skb);
					cp->tx_skb[ring_num][*entry].skb = skb;
					*entry = NEXT_TX(*entry, cp->re8670_tx_ring_size[ring_num]);
					return;
				}
				else
				{
					first_txd->opts1.dw &= ~(IPCS|L4CS);
				}
			}

#ifdef TX_RING_DEBUG
			if(cp->tx_ring_backup_debug)
			{
				DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ring_num].txDescriptor[*entry];
				memset(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
				memcpy(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry], skb->data, (skb->len - skb->data_len));
				apply_to_txdesc(dbg_txd, first_txd);
				dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[*entry];
			}
#endif
			dma_cache_wback_inv(first_txd, sizeof(struct tx_info));
			dev_kfree_skb_any(skb);
		}
		else
		{
			cp->tx_skb[ring_num][*entry].skb = skb;
		}
		*entry = NEXT_TX(*entry, cp->re8670_tx_ring_size[ring_num]);
	}
}
#endif
#endif

static inline void kick_tx(unsigned int gmac, int ring_num)
{
#ifdef TX_CREATE_TEST_PACKET_DEBUG
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	if(!cp->test_packet_start)
		return;
#endif

	ring_num = idx_sw2hw(ring_num);
	
	switch(ring_num) {
		case 0:
		case 1:
		case 2:
		case 3:
			RLE0787_W32(gmac, IO_CMD, RLE0787_R32(gmac, IO_CMD) | (1 << ring_num));
			break;
		case 4:
			RLE0787_W32(gmac, IO_CMD1, RLE0787_R32(gmac, IO_CMD1) | TX_POLL5);
			break;
		default:
			printk(KERN_CONT "%s %d: wrong ring num %d\n", __func__, __LINE__, ring_num);
			break;
	}
}

static inline void kick_tx_hw(unsigned int gmac, int ring_num)
{
#ifdef TX_CREATE_TEST_PACKET_DEBUG
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	if(!cp->test_packet_start)
		return;
#endif

	switch(ring_num) {
		case 0:
		case 1:
		case 2:
		case 3:
			RLE0787_W32(gmac, IO_CMD, RLE0787_R32(gmac, IO_CMD) | (1 << ring_num));
			break;
		case 4:
			RLE0787_W32(gmac, IO_CMD1, RLE0787_R32(gmac, IO_CMD1) | TX_POLL5);
			break;
		default:
			printk(KERN_CONT "%s %d: wrong ring num %d\n", __func__, __LINE__, ring_num);
			break;
	}
}

static inline void apply_to_txdesc(DMA_TX_DESC  *txd, struct tx_info *pTxInfo)
{
	txd->addr = pTxInfo->addr;
	txd->opts2 = pTxInfo->opts2.dw;
	txd->opts3 = pTxInfo->opts3.dw;
	txd->opts4 = pTxInfo->opts4.dw;
	//must be last write....
	wmb();
	txd->opts1 = pTxInfo->opts1.dw;
}

__IRAM_NIC
void do_txInfoMask(struct tx_info* pTxInfo, struct tx_info* ptx, struct tx_info* ptxMask)
{
	if(ptxMask && ptx){
		if(ptxMask->opts1.dw)
		{
			pTxInfo->opts1.dw &= (~ptxMask->opts1.dw);
			pTxInfo->opts1.dw |= (ptx->opts1.dw & ptxMask->opts1.dw);
		}
		if(ptxMask->opts2.dw)
		{
			pTxInfo->opts2.dw &= (~ptxMask->opts2.dw);
			pTxInfo->opts2.dw |= (ptx->opts2.dw & ptxMask->opts2.dw);
		}
		if(ptxMask->opts3.dw)
		{
			pTxInfo->opts3.dw &= (~ptxMask->opts3.dw);
			pTxInfo->opts3.dw |= (ptx->opts3.dw & ptxMask->opts3.dw);
		}
		if(ptxMask->opts4.dw)
		{
			pTxInfo->opts4.dw &= (~ptxMask->opts4.dw);
			pTxInfo->opts4.dw |= (ptx->opts4.dw & ptxMask->opts4.dw);
		}
	}
}

void re8686_direct_tx_to_cpu_port(struct tx_info *tx_info_ptr)
{
	if((tx_info_ptr->opts2.bit.tx_portmask & CPU_PORT_MASK_ALL)
		&& !(tx_info_ptr->opts3.bit.tx_dst_stream_id & 0x7F)
	)
	{
		tx_info_ptr->opts3.bit.tx_dst_stream_id |= 0x1;
	}
}

#ifdef TX_BURST_PACKET_SEND
#define UNCACHE(x)  (*((volatile u32*)(((u32)x)|0xa0000000)))
__IRAM_NIC void _nic_burst_wq_func(burst_tx_info_t *pBurstInfo)
{
	unsigned entry;
	u32 eor;
	DMA_TX_DESC  *txd;
	unsigned int ring_counter=0;
	
	GMAC_SPIN_LOCK(&pBurstInfo->cp->tx_lock);

	checkTXDesc(pBurstInfo->ringNum, pBurstInfo->cp->gmac);

	for(;ring_counter<pBurstInfo->cp->re8670_tx_ring_size[pBurstInfo->ringNum]-1;pBurstInfo->burst_index++,ring_counter++)
	{
		entry = pBurstInfo->cp->tx_Mhqhead[pBurstInfo->ringNum];
		eor = (entry == (pBurstInfo->cp->re8670_tx_ring_size[pBurstInfo->ringNum] - 1)) ? RingEnd : 0;		
		txd = &pBurstInfo->cp->tx_Mhqring[pBurstInfo->ringNum][entry];    

		if(pBurstInfo->burst_delay!=0){
			if(unlikely(pBurstInfo->burst_sentPacketCount==0)){				
				REG32(0xb8003290)=0xffffffff;
			}
		}
		if(UNCACHE(&txd->opts1)&DescOwn)
			break;				//stop since gmac could not catch up

		if((pBurstInfo->burst_index>=pBurstInfo->burst_number)&&(pBurstInfo->burst_number>=0)){
			pBurstInfo->cp->tx_skb[pBurstInfo->ringNum][entry].skb = NULL;
			break;				//stop since we finished
		}
		
		//apply to txdesc
		if(eor)
			pBurstInfo->txInfo.opts1.dw |= eor;
		else
			pBurstInfo->txInfo.opts1.dw &= ~(RingEnd);
		apply_to_txdesc(txd, &pBurstInfo->txInfo);
		dma_cache_wback_inv((unsigned long)txd, sizeof(DMA_TX_DESC));
		pBurstInfo->cp->tx_skb[pBurstInfo->ringNum][entry].skb = 0xffffffff;
		pBurstInfo->cp->tx_skb[pBurstInfo->ringNum][entry].mapping = NULL;
		pBurstInfo->cp->tx_skb[pBurstInfo->ringNum][entry].frag = 0;

		//UNCACHE(&txd->opts1) = (pBurstInfo->txInfo.opts1.dw | DescOwn | eor);
		//printk(KERN_CONT "opts1=0x%x txd->addr=0x%x  opts2=0x%x  opts3=0x%x\n",txd->opts1,txd->addr,txd->opts2,txd->opts3 );

		entry = NEXT_TX(entry, pBurstInfo->cp->re8670_tx_ring_size[pBurstInfo->ringNum]);
		pBurstInfo->cp->tx_Mhqhead[pBurstInfo->ringNum] = entry;

		if(pBurstInfo->burst_delay!=0){
			pBurstInfo->burst_sentPacketCount++;
			if(pBurstInfo->burst_sentPacketCount>=pBurstInfo->burst_packetNumberPerSecond){
				pBurstInfo->burst_sentPacketCount=0;
				kick_tx(pBurstInfo->cp->gmac, pBurstInfo->ringNum);
#ifdef CONFIG_RTK_PTOOL_LA_BY_GPIO
				LA_GPIO_HIGH(LA_GPIO_AUX7);
#endif				
				while(1){
					if(REG32(0xb8003294)>=pBurstInfo->burst_delay) break;
				}
#ifdef CONFIG_RTK_PTOOL_LA_BY_GPIO
				LA_GPIO_LOW(LA_GPIO_AUX7);
#endif				
			}
		}
	}

	mb();

	kick_tx(pBurstInfo->cp->gmac, pBurstInfo->ringNum);

	GMAC_SPIN_UNLOCK(&pBurstInfo->cp->tx_lock);

	if((pBurstInfo->burst_index < pBurstInfo->burst_number)||(pBurstInfo->burst_number==-1)){
		tasklet_hi_schedule(&pBurstInfo->burst_tasklets);
	}else{
		//unsigned long burst_finish_jiffies=jiffies;
		//printk(KERN_CONT "send finished: %d ms!",(burst_finish_jiffies-burst_begin_jiffies)*(1000/CONFIG_HZ));
		kfree(pBurstInfo->burst_buffer);
		pBurstInfo->burst_buffer=NULL;
	}
}


bool rtk_nic_isPhyDev(struct net_device *dev)
{
	int i;
	for(i=1; i<TOTAL_RTL8686_DEV_NUM ;i++)
	{
		if(rtl8686_dev_table[i].dev_instant==dev)
			return TRUE;
	}
	return FALSE;
}


int re8686_send_with_txInfo_and_mask_burst(char *pktdata,int len, struct tx_info* ptxInfo, int ring_num, struct tx_info* ptxInfoMask,int burstNum,int nicSendRate)
{
	burst_tx_info_t *pBurstInfo;
	struct net_device *dev = ROOTDEV;
	struct re_private_root *root_cp = DEV2CP(dev);
	struct re_private *cp;
	
	//initialization
	if(ptxInfo){
		cp = re_private_data_root.re_private_data_ptr[ptxInfo->opts3.bit.gmac_id];
		pBurstInfo=&(root_cp->re_private_data_ptr[ptxInfo->opts3.bit.gmac_id]->burst_tx_info);
		pBurstInfo->cp=root_cp->re_private_data_ptr[ptxInfo->opts3.bit.gmac_id];
		pBurstInfo->ringNum=ring_num;
	}else{
		cp = re_private_data_root.re_private_data_ptr[0];
		pBurstInfo=&cp->burst_tx_info;
		pBurstInfo->cp=cp;
		pBurstInfo->ringNum=ring_num;
	}

	//stop the burst if the buffer is not NULL
	if(pBurstInfo->burst_buffer){
		if(burstNum==0){
			pBurstInfo->burst_number=0;
			printk(KERN_CONT "Stop sending.\n");
		} else {
			printk(KERN_CONT "The last job has not been completed yet.\n");
		}
		return -1;
	}else if(burstNum==0){
		//do nothing
		return -1;
	}

	GMAC_SPIN_LOCK(&pBurstInfo->cp->tx_lock);

	if(unlikely(pBurstInfo->cp->eth_close == GMAC_TRUE)) {
		GMAC_SPIN_UNLOCK(&pBurstInfo->cp->tx_lock);
		return -1;
	}

	if (unlikely(TX_HQBUFFS_AVAIL(pBurstInfo->cp,ring_num) <= 1))	{
		GMAC_SPIN_UNLOCK(&pBurstInfo->cp->tx_lock);
		return -1;
	}
	
	GMAC_SPIN_UNLOCK(&pBurstInfo->cp->tx_lock);

	pBurstInfo->burst_buffer=kmalloc(len,GFP_ATOMIC);
	if(!pBurstInfo->burst_buffer){
		printk(KERN_CONT "no mem for burst buffer..\n");
		return -1;
	}	

	//timer/counter 8
	REG32(0xb8003298)=0x11000014;  // count per 100ns	
	pBurstInfo->burst_sentPacketCount=0;
	pBurstInfo->burst_number=burstNum;
	pBurstInfo->burst_index=0;

	if(nicSendRate==100){
		pBurstInfo->burst_delay=0;
		//printk(KERN_CONT "nicSendRate=100%% delayPerBurst=0 ns\n");
	}else{		
		unsigned int sendPktPerSec;
		sendPktPerSec=nicSendRate*(10000000>>3)/(len+24);		//24=ifg 20byte + crc 4byte
		pBurstInfo->burst_packetNumberPerSecond=len>1518?1:110-((len*62+6280)/1000);	
		pBurstInfo->burst_delay=1000000000/(sendPktPerSec/pBurstInfo->burst_packetNumberPerSecond)/100;
		//printk(KERN_CONT "nicSendRate=%d%% SendPktPerSec=%d BurstPkt=%d delayPerBurst=%d00 ns\n",nicSendRate,sendPktPerSec,pBurstInfo->burst_packetNumberPerSecond,pBurstInfo->burst_delay);
	}
	
	memcpy(pBurstInfo->burst_buffer,pktdata,len);
	//printk(KERN_CONT "Packet Length=%d Send packet count=%d\n",len, burstNum);
	dma_cache_wback_inv((unsigned long)pBurstInfo->burst_buffer,len);

	memset(&pBurstInfo->txInfo,0,sizeof(struct tx_info));
	pBurstInfo->txInfo.addr = (u32)pBurstInfo->burst_buffer;
	do_txInfoMask(&pBurstInfo->txInfo, ptxInfo, ptxInfoMask);
	pBurstInfo->txInfo.opts1.dw |= (len|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);
	if(len>RE8670_MAX_ETH_FRAME_SIZE)pBurstInfo->txInfo.opts1.dw &= ~(IPCS|L4CS);

	tasklet_hi_schedule(&pBurstInfo->burst_tasklets);

	return 0;
}
#else
int re8686_send_with_txInfo_and_mask_burst(char *pktdata,int len, struct tx_info* ptxInfo, int ring_num, struct tx_info* ptxInfoMask,int burstNum,int nicSendRate)
{
	printk(KERN_CONT "Please enable TX_BURST_PACKET_SEND in NIC driver.\n");
	return 0;
}
#endif

#ifdef TX_BACKUP_RING
int nic_available_tx_ring_get(struct re_private *cp);
#endif

int re8686_send_with_txInfo_and_mask(struct sk_buff *skb, struct tx_info* ptxInfo, int ring_num, struct tx_info* ptxInfoMask)
{
	struct net_device *dev = ROOTDEV;
	struct re_private_root *root_cp = DEV2CP(dev);
	unsigned int gmac = ptxInfo->opts3.bit.gmac_id;
	struct re_private *cp;
	unsigned entry;
	u32 eor;
	struct tx_info local_txInfo={{{0}},0,{{0}},{{0}}};
	int ringNum=ring_num;
#ifdef HW_LSO_ENABLE
	int mtu;
#endif
#ifdef TX_BACKUP_RING
	u32 tx_backup_ring_idx;
	u32 tx_backup_ring_mask;
#endif
#ifdef TX_BACKUP_GMAC
	u32 tx_backup_gmac_idx;
	u32 tx_backup_gmac_mask;
#endif

	if (skb->cb[RTK_NIC_TX_HASH_CB_NUMBER])
		gmac = (skb->cb[RTK_NIC_TX_HASH_CB_NUMBER] - 1) % MAX_GMAC_NUM;

	if (unlikely(gmac >= MAX_GMAC_NUM))
		gmac = 0;

	cp = root_cp->re_private_data_ptr[gmac];
#ifdef TX_BACKUP_RING
	tx_backup_ring_mask = cp->tx_multiring_bitmap;
#endif
#ifdef TX_BACKUP_GMAC
	tx_backup_gmac_mask = root_cp->gmac_enable_mask;
#endif

	if(unlikely(gmac >= MAX_GMAC_NUM) || unlikely(cp->gmac_enabled != GMAC_TRUE)
#ifdef TX_BURST_PACKET_SEND
		|| unlikely(cp->burst_tx_info.burst_buffer)
#endif
		) {
		dev_kfree_skb_any(skb);
		TX_TRACE(0, cp->debug_enable, cp->debug_times, "GMAC%d disabled, free skb.\n", gmac);
		return NET_XMIT_DROP;
	}

#ifdef TX_BACKUP_GMAC
TX_BACKUP_GMAC_RETRY:
#endif

	GMAC_SPIN_LOCK(&cp->tx_lock);
#ifdef HWNAT_CUSTOMIZE	
		if(re8686_tx_ring_customized[gmac][ring_num])ringNum=MAX_TXRING_NUM-1;	
#endif
	checkTXDesc(ringNum, gmac);

	if(unlikely(cp->eth_close == GMAC_TRUE)) {
		DEVPRIV(skb->dev)->net_stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		GMAC_SPIN_UNLOCK(&cp->tx_lock);
		return NET_XMIT_DROP;
	}

#ifdef TX_BACKUP_RING
TX_BACKUP_RING_RETRY:
#endif

	if((skb->dev==NULL) || !(skb->dev->rtk_priv_flags&(RTK_IFF_DOMAIN_ELAN|RTK_IFF_DOMAIN_WAN)))
		skb->dev=dev;
	
	//save ptxInfo, now we only need to save opts1 and opts2
	memcpy(&local_txInfo, ptxInfo, sizeof(struct tx_info));
	
	ETHDBG_PRINT(gmac, cp->debug_enable, cp->debug_times, RTL8686_SKB_TX, "Tx dev=%s nr_frags=%d\n", dev->name, skb_shinfo(skb)->nr_frags);
	SKB_DBG(skb, cp->debug_enable, cp->debug_times, RTL8686_SKB_TX);
	cp->cp_stats.tx_sw_num++;
	checkTXDesc(ringNum, gmac);

	if (unlikely(TX_HQBUFFS_AVAIL(cp,ringNum) <= (skb_shinfo(skb)->nr_frags + 1)))
	{
		kick_tx(gmac, ringNum);
#ifdef TX_BACKUP_RING
		tx_backup_ring_mask &= ~(1<<ringNum);
		for (tx_backup_ring_idx=0 ; tx_backup_ring_idx<MAX_TXRING_NUM ; tx_backup_ring_idx++) {
			if (!(tx_backup_ring_mask&(1<<tx_backup_ring_idx)))
				continue;

			if (TX_HQBUFFS_AVAIL(cp,tx_backup_ring_idx) <= (skb_shinfo(skb)->nr_frags + 1))
				continue;

			ringNum=tx_backup_ring_idx;
			goto TX_BACKUP_RING_RETRY;
		}

		ringNum=nic_available_tx_ring_get(cp);
		if (ringNum != -1)
			goto TX_BACKUP_RING_RETRY;
		else
			ringNum=0;
#endif
#ifdef TX_BACKUP_GMAC
		tx_backup_gmac_mask &= ~(1<<gmac);
		for (tx_backup_gmac_idx=0 ; tx_backup_gmac_idx<MAX_GMAC_NUM ; tx_backup_gmac_idx++) {
			if (!(tx_backup_gmac_mask&(1<<tx_backup_gmac_idx)))
				continue;

			GMAC_SPIN_UNLOCK(&cp->tx_lock);
			cp = root_cp->re_private_data_ptr[tx_backup_gmac_idx];
			gmac = tx_backup_gmac_idx;
			goto TX_BACKUP_GMAC_RETRY;
		}
#endif
		DEVPRIV(skb->dev)->net_stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		cp->cp_stats.tx_no_desc++;
		netif_stop_queue(dev);	
		kick_tx(gmac, ringNum);
		GMAC_SPIN_UNLOCK(&cp->tx_lock);
		return NET_XMIT_DROP;
	}
			
	entry = cp->tx_Mhqhead[ringNum];    
	eor = (entry == (cp->re8670_tx_ring_size[ringNum] - 1)) ? RingEnd : 0;
	if (skb_shinfo(skb)->nr_frags == 0) {
		u32 len;
		DMA_TX_DESC  *txd;
		txd = &cp->tx_Mhqring[ringNum][entry];    

		len = skb->len;

		if(len < RE8686_HW_SMALLEST_DATA_LEN)
		{
			DEVPRIV(skb->dev)->net_stats.tx_dropped++;
			dev_kfree_skb_any(skb);
			cp->cp_stats.tooshort++;
			TX_TRACE(gmac, cp->debug_enable, cp->debug_times, "skb len too short dropped (len=%d).\n", len);
			GMAC_SPIN_UNLOCK(&cp->tx_lock);
			return NET_XMIT_DROP;
		}

		if(len > 0)		
		// Kaohj --- invalidate DCache before NIC DMA
			dma_cache_wback_inv((unsigned long)skb->data, len);

		//default setting, always need this
		local_txInfo.addr = (u32)(skb->data);

		//[step1] opts1 clear & set init value
		local_txInfo.opts1.dw &= ~(RingEnd|0x1ffff|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);
		local_txInfo.opts1.dw |= (eor|len|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);

		//[step2] opts1 might be changed by some additional setting or parameter ptxInfo
		//plz put tx additional setting into this function
		tx_additional_setting(skb, dev, &local_txInfo);
		do_txInfoMask(&local_txInfo, ptxInfo, ptxInfoMask);

		//[step3] HW LSO or jumbo frame decision
#ifdef CONFIG_REALTEK_HW_LSO
		local_txInfo.opts4.bit.lgsen = 0;
		mtu = re8670_get_mtu(skb, cp);
		if(skb_is_gso(skb) && mtu && (skb->len-14) > mtu)
		{
			local_txInfo.opts4.bit.lgsen = 1;
			if (__vlan_get_protocol(skb, skb->protocol, NULL) == htons(ETH_P_PPP_SES))
				mtu -= RE8670_PPPOE_HDR_SIZE;
			local_txInfo.opts4.bit.lgmtu = mtu; //real MTU
		}
		else
#endif
		if(skb->len > RE8670_MAX_ETH_FRAME_SIZE)
		{
			if(!cp->tx_jumbo_frame_enabled || (skb->len >= RE8670_MAX_ETH_JUMBO_FRAME_SIZE))
			{
				//memDump(skb->data, (skb->len > NUM2PRINT)?NUM2PRINT : skb->len, "TX SKB LEN > 1522", RTL8686_MEM_DUMP_FORMAT_ALL);
				DEVPRIV(skb->dev)->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				GMAC_SPIN_UNLOCK(&cp->tx_lock);
				return NET_XMIT_DROP;
			}
			else
			{
				local_txInfo.opts1.dw &= ~(IPCS|L4CS);
			}
		}

		//[step4] support CPU port direct Tx 
		if(local_txInfo.opts2.bit.tx_portmask&CPU_PORT_MASK_ALL)
		{
			re8686_direct_tx_to_cpu_port(&local_txInfo);
		}

		TXINFO_DBG(cp, &local_txInfo);
		
		//apply to txdesc
		apply_to_txdesc(txd, &local_txInfo);
#ifdef TX_RING_DEBUG
		if(cp->tx_ring_backup_debug)
		{
			DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ringNum].txDescriptor[entry];
			memset(&cp->rtl8686_tx_ring_debug[ringNum].dataBuffer[entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
			memcpy(&cp->rtl8686_tx_ring_debug[ringNum].dataBuffer[entry], skb->data, skb->len);
			apply_to_txdesc(dbg_txd, &local_txInfo);
			dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ringNum].dataBuffer[entry];
		}
#endif
		cp->tx_skb[ringNum][entry].skb = skb;
		cp->tx_skb[ringNum][entry].mapping = (dma_addr_t)(skb->data);
		cp->tx_skb[ringNum][entry].frag = 0;

		TX_TRACE(gmac, cp->debug_enable, cp->debug_times, "FROM_FWD[%x] DA=%02x:%02x:%02x:%02x:%02x:%02x SA=%02x:%02x:%02x:%02x:%02x:%02x ethtype=%04x len=%d VlanAct=%d Vlan=%d Pri=%d ExtSpa=%d TxPmsdk=0x%x L34Keep=%x PON_SID=%d\n",
		(u32)skb&0xffff,
		skb->data[0],skb->data[1],skb->data[2],skb->data[3],skb->data[4],skb->data[5],
		skb->data[6],skb->data[7],skb->data[8],skb->data[9],skb->data[10],skb->data[11],
		(skb->data[12]<<8)|skb->data[13],skb->len,
		local_txInfo.opts2.bit.tx_cvlan_action,
		local_txInfo.opts2.bit.cvlan_vidh<<8|local_txInfo.opts2.bit.cvlan_vidl,
		local_txInfo.opts2.bit.cvlan_prio,
		local_txInfo.opts3.bit.extspa,
		local_txInfo.opts2.bit.tx_portmask,
		local_txInfo.opts3.bit.l34_keep,
		local_txInfo.opts3.bit.tx_dst_stream_id);

		entry = NEXT_TX(entry, cp->re8670_tx_ring_size[ringNum]);
	} 
#ifdef CONFIG_REALTEK_HW_LSO
	else {
		re8670_mFrag_xmit(skb, cp, &entry, NULL, ringNum);
	}
#endif

	cp->tx_Mhqhead[ringNum] = entry;

	if (unlikely(TX_HQBUFFS_AVAIL(cp,ringNum) <= 1))
		netif_stop_queue(dev);

	//for memory controller's write buffer
	//write_buffer_flush();
	mb();
	//cp->cp_stats.tx_hw_num++;
	cp->cp_stats.tx_hw_num_ring[ringNum]++;
	TXDBG_TIMES_UPDATE(cp);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,4,0)
	dev->trans_start = jiffies;
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5,1,0)
	netdev_pick_tx(dev, skb, NULL)->trans_start = jiffies;
#else
	netdev_core_pick_tx(dev, skb, NULL)->trans_start = jiffies;
#endif
	sk_pacing_shift_update(skb->sk, 8);
	GMAC_SPIN_UNLOCK(&cp->tx_lock);

	kick_tx(gmac, ringNum);

	return NETDEV_TX_OK;
}

__IRAM_NIC
int re8686_send_with_txInfo(struct sk_buff *skb, struct tx_info* ptxInfo, int ring_num) 
{
	struct net_device *dev = ROOTDEV;
	struct re_private_root *root_cp = DEV2CP(dev);	
	unsigned int gmac = ptxInfo->opts3.bit.gmac_id;
	struct re_private *cp;
	unsigned entry;
	u32 eor;
#ifdef HW_LSO_ENABLE
	int mtu;
#endif
	int ringNum=ring_num;
#ifdef TX_BACKUP_RING
	u32 tx_backup_ring_idx;
	u32 tx_backup_ring_mask;
#endif
#ifdef TX_BACKUP_GMAC
	u32 tx_backup_gmac_idx;
	u32 tx_backup_gmac_mask;
#endif

	if (skb->cb[RTK_NIC_TX_HASH_CB_NUMBER])
		gmac = (skb->cb[RTK_NIC_TX_HASH_CB_NUMBER] - 1) % MAX_GMAC_NUM;

	if (unlikely(gmac >= MAX_GMAC_NUM))
		gmac = 0;

	cp = root_cp->re_private_data_ptr[gmac];
#ifdef TX_BACKUP_RING
	tx_backup_ring_mask = cp->tx_multiring_bitmap;
#endif
#ifdef TX_BACKUP_GMAC
	tx_backup_gmac_mask = root_cp->gmac_enable_mask;
#endif

	if(unlikely(gmac >= MAX_GMAC_NUM) || unlikely(cp->gmac_enabled != GMAC_TRUE)
#ifdef TX_BURST_PACKET_SEND
		|| unlikely(cp->burst_tx_info.burst_buffer)
#endif
		) {
		dev_kfree_skb_any(skb);
		TX_TRACE(0, cp->debug_enable, cp->debug_times, "GMAC%d disabled, free skb.\n", gmac);
		return -1;
	}
	
#ifdef TX_BACKUP_GMAC
	TX_BACKUP_GMAC_RETRY:
#endif
	
	GMAC_SPIN_LOCK(&cp->tx_lock);

	if(unlikely(cp->eth_close == GMAC_TRUE)
#ifdef TX_CREATE_TEST_PACKET_DEBUG
		|| cp->test_packet_start
#endif
		) 
	{
		DEVPRIV(skb->dev)->net_stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		GMAC_SPIN_UNLOCK(&cp->tx_lock);
		return NET_XMIT_DROP;
	}

#ifdef TX_BACKUP_RING
TX_BACKUP_RING_RETRY:
#endif

	if((skb->dev==NULL) || !(skb->dev->rtk_priv_flags&(RTK_IFF_DOMAIN_ELAN|RTK_IFF_DOMAIN_WAN)))
		skb->dev=dev;
	
	ETHDBG_PRINT(gmac, cp->debug_enable, cp->debug_times, RTL8686_SKB_TX, "Tx dev=%s nr_frags=%d\n", skb->dev->name, skb_shinfo(skb)->nr_frags);
	SKB_DBG(skb, cp->debug_enable, cp->debug_times, RTL8686_SKB_TX);
	cp->cp_stats.tx_sw_num++;
	checkTXDesc(ringNum, gmac);

	if (unlikely(TX_HQBUFFS_AVAIL(cp,ringNum) <= (skb_shinfo(skb)->nr_frags + 1)))
	{
		kick_tx(gmac, ringNum);
#ifdef TX_BACKUP_RING
		tx_backup_ring_mask &= ~(1<<ringNum);
		for (tx_backup_ring_idx=0 ; tx_backup_ring_idx<MAX_TXRING_NUM ; tx_backup_ring_idx++) {
			if (!(tx_backup_ring_mask&(1<<tx_backup_ring_idx)))
				continue;

			if (TX_HQBUFFS_AVAIL(cp,tx_backup_ring_idx) <= (skb_shinfo(skb)->nr_frags + 1)) {
				continue;
			}

			ringNum=tx_backup_ring_idx;
			goto TX_BACKUP_RING_RETRY;
		}
	
		ringNum=nic_available_tx_ring_get(cp);
		if (ringNum != -1)
			goto TX_BACKUP_RING_RETRY;
		else
			ringNum=0;
#endif
#ifdef TX_BACKUP_GMAC
		tx_backup_gmac_mask &= ~(1<<gmac);
		for (tx_backup_gmac_idx=0 ; tx_backup_gmac_idx<MAX_GMAC_NUM ; tx_backup_gmac_idx++) {
			if (!(tx_backup_gmac_mask&(1<<tx_backup_gmac_idx)))
				continue;

			GMAC_SPIN_UNLOCK(&cp->tx_lock);
			cp = root_cp->re_private_data_ptr[tx_backup_gmac_idx];
			gmac = tx_backup_gmac_idx;
			goto TX_BACKUP_GMAC_RETRY;
		}
#endif
		DEVPRIV(skb->dev)->net_stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		cp->cp_stats.tx_no_desc++;
		netif_stop_queue(dev);
		GMAC_SPIN_UNLOCK(&cp->tx_lock);
		return NET_XMIT_DROP;
	}

	entry = cp->tx_Mhqhead[ringNum];	 
	eor = (entry == (cp->re8670_tx_ring_size[ringNum] - 1)) ? RingEnd : 0;
	if (skb_shinfo(skb)->nr_frags == 0) {
		u32 len;
		DMA_TX_DESC  *txd;
		txd = &cp->tx_Mhqring[ringNum][entry];    

		len = skb->len;

		if(len < RE8686_HW_SMALLEST_DATA_LEN)
		{
			DEVPRIV(skb->dev)->net_stats.tx_dropped++;
			dev_kfree_skb_any(skb);
			cp->cp_stats.tooshort++;
			TX_TRACE(gmac, cp->debug_enable, cp->debug_times, "skb len too short dropped (len=%d).\n", len);
			GMAC_SPIN_UNLOCK(&cp->tx_lock);
			return NET_XMIT_DROP;
		}
		
		if(len > 0) 	
		// Kaohj --- invalidate DCache before NIC DMA
			dma_cache_wback_inv((unsigned long)skb->data, len);

		//default setting, always need this
		ptxInfo->addr = (u32)(skb->data);

		//[step1] opts1 clear & set init value
		ptxInfo->opts1.dw &= ~(RingEnd|0x1ffff|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);
		ptxInfo->opts1.dw |= (eor|len|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);

		//[step2] HW LSO or jumbo frame decision
#ifdef CONFIG_REALTEK_HW_LSO
		ptxInfo->opts4.bit.lgsen = 0;
		mtu = re8670_get_mtu(skb, cp);
		if(skb_is_gso(skb) && mtu && (skb->len-14) > mtu)
		{
			ptxInfo->opts4.bit.lgsen = 1;
			if (__vlan_get_protocol(skb, skb->protocol, NULL) == htons(ETH_P_PPP_SES))
				mtu -= RE8670_PPPOE_HDR_SIZE;
			ptxInfo->opts4.bit.lgmtu = mtu; //real MTU
		}
		else
#endif
		if(skb->len > RE8670_MAX_ETH_FRAME_SIZE)
		{
			if(!cp->tx_jumbo_frame_enabled || (skb->len >= RE8670_MAX_ETH_JUMBO_FRAME_SIZE))
			{
				//memDump(skb->data, (skb->len > NUM2PRINT)?NUM2PRINT : skb->len, "TX SKB LEN > 1522", RTL8686_MEM_DUMP_FORMAT_ALL);
				DEVPRIV(skb->dev)->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				GMAC_SPIN_UNLOCK(&cp->tx_lock);
				return NET_XMIT_DROP;
			}
			else
			{
				ptxInfo->opts1.dw &= ~(IPCS|L4CS);
			}
		}

		//[step3] support CPU port direct Tx 
		if(ptxInfo->opts2.bit.tx_portmask&CPU_PORT_MASK_ALL)
		{
			re8686_direct_tx_to_cpu_port(ptxInfo);
		}
		
		ptxInfo->opts2.bit.cputag = 1;

		TXINFO_DBG(cp, ptxInfo);
		
		//apply to txdesc
		apply_to_txdesc(txd, ptxInfo);
#ifdef TX_RING_DEBUG
		if(cp->tx_ring_backup_debug)
		{
			DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ringNum].txDescriptor[entry];
			memset(&cp->rtl8686_tx_ring_debug[ringNum].dataBuffer[entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
			memcpy(&cp->rtl8686_tx_ring_debug[ringNum].dataBuffer[entry], skb->data, skb->len);
			apply_to_txdesc(dbg_txd, ptxInfo);
			dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ringNum].dataBuffer[entry];
		}
#endif
		cp->tx_skb[ringNum][entry].skb = skb;
		cp->tx_skb[ringNum][entry].mapping = (dma_addr_t)(skb->data);
		cp->tx_skb[ringNum][entry].frag = 0;

		TX_TRACE(gmac, cp->debug_enable, cp->debug_times, "FROM_FC[%x] DA=%02x:%02x:%02x:%02x:%02x:%02x SA=%02x:%02x:%02x:%02x:%02x:%02x ethtype=%04x len=%d VlanAct=%d Vlan=%d Pri=%d ExtSpa=%d TxPmsdk=0x%x L34Keep=%x PON_SID=%d\n",
		(u32)skb&0xffff,
		skb->data[0],skb->data[1],skb->data[2],skb->data[3],skb->data[4],skb->data[5],
		skb->data[6],skb->data[7],skb->data[8],skb->data[9],skb->data[10],skb->data[11],
		(skb->data[12]<<8)|skb->data[13],skb->len,
		ptxInfo->opts2.bit.tx_cvlan_action,
		ptxInfo->opts2.bit.cvlan_vidh<<8|ptxInfo->opts2.bit.cvlan_vidl,
		ptxInfo->opts2.bit.cvlan_prio,
		ptxInfo->opts3.bit.extspa,
		ptxInfo->opts2.bit.tx_portmask,
		ptxInfo->opts3.bit.l34_keep,
		ptxInfo->opts3.bit.tx_dst_stream_id);

		entry = NEXT_TX(entry, cp->re8670_tx_ring_size[ringNum]);
	} 
#ifdef CONFIG_REALTEK_HW_LSO
	else {
		re8670_mFrag_xmit(skb, cp, &entry, ptxInfo, ringNum);
	}
#endif

	cp->tx_Mhqhead[ringNum] = entry;

	if (unlikely(TX_HQBUFFS_AVAIL(cp,ringNum) <= 1))
		netif_stop_queue(dev);

	//for memory controller's write buffer
	//write_buffer_flush();
	mb();
	//cp->cp_stats.tx_hw_num++;
	cp->cp_stats.tx_hw_num_ring[ringNum]++;
	TXDBG_TIMES_UPDATE(cp);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,4,0)
	dev->trans_start = jiffies;
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5,1,0)
	netdev_pick_tx(dev, skb, NULL)->trans_start = jiffies;
#else
	netdev_core_pick_tx(dev, skb, NULL)->trans_start = jiffies;
#endif
	sk_pacing_shift_update(skb->sk, 8);
	GMAC_SPIN_UNLOCK(&cp->tx_lock);

	kick_tx(gmac, ringNum);

	return NETDEV_TX_OK;
}

#if defined(CONFIG_RTK_NIC_HWLOOKUP_DEAMSDU_OFFLOAD)
extern uint32 _rtk_fc_wlan_rx_lookup_gmac(unsigned char *da);

unsigned long amsdu_counter=0;
int rtk_nic_amsdu_cnt_get(unsigned long *pAmsduCnt)
{
	*pAmsduCnt=amsdu_counter;
	return 0;
}
int rtk_nic_amsdu_cnt_clear(void)
{
	amsdu_counter=0;
	return 0;
}
EXPORT_SYMBOL(rtk_nic_amsdu_cnt_get);
EXPORT_SYMBOL(rtk_nic_amsdu_cnt_clear);

__IRAM_NIC
int re8686_wifi_hwlookup_deamsdu(struct sk_buff *skb, struct tx_info* ptxInfo, int ring_num)
{
	unsigned char *pAmsdu_data=skb->data;
	int snap_len=ntohs(*(unsigned short *)(pAmsdu_data+ETH_ALEN+ETH_ALEN));

	//normal single skb, not amsdu packet.
	if(snap_len >= 0x0800){
#ifndef CONFIG_FC_WIFI_RX_GMAC_TRUNKING_SUPPORT
		if (ptxInfo->opts3.bit.gmac_id != 2)
#endif
		{
			ptxInfo->opts3.bit.gmac_id = _rtk_fc_wlan_rx_lookup_gmac(pAmsdu_data);
		}
		return re8686_send_with_txInfo(skb, ptxInfo, ring_num);
	}else{
		struct net_device *dev = ROOTDEV;
		struct re_private_root *root_cp = DEV2CP(dev);
		unsigned int gmac = ptxInfo->opts3.bit.gmac_id;
		struct re_private *cp;// = root_cp->re_private_data_ptr[gmac];
		unsigned int entry;
		u32 eor;
#ifdef HW_LSO_ENABLE
		int mtu;
#endif
		int ringNum=ring_num;

		DMA_TX_DESC  *txd;
#if defined(CONFIG_RTK_NIC_HWLOOKUP_SNAP_TRANSFORM_BY_TX_DESC)
		int DESC_NEED_NUM=2;
#else
		int DESC_NEED_NUM=1;
#endif
		struct sk_buff *txq_skb;
		unsigned char padding;
		int amsdu_len,offset,keepLoop=1;
		unsigned char *pSnap_data;

		amsdu_len=skb->len;

		while(keepLoop){
#ifndef CONFIG_FC_WIFI_RX_GMAC_TRUNKING_SUPPORT
			if (ptxInfo->opts3.bit.gmac_id != 2)
#endif
			{
				gmac = _rtk_fc_wlan_rx_lookup_gmac(pAmsdu_data);
			}
			cp=root_cp->re_private_data_ptr[gmac];

			if(unlikely(gmac >= MAX_GMAC_NUM) || unlikely(cp->gmac_enabled != GMAC_TRUE)
#ifdef TX_BURST_PACKET_SEND
				|| unlikely(cp->burst_tx_info.burst_buffer)
#endif
				) {
				dev_kfree_skb_any(skb);
				TX_TRACE(0, cp->debug_enable, cp->debug_times, "GMAC%d disabled, free skb.\n", gmac);
				return -1;
			}

			GMAC_SPIN_LOCK(&cp->tx_lock);
#ifdef HWNAT_CUSTOMIZE
			if(re8686_tx_ring_customized[gmac][ring_num])ringNum=MAX_TXRING_NUM-1;
#endif

			if((skb->dev==NULL) || !(skb->dev->priv_flags&(RTK_IFF_DOMAIN_ELAN|RTK_IFF_DOMAIN_WAN)))
				skb->dev=dev;

			ETHDBG_PRINT(gmac, cp->debug_enable, cp->debug_times, RTL8686_SKB_TX, "Tx dev=%s\n", skb->dev->name);
			SKB_DBG(skb, cp->debug_enable, cp->debug_times, RTL8686_SKB_TX);

			cp->cp_stats.tx_sw_num++;

			checkTXDesc(ringNum, gmac);

			if(unlikely(cp->eth_close == GMAC_TRUE)
#ifdef TX_CREATE_TEST_PACKET_DEBUG
				|| cp->test_packet_start
#endif
				)
			{
				DEVPRIV(skb->dev)->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				GMAC_SPIN_UNLOCK(&cp->tx_lock);
				return NET_XMIT_DROP;
			}

			if (unlikely(TX_HQBUFFS_AVAIL(cp,ringNum) <= DESC_NEED_NUM))
			{
				DEVPRIV(skb->dev)->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				cp->cp_stats.tx_no_desc++;
				netif_stop_queue(dev);
				kick_tx(gmac, ringNum);
				GMAC_SPIN_UNLOCK(&cp->tx_lock);
				return NET_XMIT_DROP;
			}

		//while(1){
			amsdu_counter++;
#if defined(CONFIG_RTK_NIC_HWLOOKUP_SNAP_TRANSFORM_BY_TX_DESC)
			pSnap_data=pAmsdu_data+ETH_HLEN+6;					//pointer to ahead of ETHTYPE of SNAP header
#else
			pSnap_data=pAmsdu_data+8;							//DA(6B)+SA(6B)+LEN(2B)+LLC(3B)+SNAP(5B)-DA(6B)-SA(6B)-ETHTYPE(2B)=8
			memmove(pSnap_data,pAmsdu_data,ETH_ALEN<<1);		//move DA and SA to ahead of ETHTYPE of SNAP header
#endif

			padding=(snap_len+ETH_HLEN)&0x3;
			if(padding){
				offset=4-padding;
			}else
				offset=0;
			offset+=snap_len;
			offset+=ETH_HLEN;	//DA(6B)+SA(6B)+LEN(2B)
			amsdu_len-=offset;

#if defined(CONFIG_RTK_NIC_HWLOOKUP_SNAP_TRANSFORM_BY_TX_DESC)
			//second tx desc len = snap_len - LLC(3B) - SNAP(5B) + ETHERTYPE(2B)
			snap_len-=6;
#else
			//packet len = snap_len - LLC(3B) - SNAP(5B) + DA(6B) + SA(6B) + ETHERTYPE(2B)
			snap_len+=6;
#endif

			txq_skb=skb;

			if(snap_len < RE8686_HW_SMALLEST_DATA_LEN)
			{
				DEVPRIV(skb->dev)->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				cp->cp_stats.tooshort++;
				TX_TRACE(gmac, cp->debug_enable, cp->debug_times, "skb len too short dropped (len=%d).\n", snap_len);
				GMAC_SPIN_UNLOCK(&cp->tx_lock);
				return NET_XMIT_DROP;
			}
			if(snap_len > RE8670_MAX_ETH_FRAME_SIZE)
			{
				if(!cp->tx_jumbo_frame_enabled || (snap_len >= RE8670_MAX_ETH_JUMBO_FRAME_SIZE))
				{
					//memDump(skb->data, (skb->len > NUM2PRINT)?NUM2PRINT : skb->len, "TX SKB LEN > 1522", RTL8686_MEM_DUMP_FORMAT_ALL);
					DEVPRIV(skb->dev)->net_stats.tx_dropped++;
					dev_kfree_skb_any(skb);
					GMAC_SPIN_UNLOCK(&cp->tx_lock);
					return NET_XMIT_DROP;
				}
				else
				{
					ptxInfo->opts1.dw &= ~(IPCS|L4CS);
				}
			}

			if(amsdu_len>ETH_HLEN){
				refcount_inc(&txq_skb->users);
			}

			entry = cp->tx_Mhqhead[ringNum];
#if defined(CONFIG_RTK_NIC_HWLOOKUP_SNAP_TRANSFORM_BY_TX_DESC)
			eor = (entry == (cp->re8670_tx_ring_size[ringNum] - 1)) ? RingEnd : 0;
			txd = &cp->tx_Mhqring[ringNum][entry];

			// Kaohj --- invalidate DCache before NIC DMA
			dma_cache_inv((unsigned long)pAmsdu_data, ETH_ALEN<<1);

			//default setting, always need this
			ptxInfo->addr = (u32)(pAmsdu_data);

			//[step1] opts1 clear & set init value
			ptxInfo->opts1.dw &= ~(RingEnd|0x1ffff|LastFrag);
			ptxInfo->opts1.dw |= (eor|ETH_ALEN<<1|DescOwn|FirstFrag|TxCRC);

			//[step2] HW LSO or jumbo frame decision
#ifdef CONFIG_REALTEK_HW_LSO
			ptxInfo->opts4.bit.lgsen = 0;
#endif
			//[step3] support CPU port direct Tx
			if(ptxInfo->opts2.bit.tx_portmask&CPU_PORT_MASK_ALL)
			{
				re8686_direct_tx_to_cpu_port(ptxInfo);
			}

			ptxInfo->opts2.bit.cputag = 1;

			TXINFO_DBG(cp, ptxInfo);

			//apply to first txdesc
			apply_to_txdesc(txd, ptxInfo);

			cp->tx_skb[ringNum][entry].skb = 0xFFFFFFFF;
			cp->tx_skb[ringNum][entry].mapping = 0;
			cp->tx_skb[ringNum][entry].frag = 0;

			//get next txd for second tx desc
			entry = NEXT_TX(entry, cp->re8670_tx_ring_size[ringNum]);
#endif
			eor = (entry == (cp->re8670_tx_ring_size[ringNum] - 1)) ? RingEnd : 0;
			txd = &cp->tx_Mhqring[ringNum][entry];

			// Kaohj --- invalidate DCache before NIC DMA
#if defined(CONFIG_RTK_NIC_HWLOOKUP_SNAP_TRANSFORM_BY_TX_DESC)
			dma_cache_inv((unsigned long)pSnap_data, snap_len);
#else
			dma_cache_wback_inv((unsigned long)pSnap_data, snap_len);
#endif

			//default setting, always need this
			ptxInfo->addr = (u32)(pSnap_data);

			//[step1] opts1 clear & set init value
#if defined(CONFIG_RTK_NIC_HWLOOKUP_SNAP_TRANSFORM_BY_TX_DESC)
			ptxInfo->opts1.dw &= ~(RingEnd|0x1ffff|FirstFrag);
			ptxInfo->opts1.dw |= (eor|snap_len|DescOwn|LastFrag|TxCRC);
#else
			ptxInfo->opts1.dw &= ~(RingEnd|0x1ffff);
			ptxInfo->opts1.dw |= (eor|snap_len|DescOwn|FirstFrag|LastFrag|TxCRC);
#endif

			//[step2] HW LSO or jumbo frame decision
#ifdef CONFIG_REALTEK_HW_LSO
			ptxInfo->opts4.bit.lgsen = 0;
#endif
			//[step3] support CPU port direct Tx
			if(ptxInfo->opts2.bit.tx_portmask&CPU_PORT_MASK_ALL)
			{
				re8686_direct_tx_to_cpu_port(ptxInfo);
			}

			ptxInfo->opts2.bit.cputag = 1;

			TXINFO_DBG(cp, ptxInfo);

			//apply to txdesc
			apply_to_txdesc(txd, ptxInfo);
#ifdef TX_RING_DEBUG
			if(cp->tx_ring_backup_debug)
			{
				DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ringNum].txDescriptor[entry];
				memset(&cp->rtl8686_tx_ring_debug[ringNum].dataBuffer[entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
				memcpy(&cp->rtl8686_tx_ring_debug[ringNum].dataBuffer[entry], pSnap_data, snap_len);
				apply_to_txdesc(dbg_txd, ptxInfo);
				dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ringNum].dataBuffer[entry];
			}
#endif
			cp->tx_skb[ringNum][entry].skb = txq_skb;
			cp->tx_skb[ringNum][entry].mapping = 0;
			cp->tx_skb[ringNum][entry].frag = 0;

			TX_TRACE(gmac, cp->debug_enable, cp->debug_times, "FROM_FC[%x] DA=%02x:%02x:%02x:%02x:%02x:%02x SA=%02x:%02x:%02x:%02x:%02x:%02x ethtype=%04x len=%d VlanAct=%d Vlan=%d Pri=%d ExtSpa=%d TxPmsdk=0x%x L34Keep=%x PON_SID=%d\n",
				(u32)skb&0xffff,
				pAmsdu_data[0],pAmsdu_data[1],pAmsdu_data[2],pAmsdu_data[3],pAmsdu_data[4],pAmsdu_data[5],
				pAmsdu_data[6],pAmsdu_data[7],pAmsdu_data[8],pAmsdu_data[9],pAmsdu_data[10],pAmsdu_data[11],
#if defined(CONFIG_RTK_NIC_HWLOOKUP_SNAP_TRANSFORM_BY_TX_DESC)
				(pSnap_data[0]<<8)|pSnap_data[1],snap_len,
#else
				(pSnap_data[12]<<8)|pSnap_data[13],snap_len,
#endif
				ptxInfo->opts2.bit.tx_cvlan_action,
				ptxInfo->opts2.bit.cvlan_vidh<<8|ptxInfo->opts2.bit.cvlan_vidl,
				ptxInfo->opts2.bit.cvlan_prio,
				ptxInfo->opts3.bit.extspa,
				ptxInfo->opts2.bit.tx_portmask,
				ptxInfo->opts3.bit.l34_keep,
				ptxInfo->opts3.bit.tx_dst_stream_id);

			//last_entry = entry;
			cp->tx_Mhqhead[ringNum] = NEXT_TX(entry, cp->re8670_tx_ring_size[ringNum]);
			cp->cp_stats.tx_hw_num++;

			if(amsdu_len>ETH_HLEN){
				//every snap will increase one to users
				//refcount_inc(&txq_skb->users);
				//move to next snap packet
				pAmsdu_data+=offset;
				//len for next one
				snap_len=ntohs(*(unsigned short *)(pAmsdu_data+ETH_ALEN+ETH_ALEN));
			}else{
				//finish all snap packets, just leave now.
				keepLoop=0;
			}
		//}

			//for memory controller's write buffer
			//write_buffer_flush();
			wmb();
			TXDBG_TIMES_UPDATE(cp);
//#if 0 //LINUX_VERSION_CODE <= KERNEL_VERSION(4, 4, 0)
//			dev->trans_start = jiffies;
//#endif
//#if 0 //LINUX_VERSION_CODE <= KERNEL_VERSION(5, 1, 0)
//			netdev_pick_tx(dev, skb, NULL)->trans_start = jiffies;
//#else
			netdev_core_pick_tx(dev, skb, NULL)->trans_start = jiffies;
//#endif
			sk_pacing_shift_update(skb->sk, 8);
			GMAC_SPIN_UNLOCK(&cp->tx_lock);

#ifdef TX_CREATE_TEST_PACKET_DEBUG
			if(!cp->test_packet_start)
#endif
			{
				kick_tx(gmac, ringNum);
			}

		}

		return NETDEV_TX_OK;
	}
}
#endif

/*
*	return 0 -> set OK, return -1 -> set FAIL
*	ptxInfo: pointer of tx_info(ptxInfo->opts3.bit.gmac_id would be used)
*	reg_num: 0 to set VLAN_REG, 1 to set VLAN1_REG
*	value: the value to set VLAN_REG/ VLAN1_REG
*/
__IRAM_NIC
int re8686_set_vlan_register(struct tx_info* ptxInfo, unsigned char reg_num, unsigned int value) 
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[ptxInfo->opts3.bit.gmac_id];
	unsigned int gmac = cp->gmac;
	
	if(!value)
	{
		printk(KERN_CONT " %s %d: wrong VLAN register value=%d\n", __func__, __LINE__, value);
		return -1;
	}

	switch(reg_num)
	{
		case 0:
			cp->stag_pid = value;
			RLE0787_W32(gmac, VLAN_REG, value);
			break;
		case 1:
			cp->stag_pid1 = value;
			RLE0787_W32(gmac, VLAN1_REG, value);
			break;
		default:
			printk(KERN_CONT " %s %d: wrong reg_num=%d\n", __func__, __LINE__, reg_num);
			return -1;
	}

	return 0;
}

/*
*	return 0 -> get OK, return -1 -> get FAIL
*	ptxInfo: pointer of tx_info(ptxInfo->opts3.bit.gmac_id would be used)
*	reg_num: 0 to get VLAN_REG, 1 to get VLAN1_REG
*	value_p: the pointer to get content of VLAN_REG/ VLAN1_REG
*/
__IRAM_NIC
int re8686_get_vlan_register(struct tx_info* ptxInfo, unsigned char reg_num, unsigned int *value_p) 
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[ptxInfo->opts3.bit.gmac_id];
	unsigned int gmac = cp->gmac;

	if(value_p==NULL)
	{
		printk(KERN_CONT " %s %d: wrong VLAN register value pointer=NULL\n", __func__, __LINE__);
		return -1;
	}

	switch(reg_num)
	{
		case 0:
			*value_p = RLE0787_R32(gmac, VLAN_REG);
			break;
		case 1:
			*value_p = RLE0787_R32(gmac, VLAN1_REG);
			break;
		default:
			printk(KERN_CONT " %s %d: wrong reg_num=%d\n", __func__, __LINE__, reg_num);
			return -1;
	}

	return 0;
}

#if 0//def CONFIG_APOLLO_ROMEDRIVER
int re8670_start_xmit_common(struct sk_buff *skb, struct net_device *dev, struct tx_info *ptx, struct tx_info *ptxMask);

__IRAM_NIC int rtk_rg_fwdEngine_xmit (struct sk_buff *skb, void *void_ptx, void *void_ptxMask) // (void *) using (rtk_rg_txdesc_t *) casting in romeDriver, using (struct tx_info*) casting in re8686.c.
{
	struct tx_info *ptx=(struct tx_info *)void_ptx;
	struct tx_info *ptxMask=(struct tx_info *)void_ptxMask;			
	skb->cb[0]=1;
	skb->dev = rtl8686_dev_table[0].dev_instant;
	re8670_start_xmit_common(skb,skb->dev,ptx,ptxMask);	
	return 0;
}

__IRAM_NIC int re8670_start_xmit (struct sk_buff *skb, struct net_device *dev)	//shlee temp, fix this later
{
	skb->cb[0]=0;
	re8670_start_xmit_common(skb,skb->dev,NULL,NULL);
	return 0;
}

__IRAM_NIC int re8670_start_xmit_common(struct sk_buff *skb, struct net_device *dev, struct tx_info *ptx, struct tx_info *ptxMask)
#else
#if defined(CONFIG_RTK_L34_ENABLE) && defined(CONFIG_APOLLO_GPON_FPGATEST) 
extern int _rtk_rg_virtualMAC_with_PON_get(void);
#endif
__IRAM_NIC int re8670_start_xmit_txInfo (struct sk_buff *skb, struct net_device *dev, struct tx_info* ptxInfo, struct tx_info* ptxInfoMask)	//luke
#endif
{
	struct re_private_root *root_cp = &re_private_data_root;
	unsigned int gmac;
	struct re_private *cp;
	unsigned entry;
	u32 eor;
	struct tx_info txInfo;
	int ring_num=0;
#ifdef HW_LSO_ENABLE
	int mtu;
#endif
#ifdef TX_BACKUP_RING
	u32 tx_backup_ring_idx;
	u32 tx_backup_ring_mask;
#endif
#ifdef TX_BACKUP_GMAC
	u32 tx_backup_gmac_idx;
	u32 tx_backup_gmac_mask;
#endif

	if (!ptxInfo || !ptxInfoMask) {
		dev_kfree_skb_any(skb);
		return 0;
	}

	gmac = ptxInfo->opts3.bit.gmac_id;

	if (skb->cb[RTK_NIC_TX_HASH_CB_NUMBER])
		gmac = (skb->cb[RTK_NIC_TX_HASH_CB_NUMBER] - 1) % MAX_GMAC_NUM;

	if (unlikely(gmac >= MAX_GMAC_NUM))
		gmac = 0;

	cp = root_cp->re_private_data_ptr[gmac];
#ifdef TX_BACKUP_RING
	tx_backup_ring_mask = cp->tx_multiring_bitmap;
#endif
#ifdef TX_BACKUP_GMAC
	tx_backup_gmac_mask = root_cp->gmac_enable_mask;
#endif

	if(unlikely(gmac >= MAX_GMAC_NUM) || unlikely(cp->gmac_enabled != GMAC_TRUE)
#ifdef TX_BURST_PACKET_SEND
		|| unlikely(cp->burst_tx_info.burst_buffer)
#endif
		) {
		dev_kfree_skb_any(skb);
		TX_TRACE(0, cp->debug_enable, cp->debug_times, "GMAC%d disabled, free skb.\n", gmac);
		return 0;
	}

#ifdef TX_BACKUP_GMAC
TX_BACKUP_GMAC_RETRY:
#endif

	GMAC_SPIN_LOCK(&cp->tx_lock);	
#ifdef HWNAT_CUSTOMIZE	
	if(re8686_tx_ring_customized[gmac][ring_num])ring_num=MAX_TXRING_NUM-1;	
#endif

	if(unlikely(cp->eth_close == GMAC_TRUE || INVERIFYMODE)
#ifdef TX_CREATE_TEST_PACKET_DEBUG
		|| cp->test_packet_start
#endif
		) 
	{
		DEVPRIV(skb->dev)->net_stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		GMAC_SPIN_UNLOCK(&cp->tx_lock);
		return NET_XMIT_DROP;
	}

#ifdef TX_BACKUP_RING
TX_BACKUP_RING_RETRY:
#endif
#ifdef HWNAT_CUSTOMIZE
	if(re8686_tx_ring_customized[gmac][ring_num])ring_num=MAX_TXRING_NUM-1;
#endif

	memset(&txInfo, 0, sizeof(struct tx_info));
	ETHDBG_PRINT(gmac, cp->debug_enable, cp->debug_times, RTL8686_SKB_TX, "Tx dev=%s nr_frags=%d\n", dev->name, skb_shinfo(skb)->nr_frags);
	SKB_DBG(skb, cp->debug_enable, cp->debug_times, RTL8686_SKB_TX);
	cp->cp_stats.tx_sw_num++;
	checkTXDesc(ring_num, gmac);

	if (unlikely(TX_HQBUFFS_AVAIL(cp,ring_num) <= (skb_shinfo(skb)->nr_frags + 1)))
	{
		kick_tx(gmac, ring_num);
#ifdef TX_BACKUP_RING
		tx_backup_ring_mask &= ~(1<<ring_num);
		for (tx_backup_ring_idx=0 ; tx_backup_ring_idx<MAX_TXRING_NUM ; tx_backup_ring_idx++) {
			if (!(tx_backup_ring_mask&(1<<tx_backup_ring_idx)))
				continue;

			if (TX_HQBUFFS_AVAIL(cp,tx_backup_ring_idx) <= (skb_shinfo(skb)->nr_frags + 1))
				continue;

			ring_num=tx_backup_ring_idx;
			goto TX_BACKUP_RING_RETRY;
		}

		ring_num=nic_available_tx_ring_get(cp);
		if (ring_num != -1)
			goto TX_BACKUP_RING_RETRY;
		else
			ring_num=0;
#endif
#ifdef TX_BACKUP_GMAC
		tx_backup_gmac_mask &= ~(1<<gmac);
		for (tx_backup_gmac_idx=0 ; tx_backup_gmac_idx<MAX_GMAC_NUM ; tx_backup_gmac_idx++) {
			if (!(tx_backup_gmac_mask&(1<<tx_backup_gmac_idx)))
				continue;

			GMAC_SPIN_UNLOCK(&cp->tx_lock);
			cp = root_cp->re_private_data_ptr[tx_backup_gmac_idx];
			gmac = tx_backup_gmac_idx;
			goto TX_BACKUP_GMAC_RETRY;
		}
#endif
		DEVPRIV(skb->dev)->net_stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		cp->cp_stats.tx_no_desc++;		
		netif_stop_queue(dev);
		GMAC_SPIN_UNLOCK(&cp->tx_lock);
		return NET_XMIT_DROP;
	}	

	entry = cp->tx_Mhqhead[ring_num];
	eor = (entry == (cp->re8670_tx_ring_size[ring_num] - 1)) ? RingEnd : 0;
	if (skb_shinfo(skb)->nr_frags == 0) {
		DMA_TX_DESC  *txd = &cp->tx_Mhqring[ring_num][entry];
		u32 len;

		len = skb->len;	

		if(len < RE8686_HW_SMALLEST_DATA_LEN)
		{
			DEVPRIV(skb->dev)->net_stats.tx_dropped++;
			dev_kfree_skb_any(skb);
			cp->cp_stats.tooshort++;
			TX_TRACE(gmac, cp->debug_enable, cp->debug_times, "skb len too short dropped (len=%d).\n", len);
			GMAC_SPIN_UNLOCK(&cp->tx_lock);
			return NET_XMIT_DROP;
		}
		
		// Kaohj --- invalidate DCache before NIC DMA
		dma_cache_wback_inv((unsigned long)skb->data, len);

		//default setting, always need this
		txInfo.addr = CPHYSADDR(skb->data);
		
		//[step1] opts1 set init value
		txInfo.opts1.dw = (eor|len|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);

		//[step2] opts1 might be changed by some additional setting or parameter ptxInfo
#if 0//def CONFIG_APOLLO_ROMEDRIVER
		if(skb->cb[0]) //from RomeDriver
			_rtk_rg_fwdEngineTxDescSetting((void*)&txInfo,(void*)ptx,(void*)ptxMask);
#endif
		//plz put tx additional setting into this function
		tx_additional_setting(skb, dev, &txInfo);		
		do_txInfoMask(&txInfo, ptxInfo, ptxInfoMask);

		//[step3] HW LSO or jumbo frame decision
#ifdef CONFIG_REALTEK_HW_LSO
		txInfo.opts4.bit.lgsen = 0;
		mtu = re8670_get_mtu(skb, cp);
		if(skb_is_gso(skb) && mtu && (skb->len-14) > mtu)
		{
			txInfo.opts4.bit.lgsen = 1;
			if (__vlan_get_protocol(skb, skb->protocol, NULL) == htons(ETH_P_PPP_SES))
				mtu -= RE8670_PPPOE_HDR_SIZE;
			txInfo.opts4.bit.lgmtu = mtu; //real MTU
		}
		else
#endif
		if(skb->len > RE8670_MAX_ETH_FRAME_SIZE)
		{
			if(!cp->tx_jumbo_frame_enabled || (skb->len >= RE8670_MAX_ETH_JUMBO_FRAME_SIZE))
			{
				//memDump(skb->data, (skb->len > NUM2PRINT)?NUM2PRINT : skb->len, "TX SKB LEN > 1522", RTL8686_MEM_DUMP_FORMAT_ALL);
				DEVPRIV(skb->dev)->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				GMAC_SPIN_UNLOCK(&cp->tx_lock);
				return NET_XMIT_DROP;
			}
			else
			{
				txInfo.opts1.dw &= ~(IPCS|L4CS);
			}
		}

		//[step4] support CPU port direct Tx 
		if(txInfo.opts2.bit.tx_portmask&CPU_PORT_MASK_ALL)
		{
			re8686_direct_tx_to_cpu_port(&txInfo);			
		}

#if defined(CONFIG_RTK_L34_ENABLE) && defined(CONFIG_APOLLO_GPON_FPGATEST) 
		//20150703LUKE: filter packet from protocol stack send to virtualmac mapping portmask!
		txInfo.opts3.bit.tx_portmask&=~(_rtk_rg_virtualMAC_with_PON_get());
#endif

		TXINFO_DBG(cp, &txInfo);
		//apply to txdesc
		apply_to_txdesc(txd, &txInfo);
#ifdef TX_RING_DEBUG
		if(cp->tx_ring_backup_debug)
		{
			DMA_TX_DESC  *dbg_txd = &cp->rtl8686_tx_ring_debug[ring_num].txDescriptor[entry];
			memset(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[entry], 0 , TX_RING_DEBUG_BUFFER_SIZE);
			memcpy(&cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[entry], skb->data, skb->len);
			apply_to_txdesc(dbg_txd, &txInfo);
			dbg_txd->addr = &cp->rtl8686_tx_ring_debug[ring_num].dataBuffer[entry];
		}
#endif
		cp->tx_skb[ring_num][entry].skb = skb;
		cp->tx_skb[ring_num][entry].mapping = CPHYSADDR(skb->data);
		cp->tx_skb[ring_num][entry].frag = 0;
		entry = NEXT_TX(entry, cp->re8670_tx_ring_size[ring_num]);
	}
#ifdef CONFIG_REALTEK_HW_LSO
	else {
		re8670_mFrag_xmit(skb, cp, &entry, ptxInfo, ring_num);
	}
#endif
	cp->tx_Mhqhead[ring_num] = entry;

#if 0//krammer close this
	if (unlikely(TX_HQBUFFS_AVAIL(cp,ring_num) <= 1))
		netif_stop_queue(dev);
#endif

	//for memory controller's write buffer
	//write_buffer_flush();
	mb();
	//cp->cp_stats.tx_hw_num++;
	cp->cp_stats.tx_hw_num_ring[ring_num]++;
	TX_TRACE(gmac, cp->debug_enable, cp->debug_times, "FROM_PS[%x] DA=%02x:%02x:%02x:%02x:%02x:%02x SA=%02x:%02x:%02x:%02x:%02x:%02x ethtype=%04x len=%d Vlan=%d Pri=%d ExtSpa=%d TxPmsdk=0x%x L34Keep=%x PON_SID=%d\n",
		(u32)skb&0xffff,
		skb->data[0],skb->data[1],skb->data[2],skb->data[3],skb->data[4],skb->data[5],
		skb->data[6],skb->data[7],skb->data[8],skb->data[9],skb->data[10],skb->data[11],
		(skb->data[12]<<8)|skb->data[13],skb->len,
		txInfo.opts2.bit.cvlan_vidh<<8|txInfo.opts2.bit.cvlan_vidl,
		txInfo.opts2.bit.cvlan_prio,
		txInfo.opts3.bit.extspa,
		txInfo.opts2.bit.tx_portmask,
		txInfo.opts3.bit.l34_keep,
		txInfo.opts3.bit.tx_dst_stream_id);
	TXDBG_TIMES_UPDATE(cp);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,4,0)
	dev->trans_start = jiffies;
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(5,1,0)
	netdev_pick_tx(dev, skb, NULL)->trans_start = jiffies;
#else
	netdev_core_pick_tx(dev, skb, NULL)->trans_start = jiffies;
#endif
	sk_pacing_shift_update(skb->sk, 8);
	GMAC_SPIN_UNLOCK(&cp->tx_lock);

	kick_tx(gmac, ring_num);

	return NETDEV_TX_OK;
}

__IRAM_NIC int re8670_start_xmit (struct sk_buff *skb, struct net_device *dev)	//shlee temp, fix this later
{
	struct re_private_root *root_cp = &re_private_data_root;
	
	if (root_cp->txfunc)
		return root_cp->txfunc(skb, dev);
	else
		return re8670_start_xmit_txInfo(skb,dev,NULL,NULL);
}

/* Set or clear the multicast filter for this adaptor.
   This routine is not state sensitive and need not be SMP locked. */
static void __re8670_set_rx_mode (unsigned int gmac)
{
	int rx_mode;

	rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys | AcceptAllPhys;

	/* We can safely update without stopping the chip. */
	// Kao, 2004/01/07
	RLE0787_W32(gmac, MAR0, 0xFFFFFFFF);
	RLE0787_W32(gmac, MAR4, 0xFFFFFFFF);
	RLE0787_W32(gmac, RCR, rx_mode);
}

static void re8670_set_rx_mode (struct net_device *dev)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp;
	unsigned int gmac;

	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		cp = root_cp->re_private_data_ptr[gmac];

		if(cp->gmac_enabled != GMAC_TRUE)
			continue;
		
		//GMAC_SPIN_LOCK(&cp->rx_lock);
		__re8670_set_rx_mode(cp->gmac);
		//GMAC_SPIN_UNLOCK(&cp->rx_lock);
	}
}

void re8670_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats64)
{
	if (unlikely(stats64 == NULL))
		return;

	stats64->rx_packets 	= DEVPRIV(dev)->net_stats.rx_packets;
	stats64->rx_bytes   	= DEVPRIV(dev)->net_stats.rx_bytes;
	stats64->rx_dropped   	= DEVPRIV(dev)->net_stats.rx_dropped;
	stats64->tx_packets 	= DEVPRIV(dev)->net_stats.tx_packets;
	stats64->tx_bytes   	= DEVPRIV(dev)->net_stats.tx_bytes;
	stats64->tx_dropped   	= DEVPRIV(dev)->net_stats.tx_dropped;
	stats64->multicast  	= DEVPRIV(dev)->net_stats.multicast;

#ifdef CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE
	rtk_dev_update_stats(dev, stats64);
#endif
}

static void re8670_init_trx_cdo(struct re_private *cp)
{
	int j;
	for(j=0;j<MAX_TXRING_NUM;j++){
		cp->tx_Mhqhead[j] = cp->tx_Mhqtail[j] = 0;
	}

	for(j=0;j<MAX_RXRING_NUM;j++){
		cp->rx_Mtail[j] = 0;
	}
}

static void re8670_stop_hw (struct re_private *cp)
{
	unsigned int gmac=cp->gmac;
	int j;
	RLE0787_W32(gmac, IO_CMD, 0); /* timer  rx int 1 packets*/
	RLE0787_W32(gmac, IO_CMD1, 0); //czyao
	RLE0787_W16(gmac, IMR, 0);
	RLE0787_W32(gmac, IMR0, 0);
	RLE0787_W16(gmac, ISR, 0xffff);
	RLE0787_W32(gmac, ISR1, 0xffffffff);
	//synchronize_irq();
	//synchronize_irq(cp->irq);/*linux-2.6.19*/
	udelay(10);

	for(j=0;j<MAX_RXRING_NUM;j++)
		cp->rx_Mtail[j] = 0;		
	for(j=0;j<MAX_TXRING_NUM;j++)
		cp->tx_Mhqhead[j] = cp->tx_Mhqtail[j] = 0;
}

static void re8670_ip_enable_all(void)
{
	REG32(BSP_IP_SEL) |= BSP_EN_GMAC;
#ifdef CONFIG_GMAC1_USABLE
	REG32(NEW_BSP_IP_SEL) |= BSP_EN_GMAC1;
#endif
#ifdef CONFIG_GMAC2_USABLE
	REG32(NEW_BSP_IP_SEL) |= BSP_EN_GMAC2;
#endif
}

static void re8670_ip_enable(unsigned int gmac)
{
	if(gmac == 0)
	{
		REG32(BSP_IP_SEL) |= BSP_EN_GMAC;
	}
#ifdef CONFIG_GMAC1_USABLE
	else if(gmac == 1)
	{
		REG32(NEW_BSP_IP_SEL) |= BSP_EN_GMAC1;
	}
#endif
#ifdef CONFIG_GMAC2_USABLE
	else if(gmac == 2)
	{
		REG32(NEW_BSP_IP_SEL) |= BSP_EN_GMAC2;
	}
#endif
}

static void re8670_reset_hw (struct re_private *cp)
	{
	unsigned int gmac = cp->gmac;
	
	/* After apollo use this for totaly gmac reset
	, in old method, mring can't receive packet at first time packet coming */
	//disable_irq(cp->irq);
	switch(gmac) {
		case 0:
			REG32(BSP_IP_SEL) &= ~BSP_EN_GMAC;
			mdelay(10);
			REG32(BSP_IP_SEL) |= BSP_EN_GMAC;
			break;
		case 1:
			REG32(NEW_BSP_IP_SEL) &= ~BSP_EN_GMAC1;
			mdelay(10);
			REG32(NEW_BSP_IP_SEL) |= BSP_EN_GMAC1;
			break;
		case 2:
			REG32(NEW_BSP_IP_SEL) &= ~BSP_EN_GMAC2;
			mdelay(10);
			REG32(NEW_BSP_IP_SEL) |= BSP_EN_GMAC2;
			break;
	}
	//enable_irq(cp->irq);
}

static inline void re8670_start_hw (struct re_private *cp)
{
	unsigned int gmac=cp->gmac;

	RLE0787_W32(gmac, IO_CMD1, cp->iocmd1_reg);
	RLE0787_W32(gmac, IO_CMD, cp->iocmd_reg); /* timer  rx int 1 packets*/
	//printk(KERN_CONT "Start NIC in Pkt Processor disabled mode.. IO_CMD %x\n", RLE0787_R32(gmac, IO_CMD));
}

int re8686_set_flow_control(unsigned int gmac, unsigned int ring, unsigned char enable)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	u32 reg32_val, ring_size, ring0_size_msk;
	u16 desc_l, desc_h;
	u16 reg16_val;
	#ifndef CONFIG_GMAC1_USABLE
	if (gmac==1)
		goto error;
	#endif 
	#ifndef CONFIG_GMAC2_USABLE
	if (gmac==2)
		goto error;
	#endif 
	#ifdef CONFIG_RTK_SINGLE_RX_RING
	if (ring >= MAX_RXRING_NUM)
		goto error;
	#endif
	
	if(enable || (cp->re8670_rx_ring_size[ring] == 4096))
	{
		ring_size = (cp->re8670_rx_ring_size[ring]);
	}
	else
	{
		ring_size = ((cp->re8670_rx_ring_size[ring])<<1);
	}
#ifndef CONFIG_RTK_SINGLE_RX_RING
	if(ring==0)
#endif
	{
		ring0_size_msk = ring_size-1;
		desc_l = ring0_size_msk&0xff;
		desc_h = (ring0_size_msk&0xf00)>>8;
		reg32_val = (desc_l<<24)|((desc_h)<<4)|(RLE0787_R32(gmac, EthrntRxCPU_Des_Num)&0x00ffff0f);
		RLE0787_W32(gmac, EthrntRxCPU_Des_Num, reg32_val);
		reg32_val = (desc_l<<8)|desc_h|(RLE0787_R32(gmac, RxCDO)&0xffff00f0);
		RLE0787_W32(gmac, RxCDO, reg32_val);		
	}
#ifndef CONFIG_RTK_SINGLE_RX_RING
	else
	{
		reg32_val = ((ring_size&0xfff)-1)|(RLE0787_R32(gmac, EthrntRxCPU_Des_Num2+(ADDR_OFFSET*(ring-1)))&0xfffff000);
		RLE0787_W32(gmac, EthrntRxCPU_Des_Num2+(ADDR_OFFSET*(ring-1)), reg32_val);
		reg16_val = ((ring_size&0xfff)-1);
		RLE0787_W16(gmac, RxRingSize2+(ADDR_OFFSET*(ring-1)), reg16_val);
	}
#endif
	cp->re8670_rx_flow_control_status[ring] = enable?GMAC_ON:GMAC_OFF;
	if (cp->re8670_rx_ring_size[ring] == 4096)
			cp->re8670_rx_flow_control_status[ring] = GMAC_ON;
	return 0;
#if !defined(CONFIG_GMAC1_USABLE) || !defined(CONFIG_GMAC2_USABLE) || defined(CONFIG_RTK_SINGLE_RX_RING)
error:
	//printk(KERN_CONT "%s(%d): ignored gmac=%d ring=%u\n",__func__,__LINE__,gmac,ring);
	return -1;
#endif
}

static void multi_rtx_ring_init(struct re_private *cp)
{
	unsigned int gmac=cp->gmac;
	u32 reg32_val, ring0_size_msk;
	u16 desc_l, desc_h;
	int i;
	
	for(i=0;i<MAX_TXRING_NUM;i++)
	{		
		RLE0787_W32(gmac, TxFDP1+(ADDR_OFFSET*i), CPHYSADDR(cp->tx_Mhqring[idx_sw2hw(i)]));
		RLE0787_W16(gmac, TxCDO1+(ADDR_OFFSET*i), 0);
	}
	for(i=0;i<MAX_RXRING_NUM;i++)
	{			
		/*we set flow control even if we don't enable this queue.........
		this is because we want to prevent triggering flow control of the queue we disable.....*/
		if(i==0)
		{
			ring0_size_msk = (cp->re8670_rx_ring_size[i])-1;
			desc_l = ring0_size_msk&0xff;
			desc_h = (ring0_size_msk&0xf00)>>8;
			RLE0787_W32(gmac, RxFDP, CPHYSADDR(cp->rx_Mring[0]));
			reg32_val = (desc_l<<24)|((TH_ON_VAL&0xff)<<16)|((TH_OFF_VAL&0xff)<<8)|((desc_h)<<4)|((TH_ON_VAL&0xf00)>>8);
			RLE0787_W32(gmac, EthrntRxCPU_Des_Num, reg32_val);
			reg32_val = (desc_l<<8)|desc_h;
			RLE0787_W32(gmac, RxCDO, reg32_val);
			reg32_val = ((TH_OFF_VAL&0xf00)<<16)|(RLE0787_R32(gmac, Rx_Pse_Des_Thres_h)&0xf0ffffff);
			RLE0787_W32(gmac, Rx_Pse_Des_Thres_h, reg32_val);
		}
		else
		{
			RLE0787_W32(gmac, RxFDP2+(ADDR_OFFSET*(i-1)), CPHYSADDR(cp->rx_Mring[i]));
			RLE0787_W32(gmac, EthrntRxCPU_Des_Wrap2+(ADDR_OFFSET*(i-1)), 0);  //initialize the register content
			RLE0787_W32(gmac, EthrntRxCPU_Des_Wrap2+(ADDR_OFFSET*(i-1)), ((TH_ON_VAL&0xfff)<<16)|(TH_OFF_VAL&0xfff));
			RLE0787_W32(gmac, EthrntRxCPU_Des_Num2+(ADDR_OFFSET*(i-1)), (cp->re8670_rx_ring_size[i]&0xfff)-1);
			RLE0787_W16(gmac, RxRingSize2+(ADDR_OFFSET*(i-1)), (cp->re8670_rx_ring_size[i]&0xfff)-1);
		}
	}
	// set queue 456 to default setting , otherwise the eth0 can't rx packet (due to gmac flow control)
	re8686_set_flow_control(gmac, 0, cp->re8670_rx_flow_control_status[0]);
#ifndef CONFIG_RTK_SINGLE_RX_RING
	re8686_set_flow_control(gmac, 1, cp->re8670_rx_flow_control_status[1]);
	re8686_set_flow_control(gmac, 2, cp->re8670_rx_flow_control_status[2]);
	re8686_set_flow_control(gmac, 3, cp->re8670_rx_flow_control_status[3]);
	re8686_set_flow_control(gmac, 4, cp->re8670_rx_flow_control_status[4]);
	re8686_set_flow_control(gmac, 5, cp->re8670_rx_flow_control_status[5]);
#endif

}

static void re8670_init_hw (struct re_private *cp)
{
	unsigned int gmac = cp->gmac;
	u8 status;
	// Kao
	u32 *hwaddr, cputag_info;
	//unsigned int value;

	re8670_reset_hw(cp);

	RLE0787_W8(gmac, CMD, RxChkSum|RxJumboSupport);	 /* checksum */		//shlee 8672
//#ifdef CONFIG_ETHWAN
//	vlan_detag(GMAC_ON);//krammer: default de-tag from skb into desc
//#endif
	vlan_detag(gmac, GMAC_OFF);
	// Kao
	//20170502: disable gmac padding by default
	RLE0787_W32(gmac, TCR,(u32)(0x0C01));	
	RLE0787_W32(gmac, CPUtagCR,(u32)(0x0000));  /* Turn off CPU tag function */
	//cpu tag function
	cputag_info = (CTEN_RX | 2<<CT_RSIZE_L | 2<<CT_TSIZE | CT_APPLO_PRO | CTPM_8370 | CTPV_8370);
	RLE0787_W32(gmac, CPUtagCR,cputag_info); /* Turn on the cpu tag adding function */  //czyao 8672c
	RLE0787_W32(gmac, CPUtag1CR, (CT1_SID));	

	multi_rtx_ring_init(cp);

	status = RLE0787_R8(gmac, MSR);
	status = status | (TXFCE|FORCE_TX);	// enable tx flowctrl
	status = status | RXFCE;
	RLE0787_W8(gmac, MSR, status);	
	// Kao, set hw ID for physical match
	hwaddr = (u32 *)ROOTDEV->dev_addr;
	RLE0787_W32(gmac, IDR0, *hwaddr);	
	hwaddr = (u32 *)(ROOTDEV->dev_addr+4);
	RLE0787_W32(gmac, IDR4, *hwaddr);	
	
	RLE0787_W32(gmac, CONFIG_REG, Rff_size_sel_2k);	
	en_rx_mring_int_split(gmac);
	config_rx_sideband(gmac, GMAC_ON);
	set_rring_route(gmac);
	
	re8670_start_hw(cp);
	__re8670_set_rx_mode(cp->gmac);

	RLE0787_W16(gmac, ISR, 0xffff);/*clear all interrupt*/
	RLE0787_W32(gmac, ISR1, 0xffffffff);/*clear all interrupt*/
	RLE0787_W16(gmac, IMR, RX_ALL(gmac)); 
	UNMASK_IMR0_RXALL(gmac);
#ifdef TX_INTR_HANDLE
#ifdef TX_RECYCLE_SKB_USING_TOK_INT
	RLE0787_W16(gmac, IMR, RLE0787_R16(gmac, IMR)|IMR_TOK_TI);
	UNMASK_IMR0_TOK(gmac, IMR0_TOK_ALL);
#endif
#ifdef TX_KICK_RING_USING_TDU_INT
	RLE0787_W16(gmac, IMR, RLE0787_R16(gmac, IMR)|IMR_TDU_EN);
	UNMASK_IMR0_TDU(gmac, IMR0_TDU_ALL);
#endif
#endif
}

#ifdef HWNAT_CUSTOMIZE
inline void re8686_customized_tx(struct re_private *cp, unsigned int len, unsigned int ringNum, struct tx_info *pTxInfo, char addr_offset)
{
	unsigned entry;
	volatile DMA_TX_DESC *txd;
	unsigned int gmac=cp->gmac;
	
	GMAC_SPIN_LOCK(&cp->tx_lock);
	
	entry = cp->tx_Mhqhead[ringNum];
	txd = &cp->tx_Mhqring[ringNum][entry];

	if(unlikely(txd->opts1&DescOwn)){
		cp->cp_stats.rx_customized_tx_owned++;
		goto KICK_TX;
	}

	txd->addr += addr_offset;
	txd->opts2 = pTxInfo->opts2.dw;
	txd->opts3 = pTxInfo->opts3.dw;
	txd->opts4 = pTxInfo->opts4.dw;
	wmb();		
	txd->opts1 &= ~(0x1ffff|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);
	txd->opts1 |= (len|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);

KICK_TX:
	cp->tx_Mhqhead[ringNum] = NEXT_TX(entry, cp->re8670_tx_ring_size[ringNum]);
	
	GMAC_SPIN_UNLOCK(&cp->tx_lock);

	//wmb();
	
	kick_tx(gmac, ringNum);

	return;
}

inline void re8686_customized_quickTx(struct re_private *cp, unsigned int len, unsigned int ringNum)
{
	unsigned entry;
	volatile DMA_TX_DESC *txd;
	u32 txd_opts1;
	unsigned int gmac=cp->gmac;
	int delayTimes;
	
	//GMAC_SPIN_LOCK(&cp->tx_lock);
	
	entry = cp->tx_Mhqhead[ringNum];
	txd = &cp->tx_Mhqring[ringNum][entry];

	txd_opts1 = txd->opts1;
	
	if(txd->opts1&DescOwn){
		cp->cp_stats.rx_customized_tx_owned++;
		while(txd->opts1&DescOwn)
		{
			delayTimes = 100;
			while(delayTimes)
			{
				delayTimes--;
				if(delayTimes==0)
					break;
			}
		}
		//goto KICK_TX;
	}

	txd_opts1 &= ~(0x1ffff|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);
	txd_opts1 |= (len|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);
	txd->opts1 = txd_opts1;

	//TXINFO_DBG(gmac, (struct tx_info *)txd);if(debug_enable[gmac]&RTL8686_SKB_TX)memDump(txd->addr, 80, "txd");

KICK_TX:
	cp->tx_Mhqhead[ringNum] = NEXT_TX(entry,  cp->re8670_tx_ring_size[ringNum]);
	
	//GMAC_SPIN_UNLOCK(&cp->tx_lock);

	//wmb();
	
	kick_tx(gmac, ringNum);

	return;
}

inline void re8686_customized_dualTx(struct re_private *cp, unsigned int len, unsigned int ringNum)
{
	unsigned entry;
	volatile DMA_TX_DESC  *txd_fs;
	volatile DMA_TX_DESC  *txd_ls;
	u32 txd_fs_opts1, txd_ls_opts1;
	unsigned int gmac=cp->gmac;
	int delayTimes;

	//GMAC_SPIN_LOCK(&cp->tx_lock);

	entry = cp->tx_Mhqhead[ringNum];
	txd_fs = &cp->tx_Mhqring[ringNum][entry];
	entry = NEXT_TX(entry,  cp->re8670_tx_ring_size[ringNum]);
	txd_ls = &cp->tx_Mhqring[ringNum][entry];

	txd_fs_opts1 = txd_fs->opts1;
	txd_ls_opts1 = txd_ls->opts1;

	if(txd_fs_opts1&DescOwn || txd_ls_opts1&DescOwn){
		cp->cp_stats.rx_customized_tx_owned++;
		while(txd_fs->opts1&DescOwn || txd_ls->opts1&DescOwn)
		{
			delayTimes = 100;
			while(delayTimes)
			{
				delayTimes--;
				if(delayTimes==0)
					break;
			}
		}
		//goto KICK_TX;
	}

	len=(re8686_tx_ring_customized_func[gmac][ringNum])(cp, (struct tx_info *)txd_fs, len);

	//LS
	txd_ls_opts1 &= ~(0x1ffff|DescOwn|FirstFrag|LastFrag|TxCRC|IPCS);
	txd_ls_opts1 |= (len|DescOwn|LastFrag|TxCRC|IPCS);		//eliminate the broken RX_OFFSET bytes and fake DMAC
	txd_ls->opts1 = txd_ls_opts1;

	//FS
	wmb();
	txd_fs_opts1 |= (DescOwn|FirstFrag|TxCRC|IPCS); //this should be last updated!!!!!!!!!
	txd_fs->opts1 = txd_fs_opts1;

	//TXINFO_DBG(gmac, (struct tx_info *)txd_fs);if(debug_enable[gmac]&RTL8686_SKB_TX)memDump(txd_fs->addr, 80, "txd");
	//TXINFO_DBG(gmac, (struct tx_info *)txd_ls);if(debug_enable[gmac]&RTL8686_SKB_TX)memDump(txd_ls->addr, 80, "txd");

KICK_TX:
	cp->tx_Mhqhead[ringNum] = NEXT_TX(entry,  cp->re8670_tx_ring_size[ringNum]);
	
	//GMAC_SPIN_UNLOCK(&cp->tx_lock);

	//wmb();
	
	kick_tx(gmac, ringNum);

	return;
}

#endif
int re8686_customized_rx_and_tx(struct rtl8686_hwnat_customized_entry customized_entry, customized_rxPrepare_t rxPrepareFunc, customized_txPrepare_t txPrepareFunc, customized_rxHook_t rxHookFunc, customized_txHook_t txHookFunc)
{
#ifdef HWNAT_CUSTOMIZE
	int i,j;
	struct re_private *cp;		
	struct rx_info rxInfo;
	uint32 gmac,rxRingNum,txRingNum;
	unsigned char *rx_data_buffer=NULL;
		
	if((customized_entry.gmac >= MAX_GMAC_NUM)||(customized_entry.rxRingNum >= (MAX_RXRING_NUM-1))||(customized_entry.txRingNum >= (MAX_TXRING_NUM-1))||(customized_entry.type>=CUSTOMIZE_TYPE_MAX)) return -ERANGE;

	gmac=customized_entry.gmac;
	rxRingNum=customized_entry.rxRingNum;
	txRingNum=customized_entry.txRingNum;
		
	cp=&re_private_data[gmac];

	printk(KERN_CONT "re8686_customized_rx_and_tx gmac = %d rxRingNum = %d\n",gmac, rxRingNum);
	//prepare rxring buffer
	if(re8686_rx_ring_data_buffer[gmac][rxRingNum]==NULL){
		rx_data_buffer=kzalloc(cp->re8670_rx_ring_size[rxRingNum]*cp->rx_buff_size,GFP_ATOMIC);
		if(rx_data_buffer){
			dma_cache_inv((unsigned long)rx_data_buffer, cp->re8670_rx_ring_size[rxRingNum]*cp->rx_buff_size);
		}else
			return -ENOMEM;
	}
	else
	{
		printk(KERN_CONT "re8686_rx_ring_data_buffer[%d][%d] has been set before! But now need to reset!\n",gmac,rxRingNum);
		
		kfree(re8686_rx_ring_data_buffer[gmac][rxRingNum]);
		re8686_rx_ring_data_buffer[gmac][rxRingNum]=NULL;
		
		rx_data_buffer=kzalloc(cp->re8670_rx_ring_size[rxRingNum]*cp->rx_buff_size,GFP_ATOMIC);

		if(rx_data_buffer){
			dma_cache_inv((unsigned long)rx_data_buffer, cp->re8670_rx_ring_size[rxRingNum]*cp->rx_buff_size);
		}else{
			printk(KERN_CONT "[re8686_rx_ring_data_buffer]No memory!\n");
			return -ENOMEM;
		}
	}
	
	if(re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum]==NULL){
		re8686_customized_tx_descAddr_t *tx_descAddr_buffer=kzalloc(cp->re8670_rx_ring_size[rxRingNum]*sizeof(re8686_customized_tx_descAddr_t),GFP_ATOMIC);
		if(tx_descAddr_buffer){
			dma_cache_inv((unsigned long)tx_descAddr_buffer, cp->re8670_rx_ring_size[rxRingNum]*sizeof(re8686_customized_tx_descAddr_t));
			re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum]=tx_descAddr_buffer;		
		}else{
			printk(KERN_CONT "[re8686_rx_descIdx_customized_tx_descAddr]No memory!\n");
			return -ENOMEM;
		}
	}
	else
	{
			printk(KERN_CONT "re8686_rx_descIdx_customized_tx_descAddr[%d][%d] has been set before! But now need to reset!\n",gmac,rxRingNum);
	}
	
	printk(KERN_CONT "enter %s %s gmac%d rxRing%d txRing%d!! \n",__FUNCTION__,customized_entry.valid?"valid":"invalid",gmac,rxRingNum,txRingNum);
#if 1 //Wen: For sync VXLAN/NPTv6 fastforward issue, sync from luna_pro_cmcc_cu: [JIM][revision 39486]fix lock wait problem for nic acc data path.
	re8686_customized_rx_and_tx_used[gmac] = 1;
#endif
	if(customized_entry.valid){
		DMA_RX_DESC *desc;
		unsigned int tx_addr;

		GMAC_SPIN_LOCK(&cp->rx_lock);

		eth_close[gmac]=1;
		re8670_stop_hw(cp);

		
		re8686_rx_ring_customized_func[gmac][rxRingNum][customized_entry.type-1]=rxHookFunc;
		re8686_rx_ring_ext_pmsk[gmac][rxRingNum][customized_entry.type-1]=customized_entry.rx_ext_pmsk;
		re8686_tx_ring_customized_func[gmac][txRingNum]=txHookFunc;
		re8686_rx_ring_customized_preLen[gmac][rxRingNum]=customized_entry.rxPreLen;
		re8686_rx_ring_customized_tx_ringNum[gmac][rxRingNum]=txRingNum;

		for(j=0; j<MAX_RXRING_NUM ;j++){
			//clear all rx ring and enable the own bit since we reset the hardware.
			for(i=0; i<CHECK_TX_OWN_BIT_INTERVAL_NUM; i++)
				re8686_rx_ring_previousDesc[gmac][j][i]=-1;
			//if(re8686_rx_ring_data_buffer[gmac][j]){ //WEN: SVN 38896 from luna_pro_cmcc: Jim fix- fix synchronization problem between hw and sw when re8686_customized_rx_and_tx is used.
			{
				for(i=0; i<cp->re8670_rx_ring_size[j]; i++){
					if (i == (cp->re8670_rx_ring_size[j] - 1))			
						cp->rx_Mring[j][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
					else
						cp->rx_Mring[j][i].opts1 = (DescOwn | cp->rx_buff_size);
				}
				//reset tx ring head also
				cp->tx_Mhqhead[re8686_rx_ring_customized_tx_ringNum[gmac][j]]=0;
			}
		}
		
		//prepare rx desc
		for(i = 0; i < cp->re8670_rx_ring_size[rxRingNum]; i++){
			desc = &cp->rx_Mring[rxRingNum][i];

			desc->addr = (u32)(rx_data_buffer+i*cp->rx_buff_size) | UNCACHE_MASK;
			
			if(dynamic_sram_desc==0 && rxPrepareFunc)rxPrepareFunc(cp, (struct rx_info *)desc);
		
			if (i == (cp->re8670_rx_ring_size[rxRingNum] - 1))			
				cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
			else
				cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
		}

		re8670_init_hw(cp);
		eth_close[gmac]=0;

		//disable fc
		re8686_rx_ring_fc_state[gmac][rxRingNum] = cp->re8670_rx_flow_control_status[rxRingNum];
		re8686_set_flow_control(gmac, rxRingNum, OFF);
		
		GMAC_SPIN_UNLOCK(&cp->rx_lock);

		GMAC_SPIN_LOCK(&cp->tx_lock);

		re8686_tx_ring_customized[gmac][txRingNum]=1;
		
		memset(re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum], 0, cp->re8670_rx_ring_size[rxRingNum]*sizeof(re8686_customized_tx_descAddr_t));
		//refill tx desc
		if(dynamic_sram_desc>0 && txHookFunc)
		{
			for(i = 0; i < cp->re8670_tx_ring_size[txRingNum]; i++){

				apply_to_txdesc(&cp->tx_Mhqring[txRingNum][i], customized_entry.pCustomized_txInfo);

				if(i&0x1){
					//LS
					tx_addr=(u32)(rx_data_buffer+((i>>1)*cp->rx_buff_size)) | UNCACHE_MASK;
				
					cp->tx_Mhqring[txRingNum][i].opts1&=(~(DescOwn|FirstFrag|RingEnd));
					cp->tx_Mhqring[txRingNum][i].opts1|=LastFrag;
					cp->tx_Mhqring[txRingNum][i].addr=tx_addr+customized_entry.txInfo_addr_offset_v2;

					if (i == (cp->re8670_tx_ring_size[txRingNum] - 1))
						cp->tx_Mhqring[txRingNum][i].opts1|=RingEnd;

					//record tx desc addr
					re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum][(i-1)/2].ls_descAddr = (DMA_TX_DESC *)&cp->tx_Mhqring[txRingNum][i];
				}else{
					//FS
					tx_addr=(u32)(re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum]+((i>>1)*MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE)) | UNCACHE_MASK;
					
					cp->tx_Mhqring[txRingNum][i].opts1&=(~(0x1ffff|DescOwn|LastFrag|RingEnd));
					cp->tx_Mhqring[txRingNum][i].opts1|=(customized_entry.txPreLen|FirstFrag);
					cp->tx_Mhqring[txRingNum][i].addr=tx_addr;

					if(txPrepareFunc)txPrepareFunc(cp, (struct tx_info *)&cp->tx_Mhqring[txRingNum][i]);

					//record tx desc addr
					re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum][i/2].fs_descAddr = (DMA_TX_DESC *)&cp->tx_Mhqring[txRingNum][i];
				}
			}
		}
		else
		{
			re8686_tx_ring_addr_offset[gmac][rxRingNum][customized_entry.type-1]=customized_entry.txInfo_addr_offset_v1;
			//addr and opts1 is not necessary.
			re8686_tx_ring_txInfo[gmac][rxRingNum][customized_entry.type-1].opts2=customized_entry.pCustomized_txInfo->opts2;
			re8686_tx_ring_txInfo[gmac][rxRingNum][customized_entry.type-1].opts3=customized_entry.pCustomized_txInfo->opts3;
			re8686_tx_ring_txInfo[gmac][rxRingNum][customized_entry.type-1].opts4=customized_entry.pCustomized_txInfo->opts4;
			for(i = 0; i < cp->re8670_tx_ring_size[txRingNum]; i++){
				tx_addr=(u32)(rx_data_buffer+i*cp->rx_buff_size) | UNCACHE_MASK;

				apply_to_txdesc(&cp->tx_Mhqring[txRingNum][i], customized_entry.pCustomized_txInfo);

				cp->tx_Mhqring[txRingNum][i].opts1&=(~DescOwn);
				cp->tx_Mhqring[txRingNum][i].addr=tx_addr+((dynamic_sram_desc>0)?customized_entry.txInfo_addr_offset_v1:0);

				if (i == (cp->re8670_tx_ring_size[txRingNum] - 1))
					cp->tx_Mhqring[txRingNum][i].opts1|=RingEnd;

				//record tx desc addr
				re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum][i].fs_descAddr = (DMA_TX_DESC *)&cp->tx_Mhqring[txRingNum][i];
			}
		}

		GMAC_SPIN_UNLOCK(&cp->tx_lock);

		//20200729LUKE: after re8686_rx_ring_data_buffer is not null, the acceleration path will be entered!
		re8686_rx_ring_data_buffer[gmac][rxRingNum]=rx_data_buffer;
	}else{
		struct tx_info zeroInfo={0};
		GMAC_SPIN_LOCK(&cp->rx_lock);

		eth_close[gmac]=1;
		re8670_stop_hw(cp);

		re8686_rx_ring_customized_func[gmac][rxRingNum][customized_entry.type-1]=NULL;
		re8686_rx_ring_ext_pmsk[gmac][rxRingNum][customized_entry.type-1]=0;
		re8686_rx_ring_customized_preLen[gmac][rxRingNum]=0;
		re8686_rx_ring_customized_tx_ringNum[gmac][rxRingNum]=0;
		
		for(i = 0; i < cp->re8670_rx_ring_size[rxRingNum]; i++){
			cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
			if (i == (cp->re8670_rx_ring_size[rxRingNum] - 1))			
				cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
			else
				cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
			cp->rx_Mring[rxRingNum][i].opts2 = 0;
		}
		
		if(re8686_rx_ring_data_buffer[gmac][rxRingNum])kfree(re8686_rx_ring_data_buffer[gmac][rxRingNum]);
		re8686_rx_ring_data_buffer[gmac][rxRingNum]=NULL;
		
		if(re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum])kfree(re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum]);
		re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum]=NULL;

		re8670_init_hw(cp);
		eth_close[gmac]=0;

		//recovery fc
		re8686_set_flow_control(gmac, rxRingNum, re8686_rx_ring_fc_state[gmac][rxRingNum]);
		re8686_rx_ring_fc_state[gmac][rxRingNum] = ON;
		
		GMAC_SPIN_UNLOCK(&cp->rx_lock);

		GMAC_SPIN_LOCK(&cp->tx_lock);

		re8686_tx_ring_addr_offset[gmac][rxRingNum][customized_entry.type-1]=0;
		re8686_tx_ring_txInfo[gmac][rxRingNum][customized_entry.type-1]=zeroInfo;
		re8686_tx_ring_customized[gmac][txRingNum]=0;
		re8686_tx_ring_customized_func[gmac][txRingNum]=NULL;

		GMAC_SPIN_UNLOCK(&cp->tx_lock);
	}
#if 1 //wen: For sync VXLAN/NPTv6 fastforward issue, sync from luna_pro_cmcc_cu: [JIM][revision 39486]fix lock wait problem for nic acc data path.
	re8686_customized_rx_and_tx_used[gmac] = 0;
#endif

#endif
	return 0;
}


int re8686_customized_tx_stream_id(unsigned int gmac, unsigned int txRingNum, unsigned char streamID)
{
#ifdef HWNAT_CUSTOMIZE
	int i;
	struct re_private *cp;

	if((gmac >= MAX_GMAC_NUM)||(txRingNum >= (MAX_TXRING_NUM-1))||(streamID >= 0x7f)) return -ERANGE;
	
	cp=&re_private_data[gmac];
	
	//refill tx desc
	GMAC_SPIN_LOCK(&cp->tx_lock);
	for(i = 0; i < cp->re8670_tx_ring_size[txRingNum]; i++){
		cp->tx_Mhqring[txRingNum][i].opts3 &= ~(0x7f);
		cp->tx_Mhqring[txRingNum][i].opts3 |= TxPsel;
		cp->tx_Mhqring[txRingNum][i].opts3 |= streamID;
	}
	GMAC_SPIN_UNLOCK(&cp->tx_lock);
#endif
	return 0;
}

static int re8670_refill_rx (struct re_private *cp)
{
	unsigned int gmac = cp->gmac;
	unsigned int i, j;	

	for(j=0;j<MAX_RXRING_NUM;j++)
	{
		for (i = 0; i < cp->re8670_rx_ring_size[j]; i++) {
			struct sk_buff *skb;
			skb = re8670_getAlloc(cp->rx_buff_size);
			if (!skb)
				goto err_out;
			// Kaohj --- invalidate DCache for uncachable usage
			//ql_xu
			dma_cache_wback_inv((unsigned long)skb->data, cp->rx_buff_size);
			skb->len=0;
			skb->dev = ROOTDEV;
			cp->rx_skb[j][i].skb = skb;
			cp->rx_skb[j][i].frag = 0;
			if ((u32)skb->data &0x3)
				printk(KERN_DEBUG "skb->data unaligment %8x\n",(u32)skb->data);

			cp->rx_Mring[j][i].addr = (u32)skb->data|UNCACHE_MASK;      

			if (i == (cp->re8670_rx_ring_size[j] - 1))          
				cp->rx_Mring[j][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
			else
				cp->rx_Mring[j][i].opts1 =(DescOwn | cp->rx_buff_size);
			cp->rx_Mring[j][i].opts2 = 0;

		}    
	}

	return 0;
err_out:
	re8670_clean_rings(cp);
	return -ENOMEM;
}

static int re8670_refill_rx_desc(struct re_private *cp)
{
	unsigned int gmac = cp->gmac;
	unsigned int i, j;

	for(j=0;j<MAX_RXRING_NUM;j++)
	{
		for (i = 0; i < cp->re8670_rx_ring_size[j]; i++) {
			if (i == (cp->re8670_rx_ring_size[j] - 1))
				cp->rx_Mring[j][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
			else
				cp->rx_Mring[j][i].opts1 = (DescOwn | cp->rx_buff_size);
			cp->rx_Mring[j][i].opts2 = 0;
		}
	}

	return 0;
err_out:
	return -ENOMEM;
}

static void re8670_tx_timeout (struct net_device *dev, unsigned int txqueue)
{
	struct re_private_root *root_cp = DEV2CP(dev);
	struct re_private *cp;
	unsigned int gmac;
	
	printk(KERN_CONT "%s %d enter dev=%s\n", __func__, __LINE__, dev->name);
	
#ifdef TX_WATCHDOG_TIMEOUT_RESET
	re8670_reset();
#endif
	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		cp = root_cp->re_private_data_ptr[gmac];
		cp->cp_stats.tx_timeouts++;		
	}
	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);

	printk(KERN_CONT "%s %d exit\n", __func__, __LINE__);
}

static int re8670_init_rings (struct re_private *cp)
{
	int j;
	for(j=0;j<MAX_TXRING_NUM;j++){
		cp->tx_Mhqhead[j] = cp->tx_Mhqtail[j] = 0;
	}	

	for(j=0;j<MAX_RXRING_NUM;j++){
		cp->rx_Mtail[j] = 0;		
	}	
	return re8670_refill_rx (cp);
}

static int re8670_alloc_rings (struct re_private *cp)
{
	unsigned int gmac;
	void*	pBuf;
	int j;
	
	gmac = cp->gmac;
	for(j=0;j<MAX_RXRING_NUM;j++)
	{    
		pBuf = kzalloc(RE8670_RXRING_BYTES(cp->re8670_rx_ring_size[j]), GFP_ATOMIC);
		if (!pBuf)
			goto ErrMem;

		dma_cache_wback_inv((unsigned long)pBuf, RE8670_RXRING_BYTES(cp->re8670_rx_ring_size[j]));
		cp->rxdesc_Mbuf[j] = pBuf;

		pBuf = (void*)( (u32)(pBuf + DESC_ALIGN-1) &  ~(DESC_ALIGN -1) ) ;
		cp->rx_Mring[j] = (DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
		if(cp->re8670_rx_ring_size[j]) {
			pBuf=(struct ring_info*)kzalloc(sizeof(struct ring_info)*cp->re8670_rx_ring_size[j],GFP_ATOMIC);
			if (!pBuf)
				goto ErrMem;

			cp->rx_skb[j]= pBuf;
		}
	}

	for(j=0;j<MAX_TXRING_NUM;j++)
	{
		if(!cp->re8670_tx_ring_size[j])
			continue;

		pBuf = kzalloc(RE8670_TXRING_BYTES(cp->re8670_tx_ring_size[j]), GFP_ATOMIC);
		if (!pBuf)
			goto ErrMem;

		dma_cache_wback_inv((unsigned long)pBuf, RE8670_TXRING_BYTES(cp->re8670_tx_ring_size[j]));
		cp->txdesc_Mbuf[j] = pBuf;
		pBuf = (void*)( (u32)(pBuf + DESC_ALIGN-1) &  ~(DESC_ALIGN -1) ) ;
		cp->tx_Mhqring[j] = (DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
		pBuf =(struct ring_info*)kzalloc(sizeof(struct ring_info)*cp->re8670_tx_ring_size[j],GFP_ATOMIC);
		if (!pBuf)
			goto ErrMem;
		cp->tx_skb[j] = pBuf;
	}

	return re8670_init_rings(cp);

ErrMem:

	for(j=0;j<MAX_RXRING_NUM;j++)
	{
		if (cp->rx_skb[j])    
			kfree(cp->rx_skb[j]);
	}        

	for(j=0;j<MAX_TXRING_NUM;j++)
	{        
		if (cp->tx_skb[j])    
			kfree(cp->tx_skb[j]);	
	}
	
	return -ENOMEM;

}

static void re8670_clean_rings (struct re_private *cp)
{
	unsigned int gmac = cp->gmac;
	unsigned i,j;
	
	for (j = 0; j < MAX_RXRING_NUM; j++) {
		if(cp->rx_skb[j]){
			for (i = 0; i < cp->re8670_rx_ring_size[j]; i++) {
				if (cp->rx_skb[j][i].skb) {
					dev_kfree_skb(cp->rx_skb[j][i].skb);
				}
			}
			memset(cp->rx_skb[j], 0, sizeof(struct ring_info) * cp->re8670_rx_ring_size[j]);		
		}
	}
	for (j = 0; j < MAX_TXRING_NUM; j++) {
		if(cp->tx_skb[j]){
			for (i = 0; i < cp->re8670_tx_ring_size[j]; i++) {
				if (cp->tx_skb[j][i].skb) {
					struct sk_buff *skb = cp->tx_skb[j][i].skb;
					if ((u32)skb!=0xffffffff) {
						DEVPRIV(skb->dev)->net_stats.tx_dropped++;
						dev_kfree_skb(skb);
					}
				}
			}
			memset(cp->tx_skb[j], 0, sizeof(struct ring_info) * cp->re8670_tx_ring_size[j]);	
		}
	}
}

static void re8670_free_rings (struct re_private *cp)
{
	int j;
	re8670_clean_rings(cp);
	/*pci_free_consistent(cp->pdev, CP_RING_BYTES, cp->rx_ring, cp->ring_dma);*/

	for(j=0;j<MAX_RXRING_NUM;j++)
	{
		if (cp->rxdesc_Mbuf[j]) {
			kfree(cp->rxdesc_Mbuf[j]);
			cp->rxdesc_Mbuf[j] = NULL;
		}
		
		cp->rx_Mring[j] = NULL;   

		if (cp->rx_skb[j]) {   			
			kfree(cp->rx_skb[j]);
			cp->rx_skb[j] = NULL;
		}
		cp->rx_skb[j]=NULL;
	}     

	for(j=0;j<MAX_TXRING_NUM;j++)        
	{
		if (cp->txdesc_Mbuf[j]) {
			kfree(cp->txdesc_Mbuf[j]);
			cp->txdesc_Mbuf[j] = NULL;
		}
		
		cp->tx_Mhqring[j] = NULL;

		if (cp->tx_skb[j])   {			
			kfree(cp->tx_skb[j]);
			cp->tx_skb[j] = NULL;
		}
		cp->tx_skb[j]=NULL;	
	}    

}

static int re8670_open (struct net_device *dev)
{
	struct re_private_root *root_cp = DEV2CP(dev);
	struct re_private *cp;
	unsigned int gmac;
	int rc=0;	
#if defined(CONFIG_RTL9607C_SERIES) && defined(HWNAT_CUSTOMIZE)	
	unsigned int sramIsUsedByOthers = FALSE;	
#endif
#ifdef TX_KICK_RING_USING_POLLING
	int ring_num;
#endif

	if (netif_msg_ifup(root_cp))
		printk(KERN_DEBUG "%s: enabling interface\n", dev->name);

	if(dev_num == 0) {	
		printk(KERN_CONT "%s %d\n", __func__, __LINE__);
#if 0//defined(HWNAT_CUSTOMIZE)	
		_hwnat_customized_version_set(hwnat_customized_version);	
#endif	
#if 0//defined(CONFIG_RTL9607C_SERIES) && defined(HWNAT_CUSTOMIZE)	
		if(rtk_dynamic_sram_state_get()==ENABLED)	
		{	
			printk("\033[1;33;41m[WARNING] Sram is used by others, so skip dynamic sram settings for rx/tx desc @ %s(%d)\033[0m\n", __FUNCTION__, __LINE__);	
			sramIsUsedByOthers = TRUE;	
		}	
#endif
		for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
		{
			cp = root_cp->re_private_data_ptr[gmac];
			printk(KERN_CONT "%s %d\n", __func__, __LINE__);
			if(cp->gmac_enabled == GMAC_TRUE) 
			{
				rtk_gmac_set_rxbufsize(root_cp);	/* set new rx buf size */	
#if 0//defined(CONFIG_RTL9607C_SERIES)  && defined(HWNAT_CUSTOMIZE)	
				if(sramIsUsedByOthers==FALSE)	
						rtk_dynamic_sram_restart(gmac);	
#endif
#ifdef CONFIG_RTK_WFOAX
				if (gmac != WFO_GMAC_NO)
#endif
				{
				//rc = request_irq(dev->irq, (irq_handler_t)re8670_interrupt, IRQF_DISABLED, dev->name, dev);
				rc = request_irq(cp->irq, (irq_handler_t)re8670_interrupt, 0, root_cp->dev->name, cp);
				if (rc)
					goto err_out_hw;
				}
#ifdef RX_NAPI_MODE
				//netif_napi_add(root_cp->dev, &cp->napi, re8670_poll, cp->napi_budget);
				napi_enable(&cp->napi);
#endif

#ifdef CONFIG_RTK_WFOAX
				if (gmac != WFO_GMAC_NO)
#endif
				{
				re8670_init_hw(cp);
					re8670_init_trx_cdo(cp);
					re8670_refill_rx_desc(cp);
				}
				if(cp->stag_pid)
					RLE0787_R32(gmac, VLAN_REG) = cp->stag_pid;
				if(cp->stag_pid1)
					RLE0787_R32(gmac, VLAN1_REG) = cp->stag_pid1;

#ifdef TX_RECYCLE_SKB_USING_POLLING
				if(timer_pending(&cp->tok_polling_timer))
					timer_delete(&cp->tok_polling_timer);	
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
				init_timer(&cp->tok_polling_timer);
				cp->tok_polling_timer.data = cp;
				cp->tok_polling_timer.function = re8670_tx_all;
#else
				timer_setup(&cp->tok_polling_timer, re8670_tx_all, 0);
#endif
				mod_timer(&cp->tok_polling_timer, jiffies+1);
#endif
#ifdef TX_KICK_RING_USING_POLLING
				if(timer_pending(&cp->tx_ring_active_polling_timer))
					timer_delete(&cp->tx_ring_active_polling_timer);	
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
				init_timer(&cp->tx_ring_active_polling_timer);
				cp->tx_ring_active_polling_timer.data = cp;
				cp->tx_ring_active_polling_timer.function = re8670_tx_ring_active_poll;
#else
				if (cp->gmac==0)
					timer_setup(&cp->tx_ring_active_polling_timer, re8670_tx_ring_active_poll_gmac0, 0);
				else if (cp->gmac==1)
					timer_setup(&cp->tx_ring_active_polling_timer, re8670_tx_ring_active_poll_gmac1, 0);
				else if (cp->gmac==2)
					timer_setup(&cp->tx_ring_active_polling_timer, re8670_tx_ring_active_poll_gmac2, 0);
#endif
				if(timer_pending(&cp->tx_tdu_kick_polling_timer))
					timer_delete(&cp->tx_tdu_kick_polling_timer);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
				init_timer(&cp->tx_tdu_kick_polling_timer);
				cp->tx_tdu_kick_polling_timer.data = cp;
				cp->tx_tdu_kick_polling_timer.function = re8670_tx_tdu_kick_poll;
#else
				if (cp->gmac==0)
					timer_setup(&cp->tx_tdu_kick_polling_timer, re8670_tx_tdu_kick_poll_gmac0, 0);
				else if (cp->gmac==1)
					timer_setup(&cp->tx_tdu_kick_polling_timer, re8670_tx_tdu_kick_poll_gmac1, 0);
				else if (cp->gmac==2)
					timer_setup(&cp->tx_tdu_kick_polling_timer, re8670_tx_tdu_kick_poll_gmac2, 0);
#endif
#endif
			}
		}
	}

	dev_num++;
	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		cp = root_cp->re_private_data_ptr[gmac];
		
		if(cp->gmac_enabled != GMAC_TRUE)
			continue;
		
		cp->eth_close = GMAC_FALSE;
	}
#ifdef CONFIG_AUTO_DHCP_CHECK
	cp = root_cp->re_private_data_ptr[0];
	if(cp->gmac_enabled == GMAC_TRUE) {
	    int num = 0;
        struct net_device *dev_tmp = NULL;
	    for (num=0; num<SW_PORT_NUM; num++)
	    {
			dev_tmp = cp->port2dev[num];
            if (dev_tmp && (dev==dev_tmp)) {
            	//rtk_port_adminEnable_set(num, 1);
            	rtk_port_phyPowerDown_set(num, 0);
				printk(KERN_CONT "%s[%d], num=%d\n", __FUNCTION__, __LINE__, num);
            }
	    }
	}
#endif
	netif_start_queue(dev);

	return 0;

err_out_hw:	
	return rc;
}

static int re8670_close (struct net_device *dev)
{
	struct re_private_root *root_cp = DEV2CP(dev);
	struct re_private 	   *cp;
	unsigned int gmac;

	dev_num--;	

	if(dev_num == 0)
	{
		netif_stop_queue(dev);
		for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
		{
			cp = root_cp->re_private_data_ptr[gmac];
			if(cp->gmac_enabled == GMAC_TRUE) 
			{			
				cp->eth_close = GMAC_TRUE;
				re8670_stop_hw(cp);

				printk(KERN_CONT "%s %d irq=%d name=%s\n", __func__, __LINE__, cp->irq, root_cp->dev->name);
				irq_set_affinity_hint(cp->irq, NULL);
				free_irq(cp->irq, cp);	
#ifdef RX_NAPI_MODE
				//netif_napi_add(root_cp->dev, &cp->napi, re8670_poll, cp->napi_budget);
				napi_disable(&cp->napi);
#endif
				printk(KERN_CONT "%s %d\n", __func__, __LINE__);
				cp->stag_pid = RLE0787_R32(cp->gmac, VLAN_REG);
				cp->stag_pid1 = RLE0787_R32(cp->gmac, VLAN1_REG);
			}
		}
	}
#ifdef CONFIG_AUTO_DHCP_CHECK	
	cp = root_cp->re_private_data_ptr[0];
	if(cp->gmac_enabled == GMAC_TRUE) 
	{		
		int num = 0;
		struct net_device *dev_tmp = NULL;

		for (num=0; num<SW_PORT_NUM; num++)
		{
			dev_tmp = cp->port2dev[num];
			if (dev_tmp && (dev==dev_tmp)) {
				//rtk_port_adminEnable_set(num, 0);
				rtk_port_phyPowerDown_set(num, 1);
				printk(KERN_CONT "%s[%d], num=%d\n", __FUNCTION__, __LINE__, num);
			}
		}
	}
#endif	
	if (netif_msg_ifdown(root_cp))
		printk(KERN_DEBUG "%s: disabling interface\n", dev->name);

	return 0;
}

int re8670_ioctl (struct net_device *dev, struct ifreq *rq, int cmd)
{
	int rc = 0;

	if (!netif_running(dev) && cmd!=SIOCETHTEST)
		return -EINVAL;

	switch (cmd) {
#if 1
		case SIOCETHTEST:
			//eth_ctl((struct eth_arg *)rq->ifr_data);	// FIXME
			break;
#endif
		default:
			rc = -EOPNOTSUPP;
			break;
	}

	return rc;
}

static ssize_t dbg_level_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int gmac;
	unsigned char tmpBuf[16] = {0};
	int len = (count > 15) ? 15 : count;	
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}
	
	gmac = data->gmac;
	if (buf && !copy_from_user(tmpBuf, buf, len))
	{
		data->debug_enable=simple_strtoul(tmpBuf, NULL, 16);
		printk(KERN_CONT "write debug_enable to 0x%08x\n", data->debug_enable);
		return count;
	}
	return -EFAULT;
}

static ssize_t dbg_times_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int gmac;
	unsigned char tmpBuf[16] = {0};
	int len = (count > 15) ? 15 : count;	
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}
	
	gmac = data->gmac;
	if (buf && !copy_from_user(tmpBuf, buf, len))
	{
		data->debug_times=simple_strtoul(tmpBuf, NULL, 16);
		printk(KERN_CONT "write debug_times to %d times\n", data->debug_times);
		return count;
	}
	return -EFAULT;
}

static int memrw_read(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	return 0;
}

static ssize_t memrw_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	char 		tmpbuf[64];
	unsigned int	*mem_addr, mem_data, mem_len;
	char		*strptr, *cmd_addr;
	char		*tokptr;
	unsigned int ring_num, i;
	unsigned int start_idx, end_idx;
	static struct re_private *data;
	unsigned int gmac;
	size_t len;

	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	len = min(count, sizeof(tmpbuf) - 1);
	gmac = data->gmac;
	if (buf && !copy_from_user(tmpbuf, buf, len)) {
		tmpbuf[len] = '\0';
		if (!str_valid(tmpbuf))
			return -EINVAL;

		strptr=tmpbuf;
		cmd_addr = strsep(&strptr," ");
		if (cmd_addr==NULL)
		{
			goto errout;
		}
		printk(KERN_CONT "cmd %s\n", cmd_addr);
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errout;
		}

#ifdef TX_RING_DEBUG
		if (!memcmp(cmd_addr, "txr", 3))
		{
			if(!data->tx_ring_backup_debug)
			{
				printk(KERN_CONT "%s %d invalid tx_ring_backup_debug !\n", __func__, __LINE__);
				goto errout;
			}
			ring_num=(unsigned int*)simple_strtol(tokptr, NULL, 0);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			start_idx=simple_strtol(tokptr, NULL, 0);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			end_idx=simple_strtol(tokptr, NULL, 0);
			if(ring_num >= MAX_TXRING_NUM)
			{
				printk(KERN_CONT "%s %d invalid ring_num !\n", __func__, __LINE__);
				goto errout;
			}
			if(start_idx >= re_private_data[gmac].re8670_tx_ring_size[ring_num])
			{
				printk(KERN_CONT "%s %d invalid start_idx !\n", __func__, __LINE__);
				goto errout;
			}
			if(end_idx >= re_private_data[gmac].re8670_tx_ring_size[ring_num])
			{
				printk(KERN_CONT "%s %d invalid end_idx !\n", __func__, __LINE__);
				goto errout;
			}
			for(i=start_idx ; i<=end_idx ; i++)
			{
				printk(KERN_CONT "########################ring[%d] [i=%d]########################", ring_num, i);
				if(((DMA_TX_DESC *)&re_private_data[gmac].rtl8686_tx_ring_debug[ring_num].txDescriptor[i])->opts1&0x80000000)

				memDump(&re_private_data[gmac].rtl8686_tx_ring_debug[ring_num].dataBuffer[i], ((DMA_TX_DESC *)&re_private_data[gmac].rtl8686_tx_ring_debug[ring_num].txDescriptor[i])->opts1&0x1ffff, "", RTL8686_MEM_DUMP_FORMAT_ALL);
				printk(KERN_CONT "#################################################################\n");
			}
		}
		else
#endif
		if(!memcmp(cmd_addr, "r_addr_hex_text", 15))
		{
			mem_addr=(unsigned int*)simple_strtol(tokptr, NULL, 0);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			mem_len=simple_strtol(tokptr, NULL, 0);
			memDump(mem_addr, mem_len, "", RTL8686_MEM_DUMP_FORMAT_ALL);
		}
		else if (!memcmp(cmd_addr, "r_addr_hex", 10))
		{
			mem_addr=(unsigned int*)simple_strtol(tokptr, NULL, 0);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			mem_len=simple_strtol(tokptr, NULL, 0);
			memDump(mem_addr, mem_len, "", RTL8686_MEM_DUMP_FORMAT_ADDR | RTL8686_MEM_DUMP_FORMAT_HEX);
		}
		else if (!memcmp(cmd_addr, "r_hex", 5))
		{
			mem_addr=(unsigned int*)simple_strtol(tokptr, NULL, 0);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			mem_len=simple_strtol(tokptr, NULL, 0);
			memDump(mem_addr, mem_len, "", RTL8686_MEM_DUMP_FORMAT_HEX);
		}
		else if (!memcmp(cmd_addr, "r_text", 5))
		{
			mem_addr=(unsigned int*)simple_strtol(tokptr, NULL, 0);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			mem_len=simple_strtol(tokptr, NULL, 0);
			memDump(mem_addr, mem_len, "", RTL8686_MEM_DUMP_FORMAT_TEXT);
		}
		else if (!memcmp(cmd_addr, "r", 1))
		{
			mem_addr=(unsigned int*)simple_strtol(tokptr, NULL, 0);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			mem_len=simple_strtol(tokptr, NULL, 0);
			memDump(mem_addr, mem_len, "", RTL8686_MEM_DUMP_FORMAT_ALL);
		}
		else if (!memcmp(cmd_addr, "w", 1))
		{
			mem_addr=(unsigned int*)simple_strtol(tokptr, NULL, 0);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			mem_data=simple_strtol(tokptr, NULL, 0);
			WRITE_MEM32(mem_addr, mem_data);
			printk(KERN_CONT "Write memory 0x%p dat 0x%x: 0x%x\n", mem_addr, mem_data, READ_MEM32(mem_addr));
		}
		else
		{
			goto errout;
		}
	}
	else
	{
errout:
		printk(KERN_CONT "Memory operation only support \"r\" and \"w\" as the first parameter\n");
		printk(KERN_CONT "Read format:	\"r mem_addr length\"\n");
		printk(KERN_CONT "Write format:	\"w mem_addr mem_data\"\n");
#ifdef TX_RING_DEBUG
		printk(KERN_CONT "Tx ring debug data buffer format:	\"txr ring_num start_idx end_idx\"\n");
#endif
	}
	return count;
}
#ifdef CONFIG_RTL8686_SWITCH
#ifndef CONFIG_RTK_L34_ENABLE
static int switch_mode_read(struct seq_file *seq, void *v)
{	  
	  switch(SWITCH_MODE)
	  {
		case RTL8686_Switch_Mode_Trap2Cpu:
			printk(KERN_CONT "Asic switch mode : trap2cpu\n");
			break;
		case RTL8686_Switch_Mode_Normal:
			printk(KERN_CONT "Asic switch mode : normal\n");
			break;
		default:
			printk(KERN_CONT "Asic switch mode : Unknown\n");
	  }      
      return 0;
}
static int switch_control_set_mode(int mode)
{
	if( mode!=RTL8686_Switch_Mode_Trap2Cpu
		&& mode!=RTL8686_Switch_Mode_Normal)
		return -1;	
	SWITCH_MODE = mode;
	return 0;
}
#define SWITCH_PORT_NUM 5
static int switch_normal(void)
{
	int ret = RT_ERR_FAILED;
	rtk_acl_ingress_entry_t aclRule;
	memset(&aclRule, 0, sizeof(rtk_acl_ingress_entry_t)); 
    aclRule.index = 0;	
	if((ret = rtk_acl_igrRuleEntry_del(aclRule.index))!= RT_ERR_OK)
	{
		printk(KERN_CONT "%s-%d error rtk_acl_igrRuleEntry_del index %d\n",__func__,__LINE__,aclRule.index);
		return ret; 	
	}
	return 0;
}
static int switch_trap2cpu(void)
{
	int ret = RT_ERR_FAILED;
	rtk_acl_ingress_entry_t aclRule;	
	unsigned int port;
	memset(&aclRule, 0, sizeof(rtk_acl_ingress_entry_t)); 
	//default set to acl index 0, if set trap2cpu
    aclRule.index = 0;	
    aclRule.templateIdx = 0;
	//aclRule.activePorts.bits[0] = 1 << 0; /* port 0*/
   	for(port=0;port < SWITCH_PORT_NUM ; port ++)
		aclRule.activePorts.bits[0] |= (1 << port);
	aclRule.valid = PADDING_ENABLED;
	aclRule.act.enableAct[ACL_IGR_FORWARD_ACT]= PADDING_ENABLED;
	aclRule.act.forwardAct.act = ACL_IGR_FORWARD_TRAP_ACT;
	if((ret = rtk_acl_igrRuleEntry_add(&aclRule))!= RT_ERR_OK) 
	{
		printk(KERN_CONT "%s-%d error rtk_acl_igrRuleEntry_add index %d\n",__func__,__LINE__,aclRule.index);
		return ret; 
	}
   	for(port=0;port < SWITCH_PORT_NUM ; port ++)
   	{ 
		rtk_acl_igrState_set(port,PADDING_ENABLED);
	}
	return 0;
}
static ssize_t switch_mode_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	char 	tmpbuf[512];
	char		*strptr;	
	int retval = -1;
	static struct re_private *data;
	unsigned int gmac;
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;	
	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		tmpbuf[count] = '\0';
		strptr=tmpbuf;
		if(strlen(strptr)==0)
		{
			goto errout;
		}
		if(strncmp(strptr, "trap2cpu",8) == 0)
		{
			retval = switch_control_set_mode(RTL8686_Switch_Mode_Trap2Cpu);
			vlan_detag(gmac, GMAC_ON);
			retval = switch_trap2cpu();
#ifdef CONFIG_RTL_MULTI_LAN_DEV			
			change_dev_port_mapping(LAN_PORT1,"eth0.2");
			change_dev_port_mapping(LAN_PORT2,"eth0.3");
			change_dev_port_mapping(LAN_PORT3,"eth0.4");
			change_dev_port_mapping(LAN_PORT4,"eth0.5");
			change_dev_port_mapping(LAN_PORT5,"eth0.6");
			change_dev_port_mapping(LAN_PORT6,"eth0.7");
			change_dev_port_mapping(WAN_PORT,"nas0");						
			#if defined(CONFIG_RTL_MULTI_PHY_ETH_WAN)
			change_dev_port_mapping(LAN_PORT6,"ifprobe");
			#endif
#endif						
		}
		else if(strncmp(strptr, "normal",6) == 0)
		{
			retval = switch_control_set_mode(RTL8686_Switch_Mode_Normal);
			vlan_detag(gmac, GMAC_OFF);
			retval = switch_normal();
#ifdef CONFIG_RTL_MULTI_LAN_DEV			
			change_dev_port_mapping(LAN_PORT1,"eth0.2");
			change_dev_port_mapping(LAN_PORT2,"eth0.3");
			change_dev_port_mapping(LAN_PORT3,"eth0.4");
			change_dev_port_mapping(LAN_PORT4,"eth0.5");
			change_dev_port_mapping(LAN_PORT5,"eth0.6");
			change_dev_port_mapping(LAN_PORT6,"eth0.7");
			change_dev_port_mapping(WAN_PORT,"nas0");
			#if defined(CONFIG_RTL_MULTI_PHY_ETH_WAN)
			change_dev_port_mapping(LAN_PORT6,"ifprobe");
			#endif
#endif	
		}
		else
		{
			goto errout;
		}
		if(retval==0)
			printk(KERN_CONT "write success ! \n");
		else
			printk(KERN_CONT "error : change mode fail ! \n");
	}
	else
	{
errout:
		printk(KERN_CONT "error input  (trap/normal)\n");
	}
	return count;
}
#endif //CONFIG_RTK_L34_ENABLE

static void rtl_ethtool_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *drvinfo)
{
	strscpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strscpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version));
	strscpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strscpy(drvinfo->bus_info, "platform", sizeof(drvinfo->bus_info));
	drvinfo->n_stats = RTL_NUM_STATS;
}

static u32 rtl_ethtool_get_port_link(struct net_device *dev)
{
	//unsigned int i;
	u32 txportmask = DEVPRIV(dev)->txPortMask;
	rtk_port_linkStatus_t LinkStatus;
	int portnum;

	if(txportmask == 0){//eth0, always link, have problem?
		return 1;
	}
#if 0
	for(i=0;i<SW_PORT_NUM;i++){
		if(txportmask & (1<<i)){
			//return get_port_link(i);//first match bit
			return 1;
		}
	}
#endif
	//krammer change to default link up
	//return 0;//default link down
	for(portnum = 0; portnum < SW_PORT_NUM; ++portnum)
	{
		if(txportmask & (1 << portnum))
			break;
	}
		
	if(rtk_port_link_get(portnum, &LinkStatus) != RT_ERR_OK){
		printk(KERN_CONT "\n %s %d\n", __FUNCTION__, __LINE__);
	}
#if 0
	if (0 == LinkStatus)
		printk(KERN_CONT "port %d is down.\n", portnum);
	else if (1 == LinkStatus)
		printk(KERN_CONT "port %d is up.\n", portnum);
	else
		printk(KERN_CONT "port %d is chaos\n", portnum);
#endif
	return LinkStatus;//default link down
}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(4,4,0))
static int rtl_ethtool_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
    rtk_port_speed_t linkSpeed;
    rtk_port_duplex_t linkDuplex;
	u32 txportmask = DEVPRIV(dev)->txPortMask;
	unsigned int portnum;
	
	if (0 == txportmask)
		return 1;

	for(portnum = 0; portnum < SW_PORT_NUM; ++portnum)
	{
		if(txportmask & (1 << portnum))
			break;
	}
	if (portnum >= SW_PORT_NUM)
		return -EINVAL;

	memset(&linkSpeed, 0, sizeof(rtk_port_speed_t));
	memset(&linkDuplex, 0, sizeof(rtk_port_duplex_t));

	rtk_port_speedDuplex_get(portnum, &linkSpeed, &linkDuplex);
	if (linkSpeed == PORT_SPEED_10M)
		ecmd->speed = SPEED_10;
	else if (linkSpeed == PORT_SPEED_100M)
		ecmd->speed = SPEED_100;
	else
		ecmd->speed = SPEED_1000;
	ecmd->duplex = linkDuplex;
	return 0;	
}

static int rtl_ethtool_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	u32 txportmask = DEVPRIV(dev)->txPortMask;
	unsigned int portnum;
	rtk_port_phy_ability_t ability;

	if (0 == txportmask)
		return 1;

	for(portnum = 0; portnum < SW_PORT_NUM; ++portnum)
	{
		if(txportmask & (1 << portnum))
			break;
	}
	
	if (portnum >= SW_PORT_NUM)
		return -EINVAL;
	
	memset(&ability, 0, sizeof(rtk_port_phy_ability_t));

	if (ecmd->autoneg)
	{
		ability.Half_10 = ENABLED;
		ability.Full_10 = ENABLED;
		ability.Half_100 = ENABLED;
		ability.Full_100 = ENABLED;
		ability.Full_1000 = ENABLED;
	}
	else
	{
		if ((ecmd->speed == SPEED_100) && (ecmd->duplex == DUPLEX_FULL))
			ability.Full_100 = ENABLED;
		else if ((ecmd->speed == SPEED_100) && (ecmd->duplex == DUPLEX_HALF))
			ability.Half_100 = ENABLED;
		else if ((ecmd->speed == SPEED_10) && (ecmd->duplex == DUPLEX_FULL))
			ability.Full_10 = ENABLED;
		else if ((ecmd->speed == SPEED_10) && (ecmd->duplex == DUPLEX_HALF))
			ability.Half_10 = ENABLED;
		else
			ability.Full_1000 = ENABLED;
	}

	rtk_port_phyAutoNegoAbility_set(portnum, &ability);
	
	return 0;
}
#endif
#else
static u32 rtl_ethtool_get_port_link(struct net_device *dev)
{
	return 1;//always link, have problem?
}
#endif

static const struct {
	const char string[ETH_GSTRING_LEN];
} ethtool_stats_keys[RTL_NUM_STATS] = {
	{ "RxUCPktCnt" },
	{ "RxMCFrmCnt" },
	{ "RxBCFrmCnt" },
	{ "RxOAMFrmCnt" },
	{ "RxJumboFrmCnt" },
	{ "RxPauseFrmCnt" },
	{ "RxUnKnownOCFrmCnt" },
	{ "RxCrcErrFrmCnt" },
	{ "RxUndersizeFrmCnt" },
	{ "RxRuntFrmCnt" },
	{ "RxOvSizeFrmCnt" },
	{ "RxJabberFrmCnt" },
	{ "RxInvalidFrmCnt" },
	{ "RxStatsFrm64oct" },
	{ "RxStatsFrm65to127oct" },
	{ "RxStatsFrm128to255oct" },
	{ "RxStatsFrm256to511oct" },
	{ "RxStatsFrm512to1023oct" },
	{ "RxStatsFrm1024to1518oct" },
	{ "RxStatsFrm1519to2100oct" },
	{ "RxStatsFrm2101to9200oct" },
	{ "RxStatsFrm9201toMaxoct" },
	{ "RxByteCount_Lo" },
	{ "RxByteCount_Hi" },
	{ "TxUCPktCnt" },
	{ "TxMCFrmCnt" },
	{ "TxBCFrmCnt" },
	{ "TxOAMFrmCnt" },
	{ "TxJumboFrmCnt" },
	{ "TxPauseFrmCnt" },
	{ "TxCrcErrFrmCnt" },
	{ "TxOvSizeFrmCnt" },
	{ "TxSingleColFrm" },
	{ "TxMultiColFrm" },
	{ "TxLateColFrm" },
	{ "TxExessColFrm" },
	{ "TxStatsFrm64oct" },
	{ "TxStatsFrm65to127oct" },
	{ "TxStatsFrm128to255oct" },
	{ "TxStatsFrm256to511oct" },
	{ "TxStatsFrm512to1023oct" },
	{ "TxStatsFrm1024to1518oct" },
	{ "TxStatsFrm1519to2100oct" },
	{ "TxStatsFrm2101to9200oct" },
	{ "TxStatsFrm9201toMaxoct" },
	{ "TxByteCount_Lo" },
	{ "TxByteCount_Hi" }
};

static void rtl_ethtool_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch(stringset) {
	case ETH_SS_STATS:
		memcpy(data, &ethtool_stats_keys, sizeof(ethtool_stats_keys));
		break;
	}
}

static void rtl_ethtool_update_stats(struct net_device *dev, struct rtl_ethtool_stats *rtl_stats_p)
{
	struct re_private_root *root_cp = DEV2CP(dev);
#ifdef CONFIG_RTL_MULTI_LAN_DEV
#ifdef CONFIG_COMMON_RT_API
	rt_stat_port_cntr_t pPortCntrs;
	rt_gpon_pm_type_t pmCntType;
	rt_gpon_pm_counter_t pPmCnt;
	rt_port_t port;
#endif
	int i;
#endif
	
	unsigned long flags;

	spin_lock_irqsave(&root_cp->stats_lock, flags);

#ifdef CONFIG_RTL_MULTI_LAN_DEV
	if(dev->rtk_priv_flags&RTK_IFF_DOMAIN_WAN || dev->rtk_priv_flags&RTK_IFF_DOMAIN_ELAN
#ifdef CONFIG_RTL_SMUX_DEV
	&& !(dev->rtk_priv_flags & RTK_IFF_VSMUX)
#endif
	)
	{
		for(i=0 ; i<10 ; i++)
		{
			if(DEVPRIV(dev)->txPortMask&(1<<i))
			{
#ifdef CONFIG_COMMON_RT_API
				port = i;
				if (rt_stat_port_getAll(port, &pPortCntrs) == RT_ERR_OK)
				{
					rtl_stats_p->rxbytecount_lo = pPortCntrs.ifInOctets;
					rtl_stats_p->rxinvalidfrmcnt = pPortCntrs.ifInDiscards;
					rtl_stats_p->rxucpktcnt = pPortCntrs.ifInUcastPkts;
					rtl_stats_p->rxmcfrmcnt = pPortCntrs.ifInMulticastPkts;
					rtl_stats_p->rxbcfrmcnt = pPortCntrs.ifInBroadcastPkts;
					rtl_stats_p->txbytecount_lo = pPortCntrs.ifOutOctets;
					rtl_stats_p->txucpktcnt = pPortCntrs.ifOutUcastPkts;
					rtl_stats_p->txmcfrmcnt = pPortCntrs.ifOutMulticastPkts;
					rtl_stats_p->txbcfrmcnt = pPortCntrs.ifOutBrocastPkts;	
					rtl_stats_p->txpausefrmcnt = pPortCntrs.dot3OutPauseFrames;
					rtl_stats_p->rxpausefrmcnt = pPortCntrs.dot3InPauseFrames;
					rtl_stats_p->txcrcerrfrmcnt = pPortCntrs.ifOutDiscards;
					break;
				}
#endif
			}
		}
	}
#endif

	spin_unlock_irqrestore(&root_cp->stats_lock, flags);
}

static void rtl_ethtool_get_ethtool_stats(struct net_device *dev, struct ethtool_stats *stats, u64 *data)
{
	struct re_private_root *root_cp = DEV2CP(dev);
	struct rtl_ethtool_stats rtl_stats;

	memset(&rtl_stats, 0, sizeof(rtl_stats));
	rtl_ethtool_update_stats(dev, &rtl_stats);

	memcpy(data, &rtl_stats, sizeof(rtl_stats));
}

static int rtl_ethtool_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return RTL_NUM_STATS;
	}

	return -EOPNOTSUPP;
}

static const struct ethtool_ops rtl_ethtool_ops = {
	.get_drvinfo		= rtl_ethtool_get_drvinfo,
	.get_link 			= rtl_ethtool_get_port_link,
#if defined(CONFIG_RTL8686_SWITCH) && (LINUX_VERSION_CODE <= KERNEL_VERSION(4,4,0))
	.get_settings 		= rtl_ethtool_get_settings,
	.set_settings       = rtl_ethtool_set_settings,
#endif
	.get_strings		= rtl_ethtool_get_strings,
	.get_ethtool_stats	= rtl_ethtool_get_ethtool_stats,
	.get_sset_count		= rtl_ethtool_get_sset_count,
};

void rtl_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &rtl_ethtool_ops;
}

#ifdef TX_INTR_HANDLE
static void tx_int_mitigation_set(unsigned int gmac, unsigned int pkts)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	unsigned int value, mask;
	
	if(!pkts) {
		printk(KERN_CONT "%s %d ERROR ! pkts is NULL\n", __func__, __LINE__);
		return;
	}

	printk(KERN_CONT "%s %d pkts=%d\n", __func__, __LINE__, pkts);
	mask = TX_MIT_MASK;
	if(pkts==1) {
		value = TX_MIT_1PKT;
	}else if(pkts==4) {
		value = TX_MIT_4PKT;
	}else if(pkts==8) {
		value = TX_MIT_8PKT;
	}else if(pkts==12) {
		value = TX_MIT_12PKT;
	}else if(pkts==16) {
		value = TX_MIT_16PKT;
	}else if(pkts==20) {
		value = TX_MIT_20PKT;
	}else if(pkts==24) {
		value = TX_MIT_24PKT;
	}else if(pkts==28) {
		value = TX_MIT_28PKT;
	}else {
		printk(KERN_CONT "%s %d ERROR ! No such case\n", __func__, __LINE__);
		return;
	}

	cp->iocmd_reg &= ~(mask);
	cp->iocmd_reg |= value;
	RLE0787_W32(gmac, IO_CMD, cp->iocmd_reg);
	printk(KERN_CONT "%s %d set OK\n", __func__, __LINE__);
	return;
}

static void tx_int_mitigation_timer_set(unsigned int gmac, unsigned int tus)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	unsigned int value, mask;
	
	if(tus<0x0 || tus>0xf) {
		printk(KERN_CONT "%s %d ERROR ! No such case tus=0x%x\n", __func__, __LINE__, tus);
		return;
	}

	mask = TX_PKT_TIMER_MASK;
	value = (tus << 24);

	cp->iocmd_reg &= ~(mask);
	cp->iocmd_reg |= value;
	RLE0787_W32(gmac, IO_CMD, cp->iocmd_reg);
	printk(KERN_CONT "%s %d set OK\n", __func__, __LINE__);
	return;
}
#endif

static int misc_read(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int rx_ring_idx;
	
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}
	printk(KERN_CONT "spin lock type: ");
#if (GMAC_SPIN_LOCK_TYPE==GMAC_SPIN_LOCK_TYPE_BH)
	printk(KERN_CONT "BH\n");
#else
	printk(KERN_CONT "IRQ_SAVE\n");
#endif
	printk(KERN_CONT "Rx multiple ring interrpt split: ");
#ifdef RX_MRING_INT_SPLIT
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Rx NAPI mode: ");
#ifdef RX_NAPI_MODE
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Rx NAPI mode debug: ");
#ifdef RX_NAPI_MODE_DEBUG
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Tx ring debug: ");
#ifdef TX_RING_DEBUG
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Tx create test packet debug: ");
#ifdef TX_CREATE_TEST_PACKET_DEBUG
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Tx watchdog timeout reset: ");
#ifdef TX_WATCHDOG_TIMEOUT_RESET
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Tx HW LSO: ");
#ifdef CONFIG_REALTEK_HW_LSO
	printk(KERN_CONT "Enabled\n");
	printk(KERN_CONT "	-GMAC mode: ");
#if	GMAC_MODE==MODE_PURE_SW
	printk(KERN_CONT "MODE_PURE_SW\n");
#elif GMAC_MODE==MODE_SG_GSO
	printk(KERN_CONT "MODE_SG_GSO\n");
#elif GMAC_MODE==MODE_HW_LSO
	printk(KERN_CONT "MODE_HW_LSO\n");
#elif GMAC_MODE==MODE_HW_LSO_SG
	printk(KERN_CONT "MODE_HW_LSO_SG\n");
#endif
	printk(KERN_CONT "	-HW_LSO_ENABLE: ");
#ifdef HW_LSO_ENABLE
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "	-HW_CHECKSUM_OFFLOAD: ");
#ifdef HW_CHECKSUM_OFFLOAD
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "	-LINUX_LSO_ENABLE: ");
#ifdef LINUX_LSO_ENABLE
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "	-LINUX_SG_ENABLE: ");
#ifdef LINUX_SG_ENABLE
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Local service accelerate: ");
#ifdef LOCAL_SERVICE_ACCELERATE
	printk(KERN_CONT "%s\n", data->local_service_acc_enable?"Enabled":"Disabled");
#else
	printk(KERN_CONT "Not active\n");
#endif
	printk(KERN_CONT "Tx handle interrpts: ");
#ifdef TX_INTR_HANDLE
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Tx recycle skb using TOK interrpt: ");
#ifdef TX_RECYCLE_SKB_USING_TOK_INT
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Tx kick ring using TDU interrpt: ");
#ifdef TX_KICK_RING_USING_TDU_INT
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Tx recycle skb using timer polling: ");
#ifdef TX_RECYCLE_SKB_USING_POLLING
	printk(KERN_CONT "Enabled\n");
	printk(KERN_CONT "	-polling per 10mseconds: %d\n", TX_RECYCLE_SKB_POLLING_10MSECONDS);
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Tx kick ring using timer polling: ");
#ifdef TX_KICK_RING_USING_POLLING
	printk(KERN_CONT "Enabled\n");
	printk(KERN_CONT "	-polling ring active once per %d sec\n", tx_ring_active_poll_sec);
	printk(KERN_CONT "	-check TDU once per %d 10msec\n", tx_tdu_kick_ring_10msec);
#else
	printk(KERN_CONT "Disabled\n");
#endif
#ifdef CONFIG_RTL8686_SWITCH
	printk(KERN_CONT "Rx pause by software enable: ");
	if(re_private_data_root.rx_pause_by_software_enable)
		printk(KERN_CONT "Enabled\n");
	else
		printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "iocmd_reg 0x%x\n", data->iocmd_reg);
	printk(KERN_CONT "iocmd1_reg 0x%x\n", data->iocmd1_reg);
	printk(KERN_CONT "	tx_jumbo_frame_enabled: %s\n", data->tx_jumbo_frame_enabled?"Enabled":"Disabled");
	printk(KERN_CONT "	rx_save_int_prio_to_skb: ");
#ifdef CONFIG_RTK_CONTROL_PACKET_PROTECTION
	printk(KERN_CONT "%s\n", data->rx_save_int_prio_to_skb?"Enabled":"Disabled");
#else
	printk(KERN_CONT "Not active\n");
#endif
#ifdef TX_RING_DEBUG
	printk(KERN_CONT "	tx_ring_backup_debug: %s\n", data->tx_ring_backup_debug?"Enabled":"Disabled");
#endif
#ifdef TX_CREATE_TEST_PACKET_DEBUG
	printk(KERN_CONT "tx_packet_test: %d\n", data->test_packet_start);
#endif
#ifdef TX_INTR_HANDLE
	printk(KERN_CONT "Tx recycle using interrupt mode\n");
	printk(KERN_CONT "	-tx_int_mitigation: 0x%x\n", data->tx_int_mitigation);
	printk(KERN_CONT "	-tx_int_mitigation_timer: 0x%x\n", data->tx_int_mitigation_timer);
#endif
	printk(KERN_CONT "Tx using backup ring: ");
#ifdef TX_BACKUP_RING
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	printk(KERN_CONT "Tx using backup GMAC: ");
#ifdef TX_BACKUP_GMAC
	printk(KERN_CONT "Enabled\n");
#else
	printk(KERN_CONT "Disabled\n");
#endif
	for(rx_ring_idx=0 ; rx_ring_idx<MAX_RXRING_NUM ; rx_ring_idx++)
	{
		printk(KERN_CONT "RX_RING_SIZE[%d]=%d\n", rx_ring_idx, data->re8670_rx_ring_size[rx_ring_idx]);
	}
	printk(KERN_CONT "rx_buff_size=%d\n", data->rx_buff_size);
	printk(KERN_CONT "==============================================================\n");
	printk(KERN_CONT "Usage:\n");
	printk(KERN_CONT "	-iocmd_reg [0x********]\n");
	printk(KERN_CONT "	-iocmd1_reg [0x********]\n");
	printk(KERN_CONT "	-tx_jumbo_frame_enabled [0/1]\n");
#ifdef CONFIG_RTK_CONTROL_PACKET_PROTECTION
	printk(KERN_CONT "	-rx_save_int_prio_to_skb [0/1]\n");
#endif
#ifdef TX_RING_DEBUG
	printk(KERN_CONT "	-tx_ring_backup_debug [0/1]\n");
#endif
#ifdef TX_INTR_HANDLE
	printk(KERN_CONT "	-tx_mit [1/4/8/12/16/20/24/28]\n");
	printk(KERN_CONT "	-tx_mit_timer [0/1/.../15]\n");
#endif
#ifdef TX_CREATE_TEST_PACKET_DEBUG
	printk(KERN_CONT "	-tx_packet_test [1/2/0] --> [start/kick/end]\n");
#endif
#ifdef LOCAL_SERVICE_ACCELERATE
	printk(KERN_CONT "	-local_service_acc_enable [0/1]\n");
#endif
	printk(KERN_CONT "==============================================================\n");
	return 0;
}

#ifdef TX_CREATE_TEST_PACKET_DEBUG
void send_test_packet_out_1(struct re_private *cp)
{
	struct sk_buff *test_skb = NULL;
	struct tx_info txInfo, txInfoMask;
	u32 len = 66;
	struct net_device *dev;	
	int i, j;

	cp->test_packet[0] = 0xe8;cp->test_packet[1] = 0x9d;cp->test_packet[2] = 0x87;cp->test_packet[3] = 0x9b;
	cp->test_packet[4] = 0xe8;cp->test_packet[5] = 0x2e;
	cp->test_packet[6] = 0x00;cp->test_packet[7] = 0xe0;cp->test_packet[8] = 0x4c;cp->test_packet[9] = 0x86;
	cp->test_packet[10] = 0x70;cp->test_packet[11] = 0x01;
	cp->test_packet[12] = 0x08;
	cp->test_packet[13] = 0x00;
	cp->test_packet[14] = 0x45;
	cp->test_packet[15] = 0x00;
	cp->test_packet[16] = 0x05;
	cp->test_packet[17] = 0xdc;
	cp->test_packet[18] = 0x39;
	cp->test_packet[19] = 0x50;

	cp->test_packet[20] = 0x40;
	cp->test_packet[21] = 0x00;
	cp->test_packet[22] = 0x40;
	cp->test_packet[23] = 0x08;

	for(j=24 ; j<300 ; j++)
	{
		cp->test_packet[j] = 0xaa;
	}

	dev = dev_get_by_name(&init_net,"eth0.4");
	if(dev==NULL)
	{
		printk(KERN_CONT "can't find eth0.4 device\n");
		return;
	}
	test_skb = dev_alloc_skb(JUMBO_SKB_BUF_SIZE);
    if(!test_skb) {
		//spin_unlock_irqrestore(&test_lock, flags);
        printk(KERN_CONT "%s:%d allocte skb for jumbo frame fail\n", __FILE__, __LINE__);
		return;			
    }
	test_skb->dev = dev;
    skb_put(test_skb, len);
	memcpy(test_skb->data, &cp->test_packet[0], len);
	txInfo.opts1.dw = 0x20000042;
	txInfo.opts2.dw = 0x80040000;
	txInfo.opts3.dw = 0x08220000;
	txInfo.opts4.dw = 0xddc00000;
	txInfoMask.opts1.dw = 0xffffffff;
	txInfoMask.opts2.dw = 0xffffffff;
	txInfoMask.opts3.dw = 0xffffffff;
	txInfoMask.opts4.dw = 0xffffffff;
	re8686_send_with_txInfo_and_mask(test_skb, &txInfo, 0, &txInfoMask);
}

void send_test_packet_out_2(struct re_private *cp)
{
	struct sk_buff *test_skb = NULL;
	struct tx_info txInfo, txInfoMask;
	u32 len = 3376;
	struct net_device *dev;
	int i, j;

	for(j=0 ; j<5000 ; j++)
	{
		cp->test_packet[j] = 0xbb;
	}
	
	dev = dev_get_by_name(&init_net,"eth0.4");
	if(dev==NULL)
	{
		printk(KERN_CONT "can't find eth0.4 device\n");
		return;
	}
	test_skb = dev_alloc_skb(JUMBO_SKB_BUF_SIZE);
    if(!test_skb) {
        printk(KERN_CONT "%s:%d allocte skb for jumbo frame fail\n", __FILE__, __LINE__);
		return;			
    }
	test_skb->dev = dev;
    skb_put(test_skb, len);
	memcpy(test_skb->data, &cp->test_packet[0], len);
	txInfo.opts1.dw = 0x00000d30;
	txInfo.opts2.dw = 0x80040000;
	txInfo.opts3.dw = 0x08220000;
	txInfo.opts4.dw = 0xddc00000;
	txInfoMask.opts1.dw = 0xffffffff;
	txInfoMask.opts2.dw = 0xffffffff;
	txInfoMask.opts3.dw = 0xffffffff;
	txInfoMask.opts4.dw = 0xffffffff;
	re8686_send_with_txInfo_and_mask(test_skb, &txInfo, 0, &txInfoMask);
}

void send_test_packet_out_3(struct re_private *cp)
{
	struct sk_buff *test_skb = NULL;
	struct tx_info txInfo, txInfoMask;
	u32 len = 11104;
	struct net_device *dev;
	int i, j;

	for(j=0 ; j<12000 ; j++)
	{
		cp->test_packet[j] = 0xcc;
	}
	
	dev = dev_get_by_name(&init_net,"eth0.4");
	if(dev==NULL)
	{
		printk(KERN_CONT "can't find eth0.4 device\n");
		return;
	}
	test_skb = dev_alloc_skb(JUMBO_SKB_BUF_SIZE);
    if(!test_skb) {
        printk(KERN_CONT "%s:%d allocte skb for jumbo frame fail\n", __FILE__, __LINE__);
		return;			
    }
	test_skb->dev = dev;
    skb_put(test_skb, len);
	memcpy(test_skb->data, &cp->test_packet[0], len);
	txInfo.opts1.dw = 0x10002b60;
	txInfo.opts2.dw = 0x80040000;
	txInfo.opts3.dw = 0x08220000;
	txInfo.opts4.dw = 0xddc00000;
	txInfoMask.opts1.dw = 0xffffffff;
	txInfoMask.opts2.dw = 0xffffffff;
	txInfoMask.opts3.dw = 0xffffffff;
	txInfoMask.opts4.dw = 0xffffffff;
	re8686_send_with_txInfo_and_mask(test_skb, &txInfo, 0, &txInfoMask);
}
#endif

static ssize_t misc_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int value;
	unsigned int gmac;
	char	tmpbuf[64];
	char	*strptr, *var;
	char	*tokptr;
	void*	pBuf;
	int i, j;
	size_t len;

#define VAR_NUM 16
	char var_name[VAR_NUM][64] = {"iocmd_reg", "iocmd1_reg", "tx_jumbo_frame_enabled", "rx_save_int_prio_to_skb", "tx_ring_backup_debug", "tx_mit", "tx_mit_timer", "tx_packet_test", "local_service_acc_enable", ""};

	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	unsigned int* pVar[VAR_NUM] = {&(data->iocmd_reg), &(data->iocmd1_reg), &(data->tx_jumbo_frame_enabled), &(data->rx_save_int_prio_to_skb), &(data->tx_ring_backup_debug), &(data->tx_int_mitigation), &(data->tx_int_mitigation_timer), &(data->test_packet_start), &(data->local_service_acc_enable), NULL};
	len = min(count, sizeof(tmpbuf) - 1);
	gmac = data->gmac;
	if (buf && !copy_from_user(tmpbuf, buf, len)) {
		tmpbuf[len] = '\0';

		if (!str_valid(tmpbuf))
			return -EINVAL;

		strptr=tmpbuf;
		var = strsep(&strptr," ");
		if (var==NULL)
		{
			goto errout;
		}
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errout;
		}
		
		for(i=0;strlen(var_name[i])&&pVar[i];i++){
			if(!memcmp(var, var_name[i], strlen(var_name[i]))){
				if(!strcmp(var, "tx_jumbo_frame_enabled"))
				{
					value = simple_strtol(tokptr, NULL, 0);
					*pVar[i] = value;
					config_tx_jumbo(gmac, value);
				}
#ifdef TX_RING_DEBUG
				else if(!strcmp(var, "tx_ring_backup_debug"))
				{
					memset(data->rtl8686_tx_ring_debug, 0, sizeof(struct rtl8686_tx_ring_debug_entry)*MAX_GMAC_NUM*MAX_TXRING_NUM);
					value = simple_strtol(tokptr, NULL, 0);
					*pVar[i] = value;
					if(value)
					{
						for(j=0;j<MAX_TXRING_NUM;j++)
						{
							if(!data->re8670_tx_ring_size[j])
								continue;

							pBuf = kzalloc(RE8670_TXRING_BYTES(data->re8670_tx_ring_size[j]), GFP_KERNEL);
							if (!pBuf)
								printk(KERN_CONT "%s %d FAIL !\n\n", __func__, __LINE__);

							dma_cache_wback_inv((unsigned long)pBuf, RE8670_TXRING_BYTES(data->re8670_tx_ring_size[j]));
							pBuf = (void*)( (u32)(pBuf + DESC_ALIGN-1) &  ~(DESC_ALIGN -1) ) ;
							data->rtl8686_tx_ring_debug[j].txDescriptor = (DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);

							pBuf = kzalloc(data->re8670_tx_ring_size[j]*sizeof(struct tx_data_buffter), GFP_KERNEL);
							if (!pBuf)
								printk(KERN_CONT "%s %d FAIL !\n\n", __func__, __LINE__);

							data->rtl8686_tx_ring_debug[j].dataBuffer = pBuf;
						}
					}
					else
					{
						for(j=0;j<MAX_TXRING_NUM;j++)
						{
							kfree(data->rtl8686_tx_ring_debug[j].txDescriptor);
							kfree(data->rtl8686_tx_ring_debug[j].dataBuffer);
						}
					}
				}
#endif
#ifdef TX_INTR_HANDLE
				else if(!strcmp(var, "tx_mit")) 
				{
					value = simple_strtol(tokptr, NULL, 0);
					*pVar[i] = value;
					tx_int_mitigation_set(gmac, value);
				} else if(!strcmp(var, "tx_mit_timer")) 
				{
					value = simple_strtol(tokptr, NULL, 0);
					*pVar[i] = value;
					tx_int_mitigation_timer_set(gmac, value);
				}
#endif
#ifdef TX_CREATE_TEST_PACKET_DEBUG
				else if(!strcmp(var, "tx_packet_test")) 
				{
					value = simple_strtol(tokptr, NULL, 0);					
					if(value == 1)
					{
						*pVar[i] = value;
						send_test_packet_out_1(data);
						send_test_packet_out_2(data);
						send_test_packet_out_3(data);
					}
					else if(value == 2)
					{
						*pVar[i] = value;
						kick_tx(0, 0);
					}
				}
#endif
				else
				{
					value = simple_strtol(tokptr, NULL, 0);
					*pVar[i] = value;
				}
				break;
			}
		}

		if(!pVar[i]){
			goto errout;
		}

	}
	else
	{
errout:
		printk(KERN_CONT "iocmd_reg 0x%x\n", data->iocmd_reg);
		printk(KERN_CONT "iocmd1_reg 0x%x\n", data->iocmd1_reg);
		printk(KERN_CONT "tx_jumbo_frame_enabled: %s\n", data->tx_jumbo_frame_enabled?"Enabled":"Disabled");
		printk(KERN_CONT "rx_save_int_prio_to_skb: ");
#ifdef CONFIG_RTK_CONTROL_PACKET_PROTECTION
		printk(KERN_CONT "%s\n", data->rx_save_int_prio_to_skb?"Enabled":"Disabled");
#else
		printk(KERN_CONT "Not active\n");
#endif
#ifdef TX_RING_DEBUG
		printk(KERN_CONT "tx_ring_backup_debug: %s\n", data->tx_ring_backup_debug?"Enabled":"Disabled");
#endif
#ifdef TX_CREATE_TEST_PACKET_DEBUG
		printk(KERN_CONT "tx_packet_test: %d\n", data->test_packet_start);
#endif
		printk(KERN_CONT "Local service accelerate: ");
#ifdef LOCAL_SERVICE_ACCELERATE
		printk(KERN_CONT "%s\n", data->local_service_acc_enable?"Enabled":"Disabled");
#else
		printk(KERN_CONT "Not active\n");
#endif
#ifdef TX_RECYCLE_SKB_USING_TOK_INT
		printk(KERN_CONT "Tx recycle using interrupt mode\n");
		printk(KERN_CONT "	-tx_int_mitigation: 0x%x\n", data->tx_int_mitigation);
		printk(KERN_CONT "	-tx_int_mitigation_timer: 0x%x\n", data->tx_int_mitigation_timer);
#endif
#ifdef TX_RECYCLE_SKB_USING_POLLING
		printk(KERN_CONT "Tx recycle using polling mode\n");
		printk(KERN_CONT "	-polling per 10mseconds: %d\n", TX_RECYCLE_SKB_POLLING_10MSECONDS);
#endif
		printk(KERN_CONT "==============================================================\n");
		printk(KERN_CONT "Usage:\n");
		printk(KERN_CONT "	-iocmd_reg [0x********]\n");
		printk(KERN_CONT "	-iocmd1_reg [0x********]\n");
		printk(KERN_CONT "	-tx_jumbo_frame_enabled [0/1]\n");
#ifdef CONFIG_RTK_CONTROL_PACKET_PROTECTION
		printk(KERN_CONT "	-rx_save_int_prio_to_skb [0/1]\n");
#endif
#ifdef TX_RING_DEBUG
		printk(KERN_CONT "	-tx_ring_backup_debug [0/1]\n");
#endif
#ifdef TX_INTR_HANDLE
		printk(KERN_CONT "	-tx_mit [1/4/8/12/16/20/24/28]\n");
		printk(KERN_CONT "	-tx_mit_timer [0/1/.../15]\n");
#endif
#ifdef TX_CREATE_TEST_PACKET_DEBUG
		printk(KERN_CONT "	-tx_packet_test [1/2/0] --> [start/kick/end]\n");
#endif
#ifdef LOCAL_SERVICE_ACCELERATE
		printk(KERN_CONT "	-local_service_acc_enable [0/1]\n");
#endif
		printk("==============================================================\n");

	}
	return count;
}

static int dev_port_mapping_read(struct seq_file *m, void *v)
{
	static struct re_private *data;
	unsigned int i;
	unsigned int totalDev = TOTAL_RTL8686_DEV_NUM;
	struct file *file = m->private;
	
	data=pde_data(file_inode(file));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}
	
	seq_printf(m, "WAN PORT %d, CPU PORT %d\n", WAN_PORT, data->gmac_cpu_port);
	seq_printf(m, "DEV ability: ");
	for(i=0;i<totalDev;i++){
		if(rtl8686_dev_table[i].dev_instant)
			seq_printf(m, "%s ", rtl8686_dev_table[i].dev_instant->name);
	}
	seq_printf(m, "\n");

	seq_printf(m, "rx: phyPort -> dev[the packet rx from phyPort will send to kernel using dev]\n");
	for(i=0;i<SW_PORT_NUM;i++){
		if(data->port2dev[i])
			seq_printf(m, "port%d -> %s\n", i, data->port2dev[i]->name);
	}

	seq_printf(m, "tx:dev -> txPortMask[when tx from dev, we will use this txPortMask]\n");
	for(i=0;i<totalDev;i++){
		if(rtl8686_dev_table[i].dev_instant)
			seq_printf(m, "%s -> 0x%x\n",
				rtl8686_dev_table[i].dev_instant->name, DEVPRIV(rtl8686_dev_table[i].dev_instant)->txPortMask);
	}

#ifdef CONFIG_RTL8686_SWITCH
	seq_printf(m, "netdev carrier mapping\n");
	for(i=0;i<SW_PORT_NUM;i++)
	{
		seq_printf(m, "Port%d => ifname:%s , dev:%s(0x%p) , status:%d \n",
			i , LCDev_mapping[i].ifname , LCDev_mapping[i].phy_dev->name, LCDev_mapping[i].phy_dev, LCDev_mapping[i].status);
	}
#endif
	return 0;
}

static ssize_t dev_port_mapping_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	char 		tmpbuf[64];
	char		*strptr;
	unsigned int port_num;//, i;
	char		*tokptr;
	unsigned long buf_size;
	
	buf_size = min(count, (sizeof(tmpbuf)-1));
	if (buf && !copy_from_user(tmpbuf, buf, buf_size)) {
	
		tmpbuf[count] = '\0';

		if (!str_valid(tmpbuf))
			return -EINVAL;

		strptr=tmpbuf;
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errout;
		}
		port_num = simple_strtol(tokptr, NULL, 0);
		printk(KERN_CONT "port %d ", port_num);
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errout;
		}
		if(tokptr[strlen(tokptr) - 1] == 0x0a)
			tokptr[strlen(tokptr) - 1] = 0x00;
		printk(KERN_CONT "assign to %s\n", tokptr);
		change_dev_port_mapping(port_num, tokptr);
		sync_dev_port_mapping();
	}
	else
	{
errout:
		printk(KERN_CONT "num_of_port dev_name\n");
		printk(KERN_CONT "tx_force_gmac GMAC0/GMAC1/GMAC2 \n");
		return -EFAULT;
	}
		
	return count;
}

static int change_dev_tx_port_mask(int port_num, char* name, int index)
{
	if(strcmp(name, "eth0")){
		DEVPRIV(rtl8686_dev_table[index].dev_instant)->txPortMask = 1 << port_num;
		//printk("%s -> 0x%x\n", 
			//rtl8686_dev_table[index].dev_instant->name, DEVPRIV(rtl8686_dev_table[index].dev_instant)->txPortMask);
	}
	return 0;
}

/*ccwei: add dev port mapping api*/
static int change_dev_port_mapping(int port_num, char* name)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp;
	unsigned int totalDev = TOTAL_RTL8686_DEV_NUM;
	struct net_device *dev = NULL;
	unsigned int gmac;
	char* dev_name;	
	int i;
	
	for(i=0;i<totalDev;i++){

		if(i == 0)
		{
			continue;
		}
		
		if(rtl8686_dev_table[i].dev_instant)
		{
			dev_name = rtl8686_dev_table[i].dev_instant->name;
			dev = rtl8686_dev_table[i].dev_instant;
		}
		else
		{
			printk(KERN_CONT "no dev_instant, strange.......\n");
			dev_name = rtl8686_dev_table[i].ifname;
		}		
		if(!strcmp(dev_name, name)){
			for(gmac=0;gmac<MAX_GMAC_NUM;gmac++) {

				cp = root_cp->re_private_data_ptr[gmac];
				
				if(cp->gmac_enabled != GMAC_TRUE)
					continue;	
				
				cp->port2dev[port_num] = rtl8686_dev_table[i].dev_instant;
			}
			change_dev_tx_port_mask(port_num, name, i);
			break;
		}
	}
	
	if(i == totalDev){
		printk(KERN_CONT "can't find dev %s\n", name);
		return -1;
	}
	
#ifdef CONFIG_RTL8686_SWITCH 
	if(port_num < SW_PORT_NUM && port_num >= 0 && dev )
	{
		LCDev_mapping[port_num].phy_dev = dev;
		strcpy(LCDev_mapping[port_num].ifname,dev->name);
	}
#endif
	
	return 0;
}

static void sync_dev_port_mapping(void)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp;
	unsigned int totalDev = TOTAL_RTL8686_DEV_NUM;
	unsigned int gmac;
	struct re_dev_private *virtualCp;
	int port, index, found;

	for (index=0 ; index<totalDev ; index++) {

		if (rtl8686_dev_table[index].ifflag != RTL8686_ELAN)
			continue;

		if (!rtl8686_dev_table[index].dev_instant)
			continue;

		virtualCp = DEVPRIV(rtl8686_dev_table[index].dev_instant);

		for(gmac=0U;gmac<MAX_GMAC_NUM;gmac++) {
			cp = root_cp->re_private_data_ptr[gmac];

			if(cp->gmac_enabled != (u32)GMAC_TRUE)
				continue;

			found=0;
			for(port=0;port<SW_PORT_NUM;port++){
				if (!cp->port2dev[port])
					continue;
				if (cp->port2dev[port] == rtl8686_dev_table[index].dev_instant) {
					if (virtualCp && virtualCp->txPortMask == (u32)1 << port) {
						found=1;
						if (LCDev_mapping[port].status!=0xff) {
							if (LCDev_mapping[port].status==1 && !netif_carrier_ok(cp->port2dev[port])) {
								netif_carrier_on(cp->port2dev[port]);
							}
							else if (LCDev_mapping[port].status==0 && netif_carrier_ok(cp->port2dev[port])) {
								netif_carrier_off(cp->port2dev[port]);
							}
						}
					}
				}
			}
			if (!found)
			{
				netif_carrier_off(rtl8686_dev_table[index].dev_instant);
			}
		}
	}
}

#if defined(CONFIG_APOLLO_ROMEDRIVER)
extern int fwdEngine_rx_skb(struct re_private *cp, struct sk_buff *skb,struct rx_info *pRxInfo);
#endif
static int port_to_rxfunc_read(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int totalPortTable = SW_PORT_NUM;
	unsigned int i;
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}
	
	//default set to eth0
	for(i=0;i<totalPortTable;i++){
		printk(KERN_CONT "port%d -> 0x%p\n", i, data->port2rxfunc[i]);
	}
	printk(KERN_CONT "================[port2rxfunc func pointer table]================\n");
#if defined(CONFIG_APOLLO_ROMEDRIVER)
	printk(KERN_CONT "fwdEngine_rx_skb: 0x%p\n", fwdEngine_rx_skb);
#endif
	printk("re8670_rx_skb: 0x%p\n", re8670_rx_skb);
	printk("================================================================\n");
	return 0;
}

static ssize_t port_to_rxfunc_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	char 	tmpbuf[512];
	char		*strptr;	
	//int retval = -1, i;
	static struct re_private *data;
	
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		tmpbuf[count] = '\0';
		strptr=tmpbuf;
		if( strlen(strptr)==0 )
		{
			goto errout;
		}
#if defined(CONFIG_APOLLO_ROMEDRIVER)
		if(strncmp(strptr, "force2rg", 8) == 0)
		{
			re8686_register_rxfunc_all_port(&fwdEngine_rx_skb);
			printk(KERN_CONT "force NIC Rx hook to RG only!\n");
		}
		else 
#endif
		if(strncmp(strptr, "force2nf", 8) == 0)
		{
			re8686_register_rxfunc_all_port(&re8670_rx_skb);
			printk(KERN_CONT "force NIC Rx hook to PS only!\n");
		}
		else
		{
			goto errout;
		}
	}
	else
	{
errout:
#if defined(CONFIG_APOLLO_ROMEDRIVER)
		printk(KERN_CONT "error input (force2rg/force2nf)\n");
#else
		printk(KERN_CONT "error input (force2nf)\n");
#endif
	}
	return count;
}

int dbg_level_read(struct file *filp, char *buf, size_t count, loff_t *offp ) 
{
	static struct re_private *data;
	unsigned int gmac;
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}
	
	gmac = data->gmac;
	printk(KERN_CONT "[debug_enable = 0x%08x]\n", data->debug_enable);
	printk(KERN_CONT "RTL8686_PRINT_NOTHING\t0x%08x\n", RTL8686_PRINT_NOTHING);
	printk(KERN_CONT "RTL8686_SKB_RX\t\t0x%08x\n", RTL8686_SKB_RX);
	printk(KERN_CONT "RTL8686_SKB_TX\t\t0x%08x\n", RTL8686_SKB_TX);
	printk(KERN_CONT "RTL8686_RXINFO\t\t0x%08x\n", RTL8686_RXINFO);
	printk(KERN_CONT "RTL8686_TXINFO\t\t0x%08x\n", RTL8686_TXINFO);
	printk(KERN_CONT "RTL8686_RX_TRACE\t\t0x%08x\n", RTL8686_RX_TRACE);
	printk(KERN_CONT "RTL8686_TX_TRACE\t\t0x%08x\n", RTL8686_TX_TRACE);
	printk(KERN_CONT "RTL8686_RX_WARN\t\t0x%08x\n", RTL8686_RX_WARN);
	printk(KERN_CONT "RTL8686_TX_WARN\t\t0x%08x\n", RTL8686_TX_WARN);
	printk(KERN_CONT "RTL8686_OTHERS\t\t0x%08x\n", RTL8686_OTHERS);
	
	return 0;
}

int dbg_times_read(struct file *filp, char *buf, size_t count, loff_t *offp ) 
{
	static struct re_private *data;
	unsigned int gmac;
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}
	
	gmac = data->gmac;
	printk(KERN_CONT "[debug_times = %d times]\n", data->debug_times);
	
	return 0;
}

static int hwreg_read(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int gmac;
	int len = 0, i;
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;
	printk(KERN_CONT "ETHBASE		=0x%08x\n", data->base);
	printk(KERN_CONT "IDR		=%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x\n", 
		RLE0787_R8(gmac, IDR0), RLE0787_R8(gmac, IDR1), RLE0787_R8(gmac, IDR2), RLE0787_R8(gmac, IDR3), RLE0787_R8(gmac, IDR4), RLE0787_R8(gmac, IDR5));
	printk(KERN_CONT "MAR		=%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x\n", 
		RLE0787_R8(gmac, MAR0), RLE0787_R8(gmac, MAR1), RLE0787_R8(gmac, MAR2), RLE0787_R8(gmac, MAR3), 
		RLE0787_R8(gmac, MAR4), RLE0787_R8(gmac, MAR5), RLE0787_R8(gmac, MAR6), RLE0787_R8(gmac, MAR7));
	printk(KERN_CONT "TXOKCNT		=0x%04x		RXOKCNT		=0x%04x\n", RLE0787_R16(gmac, TXOKCNT), RLE0787_R16(gmac, RXOKCNT));
	printk(KERN_CONT "TXERR		=0x%04x		RXERRR		=0x%04x\n", RLE0787_R16(gmac, TXERR), RLE0787_R16(gmac, RXERRR));
	printk(KERN_CONT "MISSPKT		=0x%04x		FAE		=0x%04x\n", RLE0787_R16(gmac, MISSPKT), RLE0787_R16(gmac, FAE));
	printk(KERN_CONT "TX1COL		=0x%04x		TXMCOL		=0x%04x\n", RLE0787_R16(gmac, TX1COL), RLE0787_R16(gmac, TXMCOL));
	printk(KERN_CONT "RXOKPHY		=0x%04x		RXOKBRD		=0x%04x\n", RLE0787_R16(gmac, RXOKPHY), RLE0787_R16(gmac, RXOKBRD));
	printk(KERN_CONT "RXOKMUL		=0x%04x		TXABT		=0x%04x\n", RLE0787_R16(gmac, RXOKMUL), RLE0787_R16(gmac, TXABT));
	printk(KERN_CONT "TXUNDRN		=0x%04x		RDUMISSPKT	=0x%04x\n", RLE0787_R16(gmac, TXUNDRN), RLE0787_R16(gmac, RDUMISSPKT));
	printk(KERN_CONT "TRSR		=0x%08x\n", RLE0787_R32(gmac, TRSR));
	printk(KERN_CONT "CMD		=0x%02x		IMR		=0x%04x\n", RLE0787_R8(gmac, CMD), RLE0787_R16(gmac, IMR));
	printk(KERN_CONT "ISR		=0x%04x		TCR		=0x%08x\n", RLE0787_R16(gmac, ISR), RLE0787_R32(gmac, TCR));
	printk(KERN_CONT "ISR1		=0x%08x	IMR0		=0x%08x\n", RLE0787_R32(gmac, ISR1), RLE0787_R32(gmac, IMR0));
	printk(KERN_CONT "RCR		=0x%08x	CPUtagCR	=0x%08x\n", RLE0787_R32(gmac, RCR), RLE0787_R32(gmac, CPUtagCR));
	printk(KERN_CONT "CONFIG_REG	=0x%08x	CPUtag1CR	=0x%08x\n", RLE0787_R32(gmac, CONFIG_REG), RLE0787_R32(gmac, CPUtag1CR));
	printk(KERN_CONT "MSR		=0x%08x	VLAN_REG	=0x%08x\n", RLE0787_R32(gmac, MSR), RLE0787_R32(gmac, VLAN_REG));
	printk(KERN_CONT "VLAN1_REG	=0x%08x		LEDCR		=0x%08x\n", RLE0787_R32(gmac, VLAN1_REG), RLE0787_R32(gmac, LEDCR));

	for(i=0;i<MAX_TXRING_NUM;i++)
	{
		printk(KERN_CONT "TxFDP%d		=0x%08x	TxCDO%d		=0x%04x\n",i,RLE0787_R32(gmac, TxFDP1+(ADDR_OFFSET*i)),i,RLE0787_R16(gmac, TxCDO1+(ADDR_OFFSET*i)));
	}
	for(i=0;i<MAX_RXRING_NUM;i++){
#ifndef CONFIG_RTK_SINGLE_RX_RING
		if(i==0)
#endif
		{
			printk(KERN_CONT "RxRingSize%d	=0x%04x\n", i, RLE0787_R16(gmac, RxRingSize));
			printk(KERN_CONT "RxFDP%d		=0x%08x	RxCDO%d		=0x%04x\n",i,RLE0787_R32(gmac, RxFDP), i, RLE0787_R16(gmac, RxCDO));
			printk(KERN_CONT "EthrntRxCPU_Des_Num	=0x%02x	EthrntRxCPU_Des_Wrap	=0x%02x\n", 
				RLE0787_R8(gmac, EthrntRxCPU_Des_Num), RLE0787_R8(gmac, EthrntRxCPU_Des_Wrap));
			printk(KERN_CONT "Rx_Pse_Des_Thres	=0x%02x	EthrntRxCPU_Des_Num_h	=0x%02x\n", 
				RLE0787_R8(gmac, Rx_Pse_Des_Thres), RLE0787_R8(gmac, EthrntRxCPU_Des_Num_h));
			printk(KERN_CONT "Rx_Pse_Des_Thres_h	=0x%02x\n", RLE0787_R8(gmac, Rx_Pse_Des_Thres_h));
		}
#ifndef CONFIG_RTK_SINGLE_RX_RING
		else
		{
			printk(KERN_CONT "RxRingSize%d	=0x%04x\n", i, RLE0787_R16(gmac, RxRingSize2+(ADDR_OFFSET*(i-1))));
			printk(KERN_CONT "RxFDP%d		=0x%08x	RxCDO%d		=0x%04x\n",i,RLE0787_R32(gmac, RxFDP2+(ADDR_OFFSET*(i-1))),i,RLE0787_R16(gmac, RxCDO2+(ADDR_OFFSET*(i-1))));
			printk(KERN_CONT "RxCPU_Des_Num%d	=0x%08x	RxCPU_Des_Thres%d=0x%08x\n", 
				i, RLE0787_R32(gmac, EthrntRxCPU_Des_Num2+(ADDR_OFFSET*(i-1))), 
				i, RLE0787_R32(gmac, EthrntRxCPU_Des_Wrap2+(ADDR_OFFSET*(i-1))));
		}
#endif
	}
	printk(KERN_CONT "RRING_ROUTING1	=0x%08x	RRING_ROUTING2	=0x%08x\n", RLE0787_R32(gmac, RRING_ROUTING1), RLE0787_R32(gmac, RRING_ROUTING2));
	printk(KERN_CONT "RRING_ROUTING3	=0x%08x	RRING_ROUTING4	=0x%08x\n", RLE0787_R32(gmac, RRING_ROUTING3), RLE0787_R32(gmac, RRING_ROUTING4));
	printk(KERN_CONT "RRING_ROUTING5	=0x%08x	RRING_ROUTING6	=0x%08x\n", RLE0787_R32(gmac, RRING_ROUTING5), RLE0787_R32(gmac, RRING_ROUTING6));
	printk(KERN_CONT "RRING_ROUTING7	=0x%08x\n", RLE0787_R32(gmac, RRING_ROUTING7));
	printk(KERN_CONT "IO_CMD		=0x%08x	IO_CMD1		=0x%08x\n", RLE0787_R32(gmac, IO_CMD), RLE0787_R32(gmac, IO_CMD1));
	printk(KERN_CONT "DIAG1_REG       =0x%08x\n",RLE0787_R32(gmac, DIAG1_REG));

	return len;
}

static int hwreg_write(struct file *file, const char __user *buff, size_t len, loff_t *ppos)
{
	char 	tmpbuf[512];
	char		*strptr;
	static struct re_private *data;
	unsigned int gmac;
	data=pde_data(file_inode(file));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;
	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		tmpbuf[len] = '\0';
		strptr=tmpbuf;
		if( strlen(strptr)==0 )
		{
			goto errout;
		}
		if(strncmp(strptr, "reset", 5) == 0)
		{
			if(re8670_reset())
				goto errout;
		}
		else if(strncmp(strptr, "swint", 5) == 0)
		{
			RLE0787_W32(gmac, SWINT_REG, (1<<24));
			printk(KERN_CONT "SWINT triggered !\n");
		}
		else
		{
			goto errout;
		}
	}
	else
	{
errout:
		printk(KERN_CONT "error input !\n");
	}
	return len;
}

static int sw_cnt_seq_read(struct seq_file *m, void *v)
{
	static struct re_private *data;
	struct file *file = m->private;
	int len = 0, i;
	u32 tx_hw_num=0;
	
	data=pde_data(file_inode(file));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rx_hw_num", data->cp_stats.rx_hw_num, "rx_sw_num", data->cp_stats.rx_sw_num);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rer_runt", data->cp_stats.rer_runt, "rer_ovf", data->cp_stats.rer_ovf);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rdu", data->cp_stats.rdu, "frag", data->cp_stats.frag);
#ifdef CONFIG_RG_JUMBO_FRAME
	seq_printf(m, "%-24s:%14d\n", "toobig", data->cp_stats.toobig);
#endif
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"crcerr", data->cp_stats.crcerr, "rcdf", data->cp_stats.rcdf);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rx_no_mem", data->cp_stats.rx_no_mem, "tx_sw_num", data->cp_stats.tx_sw_num);
	for(i=0 ; i<MAX_TXRING_NUM ; i++)
	{
		tx_hw_num += data->cp_stats.tx_hw_num_ring[i];
	}
	seq_printf(m, "%-24s:%14d\n", "tx_hw_num", tx_hw_num);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rx_critical_num", data->cp_stats.rx_critical_num, "rx_critical_drop_num", data->cp_stats.rx_critical_drop_num);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"tx_timeouts", data->cp_stats.tx_timeouts, "tooshort", data->cp_stats.tooshort);
#if defined(CONFIG_RTL865X_ETH_PRIV_SKB)
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"nic_max_alloc_threshold", RE8670_MAX_ALLOC_RXSKB_NUM, "prealloc_pool_size", MAX_ETH_SKB_NUM);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"dynamic_alloc_threshold", RE8670_DYNAMIC_ALLOC_RXSKB_NUM, "dynamic_alloc_skb_num", dynamic_alloc_skb_num);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"eth_skb_free_num", eth_skb_free_num, "eth_skb_alloc_num", eth_skb_alloc_num);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"top_eth_skb_alloc_num", (MAX_ETH_SKB_NUM-lowest_eth_skb_free_num), "cri_eth_skb_free_num", critical_eth_skb_free_num);
#else
	seq_printf(m, "%-24s:%14d\n", 
		"nic_prealloc_threshold", RE8670_MAX_ALLOC_RXSKB_NUM);
#endif
#ifdef RX_NAPI_MODE
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rx_napi_poll", data->cp_stats.rx_napi_poll, "rx_napi_interrupt", data->cp_stats.rx_napi_interrupt);
#ifdef RX_NAPI_MODE_DEBUG
	unsigned long long total_packet_cnt = 0;
	for(i=0 ; i<MAX_NAPI_STATISTIC_NUM ; i++)
	{
		if(napi_statistic[i])
		{
			seq_printf(m, "	napi_statistic[%d]=%d\n", i, napi_statistic[i]);
			total_packet_cnt = total_packet_cnt + napi_statistic[i];
		}
	}	
	seq_printf(m, "	total_poll_and_interrupt=%llu\n", total_packet_cnt);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rx_napi_gro_normal", data->cp_stats.rx_napi_gro_normal, "rx_napi_gro_drop", data->cp_stats.rx_napi_gro_drop);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rx_napi_gro_merged_free", data->cp_stats.rx_napi_gro_merged_free, "rx_napi_gro_held", data->cp_stats.rx_napi_gro_held);
	seq_printf(m, "%-24s:%14d\n", "rx_napi_gro_merged", data->cp_stats.rx_napi_gro_merged);
#endif
#endif
#ifdef HWNAT_CUSTOMIZE
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rx_cus_fNum", data->cp_stats.rx_customized_forward, "rx_cus_cNum", data->cp_stats.rx_customized_copied);
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n",
		"rx_cus_oNum", data->cp_stats.rx_customized_rx_owned, "tx_cus_oNum", data->cp_stats.rx_customized_tx_owned);
	seq_printf(m, "%-24s:%14d\n", "rx_cus_tx_own_waiting_times", data->cp_stats.rx_customized_tx_own_waiting_times);
#endif
	seq_printf(m, "%-24s:%14d\n", "tx_no_desc", data->cp_stats.tx_no_desc);
	for(i=0 ; i<MAX_TXRING_NUM ; i++)
	{
		seq_printf(m, "tx_hw_num_ring[%d]:%d\n", i, data->cp_stats.tx_hw_num_ring[i]);
	}
#ifdef TX_INTR_HANDLE
	seq_printf(m, "%-24s:%14d          %-24s:%14d\n", "tok", data->cp_stats.tok, "tdu", data->cp_stats.tdu);
#ifdef TX_KICK_RING_USING_TDU_INT
	for(i=0 ; i<MAX_TXRING_NUM ; i++)
	{
		seq_printf(m, "tdu_kick_ring[%d]:%d (%d)\n", i, data->cp_stats.tdu_kick_ring_intr[i], data->cp_stats.tdu_kick_ring_retry[i]);
	}
#endif
#endif
#ifdef TX_KICK_RING_USING_POLLING
	for(i=0 ; i<MAX_TXRING_NUM ; i++)
	{
		seq_printf(m, "poll_kick_ring[%d]:%d/%d\n", i, data->cp_stats.tdu_kick_ring_poll[i], data->cp_stats.tdu_poll[i]);
	}
#endif
#if defined(TX_RECYCLE_SKB_USING_TOK_INT) || defined(TX_RECYCLE_SKB_USING_POLLING)
	for(i=0 ; i<MAX_TXRING_NUM ; i++)
	{
		seq_printf(m, "tok_free_skb[%d]:%d\n", i, data->cp_stats.tok_free_skb[i]);
	}
#endif
	for(i=0 ; i<MAX_RXRING_NUM ; i++)
	{
		if (i==0)
			seq_printf(m, "rx_Mtail[%d]=%d RxCDO=%d, CpuOwn=%d\n", i, data->rx_Mtail[i], RLE0787_R16(data->gmac, RxCDO), re8670_cpu_own_get(data, i));
		else
			seq_printf(m, "rx_Mtail[%d]=%d RxCDO%d=%d, CpuOwn=%d\n", i, data->rx_Mtail[i], i, RLE0787_R16(data->gmac, RxCDO2+(ADDR_OFFSET*(i-1))), re8670_cpu_own_get(data, i));
	}

	return len;
}

static ssize_t sw_cnt_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	char 	tmpbuf[512];
	char		*strptr;	
	int i;
	//int retval = -1, i;
	static struct re_private *data;
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		tmpbuf[count] = '\0';
		strptr=tmpbuf;
		if( strlen(strptr)==0 )
		{
			goto errout;
		}
		if(strncmp(strptr, "all", 8) == 0 || strncmp(strptr, "1", 1) == 0)
		{
			memset(&data->cp_stats, 0, sizeof(struct cp_extra_stats));
#ifdef RX_NAPI_MODE
#ifdef RX_NAPI_MODE_DEBUG
			for(i=0 ; i<MAX_NAPI_STATISTIC_NUM ; i++)
			{
				if(napi_statistic[i])
				{
					napi_statistic[i] = 0;
				}
			}
#endif
#endif
#if defined(CONFIG_RTL865X_ETH_PRIV_SKB)
			lowest_eth_skb_free_num = MAX_ETH_SKB_NUM;
			dynamic_alloc_skb_num = 0;
#endif	
			printk(KERN_CONT "all software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_hw_num", 9) == 0 || strncmp(strptr, "2", 1) == 0)
		{
			data->cp_stats.rx_hw_num=0;
			printk(KERN_CONT "rx_hw_num software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_sw_num", 9) == 0 || strncmp(strptr, "3", 1) == 0)
		{
			data->cp_stats.rx_sw_num=0;
			printk(KERN_CONT "rx_sw_num software counter cleared !\n");
		}
		else if(strncmp(strptr, "rer_runt", 8) == 0 || strncmp(strptr, "4", 1) == 0)
		{
			data->cp_stats.rer_runt=0;
			printk(KERN_CONT "rer_runt software counter cleared !\n");
		}
		else if(strncmp(strptr, "rer_ovf", 7) == 0 || strncmp(strptr, "5", 1) == 0)
		{
			data->cp_stats.rer_ovf=0;
			printk(KERN_CONT "rer_ovf software counter cleared !\n");
		}
		else if(strncmp(strptr, "rdu", 3) == 0 || strncmp(strptr, "6", 1) == 0)
		{
			data->cp_stats.rdu=0;
			printk(KERN_CONT "rdu software counter cleared !\n");
		}
		else if(strncmp(strptr, "frag", 4) == 0 || strncmp(strptr, "7", 1) == 0)
		{
			data->cp_stats.frag=0;
			printk(KERN_CONT "frag software counter cleared !\n");
		}
		else if(strncmp(strptr, "toobig", 6) == 0 || strncmp(strptr, "8", 1) == 0)
		{
			data->cp_stats.toobig=0;
			printk(KERN_CONT "toobig software counter cleared !\n");
		}
		else if(strncmp(strptr, "crcerr", 6) == 0 || strncmp(strptr, "9", 1) == 0)
		{
			data->cp_stats.crcerr=0;
			printk(KERN_CONT "crcerr software counter cleared !\n");
		}
		else if(strncmp(strptr, "rcdf", 4) == 0 || strncmp(strptr, "10", 2) == 0)
		{
			data->cp_stats.rcdf=0;
			printk(KERN_CONT "rcdf software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_no_mem", 9) == 0 || strncmp(strptr, "11", 2) == 0)
		{
			data->cp_stats.rx_no_mem=0;
			printk(KERN_CONT "rx_no_mem software counter cleared !\n");
		}
		else if(strncmp(strptr, "tx_sw_num", 9) == 0 || strncmp(strptr, "12", 2) == 0)
		{
			data->cp_stats.tx_sw_num=0;
			printk(KERN_CONT "tx_sw_num software counter cleared !\n");
		}
		else if(strncmp(strptr, "tx_hw_num", 9) == 0 || strncmp(strptr, "13", 2) == 0)
		{
			data->cp_stats.tx_hw_num=0;
			printk(KERN_CONT "tx_hw_num software counter cleared !\n");
		}
		else if(strncmp(strptr, "tx_no_desc", 10) == 0 || strncmp(strptr, "14", 2) == 0)
		{
			data->cp_stats.tx_no_desc=0;
			printk(KERN_CONT "tx_no_desc software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_critical_num", 15) == 0 || strncmp(strptr, "15", 2) == 0)
		{
			data->cp_stats.rx_critical_num=0;
			printk(KERN_CONT "rx_critical_num software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_cri_no_desc", 14) == 0 || strncmp(strptr, "16", 2) == 0)
		{
			data->cp_stats.rx_critical_drop_num=0;
			printk(KERN_CONT "rx_cri_no_desc software counter cleared !\n");
		}
#ifdef RX_NAPI_MODE
		else if(strncmp(strptr, "rx_napi_poll", 12) == 0 || strncmp(strptr, "17", 2) == 0)
		{
			data->cp_stats.rx_napi_poll=0;
			printk(KERN_CONT "rx_napi_poll software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_napi_interrupt", 17) == 0 || strncmp(strptr, "18", 2) == 0)
		{
			data->cp_stats.rx_napi_interrupt=0;
			printk(KERN_CONT "rx_napi_interrupt software counter cleared !\n");
		}
#endif
#if defined(CONFIG_RTL865X_ETH_PRIV_SKB)
		else if(strncmp(strptr, "top_eth_skb_alloc_num", 21) == 0 || strncmp(strptr, "19", 2) == 0)
		{
			lowest_eth_skb_free_num = MAX_ETH_SKB_NUM;
			printk(KERN_CONT "top_eth_skb_alloc_num software counter cleared !\n");
		}
		else if(strncmp(strptr, "dynamic_alloc_skb_num", 21) == 0 || strncmp(strptr, "20", 2) == 0)
		{
			dynamic_alloc_skb_num = 0;
			printk(KERN_CONT "dynamic_alloc_skb_num software counter cleared !\n");
		}
#endif
	else if(strncmp(strptr, "tooshort", 10) == 0 || strncmp(strptr, "21", 2) == 0)
	{
		data->cp_stats.tooshort=0;
			printk(KERN_CONT "tooshort software counter cleared !\n");
	}
#ifdef RX_NAPI_MODE_DEBUG
		else if(strncmp(strptr, "napi_stat_reset", 15) == 0 || strncmp(strptr, "22", 2) == 0)
		{
			for(i=0 ; i<MAX_NAPI_STATISTIC_NUM ; i++)
			{
				if(napi_statistic[i])
				{
					napi_statistic[i] = 0;
				}
			}
			printk(KERN_CONT "napi_statistic software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_napi_gro_normal", 18) == 0 || strncmp(strptr, "23", 2) == 0)
		{
			data->cp_stats.rx_napi_gro_normal=0;
			printk(KERN_CONT "rx_napi_gro_normal software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_napi_gro_drop", 16) == 0 || strncmp(strptr, "24", 2) == 0)
		{
			data->cp_stats.rx_napi_gro_drop=0;
			printk(KERN_CONT "rx_napi_gro_drop software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_napi_gro_merged_free", 23) == 0 || strncmp(strptr, "25", 2) == 0)
		{
			data->cp_stats.rx_napi_gro_merged_free=0;
			printk(KERN_CONT "rx_napi_gro_merged_free software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_napi_gro_held", 16) == 0 || strncmp(strptr, "26", 2) == 0)
		{
			data->cp_stats.rx_napi_gro_held=0;
			printk(KERN_CONT "rx_napi_gro_held software counter cleared !\n");
		}
		else if(strncmp(strptr, "rx_napi_gro_merged", 18) == 0 || strncmp(strptr, "27", 2) == 0)
		{
			data->cp_stats.rx_napi_gro_merged=0;
			printk(KERN_CONT "rx_napi_gro_merged software counter cleared !\n");
		}
#endif
		else
		{
			goto errout;
		}
	}
	else
	{
errout:
		printk(KERN_CONT "error input: \n");
		printk(KERN_CONT "1/ all - clear all\n");
		printk(KERN_CONT "2/ rx_hw_num - clear rx_hw_num\n");
		printk(KERN_CONT "3/ rx_sw_num - clear rx_sw_num\n");
		printk(KERN_CONT "4/ rer_runt - clear rer_runt\n");
		printk(KERN_CONT "5/ rer_ovf - clear rer_ovf\n");
		printk(KERN_CONT "6/ rdu - clear rdu\n");
		printk(KERN_CONT "7/ frag - clear frag\n");
		printk(KERN_CONT "8/ toobig - clear toobig\n");
		printk(KERN_CONT "9/ crcerr - clear crcerr\n");
		printk(KERN_CONT "10/ rcdf - clear rcdf\n");
		printk(KERN_CONT "11/ rx_no_mem - clear rx_no_mem\n");
		printk(KERN_CONT "12/ tx_sw_num - clear tx_sw_num\n");
		printk(KERN_CONT "13/ tx_hw_num - clear tx_hw_num\n");
		printk(KERN_CONT "14/ tx_no_desc - clear tx_no_desc\n");
		printk(KERN_CONT "15/ rx_critical_num - clear rx_critical_num\n");
		printk(KERN_CONT "16/ rx_cri_no_desc - clear rx_cri_no_desc\n");
#ifdef RX_NAPI_MODE
		printk(KERN_CONT "17/ rx_napi_poll - clear rx_napi_poll\n");
		printk(KERN_CONT "18/ rx_napi_interrupt - clear rx_napi_interrupt\n");
#endif
#if defined(CONFIG_RTL865X_ETH_PRIV_SKB)
		printk(KERN_CONT "19/ top_eth_skb_alloc_num - clear top_eth_skb_alloc_num\n");
		printk(KERN_CONT "20/ dynamic_alloc_skb_num - clear dynamic_alloc_skb_num\n");
#endif
		printk(KERN_CONT "21/	tooshort - clear tooshort\n");
#ifdef RX_NAPI_MODE_DEBUG
		printk(KERN_CONT "22/ napi_stat_reset - clear napi_stat_reset\n");
		printk(KERN_CONT "23/ rx_napi_gro_normal - clear rx_napi_gro_normal\n");
		printk(KERN_CONT "24/ rx_napi_gro_drop - clear rx_napi_gro_drop\n");
		printk(KERN_CONT "25/ rx_napi_gro_merged_free - clear rx_napi_gro_merged_free\n");
		printk(KERN_CONT "26/ rx_napi_gro_held - clear rx_napi_gro_held\n");
		printk(KERN_CONT "27/ rx_napi_gro_merged - clear rx_napi_gro_merged\n");
#endif
	}
	return count;
}

static ssize_t rx_ring_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	unsigned int	ring_num, fc_enabled;
	static struct re_private *data;
	unsigned int gmac;
	char 	tmpbuf[64];
	char	*strptr;
	char	*tokptr;
	int 	i=0;
	size_t	len;

	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	len = min(count, sizeof(tmpbuf) - 1);

	gmac = data->gmac;
	if (buf && !copy_from_user(tmpbuf, buf, len)) {
		tmpbuf[len] = '\0';
		if (len)
			tmpbuf[len - 1] = '\0'; /* strip extra CR */

		if (!str_valid(tmpbuf))
			return -EINVAL;

		strptr=tmpbuf;
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errout;
		}
		if(!strcmp(tokptr, "free"))
		{
			printk(KERN_CONT "\r\nFree 20 Rx skb to GMAC ring 0 !! \n\n");
			if(data->rx_Mring[0] == 0 || data->rx_skb[0] == 0)
			{
				printk(KERN_CONT "no rx_ring info!\n");
			}

			for(i=0;i<20;i++)
			{
				printk(KERN_CONT "[idx%3d]:desc[0x%p] \n", data->rx_Mtail[0], &data->rx_Mring[0][data->rx_Mtail[0]]);
				data->rx_Mring[0][data->rx_Mtail[0]].opts1 = (DescOwn | data->rx_buf_sz) | ((data->rx_Mtail[0] == (data->re8670_rx_ring_size[0] - 1))?RingEnd:0);
				updateGmacFlowControl(gmac, data->rx_Mtail[0], 0);
				data->rx_Mtail[0] = NEXT_RX(data->rx_Mtail[0],data->re8670_rx_ring_size[0]);
			}
		}
		else if(!strcmp(tokptr, "fc"))
		{
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			ring_num=simple_strtol(tokptr, NULL, 0);
			if(ring_num>=MAX_RXRING_NUM)
			{
				goto errout;	
			}
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;	
			}
			fc_enabled=simple_strtol(tokptr, NULL, 0);
			if(fc_enabled != 0 && fc_enabled != 1)
			{
				goto errout;
			}
			re8686_set_flow_control(gmac, ring_num, fc_enabled);
			printk(KERN_CONT "\r\nGMAC%d ring%d flow control is %s \n", gmac, ring_num, fc_enabled?"Enabled":"Disabled");
			goto show_fc_result;
		}
		else if(!strcmp(tokptr, "?") || !strcmp(tokptr, "help"))
		{
			goto errout;
		}
		else 
		{
			data->rx_ring_show_bitmap = simple_strtol(tokptr, NULL, 0);
			printk(KERN_CONT "\r\nrx_ring_show_bitmap 0x%08x \n", data->rx_ring_show_bitmap);
		}
	}
	else
	{
errout:
		printk(KERN_CONT "\r\nRx ring operation only support \"fc\", \"?\" or \"help\" as the first parameter\n");
		printk(KERN_CONT "Ring flow control:	\"fc ring_num(0~%d) switch(0/1)\"\n", (MAX_RXRING_NUM-1));
		printk(KERN_CONT "rx_ring bitmap: 0x%08x\n", data->rx_ring_show_bitmap);
show_fc_result:
		printk(KERN_CONT "flow control of GMAC%d of each Rx ring:\n", gmac);
		for(i=0;i<MAX_RXRING_NUM;i++)
		{
			printk(KERN_CONT "	Rx ring %d is %s\n", i, (data->re8670_rx_flow_control_status[i]==GMAC_ON)?"ON":"OFF");
		}
	}
	return count;
}
#if defined(CONFIG_RTL9607C_SERIES) && defined(HWNAT_CUSTOMIZE)
void rtk_dynamic_sram_state_set(rtk_enable_t state)
{	
	if(state==ENABLED)
		REG32(0xb8000204) |= 1; // SRAM CLK on
	else
		REG32(0xb8000204) &= ~1; // SRAM CLK off

	mdelay(10);

	return;
}
void rtk_dynamic_reset_to_dram_mode(void)
{	
	int i ;
	uint32 state_map, state_unmap;
	uint32 basedAddr_map, basedAddr_unmap;
	uint32 offset_map;
	rtk_dynamic_sram_size_t sram_size_map, sram_size_unmap;
	for(i = 0 ; i < 4 ; i ++)
	{
		if((i == 0 && previous_dynamic_sram_desc ==1))
		{
			
			uint32 tmpBuff, tmpBuff1;
			
			//cache invalid
			dma_cache_wback_inv((u32)dynamic_mapping_buffer[0], sramSizeMappingArray[RTK_DYNAMIC_SRAM_32K_BYTES]);
			dma_cache_wback_inv((u32)dynamic_mapping_buffer[1], sramSizeMappingArray[RTK_DYNAMIC_SRAM_8K_BYTES]);
			
			//mdelay(10);
			
			memset(0, UNCACHE_ADDR((unsigned long)dynamic_mapping_buffer[0]), sramSizeMappingArray[RTK_DYNAMIC_SRAM_32K_BYTES]);
			memset(0, UNCACHE_ADDR((unsigned long)dynamic_mapping_buffer[1]), sramSizeMappingArray[RTK_DYNAMIC_SRAM_8K_BYTES]);
			memcpy(&tmpBuff, UNCACHE_ADDR(dynamic_mapping_buffer[0]), sramSizeMappingArray[RTK_DYNAMIC_SRAM_32K_BYTES]);
			memcpy(&tmpBuff1, UNCACHE_ADDR(dynamic_mapping_buffer[1]), sramSizeMappingArray[RTK_DYNAMIC_SRAM_8K_BYTES]);	
			
			state_map = 0;
			state_map = REG32(0xb8004000 + (0x10*i));
			REG32(0xb8004000 + (0x10*i))= (state_map&0xfffffffe); 
			mdelay(10);
			//copy original from dram to sram
			memcpy(UNCACHE_ADDR(dynamic_mapping_buffer[0]), &tmpBuff, sramSizeMappingArray[RTK_DYNAMIC_SRAM_32K_BYTES]);
			memcpy(UNCACHE_ADDR(dynamic_mapping_buffer[1]), &tmpBuff1, sramSizeMappingArray[RTK_DYNAMIC_SRAM_8K_BYTES]);


			//rtk_dynamic_sram_set(1, TRUE, dynamic_mapping_buffer[1], RTK_DYNAMIC_SRAM_8K_BYTES, sramSizeMappingArray[RTK_DYNAMIC_SRAM_32K_BYTES]);
			
		
		}
		else
		{
		
			state_map = 0;
			state_map = REG32(0xb8004000 + (0x10*i));
			REG32(0xb8004000 + (0x10*i))= (state_map&0xfffffffe); 

		}
	}

	if(rtk_dynamic_sram_state_get()==DISABLED)
	{
		printk(KERN_CONT "\033[1;33;41m[WARNING] SRAM clock is off, please turn on it first\033[0m\n");
		return;
	}
	
	for(i = 0 ; i < 4 ; i ++)
	{
		REG32(0xb8001304 + (0x10*i)) = 0x8;
		REG32(0xb8001300 + (0x10*i)) = 0x0;
	}
	/*
		Reset to default setting
		
		RTK.0> debug get soc-memory 0xb8001304
		Memory 0xb8001304 : 0x00000008
		RTK.0> debug get soc-memory 0xb8001314
		Memory 0xb8001314 : 0x00000008
		RTK.0> debug get soc-memory 0xb8001324
		Memory 0xb8001324 : 0x00000008
		RTK.0> debug get soc-memory 0xb8001334
		Memory 0xb8001334 : 0x00000008
		RTK.0> debug get soc-memory 0xb8001300
		Memory 0xb8001300 : 0x00000000
		RTK.0>
		RTK.0>
		RTK.0> debug get soc-memory 0xb8001310
		Memory 0xb8001310 : 0x00000000
		RTK.0> debug get soc-memory 0xb8001320
		Memory 0xb8001320 : 0x00000000
		RTK.0> debug get soc-memory 0xb8001330
		Memory 0xb8001330 : 0x00000000
	*/
	
	
	mdelay(10);
	
	return;
}

rtk_enable_t rtk_dynamic_sram_state_get(void)
{
	rtk_enable_t state = DISABLED;

	if(REG32(0xb8000204) & 0x1)
		state = ENABLED;
	else
		state = DISABLED;

	return state;
}

int rtk_dynamic_sram_set(uint32 index, uint32 state, void *addr, rtk_dynamic_sram_size_t sram_size, uint32 offset)
{
	uint32 buffSize;
	uint32 basedAddr = (((u32)(addr))&0x0fffff00);
	void *tmpBuff=NULL;

	if(rtk_dynamic_sram_state_get()==DISABLED)
	{
		printk(KERN_CONT "\033[1;33;41m[WARNING] SRAM clock is off, please turn on it first\033[0m\n");
		return FALSE;
	}
	if(index>=MAX_DYNAMIC_SRAM_SIZE)
	{
		printk(KERN_CONT "\033[1;33;41m[WARNING] index[%d] is out of range(0~3) \033[0m\n", index);
		return FALSE;
	}	
	if(sram_size<=RTK_DYNAMIC_SRAM_MIN_BYTES || sram_size>=RTK_DYNAMIC_SRAM_MAX_BYTES)
	{
		printk(KERN_CONT "\033[1;33;41m[WARNING] size is out of range(256~32K) \033[0m\n");
		return FALSE;
	}
	buffSize = sramSizeMappingArray[sram_size];

	printk(KERN_CONT "idx[%d] addr=0x%x, addr(uncached)=0x%x, basedAddr=0x%x buffSize=%d\n", index, (u32)addr, UNCACHE_ADDR(addr), basedAddr, buffSize);

	tmpBuff = kmalloc(buffSize, GFP_ATOMIC);
	if(tmpBuff==NULL)
	{
		printk(KERN_CONT "\033[1;33;41m[WARNING] memory is not enough \033[0m\n");
		return FALSE;
	}

	//cache invalid
	dma_cache_wback_inv((u32)addr, buffSize);
	memcpy(tmpBuff, UNCACHE_ADDR(addr), buffSize);
	//map to sram	
	REG32(0xb8004004 + (0x10*index)) = sram_size;
	REG32(0xb8004008 + (0x10*index)) = offset;
	REG32(0xb8004000 + (0x10*index)) = (basedAddr | state);
	//unmap dram
	REG32(0xb8001304 + (0x10*index)) = sram_size;
	REG32(0xb8001300 + (0x10*index)) = (basedAddr | state);

	mdelay(10);

	//copy original from dram to sram
	memcpy(UNCACHE_ADDR(addr), tmpBuff, buffSize);

	if(tmpBuff)
		kfree(tmpBuff);
	
	return TRUE;
}


int rtk_dynamic_sram_get(uint32 index, uint32 *state, uint32 *addrValue, rtk_dynamic_sram_size_t *sram_size, uint32 *offset)
{
	uint32 state_map, state_unmap;
	uint32 basedAddr_map, basedAddr_unmap;
	uint32 offset_map;
	rtk_dynamic_sram_size_t sram_size_map, sram_size_unmap;
	
	if(rtk_dynamic_sram_state_get()==DISABLED)
	{
		printk(KERN_CONT "\033[1;33;41m[WARNING] SRAM clock is off, please turn on it first\033[0m\n");
		return FALSE;
	}	
	if(index>=MAX_DYNAMIC_SRAM_SIZE)
	{
		printk(KERN_CONT "\033[1;33;41m[WARNING] index[%d] is out of range(0~3) \033[0m\n", index);
		return FALSE;
		}
	if(state==NULL || addrValue==NULL || sram_size==NULL || offset==NULL)
	{
		printk(KERN_CONT "\033[1;33;41m[WARNING] size is out of range(256~32K) \033[0m\n");
		return FALSE;
		}
	//map
	state_map = (REG32(0xb8004000 + (0x10*index)) & 0x1);
	basedAddr_map = (REG32(0xb8004000 + (0x10*index)) & ~0x1);
	sram_size_map = REG32(0xb8004004 + (0x10*index));
	offset_map = REG32(0xb8004008 + (0x10*index));
	//unmap
	state_unmap = (REG32(0xb8001300 + (0x10*index)) & 0x1);
	basedAddr_unmap = (REG32(0xb8001300 + (0x10*index)) & ~0x1);
	sram_size_unmap = REG32(0xb8001304 + (0x10*index));
		
	if(state_map!=state_unmap)
	{
		printk(KERN_CONT "\033[1;33;41m[WARNING] Index[%d]'s state is not synchronized, state_map=%d state_unmap=%d\033[0m\n", index, state_map, state_unmap);
	}
	else if(state_map)
	{
		if(basedAddr_map!=basedAddr_unmap)
	{
			printk(KERN_CONT "\033[1;33;41m[WARNING] Index[%d]'s basedAddr is not synchronized, basedAddr_map=%d basedAddr_unmap=%d\033[0m\n", index, basedAddr_map, basedAddr_unmap);
		}
		if(sram_size_map!=sram_size_unmap)
		{
			printk(KERN_CONT "\033[1;33;41m[WARNING] Index[%d]'s sram_size is not synchronized, sram_size_map=%d sram_size_unmap=%d\033[0m\n", index, sramSizeMappingArray[sram_size_map], sramSizeMappingArray[sram_size_unmap]);
		}
	}
	
	*state 		= state_map;
	*addrValue	= basedAddr_map;
	*sram_size	= sram_size_map;
	*offset	= offset_map;

	return TRUE;
}

void rtk_dynamic_sram_restart(uint32 gmac)
{
#if defined(HWNAT_CUSTOMIZE)
	uint32 i, addrValue, offset, flowIdx;
	rtk_enable_t state;
	rtk_dynamic_sram_size_t sram_size;
	void *pBuf, *pBuf2, *pSramBufStart;
	struct re_private *cp=&re_private_data[gmac];
	int rxRingNum=4,txRingNum=3, rxRingSize;;
	unsigned int sramBlockSizeIdx=RTK_DYNAMIC_SRAM_MIN_BYTES,previousSramBlockSizeIdx=RTK_DYNAMIC_SRAM_MIN_BYTES;
	printk(KERN_CONT "--------------rtk_dynamic_sram_restart!!!! gmac %d dynamic_sram_desc = %d\n",gmac, dynamic_sram_desc);
	if(dynamic_sram_desc==1) //nptv6 setting
	{
		rtk_dynamic_sram_state_set(ENABLED);
		
		if(gmac==0) //first enter this function
		{
			dynamic_mapping_buffer[0] = NULL;
			dynamic_mapping_buffer[1] = NULL;
			dynamic_mapping_buffer[0] = kzalloc(sramSizeMappingArray[RTK_DYNAMIC_SRAM_32K_BYTES]+DESC_ALIGN, GFP_KERNEL);
			dynamic_mapping_buffer[1] = kzalloc(sramSizeMappingArray[RTK_DYNAMIC_SRAM_8K_BYTES]+DESC_ALIGN, GFP_KERNEL);
			if(dynamic_mapping_buffer[0] && dynamic_mapping_buffer[1])
			{
				dma_cache_wback_inv((unsigned long)dynamic_mapping_buffer[0], sramSizeMappingArray[RTK_DYNAMIC_SRAM_32K_BYTES]+DESC_ALIGN);
				dma_cache_wback_inv((unsigned long)dynamic_mapping_buffer[1], sramSizeMappingArray[RTK_DYNAMIC_SRAM_8K_BYTES]+DESC_ALIGN);
				rtk_dynamic_sram_set(0, TRUE, dynamic_mapping_buffer[0], RTK_DYNAMIC_SRAM_32K_BYTES, 0x0);
				rtk_dynamic_sram_set(1, TRUE, dynamic_mapping_buffer[1], RTK_DYNAMIC_SRAM_8K_BYTES, sramSizeMappingArray[RTK_DYNAMIC_SRAM_32K_BYTES]);
			}
			else
			{
				if(dynamic_mapping_buffer[0]==NULL)
					printk(KERN_CONT "\033[1;33;41m[WARNING] Can not allocate a 32k buffer!! \033[0m\n");
				else
					kfree(dynamic_mapping_buffer[0]);
				
				if(dynamic_mapping_buffer[1]==NULL)
					printk(KERN_CONT "\033[1;33;41m[WARNING] Can not allocate a 8k buffer!! \033[0m\n");
				else
					kfree(dynamic_mapping_buffer[1]);
			}
		}
		if(dynamic_mapping_buffer[0]==NULL || dynamic_mapping_buffer[1]==NULL)
			return;
		
#if defined(HWNAT_CUSTOMIZE_NPTV6_SRAM_ACC_V2)
		if(gmac==hwnat_customized_up_gmac) //gmac 1
		{
			for(flowIdx=0; flowIdx<hwnat_customized_up_flowNum; flowIdx++)
			{
				rxRingNum = hwnat_customized_up_rx_ringNum - flowIdx; 	// 4, 3
				txRingNum = hwnat_customized_up_tx_ringNum - flowIdx;	// 3, 2
				printk(KERN_CONT "------------------rtk_dynamic_sram_restart : rxRingNum = %d txRingNum = %d\n", rxRingNum,txRingNum);
				pBuf = (u32)((dynamic_mapping_buffer[0] + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1));
				for(i=0; i<hwnat_customized_down_flowNum; i++)
				{
					rxRingSize = re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum-i];
					pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				}
				for(i=rxRingNum+1; i<=hwnat_customized_up_rx_ringNum; i++)
				{
					rxRingSize = re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[i];
					pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				}

				//rx desc	
				cp->rx_Mring[rxRingNum] = (DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				//refill rx desc
				for(i = 0; i < re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
					cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
					if (i == (re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
						cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
					else
						cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
					cp->rx_Mring[rxRingNum][i].opts2 = 0;
				}
				pBuf += sizeof(DMA_RX_DESC)*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//tx desc
				cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				pBuf += sizeof(DMA_TX_DESC)*re_private_data[gmac].re8670_tx_ring_size[txRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//fs first data block		
				re8686_tx_ring_hdr_buffer[gmac][rxRingNum]=pBuf;
				re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum] = (u32)pBuf | UNCACHE_MASK;
				pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;

				printk(KERN_CONT "\033[1;33;40m [gmac %d, rxRingNum %d, txRingNum %d] cp->rx_Mring=0x%x cp->tx_Mhqring=0x%x fs_data=0x%x \033[0m\n", 
						gmac, rxRingNum, txRingNum, cp->rx_Mring[rxRingNum], cp->tx_Mhqring[txRingNum], re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum]);
			}
		}
		if(gmac==hwnat_customized_up0_gmac) //gmac 2
		{
			printk(KERN_CONT "\033[1;33;40m hwnat_customized_up0_flowNum = %d \033[0m\n", hwnat_customized_up0_flowNum);
			for(flowIdx=0; flowIdx<hwnat_customized_up0_flowNum; flowIdx++)
			{
				rxRingNum = hwnat_customized_up0_rx_ringNum - flowIdx; 	// 4, 3
				txRingNum = hwnat_customized_up0_tx_ringNum - flowIdx;	// 3, 2

				if(rxRingNum==hwnat_customized_up0_rx_ringNum) // rx ring number 4
				{
					pBuf = (u32)((dynamic_mapping_buffer[1] + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1));
				}
				else
				{
					pBuf = (u32)((dynamic_mapping_buffer[0] + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1));
					for(i=0; i<hwnat_customized_down_flowNum; i++)
					{
						rxRingSize = re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum-i];
						pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					}
					for(i=0; i<hwnat_customized_up_flowNum; i++)
					{
						rxRingSize = re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum-i];
						pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					}
				}
				
				//rx desc	
				cp->rx_Mring[rxRingNum] = (DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				//refill rx desc
				for(i = 0; i < re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
					cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
					if (i == (re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
						cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
					else
						cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
					cp->rx_Mring[rxRingNum][i].opts2 = 0;
				}
				pBuf += sizeof(DMA_RX_DESC)*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//tx desc
				cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				pBuf += sizeof(DMA_TX_DESC)*re_private_data[gmac].re8670_tx_ring_size[txRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//fs first data block		
				re8686_tx_ring_hdr_buffer[gmac][rxRingNum]=pBuf;
				re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum] = (u32)pBuf | UNCACHE_MASK;
				pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;

				printk(KERN_CONT "\033[1;33;40m [gmac %d, rxRingNum %d, txRingNum %d] cp->rx_Mring=0x%x cp->tx_Mhqring=0x%x fs_data=0x%x \033[0m\n", 
						gmac, rxRingNum, txRingNum, cp->rx_Mring[rxRingNum], cp->tx_Mhqring[txRingNum], re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum]);
			}
		}
		if(gmac==hwnat_customized_down_gmac) //gmac 0
		{
			for(flowIdx=0; flowIdx<hwnat_customized_down_flowNum; flowIdx++)
			{
				rxRingNum = hwnat_customized_down_rx_ringNum - flowIdx; 	// 4, 3, 2, 1
				txRingNum = hwnat_customized_down_tx_ringNum - flowIdx;		// 3, 2, 1, 0

				pBuf = (u32)((dynamic_mapping_buffer[0] + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1));
				for(i=rxRingNum+1; i<=hwnat_customized_down_rx_ringNum; i++)
				{
					rxRingSize = re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[i];
					pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				}
				
				//rx desc	
				cp->rx_Mring[rxRingNum] = (DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				//refill rx desc
				for(i = 0; i < re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
					cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
					if (i == (re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
						cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
					else
						cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
					cp->rx_Mring[rxRingNum][i].opts2 = 0;
				}
				pBuf += sizeof(DMA_RX_DESC)*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//tx desc
				cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				pBuf += sizeof(DMA_TX_DESC)*re_private_data[gmac].re8670_tx_ring_size[txRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//fs first data block		
				re8686_tx_ring_hdr_buffer[gmac][rxRingNum]=pBuf;
				re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum] = (u32)pBuf | UNCACHE_MASK;
				pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;

				printk(KERN_CONT "\033[1;33;40m [gmac %d, rxRingNum %d, txRingNum %d] cp->rx_Mring=0x%x cp->tx_Mhqring=0x%x fs_data=0x%x \033[0m\n", 
						gmac, rxRingNum, txRingNum, cp->rx_Mring[rxRingNum], cp->tx_Mhqring[txRingNum], re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum]);
			}
		}
	
#else
		if(gmac==hwnat_customized_up_gmac) //gmac 1
		{
			for(flowIdx=0; flowIdx<hwnat_customized_up_flowNum; flowIdx++)
			{
				rxRingNum = hwnat_customized_up_rx_ringNum - flowIdx; 	// 4, 3, 2, 1
				txRingNum = hwnat_customized_up_tx_ringNum - flowIdx;	// 3, 2, 1, 0

				if(rxRingNum==hwnat_customized_up_rx_ringNum) // rx ring number = 4
				{
					pBuf = (u32)((dynamic_mapping_buffer[0] + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1));
					for(i=0; i<hwnat_customized_down_flowNum; i++)
					{
						rxRingSize = re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum-i];
						pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					}
					for(i=0; i<hwnat_customized_down0_flowNum; i++)
					{
						rxRingSize = re_private_data[hwnat_customized_down0_gmac].re8670_rx_ring_size[hwnat_customized_down0_rx_ringNum-i];
						pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					}
				}
				else // rx ring number = 3, 2, 1
				{
					pBuf = (u32)((dynamic_mapping_buffer[1] + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1));
					for(i=rxRingNum+1; i<hwnat_customized_up_rx_ringNum; i++)
					{
						rxRingSize = re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[i];
						pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
						pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
						pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					}
				}

				//rx desc	
				cp->rx_Mring[rxRingNum] = (DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				//refill rx desc
				for(i = 0; i < re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
					cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
					if (i == (re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
						cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
					else
						cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
					cp->rx_Mring[rxRingNum][i].opts2 = 0;
				}
				pBuf += sizeof(DMA_RX_DESC)*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//tx desc
				cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				pBuf += sizeof(DMA_TX_DESC)*re_private_data[gmac].re8670_tx_ring_size[txRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//fs first data block		
				re8686_tx_ring_hdr_buffer[gmac][rxRingNum]=pBuf;
				re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum] = (u32)pBuf | UNCACHE_MASK;
				pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;

				printk(KERN_CONT "\033[1;33;40m [gmac %d, rxRingNum %d, txRingNum %d] cp->rx_Mring=0x%x cp->tx_Mhqring=0x%x fs_data=0x%x \033[0m\n", 
						gmac, rxRingNum, txRingNum, cp->rx_Mring[rxRingNum], cp->tx_Mhqring[txRingNum], re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum]);
			}
		}
		if(gmac==hwnat_customized_down_gmac) //gmac 2
		{
			for(flowIdx=0; flowIdx<hwnat_customized_down_flowNum; flowIdx++)
			{
				rxRingNum = hwnat_customized_down_rx_ringNum - flowIdx; 	// 4, 3
				txRingNum = hwnat_customized_down_tx_ringNum - flowIdx;		// 3, 2

				pBuf = (u32)((dynamic_mapping_buffer[0] + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1));
				for(i=rxRingNum+1; i<=hwnat_customized_down_rx_ringNum; i++)
				{
					rxRingSize = re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[i];
					pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				}
				
				//rx desc	
				cp->rx_Mring[rxRingNum] = (DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				//refill rx desc
				for(i = 0; i < re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
					cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
					if (i == (re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
						cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
					else
						cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
					cp->rx_Mring[rxRingNum][i].opts2 = 0;
				}
				pBuf += sizeof(DMA_RX_DESC)*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//tx desc
				cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				pBuf += sizeof(DMA_TX_DESC)*re_private_data[gmac].re8670_tx_ring_size[txRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//fs first data block		
				re8686_tx_ring_hdr_buffer[gmac][rxRingNum]=pBuf;
				re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum] = (u32)pBuf | UNCACHE_MASK;
				pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;

				printk(KERN_CONT "\033[1;33;40m [gmac %d, rxRingNum %d, txRingNum %d] cp->rx_Mring=0x%x cp->tx_Mhqring=0x%x fs_data=0x%x \033[0m\n", 
						gmac, rxRingNum, txRingNum, cp->rx_Mring[rxRingNum], cp->tx_Mhqring[txRingNum], re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum]);
			}
		}
		if(gmac==hwnat_customized_down0_gmac) //gmac 0
		{
			for(flowIdx=0; flowIdx<hwnat_customized_down0_flowNum; flowIdx++)
			{
				rxRingNum = hwnat_customized_down0_rx_ringNum - flowIdx; 	// 4, 3
				txRingNum = hwnat_customized_down0_tx_ringNum - flowIdx;	// 3, 2

				pBuf = (u32)((dynamic_mapping_buffer[0] + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1));
				for(i=0; i<hwnat_customized_down_flowNum; i++)
				{
					rxRingSize = re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum-i];
					pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				}
				for(i=rxRingNum+1; i<=hwnat_customized_down0_rx_ringNum; i++)
				{
					rxRingSize = re_private_data[hwnat_customized_down0_gmac].re8670_rx_ring_size[i];
					pBuf += sizeof(DMA_RX_DESC)*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += sizeof(DMA_TX_DESC)*rxRingSize*2;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
					pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE*rxRingSize;
					pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				}
				
				//rx desc	
				cp->rx_Mring[rxRingNum] = (DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				//refill rx desc
				for(i = 0; i < re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
					cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
					if (i == ( re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
						cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
					else
						cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
					cp->rx_Mring[rxRingNum][i].opts2 = 0;
				}
				pBuf += sizeof(DMA_RX_DESC)* re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//tx desc
				cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
				pBuf += sizeof(DMA_TX_DESC)* re_private_data[gmac].re8670_tx_ring_size[txRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
				//fs first data block		
				re8686_tx_ring_hdr_buffer[gmac][rxRingNum]=pBuf;
				re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum] = (u32)pBuf | UNCACHE_MASK;
				pBuf += MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE* re_private_data[gmac].re8670_rx_ring_size[rxRingNum];
				pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;

				printk(KERN_CONT "\033[1;33;40m [gmac %d, rxRingNum %d, txRingNum %d] cp->rx_Mring=0x%x cp->tx_Mhqring=0x%x fs_data=0x%x \033[0m\n", 
						gmac, rxRingNum, txRingNum, cp->rx_Mring[rxRingNum], cp->tx_Mhqring[txRingNum], re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum]);
			}
		}
#endif		
	}
	else if(dynamic_sram_desc==2) //VXLAN setting
	{
		rtk_dynamic_sram_state_set(ENABLED);
		
		if(gmac==hwnat_customized_up_gmac)
		{
			//combine rxring and txring to single 8K or 16K sram zone
			rxRingNum=hwnat_customized_up_rx_ringNum;
			txRingNum=hwnat_customized_up_tx_ringNum;
			sramBlockSizeIdx=dynamic_mapping_buffer_size_idx[0];
			
			pBuf = kzalloc(sramSizeMappingArray[sramBlockSizeIdx]+(DESC_ALIGN*2), GFP_KERNEL);
			if(!pBuf)return;
			dynamic_mapping_buffer[0]=pBuf;
			dma_cache_wback_inv((unsigned long)pBuf, sramSizeMappingArray[sramBlockSizeIdx]+(DESC_ALIGN*2));
			pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
			cp->rx_Mring[rxRingNum]=(DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);

			//refill rx desc
			for(i = 0; i <  re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
				cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
				if (i == ( re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
					cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
				else
					cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
				cp->rx_Mring[rxRingNum][i].opts2 = 0;
			}
			
			pBuf += RE8670_RXRING_BYTES( re_private_data[gmac].re8670_rx_ring_size[rxRingNum])-DESC_ALIGN;
			pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
			cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);

			pBuf += RE8670_TXRING_BYTES( re_private_data[gmac].re8670_tx_ring_size[txRingNum])-DESC_ALIGN;
			re8686_tx_ring_hdr_buffer[gmac][rxRingNum]=pBuf;
			//sram base address should be aligned with 0x100(256)
			pBuf = (unsigned char*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN -1) );
			re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum]=(u32)pBuf | UNCACHE_MASK;
			printk(KERN_CONT "pSramBufsize = %d + %d + %d\n",RE8670_RXRING_BYTES( re_private_data[gmac].re8670_rx_ring_size[rxRingNum])-DESC_ALIGN,RE8670_TXRING_BYTES( re_private_data[gmac].re8670_tx_ring_size[txRingNum])-DESC_ALIGN, re_private_data[gmac].re8670_rx_ring_size[rxRingNum]*MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE);

			rtk_dynamic_sram_set(0, 1, re_private_data[gmac].rx_Mring[rxRingNum], sramBlockSizeIdx, 0x0);
		}
		if(gmac==hwnat_customized_down_gmac)
		{
			//combine rxring and txring to single 8K sram zone
			rxRingNum=hwnat_customized_down_rx_ringNum;
			txRingNum=hwnat_customized_down_tx_ringNum;
			sramBlockSizeIdx=dynamic_mapping_buffer_size_idx[2];
			
			pBuf = kzalloc(sramSizeMappingArray[sramBlockSizeIdx]+(DESC_ALIGN*2), GFP_KERNEL);
			if(!pBuf)return;
			dynamic_mapping_buffer[2]=pBuf;
			dma_cache_wback_inv((unsigned long)pBuf, sramSizeMappingArray[sramBlockSizeIdx]+(DESC_ALIGN*2));
			pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
			cp->rx_Mring[rxRingNum]=(DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);

			//refill rx desc
			for(i = 0; i < re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
				cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
				if (i == (re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
					cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
				else
					cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
				cp->rx_Mring[rxRingNum][i].opts2 = 0;
			}
			
			pBuf += RE8670_RXRING_BYTES(re_private_data[gmac].re8670_rx_ring_size[rxRingNum])-DESC_ALIGN;
			pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
			cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);
			printk(KERN_CONT "pSramBufsize = %d + %d\n",RE8670_RXRING_BYTES(re_private_data[gmac].re8670_rx_ring_size[rxRingNum])-DESC_ALIGN,RE8670_TXRING_BYTES(re_private_data[gmac].re8670_tx_ring_size[txRingNum])-DESC_ALIGN);
			
			rtk_dynamic_sram_set(2, 1, re_private_data[gmac].rx_Mring[rxRingNum], sramBlockSizeIdx, sramSizeMappingArray[dynamic_mapping_buffer_size_idx[0]]+sramSizeMappingArray[dynamic_mapping_buffer_size_idx[1]]);

		}
		if(gmac==hwnat_customized_extra_gmac){
			//upstream
			//combine rxring and txring to single 8K sram zone
			rxRingNum=hwnat_customized_extra_up_rx_ringNum;
			txRingNum=hwnat_customized_extra_up_tx_ringNum;
			sramBlockSizeIdx=dynamic_mapping_buffer_size_idx[1];
			
			pBuf = kzalloc(sramSizeMappingArray[sramBlockSizeIdx]+(DESC_ALIGN*2), GFP_KERNEL);
			if(!pBuf)return;
			pBuf2 = kzalloc(sramSizeMappingArray[sramBlockSizeIdx]+(DESC_ALIGN*2), GFP_KERNEL);
			if(!pBuf2){
				kfree(pBuf);
				return;
			}
			dynamic_mapping_buffer[1]=pBuf;
			
			dma_cache_wback_inv((unsigned long)pBuf, sramSizeMappingArray[sramBlockSizeIdx]+(DESC_ALIGN*2));
			pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
			cp->rx_Mring[rxRingNum]=(DMA_RX_DESC*)((u32)(pBuf) | UNCACHE_MASK);

			//refill rx desc
			for(i = 0; i <  re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
				cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
				if (i == ( re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
					cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
				else
					cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
				cp->rx_Mring[rxRingNum][i].opts2 = 0;
			}
			
			pBuf += RE8670_RXRING_BYTES( re_private_data[gmac].re8670_rx_ring_size[rxRingNum])-DESC_ALIGN;
			pBuf = (void*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
			cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);

			pBuf += RE8670_TXRING_BYTES( re_private_data[gmac].re8670_tx_ring_size[txRingNum])-DESC_ALIGN;
			//sram base address should be aligned with 0x100(256)
			pBuf = (unsigned char*)( (u32)(pBuf + DESC_ALIGN - 1) & ~(DESC_ALIGN -1) );
			re8686_tx_ring_hdr_buffer[gmac][rxRingNum]=pBuf;
			re8686_tx_ring_hdr_buffer_sram_aligned[gmac][rxRingNum]=(u32)pBuf | UNCACHE_MASK;
			printk(KERN_CONT "pSramBufsize = %d + %d + %d\n",RE8670_RXRING_BYTES( re_private_data[gmac].re8670_rx_ring_size[rxRingNum])-DESC_ALIGN,RE8670_TXRING_BYTES( re_private_data[gmac].re8670_tx_ring_size[txRingNum])-DESC_ALIGN, re_private_data[gmac].re8670_rx_ring_size[rxRingNum]*MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE);

			rtk_dynamic_sram_set(1, 1, re_private_data[gmac].rx_Mring[rxRingNum], sramBlockSizeIdx, sramSizeMappingArray[dynamic_mapping_buffer_size_idx[0]]);
			
			//downstream
			//combine rxring and txring to single 8K sram zone
			rxRingNum=hwnat_customized_extra_down_rx_ringNum;
			txRingNum=hwnat_customized_extra_down_tx_ringNum;
			sramBlockSizeIdx=dynamic_mapping_buffer_size_idx[3];
			dynamic_mapping_buffer[3]=pBuf2;

			dma_cache_wback_inv((unsigned long)pBuf2, sramSizeMappingArray[sramBlockSizeIdx]+(DESC_ALIGN*2));
			pBuf2 = (void*)( (u32)(pBuf2 + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
			cp->rx_Mring[rxRingNum]=(DMA_RX_DESC*)((u32)(pBuf2) | UNCACHE_MASK);

			//refill rx desc
			for(i = 0; i <  re_private_data[gmac].re8670_rx_ring_size[rxRingNum]; i++){
				cp->rx_Mring[rxRingNum][i].addr = (u32)cp->rx_skb[rxRingNum][i].skb->data|UNCACHE_MASK;
				if (i == ( re_private_data[gmac].re8670_rx_ring_size[rxRingNum] - 1))		  
					cp->rx_Mring[rxRingNum][i].opts1 = (DescOwn | RingEnd | cp->rx_buff_size);
				else
					cp->rx_Mring[rxRingNum][i].opts1 =(DescOwn | cp->rx_buff_size);
				cp->rx_Mring[rxRingNum][i].opts2 = 0;
			}

			pBuf2 += RE8670_RXRING_BYTES( re_private_data[gmac].re8670_rx_ring_size[rxRingNum])-DESC_ALIGN;
			pBuf2 = (void*)( (u32)(pBuf2 + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1) ) ;
			cp->tx_Mhqring[txRingNum]=(DMA_TX_DESC*)((u32)(pBuf2) | UNCACHE_MASK);
			printk(KERN_CONT "pSramBufsize = %d + %d\n",RE8670_RXRING_BYTES( re_private_data[gmac].re8670_rx_ring_size[rxRingNum])-DESC_ALIGN,RE8670_TXRING_BYTES( re_private_data[gmac].re8670_tx_ring_size[txRingNum])-DESC_ALIGN);

			rtk_dynamic_sram_set(3, 1, re_private_data[gmac].rx_Mring[rxRingNum], sramBlockSizeIdx, sramSizeMappingArray[dynamic_mapping_buffer_size_idx[0]]+sramSizeMappingArray[dynamic_mapping_buffer_size_idx[1]]+sramSizeMappingArray[dynamic_mapping_buffer_size_idx[2]]);
		}
	}
	else if(dynamic_sram_desc==0 && rtk_dynamic_sram_state_get()==ENABLED) // dram mode
	{
		printk(KERN_CONT "------------------------Reset to dram mode!\n");
		for(i=0; i<MAX_DYNAMIC_SRAM_SIZE; i++)
		{
			rtk_dynamic_sram_get(i, &state, &addrValue, &sram_size, &offset);
			if(state)
				break;
		}
		if(i==MAX_DYNAMIC_SRAM_SIZE)
			rtk_dynamic_sram_state_set(DISABLED);
		else
		{
			rtk_dynamic_reset_to_dram_mode();
			rtk_dynamic_sram_state_set(DISABLED);
		}
	}
#endif	
	return;
}


static int dynamic_sram_desc_read(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	int len = 0, i,j;
	rtk_enable_t state;
	uint32 addrValue;
	rtk_dynamic_sram_size_t sram_size;
	uint32 offset;
	
	printk(KERN_CONT "%s(%d)\n", (dynamic_sram_desc>0)?"Enable":"Disable", dynamic_sram_desc);
	
	if((dynamic_sram_desc>0) != rtk_dynamic_sram_state_get())
		printk(KERN_CONT "\033[1;33;41m[WARNING] State of dynamic_sram_desc is changed, please use re8670_reset to reset rx/tx descriptors\033[0m\n");
	if(rtk_dynamic_sram_state_get()==DISABLED)
	{
		printk(KERN_CONT "Sram clock is disabled\n");
		return SUCCESS;
	}
	else
		printk(KERN_CONT "Sram clock is enabled\n");
	
	printk(KERN_CONT "========== Dynamic sram table ==========\n");
	for(i=0; i<MAX_DYNAMIC_SRAM_SIZE; i++)
	{
		if(rtk_dynamic_sram_get(i, &state, &addrValue, &sram_size, &offset) && state>0)
			printk(KERN_CONT "Index[%d] state=%s addrValue=0x%x sram_size=%d offset=%d\n", i, (state)?"Enable":"Disable", addrValue, sramSizeMappingArray[sram_size], offset);
	}

	

	return len;
}

static int dynamic_sram_desc_write(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	char 	tmpbuf[512];
	char	*strptr;
	static struct re_private *data;
	unsigned int i, gmac;
	uint32 addrValue;
	rtk_dynamic_sram_size_t sram_size;
	uint32 offset;
	
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;	
	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		tmpbuf[count] = '\0';
		strptr=tmpbuf;
		if( strlen(strptr)==0 )
		{
			goto errout;
		}
		if(strncmp(strptr, "1", 1) == 0)
		{
			previous_dynamic_sram_desc = dynamic_sram_desc;
			dynamic_sram_desc = 1;
		}
		else if(strncmp(strptr, "2", 1) == 0)
		{
			previous_dynamic_sram_desc = dynamic_sram_desc;
			dynamic_sram_desc = 2;
		}
		else if(strncmp(strptr, "0", 1) == 0)
		{
			previous_dynamic_sram_desc = dynamic_sram_desc;
			dynamic_sram_desc = 0;
		}
		else
		{
			printk(KERN_CONT "\033[1;33;41m[WARNING] Please enter 0 or 1 or 2\033[0m\n");
			goto errout;
		}
	}

	printk(KERN_CONT "%s %s\n", __func__, (dynamic_sram_desc>0)?"Enable":"Disable");
	if (previous_dynamic_sram_desc != dynamic_sram_desc)
	{
		re8670_reset();
	}
	//if((dynamic_sram_desc>0) != rtk_dynamic_sram_state_get())
		//printk("\033[1;33;41m[WARNING] State of dynamic_sram_desc is changed, please use re8670_open to reset rx/tx descriptors\033[0m\n");
errout:	
	return count;
}
#ifdef HWNAT_CUSTOMIZE
static int hwnat_customized_version_read(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	int len = 0, i, rxRingNum, gmac;
	static struct re_private *data;
	rtk_enable_t state;
	uint32 addrValue;
	rtk_dynamic_sram_size_t sram_size;
	uint32 offset;

	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;	
	
	printk(KERN_CONT "version: %d\n", hwnat_customized_version);
#if 0
	for(rxRingNum=0; rxRingNum<MAX_RXRING_NUM; rxRingNum++)
	{
		if(re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum])
		{
			printk("===== re8686_rx_descIdx_customized_tx_descAddr, rxRingNum=%d ===== \n", rxRingNum);
			for(i=0; i<re8670_rx_ring_size[gmac][rxRingNum]; i++)
			{
				printk("fs.tx_descAddr[%d]=0x%x \n", i, re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum][i].fs_descAddr);
				printk("ls.tx_descAddr[%d]=0x%x \n", i, re8686_rx_descIdx_customized_tx_descAddr[gmac][rxRingNum][i].ls_descAddr);
			}
		}
	}
#endif	
	for(rxRingNum=0; rxRingNum<MAX_RXRING_NUM; rxRingNum++)
	{
		if(re8686_rx_ring_data_buffer[gmac][rxRingNum])
		{
			printk(KERN_CONT "===== re8686_rx_ring_previousDesc, rxRingNum=%d ===== \n", rxRingNum);
			for(i=0; i<CHECK_TX_OWN_BIT_INTERVAL_NUM; i++)
			{
				printk(KERN_CONT "rx_ring_previousDesc[%d]=%d \n", i, re8686_rx_ring_previousDesc[gmac][rxRingNum][i]);

			}
		}	
	}
	
	return len;
}

void _hwnat_customized_version_set_by_gmac(int gmac)
{
	int i = 0, j = 0;
	
	{
		hwnat_customized_version = 2;
		printk(KERN_CONT "In _hwnat_customized_version_set_by_gmac: dynamic_sram_desc = %d\n",dynamic_sram_desc);
			
		if(dynamic_sram_desc==0){  // dram mode setting
		
			hwnat_customized_up_gmac=1;
			hwnat_customized_down_gmac=2;
			
			hwnat_customized_up_rx_ringNum=4;
			hwnat_customized_up_tx_ringNum=3;
			hwnat_customized_down_rx_ringNum=4;
			hwnat_customized_down_tx_ringNum=3;
			if(gmac == hwnat_customized_up_gmac)
			{
				re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=1024;
				re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=1024;
			}
			else if(gmac == hwnat_customized_down_gmac)
			{
				re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=1024;
				re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=1024;
			}
			
		}else if(dynamic_sram_desc==1){  // NPTv6 sram mode setting
			//if(chipID == RTL9607C_CHIP_ID){
#if defined(HWNAT_CUSTOMIZE_NPTV6_SRAM_ACC_V2)
				hwnat_customized_up_gmac = 1;
				hwnat_customized_up0_gmac = 2;
				hwnat_customized_down_gmac = 0;
#if defined(CONFIG_CMCC) //CMCC
				hwnat_customized_up_flowNum = 2;
#elif defined(CONFIG_YUEME)

				hwnat_customized_up_flowNum = 3;
#endif
				
				
				hwnat_customized_up_rx_ringNum = 4;
				hwnat_customized_up_tx_ringNum = 3;

#if defined(CONFIG_CMCC) //CMCC
				hwnat_customized_up0_flowNum = 2;			
#elif defined(CONFIG_YUEME)

				hwnat_customized_up0_flowNum = 1;				
#endif
				
				hwnat_customized_up0_rx_ringNum = 4;
				hwnat_customized_up0_tx_ringNum = 3;

				hwnat_customized_down_flowNum = 4;
				hwnat_customized_down_rx_ringNum = 4;
				hwnat_customized_down_tx_ringNum = 3;

				if(gmac == hwnat_customized_up_gmac)
				{
					for(i=0; i<hwnat_customized_up_flowNum; i++)
					{
#if defined(CONFIG_CMCC) //CMCC
						re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum-i]=64;
						re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum-i]=128;
						
#elif defined(CONFIG_YUEME)

						if(i==2)
						{
							re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum-i]=32;
							re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum-i]=64;
						}
						else
						{
							re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum-i]=64;
							re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum-i]=128;

						}
#endif
					}

				}
				if(gmac == hwnat_customized_up0_gmac)
				{
					for(i=0; i<hwnat_customized_up0_flowNum; i++)
					{
#if defined(CONFIG_CMCC) //CMCC
						if(i==0)
						{
							re_private_data[hwnat_customized_up0_gmac].re8670_rx_ring_size[hwnat_customized_up0_rx_ringNum-i]=64;
							re_private_data[hwnat_customized_up0_gmac].re8670_tx_ring_size[hwnat_customized_up0_tx_ringNum-i]=128;
						}
						else
						{
								re_private_data[hwnat_customized_up0_gmac].re8670_rx_ring_size[hwnat_customized_up0_rx_ringNum-i]=32;
								re_private_data[hwnat_customized_up0_gmac].re8670_tx_ring_size[hwnat_customized_up0_tx_ringNum-i]=64;
						}
#elif defined(CONFIG_YUEME)

						if(i==0)
						{
							re_private_data[hwnat_customized_up0_gmac].re8670_rx_ring_size[hwnat_customized_up0_rx_ringNum-i]=64;
							re_private_data[hwnat_customized_up0_gmac].re8670_tx_ring_size[hwnat_customized_up0_tx_ringNum-i]=128;
						}
#endif
					}

				}
				if(gmac == hwnat_customized_down_gmac)
				{	
					for(i=0; i<hwnat_customized_down_flowNum; i++)
					{
						re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum-i]=32;
						re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum-i]=64;	
					}

				}
				
#else
				hwnat_customized_up_gmac = 1;
				hwnat_customized_down_gmac = 2;
				hwnat_customized_down0_gmac = 0;

				hwnat_customized_up_flowNum = 4;
				hwnat_customized_up_rx_ringNum = 4;
				hwnat_customized_up_tx_ringNum = 3;

				hwnat_customized_down_flowNum = 2;
				hwnat_customized_down_rx_ringNum = 4;
				hwnat_customized_down_tx_ringNum = 3;

				hwnat_customized_down0_flowNum = 2;
				hwnat_customized_down0_rx_ringNum = 4;
				hwnat_customized_down0_tx_ringNum = 3;

				if(gmac == hwnat_customized_up_gmac)
				{
					for(i=0; i<hwnat_customized_up_flowNum; i++)
					{
						if(i<2)
						{
							re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum-i]=32;
							re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum-i]=64;
						}
						else
						{
							re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum-i]=16;
							re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum-i]=32;
						}
					}
				}
				if(gmac == hwnat_customized_down_gmac)
				{
					for(i=0; i<hwnat_customized_down_flowNum; i++)
					{
						re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum-i]=64;
						re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum-i]=128;
					}

				}

				if(gmac == hwnat_customized_down0_gmac)
				{
					for(i=0; i<hwnat_customized_down0_flowNum; i++)
					{
						re_private_data[hwnat_customized_down0_gmac].re8670_rx_ring_size[hwnat_customized_down0_rx_ringNum-i]=64;
						re_private_data[hwnat_customized_down0_gmac].re8670_tx_ring_size[hwnat_customized_down0_tx_ringNum-i]=128;
					}

				}
#endif				

		}else{ // VXLAN sram mode setting
				hwnat_customized_up_gmac=1;
				hwnat_customized_down_gmac=2;
				hwnat_customized_extra_gmac=0;
			
				hwnat_customized_up_rx_ringNum=4;
				hwnat_customized_up_tx_ringNum=3;
				hwnat_customized_down_rx_ringNum=4;
				hwnat_customized_down_tx_ringNum=3;
				hwnat_customized_extra_up_rx_ringNum = 4;
				hwnat_customized_extra_up_tx_ringNum = 3;
				hwnat_customized_extra_down_rx_ringNum = 3;
				hwnat_customized_extra_down_tx_ringNum = 2;

				if(gmac == hwnat_customized_up_gmac)
				{
					re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=128;
					re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]*2;
				
				}
				if(gmac == hwnat_customized_down_gmac)
				{
					re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=128;
					re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum];
				
				}
				if(gmac == hwnat_customized_extra_gmac)
				{
					
					re_private_data[hwnat_customized_extra_gmac].re8670_rx_ring_size[hwnat_customized_extra_up_rx_ringNum]=64;
					re_private_data[hwnat_customized_extra_gmac].re8670_tx_ring_size[hwnat_customized_extra_up_tx_ringNum]=re_private_data[hwnat_customized_extra_gmac].re8670_rx_ring_size[hwnat_customized_extra_up_rx_ringNum]*2;
					re_private_data[hwnat_customized_extra_gmac].re8670_rx_ring_size[hwnat_customized_extra_down_rx_ringNum]=64;
					re_private_data[hwnat_customized_extra_gmac].re8670_tx_ring_size[hwnat_customized_extra_down_tx_ringNum]=re_private_data[hwnat_customized_extra_gmac].re8670_rx_ring_size[hwnat_customized_extra_down_rx_ringNum];
				}
				
				//sram mapping plan
				dynamic_mapping_buffer_size_idx[0]=RTK_DYNAMIC_SRAM_16K_BYTES;
				dynamic_mapping_buffer_size_idx[1]=RTK_DYNAMIC_SRAM_8K_BYTES;
				dynamic_mapping_buffer_size_idx[2]=RTK_DYNAMIC_SRAM_8K_BYTES;
				dynamic_mapping_buffer_size_idx[3]=RTK_DYNAMIC_SRAM_8K_BYTES;
			
		}
	}
	if(gmac == hwnat_customized_up_gmac){
		for(i=0; i<=hwnat_customized_up_rx_ringNum; i++)
			higherq[hwnat_customized_up_gmac][i] &= ~(0x1<<hwnat_customized_up_rx_ringNum);
	}
	if(gmac == hwnat_customized_down_gmac){
		for(i=0; i<=hwnat_customized_down_rx_ringNum; i++)
			higherq[hwnat_customized_down_gmac][i] &= ~(0x1<<hwnat_customized_down_rx_ringNum);
	}

	if(gmac == hwnat_customized_extra_gmac){
		for(i=0; i<=hwnat_customized_extra_up_rx_ringNum; i++)
			higherq[hwnat_customized_extra_gmac][i] &= ~(0x1<<hwnat_customized_extra_up_rx_ringNum);
		for(i=0; i<=hwnat_customized_extra_down_rx_ringNum; i++)
			higherq[hwnat_customized_extra_gmac][i] &= ~(0x1<<hwnat_customized_extra_down_rx_ringNum);
	}

	
	
	
	

	if(dynamic_sram_desc)
	{
		if(gmac == hwnat_customized_up_gmac)
			re_private_data[hwnat_customized_up_gmac].iocmd1_reg |= TX_en_precise_dma;
		
		if(gmac == hwnat_customized_down_gmac)
			re_private_data[hwnat_customized_down_gmac].iocmd1_reg |= TX_en_precise_dma;

		if(gmac == hwnat_customized_down0_gmac)
			re_private_data[hwnat_customized_down0_gmac].iocmd1_reg |= TX_en_precise_dma;
		
		if(gmac == hwnat_customized_extra_gmac)
			re_private_data[hwnat_customized_extra_gmac].iocmd1_reg |= TX_en_precise_dma;
	}
	else if(dynamic_sram_desc == 0)
	{
		if(gmac == hwnat_customized_up_gmac && (re_private_data[hwnat_customized_up_gmac].iocmd1_reg&TX_en_precise_dma) )
			re_private_data[hwnat_customized_up_gmac].iocmd1_reg &= ~(TX_en_precise_dma);
		
		if(gmac == hwnat_customized_down_gmac && (re_private_data[hwnat_customized_down_gmac].iocmd1_reg & TX_en_precise_dma))
			re_private_data[hwnat_customized_down_gmac].iocmd1_reg &= ~(TX_en_precise_dma);

		if(gmac == hwnat_customized_down0_gmac && (re_private_data[hwnat_customized_down0_gmac].iocmd1_reg & TX_en_precise_dma) )
			re_private_data[hwnat_customized_down0_gmac].iocmd1_reg &= ~(TX_en_precise_dma);
		
		if(gmac == hwnat_customized_extra_gmac && (re_private_data[hwnat_customized_extra_gmac].iocmd1_reg & TX_en_precise_dma) )
			re_private_data[hwnat_customized_extra_gmac].iocmd1_reg &= ~(TX_en_precise_dma);
	}
}

void _hwnat_customized_version_set(int version){
	int i,j;
	printk(KERN_CONT "_hwnat_customized_version_set!!!!!!!! version = %d\n",version);
	if(version==1)
	{
		hwnat_customized_version = 1;
		if(dynamic_sram_desc==0){
			//if(chipID == RTL9607C_CHIP_ID){
				hwnat_customized_up_gmac=1;
				hwnat_customized_down_gmac=2;
		/*
			}else{
				hwnat_customized_up_gmac=0;
				hwnat_customized_down_gmac=0;
			}
		*/
			hwnat_customized_up_rx_ringNum=4;
			hwnat_customized_up_tx_ringNum=3;
			hwnat_customized_down_rx_ringNum=4;
			hwnat_customized_down_tx_ringNum=3;
			re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=1024;
			re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=1024;
			re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=1024;
			re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=1024;
			
		}else if(dynamic_sram_desc==1){
			//if(chipID == RTL9607C_CHIP_ID){
				hwnat_customized_up_gmac=1;
				hwnat_customized_down_gmac=2;
			
				hwnat_customized_up_rx_ringNum=4;
				hwnat_customized_up_tx_ringNum=3;
				hwnat_customized_down_rx_ringNum=4;
				hwnat_customized_down_tx_ringNum=3;
				re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=256;
				re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=256;
			/*	
			}else{
				hwnat_customized_up_gmac=0;
				hwnat_customized_down_gmac=0;
			
				hwnat_customized_up_rx_ringNum=4;
				hwnat_customized_up_tx_ringNum=3;
				hwnat_customized_down_rx_ringNum=3;
				hwnat_customized_down_tx_ringNum=2;
				re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=256;
				re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=256;
			}
			*/
		}else{
			//if(chipID == RTL9607C_CHIP_ID){
				hwnat_customized_up_gmac=1;
				hwnat_customized_down_gmac=2;
			
				hwnat_customized_up_rx_ringNum=4;
				hwnat_customized_up_tx_ringNum=3;
				hwnat_customized_down_rx_ringNum=4;
				hwnat_customized_down_tx_ringNum=3;
				re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=256;
				re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=256;
			/*}else{
				hwnat_customized_up_gmac=0;
				hwnat_customized_down_gmac=0;
			
				hwnat_customized_up_rx_ringNum=4;
				hwnat_customized_up_tx_ringNum=3;
				hwnat_customized_down_rx_ringNum=3;
				hwnat_customized_down_tx_ringNum=2;
				re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=256;
				re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=256;
			}*/
		}
	}
	else
	{
		hwnat_customized_version = 2;
		printk(KERN_CONT "In _hwnat_customized_version_set: dynamic_sram_desc = %d\n",dynamic_sram_desc);
			
		if(dynamic_sram_desc==0){  // dram mode setting
			//if(chipID == RTL9607C_CHIP_ID){
				hwnat_customized_up_gmac=1;
				hwnat_customized_down_gmac=2;
			/*}else{
				hwnat_customized_up_gmac=0;
				hwnat_customized_down_gmac=0;
			}*/
			
			hwnat_customized_up_rx_ringNum=4;
			hwnat_customized_up_tx_ringNum=3;
			hwnat_customized_down_rx_ringNum=4;
			hwnat_customized_down_tx_ringNum=3;
			re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=1024;
			re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=1024;
			re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=1024;
			re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=1024;
			
		}else if(dynamic_sram_desc==1){  // NPTv6 sram mode setting
			//if(chipID == RTL9607C_CHIP_ID){
#if defined(HWNAT_CUSTOMIZE_NPTV6_SRAM_ACC_V2)
				hwnat_customized_up_gmac = 1;
				hwnat_customized_up0_gmac = 2;
				hwnat_customized_down_gmac = 0;

				hwnat_customized_up_flowNum = 2;
				hwnat_customized_up_rx_ringNum = 4;
				hwnat_customized_up_tx_ringNum = 3;

				hwnat_customized_up0_flowNum = 2;
				hwnat_customized_up0_rx_ringNum = 4;
				hwnat_customized_up0_tx_ringNum = 3;

				hwnat_customized_down_flowNum = 4;
				hwnat_customized_down_rx_ringNum = 4;
				hwnat_customized_down_tx_ringNum = 3;

				for(i=0; i<hwnat_customized_up_flowNum; i++)
				{
					re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum-i]=64;
					re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum-i]=128;
				}
				for(i=0; i<hwnat_customized_up0_flowNum; i++)
				{
					if(i==0)
					{
						re_private_data[hwnat_customized_up0_gmac].re8670_rx_ring_size[hwnat_customized_up0_rx_ringNum-i]=64;
						re_private_data[hwnat_customized_up0_gmac].re8670_tx_ring_size[hwnat_customized_up0_tx_ringNum-i]=128;
					}
					else
					{
						re_private_data[hwnat_customized_up0_gmac].re8670_rx_ring_size[hwnat_customized_up0_rx_ringNum-i]=32;
						re_private_data[hwnat_customized_up0_gmac].re8670_tx_ring_size[hwnat_customized_up0_tx_ringNum-i]=64;
					}
				}
				for(i=0; i<hwnat_customized_down_flowNum; i++)
				{
					re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum-i]=32;
					re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum-i]=64;	
				}
				
#else
				hwnat_customized_up_gmac = 1;
				hwnat_customized_down_gmac = 2;
				hwnat_customized_down0_gmac = 0;

				hwnat_customized_up_flowNum = 4;
				hwnat_customized_up_rx_ringNum = 4;
				hwnat_customized_up_tx_ringNum = 3;

				hwnat_customized_down_flowNum = 2;
				hwnat_customized_down_rx_ringNum = 4;
				hwnat_customized_down_tx_ringNum = 3;

				hwnat_customized_down0_flowNum = 2;
				hwnat_customized_down0_rx_ringNum = 4;
				hwnat_customized_down0_tx_ringNum = 3;

				for(i=0; i<hwnat_customized_up_flowNum; i++)
				{
					if(i<2)
					{
						re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum-i]=32;
						re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum-i]=64;
					}
					else
					{
						re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum-i]=16;
						re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum-i]=32;
					}
				}
				for(i=0; i<hwnat_customized_down_flowNum; i++)
				{
					re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum-i]=64;
					re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum-i]=128;
				}
				for(i=0; i<hwnat_customized_down0_flowNum; i++)
				{
					re_private_data[hwnat_customized_down0_gmac].re8670_rx_ring_size[hwnat_customized_down0_rx_ringNum-i]=64;
					re_private_data[hwnat_customized_down0_gmac].re8670_tx_ring_size[hwnat_customized_down0_tx_ringNum-i]=128;
				}
#endif				
			/*}else{
				hwnat_customized_up_gmac=0;
				hwnat_customized_down_gmac=0;
			
				hwnat_customized_up_rx_ringNum=4;
				hwnat_customized_up_tx_ringNum=3;
				hwnat_customized_down_rx_ringNum=3;
				hwnat_customized_down_tx_ringNum=2;
				re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=128;
				re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=128;
				re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=256;
			}*/
		}else{ // VXLAN sram mode setting
			//if(chipID == RTL9607C_CHIP_ID){
				hwnat_customized_up_gmac=1;
				hwnat_customized_down_gmac=2;
				hwnat_customized_extra_gmac=0;
			
				hwnat_customized_up_rx_ringNum=4;
				hwnat_customized_up_tx_ringNum=3;
				hwnat_customized_down_rx_ringNum=4;
				hwnat_customized_down_tx_ringNum=3;
				hwnat_customized_extra_up_rx_ringNum = 4;
				hwnat_customized_extra_up_tx_ringNum = 3;
				hwnat_customized_extra_down_rx_ringNum = 3;
				hwnat_customized_extra_down_tx_ringNum = 2;
				
				re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=128;
				re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]*2;
				re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=128;
				re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum];
				re_private_data[hwnat_customized_extra_gmac].re8670_rx_ring_size[hwnat_customized_extra_up_rx_ringNum]=64;
				re_private_data[hwnat_customized_extra_gmac].re8670_tx_ring_size[hwnat_customized_extra_up_tx_ringNum]=re_private_data[hwnat_customized_extra_gmac].re8670_rx_ring_size[hwnat_customized_extra_up_rx_ringNum]*2;
				re_private_data[hwnat_customized_extra_gmac].re8670_rx_ring_size[hwnat_customized_extra_down_rx_ringNum]=64;
				re_private_data[hwnat_customized_extra_gmac].re8670_tx_ring_size[hwnat_customized_extra_down_tx_ringNum]=re_private_data[hwnat_customized_extra_gmac].re8670_rx_ring_size[hwnat_customized_extra_down_rx_ringNum];

				//sram mapping plan
				dynamic_mapping_buffer_size_idx[0]=RTK_DYNAMIC_SRAM_16K_BYTES;
				dynamic_mapping_buffer_size_idx[1]=RTK_DYNAMIC_SRAM_8K_BYTES;
				dynamic_mapping_buffer_size_idx[2]=RTK_DYNAMIC_SRAM_8K_BYTES;
				dynamic_mapping_buffer_size_idx[3]=RTK_DYNAMIC_SRAM_8K_BYTES;
			/*}else{
				hwnat_customized_up_gmac=0;
				hwnat_customized_down_gmac=0;
			
				hwnat_customized_up_rx_ringNum=4;
				hwnat_customized_up_tx_ringNum=3;
				hwnat_customized_down_rx_ringNum=3;
				hwnat_customized_down_tx_ringNum=2;
				re_private_data[hwnat_customized_up_gmac].re8670_rx_ring_size[hwnat_customized_up_rx_ringNum]=256;
				re_private_data[hwnat_customized_down_gmac].re8670_rx_ring_size[hwnat_customized_down_rx_ringNum]=128;
				re_private_data[hwnat_customized_up_gmac].re8670_tx_ring_size[hwnat_customized_up_tx_ringNum]=512;
				re_private_data[hwnat_customized_down_gmac].re8670_tx_ring_size[hwnat_customized_down_tx_ringNum]=128;
			}*/
		}
	}
	for(i=0; i<=hwnat_customized_up_rx_ringNum; i++)
		higherq[hwnat_customized_up_gmac][i] &= ~(0x1<<hwnat_customized_up_rx_ringNum);
	for(i=0; i<=hwnat_customized_down_rx_ringNum; i++)
		higherq[hwnat_customized_down_gmac][i] &= ~(0x1<<hwnat_customized_down_rx_ringNum);
	for(i=0; i<=hwnat_customized_extra_up_rx_ringNum; i++)
		higherq[hwnat_customized_extra_gmac][i] &= ~(0x1<<hwnat_customized_extra_up_rx_ringNum);
	for(i=0; i<=hwnat_customized_extra_down_rx_ringNum; i++)
		higherq[hwnat_customized_extra_gmac][i] &= ~(0x1<<hwnat_customized_extra_down_rx_ringNum);

	if(dynamic_sram_desc)
	{
		re_private_data[hwnat_customized_up_gmac].iocmd1_reg |= TX_en_precise_dma;
		re_private_data[hwnat_customized_down_gmac].iocmd1_reg |= TX_en_precise_dma;
		re_private_data[hwnat_customized_down0_gmac].iocmd1_reg |= TX_en_precise_dma;
		re_private_data[hwnat_customized_extra_gmac].iocmd1_reg |= TX_en_precise_dma;
	}
}

static int hwnat_customized_version_write(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	char 	tmpbuf[512];
	char	*strptr;
	static struct re_private *data;
	unsigned int i, gmac;
	uint32 addrValue;
	rtk_dynamic_sram_size_t sram_size;
	uint32 offset;
	
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		tmpbuf[count] = '\0';
		strptr=tmpbuf;
		if( strlen(strptr)==0 )
		{
			goto errout;
		}
		if(strncmp(strptr, "1", 1) == 0){
			_hwnat_customized_version_set(1);
		}else if(strncmp(strptr, "2", 1) == 0){
			_hwnat_customized_version_set(2);	
		}else{
			printk(KERN_CONT "\033[1;33;41m[WARNING] Please enter 1 or 2 \033[0m\n");
			goto errout;
		}
	}

	printk(KERN_CONT "version %d\n", hwnat_customized_version);
errout:	
	return count;
}

static int hwnat_customized_check_tx_done_read(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	int len = 0;
	
	printk(KERN_CONT "%s(%d)\n", (hwnat_customized_check_tx_done)?"Enable":"Disable", hwnat_customized_check_tx_done);

	return len;
}

static int hwnat_customized_check_tx_done_write(struct file *filp, char *buf, size_t count, loff_t *offp)
{
	char 	tmpbuf[512];
	char	*strptr;
	static struct re_private *data;
	unsigned int gmac;
	
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;	
	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		tmpbuf[count] = '\0';
		strptr=tmpbuf;
		if( strlen(strptr)==0 )
		{
			goto errout;
		}
		if(strncmp(strptr, "1", 1) == 0)
			hwnat_customized_check_tx_done = 1;
		else if(strncmp(strptr, "0", 1) == 0)
		{
			if(dynamic_sram_desc==0/*dram mode*/)
			{
				printk(KERN_CONT "\033[1;33;41m[WARNING] Dram mode does not support!!\033[0m\n");
				goto errout;
			}
			hwnat_customized_check_tx_done = 0;
		}
		else
		{
			printk(KERN_CONT "\033[1;33;41m[WARNING] Please enter 0 or 1\033[0m\n");
			goto errout;
		}
	}

	printk(KERN_CONT "%s(%d)\n", (hwnat_customized_check_tx_done)?"Enable":"Disable", hwnat_customized_check_tx_done);
	
errout:	
	return count;
}

#endif
#endif

static int rx_ring_read(struct seq_file *m, void *v)
{
	static struct re_private *data;
	struct file *file = m->private;
	unsigned int gmac;
	int len = 0;
	int i = 0,j = 0;
	data=pde_data(file_inode(file));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;
	
	for(j=0;j<MAX_RXRING_NUM;j++)
	{
		if(data->rx_ring_show_bitmap&(1<<j))
		{
			seq_printf(m, "==================rx_ring [%d] info==================\n\n",j);
			if(data->rx_Mring[j] == 0 || data->rx_skb[j] == 0){
				seq_printf(m, "no rx_ring info!\n");
				continue;
			}

			for(i=0;i<data->re8670_rx_ring_size[j];i++){
				seq_printf(m, "[idx%3d]:desc[0x%p]->skb[0x%p]->buf[0x%08x]:%s", 
					i, &data->rx_Mring[j][i], data->rx_skb[j][i].skb, 
					data->rx_Mring[j][i].addr, 
					(data->rx_Mring[j][i].opts1 & DescOwn)? "NIC" : "CPU");
				if(i == data->rx_Mtail[j]){
					seq_printf(m, "<=rx_tail");
				}
				if(j == 0 && i == RLE0787_R16(gmac, RxCDO)) {
					seq_printf(m, "<=RxCDO");
				} else if (j != 0 && i == RLE0787_R16(gmac, RxCDO2+(ADDR_OFFSET*(j-1)))) {
					seq_printf(m, "<=RxCDO%d", j);
				}
				seq_printf(m, "\n");
			}
		}
	}
	return len;
}

static ssize_t tx_ring_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int gmac;
	char 	tmpbuf[64];
	char	*strptr;
	char	*tokptr;
	size_t	len;

	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;
	len = min(count, sizeof(tmpbuf) - 1);

	if (buf && !copy_from_user(tmpbuf, buf, len)) {
		tmpbuf[len] = '\0';
		if (len)
			tmpbuf[len - 1] = '\0'; /* strip extra CR */

		if (!str_valid(tmpbuf))
			return -EINVAL;

		strptr=tmpbuf;
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errout;
		}
		data->tx_ring_show_bitmap = simple_strtol(tokptr, NULL, 0);
		printk(KERN_CONT "tx_ring_show_bitmap 0x%08x\n", data->tx_ring_show_bitmap);
	}
	else
	{
errout:
		printk(KERN_CONT "tx_ring_show_bitmap: 0x%08x\n", data->tx_ring_show_bitmap);
	}
	return count;
}
static int tx_ring_read(struct seq_file *m, void *v)
{
	static struct re_private *data;
	struct file *file = m->private;
	unsigned int gmac;
	int len = 0;
	int i = 0,j = 0;
	data=pde_data(file_inode(file));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;
	for(j=0;j<MAX_TXRING_NUM;j++)
	{
		if(data->tx_ring_show_bitmap&(1<<j))
		{
			seq_printf(m, "=============FDP%d:tx_ring [%d] info==================\n\n",idx_sw2hw(j)+1,j);
			if(data->tx_Mhqring[j] == 0 || data->tx_skb[j] == 0){
				printk(KERN_INFO "no tx_ring info\n");
				continue;
			}

			for(i=0;i<data->re8670_tx_ring_size[j];i++) {
				seq_printf(m, "[idx%3d]:desc[0x%p]->skb[0x%p]->buf[0x%08x]:%s", 
					i, &data->tx_Mhqring[j][i], data->tx_skb[j][i].skb, 
					data->tx_Mhqring[j][i].addr, 
					(data->tx_Mhqring[j][i].opts1 & DescOwn)? "NIC" : "CPU");
				if(i == data->tx_Mhqtail[j]) {
					seq_printf(m, "<=tx_hqtail");
				}
				if(i == data->tx_Mhqhead[j]) {
					seq_printf(m, "<=tx_hqhead");
				}
				if(i == RLE0787_R16(gmac, TxCDO1+(ADDR_OFFSET*idx_hw2sw(i)))) {
					seq_printf(m, "<=TxCDO%d", idx_hw2sw(i));
				}
				seq_printf(m, "\n");
			}
		}
	}

	return len;
}

#ifdef TX_BACKUP_RING
int nic_available_tx_ring_get(struct re_private *cp)
{
	DMA_TX_DESC *txd;
	int i, cpu_own;
	int ring_idx;

	for (ring_idx=0 ; ring_idx<MAX_TXRING_NUM ; ring_idx++)
	{
		for(i=0;i<cp->re8670_tx_ring_size[ring_idx];i++)
		{
			cpu_own=0;
			txd = (DMA_TX_DESC *)((u32)&cp->tx_Mhqring[ring_idx][i]|0xa0000000);
			if (!(txd->opts1&0x80000000))
				cpu_own++;

			if (cpu_own >=2)
				return ring_idx;
		}
	}

	return -1;
}
#endif

void nic_tx_ring_dump(unsigned int gmac, struct seq_file *m)
{
	struct re_private *cp;
	DMA_TX_DESC *txd;
	int ring_index=0;
	int i;
	
	cp=re_private_data_root.re_private_data_ptr[gmac];	
	for(ring_index=0;ring_index<MAX_TXRING_NUM;ring_index++)
	{
		if(cp->tx_ring_show_bitmap&(1<<ring_index))
		{
			for(i=0;i<cp->re8670_tx_ring_size[ring_index];i++)
			{
				txd = (DMA_TX_DESC *)((u32)&cp->tx_Mhqring[ring_index][i]|0xa0000000);
				seq_printf(m, "%08x[%03d] %08x %08x %08x %08x %08x OWN=%d E=%d F=%d L=%d LEN=%05d LSO=%d MTU=%d",(u32)&txd->opts1,i,txd->opts1,txd->addr,txd->opts2,txd->opts3,txd->opts4
					,(txd->opts1&0x80000000)?1:0
					,(txd->opts1&0x40000000)?1:0
					,(txd->opts1&0x20000000)?1:0
					,(txd->opts1&0x10000000)?1:0
					,(txd->opts1&0x1ffff)
					,(txd->opts4&0x80000000)?1:0
					,(txd->opts4&0x7ff00000)>>20
				);
				if(i == cp->tx_Mhqtail[ring_index]) {
					seq_printf(m, "<=tx_hqtail");
				}
				if(i == cp->tx_Mhqhead[ring_index]) {
					seq_printf(m, "<=tx_hqhead");
				}
				if(i == RLE0787_R16(gmac, TxCDO1+(ADDR_OFFSET*idx_hw2sw(ring_index)))) {
					seq_printf(m, "<=TxCDO%d", idx_hw2sw(ring_index));
				}
				seq_printf(m, "\n");
			}
			seq_printf(m, "\n");
		}
	}
	
#ifdef TX_RING_DEBUG
	if(cp->tx_ring_backup_debug)
	{
		seq_printf(m, "###########################Additional Tx ring debug info start###########################\n");
		for(ring_index=0;ring_index<MAX_TXRING_NUM;ring_index++)
		{
			if(cp->tx_ring_show_bitmap&(1<<ring_index))
			{
				for(i=0;i<cp->re8670_tx_ring_size[ring_index];i++)
				{
					txd = (DMA_TX_DESC *)((u32)&cp->rtl8686_tx_ring_debug[ring_index].txDescriptor[i]|0xa0000000);
					
					seq_printf(m, "%08x[%03d] %08x %08x %08x %08x %08x OWN=%d E=%d F=%d L=%d LEN=%05d LSO=%d MTU=%d\n",(u32)&txd->opts1,i,txd->opts1,txd->addr,txd->opts2,txd->opts3,txd->opts4
						,(txd->opts1&0x80000000)?1:0
						,(txd->opts1&0x40000000)?1:0
						,(txd->opts1&0x20000000)?1:0
						,(txd->opts1&0x10000000)?1:0
						,(txd->opts1&0x1ffff)
						,(txd->opts4&0x80000000)?1:0
						,(txd->opts4&0x7ff00000)>>20
					);
				}
				seq_printf(m, "\n");
			}
		}
		seq_printf(m, "###########################Additional Tx ring debug info end###########################\n");
	}
#endif	
}

static int nic_tx_ring_dump_read(struct seq_file *m, void *v)
{
	static struct re_private *data;
	struct file *file = m->private;
	unsigned int gmac;
	data=pde_data(file_inode(file));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;
	nic_tx_ring_dump(gmac, m);
	return 0;
}

static ssize_t nic_tx_ring_dump_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{	
	return	tx_ring_write(filp, buf, count, offp);
}


void nic_rx_ring_dump(unsigned int gmac, struct seq_file *m)
{
	struct re_private *cp;
	DMA_RX_DESC *rxd;
	int i;
	int ring_index=0;

	cp=re_private_data_root.re_private_data_ptr[gmac];	
	for(ring_index=0;ring_index<MAX_RXRING_NUM;ring_index++)
	{
		if(cp->rx_ring_show_bitmap&(1<<ring_index))
		{
			for(i=0;i<cp->re8670_rx_ring_size[ring_index];i++)
			{
				rxd = (DMA_RX_DESC *)((u32)&cp->rx_Mring[ring_index][i]|0xa0000000);
				seq_printf(m, "%08x[%03d] %08x %08x %08x %08x OWN=%d E=%d LEN=%05d",(u32)&rxd->opts1,i,rxd->opts1,rxd->addr,rxd->opts2,rxd->opts3
					,(rxd->opts1&0x80000000)?1:0
					,(rxd->opts1&0x40000000)?1:0
					,(rxd->opts1&0xfff)
				);
				if(ring_index < MAX_RXRING_NUM && i == cp->rx_Mtail[ring_index]) {
					seq_printf(m, "<=rx_Mtail");
				}
				if (ring_index < MAX_RXRING_NUM) {
					if(ring_index == 0 && i == RLE0787_R16(gmac, RxCDO)) {
						seq_printf(m, "<=RxCDO");
					} else if (ring_index != 0 && i == RLE0787_R16(gmac, RxCDO2+(ADDR_OFFSET*(ring_index-1)))) {
						seq_printf(m, "<=RxCDO%d", ring_index);
				}
				}
				seq_printf(m, "\n");
			}
			seq_printf(m, "\n");
		}
	}
}

static int nic_rx_ring_dump_read(struct seq_file *m, void *v)
{
	static struct re_private *data;
	struct file *file = m->private;
	unsigned int gmac;
	data=pde_data(file_inode(file));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;
	nic_rx_ring_dump(gmac, m);
	return 0;
}

static ssize_t nic_rx_ring_dump_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	return	rx_ring_write(filp, buf, count, offp);
}

static int padding_enable_read(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int gmac;
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;
	//printk("[padding_enable = %d]\n", data->padding_enable);
	switch (data->padding_enable) {
	case 0:
		printk(KERN_CONT "0, (padding is disable)\n");
		break;
	case 1:
		printk(KERN_CONT "1, (padding is enable)\n");
		break;
	default:
		printk(KERN_CONT "Error, (padding_enable is invalid)\n");
	}
	return 0;
}

void gmac_padding_enable(unsigned int gmac, char enable)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	
	if (gmac>2)
		goto error;
#ifndef CONFIG_GMAC1_USABLE
	if (gmac==1)
		goto error;
#endif
#ifndef CONFIG_GMAC2_USABLE
	if (gmac==2)
		goto error;
#endif
	if(enable==PADDING_ENABLED)
	{
		cp->padding_enable=1;
		RLE0787_W32(gmac, TCR, RLE0787_R32(gmac, TCR) & (~0x1));
		printk(KERN_CONT "Enable gmac[%d] padding\n", gmac);
	}
	else
	{
		cp->padding_enable=0;
		RLE0787_W32(gmac, TCR, RLE0787_R32(gmac, TCR) | 0x1);
		printk(KERN_CONT "Disable gmac[%d] padding\n", gmac);
	}
	
	return;
error:
	printk(KERN_CONT "%s: ignored gmac %d\n",__func__,gmac);
}

static ssize_t padding_enable_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int gmac;
	unsigned char tmpBuf[16] = {0};
	int i=0;	
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	gmac = data->gmac;
	if (buf && count==2 && !copy_from_user(tmpBuf, buf, count))
	{
		tmpBuf[count] = '\0';
		i=simple_strtoul(tmpBuf, NULL, 10);
		switch (i) {
		case 0:
			gmac_padding_enable(gmac, PADDING_DISABLED);
			break;
		case 1:
			gmac_padding_enable(gmac, PADDING_ENABLED);
			break;
		default:
			printk(KERN_CONT "Unknown Setting\nDisable: echo 0 > /proc/rtl8686gmac/padding_enable\nEnable: echo 1 > /proc/rtl8686gmac/padding_enable\n");
			break;
		}
	}

	return count;
}

#ifdef CONFIG_RTL8192CD
static int wifi_tx_qos_mapping_read(struct seq_file *m, void *v)
{
	static struct re_private *data;
	struct file *file = m->private;
	unsigned int i;
	
	data=pde_data(file_inode(file));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}
	
	seq_printf(m, "Wifi Tx QoS mapping: \n");
	seq_printf(m, "  1,2: BK low\n");
	seq_printf(m, "  0,3: BE\n");
	seq_printf(m, "  4,5: VI\n");
	seq_printf(m, "  6,7: VO high\n");
	seq_printf(m, "  wifi_tx_qos_enable: %s\n", data->wifi_tx_qos_enable ? "ON":"OFF");
	if(data->wifi_tx_qos_enable)
	{
		for(i=0 ; i<GMAC_MAX_QUEUE_NUM ; i++)
		{
			seq_printf(m, "  internal priority %d mapping to wifi priority %d ", i, data->wifi_tx_qos_mapping[i]);
			switch(data->wifi_tx_qos_mapping[i])
			{
				case 1:
				case 2:
					seq_printf(m, "BK, queue 0.\n");
					break;
				case 0:
				case 3:
					seq_printf(m, "BE, queue 1.\n");
					break;
				case 4:
				case 5:
					seq_printf(m, "VI, queue 2.\n");
					break;
				case 6:
				case 7:
					seq_printf(m, "VO, queue 3.\n");
					break;
				default:
					seq_printf(m, "[WARNING] mapping FAIL !\n");
					break;
			}
		}
	}
	
	return 0;
}

static int wifi_tx_qos_mapping_write(struct file *filp, char *buf, size_t count, loff_t *offp )
{
	static struct re_private *data;
	unsigned int wifi_queue_index;
	unsigned int internal_priority;
	char 		tmpbuf[64];
	char		*strptr;
	char		*tokptr;
	unsigned long buf_size;

	/*
		1,2: BK low
		0,3: BE
		4,5: VI
		6,7: VO high
	*/

	data=pde_data(file_inode(filp));
	buf_size = min(count, (sizeof(tmpbuf)-1));
	if (buf && !copy_from_user(tmpbuf, buf, buf_size)) {
		tmpbuf[buf_size] = '\0';

		if (!str_valid(tmpbuf))
			return -EINVAL;

		strptr=tmpbuf;
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errout;
		}
		internal_priority = simple_strtol(tokptr, NULL, 0);
		if(!strcmp(tokptr, "enable")) 
		{
			data->wifi_tx_qos_enable = GMAC_ON;
			printk(KERN_CONT "wifi_tx_qos_enable=%d ", data->wifi_tx_qos_enable);
		}
		else if(!strcmp(tokptr, "disable"))
		{
			data->wifi_tx_qos_enable = GMAC_OFF;
			printk(KERN_CONT "wifi_tx_qos_enable=%d ", data->wifi_tx_qos_enable);
		}
		else
		{
			if(internal_priority > 7)
			{
				printk(KERN_CONT "invalid internal_priority=%d ", internal_priority);
				goto errout;
			}
			
			printk(KERN_CONT "internal priority %d ", internal_priority);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			wifi_queue_index = simple_strtol(tokptr, NULL, 0);

			if(wifi_queue_index > 3)
			{
				printk(KERN_CONT "invalid wifi_queue_index=%d ", wifi_queue_index);
				goto errout;
			}
			
			printk(KERN_CONT "mapping to wifi queue %d\n", wifi_queue_index);
			if(wifi_queue_index == 0)
				data->wifi_tx_qos_mapping[internal_priority] = 1;
			else if(wifi_queue_index == 1)
				data->wifi_tx_qos_mapping[internal_priority] = 3;
			else if(wifi_queue_index == 2)
				data->wifi_tx_qos_mapping[internal_priority] = 5;
			else if(wifi_queue_index == 3)
				data->wifi_tx_qos_mapping[internal_priority] = 7;
		}
	}
	else
	{
errout:
		printk(KERN_CONT "internal_priority wifi_queue_index\n");
		return -EFAULT;
	}
		
	return count;
}
#endif

static int skb_dynamic_allocate_disable_read(struct seq_file *m, void *v)
{
	struct re_private_root *root_cp = &re_private_data_root;

	seq_printf(m, "allocate dynamic skb: %s\n", root_cp->skb_dynamic_allocate_disable?"Disabled":"Enabled");

	return 0;
}

static ssize_t skb_dynamic_allocate_disable_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	static struct re_private_root *data;
	unsigned int gmac;
	unsigned char tmpBuf[16] = {0};
	int len = (count > 15) ? 15 : count;
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
		return 0;
	}

	if (buf && !copy_from_user(tmpBuf, buf, len))
	{
		data->skb_dynamic_allocate_disable=(u8)simple_strtoul(tmpBuf, NULL, 10);
		printk(KERN_CONT "allocate dynamic skb: %s\n", data->skb_dynamic_allocate_disable?"Disabled":"Enabled");
		return count;
	}

	return -EFAULT;
}

#if defined(CONFIG_RTK_SOC_RTL8198D)
static int qos_dump_info(rtk_port_t port){
	rtk_qid_t queue;
	rtk_enable_t enable, IfgInclude;
	uint32 meterIdx, q_rate, p_rate;

	if(port>=SW_PORT_NUM)
		return 0;

	// get port BW
	if(rtk_rate_portEgrBandwidthCtrlRate_get(port,&p_rate) < 0){
		printk(KERN_CONT "%s:%d fail to get port rate for port%d!\n",__FUNCTION__,__LINE__,port);
		return 0;
	}
	printk(KERN_CONT "port[%d] | status:%d | BW:%d=======================\n", port,LCDev_mapping[port].status,p_rate);
	for(queue=0;queue<HAL_MAX_NUM_OF_QUEUE();queue++){
		meterIdx = 0;
		// get queue enable
		if(rtk_rate_egrQueueBwCtrlEnable_get(port,queue,&enable) < 0){
			printk(KERN_CONT "%s:%d fail to get enable for port%d queue%d!\n",__FUNCTION__,__LINE__,port,queue);
			return 0;
		}
		// get queue mapping to meterIdx
		if(rtk_rate_egrQueueBwCtrlMeterIdx_get(port,queue,&meterIdx) < 0){
			printk(KERN_CONT "%s:%d fail to get meterIdx for port%d queue%d!\n",__FUNCTION__,__LINE__,port,meterIdx);
			return 0;
		}else{
			meterIdx += (port%6)*8;
		}
		// get rate of meterIdx
		if(rtk_rate_shareMeter_get(meterIdx,&q_rate,&IfgInclude) < 0){
			printk(KERN_CONT "%s:%d fail to get rate or IfgInclude for meterIdx%d!\n",__FUNCTION__,__LINE__,meterIdx);
			return 0;
		}
		printk(KERN_CONT "queue%d -> enable=%d | meterIdx=%d | rate=%d | IfgInclude=%d\n", queue, enable, meterIdx, q_rate, IfgInclude);
	}

	return 1;
}

static void qos_help(void){
	int i;
	printk(KERN_CONT "port:");
	for(i=0;i<SW_PORT_NUM;i++){
		if(HAL_IS_PORT_EXIST(i))
			printk("%d,",i);
	}
	printk(KERN_CONT "queue:0~%d,enable:0/1,meterIdx:0~%d,rate:0~4194296\n",HAL_MAX_NUM_OF_QUEUE()-1,HAL_MAX_NUM_OF_METERING()-1);
	printk(KERN_CONT "Set queue enable:   echo set_en [port] [queue] [enable] > proc/rtl8686gmac/qos_cfg\n");
	printk(KERN_CONT "Map queue to Meter: echo q2meter [port] [queue] [meterIdx] > proc/rtl8686gmac/qos_cfg\n");
	printk(KERN_CONT "Set Meter rate:     echo set_rate [MeterIdx] [rate] > proc/rtl8686gmac/qos_cfg\n");
	printk(KERN_CONT "Get port info :     echo r [port] > proc/rtl8686gmac/qos_cfg\n");
	return;
}

static int qos_cfg_read(struct seq_file *m, void *v){
	rtk_port_t port;
	for(port=0;port<SW_PORT_NUM;port++){
		if(!HAL_IS_PORT_EXIST(port))
			continue;
		if(!qos_dump_info(port)){
			printk(KERN_CONT "%s:%d dump qos_cfg fail for port %d!\n",__FUNCTION__,__LINE__,port);
			return 0;
		}
	}
	return 0;
}

static int qos_cfg_write(struct file *filp, char *buf, size_t count, loff_t *offp ){
	char tmpbuf[64];
	char  *strptr,*tokptr;
	uint32 cmd_buff[2] = {0};
	int i=0;
	rtk_port_t port;
	rtk_qid_t queue;
	uint32 meterIdx, meterRate;
	rtk_enable_t enable;
	int len = (count > 63) ? 63 : count;

	if (buf && !copy_from_user(tmpbuf, buf, len)) {
		tmpbuf[len] = '\0';
		strptr=tmpbuf;
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
			goto err;

		if(!memcmp(tokptr, "q2meter", 7)){// queue map to meter
			if(sscanf(buf,"q2meter %d %d %d",&port,&queue,&meterIdx)!=3){
				printk(KERN_CONT "%s:%d Invalid input!\n",__FUNCTION__,__LINE__);
				goto err;
			}
			if(!HAL_IS_PORT_EXIST(port) || queue>=HAL_MAX_NUM_OF_QUEUE() || meterIdx>=HAL_MAX_NUM_OF_METERING()){ 
				printk(KERN_CONT "%s:%d variables %d,%d,%d out of range!\n",__FUNCTION__,__LINE__,port,queue,meterIdx);
				goto err;
			}
			else{
				if(rtk_rate_egrQueueBwCtrlMeterIdx_set(port, queue, meterIdx)<0){
					printk(KERN_CONT "%s:%d fail in rtk_rate_shareMeter_set!\n",__FUNCTION__,__LINE__);
					goto err;
				}
				else
					printk(KERN_CONT "%s:%d rtk_rate_shareMeter_set success!\n",__FUNCTION__,__LINE__);
			}
			
		}
		else if(!memcmp(tokptr, "set_en", 6)){// queue enable
			if(sscanf(buf,"set_en %d %d %d",&port,&queue,&enable)!=3){
				printk(KERN_CONT "%s:%d Invalid input!\n",__FUNCTION__,__LINE__);
				goto err;
			}
			if(!HAL_IS_PORT_EXIST(port) || queue>=HAL_MAX_NUM_OF_QUEUE() || enable>1){
				printk(KERN_CONT "%s:%d variables %d,%d,%d out of range!\n",__FUNCTION__,__LINE__,port,queue,enable);
				goto err;
			}
			else{
				if(rtk_rate_egrQueueBwCtrlEnable_set(port, queue, enable)<0){
					printk(KERN_CONT "%s:%d fail in rtk_rate_egrQueueBwCtrlEnable_set!\n",__FUNCTION__,__LINE__);
					goto err;
				}
				else
					printk(KERN_CONT "%s:%d rtk_rate_egrQueueBwCtrlEnable_set success!\n",__FUNCTION__,__LINE__);
			}
		}
		else if(!memcmp(tokptr, "set_rate", 8)){// meter rate
			if(sscanf(buf,"set_rate %d %d",&meterIdx,&meterRate)!=2){
				printk(KERN_CONT "%s:%d Invalid input!\n",__FUNCTION__,__LINE__);
				goto err;
			}
			if(meterIdx>=HAL_MAX_NUM_OF_METERING() || meterRate> (0x7ffff<<3) ){
				printk(KERN_CONT "%s:%d invalid variables!\n",__FUNCTION__,__LINE__);
				goto err;
			}
			else{
				if(rtk_rate_shareMeter_set(meterIdx, meterRate, 1)<0){
					printk(KERN_CONT "%s:%d fail in rtk_rate_shareMeter_set!\n",__FUNCTION__,__LINE__);
					goto err;
				}
				else
					printk(KERN_CONT "%s:%d rtk_rate_shareMeter_set success!\n",__FUNCTION__,__LINE__);
			}
		}
		else if(!memcmp(tokptr, "r", 1)){// read info
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
				goto err;
			port = (rtk_port_t)simple_strtol(tokptr, NULL, 0);
			if(!HAL_IS_PORT_EXIST(port)){
				printk(KERN_CONT "%s:%d Invalid port %d!\n",__FUNCTION__,__LINE__,port);
				goto err;
			}
			else if(!qos_dump_info(port)){
				printk(KERN_CONT "%s:%d Fail to dump qos info!\n",__FUNCTION__,__LINE__);
				goto err;
			}
		}
		else
			goto err;
	}
	else{
err:
		qos_help();
	}

	return count;

}

static void asic_counter_help(void){
	int i;
	printk(KERN_CONT "port:");
	for(i=0;i<SW_PORT_NUM;i++){
		if(HAL_IS_PORT_EXIST(i))
			printk("%d,",i);
	}
	printk(KERN_CONT "\n");
	printk(KERN_CONT "Dump port counter info             : echo r [port] > proc/rtl8686gmac/asic_counter\n");
	printk(KERN_CONT "Reset mib counter for single port  : echo clear [port] > proc/rtl8686gmac/asic_counter\n");
	printk(KERN_CONT "Reset mib counter for all ports    : echo clear all > proc/rtl8686gmac/asic_counter\n");
	return;
}

const char *counterStr[] ={
	MIB_COUNTERSTR_IF_IN_OCTETS,
    MIB_COUNTERSTR_IF_IN_UCAST_PKTS,
    MIB_COUNTERSTR_F_IN_MULTICAST_PKTS,
    MIB_COUNTERSTR_IF_IN_BROADCAST_PKTS,
    MIB_COUNTERSTR_IF_IN_DISCARDS,
    MIB_COUNTERSTR_IF_OUT_OCTETS,
    MIB_COUNTERSTR_IF_OUT_DISCARDS,
    MIB_COUNTERSTR_IF_OUT_UCAST_PKTS_CNT,
    MIB_COUNTERSTR_IF_OUT_MULTICAST_PKTS_CNT,
    MIB_COUNTERSTR_IF_OUT_BROADCAST_PKTS_CNT,
    MIB_COUNTERSTR_DOT1D_PORT_DELAY_EXCEEDED_DISCARDS,
    MIB_COUNTERSTR_DOT1D_TP_PORT_IN_DISCARDS,
    MIB_COUNTERSTR_DOT1D_TP_HC_PORT_IN_DISCARDS,
    MIB_COUNTERSTR_DOT3_IN_PAUSE_FRAMES,
    MIB_COUNTERSTR_DOT3_OUT_PAUSE_FRAMES,
    MIB_COUNTERSTR_DOT3_OUT_PAUSE_ON_FRAMES,
    MIB_COUNTERSTR_DOT3_STATS_ALIGNMENT_ERRORS,
    MIB_COUNTERSTR_DOT3_STATS_FCS_ERRORS,
    MIB_COUNTERSTR_DOT3_STATS_SINGLE_COLLISION_FRAMES,
    MIB_COUNTERSTR_DOT3_STATS_MULTIPLE_COLLISION_FRAMES,
    MIB_COUNTERSTR_DOT3_STATS_DEFERRED_TRANSMISSIONS,
    MIB_COUNTERSTR_DOT3_STATS_LATE_COLLISIONS,
    MIB_COUNTERSTR_DOT3_STATS_EXCESSIVE_COLLISIONS,
    MIB_COUNTERSTR_DOT3_STATS_FRAME_TOO_LONGS,
    MIB_COUNTERSTR_DOT3_STATS_SYMBOL_ERRORS,
    MIB_COUNTERSTR_DOT3_CONTROL_IN_UNKNOWN_OPCODES,
    MIB_COUNTERSTR_ETHER_STATS_DROP_EVENTS,
    MIB_COUNTERSTR_ETHER_STATS_OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_BROADCAST_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_MULTICAST_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_UNDER_SIZE_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_OVERSIZE_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_FRAGMENTS,
    MIB_COUNTERSTR_ETHER_STATS_JABBERS,
    MIB_COUNTERSTR_ETHER_STATS_COLLISIONS,
    MIB_COUNTERSTR_ETHER_STATS_CRC_ALIGN_ERRORS,
    MIB_COUNTERSTR_ETHER_STATS_PKTS_64OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_PKTS_65TO127OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_PKTS_128TO255OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_PKTS_256TO511OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_PKTS_512TO1023OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_PKTS_1024TO1518OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_TX_OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_TX_UNDER_SIZE_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_TX_OVERSIZE_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_64OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_65TO127OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_128TO255OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_256TO511OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_512TO1023OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_1024TO1518OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_1519TOMAXOCTETS,
    MIB_COUNTERSTR_ETHER_STATS_TX_BROADCAST_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_TX_MULTICAST_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_TX_FRAGMENTS,
    MIB_COUNTERSTR_ETHER_STATS_TX_JABBERS,
    MIB_COUNTERSTR_ETHER_STATS_TX_CRC_ALIGN_ERRORS,
    MIB_COUNTERSTR_ETHER_STATS_RX_UNDER_SIZE_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_RX_UNDER_SIZE_DROP_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_RX_OVERSIZE_PKTS,
    MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_64OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_65TO127OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_128TO255OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_256TO511OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_512TO1023OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_1024TO1518OCTETS,
    MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_1519TOMAXOCTETS,
    MIB_COUNTERSTR_IN_OAM_PDU_PKTS,
    MIB_COUNTERSTR_OUT_OAM_PDU_PKTS
};

static int asic_counter_dump_info(rtk_port_t port){
	uint64 cntr;
	rtk_stat_port_type_t type;

	if(!HAL_IS_PORT_EXIST(port))
		return 0;

	printk(KERN_CONT "Port: %d\n", port);
	for (type = 0; type <= MIB_PORT_CNTR_END; type++){
		if(rtk_stat_port_get(port, type, &cntr) == RT_ERR_OK)
			printk(KERN_CONT "%-35s: %25llu\n",counterStr[type],cntr);
	}
	return 1;
}

static int asic_counter_read(struct seq_file *m, void *v){
	rtk_port_t port;
	for(port=0;port<SW_PORT_NUM;port++){
		if(!HAL_IS_PORT_EXIST(port))
			continue;
		if(!asic_counter_dump_info(port)){
			printk(KERN_CONT "%s:%d dump asic_counter fail for port %d!\n",__FUNCTION__,__LINE__,port);
			return 0;
		}
	}
	return 0;
}

static int asic_counter_write(struct file *filp, char *buf, size_t count, loff_t *offp ){
	char tmpbuf[64];
	char *strptr, *tokptr;
	rtk_port_t port;
	int len = (count > 63) ? 63 : count;

	if (buf && !copy_from_user(tmpbuf, buf, len)) {
		tmpbuf[len] = '\0';
		strptr=tmpbuf;
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
			goto err;

		if(!memcmp(tokptr, "r", 1)){// read mib counter
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
				goto err;
			port = (rtk_port_t)simple_strtol(tokptr, NULL, 0);
			if(!HAL_IS_PORT_EXIST(port) || !asic_counter_dump_info(port)){
				printk(KERN_CONT "%s:%d error in dumping port info!\n",__FUNCTION__,__LINE__);
				goto err;
			}
		}
		else if(!memcmp(tokptr, "clear", 5)){
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
				goto err;
			if(!memcmp(tokptr, "all", 3)){// reset all ports
				for(port=0;port<SW_PORT_NUM;port++){
					if(!HAL_IS_PORT_EXIST(port))
						continue;
					if(rtk_stat_port_reset(port) != RT_ERR_OK){
						printk(KERN_CONT "%s:%d fail to reset mib counter for port %d!\n",__FUNCTION__,__LINE__,port);
						goto err;
					}
				}
			}
			else{// reset single port
				port = (rtk_port_t)simple_strtol(tokptr, NULL, 0);
				if(!HAL_IS_PORT_EXIST(port)){
					printk(KERN_CONT "%s:%d Invalid port %d!\n",__FUNCTION__,__LINE__,port);
					goto err;
				}
				if(rtk_stat_port_reset(port) != RT_ERR_OK){
					printk(KERN_CONT "%s:%d reset port %d error!\n",__FUNCTION__,__LINE__,port);
					goto err;
				}
				else{
					printk(KERN_CONT "%s:%d reset port %d successfully!\n",__FUNCTION__,__LINE__,port);
					asic_counter_dump_info(port);
				}
			}
		}
		else{
			goto err;
		}
	}
	else{
err:
		printk(KERN_CONT "%s:%d error!\n",__FUNCTION__,__LINE__);
		asic_counter_help();
	}

	return count;
}

const char *Str_portSpeed[] = {
    PORT_STR_SPEED_10M,
    PORT_STR_SPEED_100M,
    PORT_STR_SPEED_GIGA,
    PORT_STR_SPEED_500M,
    PORT_STR_SPEED_10G,
    PORT_STR_SPEED_2G5,
    PORT_STR_SPEED_5G,
    PORT_STR_SPEED_2G5LITE,
    PORT_STR_SPEED_5GLITE,
    
};
const char *Str_portDuplex[] = {
	PORT_STR_HALF_DUPLEX,
	PORT_STR_FULL_DUPLEX
};

const char *Str_portLinkStatus[] = {
	PORT_STR_LINK_DOWN,
	PORT_STR_LINK_UP
};

const char *Str_enDisplay[] = {
	PORT_STR_X,
	PORT_STR_V
};

static void port_status_help(void){
	int i;
	printk(KERN_CONT "port:");
	for(i=0;i<SW_PORT_NUM;i++){
		if(HAL_IS_PORT_EXIST(i))
			printk("%d,",i);
	}
	printk(KERN_CONT "\n");
	printk(KERN_CONT "Dump port status             : echo get [port] > proc/rtl8686gmac/port_status\n");
	return;
}

static int port_dump_info(rtk_port_t port){
	rtk_port_linkStatus_t linkStatus;
	rtk_port_speed_t speed;
	rtk_port_duplex_t duplex;
	uint32 txfc,rxfc;

	if(!HAL_IS_PORT_EXIST(port))
		return 0;

	if(rtk_port_link_get(port, &linkStatus)!= RT_ERR_OK){
		printk(KERN_CONT "%s:%d rt_port_link_get fail!\n",__FUNCTION__,__LINE__);
		return 0;
	}
	if(rtk_port_speedDuplex_get(port, &speed, &duplex)!=RT_ERR_OK){
		printk(KERN_CONT "%s:%d rt_port_speedDuplex_get fail!\n",__FUNCTION__,__LINE__);
		return 0;
	}
	if(rtk_port_flowctrl_get(port, &txfc, &rxfc)!=RT_ERR_OK){
		printk(KERN_CONT "%s:%d rt_port_flowctrl_get fail!\n",__FUNCTION__,__LINE__);
		return 0;
	}

	printk(KERN_CONT "Port:%d Status:%s Speed:%s Duplex:%s TX_FC:%s RX_FC:%s\n",port,Str_portLinkStatus[linkStatus],
																			Str_portSpeed[speed],
																			Str_portDuplex[duplex],
																			Str_enDisplay[txfc],
																			Str_enDisplay[rxfc]);
	return 1;
}

static int port_status_write(struct file *filp, char *buf, size_t count, loff_t *offp ){
	char tmpbuf[64];
	char *strptr,*tokptr;
	rtk_port_t port;
	int len = (count > 63) ? 63 : count;

	if (buf && !copy_from_user(tmpbuf, buf, len)) {
		tmpbuf[len] = '\0';
		strptr=tmpbuf;
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
			goto err;
		if(!memcmp(tokptr, "get", 3)){
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
				goto err;
			port = (rtk_port_t)simple_strtol(tokptr, NULL, 0);
			if(!HAL_IS_PORT_EXIST(port)){
				printk(KERN_CONT "%s:%d Invalid port %d!\n",__FUNCTION__,__LINE__,port);
				goto err;
			}
			if(!port_dump_info(port)){
				printk(KERN_CONT "%s:%d fail to dump port %d status!\n",__FUNCTION__,__LINE__,port);
				goto err;
			}
		}
		else
			goto err;
	}
	else{
err:
		printk(KERN_CONT "%s:%d error!\n",__FUNCTION__,__LINE__);
		port_status_help();
	}

	return count;

}
static int port_status_read(struct seq_file *m, void *v){
	rtk_port_t port;
	for(port=0;port<SW_PORT_NUM;port++){
		if(!HAL_IS_PORT_EXIST(port))
			continue;
		if(!port_dump_info(port)){
			printk(KERN_CONT "%s:%d dump port status fail for port %d!\n",__FUNCTION__,__LINE__,port);
			return 0;
		}
	}
	return 0;
}

#endif

#ifdef TX_KICK_RING_USING_POLLING
static int tx_tdu_poll_seq_read(struct seq_file *m, void *v)
{
	static struct re_private *data;
	struct file *file = m->private;
	int len = 0, i;

	data=pde_data(file_inode(file));
	if(!(data)){
		printk(KERN_INFO "Null data");
			return 0;
		}

	seq_printf(m, "tx_ring_active_poll_enabled=%s\n", data->tx_ring_active_poll_enabled?"enabled":"disabled");
	seq_printf(m, "detect ring active once per %d sec, packet cnt=%d\n", tx_ring_active_poll_sec, tx_ring_active_poll_packet_num);
	seq_printf(m, "detect TDU status once per %d 10msec\n", tx_tdu_kick_ring_10msec);
	for(i=0 ; i<MAX_TXRING_NUM ; i++)
	{
		seq_printf(m, "tx_tdu_poll_status[%d]=%s\n", i, data->tx_tdu_poll_status[i]?"ON":"OFF");
	}
	for(i=0 ; i<MAX_TXRING_NUM ; i++)
	{
		seq_printf(m, "poll_kick_ring[%d]:%d/%d\n", i, data->cp_stats.tdu_kick_ring_poll[i], data->cp_stats.tdu_poll[i]);
	}

	return len;
	}

static ssize_t tx_tdu_poll_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp )
{
	char tmpbuf[512];
	char *strptr;
	int i;
	//int retval = -1, i;
	static struct re_private *data;
	data=pde_data(file_inode(filp));
	if(!(data)){
		printk(KERN_INFO "Null data");
	return 0;
}

	if (buf && !copy_from_user(tmpbuf, buf, count))
	{
		tmpbuf[count] = '\0';
		strptr=tmpbuf;
		if( strlen(strptr)==0 )
		{
			goto errout;
		}
		if(strncmp(strptr, "enable", 6) == 0)
		{
			data->tx_ring_active_poll_enabled = 1;
			if(timer_pending(&data->tx_ring_active_polling_timer))
				timer_delete(&data->tx_ring_active_polling_timer);
			mod_timer(&data->tx_ring_active_polling_timer, jiffies+(tx_ring_active_poll_sec*
HZ));
			printk(KERN_CONT "Tx ring active poll mechanism enabled !\n");
		}
		else if(strncmp(strptr, "disable", 6) == 0)
		{
			data->tx_ring_active_poll_enabled = 0;
			if(timer_pending(&data->tx_ring_active_polling_timer))
				timer_delete(&data->tx_ring_active_polling_timer);
			if(timer_pending(&data->tx_tdu_kick_polling_timer))
				timer_delete(&data->tx_tdu_kick_polling_timer);
			printk(KERN_CONT "Tx ring active poll mechanism disabled !\n");
		}
		else
		{
			goto errout;
		}
	}
	else
	{
errout:
		printk(KERN_CONT "error input: \n");
		printk(KERN_CONT "enable/disable\n");
	}
	return count;
}
#endif

static int dbg_level_open(struct inode *inode, struct file *file)
{
        return single_open(file, NULL, inode->i_private);
}

static const struct proc_ops dbglv_fops = {
        .proc_open           = dbg_level_open,
        .proc_read           = dbg_level_read,
        .proc_write          = dbg_level_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};

static int dbg_times_open(struct inode *inode, struct file *file)
{
        return single_open(file, NULL, inode->i_private);
}

static const struct proc_ops dbgtimes_fops = {
        .proc_open           = dbg_times_open,
        .proc_read           = dbg_times_read,
        .proc_write          = dbg_times_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};

static int hwreg_open(struct inode *inode, struct file *file)
{
        return single_open(file, NULL, inode->i_private);
}
static const struct proc_ops hwreg_fops = {
        .proc_open           = hwreg_open,
        .proc_read           = hwreg_read,
        .proc_write          = hwreg_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};

static int sw_cnt_open(struct inode *inode, struct file *file)
{
        return single_open(file, sw_cnt_seq_read, (void *) file);
}

static const struct proc_ops sw_cnt_fops = {
        .proc_open           = sw_cnt_open,
        .proc_read           = seq_read,
        .proc_write          = sw_cnt_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};
#if defined(CONFIG_RTL9607C_SERIES) && defined(HWNAT_CUSTOMIZE)
static int dynamic_sram_desc_open(struct inode *inode, struct file *file)
{
		return single_open(file, NULL, inode->i_private);
}
static const struct proc_ops dynamic_sram_desc_ops = {
		.proc_open			= dynamic_sram_desc_open,
		.proc_read			= dynamic_sram_desc_read,
		.proc_write			 = dynamic_sram_desc_write,
		.proc_lseek 		= seq_lseek,
		.proc_release		= single_release,
};
static int hwnat_customized_version_open(struct inode *inode, struct file *file)
{
		return single_open(file, NULL, inode->i_private);
}
static const struct proc_ops hwnat_customized_version_ops = {
		.proc_open			= hwnat_customized_version_open,
		.proc_read			= hwnat_customized_version_read,
		.proc_write			= hwnat_customized_version_write,
		.proc_lseek 		= seq_lseek,
		.proc_release		= single_release,
};
static int hwnat_customized_check_tx_done_open(struct inode *inode, struct file *file)
{
		return single_open(file, NULL, inode->i_private);
}
static const struct proc_ops hwnat_customized_check_tx_done_ops = {
		.proc_open			= hwnat_customized_check_tx_done_open,
		.proc_read			= hwnat_customized_check_tx_done_read,
		.proc_write			= hwnat_customized_check_tx_done_write,
		.proc_lseek 		= seq_lseek,
		.proc_release		= single_release,
};

#endif

static int rxring_open(struct inode *inode, struct file *file)
{
		return single_open(file, rx_ring_read, (void *) file);
}
static const struct proc_ops rxring_fops = {
        .proc_open           = rxring_open,
        .proc_read           = seq_read,
        .proc_lseek          = seq_lseek,
		.proc_write          = rx_ring_write,
        .proc_release        = single_release,
};

static int txring_open(struct inode *inode, struct file *file)
{
		return single_open(file, tx_ring_read, (void *) file);
}
static const struct proc_ops txring_fops = {
        .proc_open           = txring_open,
        .proc_read           = seq_read,
        .proc_lseek          = seq_lseek,
		.proc_write          = tx_ring_write,
        .proc_release        = single_release,
};

static int memrw_open(struct inode *inode, struct file *file)
{
        return single_open(file, NULL, inode->i_private);
}
static const struct proc_ops mem_fops = {
        .proc_open           = memrw_open,
        .proc_read           = memrw_read,
        .proc_write          = memrw_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};

int dev_port_mapping_open(struct inode *inode, struct file *file)
{
	return single_open(file, dev_port_mapping_read, (void *) file);
}

static const struct proc_ops devport_fops = {
	.proc_open		= dev_port_mapping_open,
	.proc_read		= seq_read,
	.proc_write		= dev_port_mapping_write,
	.proc_lseek     = seq_lseek,
	.proc_release	= single_release,
};

static int misc_open(struct inode *inode, struct file *file)
{
        return single_open(file, NULL, inode->i_private);
}
static const struct proc_ops misc_fops = {
        .proc_open           = misc_open,
        .proc_read           = misc_read,
        .proc_write          = misc_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};

static int port_to_rxfunc_open(struct inode *inode, struct file *file)
{
        return single_open(file, NULL, inode->i_private);
}
static const struct proc_ops port2rx_fops = {
        .proc_open           = port_to_rxfunc_open,
        .proc_read           = port_to_rxfunc_read,
        .proc_write          = port_to_rxfunc_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};

#ifdef CONFIG_RTL8686_SWITCH
#ifndef CONFIG_RTK_L34_ENABLE
static int swmode_open(struct inode *inode, struct file *file)
{
        return single_open(file, switch_mode_read, inode->i_private);
}
static const struct proc_ops swmode_fops = {
        .proc_open           = swmode_open,
        .proc_read           = seq_read,
        .proc_write          = switch_mode_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};
#endif // CONFIG_RTK_L34_ENABLE
#endif // CONFIG_RTL8686_SWITCH

#ifdef RE8686_VERIFY
static int verify_open(struct inode *inode, struct file *file)
{
        return single_open(file, verify_read, inode->i_private);
}
static const struct proc_ops verify_ops = {
        .proc_open           = verify_open,
        .proc_read           = seq_read,
        .proc_write          = verify_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};
#endif //RE8686_VERIFY

static int txdetail_open(struct inode *inode, struct file *file)
{
		return single_open(file, nic_tx_ring_dump_read, (void *) file);
}
static const struct proc_ops txdetail_fops = {
        .proc_open           = txdetail_open,
        .proc_read           = seq_read,
        .proc_write          = nic_tx_ring_dump_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};

static int rxdetail_open(struct inode *inode, struct file *file)
{
		return single_open(file, nic_rx_ring_dump_read, (void *) file);
}
static const struct proc_ops rxdetail_fops = {
        .proc_open           = rxdetail_open,
        .proc_read           = seq_read,
        .proc_write          = nic_rx_ring_dump_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};

static int padding_open(struct inode *inode, struct file *file)
{
        return single_open(file, NULL, inode->i_private);
}
static const struct proc_ops padding_fops = {
        .proc_open           = padding_open,
        .proc_read           = padding_enable_read,
        .proc_write          = padding_enable_write,
        .proc_lseek          = seq_lseek,
        .proc_release        = single_release,
};

#ifdef CONFIG_RTL8192CD
int wifi_tx_qos_mapping_open(struct inode *inode, struct file *file)
{
	return single_open(file, wifi_tx_qos_mapping_read, (void *) file);
}

static const struct proc_ops wifitxqos_ops = {
	.proc_open		= wifi_tx_qos_mapping_open,
	.proc_read		= seq_read,
	.proc_write		= wifi_tx_qos_mapping_write,
	.proc_lseek     = seq_lseek,
	.proc_release	= single_release,
};
#endif

int skb_dynamic_allocate_disable_open(struct inode *inode, struct file *file)
{
	return single_open(file, skb_dynamic_allocate_disable_read, (void *) file);
}

static const struct proc_ops skb_dynamic_allocate_disable_fops = {
	.proc_open		= skb_dynamic_allocate_disable_open,
	.proc_read		= seq_read,
	.proc_write		= skb_dynamic_allocate_disable_write,
	.proc_lseek     = seq_lseek,
	.proc_release	= single_release,
};

#if defined(CONFIG_RTK_SOC_RTL8198D)
int qos_cfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, qos_cfg_read, (void *) file);
}

static const struct proc_ops qos_fops = {
	.proc_open		= qos_cfg_open,
	.proc_read		= seq_read,
	.proc_write		= qos_cfg_write,
	.proc_lseek     = seq_lseek,
	.proc_release	= single_release,
};

int asic_counter_open(struct inode *inode, struct file *file)
{
	return single_open(file, asic_counter_read, (void *) file);
}

static const struct proc_ops asicounter_fops = {
	.proc_open		= asic_counter_open,
	.proc_read		= seq_read,
	.proc_write		= asic_counter_write,
	.proc_lseek 	= seq_lseek,
	.proc_release	= single_release,
};

int port_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, port_status_read, (void *) file);
}

static const struct proc_ops portstatus_fops = {
	.proc_open		= port_status_open,
	.proc_read		= seq_read,
	.proc_write		= port_status_write,
	.proc_lseek     = seq_lseek,
	.proc_release	= single_release,
};
#endif

#ifdef TX_KICK_RING_USING_POLLING
static int tx_tdu_poll_open(struct inode *inode, struct file *file)
{
		return single_open(file, tx_tdu_poll_seq_read, (void *) file);
}
static const struct proc_ops tx_tdu_poll_fops = {
        .proc_open           = tx_tdu_poll_open,
        .proc_read           = seq_read,
        .proc_lseek          = seq_lseek,
		.proc_write          = tx_tdu_poll_write,
        .proc_release        = single_release,
};
#endif

#define RTL8686_GMAC0_PROC_DIR_NAME "rtl8686gmac"
#define RTL8686_GMAC1_PROC_DIR_NAME "rtl8686gmac1"
#define RTL8686_GMAC2_PROC_DIR_NAME "rtl8686gmac2"
unsigned char *rtl8686_proc_dir_name[MAX_GMAC_NUM] =
{
	RTL8686_GMAC0_PROC_DIR_NAME,
	RTL8686_GMAC1_PROC_DIR_NAME,
	RTL8686_GMAC2_PROC_DIR_NAME
};
struct proc_dir_entry *rtl8686_proc_dir[MAX_GMAC_NUM]={0};

struct proc_dir_entry *rtl8686_proc_dir_root_symlink=NULL;
#define RTL8686_GMAC0_PROC_DIR_NAME_SYMLINK "eth_nic"
#define RTL8686_GMAC1_PROC_DIR_NAME_SYMLINK "eth_nic1"
#define RTL8686_GMAC2_PROC_DIR_NAME_SYMLINK "eth_nic2"
unsigned char *rtl8686_proc_dir_name_symlink[MAX_GMAC_NUM] =
{
	RTL8686_GMAC0_PROC_DIR_NAME_SYMLINK,
	RTL8686_GMAC1_PROC_DIR_NAME_SYMLINK,
	RTL8686_GMAC2_PROC_DIR_NAME_SYMLINK
};
struct proc_dir_entry *rtl8686_proc_dir_symlink[MAX_GMAC_NUM]={0};

#define MAX_RTK_NI_PROC_NUM		50
	
struct rtk_ni_proc_entry {
	char *proc_name;
	struct proc_ops *op;
	char isRoot;
};
	
static struct rtk_ni_proc_entry rtk_ni_proc_table[MAX_RTK_NI_PROC_NUM] = {
	{"dbg_level", &dbglv_fops, 0},
	{"dbg_times", &dbgtimes_fops, 0},
	{"hw_reg", &hwreg_fops, 0},
	{"sw_cnt", &sw_cnt_fops, 0},
	{"rx_ring", &rxring_fops, 0},
	{"tx_ring", &txring_fops, 0},
	{"mem", &mem_fops, 0},
	{"dev_port_mapping", &devport_fops, 0},
	{"misc", &misc_fops, 0},
	{"port2rxfunc", &port2rx_fops, 0},
		#ifdef CONFIG_RTL8686_SWITCH
		#ifndef CONFIG_RTK_L34_ENABLE
	{"switch_mode", &swmode_fops, 0},
		#endif
		#endif
		#ifdef RE8686_VERIFY
	{"verify", &verify_fops, 0},
#endif
	{"rx_ring_detail", &rxdetail_fops, 0},
	{"tx_ring_detail", &txdetail_fops, 0},
	{"padding_enable", &padding_fops, 0},
#ifdef CONFIG_RTL8192CD
	{"wifi_tx_qos_mapping", &wifitxqos_ops, 0},
#endif
#if defined(CONFIG_RTL9607C_SERIES) && defined(HWNAT_CUSTOMIZE)
	{"dynamic_sram_desc", &dynamic_sram_desc_ops, 0},
	{"hwnat_customized_version", &hwnat_customized_version_ops, 0},
	{"hwnat_customized_check_tx_done", &hwnat_customized_check_tx_done_ops, 0},
#endif
#if defined(CONFIG_RTK_SOC_RTL8198D)
	{"qos_cfg", &qos_fops, 0},
	{"asic_counter", &asicounter_fops, 0},
	{"port_status", &portstatus_fops, 0},
#endif
	{"skb_dynamic_allocate_disable", &skb_dynamic_allocate_disable_fops, 1},
#ifdef TX_KICK_RING_USING_POLLING
	{"tx_tdu_poll", &tx_tdu_poll_fops, 0},
#endif
	{NULL, NULL}
};

static void rtl8686_proc_debug_init(unsigned int gmac)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	char org_proc_name[128];
	int i;
	
	if(cp->gmac_enabled != GMAC_TRUE)
		return;
	
	if(rtl8686_proc_dir[gmac]==NULL)
		rtl8686_proc_dir[gmac] = proc_mkdir(rtl8686_proc_dir_name[gmac], NULL);
	
	if(rtl8686_proc_dir_symlink[gmac]==NULL)
		rtl8686_proc_dir_symlink[gmac] = proc_mkdir(rtl8686_proc_dir_name_symlink[gmac], NULL);

	for (i=0 ; i<MAX_RTK_NI_PROC_NUM ; i++)
	{
		if (rtk_ni_proc_table[i].proc_name == NULL)
			break;

		if (rtk_ni_proc_table[i].isRoot)
			proc_create_data(rtk_ni_proc_table[i].proc_name, 0, rtl8686_proc_dir[gmac], rtk_ni_proc_table[i].op, root_cp);
		else
			proc_create_data(rtk_ni_proc_table[i].proc_name, 0, rtl8686_proc_dir[gmac], rtk_ni_proc_table[i].op, cp);

		snprintf(org_proc_name, sizeof(org_proc_name)-1, "/proc/%s/%s", rtl8686_proc_dir_name[gmac], rtk_ni_proc_table[i].proc_name);

		if(!proc_symlink(rtk_ni_proc_table[i].proc_name, rtl8686_proc_dir_symlink[gmac], org_proc_name))
			return;
	}
}

void port_relate_setting(unsigned int gmac) 
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	unsigned int totalDev = TOTAL_RTL8686_DEV_NUM;
	int i, j;

	re8686_reset_rxfunc_to_default(gmac);
	// port2dev: default set to eth0, eth1 and eth3 respectly
	for(j=0;j<SW_PORT_NUM;j++) {
		cp->port2dev[j] = eth_net_dev[gmac];
	}
	
	for(i=0;i<totalDev;i++) {
		if(rtl8686_dev_table[i].dev_instant==NULL)
			continue;
		DEVPRIV(rtl8686_dev_table[i].dev_instant)->txPortMask = 
			IS_CPU_PORT(rtl8686_dev_table[i].phyPort) ? 0 : (1<<rtl8686_dev_table[i].phyPort);
	}
}

#if defined(CONFIG_APOLLO_ROMEDRIVER) //&& defined(CONFIG_GPON_FEATURE)
extern int rtk_rg_fwdEngine_xmit (struct sk_buff *skb, struct net_device *dev);
#endif

#if defined(CONFIG_APOLLO_ROMEDRIVER)
__IRAM_NIC int re8670_start_xmit_check(struct sk_buff *skb, struct net_device *dev)
{
	/* direct tx */
	if (memcmp(skb->dev->name, "wlan1", 5) == 0 || memcmp(skb->dev->name, "vwlan", 5) == 0) {
		return re8670_start_xmit_txInfo(skb, dev, NULL, NULL);
	} else { /* send to fwdEngine */
		return rtk_rg_fwdEngine_xmit(skb, dev);
	}
}
#elif defined(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE) && IS_BUILTIN(CONFIG_RTK_L34_FC_KERNEL_MODULE)
int re8670_start_xmit_fc(struct sk_buff *skb, struct net_device *dev)
{
	 return rtk_fc_skb_tx(skb, dev);
}
#endif


struct net_device_ops rtl_netdevops = {
    .ndo_open       = re8670_open,
    .ndo_stop       = re8670_close,
#if defined(CONFIG_APOLLO_ROMEDRIVER)
    .ndo_start_xmit = re8670_start_xmit_check,
#elif defined(CONFIG_RTK_L34_FLEETCONNTRACK_ENABLE) && IS_BUILTIN(CONFIG_RTK_L34_FC_KERNEL_MODULE)
	.ndo_start_xmit = re8670_start_xmit_fc,
#else
	.ndo_start_xmit = re8670_start_xmit,
#endif
    .ndo_do_ioctl   = re8670_ioctl,
    .ndo_tx_timeout = re8670_tx_timeout,
	.ndo_set_rx_mode = re8670_set_rx_mode,
	.ndo_set_mac_address = re8670_set_mac_addr,
	.ndo_set_features = re8670_set_features,
	.ndo_get_stats64 = re8670_get_stats,
#ifdef CONFIG_REALTEK_HW_LSO
	.ndo_change_mtu = re8670_set_mtu,
#endif
};

//int __init re8670_probe (void)
//{
//	printk(KERN_CONT "%s: do nothing here !!\n", __func__);
//}

//void __exit re8670_exit (void)
//{
//	struct re_private_root *root_cp = &re_private_data_root;
//	struct re_private *cp;
//	unsigned int totalDev = TOTAL_RTL8686_DEV_NUM;
//    extern int drv_nic_rxhook_exit(void);
//	unsigned int gmac;
//	unsigned i;
//
//	//desc and hw setting & proc		
//	if(dev_num){			
//		for(gmac=0;gmac<MAX_GMAC_NUM;gmac++) {
//
//			cp = root_cp->re_private_data_ptr[gmac];
//			
//			if(cp->gmac_enabled != GMAC_TRUE)
//				continue;
//		
//			re8670_close(eth_net_dev[gmac]);			
//		}			
//	}
//	
//	for(gmac=0;gmac<MAX_GMAC_NUM;gmac++) {
//
//		cp = root_cp->re_private_data_ptr[gmac];
//		
//		if(cp->gmac_enabled != GMAC_TRUE)
//			continue;
//		
//		re8670_free_rings(cp);
//		proc_remove(rtl8686_proc_dir[gmac]);
//	}
//	
//	//dev
//	for(i=0; i < totalDev; i++){		
//		if(rtl8686_dev_table[i].dev_instant){
//			unregister_netdev(rtl8686_dev_table[i].dev_instant);
//			free_netdev(rtl8686_dev_table[i].dev_instant);
//		}
//	}
//#ifdef CONFIG_RTL8686_SWITCH
//	if(timer_pending(&re_private_data_root.rx_pause_by_software_interrupt_timer))
//		timer_delete(&re_private_data_root.rx_pause_by_software_interrupt_timer);
//	
//	//for gpon driver
//    drv_nic_rxhook_exit();
//#endif
//}


#if 1 // CONFIG_OF
static const struct of_device_id of_platform_rtk_gmac_table[] = {
	{ .compatible = "rtk,gmac" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, of_platform_rtk_gmac_table);

static int rtk_gmac_re_private_data_init(void)
{
	struct re_private_root *root_cp = &re_private_data_root;
	unsigned int gmac, rx_ring_idx, i;

	
	root_cp->txfunc = NULL;
	///--init re_private_data_root
	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		root_cp->re_private_data_ptr[gmac] = &re_private_data[gmac];
#ifdef CONFIG_RTL8686_SWITCH
		root_cp->rx_pause_by_software_bitmap = 0;
		root_cp->rx_pause_by_software_enable = GMAC_FALSE;
#endif
	}

	///--init re_private_data
	root_cp->re_private_data_ptr[0]->gmac_enabled = GMAC_TRUE;
#ifdef CONFIG_GMAC1_USABLE	
	root_cp->re_private_data_ptr[1]->gmac_enabled = GMAC_TRUE; 
#else
	root_cp->re_private_data_ptr[1]->gmac_enabled = GMAC_FALSE; 
#endif 
#ifdef CONFIG_GMAC2_USABLE
	root_cp->re_private_data_ptr[2]->gmac_enabled = GMAC_TRUE; 
#else
	root_cp->re_private_data_ptr[2]->gmac_enabled = GMAC_FALSE;
#endif

#ifdef TX_RING_DEBUG
	root_cp->re_private_data_ptr[0]->tx_ring_backup_debug = GMAC_TRUE;
	root_cp->re_private_data_ptr[1]->tx_ring_backup_debug = GMAC_FALSE;
	root_cp->re_private_data_ptr[2]->tx_ring_backup_debug = GMAC_FALSE;
#endif

#ifdef TX_KICK_RING_USING_POLLING
	root_cp->re_private_data_ptr[0]->tx_ring_backup_debug = GMAC_FALSE;
	root_cp->re_private_data_ptr[1]->tx_ring_backup_debug = GMAC_FALSE;
	root_cp->re_private_data_ptr[2]->tx_ring_backup_debug = GMAC_FALSE;
#endif

#ifdef RX_NAPI_MODE
		
	if(dynamic_sram_desc!=0)	
	{	
		root_cp->re_private_data_ptr[0]->napi_budget = 8192;	
		root_cp->re_private_data_ptr[1]->napi_budget = 8192;	
		root_cp->re_private_data_ptr[2]->napi_budget = 8192;	
	}	
	else	
	{	
		root_cp->re_private_data_ptr[0]->napi_budget = GMAC0_RX_NAPI_BUDGET;	
		root_cp->re_private_data_ptr[1]->napi_budget = GMAC1_RX_NAPI_BUDGET;	
		root_cp->re_private_data_ptr[2]->napi_budget = GMAC2_RX_NAPI_BUDGET;	
	}
#endif

	root_cp->re_private_data_ptr[0]->gmac_cpu_port = CPU_PORT0;
#ifndef CONFIG_RTL9603CVD_SERIES
	root_cp->re_private_data_ptr[1]->gmac_cpu_port = CPU_PORT1;
	root_cp->re_private_data_ptr[2]->gmac_cpu_port = CPU_PORT2;
#endif
	root_cp->re_private_data_ptr[0]->re8670_rx_ring_size[0] = GMAC0_RX1_SIZE;
#ifndef CONFIG_RTK_SINGLE_RX_RING
	root_cp->re_private_data_ptr[0]->re8670_rx_ring_size[1] = GMAC0_RX2_SIZE;
	root_cp->re_private_data_ptr[0]->re8670_rx_ring_size[2] = GMAC0_RX3_SIZE;
	root_cp->re_private_data_ptr[0]->re8670_rx_ring_size[3] = GMAC0_RX4_SIZE;
	root_cp->re_private_data_ptr[0]->re8670_rx_ring_size[4] = GMAC0_RX5_SIZE;
	root_cp->re_private_data_ptr[0]->re8670_rx_ring_size[5] = GMAC0_RX6_SIZE;
#endif
	root_cp->re_private_data_ptr[0]->re8670_tx_ring_size[0] = GMAC0_TX1_SIZE;
	root_cp->re_private_data_ptr[0]->re8670_tx_ring_size[1] = GMAC0_TX2_SIZE;
	root_cp->re_private_data_ptr[0]->re8670_tx_ring_size[2] = GMAC0_TX3_SIZE;
	root_cp->re_private_data_ptr[0]->re8670_tx_ring_size[3] = GMAC0_TX4_SIZE;
	root_cp->re_private_data_ptr[0]->re8670_tx_ring_size[4] = GMAC0_TX5_SIZE;

	root_cp->re_private_data_ptr[1]->re8670_rx_ring_size[0] = GMAC1_RX1_SIZE;
#ifndef CONFIG_RTK_SINGLE_RX_RING
	root_cp->re_private_data_ptr[1]->re8670_rx_ring_size[1] = GMAC1_RX2_SIZE;
	root_cp->re_private_data_ptr[1]->re8670_rx_ring_size[2] = GMAC1_RX3_SIZE;
	root_cp->re_private_data_ptr[1]->re8670_rx_ring_size[3] = GMAC1_RX4_SIZE;
	root_cp->re_private_data_ptr[1]->re8670_rx_ring_size[4] = GMAC1_RX5_SIZE;
	root_cp->re_private_data_ptr[1]->re8670_rx_ring_size[5] = GMAC1_RX6_SIZE;
#endif
	root_cp->re_private_data_ptr[1]->re8670_tx_ring_size[0] = GMAC1_TX1_SIZE;
	root_cp->re_private_data_ptr[1]->re8670_tx_ring_size[1] = GMAC1_TX2_SIZE;
	root_cp->re_private_data_ptr[1]->re8670_tx_ring_size[2] = GMAC1_TX3_SIZE;
	root_cp->re_private_data_ptr[1]->re8670_tx_ring_size[3] = GMAC1_TX4_SIZE;
	root_cp->re_private_data_ptr[1]->re8670_tx_ring_size[4] = GMAC1_TX5_SIZE;

	root_cp->re_private_data_ptr[2]->re8670_rx_ring_size[0] = GMAC2_RX1_SIZE;
#ifndef CONFIG_RTK_SINGLE_RX_RING
	root_cp->re_private_data_ptr[2]->re8670_rx_ring_size[1] = GMAC2_RX2_SIZE;
	root_cp->re_private_data_ptr[2]->re8670_rx_ring_size[2] = GMAC2_RX3_SIZE;
	root_cp->re_private_data_ptr[2]->re8670_rx_ring_size[3] = GMAC2_RX4_SIZE;
	root_cp->re_private_data_ptr[2]->re8670_rx_ring_size[4] = GMAC2_RX5_SIZE;
	root_cp->re_private_data_ptr[2]->re8670_rx_ring_size[5] = GMAC2_RX6_SIZE;
#endif
	root_cp->re_private_data_ptr[2]->re8670_tx_ring_size[0] = GMAC2_TX1_SIZE;
	root_cp->re_private_data_ptr[2]->re8670_tx_ring_size[1] = GMAC2_TX2_SIZE;
	root_cp->re_private_data_ptr[2]->re8670_tx_ring_size[2] = GMAC2_TX3_SIZE;
	root_cp->re_private_data_ptr[2]->re8670_tx_ring_size[3] = GMAC2_TX4_SIZE;
	root_cp->re_private_data_ptr[2]->re8670_tx_ring_size[4] = GMAC2_TX5_SIZE;

	root_cp->re_private_data_ptr[0]->rx_multiring_bitmap = GMAC0_RX_MULTIRING_BITMAP;
	root_cp->re_private_data_ptr[1]->rx_multiring_bitmap = GMAC1_RX_MULTIRING_BITMAP;
	root_cp->re_private_data_ptr[2]->rx_multiring_bitmap = GMAC2_RX_MULTIRING_BITMAP;

	root_cp->re_private_data_ptr[0]->tx_multiring_bitmap = GMAC0_TX_MULTIRING_BITMAP;
	root_cp->re_private_data_ptr[1]->tx_multiring_bitmap = GMAC1_TX_MULTIRING_BITMAP;
	root_cp->re_private_data_ptr[2]->tx_multiring_bitmap = GMAC2_TX_MULTIRING_BITMAP;

	root_cp->re_private_data_ptr[0]->rx_only_ring1 = GMAC0_RX_ONLY_RING1;
	root_cp->re_private_data_ptr[1]->rx_only_ring1 = GMAC1_RX_ONLY_RING1;
	root_cp->re_private_data_ptr[2]->rx_only_ring1 = GMAC2_RX_ONLY_RING1;

	root_cp->re_private_data_ptr[0]->rx_not_only_ring1 = GMAC0_RX_NOT_ONLY_RING1;
	root_cp->re_private_data_ptr[1]->rx_not_only_ring1 = GMAC1_RX_NOT_ONLY_RING1;
	root_cp->re_private_data_ptr[2]->rx_not_only_ring1 = GMAC2_RX_NOT_ONLY_RING1;
	
	root_cp->re_private_data_ptr[0]->iocmd_reg = CMD_CONFIG;
	root_cp->re_private_data_ptr[1]->iocmd_reg = CMD_CONFIG;
	root_cp->re_private_data_ptr[2]->iocmd_reg = CMD_CONFIG;

	root_cp->re_private_data_ptr[0]->iocmd1_reg = (CMD1_CONFIG | (GMAC0_RX_NOT_ONLY_RING1<<25) | GMAC0_RX_MULTIRING_BITMAP << 16 | txq1_h);
	root_cp->re_private_data_ptr[1]->iocmd1_reg = (CMD1_CONFIG | (GMAC1_RX_NOT_ONLY_RING1<<25) | GMAC1_RX_MULTIRING_BITMAP << 16 | txq1_h);
	root_cp->re_private_data_ptr[2]->iocmd1_reg = (CMD1_CONFIG | (GMAC2_RX_NOT_ONLY_RING1<<25) | GMAC2_RX_MULTIRING_BITMAP << 16 | txq1_h);

	spin_lock_init (&(root_cp->stats_lock));

	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		for(rx_ring_idx=0 ; rx_ring_idx<MAX_RXRING_NUM ; rx_ring_idx++)
		{
			root_cp->re_private_data_ptr[gmac]->re8670_rx_flow_control_status[rx_ring_idx] = GMAC_ON;
		}
		root_cp->re_private_data_ptr[gmac]->rx_ring_show_bitmap = 0x1;
		root_cp->re_private_data_ptr[gmac]->tx_ring_show_bitmap = 0x1;
#ifdef HW_LSO_ENABLE
		root_cp->re_private_data_ptr[gmac]->tx_jumbo_frame_enabled = GMAC_OFF;
#else
		root_cp->re_private_data_ptr[gmac]->tx_jumbo_frame_enabled = GMAC_ON;
#endif
		root_cp->re_private_data_ptr[gmac]->debug_enable = 0;
		root_cp->re_private_data_ptr[gmac]->debug_times = 0;
		root_cp->re_private_data_ptr[gmac]->padding_enable = 0;
		root_cp->re_private_data_ptr[gmac]->pauseBySwRingBitmap = 0;
		root_cp->re_private_data_ptr[gmac]->tx_int_mitigation = 0xd;
		root_cp->re_private_data_ptr[gmac]->tx_int_mitigation_timer = 1;
	}

	root_cp->re_private_data_ptr[0]->rx_buff_size = SKB_BUF_SIZE;
	root_cp->re_private_data_ptr[1]->rx_buff_size = SKB_BUF_SIZE;
	root_cp->re_private_data_ptr[2]->rx_buff_size = SKB_BUF_SIZE;
	
	root_cp->skb_dynamic_allocate_disable = (u8)GMAC_ON;

	for (i=0U ; i<SW_PORT_NUM ; i++)
	{
		LCDev_mapping[i].status=0xff;
	}
	
	return 0;
}

static int rtk_gmac_customize_re_private_data_init(unsigned int gmac, struct device_node *child)
{
	struct re_private_root *root_cp = &re_private_data_root;
	unsigned int rx_ring_idx;
	u32 *rx_ring_size;
	u32 *rx_buff_size;

	rx_ring_size = of_get_property(child, "rx-ring-size", NULL);
	if (rx_ring_size)
	{
		for(rx_ring_idx=0 ; rx_ring_idx<MAX_RXRING_NUM && rx_ring_size[rx_ring_idx] ; rx_ring_idx++)
		{
			if (!IS_LEGAL_RX_RING_SIZE(rx_ring_size[rx_ring_idx])) {
				printk(KERN_CONT "%s-%d: illegal customize Rx[%d] ring size=%d -> (keep Rx ring size=%d)\n", __func__, __LINE__, rx_ring_idx, rx_ring_size[rx_ring_idx], root_cp->re_private_data_ptr[gmac]->re8670_rx_ring_size[rx_ring_idx]);
				WARN_ON(1);
				continue;
			}
			root_cp->re_private_data_ptr[gmac]->re8670_rx_ring_size[rx_ring_idx] = rx_ring_size[rx_ring_idx];
		}
	}

	rx_buff_size = of_get_property(child, "rx-buff-size", NULL);
	if (rx_buff_size)
	{
		root_cp->re_private_data_ptr[gmac]->rx_buff_size = rx_buff_size[0];
	}

	return 0;
}

static int rtk_gmac_register_root_netdev(unsigned int base, int irq)
{
	struct net_device *dev_temp;
	int i, j, rc;
	
	//printk("\n%s %d: allocate new netdev name=%s\n", __func__, __LINE__, rtl8686_dev_table[0].ifname);
	dev_temp = alloc_etherdev(sizeof(struct re_dev_private));
	if (!dev_temp) {
		printk(KERN_CONT "%s %d: alloc_etherdev fail!\n", __func__, __LINE__);
		return NULL;
	}

	sprintf(dev_temp->name, rtl8686_dev_table[0].ifname);
	dev_temp->netdev_ops = &rtl_netdevops;
	dev_temp->watchdog_timeo = TX_TIMEOUT;
	
#ifdef CONFIG_REALTEK_HW_LSO
#ifdef HW_CHECKSUM_OFFLOAD
	dev_temp->features |= NETIF_F_HW_CSUM;
	dev_temp->hw_features |= NETIF_F_HW_CSUM;
#endif
#ifdef LINUX_SG_ENABLE
	dev_temp->features |= NETIF_F_SG;
	dev_temp->hw_features |= NETIF_F_SG;
#endif
#ifdef LINUX_LSO_ENABLE
#ifdef HW_LSO_ENABLE
	dev_temp->features |= NETIF_F_GSO | NETIF_F_TSO;
	dev_temp->hw_features |= NETIF_F_GSO | NETIF_F_TSO;
#else
//	dev->features |= NETIF_F_SG |  NETIF_F_GSO; //: this configuration will crash in skb_segment
	dev_temp->features |= NETIF_F_GSO; //:test
	dev_temp->hw_features |= NETIF_F_GSO;
#endif
	netif_set_gso_max_size(dev_temp, 65535); 	
#endif
#endif
#ifdef RX_NAPI_MODE
	dev_temp->features |= NETIF_F_GRO;
	dev_temp->hw_features |= NETIF_F_GRO;
#endif

	dev_temp->min_mtu = 68;
	dev_temp->max_mtu = RE8686_ETH_DATA_LEN;

	dev_temp->irq = irq;	// internal phy
	//priv data setting
	dev_temp->rtk_priv_flags = RTK_IFF_DOMAIN_ELAN;
	
	dev_temp->base_addr = (unsigned long) base;

	/* read MAC address from EEPROM */
	{
		u8 __dummy_mac[ETH_ALEN];
		for (i = 0; i < 3; i++)
			((u16 *) __dummy_mac)[i] = i;
		eth_hw_addr_set(dev_temp, __dummy_mac);
	}

	DEV2CP(dev_temp) = &re_private_data_root;
	rtl8686_dev_table[0].dev_instant = dev_temp;
	rtl_set_ethtool_ops(dev_temp);
	rc = register_netdev(dev_temp);
	if (rc) {
		printk(KERN_CONT "%s %d rc = %d\n", __func__, __LINE__, rc);
		return NULL;
	}

	/*
	printk (KERN_INFO "%s: %s at 0x%lx, "
			"%02x:%02x:%02x:%02x:%02x:%02x, "
			"IRQ %d\n",
			dev_temp->name,
			"RTL-8686",
			dev_temp->base_addr,
			dev_temp->dev_addr[0], dev_temp->dev_addr[1],
			dev_temp->dev_addr[2], dev_temp->dev_addr[3],
			dev_temp->dev_addr[4], dev_temp->dev_addr[5],
			dev_temp->irq);
			*/
	
	return dev_temp;
}

static int rtk_gmac_register_other_netdev(void)
{
	unsigned int totalDev = TOTAL_RTL8686_DEV_NUM;
	struct net_device *dev = NULL;
	int i, j, rc;
	
	for(i=1;i<totalDev;i++) {
		dev = alloc_etherdev(sizeof(struct re_dev_private));
		if (!dev) {
			printk(KERN_CONT "%s %d alloc_etherdev fail!\n", __func__, __LINE__);
			goto err_out_iomap;
		}
		/* read MAC address from EEPROM */
		{
			u8 __dummy_mac[ETH_ALEN];
			for (j = 0; j < 3; j++)
				((u16 *) __dummy_mac)[j] = j;
			eth_hw_addr_set(dev, __dummy_mac);
		}

		DEV2CP(dev) = &re_private_data_root;
		if (ROOTDEV->dev.parent)
			SET_NETDEV_DEV(dev, ROOTDEV->dev.parent);
		dev->netdev_ops = &rtl_netdevops;
		dev->watchdog_timeo = TX_TIMEOUT;
		// priv data setting
		switch(rtl8686_dev_table[i].ifflag)
		{
			case RTL8686_ELAN:
				dev->rtk_priv_flags = RTK_IFF_DOMAIN_ELAN;
				break;
			case RTL8686_WAN:
				dev->rtk_priv_flags = RTK_IFF_DOMAIN_WAN;
				break;
			case RTL8686_WLAN:
				dev->rtk_priv_flags = RTK_IFF_DOMAIN_WLAN;
				break;
			case RTL8686_SVAP:		//for slave VAP interfaces, bridge should not send query for each, just need one for slave's br0
				dev->rtk_priv_flags = RTK_IFF_DOMAIN_WLAN | RTK_IFF_DOMAIN_WAN;
				break;
			default:
				printk(KERN_CONT "Error! Should not go here!\n");
		}
		
#ifdef CONFIG_REALTEK_HW_LSO
#ifdef HW_CHECKSUM_OFFLOAD
		dev->features |= NETIF_F_HW_CSUM;
		dev->hw_features |= NETIF_F_HW_CSUM;
#endif
#ifdef LINUX_SG_ENABLE
		dev->features |= NETIF_F_SG;
		dev->hw_features |= NETIF_F_SG;
#endif
#ifdef LINUX_LSO_ENABLE
#ifdef HW_LSO_ENABLE
		dev->features |= NETIF_F_GSO | NETIF_F_TSO;
		dev->hw_features |= NETIF_F_GSO | NETIF_F_TSO;
#else
	//	dev->features |= NETIF_F_SG |  NETIF_F_GSO; //: this configuration will crash in skb_segment
		dev->features |= NETIF_F_GSO; //:test
		dev->hw_features |= NETIF_F_GSO;
#endif
		netif_set_gso_max_size(dev, 65535); 	
#endif
#endif
#ifdef RX_NAPI_MODE
		dev->features |= NETIF_F_GRO;
		dev->hw_features |= NETIF_F_GRO;
#endif
		dev->vlan_features = dev->hw_features;

		dev->min_mtu = 68;
		dev->max_mtu = RE8686_ETH_DATA_LEN;

		dev->base_addr = PRIV2DEV(&re_private_data_root)->base_addr;
		dev->irq = PRIV2DEV(&re_private_data_root)->irq; // internal phy		
		rtl8686_dev_table[i].dev_instant = dev;
		memset(dev->name, 0, sizeof(dev->name));
		memcpy(dev->name, rtl8686_dev_table[i].ifname, strlen(rtl8686_dev_table[i].ifname));
		
		rtl_set_ethtool_ops(dev);
		rc = register_netdev(dev);
		if (rc) {
			printk(KERN_CONT "%s %d rc = %d\n", __func__, __LINE__, rc);
			goto err_out_iomap;
		}
#ifdef CONFIG_RTL8686_SWITCH 
		if(i < (1+MAX_LAN_PORT+MAX_PON_PORT) && i >= 1 )
		{
			// copy interface name
			memcpy(&LCDev_mapping[rtl8686_dev_table[i].phyPort].ifname,dev->name , strlen(dev->name));
			// assign net_dev
			LCDev_mapping[rtl8686_dev_table[i].phyPort].phy_dev = dev;
			/* sfu requires port link change notifier */
			if ( rtl8686_dev_table[i].isPanelPort )
				netif_carrier_off(LCDev_mapping[rtl8686_dev_table[i].phyPort].phy_dev);
		}
#endif
		/*
		printk (KERN_INFO "%s: %s at 0x%lx, "
				"%02x:%02x:%02x:%02x:%02x:%02x, "
				"IRQ %d\n",
				dev->name,
				"RTL-8686",
				dev->base_addr,
				dev->dev_addr[0], dev->dev_addr[1],
				dev->dev_addr[2], dev->dev_addr[3],
				dev->dev_addr[4], dev->dev_addr[5],
				dev->irq);
				*/
#if defined(CONFIG_GPON_FEATURE) || defined(CONFIG_EPON_FEATURE)
		if(!strncmp(dev->name, "nas", 3) && dev->rtk_priv_flags == RTK_IFF_DOMAIN_WAN)
		{
			netif_carrier_off(dev);
			printk(KERN_CONT ">>>> Set %s carrier off !!! \n",dev->name);
		}
#endif
	}

	return 0;

err_out_iomap:
	printk(KERN_CONT "%s %d: here is a error when probe! \n", __func__, __LINE__);
	return -1 ;
}

static int rtk_gmac_multi_lan_device_init(void)
{
#ifdef CONFIG_RTL8686_SWITCH 
	// port 5 is rgmii port
#ifdef CONFIG_RGMII_RESET_PROCESS
	strcpy(LCDev_mapping[RGMII_PORT].ifname, "eth0");
    LCDev_mapping[RGMII_PORT].phy_dev = eth_net_dev[0];
#else
	strcpy(LCDev_mapping[RGMII_PORT].ifname, "nas0");
	LCDev_mapping[RGMII_PORT].phy_dev = LCDev_mapping[WAN_PORT].phy_dev;
#endif
	/* sfu requires port link change notifier */
	if(intr_bcaster_notifier_cb_register(&GMAClinkChangeNotifier) != 0)
	{
		printk(KERN_CONT "Interrupt Broadcaster for Link Change Error !! \n");
	}
#if defined(CONFIG_ETHWAN_USE_USB_SGMII) || defined(CONFIG_ETHWAN_USE_PCIE1_SGMII)
	if(intr_bcaster_notifier_cb_register(&GMACethwanStateChangeNotifier) != 0)
	{
		printk(KERN_CONT "Interrupt Broadcaster for ether WAN Link Change Error !! \n");
	}
#else
	if(intr_bcaster_notifier_cb_register(&GMAConuStateChangeNotifier) != 0)
	{
		printk(KERN_CONT "Interrupt Broadcaster for onu state Change Error !! \n");
	}
	if(intr_bcaster_notifier_cb_register(&GMACoamStateChangeNotifier) != 0)
	{
		printk(KERN_CONT "Interrupt Broadcaster for Epon state Change Error !! \n");
	}
#endif
#endif

#ifdef CONFIG_RTL_MULTI_LAN_DEV
	// Setup default port mapping
	change_dev_port_mapping(LAN_PORT1,"eth0.2");
	change_dev_port_mapping(LAN_PORT2,"eth0.3");
	change_dev_port_mapping(LAN_PORT3,"eth0.4");
	change_dev_port_mapping(LAN_PORT4,"eth0.5");
	change_dev_port_mapping(LAN_PORT5,"eth0.6");
	change_dev_port_mapping(LAN_PORT6,"eth0.7");
	change_dev_port_mapping(WAN_PORT,"nas0");	
	#if defined(CONFIG_RTL_MULTI_PHY_ETH_WAN)
	change_dev_port_mapping(LAN_PORT6,"ifprobe");
	#endif
#endif

#if defined(CONFIG_EXTERNAL_PHY_POLLING) && defined(CONFIG_MDC_MDIO_EXT_PORT)
	#if (CONFIG_MDC_MDIO_EXT_PORT==APOLLOPRO_SGMII0_PORT) || (CONFIG_MDC_MDIO_EXT_PORT==APOLLOPRO_SGMII1_PORT)
	rtk_port_serdesMode_set((CONFIG_MDC_MDIO_EXT_PORT-APOLLOPRO_SGMII0_PORT), LAN_SDS_MODE_SGMII_MAC);
	rtk_port_serdesNWay_set((CONFIG_MDC_MDIO_EXT_PORT-APOLLOPRO_SGMII0_PORT), LAN_SDS_NWAY_AUTO);
	#endif
#endif

	return 0;
}

static int rtk_gmac_netdev_init(unsigned int gmac, unsigned int base, int irq,
				struct platform_device *ofdev)
{
	unsigned int current_gmac=0;
	unsigned int ring_index;
	struct net_device *dev = NULL;
	int netdev_registered;
	int i, rc;
	void*	pBuf;

	if(gmac >= MAX_GMAC_NUM)
	{
		printk(KERN_CONT "%s %d: invalid gmac=%d\n", __func__, __LINE__, gmac);
		return -1;
	}
	
	if(re_private_data[gmac].gmac_enabled != GMAC_TRUE)
		return -1;

	/*
	printk("GMAC%d Tx Ring Size:\n", gmac);
	for(ring_index=0;ring_index<MAX_TXRING_NUM;ring_index++) {
		printk("[#%d]:%d ", ring_index, re_private_data[gmac].re8670_tx_ring_size[ring_index]);
	}
	printk("\nGMAC%d Rx Ring Size:\n", gmac);
	for(ring_index=0;ring_index<MAX_RXRING_NUM;ring_index++) {
		printk("[#%d]:%d ", ring_index, re_private_data[gmac].re8670_rx_ring_size[ring_index]);
	}
	*/

	//allocate and register root dev first
	dev = dev_get_by_name(&init_net, rtl8686_dev_table[0].ifname);
	if(dev==NULL)
	{
		dev = rtk_gmac_register_root_netdev(base, irq); 
		if(dev == NULL) {
			printk(KERN_CONT "%s %d rtk_gmac_register_root_netdev return FAIL\n", __func__, __LINE__);
			return -1;
		}

		netdev_registered = 0;
	}
	else
	{
		printk(KERN_CONT "\n%s %d: netdev name=%s exist\n", __func__, __LINE__, rtl8686_dev_table[0].ifname);
		netdev_registered = 1;
	}

	SET_NETDEV_DEV(dev, &ofdev->dev);

	//set remain re_private_data
	re_private_data[gmac].gmac = gmac;
	re_private_data_root.dev = dev;
	spin_lock_init (&(re_private_data[gmac].tx_lock));
	spin_lock_init (&(re_private_data[gmac].rx_lock));
	re_private_data[gmac].base = base;
	re_private_data[gmac].irq = irq;
	tasklet_init(&re_private_data[gmac].rx_tasklets, __re8670_rx, &re_private_data[gmac]);
#ifdef TX_INTR_HANDLE
	tasklet_init(&re_private_data[gmac].tx_tok_tasklets_ring[0], re8670_tx_tok_ring0, &re_private_data[gmac]);
	tasklet_init(&re_private_data[gmac].tx_tok_tasklets_ring[1], re8670_tx_tok_ring1, &re_private_data[gmac]);
	tasklet_init(&re_private_data[gmac].tx_tok_tasklets_ring[2], re8670_tx_tok_ring2, &re_private_data[gmac]);
	tasklet_init(&re_private_data[gmac].tx_tok_tasklets_ring[3], re8670_tx_tok_ring3, &re_private_data[gmac]);
	tasklet_init(&re_private_data[gmac].tx_tok_tasklets_ring[4], re8670_tx_tok_ring4, &re_private_data[gmac]);
#ifdef TX_KICK_RING_USING_TDU_INT
	tasklet_init(&re_private_data[gmac].tx_tdu_kick_tasklets, re8670_tdu_kick_all, &re_private_data[gmac]);
#endif
#endif
	eth_net_dev[gmac] = dev;
	rtl8686_proc_debug_init(gmac);
	port_relate_setting(gmac);

#ifdef RX_NAPI_MODE
	re_private_data[gmac].napi.dev = dev;
	netif_napi_add_weight(dev, &re_private_data[gmac].napi, re8670_poll, re_private_data[gmac].napi_budget);
#endif

	if(!netdev_registered)
	{
		// allocate and register other device
		if(rtk_gmac_register_other_netdev()) {
			printk(KERN_CONT "%s %d rtk_gmac_register_other_netdev return FAIL\n", __func__, __LINE__);
			return -1;
		}
		rtk_gmac_multi_lan_device_init();
		
		//sw stuff
#if defined(CONFIG_RTL865X_ETH_PRIV_SKB) || defined(CONFIG_RTL865X_ETH_PRIV_SKB_ADV)
		init_priv_eth_skb_buf();
#endif
#if defined(CONFIG_RTL_ETH_RECYCLED_SKB)
		i = init_recycle_eth_skb_buf(RE8670_RX_RING_SIZE, CROSS_LAN_MBUF_LEN, 0, 1);
		if (i < 0)
			return -1;
		re_private_data_root.recycle_skb_pool_id = i;
#endif

#if defined(CONFIG_RTW_MEMPOOL)
		rtw_create_mem_pool_ex("8670_buffer",4*(RE8670_RX_RING_SIZE),(RE8670_RX_RING_SIZE),re_private_data[gmac].rx_buff_size);
#endif

#ifdef CONFIG_RTL8686_SWITCH
		if(timer_pending(&re_private_data_root.rx_pause_by_software_interrupt_timer))
			timer_delete(&re_private_data_root.rx_pause_by_software_interrupt_timer);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
		init_timer(&re_private_data_root.rx_pause_by_software_interrupt_timer);
		dev->getIntrTimer.data = (unsigned long)NULL;
		dev->getIntrTimer.function = setSoftwareInterrupt;
#else
		timer_setup(&re_private_data_root.rx_pause_by_software_interrupt_timer, setSoftwareInterrupt, 0);
#endif
#endif
	}
	
#ifdef TX_RING_DEBUG
	if(re_private_data[gmac].tx_ring_backup_debug)
	{
		memset(re_private_data[gmac].rtl8686_tx_ring_debug, 0, sizeof(struct rtl8686_tx_ring_debug_entry)*MAX_GMAC_NUM*MAX_TXRING_NUM);
		for(i=0;i<MAX_TXRING_NUM;i++)
		{
			if(!re_private_data[gmac].re8670_tx_ring_size[i])
				continue;

			pBuf = kzalloc(RE8670_TXRING_BYTES(re_private_data[gmac].re8670_tx_ring_size[i]), GFP_KERNEL);
			if (!pBuf)
				printk(KERN_CONT "%s %d FAIL !\n\n", __func__, __LINE__);

			pBuf = (void*)( (u32)(pBuf + DESC_ALIGN-1) &  ~(DESC_ALIGN -1) ) ;
			re_private_data[gmac].rtl8686_tx_ring_debug[i].txDescriptor = (DMA_TX_DESC*)((u32)(pBuf) | UNCACHE_MASK);

			pBuf = kzalloc(re_private_data[gmac].re8670_tx_ring_size[i]*sizeof(struct tx_data_buffter), GFP_KERNEL);
			if (!pBuf)
				printk(KERN_CONT "%s %d FAIL !\n\n", __func__, __LINE__);

			re_private_data[gmac].rtl8686_tx_ring_debug[i].dataBuffer = pBuf;
		}
	}
#endif
#ifdef TX_BURST_PACKET_SEND
	memset(&re_private_data[gmac].burst_tx_info.burst_tasklets, 0, sizeof(struct tasklet_struct));
	re_private_data[gmac].burst_tx_info.burst_tasklets.func=(void (*)(unsigned long))_nic_burst_wq_func;
	re_private_data[gmac].burst_tx_info.burst_tasklets.data=(unsigned long)&re_private_data[gmac].burst_tx_info;
#endif

#ifdef CONFIG_RTK_WFOAX
	if(gmac != WFO_GMAC_NO)
#endif
	rc = re8670_alloc_rings(&re_private_data[gmac]);
	if (rc)
		return -1;

	return 0;
}

static int rtk_gmac_hw_init(unsigned int gmac)
{
	re8670_ip_enable(gmac);
	
	if(re_private_data[gmac].gmac_enabled != GMAC_TRUE)
		return -1;

	//stop hw
	re8670_stop_hw(&re_private_data[gmac]);		
	re8670_reset_hw(&re_private_data[gmac]);

	config_tx_jumbo(gmac, re_private_data[gmac].tx_jumbo_frame_enabled);

	//20170502: disable gmac padding by default
	gmac_padding_enable(gmac, DISABLED);

#ifdef TX_INTR_HANDLE
	tx_int_mitigation_set(gmac, 1);
#endif

	return 0;
}

static int of_platform_rtk_gmac_probe(struct platform_device *ofdev)
{
	struct re_private_root *root_cp = &re_private_data_root;
	const struct of_device_id *match;
	struct device_node *np;
	int rc, irq, i;
	u32 *regs;
	void *data;	

#ifdef CONFIG_RTL8686_SWITCH
	extern int drv_nic_rxhook_init(void);
#endif
	
	match = of_match_device(of_platform_rtk_gmac_table, &ofdev->dev);
	if (!match)
		return -EINVAL;
	
	np = ofdev->dev.of_node;
		
	rtk_gmac_re_private_data_init();
	for (i=0; i<MAX_GMAC_NUM; i++) {
		char name[8];
		struct device_node *child;
		snprintf(name, sizeof(name)-1, "gmac%d", i);
		child = of_find_node_by_name(np, name);		
		if (child) {
			regs  = of_get_property(child, "reg", NULL);
			if (regs) {
				irq = irq_of_parse_and_map(child, 0);
				//printk(KERN_CONT "%s(%d): reg %x %x irq=%d\n", __func__,__LINE__,regs[0],regs[1],irq);
				rtk_gmac_customize_re_private_data_init(i, child);
				rtk_gmac_netdev_init(i, ioremap(regs[0], regs[1]), irq, ofdev);
				rtk_gmac_hw_init(i);			
			}
		} else {
			root_cp->re_private_data_ptr[i]->gmac_enabled = GMAC_FALSE;
		}

		root_cp->gmac_enable_mask |= (root_cp->re_private_data_ptr[i]->gmac_enabled == GMAC_TRUE) ? (1<<i):0;
	}

#ifdef CONFIG_RTL8686_SWITCH
	drv_nic_rxhook_init();	
#endif
#ifdef CONFIG_REALTEK_HW_LSO
	re8670_init_mtu();
#endif
	
	return 0;
}

static int of_platform_rtk_gmac_remove(struct platform_device *ofdev) {
	return 0;
}

static struct platform_driver of_platform_rtk_gmac_driver = {
	.driver = {
		.name = "rtk_gmac",
		.of_match_table = of_platform_rtk_gmac_table,
	},
	.probe = of_platform_rtk_gmac_probe,
	.remove = of_platform_rtk_gmac_remove,
};
module_platform_driver(of_platform_rtk_gmac_driver);
#endif // CONFIG_OF

void re8670_hardware_reset(unsigned int gmac)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp = root_cp->re_private_data_ptr[gmac];
	unsigned int waitingTimes = 0;
	
	printk(KERN_CONT "%s %d enter\n", __func__, __LINE__);
	
	//MII Tx Disable
	RLE0787_W32(gmac, IO_CMD, RLE0787_R32(gmac, IO_CMD)&~(1<<4));
	//MII Rx Disable
	RLE0787_W32(gmac, IO_CMD, RLE0787_R32(gmac, IO_CMD)&~(1<<5));
	
	re8670_stop_hw(cp);

	switch(gmac) {
		case 0:
			REG32(BSP_IP_SEL) &= ~BSP_EN_GMAC;
			mdelay(10);
			REG32(BSP_IP_SEL) |= BSP_EN_GMAC;
			break;
		case 1:
			REG32(NEW_BSP_IP_SEL) &= ~BSP_EN_GMAC1;
			mdelay(10);
			REG32(NEW_BSP_IP_SEL) |= BSP_EN_GMAC1;
			break;
		case 2:
			REG32(NEW_BSP_IP_SEL) &= ~BSP_EN_GMAC2;
			mdelay(10);
			REG32(NEW_BSP_IP_SEL) |= BSP_EN_GMAC2;
			break;
	}
	
	//Setting to 1 forces the Ethernet module to a software reset state which disables the transmitter and receiver
	RLE0787_W32(gmac, COM_REG, RLE0787_R32(gmac, COM_REG) | 0x1);
	while(RLE0787_R32(gmac, COM_REG) & 0x1 && (waitingTimes < 1000))
	{
		printk(KERN_CONT "%s %d waiting ... (waitingTimes=%d)\n", __func__, __LINE__, waitingTimes);
		waitingTimes++;
	}

	//MII Tx Enable
	RLE0787_W32(gmac, IO_CMD, RLE0787_R32(gmac, IO_CMD)|(1<<4));
	//MII Rx Enable
	RLE0787_W32(gmac, IO_CMD, RLE0787_R32(gmac, IO_CMD)|(1<<5));
	
	config_tx_jumbo(gmac, cp->tx_jumbo_frame_enabled);
	gmac_padding_enable(gmac, DISABLED);

	printk(KERN_CONT "%s %d exit\n", __func__, __LINE__);
}

int re8670_reset(void)
{
	struct re_private_root *root_cp = &re_private_data_root;
	struct re_private *cp;
	unsigned int totalDev = TOTAL_RTL8686_DEV_NUM;
	unsigned int tempLxcbus0MasterReg;
	unsigned int tempLxcbus0SlaveReg;
	unsigned int tempLxcbus1MasterReg;
	unsigned int tempLxcbus1SlaveReg;
	unsigned int upMask = 0;
	unsigned int gmac;
	int i;	
#if defined(CONFIG_RTL9607C_SERIES) && defined(HWNAT_CUSTOMIZE)	
	unsigned int sramIsUsedByOthers = FALSE;	
#endif

	printk(KERN_CONT "%s %d enter\n", __func__, __LINE__);

	//printk("%s %d LXCBUS0_MASTER_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS0_MASTER_REG)));
	//printk("%s %d LXCBUS0_SLAVE_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS0_SLAVE_REG)));
	//printk("%s %d LXCBUS1_MASTER_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS1_MASTER_REG)));
	//printk("%s %d LXCBUS1_SLAVE_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS1_SLAVE_REG)));


	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		cp = root_cp->re_private_data_ptr[gmac];
		if(cp->gmac_enabled == GMAC_FALSE)
			continue;

		GMAC_SPIN_LOCK(&cp->tx_lock);
		disable_irq(cp->irq);
		cp->eth_close=GMAC_TRUE;
	}

	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		cp = root_cp->re_private_data_ptr[gmac];
		if(cp->gmac_enabled == GMAC_FALSE)
			continue;

		printk(KERN_CONT "%s %d gmac=%d\n", __func__, __LINE__, gmac);
		
		//Enable LX bus timeout monitor
		if(gmac==0 || gmac==2)
		{
			printk(KERN_CONT "%s %d Enable LX bus 0 timeout monitor\n", __func__, __LINE__);
			tempLxcbus0MasterReg = (*(volatile u32*)((u32) LXCBUS0_MASTER_REG));
			tempLxcbus0SlaveReg = (*(volatile u32*)((u32) LXCBUS0_SLAVE_REG));
			(*(volatile u32*)((u32)LXCBUS0_MASTER_REG)) = tempLxcbus0MasterReg | (1<<31);
			(*(volatile u32*)((u32)LXCBUS0_SLAVE_REG)) = tempLxcbus0SlaveReg | (1<<31);
			//printk("%s %d LXCBUS0_MASTER_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS0_MASTER_REG)));
			//printk("%s %d LXCBUS0_SLAVE_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS0_SLAVE_REG)));
		}
		if(gmac==1)
		{
			printk(KERN_CONT "%s %d Enable LX bus 1 timeout monitor\n", __func__, __LINE__);
			tempLxcbus1MasterReg = (*(volatile u32*)((u32) LXCBUS1_MASTER_REG));
			tempLxcbus1SlaveReg = (*(volatile u32*)((u32) LXCBUS1_SLAVE_REG));
			(*(volatile u32*)((u32)LXCBUS1_MASTER_REG)) = tempLxcbus1MasterReg | (1<<31);
			(*(volatile u32*)((u32)LXCBUS1_SLAVE_REG)) = tempLxcbus1SlaveReg | (1<<31);
			//printk("%s %d LXCBUS1_MASTER_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS1_MASTER_REG)));
			//printk("%s %d LXCBUS1_SLAVE_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS1_SLAVE_REG)));
		}
		
		cp->stag_pid = RLE0787_R32(gmac, VLAN_REG);
		cp->stag_pid1 = RLE0787_R32(gmac, VLAN1_REG);
		
		re8670_hardware_reset(gmac);

		re8670_free_rings(cp);
#if defined(CONFIG_RTL9607C_SERIES)  && defined(HWNAT_CUSTOMIZE)	
		//_hwnat_customized_version_set(hwnat_customized_version);	
		_hwnat_customized_version_set_by_gmac(gmac);	

		memset(re8686_rx_descIdx_customized_tx_descAddr,0, sizeof(re8686_rx_descIdx_customized_tx_descAddr[0][0])*MAX_GMAC_NUM*MAX_RXRING_NUM); 
		if(rtk_dynamic_sram_state_get()==ENABLED)	
		{	
			printk(KERN_CONT "\033[1;33;41m Sram is set before! @ %s(%d)\033[0m\n", __FUNCTION__, __LINE__);
			//sramIsUsedByOthers = TRUE;	
		}	
#endif	
			
		re8670_alloc_rings(cp);
					
#if defined(CONFIG_RTL9607C_SERIES)  && defined(HWNAT_CUSTOMIZE)	
		printk(KERN_CONT "%s %d\n", __func__, __LINE__);
		if(cp->gmac_enabled == GMAC_TRUE)	
		{	
			//if(sramIsUsedByOthers==FALSE) 
			rtk_dynamic_sram_restart(gmac); 
		}	
#endif	

		re8670_init_hw(cp);
		re8670_init_trx_cdo(cp);

		if(cp->stag_pid)
			RLE0787_R32(gmac, VLAN_REG) = cp->stag_pid;
		if(cp->stag_pid1)
			RLE0787_R32(gmac, VLAN1_REG) = cp->stag_pid1;

		mdelay(500);
		//Disable LX bus timeout monitor
		if(gmac==0 || gmac==2)
		{
			printk(KERN_CONT "%s %d Disable LX bus 0 timeout monitor\n", __func__, __LINE__);
			(*(volatile u32*)((u32)LXCBUS0_MASTER_REG)) = tempLxcbus0MasterReg;
			(*(volatile u32*)((u32)LXCBUS0_SLAVE_REG)) = tempLxcbus0SlaveReg;
			//printk("%s %d LXCBUS0_MASTER_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS0_MASTER_REG)));
			//printk("%s %d LXCBUS0_SLAVE_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS0_SLAVE_REG)));
		}
		if(gmac==1)
		{
			printk("%s %d Disable LX bus 1 timeout monitor\n", __func__, __LINE__);
			(*(volatile u32*)((u32)LXCBUS1_MASTER_REG)) = tempLxcbus1MasterReg;
			(*(volatile u32*)((u32)LXCBUS1_SLAVE_REG)) = tempLxcbus1SlaveReg;
			//printk("%s %d LXCBUS1_MASTER_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS1_MASTER_REG)));
			//printk("%s %d LXCBUS1_SLAVE_REG=0x%x\n", __func__, __LINE__, (*(volatile u32*)((u32)LXCBUS1_SLAVE_REG)));
		}
	}

	for(gmac=0 ; gmac<MAX_GMAC_NUM ; gmac++)
	{
		cp = root_cp->re_private_data_ptr[gmac];
		if(cp->gmac_enabled == GMAC_FALSE)
			continue;

		cp->eth_close=GMAC_FALSE;
		enable_irq(cp->irq);
		GMAC_SPIN_UNLOCK(&cp->tx_lock);
	}

	printk("%s %d exit\n", __func__, __LINE__);
	return 0;
}

//module_init(re8670_probe);
//module_exit(re8670_exit);
#ifdef CONFIG_WIRELESS_LAN_MODULE
int (*wirelessnet_hook)(void) = NULL;
EXPORT_SYMBOL(wirelessnet_hook);
#endif
EXPORT_SYMBOL(rtk_nic_isPhyDev);
EXPORT_SYMBOL(re8686_send_with_txInfo_and_mask);
EXPORT_SYMBOL(re8686_send_with_txInfo_and_mask_burst);
EXPORT_SYMBOL(re8686_set_flow_control);
#ifdef CONFIG_RTL8686_SWITCH
EXPORT_SYMBOL(re8686_set_pauseBySw);
#endif
EXPORT_SYMBOL(re8686_set_vlan_register);
EXPORT_SYMBOL(re8686_get_vlan_register);	
EXPORT_SYMBOL(re8686_customized_rx_and_tx);

