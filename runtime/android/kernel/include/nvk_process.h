/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_PROCESS_H
#define NVK_PROCESS_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <nvk_mem.h>

typedef int  (*nvk_task_pid_nr_fn)(struct task_struct *);
typedef int  (*nvk_task_tgid_nr_fn)(struct task_struct *);
typedef struct task_struct *(*nvk_find_task_fn)(int pid);
typedef void *(*nvk_get_task_cred_fn)(struct task_struct *);
typedef void *(*nvk_prepare_creds_fn)(void);
typedef int   (*nvk_commit_creds_fn)(void *);
typedef int   (*nvk_send_sig_info_fn)(int sig, void *info,
				      struct task_struct *p, int type);

static nvk_task_pid_nr_fn   _nvk_task_pid_nr;
static nvk_task_tgid_nr_fn  _nvk_task_tgid_nr;
static nvk_find_task_fn     _nvk_find_task_by_vpid;
static nvk_get_task_cred_fn _nvk_get_task_cred;
static nvk_prepare_creds_fn _nvk_prepare_creds;
static nvk_commit_creds_fn  _nvk_commit_creds;
static nvk_send_sig_info_fn _nvk_send_sig_info;
static struct task_struct   *_nvk_init_task;
static int                   _nvk_proc_inited;

typedef void (*nvk_rcu_lock_fn)(void);
typedef void (*nvk_rcu_unlock_fn)(void);
static nvk_rcu_lock_fn   _nvk_rcu_read_lock;
static nvk_rcu_unlock_fn _nvk_rcu_read_unlock;

static unsigned long _nvk_off_comm;
static unsigned long _nvk_off_tasks;
static unsigned long _nvk_off_real_cred;

typedef unsigned long (*nvk_ksize_off_fn)(unsigned long addr,
					  unsigned long *sz,
					  unsigned long *off);

static int nvk_process_init(void)
{
	if (_nvk_proc_inited) return 0;

	if (!_nvk_mem_inited)
		nvk_mem_init();

	_nvk_task_pid_nr =
		(nvk_task_pid_nr_fn)NVK_LOOKUP("task_pid_nr");
	_nvk_task_tgid_nr =
		(nvk_task_tgid_nr_fn)NVK_LOOKUP("task_tgid_nr");
	_nvk_find_task_by_vpid =
		(nvk_find_task_fn)NVK_LOOKUP("find_task_by_vpid");
	_nvk_get_task_cred =
		(nvk_get_task_cred_fn)NVK_LOOKUP("get_task_cred");
	_nvk_prepare_creds =
		(nvk_prepare_creds_fn)NVK_LOOKUP("prepare_creds");
	_nvk_commit_creds =
		(nvk_commit_creds_fn)NVK_LOOKUP("commit_creds");
	_nvk_send_sig_info =
		(nvk_send_sig_info_fn)NVK_LOOKUP("send_sig_info");
	_nvk_init_task =
		(struct task_struct *)NVK_LOOKUP("init_task");
	_nvk_rcu_read_lock =
		(nvk_rcu_lock_fn)NVK_LOOKUP("rcu_read_lock");
	if (!_nvk_rcu_read_lock)
		_nvk_rcu_read_lock =
			(nvk_rcu_lock_fn)NVK_LOOKUP("__rcu_read_lock");
	_nvk_rcu_read_unlock =
		(nvk_rcu_unlock_fn)NVK_LOOKUP("rcu_read_unlock");
	if (!_nvk_rcu_read_unlock)
		_nvk_rcu_read_unlock =
			(nvk_rcu_unlock_fn)NVK_LOOKUP("__rcu_read_unlock");

	_nvk_proc_inited = 1;
	return 0;
}

static __always_inline int nvk_current_pid(void)
{
	if (_nvk_task_pid_nr)
		return _nvk_task_pid_nr(current);
	return -1;
}

static __always_inline int nvk_current_tgid(void)
{
	if (_nvk_task_tgid_nr)
		return _nvk_task_tgid_nr(current);
	return -1;
}

static __always_inline int nvk_task_pid(struct task_struct *task)
{
	if (_nvk_task_pid_nr && task)
		return _nvk_task_pid_nr(task);
	return -1;
}

static __always_inline struct task_struct *nvk_find_task(int pid)
{
	struct task_struct *t;
	if (!_nvk_find_task_by_vpid) return (void *)0;
	if (_nvk_rcu_read_lock) _nvk_rcu_read_lock();
	t = _nvk_find_task_by_vpid(pid);
	if (_nvk_rcu_read_unlock) _nvk_rcu_read_unlock();
	return t;
}

static const char *nvk_task_comm(struct task_struct *task)
{
	if (!task) return "";

	if (__atomic_load_n(&_nvk_off_comm, __ATOMIC_ACQUIRE) == 0 &&
	    _nvk_init_task) {
		struct task_struct *init = _nvk_init_task;
		const unsigned char *p = (const unsigned char *)init;
		unsigned long i;
		for (i = 0x200; i < 0x1400; i++) {
			if (p[i] == 's' && p[i+1] == 'w' &&
			    p[i+2] == 'a' && p[i+3] == 'p' &&
			    p[i+4] == 'p' && p[i+5] == 'e' &&
			    p[i+6] == 'r' && p[i+7] == '\0') {
				__atomic_store_n(&_nvk_off_comm, i,
						 __ATOMIC_RELEASE);
				break;
			}
		}
	}

	if (_nvk_off_comm)
		return (const char *)((unsigned long)task + _nvk_off_comm);
	return "";
}

typedef int (*nvk_task_callback_t)(struct task_struct *task, void *data);

static int nvk_for_each_task(nvk_task_callback_t callback, void *data)
{
	struct task_struct *init;
	struct task_struct *task;
	struct list_head *pos;
	struct list_head *head;
	int count = 0;

	if (!_nvk_init_task) return -1;
	init = _nvk_init_task;

	if (__atomic_load_n(&_nvk_off_tasks, __ATOMIC_ACQUIRE) == 0) {
		const unsigned char *p = (const unsigned char *)init;
		unsigned long i;
		for (i = 0x200; i < 0xE00; i += 8) {
			unsigned long next = *(unsigned long *)(p + i);
			unsigned long prev = *(unsigned long *)(p + i + 8);
			if (next <= 0xFFFF000000000000UL ||
			    prev <= 0xFFFF000000000000UL)
				continue;
			if (next == (unsigned long)(p + i))
				continue;
			unsigned long peer_prev;
			if (nvk_mem_read(&peer_prev,
					 (void *)(next + 8), 8))
				continue;
			if (peer_prev == (unsigned long)(p + i)) {
				__atomic_store_n(&_nvk_off_tasks, i,
						 __ATOMIC_RELEASE);
				break;
			}
		}
	}

	if (!_nvk_off_tasks) return -1;

	if (_nvk_rcu_read_lock) _nvk_rcu_read_lock();

	head = (struct list_head *)((unsigned long)init + _nvk_off_tasks);
	pos = head->next;
	while (pos && pos != head && count < 32768) {
		struct list_head *next_pos;
		if (nvk_mem_read(&next_pos, &pos->next, sizeof(next_pos)))
			break;
		if ((unsigned long)pos < 0xFFFF000000000000UL)
			break;
		task = (struct task_struct *)
			((unsigned long)pos - _nvk_off_tasks);
		if (callback(task, data))
			break;
		count++;
		pos = next_pos;
	}

	if (_nvk_rcu_read_unlock) _nvk_rcu_read_unlock();

	return count;
}

struct _nvk_find_ctx {
	const char *target;
	struct task_struct *result;
};

static int _nvk_find_by_name_cb(struct task_struct *task, void *data)
{
	struct _nvk_find_ctx *ctx = (struct _nvk_find_ctx *)data;
	const char *comm = nvk_task_comm(task);
	const char *a = comm;
	const char *b = ctx->target;

	while (*a && *b && *a == *b) { a++; b++; }
	if (*a == *b) {
		ctx->result = task;
		return 1;
	}
	return 0;
}

static struct task_struct *nvk_find_task_by_name(const char *name)
{
	struct _nvk_find_ctx ctx = { .target = name, .result = (void *)0 };
	nvk_for_each_task(_nvk_find_by_name_cb, &ctx);
	return ctx.result;
}

static __always_inline void *nvk_task_get_cred(struct task_struct *task)
{
	if (_nvk_get_task_cred && task)
		return _nvk_get_task_cred(task);
	return (void *)0;
}

static __always_inline void *nvk_prepare_creds(void)
{
	if (_nvk_prepare_creds)
		return _nvk_prepare_creds();
	return (void *)0;
}

static __always_inline int nvk_commit_creds(void *cred)
{
	if (_nvk_commit_creds && cred)
		return _nvk_commit_creds(cred);
	return -1;
}

static int nvk_send_signal(int pid, int sig)
{
	struct task_struct *task;

	if (!_nvk_send_sig_info || !_nvk_find_task_by_vpid)
		return -1;

	if (_nvk_rcu_read_lock) _nvk_rcu_read_lock();
	task = _nvk_find_task_by_vpid(pid);
	if (!task) {
		if (_nvk_rcu_read_unlock) _nvk_rcu_read_unlock();
		return -3;
	}

	int ret = _nvk_send_sig_info(sig, (void *)0, task, 0);
	if (_nvk_rcu_read_unlock) _nvk_rcu_read_unlock();
	return ret;
}

static int nvk_is_current_root(void)
{
	if (!_nvk_proc_inited) return -1;

	unsigned char *task = (unsigned char *)current;
	unsigned long i;
	for (i = 0x400; i < 0xE00; i += 8) {
		unsigned long v;
		if (nvk_mem_read(&v, task + i, 8)) continue;
		if (v > 0xFFFF000000000000UL && v < 0xFFFFFFFFFFFFF000UL) {
			u32 cp[4];
			if (nvk_mem_read(cp, (void *)v, sizeof(cp)))
				continue;
			if (cp[0] < 1 || cp[0] > 10000) continue;
			if (cp[1] == 0 && cp[2] == 0 && cp[3] == 0)
				return 1;
		}
	}
	return 0;
}

#endif /* NVK_PROCESS_H */
