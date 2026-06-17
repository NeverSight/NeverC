/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SPINLOCK_H
#define _NEVERC_KRT_LINUX_SPINLOCK_H

#include <linux/types.h>

/*
 * Lock opaque blob sizes — GKI 5.10–6.12 (arm64, production):
 *   arch_spinlock_t = qspinlock { atomic_t val; } = 4 bytes
 *   raw_spinlock_t  = arch_spinlock_t             = 4 bytes
 *   spinlock_t      = raw_spinlock_t              = 4 bytes
 *   rwlock_t        = arch_rwlock_t (qrwlock)     = 4 bytes
 *
 * GKI defconfigs do NOT enable CONFIG_DEBUG_SPINLOCK,
 * CONFIG_DEBUG_LOCK_ALLOC, or CONFIG_PREEMPT_RT.
 * 8 bytes per lock gives comfortable headroom.
 */
typedef struct { unsigned char __opaque[8]; } spinlock_t;
typedef struct { unsigned char __opaque[8]; } raw_spinlock_t;
typedef struct { unsigned char __opaque[8]; } rwlock_t;

#define DEFINE_SPINLOCK(x) spinlock_t x = { { 0 } }
#define __SPIN_LOCK_UNLOCKED(x) { { 0 } }

void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
void spin_lock_init(spinlock_t *lock);
void spin_lock_bh(spinlock_t *lock);
void spin_unlock_bh(spinlock_t *lock);
void spin_lock_irq(spinlock_t *lock);
void spin_unlock_irq(spinlock_t *lock);
unsigned long _raw_spin_lock_irqsave(raw_spinlock_t *lock);
void _raw_spin_unlock_irqrestore(raw_spinlock_t *lock, unsigned long flags);

#define spin_lock_irqsave(lock, flags)                                   \
	do { flags = _raw_spin_lock_irqsave((raw_spinlock_t *)(lock)); } while (0)
#define spin_unlock_irqrestore(lock, flags)                              \
	_raw_spin_unlock_irqrestore((raw_spinlock_t *)(lock), flags)

int spin_trylock(spinlock_t *lock);
int spin_is_locked(spinlock_t *lock);

void read_lock(rwlock_t *lock);
void read_unlock(rwlock_t *lock);
void write_lock(rwlock_t *lock);
void write_unlock(rwlock_t *lock);

#endif /* _NEVERC_KRT_LINUX_SPINLOCK_H */
