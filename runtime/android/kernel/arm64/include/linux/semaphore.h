/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SEMAPHORE_H
#define _NEVERC_KRT_LINUX_SEMAPHORE_H

#include <linux/types.h>
#include <linux/compiler.h>

/*
 * GKI 5.10–6.18 (arm64, no debug):
 *   raw_spinlock_t lock      4 bytes
 *   unsigned int count       4 bytes
 *   struct list_head wait    16 bytes
 *   Total = 24 bytes.  32 gives headroom.
 */
struct semaphore {
	unsigned char __opaque[32];
};

_Static_assert(sizeof(struct semaphore) >= 24,
	       "struct semaphore too small for GKI arm64");

/*
 * sema_init is always inline in GKI.
 * Layout: lock(4) + count(4) + wait_list(16).
 */
__always_inline void sema_init(struct semaphore *sem, int val)
{
	__builtin_memset(sem, 0, sizeof(*sem));
	*(unsigned int *)((char *)sem + 4) = val;
	/* init wait_list head (self-referencing) at offset 8 */
	void **head = (void **)((char *)sem + 8);
	head[0] = head;  /* next = self */
	head[1] = head;  /* prev = self */
}
void down(struct semaphore *sem);
int down_interruptible(struct semaphore *sem);
int down_trylock(struct semaphore *sem);
int down_timeout(struct semaphore *sem, long jiffies);
void up(struct semaphore *sem);

#endif /* _NEVERC_KRT_LINUX_SEMAPHORE_H */
