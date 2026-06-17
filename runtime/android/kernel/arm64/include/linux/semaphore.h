/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SEMAPHORE_H
#define _NEVERC_KRT_LINUX_SEMAPHORE_H

#include <linux/types.h>

/*
 * GKI 5.10–6.12 (arm64, no debug):
 *   raw_spinlock_t lock      4 bytes
 *   unsigned int count       4 bytes
 *   struct list_head wait    16 bytes
 *   Total = 24 bytes.  32 gives headroom.
 */
struct semaphore {
	unsigned char __opaque[32];
};

void sema_init(struct semaphore *sem, int val);
void down(struct semaphore *sem);
int down_interruptible(struct semaphore *sem);
int down_trylock(struct semaphore *sem);
int down_timeout(struct semaphore *sem, long jiffies);
void up(struct semaphore *sem);

#endif /* _NEVERC_KRT_LINUX_SEMAPHORE_H */
