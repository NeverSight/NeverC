/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_mem.c — implementations extracted from nvk_mem.h. */
#include <nvk.h>

int nvk_mem_init(void)
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

long nvk_mem_read(void *dst, const void *src, size_t len)
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

long nvk_mem_write(void *dst, const void *src, size_t len)
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

long nvk_mem_read_user(void *dst, const void __user *src, size_t len)
{
	if (!_nvk_copy_from_user) return -1;
	return _nvk_copy_from_user(dst, src, len) ? -14 : 0;
}

long nvk_mem_write_user(void __user *dst, const void *src, size_t len)
{
	if (!_nvk_copy_to_user) return -1;
	return _nvk_copy_to_user(dst, src, len) ? -14 : 0;
}

int _nvk_pte_walk_set(unsigned long addr, int writable)
{
	unsigned long tcr, t1sz, levels, pgsz;
	unsigned long ttbr1, table, idx, desc;
	int va_bits, bits_per_level;

	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	t1sz = (tcr >> 16) & 0x3F;
	va_bits = 64 - (int)t1sz;

	pgsz = _nvk_mem_get_page_size();
	if (pgsz == 4096) {
		bits_per_level = 9;
	} else if (pgsz == 16384) {
		bits_per_level = 11;
	} else {
		bits_per_level = 13;
	}

	levels = (va_bits - 12 + bits_per_level - 1) / bits_per_level;
	if (levels < 2) levels = 2;
	if (levels > 4) levels = 4;

	__asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(ttbr1));
	table = ttbr1 & ~0xFFFFUL & ~1UL;
	if (!table) return -1;

	unsigned long level;
	for (level = 4 - levels; level < 3; level++) {
		int shift = (3 - level) * bits_per_level + 12;
		unsigned long mask = (1UL << bits_per_level) - 1;
		idx = (addr >> shift) & mask;
		unsigned long entry_addr = table + idx * 8;

		if (nvk_mem_read(&desc, (void *)entry_addr, 8))
			return -2;

		if ((desc & 3) != 3) return -3;
		table = desc & ~0xFFFUL & ~(0xFFFFUL << 48);
	}

	{
		int shift = 12;
		unsigned long mask = (1UL << bits_per_level) - 1;
		idx = (addr >> shift) & mask;
		unsigned long pte_addr = table + idx * 8;

		if (nvk_mem_read(&desc, (void *)pte_addr, 8))
			return -4;

		if ((desc & 1) == 0) return -5;

		unsigned long new_desc;
		if (writable)
			new_desc = desc & ~_NVK_PTE_RDONLY;
		else
			new_desc = desc | _NVK_PTE_RDONLY;

		if (new_desc == desc) return 0;

		*(volatile unsigned long *)pte_addr = new_desc;
		__asm__ __volatile__("dsb ishst" ::: "memory");
		__asm__ __volatile__("tlbi vale1is, %0" :: "r"(addr >> 12) : "memory");
		__asm__ __volatile__("dsb ish" ::: "memory");
		__asm__ __volatile__("isb" ::: "memory");
	}
	return 0;
}

int nvk_mem_make_rw(unsigned long addr)
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
	return _nvk_pte_walk_set(page, 1);
}

int nvk_mem_make_ro(unsigned long addr)
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
	return _nvk_pte_walk_set(page, 0);
}

int nvk_mem_write_protected(unsigned long addr, const void *src,
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

void *nvk_mem_scan(const void *start, size_t region_len,
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

void *nvk_mem_scan_mask(const void *start, size_t region_len,
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

u8  nvk_mem_read8(const void *addr)
{ u8 v = 0;  nvk_mem_read(&v, addr, 1); return v; }

u16 nvk_mem_read16(const void *addr)
{ u16 v = 0; nvk_mem_read(&v, addr, 2); return v; }

u32 nvk_mem_read32(const void *addr)
{ u32 v = 0; nvk_mem_read(&v, addr, 4); return v; }

u64 nvk_mem_read64(const void *addr)
{ u64 v = 0; nvk_mem_read(&v, addr, 8); return v; }

int nvk_mem_cmp(const void *a, const void *b, size_t len)
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

int nvk_mem_cmp_ct(const void *a, const void *b, size_t len)
{
	const unsigned char *pa = (const unsigned char *)a;
	const unsigned char *pb = (const unsigned char *)b;
	unsigned int diff = 0;
	size_t i;
	for (i = 0; i < len; i++)
		diff |= pa[i] ^ pb[i];
	return diff ? 1 : 0;
}

void *nvk_mem_scan_safe(const void *start, size_t region_len,
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

void nvk_mem_fill(void *dst, unsigned char val, size_t len)
{
	volatile unsigned char *d = (volatile unsigned char *)dst;
	size_t i;
	for (i = 0; i < len; i++)
		d[i] = val;
}

void nvk_mem_zero(void *dst, size_t len)
{
	nvk_mem_fill(dst, 0, len);
}

long nvk_mem_read_cross_page(void *dst, const void *src, size_t len)
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

void *nvk_mem_scan_mask_safe(const void *start, size_t region_len,
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

