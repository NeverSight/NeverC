/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_SELINUX_H
#define NEVERC_KRT_SELINUX_H

#include <linux/types.h>
#include <nvk_interpose.h>

int neverc_krt_selinux_init(void);

int neverc_krt_selinux_is_enforcing(void);

int neverc_krt_selinux_set_permissive(void);
int neverc_krt_selinux_set_enforcing(void);

struct neverc_krt_selinux_policy {
	int avc_interposed;
	int inode_interposed;
	int task_perm_interposed;
	int cred_perm_interposed;
	int state_patched;
	int saved_enforce;
};

int neverc_krt_selinux_policy_install(struct neverc_krt_selinux_policy *state);
int neverc_krt_selinux_patch_state(struct neverc_krt_selinux_policy *state);
void neverc_krt_selinux_restore_state(struct neverc_krt_selinux_policy *state);
int neverc_krt_selinux_policy_full(struct neverc_krt_selinux_policy *state);
void neverc_krt_selinux_policy_remove(struct neverc_krt_selinux_policy *state);

int neverc_krt_selinux_pause_interposes(void);
void neverc_krt_selinux_remove_interposes(void);

/* --- Per-UID selective SELinux policy-control helpers --- */

#define NEVERC_KRT_SE_UID_MAX 16

#define NEVERC_KRT_SE_FLAG_AVC     (1U << 0)
#define NEVERC_KRT_SE_FLAG_INODE   (1U << 1)
#define NEVERC_KRT_SE_FLAG_TASK    (1U << 2)
#define NEVERC_KRT_SE_FLAG_CAPABLE (1U << 3)
#define NEVERC_KRT_SE_FLAG_ALL     0xFU

int neverc_krt_se_selective_add(u32 uid, u32 flags);
int neverc_krt_se_selective_remove(u32 uid);
int neverc_krt_se_selective_install(void);
void neverc_krt_se_selective_cleanup(void);

#endif /* NEVERC_KRT_SELINUX_H */
