/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SEMAPHORE_H
#define _NEVERC_KRT_LINUX_SEMAPHORE_H

#include <linux/types.h>

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
