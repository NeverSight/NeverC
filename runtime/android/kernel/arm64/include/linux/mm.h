/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_MM_H
#define _NEVERC_KRT_LINUX_MM_H

#include <linux/types.h>
#include <asm/page.h>

struct vm_area_struct; /* opaque */
struct page;           /* opaque */
struct mm_struct;      /* opaque */

#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & PAGE_MASK)

void *memdup_user(const void __user *src, size_t len);

#endif /* _NEVERC_KRT_LINUX_MM_H */
