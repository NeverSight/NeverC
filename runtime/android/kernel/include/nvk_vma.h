/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_VMA_H
#define NEVERC_KRT_VMA_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/sched.h>

#define NEVERC_KRT_VM_READ    0x01
#define NEVERC_KRT_VM_WRITE   0x02
#define NEVERC_KRT_VM_EXEC    0x04
#define NEVERC_KRT_VM_SHARED  0x08
#define NEVERC_KRT_VM_MAYREAD  0x10
#define NEVERC_KRT_VM_MAYWRITE 0x20
#define NEVERC_KRT_VM_MAYEXEC  0x40

struct neverc_krt_vma_info {
	unsigned long start;
	unsigned long end;
	unsigned long flags;
	unsigned long pgoff;
};

int neverc_krt_vma_init(void);

int neverc_krt_vma_find(struct task_struct *task, unsigned long addr,
			struct neverc_krt_vma_info *info);


typedef int (*neverc_krt_vma_callback_t)(const struct neverc_krt_vma_info *vma, void *data);

int neverc_krt_vma_walk(struct task_struct *task,
			neverc_krt_vma_callback_t callback, void *data);


long neverc_krt_vma_read_remote(struct task_struct *task,
				unsigned long addr,
				void *buf, size_t len);


long neverc_krt_vma_write_remote(struct task_struct *task,
				 unsigned long addr,
				 const void *buf, size_t len);


int neverc_krt_vma_find_exec(struct task_struct *task,
			     unsigned long min_size,
			     struct neverc_krt_vma_info *out);


int neverc_krt_vma_find_writable(struct task_struct *task,
				 unsigned long min_size,
				 struct neverc_krt_vma_info *out);


#endif /* NEVERC_KRT_VMA_H */
