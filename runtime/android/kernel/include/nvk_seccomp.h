/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_SECCOMP_H
#define NEVERC_KRT_SECCOMP_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_hook.h>

#define NEVERC_KRT_SECCOMP_MODE_DISABLED  0
#define NEVERC_KRT_SECCOMP_MODE_STRICT    1
#define NEVERC_KRT_SECCOMP_MODE_FILTER    2

NEVERC_KRT_RT_VAR unsigned long _neverc_krt_off_seccomp;



int neverc_krt_seccomp_get_mode(struct task_struct *task);


int neverc_krt_seccomp_is_filtered(struct task_struct *task);


int neverc_krt_seccomp_disable(struct task_struct *task);


int neverc_krt_seccomp_set_mode(struct task_struct *task, int mode);


typedef int (*neverc_krt_seccomp_check_fn)(int this_syscall, void *sd);

NEVERC_KRT_RT_VAR struct neverc_krt_hook _neverc_krt_seccomp_hook;
NEVERC_KRT_RT_VAR neverc_krt_seccomp_check_fn _neverc_krt_orig_seccomp_check;
NEVERC_KRT_RT_VAR int _neverc_krt_seccomp_hooked;

#define NEVERC_KRT_SECCOMP_ALLOW_MAX 32

NEVERC_KRT_RT_VAR int _neverc_krt_seccomp_allow_pids[NEVERC_KRT_SECCOMP_ALLOW_MAX];
NEVERC_KRT_RT_VAR volatile int _neverc_krt_seccomp_allow_cnt;



int neverc_krt_seccomp_hook_install(void);


void neverc_krt_seccomp_hook_remove(void);


NEVERC_KRT_RT_VAR volatile int _neverc_krt_seccomp_pid_lock;

int neverc_krt_seccomp_allow_pid(int pid);


int neverc_krt_seccomp_disallow_pid(int pid);


#endif /* NEVERC_KRT_SECCOMP_H */
