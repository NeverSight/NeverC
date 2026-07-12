/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_ASM_PAGE_H
#define _NEVERC_KRT_ASM_PAGE_H

#include <nvkmod_version.h>

/*
 * Compile-time profile fact for ABI sizing.
 * Use neverc_krt_page_size() / neverc_krt_page_shift() from nvk_addr.h
 * when inspecting the running kernel.
 */
#ifndef PAGE_SHIFT
#define PAGE_SHIFT NEVERC_KRT_PAGE_SHIFT
#endif
#define PAGE_SIZE  (1UL << PAGE_SHIFT)
#define PAGE_MASK  (~(PAGE_SIZE - 1))

#define PFN_UP(x)    (((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_DOWN(x)  ((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)  ((unsigned long)(x) << PAGE_SHIFT)

#endif /* _NEVERC_KRT_ASM_PAGE_H */
