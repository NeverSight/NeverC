/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_ns.c — implementations extracted from nvk_ns.h. */
#include <nvk.h>

int nvk_ns_init(void)
{
	if (_nvk_ns_inited) return 0;

	if (!_nvk_proc_inited)
		nvk_process_init();

	_nvk_task_pid_ns =
		(nvk_task_active_pid_ns_fn)NVK_LOOKUP("task_active_pid_ns");
	_nvk_pid_nr_ns =
		(nvk_pid_nr_ns_fn)NVK_LOOKUP("pid_nr_ns");
	_nvk_task_pid_struct =
		(nvk_task_pid_fn)NVK_LOOKUP("task_pid");
	_nvk_pid_vnr =
		(nvk_pid_vnr_fn)NVK_LOOKUP("pid_vnr");

	_nvk_ns_inited = 1;
	return 0;
}

void *nvk_ns_get_pid_ns(struct task_struct *task)
{
	if (_nvk_task_pid_ns && task)
		return _nvk_task_pid_ns(task);
	return (void *)0;
}

int nvk_ns_same_pidns(struct task_struct *a, struct task_struct *b)
{
	if (!_nvk_task_pid_ns) return -1;
	void *ns_a = _nvk_task_pid_ns(a);
	void *ns_b = _nvk_task_pid_ns(b);
	if (!ns_a || !ns_b) return -1;
	return ns_a == ns_b ? 1 : 0;
}

int nvk_ns_pid_in_ns(struct task_struct *task, void *target_ns)
{
	if (!_nvk_pid_nr_ns || !_nvk_task_pid_struct)
		return -1;
	void *pid = _nvk_task_pid_struct(task);
	if (!pid) return -1;
	return _nvk_pid_nr_ns(pid, target_ns);
}

int nvk_ns_is_init_pid_ns(struct task_struct *task)
{
	void *ns = nvk_ns_get_pid_ns(task);
	if (!ns) return -1;

	void *init_ns = (void *)NVK_LOOKUP("init_pid_ns");
	if (!init_ns) return -1;

	return ns == init_ns ? 1 : 0;
}

int nvk_ns_in_root_ns(void)
{
	return nvk_ns_is_init_pid_ns(current);
}

void *_nvk_get_nsproxy(struct task_struct *task)
{
	if (!task) return (void *)0;

	if (_nvk_off_nsproxy) {
		unsigned long *p = (unsigned long *)
			((unsigned long)task + _nvk_off_nsproxy);
		return (void *)*p;
	}

	const unsigned char *p = (const unsigned char *)task;
	unsigned long i;
	for (i = 0x400; i < 0xC00; i += 8) {
		unsigned long v;
		if (nvk_mem_read(&v, p + i, 8)) continue;
		if (v < 0xFFFF000000000000UL || v == 0) continue;

		unsigned long first, second;
		if (nvk_mem_read(&first, (void *)v, 8)) continue;
		if (first < 1 || first > 1000) continue;
		if (nvk_mem_read(&second, (void *)(v + 8), 8)) continue;
		if (second > 0xFFFF000000000000UL) {
			_nvk_off_nsproxy = i;
			return (void *)v;
		}
	}
	return (void *)0;
}

int nvk_ns_get_info(struct task_struct *task, struct nvk_ns_info *info)
{
	if (!task || !info) return -1;

	unsigned char *p = (unsigned char *)info;
	unsigned long i;
	for (i = 0; i < sizeof(*info); i++) p[i] = 0;

	info->pid_ns = nvk_ns_get_pid_ns(task);
	info->in_root_pidns = nvk_ns_is_init_pid_ns(task);

	void *nsproxy = _nvk_get_nsproxy(task);
	if (nsproxy) {
		unsigned long *np = (unsigned long *)nsproxy;
		if (np[1] > 0xFFFF000000000000UL)
			info->mnt_ns = (void *)np[1];
		if (np[3] > 0xFFFF000000000000UL)
			info->net_ns = (void *)np[3];
	}

	return 0;
}

