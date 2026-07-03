/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SLAB_H
#define _NEVERC_KRT_LINUX_SLAB_H

#include <linux/types.h>
#include <linux/gfp.h>
#include <nvkmod_version.h>

/*
 * Slab allocator — version-aware declarations.
 *
 * The kernel never exports `kmalloc` / `kzalloc` as plain symbols.
 * User-visible names are always inline macros that call:
 *
 *   5.10–6.6:  __kmalloc(size, flags)
 *   6.12+:     __kmalloc_noprof(size, flags)
 *
 * krealloc changed signature in 6.18:
 *   5.10–6.6:  krealloc(p, new_size, flags)
 *   6.12:      krealloc_noprof(p, new_size, flags)
 *   6.18+:     krealloc_node_align_noprof(p, new_size, flags, node, align)
 *
 * vmalloc/vzalloc follow the same _noprof split at 6.12.
 */

/* ---- kmalloc / kzalloc / kcalloc ---- */

#if NEVERC_KRT_KERNEL >= 612
void *__kmalloc_noprof(size_t size, gfp_t flags);
#define kmalloc(size, flags) __kmalloc_noprof(size, flags)
#else
void *__kmalloc(size_t size, gfp_t flags);
#define kmalloc(size, flags) __kmalloc(size, flags)
#endif

#define kzalloc(size, flags) kmalloc(size, (flags) | __GFP_ZERO)
#define kcalloc(n, size, flags) kmalloc((n) * (size), (flags) | __GFP_ZERO)

/* ---- krealloc ---- */

#if NEVERC_KRT_KERNEL >= 618
void *krealloc_node_align_noprof(const void *p, size_t new_size,
				 gfp_t flags, int node, unsigned long align);
#define krealloc(p, new_size, flags) \
	krealloc_node_align_noprof(p, new_size, flags, -1, 0)
#elif NEVERC_KRT_KERNEL >= 612
void *krealloc_noprof(const void *p, size_t new_size, gfp_t flags);
#define krealloc(p, new_size, flags) krealloc_noprof(p, new_size, flags)
#else
void *krealloc(const void *p, size_t new_size, gfp_t flags);
#endif

/* ---- kfree (stable across 5.10–6.18) ---- */

void kfree(const void *objp);

/* ---- vmalloc / vzalloc ---- */

#if NEVERC_KRT_KERNEL >= 612
void *vmalloc_noprof(unsigned long size);
void *vzalloc_noprof(unsigned long size);
#define vmalloc(size) vmalloc_noprof(size)
#define vzalloc(size) vzalloc_noprof(size)
#else
void *vmalloc(unsigned long size);
void *vzalloc(unsigned long size);
#endif

void vfree(const void *addr);

/* ---- kvmalloc / kvfree ---- */

#if NEVERC_KRT_KERNEL >= 612
void *__kvmalloc_node_noprof(size_t size, gfp_t flags, int node);
#define kvmalloc(size, flags) __kvmalloc_node_noprof(size, flags, -1)
#else
void *kvmalloc_node(size_t size, gfp_t flags, int node);
#define kvmalloc(size, flags) kvmalloc_node(size, flags, -1)
#endif

void kvfree(const void *addr);

#endif /* _NEVERC_KRT_LINUX_SLAB_H */
