/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_RWSEM_H
#define _NEVERC_KRT_LINUX_RWSEM_H

#include <linux/types.h>
#include <nvkmod_version.h>

/*
 * GKI 5.10-6.18: count(8) + owner(8) + osq(4) + lock(4) + wait_list(16)
 *                + ANDROID_VENDOR_DATA(8) + ANDROID_OEM_DATA_ARRAY(16)
 *                = 64 bytes in the official arm64 configurations.
 * Linux 7.2 uses first_waiter and removes vendor reserves (32 bytes).
 */
struct rw_semaphore {
#if NEVERC_KRT_LINUX_AT_LEAST(7, 2, 0)
	unsigned char __opaque[32];
#else
	unsigned char __opaque[64];
#endif
};

#if NEVERC_KRT_LINUX_AT_LEAST(7, 2, 0)
_Static_assert(sizeof(struct rw_semaphore) == 32,
	       "unexpected GKI 7.2 rw_semaphore layout");
#else
_Static_assert(sizeof(struct rw_semaphore) == 64,
	       "unexpected GKI rw_semaphore layout");
#endif

void __init_rwsem(struct rw_semaphore *sem, const char *name, void *key);
#define init_rwsem(sem) __init_rwsem(sem, "?", (void *)0)
void down_read(struct rw_semaphore *sem);
int down_read_trylock(struct rw_semaphore *sem);
void up_read(struct rw_semaphore *sem);
void down_write(struct rw_semaphore *sem);
int down_write_trylock(struct rw_semaphore *sem);
void up_write(struct rw_semaphore *sem);
void downgrade_write(struct rw_semaphore *sem);

#endif /* _NEVERC_KRT_LINUX_RWSEM_H */
