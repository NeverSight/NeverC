/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_COMPAT_H
#define NEVERC_KRT_COMPAT_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvkmod_version.h>

struct neverc_krt_this_module;

struct neverc_krt_kernel_info {
	u32 major;
	u32 minor;
	u32 patch;
	u32 android_version;
	u32 kmi_generation;
	u32 page_shift;
	u32 sublevel;
	int detected;
};

int neverc_krt_fmt_init(void);

int neverc_krt_snprintf(char *buf, size_t size, const char *fmt, ...);
int neverc_krt_sscanf(const char *buf, const char *fmt, ...);

int neverc_krt_compat_init(void);
const struct neverc_krt_kernel_info *neverc_krt_kernel_version(void);

u32 neverc_krt_kernel_code(void);

int neverc_krt_kernel_ge(u32 maj, u32 min);

int neverc_krt_kernel_lt(u32 maj, u32 min);

void *neverc_krt_lookup_printk(void);
void *neverc_krt_lookup_copy_from_user(void);
void *neverc_krt_lookup_copy_to_user(void);
void *neverc_krt_lookup_probe_read(void);
void *neverc_krt_lookup_probe_write(void);
void *neverc_krt_lookup_module_alloc(void);
void *neverc_krt_lookup_module_free(void);

int neverc_krt_has_cfi(void);

enum neverc_krt_version_match {
	/* Complete certified release identity and page size matched. */
	NEVERC_KRT_VER_EXACT   =  0,
	/*
	 * Explicitly selected profile; same Linux major.minor, Android
	 * generation, and page size.  A certificate may overlay offsets.
	 */
	NEVERC_KRT_VER_COMPAT  =  1,
	NEVERC_KRT_VER_MISMATCH = -1,
	NEVERC_KRT_VER_UNKNOWN  = -2,
};

int neverc_krt_check_kernel_match(void);
int neverc_krt_should_abort_on_mismatch(void);
int neverc_krt_verify_module_offsets(struct neverc_krt_this_module *mod,
				     const char *expected_name);
int neverc_krt_probe_module_offsets(struct neverc_krt_this_module *mod,
				    void *expected_init,
				    void *expected_exit);
unsigned long neverc_krt_rt_off_init(void);
unsigned long neverc_krt_rt_off_exit(void);
int neverc_krt_validate_runtime(struct neverc_krt_this_module *mod,
				const char *name,
				void *init_fn, void *exit_fn);
/*
 * Rewrite a "vermagic=" blob inside this_module after identity accept.
 * This is not a loader bypass: insmod already compared .modinfo.
 */
int neverc_krt_patch_vermagic(struct neverc_krt_this_module *mod);
int neverc_krt_fixup_runtime(struct neverc_krt_this_module *mod,
			     const char *name,
			     void *init_fn, void *exit_fn);

#endif /* NEVERC_KRT_COMPAT_H */
