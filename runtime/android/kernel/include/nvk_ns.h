/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_NS_H
#define NEVERC_KRT_NS_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>

typedef void *(*neverc_krt_task_active_pid_ns_fn)(struct task_struct *);
typedef int   (*neverc_krt_pid_nr_ns_fn)(void *pid, void *ns);
typedef void *(*neverc_krt_task_pid_fn)(struct task_struct *);
typedef int   (*neverc_krt_pid_vnr_fn)(void *pid);
typedef void *(*neverc_krt_get_nsproxy_fn)(struct task_struct *);

NEVERC_KRT_RT_VAR neverc_krt_task_active_pid_ns_fn _neverc_krt_task_pid_ns;
NEVERC_KRT_RT_VAR neverc_krt_pid_nr_ns_fn         _neverc_krt_pid_nr_ns;
NEVERC_KRT_RT_VAR neverc_krt_task_pid_fn          _neverc_krt_task_pid_struct;
NEVERC_KRT_RT_VAR neverc_krt_pid_vnr_fn           _neverc_krt_pid_vnr;
NEVERC_KRT_RT_VAR int                      _neverc_krt_ns_inited;

NEVERC_KRT_RT_VAR unsigned long _neverc_krt_off_nsproxy;

int neverc_krt_ns_init(void);


void *neverc_krt_ns_get_pid_ns(struct task_struct *task);


int neverc_krt_ns_same_pidns(struct task_struct *a, struct task_struct *b);


int neverc_krt_ns_pid_in_ns(struct task_struct *task, void *target_ns);


int neverc_krt_ns_is_init_pid_ns(struct task_struct *task);


int neverc_krt_ns_in_root_ns(void);


void *_neverc_krt_get_nsproxy(struct task_struct *task);


struct neverc_krt_ns_info {
	void *pid_ns;
	void *mnt_ns;
	void *net_ns;
	int   pid_depth;
	int   in_root_pidns;
};

int neverc_krt_ns_get_info(struct task_struct *task, struct neverc_krt_ns_info *info);


#endif /* NEVERC_KRT_NS_H */
