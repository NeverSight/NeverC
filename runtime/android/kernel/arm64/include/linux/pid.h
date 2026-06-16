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

struct pid *find_get_pid(pid_t nr);
struct task_struct *pid_task(struct pid *pid, enum pid_type type);
struct task_struct *find_task_by_vpid(pid_t vpid);
pid_t pid_vnr(struct pid *pid);
void put_pid(struct pid *pid);
pid_t task_pid_nr(struct task_struct *tsk);
pid_t task_tgid_nr(struct task_struct *tsk);

#endif /* _NEVERC_KRT_LINUX_PID_H */
