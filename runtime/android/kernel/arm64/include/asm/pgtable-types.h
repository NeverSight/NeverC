/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_PGTABLE_TYPES_H
#define _ASM_ARM64_PGTABLE_TYPES_H

#include <linux/types.h>

/*
 * Minimal arm64 page-table protection ABI.
 *
 * Android GKI 5.10, 5.15, 6.1, 6.6, and 6.12 all pass pgprot_t by value as
 * one 64-bit descriptor.  Keep only that stable calling-convention surface;
 * the SDK does not expose kernel page-table internals here.
 */
typedef u64 pteval_t;

typedef struct {
	pteval_t pgprot;
} pgprot_t;

#define pgprot_val(x)	((x).pgprot)
#define __pgprot(x)	((pgprot_t) { (x) })

#endif /* _ASM_ARM64_PGTABLE_TYPES_H */
