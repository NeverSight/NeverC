/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_RCUPDATE_H
#define _NEVERC_KRT_LINUX_RCUPDATE_H

#include <linux/compiler.h>

/*
 * On non-RT GKI kernels (the vast majority), rcu_read_lock/unlock are
 * essentially preempt_disable/enable.  We provide a no-op version here
 * that matches non-preemptible kernels; if your target uses PREEMPT_RT,
 * resolve __rcu_read_lock / __rcu_read_unlock via kallsyms instead.
 */
__always_inline void rcu_read_lock(void) { barrier(); }
__always_inline void rcu_read_unlock(void) { barrier(); }

void synchronize_rcu(void);
void call_rcu(struct callback_head *head, void (*func)(struct callback_head *));
void rcu_barrier(void);

#define rcu_dereference(p) ({                                                 \
	__typeof__(p) __p = (*(volatile __typeof__(p) *)&(p));                \
	barrier();                                                            \
	__p;                                                                  \
})

#define rcu_assign_pointer(p, v) ({                                           \
	barrier();                                                            \
	(p) = (v);                                                            \
})

struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
};
#define rcu_head callback_head

#endif /* _NEVERC_KRT_LINUX_RCUPDATE_H */
