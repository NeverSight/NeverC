/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_HASHTABLE_H
#define _NVK_LINUX_HASHTABLE_H

#include <linux/types.h>
#include <linux/list.h>

#define DEFINE_HASHTABLE(name, bits) \
	struct hlist_head name[1 << (bits)]

#define hash_init(ht) do {                                                    \
	unsigned int __i;                                                     \
	for (__i = 0; __i < (sizeof(ht) / sizeof((ht)[0])); __i++)           \
		(ht)[__i].first = (void *)0;                                  \
} while (0)

#define hash_add(ht, node, key)                                               \
	do { (node)->next = (ht)[(key) % (sizeof(ht)/sizeof((ht)[0]))].first; \
	     (ht)[(key) % (sizeof(ht)/sizeof((ht)[0]))].first = (node);      \
	     (node)->pprev = &(ht)[(key) % (sizeof(ht)/sizeof((ht)[0]))].first; \
	     if ((node)->next) (node)->next->pprev = &(node)->next;           \
	} while (0)

#define hash_del(node)                                                        \
	do { if ((node)->pprev) { *(node)->pprev = (node)->next;              \
	     if ((node)->next) (node)->next->pprev = (node)->pprev;           \
	     (node)->pprev = (void *)0; } } while (0)

#define hash_empty(ht) ({                                                     \
	unsigned int __i; bool __e = 1;                                       \
	for (__i = 0; __i < (sizeof(ht)/sizeof((ht)[0])); __i++)             \
		if ((ht)[__i].first) { __e = 0; break; }                     \
	__e; })

#define hash_for_each(ht, bkt, obj, member)                                   \
	for ((bkt) = 0; (bkt) < (sizeof(ht)/sizeof((ht)[0])); (bkt)++)      \
		for ((obj) = (ht)[(bkt)].first ?                              \
		     container_of((ht)[(bkt)].first, typeof(*(obj)), member)  \
		     : (void *)0;                                             \
		     (obj); (obj) = (obj)->member.next ?                      \
		     container_of((obj)->member.next, typeof(*(obj)), member) \
		     : (void *)0)

#define hash_for_each_possible(ht, obj, member, key)                          \
	for ((obj) = (ht)[(key) % (sizeof(ht)/sizeof((ht)[0]))].first ?      \
	     container_of((ht)[(key) % (sizeof(ht)/sizeof((ht)[0]))].first,  \
			  typeof(*(obj)), member) : (void *)0;                \
	     (obj); (obj) = (obj)->member.next ?                              \
	     container_of((obj)->member.next, typeof(*(obj)), member)         \
	     : (void *)0)

#endif
