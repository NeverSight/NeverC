/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

#define NEVERC_KRT_MEM_FORCE_INLINE __attribute__((always_inline))

/* ---- public forced-inline atomic operations ---- */

NEVERC_KRT_MEM_FORCE_INLINE u64
neverc_krt_mem_atomic_read64(const volatile u64 *addr)
{
	u64 value;

	__asm__ __volatile__("ldar %0, [%1]"
			     : "=r"(value)
			     : "r"(addr)
			     : "memory");
	return value;
}

NEVERC_KRT_MEM_FORCE_INLINE void
neverc_krt_mem_atomic_write64(volatile u64 *addr, u64 value)
{
	__asm__ __volatile__("stlr %1, [%0]"
			     :
			     : "r"(addr), "r"(value)
			     : "memory");
}

NEVERC_KRT_MEM_FORCE_INLINE u64
neverc_krt_mem_xchg64(volatile u64 *addr, u64 new_value)
{
	u64 old_value;
	u32 status;

	__asm__ __volatile__(
		"1: ldaxr %0, [%2]\n"
		"   stlxr %w1, %3, [%2]\n"
		"   cbnz  %w1, 1b\n"
		: "=&r"(old_value), "=&r"(status)
		: "r"(addr), "r"(new_value)
		: "memory");
	return old_value;
}

NEVERC_KRT_MEM_FORCE_INLINE int
neverc_krt_mem_cas64(volatile u64 *addr, u64 expected, u64 desired)
{
	u64 old_value;
	u32 status;

	__asm__ __volatile__(
		"1: ldaxr %0, [%2]\n"
		"   cmp   %0, %3\n"
		"   b.ne  2f\n"
		"   stlxr %w1, %4, [%2]\n"
		"   cbnz  %w1, 1b\n"
		"2:\n"
		: "=&r"(old_value), "=&r"(status)
		: "r"(addr), "r"(expected), "r"(desired)
		: "memory", "cc");
	return old_value == expected;
}

/* ---- file-local typedefs ---- */

typedef long (*neverc_krt_probe_read_fn)(void *dst, const void *src,
					 size_t len);
typedef long (*neverc_krt_probe_write_fn)(void *dst, const void *src,
					  size_t len);
typedef int (*neverc_krt_set_memory_fn)(unsigned long addr, int numpages);
typedef void (*neverc_krt_update_mapping_prot_fn)(u64 phys, unsigned long virt,
						  u64 size, u64 prot);
typedef int  (*neverc_krt_insn_write_fn)(void *addr, u32 insn);
typedef int  (*neverc_krt_insn_patch_text_fn)(void *addrs[], u32 insns[], int cnt);

/* ---- variables ---- */

static unsigned long              _neverc_krt_mem_page_sz;
int                               _neverc_krt_mem_inited = 0;
neverc_krt_pte_rw_fn              _neverc_krt_pte_make_rw = (void *)0;
neverc_krt_pte_rw_fn              _neverc_krt_pte_make_ro = (void *)0;
neverc_krt_copy_from_user_fn      _neverc_krt_copy_from_user = (void *)0;
neverc_krt_copy_to_user_fn        _neverc_krt_copy_to_user = (void *)0;

static neverc_krt_probe_read_fn          _neverc_krt_probe_read;
static neverc_krt_probe_write_fn         _neverc_krt_probe_write;
static neverc_krt_set_memory_fn          _neverc_krt_set_memory_rw;
static neverc_krt_set_memory_fn          _neverc_krt_set_memory_ro;
static neverc_krt_update_mapping_prot_fn _neverc_krt_update_prot;
static unsigned long                    *_neverc_krt_kimage_voffset;
static unsigned long                    *_neverc_krt_memstart_addr;
static neverc_krt_insn_write_fn          _neverc_krt_insn_write;
static neverc_krt_insn_patch_text_fn     _neverc_krt_insn_patch_text;

/* ---- internal inline helpers ---- */

static __always_inline unsigned long _neverc_krt_strip_tags(unsigned long addr)
{
	return addr & ~(0xFFUL << 56);
}

unsigned long _neverc_krt_mem_get_page_size(void)
{
	if (__builtin_expect(_neverc_krt_mem_page_sz != 0, 1))
		return _neverc_krt_mem_page_sz;
	unsigned long tcr;
	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	u32 tg1 = (tcr >> 30) & 3;
	unsigned long sz;
	if (tg1 == 1) sz = 16384;
	else if (tg1 == 3) sz = 65536;
	else sz = 4096;
	_neverc_krt_mem_page_sz = sz;
	return sz;
}

/* ---- internal defines ---- */

#define _NEVERC_KRT_PTE_TYPE_PAGE  (3UL << 0)
#define _NEVERC_KRT_PTE_AF         (1UL << 10)
#define _NEVERC_KRT_PTE_SH_IS      (3UL << 8)
#define _NEVERC_KRT_PTE_RDONLY     (1UL << 7)
#define _NEVERC_KRT_PTE_ATTRINDX(x) ((unsigned long)(x) << 2)
#define _NEVERC_KRT_PTE_UXN        (1UL << 54)
#define _NEVERC_KRT_PAGE_KERNEL     (_NEVERC_KRT_PTE_TYPE_PAGE | _NEVERC_KRT_PTE_AF | \
			      _NEVERC_KRT_PTE_SH_IS | _NEVERC_KRT_PTE_ATTRINDX(0) | \
			      _NEVERC_KRT_PTE_UXN)
#define _NEVERC_KRT_PAGE_KERNEL_RO  (_NEVERC_KRT_PAGE_KERNEL | _NEVERC_KRT_PTE_RDONLY)

int neverc_krt_mem_init(void)
{
	if (_neverc_krt_mem_inited) return 0;

	_neverc_krt_probe_read = (neverc_krt_probe_read_fn)NEVERC_KRT_LOOKUP("copy_from_kernel_nofault");
	if (!_neverc_krt_probe_read)
		_neverc_krt_probe_read = (neverc_krt_probe_read_fn)NEVERC_KRT_LOOKUP("probe_kernel_read");

	_neverc_krt_probe_write = (neverc_krt_probe_write_fn)NEVERC_KRT_LOOKUP("copy_to_kernel_nofault");
	if (!_neverc_krt_probe_write)
		_neverc_krt_probe_write = (neverc_krt_probe_write_fn)NEVERC_KRT_LOOKUP("probe_kernel_write");

	_neverc_krt_copy_from_user = (neverc_krt_copy_from_user_fn)NEVERC_KRT_LOOKUP("_copy_from_user");
	if (!_neverc_krt_copy_from_user)
		_neverc_krt_copy_from_user =
			(neverc_krt_copy_from_user_fn)NEVERC_KRT_LOOKUP("raw_copy_from_user");

	_neverc_krt_copy_to_user = (neverc_krt_copy_to_user_fn)NEVERC_KRT_LOOKUP("_copy_to_user");
	if (!_neverc_krt_copy_to_user)
		_neverc_krt_copy_to_user =
			(neverc_krt_copy_to_user_fn)NEVERC_KRT_LOOKUP("raw_copy_to_user");

	_neverc_krt_set_memory_rw = (neverc_krt_set_memory_fn)NEVERC_KRT_LOOKUP("set_memory_rw");
	_neverc_krt_set_memory_ro = (neverc_krt_set_memory_fn)NEVERC_KRT_LOOKUP("set_memory_ro");
	_neverc_krt_update_prot =
		(neverc_krt_update_mapping_prot_fn)NEVERC_KRT_LOOKUP("update_mapping_prot");
	_neverc_krt_kimage_voffset =
		(unsigned long *)NEVERC_KRT_LOOKUP("kimage_voffset");
	_neverc_krt_memstart_addr =
		(unsigned long *)NEVERC_KRT_LOOKUP("memstart_addr");
	_neverc_krt_insn_write =
		(neverc_krt_insn_write_fn)NEVERC_KRT_LOOKUP("aarch64_insn_write");
	if (!_neverc_krt_insn_write)
		_neverc_krt_insn_write =
			(neverc_krt_insn_write_fn)NEVERC_KRT_LOOKUP("aarch64_insn_patch_text_nosync");
	_neverc_krt_insn_patch_text =
		(neverc_krt_insn_patch_text_fn)NEVERC_KRT_LOOKUP("aarch64_insn_patch_text");

	/*
	 * NEVERC_KRT_BOOTSTRAP() records the caller's profile before this
	 * function runs.  neverc_krt_init_all() is part of the neutral
	 * embedded bitcode, so it cannot carry a compile-time profile and
	 * reaches this banner-based fallback instead.
	 */
	if (!__atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE))
		_neverc_krt_version_try_detect_from_banner();

	_neverc_krt_mem_inited = 1;
	return 0;
}

long neverc_krt_mem_read(void *dst, const void *src, size_t len)
{
	if (likely(_neverc_krt_probe_read))
		return _neverc_krt_probe_read(dst, src, len);

	unsigned char *d = (unsigned char *)dst;
	const volatile unsigned char *s =
		(const volatile unsigned char *)_neverc_krt_strip_tags((unsigned long)src);
	size_t i;
	for (i = 0; i < len; i++)
		d[i] = s[i];
	return 0;
}

long neverc_krt_mem_write(void *dst, const void *src, size_t len)
{
	if (likely(_neverc_krt_probe_write))
		return _neverc_krt_probe_write(dst, src, len);

	volatile unsigned char *d =
		(volatile unsigned char *)_neverc_krt_strip_tags((unsigned long)dst);
	const unsigned char *s = (const unsigned char *)src;
	size_t i;
	for (i = 0; i < len; i++)
		d[i] = s[i];
	return 0;
}

long neverc_krt_mem_read_user(void *dst, const void __user *src, size_t len)
{
	unsigned long not_copied;
	if (!_neverc_krt_copy_from_user) return -1;
	not_copied = _neverc_krt_copy_from_user(dst, src, len);
	if (not_copied) {
		unsigned char *p = (unsigned char *)dst + (len - not_copied);
		unsigned long i;
		for (i = 0; i < not_copied; i++)
			p[i] = 0;
		return -14;
	}
	return 0;
}

long neverc_krt_mem_write_user(void __user *dst, const void *src, size_t len)
{
	if (!_neverc_krt_copy_to_user) return -1;
	return _neverc_krt_copy_to_user(dst, src, len) ? -14 : 0;
}

/*
 * Convert a physical address from a page table entry to a linear-map
 * kernel virtual address.  On ARM64:
 *   PAGE_OFFSET = -(1UL << VA_BITS)
 *   virt = phys - memstart_addr + PAGE_OFFSET
 * Returns 0 on failure (memstart_addr not resolved).
 */
static __always_inline unsigned long
_neverc_krt_pa_to_lm(unsigned long pa, int va_bits)
{
	if (!_neverc_krt_memstart_addr) return 0;
	unsigned long page_offset = ~0UL << va_bits;
	return pa - *_neverc_krt_memstart_addr + page_offset;
}

static __always_inline void _neverc_krt_flush_tlb_all(void)
{
	__asm__ __volatile__("dsb ishst" ::: "memory");
	__asm__ __volatile__("tlbi vmalle1is" ::: "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

static int _neverc_krt_pte_walk_set(unsigned long addr, int writable)
{
	unsigned long tcr, t1sz, levels, pgsz;
	unsigned long ttbr1, table_pa, table, idx, desc;
	int va_bits, bits_per_level;

	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	t1sz = (tcr >> 16) & 0x3F;
	va_bits = 64 - (int)t1sz;

	pgsz = _neverc_krt_mem_get_page_size();
	if (pgsz == 4096)
		bits_per_level = 9;
	else if (pgsz == 16384)
		bits_per_level = 11;
	else
		bits_per_level = 13;

	levels = (va_bits - 12 + bits_per_level - 1) / bits_per_level;
	if (levels < 2) levels = 2;
	if (levels > 4) levels = 4;

	__asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(ttbr1));
	table_pa = ttbr1 & 0x0000FFFFFFFFFFFF & ~(pgsz - 1);
	if (!table_pa) return -1;

	table = _neverc_krt_pa_to_lm(table_pa, va_bits);
	if (!table) return -1;

	unsigned long level;
	for (level = 4 - levels; level < 3; level++) {
		int shift = (3 - level) * bits_per_level + 12;
		unsigned long mask = (1UL << bits_per_level) - 1;
		idx = (addr >> shift) & mask;
		unsigned long entry_addr = table + idx * 8;

		if (neverc_krt_mem_read(&desc, (void *)entry_addr, 8))
			return -2;

		/*
		 * Block entries (type 1) map large regions (2MB on 4KB
		 * granule).  Toggling only the permission bits (RDONLY)
		 * is safe without a full break-before-make sequence
		 * because the physical address and block size stay the
		 * same.  A full TLB flush ensures coherence.
		 */
		if ((desc & 3) == 1) {
			unsigned long new_desc;
			if (writable)
				new_desc = desc & ~_NEVERC_KRT_PTE_RDONLY;
			else
				new_desc = desc | _NEVERC_KRT_PTE_RDONLY;
			if (new_desc == desc) return 0;
			*(volatile unsigned long *)entry_addr = new_desc;
			_neverc_krt_flush_tlb_all();
			return 0;
		}

		if ((desc & 3) != 3) return -3;
		unsigned long next_pa = desc & ~0xFFFUL & ~(0xFFFFUL << 48);
		table = _neverc_krt_pa_to_lm(next_pa, va_bits);
		if (!table) return -2;
	}

	{
		int shift = 12;
		unsigned long mask = (1UL << bits_per_level) - 1;
		idx = (addr >> shift) & mask;
		unsigned long pte_addr = table + idx * 8;

		if (neverc_krt_mem_read(&desc, (void *)pte_addr, 8))
			return -4;

		if ((desc & 1) == 0) return -5;

		unsigned long new_desc;
		if (writable)
			new_desc = desc & ~_NEVERC_KRT_PTE_RDONLY;
		else
			new_desc = desc | _NEVERC_KRT_PTE_RDONLY;

		if (new_desc == desc) return 0;

		*(volatile unsigned long *)pte_addr = new_desc;
		_neverc_krt_flush_tlb_all();
	}
	return 0;
}

int neverc_krt_mem_make_rw(unsigned long addr)
{
	unsigned long pgsz = _neverc_krt_mem_get_page_size();
	unsigned long page = addr & ~(pgsz - 1);

	if (_neverc_krt_update_prot && _neverc_krt_kimage_voffset) {
		u64 phys = page - *_neverc_krt_kimage_voffset;
		_neverc_krt_update_prot(phys, page, pgsz, _NEVERC_KRT_PAGE_KERNEL);
		return 0;
	}
	if (_neverc_krt_pte_make_rw &&
	    _neverc_krt_pte_make_rw(page) == 0)
		return 0;
	return _neverc_krt_pte_walk_set(page, 1);
}

int neverc_krt_mem_make_ro(unsigned long addr)
{
	unsigned long pgsz = _neverc_krt_mem_get_page_size();
	unsigned long page = addr & ~(pgsz - 1);

	if (_neverc_krt_update_prot && _neverc_krt_kimage_voffset) {
		u64 phys = page - *_neverc_krt_kimage_voffset;
		_neverc_krt_update_prot(phys, page, pgsz, _NEVERC_KRT_PAGE_KERNEL_RO);
		return 0;
	}
	if (_neverc_krt_pte_make_ro &&
	    _neverc_krt_pte_make_ro(page) == 0)
		return 0;
	return _neverc_krt_pte_walk_set(page, 0);
}

/*
 * Write to read-only kernel memory using the kernel's instruction
 * patching mechanism.  Prefers aarch64_insn_patch_text (stop_machine,
 * atomic across all CPUs) and falls back to per-word aarch64_insn_write.
 * Both use fixmap to create a temporary writable mapping of the target
 * page, so this works even for kernel image .rodata pages.
 */
static int _neverc_krt_mem_write_via_insn_write(unsigned long addr,
					       const void *src, size_t len)
{
	const unsigned char *s = (const unsigned char *)src;
	size_t count = len / 4;
	size_t tail_len = len & 3;
	size_t total = count + (tail_len ? 1 : 0);
	unsigned int i;

	if (total == 0) return 0;
	if (!_neverc_krt_insn_write && !_neverc_krt_insn_patch_text) return -1;
	if (total > 4) return -1;

	void *addrs[4];
	u32 insns[4];

	for (i = 0; i < count; i++) {
		addrs[i] = (void *)(addr + i * 4);
		u32 val;
		unsigned int j;
		for (j = 0; j < 4; j++)
			((unsigned char *)&val)[j] = s[i * 4 + j];
		insns[i] = val;
	}
	if (tail_len) {
		addrs[count] = (void *)(addr + count * 4);
		u32 tail = 0;
		neverc_krt_mem_read(&tail, addrs[count], 4);
		for (i = 0; i < tail_len; i++)
			((unsigned char *)&tail)[i] = s[count * 4 + i];
		insns[count] = tail;
	}

	int ret;
	if (_neverc_krt_insn_patch_text) {
		ret = _neverc_krt_insn_patch_text(addrs, insns, (int)total);
	} else if (_neverc_krt_insn_write) {
		ret = 0;
		for (i = 0; i < total && ret == 0; i++)
			ret = _neverc_krt_insn_write(addrs[i], insns[i]);
	} else {
		ret = -1;
	}

	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
	return ret;
}

int neverc_krt_mem_write_protected(unsigned long addr, const void *src,
				   size_t len)
{
	unsigned long pgsz = _neverc_krt_mem_get_page_size();
	unsigned long mask = pgsz - 1;
	unsigned long page_start = addr & ~mask;
	unsigned long page_end = (addr + len - 1) & ~mask;
	unsigned long p;
	int ret;
	int rw_ok = 1;
	for (p = page_start; p <= page_end; p += pgsz) {
		ret = neverc_krt_mem_make_rw(p);
		if (ret < 0) { rw_ok = 0; break; }
	}

	if (rw_ok) {
		ret = neverc_krt_mem_write((void *)addr, src, len);
		for (p = page_start; p <= page_end; p += pgsz)
			neverc_krt_mem_make_ro(p);
	} else {
		ret = _neverc_krt_mem_write_via_insn_write(addr, src, len);
	}

	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");

	return ret;
}

void *neverc_krt_mem_scan(const void *start, size_t region_len,
			  const void *pattern, size_t pat_len)
{
	const unsigned char *base = (const unsigned char *)start;
	const unsigned char *pat  = (const unsigned char *)pattern;
	size_t i, j;

	if (pat_len == 0 || pat_len > region_len)
		return (void *)0;

	if (pat_len >= 4) {
		size_t skip[256];
		for (i = 0; i < 256; i++)
			skip[i] = pat_len;
		for (i = 0; i < pat_len - 1; i++)
			skip[pat[i]] = pat_len - 1 - i;

		i = 0;
		while (i <= region_len - pat_len) {
			j = pat_len;
			while (j > 0 && base[i + j - 1] == pat[j - 1])
				j--;
			if (j == 0)
				return (void *)&base[i];
			i += skip[base[i + pat_len - 1]];
		}
		return (void *)0;
	}

	for (i = 0; i <= region_len - pat_len; i++) {
		int match = 1;
		for (j = 0; j < pat_len; j++) {
			if (base[i + j] != pat[j]) {
				match = 0;
				break;
			}
		}
		if (match) return (void *)&base[i];
	}
	return (void *)0;
}

void *neverc_krt_mem_scan_mask(const void *start, size_t region_len,
			       const unsigned char *pattern,
			       const unsigned char *mask, size_t pat_len)
{
	const unsigned char *base = (const unsigned char *)start;
	size_t i, j;

	if (pat_len == 0 || pat_len > region_len)
		return (void *)0;

	for (i = 0; i <= region_len - pat_len; i++) {
		int match = 1;
		for (j = 0; j < pat_len; j++) {
			if ((base[i + j] & mask[j]) != (pattern[j] & mask[j])) {
				match = 0;
				break;
			}
		}
		if (match) return (void *)&base[i];
	}
	return (void *)0;
}

u8  neverc_krt_mem_read8(const void *addr)
{ u8 v = 0;  neverc_krt_mem_read(&v, addr, 1); return v; }

u16 neverc_krt_mem_read16(const void *addr)
{ u16 v = 0; neverc_krt_mem_read(&v, addr, 2); return v; }

u32 neverc_krt_mem_read32(const void *addr)
{ u32 v = 0; neverc_krt_mem_read(&v, addr, 4); return v; }

u64 neverc_krt_mem_read64(const void *addr)
{ u64 v = 0; neverc_krt_mem_read(&v, addr, 8); return v; }

int neverc_krt_mem_cmp(const void *a, const void *b, size_t len)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;
	size_t i;
	for (i = 0; i < len; i++) {
		if (pa[i] != pb[i])
			return (int)pa[i] - (int)pb[i];
	}
	return 0;
}

int neverc_krt_mem_cmp_ct(const void *a, const void *b, size_t len)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;
	unsigned int diff = 0;
	size_t i;
	for (i = 0; i < len; i++)
		diff |= pa[i] ^ pb[i];
	return diff ? 1 : 0;
}

void *neverc_krt_mem_scan_safe(const void *start, size_t region_len,
			       const void *pattern, size_t pat_len)
{
	const unsigned char *base = (const unsigned char *)start;
	const unsigned char *pat  = (const unsigned char *)pattern;
	unsigned char buf[64];
	size_t i, j;

	if (pat_len == 0 || pat_len > region_len || pat_len > sizeof(buf))
		return (void *)0;
	if (!_neverc_krt_probe_read)
		return neverc_krt_mem_scan(start, region_len, pattern, pat_len);

	for (i = 0; i <= region_len - pat_len; i++) {
		long ret = _neverc_krt_probe_read(buf, &base[i], pat_len);
		if (ret) continue;
		int match = 1;
		for (j = 0; j < pat_len; j++) {
			if (buf[j] != pat[j]) { match = 0; break; }
		}
		if (match) return (void *)&base[i];
	}
	return (void *)0;
}

void neverc_krt_mem_fill(void *dst, unsigned char val, size_t len)
{
	volatile unsigned char *d = (volatile unsigned char *)dst;
	size_t i;
	for (i = 0; i < len; i++)
		d[i] = val;
}

void neverc_krt_mem_zero(void *dst, size_t len)
{
	neverc_krt_mem_fill(dst, 0, len);
}

long neverc_krt_mem_read_cross_page(void *dst, const void *src, size_t len)
{
	unsigned long addr = (unsigned long)src;
	unsigned char *d = (unsigned char *)dst;
	unsigned long pgsz = _neverc_krt_mem_get_page_size();
	unsigned long mask = pgsz - 1;
	size_t done = 0;

	while (done < len) {
		unsigned long page_end = (addr & ~mask) + pgsz;
		size_t chunk = page_end - addr;
		if (chunk > len - done) chunk = len - done;

		long ret = neverc_krt_mem_read(d + done, (const void *)addr, chunk);
		if (ret) return ret;

		done += chunk;
		addr += chunk;
	}
	return 0;
}

void *neverc_krt_mem_scan_mask_safe(const void *start, size_t region_len,
				    const unsigned char *pattern,
				    const unsigned char *mask, size_t pat_len)
{
	const unsigned char *base = (const unsigned char *)start;
	unsigned char buf[64];
	size_t i, j;

	if (pat_len == 0 || pat_len > region_len || pat_len > sizeof(buf))
		return (void *)0;
	if (!_neverc_krt_probe_read)
		return neverc_krt_mem_scan_mask(start, region_len, pattern,
					mask, pat_len);

	for (i = 0; i <= region_len - pat_len; i++) {
		if (_neverc_krt_probe_read(buf, &base[i], pat_len))
			continue;
		int match = 1;
		for (j = 0; j < pat_len; j++) {
			if ((buf[j] & mask[j]) != (pattern[j] & mask[j])) {
				match = 0;
				break;
			}
		}
		if (match) return (void *)&base[i];
	}
	return (void *)0;
}

#undef NEVERC_KRT_MEM_FORCE_INLINE

