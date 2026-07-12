/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_vma.c — virtual memory area operations. */
#include <nvk.h>
#include "nvk_internal.h"

/* ---- internal types ---- */

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

/* ---- internal variables (file-local) ---- */

static neverc_krt_find_vma_fn          _neverc_krt_find_vma;
static neverc_krt_access_task_fn       _neverc_krt_access_task_vm;
static neverc_krt_access_mm_fn         _neverc_krt_access_mm_vm;
static neverc_krt_get_user_pages_fn    _neverc_krt_get_user_pages;
static neverc_krt_page_address_fn      _neverc_krt_page_address;
static neverc_krt_put_page_fn          _neverc_krt_put_page;
static neverc_krt_kmap_fn              _neverc_krt_kmap;
static neverc_krt_kunmap_fn            _neverc_krt_kunmap;
static neverc_krt_mmap_read_lock_fn    _neverc_krt_mmap_read_lock;
static neverc_krt_mmap_read_unlock_fn  _neverc_krt_mmap_read_unlock;
neverc_krt_get_task_mm_fn              _neverc_krt_get_task_mm = (void *)0;
neverc_krt_mmput_fn                    _neverc_krt_mmput = (void *)0;
static int                             _neverc_krt_vma_inited;
static unsigned long                   _neverc_krt_off_mm;
static unsigned long                   _neverc_krt_off_vm_flags;

/* ---- internal types (continued) ---- */

struct _neverc_krt_find_map_ctx {
	unsigned long prot;
	unsigned long min_size;
	struct neverc_krt_vma_info result;
	int found;
};

/* ---- internal helpers ---- */

static void *_neverc_krt_task_mm(struct task_struct *task);
static void _neverc_krt_detect_vm_flags_off(const void *vma);
static void _neverc_krt_read_vma_info(const void *vma, struct neverc_krt_vma_info *info);
static int _neverc_krt_find_map_cb(const struct neverc_krt_vma_info *vma, void *data);

int neverc_krt_vma_init(void)
{
	if (_neverc_krt_vma_inited) return 0;

	neverc_krt_mem_init();
	neverc_krt_process_init();

	_neverc_krt_find_vma =
		(neverc_krt_find_vma_fn)NEVERC_KRT_LOOKUP("find_vma");
	_neverc_krt_access_task_vm =
		(neverc_krt_access_task_fn)NEVERC_KRT_LOOKUP("access_process_vm");
	_neverc_krt_access_mm_vm =
		(neverc_krt_access_mm_fn)NEVERC_KRT_LOOKUP("access_remote_vm");
	_neverc_krt_get_user_pages =
		(neverc_krt_get_user_pages_fn)NEVERC_KRT_LOOKUP("get_user_pages_remote");
	_neverc_krt_page_address =
		(neverc_krt_page_address_fn)NEVERC_KRT_LOOKUP("page_address");
	_neverc_krt_put_page =
		(neverc_krt_put_page_fn)NEVERC_KRT_LOOKUP("put_page");
	_neverc_krt_kmap =
		(neverc_krt_kmap_fn)NEVERC_KRT_LOOKUP("kmap");
	_neverc_krt_kunmap =
		(neverc_krt_kunmap_fn)NEVERC_KRT_LOOKUP("kunmap");
	_neverc_krt_mmap_read_lock =
		(neverc_krt_mmap_read_lock_fn)NEVERC_KRT_LOOKUP("mmap_read_lock");
	_neverc_krt_mmap_read_unlock =
		(neverc_krt_mmap_read_unlock_fn)NEVERC_KRT_LOOKUP("mmap_read_unlock");

	_neverc_krt_get_task_mm =
		(neverc_krt_get_task_mm_fn)NEVERC_KRT_LOOKUP("get_task_mm");
	_neverc_krt_mmput =
		(neverc_krt_mmput_fn)NEVERC_KRT_LOOKUP("mmput");

	_neverc_krt_vma_inited = 1;
	return _neverc_krt_find_vma ? 0 :
	       (_neverc_krt_access_task_vm || _neverc_krt_access_mm_vm) ? 0 : -1;
}

static void *_neverc_krt_task_mm(struct task_struct *task)
{
	if (!task) return (void *)0;

	if (__atomic_load_n(&_neverc_krt_off_mm, __ATOMIC_ACQUIRE) == 0) {
		/*
		 * Method 1 (6.1+safe): ask the kernel for mm, then scan
		 * task_struct to discover at which offset the pointer lives.
		 * Works regardless of mm_struct layout changes (5.10 mmap
		 * pointer vs 6.1+ maple_tree first field).
		 */
		if (_neverc_krt_get_task_mm && _neverc_krt_mmput) {
			void *mm = _neverc_krt_get_task_mm(task);
			if (mm) {
				const unsigned char *p =
					(const unsigned char *)task;
				unsigned long i;
				for (i = 0x100; i < 0xE00; i += 8) {
					unsigned long v;
					if (neverc_krt_mem_read(&v, p + i, 8))
						continue;
					if (v == (unsigned long)mm) {
						__atomic_store_n(
							&_neverc_krt_off_mm,
							i, __ATOMIC_RELEASE);
						break;
					}
				}
				_neverc_krt_mmput(mm);
			}
		}

		/*
		 * Method 2 (fallback): heuristic scan if get_task_mm was
		 * not available.  Checks that the first 8 bytes of the
		 * candidate struct look like a kernel pointer — only
		 * reliable on 5.10/5.15 where mm_struct starts with the
		 * mmap VMA pointer.
		 */
		if (__atomic_load_n(&_neverc_krt_off_mm, __ATOMIC_ACQUIRE)
		    == 0) {
			const unsigned char *p =
				(const unsigned char *)task;
			unsigned long i;
			for (i = 0x200; i < 0xE00; i += 8) {
				unsigned long v;
				if (neverc_krt_mem_read(&v, p + i, 8))
					continue;
				if (v < 0xFFFF000000000000UL || v == 0)
					continue;
				unsigned long first;
				if (neverc_krt_mem_read(&first,
						       (void *)v, 8))
					continue;
				if (first > 0xFFFF000000000000UL) {
					unsigned long second;
					if (neverc_krt_mem_read(
						    &second,
						    (void *)(v + 8), 8))
						continue;
					if (second > 0xFFFF000000000000UL ||
					    second == 0) {
						__atomic_store_n(
							&_neverc_krt_off_mm,
							i,
							__ATOMIC_RELEASE);
						break;
					}
				}
			}
		}
	}

	if (!_neverc_krt_off_mm) return (void *)0;
	unsigned long mm_val;
	if (neverc_krt_mem_read(&mm_val,
			(void *)((unsigned long)task + _neverc_krt_off_mm), 8))
		return (void *)0;
	return (void *)mm_val;
}

static void _neverc_krt_detect_vm_flags_off(const void *vma)
{
	if (__atomic_load_n(&_neverc_krt_off_vm_flags, __ATOMIC_ACQUIRE))
		return;

	unsigned long start, end;
	if (neverc_krt_mem_read(&start, vma, 8))
		return;
	if (neverc_krt_mem_read(&end, (const char *)vma + 8, 8))
		return;
	if (end <= start || start == 0) return;

	unsigned long i;
	for (i = 2; i < 16; i++) {
		unsigned long val;
		if (neverc_krt_mem_read(&val, (const char *)vma + i * 8, 8))
			continue;
		if (val == 0 || val >= 0xFFFF000000000000UL) continue;
		if (val > 0x100000) continue;
		if (val & NEVERC_KRT_VM_READ) {
			__atomic_store_n(&_neverc_krt_off_vm_flags, i * 8,
					 __ATOMIC_RELEASE);
			return;
		}
	}
	int kv = __atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE);
	if (kv >= 601)
		__atomic_store_n(&_neverc_krt_off_vm_flags, 32, __ATOMIC_RELEASE);
	else
		__atomic_store_n(&_neverc_krt_off_vm_flags, 80, __ATOMIC_RELEASE);
}

static void _neverc_krt_read_vma_info(const void *vma, struct neverc_krt_vma_info *info)
{
	if (neverc_krt_mem_read(&info->start, vma, 8)) {
		info->start = 0; info->end = 0; info->flags = 0;
		info->pgoff = 0; return;
	}
	if (neverc_krt_mem_read(&info->end, (const char *)vma + 8, 8)) {
		info->end = 0; info->flags = 0; info->pgoff = 0; return;
	}
	_neverc_krt_detect_vm_flags_off(vma);
	unsigned long raw_flags;
	if (neverc_krt_mem_read(&raw_flags,
			(const char *)vma + _neverc_krt_off_vm_flags, 8))
		raw_flags = 0;
	info->flags = raw_flags & 0xFFFF;
	info->pgoff = 0;
}

int neverc_krt_vma_find(struct task_struct *task, unsigned long addr,
			struct neverc_krt_vma_info *info)
{
	void *mm, *vma;
	int locked = 0;

	if (!_neverc_krt_find_vma || !info)
		return -1;

	mm = _neverc_krt_task_mm(task);
	if (!mm) return -2;

	if (_neverc_krt_mmap_read_lock) {
		_neverc_krt_mmap_read_lock(mm);
		locked = 1;
	}

	vma = _neverc_krt_find_vma(mm, addr);
	if (!vma) {
		if (locked) _neverc_krt_mmap_read_unlock(mm);
		return -3;
	}

	_neverc_krt_read_vma_info(vma, info);

	if (locked) _neverc_krt_mmap_read_unlock(mm);

	if (addr < info->start)
		return -3;

	return 0;
}

int neverc_krt_vma_walk(struct task_struct *task,
			neverc_krt_vma_callback_t callback, void *data)
{
	void *mm;
	int count = 0, locked = 0;

	if (!_neverc_krt_find_vma || !callback)
		return -1;

	mm = _neverc_krt_task_mm(task);
	if (!mm) return -2;

	if (_neverc_krt_mmap_read_lock) {
		_neverc_krt_mmap_read_lock(mm);
		locked = 1;
	}

	unsigned long addr = 0;
	for (;;) {
		void *vma = _neverc_krt_find_vma(mm, addr);
		if (!vma) break;

		struct neverc_krt_vma_info info;
		_neverc_krt_read_vma_info(vma, &info);

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

	if (locked) _neverc_krt_mmap_read_unlock(mm);
	return count;
}

long neverc_krt_vma_read_remote(struct task_struct *task,
				unsigned long addr,
				void *buf, size_t len)
{
	if (_neverc_krt_access_task_vm)
		return _neverc_krt_access_task_vm(task, addr, buf, (int)len, 0);

	if (_neverc_krt_access_mm_vm) {
		void *mm = _neverc_krt_task_mm(task);
		if (!mm) return -1;
		return _neverc_krt_access_mm_vm(mm, addr, buf, (int)len, 0);
	}

	unsigned long pa = neverc_krt_translate_user(addr);
	if (!pa) return -2;

	unsigned long va = neverc_krt_phys_to_virt(pa);
	if (!va) return -3;

	return neverc_krt_mem_read(buf, (void *)va, len);
}

long neverc_krt_vma_write_remote(struct task_struct *task,
				 unsigned long addr,
				 const void *buf, size_t len)
{
	if (_neverc_krt_access_task_vm)
		return _neverc_krt_access_task_vm(task, addr, (void *)buf,
					   (int)len, 1);

	if (_neverc_krt_access_mm_vm) {
		void *mm = _neverc_krt_task_mm(task);
		if (!mm) return -1;
		return _neverc_krt_access_mm_vm(mm, addr, (void *)buf,
					 (int)len, 1);
	}

	unsigned long pa = neverc_krt_translate_user(addr);
	if (!pa) return -2;

	unsigned long va = neverc_krt_phys_to_virt(pa);
	if (!va) return -3;

	return neverc_krt_mem_write((void *)va, buf, len);
}

static int _neverc_krt_find_map_cb(const struct neverc_krt_vma_info *vma, void *data)
{
	struct _neverc_krt_find_map_ctx *ctx = (struct _neverc_krt_find_map_ctx *)data;
	unsigned long sz = vma->end - vma->start;

	if (sz < ctx->min_size)
		return 0;
	if ((vma->flags & ctx->prot) != ctx->prot)
		return 0;

	ctx->result = *vma;
	ctx->found = 1;
	return 1;
}

int neverc_krt_vma_find_exec(struct task_struct *task,
			     unsigned long min_size,
			     struct neverc_krt_vma_info *out)
{
	struct _neverc_krt_find_map_ctx ctx;
	ctx.prot = NEVERC_KRT_VM_READ | NEVERC_KRT_VM_EXEC;
	ctx.min_size = min_size;
	ctx.found = 0;

	int ret = neverc_krt_vma_walk(task, _neverc_krt_find_map_cb, &ctx);
	if (ret < 0) return ret;
	if (!ctx.found) return -1;
	*out = ctx.result;
	return 0;
}

int neverc_krt_vma_find_writable(struct task_struct *task,
				 unsigned long min_size,
				 struct neverc_krt_vma_info *out)
{
	struct _neverc_krt_find_map_ctx ctx;
	ctx.prot = NEVERC_KRT_VM_READ | NEVERC_KRT_VM_WRITE;
	ctx.min_size = min_size;
	ctx.found = 0;

	int ret = neverc_krt_vma_walk(task, _neverc_krt_find_map_cb, &ctx);
	if (ret < 0) return ret;
	if (!ctx.found) return -1;
	*out = ctx.result;
	return 0;
}

