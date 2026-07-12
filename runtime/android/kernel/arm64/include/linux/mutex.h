/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_MUTEX_H
#define _NEVERC_KRT_LINUX_MUTEX_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <nvkmod_version.h>

/*
 * GKI 5.10–6.12 (arm64, ANDROID_VENDOR_OEM_DATA=y, no debug):
 *   atomic_long_t owner                   8 bytes
 *   raw_spinlock_t wait_lock              4 bytes (qspinlock)
 *   struct optimistic_spin_queue osq      4 bytes (atomic_t)
 *   struct list_head wait_list           16 bytes
 *   ANDROID_OEM_DATA_ARRAY(1, 2)         16 bytes
 *   Total = 48 bytes.
 * GKI 6.18 keeps one 8-byte OEM reserve, for a total of 40 bytes.
 */
struct optimistic_spin_queue {
	atomic_t tail;
};

struct mutex {
	unsigned long owner;
	raw_spinlock_t wait_lock;
	struct optimistic_spin_queue osq;
	struct list_head wait_list;
#if NEVERC_KRT_KERNEL >= 618
	u64 __kabi_reserved1;
#else
	u64 __kabi_reserved1;
	u64 __kabi_reserved2;
#endif
};

_Static_assert(__builtin_offsetof(struct mutex, owner) == 0,
	       "unexpected GKI mutex owner offset");
_Static_assert(__builtin_offsetof(struct mutex, wait_lock) == 8,
	       "unexpected GKI mutex wait_lock offset");
_Static_assert(__builtin_offsetof(struct mutex, osq) == 12,
	       "unexpected GKI mutex osq offset");
_Static_assert(__builtin_offsetof(struct mutex, wait_list) == 16,
	       "unexpected GKI mutex wait_list offset");
#if NEVERC_KRT_KERNEL >= 618
_Static_assert(sizeof(struct mutex) == 40,
	       "unexpected GKI 6.18 mutex layout");
#else
_Static_assert(sizeof(struct mutex) == 48,
	       "unexpected GKI 5.10-6.12 mutex layout");
#endif

#define __MUTEX_INITIALIZER(name) {                                           \
	.owner = 0,                                                           \
	.wait_lock = __SPIN_LOCK_UNLOCKED(name.wait_lock),                    \
	.osq = { .tail = ATOMIC_INIT(0) },                                    \
	.wait_list = LIST_HEAD_INIT(name.wait_list),                          \
}
#define DEFINE_MUTEX(name) struct mutex name = __MUTEX_INITIALIZER(name)

/*
 * mutex_init / mutex_destroy are never exported.
 *   mutex_init is always a macro → __mutex_init (real export).
 *   mutex_destroy is a no-op in GKI (CONFIG_DEBUG_LOCK_ALLOC=n).
 */
struct lock_class_key;
void __mutex_init(struct mutex *lock, const char *name,
		  struct lock_class_key *key);
#define mutex_init(lock)                                                      \
	__mutex_init((lock), #lock, (struct lock_class_key *)0)
#define mutex_destroy(lock) do { (void)(lock); } while (0)
void mutex_lock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);
int mutex_trylock(struct mutex *lock);
int mutex_lock_interruptible(struct mutex *lock);
static __always_inline int mutex_is_locked(const struct mutex *lock)
{
	return __atomic_load_n(&lock->owner, __ATOMIC_RELAXED) != 0;
}

#endif /* _NEVERC_KRT_LINUX_MUTEX_H */
