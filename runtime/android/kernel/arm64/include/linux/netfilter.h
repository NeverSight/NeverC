/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_NETFILTER_H
#define _NEVERC_KRT_LINUX_NETFILTER_H

#include <linux/types.h>
#include <nvkmod_version.h>

struct net;
struct net_device;
struct sk_buff;
struct sock;

#define NF_DROP   0
#define NF_ACCEPT 1
#define NF_STOLEN 2
#define NF_QUEUE  3
#define NF_REPEAT 4
#define NF_STOP   5
#define NF_MAX_VERDICT NF_STOP

enum nf_inet_hooks {
	NF_INET_PRE_ROUTING,
	NF_INET_LOCAL_IN,
	NF_INET_FORWARD,
	NF_INET_LOCAL_OUT,
	NF_INET_POST_ROUTING,
	NF_INET_NUMHOOKS,
};

struct nf_hook_state {
#if NEVERC_KRT_KERNEL == 510
	unsigned int hook;
	u8 pf;
	u8 __pad[3];
#else
	u8 hook;
	u8 pf;
	u8 __pad[6];
#endif
	struct net_device *in;
	struct net_device *out;
	struct sock *sk;
	struct net *net;
	int (*okfn)(struct net *, struct sock *, struct sk_buff *);
};

typedef unsigned int nf_hookfn(void *priv, struct sk_buff *skb,
			       const struct nf_hook_state *state);

enum nf_hook_ops_type {
	NF_HOOK_OP_UNDEFINED,
	NF_HOOK_OP_NF_TABLES,
#if NEVERC_KRT_KERNEL >= 612
	NF_HOOK_OP_BPF,
#endif
#if NEVERC_KRT_KERNEL >= 618
	NF_HOOK_OP_NFT_FT,
#endif
};

/*
 * Linux 6.18 prepends list/RCU bookkeeping to nf_hook_ops.  The registration
 * API owns those bytes, so expose them as opaque storage.  User fields retain
 * their upstream names and exact offsets for each selected GKI profile.
 */
struct nf_hook_ops {
#if NEVERC_KRT_KERNEL >= 618
	u8 __kernel_private[32];
#endif
	nf_hookfn *hook;
	struct net_device *dev;
	void *priv;
	u8 pf;
	u8 hook_ops_type;
	u16 __pad;
	unsigned int hooknum;
	int priority;
};

#if NEVERC_KRT_KERNEL >= 618
_Static_assert(sizeof(struct nf_hook_ops) == 72,
	       "unexpected GKI 6.18 nf_hook_ops layout");
_Static_assert(__builtin_offsetof(struct nf_hook_ops, hook) == 32,
	       "unexpected GKI 6.18 nf_hook_ops hook offset");
#else
_Static_assert(sizeof(struct nf_hook_ops) == 40,
	       "unexpected GKI 5.10-6.12 nf_hook_ops layout");
_Static_assert(__builtin_offsetof(struct nf_hook_ops, hook) == 0,
	       "unexpected GKI 5.10-6.12 nf_hook_ops hook offset");
#endif

int nf_register_net_hook(struct net *net, const struct nf_hook_ops *ops);
void nf_unregister_net_hook(struct net *net, const struct nf_hook_ops *ops);
int nf_register_net_hooks(struct net *net, const struct nf_hook_ops *reg,
			  unsigned int n);
void nf_unregister_net_hooks(struct net *net, const struct nf_hook_ops *reg,
			     unsigned int n);

#define NF_INET_PRI_FIRST       (-0x7fffffff - 1)
#define NF_INET_PRI_CONNTRACK   (-200)
#define NF_INET_PRI_FILTER      0
#define NF_INET_PRI_LAST        0x7fffffff

#endif /* _NEVERC_KRT_LINUX_NETFILTER_H */
