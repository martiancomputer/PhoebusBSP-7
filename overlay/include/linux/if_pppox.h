/* SPDX-License-Identifier: GPL-2.0-or-later */
/***************************************************************************
 * Linux PPP over X - Generic PPP transport layer sockets
 * Linux PPP over Ethernet (PPPoE) Socket Implementation (RFC 2516) 
 *
 * This file supplies definitions required by the PPP over Ethernet driver
 * (pppox.c).  All version information wrt this file is located in pppox.c
 */
#ifndef __LINUX_IF_PPPOX_H
#define __LINUX_IF_PPPOX_H

#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/ppp_channel.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <uapi/linux/if_pppox.h>

static inline struct pppoe_hdr *pppoe_hdr(const struct sk_buff *skb)
{
	return (struct pppoe_hdr *)skb_network_header(skb);
}

struct pppoe_opt {
	struct net_device      *dev;	  /* device associated with socket*/
	int			ifindex;  /* ifindex of device associated with socket */
	struct pppoe_addr	pa;	  /* what this socket is bound to*/
	struct work_struct      padt_work;/* Work item for handling PADT */
};

struct pptp_opt {
	struct pptp_addr src_addr;
	struct pptp_addr dst_addr;
	u32 ack_sent, ack_recv;
	u32 seq_sent, seq_recv;
	int ppp_flags;
};
#include <net/sock.h>

struct pppox_sock {
	/* struct sock must be the first member of pppox_sock */
	struct sock sk;
	struct ppp_channel chan;
	struct pppox_sock __rcu	*next;	  /* for hash table */
	union {
		struct pppoe_opt pppoe;
		struct pptp_opt  pptp;
	} proto;
	__be16			num;
};
#define pppoe_dev	proto.pppoe.dev
#define pppoe_ifindex	proto.pppoe.ifindex
#define pppoe_pa	proto.pppoe.pa

static inline struct pppox_sock *pppox_sk(struct sock *sk)
{
	return container_of(sk, struct pppox_sock, sk);
}

struct module;

struct pppox_proto {
	int		(*create)(struct net *net, struct socket *sock, int kern);
	int		(*ioctl)(struct socket *sock, unsigned int cmd,
				 unsigned long arg);
	struct module	*owner;
};

extern int register_pppox_proto(int proto_num, const struct pppox_proto *pp);
extern void unregister_pppox_proto(int proto_num);
extern void pppox_unbind_sock(struct sock *sk);/* delete ppp-channel binding */
extern int pppox_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
extern int pppox_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);

/* PPPoX socket states */
enum {
    PPPOX_NONE		= 0,  /* initial state */
    PPPOX_CONNECTED	= 1,  /* connection established ==TCP_ESTABLISHED */
    PPPOX_BOUND		= 2,  /* bound to ppp device */
    PPPOX_DEAD		= 16  /* dead, useless, please clean me up!*/
};

#ifdef CONFIG_LCP_ACCELERATE
extern int is_pppoe_channel(struct ppp_channel *chan);

struct ST_MAGIC_NUM {
	unsigned short sid; //pppoe session ID
	unsigned int magic;
	int ifindex;
	unsigned char remote[ETH_ALEN];
};
extern struct ST_MAGIC_NUM stMagic_num[];
extern spinlock_t stMagic_num_lock;

#endif

/* 7.1 hid the flexible-array members of the uapi PPPoE structs behind
 * #ifndef __KERNEL__ (uapi hardening):
 *
 *   struct pppoe_tag { __be16 tag_type; __be16 tag_len;
 *   #ifndef __KERNEL__
 *           char tag_data[];
 *   #endif  } __packed;
 *
 * and likewise `struct pppoe_tag tag[]` in struct pppoe_hdr. In-kernel code is
 * expected to compute those offsets itself. Both Realtek bridge-extension
 * files (rtl8192cd/8192cd_br_ext.c and g6_wifi_driver/core/rtw_br_ext.c) parse
 * PPPoE tags, so the accessors live here rather than being duplicated.
 * Both structs are __packed, so sizeof() is the exact on-wire header length. */
#define pppoe_hdr_tags(ph)	((unsigned char *)((ph) + 1))
#define pppoe_tag_data(t)	((unsigned char *)((t) + 1))

#endif /* !(__LINUX_IF_PPPOX_H) */
