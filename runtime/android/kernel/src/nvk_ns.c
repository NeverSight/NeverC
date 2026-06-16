/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_ns.c — implementations extracted from neverc_krt_ns.h. */
#include <nvk.h>

int neverc_krt_ns_init(void)
{
	if (_neverc_krt_ns_inited) return 0;

	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_task_pid_ns =
		(neverc_krt_task_active_pid_ns_fn)NEVERC_KRT_LOOKUP("task_active_pid_ns");
	_neverc_krt_pid_nr_ns =
		(neverc_krt_pid_nr_ns_fn)NEVERC_KRT_LOOKUP("pid_nr_ns");
	_neverc_krt_task_pid_struct =
		(neverc_krt_task_pid_fn)NEVERC_KRT_LOOKUP("task_pid");
	_neverc_krt_pid_vnr =
		(neverc_krt_pid_vnr_fn)NEVERC_KRT_LOOKUP("pid_vnr");

	_neverc_krt_ns_inited = 1;
	return 0;
}

void *neverc_krt_ns_get_pid_ns(struct task_struct *task)
{
	if (_neverc_krt_task_pid_ns && task)
		return _neverc_krt_task_pid_ns(task);
	return (void *)0;
}

int neverc_krt_ns_same_pidns(struct task_struct *a, struct task_struct *b)
{
	if (!_neverc_krt_task_pid_ns) return -1;
	void *ns_a = _neverc_krt_task_pid_ns(a);
	void *ns_b = _neverc_krt_task_pid_ns(b);
	if (!ns_a || !ns_b) return -1;
	return ns_a == ns_b ? 1 : 0;
}

int neverc_krt_ns_pid_in_ns(struct task_struct *task, void *target_ns)
{
	if (!_neverc_krt_pid_nr_ns || !_neverc_krt_task_pid_struct)
		return -1;
	void *pid = _neverc_krt_task_pid_struct(task);
	if (!pid) return -1;
	return _neverc_krt_pid_nr_ns(pid, target_ns);
}

int neverc_krt_ns_is_init_pid_ns(struct task_struct *task)
{
	void *ns = neverc_krt_ns_get_pid_ns(task);
	if (!ns) return -1;

	void *init_ns = (void *)NEVERC_KRT_LOOKUP("init_pid_ns");
	if (!init_ns) return -1;

	return ns == init_ns ? 1 : 0;
}

int neverc_krt_ns_in_root_ns(void)
{
	return neverc_krt_ns_is_init_pid_ns(current);
}

void *_neverc_krt_get_nsproxy(struct task_struct *task)
{
	if (!task) return (void *)0;

	if (_neverc_krt_off_nsproxy) {
		unsigned long *p = (unsigned long *)
			((unsigned long)task + _neverc_krt_off_nsproxy);
		return (void *)*p;
	}

	const unsigned char *p = (const unsigned char *)task;
	unsigned long i;
	for (i = 0x400; i < 0xC00; i += 8) {
		unsigned long v;
		if (neverc_krt_mem_read(&v, p + i, 8)) continue;
		if (v < 0xFFFF000000000000UL || v == 0) continue;

		unsigned long first, second;
		if (neverc_krt_mem_read(&first, (void *)v, 8)) continue;
		if (first < 1 || first > 1000) continue;
		if (neverc_krt_mem_read(&second, (void *)(v + 8), 8)) continue;
		if (second > 0xFFFF000000000000UL) {
			_neverc_krt_off_nsproxy = i;
			return (void *)v;
		}
	}
	return (void *)0;
}

int neverc_krt_ns_get_info(struct task_struct *task, struct neverc_krt_ns_info *info)
{
	if (!task || !info) return -1;

	unsigned char *p = (unsigned char *)info;
	unsigned long i;
	for (i = 0; i < sizeof(*info); i++) p[i] = 0;

	info->pid_ns = neverc_krt_ns_get_pid_ns(task);
	info->in_root_pidns = neverc_krt_ns_is_init_pid_ns(task);

	void *nsproxy = _neverc_krt_get_nsproxy(task);
	if (nsproxy) {
		unsigned long *np = (unsigned long *)nsproxy;
		if (np[1] > 0xFFFF000000000000UL)
			info->mnt_ns = (void *)np[1];
		if (np[3] > 0xFFFF000000000000UL)
			info->net_ns = (void *)np[3];
	}

	return 0;
}

