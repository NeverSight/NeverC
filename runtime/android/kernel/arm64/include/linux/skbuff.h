/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SKBUFF_H
#define _NEVERC_KRT_LINUX_SKBUFF_H

#include <linux/types.h>

struct sk_buff; /* opaque (layout is extremely version-dependent) */

struct sk_buff *alloc_skb(unsigned int size, gfp_t priority);
struct sk_buff *dev_alloc_skb(unsigned int length);
void kfree_skb(struct sk_buff *skb);
void consume_skb(struct sk_buff *skb);

unsigned char *skb_put(struct sk_buff *skb, unsigned int len);
unsigned char *skb_push(struct sk_buff *skb, unsigned int len);
unsigned char *skb_pull(struct sk_buff *skb, unsigned int len);

struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t priority);
struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t priority);

#endif
