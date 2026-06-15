/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_COMPAT_H
#define NVK_COMPAT_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <nvk_mem.h>

struct nvk_kernel_info {
	u32 major;
	u32 minor;
	u32 patch;
	u32 android_version;
	u32 sublevel;
	int detected;
};

static struct nvk_kernel_info _nvk_kinfo;
static int _nvk_compat_inited;

typedef int (*nvk_snprintf_fn)(char *buf, size_t size, const char *fmt, ...);
typedef int (*nvk_sscanf_fn)(const char *buf, const char *fmt, ...);

static nvk_snprintf_fn _nvk_snprintf;
static nvk_sscanf_fn   _nvk_sscanf;

static __always_inline int nvk_fmt_init(void)
{
	if (!_nvk_snprintf) {
		_nvk_snprintf = (nvk_snprintf_fn)NVK_LOOKUP("snprintf");
		if (!_nvk_snprintf)
			_nvk_snprintf = (nvk_snprintf_fn)NVK_LOOKUP("scnprintf");
	}
	_nvk_sscanf = (nvk_sscanf_fn)NVK_LOOKUP("sscanf");
	return _nvk_snprintf ? 0 : -1;
}

#define nvk_snprintf(buf, sz, fmt, ...)                                       \
	(_nvk_snprintf ? _nvk_snprintf((buf), (sz), (fmt), ##__VA_ARGS__) : -1)

#define nvk_sscanf(buf, fmt, ...)                                             \
	(_nvk_sscanf ? _nvk_sscanf((buf), (fmt), ##__VA_ARGS__) : -1)

static void _nvk_parse_version(const char *str, struct nvk_kernel_info *info)
{
	const char *p = str;
	u32 parts[3] = {0, 0, 0};
	int pi = 0;

	while (*p && pi < 3) {
		if (*p >= '0' && *p <= '9') {
			parts[pi] = parts[pi] * 10 + (*p - '0');
		} else if (*p == '.') {
			pi++;
		} else {
			break;
		}
		p++;
	}

	info->major = parts[0];
	info->minor = parts[1];
	info->patch = parts[2];

	while (*p && *p != '-') p++;
	if (*p == '-') p++;
	if (p[0] == 'a' && p[1] == 'n' && p[2] == 'd' && p[3] == 'r') {
		while (*p && !(*p >= '0' && *p <= '9')) p++;
		u32 av = 0;
		while (*p >= '0' && *p <= '9') {
			av = av * 10 + (*p - '0');
			p++;
		}
		info->android_version = av;
	}

	info->detected = 1;
}

static int nvk_compat_init(void)
{
	const char *banner;

	if (_nvk_compat_inited) return 0;

	if (!_nvk_mem_inited)
		nvk_mem_init();

	banner = (const char *)NVK_LOOKUP("linux_banner");
	if (!banner) {
		banner = (const char *)NVK_LOOKUP("linux_proc_banner");
	}

	if (banner) {
		const char *ver_start = banner;
		while (*ver_start && !(*ver_start >= '0' && *ver_start <= '9'))
			ver_start++;
		if (*ver_start)
			_nvk_parse_version(ver_start, &_nvk_kinfo);
	}

	if (!_nvk_kinfo.detected) {
#if NVK_KERNEL == 510
		_nvk_kinfo.major = 5;
		_nvk_kinfo.minor = 10;
		_nvk_kinfo.android_version = 12;
#elif NVK_KERNEL == 515
		_nvk_kinfo.major = 5;
		_nvk_kinfo.minor = 15;
		_nvk_kinfo.android_version = 13;
#elif NVK_KERNEL == 601
		_nvk_kinfo.major = 6;
		_nvk_kinfo.minor = 1;
		_nvk_kinfo.android_version = 14;
#elif NVK_KERNEL == 606
		_nvk_kinfo.major = 6;
		_nvk_kinfo.minor = 6;
		_nvk_kinfo.android_version = 15;
#elif NVK_KERNEL == 612
		_nvk_kinfo.major = 6;
		_nvk_kinfo.minor = 12;
		_nvk_kinfo.android_version = 16;
#endif
		_nvk_kinfo.detected = 1;
	}

	nvk_fmt_init();

	_nvk_compat_inited = 1;
	return 0;
}

static __always_inline const struct nvk_kernel_info *nvk_kernel_version(void)
{
	return &_nvk_kinfo;
}

static __always_inline u32 nvk_kernel_code(void)
{
	return _nvk_kinfo.major * 10000 + _nvk_kinfo.minor * 100
	       + _nvk_kinfo.patch;
}

#define NVK_KERNEL_GE(maj, min) \
	(_nvk_kinfo.major > (maj) || \
	 (_nvk_kinfo.major == (maj) && _nvk_kinfo.minor >= (min)))

#define NVK_KERNEL_LT(maj, min) (!NVK_KERNEL_GE(maj, min))

static __always_inline void *nvk_lookup_printk(void)
{
	void *sym = NVK_LOOKUP("_printk");
	if (!sym) sym = NVK_LOOKUP("printk");
	return sym;
}

static __always_inline void *nvk_lookup_copy_from_user(void)
{
	void *sym = NVK_LOOKUP("_copy_from_user");
	if (!sym) sym = NVK_LOOKUP("raw_copy_from_user");
	if (!sym) sym = NVK_LOOKUP("copy_from_user");
	return sym;
}

static __always_inline void *nvk_lookup_copy_to_user(void)
{
	void *sym = NVK_LOOKUP("_copy_to_user");
	if (!sym) sym = NVK_LOOKUP("raw_copy_to_user");
	if (!sym) sym = NVK_LOOKUP("copy_to_user");
	return sym;
}

static __always_inline void *nvk_lookup_probe_read(void)
{
	void *sym = NVK_LOOKUP("copy_from_kernel_nofault");
	if (!sym) sym = NVK_LOOKUP("probe_kernel_read");
	return sym;
}

static __always_inline void *nvk_lookup_probe_write(void)
{
	void *sym = NVK_LOOKUP("copy_to_kernel_nofault");
	if (!sym) sym = NVK_LOOKUP("probe_kernel_write");
	return sym;
}

static __always_inline void *nvk_lookup_module_alloc(void)
{
	void *sym = NVK_LOOKUP("module_alloc");
	if (!sym) sym = NVK_LOOKUP("execmem_alloc");
	return sym;
}

static __always_inline void *nvk_lookup_module_free(void)
{
	void *sym = NVK_LOOKUP("module_memfree");
	if (!sym) sym = NVK_LOOKUP("execmem_free");
	if (!sym) sym = NVK_LOOKUP("vfree");
	return sym;
}

static __always_inline int nvk_has_cfi(void)
{
	return NVK_LOOKUP("__cfi_check") != (void *)0;
}

static __always_inline int nvk_has_pac(void)
{
	unsigned long isar1;
	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
	int apa = (isar1 >> 4) & 0xF;
	int api = (isar1 >> 8) & 0xF;
	return (apa | api) != 0;
}

static __always_inline int nvk_has_bti(void)
{
	unsigned long pfr1;
	__asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(pfr1));
	return (pfr1 & 0xF) != 0;
}

static __always_inline int nvk_has_mte(void)
{
	unsigned long pfr1;
	__asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(pfr1));
	return ((pfr1 >> 8) & 0xF) >= 2;
}

static __always_inline int nvk_has_epac(void)
{
	unsigned long isar1;
	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
	int apa = (isar1 >> 4) & 0xF;
	int api = (isar1 >> 8) & 0xF;
	return (apa >= 2) || (api >= 2);
}

static __always_inline int nvk_has_fpac(void)
{
	unsigned long isar1;
	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
	int apa = (isar1 >> 4) & 0xF;
	int api = (isar1 >> 8) & 0xF;
	return (apa >= 3) || (api >= 3);
}

static __always_inline int nvk_has_sve(void)
{
	unsigned long pfr0;
	__asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
	return ((pfr0 >> 32) & 0xF) != 0;
}

struct nvk_hw_caps {
	int pac;
	int epac;
	int fpac;
	int bti;
	int mte;
	int sve;
	int cfi;
};

static void nvk_detect_hw_caps(struct nvk_hw_caps *caps)
{
	if (!caps) return;
	caps->pac  = nvk_has_pac();
	caps->epac = nvk_has_epac();
	caps->fpac = nvk_has_fpac();
	caps->bti  = nvk_has_bti();
	caps->mte  = nvk_has_mte();
	caps->sve  = nvk_has_sve();
	caps->cfi  = nvk_has_cfi();
}

enum nvk_version_match {
	NVK_VER_EXACT   =  0,
	NVK_VER_COMPAT  =  1,
	NVK_VER_MISMATCH = -1,
	NVK_VER_UNKNOWN  = -2,
};

static int nvk_check_kernel_match(void)
{
	if (!_nvk_kinfo.detected)
		return NVK_VER_UNKNOWN;

	u32 expected_major = 0, expected_minor = 0;
#if NVK_KERNEL == 510
	expected_major = 5; expected_minor = 10;
#elif NVK_KERNEL == 515
	expected_major = 5; expected_minor = 15;
#elif NVK_KERNEL == 601
	expected_major = 6; expected_minor = 1;
#elif NVK_KERNEL == 606
	expected_major = 6; expected_minor = 6;
#elif NVK_KERNEL == 612
	expected_major = 6; expected_minor = 12;
#endif

	if (_nvk_kinfo.major == expected_major &&
	    _nvk_kinfo.minor == expected_minor)
		return NVK_VER_EXACT;

	if (_nvk_kinfo.major == expected_major)
		return NVK_VER_COMPAT;

	return NVK_VER_MISMATCH;
}

static __always_inline int nvk_should_abort_on_mismatch(void)
{
	int r = nvk_check_kernel_match();
	return r == NVK_VER_MISMATCH;
}

static int nvk_verify_module_offsets(struct nvk_this_module *mod,
				     const char *expected_name)
{
	struct list_head *list;
	const char *name;
	int ok = 0;

	list = (struct list_head *)((char *)mod + NVK_OFF_LIST);
	if ((unsigned long)list->next < 0xFFFF000000000000UL &&
	    list->next != list)
		return -1;
	if ((unsigned long)list->prev < 0xFFFF000000000000UL &&
	    list->prev != list)
		return -2;

	name = (const char *)((char *)mod + NVK_OFF_NAME);
	if (expected_name) {
		const char *a = name;
		const char *b = expected_name;
		while (*a && *b && *a == *b) { a++; b++; }
		if (*a != *b) return -3;
	} else {
		unsigned char c;
		if (nvk_mem_read(&c, name, 1))
			return -4;
		if (c < 0x20 || c > 0x7E)
			return -5;
	}

	return 0;
}

#endif /* NVK_COMPAT_H */
