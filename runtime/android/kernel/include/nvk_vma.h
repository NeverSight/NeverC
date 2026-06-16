/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_VMA_H
#define NEVERC_KRT_VMA_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_addr.h>

typedef void *(*neverc_krt_find_vma_fn)(void *mm, unsigned long addr);
typedef int   (*neverc_krt_access_task_fn)(void *task, unsigned long addr,
				    void *buf, int len, unsigned int gup);
typedef int   (*neverc_krt_access_mm_fn)(void *mm, unsigned long addr,
				  void *buf, int len, unsigned int gup);
typedef long  (*neverc_krt_get_user_pages_fn)(unsigned long start,
				       unsigned long nr_pages,
				       unsigned int gup_flags,
				       void **pages, void **vmas);
typedef void *(*neverc_krt_page_address_fn)(void *page);
typedef void  (*neverc_krt_put_page_fn)(void *page);
typedef void  (*neverc_krt_kunmap_fn)(void *page);
typedef void *(*neverc_krt_kmap_fn)(void *page);
typedef void  (*neverc_krt_mmap_read_lock_fn)(void *mm);
typedef void  (*neverc_krt_mmap_read_unlock_fn)(void *mm);

NEVERC_KRT_RT_VAR neverc_krt_find_vma_fn          _neverc_krt_find_vma;
NEVERC_KRT_RT_VAR neverc_krt_access_task_fn       _neverc_krt_access_task_vm;
NEVERC_KRT_RT_VAR neverc_krt_access_mm_fn         _neverc_krt_access_mm_vm;
NEVERC_KRT_RT_VAR neverc_krt_get_user_pages_fn    _neverc_krt_get_user_pages;
NEVERC_KRT_RT_VAR neverc_krt_page_address_fn      _neverc_krt_page_address;
NEVERC_KRT_RT_VAR neverc_krt_put_page_fn          _neverc_krt_put_page;
NEVERC_KRT_RT_VAR neverc_krt_kmap_fn              _neverc_krt_kmap;
NEVERC_KRT_RT_VAR neverc_krt_kunmap_fn            _neverc_krt_kunmap;
NEVERC_KRT_RT_VAR neverc_krt_mmap_read_lock_fn    _neverc_krt_mmap_read_lock;
NEVERC_KRT_RT_VAR neverc_krt_mmap_read_unlock_fn  _neverc_krt_mmap_read_unlock;
NEVERC_KRT_RT_VAR int                      _neverc_krt_vma_inited;

NEVERC_KRT_RT_VAR unsigned long _neverc_krt_off_mm;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_off_vm_flags;

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


void *_neverc_krt_task_mm(struct task_struct *task);


void _neverc_krt_detect_vm_flags_off(const void *vma);


void _neverc_krt_read_vma_info(const void *vma, struct neverc_krt_vma_info *info);


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


struct _neverc_krt_find_map_ctx {
	unsigned long prot;
	unsigned long min_size;
	struct neverc_krt_vma_info result;
	int found;
};

int _neverc_krt_find_map_cb(const struct neverc_krt_vma_info *vma, void *data);


int neverc_krt_vma_find_exec(struct task_struct *task,
			     unsigned long min_size,
			     struct neverc_krt_vma_info *out);


int neverc_krt_vma_find_writable(struct task_struct *task,
				 unsigned long min_size,
				 struct neverc_krt_vma_info *out);


#endif /* NEVERC_KRT_VMA_H */
