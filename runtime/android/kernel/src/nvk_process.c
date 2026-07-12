/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

/* ---- file-local typedefs ---- */

typedef int  (*neverc_krt_task_pid_nr_fn)(struct task_struct *);
typedef int  (*neverc_krt_task_tgid_nr_fn)(struct task_struct *);
typedef int  (*neverc_krt_task_pid_nr_ns_fn)(struct task_struct *, int type,
					     void *ns);
typedef struct task_struct *(*neverc_krt_find_task_fn)(int pid);
typedef int   (*neverc_krt_send_sig_info_fn)(int sig, void *info,
					     struct task_struct *p, int type);
typedef void (*neverc_krt_rcu_lock_fn)(void);
typedef void (*neverc_krt_rcu_unlock_fn)(void);
typedef void *(*neverc_krt_get_task_cred_fn)(struct task_struct *);

/* ---- file-local state ---- */

static neverc_krt_task_pid_nr_fn    _neverc_krt_task_pid_nr;
static neverc_krt_task_tgid_nr_fn   _neverc_krt_task_tgid_nr;
static neverc_krt_task_pid_nr_ns_fn _neverc_krt_task_pid_nr_ns;
static neverc_krt_find_task_fn      _neverc_krt_find_task_by_vpid;
static neverc_krt_send_sig_info_fn  _neverc_krt_send_sig_info;
static struct task_struct          *_neverc_krt_init_task;
static int                          _neverc_krt_proc_inited;
static struct neverc_krt_task_offsets _neverc_krt_toff;
static neverc_krt_rcu_lock_fn       _neverc_krt_rcu_read_lock;
static neverc_krt_rcu_unlock_fn     _neverc_krt_rcu_read_unlock;
static neverc_krt_get_task_cred_fn  _neverc_krt_get_task_cred;

/*
 * Cross-file variables — referenced by nvk_cred.c and nvk_anti.c.
 * Non-static so they are visible across the unity TU.
 */
unsigned long               _neverc_krt_off_comm = 0;
neverc_krt_prepare_creds_fn _neverc_krt_prepare_creds = (void *)0;
neverc_krt_commit_creds_fn  _neverc_krt_commit_creds = (void *)0;

/* ---- implementation ---- */

static void _neverc_krt_ensure_pid_ops(void)
{
	if (_neverc_krt_task_pid_nr || _neverc_krt_task_pid_nr_ns)
		return;
	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_task_pid_nr =
		(neverc_krt_task_pid_nr_fn)NEVERC_KRT_LOOKUP("task_pid_nr");
	_neverc_krt_task_tgid_nr =
		(neverc_krt_task_tgid_nr_fn)NEVERC_KRT_LOOKUP("task_tgid_nr");
	if (!_neverc_krt_task_pid_nr || !_neverc_krt_task_tgid_nr)
		_neverc_krt_task_pid_nr_ns =
			(neverc_krt_task_pid_nr_ns_fn)NEVERC_KRT_LOOKUP(
				"__task_pid_nr_ns");
}

static void _neverc_krt_ensure_task_lookup(void)
{
	if (_neverc_krt_find_task_by_vpid)
		return;
	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_find_task_by_vpid =
		(neverc_krt_find_task_fn)NEVERC_KRT_LOOKUP("find_task_by_vpid");
	_neverc_krt_rcu_read_lock =
		(neverc_krt_rcu_lock_fn)NEVERC_KRT_LOOKUP("rcu_read_lock");
	if (!_neverc_krt_rcu_read_lock)
		_neverc_krt_rcu_read_lock =
			(neverc_krt_rcu_lock_fn)NEVERC_KRT_LOOKUP("__rcu_read_lock");
	_neverc_krt_rcu_read_unlock =
		(neverc_krt_rcu_unlock_fn)NEVERC_KRT_LOOKUP("rcu_read_unlock");
	if (!_neverc_krt_rcu_read_unlock)
		_neverc_krt_rcu_read_unlock =
			(neverc_krt_rcu_unlock_fn)NEVERC_KRT_LOOKUP("__rcu_read_unlock");
}

static void _neverc_krt_ensure_init_task(void)
{
	if (_neverc_krt_init_task)
		return;
	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_ensure_task_lookup();
	_neverc_krt_init_task =
		(struct task_struct *)NEVERC_KRT_LOOKUP("init_task");
}

static void _neverc_krt_ensure_get_task_cred(void)
{
	if (_neverc_krt_get_task_cred)
		return;
	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_get_task_cred =
		(neverc_krt_get_task_cred_fn)NEVERC_KRT_LOOKUP("get_task_cred");
}

static void _neverc_krt_ensure_send_sig(void)
{
	if (_neverc_krt_send_sig_info)
		return;
	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_ensure_task_lookup();
	_neverc_krt_send_sig_info =
		(neverc_krt_send_sig_info_fn)NEVERC_KRT_LOOKUP("send_sig_info");
}

static void _neverc_krt_resolve_task_offsets(void)
{
	const struct neverc_krt_gki_layout *layout;

	if (__atomic_load_n(&_neverc_krt_toff.resolved, __ATOMIC_ACQUIRE))
		return;

	layout = _neverc_krt_get_gki_layout();
	_neverc_krt_toff.comm = layout->task_comm;
	_neverc_krt_toff.tasks = layout->task_tasks;
	_neverc_krt_toff.cred = layout->task_cred;
	_neverc_krt_toff.mm = layout->task_mm;
	_neverc_krt_toff.pid_field = layout->task_pid;
	__atomic_store_n(&_neverc_krt_off_comm, layout->task_comm,
			 __ATOMIC_RELEASE);

	__atomic_store_n(&_neverc_krt_toff.resolved, 1, __ATOMIC_RELEASE);
}

const struct neverc_krt_task_offsets *neverc_krt_task_offsets(void)
{
	if (!__atomic_load_n(&_neverc_krt_toff.resolved, __ATOMIC_ACQUIRE))
		_neverc_krt_resolve_task_offsets();
	return &_neverc_krt_toff;
}

int neverc_krt_process_init(void)
{
	if (_neverc_krt_proc_inited) return 0;

	neverc_krt_mem_init();
	_neverc_krt_resolve_task_offsets();

	_neverc_krt_prepare_creds =
		(neverc_krt_prepare_creds_fn)NEVERC_KRT_LOOKUP("prepare_creds");
	_neverc_krt_commit_creds =
		(neverc_krt_commit_creds_fn)NEVERC_KRT_LOOKUP("commit_creds");

	_neverc_krt_proc_inited = 1;
	return 0;
}

int neverc_krt_current_pid(void)
{
	_neverc_krt_ensure_pid_ops();
	if (_neverc_krt_task_pid_nr)
		return _neverc_krt_task_pid_nr(current);
	if (_neverc_krt_task_pid_nr_ns)
		return _neverc_krt_task_pid_nr_ns(current, 0 /* PIDTYPE_PID */,
						  (void *)0);
	return -1;
}

int neverc_krt_current_tgid(void)
{
	_neverc_krt_ensure_pid_ops();
	if (_neverc_krt_task_tgid_nr)
		return _neverc_krt_task_tgid_nr(current);
	if (_neverc_krt_task_pid_nr_ns)
		return _neverc_krt_task_pid_nr_ns(current, 1 /* PIDTYPE_TGID */,
						  (void *)0);
	return -1;
}

int neverc_krt_task_pid(struct task_struct *task)
{
	_neverc_krt_ensure_pid_ops();
	if (_neverc_krt_task_pid_nr && task)
		return _neverc_krt_task_pid_nr(task);
	if (_neverc_krt_task_pid_nr_ns && task)
		return _neverc_krt_task_pid_nr_ns(task, 0 /* PIDTYPE_PID */,
						  (void *)0);
	return -1;
}

struct task_struct *neverc_krt_find_task(int pid)
{
	struct task_struct *t;

	_neverc_krt_ensure_task_lookup();
	if (!_neverc_krt_find_task_by_vpid) return (void *)0;
	if (_neverc_krt_rcu_read_lock) _neverc_krt_rcu_read_lock();
	t = _neverc_krt_find_task_by_vpid(pid);
	if (_neverc_krt_rcu_read_unlock) _neverc_krt_rcu_read_unlock();
	return t;
}

void *neverc_krt_task_get_cred(struct task_struct *task)
{
	_neverc_krt_ensure_get_task_cred();
	if (_neverc_krt_get_task_cred && task)
		return _neverc_krt_get_task_cred(task);
	return (void *)0;
}

void *neverc_krt_prepare_creds(void)
{
	if (_neverc_krt_prepare_creds)
		return _neverc_krt_prepare_creds();
	return (void *)0;
}

int neverc_krt_commit_creds(void *cred)
{
	if (_neverc_krt_commit_creds && cred)
		return _neverc_krt_commit_creds(cred);
	return -1;
}

int neverc_krt_for_each_task(neverc_krt_task_callback_t callback, void *data)
{
	struct task_struct *init;
	struct task_struct *task;
	struct list_head *pos;
	struct list_head *head;
	unsigned long tasks_off;
	int count = 0;

	_neverc_krt_ensure_init_task();
	if (!_neverc_krt_init_task) return -1;
	init = _neverc_krt_init_task;
	tasks_off = neverc_krt_task_offsets()->tasks;
	if (!tasks_off) return -1;

	if (_neverc_krt_rcu_read_lock) _neverc_krt_rcu_read_lock();

	head = (struct list_head *)((unsigned long)init + tasks_off);
	if (neverc_krt_mem_read(&pos, &head->next, sizeof(pos))) {
		if (_neverc_krt_rcu_read_unlock) _neverc_krt_rcu_read_unlock();
		return -1;
	}
	while (pos && pos != head && count < 32768) {
		struct list_head *next_pos;
		if (neverc_krt_mem_read(&next_pos, &pos->next, sizeof(next_pos)))
			break;
		if ((unsigned long)pos < 0xFFFF000000000000UL)
			break;
		task = (struct task_struct *)
			((unsigned long)pos - tasks_off);
		if (callback(task, data))
			break;
		count++;
		pos = next_pos;
	}

	if (_neverc_krt_rcu_read_unlock) _neverc_krt_rcu_read_unlock();

	return count;
}

struct _neverc_krt_find_ctx {
	const char *target;
	struct task_struct *result;
};

static int _neverc_krt_find_by_name_cb(struct task_struct *task, void *data)
{
	struct _neverc_krt_find_ctx *ctx = (struct _neverc_krt_find_ctx *)data;
	unsigned long off = __atomic_load_n(&_neverc_krt_off_comm,
					    __ATOMIC_ACQUIRE);
	if (!off)
		return 0;

	char buf[16];
	if (neverc_krt_mem_read(buf, (const char *)task + off, 16))
		return 0;
	buf[15] = '\0';

	const char *a = buf;
	const char *b = ctx->target;
	while (*a && *b && *a == *b) { a++; b++; }
	if (*a == *b) {
		ctx->result = task;
		return 1;
	}
	return 0;
}

struct task_struct *neverc_krt_find_task_by_name(const char *name)
{
	struct _neverc_krt_find_ctx ctx = { .target = name, .result = (void *)0 };
	_neverc_krt_ensure_init_task();
	if (_neverc_krt_init_task)
		neverc_krt_task_comm(_neverc_krt_init_task);
	neverc_krt_for_each_task(_neverc_krt_find_by_name_cb, &ctx);
	return ctx.result;
}

int neverc_krt_send_signal(int pid, int sig)
{
	struct task_struct *task;

	_neverc_krt_ensure_send_sig();
	if (!_neverc_krt_send_sig_info || !_neverc_krt_find_task_by_vpid)
		return -1;

	if (_neverc_krt_rcu_read_lock) _neverc_krt_rcu_read_lock();
	task = _neverc_krt_find_task_by_vpid(pid);
	if (!task) {
		if (_neverc_krt_rcu_read_unlock) _neverc_krt_rcu_read_unlock();
		return -3;
	}

	int ret = _neverc_krt_send_sig_info(sig, (void *)0, task, 0);
	if (_neverc_krt_rcu_read_unlock) _neverc_krt_rcu_read_unlock();
	return ret;
}

int neverc_krt_is_current_root(void)
{
	if (!_neverc_krt_proc_inited) return -1;

	unsigned char *task = (unsigned char *)current;

	if (_neverc_krt_off_cred) {
		unsigned long cv;
		if (neverc_krt_mem_read(&cv, task + _neverc_krt_off_cred, 8))
			return -1;
		if (cv < 0xFFFF000000000000UL || cv >= 0xFFFFFFFFFFFFF000UL)
			return -1;
		unsigned long base = _neverc_krt_off_uid ? _neverc_krt_off_uid
					  : _neverc_krt_cred_uid_base();
		u32 uid;
		if (neverc_krt_mem_read(&uid, (void *)(cv + base), 4))
			return -1;
		return uid == 0 ? 1 : 0;
	}

	unsigned long uid_off = _neverc_krt_cred_uid_base();
	unsigned long i;
	for (i = 0x400; i < 0xE00; i += 8) {
		unsigned long v;
		if (neverc_krt_mem_read(&v, task + i, 8)) continue;
		if (v > 0xFFFF000000000000UL && v < 0xFFFFFFFFFFFFF000UL) {
			u32 refcnt;
			if (neverc_krt_mem_read(&refcnt, (void *)v, 4))
				continue;
			if (refcnt < 1 || refcnt > 10000) continue;
			u32 ids[3];
			if (neverc_krt_mem_read(ids,
					(void *)(v + uid_off), sizeof(ids)))
				continue;
			if (ids[0] == 0 && ids[1] == 0 && ids[2] == 0)
				return 1;
		}
	}
	return 0;
}

int neverc_krt_task_comm_safe(struct task_struct *task, char *buf, int bufsz)
{
	if (!buf || bufsz < 1) return -1;
	buf[0] = '\0';
	if (!task) return -1;

	if (!__atomic_load_n(&_neverc_krt_off_comm, __ATOMIC_ACQUIRE))
		neverc_krt_task_comm(task);

	unsigned long off = __atomic_load_n(&_neverc_krt_off_comm,
					    __ATOMIC_ACQUIRE);
	if (!off) return -1;

	int n = bufsz < 16 ? bufsz - 1 : 15;
	if (neverc_krt_mem_read(buf, (const char *)task + off, n)) {
		buf[0] = '\0';
		return -1;
	}
	buf[n] = '\0';
	return 0;
}

const char *neverc_krt_task_comm(struct task_struct *task)
{
	unsigned long off;

	if (!task) return "";

	off = __atomic_load_n(&_neverc_krt_off_comm, __ATOMIC_ACQUIRE);
	if (!off) {
		_neverc_krt_resolve_task_offsets();
		off = __atomic_load_n(&_neverc_krt_off_comm, __ATOMIC_ACQUIRE);
	}
	if (off)
		return (const char *)((unsigned long)task + off);
	return "";
}
