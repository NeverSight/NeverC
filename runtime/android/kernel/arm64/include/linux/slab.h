/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_SLAB_H
#define _NVK_LINUX_SLAB_H

#include <linux/types.h>
#include <linux/gfp.h>

void *kmalloc(size_t size, gfp_t flags);
void *kzalloc(size_t size, gfp_t flags);
void *kcalloc(size_t n, size_t size, gfp_t flags);
void *krealloc(const void *p, size_t new_size, gfp_t flags);
void kfree(const void *objp);
void *vmalloc(unsigned long size);
void *vzalloc(unsigned long size);
void vfree(const void *addr);
void *kvmalloc(size_t size, gfp_t flags);
void kvfree(const void *addr);

#endif /* _NVK_LINUX_SLAB_H */
