/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_ASM_PAGE_H
#define _NEVERC_KRT_ASM_PAGE_H

/*
 * Compile-time default for struct sizing only.
 * GKI builds use 4K pages (PAGE_SHIFT=12) on all supported arm64 targets.
 * Use neverc_krt_page_size() / neverc_krt_page_shift() from nvk_addr.h
 * for the runtime-correct value — some vendor kernels use 16K.
 */
#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#define PAGE_SIZE  (1UL << PAGE_SHIFT)
#define PAGE_MASK  (~(PAGE_SIZE - 1))

#define PFN_UP(x)    (((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_DOWN(x)  ((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)  ((unsigned long)(x) << PAGE_SHIFT)

#endif /* _NEVERC_KRT_ASM_PAGE_H */
