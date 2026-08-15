/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SCHED_H
#define _NEVERC_KRT_LINUX_SCHED_H

#include <linux/types.h>
#include <linux/compiler.h>

struct task_struct; /* opaque */
struct mm_struct;   /* opaque */
struct pid;         /* opaque */

static __always_inline struct task_struct *get_current(void)
{
	uintptr_t sp_el0;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(sp_el0));
	return (struct task_struct *)sp_el0;
}
#define current get_current()

/* Common scheduler entry points (resolve dynamically or import). */
void msleep(unsigned int msecs);
long schedule_timeout(long timeout);
int wake_up_process(struct task_struct *tsk);

#endif /* _NEVERC_KRT_LINUX_SCHED_H */
