/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_VMALLOC_H
#define _NEVERC_KRT_LINUX_VMALLOC_H

#include <linux/slab.h>

#if NEVERC_KRT_KERNEL >= 612
void *vmalloc_user_noprof(unsigned long size);
void *__vmalloc_noprof(unsigned long size, gfp_t gfp_mask);

#define vmalloc_user(size) vmalloc_user_noprof(size)
#define __vmalloc(size, flags) __vmalloc_noprof((size), (flags))
#else
void *vmalloc_user(unsigned long size);
void *__vmalloc(unsigned long size, gfp_t gfp_mask);
#endif

#endif /* _NEVERC_KRT_LINUX_VMALLOC_H */
