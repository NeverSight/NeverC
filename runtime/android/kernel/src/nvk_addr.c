/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk_addr.h>
#include <asm/pgtable-types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include "nvk_internal.h"

#define _NEVERC_KRT_PTE_VALID       (1UL << 0)
#define _NEVERC_KRT_PTE_TABLE       (1UL << 1)
#define _NEVERC_KRT_PTE_ATTRINDX(t) ((unsigned long)(t) << 2)
#define _NEVERC_KRT_PTE_RO          (1UL << 7)
#define _NEVERC_KRT_PTE_SHARED      (3UL << 8)
#define _NEVERC_KRT_PTE_AF          (1UL << 10)
#define _NEVERC_KRT_PTE_NG          (1UL << 11)
#define _NEVERC_KRT_PTE_WRITE       (1UL << 51)
#define _NEVERC_KRT_PTE_PXN         (1UL << 53)
#define _NEVERC_KRT_PTE_UXN         (1UL << 54)
#define _NEVERC_KRT_TCR_DS          (1UL << 59)
#define _NEVERC_KRT_MAIR_DEVICE_NGNRE 0x04UL
#define _NEVERC_KRT_MAIR_ATTRS      8U
#define _NEVERC_KRT_PHYS_ADDR_BITS  48

/*
 * arm64 ioremap() is inline, not a callable KMI symbol.  GKI 5.10/5.15 use
 * __ioremap(..., pgprot_t); GKI 6.1 and newer use
 * ioremap_prot(..., unsigned long).
 */
typedef void *(*neverc_krt_ioremap_prot_fn)(phys_addr_t, size_t,
					    unsigned long);
typedef void *(*neverc_krt_ioremap_legacy_fn)(phys_addr_t, size_t, pgprot_t);

static unsigned long *_neverc_krt_kimage_voffset_a;
static unsigned long *_neverc_krt_memstart_addr;
static unsigned long  _neverc_krt_derived_voffset;
static int            _neverc_krt_voffset_derived;
static unsigned long  _neverc_krt_linmap_offset;
static int            _neverc_krt_linmap_detected;
static int            _neverc_krt_addr_inited;
static neverc_krt_ioremap_prot_fn _neverc_krt_ioremap_prot;
static neverc_krt_ioremap_legacy_fn _neverc_krt_ioremap_legacy;
static bool *_neverc_krt_arm64_use_ng_mappings;
static unsigned long _neverc_krt_ioremap_prot_cache;
static int _neverc_krt_ioremap_prot_ready;
static int _neverc_krt_ioremap_symbols_ready;

static void _neverc_krt_detect_linmap(void);

static __always_inline unsigned long _neverc_krt_read_tcr(void)
{
	unsigned long tcr;

	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	return tcr;
}

static unsigned long _neverc_krt_decode_va_bits(unsigned long tcr)
{
	unsigned long bits = 64 - ((tcr >> 16) & 0x3FUL);

	return bits >= 36 && bits <= 52 ? bits : 0;
}

static int _neverc_krt_decode_page_shift(unsigned long tcr)
{
	switch ((tcr >> 30) & 3UL) {
	case 1:
		return 14;
	case 2:
		return 12;
	case 3:
		return 16;
	default:
		return 0;
	}
}

static unsigned long _neverc_krt_page_offset_mask(int page_shift)
{
	if (page_shift != 12 && page_shift != 14 && page_shift != 16)
		return 0;
	return (1UL << page_shift) - 1;
}

static unsigned long _neverc_krt_pte_addr_mask(int page_shift)
{
	unsigned long offset_mask =
		_neverc_krt_page_offset_mask(page_shift);

	if (!offset_mask)
		return 0;
	return ((1UL << _NEVERC_KRT_PHYS_ADDR_BITS) - 1) & ~offset_mask;
}

static __always_inline void _neverc_krt_flush_tlb_va(unsigned long vaddr)
{
	unsigned long operand = (vaddr >> 12) & ((1UL << 44) - 1);

	__asm__ __volatile__(
		"dsb ishst\n"
		"tlbi vae1is, %0\n"
		"dsb ish\n"
		"isb\n"
		:
		: "r"(operand)
		: "memory");
}

static __always_inline unsigned long _neverc_krt_get_voffset(void)
{
	unsigned long value;

	if (_neverc_krt_kimage_voffset_a &&
	    !neverc_krt_mem_read(&value, _neverc_krt_kimage_voffset_a,
				 sizeof(value)))
		return value;
	if (_neverc_krt_voffset_derived)
		return _neverc_krt_derived_voffset;
	return 0;
}

static __always_inline unsigned long _neverc_krt_linmap_phys_to_virt(unsigned long pa)
{
	return pa + _neverc_krt_linmap_offset;
}

unsigned long neverc_krt_va_bits(void)
{
	return _neverc_krt_decode_va_bits(_neverc_krt_read_tcr());
}

int _neverc_krt_kernel_pointer_is_valid(const void *pointer)
{
	unsigned long address = (unsigned long)pointer;
	unsigned long va_bits = neverc_krt_va_bits();
	unsigned long kernel_base;

	if (!pointer || va_bits < 36 || va_bits > 52)
		return 0;
	kernel_base = 0UL - (1UL << va_bits);
	return address >= kernel_base && !(address & 7UL);
}

int neverc_krt_page_shift(void)
{
	return _neverc_krt_decode_page_shift(_neverc_krt_read_tcr());
}

unsigned long neverc_krt_page_size(void)
{
	int shift = neverc_krt_page_shift();

	return shift != 0 ? 1UL << shift : 0;
}

static int _neverc_krt_build_ioremap_prot(unsigned long *prot_out)
{
	bool *use_ng_addr;
	bool use_ng;
	unsigned long mair;
	unsigned long prot;
	unsigned long tcr;
	unsigned int attr_index;

	if (!prot_out)
		return -1;

	if (__atomic_load_n(&_neverc_krt_ioremap_prot_ready,
			    __ATOMIC_ACQUIRE)) {
		*prot_out = __atomic_load_n(&_neverc_krt_ioremap_prot_cache,
					   __ATOMIC_RELAXED);
		return 0;
	}

	/*
	 * Reconstruct PROT_DEVICE_nGnRE from live architectural state instead
	 * of embedding a version/configuration-specific _PAGE_IOREMAP value.
	 */
	__asm__ __volatile__("mrs %0, mair_el1" : "=r"(mair));
	tcr = _neverc_krt_read_tcr();

	for (attr_index = 0; attr_index < _NEVERC_KRT_MAIR_ATTRS;
	     ++attr_index) {
		if (((mair >> (attr_index * 8U)) & 0xffUL) ==
		    _NEVERC_KRT_MAIR_DEVICE_NGNRE)
			break;
	}
	if (attr_index == _NEVERC_KRT_MAIR_ATTRS)
		return -1;

	use_ng_addr = __atomic_load_n(&_neverc_krt_arm64_use_ng_mappings,
				     __ATOMIC_ACQUIRE);
	if (!use_ng_addr) {
		use_ng_addr = (bool *)NEVERC_KRT_LOOKUP(
			"arm64_use_ng_mappings");
		if (!use_ng_addr)
			return -1;
		__atomic_store_n(&_neverc_krt_arm64_use_ng_mappings,
				 use_ng_addr, __ATOMIC_RELEASE);
	}

	neverc_krt_mem_init();
	if (neverc_krt_mem_read(&use_ng, use_ng_addr, sizeof(use_ng)) != 0)
		return -1;

	prot = _NEVERC_KRT_PTE_VALID | _NEVERC_KRT_PTE_TABLE |
	       _NEVERC_KRT_PTE_AF | _NEVERC_KRT_PTE_WRITE |
	       _NEVERC_KRT_PTE_PXN | _NEVERC_KRT_PTE_UXN |
	       _NEVERC_KRT_PTE_ATTRINDX(attr_index);
	if (!(tcr & _NEVERC_KRT_TCR_DS))
		prot |= _NEVERC_KRT_PTE_SHARED;
	if (use_ng)
		prot |= _NEVERC_KRT_PTE_NG;

	__atomic_store_n(&_neverc_krt_ioremap_prot_cache, prot,
			 __ATOMIC_RELAXED);
	__atomic_store_n(&_neverc_krt_ioremap_prot_ready, 1,
			 __ATOMIC_RELEASE);
	*prot_out = prot;
	return 0;
}

static void _neverc_krt_resolve_ioremap_symbols(void)
{
	neverc_krt_ioremap_prot_fn prot_fn;
	neverc_krt_ioremap_legacy_fn legacy_fn;

	if (__atomic_load_n(&_neverc_krt_ioremap_symbols_ready,
			    __ATOMIC_ACQUIRE))
		return;

	prot_fn = (neverc_krt_ioremap_prot_fn)NEVERC_KRT_LOOKUP(
		"ioremap_prot");
	legacy_fn = (neverc_krt_ioremap_legacy_fn)0;
	if (!prot_fn) {
		legacy_fn = (neverc_krt_ioremap_legacy_fn)NEVERC_KRT_LOOKUP(
			"__ioremap");
	}

	__atomic_store_n(&_neverc_krt_ioremap_prot, prot_fn,
			 __ATOMIC_RELAXED);
	__atomic_store_n(&_neverc_krt_ioremap_legacy, legacy_fn,
			 __ATOMIC_RELAXED);
	__atomic_store_n(&_neverc_krt_ioremap_symbols_ready, 1,
			 __ATOMIC_RELEASE);
}

void *neverc_krt_ioremap(phys_addr_t phys_addr, size_t size)
{
	neverc_krt_ioremap_legacy_fn legacy_fn;
	neverc_krt_ioremap_prot_fn prot_fn;
	unsigned long prot;

	_neverc_krt_resolve_ioremap_symbols();
	if (_neverc_krt_build_ioremap_prot(&prot) != 0)
		return (void *)0;

	prot_fn = __atomic_load_n(&_neverc_krt_ioremap_prot,
				  __ATOMIC_ACQUIRE);
	if (prot_fn)
		return prot_fn(phys_addr, size, prot);

	legacy_fn = __atomic_load_n(&_neverc_krt_ioremap_legacy,
				    __ATOMIC_ACQUIRE);
	if (legacy_fn)
		return legacy_fn(phys_addr, size, __pgprot(prot));

	return (void *)0;
}

unsigned long neverc_krt_virt_to_phys(unsigned long vaddr)
{
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

unsigned long neverc_krt_phys_to_virt(unsigned long paddr)
{
	if (!_neverc_krt_linmap_detected)
		_neverc_krt_detect_linmap();
	return _neverc_krt_linmap_detected
		? _neverc_krt_linmap_phys_to_virt(paddr)
		: 0;
}

unsigned long neverc_krt_kimage_voffset(void)
{
	return _neverc_krt_get_voffset();
}

int neverc_krt_linmap_available(void)
{
	return _neverc_krt_linmap_detected;
}

unsigned long neverc_krt_linmap_offset(void)
{
	return _neverc_krt_linmap_offset;
}

static void _neverc_krt_detect_linmap(void)
{
	unsigned long memstart;
	unsigned long va_bits;
	unsigned long page_offset;

	if (_neverc_krt_linmap_detected)
		return;
	if (!_neverc_krt_memstart_addr)
		_neverc_krt_memstart_addr =
			(unsigned long *)NEVERC_KRT_LOOKUP("memstart_addr");
	if (!_neverc_krt_memstart_addr ||
	    neverc_krt_mem_read(&memstart, _neverc_krt_memstart_addr,
				sizeof(memstart)))
		return;

	va_bits = neverc_krt_va_bits();
	if (!va_bits)
		return;

	page_offset = 0UL - (1UL << va_bits);
	_neverc_krt_linmap_offset = page_offset - memstart;
	_neverc_krt_linmap_detected = 1;
}

int neverc_krt_addr_init(void)
{
	if (_neverc_krt_addr_inited) return 0;

	neverc_krt_mem_init();

	_neverc_krt_kimage_voffset_a =
		(unsigned long *)NEVERC_KRT_LOOKUP("kimage_voffset");
	_neverc_krt_memstart_addr =
		(unsigned long *)NEVERC_KRT_LOOKUP("memstart_addr");

	if (!_neverc_krt_kimage_voffset_a) {
		unsigned long test_sym =
			(unsigned long)NEVERC_KRT_LOOKUP("_text");
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
				_neverc_krt_derived_voffset = test_sym - phys;
				_neverc_krt_voffset_derived = 1;
			}
		}
	}

	_neverc_krt_detect_linmap();

	if (_neverc_krt_linmap_detected) {
		_neverc_krt_pte_make_rw = neverc_krt_pte_set_rw;
		_neverc_krt_pte_make_ro = neverc_krt_pte_set_ro;
	}

	_neverc_krt_addr_inited = 1;
	return 0;
}

unsigned long neverc_krt_kaslr_offset(void)
{
	unsigned long text;
	unsigned long base;

	if (!_neverc_krt_addr_inited)
		neverc_krt_addr_init();

	text = (unsigned long)NEVERC_KRT_LOOKUP("_text");
	base = _neverc_krt_get_kimage_vaddr_base();
	return text && base && text >= base ? text - base : 0;
}

unsigned long neverc_krt_translate_user(unsigned long uaddr)
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

int neverc_krt_walk_pgtable(unsigned long vaddr, struct neverc_krt_pte_info *info)
{
	return neverc_krt_walk_pgtable_ex(vaddr, info, (void *)0);
}

int neverc_krt_walk_pgtable_ex(unsigned long vaddr, struct neverc_krt_pte_info *info,
			       struct neverc_krt_pte_walk_result *result)
{
	unsigned long ttbr, table_addr, entry;
	unsigned long pte_addr_mask;
	int shift = neverc_krt_page_shift();
	int bits = (int)neverc_krt_va_bits();
	int level, levels;
	int idx_bits = shift - 3;

	if (!info) return -1;
	if (shift != 12 && shift != 14 && shift != 16) return -6;
	if (bits < shift || bits > 52) return -7;
	pte_addr_mask = _neverc_krt_pte_addr_mask(shift);
	if (!pte_addr_mask) return -7;
	if (!_neverc_krt_linmap_detected) _neverc_krt_detect_linmap();
	if (!_neverc_krt_linmap_detected) return -5;

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

	table_addr = ttbr & pte_addr_mask;

	levels = (bits - shift + idx_bits - 1) / idx_bits;
	if (levels < 1 || levels > 4)
		return -7;

	for (level = 4 - levels; level < 4; level++) {
		int s;
		unsigned long idx, mask;

		s = shift + idx_bits * (3 - level);

		mask = (1UL << idx_bits) - 1;
		idx = (vaddr >> s) & mask;

		unsigned long pte_phys = table_addr + idx * 8;
		unsigned long pte_addr =
			_neverc_krt_linmap_phys_to_virt(pte_phys);

		if (neverc_krt_mem_read(&entry, (void *)pte_addr, 8))
			return -2;

		if (!(entry & _NEVERC_KRT_PTE_VALID))
			return -3;

		if (level < 3 && (entry & _NEVERC_KRT_PTE_TABLE)) {
			table_addr = entry & pte_addr_mask;
			continue;
		}

		info->pte_val = entry;
		info->level = level;
		info->valid = 1;

		if (level == 3)
			info->phys_addr = (entry & pte_addr_mask)
					  | (vaddr & _neverc_krt_page_offset_mask(shift));
		else
			info->phys_addr = (entry & pte_addr_mask)
					  | (vaddr & ((1UL << s) - 1));

		info->writable = !(entry & _NEVERC_KRT_PTE_RO);
		info->executable = vaddr & (1UL << 63)
			? !(entry & _NEVERC_KRT_PTE_PXN)
			: !(entry & _NEVERC_KRT_PTE_UXN);
		info->user_accessible = ((entry >> 6) & 1) != 0;
		if (result) {
			result->pte_phys = pte_phys;
			result->pte_virt = (unsigned long *)pte_addr;
		}
		return 0;
	}

	return -4;
}

unsigned long neverc_krt_read_ttbr0(void)
{
	unsigned long v;
	__asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(v));
	return v;
}

unsigned long neverc_krt_read_ttbr1(void)
{
	unsigned long v;
	__asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(v));
	return v;
}

int neverc_krt_pte_set_rw(unsigned long vaddr)
{
	struct neverc_krt_pte_info info;
	struct neverc_krt_pte_walk_result wr;
	int ret = neverc_krt_walk_pgtable_ex(vaddr, &info, &wr);
	if (ret) return ret;
	if (!info.valid) return -1;
	if (info.writable) return 1;

	unsigned long new_pte = info.pte_val & ~_NEVERC_KRT_PTE_RO;
	__atomic_store_n(wr.pte_virt, new_pte, __ATOMIC_RELEASE);
	_neverc_krt_flush_tlb_va(vaddr);
	return 0;
}

int neverc_krt_pte_set_ro(unsigned long vaddr)
{
	struct neverc_krt_pte_info info;
	struct neverc_krt_pte_walk_result wr;
	int ret = neverc_krt_walk_pgtable_ex(vaddr, &info, &wr);
	if (ret) return ret;
	if (!info.valid) return -1;
	if (!info.writable) return 1;

	unsigned long new_pte = info.pte_val | _NEVERC_KRT_PTE_RO;
	__atomic_store_n(wr.pte_virt, new_pte, __ATOMIC_RELEASE);
	_neverc_krt_flush_tlb_va(vaddr);
	return 0;
}

int neverc_krt_pte_set_exec(unsigned long vaddr)
{
	struct neverc_krt_pte_info info;
	struct neverc_krt_pte_walk_result wr;
	int ret = neverc_krt_walk_pgtable_ex(vaddr, &info, &wr);
	if (ret) return ret;
	if (!info.valid) return -1;

	unsigned long new_pte = vaddr & (1UL << 63)
		? info.pte_val & ~_NEVERC_KRT_PTE_PXN
		: info.pte_val & ~_NEVERC_KRT_PTE_UXN;
	__atomic_store_n(wr.pte_virt, new_pte, __ATOMIC_RELEASE);
	_neverc_krt_flush_tlb_va(vaddr);
	return 0;
}

int neverc_krt_pte_set_rw_range(unsigned long start, unsigned long end)
{
	unsigned long pgsz = neverc_krt_page_size();
	unsigned long addr;
	int count = 0;

	if (pgsz == 0 || start >= end)
		return 0;
	addr = start & ~(pgsz - 1);
	while (addr < end) {
		if (neverc_krt_pte_set_rw(addr) == 0)
			count++;
		if (addr > ~0UL - pgsz)
			break;
		addr += pgsz;
	}
	return count;
}

int neverc_krt_pte_set_ro_range(unsigned long start, unsigned long end)
{
	unsigned long pgsz = neverc_krt_page_size();
	unsigned long addr;
	int count = 0;

	if (pgsz == 0 || start >= end)
		return 0;
	addr = start & ~(pgsz - 1);
	while (addr < end) {
		if (neverc_krt_pte_set_ro(addr) == 0)
			count++;
		if (addr > ~0UL - pgsz)
			break;
		addr += pgsz;
	}
	return count;
}
