/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_NETLINK_H
#define _NEVERC_KRT_LINUX_NETLINK_H

#include <linux/types.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <nvkmod_version.h>

struct sock;     /* opaque */
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
 * across GKI 5.10–6.18.  Fields after input differ:
 *   5.10–6.1:  cb_mutex, bind, unbind, compare
 *   6.6:       cb_mutex, bind, unbind, release  (compare→release)
 *   6.12+:     bind, unbind, release            (cb_mutex removed)
 * Zero-fill the entire struct and set only groups/flags/input.
 */
struct netlink_kernel_cfg {
	unsigned int groups;
	unsigned int flags;
	void (*input)(struct sk_buff *skb);
#if NEVERC_KRT_LINUX_AT_LEAST(6, 12, 0)
	unsigned char __opaque[24];
#else
	unsigned char __opaque[32];
#endif
};

#if NEVERC_KRT_LINUX_AT_LEAST(6, 12, 0)
_Static_assert(sizeof(struct netlink_kernel_cfg) == 40,
	       "unexpected GKI 6.12+ netlink_kernel_cfg layout");
#else
_Static_assert(sizeof(struct netlink_kernel_cfg) == 48,
	       "unexpected GKI 5.10-6.6 netlink_kernel_cfg layout");
#endif

/*
 * netlink_kernel_create is always an inline wrapper around
 * __netlink_kernel_create (passing THIS_MODULE).  The __-prefixed
 * version is the real export in all GKI 5.10–6.18.
 */
struct sock *__netlink_kernel_create(struct net *net, int unit,
				     struct module *module,
				     struct netlink_kernel_cfg *cfg);

static __always_inline struct sock *
netlink_kernel_create(struct net *net, int unit,
		      struct netlink_kernel_cfg *cfg)
{
	return __netlink_kernel_create(net, unit, THIS_MODULE, cfg);
}

void netlink_kernel_release(struct sock *sk);
int netlink_unicast(struct sock *sk, struct sk_buff *skb, u32 portid,
		    int nonblock);
int netlink_broadcast(struct sock *sk, struct sk_buff *skb, u32 portid,
		      u32 group, gfp_t allocation);

/*
 * nlmsg_new / nlmsg_free are inline wrappers — never exported.
 * nlmsg_put is resolved at runtime by the neverc_krt_nl API.
 */
static __always_inline struct sk_buff *nlmsg_new(size_t payload, gfp_t flags)
{
	return alloc_skb(NLMSG_ALIGN(NLMSG_HDRLEN + (int)payload), flags);
}

static __always_inline void nlmsg_free(struct sk_buff *skb)
{
	kfree_skb(skb);
}

#endif /* _NEVERC_KRT_LINUX_NETLINK_H */
