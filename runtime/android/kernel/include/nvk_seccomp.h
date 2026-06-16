/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_SECCOMP_H
#define NVK_SECCOMP_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_hook.h>

#define NVK_SECCOMP_MODE_DISABLED  0
#define NVK_SECCOMP_MODE_STRICT    1
#define NVK_SECCOMP_MODE_FILTER    2

static unsigned long _nvk_off_seccomp;

static int _nvk_seccomp_find_offset(struct task_struct *task)
{
	if (_nvk_off_seccomp) return 0;
	if (!task) return -1;

	const unsigned char *p = (const unsigned char *)task;
	unsigned long i;

	for (i = 0x200; i < 0xE00; i += 4) {
		u32 mode = *(u32 *)(p + i);
		if (mode != NVK_SECCOMP_MODE_DISABLED &&
		    mode != NVK_SECCOMP_MODE_STRICT &&
		    mode != NVK_SECCOMP_MODE_FILTER)
			continue;

		u32 next = *(u32 *)(p + i + 4);
		if (next > 2) continue;

		unsigned long filter_ptr = *(unsigned long *)(p + i + 8);
		if (mode == NVK_SECCOMP_MODE_DISABLED && filter_ptr == 0) {
			_nvk_off_seccomp = i;
			return 0;
		}
		if (mode == NVK_SECCOMP_MODE_FILTER &&
		    filter_ptr > 0xFFFF000000000000UL) {
			_nvk_off_seccomp = i;
			return 0;
		}
	}

	return -1;
}

static int nvk_seccomp_get_mode(struct task_struct *task)
{
	if (!task) return -1;
	if (_nvk_seccomp_find_offset(task))
		return -1;
	return *(int *)((unsigned long)task + _nvk_off_seccomp);
}

static int nvk_seccomp_is_filtered(struct task_struct *task)
{
	int mode = nvk_seccomp_get_mode(task);
	return mode == NVK_SECCOMP_MODE_FILTER;
}

static int nvk_seccomp_disable(struct task_struct *task)
{
	if (!task) return -1;
	if (_nvk_seccomp_find_offset(task))
		return -1;

	unsigned long addr = (unsigned long)task + _nvk_off_seccomp;
	int zero = 0;
	return nvk_mem_write((void *)addr, &zero, 4);
}

static int nvk_seccomp_set_mode(struct task_struct *task, int mode)
{
	if (!task || mode < 0 || mode > 2) return -1;
	if (_nvk_seccomp_find_offset(task))
		return -1;

	unsigned long addr = (unsigned long)task + _nvk_off_seccomp;
	return nvk_mem_write((void *)addr, &mode, 4);
}

typedef int (*nvk_seccomp_check_fn)(int this_syscall, void *sd);

static struct nvk_hook _nvk_seccomp_hook;
static nvk_seccomp_check_fn _nvk_orig_seccomp_check;
static int _nvk_seccomp_hooked;

#define NVK_SECCOMP_ALLOW_MAX 32

static int _nvk_seccomp_allow_pids[NVK_SECCOMP_ALLOW_MAX];
static volatile int _nvk_seccomp_allow_cnt;

static int _nvk_seccomp_is_allowed_pid(int pid)
{
	int i, cnt = __atomic_load_n(&_nvk_seccomp_allow_cnt,
				     __ATOMIC_ACQUIRE);
	for (i = 0; i < cnt; i++) {
		if (_nvk_seccomp_allow_pids[i] == pid)
			return 1;
	}
	return 0;
}

static int _nvk_seccomp_hook_fn(int this_syscall, void *sd)
{
	int pid = -1;
	if (_nvk_task_pid_nr)
		pid = _nvk_task_pid_nr(current);

	if (pid > 0 && _nvk_seccomp_is_allowed_pid(pid))
		return 0;

	if (_nvk_orig_seccomp_check)
		return _nvk_orig_seccomp_check(this_syscall, sd);
	return 0;
}

static int nvk_seccomp_hook_install(void)
{
	void *target;

	if (_nvk_seccomp_hooked) return 0;

	target = NVK_LOOKUP("__secure_computing");
	if (!target)
		target = NVK_LOOKUP("__seccomp_filter");
	if (!target)
		target = NVK_LOOKUP("seccomp_run_filters");
	if (!target) return -1;

	int ret = nvk_hook_install(&_nvk_seccomp_hook, target,
				    (void *)_nvk_seccomp_hook_fn,
				    (void **)&_nvk_orig_seccomp_check);
	if (ret) return ret;

	_nvk_seccomp_hooked = 1;
	return 0;
}

static void nvk_seccomp_hook_remove(void)
{
	if (!_nvk_seccomp_hooked) return;
	nvk_hook_remove(&_nvk_seccomp_hook);
	_nvk_seccomp_hooked = 0;
}

static volatile int _nvk_seccomp_pid_lock;

static int nvk_seccomp_allow_pid(int pid)
{
	while (__atomic_exchange_n(&_nvk_seccomp_pid_lock, 1,
				   __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");

	int cnt = _nvk_seccomp_allow_cnt;
	if (cnt >= NVK_SECCOMP_ALLOW_MAX) {
		__atomic_store_n(&_nvk_seccomp_pid_lock, 0,
				 __ATOMIC_RELEASE);
		return -1;
	}

	int i;
	for (i = 0; i < cnt; i++) {
		if (_nvk_seccomp_allow_pids[i] == pid) {
			__atomic_store_n(&_nvk_seccomp_pid_lock, 0,
					 __ATOMIC_RELEASE);
			return 0;
		}
	}

	_nvk_seccomp_allow_pids[cnt] = pid;
	__asm__ __volatile__("dmb ish" ::: "memory");
	_nvk_seccomp_allow_cnt = cnt + 1;

	__atomic_store_n(&_nvk_seccomp_pid_lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
	return 0;
}

static int nvk_seccomp_disallow_pid(int pid)
{
	while (__atomic_exchange_n(&_nvk_seccomp_pid_lock, 1,
				   __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");

	int i, cnt = _nvk_seccomp_allow_cnt;
	for (i = 0; i < cnt; i++) {
		if (_nvk_seccomp_allow_pids[i] == pid) {
			_nvk_seccomp_allow_pids[i] =
				_nvk_seccomp_allow_pids[cnt - 1];
			__asm__ __volatile__("dmb ish" ::: "memory");
			_nvk_seccomp_allow_cnt = cnt - 1;
			__atomic_store_n(&_nvk_seccomp_pid_lock, 0,
					 __ATOMIC_RELEASE);
			__asm__ __volatile__("sev" ::: "memory");
			return 0;
		}
	}

	__atomic_store_n(&_nvk_seccomp_pid_lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
	return -1;
}

#endif /* NVK_SECCOMP_H */
