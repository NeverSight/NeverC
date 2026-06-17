/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_PROCESS_H
#define NEVERC_KRT_PROCESS_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <nvk_mem.h>

typedef int  (*neverc_krt_task_pid_nr_fn)(struct task_struct *);
typedef int  (*neverc_krt_task_tgid_nr_fn)(struct task_struct *);
typedef struct task_struct *(*neverc_krt_find_task_fn)(int pid);
typedef void *(*neverc_krt_get_task_cred_fn)(struct task_struct *);
typedef void *(*neverc_krt_prepare_creds_fn)(void);
typedef int   (*neverc_krt_commit_creds_fn)(void *);
typedef int   (*neverc_krt_send_sig_info_fn)(int sig, void *info,
				      struct task_struct *p, int type);

NEVERC_KRT_RT_VAR neverc_krt_task_pid_nr_fn   _neverc_krt_task_pid_nr;
NEVERC_KRT_RT_VAR neverc_krt_task_tgid_nr_fn  _neverc_krt_task_tgid_nr;
NEVERC_KRT_RT_VAR neverc_krt_find_task_fn     _neverc_krt_find_task_by_vpid;
NEVERC_KRT_RT_VAR neverc_krt_get_task_cred_fn _neverc_krt_get_task_cred;
NEVERC_KRT_RT_VAR neverc_krt_prepare_creds_fn _neverc_krt_prepare_creds;
NEVERC_KRT_RT_VAR neverc_krt_commit_creds_fn  _neverc_krt_commit_creds;
NEVERC_KRT_RT_VAR int                   _neverc_krt_proc_inited;

typedef void (*neverc_krt_rcu_lock_fn)(void);
typedef void (*neverc_krt_rcu_unlock_fn)(void);
NEVERC_KRT_RT_VAR neverc_krt_rcu_lock_fn   _neverc_krt_rcu_read_lock;
NEVERC_KRT_RT_VAR neverc_krt_rcu_unlock_fn _neverc_krt_rcu_read_unlock;

NEVERC_KRT_RT_VAR unsigned long _neverc_krt_off_comm;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_off_tasks;

struct neverc_krt_task_offsets {
	unsigned long comm;
	unsigned long tasks;
	unsigned long cred;
	unsigned long mm;
	unsigned long pid_field;
	volatile int  resolved;
};

NEVERC_KRT_RT_VAR struct neverc_krt_task_offsets _neverc_krt_toff;

void _neverc_krt_resolve_task_offsets(void);


static __always_inline const struct neverc_krt_task_offsets *neverc_krt_task_offsets(void)
{
	if (!__atomic_load_n(&_neverc_krt_toff.resolved, __ATOMIC_ACQUIRE))
		_neverc_krt_resolve_task_offsets();
	return &_neverc_krt_toff;
}

int neverc_krt_process_init(void);


static __always_inline int neverc_krt_current_pid(void)
{
	if (_neverc_krt_task_pid_nr)
		return _neverc_krt_task_pid_nr(current);
	return -1;
}

static __always_inline int neverc_krt_current_tgid(void)
{
	if (_neverc_krt_task_tgid_nr)
		return _neverc_krt_task_tgid_nr(current);
	return -1;
}

static __always_inline int neverc_krt_task_pid(struct task_struct *task)
{
	if (_neverc_krt_task_pid_nr && task)
		return _neverc_krt_task_pid_nr(task);
	return -1;
}

static __always_inline struct task_struct *neverc_krt_find_task(int pid)
{
	struct task_struct *t;
	if (!_neverc_krt_find_task_by_vpid) return (void *)0;
	if (_neverc_krt_rcu_read_lock) _neverc_krt_rcu_read_lock();
	t = _neverc_krt_find_task_by_vpid(pid);
	if (_neverc_krt_rcu_read_unlock) _neverc_krt_rcu_read_unlock();
	return t;
}

const char *neverc_krt_task_comm(struct task_struct *task);

/*
 * Safe variant: copies up to 15 bytes of task->comm into a caller-
 * supplied buffer via neverc_krt_mem_read.  Always NUL-terminates.
 * Returns 0 on success, -1 on failure (buf filled with "").
 */
int neverc_krt_task_comm_safe(struct task_struct *task, char *buf, int bufsz);

typedef int (*neverc_krt_task_callback_t)(struct task_struct *task, void *data);

int neverc_krt_for_each_task(neverc_krt_task_callback_t callback, void *data);


struct task_struct *neverc_krt_find_task_by_name(const char *name);


static __always_inline void *neverc_krt_task_get_cred(struct task_struct *task)
{
	if (_neverc_krt_get_task_cred && task)
		return _neverc_krt_get_task_cred(task);
	return (void *)0;
}

static __always_inline void *neverc_krt_prepare_creds(void)
{
	if (_neverc_krt_prepare_creds)
		return _neverc_krt_prepare_creds();
	return (void *)0;
}

static __always_inline int neverc_krt_commit_creds(void *cred)
{
	if (_neverc_krt_commit_creds && cred)
		return _neverc_krt_commit_creds(cred);
	return -1;
}

int neverc_krt_send_signal(int pid, int sig);


int neverc_krt_is_current_root(void);


#endif /* NEVERC_KRT_PROCESS_H */
