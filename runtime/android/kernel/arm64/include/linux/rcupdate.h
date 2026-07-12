/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_RCUPDATE_H
#define _NEVERC_KRT_LINUX_RCUPDATE_H

#include <linux/compiler.h>

struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
};
#define rcu_head callback_head

/*
 * Android GKI enables PREEMPT_RCU.  These helpers update per-task nesting
 * state; a compiler barrier alone is not a valid RCU read-side section.
 */
void __rcu_read_lock(void);
void __rcu_read_unlock(void);
static __always_inline void rcu_read_lock(void)
{
	__rcu_read_lock();
}
static __always_inline void rcu_read_unlock(void)
{
	__rcu_read_unlock();
}

void synchronize_rcu(void);
void call_rcu(struct callback_head *head, void (*func)(struct callback_head *));
void rcu_barrier(void);

#define rcu_dereference(p) ({                                                 \
	__typeof__(p) __p = __atomic_load_n(&(p), __ATOMIC_ACQUIRE);          \
	__p;                                                                  \
})

#define rcu_assign_pointer(p, v)                                              \
	do {                                                                   \
		__atomic_store_n(&(p), (v), __ATOMIC_RELEASE);                 \
	} while (0)

#endif /* _NEVERC_KRT_LINUX_RCUPDATE_H */
