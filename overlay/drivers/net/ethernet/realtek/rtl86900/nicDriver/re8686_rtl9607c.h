/*	
 *	re8686_rtl9607c.h
*/
#ifndef _RE8686_RTL9607C_H_
#define _RE8686_RTL9607C_H_
#include <uapi/linux/if.h>
#include <linux/netdevice.h>
#include "../sdk/system/include/common/type.h"


/*================================================
				GMAC used Macros
================================================*/

#define GMAC_SPIN_LOCK_TYPE_BH			1
#define GMAC_SPIN_LOCK_TYPE_IRQSAVE		2
#define GMAC_SPIN_LOCK_TYPE				GMAC_SPIN_LOCK_TYPE_BH

#if (GMAC_SPIN_LOCK_TYPE==GMAC_SPIN_LOCK_TYPE_BH) /* for VoIP test */
#define GMAC_SPIN_LOCK(lock)	spin_lock_bh(lock)
#define GMAC_SPIN_UNLOCK(lock)	spin_unlock_bh(lock)
#else
#define GMAC_SPIN_LOCK(lock)	unsigned long flags;spin_lock_irqsave(lock, flags)
#define GMAC_SPIN_UNLOCK(lock)	spin_unlock_irqrestore(lock, flags)
#endif

	
#if defined(CONFIG_FC_SPECIAL_FAST_FORWARD)	
#define HWNAT_CUSTOMIZE	
#define HWNAT_CUSTOMIZE_NPTV6_SRAM_ACC_V2	
#endif	
#ifndef uint32	
#define uint32 unsigned int	
#endif	
#	
#ifndef SUCCESS	
#define SUCCESS 0	
#endif	
#ifndef FAIL	
#define FAIL -1	
#endif	

#ifdef CONFIG_RX_MRING_INT_SPLIT
#define RX_MRING_INT_SPLIT
#endif

#define TX_RING_DEBUG
#undef TX_RING_DEBUG

#define TX_CREATE_TEST_PACKET_DEBUG
#undef TX_CREATE_TEST_PACKET_DEBUG

#define TX_BURST_PACKET_SEND
//#undef TX_BURST_PACKET_SEND

#ifdef CONFIG_TX_WATCHDOG_TIMEOUT_RESET
#define TX_WATCHDOG_TIMEOUT_RESET
#endif

#ifdef CONFIG_RX_NAPI_MODE
#define RX_NAPI_MODE // otherwise interrupt mode
#endif

#ifdef RX_NAPI_MODE
#define RX_NAPI_MODE_DEBUG // otherwise interrupt mode
#undef RX_NAPI_MODE_DEBUG
#endif

#ifdef CONFIG_TX_HW_LSO
#define CONFIG_REALTEK_HW_LSO
#endif

#ifdef CONFIG_TX_BACKUP_RING
#define TX_BACKUP_RING
#endif

#ifdef CONFIG_TX_BACKUP_GMAC
#define TX_BACKUP_GMAC
#endif

#ifdef CONFIG_RTK_SKB_MARK2 
#define LOCAL_SERVICE_ACCELERATE
//#undef LOCAL_SERVICE_ACCELERATE
#endif

#ifdef CONFIG_TX_INTR_HANDLE
#define TX_INTR_HANDLE
#endif

#ifdef CONFIG_TX_RECYCLE_SKB_USING_TOK_INT
#define TX_RECYCLE_SKB_USING_TOK_INT
#endif

#ifdef CONFIG_TX_KICK_RING_USING_TDU_INT
#define TX_KICK_RING_USING_TDU_INT
#endif

#ifdef CONFIG_TX_RECYCLE_SKB_USING_POLLING
#define TX_RECYCLE_SKB_USING_POLLING
#ifdef CONFIG_TX_RECYCLE_SKB_POLLING_10MSECONDS
#define TX_RECYCLE_SKB_POLLING_10MSECONDS	CONFIG_TX_RECYCLE_SKB_POLLING_10MSECONDS
#else
#define TX_RECYCLE_SKB_POLLING_10MSECONDS	(1)
#endif
#endif

#ifdef CONFIG_TX_KICK_RING_USING_POLLING
#define TX_KICK_RING_USING_POLLING
#ifdef CONFIG_TX_RING_ACTIVE_POLLING_SECONDS
#define TX_RING_ACTIVE_POLLING_SECONDS	CONFIG_TX_RING_ACTIVE_POLLING_SECONDS
#else
#define TX_RING_ACTIVE_POLLING_SECONDS	(1)
#endif
#ifdef CONFIG_TX_RING_ACTIVE_POLLING_PACKET_NUM
#define TX_RING_ACTIVE_POLLING_PACKET_NUM	CONFIG_TX_RING_ACTIVE_POLLING_PACKET_NUM
#else
#define TX_RING_ACTIVE_POLLING_PACKET_NUM	(50)
#endif
#ifdef CONFIG_TX_KICK_RING_POLLING_10MSECONDS
#define TX_KICK_RING_POLLING_10MSECONDS	CONFIG_TX_KICK_RING_POLLING_10MSECONDS
#else
#define TX_KICK_RING_POLLING_10MSECONDS	(1)
#endif
#endif

enum {
	GMAC_OFF = 0,
	GMAC_ON = 1
};
enum {
	GMAC_FALSE = 0,
	GMAC_TRUE = 1
};

#define PADDING_ENABLED GMAC_ON
#define PADDING_DISABLED GMAC_OFF

#define ON 1
#define OFF 0

#define ADDR_OFFSET 0x0010

#define CONFIG_RG_JUMBO_FRAME 1
//#define RTL0371
//#define GMAC_FPGA

#ifdef CONFIG_RG_JUMBO_FRAME
#define RE8686_ETH_DATA_LEN 10000
#endif

#define RE8686_HW_SMALLEST_DATA_LEN	14

#define VPORT_CPU_TAG			0
#define VPORT_VLAN_TAG			1
#define ETH_WAN_PORT 			3 // in virtual view
#define SW_PORT_NUM 			11
#if !defined(CONFIG_LUNA_G3_SERIES)
#define APOLLOPRO_SGMII0_PORT	6
#define APOLLOPRO_SGMII1_PORT	7
#define RGMII_PORT 				8
#ifdef CONFIG_RTL9603CVD_SERIES
#define APOLLOPRO_PON_PORT                     4
#define CPU_PORT0                              5               // IA
#define LAN_PORT1                              0
#define LAN_PORT2                              1
#define LAN_PORT3                              2
#define LAN_PORT4                              3
#define LAN_PORT5                              8
#define LAN_PORT6                              LAN_PORT5
#define CPU_PORT_MASK_ALL                      ((1<<CPU_PORT0))
#else /*CONFIG_RTL9603CVD_SERIES*/
#define APOLLOPRO_PON_PORT                     5
#define CPU_PORT0 				9		// IA
#define CPU_PORT1 				10		// IA
#define CPU_PORT2 				7		// 5281
#define LAN_PORT1				0
#define LAN_PORT2				1
#define LAN_PORT3				2
#define LAN_PORT4				3
#define LAN_PORT5				4
#define LAN_PORT6				8
#define CPU_PORT_MASK_ALL 			((1<<CPU_PORT0)|(1<<CPU_PORT1)|((1<<CPU_PORT2)))
#endif /*end of CONFIG_RTL9603CVD_SERIES*/

#if defined(CONFIG_ETHWAN_USE_USB_SGMII)
#define WAN_PORT 				APOLLOPRO_SGMII0_PORT
#elif defined(CONFIG_ETHWAN_USE_PCIE1_SGMII)
#define WAN_PORT 				APOLLOPRO_SGMII1_PORT
#else
#define WAN_PORT 				APOLLOPRO_PON_PORT
#endif
#define MAX_PON_PORT		1
//#define IS_CPU_PORT(port)	(port==CPU_PORT0||port==CPU_PORT1||port==CPU_PORT2)
#if defined(CONFIG_RTK_SOC_RTL8198D)
#define MAX_LAN_PORT		9
/*
 * Note: IS_LAN_PORT() macro is used in gmacintr_notifier_link_change() function only,
 *   if IS_LAN_PORT() return false, gmacintr_notifier_link_change() will do nothing,
 *   if IS_LAN_PORT() return true, gmacintr_notifier_link_change() will do carrier on or carrier off of network interface
 *   depend on link up or link down of Link Change Interrupt status.
 */
#define IS_LAN_PORT(port)	(port==LAN_PORT1||port==LAN_PORT2||port==LAN_PORT3||port==LAN_PORT4||port==LAN_PORT5\
		||port==LAN_PORT6||port==APOLLOPRO_SGMII0_PORT||port==APOLLOPRO_SGMII1_PORT||port==APOLLOPRO_PON_PORT)
#elif !defined(CONFIG_ETHWAN_USE_USB_SGMII) && !defined(CONFIG_ETHWAN_USE_PCIE1_SGMII)
#define MAX_LAN_PORT		8
/* APOLLOPRO_PON_PORT is deliberately NOT in this list.
 *
 * Adding it was tried: IS_LAN_PORT() gates gmacintr_notifier_link_change(), so
 * with port 5 included, nas0 finally got netif_carrier_on() calls. What came
 * through was the PON SerDes's permanent "up" -- nas0 then reported carrier=1
 * with no cable attached to the router at all, exactly like eth0.9 (SGMII1).
 * A carrier that is always 1 is worse than no carrier: it reads as a live WAN
 * to anything that checks, while rx_packets stays 0. */
#define IS_LAN_PORT(port)	(port==LAN_PORT1||port==LAN_PORT2||port==LAN_PORT3||port==LAN_PORT4||port==LAN_PORT5||port==LAN_PORT6||port==APOLLOPRO_SGMII0_PORT||port==APOLLOPRO_SGMII1_PORT)
#elif !defined(CONFIG_ETHWAN_USE_USB_SGMII)
#define MAX_LAN_PORT		7
#define IS_LAN_PORT(port)	(port==LAN_PORT1||port==LAN_PORT2||port==LAN_PORT3||port==LAN_PORT4||port==LAN_PORT5||port==LAN_PORT6||port==APOLLOPRO_SGMII0_PORT)
#elif !defined(CONFIG_ETHWAN_USE_PCIE1_SGMII)
#define MAX_LAN_PORT		7
#define IS_LAN_PORT(port)	(port==LAN_PORT1||port==LAN_PORT2||port==LAN_PORT3||port==LAN_PORT4||port==LAN_PORT5||port==LAN_PORT6||port==APOLLOPRO_SGMII1_PORT)
#else
#define MAX_LAN_PORT		6
#define IS_LAN_PORT(port)	(port==LAN_PORT1||port==LAN_PORT2||port==LAN_PORT3||port==LAN_PORT4||port==LAN_PORT5||port==LAN_PORT6)
#endif

#define IS_CPU_PORT(port)	(port&CPU_PORT_MASK_ALL)
#define IS_PON_PORT(port)	(port==APOLLOPRO_PON_PORT)
#define IS_WAN_PORT(port)	(port==WAN_PORT)

#endif // !CONFIG_LUNA_G3_SERIES

#define GMAC0_ETHBASE	0xB8012000  // memory mapping of GMAC0
#define GMAC1_ETHBASE	0xB8014000	// memory mapping of GMAC1
#define GMAC2_ETHBASE	0xB8016000	// memory mapping of GMAC2

/*
#define CPU_PORT0_MASK 			(1<<CPU_PORT0)		// IA
#define CPU_PORT1_MASK 			(1<<CPU_PORT1)		// IA
#define CPU_PORT2_MASK 			(1<<CPU_PORT2)		// 5281
#define CPU_PORT_MASK_ALL		(CPU_PORT0_MASK|CPU_PORT1_MASK|CPU_PORT2_MASK)
*/
#define MAX_GMAC_NUM	 3

#ifdef CONFIG_RTK_SINGLE_RX_RING
#define MAX_RXRING_NUM 1
#else
#define MAX_RXRING_NUM 6
#endif
#define MAX_TXRING_NUM 5
// plz use the order of 2, set size 0 to no use ring
// GMAC0, Rx ring:

//#ifdef HWNAT_CUSTOMIZE
#define MAX_HWNAT_CUSTOMIZED_TX_HDR_BUFFER_SIZE 56
#define GMAC_ACC_RING_SIZE 256
//#endif

#ifdef CONFIG_RTK_SINGLE_RX_RING
#define GMAC0_RX1_SIZE 1024
#define GMAC0_RX2_SIZE 0
#define GMAC0_RX3_SIZE 0
#define GMAC0_RX4_SIZE 0
#define GMAC0_RX5_SIZE 0
#define GMAC0_RX6_SIZE 0
#else
#define GMAC0_RX1_SIZE 1024
#define GMAC0_RX2_SIZE 256
#define GMAC0_RX3_SIZE 256
#if defined(HWNAT_CUSTOMIZE) && !defined(CONFIG_GMAC2_USABLE)
#define GMAC0_RX4_SIZE GMAC_ACC_RING_SIZE	
#define GMAC0_RX5_SIZE GMAC_ACC_RING_SIZE	
#else

#define GMAC0_RX4_SIZE 64
#define GMAC0_RX5_SIZE 64
#endif

#define GMAC0_RX6_SIZE 64
#endif //CONFIG_RTK_SINGLE_RX_RING


// GMAC0, Tx ring:
#define GMAC0_TX1_SIZE 2048
#ifdef TX_BACKUP_RING
#define GMAC0_TX2_SIZE GMAC0_TX1_SIZE
#else
#define GMAC0_TX2_SIZE 64
#endif
#if defined(HWNAT_CUSTOMIZE) && !defined(CONFIG_GMAC2_USABLE)
#define GMAC0_TX3_SIZE GMAC_ACC_RING_SIZE
#define GMAC0_TX4_SIZE GMAC_ACC_RING_SIZE
#else

#define GMAC0_TX3_SIZE 64
#define GMAC0_TX4_SIZE 64
#endif

#define GMAC0_TX5_SIZE 64

// GMAC1, Rx ring:
#ifdef CONFIG_GMAC1_USABLE
#ifdef CONFIG_RTK_SINGLE_RX_RING
#define GMAC1_RX1_SIZE 1024
#define GMAC1_RX2_SIZE 0
#define GMAC1_RX3_SIZE 0
#define GMAC1_RX4_SIZE 0
#define GMAC1_RX5_SIZE 0
#define GMAC1_RX6_SIZE 0
#else
#define GMAC1_RX1_SIZE 1024
#define GMAC1_RX2_SIZE 256
#define GMAC1_RX3_SIZE 256
#define GMAC1_RX4_SIZE 64
#ifdef HWNAT_CUSTOMIZE
#define GMAC1_RX5_SIZE GMAC_ACC_RING_SIZE
#else
#define GMAC1_RX5_SIZE 64
#endif
#define GMAC1_RX6_SIZE 64
#endif
#else 
#define GMAC1_RX1_SIZE 0
#define GMAC1_RX2_SIZE 0
#define GMAC1_RX3_SIZE 0
#define GMAC1_RX4_SIZE 0
#define GMAC1_RX5_SIZE 0
#define GMAC1_RX6_SIZE 0
#endif 

// GMAC1, Tx ring:
#define GMAC1_TX1_SIZE 2048
#ifdef TX_BACKUP_RING
#define GMAC1_TX2_SIZE GMAC1_TX1_SIZE
#else
#define GMAC1_TX2_SIZE 64
#endif
#define GMAC1_TX3_SIZE 64
#ifdef HWNAT_CUSTOMIZE
#define GMAC1_TX4_SIZE GMAC_ACC_RING_SIZE
#else
#define GMAC1_TX4_SIZE 64
#endif
#define GMAC1_TX5_SIZE 64

// GMAC2, Rx ring:
#ifdef CONFIG_GMAC2_USABLE
#ifdef CONFIG_RTK_SINGLE_RX_RING
#define GMAC2_RX1_SIZE 256
#define GMAC2_RX2_SIZE 0
#define GMAC2_RX3_SIZE 0
#define GMAC2_RX4_SIZE 0
#define GMAC2_RX5_SIZE 0
#define GMAC2_RX6_SIZE 0
#else
#define GMAC2_RX1_SIZE 256
#define GMAC2_RX2_SIZE 256
#define GMAC2_RX3_SIZE 256
#define GMAC2_RX4_SIZE 64
#ifdef HWNAT_CUSTOMIZE
#define GMAC2_RX5_SIZE GMAC_ACC_RING_SIZE
#else
#define GMAC2_RX5_SIZE 64
#endif
#define GMAC2_RX6_SIZE 64
#endif
#else
#define GMAC2_RX1_SIZE 0
#define GMAC2_RX2_SIZE 0
#define GMAC2_RX3_SIZE 0
#define GMAC2_RX4_SIZE 0
#define GMAC2_RX5_SIZE 0
#define GMAC2_RX6_SIZE 0
#endif 

// GMAC2, Tx ring:
#define GMAC2_TX1_SIZE 2048
#ifdef TX_BACKUP_RING
#define GMAC2_TX2_SIZE GMAC2_TX1_SIZE
#else
#define GMAC2_TX2_SIZE 64
#endif
#define GMAC2_TX3_SIZE 64
#ifdef HWNAT_CUSTOMIZE
#define GMAC2_TX4_SIZE GMAC_ACC_RING_SIZE
#else
#define GMAC2_TX4_SIZE 64
#endif
#define GMAC2_TX5_SIZE 64

// GMAC0 total ring size:
#define GMAC0_RX_SIZE	(GMAC0_RX1_SIZE+GMAC0_RX2_SIZE+GMAC0_RX3_SIZE+GMAC0_RX4_SIZE+GMAC0_RX5_SIZE+GMAC0_RX6_SIZE)
// GMAC1 total ring size:
#define GMAC1_RX_SIZE	(GMAC1_RX1_SIZE+GMAC1_RX2_SIZE+GMAC1_RX3_SIZE+GMAC1_RX4_SIZE+GMAC1_RX5_SIZE+GMAC1_RX6_SIZE)
// GMAC2 total ring size:
#define GMAC2_RX_SIZE	(GMAC2_RX1_SIZE+GMAC2_RX2_SIZE+GMAC2_RX3_SIZE+GMAC2_RX4_SIZE+GMAC2_RX5_SIZE+GMAC2_RX6_SIZE)

#define RE8670_RX_RING_SIZE \
GMAC0_RX_SIZE+   \
GMAC1_RX_SIZE+   \
GMAC2_RX_SIZE

#define IS_LEGAL_RX_RING_SIZE(rsize)	(rsize==16 || rsize==32 || rsize==64 || rsize==128 || rsize==256 || rsize==512 || rsize==1024 || rsize==2048 || rsize==4096)

#ifdef RX_NAPI_MODE
#define GMAC0_RX_NAPI_BUDGET	64
#define GMAC1_RX_NAPI_BUDGET	64
#define GMAC2_RX_NAPI_BUDGET	64
#endif

#define GMAC0_RX_MULTIRING_BITMAP	((GMAC0_RX1_SIZE? 1: 0) \
								| (GMAC0_RX2_SIZE? 1<<1 : 0) \
								| (GMAC0_RX3_SIZE? 1<<2 : 0) \
								| (GMAC0_RX4_SIZE? 1<<3 : 0) \
								| (GMAC0_RX5_SIZE? 1<<4 : 0) \
								| (GMAC0_RX6_SIZE? 1<<5 : 0))
#define GMAC1_RX_MULTIRING_BITMAP ((GMAC1_RX1_SIZE? 1: 0) \
								| (GMAC1_RX2_SIZE? 1<<1 : 0) \
								| (GMAC1_RX3_SIZE? 1<<2 : 0) \
								| (GMAC1_RX4_SIZE? 1<<3 : 0) \
								| (GMAC1_RX5_SIZE? 1<<4 : 0) \
								| (GMAC1_RX6_SIZE? 1<<5 : 0))
#define GMAC2_RX_MULTIRING_BITMAP ((GMAC2_RX1_SIZE? 1: 0) \
								| (GMAC2_RX2_SIZE? 1<<1 : 0) \
								| (GMAC2_RX3_SIZE? 1<<2 : 0) \
								| (GMAC2_RX4_SIZE? 1<<3 : 0) \
								| (GMAC2_RX5_SIZE? 1<<4 : 0) \
								| (GMAC2_RX6_SIZE? 1<<5 : 0))
#define GMAC0_TX_MULTIRING_BITMAP ((GMAC0_TX1_SIZE? 1 : 0) \
								| (GMAC0_TX2_SIZE? 1<<1 : 0) \
								| (GMAC0_TX3_SIZE? 1<<2 : 0) \
								| (GMAC0_TX4_SIZE? 1<<3 : 0) \
								| (GMAC0_TX5_SIZE? 1<<4 : 0))
#define GMAC1_TX_MULTIRING_BITMAP ((GMAC1_TX1_SIZE? 1 : 0) \
								| (GMAC1_TX2_SIZE? 1<<1 : 0) \
								| (GMAC1_TX3_SIZE? 1<<2 : 0) \
								| (GMAC1_TX4_SIZE? 1<<3 : 0) \
								| (GMAC1_TX5_SIZE? 1<<4 : 0))
#define GMAC2_TX_MULTIRING_BITMAP ((GMAC2_TX1_SIZE? 1 : 0) \
								| (GMAC2_TX2_SIZE? 1<<1 : 0) \
								| (GMAC2_TX3_SIZE? 1<<2 : 0) \
								| (GMAC2_TX4_SIZE? 1<<3 : 0) \
								| (GMAC2_TX5_SIZE? 1<<4 : 0))

#define GMAC0_RX_ONLY_RING1			((GMAC0_RX_MULTIRING_BITMAP == 1)? 1 : 0)
#define GMAC0_RX_NOT_ONLY_RING1		((GMAC0_RX_MULTIRING_BITMAP == 1)? 0 : 1)
#define GMAC1_RX_ONLY_RING1			((GMAC1_RX_MULTIRING_BITMAP == 1)? 1 : 0)
#define GMAC1_RX_NOT_ONLY_RING1		((GMAC1_RX_MULTIRING_BITMAP == 1)? 0 : 1)
#define GMAC2_RX_ONLY_RING1			((GMAC2_RX_MULTIRING_BITMAP == 1)? 1 : 0)
#define GMAC2_RX_NOT_ONLY_RING1		((GMAC2_RX_MULTIRING_BITMAP == 1)? 0 : 1)

#ifdef TX_RECYCLE_SKB_USING_TOK_INT
#define CMD_CONFIG	0xd15ff130	// 0x4049E130// pkt timer = 15 => 4pkt trigger int
#else
#define CMD_CONFIG	0xd059f130	// 0x4049E130// pkt timer = 15 => 4pkt trigger int
#endif
#define CMD1_CONFIG	0x30000000   // desc format ==> apollo type, not support multiple ring

/* Macros for Tx/Rx info set/get */
#define GMAC_RXINFO_OWN(x)                  (((struct rx_info *)x)->opts1.bit.own)
#define GMAC_RXINFO_EOR(x)                  (((struct rx_info *)x)->opts1.bit.eor)
#define GMAC_RXINFO_FS(x)                   (((struct rx_info *)x)->opts1.bit.fs)
#define GMAC_RXINFO_LS(x)                   (((struct rx_info *)x)->opts1.bit.ls)
#define GMAC_RXINFO_CRCERR(x)               (((struct rx_info *)x)->opts1.bit.crcerr)
#define GMAC_RXINFO_IPV4CSF(x)              (((struct rx_info *)x)->opts1.bit.ipv4csf)
#define GMAC_RXINFO_L4CSF(x)                (((struct rx_info *)x)->opts1.bit.l4csf)
#define GMAC_RXINFO_RCDF(x)                 (((struct rx_info *)x)->opts1.bit.rcdf)
#define GMAC_RXINFO_IPFRAG(x)               (((struct rx_info *)x)->opts1.bit.ipfrag)
#define GMAC_RXINFO_PPPOETAG(x)             (((struct rx_info *)x)->opts1.bit.pppoetag)
#define GMAC_RXINFO_RWT(x)                  (((struct rx_info *)x)->opts1.bit.rwt)
#define GMAC_RXINFO_DATA_LENGTH(x)          (((struct rx_info *)x)->opts1.bit.data_length)

#define GMAC_RXINFO_CPUTAG(x)               (((struct rx_info *)x)->opts2.bit.cputag)
#define GMAC_RXINFO_PTP_EXIST(x)            (((struct rx_info *)x)->opts2.bit.ptp_in_cpu_tag_exist)
#define GMAC_RXINFO_SVLAN_EXIST(x)          (((struct rx_info *)x)->opts2.bit.svlan_tag_exist)
#define GMAC_RXINFO_REASON(x)               (((struct rx_info *)x)->opts2.bit.reason)
#define GMAC_RXINFO_CTAGVA(x)               (((struct rx_info *)x)->opts2.bit.ctagva)
#define GMAC_RXINFO_CVLAN_TAG(x)            (((struct rx_info *)x)->opts2.bit.cvlan_tag)

#define GMAC_RXINFO_INTERNAL_PRIORITY(x)    (((struct rx_info *)x)->opts3.bit.internal_priority)
#define GMAC_RXINFO_PON_STREAM_ID(x)        (((struct rx_info *)x)->opts3.bit.pon_sid_or_extspa)
#define GMAC_RXINFO_EXTSPA(x)               (((struct rx_info *)x)->opts3.bit.pon_sid_or_extspa)
#define GMAC_RXINFO_L3ROUTING(x)            (((struct rx_info *)x)->opts3.bit.l3routing)
#define GMAC_RXINFO_ORIGFORMAT(x)           (((struct rx_info *)x)->opts3.bit.origformat)
#define GMAC_RXINFO_SRC_PORT_NUM(x)         (((struct rx_info *)x)->opts3.bit.src_port_num)
#define GMAC_RXINFO_FBI(x)                  (((struct rx_info *)x)->opts3.bit.fbi)
#define GMAC_RXINFO_DST_PORT_MASK(x)        (((struct rx_info *)x)->opts3.bit.fb_hash_or_dst_portmsk)
#define GMAC_RXINFO_FB_HASH(x)              (((struct rx_info *)x)->opts3.bit.fb_hash_or_dst_portmsk)

#define GMAC_RXINFO_PKTTYPE(x)              (0)/* No such field */
#define GMAC_RXINFO_PCTRL(x)                (0)/* No such field */
#define GMAC_RXINFO_EXT_PORT_TTL_1(x)       (0)/* No such field */

#define GMAC_TXINFO_OWN(x)                  (((struct tx_info *)x)->opts1.bit.own)
#define GMAC_TXINFO_EOR(x)                  (((struct tx_info *)x)->opts1.bit.eor)
#define GMAC_TXINFO_FS(x)                   (((struct tx_info *)x)->opts1.bit.fs)
#define GMAC_TXINFO_LS(x)                   (((struct tx_info *)x)->opts1.bit.ls)
#define GMAC_TXINFO_IPCS(x)                 (((struct tx_info *)x)->opts1.bit.ipcs)
#define GMAC_TXINFO_L4CS(x)                 (((struct tx_info *)x)->opts1.bit.l4cs)
#define GMAC_TXINFO_TPID_SEL(x)             (((struct tx_info *)x)->opts1.bit.tpid_sel)
#define GMAC_TXINFO_STAG_AWARE(x)           (((struct tx_info *)x)->opts1.bit.stag_aware)
#define GMAC_TXINFO_CRC(x)                  (((struct tx_info *)x)->opts1.bit.crc)
#define GMAC_TXINFO_DATA_LENGTH(x)          (((struct tx_info *)x)->opts1.bit.data_length)

#define GMAC_TXINFO_CPUTAG(x)               (((struct tx_info *)x)->opts2.bit.cputag)
#define GMAC_TXINFO_TX_SVLAN_ACTION(x)      (((struct tx_info *)x)->opts2.bit.tx_svlan_action)
#define GMAC_TXINFO_TX_CVLAN_ACTION(x)      (((struct tx_info *)x)->opts2.bit.tx_cvlan_action)
#define GMAC_TXINFO_TX_PMASK(x)             (((struct tx_info *)x)->opts2.bit.tx_portmask)
#define GMAC_TXINFO_CVLAN_VIDL(x)           (((struct tx_info *)x)->opts2.bit.cvlan_vidl)
#define GMAC_TXINFO_CVLAN_PRIO(x)           (((struct tx_info *)x)->opts2.bit.cvlan_prio)
#define GMAC_TXINFO_CVLAN_CFI(x)            (((struct tx_info *)x)->opts2.bit.cvlan_cfi)
#define GMAC_TXINFO_CVLAN_VIDH(x)           (((struct tx_info *)x)->opts2.bit.cvlan_vidh)

#define GMAC_TXINFO_ASPRI(x)                (((struct tx_info *)x)->opts3.bit.aspri)
#define GMAC_TXINFO_CPUTAG_PRI(x)           (((struct tx_info *)x)->opts3.bit.cputag_pri)
#define GMAC_TXINFO_KEEP(x)                 (((struct tx_info *)x)->opts3.bit.keep)
#define GMAC_TXINFO_DISLRN(x)               (((struct tx_info *)x)->opts3.bit.dislrn)
#define GMAC_TXINFO_CPUTAG_PSEL(x)          (((struct tx_info *)x)->opts3.bit.cputag_psel)
#define GMAC_TXINFO_GMAC_ID(x)              (((struct tx_info *)x)->opts3.bit.gmac_id)
#define GMAC_TXINFO_L34_KEEP(x)             (((struct tx_info *)x)->opts3.bit.l34_keep)
#define GMAC_TXINFO_EXTSPA(x)               (((struct tx_info *)x)->opts3.bit.extspa)
#define GMAC_TXINFO_TX_PPPOE_ACTION(x)      (((struct tx_info *)x)->opts3.bit.tx_pppoe_action)
#define GMAC_TXINFO_TX_PPPOE_IDX(x)         (((struct tx_info *)x)->opts3.bit.tx_pppoe_idx)
#define GMAC_TXINFO_TX_DST_STREAM_ID(x)     (((struct tx_info *)x)->opts3.bit.tx_dst_stream_id)

#define GMAC_TXINFO_LGSEN(x)                (((struct tx_info *)x)->opts4.bit.lgsen)
#define GMAC_TXINFO_LGMTU(x)                (((struct tx_info *)x)->opts4.bit.lgmtu)
#define GMAC_TXINFO_SVLAN_VIDL(x)           (((struct tx_info *)x)->opts4.bit.svlan_vidl)
#define GMAC_TXINFO_SVLAN_PRIO(x)           (((struct tx_info *)x)->opts4.bit.svlan_prio)
#define GMAC_TXINFO_SVLAN_CFI(x)            (((struct tx_info *)x)->opts4.bit.svlan_cfi)
#define GMAC_TXINFO_SVLAN_VIDH(x)           (((struct tx_info *)x)->opts4.bit.svlan_vidh)

/* Obsolete fields */
#define GMAC_TXINFO_BLU(x)                  /* No such field */
#define GMAC_TXINFO_VSEL(x)                 /* No such field */
#define GMAC_TXINFO_CPUTAG_IPCS(x)          /* No such field */
#define GMAC_TXINFO_CPUTAG_L4CS(x)          /* No such field */
#define GMAC_TXINFO_TX_VLAN_ACTION(x)       /* No such field */
#define GMAC_TXINFO_EFID(x)                 /* No such field */
#define GMAC_TXINFO_ENHANCE_FID(x)          /* No such field */
#define GMAC_TXINFO_VIDL(x)                 /* No such field */
#define GMAC_TXINFO_PRIO(x)                 /* No such field */
#define GMAC_TXINFO_CFI(x)                  /* No such field */
#define GMAC_TXINFO_VIDH(x)                 /* No such field */
#define GMAC_TXINFO_SB(x)                   /* No such field */
#define GMAC_TXINFO_PTP(x)                  /* No such field */

#define RX_ALL(idx)	\
	(SW_INT | RX_OK | RER_RUNT | RER_OVF \
	|((re_private_data[idx].rx_multiring_bitmap & 1) ? RDU : 0)\
	|(re_private_data[idx].rx_multiring_bitmap >> 1) << 11)

#define TX_ALL(idx)	\
	((re_private_data[idx].tx_multiring_bitmap << 16) | (re_private_data[idx].tx_multiring_bitmap << 24))

#ifdef CONFIG_RTL8686_SWITCH
#ifndef CONFIG_RTK_L34_ENABLE
#define RTL8686_Switch_Mode_Normal		0x00
#define RTL8686_Switch_Mode_Trap2Cpu	0x01
#endif
#endif

//#define KERNEL_SOCKET

#define RTK_NIC_TX_HASH_CB_NUMBER		    (sizeof(((struct sk_buff*)0)->cb) - 2)
#define RE8670_PPPOE_HDR_SIZE				8

#ifdef CONFIG_REALTEK_HW_LSO
#define MODE_PURE_SW 1
#define MODE_SG_GSO 3
#define MODE_HW_LSO 4
#define MODE_HW_LSO_SG 5

#define RE8670_HW_LSO_FS_TX_STUCK_LEN		102
#define RE8670_HW_LSO_FS_TX_STUCK_LEN_1		66

//#define GMAC_MODE MODE_PURE_SW //Choise Mode Here!!
//#define GMAC_MODE MODE_SG_GSO
#define GMAC_MODE MODE_HW_LSO_SG //Choise Mode Here!!

#define NIC_DESC_ACCELERATE_FOR_SG //tysu: many pages which have the continuious address are using the same descriptor, for accelerating.
//#undef NIC_DESC_ACCELERATE_FOR_SG

#if	GMAC_MODE==MODE_PURE_SW
#undef HW_CHECKSUM_OFFLOAD //tysu: for hardware checksum offload.
#undef LINUX_LSO_ENABLE //tysu: enable new version kernel lso(sw & hw LSO are required)
#undef LINUX_SG_ENABLE //tysu: enable kernel SG.
#undef HW_LSO_ENABLE //tysu: enable hw lso (new or old kernel are required)
#elif GMAC_MODE==MODE_SG_GSO
#undef HW_LSO_ENABLE
#define HW_CHECKSUM_OFFLOAD
#define LINUX_LSO_ENABLE
#define LINUX_SG_ENABLE
#elif GMAC_MODE==MODE_HW_LSO
#define HW_LSO_ENABLE
#define HW_CHECKSUM_OFFLOAD
#define LINUX_LSO_ENABLE
#undef LINUX_SG_ENABLE
#elif GMAC_MODE==MODE_HW_LSO_SG
#define HW_LSO_ENABLE
#define HW_CHECKSUM_OFFLOAD
#define LINUX_LSO_ENABLE
#define LINUX_SG_ENABLE
#endif

/* hardware minimum and maximum for a single frame's data payload */
#define CP_MIN_MTU		60	/* TODO: allow lower, but pad */

#ifdef HW_LSO_ENABLE
#define CP_LS_MTU		1500  //for LSO
#endif
#endif

#ifdef CONFIG_RTL865X_ETH_PRIV_SKB
#define RE8670_DYNAMIC_ALLOC_RXSKB_NUM 0//MAX_ETH_DYNAMIC_SKB_NUM
#define RE8670_PRE_ALLOC_RXSKB_NUM MAX_ETH_SKB_NUM
#define RE8670_MAX_ALLOC_RXSKB_NUM (RE8670_PRE_ALLOC_RXSKB_NUM+RE8670_DYNAMIC_ALLOC_RXSKB_NUM)
#else
#define RE8670_MAX_ALLOC_RXSKB_NUM 7680//(5120*MAX_GMAC_NUM)
#endif

#define RE8670_MAX_ALLOC_MC_NUM (RE8670_MAX_ALLOC_RXSKB_NUM-200)
#define RE8670_MAX_ALLOC_BC_NUM (RE8670_MAX_ALLOC_RXSKB_NUM-100)

#define RE8670_MAX_ETH_FRAME_SIZE			1700
#define RE8670_MAX_ETH_JUMBO_FRAME_SIZE		8000

#define SKB_BUF_SIZE  1600

#ifdef CONFIG_RG_JUMBO_FRAME
#define JUMBO_SKB_BUF_SIZE	(13312+2)		//IXIA max packet size.
#endif

#define GMAC_PRIORITY_NORMAL				0
#define GMAC_PRIORITY_MIDDLE				1
#define GMAC_PRIORITY_HIGH					2
#ifdef LOCAL_SERVICE_ACCELERATE
#define GMAC_PRIORITY_LOCAL_SERVICE_ACC		1
#endif
#define GMAC_PRIORITY_CRITICAL				7

#define GMAC_MAX_QUEUE_NUM					8

#define LXCBUS0_MASTER_REG		0xb8005210
#define LXCBUS0_SLAVE_REG		0xb8005220
#define LXCBUS1_MASTER_REG		0xb8005230
#define LXCBUS1_SLAVE_REG		0xb8005240

#define RTL_NUM_STATS		(sizeof(struct rtl_ethtool_stats)/sizeof(u64))

/*================================================
			GMAC used Enumeration
================================================*/
enum {
	/* NIC register offsets */
	IDR0 = 0,			/* Ethernet ID */
	IDR1 = 0x1,			/* Ethernet ID */
	IDR2 = 0x2,			/* Ethernet ID */
	IDR3 = 0x3,			/* Ethernet ID */
	IDR4 = 0x4,			/* Ethernet ID */
	IDR5 = 0x5,			/* Ethernet ID */
	MAR0 = 0x8,			/* Multicast register */
	MAR1 = 0x9,
	MAR2 = 0xa,
	MAR3 = 0xb,
	MAR4 = 0xc,
	MAR5 = 0xd,
	MAR6 = 0xe,
	MAR7 = 0xf,
	TXOKCNT=0x10,
	RXOKCNT=0x12,
	TXERR = 0x14,
	RXERRR = 0x16,
	MISSPKT = 0x18,
	FAE=0x1A,
	TX1COL = 0x1c,
	TXMCOL=0x1e,
	RXOKPHY=0x20,
	RXOKBRD=0x22,
	RXOKMUL=0x24,
	TXABT=0x26,
	TXUNDRN=0x28,
	RDUMISSPKT=0x2a,
	TRSR=0x34,
	COM_REG=0x38,
	CMD=0x3B,
	IMR=0x3C,
	ISR=0x3E,
	TCR=0x40,
	RCR=0x44,
	CPUtagCR=0x48,
	CONFIG_REG=0x4C,	
	CPUtag1CR=0x50,
	MSR=0x58,
	MIIAR=0x5C,
	SWINT_REG=0x60,
	VLAN_REG=0x64,
	VLAN1_REG=0x68,
	LEDCR=0x70,
	IMR0=0xd0,
	IMR1=0xd4,
	ISR1=0xd8,
	INTR_REG=0xdc,
	TxFDP1=0x1300,
	TxCDO1=0x1304,
    TxFDP2=0x1310, 
	TxCDO2=0x1314, 
	TxFDP3=0x1320, 
	TxCDO3=0x1324, 
	TxFDP4=0x1330, 
	TxCDO4=0x1334,
	TxFDP5=0x1340,
	TxCDO5=0x1344,
	RRING_ROUTING1=0x1370,
	RRING_ROUTING2=0x1374,
	RRING_ROUTING3=0x1378,
	RRING_ROUTING4=0x137c,
	RRING_ROUTING5=0x1380,
	RRING_ROUTING6=0x1384,
	RRING_ROUTING7=0x1388,
	RxFDP=0x13F0,
	RxCDO=0x13F4,	
	RxRingSize=0x13F6,
	SMSA=0x13FC,
	EthrntRxCPU_Des_Num=0x1430,
	EthrntRxCPU_Des_Wrap =	0x1431,
	Rx_Pse_Des_Thres = 	0x1432,	
	RxRingSize_h=0x13F7, 
	EthrntRxCPU_Des_Num_h=0x1433,
	EthrntRxCPU_Des_Wrap_h =0x1433,
	Rx_Pse_Des_Thres_h =0x142c,

	RST = (1<<0),
	RxChkSum = (1<<1),
	RxVLAN_Detag = (1<<2),
	RxJumboSupport = (1<<3),
	IO_CMD = 0x1434,
	IO_CMD1 = 0x1438,  // RLE0371 and other_platform set different value.
	RX_IntNum_Shift = 0x8,             /// ????
	TX_OWN = (1<<5),
	RX_OWN = (1<<4),
	MII_RE = (1<<3),
	MII_TE = (1<<2),
	TX_FNL = (1<<1),
	TX_FNH = (1),
	/*TX_START= MII_RE | MII_TE | TX_FNL | TX_FNH,
	TX_START = 0x8113c,*/
	RXMII = MII_RE,
	TXMII = MII_TE,
	LSO_F = (1<<28),
	EN_1GB = (1<<24),

	Rff_size_sel_2k = (0x2 << 28),
	Int_route_r4r5r6t2t4 = (0x00440540),
	Int_route_enable = (1<<25),
	En_int_split = (0x01 << 24),
		
	RxFDP2=0x1390,
	RxCDO2=0x1394,
	RxRingSize2=0x1396,
	RxRingSize_h2=0x1397,
	EthrntRxCPU_Des_Num2=0x1398,
	EthrntRxCPU_Des_Wrap2 =	0x139c,
	DIAG1_REG=0x1404,
};

enum RE8670_IOCMD_REG
{
	TX_PKT_TIMER_MASK = (0xf << 24),
	TX_PKT_TIMER_NONE = (0x0 << 24),
	TX_MIT_MASK 	= (0x7 << 16),
	TX_MIT_28PKT 	= (0x7 << 16),
	TX_MIT_24PKT 	= (0x6 << 16),
	TX_MIT_20PKT 	= (0x5 << 16),
	TX_MIT_16PKT 	= (0x4 << 16),
	TX_MIT_12PKT 	= (0x3 << 16),
	TX_MIT_8PKT 	= (0x2 << 16),
	TX_MIT_4PKT 	= (0x1 << 16),
	TX_MIT_1PKT 	= (0x0 << 16),
	RX_MIT 			= 7,
	RX_TIMER 		= 1,
	RX_FIFO 		= 2,
	TX_FIFO			= 1,
	TX_MIT			= 7,
	TX_POLL4		= (1 << 3),
	TX_POLL3		= (1 << 2),
	TX_POLL2		= (1 << 1),
	TX_POLL			= (1 << 0),
};

enum RE8670_IOCMD1_REG
{	
	TX_en_precise_dma = (1 << 27),
	TX_RR_scheduler	= (1 << 14),
	TX_POLL5		= (1 << 8),
	txq5_h			= (1 << 4),
	txq4_h			= (1 << 3),
	txq3_h			= (1 << 2),
	txq2_h			= (1 << 1),
	txq1_h			= (1 << 0),
};

enum{
	TXD_VLAN_INTACT,
	TXD_VLAN_INSERT,
	TXD_VLAN_REMOVE,
	TXD_VLAN_REMARKING,
};

enum {
	RE8670_RX_STOP=0,
	RE8670_RX_CONTINUE,
	RE8670_RX_STOP_SKBNOFREE,
	RE8670_RX_END
};

/*
*   | callback                      	| priority		| return
*   | re8670_rx_skb                 	| 0			| STOP
*   | fwdEngine_rx_skb      		| 1			| STOP
*   | rtk_gpon_omci_rx_wrapper	| 2			| STOP
*   | re8686_dump_rx			| 6			| IS CONTINUE
*   | re8686_rx_patch			| 7			| IS CONTINUE
*/

typedef enum {
	RE8686_RXPRI_DEFAULT=0,
	RE8686_RXPRI_RG,
	RE8686_RXPRI_L34LITE,
	RE8686_RXPRI_VOIP,
	RE8686_RXPRI_OMCI,
	RE8686_RXPRI_OAM,
	RE8686_RXPRI_MPCP,
	RE8686_RXPRI_MUTICAST,
	RE8686_RXPRI_DUMP,
	RE8686_RXPRI_PATCH,	
	RE8686_RXPRI_NPTV6_FF,
}RE8686_RX_PRI_T;

enum PHY_REGS{
	FORCE_TX = (1<<7),
	RXFCE = (1<<6),
	TXFCE = (1<<5),
	SP1000 = (1<<4),
	SP10 = (1<<3),
	LINK = (1<<2),
	TXPF = (1<<1),
	RXPF = (1<<0),
};

enum RE8670_STATUS_REGS
{
	/*TX/RX share */
	DescOwn 	= (1 << 31), /* Descriptor is owned by NIC */
	RingEnd 	= (1 << 30), /* End of descriptor ring */
	FirstFrag 	= (1 << 29), /* First segment of a packet */
	LastFrag 	= (1 << 28), /* Final segment of a packet */

	/*Tx descriptor opt1*/
	IPCS		= (1 << 27),
	L4CS		= (1 << 26),
	KEEP		= (1 << 25),
	BLU			= (1 << 24),
	TxCRC		= (1 << 23),
	VSEL		= (1 << 22),
	DisLrn		= (1 << 21),
	CPUTag_ipcs = (1 << 20),
	CPUTag_l4cs	= (1 << 19),

	/*Tx descriptor opt2*/
	CPUTag		= (1 << 31),
	aspri		= (1 << 30),
	CPRI		= (1 << 27),
	TxVLAN_int	= (0 << 25),  //intact
	TxVLAN_ins	= (1 << 25),  //insert
	TxVLAN_rm	= (2 << 25),  //remove
	TxVLAN_re	= (3 << 25),  //remark
	//TxPPPoEAct	= (1 << 23),
	TxPPPoEAct	= 23,
	//TxPPPoEIdx	= (1 << 20),
	TxPPPoEIdx	= 20,
	Efid			= (1 << 19),
	//Enhan_Fid	= (1 << 16),
	Enhan_Fid 	= 16,
	/*Tx descriptor opt3*/
	SrcExtPort	= 29,
	TxDesPortM	= 23,
	TxDesStrID 	= 16,
	TxDesVCM	= 0,	
	TxPsel		= (1 << 20),
	/*Tx descriptor opt4*/
	/*Rx descriptor  opt1*/
	CRCErr		= (1 << 27),
	IPV4CSF		= (1 << 26),
	L4CSF		= (1 << 25),
	RCDF		= (1 << 24),
	IP_FRAG		= (1 << 23),
	PPPoE_tag	= (1 << 22),
	RWT			= (1 << 21),
	PktType		= (1 << 17),
	RxProtoIP	= 1,
	RxProtoPPTP	= 2,
	RxProtoICMP	= 3,
	RxProtoIGMP	= 4,
	RxProtoTCP	= 5,   
	RxProtoUDP	= 6,
	RxProtoIPv6	= 7,
	RxProtoICMPv6	= 8,
	RxProtoTCPv6	= 9,
	RxProtoUDPv6	= 10,
	L3route		= (1 << 16),
	OrigFormat	= (1 << 15),
	PCTRL		= (1 << 14),
	/*Rx descriptor opt2*/
	PTPinCPU	= (1 << 30),
	SVlanTag		= (1 << 29),
	/*Rx descriptor opt3*/
	SrcPort		= (1 << 27),
	DesPortM	= (1 << 21),
	Reason		= (1 << 13),
	IntPriority	= (1 << 10),
	ExtPortTTL	= (1 << 5),
};

enum RE8670_THRESHOLD_REGS{
	//shlee	THVAL		= 2,
	TH_ON_VAL = 0x10,	//shlee flow control assert threshold: available desc <= 16
	TH_OFF_VAL= 0x30,	//shlee flow control de-assert threshold : available desc>=48
	//	RINGSIZE	= 0x0f,	//shlee 	2,
	LOOPBACK	= (0x3 << 8),
	AcceptErr	= 0x20,	     /* Accept packets with CRC errors */
	AcceptRunt	= 0x10,	     /* Accept runt (<64 bytes) packets */
	AcceptBroadcast	= 0x08,	     /* Accept broadcast packets */
	AcceptMulticast	= 0x04,	     /* Accept multicast packets */
	AcceptMyPhys	= 0x02,	     /* Accept pkts with our MAC as dest */
	AcceptAllPhys	= 0x01,	     /* Accept all pkts w/ physical dest */
	AcceptAll = AcceptBroadcast | AcceptMulticast | AcceptMyPhys |  AcceptAllPhys | AcceptErr | AcceptRunt,
	AcceptNoBroad = AcceptMulticast |AcceptMyPhys |  AcceptAllPhys | AcceptErr | AcceptRunt,
	AcceptNoMulti =  AcceptMyPhys |  AcceptAllPhys | AcceptErr | AcceptRunt,
	NoErrAccept = AcceptBroadcast | AcceptMulticast | AcceptMyPhys,
	NoErrPromiscAccept = AcceptBroadcast | AcceptMulticast | AcceptMyPhys |  AcceptAllPhys,
};

enum RE8670_IMR_REGS{
	IMR_TDU_EN		= (1 << 9),
	IMR_TOK_TI		= (1 << 6)
};

enum RE8670_IMR0_REGS{
	IMR0_TDU5		= (1 << 28),
	IMR0_TDU4		= (1 << 27),
	IMR0_TDU3		= (1 << 26),
	IMR0_TDU2		= (1 << 25),
	IMR0_TDU		= (1 << 24),
	IMR0_TDU_ALL	= (IMR0_TDU|IMR0_TDU2|IMR0_TDU3|IMR0_TDU4|IMR0_TDU5),
	IMR0_TOK5		= (1 << 20),
	IMR0_TOK4		= (1 << 19),
	IMR0_TOK3		= (1 << 18),
	IMR0_TOK2		= (1 << 17),
	IMR0_TOK		= (1 << 16),
	IMR0_TOK_ALL	= (IMR0_TOK|IMR0_TOK2|IMR0_TOK3|IMR0_TOK4|IMR0_TOK5)
};

enum RE8670_IMR1_REGS{
	IMR1_TDU5		= (1 << 28),
	IMR1_TDU4		= (1 << 27),
	IMR1_TDU3		= (1 << 26),
	IMR1_TDU2		= (1 << 25),
	IMR1_TDU		= (1 << 24),
	IMR1_TOK5		= (1 << 20),
	IMR1_TOK4		= (1 << 19),
	IMR1_TOK3		= (1 << 18),
	IMR1_TOK2		= (1 << 17),
	IMR1_TOK		= (1 << 16)
};

enum RE8670_ISR_REGS{
	RDU6		= (1 << 15),
	RDU5		= (1 << 14),
	RDU4		= (1 << 13),
	RDU3		= (1 << 12),
	RDU2		= (1 << 11),
	SW_INT 		= (1 <<10),
	TDU			= (1 << 9),
	LINK_CHG	= (1 <<	8),
	TER			= (1 << 7),
	TOK			= (1 << 6),
	RDU			= (1 << 5),
	RER_OVF		= (1 << 4),
	RER_RUNT	= (1 << 2),
	RX_OK		= (1 << 0),
	RDU_ALL 	= (RDU | RDU2 | RDU3 | RDU4 | RDU5 | RDU6)
};

#define RX_RDU_CHECK(x) (((x&(RDU2|RDU3|RDU4|RDU5|RDU6))>>11<<1)|(((x&RDU)>>5)&1))

enum RE8670_ISR1_REGS{
	ISR1_TDU5		= (1 << 28),
	ISR1_TDU4		= (1 << 27),
	ISR1_TDU3		= (1 << 26),
	ISR1_TDU2		= (1 << 25),
	ISR1_TDU		= (1 << 24),
	ISR1_TDU_ALL	= (ISR1_TDU|ISR1_TDU2|ISR1_TDU3|ISR1_TDU4|ISR1_TDU5),
	ISR1_TOK5		= (1 << 20),
	ISR1_TOK4		= (1 << 19),
	ISR1_TOK3		= (1 << 18),
	ISR1_TOK2		= (1 << 17),
	ISR1_TOK		= (1 << 16),
	ISR1_TOK_ALL	= (ISR1_TOK|ISR1_TOK2|ISR1_TOK3|ISR1_TOK4|ISR1_TOK5)
};

enum RTL8672GMAC_CPUtag_Control
{
	CTEN_RX     = (1<<31),
	CT_TSIZE	= 27,
	CT_DSLRN	= (1 << 24),
	CT_NORMK	= (1 << 23),
	CT_ASPRI	= (1 << 22),
	CT_APPLO_PRO	= (8 << 18),

	CT_RSIZE_H = 25,
	CT_RSIZE_L = 16,
	CTPM_8306   = (0xf0 << 8),
	CTPM_8368   = (0xe0 << 8),
	CTPM_8370   = (0xff << 8),
	CTPM_8307   = (0xff << 8),
	CTPV_8306   = 0x90,
	CTPV_8368   = 0xa0,
	CTPV_8370   = 0x04,
	CTPV_8307	  = 0x04,
};

enum RTL8672GMAC_CPUtag1_Control
{
	CT1_SID     = (64<<8)

};

enum RTL8672GMAC_PG_REG
{
	EN_PGLBK     = (1<<15),
	DATA_SEL     = (1<<14),
	LEN_SEL      = (1<<11),
	NUM_SEL      = (1<<10),
};

typedef enum
{
	FLAG_WRITE		= (1<<31),
	FLAG_READ		= (0<<31),

	MII_PHY_ADDR_SHIFT	= 26, 
	MII_REG_ADDR_SHIFT	= 16,
	MII_DATA_SHIFT		= 0,
}MIIAR_MASK;

enum RTL8686GMAC_DEBUG_LEVEL{
	RTL8686_PRINT_NOTHING = 0,
	RTL8686_SKB_RX = (1<<0),
	RTL8686_SKB_TX = (1<<1),
	RTL8686_RXINFO = (1<<2),
	RTL8686_TXINFO = (1<<3),
	RTL8686_RX_TRACE = (1<<4),
	RTL8686_TX_TRACE = (1<<5),	
	RTL8686_RX_WARN = (1<<6),	
	RTL8686_TX_WARN = (1<<7),
	RTL8686_OTHERS = (1<<8),
	RTL8686_RX_ALL = (RTL8686_SKB_RX|RTL8686_RXINFO|RTL8686_RX_TRACE),
	RTL8686_TX_ALL = (RTL8686_SKB_TX|RTL8686_TXINFO|RTL8686_TX_TRACE),
};

enum RTL8686_IFFLAG{
	RTL8686_ELAN = 0,
	RTL8686_WAN = 1,
	RTL8686_WLAN = 2,
	RTL8686_SVAP = 3,		// Luke: slave VAP interface, should not be sent by bridge
};	
#ifdef HWNAT_CUSTOMIZE	
enum RTL8686_customize_type{	
	CUSTOMIZE_TYPE_NONE=0,	
	CUSTOMIZE_TYPE_NPTV6_UP=1,	
	CUSTOMIZE_TYPE_NPTV6_DOWN=2,	
	CUSTOMIZE_TYPE_VXLAN_UP=3,	
	CUSTOMIZE_TYPE_VXLAN_DOWN=4,	
	CUSTOMIZE_TYPE_MAX=5,	
};	
#endif

enum RTL8686_MEM_DUMP_FORMAT{
	RTL8686_MEM_DUMP_FORMAT_ADDR = (1<<0),
	RTL8686_MEM_DUMP_FORMAT_HEX = (1<<1),
	RTL8686_MEM_DUMP_FORMAT_TEXT = (1<<2),
	RTL8686_MEM_DUMP_FORMAT_ALL = (RTL8686_MEM_DUMP_FORMAT_ADDR | RTL8686_MEM_DUMP_FORMAT_HEX | RTL8686_MEM_DUMP_FORMAT_TEXT),
};

/*================================================
			GMAC used Structure
================================================*/
struct rx_info{
	union{
		struct{
			u32 own:1;//31
			u32 eor:1;//30
			u32 fs:1;//29
			u32 ls:1;//28
			u32 crcerr:1;//27
			u32 ipv4csf:1;//26
			u32 l4csf:1;//25
			u32 rcdf:1;//24
			u32 ipfrag:1;//23
			u32 pppoetag:1;//22
			u32 rwt:1;//21
			u32 rsvd1:7;//14~17
#ifdef CONFIG_RG_JUMBO_FRAME
			u32 data_length:14;//0~13
#else
			u32 rsvd2:2;//12~13
			u32 data_length:12;//0~11
#endif
		}bit;
		u32 dw;//double word
	}opts1;
	u32 addr;
	union{
		struct{
			u32 cputag:1;//31
			u32 ptp_in_cpu_tag_exist:1;//30
			u32 svlan_tag_exist:1;//29
			u32 reason:8;//21~28
			u32 rsvd_1:4;//17~20
			u32 ctagva:1;//16
			u32 cvlan_tag:16;//0~15
		}bit;
		u32 dw;//double word
	}opts2;
	union{
		struct{
			u32 internal_priority:3;//29~31
			u32 pon_sid_or_extspa:7;//22~28 or 26~28
			u32 l3routing:1;//21
			u32 origformat:1;//20
			u32 src_port_num:4;//16~19
			u32 fbi:1;//15
			u32 fb_hash_or_dst_portmsk:15;//0~14 or 0~6
		}bit;
		struct{
			u32 internal_priority:3;//29~31
			u32 extspa:3;//26~28
			u32 rsvd_1:4;//22~25
			u32 l3routing:1;//21
			u32 origformat:1;//20
			u32 src_port_num:4;//16~19
			u32 fbi:1;//15
			u32 fb_hash_or_dst_portmsk:15;//0~14 or 0~6
		}bit1;
		u32 dw;//double word
	}opts3;
};

struct tx_info{
	union{
		struct{
			u32 own:1;//31
			u32 eor:1;//30
			u32 fs:1;//29
			u32 ls:1;//28
			u32 ipcs:1;//27
			u32 l4cs:1;//26
			u32 tpid_sel:1;//25
			u32 stag_aware:1;//24		
			u32 crc:1;//23
			u32 rsvd:6;//17~22
			u32 data_length:17;//0~16
		}bit;
		u32 dw;//double word
	}opts1;
	u32 addr;
	union{
		struct{
			u32 cputag:1;//31
			u32 tx_svlan_action:2;//29~30
			u32 tx_cvlan_action:2;//27~28
			u32 tx_portmask:11;//16~26
			u32 cvlan_vidl:8;//8~15
			u32 cvlan_prio:3;//5~7
			u32 cvlan_cfi:1;// 4
			u32 cvlan_vidh:4;//0~3
		}bit;
		u32 dw;//double word
	}opts2;
	union{
		struct{
			u32 rsvd1:4;//28~31
			u32 aspri:1;//27
			u32 cputag_pri:3;//24~26
			u32 keep:1;//23
			u32 rsvd2:1;//22
			u32 dislrn:1;//21
			u32 cputag_psel:1;//20
        	u32 gmac_id:2;//18~19  //software used for gmac_tx_idx(0:gmac9, 1:gmac10, 2:gmac7)
			u32 l34_keep:1;//17
			u32 rsvd3:1;//16
			u32 extspa:3;//13~15
			u32 tx_pppoe_action:2;//11~12
			u32 tx_pppoe_idx:4;//7~10
			u32 tx_dst_stream_id:7;//0~6
		}bit;
		u32 dw;//double word
	}opts3;
	union{
		struct{
			u32 lgsen:1;//31
			u32 lgmtu:11;//20~30
			u32 rsvd:4;//16~19
			u32 svlan_vidl:8;//8~15
			u32 svlan_prio:3;//5~7
			u32 svlan_cfi:1;// 4
			u32 svlan_vidh:4;//0~3
		}bit;
		u32 dw;//double word
	}opts4;	
#define tx_fs					opts1.bit.fs	
#define tx_ls					opts1.bit.ls	
#define tx_ipcs					opts1.bit.ipcs	
#define tx_l4cs					opts1.bit.l4cs	
#define tx_tpid_sel				opts1.bit.tpid_sel	
#define tx_stag_aware			opts1.bit.stag_aware	
#define tx_data_length			opts1.bit.data_length	
#define tx_cputag				opts2.bit.cputag	
#define tx_tx_svlan_action		opts2.bit.tx_svlan_action	
#define tx_tx_cvlan_action		opts2.bit.tx_cvlan_action	
#define tx_tx_portmask			opts2.bit.tx_portmask	
#define tx_cvlan_vidl			opts2.bit.cvlan_vidl	
#define tx_cvlan_prio			opts2.bit.cvlan_prio	
#define tx_cvlan_cfi			opts2.bit.cvlan_cfi	
#define tx_cvlan_vidh			opts2.bit.cvlan_vidh	
#define tx_aspri				opts3.bit.aspri	
#define tx_cputag_pri			opts3.bit.cputag_pri	
#define tx_keep					opts3.bit.keep	
#define tx_dislrn				opts3.bit.dislrn	
#define tx_cputag_psel			opts3.bit.cputag_psel	
#define tx_l34_keep				opts3.bit.l34_keep				//direct tx	
#define tx_gmac_id				opts3.bit.gmac_id				//software used for gmac_tx_idx(0:gmac9, 1:gmac10)	
#define tx_extspa				opts3.bit.extspa	
#define tx_tx_pppoe_action		opts3.bit.tx_pppoe_action	
#define tx_tx_pppoe_idx			opts3.bit.tx_pppoe_idx	
#define tx_tx_dst_stream_id		opts3.bit.tx_dst_stream_id	
#define tx_svlan_vidl			opts4.bit.svlan_vidl	
#define tx_svlan_prio			opts4.bit.svlan_prio	
#define tx_svlan_cfi			opts4.bit.svlan_cfi	
#define tx_svlan_vidh			opts4.bit.svlan_vidh
};

typedef struct dma_tx_desc {
	u32		opts1;
	u32		addr;
	u32		opts2;
	u32		opts3; //cputag
	u32		opts4; //lso
}DMA_TX_DESC;

typedef struct dma_rx_desc {
	u32		opts1;
	u32		addr;
	u32		opts2;
	u32		opts3;
}DMA_RX_DESC;

struct cp_extra_stats {
	u32 rx_frags;
	u32 tx_timeouts;
	//krammer add for rx info
	u32 rx_hw_num;
	u32 rx_sw_num;
	u32 rer_runt;
	u32 rer_ovf;
	u32 rdu;
	u32 tok;
	u32 tok_free_skb[MAX_TXRING_NUM];
	u32 tdu;
#ifdef TX_KICK_RING_USING_TDU_INT
	u32 tdu_kick_ring_intr[MAX_TXRING_NUM];
	u32 tdu_kick_ring_retry[MAX_TXRING_NUM];
#endif
#ifdef TX_KICK_RING_USING_POLLING
	u32 tdu_kick_ring_poll[MAX_TXRING_NUM];
	u32 tdu_poll[MAX_TXRING_NUM];
#endif
	u32 frag;
#ifdef CONFIG_RG_JUMBO_FRAME
	u32 toobig;
#endif
	u32 crcerr;
	u32 rcdf;
	u32 rx_no_mem;
	//krammer add for tx info
	u32 tx_sw_num;
	u32 tx_hw_num;
	u32 tx_no_desc;
	u32 rx_critical_num;
	u32 rx_critical_drop_num;
#ifdef RX_NAPI_MODE
	u32 rx_napi_poll;
	u32 rx_napi_interrupt;
#endif
#ifdef RX_NAPI_MODE_DEBUG
	u32 rx_napi_gro_normal;
	u32 rx_napi_gro_drop;
	u32 rx_napi_gro_merged_free;
	u32 rx_napi_gro_held;
	u32 rx_napi_gro_merged;
#endif
	u32 tooshort;	
#ifdef HWNAT_CUSTOMIZE	
	u32 rx_customized_forward;
	u32 rx_customized_copied;
	u32 rx_customized_tx_owned;
	u32 rx_customized_rx_owned;
	u32 rx_customized_tx_own_waiting_times;
#endif
	u32 tx_hw_num_ring[MAX_TXRING_NUM];
};

#ifdef TX_RING_DEBUG
#define TX_RING_DEBUG_BUFFER_SIZE	3000
typedef struct tx_data_buffter {
	unsigned char buffer[TX_RING_DEBUG_BUFFER_SIZE];
}TX_DATA_BUFF;
typedef struct rtl8686_tx_ring_debug_entry {
	DMA_TX_DESC *txDescriptor;
	TX_DATA_BUFF *dataBuffer;
}TX_RING_DEBUG_T;
#endif

#ifdef TX_BURST_PACKET_SEND
typedef struct burst_tx_info_s
{
	struct re_private *cp;
	int ringNum;
	struct tx_info txInfo;

	unsigned char *burst_buffer;
	unsigned int burst_index;
	int burst_number;	//-1:infinite

	int burst_sentPacketCount;	
	int burst_packetNumberPerSecond;
	unsigned int burst_delay;

	struct tasklet_struct burst_tasklets;
}burst_tx_info_t;
#endif

struct re_private {
	u32		gmac;
	spinlock_t		tx_lock;
	spinlock_t		rx_lock;

#ifdef TX_RING_DEBUG
	TX_RING_DEBUG_T rtl8686_tx_ring_debug[MAX_TXRING_NUM];
#endif

    DMA_RX_DESC     *rx_Mring[MAX_RXRING_NUM];
    u32		rx_Mtail[MAX_RXRING_NUM];
    u8		*rxdesc_Mbuf[MAX_RXRING_NUM];

    DMA_TX_DESC		*tx_Mhqring[MAX_TXRING_NUM];
	u8				*txdesc_Mbuf[MAX_TXRING_NUM];
	u32		tx_Mhqhead[MAX_TXRING_NUM];
	u32		tx_Mhqtail[MAX_TXRING_NUM];
	u32		last_TxCDO[MAX_TXRING_NUM];

	struct ring_info	*tx_skb[MAX_TXRING_NUM];
	struct ring_info	*rx_skb[MAX_RXRING_NUM];	
	
	u32		rx_buf_sz;
	//dma_addr_t		ring_dma;

	struct cp_extra_stats	cp_stats;
	u32 isr_status;
	u32 isr1_status;
	u32 isr1_status_tx;

	//struct pci_dev		*pdev;

	struct sk_buff		*frag_skb;

	struct tasklet_struct rx_tasklets;
#ifdef TX_INTR_HANDLE
	struct tasklet_struct tx_tok_tasklets_ring[MAX_TXRING_NUM];
#ifdef TX_KICK_RING_USING_TDU_INT
	struct tasklet_struct tx_tdu_kick_tasklets;
#endif
#endif
#ifdef TX_RECYCLE_SKB_USING_POLLING
	struct timer_list	tok_polling_timer;
#endif
#ifdef TX_KICK_RING_USING_POLLING
	u32 tx_ring_active_poll_enabled;
	u32 tx_tdu_poll_status[MAX_TXRING_NUM];
	u32 last_tx_hw_num_ring[MAX_TXRING_NUM];
	struct timer_list tx_ring_active_polling_timer;
	struct timer_list tx_tdu_kick_polling_timer;
#endif
	struct net_device* port2dev[SW_PORT_NUM];
	int (*port2rxfunc[SW_PORT_NUM])(struct re_private *cp, struct sk_buff *skb, struct rx_info *pRxInfo);

#ifdef CONFIG_RG_JUMBO_FRAME
	struct sk_buff *jumboFrame;
	u16 jumboLength;
#endif
	u32 stag_pid;
	u32 stag_pid1;

	u32 gmac_enabled;
	void __iomem	*base;
	int				irq; 
	u32 gmac_cpu_port;
	u32 re8670_rx_ring_size[MAX_RXRING_NUM];
	u32 re8670_tx_ring_size[MAX_TXRING_NUM];
	u8 re8670_rx_flow_control_status[MAX_RXRING_NUM];
	u32 rx_multiring_bitmap;
	u32 tx_multiring_bitmap;
	u32 rx_only_ring1;
	u32 rx_not_only_ring1;
	u32 iocmd_reg;
	u32 iocmd1_reg;
	u32 tx_ring_show_bitmap;
	u32 rx_ring_show_bitmap;
	u32 tx_jumbo_frame_enabled;
	u32 rx_save_int_prio_to_skb;
	u32 debug_enable;
	u32 debug_times;
	u32 padding_enable;
	u32 tx_ring_backup_debug;
#ifdef RX_NAPI_MODE
	u32 napi_budget;
	struct napi_struct napi;
#endif
	u32 tx_int_mitigation;
	u32 tx_int_mitigation_timer;
	u32 test_packet_start;
	u32 local_service_acc_enable;
#ifdef TX_CREATE_TEST_PACKET_DEBUG
	u8 test_packet[JUMBO_SKB_BUF_SIZE];
#endif
	u8 pauseBySwRingBitmap;
	u8 eth_close;
#ifdef TX_BURST_PACKET_SEND
	burst_tx_info_t burst_tx_info;
#endif
#ifdef CONFIG_RTL8192CD
	u8 wifi_tx_qos_enable;
	u8 wifi_tx_qos_mapping[GMAC_MAX_QUEUE_NUM];
#endif
	u32 rx_buff_size;
};

/* Statistics counters collected by the MAC */
struct rtl_ethtool_stats {
	u64 rxucpktcnt;
	u64 rxmcfrmcnt;
	u64 rxbcfrmcnt;
	u64 rxoamfrmcnt;
	u64 rxjumbofrmcnt;
	u64 rxpausefrmcnt;
	u64 rxunknownocfrmcnt;
	u64 rxcrcerrfrmcnt;
	u64 rxundersizefrmcnt;
	u64 rxruntfrmcnt;
	u64 rxovsizefrmcnt;
	u64 rxjabberfrmcnt;
	u64 rxinvalidfrmcnt;
	u64 rxstatsfrm64oct;
	u64 rxstatsfrm65to127oct;
	u64 rxstatsfrm128to255oct;
	u64 rxstatsfrm256to511oct;
	u64 rxstatsfrm512to1023oct;
	u64 rxstatsfrm1024to1518oct;
	u64 rxstatsfrm1519to2100oct;
	u64 rxstatsfrm2101to9200oct;
	u64 rxstatsfrm9201tomaxoct;
	u64 rxbytecount_lo;
	u64 rxbytecount_hi;
	u64 txucpktcnt;
	u64 txmcfrmcnt;
	u64 txbcfrmcnt;
	u64 txoamfrmcnt;
	u64 txjumbofrmcnt;
	u64 txpausefrmcnt;
	u64 txcrcerrfrmcnt;
	u64 txovsizefrmcnt;
	u64 txsinglecolfrm;
	u64 txmulticolfrm;
	u64 txlatecolfrm;
	u64 txexesscolfrm;
	u64 txstatsfrm64oct;
	u64 txstatsfrm65to127oct;
	u64 txstatsfrm128to255oct;
	u64 txstatsfrm256to511oct;
	u64 txstatsfrm512to1023oct;
	u64 txstatsfrm1024to1518oct;
	u64 txstatsfrm1519to2100oct;
	u64 txstatsfrm2101to9200oct;
	u64 txstatsfrm9201tomaxoct;
	u64 txbytecount_lo;
	u64 txbytecount_hi;
};

struct re_private_root {
	struct net_device	*dev;
	struct re_private	*re_private_data_ptr[MAX_GMAC_NUM];
	spinlock_t 			stats_lock;
	u32					msg_enable;
	u32 				gmac_enable_mask;
#ifdef CONFIG_RTL8686_SWITCH
	u8					rx_pause_by_software_bitmap;
	u8					rx_pause_by_software_enable;
	struct timer_list 	rx_pause_by_software_interrupt_timer;
#endif
	u8 					skb_dynamic_allocate_disable;		//0:alloc dynamic if need 1:don't alloc anymore
#if defined(CONFIG_RTL_ETH_RECYCLED_SKB)
	u8					recycle_skb_pool_id;
#endif
	int (*txfunc)(struct sk_buff *skb, struct net_device *dev);
};

struct rtl8686_dev_table_entry {
	u8 			ifname[IFNAMSIZ];
	u8			ifflag; //0:ELAN, 1:WAN, 2:WLAN
	u16			vid;
	u32			phyPort;
	u8 			isPanelPort;
	struct net_device *dev_instant;
};
struct rtl8686_hwnat_customized_entry {
	unsigned char valid;
	unsigned char gmac;
	unsigned char rxRingNum;
	unsigned char txRingNum;
	
	unsigned char rxPreLen;
	unsigned char txPreLen;
	char txInfo_addr_offset_v1;
	char txInfo_addr_offset_v2;
	unsigned char type; 		//refer to RTL8686_customize_type
	unsigned int  rx_ext_pmsk;
	
	struct tx_info *pCustomized_txInfo;
};

/*================================================
			Function Prototype
================================================*/
//must be same with port2rxfunc define in cp
#if defined(CONFIG_LUNA_G3_SERIES)
#include "ca_ni.h"			// for p2rfunc_t
#elif defined(CONFIG_RTL9607C_SERIES)
typedef int (*p2rfunc_t)(struct re_private *cp, struct sk_buff *skb, struct rx_info *pRxInfo);
#endif
typedef int (*tfunc_t)(struct sk_buff *skb, struct net_device *dev);
typedef int (*customized_rxPrepare_t)(struct re_private *cp, struct rx_info *pRxInfo);
typedef int (*customized_txPrepare_t)(struct re_private *cp, struct tx_info *pTxInfo);
typedef int (*customized_rxHook_t)(struct re_private *cp, struct rx_info *pRxInfo, int len);
typedef int (*customized_txHook_t)(struct re_private *cp, struct tx_info *pTxInfo, int len);

struct net_device* nic_decide_rx_device_by_spa(unsigned int phy_src_port);
int re8686_register_rxfunc_all_port(p2rfunc_t pfunc);
int re8686_send_with_txInfo(struct sk_buff *skb, struct tx_info* ptxInfo, int ring_num);
#if defined(CONFIG_RTK_NIC_HWLOOKUP_DEAMSDU_OFFLOAD)
int re8686_wifi_hwlookup_deamsdu(struct sk_buff *skb, struct tx_info* ptxInfo, int ring_num);
#endif
bool rtk_nic_isPhyDev(struct net_device *dev);
int re8686_send_with_txInfo_and_mask(struct sk_buff * skb,struct tx_info * ptxInfo,int ring_num,struct tx_info * ptxInfoMask);
int re8686_send_with_txInfo_and_mask_burst(char *pktbuff,int len,struct tx_info * ptxInfo,int ring_num,struct tx_info * ptxInfoMask,int BustSzie,int nicSendRate);
void re8686_reset_rxfunc_to_default(unsigned int gmac);
int re8670_rx_skb (struct re_private *cp, struct sk_buff *skb, struct rx_info *pRxInfo);
int re8686_set_vlan_register(struct tx_info* ptxInfo, unsigned char reg_num, unsigned int value);
int re8686_get_vlan_register(struct tx_info* ptxInfo, unsigned char reg_num, unsigned int *value_p);
struct sk_buff *re8670_getAlloc(unsigned int size);
int re8670_host_speedup (struct re_private *cp, struct sk_buff *skb, struct rx_info *pRxInfo);
#ifdef HWNAT_CUSTOMIZE
void re8686_customized_dualTx(struct re_private *cp, unsigned int len, unsigned int ringNum);
void re8686_customized_quickTx(struct re_private *cp, unsigned int len, unsigned int ringNum);
void re8686_customized_tx(struct re_private *cp, unsigned int len, unsigned int ringNum, struct tx_info *pTxInfo, char addr_offset);
void _hwnat_customized_version_set(int version);
#endif
int re8686_customized_rx_and_tx(struct rtl8686_hwnat_customized_entry customized_entry, customized_rxPrepare_t rxPrepareFunc, customized_txPrepare_t txPrepareFunc, customized_rxHook_t rxHookFunc, customized_txHook_t txHookFunc);
int re8686_customized_tx_stream_id(unsigned int gmac, unsigned int txRingNum, unsigned char streamID);
#if defined(CONFIG_RTL9607C_SERIES)
#define MAX_DYNAMIC_SRAM_SIZE 4
typedef enum rtk_dynamic_sram_size_s
{
	RTK_DYNAMIC_SRAM_MIN_BYTES=0,
	RTK_DYNAMIC_SRAM_256_BYTES,
	RTK_DYNAMIC_SRAM_512_BYTES,
	RTK_DYNAMIC_SRAM_1K_BYTES,
	RTK_DYNAMIC_SRAM_2K_BYTES,
	RTK_DYNAMIC_SRAM_4K_BYTES,
	RTK_DYNAMIC_SRAM_8K_BYTES,
	RTK_DYNAMIC_SRAM_16K_BYTES,
	RTK_DYNAMIC_SRAM_32K_BYTES,
	RTK_DYNAMIC_SRAM_MAX_BYTES
}rtk_dynamic_sram_size_t;
void rtk_dynamic_sram_state_set(rtk_enable_t state);
rtk_enable_t rtk_dynamic_sram_state_get(void);
int rtk_dynamic_sram_set(uint32 index, rtk_enable_t state, void *addr, rtk_dynamic_sram_size_t sram_size, uint32 offset);
int rtk_dynamic_sram_get(uint32 index, rtk_enable_t *state, uint32 *addrValue, rtk_dynamic_sram_size_t *sram_size, uint32 *offset);
void rtk_dynamic_sram_restart(uint32 gmac);
#endif

struct re_dev_private {
	struct re_private_root* pCp;
	struct rtnl_link_stats64 net_stats;
	u32 txPortMask;
};

static inline u32 is_priv_flags_set(const struct net_device *dev, u32 flags_mask) {
        u32 ret = 0;

        if (dev)
                #ifdef RTK_NETDEV_PRIV_FLAGS
                        #define PRIV_RSMUX       RTK_IFF_RSMUX
                        #define PRIV_OSMUX       RTK_IFF_OSMUX
                        #define PRIV_VSMUX       RTK_IFF_VSMUX
                        #define PRIV_DOMAIN_ELAN RTK_IFF_DOMAIN_ELAN
                        #define PRIV_DOMAIN_WAN  RTK_IFF_DOMAIN_WAN
                        #define PRIV_DOMAIN_WLAN RTK_IFF_DOMAIN_WLAN
                ret = rtk_netdev_get_flags(dev) & flags_mask;
                #else
                        #define PRIV_RSMUX       IFF_RSMUX
                        #define PRIV_OSMUX       IFF_OSMUX
                        #define PRIV_VSMUX       IFF_VSMUX
                        #define PRIV_DOMAIN_ELAN IFF_DOMAIN_ELAN
                        #define PRIV_DOMAIN_WAN  IFF_DOMAIN_WAN
                        #define PRIV_DOMAIN_WLAN IFF_DOMAIN_WLAN
                ret = dev->priv_flags & flags_mask;
                #endif
        return ret;
}

#ifdef CONFIG_RTK_SOC_RTL8198D
#define     MIB_COUNTERSTR_IF_IN_OCTETS                           "ifInOctets"
#define     MIB_COUNTERSTR_IF_IN_UCAST_PKTS                       "ifInUcastPkts"
#define     MIB_COUNTERSTR_F_IN_MULTICAST_PKTS                    "ifInMulticastPkts"
#define     MIB_COUNTERSTR_IF_IN_BROADCAST_PKTS                   "ifInBroadcastPkts"
#define     MIB_COUNTERSTR_IF_IN_DISCARDS                         "ifInDiscards"
#define     MIB_COUNTERSTR_IF_OUT_OCTETS                          "ifOutOctets"
#define     MIB_COUNTERSTR_IF_OUT_DISCARDS                        "ifOutDiscards"
#define     MIB_COUNTERSTR_IF_OUT_UCAST_PKTS_CNT                  "ifOutUcastPkts"
#define     MIB_COUNTERSTR_IF_OUT_MULTICAST_PKTS_CNT              "ifOutMulticastPkts"
#define     MIB_COUNTERSTR_IF_OUT_BROADCAST_PKTS_CNT              "ifOutBroadcastPkts"
#define     MIB_COUNTERSTR_DOT1D_PORT_DELAY_EXCEEDED_DISCARDS     "dot1dPortDelayExceedDiscards"
#define     MIB_COUNTERSTR_DOT1D_TP_PORT_IN_DISCARDS              "dot1dTpPortInDiscards"
#define     MIB_COUNTERSTR_DOT1D_TP_HC_PORT_IN_DISCARDS           "dot1dTpHcPortInDiscards"
#define     MIB_COUNTERSTR_DOT3_IN_PAUSE_FRAMES                   "dot3InPauseFrames"
#define     MIB_COUNTERSTR_DOT3_OUT_PAUSE_FRAMES                  "dot3OutPauseFrames"
#define     MIB_COUNTERSTR_DOT3_OUT_PAUSE_ON_FRAMES               "dot3OutPauseOnFrames"
#define     MIB_COUNTERSTR_DOT3_STATS_ALIGNMENT_ERRORS            "dot3StatsAlignmentErrors"
#define     MIB_COUNTERSTR_DOT3_STATS_FCS_ERRORS                  "dot3StatsFcsErrors"
#define     MIB_COUNTERSTR_DOT3_STATS_SINGLE_COLLISION_FRAMES     "dot3StatsSingleCollisionFrames"
#define     MIB_COUNTERSTR_DOT3_STATS_MULTIPLE_COLLISION_FRAMES   "dot3StatsMultipleCollisionFrames"
#define     MIB_COUNTERSTR_DOT3_STATS_DEFERRED_TRANSMISSIONS      "dot3StatsDeferredTransmissions"
#define     MIB_COUNTERSTR_DOT3_STATS_LATE_COLLISIONS             "dot3StatsLateCollisions"
#define     MIB_COUNTERSTR_DOT3_STATS_EXCESSIVE_COLLISIONS        "dot3StatsExcessiveCollisions"
#define     MIB_COUNTERSTR_DOT3_STATS_FRAME_TOO_LONGS             "dot3StatsFrameTooLongs"
#define     MIB_COUNTERSTR_DOT3_STATS_SYMBOL_ERRORS               "dot3StatsSymbolErrors"
#define     MIB_COUNTERSTR_DOT3_CONTROL_IN_UNKNOWN_OPCODES        "dot3ControlInUnknownOpcodes"
#define     MIB_COUNTERSTR_ETHER_STATS_DROP_EVENTS                "etherStatsDropEvents"
#define     MIB_COUNTERSTR_ETHER_STATS_OCTETS                     "etherStatsOctets"
#define     MIB_COUNTERSTR_ETHER_STATS_BROADCAST_PKTS             "etherStatsBroadcastPkts"
#define     MIB_COUNTERSTR_ETHER_STATS_MULTICAST_PKTS             "etherStatsMulticastPkts"
#define     MIB_COUNTERSTR_ETHER_STATS_UNDER_SIZE_PKTS            "etherStatsUndersizePkts"
#define     MIB_COUNTERSTR_ETHER_STATS_OVERSIZE_PKTS              "etherStatsOversizePkts"
#define     MIB_COUNTERSTR_ETHER_STATS_FRAGMENTS                  "etherStatsFragments"
#define     MIB_COUNTERSTR_ETHER_STATS_JABBERS                    "etherStatsJabbers"
#define     MIB_COUNTERSTR_ETHER_STATS_COLLISIONS                 "etherStatsCollisions"
#define     MIB_COUNTERSTR_ETHER_STATS_CRC_ALIGN_ERRORS           "etherStatsCRCAlignErrors"
#define     MIB_COUNTERSTR_ETHER_STATS_PKTS_64OCTETS              "etherStatsPkts64Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_PKTS_65TO127OCTETS         "etherStatsPkts65to127Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_PKTS_128TO255OCTETS        "etherStatsPkts128to255Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_PKTS_256TO511OCTETS        "etherStatsPkts256to511Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_PKTS_512TO1023OCTETS       "etherStatsPkts512to1023Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_PKTS_1024TO1518OCTETS      "etherStatsPkts1024to1518Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_PKTS_1519TOMAXOCTETS       "etherStatsPkts1519toMaxOctets"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_OCTETS                  "etherStatsTxOctets"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_UNDER_SIZE_PKTS         "etherStatsTxUndersizePkts"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_OVERSIZE_PKTS           "etherStatsTxOversizePkts"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_64OCTETS           "etherStatsTxPkts64Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_65TO127OCTETS      "etherStatsTxPkts65to127Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_128TO255OCTETS     "etherStatsTxPkts128to255Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_256TO511OCTETS     "etherStatsTxPkts256to511Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_512TO1023OCTETS    "etherStatsTxPkts512to1023Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_1024TO1518OCTETS   "etherStatsTxPkts1024to1518Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_PKTS_1519TOMAXOCTETS    "etherStatsTxPkts1519toMaxOctets"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_BROADCAST_PKTS          "etherStatsTxBroadcastPkts"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_MULTICAST_PKTS          "etherStatsTxMulticastPkts"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_FRAGMENTS               "etherStatsTxFragments"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_JABBERS                 "etherStatsTxJabbers"
#define     MIB_COUNTERSTR_ETHER_STATS_TX_CRC_ALIGN_ERRORS        "etherStatsTxCRCAlignErrors"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_UNDER_SIZE_PKTS         "etherStatsRxUndersizePkts"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_UNDER_SIZE_DROP_PKTS    "etherStatsRxUndersizeDropPkts"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_OVERSIZE_PKTS           "etherStatsRxOversizePkts"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_64OCTETS           "etherStatsRxPkts64Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_65TO127OCTETS      "etherStatsRxPkts65to127Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_128TO255OCTETS     "etherStatsRxPkts128to255Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_256TO511OCTETS     "etherStatsRxPkts256to511Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_512TO1023OCTETS    "etherStatsRxPkts512to1023Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_1024TO1518OCTETS   "etherStatsRxPkts1024to1518Octets"
#define     MIB_COUNTERSTR_ETHER_STATS_RX_PKTS_1519TOMAXOCTETS    "etherStatsRxPkts1519toMaxOctets"
#define     MIB_COUNTERSTR_IN_OAM_PDU_PKTS                        "inOamPduPkts"
#define     MIB_COUNTERSTR_OUT_OAM_PDU_PKTS                       "outOamPduPkts"

#define PORT_STR_SPEED_10M              "10M"
#define PORT_STR_SPEED_100M             "100M"
#define PORT_STR_SPEED_GIGA             "1000M"
#define PORT_STR_SPEED_500M             "500M"
#define PORT_STR_SPEED_2G5              "2.5G"
#define PORT_STR_SPEED_2G5LITE          "2.5GLite"
#define PORT_STR_SPEED_5G               "5G"
#define PORT_STR_SPEED_5GLITE           "5GLite"
#define PORT_STR_SPEED_10G              "10G"
#define PORT_STR_HALF_DUPLEX            "Half"
#define PORT_STR_FULL_DUPLEX            "Full"
#define PORT_STR_LINK_UP                "Up"
#define PORT_STR_LINK_DOWN              "Down"
#define PORT_STR_V                      "En"
#define PORT_STR_X                      "Dis"
#endif
#endif /*_RE8686_RTL9607C_H_*/

