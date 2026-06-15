/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_ADDR_H
#define NVK_ADDR_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

static unsigned long *_nvk_kimage_voffset_a;
static unsigned long *_nvk_phys_offset;
static int            _nvk_addr_inited;

static int nvk_addr_init(void)
{
	if (_nvk_addr_inited) return 0;

	if (!_nvk_mem_inited)
		nvk_mem_init();

	_nvk_kimage_voffset_a =
		(unsigned long *)NVK_LOOKUP("kimage_voffset");
	_nvk_phys_offset =
		(unsigned long *)NVK_LOOKUP("memstart_addr");

	_nvk_addr_inited = 1;
	return 0;
}

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

static __always_inline unsigned long nvk_virt_to_phys(unsigned long vaddr)
{
	if (_nvk_kimage_voffset_a)
		return vaddr - *_nvk_kimage_voffset_a;

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
	if (_nvk_kimage_voffset_a)
		return paddr + *_nvk_kimage_voffset_a;
	return 0;
}

static __always_inline unsigned long nvk_kaslr_offset(void)
{
	unsigned long kaddr = (unsigned long)nvk_kaslr_offset;
	unsigned long vaddr = kaddr;
	unsigned long paddr = nvk_virt_to_phys(vaddr);
	if (!paddr) return 0;
	if (_nvk_kimage_voffset_a)
		return *_nvk_kimage_voffset_a;
	return 0;
}

static unsigned long nvk_translate_user(unsigned long uaddr)
{
	unsigned long par;
	__asm__ __volatile__(
		"at s1e0r, %1\n"
		"isb\n"
		"mrs %0, par_el1\n"
		: "=r"(par)
		: "r"(uaddr)
		: "memory");

	if (par & 1)
		return 0;

	return (par & 0x0000FFFFFFFFF000UL) | (uaddr & 0xFFF);
}

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

static int nvk_walk_pgtable(unsigned long vaddr, struct nvk_pte_info *info)
{
	unsigned long ttbr, table_addr, entry;
	int shift = nvk_page_shift();
	int bits = (int)nvk_va_bits();
	int level, levels;
	int idx_bits = shift - 3;

	if (!info) return -1;
	if (!_nvk_kimage_voffset_a) return -5;

	info->valid = 0;
	info->pte_val = 0;
	info->phys_addr = 0;
	info->writable = 0;
	info->executable = 0;
	info->user_accessible = 0;
	info->level = -1;

	if (vaddr & (1UL << 63))
		__asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(ttbr));
	else
		__asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(ttbr));

	table_addr = ttbr & NVK_PTE_ADDR_MASK;

	if (shift == 12)       levels = (bits - 12 + 8) / 9;
	else if (shift == 14)  levels = (bits - 14 + 10) / 11;
	else                   levels = (bits - 16 + 12) / 13;

	for (level = 4 - levels; level < 4; level++) {
		int s;
		unsigned long idx, mask;

		s = shift + idx_bits * (3 - level);

		mask = (1UL << idx_bits) - 1;
		idx = (vaddr >> s) & mask;

		unsigned long pte_phys = table_addr + idx * 8;
		unsigned long pte_addr;
		if (_nvk_kimage_voffset_a)
			pte_addr = pte_phys + *_nvk_kimage_voffset_a;
		else
			pte_addr = pte_phys;

		if (nvk_mem_read(&entry, (void *)pte_addr, 8))
			return -2;

		if (!(entry & NVK_PTE_VALID))
			return -3;

		if (level < 3 && (entry & NVK_PTE_TABLE)) {
			table_addr = entry & NVK_PTE_ADDR_MASK;
			continue;
		}

		info->pte_val = entry;
		info->level = level;
		info->valid = 1;

		if (level == 3)
			info->phys_addr = (entry & NVK_PTE_ADDR_MASK)
					  | (vaddr & ((1UL << shift) - 1));
		else
			info->phys_addr = (entry & NVK_PTE_ADDR_MASK)
					  | (vaddr & ((1UL << s) - 1));

		info->writable = !(entry & NVK_PTE_RO);
		info->executable = !(entry & NVK_PTE_UXN)
				   || !(entry & NVK_PTE_PXN);
		info->user_accessible = ((entry >> 6) & 1) != 0;
		return 0;
	}

	return -4;
}

static __always_inline unsigned long nvk_strip_tag(unsigned long addr)
{
	return (addr & ((1UL << nvk_va_bits()) - 1))
	     | (addr & (1UL << 63));
}

static unsigned long nvk_read_ttbr0(void)
{
	unsigned long v;
	__asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(v));
	return v;
}

static unsigned long nvk_read_ttbr1(void)
{
	unsigned long v;
	__asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(v));
	return v;
}

#endif /* NVK_ADDR_H */
