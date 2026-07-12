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
 * GKI 6.18 appends last_holder (8 bytes).
 */
struct semaphore {
	u32 lock;
	unsigned int count;
	struct list_head wait_list;
#if NEVERC_KRT_KERNEL >= 618
	unsigned long last_holder;
#endif
};

#if NEVERC_KRT_KERNEL >= 618
_Static_assert(sizeof(struct semaphore) == 32,
	       "unexpected GKI 6.18 semaphore layout");
#else
_Static_assert(sizeof(struct semaphore) == 24,
	       "unexpected GKI 5.10-6.12 semaphore layout");
#endif

/*
 * sema_init is always inline in GKI.
 * Layout: lock(4) + count(4) + wait_list(16).
 */
static __always_inline void sema_init(struct semaphore *sem, int val)
{
	__builtin_memset(sem, 0, sizeof(*sem));
	sem->count = (unsigned int)val;
	INIT_LIST_HEAD(&sem->wait_list);
}
void down(struct semaphore *sem);
int down_interruptible(struct semaphore *sem);
int down_trylock(struct semaphore *sem);
int down_timeout(struct semaphore *sem, long jiffies);
void up(struct semaphore *sem);

#endif /* _NEVERC_KRT_LINUX_SEMAPHORE_H */
