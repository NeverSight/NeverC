/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_MUTEX_H
#define _NEVERC_KRT_LINUX_MUTEX_H

#include <linux/types.h>

/*
 * GKI 5.10–6.18 (arm64, ANDROID_VENDOR_OEM_DATA=y, no debug):
 *   atomic_long_t owner                   8 bytes
 *   raw_spinlock_t wait_lock              4 bytes (qspinlock)
 *   struct optimistic_spin_queue osq      4 bytes (atomic_t)
 *   struct list_head wait_list           16 bytes
 *   ANDROID_OEM_DATA_ARRAY(1, 2)         16 bytes
 *   Total = 48 bytes.  64 gives comfortable headroom.
 */
struct mutex { unsigned char __opaque[64]; };

_Static_assert(sizeof(struct mutex) >= 48,
	       "struct mutex too small for GKI with ANDROID_VENDOR_OEM_DATA");

#define DEFINE_MUTEX(name) struct mutex name = { { 0 } }
#define __MUTEX_INITIALIZER(name) { { 0 } }

/*
 * mutex_init / mutex_destroy are never exported.
 *   mutex_init is always a macro → __mutex_init (real export).
 *   mutex_destroy is a no-op in GKI (CONFIG_DEBUG_LOCK_ALLOC=n).
 */
void __mutex_init(struct mutex *lock, const char *name, void *key);
#define mutex_init(lock) __mutex_init(lock, "?", (void *)0)
#define mutex_destroy(lock) do { (void)(lock); } while (0)
void mutex_lock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);
int mutex_trylock(struct mutex *lock);
int mutex_lock_interruptible(struct mutex *lock);
int mutex_is_locked(struct mutex *lock);

#endif /* _NEVERC_KRT_LINUX_MUTEX_H */
