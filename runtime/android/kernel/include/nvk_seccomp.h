/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_SECCOMP_H
#define NEVERC_KRT_SECCOMP_H

#include <linux/types.h>
#include <linux/sched.h>

#define NEVERC_KRT_SECCOMP_MODE_DISABLED  0
#define NEVERC_KRT_SECCOMP_MODE_STRICT    1
#define NEVERC_KRT_SECCOMP_MODE_FILTER    2

#define NEVERC_KRT_SECCOMP_ALLOW_MAX 32

int neverc_krt_seccomp_get_mode(struct task_struct *task);
int neverc_krt_seccomp_is_filtered(struct task_struct *task);
int neverc_krt_seccomp_disable(struct task_struct *task);
int neverc_krt_seccomp_set_mode(struct task_struct *task, int mode);
int neverc_krt_seccomp_hook_install(void);
void neverc_krt_seccomp_hook_remove(void);
int neverc_krt_seccomp_allow_pid(int pid);
int neverc_krt_seccomp_disallow_pid(int pid);

#endif /* NEVERC_KRT_SECCOMP_H */
