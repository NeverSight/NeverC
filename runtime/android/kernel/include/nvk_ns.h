/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_NS_H
#define NVK_NS_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>

typedef void *(*nvk_task_active_pid_ns_fn)(struct task_struct *);
typedef int   (*nvk_pid_nr_ns_fn)(void *pid, void *ns);
typedef void *(*nvk_task_pid_fn)(struct task_struct *);
typedef int   (*nvk_pid_vnr_fn)(void *pid);
typedef void *(*nvk_get_nsproxy_fn)(struct task_struct *);

NVK_RT_VAR nvk_task_active_pid_ns_fn _nvk_task_pid_ns;
NVK_RT_VAR nvk_pid_nr_ns_fn         _nvk_pid_nr_ns;
NVK_RT_VAR nvk_task_pid_fn          _nvk_task_pid_struct;
NVK_RT_VAR nvk_pid_vnr_fn           _nvk_pid_vnr;
NVK_RT_VAR int                      _nvk_ns_inited;

NVK_RT_VAR unsigned long _nvk_off_nsproxy;

int nvk_ns_init(void);


void *nvk_ns_get_pid_ns(struct task_struct *task);


int nvk_ns_same_pidns(struct task_struct *a, struct task_struct *b);


int nvk_ns_pid_in_ns(struct task_struct *task, void *target_ns);


int nvk_ns_is_init_pid_ns(struct task_struct *task);


int nvk_ns_in_root_ns(void);


void *_nvk_get_nsproxy(struct task_struct *task);


struct nvk_ns_info {
	void *pid_ns;
	void *mnt_ns;
	void *net_ns;
	int   pid_depth;
	int   in_root_pidns;
};

int nvk_ns_get_info(struct task_struct *task, struct nvk_ns_info *info);


#endif /* NVK_NS_H */
