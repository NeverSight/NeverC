/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_PROCESS_H
#define NVK_PROCESS_H

#include <linux/types.h>
#include <nvk_rt.h>
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

NVK_RT_VAR nvk_task_pid_nr_fn   _nvk_task_pid_nr;
NVK_RT_VAR nvk_task_tgid_nr_fn  _nvk_task_tgid_nr;
NVK_RT_VAR nvk_find_task_fn     _nvk_find_task_by_vpid;
NVK_RT_VAR nvk_get_task_cred_fn _nvk_get_task_cred;
NVK_RT_VAR nvk_prepare_creds_fn _nvk_prepare_creds;
NVK_RT_VAR nvk_commit_creds_fn  _nvk_commit_creds;
NVK_RT_VAR nvk_send_sig_info_fn _nvk_send_sig_info;
NVK_RT_VAR struct task_struct   *_nvk_init_task;
NVK_RT_VAR int                   _nvk_proc_inited;

typedef void (*nvk_rcu_lock_fn)(void);
typedef void (*nvk_rcu_unlock_fn)(void);
NVK_RT_VAR nvk_rcu_lock_fn   _nvk_rcu_read_lock;
NVK_RT_VAR nvk_rcu_unlock_fn _nvk_rcu_read_unlock;

NVK_RT_VAR unsigned long _nvk_off_comm;
NVK_RT_VAR unsigned long _nvk_off_tasks;
NVK_RT_VAR unsigned long _nvk_off_real_cred;

struct nvk_task_offsets {
	unsigned long comm;
	unsigned long tasks;
	unsigned long cred;
	unsigned long mm;
	unsigned long pid_field;
	volatile int  resolved;
};

NVK_RT_VAR struct nvk_task_offsets _nvk_toff;

void _nvk_resolve_task_offsets(void);


static __always_inline const struct nvk_task_offsets *nvk_task_offsets(void)
{
	if (!__atomic_load_n(&_nvk_toff.resolved, __ATOMIC_ACQUIRE))
		_nvk_resolve_task_offsets();
	return &_nvk_toff;
}

typedef unsigned long (*nvk_ksize_off_fn)(unsigned long addr,
					  unsigned long *sz,
					  unsigned long *off);

int nvk_process_init(void);


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

const char *nvk_task_comm(struct task_struct *task);

typedef int (*nvk_task_callback_t)(struct task_struct *task, void *data);

int nvk_for_each_task(nvk_task_callback_t callback, void *data);


struct _nvk_find_ctx {
	const char *target;
	struct task_struct *result;
};

int _nvk_find_by_name_cb(struct task_struct *task, void *data);


struct task_struct *nvk_find_task_by_name(const char *name);


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

int nvk_send_signal(int pid, int sig);


int nvk_is_current_root(void);


#endif /* NVK_PROCESS_H */
