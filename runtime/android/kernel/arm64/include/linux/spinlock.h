/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_SPINLOCK_H
#define _NVK_LINUX_SPINLOCK_H

#include <linux/types.h>

typedef struct { unsigned char __opaque[8]; } spinlock_t;
typedef struct { unsigned char __opaque[8]; } raw_spinlock_t;

void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
void spin_lock_init(spinlock_t *lock);
unsigned long _raw_spin_lock_irqsave(raw_spinlock_t *lock);
void _raw_spin_unlock_irqrestore(raw_spinlock_t *lock, unsigned long flags);

#endif /* _NVK_LINUX_SPINLOCK_H */
