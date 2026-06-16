/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_RCULIST_H
#define _NEVERC_KRT_LINUX_RCULIST_H

#include <linux/list.h>
#include <linux/rcupdate.h>

static __always_inline void list_add_rcu(struct list_head *n,
					 struct list_head *head)
{
	n->next = head->next;
	n->prev = head;
	rcu_assign_pointer(head->next->prev, n);
	rcu_assign_pointer(head->next, n);
}

static __always_inline void list_add_tail_rcu(struct list_head *n,
					      struct list_head *head)
{
	n->next = head;
	n->prev = head->prev;
	rcu_assign_pointer(head->prev->next, n);
	rcu_assign_pointer(head->prev, n);
}

static __always_inline void list_del_rcu(struct list_head *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
}

#define list_for_each_entry_rcu(pos, head, member)                            \
	for (pos = list_entry(rcu_dereference((head)->next),                  \
			      __typeof__(*pos), member);                      \
	     &pos->member != (head);                                          \
	     pos = list_entry(rcu_dereference(pos->member.next),              \
			      __typeof__(*pos), member))

#endif
