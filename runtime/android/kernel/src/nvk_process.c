/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_process.c — implementations extracted from nvk_process.h. */
#include <nvk.h>

void _nvk_resolve_task_offsets(void)
{
	if (__atomic_load_n(&_nvk_toff.resolved, __ATOMIC_ACQUIRE))
		return;

	if (_nvk_off_comm)
		_nvk_toff.comm = _nvk_off_comm;
	if (_nvk_off_tasks)
		_nvk_toff.tasks = _nvk_off_tasks;

	__atomic_store_n(&_nvk_toff.resolved, 1, __ATOMIC_RELEASE);
}

int nvk_process_init(void)
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

int nvk_for_each_task(nvk_task_callback_t callback, void *data)
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

int _nvk_find_by_name_cb(struct task_struct *task, void *data)
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

struct task_struct *nvk_find_task_by_name(const char *name)
{
	struct _nvk_find_ctx ctx = { .target = name, .result = (void *)0 };
	nvk_for_each_task(_nvk_find_by_name_cb, &ctx);
	return ctx.result;
}

int nvk_send_signal(int pid, int sig)
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

int nvk_is_current_root(void)
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

const char *nvk_task_comm(struct task_struct *task)
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

