/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_VMA_H
#define NVK_VMA_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_addr.h>

typedef void *(*nvk_find_vma_fn)(void *mm, unsigned long addr);
typedef int   (*nvk_access_task_fn)(void *task, unsigned long addr,
				    void *buf, int len, unsigned int gup);
typedef int   (*nvk_access_mm_fn)(void *mm, unsigned long addr,
				  void *buf, int len, unsigned int gup);
typedef long  (*nvk_get_user_pages_fn)(unsigned long start,
				       unsigned long nr_pages,
				       unsigned int gup_flags,
				       void **pages, void **vmas);
typedef void *(*nvk_page_address_fn)(void *page);
typedef void  (*nvk_put_page_fn)(void *page);
typedef void  (*nvk_kunmap_fn)(void *page);
typedef void *(*nvk_kmap_fn)(void *page);
typedef void  (*nvk_mmap_read_lock_fn)(void *mm);
typedef void  (*nvk_mmap_read_unlock_fn)(void *mm);

NVK_RT_VAR nvk_find_vma_fn          _nvk_find_vma;
NVK_RT_VAR nvk_access_task_fn       _nvk_access_task_vm;
NVK_RT_VAR nvk_access_mm_fn         _nvk_access_mm_vm;
NVK_RT_VAR nvk_get_user_pages_fn    _nvk_get_user_pages;
NVK_RT_VAR nvk_page_address_fn      _nvk_page_address;
NVK_RT_VAR nvk_put_page_fn          _nvk_put_page;
NVK_RT_VAR nvk_kmap_fn              _nvk_kmap;
NVK_RT_VAR nvk_kunmap_fn            _nvk_kunmap;
NVK_RT_VAR nvk_mmap_read_lock_fn    _nvk_mmap_read_lock;
NVK_RT_VAR nvk_mmap_read_unlock_fn  _nvk_mmap_read_unlock;
NVK_RT_VAR int                      _nvk_vma_inited;

NVK_RT_VAR unsigned long _nvk_off_mm;
NVK_RT_VAR unsigned long _nvk_off_vm_flags;

#define NVK_VM_READ    0x01
#define NVK_VM_WRITE   0x02
#define NVK_VM_EXEC    0x04
#define NVK_VM_SHARED  0x08
#define NVK_VM_MAYREAD  0x10
#define NVK_VM_MAYWRITE 0x20
#define NVK_VM_MAYEXEC  0x40

struct nvk_vma_info {
	unsigned long start;
	unsigned long end;
	unsigned long flags;
	unsigned long pgoff;
};

int nvk_vma_init(void);


void *_nvk_task_mm(struct task_struct *task);


void _nvk_detect_vm_flags_off(const void *vma);


void _nvk_read_vma_info(const void *vma, struct nvk_vma_info *info);


int nvk_vma_find(struct task_struct *task, unsigned long addr,
			struct nvk_vma_info *info);


typedef int (*nvk_vma_callback_t)(const struct nvk_vma_info *vma, void *data);

int nvk_vma_walk(struct task_struct *task,
			nvk_vma_callback_t callback, void *data);


long nvk_vma_read_remote(struct task_struct *task,
				unsigned long addr,
				void *buf, size_t len);


long nvk_vma_write_remote(struct task_struct *task,
				 unsigned long addr,
				 const void *buf, size_t len);


struct _nvk_find_map_ctx {
	unsigned long prot;
	unsigned long min_size;
	struct nvk_vma_info result;
	int found;
};

int _nvk_find_map_cb(const struct nvk_vma_info *vma, void *data);


int nvk_vma_find_exec(struct task_struct *task,
			     unsigned long min_size,
			     struct nvk_vma_info *out);


int nvk_vma_find_writable(struct task_struct *task,
				 unsigned long min_size,
				 struct nvk_vma_info *out);


#endif /* NVK_VMA_H */
