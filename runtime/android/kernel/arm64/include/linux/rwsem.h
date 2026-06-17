/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_RWSEM_H
#define _NEVERC_KRT_LINUX_RWSEM_H

#include <linux/types.h>

/*
 * GKI 5.10-6.12: count(8) + owner(8) + osq(4) + lock(4) + wait_list(16)
 *                + ANDROID_VENDOR_DATA(8) + ANDROID_OEM_DATA_ARRAY(16)
 *                → ~64 bytes.  Round up to 72 for safety.
 */
struct rw_semaphore {
	unsigned char __opaque[72];
};

_Static_assert(sizeof(struct rw_semaphore) >= 64,
	       "struct rw_semaphore too small for GKI with ANDROID_VENDOR_OEM_DATA");

void init_rwsem(struct rw_semaphore *sem);
void down_read(struct rw_semaphore *sem);
int down_read_trylock(struct rw_semaphore *sem);
void up_read(struct rw_semaphore *sem);
void down_write(struct rw_semaphore *sem);
int down_write_trylock(struct rw_semaphore *sem);
void up_write(struct rw_semaphore *sem);
void downgrade_write(struct rw_semaphore *sem);

#endif /* _NEVERC_KRT_LINUX_RWSEM_H */
