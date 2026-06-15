/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_VMALLOC_H
#define _NVK_LINUX_VMALLOC_H

#include <linux/types.h>
#include <linux/gfp.h>

void *vmalloc(unsigned long size);
void *vzalloc(unsigned long size);
void *vmalloc_user(unsigned long size);
void vfree(const void *addr);

void *__vmalloc(unsigned long size, gfp_t gfp_mask);

#endif /* _NVK_LINUX_VMALLOC_H */
