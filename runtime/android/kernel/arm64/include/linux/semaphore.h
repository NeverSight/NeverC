/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SEMAPHORE_H
#define _NEVERC_KRT_LINUX_SEMAPHORE_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <nvkmod_version.h>

/*
 * GKI 5.10–6.12 (arm64):
 *   raw_spinlock_t lock      4 bytes
 *   unsigned int count       4 bytes
 *   struct list_head wait    16 bytes
 * GKI 6.18 appends last_holder (8 bytes). Linux 7.2 replaces wait_list with
 * a first_waiter pointer; the production configuration retains last_holder.
 */
struct semaphore_waiter;
struct semaphore {
	u32 lock;
	unsigned int count;
#if NEVERC_KRT_LINUX_AT_LEAST(7, 2, 0)
	struct semaphore_waiter *first_waiter;
#else
	struct list_head wait_list;
#endif
#if NEVERC_KRT_LINUX_AT_LEAST(6, 18, 0)
	unsigned long last_holder;
#endif
};

#if NEVERC_KRT_LINUX_AT_LEAST(7, 2, 0)
_Static_assert(sizeof(struct semaphore) == 24,
	       "unexpected GKI 7.2 semaphore layout");
#elif NEVERC_KRT_LINUX_AT_LEAST(6, 18, 0)
_Static_assert(sizeof(struct semaphore) == 32,
	       "unexpected GKI 6.18 semaphore layout");
#else
_Static_assert(sizeof(struct semaphore) == 24,
	       "unexpected GKI 5.10-6.12 semaphore layout");
#endif

/*
 * sema_init is always inline in GKI.
 * The cleared first_waiter is the empty queue on Linux 7.2+.
 */
static __always_inline void sema_init(struct semaphore *sem, int val)
{
	__builtin_memset(sem, 0, sizeof(*sem));
	sem->count = (unsigned int)val;
#if NEVERC_KRT_LINUX_BEFORE(7, 2, 0)
	INIT_LIST_HEAD(&sem->wait_list);
#endif
}
void down(struct semaphore *sem);
int down_interruptible(struct semaphore *sem);
int down_trylock(struct semaphore *sem);
int down_timeout(struct semaphore *sem, long jiffies);
void up(struct semaphore *sem);

#endif /* _NEVERC_KRT_LINUX_SEMAPHORE_H */
