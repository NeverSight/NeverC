/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_MUTEX_H
#define _NVK_LINUX_MUTEX_H

#include <linux/types.h>

struct mutex { unsigned char __opaque[32]; };

void mutex_init(struct mutex *lock);
void mutex_lock(struct mutex *lock);
void mutex_unlock(struct mutex *lock);
int mutex_trylock(struct mutex *lock);

#endif /* _NVK_LINUX_MUTEX_H */
