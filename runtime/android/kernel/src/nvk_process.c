/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include <linux/refcount.h>
#include "nvk_internal.h"

/* ---- file-local typedefs ---- */

typedef int  (*neverc_krt_task_pid_nr_fn)(struct task_struct *);
typedef int  (*neverc_krt_task_tgid_nr_fn)(struct task_struct *);
typedef int  (*neverc_krt_task_pid_nr_ns_fn)(struct task_struct *, int type,
					     void *ns);
typedef struct pid *(*neverc_krt_find_get_pid_fn)(int pid);
typedef struct task_struct *(*neverc_krt_get_pid_task_fn)(struct pid *pid,
							  int type);
typedef void (*neverc_krt_put_pid_fn)(struct pid *pid);
typedef void (*neverc_krt_put_task_struct_fn)(struct task_struct *task);
typedef int   (*neverc_krt_send_sig_info_fn)(int sig, void *info,
					     struct task_struct *p, int type);
typedef void (*neverc_krt_rcu_lock_fn)(void);
typedef void (*neverc_krt_rcu_unlock_fn)(void);

struct _neverc_krt_task_offsets {
	unsigned long tasks;
	unsigned long usage;
	volatile int resolved;
};

/* ---- file-local state ---- */

static neverc_krt_task_pid_nr_fn    _neverc_krt_task_pid_nr;
static neverc_krt_task_tgid_nr_fn   _neverc_krt_task_tgid_nr;
static neverc_krt_task_pid_nr_ns_fn _neverc_krt_task_pid_nr_ns;
static neverc_krt_find_get_pid_fn   _neverc_krt_find_get_pid;
static neverc_krt_get_pid_task_fn   _neverc_krt_get_pid_task;
static neverc_krt_put_pid_fn        _neverc_krt_put_pid;
static neverc_krt_put_task_struct_fn _neverc_krt_put_task_struct;
static neverc_krt_send_sig_info_fn  _neverc_krt_send_sig_info;
static struct task_struct          *_neverc_krt_init_task;
static int                          _neverc_krt_proc_inited;
static struct _neverc_krt_task_offsets _neverc_krt_toff;
static neverc_krt_rcu_lock_fn       _neverc_krt_rcu_read_lock;
static neverc_krt_rcu_unlock_fn     _neverc_krt_rcu_read_unlock;

/* Shared with nvk_anti.c through the private runtime interface. */
unsigned long _neverc_krt_off_comm = 0;

/* ---- implementation ---- */

static const struct _neverc_krt_task_offsets *_neverc_krt_task_offsets(void);

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
	if (_neverc_krt_find_get_pid && _neverc_krt_get_pid_task &&
	    _neverc_krt_put_pid && _neverc_krt_put_task_struct)
		return;
	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_find_get_pid =
		(neverc_krt_find_get_pid_fn)NEVERC_KRT_LOOKUP("find_get_pid");
	_neverc_krt_get_pid_task =
		(neverc_krt_get_pid_task_fn)NEVERC_KRT_LOOKUP("get_pid_task");
	_neverc_krt_put_pid =
		(neverc_krt_put_pid_fn)NEVERC_KRT_LOOKUP("put_pid");
	_neverc_krt_put_task_struct =
		(neverc_krt_put_task_struct_fn)NEVERC_KRT_LOOKUP(
			"__put_task_struct");
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
	_neverc_krt_toff.tasks = layout->task_tasks;
	_neverc_krt_toff.usage = layout->task_usage;
	__atomic_store_n(&_neverc_krt_off_comm, layout->task_comm,
			 __ATOMIC_RELEASE);

	__atomic_store_n(&_neverc_krt_toff.resolved, 1, __ATOMIC_RELEASE);
}

static int _neverc_krt_task_get(struct task_struct *task)
{
	unsigned long usage_off;
	refcount_t *usage;

	if (!task)
		return -1;
	usage_off = _neverc_krt_task_offsets()->usage;
	if (!usage_off)
		return -1;
	usage = (refcount_t *)((unsigned long)task + usage_off);
	return refcount_inc_not_zero(usage) ? 0 : -1;
}

static const struct _neverc_krt_task_offsets *_neverc_krt_task_offsets(void)
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
	struct pid *pid_ref;
	struct task_struct *t;

	_neverc_krt_ensure_task_lookup();
	if (!_neverc_krt_find_get_pid || !_neverc_krt_get_pid_task ||
	    !_neverc_krt_put_pid)
		return (void *)0;

	pid_ref = _neverc_krt_find_get_pid(pid);
	if (!pid_ref)
		return (void *)0;
	t = _neverc_krt_get_pid_task(pid_ref, 0 /* PIDTYPE_PID */);
	_neverc_krt_put_pid(pid_ref);
	return t;
}

void neverc_krt_put_task(struct task_struct *task)
{
	unsigned long usage_off;
	refcount_t *usage;

	if (!task)
		return;
	_neverc_krt_ensure_task_lookup();
	if (!_neverc_krt_put_task_struct)
		return;
	usage_off = _neverc_krt_task_offsets()->usage;
	if (!usage_off)
		return;

	usage = (refcount_t *)((unsigned long)task + usage_off);
	if (refcount_dec_and_test(usage))
		_neverc_krt_put_task_struct(task);
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
	tasks_off = _neverc_krt_task_offsets()->tasks;
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
		if (!_neverc_krt_task_get(task)) {
			ctx->result = task;
			return 1;
		}
	}
	return 0;
}

struct task_struct *neverc_krt_find_task_by_name(const char *name)
{
	struct _neverc_krt_find_ctx ctx = { .target = name, .result = (void *)0 };
	_neverc_krt_ensure_init_task();
	neverc_krt_for_each_task(_neverc_krt_find_by_name_cb, &ctx);
	return ctx.result;
}

int neverc_krt_send_signal(int pid, int sig)
{
	struct task_struct *task;
	int ret;

	_neverc_krt_ensure_send_sig();
	if (!_neverc_krt_send_sig_info)
		return -1;

	task = neverc_krt_find_task(pid);
	if (!task)
		return -3;
	ret = _neverc_krt_send_sig_info(sig, (void *)0, task, 0);
	neverc_krt_put_task(task);
	return ret;
}

int neverc_krt_is_current_root(void)
{
	struct neverc_krt_cred_ids ids;

	if (!_neverc_krt_proc_inited) return -1;
	if (neverc_krt_cred_get_ids(current, &ids))
		return -1;
	return ids.uid == 0 ? 1 : 0;
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
