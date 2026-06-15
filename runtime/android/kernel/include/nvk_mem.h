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
	const volatile unsigned char *s = (const volatile unsigned char *)src;
	size_t i;
	for (i = 0; i < len; i++)
		d[i] = s[i];
	return 0;
}

static long nvk_mem_write(void *dst, const void *src, size_t len)
{
	if (_nvk_probe_write)
		return _nvk_probe_write(dst, src, len);

	volatile unsigned char *d = (volatile unsigned char *)dst;
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
	unsigned long page = addr & ~0xFFFUL;

	if (_nvk_update_prot && _nvk_kimage_voffset) {
		u64 phys = page - *_nvk_kimage_voffset;
		_nvk_update_prot(phys, page, 0x1000, _NVK_PAGE_KERNEL);
		return 0;
	}
	if (_nvk_set_memory_rw)
		return _nvk_set_memory_rw(page, 1);
	return -1;
}

static int nvk_mem_make_ro(unsigned long addr)
{
	unsigned long page = addr & ~0xFFFUL;

	if (_nvk_update_prot && _nvk_kimage_voffset) {
		u64 phys = page - *_nvk_kimage_voffset;
		_nvk_update_prot(phys, page, 0x1000, _NVK_PAGE_KERNEL_RO);
		return 0;
	}
	if (_nvk_set_memory_ro)
		return _nvk_set_memory_ro(page, 1);
	return -1;
}

static int nvk_mem_write_protected(unsigned long addr, const void *src,
				   size_t len)
{
	unsigned long page_start = addr & ~0xFFFUL;
	unsigned long page_end = (addr + len - 1) & ~0xFFFUL;
	unsigned long p;
	int ret;

	for (p = page_start; p <= page_end; p += 0x1000) {
		ret = nvk_mem_make_rw(p);
		if (ret) return ret;
	}

	ret = nvk_mem_write((void *)addr, src, len);

	for (p = page_start; p <= page_end; p += 0x1000)
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

#endif /* NVK_MEM_H */
