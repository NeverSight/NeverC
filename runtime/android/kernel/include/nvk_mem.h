/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_MEM_H
#define NVK_MEM_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>

typedef long (*nvk_probe_read_fn)(void *dst, const void *src, size_t len);
typedef long (*nvk_probe_write_fn)(void *dst, const void *src, size_t len);
typedef unsigned long (*nvk_copy_from_user_fn)(void *to, const void __user *from,
					       unsigned long n);
typedef unsigned long (*nvk_copy_to_user_fn)(void __user *to, const void *from,
					     unsigned long n);
typedef int (*nvk_set_memory_fn)(unsigned long addr, int numpages);
typedef void (*nvk_update_mapping_prot_fn)(u64 phys, unsigned long virt,
					   u64 size, u64 prot);

NVK_RT_VAR nvk_probe_read_fn          _nvk_probe_read;
NVK_RT_VAR nvk_probe_write_fn         _nvk_probe_write;
NVK_RT_VAR nvk_copy_from_user_fn      _nvk_copy_from_user;
NVK_RT_VAR nvk_copy_to_user_fn        _nvk_copy_to_user;
NVK_RT_VAR nvk_set_memory_fn          _nvk_set_memory_rw;
NVK_RT_VAR nvk_set_memory_fn          _nvk_set_memory_ro;
NVK_RT_VAR nvk_update_mapping_prot_fn _nvk_update_prot;
NVK_RT_VAR unsigned long             *_nvk_kimage_voffset;
NVK_RT_VAR int                        _nvk_mem_inited;
NVK_RT_VAR unsigned long              _nvk_mem_page_sz;

typedef int (*_nvk_pte_rw_fn)(unsigned long addr);
NVK_RT_VAR _nvk_pte_rw_fn _nvk_pte_make_rw;
NVK_RT_VAR _nvk_pte_rw_fn _nvk_pte_make_ro;

static __always_inline unsigned long _nvk_strip_tags(unsigned long addr)
{
	return addr & ~(0xFFUL << 56);
}

static __always_inline unsigned long _nvk_mem_get_page_size(void)
{
	if (__builtin_expect(_nvk_mem_page_sz != 0, 1))
		return _nvk_mem_page_sz;
	unsigned long tcr;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	u32 tg1 = (tcr >> 30) & 3;
	unsigned long sz = 4096;
	if (tg1 == 1) sz = 16384;
	else if (tg1 == 2) sz = 65536;
	_nvk_mem_page_sz = sz;
	return sz;
}

static __always_inline unsigned long _nvk_mem_page_align_down(unsigned long addr)
{
	unsigned long sz = _nvk_mem_get_page_size();
	return addr & ~(sz - 1);
}

#define _NVK_PTE_TYPE_PAGE  (3UL << 0)
#define _NVK_PTE_AF         (1UL << 10)
#define _NVK_PTE_SH_IS      (3UL << 8)
#define _NVK_PTE_RDONLY     (1UL << 7)
#define _NVK_PTE_ATTRINDX(x) ((unsigned long)(x) << 2)
#define _NVK_PTE_UXN        (1UL << 54)
#define _NVK_PAGE_KERNEL     (_NVK_PTE_TYPE_PAGE | _NVK_PTE_AF | \
			      _NVK_PTE_SH_IS | _NVK_PTE_ATTRINDX(0) | \
			      _NVK_PTE_UXN)
#define _NVK_PAGE_KERNEL_RO  (_NVK_PAGE_KERNEL | _NVK_PTE_RDONLY)

int nvk_mem_init(void);



long nvk_mem_read(void *dst, const void *src, size_t len);


long nvk_mem_write(void *dst, const void *src, size_t len);



long nvk_mem_read_user(void *dst, const void __user *src, size_t len);


long nvk_mem_write_user(void __user *dst, const void *src, size_t len);



int _nvk_pte_walk_set(unsigned long addr, int writable);


int nvk_mem_make_rw(unsigned long addr);


int nvk_mem_make_ro(unsigned long addr);


int nvk_mem_write_protected(unsigned long addr, const void *src,
				   size_t len);



void *nvk_mem_scan(const void *start, size_t region_len,
			  const void *pattern, size_t pat_len);


void *nvk_mem_scan_mask(const void *start, size_t region_len,
			       const unsigned char *pattern,
			       const unsigned char *mask, size_t pat_len);


#define nvk_mem_read_val(dst, src) \
	nvk_mem_read((dst), (src), sizeof(*(dst)))

#define nvk_mem_write_val(dst, src) \
	nvk_mem_write((dst), (src), sizeof(*(src)))


u8  nvk_mem_read8(const void *addr);

u16 nvk_mem_read16(const void *addr);

u32 nvk_mem_read32(const void *addr);

u64 nvk_mem_read64(const void *addr);


int nvk_mem_cmp(const void *a, const void *b, size_t len);


int nvk_mem_cmp_ct(const void *a, const void *b, size_t len);


void *nvk_mem_scan_safe(const void *start, size_t region_len,
			       const void *pattern, size_t pat_len);


void nvk_mem_fill(void *dst, unsigned char val, size_t len);


void nvk_mem_zero(void *dst, size_t len);


long nvk_mem_read_cross_page(void *dst, const void *src, size_t len);


static __always_inline u64 nvk_mem_atomic_read64(const volatile u64 *addr)
{
	u64 val;
	__asm__ __volatile__("ldar %0, [%1]"
			     : "=r"(val)
			     : "r"(addr)
			     : "memory");
	return val;
}

static __always_inline void nvk_mem_atomic_write64(volatile u64 *addr, u64 val)
{
	__asm__ __volatile__("stlr %1, [%0]"
			     : : "r"(addr), "r"(val)
			     : "memory");
}

static __always_inline u64 nvk_mem_xchg64(volatile u64 *addr, u64 new_val)
{
	u64 old;
	u32 tmp;
	__asm__ __volatile__(
		"1: ldaxr %0, [%2]\n"
		"   stlxr %w1, %3, [%2]\n"
		"   cbnz  %w1, 1b\n"
		: "=&r"(old), "=&r"(tmp)
		: "r"(addr), "r"(new_val)
		: "memory");
	return old;
}

static __always_inline int nvk_mem_cas64(volatile u64 *addr,
					 u64 expected, u64 desired)
{
	u64 old;
	u32 tmp;
	__asm__ __volatile__(
		"1: ldaxr %0, [%2]\n"
		"   cmp   %0, %3\n"
		"   b.ne  2f\n"
		"   stlxr %w1, %4, [%2]\n"
		"   cbnz  %w1, 1b\n"
		"2:\n"
		: "=&r"(old), "=&r"(tmp)
		: "r"(addr), "r"(expected), "r"(desired)
		: "memory", "cc");
	return old == expected;
}

void *nvk_mem_scan_mask_safe(const void *start, size_t region_len,
				    const unsigned char *pattern,
				    const unsigned char *mask, size_t pat_len);


#endif /* NVK_MEM_H */
