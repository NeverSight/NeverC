/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_LIST_H
#define _NVK_LINUX_LIST_H

#include <linux/types.h>
#include <linux/kernel.h>

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static __always_inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static __always_inline void __list_add(struct list_head *n,
				       struct list_head *prev,
				       struct list_head *next)
{
	next->prev = n;
	n->next = next;
	n->prev = prev;
	prev->next = n;
}

static __always_inline void list_add(struct list_head *n, struct list_head *head)
{
	__list_add(n, head, head->next);
}

static __always_inline void list_add_tail(struct list_head *n,
					  struct list_head *head)
{
	__list_add(n, head->prev, head);
}

static __always_inline void list_del(struct list_head *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
	entry->next = entry->prev = entry;
}

static __always_inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(ptr, type, member)                                   \
	list_entry((ptr)->next, type, member)

#define list_for_each(pos, head)                                              \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)                                \
	for (pos = list_entry((head)->next, __typeof__(*pos), member);          \
	     &pos->member != (head);                                            \
	     pos = list_entry(pos->member.next, __typeof__(*pos), member))

#endif /* _NVK_LINUX_LIST_H */
