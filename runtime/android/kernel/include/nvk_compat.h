/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_COMPAT_H
#define NVK_COMPAT_H

#include <linux/types.h>
#include <nvk_rt.h>
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

NVK_RT_VAR struct nvk_kernel_info _nvk_kinfo;
NVK_RT_VAR int _nvk_compat_inited;

typedef int (*nvk_snprintf_fn)(char *buf, size_t size, const char *fmt, ...);
typedef int (*nvk_sscanf_fn)(const char *buf, const char *fmt, ...);

NVK_RT_VAR nvk_snprintf_fn _nvk_snprintf;
NVK_RT_VAR nvk_sscanf_fn   _nvk_sscanf;

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

void _nvk_parse_version(const char *str, struct nvk_kernel_info *info);


int nvk_compat_init(void);


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

void nvk_detect_hw_caps(struct nvk_hw_caps *caps);


enum nvk_version_match {
	NVK_VER_EXACT   =  0,
	NVK_VER_COMPAT  =  1,
	NVK_VER_MISMATCH = -1,
	NVK_VER_UNKNOWN  = -2,
};

int nvk_check_kernel_match(void);


static __always_inline int nvk_should_abort_on_mismatch(void)
{
	int r = nvk_check_kernel_match();
	return r == NVK_VER_MISMATCH;
}

int nvk_verify_module_offsets(struct nvk_this_module *mod,
				     const char *expected_name);



NVK_RT_VAR unsigned long _nvk_rt_off_init;
NVK_RT_VAR unsigned long _nvk_rt_off_exit;

int nvk_probe_module_offsets(struct nvk_this_module *mod,
				    void *expected_init,
				    void *expected_exit);


static __always_inline unsigned long nvk_rt_off_init(void)
{
	return _nvk_rt_off_init ? _nvk_rt_off_init : NVK_OFF_INIT;
}

static __always_inline unsigned long nvk_rt_off_exit(void)
{
	return _nvk_rt_off_exit ? _nvk_rt_off_exit : NVK_OFF_EXIT;
}

int nvk_validate_runtime(struct nvk_this_module *mod,
				const char *name,
				void *init_fn, void *exit_fn);


int nvk_patch_vermagic(struct nvk_this_module *mod);


int nvk_fixup_runtime(struct nvk_this_module *mod,
			     const char *name,
			     void *init_fn, void *exit_fn);


#endif /* NVK_COMPAT_H */
