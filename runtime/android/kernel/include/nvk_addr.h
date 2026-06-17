/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_ADDR_H
#define NEVERC_KRT_ADDR_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <nvk_mem.h>

int neverc_krt_addr_init(void);


static __always_inline unsigned long neverc_krt_va_bits(void)
{
	unsigned long tcr;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	return 64 - ((tcr >> 16) & 0x3FUL);
}

static __always_inline int neverc_krt_page_shift(void)
{
	unsigned long tcr;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	u32 tg1 = (tcr >> 30) & 3;
	if (tg1 == 1) return 14;  /* 16K */
	if (tg1 == 2) return 16;  /* 64K */
	return 12;                 /* 4K (default on GKI) */
}

static __always_inline unsigned long neverc_krt_page_size(void)
{
	return 1UL << neverc_krt_page_shift();
}

static __always_inline unsigned long neverc_krt_page_mask(void)
{
	return ~(neverc_krt_page_size() - 1);
}

static __always_inline int neverc_krt_is_kernel_addr(unsigned long addr)
{
	unsigned long bits = neverc_krt_va_bits();
	unsigned long mask = 1UL << (bits - 1);
	return (addr & mask) != 0;
}

static __always_inline int neverc_krt_is_user_addr(unsigned long addr)
{
	return !neverc_krt_is_kernel_addr(addr) && addr != 0;
}

unsigned long neverc_krt_virt_to_phys(unsigned long vaddr);

unsigned long neverc_krt_phys_to_virt(unsigned long paddr);

unsigned long neverc_krt_kimage_voffset(void);

unsigned long neverc_krt_kaslr_offset(void);


unsigned long neverc_krt_translate_user(unsigned long uaddr);


#define NEVERC_KRT_PTE_VALID     (1UL << 0)
#define NEVERC_KRT_PTE_TABLE     (1UL << 1)
#define NEVERC_KRT_PTE_PAGE      (3UL << 0)
#define NEVERC_KRT_PTE_AF        (1UL << 10)
#define NEVERC_KRT_PTE_RO        (1UL << 7)
#define NEVERC_KRT_PTE_UXN       (1UL << 54)
#define NEVERC_KRT_PTE_PXN       (1UL << 53)
#define NEVERC_KRT_PTE_ADDR_MASK 0x0000FFFFFFFFF000UL

struct neverc_krt_pte_info {
	unsigned long pte_val;
	unsigned long phys_addr;
	int           valid;
	int           writable;
	int           executable;
	int           user_accessible;
	int           level;
};

struct neverc_krt_pte_walk_result {
	unsigned long pte_phys;
	unsigned long *pte_virt;
};

int neverc_krt_walk_pgtable(unsigned long vaddr, struct neverc_krt_pte_info *info);


int neverc_krt_walk_pgtable_ex(unsigned long vaddr, struct neverc_krt_pte_info *info,
			       struct neverc_krt_pte_walk_result *result);


static __always_inline unsigned long neverc_krt_strip_tag(unsigned long addr)
{
	return (addr & ((1UL << neverc_krt_va_bits()) - 1))
	     | (addr & (1UL << 63));
}

static __always_inline unsigned long neverc_krt_strip_mte(unsigned long addr)
{
	return addr & ~(0xFFUL << 56);
}

static __always_inline unsigned long neverc_krt_clean_ptr(unsigned long addr)
{
	addr = neverc_krt_strip_mte(addr);
	unsigned long bits = neverc_krt_va_bits();
	unsigned long mask = (1UL << bits) - 1;
	if (addr & (1UL << 63))
		return addr | ~mask;
	return addr & mask;
}

unsigned long neverc_krt_read_ttbr0(void);


unsigned long neverc_krt_read_ttbr1(void);



int neverc_krt_pte_set_rw(unsigned long vaddr);


int neverc_krt_pte_set_ro(unsigned long vaddr);


int neverc_krt_pte_set_exec(unsigned long vaddr);


int neverc_krt_pte_set_rw_range(unsigned long start, unsigned long end);


int neverc_krt_pte_set_ro_range(unsigned long start, unsigned long end);


int neverc_krt_linmap_available(void);

unsigned long neverc_krt_linmap_offset(void);

#endif /* NEVERC_KRT_ADDR_H */
