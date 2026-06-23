/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_NETLINK_H
#define _NEVERC_KRT_LINUX_NETLINK_H

#include <linux/types.h>

struct sock;     /* opaque */
struct sk_buff;  /* opaque */
struct net;      /* opaque */

/* Netlink message header (UAPI, ABI-stable). */
struct nlmsghdr {
	u32 nlmsg_len;
	u16 nlmsg_type;
	u16 nlmsg_flags;
	u32 nlmsg_seq;
	u32 nlmsg_pid;
};

#define NLMSG_ALIGNTO   4U
#define NLMSG_ALIGN(len) (((len) + NLMSG_ALIGNTO - 1) & ~(NLMSG_ALIGNTO - 1))
#define NLMSG_HDRLEN    ((int)NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#define NLMSG_LENGTH(len) ((len) + NLMSG_HDRLEN)
#define NLMSG_SPACE(len)  NLMSG_ALIGN(NLMSG_LENGTH(len))
#define NLMSG_DATA(nlh) ((void *)((char *)(nlh) + NLMSG_HDRLEN))
#define NLMSG_PAYLOAD(nlh, len) ((nlh)->nlmsg_len - NLMSG_SPACE(len))

/* Standard netlink families. */
#define NETLINK_ROUTE    0
#define NETLINK_USERSOCK 2
#define NETLINK_FIREWALL 3
#define NETLINK_NETFILTER 12
#define NETLINK_KOBJECT_UEVENT 15
#define NETLINK_GENERIC  16

/* Netlink message flags. */
#define NLM_F_REQUEST 0x01
#define NLM_F_MULTI   0x02
#define NLM_F_ACK     0x04

/*
 * Netlink kernel config (input to netlink_kernel_create).
 *
 * Only the first three fields (groups, flags, input) are portable
 * across GKI 5.10–6.12.  Fields after input differ:
 *   5.10–6.1:  cb_mutex, bind, unbind, compare
 *   6.6:       cb_mutex, bind, unbind, release  (compare→release)
 *   6.12:      bind, unbind, release            (cb_mutex removed)
 * Zero-fill the entire struct and set only groups/flags/input.
 */
struct netlink_kernel_cfg {
	unsigned int groups;
	unsigned int flags;
	void (*input)(struct sk_buff *skb);
	unsigned char __opaque[32];
};

struct sock *netlink_kernel_create(struct net *net, int unit,
				   struct netlink_kernel_cfg *cfg);
void netlink_kernel_release(struct sock *sk);
int netlink_unicast(struct sock *sk, struct sk_buff *skb, u32 portid,
		    int nonblock);
int netlink_broadcast(struct sock *sk, struct sk_buff *skb, u32 portid,
		      u32 group, gfp_t allocation);

/* sk_buff helpers for netlink message construction. */
struct sk_buff *nlmsg_new(size_t payload, gfp_t flags);
struct nlmsghdr *nlmsg_put(struct sk_buff *skb, u32 portid, u32 seq,
			   int type, int payload, int flags);
void nlmsg_free(struct sk_buff *skb);

/* init_net: the default network namespace (resolve via kallsyms). */

#endif /* _NEVERC_KRT_LINUX_NETLINK_H */
