/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_COMPAT_H
#define NEVERC_KRT_COMPAT_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <nvk_mem.h>

struct neverc_krt_kernel_info {
	u32 major;
	u32 minor;
	u32 patch;
	u32 android_version;
	u32 sublevel;
	int detected;
};

NEVERC_KRT_RT_VAR struct neverc_krt_kernel_info _neverc_krt_kinfo;
NEVERC_KRT_RT_VAR int _neverc_krt_compat_inited;

typedef int (*neverc_krt_snprintf_fn)(char *buf, size_t size, const char *fmt, ...);
typedef int (*neverc_krt_sscanf_fn)(const char *buf, const char *fmt, ...);

NEVERC_KRT_RT_VAR neverc_krt_snprintf_fn _neverc_krt_snprintf;
NEVERC_KRT_RT_VAR neverc_krt_sscanf_fn   _neverc_krt_sscanf;

static __always_inline int neverc_krt_fmt_init(void)
{
	if (!_neverc_krt_snprintf) {
		_neverc_krt_snprintf = (neverc_krt_snprintf_fn)NEVERC_KRT_LOOKUP("snprintf");
		if (!_neverc_krt_snprintf)
			_neverc_krt_snprintf = (neverc_krt_snprintf_fn)NEVERC_KRT_LOOKUP("scnprintf");
	}
	_neverc_krt_sscanf = (neverc_krt_sscanf_fn)NEVERC_KRT_LOOKUP("sscanf");
	return _neverc_krt_snprintf ? 0 : -1;
}

#define neverc_krt_snprintf(buf, sz, fmt, ...)                                       \
	(_neverc_krt_snprintf ? _neverc_krt_snprintf((buf), (sz), (fmt), ##__VA_ARGS__) : -1)

#define neverc_krt_sscanf(buf, fmt, ...)                                             \
	(_neverc_krt_sscanf ? _neverc_krt_sscanf((buf), (fmt), ##__VA_ARGS__) : -1)



int neverc_krt_compat_init(void);


static __always_inline const struct neverc_krt_kernel_info *neverc_krt_kernel_version(void)
{
	return &_neverc_krt_kinfo;
}

static __always_inline u32 neverc_krt_kernel_code(void)
{
	return _neverc_krt_kinfo.major * 10000 + _neverc_krt_kinfo.minor * 100
	       + _neverc_krt_kinfo.patch;
}

#define NEVERC_KRT_KERNEL_GE(maj, min) \
	(_neverc_krt_kinfo.major > (maj) || \
	 (_neverc_krt_kinfo.major == (maj) && _neverc_krt_kinfo.minor >= (min)))

#define NEVERC_KRT_KERNEL_LT(maj, min) (!NEVERC_KRT_KERNEL_GE(maj, min))

static __always_inline void *neverc_krt_lookup_printk(void)
{
	void *sym = NEVERC_KRT_LOOKUP("_printk");
	if (!sym) sym = NEVERC_KRT_LOOKUP("printk");
	return sym;
}

static __always_inline void *neverc_krt_lookup_copy_from_user(void)
{
	void *sym = NEVERC_KRT_LOOKUP("_copy_from_user");
	if (!sym) sym = NEVERC_KRT_LOOKUP("raw_copy_from_user");
	if (!sym) sym = NEVERC_KRT_LOOKUP("copy_from_user");
	return sym;
}

static __always_inline void *neverc_krt_lookup_copy_to_user(void)
{
	void *sym = NEVERC_KRT_LOOKUP("_copy_to_user");
	if (!sym) sym = NEVERC_KRT_LOOKUP("raw_copy_to_user");
	if (!sym) sym = NEVERC_KRT_LOOKUP("copy_to_user");
	return sym;
}

static __always_inline void *neverc_krt_lookup_probe_read(void)
{
	void *sym = NEVERC_KRT_LOOKUP("copy_from_kernel_nofault");
	if (!sym) sym = NEVERC_KRT_LOOKUP("probe_kernel_read");
	return sym;
}

static __always_inline void *neverc_krt_lookup_probe_write(void)
{
	void *sym = NEVERC_KRT_LOOKUP("copy_to_kernel_nofault");
	if (!sym) sym = NEVERC_KRT_LOOKUP("probe_kernel_write");
	return sym;
}

static __always_inline void *neverc_krt_lookup_module_alloc(void)
{
	void *sym = NEVERC_KRT_LOOKUP("module_alloc");
	if (!sym) sym = NEVERC_KRT_LOOKUP("execmem_alloc");
	return sym;
}

static __always_inline void *neverc_krt_lookup_module_free(void)
{
	void *sym = NEVERC_KRT_LOOKUP("module_memfree");
	if (!sym) sym = NEVERC_KRT_LOOKUP("execmem_free");
	if (!sym) sym = NEVERC_KRT_LOOKUP("vfree");
	return sym;
}

static __always_inline int neverc_krt_has_cfi(void)
{
	return NEVERC_KRT_LOOKUP("__cfi_check") != (void *)0;
}

static __always_inline int neverc_krt_has_pac(void)
{
	unsigned long isar1;
	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
	int apa = (isar1 >> 4) & 0xF;
	int api = (isar1 >> 8) & 0xF;
	return (apa | api) != 0;
}

static __always_inline int neverc_krt_has_bti(void)
{
	unsigned long pfr1;
	__asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(pfr1));
	return (pfr1 & 0xF) != 0;
}

static __always_inline int neverc_krt_has_mte(void)
{
	unsigned long pfr1;
	__asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(pfr1));
	return ((pfr1 >> 8) & 0xF) >= 2;
}

static __always_inline int neverc_krt_has_epac(void)
{
	unsigned long isar1;
	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
	int apa = (isar1 >> 4) & 0xF;
	int api = (isar1 >> 8) & 0xF;
	return (apa >= 2) || (api >= 2);
}

static __always_inline int neverc_krt_has_fpac(void)
{
	unsigned long isar1;
	__asm__ __volatile__("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
	int apa = (isar1 >> 4) & 0xF;
	int api = (isar1 >> 8) & 0xF;
	return (apa >= 3) || (api >= 3);
}

static __always_inline int neverc_krt_has_sve(void)
{
	unsigned long pfr0;
	__asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
	return ((pfr0 >> 32) & 0xF) != 0;
}

struct neverc_krt_hw_caps {
	int pac;
	int epac;
	int fpac;
	int bti;
	int mte;
	int sve;
	int cfi;
};

void neverc_krt_detect_hw_caps(struct neverc_krt_hw_caps *caps);


enum neverc_krt_version_match {
	NEVERC_KRT_VER_EXACT   =  0,
	NEVERC_KRT_VER_COMPAT  =  1,
	NEVERC_KRT_VER_MISMATCH = -1,
	NEVERC_KRT_VER_UNKNOWN  = -2,
};

int neverc_krt_check_kernel_match(void);


static __always_inline int neverc_krt_should_abort_on_mismatch(void)
{
	int r = neverc_krt_check_kernel_match();
	return r == NEVERC_KRT_VER_MISMATCH;
}

int neverc_krt_verify_module_offsets(struct neverc_krt_this_module *mod,
				     const char *expected_name);



NEVERC_KRT_RT_VAR unsigned long _neverc_krt_rt_off_init;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_rt_off_exit;

int neverc_krt_probe_module_offsets(struct neverc_krt_this_module *mod,
				    void *expected_init,
				    void *expected_exit);


static __always_inline unsigned long neverc_krt_rt_off_init(void)
{
	return _neverc_krt_rt_off_init ? _neverc_krt_rt_off_init : NEVERC_KRT_OFF_INIT;
}

static __always_inline unsigned long neverc_krt_rt_off_exit(void)
{
	return _neverc_krt_rt_off_exit ? _neverc_krt_rt_off_exit : NEVERC_KRT_OFF_EXIT;
}

int neverc_krt_validate_runtime(struct neverc_krt_this_module *mod,
				const char *name,
				void *init_fn, void *exit_fn);


int neverc_krt_patch_vermagic(struct neverc_krt_this_module *mod);


int neverc_krt_fixup_runtime(struct neverc_krt_this_module *mod,
			     const char *name,
			     void *init_fn, void *exit_fn);


#endif /* NEVERC_KRT_COMPAT_H */
