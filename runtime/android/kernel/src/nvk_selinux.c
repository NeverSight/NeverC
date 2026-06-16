/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_selinux.c — implementations extracted from nvk_selinux.h. */
#include <nvk.h>

volatile int *_nvk_se_probe_state(void *se_state)
{
	const unsigned char *p = (const unsigned char *)se_state;
	unsigned long i;

	for (i = 0; i < 64; i += 4) {
		int v0, v1;
		if (nvk_mem_read(&v0, p + i, 4)) continue;
		if (nvk_mem_read(&v1, p + i + 4, 4)) continue;
		if (v0 == 1 && (v1 == 0 || v1 == 1))
			return (volatile int *)(p + i + 4);
		if (i == 0 && (v0 == 0 || v0 == 1)) {
			int v2;
			if (!nvk_mem_read(&v2, p + 4, 4) && v2 == 1)
				return (volatile int *)p;
		}
	}
	return (volatile int *)0;
}

volatile int *_nvk_se_probe_fn(void)
{
	void *fn = NVK_LOOKUP("enforcing_setup");
	if (!fn) fn = NVK_LOOKUP("sel_read_enforce");
	if (!fn) return (volatile int *)0;

	u32 *code = (u32 *)fn;
	int i;
	for (i = 0; i < 32; i++) {
		u32 insn = code[i];
		if ((insn & 0x9F000000) == 0x90000000) {
			int rd = insn & 0x1F;
			int immlo = (insn >> 29) & 3;
			long immhi = nvk_sext((insn >> 5) & 0x7FFFF, 19);
			unsigned long page = ((unsigned long)&code[i] & ~0xFFFUL)
					     + (((immhi << 2) | immlo) << 12);
			if (i + 1 < 32) {
				u32 next = code[i + 1];
				if ((next & 0xFFC003E0) == (0xB9400000U | (rd << 5))) {
					unsigned long imm12 =
						((next >> 10) & 0xFFF) << 2;
					unsigned long addr = page + imm12;
					if (addr > 0xFFFF000000000000UL) {
						int v;
						if (!nvk_mem_read(&v, (void *)addr, 4) &&
						    (v == 0 || v == 1))
							return (volatile int *)addr;
					}
				}
				if ((next & 0xFFC003E0) == (0x91000000U | ((u32)rd << 5) | rd)) {
					unsigned long imm12 =
						((next >> 10) & 0xFFF);
					unsigned long addr = page + imm12;
					if (addr > 0xFFFF000000000000UL) {
						int v;
						if (!nvk_mem_read(&v, (void *)addr, 4) &&
						    (v == 0 || v == 1))
							return (volatile int *)addr;
					}
				}
			}
		}
	}
	return (volatile int *)0;
}

int nvk_selinux_init(void)
{
	if (_nvk_selinux_inited) return 0;

	if (!_nvk_mem_inited)
		nvk_mem_init();

	_nvk_selinux_enforcing =
		(volatile int *)NVK_LOOKUP("selinux_enforcing");
	if (_nvk_selinux_enforcing) {
		_nvk_se_method = 1;
		_nvk_selinux_inited = 1;
		return 0;
	}

	void *se_state = NVK_LOOKUP("selinux_state");
	if (se_state) {
		_nvk_selinux_enforcing = _nvk_se_probe_state(se_state);
		if (_nvk_selinux_enforcing) {
			_nvk_se_method = 2;
			_nvk_selinux_inited = 1;
			return 0;
		}
	}

	_nvk_selinux_enforcing = _nvk_se_probe_fn();
	if (_nvk_selinux_enforcing) {
		_nvk_se_method = 3;
		_nvk_selinux_inited = 1;
		return 0;
	}

	_nvk_selinux_inited = 1;
	return -1;
}

int _nvk_se_write(volatile int *addr, int val)
{
	if (nvk_mem_make_rw((unsigned long)addr) < 0)
		return -2;
	*addr = val;
	__asm__ __volatile__("dsb ish" ::: "memory");
	nvk_mem_make_ro((unsigned long)addr);
	return 0;
}

int nvk_selinux_set_permissive(void)
{
	if (!_nvk_selinux_enforcing) return -1;
	return _nvk_se_write(_nvk_selinux_enforcing, 0);
}

int nvk_selinux_set_enforcing(void)
{
	if (!_nvk_selinux_enforcing) return -1;
	return _nvk_se_write(_nvk_selinux_enforcing, 1);
}

long _nvk_return_zero(void)
{
	return 0;
}

int nvk_selinux_bypass_install(struct nvk_selinux_bypass *state)
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

int nvk_selinux_patch_state(struct nvk_selinux_bypass *state)
{
	if (!_nvk_selinux_enforcing) return -1;

	int cur = __atomic_load_n(_nvk_selinux_enforcing, __ATOMIC_ACQUIRE);
	state->saved_enforce = cur;

	if (cur == 1) {
		_nvk_se_patched_addr =
			(unsigned long)_nvk_selinux_enforcing;
		nvk_mem_write_protected(_nvk_se_patched_addr,
					&(int){0}, 4);
		state->state_patched = 1;
	}
	return 0;
}

void nvk_selinux_restore_state(struct nvk_selinux_bypass *state)
{
	if (!state->state_patched) return;
	if (!_nvk_se_patched_addr) return;

	nvk_mem_write_protected(_nvk_se_patched_addr,
				&state->saved_enforce, 4);
	state->state_patched = 0;
	_nvk_se_patched_addr = 0;
}

int nvk_selinux_full_bypass(struct nvk_selinux_bypass *state)
{
	int ret;

	ret = nvk_selinux_bypass_install(state);

	nvk_selinux_set_permissive();

	nvk_selinux_patch_state(state);

	return ret;
}

void nvk_selinux_bypass_remove(struct nvk_selinux_bypass *state)
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

int _nvk_se_uid_allowed(u32 uid, u32 flag)
{
	int i;
	for (i = 0; i < _nvk_se_sel.count; i++) {
		if (!_nvk_se_sel.uids[i].active) continue;
		if (_nvk_se_sel.uids[i].uid == uid &&
		    (_nvk_se_sel.uids[i].flags & flag))
			return 1;
	}
	return 0;
}

int nvk_se_selective_add(u32 uid, u32 flags)
{
	int i;
	for (i = 0; i < _nvk_se_sel.count; i++) {
		if (_nvk_se_sel.uids[i].uid == uid) {
			_nvk_se_sel.uids[i].flags |= flags;
			WRITE_ONCE(_nvk_se_sel.uids[i].active, 1);
			return 0;
		}
	}
	if (_nvk_se_sel.count >= NVK_SE_UID_MAX)
		return -1;
	int slot = _nvk_se_sel.count++;
	_nvk_se_sel.uids[slot].uid = uid;
	_nvk_se_sel.uids[slot].flags = flags;
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(_nvk_se_sel.uids[slot].active, 1);
	return 0;
}

int nvk_se_selective_remove(u32 uid)
{
	int i;
	for (i = 0; i < _nvk_se_sel.count; i++) {
		if (_nvk_se_sel.uids[i].uid == uid) {
			WRITE_ONCE(_nvk_se_sel.uids[i].active, 0);
			return 0;
		}
	}
	return -1;
}

long _nvk_sel_avc_filter(void)
{
	u32 uid = _nvk_se_current_uid();
	if (_nvk_se_uid_allowed(uid, NVK_SE_FLAG_AVC))
		return 0;
	if (_nvk_sel_orig_avc)
		return _nvk_sel_orig_avc();
	return 0;
}

int _nvk_sel_inode_filter(void *inode, int mask)
{
	u32 uid = _nvk_se_current_uid();
	if (_nvk_se_uid_allowed(uid, NVK_SE_FLAG_INODE))
		return 0;
	if (_nvk_sel_orig_inode)
		return _nvk_sel_orig_inode(inode, mask);
	return 0;
}

long _nvk_sel_capable_filter(void)
{
	u32 uid = _nvk_se_current_uid();
	if (_nvk_se_uid_allowed(uid, NVK_SE_FLAG_CAPABLE))
		return 0;
	if (_nvk_sel_orig_capable)
		return _nvk_sel_orig_capable();
	return 0;
}

int nvk_se_selective_install(void)
{
	void *target;
	int hooked = 0;

	if (_nvk_se_sel.active) return 0;

	target = NVK_LOOKUP("avc_denied");
	if (target) {
		if (nvk_hook_install(&_nvk_se_sel.avc_hook, target,
				     (void *)_nvk_sel_avc_filter,
				     (void **)&_nvk_sel_orig_avc) == 0)
			hooked++;
	}

	target = NVK_LOOKUP("selinux_inode_permission");
	if (target) {
		if (nvk_hook_install(&_nvk_se_sel.inode_hook, target,
				     (void *)_nvk_sel_inode_filter,
				     (void **)&_nvk_sel_orig_inode) == 0)
			hooked++;
	}

	target = NVK_LOOKUP("selinux_capable");
	if (target) {
		if (nvk_hook_install(&_nvk_se_sel.capable_hook, target,
				     (void *)_nvk_sel_capable_filter,
				     (void **)&_nvk_sel_orig_capable) == 0)
			hooked++;
	}

	_nvk_se_sel.active = 1;
	return hooked > 0 ? 0 : -1;
}

void nvk_se_selective_cleanup(void)
{
	if (!_nvk_se_sel.active) return;

	if (_nvk_se_sel.capable_hook.active)
		nvk_hook_remove(&_nvk_se_sel.capable_hook);
	if (_nvk_se_sel.inode_hook.active)
		nvk_hook_remove(&_nvk_se_sel.inode_hook);
	if (_nvk_se_sel.avc_hook.active)
		nvk_hook_remove(&_nvk_se_sel.avc_hook);

	_nvk_se_sel.active = 0;
	_nvk_se_sel.count = 0;
}

