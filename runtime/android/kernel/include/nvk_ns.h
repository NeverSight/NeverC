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

int neverc_krt_ns_init(void);


void *neverc_krt_ns_get_pid_ns(struct task_struct *task);


int neverc_krt_ns_same_pidns(struct task_struct *a, struct task_struct *b);


int neverc_krt_ns_pid_in_ns(struct task_struct *task, void *target_ns);


int neverc_krt_ns_is_init_pid_ns(struct task_struct *task);


int neverc_krt_ns_in_root_ns(void);

struct neverc_krt_ns_info {
	void *pid_ns;
	void *mnt_ns;
	void *net_ns;
	int   pid_depth;
	int   in_root_pidns;
};

int neverc_krt_ns_get_info(struct task_struct *task, struct neverc_krt_ns_info *info);


#endif /* NEVERC_KRT_NS_H */
