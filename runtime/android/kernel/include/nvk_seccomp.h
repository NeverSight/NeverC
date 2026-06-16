/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_SECCOMP_H
#define NVK_SECCOMP_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_hook.h>

#define NVK_SECCOMP_MODE_DISABLED  0
#define NVK_SECCOMP_MODE_STRICT    1
#define NVK_SECCOMP_MODE_FILTER    2

NVK_RT_VAR unsigned long _nvk_off_seccomp;

int _nvk_seccomp_find_offset(struct task_struct *task);


int nvk_seccomp_get_mode(struct task_struct *task);


int nvk_seccomp_is_filtered(struct task_struct *task);


int nvk_seccomp_disable(struct task_struct *task);


int nvk_seccomp_set_mode(struct task_struct *task, int mode);


typedef int (*nvk_seccomp_check_fn)(int this_syscall, void *sd);

NVK_RT_VAR struct nvk_hook _nvk_seccomp_hook;
NVK_RT_VAR nvk_seccomp_check_fn _nvk_orig_seccomp_check;
NVK_RT_VAR int _nvk_seccomp_hooked;

#define NVK_SECCOMP_ALLOW_MAX 32

NVK_RT_VAR int _nvk_seccomp_allow_pids[NVK_SECCOMP_ALLOW_MAX];
NVK_RT_VAR volatile int _nvk_seccomp_allow_cnt;

int _nvk_seccomp_is_allowed_pid(int pid);


int _nvk_seccomp_hook_fn(int this_syscall, void *sd);


int nvk_seccomp_hook_install(void);


void nvk_seccomp_hook_remove(void);


NVK_RT_VAR volatile int _nvk_seccomp_pid_lock;

int nvk_seccomp_allow_pid(int pid);


int nvk_seccomp_disallow_pid(int pid);


#endif /* NVK_SECCOMP_H */
