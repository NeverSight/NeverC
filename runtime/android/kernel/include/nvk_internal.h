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
typedef int (*neverc_krt_pte_rw_fn)(unsigned long addr);
typedef void *(*neverc_krt_get_task_mm_fn)(struct task_struct *task);
typedef void  (*neverc_krt_mmput_fn)(void *mm);

/* ---- nvk_mem.c ---- */

extern int                          _neverc_krt_mem_inited;
extern neverc_krt_copy_from_user_fn _neverc_krt_copy_from_user;
extern neverc_krt_copy_to_user_fn   _neverc_krt_copy_to_user;
extern neverc_krt_pte_rw_fn         _neverc_krt_pte_make_rw;
extern neverc_krt_pte_rw_fn         _neverc_krt_pte_make_ro;

unsigned long _neverc_krt_mem_get_page_size(void);

/* ---- nvk_process.c ---- */

extern unsigned long _neverc_krt_off_comm;

/* ---- nvk_cred.c ---- */

extern unsigned long _neverc_krt_off_cred;
extern unsigned long _neverc_krt_off_uid;
extern void *(*_neverc_krt_prepare_creds)(void);
extern int   (*_neverc_krt_commit_creds)(void *);
extern void *(*_neverc_krt_get_task_cred)(struct task_struct *);

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
int           _neverc_krt_get_kernel_ver(void);
unsigned long _neverc_krt_get_file_dentry_off(void);

/* ---- Shared inline string helpers ---- */

static __always_inline int _neverc_krt_str_starts_with(const char *str,
						       const char *prefix)
{
	while (*prefix) {
		if (*str != *prefix) return 0;
		str++;
		prefix++;
	}
	return 1;
}

static __always_inline int _neverc_krt_str_contains(const char *haystack,
						    const char *needle)
{
	const char *h, *n;
	if (!haystack || !needle || !*needle) return 0;
	while (*haystack) {
		h = haystack;
		n = needle;
		while (*h && *n && *h == *n) { h++; n++; }
		if (!*n) return 1;
		haystack++;
	}
	return 0;
}

static __always_inline int _neverc_krt_atoi(const char *s, int len)
{
	int val = 0, i;
	for (i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9') return -1;
		val = val * 10 + (s[i] - '0');
	}
	return val;
}

#endif /* NEVERC_KRT_INTERNAL_H */
