/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_VMA_H
#define NVK_VMA_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_addr.h>

typedef void *(*nvk_find_vma_fn)(void *mm, unsigned long addr);
typedef int   (*nvk_access_remote_fn)(void *mm, unsigned long addr,
				      void *buf, int len, unsigned int gup);
typedef long  (*nvk_get_user_pages_fn)(unsigned long start,
				       unsigned long nr_pages,
				       unsigned int gup_flags,
				       void **pages, void **vmas);
typedef void *(*nvk_page_address_fn)(void *page);
typedef void  (*nvk_put_page_fn)(void *page);
typedef void  (*nvk_kunmap_fn)(void *page);
typedef void *(*nvk_kmap_fn)(void *page);
typedef int   (*nvk_mmap_write_lock_fn)(void *mm);
typedef void  (*nvk_mmap_write_unlock_fn)(void *mm);

static nvk_find_vma_fn          _nvk_find_vma;
static nvk_access_remote_fn     _nvk_access_remote;
static nvk_get_user_pages_fn    _nvk_get_user_pages;
static nvk_page_address_fn      _nvk_page_address;
static nvk_put_page_fn          _nvk_put_page;
static nvk_kmap_fn              _nvk_kmap;
static nvk_kunmap_fn            _nvk_kunmap;
static nvk_mmap_write_lock_fn   _nvk_mmap_write_lock;
static nvk_mmap_write_unlock_fn _nvk_mmap_write_unlock;
static int                      _nvk_vma_inited;

static unsigned long _nvk_off_mm;

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

static int nvk_vma_init(void)
{
	if (_nvk_vma_inited) return 0;

	if (!_nvk_mem_inited)
		nvk_mem_init();
	if (!_nvk_proc_inited)
		nvk_process_init();

	_nvk_find_vma =
		(nvk_find_vma_fn)NVK_LOOKUP("find_vma");
	_nvk_access_remote =
		(nvk_access_remote_fn)NVK_LOOKUP("access_process_vm");
	_nvk_get_user_pages =
		(nvk_get_user_pages_fn)NVK_LOOKUP("get_user_pages_remote");
	_nvk_page_address =
		(nvk_page_address_fn)NVK_LOOKUP("page_address");
	_nvk_put_page =
		(nvk_put_page_fn)NVK_LOOKUP("put_page");
	_nvk_kmap =
		(nvk_kmap_fn)NVK_LOOKUP("kmap");
	_nvk_kunmap =
		(nvk_kunmap_fn)NVK_LOOKUP("kunmap");
	_nvk_mmap_write_lock =
		(nvk_mmap_write_lock_fn)NVK_LOOKUP("mmap_write_lock");
	if (!_nvk_mmap_write_lock)
		_nvk_mmap_write_lock =
			(nvk_mmap_write_lock_fn)NVK_LOOKUP("down_write");
	_nvk_mmap_write_unlock =
		(nvk_mmap_write_unlock_fn)NVK_LOOKUP("mmap_write_unlock");
	if (!_nvk_mmap_write_unlock)
		_nvk_mmap_write_unlock =
			(nvk_mmap_write_unlock_fn)NVK_LOOKUP("up_write");

	_nvk_vma_inited = 1;
	return _nvk_find_vma ? 0 : -1;
}

static void *_nvk_task_mm(struct task_struct *task)
{
	if (!task) return (void *)0;

	if (_nvk_off_mm == 0) {
		const unsigned char *p = (const unsigned char *)task;
		unsigned long i;
		for (i = 0x300; i < 0x600; i += 8) {
			unsigned long v = *(unsigned long *)(p + i);
			if (v < 0xFFFF000000000000UL || v == 0)
				continue;
			unsigned long pgd = *(unsigned long *)v;
			if (pgd > 0xFFFF000000000000UL) {
				unsigned long mmap_ptr =
					*(unsigned long *)(v + 8);
				if (mmap_ptr > 0xFFFF000000000000UL ||
				    mmap_ptr == 0) {
					_nvk_off_mm = i;
					break;
				}
			}
		}
	}

	if (!_nvk_off_mm) return (void *)0;
	return *(void **)((unsigned long)task + _nvk_off_mm);
}

static int nvk_vma_find(struct task_struct *task, unsigned long addr,
			struct nvk_vma_info *info)
{
	void *mm, *vma;

	if (!_nvk_find_vma || !info)
		return -1;

	mm = _nvk_task_mm(task);
	if (!mm) return -2;

	vma = _nvk_find_vma(mm, addr);
	if (!vma) return -3;

	const unsigned long *v = (const unsigned long *)vma;
	info->start = v[0];
	info->end   = v[1];
	info->flags = v[2] & 0xFFFF;
	info->pgoff = v[3];

	if (addr < info->start)
		return -3;

	return 0;
}

typedef int (*nvk_vma_callback_t)(const struct nvk_vma_info *vma, void *data);

static int nvk_vma_walk(struct task_struct *task,
			nvk_vma_callback_t callback, void *data)
{
	void *mm;
	int count = 0;

	if (!_nvk_find_vma || !callback)
		return -1;

	mm = _nvk_task_mm(task);
	if (!mm) return -2;

	unsigned long addr = 0;
	for (;;) {
		void *vma = _nvk_find_vma(mm, addr);
		if (!vma) break;

		const unsigned long *v = (const unsigned long *)vma;
		struct nvk_vma_info info;
		info.start = v[0];
		info.end   = v[1];
		info.flags = v[2] & 0xFFFF;
		info.pgoff = v[3];

		if (info.start == 0 && info.end == 0)
			break;
		if (info.end <= addr)
			break;

		if (callback(&info, data))
			break;

		count++;
		addr = info.end;
		if (count > 65536) break;
	}
	return count;
}

static long nvk_vma_read_remote(struct task_struct *task,
				unsigned long addr,
				void *buf, size_t len)
{
	if (!_nvk_access_remote) {
		void *mm = _nvk_task_mm(task);
		if (!mm) return -1;

		unsigned long pa = nvk_translate_user(addr);
		if (!pa) return -2;

		unsigned long va = nvk_phys_to_virt(pa);
		if (!va) return -3;

		return nvk_mem_read(buf, (void *)va, len);
	}

	void *mm = _nvk_task_mm(task);
	if (!mm) return -1;

	return _nvk_access_remote(mm, addr, buf, (int)len,
				  0 /* FOLL_FORCE read */);
}

static long nvk_vma_write_remote(struct task_struct *task,
				 unsigned long addr,
				 const void *buf, size_t len)
{
	if (!_nvk_access_remote) {
		void *mm = _nvk_task_mm(task);
		if (!mm) return -1;

		unsigned long pa = nvk_translate_user(addr);
		if (!pa) return -2;

		unsigned long va = nvk_phys_to_virt(pa);
		if (!va) return -3;

		return nvk_mem_write((void *)va, buf, len);
	}

	void *mm = _nvk_task_mm(task);
	if (!mm) return -1;

	return _nvk_access_remote(mm, addr, (void *)buf, (int)len,
				  1 /* FOLL_FORCE | FOLL_WRITE */);
}

struct _nvk_find_map_ctx {
	unsigned long prot;
	unsigned long min_size;
	struct nvk_vma_info result;
	int found;
};

static int _nvk_find_map_cb(const struct nvk_vma_info *vma, void *data)
{
	struct _nvk_find_map_ctx *ctx = (struct _nvk_find_map_ctx *)data;
	unsigned long sz = vma->end - vma->start;

	if (sz < ctx->min_size)
		return 0;
	if ((vma->flags & ctx->prot) != ctx->prot)
		return 0;

	ctx->result = *vma;
	ctx->found = 1;
	return 1;
}

static int nvk_vma_find_exec(struct task_struct *task,
			     unsigned long min_size,
			     struct nvk_vma_info *out)
{
	struct _nvk_find_map_ctx ctx;
	ctx.prot = NVK_VM_READ | NVK_VM_EXEC;
	ctx.min_size = min_size;
	ctx.found = 0;

	int ret = nvk_vma_walk(task, _nvk_find_map_cb, &ctx);
	if (ret < 0) return ret;
	if (!ctx.found) return -1;
	*out = ctx.result;
	return 0;
}

static int nvk_vma_find_writable(struct task_struct *task,
				 unsigned long min_size,
				 struct nvk_vma_info *out)
{
	struct _nvk_find_map_ctx ctx;
	ctx.prot = NVK_VM_READ | NVK_VM_WRITE;
	ctx.min_size = min_size;
	ctx.found = 0;

	int ret = nvk_vma_walk(task, _nvk_find_map_cb, &ctx);
	if (ret < 0) return ret;
	if (!ctx.found) return -1;
	*out = ctx.result;
	return 0;
}

#endif /* NVK_VMA_H */
