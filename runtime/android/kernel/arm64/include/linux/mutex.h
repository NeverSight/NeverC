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
 * Linux 7.2 replaces the list with first_waiter and removes the reserve (24).
 */
struct optimistic_spin_queue {
	atomic_t tail;
};

struct mutex_waiter;
struct mutex {
	unsigned long owner;
	raw_spinlock_t wait_lock;
	struct optimistic_spin_queue osq;
#if NEVERC_KRT_LINUX_AT_LEAST(7, 2, 0)
	struct mutex_waiter *first_waiter;
#else
	struct list_head wait_list;
#if NEVERC_KRT_LINUX_AT_LEAST(6, 18, 0)
	u64 __kabi_reserved1;
#else
	u64 __kabi_reserved1;
	u64 __kabi_reserved2;
#endif
#endif
};

_Static_assert(__builtin_offsetof(struct mutex, owner) == 0,
	       "unexpected GKI mutex owner offset");
_Static_assert(__builtin_offsetof(struct mutex, wait_lock) == 8,
	       "unexpected GKI mutex wait_lock offset");
_Static_assert(__builtin_offsetof(struct mutex, osq) == 12,
	       "unexpected GKI mutex osq offset");
#if NEVERC_KRT_LINUX_AT_LEAST(7, 2, 0)
_Static_assert(__builtin_offsetof(struct mutex, first_waiter) == 16,
	       "unexpected GKI mutex first_waiter offset");
_Static_assert(sizeof(struct mutex) == 24,
	       "unexpected GKI 7.2 mutex layout");
#else
_Static_assert(__builtin_offsetof(struct mutex, wait_list) == 16,
	       "unexpected GKI mutex wait_list offset");
#if NEVERC_KRT_LINUX_AT_LEAST(6, 18, 0)
_Static_assert(sizeof(struct mutex) == 40,
	       "unexpected GKI 6.18 mutex layout");
#else
_Static_assert(sizeof(struct mutex) == 48,
	       "unexpected GKI 5.10-6.12 mutex layout");
#endif
#endif

#if NEVERC_KRT_LINUX_AT_LEAST(7, 2, 0)
#define __MUTEX_INITIALIZER(name) {                                           \
	.owner = 0,                                                           \
	.wait_lock = __SPIN_LOCK_UNLOCKED(name.wait_lock),                    \
	.osq = { .tail = ATOMIC_INIT(0) },                                    \
	.first_waiter = (void *)0,                                            \
}
#else
#define __MUTEX_INITIALIZER(name) {                                           \
	.owner = 0,                                                           \
	.wait_lock = __SPIN_LOCK_UNLOCKED(name.wait_lock),                    \
	.osq = { .tail = ATOMIC_INIT(0) },                                    \
	.wait_list = LIST_HEAD_INIT(name.wait_list),                          \
}
#endif
#define DEFINE_MUTEX(name) struct mutex name = __MUTEX_INITIALIZER(name)

/*
 * mutex_init / mutex_destroy are never exported.
 *   mutex_init calls __mutex_init before 7.2, mutex_init_generic on 7.2+.
 *   mutex_destroy is a no-op in GKI (CONFIG_DEBUG_LOCK_ALLOC=n).
 */
struct lock_class_key;
#if NEVERC_KRT_LINUX_AT_LEAST(7, 2, 0)
void mutex_init_generic(struct mutex *lock);
#define mutex_init(lock) mutex_init_generic(lock)
#else
void __mutex_init(struct mutex *lock, const char *name,
		  struct lock_class_key *key);
#define mutex_init(lock)                                                      \
	__mutex_init((lock), #lock, (struct lock_class_key *)0)
#endif
#define mutex_destroy(lock) do { (void)(lock); } while (0)
void mutex_lock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);
int mutex_trylock(struct mutex *lock);
int mutex_lock_interruptible(struct mutex *lock);
#if NEVERC_KRT_LINUX_AT_LEAST(7, 2, 0)
bool mutex_is_locked(struct mutex *lock);
#else
static __always_inline int mutex_is_locked(const struct mutex *lock)
{
	return __atomic_load_n(&lock->owner, __ATOMIC_RELAXED) != 0;
}
#endif

#endif /* _NEVERC_KRT_LINUX_MUTEX_H */
