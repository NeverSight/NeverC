/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_MUTEX_H
#define _NEVERC_KRT_LINUX_MUTEX_H

#include <linux/types.h>

struct mutex { unsigned char __opaque[32]; };

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
