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
static struct task_struct  **_nvk_init_task_ptr;
static int                   _nvk_proc_inited;

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
	_nvk_init_task_ptr =
		(struct task_struct **)NVK_LOOKUP("init_task");

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
	if (_nvk_find_task_by_vpid)
		return _nvk_find_task_by_vpid(pid);
	return (void *)0;
}

static const char *nvk_task_comm(struct task_struct *task)
{
	if (!task) return "";

	if (_nvk_off_comm == 0 && _nvk_init_task_ptr) {
		struct task_struct *init = (struct task_struct *)_nvk_init_task_ptr;
		const unsigned char *p = (const unsigned char *)init;
		unsigned long i;
		for (i = 0x400; i < 0x1000; i++) {
			if (p[i] == 's' && p[i+1] == 'w' &&
			    p[i+2] == 'a' && p[i+3] == 'p' &&
			    p[i+4] == 'p' && p[i+5] == 'e' &&
			    p[i+6] == 'r' && p[i+7] == '\0') {
				_nvk_off_comm = i;
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

	if (!_nvk_init_task_ptr) return -1;
	init = (struct task_struct *)_nvk_init_task_ptr;

	if (_nvk_off_tasks == 0) {
		const unsigned char *p = (const unsigned char *)init;
		unsigned long i;
		for (i = 0x300; i < 0xA00; i += 8) {
			unsigned long v1 = *(unsigned long *)(p + i);
			unsigned long v2 = *(unsigned long *)(p + i + 8);
			if (v1 > 0xFFFF000000000000UL &&
			    v2 > 0xFFFF000000000000UL &&
			    v1 != v2 &&
			    v1 != (unsigned long)(p + i)) {
				_nvk_off_tasks = i;
				break;
			}
		}
	}

	if (!_nvk_off_tasks) return -1;

	head = (struct list_head *)((unsigned long)init + _nvk_off_tasks);
	for (pos = head->next; pos && pos != head; pos = pos->next) {
		task = (struct task_struct *)
			((unsigned long)pos - _nvk_off_tasks);
		if (callback(task, data))
			break;
		count++;
		if (count > 32768) break;
	}
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

	task = _nvk_find_task_by_vpid(pid);
	if (!task) return -3;

	return _nvk_send_sig_info(sig, (void *)0, task, 0);
}

#endif /* NVK_PROCESS_H */
