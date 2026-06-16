/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_SELINUX_H
#define NVK_SELINUX_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include <nvk_hook.h>

NVK_RT_VAR volatile int *_nvk_selinux_enforcing;
NVK_RT_VAR int _nvk_selinux_inited;
NVK_RT_VAR int _nvk_se_method; /* 0=unknown 1=direct 2=state_struct 3=fn_scan */

volatile int *_nvk_se_probe_state(void *se_state);


volatile int *_nvk_se_probe_fn(void);


int nvk_selinux_init(void);


static __always_inline int nvk_selinux_is_enforcing(void)
{
	if (!_nvk_selinux_enforcing) return -1;
	return __atomic_load_n(_nvk_selinux_enforcing, __ATOMIC_ACQUIRE);
}

int _nvk_se_write(volatile int *addr, int val);


int nvk_selinux_set_permissive(void);


int nvk_selinux_set_enforcing(void);



typedef long (*nvk_selinux_generic_fn)(void);
typedef int (*nvk_inode_permission_fn)(void *inode, int mask);

NVK_RT_VAR struct nvk_hook _nvk_avc_hook;
NVK_RT_VAR nvk_selinux_generic_fn _nvk_orig_avc;

NVK_RT_VAR struct nvk_hook _nvk_inode_hook;
NVK_RT_VAR nvk_inode_permission_fn _nvk_orig_inode_perm;

NVK_RT_VAR struct nvk_hook _nvk_task_perm_hook;
NVK_RT_VAR nvk_selinux_generic_fn _nvk_orig_task_perm;

NVK_RT_VAR struct nvk_hook _nvk_cred_perm_hook;
NVK_RT_VAR nvk_selinux_generic_fn _nvk_orig_cred_perm;

struct nvk_selinux_bypass {
	int avc_hooked;
	int inode_hooked;
	int task_perm_hooked;
	int cred_perm_hooked;
	int state_patched;
	int saved_enforce;
};

long _nvk_return_zero(void);


int nvk_selinux_bypass_install(struct nvk_selinux_bypass *state);


NVK_RT_VAR unsigned long _nvk_se_patched_addr;

int nvk_selinux_patch_state(struct nvk_selinux_bypass *state);


void nvk_selinux_restore_state(struct nvk_selinux_bypass *state);


int nvk_selinux_full_bypass(struct nvk_selinux_bypass *state);


void nvk_selinux_bypass_remove(struct nvk_selinux_bypass *state);



/* --- Per-UID selective SELinux bypass --- */

#define NVK_SE_UID_MAX 16

struct nvk_se_uid_entry {
	u32  uid;
	u32  flags;
	volatile int active;
};

#define NVK_SE_FLAG_AVC     (1U << 0)
#define NVK_SE_FLAG_INODE   (1U << 1)
#define NVK_SE_FLAG_TASK    (1U << 2)
#define NVK_SE_FLAG_CAPABLE (1U << 3)
#define NVK_SE_FLAG_ALL     0xFU

struct nvk_se_selective {
	struct nvk_se_uid_entry uids[NVK_SE_UID_MAX];
	int count;
	struct nvk_hook avc_hook;
	struct nvk_hook inode_hook;
	struct nvk_hook capable_hook;
	int active;
};

NVK_RT_VAR struct nvk_se_selective _nvk_se_sel;

NVK_RT_VAR nvk_selinux_generic_fn _nvk_sel_orig_avc;
NVK_RT_VAR nvk_inode_permission_fn _nvk_sel_orig_inode;
NVK_RT_VAR nvk_selinux_generic_fn _nvk_sel_orig_capable;

static __always_inline u32 _nvk_se_current_uid(void)
{
	unsigned long task;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));

	unsigned long cred_off =
		__atomic_load_n(&_nvk_off_cred, __ATOMIC_ACQUIRE);
	if (!cred_off) return 0xFFFFFFFFU;

	unsigned long cred_ptr;
	if (nvk_mem_read(&cred_ptr, (void *)(task + cred_off), 8))
		return 0xFFFFFFFFU;
	cred_ptr &= ~(0xFFUL << 56);
	if (cred_ptr < 0xFFFF000000000000UL) return 0xFFFFFFFFU;

	unsigned long uid_off =
		__atomic_load_n(&_nvk_off_uid, __ATOMIC_ACQUIRE);
	if (!uid_off) uid_off = 4;
	u32 uid = 0xFFFFFFFFU;
	nvk_mem_read(&uid, (void *)(cred_ptr + uid_off), 4);
	return uid;
}

int _nvk_se_uid_allowed(u32 uid, u32 flag);


int nvk_se_selective_add(u32 uid, u32 flags);


int nvk_se_selective_remove(u32 uid);


long _nvk_sel_avc_filter(void);


int _nvk_sel_inode_filter(void *inode, int mask);


long _nvk_sel_capable_filter(void);


int nvk_se_selective_install(void);


void nvk_se_selective_cleanup(void);


#endif /* NVK_SELINUX_H */
