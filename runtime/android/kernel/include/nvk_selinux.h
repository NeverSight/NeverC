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
	if (nvk_mem_make_rw((unsigned long)_nvk_selinux_enforcing))
		return -2;
	*(volatile int *)_nvk_selinux_enforcing = 0;
	__asm__ __volatile__("dsb ish" ::: "memory");
	nvk_mem_make_ro((unsigned long)_nvk_selinux_enforcing);
	return 0;
}

static int nvk_selinux_set_enforcing(void)
{
	if (!_nvk_selinux_enforcing) return -1;
	if (nvk_mem_make_rw((unsigned long)_nvk_selinux_enforcing))
		return -2;
	*(volatile int *)_nvk_selinux_enforcing = 1;
	__asm__ __volatile__("dsb ish" ::: "memory");
	nvk_mem_make_ro((unsigned long)_nvk_selinux_enforcing);
	return 0;
}


typedef int (*nvk_avc_denied_fn)(void *ssid, void *tsid, u16 tclass,
				 u32 requested, u8 driver, u8 xperm,
				 unsigned int flags, void *avd);
typedef int (*nvk_inode_permission_fn)(void *inode, int mask);

static struct nvk_hook _nvk_avc_hook;
static nvk_avc_denied_fn _nvk_orig_avc;

static struct nvk_hook _nvk_inode_hook;
static nvk_inode_permission_fn _nvk_orig_inode_perm;

struct nvk_selinux_bypass {
	int avc_hooked;
	int inode_hooked;
};

static int _nvk_avc_allow(void *ssid, void *tsid, u16 tclass,
			   u32 requested, u8 driver, u8 xperm,
			   unsigned int flags, void *avd)
{
	(void)ssid; (void)tsid; (void)tclass; (void)requested;
	(void)driver; (void)xperm; (void)flags; (void)avd;
	return 0;
}

static int _nvk_inode_allow(void *inode, int mask)
{
	(void)inode; (void)mask;
	return 0;
}

static int nvk_selinux_bypass_install(struct nvk_selinux_bypass *state)
{
	void *target;
	int ret;

	state->avc_hooked = 0;
	state->inode_hooked = 0;

	target = NVK_LOOKUP("avc_denied");
	if (target) {
		ret = nvk_hook_install(&_nvk_avc_hook, target,
				       (void *)_nvk_avc_allow,
				       (void **)&_nvk_orig_avc);
		if (ret == 0)
			state->avc_hooked = 1;
	}

	target = NVK_LOOKUP("selinux_inode_permission");
	if (target) {
		ret = nvk_hook_install(&_nvk_inode_hook, target,
				       (void *)_nvk_inode_allow,
				       (void **)&_nvk_orig_inode_perm);
		if (ret == 0)
			state->inode_hooked = 1;
	}

	return (state->avc_hooked || state->inode_hooked) ? 0 : -1;
}

static void nvk_selinux_bypass_remove(struct nvk_selinux_bypass *state)
{
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
