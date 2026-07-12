/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_IDR_H
#define _NEVERC_KRT_LINUX_IDR_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <nvkmod_version.h>

/*
 * Minimal arm64 GKI view of struct idr.  struct radix_tree_root is an xarray
 * alias in every supported kernel: raw_spinlock_t(4), gfp_t(4), head(8).
 */
struct idr {
	u32 __xa_lock;
	gfp_t __xa_flags;
	void *__xa_head;
	unsigned int __idr_base;
	unsigned int __idr_next;
};

_Static_assert(sizeof(struct idr) == 24, "unexpected GKI idr layout");
_Static_assert(__builtin_offsetof(struct idr, __xa_flags) == 4,
	       "unexpected GKI idr xarray flags offset");
_Static_assert(__builtin_offsetof(struct idr, __idr_base) == 16,
	       "unexpected GKI idr base offset");

/*
 * IDR_RT_MARKER is ROOT_IS_IDR | XA_FLAGS_MARK(IDR_FREE).  Its high-bit
 * position follows the official GKI gfp_t configuration for each profile.
 */
#if NEVERC_KRT_KERNEL == 510
#define __NEVERC_KRT_IDR_GFP_SHIFT 26
#elif NEVERC_KRT_KERNEL == 515 || NEVERC_KRT_KERNEL == 601
#define __NEVERC_KRT_IDR_GFP_SHIFT 28
#elif NEVERC_KRT_KERNEL == 606
#define __NEVERC_KRT_IDR_GFP_SHIFT 27
#else /* 6.12 / 6.18 */
#define __NEVERC_KRT_IDR_GFP_SHIFT 28
#endif

static __always_inline void idr_init_base(struct idr *idr, int base)
{
	idr->__xa_lock = 0;
	idr->__xa_flags =
		(gfp_t)(4U | (1U << __NEVERC_KRT_IDR_GFP_SHIFT));
	idr->__xa_head = (void *)0;
	idr->__idr_base = (unsigned int)base;
	idr->__idr_next = 0;
}

static __always_inline void idr_init(struct idr *idr)
{
	idr_init_base(idr, 0);
}

#undef __NEVERC_KRT_IDR_GFP_SHIFT

void idr_destroy(struct idr *idr);
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp);
void *idr_find(const struct idr *idr, unsigned long id);
void *idr_remove(struct idr *idr, unsigned long id);
void *idr_get_next(struct idr *idr, int *nextid);

#define idr_for_each_entry(idr, entry, id)                                    \
	for ((id) = 0;                                                        \
	     ((entry) = idr_get_next((idr), &(id))) != (void *)0;             \
	     ++(id))

#endif /* _NEVERC_KRT_LINUX_IDR_H */
