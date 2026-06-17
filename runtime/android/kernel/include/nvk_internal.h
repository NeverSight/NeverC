/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvk_internal.h — Cross-file internal declarations for NVK runtime.
 *
 * This header is ONLY included by the runtime .c sources (unity build).
 * User code must NOT include this file.
 */
#ifndef NEVERC_KRT_INTERNAL_H
#define NEVERC_KRT_INTERNAL_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <nvk_hook.h>

/* ---- Shared typedefs (used across multiple .c files) ---- */

typedef long (*neverc_krt_probe_read_fn)(void *dst, const void *src,
					 size_t len);
typedef long (*neverc_krt_probe_write_fn)(void *dst, const void *src,
					  size_t len);
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
typedef void *(*neverc_krt_get_task_cred_fn)(struct task_struct *);

/* ---- nvk_mem.c ---- */

extern int                          _neverc_krt_mem_inited;
extern neverc_krt_copy_from_user_fn _neverc_krt_copy_from_user;
extern neverc_krt_copy_to_user_fn   _neverc_krt_copy_to_user;
extern neverc_krt_pte_rw_fn         _neverc_krt_pte_make_rw;
extern neverc_krt_pte_rw_fn         _neverc_krt_pte_make_ro;

unsigned long _neverc_krt_mem_get_page_size(void);

/* ---- nvk_process.c ---- */

extern unsigned long               _neverc_krt_off_comm;
extern neverc_krt_get_task_cred_fn _neverc_krt_get_task_cred;
extern neverc_krt_prepare_creds_fn _neverc_krt_prepare_creds;
extern neverc_krt_commit_creds_fn  _neverc_krt_commit_creds;

/* ---- nvk_cred.c ---- */

extern unsigned long _neverc_krt_off_cred;
extern unsigned long _neverc_krt_off_uid;

/* ---- nvk_selinux.c ---- */

volatile int *_neverc_krt_se_probe_state(void *se_state);

/* ---- nvk_hook.c ---- */

int _neverc_krt_patch_multi(u32 *target, u32 *insns, int count);

/* ---- nvk_compat.c ---- */

extern unsigned long _neverc_krt_module_size;
extern int           _neverc_krt_kernel_ver;
extern unsigned long _neverc_krt_file_dentry_off;

unsigned long _neverc_krt_get_module_size(void);
unsigned long _neverc_krt_cred_uid_base(void);
unsigned long _neverc_krt_get_file_dentry_off(void);

#endif /* NEVERC_KRT_INTERNAL_H */
