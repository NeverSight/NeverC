/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SPINLOCK_H
#define _NEVERC_KRT_LINUX_SPINLOCK_H

#include <linux/types.h>
#include <linux/compiler.h>

/*
 * Lock opaque blob sizes — GKI 5.10–6.18 (arm64, production):
 *   arch_spinlock_t = qspinlock { atomic_t val; } = 4 bytes
 *   raw_spinlock_t  = arch_spinlock_t             = 4 bytes
 *   spinlock_t      = raw_spinlock_t              = 4 bytes
 *   rwlock_t        = arch_rwlock_t (qrwlock)     = 4 bytes
 *
 * GKI defconfigs do NOT enable CONFIG_DEBUG_SPINLOCK,
 * CONFIG_DEBUG_LOCK_ALLOC, or CONFIG_PREEMPT_RT.
 * These types are embedded in public kernel structures, so their size must be
 * exact rather than a maximum-capacity opaque buffer.
 */
typedef struct { unsigned int val; } spinlock_t;
typedef struct { unsigned int val; } raw_spinlock_t;
typedef struct { unsigned int val; } rwlock_t;

_Static_assert(sizeof(spinlock_t) == 4,
	       "spinlock_t must match the arm64 qspinlock");
_Static_assert(sizeof(raw_spinlock_t) == 4,
	       "raw_spinlock_t must match the arm64 qspinlock");
_Static_assert(sizeof(rwlock_t) == 4,
	       "rwlock_t must match the arm64 qrwlock");

#define DEFINE_SPINLOCK(x) spinlock_t x = { 0 }
#define __SPIN_LOCK_UNLOCKED(x) { 0 }

/*
 * spin_lock / spin_unlock etc. are always inline wrappers in all GKI
 * kernels (5.10–6.18).  The real exports are the _raw_spin_* functions.
 *
 * In GKI production (no DEBUG_SPINLOCK, no PREEMPT_RT):
 *   spin_lock(lock) → _raw_spin_lock(lock)  [includes preempt_disable]
 *   spin_unlock(lock) → _raw_spin_unlock(lock) [includes preempt_enable]
 */
void _raw_spin_lock(raw_spinlock_t *lock);
void _raw_spin_unlock(raw_spinlock_t *lock);
void _raw_spin_lock_bh(raw_spinlock_t *lock);
void _raw_spin_unlock_bh(raw_spinlock_t *lock);
void _raw_spin_lock_irq(raw_spinlock_t *lock);
void _raw_spin_unlock_irq(raw_spinlock_t *lock);
unsigned long _raw_spin_lock_irqsave(raw_spinlock_t *lock);
void _raw_spin_unlock_irqrestore(raw_spinlock_t *lock, unsigned long flags);
int _raw_spin_trylock(raw_spinlock_t *lock);

#define spin_lock_init(lock) \
	do { __builtin_memset((lock), 0, sizeof(*(lock))); } while (0)

#define spin_lock(lock) \
	_raw_spin_lock((raw_spinlock_t *)(lock))
#define spin_unlock(lock) \
	_raw_spin_unlock((raw_spinlock_t *)(lock))
#define spin_lock_bh(lock) \
	_raw_spin_lock_bh((raw_spinlock_t *)(lock))
#define spin_unlock_bh(lock) \
	_raw_spin_unlock_bh((raw_spinlock_t *)(lock))
#define spin_lock_irq(lock) \
	_raw_spin_lock_irq((raw_spinlock_t *)(lock))
#define spin_unlock_irq(lock) \
	_raw_spin_unlock_irq((raw_spinlock_t *)(lock))
#define spin_trylock(lock) \
	_raw_spin_trylock((raw_spinlock_t *)(lock))

#define spin_lock_irqsave(lock, flags) \
	do { flags = _raw_spin_lock_irqsave((raw_spinlock_t *)(lock)); } while (0)
#define spin_unlock_irqrestore(lock, flags) \
	_raw_spin_unlock_irqrestore((raw_spinlock_t *)(lock), flags)

static __always_inline int spin_is_locked(spinlock_t *lock)
{
	return __atomic_load_n((int *)lock, __ATOMIC_RELAXED) != 0;
}

/* rwlock — _raw_read/write_lock are real exports in all GKI versions. */
void _raw_read_lock(rwlock_t *lock);
void _raw_read_unlock(rwlock_t *lock);
void _raw_write_lock(rwlock_t *lock);
void _raw_write_unlock(rwlock_t *lock);

#define read_lock(lock)    _raw_read_lock((rwlock_t *)(lock))
#define read_unlock(lock)  _raw_read_unlock((rwlock_t *)(lock))
#define write_lock(lock)   _raw_write_lock((rwlock_t *)(lock))
#define write_unlock(lock) _raw_write_unlock((rwlock_t *)(lock))

#endif /* _NEVERC_KRT_LINUX_SPINLOCK_H */
