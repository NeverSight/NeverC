/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SCHED_H
#define _NEVERC_KRT_LINUX_SCHED_H

#include <linux/types.h>
#include <linux/compiler.h>

struct task_struct; /* opaque */
struct mm_struct;   /* opaque */
struct pid;         /* opaque */

/* The 618/KMI6 alternative changed the current-task system register. */
struct task_struct *neverc_krt_current_task(void);

static __always_inline struct task_struct *get_current(void)
{
	return neverc_krt_current_task();
}
#define current get_current()

/* Common scheduler entry points (resolve dynamically or import). */
void msleep(unsigned int msecs);
long schedule_timeout(long timeout);
int wake_up_process(struct task_struct *tsk);

#endif /* _NEVERC_KRT_LINUX_SCHED_H */
