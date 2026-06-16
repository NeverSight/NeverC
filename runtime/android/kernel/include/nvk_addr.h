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

static unsigned long _nvk_derived_voffset;
static int           _nvk_voffset_derived;

/*
 * kimage_voffset converts between kernel image virtual and physical addresses.
 * Page tables live in the linear map, which uses a DIFFERENT offset.
 * _nvk_linmap_offset = linear_map_virt - physical.
 */
static unsigned long _nvk_linmap_offset;
static int           _nvk_linmap_detected;

static int nvk_pte_set_rw(unsigned long vaddr);
static int nvk_pte_set_ro(unsigned long vaddr);

static void _nvk_detect_linmap(void)
{
	if (_nvk_linmap_detected) return;

	void *swapper = NVK_LOOKUP("swapper_pg_dir");
	if (swapper) {
		unsigned long ttbr1;
		__asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(ttbr1));
		unsigned long pgd_phys = ttbr1 & 0x0000FFFFFFFFFFC0UL;
		_nvk_linmap_offset = (unsigned long)swapper - pgd_phys;
		_nvk_linmap_detected = 1;
		return;
	}

	unsigned long *memstart = (unsigned long *)NVK_LOOKUP("memstart_addr");
	if (memstart) {
		/*
		 * PAGE_OFFSET can be derived from TCR_EL1.T1SZ:
		 *   PAGE_OFFSET = ~((1UL << va_bits) - 1)
		 * e.g. VA_BITS=48 → PAGE_OFFSET = 0xFFFF000000000000
		 */
		unsigned long tcr;
		__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
		unsigned long va_bits = 64 - ((tcr >> 16) & 0x3FUL);
		unsigned long page_offset = ~((1UL << va_bits) - 1);
		_nvk_linmap_offset = page_offset - *memstart;
		_nvk_linmap_detected = 1;
		return;
	}

	/*
	 * Last resort: use AT instruction on a known linear-map address.
	 * init_task lives in the linear map on all GKI kernels.
	 */
	void *init = NVK_LOOKUP("init_task");
	if (init) {
		unsigned long par;
		__asm__ __volatile__(
			"at s1e1r, %1\n"
			"isb\n"
			"mrs %0, par_el1\n"
			: "=r"(par) : "r"(init) : "memory");
		if (!(par & 1)) {
			unsigned long phys =
				(par & 0x0000FFFFFFFFF000UL)
				| ((unsigned long)init & 0xFFF);
			_nvk_linmap_offset = (unsigned long)init - phys;
			_nvk_linmap_detected = 1;
		}
	}
}

static __always_inline unsigned long _nvk_linmap_phys_to_virt(unsigned long pa)
{
	return pa + _nvk_linmap_offset;
}

static int nvk_addr_init(void)
{
	if (_nvk_addr_inited) return 0;

	if (!_nvk_mem_inited)
		nvk_mem_init();

	_nvk_kimage_voffset_a =
		(unsigned long *)NVK_LOOKUP("kimage_voffset");
	_nvk_phys_offset =
		(unsigned long *)NVK_LOOKUP("memstart_addr");

	if (!_nvk_kimage_voffset_a) {
		unsigned long test_sym = NVK_LOOKUP("_text");
		if (test_sym) {
			unsigned long par;
			__asm__ __volatile__(
				"at s1e1r, %1\n"
				"isb\n"
				"mrs %0, par_el1\n"
				: "=r"(par) : "r"(test_sym) : "memory");
			if (!(par & 1)) {
				unsigned long phys =
					(par & 0x0000FFFFFFFFF000UL)
					| (test_sym & 0xFFF);
				_nvk_derived_voffset = test_sym - phys;
				_nvk_voffset_derived = 1;
			}
		}
	}

	_nvk_detect_linmap();

	if (_nvk_linmap_detected) {
		_nvk_pte_make_rw = nvk_pte_set_rw;
		_nvk_pte_make_ro = nvk_pte_set_ro;
	}

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

static unsigned long nvk_kaslr_offset(void)
{
	unsigned long text = NVK_LOOKUP("_text");
	if (!text) return 0;

	/*
	 * On arm64 GKI the default kernel virtual base is 0xFFFF800010000000
	 * (KIMAGE_VADDR for VA_BITS=48, 4K pages). KASLR shifts _text from
	 * this base. For VA_BITS=39 it is 0xFFFFFF8008000000.
	 */
	unsigned long bits = nvk_va_bits();
	unsigned long kimage_vaddr;
	if (bits <= 39)
		kimage_vaddr = 0xFFFFFF8008000000UL;
	else
		kimage_vaddr = 0xFFFF800010000000UL;

	if (text >= kimage_vaddr)
		return text - kimage_vaddr;

	return nvk_kimage_voffset();
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

struct nvk_pte_walk_result {
	unsigned long pte_phys;
	unsigned long *pte_virt;
};

static int nvk_walk_pgtable_ex(unsigned long vaddr, struct nvk_pte_info *info,
			       struct nvk_pte_walk_result *result);

static int nvk_walk_pgtable(unsigned long vaddr, struct nvk_pte_info *info)
{
	return nvk_walk_pgtable_ex(vaddr, info, (void *)0);
}

static int nvk_walk_pgtable_ex(unsigned long vaddr, struct nvk_pte_info *info,
			       struct nvk_pte_walk_result *result)
{
	unsigned long ttbr, table_addr, entry;
	int shift = nvk_page_shift();
	int bits = (int)nvk_va_bits();
	int level, levels;
	int idx_bits = shift - 3;

	if (!info) return -1;
	if (!_nvk_linmap_detected) _nvk_detect_linmap();
	if (!_nvk_linmap_detected) return -5;

	info->valid = 0;
	info->pte_val = 0;
	info->phys_addr = 0;
	info->writable = 0;
	info->executable = 0;
	info->user_accessible = 0;
	info->level = -1;
	if (result) { result->pte_phys = 0; result->pte_virt = (void *)0; }

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
		unsigned long pte_addr =
			_nvk_linmap_phys_to_virt(pte_phys);

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
		if (result) {
			result->pte_phys = pte_phys;
			result->pte_virt = (unsigned long *)pte_addr;
		}
		return 0;
	}

	return -4;
}

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


static int nvk_pte_set_rw(unsigned long vaddr)
{
	struct nvk_pte_info info;
	struct nvk_pte_walk_result wr;
	int ret = nvk_walk_pgtable_ex(vaddr, &info, &wr);
	if (ret) return ret;
	if (!info.valid) return -1;
	if (info.writable) return 1;

	unsigned long new_pte = info.pte_val & ~NVK_PTE_RO;
	__atomic_store_n(wr.pte_virt, new_pte, __ATOMIC_RELEASE);
	__asm__ __volatile__(
		"dsb ishst\n"
		"tlbi vale1is, %0\n"
		"dsb ish\n"
		"isb\n"
		:: "r"(vaddr >> 12) : "memory");
	return 0;
}

static int nvk_pte_set_ro(unsigned long vaddr)
{
	struct nvk_pte_info info;
	struct nvk_pte_walk_result wr;
	int ret = nvk_walk_pgtable_ex(vaddr, &info, &wr);
	if (ret) return ret;
	if (!info.valid) return -1;
	if (!info.writable) return 1;

	unsigned long new_pte = info.pte_val | NVK_PTE_RO;
	__atomic_store_n(wr.pte_virt, new_pte, __ATOMIC_RELEASE);
	__asm__ __volatile__(
		"dsb ishst\n"
		"tlbi vale1is, %0\n"
		"dsb ish\n"
		"isb\n"
		:: "r"(vaddr >> 12) : "memory");
	return 0;
}

static int nvk_pte_set_exec(unsigned long vaddr)
{
	struct nvk_pte_info info;
	struct nvk_pte_walk_result wr;
	int ret = nvk_walk_pgtable_ex(vaddr, &info, &wr);
	if (ret) return ret;
	if (!info.valid) return -1;

	unsigned long new_pte = info.pte_val & ~(NVK_PTE_PXN | NVK_PTE_UXN);
	__atomic_store_n(wr.pte_virt, new_pte, __ATOMIC_RELEASE);
	__asm__ __volatile__(
		"dsb ishst\n"
		"tlbi vale1is, %0\n"
		"dsb ish\n"
		"isb\n"
		:: "r"(vaddr >> 12) : "memory");
	return 0;
}

static int nvk_pte_set_rw_range(unsigned long start, unsigned long end)
{
	unsigned long pgsz = nvk_page_size();
	unsigned long addr;
	int count = 0;
	for (addr = start & ~(pgsz - 1); addr < end; addr += pgsz) {
		if (nvk_pte_set_rw(addr) == 0)
			count++;
	}
	return count;
}

static int nvk_pte_set_ro_range(unsigned long start, unsigned long end)
{
	unsigned long pgsz = nvk_page_size();
	unsigned long addr;
	int count = 0;
	for (addr = start & ~(pgsz - 1); addr < end; addr += pgsz) {
		if (nvk_pte_set_ro(addr) == 0)
			count++;
	}
	return count;
}

static __always_inline int nvk_linmap_available(void)
{
	return _nvk_linmap_detected;
}

static __always_inline unsigned long nvk_linmap_offset(void)
{
	return _nvk_linmap_offset;
}

#endif /* NVK_ADDR_H */
