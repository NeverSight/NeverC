/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_ASM_PAGE_H
#define _NEVERC_KRT_ASM_PAGE_H

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)
#define PAGE_MASK  (~(PAGE_SIZE - 1))

#define PFN_UP(x)    (((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_DOWN(x)  ((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)  ((unsigned long)(x) << PAGE_SHIFT)

#endif /* _NEVERC_KRT_ASM_PAGE_H */
