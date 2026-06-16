/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_RWSEM_H
#define _NEVERC_KRT_LINUX_RWSEM_H

#include <linux/types.h>

struct rw_semaphore {
	unsigned char __opaque[40];
};

void init_rwsem(struct rw_semaphore *sem);
void down_read(struct rw_semaphore *sem);
int down_read_trylock(struct rw_semaphore *sem);
void up_read(struct rw_semaphore *sem);
void down_write(struct rw_semaphore *sem);
int down_write_trylock(struct rw_semaphore *sem);
void up_write(struct rw_semaphore *sem);
void downgrade_write(struct rw_semaphore *sem);

#endif /* _NEVERC_KRT_LINUX_RWSEM_H */
