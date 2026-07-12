/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SKBUFF_H
#define _NEVERC_KRT_LINUX_SKBUFF_H

#include <linux/types.h>
#include <linux/gfp.h>
#include <nvkmod_version.h>

struct sk_buff; /* opaque (layout is extremely version-dependent) */
struct net_device;

/*
 * alloc_skb is always an inline wrapper.  __alloc_skb is the real
 * export in all GKI versions (5.10–6.18).
 */
struct sk_buff *__alloc_skb(unsigned int size, gfp_t priority,
			    int flags, int node);

static __always_inline struct sk_buff *alloc_skb(unsigned int size, gfp_t priority)
{
	return __alloc_skb(size, priority, 0, -1);
}

/*
 * __netdev_alloc_skb performs both allocation and skb_reserve().  Merely
 * adding NET_SKB_PAD to alloc_skb() leaves skb->data at the wrong offset.
 */
struct sk_buff *__netdev_alloc_skb(struct net_device *dev,
				   unsigned int length, gfp_t priority);
static __always_inline struct sk_buff *dev_alloc_skb(unsigned int length)
{
	return __netdev_alloc_skb((struct net_device *)0, length, GFP_ATOMIC);
}

/*
 * kfree_skb / consume_skb export status (verified from System.map __ksymtab):
 *   5.10–5.15:  kfree_skb ✓   consume_skb ✓
 *   6.1+:       kfree_skb ✗   consume_skb ✓
 *
 * consume_skb is the only universally-exported free across 5.10–6.18.
 */
void consume_skb(struct sk_buff *skb);
#define kfree_skb(skb) consume_skb(skb)

unsigned char *skb_put(struct sk_buff *skb, unsigned int len);
unsigned char *skb_push(struct sk_buff *skb, unsigned int len);
unsigned char *skb_pull(struct sk_buff *skb, unsigned int len);

struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t priority);
struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t priority);

#endif
