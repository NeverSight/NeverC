/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvk_internal.h — Cross-file internal declarations for NVK runtime.
 *
 * This header is only included by runtime C sources. User code must not
 * include it.
 */
#ifndef NEVERC_KRT_INTERNAL_H
#define NEVERC_KRT_INTERNAL_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <nvk_interpose_advanced.h>

/* ---- ARM64 instruction constants (used by nvk_interpose.c, nvk_anti.c) ---- */

#define NEVERC_KRT_A64_NOP       0xD503201FU
#define NEVERC_KRT_A64_BTI_C     0xD503245FU
#define NEVERC_KRT_A64_BTI_JC    0xD50324DFU
#define NEVERC_KRT_A64_PACIASP   0xD503233FU
#define NEVERC_KRT_A64_PACIBSP   0xD503237FU
#define NEVERC_KRT_A64_AUTIASP   0xD50323BFU
#define NEVERC_KRT_A64_MOV_X0_0  0xD2800000U
#define NEVERC_KRT_A64_MOV_X0_XZR 0xAA1F03E0U
#define NEVERC_KRT_A64_RET       0xD65F03C0U
#define NEVERC_KRT_A64_LDR_X16_PC8 0x58000050U
#define NEVERC_KRT_A64_BR_X16    0xD61F0200U

#define NEVERC_KRT_A64_RET_X16   0xD65F0200U
#define NEVERC_KRT_A64_RET_X17   0xD65F0220U

/* ---- Interpose engine constants (used by nvk_interpose.c, nvk_anti.c) ---- */

#define NEVERC_KRT_INTERPOSE_TRAMP_CAP  64
#define NEVERC_KRT_INTERPOSE_STUB_CAP  128

/* ---- Shared typedefs (used across multiple .c files) ---- */

typedef unsigned long (*neverc_krt_copy_from_user_fn)(void *to,
						      const void __user *from,
						      unsigned long n);
typedef unsigned long (*neverc_krt_copy_to_user_fn)(void __user *to,
						    const void *from,
						    unsigned long n);
typedef int  (*neverc_krt_pte_rw_fn)(unsigned long addr);
typedef void *(*neverc_krt_get_task_mm_fn)(struct task_struct *task);
typedef void  (*neverc_krt_mmput_fn)(void *mm);
typedef void *(*neverc_krt_prepare_creds_fn)(void);
typedef int   (*neverc_krt_commit_creds_fn)(void *);
typedef unsigned long (*_neverc_krt_sym_resolver_fn)(const char *name);

/*
 * Configured GKI offsets consumed by runtime code.  These are generated from
 * the per-profile BTF/DWARF manifests; no C source may guess them from a
 * kernel-version range or a maximum-sized opaque structure.
 */
struct neverc_krt_gki_layout {
	unsigned long task_tasks;
	unsigned long task_mm;
	unsigned long task_pid;
	unsigned long task_group_leader;
	unsigned long task_real_cred;
	unsigned long task_cred;
	unsigned long task_comm;
	unsigned long cred_uid;
	unsigned long cred_securebits;
	unsigned long cred_cap_inheritable;
	unsigned long vma_start;
	unsigned long vma_end;
	unsigned long vma_mm;
	unsigned long vma_page_prot;
	unsigned long vma_flags;
	unsigned long vma_pgoff;
	unsigned long vma_file;
	unsigned long vmap_va_start;
	unsigned long vmap_va_end;
};

/* ---- nvk_ksyms.c (shared with nvkmod.c) ---- */

extern _neverc_krt_sym_resolver_fn _neverc_krt_sym_resolver;
void _neverc_krt_cache_key_init(void);

/* ---- nvk_mem.c ---- */

extern int                          _neverc_krt_mem_inited;
extern neverc_krt_copy_from_user_fn _neverc_krt_copy_from_user;
extern neverc_krt_copy_to_user_fn   _neverc_krt_copy_to_user;
extern neverc_krt_pte_rw_fn         _neverc_krt_pte_make_rw;
extern neverc_krt_pte_rw_fn         _neverc_krt_pte_make_ro;

/* ---- nvk_vma.c (shared with nvk_xmem.c) ---- */

extern neverc_krt_get_task_mm_fn    _neverc_krt_get_task_mm;
extern neverc_krt_mmput_fn          _neverc_krt_mmput;

unsigned long _neverc_krt_mem_get_page_size(void);

/* ---- nvk_process.c ---- */

extern unsigned long               _neverc_krt_off_comm;
extern neverc_krt_prepare_creds_fn _neverc_krt_prepare_creds;
extern neverc_krt_commit_creds_fn  _neverc_krt_commit_creds;

/* ---- nvk_cred.c ---- */

extern unsigned long _neverc_krt_off_cred;
extern unsigned long _neverc_krt_off_uid;

int _neverc_krt_cred_find_uid_offset(void);

/* ---- nvk_selinux.c ---- */

volatile int *_neverc_krt_se_probe_state(void *se_state);

/* ---- nvk_interpose.c ---- */

int _neverc_krt_patch_multi(u32 *target, u32 *insns, int count);

/* ---- nvk_compat.c ---- */

extern int           _neverc_krt_kernel_ver;
extern unsigned long _neverc_krt_file_dentry_off;

void _neverc_krt_version_setup(int kv);
const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void);
unsigned long _neverc_krt_get_module_size(void);
unsigned long _neverc_krt_get_kimage_vaddr_base(void);
unsigned long _neverc_krt_cred_uid_base(void);
unsigned long _neverc_krt_get_file_dentry_off(void);

void _neverc_krt_version_try_detect_from_banner(void);

void neverc_krt_interpose_pause(struct neverc_krt_interpose *h);

long _neverc_krt_sext(long value, int bits);

#endif /* NEVERC_KRT_INTERNAL_H */
