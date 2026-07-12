/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_RCULIST_H
#define _NEVERC_KRT_LINUX_RCULIST_H

#include <linux/list.h>
#include <linux/rcupdate.h>

static __always_inline void list_add_rcu(struct list_head *n,
					 struct list_head *head)
{
	struct list_head *next = READ_ONCE(head->next);

	n->next = next;
	n->prev = head;
	rcu_assign_pointer(next->prev, n);
	rcu_assign_pointer(head->next, n);
}

static __always_inline void list_add_tail_rcu(struct list_head *n,
					      struct list_head *head)
{
	struct list_head *prev = READ_ONCE(head->prev);

	n->next = head;
	n->prev = prev;
	rcu_assign_pointer(prev->next, n);
	rcu_assign_pointer(head->prev, n);
}

static __always_inline void list_del_rcu(struct list_head *entry)
{
	struct list_head *prev = entry->prev;
	struct list_head *next = entry->next;

	rcu_assign_pointer(prev->next, next);
	WRITE_ONCE(next->prev, prev);
}

#define list_for_each_entry_rcu(pos, head, member)                            \
	for (pos = list_entry(rcu_dereference((head)->next),                  \
			      __typeof__(*pos), member);                      \
	     &pos->member != (head);                                          \
	     pos = list_entry(rcu_dereference(pos->member.next),              \
			      __typeof__(*pos), member))

#endif
