/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_process.c — implementations extracted from neverc_krt_process.h. */
#include <nvk.h>

void _neverc_krt_resolve_task_offsets(void)
{
	if (__atomic_load_n(&_neverc_krt_toff.resolved, __ATOMIC_ACQUIRE))
		return;

	if (_neverc_krt_off_comm)
		_neverc_krt_toff.comm = _neverc_krt_off_comm;
	if (_neverc_krt_off_tasks)
		_neverc_krt_toff.tasks = _neverc_krt_off_tasks;

	__atomic_store_n(&_neverc_krt_toff.resolved, 1, __ATOMIC_RELEASE);
}

int neverc_krt_process_init(void)
{
	if (_neverc_krt_proc_inited) return 0;

	if (!_neverc_krt_mem_inited)
		_neverc_krt_mem_init();

	_neverc_krt_task_pid_nr =
		(neverc_krt_task_pid_nr_fn)NEVERC_KRT_LOOKUP("task_pid_nr");
	_neverc_krt_task_tgid_nr =
		(neverc_krt_task_tgid_nr_fn)NEVERC_KRT_LOOKUP("task_tgid_nr");
	_neverc_krt_find_task_by_vpid =
		(neverc_krt_find_task_fn)NEVERC_KRT_LOOKUP("find_task_by_vpid");
	_neverc_krt_get_task_cred =
		(neverc_krt_get_task_cred_fn)NEVERC_KRT_LOOKUP("get_task_cred");
	_neverc_krt_prepare_creds =
		(neverc_krt_prepare_creds_fn)NEVERC_KRT_LOOKUP("prepare_creds");
	_neverc_krt_commit_creds =
		(neverc_krt_commit_creds_fn)NEVERC_KRT_LOOKUP("commit_creds");
	_neverc_krt_send_sig_info =
		(neverc_krt_send_sig_info_fn)NEVERC_KRT_LOOKUP("send_sig_info");
	_neverc_krt_init_task =
		(struct task_struct *)NEVERC_KRT_LOOKUP("init_task");
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

	_neverc_krt_proc_inited = 1;
	return 0;
}

int neverc_krt_for_each_task(neverc_krt_task_callback_t callback, void *data)
{
	struct task_struct *init;
	struct task_struct *task;
	struct list_head *pos;
	struct list_head *head;
	int count = 0;

	if (!_neverc_krt_init_task) return -1;
	init = _neverc_krt_init_task;

	if (__atomic_load_n(&_neverc_krt_off_tasks, __ATOMIC_ACQUIRE) == 0) {
		const unsigned char *p = (const unsigned char *)init;
		unsigned long i;
		for (i = 0x200; i < 0xE00; i += 8) {
			unsigned long next, prev;
			if (neverc_krt_mem_read(&next, p + i, 8))
				continue;
			if (neverc_krt_mem_read(&prev, p + i + 8, 8))
				continue;
			if (next <= 0xFFFF000000000000UL ||
			    prev <= 0xFFFF000000000000UL)
				continue;
			if (next == (unsigned long)(p + i))
				continue;
			unsigned long peer_prev;
			if (neverc_krt_mem_read(&peer_prev,
					 (void *)(next + 8), 8))
				continue;
			if (peer_prev == (unsigned long)(p + i)) {
				__atomic_store_n(&_neverc_krt_off_tasks, i,
						 __ATOMIC_RELEASE);
				break;
			}
		}
	}

	if (!_neverc_krt_off_tasks) return -1;

	if (_neverc_krt_rcu_read_lock) _neverc_krt_rcu_read_lock();

	head = (struct list_head *)((unsigned long)init + _neverc_krt_off_tasks);
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
			((unsigned long)pos - _neverc_krt_off_tasks);
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
	if (_neverc_krt_init_task)
		neverc_krt_task_comm(_neverc_krt_init_task);
	neverc_krt_for_each_task(_neverc_krt_find_by_name_cb, &ctx);
	return ctx.result;
}

int neverc_krt_send_signal(int pid, int sig)
{
	struct task_struct *task;

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

/*
 * Returns a pointer directly into task_struct->comm (embedded char[16]).
 * Caller must ensure task remains valid (e.g. hold rcu_read_lock, or
 * pass `current`).  The returned pointer is safe to dereference as long
 * as the task is not freed.
 */
const char *neverc_krt_task_comm(struct task_struct *task)
{
	if (!task) return "";

	if (__atomic_load_n(&_neverc_krt_off_comm, __ATOMIC_ACQUIRE) == 0 &&
	    _neverc_krt_init_task) {
		struct task_struct *init = _neverc_krt_init_task;
		const unsigned char *p = (const unsigned char *)init;
		unsigned long i;
		for (i = 0x200; i + 8 <= 0x1400; i++) {
			unsigned char sw[8];
			if (neverc_krt_mem_read(sw, p + i, 8))
				continue;
			if (sw[0] == 's' && sw[1] == 'w' &&
			    sw[2] == 'a' && sw[3] == 'p' &&
			    sw[4] == 'p' && sw[5] == 'e' &&
			    sw[6] == 'r' && sw[7] == '\0') {
				__atomic_store_n(&_neverc_krt_off_comm, i,
						 __ATOMIC_RELEASE);
				break;
			}
		}
	}

	if (_neverc_krt_off_comm)
		return (const char *)((unsigned long)task + _neverc_krt_off_comm);
	return "";
}

