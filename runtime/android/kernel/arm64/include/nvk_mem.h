/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_MEM_H
#define NVK_MEM_H

#include <linux/types.h>
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

static nvk_probe_read_fn          _nvk_probe_read;
static nvk_probe_write_fn         _nvk_probe_write;
static nvk_copy_from_user_fn      _nvk_copy_from_user;
static nvk_copy_to_user_fn        _nvk_copy_to_user;
static nvk_set_memory_fn          _nvk_set_memory_rw;
static nvk_set_memory_fn          _nvk_set_memory_ro;
static nvk_update_mapping_prot_fn _nvk_update_prot;
static unsigned long             *_nvk_kimage_voffset;
static int                        _nvk_mem_inited;
static unsigned long              _nvk_mem_page_sz;

typedef int (*_nvk_pte_rw_fn)(unsigned long addr);
static _nvk_pte_rw_fn _nvk_pte_make_rw;
static _nvk_pte_rw_fn _nvk_pte_make_ro;

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

static int nvk_mem_init(void)
{
	if (_nvk_mem_inited) return 0;

	_nvk_probe_read = (nvk_probe_read_fn)NVK_LOOKUP("copy_from_kernel_nofault");
	if (!_nvk_probe_read)
		_nvk_probe_read = (nvk_probe_read_fn)NVK_LOOKUP("probe_kernel_read");

	_nvk_probe_write = (nvk_probe_write_fn)NVK_LOOKUP("copy_to_kernel_nofault");
	if (!_nvk_probe_write)
		_nvk_probe_write = (nvk_probe_write_fn)NVK_LOOKUP("probe_kernel_write");

	_nvk_copy_from_user = (nvk_copy_from_user_fn)NVK_LOOKUP("_copy_from_user");
	if (!_nvk_copy_from_user)
		_nvk_copy_from_user =
			(nvk_copy_from_user_fn)NVK_LOOKUP("raw_copy_from_user");

	_nvk_copy_to_user = (nvk_copy_to_user_fn)NVK_LOOKUP("_copy_to_user");
	if (!_nvk_copy_to_user)
		_nvk_copy_to_user =
			(nvk_copy_to_user_fn)NVK_LOOKUP("raw_copy_to_user");

	_nvk_set_memory_rw = (nvk_set_memory_fn)NVK_LOOKUP("set_memory_rw");
	_nvk_set_memory_ro = (nvk_set_memory_fn)NVK_LOOKUP("set_memory_ro");
	_nvk_update_prot =
		(nvk_update_mapping_prot_fn)NVK_LOOKUP("update_mapping_prot");
	_nvk_kimage_voffset =
		(unsigned long *)NVK_LOOKUP("kimage_voffset");

	_nvk_mem_inited = 1;
	return 0;
}


static long nvk_mem_read(void *dst, const void *src, size_t len)
{
	if (_nvk_probe_read)
		return _nvk_probe_read(dst, src, len);

	unsigned char *d = (unsigned char *)dst;
	const volatile unsigned char *s =
		(const volatile unsigned char *)_nvk_strip_tags((unsigned long)src);
	size_t i;
	for (i = 0; i < len; i++)
		d[i] = s[i];
	return 0;
}

static long nvk_mem_write(void *dst, const void *src, size_t len)
{
	if (_nvk_probe_write)
		return _nvk_probe_write(dst, src, len);

	volatile unsigned char *d =
		(volatile unsigned char *)_nvk_strip_tags((unsigned long)dst);
	const unsigned char *s = (const unsigned char *)src;
	size_t i;
	for (i = 0; i < len; i++)
		d[i] = s[i];
	return 0;
}


static long nvk_mem_read_user(void *dst, const void __user *src, size_t len)
{
	if (!_nvk_copy_from_user) return -1;
	return _nvk_copy_from_user(dst, src, len) ? -14 : 0;
}

static long nvk_mem_write_user(void __user *dst, const void *src, size_t len)
{
	if (!_nvk_copy_to_user) return -1;
	return _nvk_copy_to_user(dst, src, len) ? -14 : 0;
}


static int nvk_mem_make_rw(unsigned long addr)
{
	unsigned long pgsz = _nvk_mem_get_page_size();
	unsigned long page = addr & ~(pgsz - 1);

	if (_nvk_update_prot && _nvk_kimage_voffset) {
		u64 phys = page - *_nvk_kimage_voffset;
		_nvk_update_prot(phys, page, pgsz, _NVK_PAGE_KERNEL);
		return 0;
	}
	if (_nvk_set_memory_rw)
		return _nvk_set_memory_rw(page, 1);
	if (_nvk_pte_make_rw)
		return _nvk_pte_make_rw(page);
	return -1;
}

static int nvk_mem_make_ro(unsigned long addr)
{
	unsigned long pgsz = _nvk_mem_get_page_size();
	unsigned long page = addr & ~(pgsz - 1);

	if (_nvk_update_prot && _nvk_kimage_voffset) {
		u64 phys = page - *_nvk_kimage_voffset;
		_nvk_update_prot(phys, page, pgsz, _NVK_PAGE_KERNEL_RO);
		return 0;
	}
	if (_nvk_set_memory_ro)
		return _nvk_set_memory_ro(page, 1);
	if (_nvk_pte_make_ro)
		return _nvk_pte_make_ro(page);
	return -1;
}

static int nvk_mem_write_protected(unsigned long addr, const void *src,
				   size_t len)
{
	unsigned long pgsz = _nvk_mem_get_page_size();
	unsigned long mask = pgsz - 1;
	unsigned long page_start = addr & ~mask;
	unsigned long page_end = (addr + len - 1) & ~mask;
	unsigned long p;
	int ret;

	for (p = page_start; p <= page_end; p += pgsz) {
		ret = nvk_mem_make_rw(p);
		if (ret < 0) return ret;
	}

	ret = nvk_mem_write((void *)addr, src, len);

	for (p = page_start; p <= page_end; p += pgsz)
		nvk_mem_make_ro(p);

	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");

	return ret;
}


static void *nvk_mem_scan(const void *start, size_t region_len,
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

static void *nvk_mem_scan_mask(const void *start, size_t region_len,
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

#define nvk_mem_read_val(dst, src) \
	nvk_mem_read((dst), (src), sizeof(*(dst)))

#define nvk_mem_write_val(dst, src) \
	nvk_mem_write((dst), (src), sizeof(*(src)))


static u8  nvk_mem_read8(const void *addr)
{ u8 v = 0;  nvk_mem_read(&v, addr, 1); return v; }
static u16 nvk_mem_read16(const void *addr)
{ u16 v = 0; nvk_mem_read(&v, addr, 2); return v; }
static u32 nvk_mem_read32(const void *addr)
{ u32 v = 0; nvk_mem_read(&v, addr, 4); return v; }
static u64 nvk_mem_read64(const void *addr)
{ u64 v = 0; nvk_mem_read(&v, addr, 8); return v; }

static int nvk_mem_cmp(const void *a, const void *b, size_t len)
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

static int nvk_mem_cmp_ct(const void *a, const void *b, size_t len)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;
	unsigned int diff = 0;
	size_t i;
	for (i = 0; i < len; i++)
		diff |= pa[i] ^ pb[i];
	return diff ? 1 : 0;
}

static void *nvk_mem_scan_safe(const void *start, size_t region_len,
			       const void *pattern, size_t pat_len)
{
	const unsigned char *base = (const unsigned char *)start;
	const unsigned char *pat  = (const unsigned char *)pattern;
	unsigned char buf[64];
	size_t i, j;

	if (pat_len == 0 || pat_len > region_len || pat_len > sizeof(buf))
		return (void *)0;
	if (!_nvk_probe_read)
		return nvk_mem_scan(start, region_len, pattern, pat_len);

	for (i = 0; i <= region_len - pat_len; i++) {
		long ret = _nvk_probe_read(buf, &base[i], pat_len);
		if (ret) continue;
		int match = 1;
		for (j = 0; j < pat_len; j++) {
			if (buf[j] != pat[j]) { match = 0; break; }
		}
		if (match) return (void *)&base[i];
	}
	return (void *)0;
}

static void nvk_mem_fill(void *dst, unsigned char val, size_t len)
{
	volatile unsigned char *d = (volatile unsigned char *)dst;
	size_t i;
	for (i = 0; i < len; i++)
		d[i] = val;
}

static void nvk_mem_zero(void *dst, size_t len)
{
	nvk_mem_fill(dst, 0, len);
}

static long nvk_mem_read_cross_page(void *dst, const void *src, size_t len)
{
	unsigned long addr = (unsigned long)src;
	unsigned char *d = (unsigned char *)dst;
	unsigned long pgsz = _nvk_mem_get_page_size();
	unsigned long mask = pgsz - 1;
	size_t done = 0;

	while (done < len) {
		unsigned long page_end = (addr & ~mask) + pgsz;
		size_t chunk = page_end - addr;
		if (chunk > len - done) chunk = len - done;

		long ret = nvk_mem_read(d + done, (const void *)addr, chunk);
		if (ret) return ret;

		done += chunk;
		addr += chunk;
	}
	return 0;
}

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

static void *nvk_mem_scan_mask_safe(const void *start, size_t region_len,
				    const unsigned char *pattern,
				    const unsigned char *mask, size_t pat_len)
{
	const unsigned char *base = (const unsigned char *)start;
	unsigned char buf[64];
	size_t i, j;

	if (pat_len == 0 || pat_len > region_len || pat_len > sizeof(buf))
		return (void *)0;
	if (!_nvk_probe_read)
		return nvk_mem_scan_mask(start, region_len, pattern,
					mask, pat_len);

	for (i = 0; i <= region_len - pat_len; i++) {
		if (_nvk_probe_read(buf, &base[i], pat_len))
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

#endif /* NVK_MEM_H */
