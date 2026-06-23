/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_KREF_H
#define _NEVERC_KRT_LINUX_KREF_H

#include <linux/types.h>
#include <linux/compiler.h>

struct kref { atomic_t refcount; };

__always_inline void kref_init(struct kref *kref)
{ kref->refcount.counter = 1; }

__always_inline void kref_get(struct kref *kref)
{ __atomic_add_fetch(&kref->refcount.counter, 1, __ATOMIC_RELAXED); }

__always_inline int kref_put(struct kref *kref,
				    void (*release)(struct kref *))
{
	if (__atomic_sub_fetch(&kref->refcount.counter, 1,
			      __ATOMIC_ACQ_REL) == 0) {
		release(kref);
		return 1;
	}
	return 0;
}

#endif /* _NEVERC_KRT_LINUX_KREF_H */
