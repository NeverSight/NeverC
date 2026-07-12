/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_PROCESS_H
#define NEVERC_KRT_PROCESS_H

#include <linux/types.h>
#include <linux/sched.h>

struct neverc_krt_task_offsets {
	unsigned long comm;
	unsigned long tasks;
	unsigned long usage;
	unsigned long cred;
	unsigned long mm;
	unsigned long pid_field;
	volatile int  resolved;
};

int neverc_krt_process_init(void);

const struct neverc_krt_task_offsets *neverc_krt_task_offsets(void);

int neverc_krt_current_pid(void);

int neverc_krt_current_tgid(void);

int neverc_krt_task_pid(struct task_struct *task);

/* Returns a referenced task. Pair every successful lookup with put_task(). */
struct task_struct *neverc_krt_find_task(int pid);
void neverc_krt_put_task(struct task_struct *task);

/*
 * Returns a raw pointer into task_struct->comm.  Prefer
 * neverc_krt_task_comm_safe() which copies into a caller buffer.
 */
const char *neverc_krt_task_comm(struct task_struct *task);

int neverc_krt_task_comm_safe(struct task_struct *task, char *buf, int bufsz);

typedef int (*neverc_krt_task_callback_t)(struct task_struct *task, void *data);

/*
 * Callback receives task pointers valid only for the duration of the
 * callback.  Do not store or use them after the callback returns.
 */
int neverc_krt_for_each_task(neverc_krt_task_callback_t callback, void *data);

/* Returns a referenced task. Pair every successful lookup with put_task(). */
struct task_struct *neverc_krt_find_task_by_name(const char *name);

int neverc_krt_send_signal(int pid, int sig);

int neverc_krt_is_current_root(void);

#endif /* NEVERC_KRT_PROCESS_H */
