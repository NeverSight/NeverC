/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

typedef void *(*neverc_krt_task_active_pid_ns_fn)(struct task_struct *);
typedef int   (*neverc_krt_pid_nr_ns_fn)(void *pid, void *ns);
typedef void *(*neverc_krt_task_pid_fn)(struct task_struct *);

static neverc_krt_task_active_pid_ns_fn _neverc_krt_task_pid_ns;
static neverc_krt_pid_nr_ns_fn          _neverc_krt_pid_nr_ns;
static neverc_krt_task_pid_fn           _neverc_krt_task_pid_struct;
static int                              _neverc_krt_ns_inited;

int neverc_krt_ns_init(void)
{
	if (_neverc_krt_ns_inited) return 0;

	(void)neverc_krt_process_init();

	_neverc_krt_task_pid_ns =
		(neverc_krt_task_active_pid_ns_fn)NEVERC_KRT_LOOKUP("task_active_pid_ns");
	_neverc_krt_pid_nr_ns =
		(neverc_krt_pid_nr_ns_fn)NEVERC_KRT_LOOKUP("pid_nr_ns");
	_neverc_krt_task_pid_struct =
		(neverc_krt_task_pid_fn)NEVERC_KRT_LOOKUP("task_pid");

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
	if (!_neverc_krt_task_pid_ns || !a || !b) return -1;
	void *ns_a = _neverc_krt_task_pid_ns(a);
	void *ns_b = _neverc_krt_task_pid_ns(b);
	if (!ns_a || !ns_b) return -1;
	return ns_a == ns_b ? 1 : 0;
}

static void *_neverc_krt_task_pid_ptr(struct task_struct *task)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long pid;

	if (!task)
		return (void *)0;
	if (_neverc_krt_task_pid_struct)
		return _neverc_krt_task_pid_struct(task);

	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_THREADS);
	if (!layout)
		return (void *)0;
	if (neverc_krt_mem_read(&pid,
			(const char *)task + layout->task_thread_pid,
			sizeof(pid)))
		return (void *)0;
	if (pid < 0xFFFF000000000000UL ||
	    pid >= 0xFFFFFFFFFFFFF000UL)
		return (void *)0;
	return (void *)pid;
}

int neverc_krt_ns_pid_in_ns(struct task_struct *task, void *target_ns)
{
	if (!_neverc_krt_pid_nr_ns || !task || !target_ns) return -1;
	void *pid = _neverc_krt_task_pid_ptr(task);
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

static void *_neverc_krt_get_nsproxy(struct task_struct *task)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long nsproxy;

	if (!task) return (void *)0;

	layout = _neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	if (!layout)
		return (void *)0;
	if (neverc_krt_mem_read(&nsproxy,
			(const char *)task + layout->task_nsproxy,
			sizeof(nsproxy)))
		return (void *)0;
	if (nsproxy < 0xFFFF000000000000UL ||
	    nsproxy >= 0xFFFFFFFFFFFFF000UL)
		return (void *)0;
	return (void *)nsproxy;
}

int neverc_krt_ns_get_info(struct task_struct *task, struct neverc_krt_ns_info *info)
{
	const struct neverc_krt_gki_layout *layout;

	if (!task || !info) return -1;

	unsigned char *p = (unsigned char *)info;
	unsigned long i;
	for (i = 0; i < sizeof(*info); i++) p[i] = 0;

	info->pid_ns = neverc_krt_ns_get_pid_ns(task);
	info->in_root_pidns = neverc_krt_ns_is_init_pid_ns(task);

	void *nsproxy = _neverc_krt_get_nsproxy(task);
	if (nsproxy) {
		unsigned long mnt_val, net_val;
		layout = _neverc_krt_get_proven_gki_layout(
			NEVERC_KRT_LAYOUT_CERT_FULL);
		if (!layout)
			return -1;
		if (!neverc_krt_mem_read(&mnt_val,
				(const char *)nsproxy + layout->nsproxy_mnt_ns,
				sizeof(mnt_val)) &&
		    mnt_val > 0xFFFF000000000000UL)
			info->mnt_ns = (void *)mnt_val;
		if (!neverc_krt_mem_read(&net_val,
				(const char *)nsproxy + layout->nsproxy_net_ns,
				sizeof(net_val)) &&
		    net_val > 0xFFFF000000000000UL)
			info->net_ns = (void *)net_val;
	}

	return 0;
}
