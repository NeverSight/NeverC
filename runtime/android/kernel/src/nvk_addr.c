/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include <nvk_internal.h>

/* ---- internal variables ---- */

static unsigned long *_neverc_krt_kimage_voffset_a;
static unsigned long  _neverc_krt_derived_voffset;
static int            _neverc_krt_voffset_derived;
static unsigned long  _neverc_krt_linmap_offset;
static int            _neverc_krt_linmap_detected;

static unsigned long *_neverc_krt_phys_offset;
static int     _neverc_krt_addr_inited;

/* ---- internal helpers ---- */

static __always_inline unsigned long _neverc_krt_get_voffset(void)
{
	if (_neverc_krt_kimage_voffset_a)
		return *_neverc_krt_kimage_voffset_a;
	if (_neverc_krt_voffset_derived)
		return _neverc_krt_derived_voffset;
	return 0;
}

static __always_inline unsigned long _neverc_krt_linmap_phys_to_virt(unsigned long pa)
{
	return pa + _neverc_krt_linmap_offset;
}

unsigned long neverc_krt_virt_to_phys(unsigned long vaddr)
{
	unsigned long off = _neverc_krt_get_voffset();
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

unsigned long neverc_krt_phys_to_virt(unsigned long paddr)
{
	unsigned long off = _neverc_krt_get_voffset();
	if (off)
		return paddr + off;
	return 0;
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

/* ---- implementation ---- */

static void _neverc_krt_detect_linmap(void)
{
	if (_neverc_krt_linmap_detected) return;

	void *swapper = NEVERC_KRT_LOOKUP("swapper_pg_dir");
	if (swapper) {
		unsigned long ttbr1;
		__asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(ttbr1));
		unsigned long pgd_phys = ttbr1 & 0x0000FFFFFFFFFFC0UL;
		_neverc_krt_linmap_offset = (unsigned long)swapper - pgd_phys;
		_neverc_krt_linmap_detected = 1;
		return;
	}

	unsigned long *memstart = (unsigned long *)NEVERC_KRT_LOOKUP("memstart_addr");
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
		_neverc_krt_linmap_offset = page_offset - *memstart;
		_neverc_krt_linmap_detected = 1;
		return;
	}

	/*
	 * Last resort: use AT instruction on a known linear-map address.
	 * init_task lives in the linear map on all GKI kernels.
	 */
	void *init = NEVERC_KRT_LOOKUP("init_task");
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
			_neverc_krt_linmap_offset = (unsigned long)init - phys;
			_neverc_krt_linmap_detected = 1;
		}
	}
}

int neverc_krt_addr_init(void)
{
	if (_neverc_krt_addr_inited) return 0;

	neverc_krt_mem_init();

	_neverc_krt_kimage_voffset_a =
		(unsigned long *)NEVERC_KRT_LOOKUP("kimage_voffset");
	_neverc_krt_phys_offset =
		(unsigned long *)NEVERC_KRT_LOOKUP("memstart_addr");

	if (!_neverc_krt_kimage_voffset_a) {
		unsigned long test_sym = NEVERC_KRT_LOOKUP("_text");
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
	unsigned long text = NEVERC_KRT_LOOKUP("_text");
	if (!text) return 0;

	/*
	 * On arm64 GKI the default kernel virtual base is 0xFFFF800010000000
	 * (KIMAGE_VADDR for VA_BITS=48, 4K pages). KASLR shifts _text from
	 * this base. For VA_BITS=39 it is 0xFFFFFF8008000000.
	 */
	unsigned long bits = neverc_krt_va_bits();
	unsigned long kimage_vaddr;
	if (bits <= 39)
		kimage_vaddr = 0xFFFFFF8008000000UL;
	else
		kimage_vaddr = 0xFFFF800010000000UL;

	if (text >= kimage_vaddr)
		return text - kimage_vaddr;

	return neverc_krt_kimage_voffset();
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
	int shift = neverc_krt_page_shift();
	int bits = (int)neverc_krt_va_bits();
	int level, levels;
	int idx_bits = shift - 3;

	if (!info) return -1;
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

	table_addr = ttbr & NEVERC_KRT_PTE_ADDR_MASK;

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
			_neverc_krt_linmap_phys_to_virt(pte_phys);

		if (neverc_krt_mem_read(&entry, (void *)pte_addr, 8))
			return -2;

		if (!(entry & NEVERC_KRT_PTE_VALID))
			return -3;

		if (level < 3 && (entry & NEVERC_KRT_PTE_TABLE)) {
			table_addr = entry & NEVERC_KRT_PTE_ADDR_MASK;
			continue;
		}

		info->pte_val = entry;
		info->level = level;
		info->valid = 1;

		if (level == 3)
			info->phys_addr = (entry & NEVERC_KRT_PTE_ADDR_MASK)
					  | (vaddr & ((1UL << shift) - 1));
		else
			info->phys_addr = (entry & NEVERC_KRT_PTE_ADDR_MASK)
					  | (vaddr & ((1UL << s) - 1));

		info->writable = !(entry & NEVERC_KRT_PTE_RO);
		info->executable = !(entry & NEVERC_KRT_PTE_UXN)
				   || !(entry & NEVERC_KRT_PTE_PXN);
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

	unsigned long new_pte = info.pte_val & ~NEVERC_KRT_PTE_RO;
	__atomic_store_n(wr.pte_virt, new_pte, __ATOMIC_RELEASE);
	__asm__ __volatile__(
		"dsb ishst\n"
		"tlbi vale1is, %0\n"
		"dsb ish\n"
		"isb\n"
		:: "r"(vaddr >> 12) : "memory");
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

	unsigned long new_pte = info.pte_val | NEVERC_KRT_PTE_RO;
	__atomic_store_n(wr.pte_virt, new_pte, __ATOMIC_RELEASE);
	__asm__ __volatile__(
		"dsb ishst\n"
		"tlbi vale1is, %0\n"
		"dsb ish\n"
		"isb\n"
		:: "r"(vaddr >> 12) : "memory");
	return 0;
}

int neverc_krt_pte_set_exec(unsigned long vaddr)
{
	struct neverc_krt_pte_info info;
	struct neverc_krt_pte_walk_result wr;
	int ret = neverc_krt_walk_pgtable_ex(vaddr, &info, &wr);
	if (ret) return ret;
	if (!info.valid) return -1;

	unsigned long new_pte = info.pte_val & ~(NEVERC_KRT_PTE_PXN | NEVERC_KRT_PTE_UXN);
	__atomic_store_n(wr.pte_virt, new_pte, __ATOMIC_RELEASE);
	__asm__ __volatile__(
		"dsb ishst\n"
		"tlbi vale1is, %0\n"
		"dsb ish\n"
		"isb\n"
		:: "r"(vaddr >> 12) : "memory");
	return 0;
}

int neverc_krt_pte_set_rw_range(unsigned long start, unsigned long end)
{
	unsigned long pgsz = neverc_krt_page_size();
	unsigned long addr;
	int count = 0;
	for (addr = start & ~(pgsz - 1); addr < end; addr += pgsz) {
		if (neverc_krt_pte_set_rw(addr) == 0)
			count++;
	}
	return count;
}

int neverc_krt_pte_set_ro_range(unsigned long start, unsigned long end)
{
	unsigned long pgsz = neverc_krt_page_size();
	unsigned long addr;
	int count = 0;
	for (addr = start & ~(pgsz - 1); addr < end; addr += pgsz) {
		if (neverc_krt_pte_set_ro(addr) == 0)
			count++;
	}
	return count;
}

