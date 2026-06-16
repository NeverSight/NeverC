/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_LIST_H
#define _NEVERC_KRT_LINUX_LIST_H

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

#define list_for_each_safe(pos, n, head)                                     \
	for (pos = (head)->next, n = pos->next; pos != (head);               \
	     pos = n, n = pos->next)

#define list_for_each_entry_safe(pos, n, head, member)                       \
	for (pos = list_entry((head)->next, __typeof__(*pos), member),        \
	     n = list_entry(pos->member.next, __typeof__(*pos), member);      \
	     &pos->member != (head);                                           \
	     pos = n, n = list_entry(n->member.next, __typeof__(*pos), member))

static __always_inline void list_del_init(struct list_head *entry)
{
	list_del(entry);
	INIT_LIST_HEAD(entry);
}

static __always_inline int list_is_last(const struct list_head *list,
					const struct list_head *head)
{
	return list->next == head;
}

static __always_inline void list_move(struct list_head *list,
				      struct list_head *head)
{
	list_del(list);
	list_add(list, head);
}

static __always_inline void list_move_tail(struct list_head *list,
					   struct list_head *head)
{
	list_del(list);
	list_add_tail(list, head);
}

static __always_inline void list_splice(struct list_head *list,
					struct list_head *head)
{
	if (!list_empty(list)) {
		struct list_head *first = list->next;
		struct list_head *last = list->prev;
		struct list_head *at = head->next;
		first->prev = head;
		head->next = first;
		last->next = at;
		at->prev = last;
	}
}

#define list_last_entry(ptr, type, member)                                    \
	list_entry((ptr)->prev, type, member)

#define list_entry_is_head(pos, head, member)                                \
	(&pos->member == (head))

#endif /* _NEVERC_KRT_LINUX_LIST_H */
