/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

/* ---- internal typedefs & variables ---- */

typedef int (*neverc_krt_seccomp_check_fn)(int this_syscall, void *sd);

static struct neverc_krt_interpose     _neverc_krt_seccomp_interpose;
static neverc_krt_seccomp_check_fn _neverc_krt_orig_seccomp_check;
static int                        _neverc_krt_seccomp_interposed;
static int                        _neverc_krt_seccomp_allow_pids[NEVERC_KRT_SECCOMP_ALLOW_MAX];
static volatile int               _neverc_krt_seccomp_allow_cnt;
static volatile int               _neverc_krt_seccomp_pid_lock;

static __always_inline unsigned long
_neverc_krt_seccomp_mode_addr(struct task_struct *task)
{
	const struct neverc_krt_gki_layout *layout =
		_neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);

	return layout ? (unsigned long)task + layout->task_seccomp +
		layout->seccomp_mode : 0;
}

int neverc_krt_seccomp_get_mode(struct task_struct *task)
{
	unsigned long mode_addr;

	if (!task) return -1;
	mode_addr = _neverc_krt_seccomp_mode_addr(task);
	if (!mode_addr)
		return -1;
	int mode;
	if (neverc_krt_mem_read(&mode,
			(void *)mode_addr, 4))
		return -1;
	return mode;
}

int neverc_krt_seccomp_is_filtered(struct task_struct *task)
{
	int mode = neverc_krt_seccomp_get_mode(task);
	return mode == NEVERC_KRT_SECCOMP_MODE_FILTER;
}

int neverc_krt_seccomp_disable(struct task_struct *task)
{
	unsigned long mode_addr;

	if (!task) return -1;
	mode_addr = _neverc_krt_seccomp_mode_addr(task);
	if (!mode_addr)
		return -1;

	int zero = 0;
	return neverc_krt_mem_write((void *)mode_addr, &zero, 4);
}

int neverc_krt_seccomp_set_mode(struct task_struct *task, int mode)
{
	unsigned long mode_addr;

	if (!task || mode < 0 || mode > 2) return -1;
	mode_addr = _neverc_krt_seccomp_mode_addr(task);
	if (!mode_addr)
		return -1;

	return neverc_krt_mem_write((void *)mode_addr, &mode, 4);
}

static int _neverc_krt_seccomp_is_allowed_pid(int pid)
{
	int i, cnt = __atomic_load_n(&_neverc_krt_seccomp_allow_cnt,
				     __ATOMIC_ACQUIRE);
	for (i = 0; i < cnt; i++) {
		if (_neverc_krt_seccomp_allow_pids[i] == pid)
			return 1;
	}
	return 0;
}

static int _neverc_krt_seccomp_interpose_fn(int this_syscall, void *sd)
{
	int pid = neverc_krt_current_pid();

	if (pid > 0 && _neverc_krt_seccomp_is_allowed_pid(pid))
		return 0;

	if (_neverc_krt_orig_seccomp_check)
		return _neverc_krt_orig_seccomp_check(this_syscall, sd);
	return 0;
}

int neverc_krt_seccomp_interpose_install(void)
{
	void *target;

	if (_neverc_krt_seccomp_interposed) return 0;

	target = NEVERC_KRT_LOOKUP("__secure_computing");
	if (!target)
		target = NEVERC_KRT_LOOKUP("__seccomp_filter");
	if (!target)
		target = NEVERC_KRT_LOOKUP("seccomp_run_filters");
	if (!target) return -1;

	int ret = neverc_krt_interpose_install(&_neverc_krt_seccomp_interpose, target,
				    (void *)_neverc_krt_seccomp_interpose_fn,
				    (void **)&_neverc_krt_orig_seccomp_check);
	if (ret) return ret;

	_neverc_krt_seccomp_interposed = 1;
	return 0;
}

void neverc_krt_seccomp_interpose_remove(void)
{
	if (!_neverc_krt_seccomp_interposed) return;
	neverc_krt_interpose_remove(&_neverc_krt_seccomp_interpose);
	_neverc_krt_seccomp_interposed = 0;
}

int neverc_krt_seccomp_allow_pid(int pid)
{
	while (__atomic_exchange_n(&_neverc_krt_seccomp_pid_lock, 1,
				   __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");

	int cnt = _neverc_krt_seccomp_allow_cnt;
	if (cnt >= NEVERC_KRT_SECCOMP_ALLOW_MAX) {
		__atomic_store_n(&_neverc_krt_seccomp_pid_lock, 0,
				 __ATOMIC_RELEASE);
		return -1;
	}

	int i;
	for (i = 0; i < cnt; i++) {
		if (_neverc_krt_seccomp_allow_pids[i] == pid) {
			__atomic_store_n(&_neverc_krt_seccomp_pid_lock, 0,
					 __ATOMIC_RELEASE);
			return 0;
		}
	}

	_neverc_krt_seccomp_allow_pids[cnt] = pid;
	__atomic_store_n(&_neverc_krt_seccomp_allow_cnt, cnt + 1,
			 __ATOMIC_RELEASE);

	__atomic_store_n(&_neverc_krt_seccomp_pid_lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
	return 0;
}

int neverc_krt_seccomp_disallow_pid(int pid)
{
	while (__atomic_exchange_n(&_neverc_krt_seccomp_pid_lock, 1,
				   __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");

	int i, cnt = _neverc_krt_seccomp_allow_cnt;
	for (i = 0; i < cnt; i++) {
		if (_neverc_krt_seccomp_allow_pids[i] == pid) {
			_neverc_krt_seccomp_allow_pids[i] =
				_neverc_krt_seccomp_allow_pids[cnt - 1];
			__atomic_store_n(&_neverc_krt_seccomp_allow_cnt,
					 cnt - 1, __ATOMIC_RELEASE);
			__atomic_store_n(&_neverc_krt_seccomp_pid_lock, 0,
					 __ATOMIC_RELEASE);
			__asm__ __volatile__("sev" ::: "memory");
			return 0;
		}
	}

	__atomic_store_n(&_neverc_krt_seccomp_pid_lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
	return -1;
}
