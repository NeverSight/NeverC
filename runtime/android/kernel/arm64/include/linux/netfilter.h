/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_NETFILTER_H
#define _NEVERC_KRT_LINUX_NETFILTER_H

#include <linux/types.h>

struct sk_buff;
struct nf_interpose_state;
struct net_device;

#define NF_DROP   0
#define NF_ACCEPT 1
#define NF_STOLEN 2
#define NF_QUEUE  3
#define NF_REPEAT 4
#define NF_STOP   5
#define NF_MAX_VERDICT NF_STOP

enum nf_inet_interposes {
	NF_INET_PRE_ROUTING,
	NF_INET_LOCAL_IN,
	NF_INET_FORWARD,
	NF_INET_LOCAL_OUT,
	NF_INET_POST_ROUTING,
	NF_INET_NUMINTERPOSES,
};

typedef unsigned int (*nf_interposefn)(void *priv, struct sk_buff *skb,
				  const struct nf_interpose_state *state);

/*
 * Layout verified against GKI 5.10–6.18.
 *
 * 5.10:     pf is u_int8_t, no interpose_ops_type field (1 byte + 3 pad).
 * 5.15–6.18: pf is u8, interpose_ops_type:8 bitfield follows (2 bytes + 2 pad).
 *
 * Zero-initializing _reserved makes this layout work on both:
 *   5.10: _reserved occupies what the kernel treats as padding.
 *   5.15+: _reserved maps to interpose_ops_type = NF_INTERPOSE_OP_UNDEFINED (0).
 */
struct nf_interpose_ops {
	nf_interposefn          *interpose;
	struct net_device  *dev;
	void               *priv;
	u8                  pf;
	u8                  _reserved;
	u16                 _pad;
	unsigned int        interposenum;
	int                 priority;
};

int nf_register_net_interpose(struct net *net, const struct nf_interpose_ops *ops);
void nf_unregister_net_interpose(struct net *net, const struct nf_interpose_ops *ops);
int nf_register_net_interposes(struct net *net, const struct nf_interpose_ops *reg,
			  unsigned int n);
void nf_unregister_net_interposes(struct net *net, const struct nf_interpose_ops *reg,
			     unsigned int n);

#define NF_INET_PRI_FIRST       (-300)
#define NF_INET_PRI_CONNTRACK   (-200)
#define NF_INET_PRI_FILTER      0
#define NF_INET_PRI_LAST        300

#endif
