/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_MUTEX_H
#define _NEVERC_KRT_LINUX_MUTEX_H

#include <linux/types.h>

/*
 * GKI 5.10–6.12 (arm64, ANDROID_VENDOR_OEM_DATA=y, no debug):
 *   atomic_long_t owner                   8 bytes
 *   raw_spinlock_t wait_lock              4 bytes (qspinlock)
 *   struct optimistic_spin_queue osq      4 bytes (atomic_t)
 *   struct list_head wait_list           16 bytes
 *   ANDROID_OEM_DATA_ARRAY(1, 2)         16 bytes
 *   Total = 48 bytes.  64 gives comfortable headroom.
 */
struct mutex { unsigned char __opaque[64]; };

#define DEFINE_MUTEX(name) struct mutex name = { { 0 } }
#define __MUTEX_INITIALIZER(name) { { 0 } }

void mutex_init(struct mutex *lock);
void mutex_destroy(struct mutex *lock);
void mutex_lock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);
int mutex_trylock(struct mutex *lock);
int mutex_lock_interruptible(struct mutex *lock);
int mutex_is_locked(struct mutex *lock);

#endif /* _NEVERC_KRT_LINUX_MUTEX_H */
