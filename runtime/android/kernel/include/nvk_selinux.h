/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_SELINUX_H
#define NEVERC_KRT_SELINUX_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include <nvk_hook.h>

NEVERC_KRT_RT_VAR volatile int *_neverc_krt_selinux_enforcing;

int neverc_krt_selinux_init(void);

static __always_inline int neverc_krt_selinux_is_enforcing(void)
{
	if (!_neverc_krt_selinux_enforcing) return -1;
	return __atomic_load_n(_neverc_krt_selinux_enforcing, __ATOMIC_ACQUIRE);
}

int neverc_krt_selinux_set_permissive(void);
int neverc_krt_selinux_set_enforcing(void);

struct neverc_krt_selinux_bypass {
	int avc_hooked;
	int inode_hooked;
	int task_perm_hooked;
	int cred_perm_hooked;
	int state_patched;
	int saved_enforce;
};

int neverc_krt_selinux_bypass_install(struct neverc_krt_selinux_bypass *state);
int neverc_krt_selinux_patch_state(struct neverc_krt_selinux_bypass *state);
void neverc_krt_selinux_restore_state(struct neverc_krt_selinux_bypass *state);
int neverc_krt_selinux_full_bypass(struct neverc_krt_selinux_bypass *state);
void neverc_krt_selinux_bypass_remove(struct neverc_krt_selinux_bypass *state);


/* --- Per-UID selective SELinux bypass --- */

#define NEVERC_KRT_SE_UID_MAX 16

#define NEVERC_KRT_SE_FLAG_AVC     (1U << 0)
#define NEVERC_KRT_SE_FLAG_INODE   (1U << 1)
#define NEVERC_KRT_SE_FLAG_TASK    (1U << 2)
#define NEVERC_KRT_SE_FLAG_CAPABLE (1U << 3)
#define NEVERC_KRT_SE_FLAG_ALL     0xFU

static __always_inline u32 _neverc_krt_se_current_uid(void)
{
	unsigned long task;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));

	unsigned long cred_off =
		__atomic_load_n(&_neverc_krt_off_cred, __ATOMIC_ACQUIRE);
	if (!cred_off) return 0xFFFFFFFFU;

	unsigned long cred_ptr;
	if (neverc_krt_mem_read(&cred_ptr, (void *)(task + cred_off), 8))
		return 0xFFFFFFFFU;
	cred_ptr &= ~(0xFFUL << 56);
	if (cred_ptr < 0xFFFF000000000000UL) return 0xFFFFFFFFU;

	unsigned long uid_off =
		__atomic_load_n(&_neverc_krt_off_uid, __ATOMIC_ACQUIRE);
	if (!uid_off) uid_off = _neverc_krt_cred_uid_base();
	u32 uid = 0xFFFFFFFFU;
	neverc_krt_mem_read(&uid, (void *)(cred_ptr + uid_off), 4);
	return uid;
}

int neverc_krt_se_selective_add(u32 uid, u32 flags);
int neverc_krt_se_selective_remove(u32 uid);
int neverc_krt_se_selective_install(void);
void neverc_krt_se_selective_cleanup(void);

#endif /* NEVERC_KRT_SELINUX_H */
