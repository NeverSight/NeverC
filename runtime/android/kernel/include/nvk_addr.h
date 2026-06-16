/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_ADDR_H
#define NVK_ADDR_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

NVK_RT_VAR unsigned long *_nvk_kimage_voffset_a;
NVK_RT_VAR unsigned long *_nvk_phys_offset;
NVK_RT_VAR int            _nvk_addr_inited;

NVK_RT_VAR unsigned long _nvk_derived_voffset;
NVK_RT_VAR int           _nvk_voffset_derived;

/*
 * kimage_voffset converts between kernel image virtual and physical addresses.
 * Page tables live in the linear map, which uses a DIFFERENT offset.
 * _nvk_linmap_offset = linear_map_virt - physical.
 */
NVK_RT_VAR unsigned long _nvk_linmap_offset;
NVK_RT_VAR int           _nvk_linmap_detected;

NVK_RT_VAR int nvk_pte_set_rw(unsigned long vaddr);
NVK_RT_VAR int nvk_pte_set_ro(unsigned long vaddr);

void _nvk_detect_linmap(void);


static __always_inline unsigned long _nvk_linmap_phys_to_virt(unsigned long pa)
{
	return pa + _nvk_linmap_offset;
}

int nvk_addr_init(void);


static __always_inline unsigned long nvk_va_bits(void)
{
	unsigned long tcr;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	return 64 - ((tcr >> 16) & 0x3FUL);
}

static __always_inline int nvk_page_shift(void)
{
	unsigned long tcr;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	u32 tg1 = (tcr >> 30) & 3;
	if (tg1 == 1) return 14;  /* 16K */
	if (tg1 == 2) return 16;  /* 64K */
	return 12;                 /* 4K (default on GKI) */
}

static __always_inline unsigned long nvk_page_size(void)
{
	return 1UL << nvk_page_shift();
}

static __always_inline unsigned long nvk_page_mask(void)
{
	return ~(nvk_page_size() - 1);
}

static __always_inline int nvk_is_kernel_addr(unsigned long addr)
{
	unsigned long bits = nvk_va_bits();
	unsigned long mask = 1UL << (bits - 1);
	return (addr & mask) != 0;
}

static __always_inline int nvk_is_user_addr(unsigned long addr)
{
	return !nvk_is_kernel_addr(addr) && addr != 0;
}

static __always_inline unsigned long _nvk_get_voffset(void)
{
	if (_nvk_kimage_voffset_a)
		return *_nvk_kimage_voffset_a;
	if (_nvk_voffset_derived)
		return _nvk_derived_voffset;
	return 0;
}

static __always_inline unsigned long nvk_virt_to_phys(unsigned long vaddr)
{
	unsigned long off = _nvk_get_voffset();
	if (off)
		return vaddr - off;

	unsigned long par;
	__asm__ __volatile__(
		"at s1e1r, %1\n"
		"isb\n"
		"mrs %0, par_el1\n"
		: "=r"(par)
		: "r"(vaddr)
		: "memory");

	if (par & 1)
		return 0;

	return (par & 0x0000FFFFFFFFF000UL) | (vaddr & 0xFFF);
}

static __always_inline unsigned long nvk_phys_to_virt(unsigned long paddr)
{
	unsigned long off = _nvk_get_voffset();
	if (off)
		return paddr + off;
	return 0;
}

static __always_inline unsigned long nvk_kimage_voffset(void)
{
	return _nvk_get_voffset();
}

unsigned long nvk_kaslr_offset(void);


unsigned long nvk_translate_user(unsigned long uaddr);


#define NVK_PTE_VALID     (1UL << 0)
#define NVK_PTE_TABLE     (1UL << 1)
#define NVK_PTE_PAGE      (3UL << 0)
#define NVK_PTE_AF        (1UL << 10)
#define NVK_PTE_RO        (1UL << 7)
#define NVK_PTE_UXN       (1UL << 54)
#define NVK_PTE_PXN       (1UL << 53)
#define NVK_PTE_ADDR_MASK 0x0000FFFFFFFFF000UL

struct nvk_pte_info {
	unsigned long pte_val;
	unsigned long phys_addr;
	int           valid;
	int           writable;
	int           executable;
	int           user_accessible;
	int           level;
};

struct nvk_pte_walk_result {
	unsigned long pte_phys;
	unsigned long *pte_virt;
};

NVK_RT_VAR int nvk_walk_pgtable_ex(unsigned long vaddr, struct nvk_pte_info *info,
			       struct nvk_pte_walk_result *result);

int nvk_walk_pgtable(unsigned long vaddr, struct nvk_pte_info *info);


int nvk_walk_pgtable_ex(unsigned long vaddr, struct nvk_pte_info *info,
			       struct nvk_pte_walk_result *result);


static __always_inline unsigned long nvk_strip_tag(unsigned long addr)
{
	return (addr & ((1UL << nvk_va_bits()) - 1))
	     | (addr & (1UL << 63));
}

static __always_inline unsigned long nvk_strip_mte(unsigned long addr)
{
	return addr & ~(0xFFUL << 56);
}

static __always_inline unsigned long nvk_clean_ptr(unsigned long addr)
{
	addr = nvk_strip_mte(addr);
	unsigned long bits = nvk_va_bits();
	unsigned long mask = (1UL << bits) - 1;
	if (addr & (1UL << 63))
		return addr | ~mask;
	return addr & mask;
}

unsigned long nvk_read_ttbr0(void);


unsigned long nvk_read_ttbr1(void);



int nvk_pte_set_rw(unsigned long vaddr);


int nvk_pte_set_ro(unsigned long vaddr);


int nvk_pte_set_exec(unsigned long vaddr);


int nvk_pte_set_rw_range(unsigned long start, unsigned long end);


int nvk_pte_set_ro_range(unsigned long start, unsigned long end);


static __always_inline int nvk_linmap_available(void)
{
	return _nvk_linmap_detected;
}

static __always_inline unsigned long nvk_linmap_offset(void)
{
	return _nvk_linmap_offset;
}

#endif /* NVK_ADDR_H */
