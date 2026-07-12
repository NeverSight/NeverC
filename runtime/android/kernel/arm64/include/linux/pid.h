/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_PID_H
#define _NEVERC_KRT_LINUX_PID_H

#include <linux/types.h>

struct pid;         /* opaque */
struct task_struct; /* opaque */

enum pid_type {
	PIDTYPE_PID,
	PIDTYPE_TGID,
	PIDTYPE_PGID,
	PIDTYPE_SID,
	PIDTYPE_MAX,
};

/*
 * find_get_pid / pid_task are intentionally omitted: using them
 * directly would depend on struct pid internals and group_leader
 * offsets that vary across GKI versions.  Use the runtime's
 * neverc_krt_find_task (NEVERC_KRT_LOOKUP-based) instead.
 */
struct task_struct *find_task_by_vpid(pid_t vpid);
#ifdef NEVERC_KRT_NON_KMI_API
pid_t pid_vnr(struct pid *pid);
void put_pid(struct pid *pid);
pid_t task_pid_nr(struct task_struct *tsk);
pid_t task_tgid_nr(struct task_struct *tsk);
#endif

#endif /* _NEVERC_KRT_LINUX_PID_H */
