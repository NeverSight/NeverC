/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_NETFILTER_H
#define _NVK_LINUX_NETFILTER_H

#include <linux/types.h>

struct sk_buff;
struct nf_hook_state;
struct net;

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

typedef unsigned int (*nf_hookfn)(void *priv, struct sk_buff *skb,
				  const struct nf_hook_state *state);

struct nf_hook_ops {
	nf_hookfn *hook;
	struct net *dev;
	void *priv;
	int pf;
	unsigned int hooknum;
	int priority;
};

int nf_register_net_hook(struct net *net, const struct nf_hook_ops *ops);
void nf_unregister_net_hook(struct net *net, const struct nf_hook_ops *ops);
int nf_register_net_hooks(struct net *net, const struct nf_hook_ops *reg,
			  unsigned int n);
void nf_unregister_net_hooks(struct net *net, const struct nf_hook_ops *reg,
			     unsigned int n);

#define NF_INET_PRI_FIRST       (-300)
#define NF_INET_PRI_CONNTRACK   (-200)
#define NF_INET_PRI_FILTER      0
#define NF_INET_PRI_LAST        300

#endif
