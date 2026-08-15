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
typedef void  (*neverc_krt_mmap_read_lock_fn)(struct mm_struct *mm);
typedef void  (*neverc_krt_mmap_read_unlock_fn)(struct mm_struct *mm);
typedef void  (*neverc_krt_mm_ref_fn)(struct mm_struct *mm);

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
static neverc_krt_mm_ref_fn            _neverc_krt_mmgrab;
static neverc_krt_mm_ref_fn            _neverc_krt_mmdrop;
neverc_krt_get_task_mm_fn              _neverc_krt_get_task_mm = (void *)0;
neverc_krt_mmput_fn                    _neverc_krt_mmput = (void *)0;
static int                             _neverc_krt_vma_inited;

/* ---- internal types (continued) ---- */

struct _neverc_krt_find_map_ctx {
	unsigned long prot;
	unsigned long min_size;
	struct neverc_krt_vma_info result;
	int found;
};

/* ---- internal helpers ---- */

static void *_neverc_krt_task_mm(struct task_struct *task, int *referenced);
static void _neverc_krt_task_mm_put(void *mm, int referenced);
static void _neverc_krt_read_vma_info(const void *vma, struct neverc_krt_vma_info *info);
static int _neverc_krt_find_map_cb(const struct neverc_krt_vma_info *vma, void *data);

static __always_inline int _neverc_krt_vma_field_fits(
	unsigned long size, unsigned long offset, unsigned long width)
{
	return size && offset < size && width <= size - offset;
}

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
	_neverc_krt_mmgrab =
		(neverc_krt_mm_ref_fn)NEVERC_KRT_LOOKUP("mmgrab");
	if (!_neverc_krt_mmgrab)
		_neverc_krt_mmgrab =
			(neverc_krt_mm_ref_fn)NEVERC_KRT_LOOKUP(
				"rust_helper_mmgrab");
	_neverc_krt_mmdrop =
		(neverc_krt_mm_ref_fn)NEVERC_KRT_LOOKUP("mmdrop");
	if (!_neverc_krt_mmdrop)
		_neverc_krt_mmdrop =
			(neverc_krt_mm_ref_fn)NEVERC_KRT_LOOKUP(
				"rust_helper_mmdrop");

	_neverc_krt_vma_inited = 1;
	return _neverc_krt_find_vma ? 0 :
	       (_neverc_krt_access_task_vm || _neverc_krt_access_mm_vm) ? 0 : -1;
}

void *neverc_krt_task_mm_get(struct task_struct *task)
{
	neverc_krt_vma_init();
	if (!task || !_neverc_krt_get_task_mm || !_neverc_krt_mmput)
		return (void *)0;
	return _neverc_krt_get_task_mm(task);
}

void neverc_krt_task_mm_put(void *mm)
{
	neverc_krt_vma_init();
	if (mm && _neverc_krt_mmput)
		_neverc_krt_mmput((struct mm_struct *)mm);
}

int neverc_krt_mm_grab(void *mm)
{
	neverc_krt_vma_init();
	if (!mm || !_neverc_krt_mmgrab || !_neverc_krt_mmdrop)
		return -1;
	_neverc_krt_mmgrab((struct mm_struct *)mm);
	return 0;
}

void neverc_krt_mm_drop(void *mm)
{
	neverc_krt_vma_init();
	if (mm && _neverc_krt_mmdrop)
		_neverc_krt_mmdrop((struct mm_struct *)mm);
}

int neverc_krt_vma_snapshot(const void *vma,
			    struct neverc_krt_vma_snapshot *snapshot)
{
	const struct neverc_krt_gki_layout *layout;
	struct neverc_krt_vma_snapshot value;

	if (!snapshot)
		return -1;
	__builtin_memset(snapshot, 0, sizeof(*snapshot));
	if (!vma)
		return -1;

	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_USER_PTMAP);
	if (!layout ||
	    !_neverc_krt_vma_field_fits(layout->vma_size, layout->vma_start,
					sizeof(value.start)) ||
	    !_neverc_krt_vma_field_fits(layout->vma_size, layout->vma_end,
					sizeof(value.end)) ||
	    !_neverc_krt_vma_field_fits(layout->vma_size, layout->vma_mm,
					sizeof(value.mm_identity)))
		return -2;

	__builtin_memset(&value, 0, sizeof(value));
	if (neverc_krt_mem_read(&value.start,
			(const char *)vma + layout->vma_start,
			sizeof(value.start)) ||
	    neverc_krt_mem_read(&value.end,
			(const char *)vma + layout->vma_end,
			sizeof(value.end)) ||
	    neverc_krt_mem_read(&value.mm_identity,
			(const char *)vma + layout->vma_mm,
			sizeof(value.mm_identity)))
		return -3;
	if (value.end < value.start)
		return -4;

	*snapshot = value;
	return 0;
}

static void *_neverc_krt_task_mm(struct task_struct *task, int *referenced)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long mm;

	*referenced = 0;
	if (!task) return (void *)0;

	if (_neverc_krt_get_task_mm && _neverc_krt_mmput) {
		void *result = _neverc_krt_get_task_mm(task);

		if (result) {
			*referenced = 1;
			return result;
		}
	}

	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout ||
	    !_neverc_krt_vma_field_fits(layout->task_size, layout->task_mm,
					sizeof(mm)))
		return (void *)0;
	if (neverc_krt_mem_read(&mm,
			(const char *)task + layout->task_mm, sizeof(mm)))
		return (void *)0;
	if (mm < 0xFFFF000000000000UL ||
	    mm >= 0xFFFFFFFFFFFFF000UL)
		return (void *)0;
	return (void *)mm;
}

static void _neverc_krt_task_mm_put(void *mm, int referenced)
{
	if (mm && referenced && _neverc_krt_mmput)
		_neverc_krt_mmput((struct mm_struct *)mm);
}

static void _neverc_krt_read_vma_info(const void *vma,
				      struct neverc_krt_vma_info *info)
{
	const struct neverc_krt_gki_layout *layout =
		_neverc_krt_get_proven_gki_layout(
			NEVERC_KRT_LAYOUT_CERT_USER_PTMAP);
	struct neverc_krt_vma_snapshot snapshot;
	unsigned long raw_flags = 0;

	__builtin_memset(info, 0, sizeof(*info));
	if (neverc_krt_vma_snapshot(vma, &snapshot))
		return;
	info->start = snapshot.start;
	info->end = snapshot.end;
	if (!layout ||
	    !_neverc_krt_vma_field_fits(layout->vma_size, layout->vma_flags,
					sizeof(raw_flags)) ||
	    neverc_krt_mem_read(&raw_flags,
			(const char *)vma + layout->vma_flags,
			sizeof(raw_flags)))
		raw_flags = 0;
	if (!layout ||
	    !_neverc_krt_vma_field_fits(layout->vma_size, layout->vma_pgoff,
					sizeof(info->pgoff)) ||
	    neverc_krt_mem_read(&info->pgoff,
			(const char *)vma + layout->vma_pgoff,
			sizeof(info->pgoff)))
		info->pgoff = 0;
	info->flags = raw_flags & 0xFFFF;
}

int neverc_krt_vma_find(struct task_struct *task, unsigned long addr,
			struct neverc_krt_vma_info *info)
{
	void *mm, *vma;
	int locked = 0, mm_referenced = 0;

	if (!_neverc_krt_find_vma || !info)
		return -1;

	mm = _neverc_krt_task_mm(task, &mm_referenced);
	if (!mm) return -2;

	if (_neverc_krt_mmap_read_lock) {
		_neverc_krt_mmap_read_lock((struct mm_struct *)mm);
		locked = 1;
	}

	vma = _neverc_krt_find_vma(mm, addr);
	if (!vma) {
		if (locked)
			_neverc_krt_mmap_read_unlock((struct mm_struct *)mm);
		_neverc_krt_task_mm_put(mm, mm_referenced);
		return -3;
	}

	_neverc_krt_read_vma_info(vma, info);

	if (locked)
		_neverc_krt_mmap_read_unlock((struct mm_struct *)mm);
	_neverc_krt_task_mm_put(mm, mm_referenced);

	if (addr < info->start)
		return -3;

	return 0;
}

int neverc_krt_vma_walk(struct task_struct *task,
			neverc_krt_vma_callback_t callback, void *data)
{
	void *mm;
	int count = 0, locked = 0, mm_referenced = 0;

	if (!_neverc_krt_find_vma || !callback)
		return -1;

	mm = _neverc_krt_task_mm(task, &mm_referenced);
	if (!mm) return -2;

	if (_neverc_krt_mmap_read_lock) {
		_neverc_krt_mmap_read_lock((struct mm_struct *)mm);
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

	if (locked)
		_neverc_krt_mmap_read_unlock((struct mm_struct *)mm);
	_neverc_krt_task_mm_put(mm, mm_referenced);
	return count;
}

long neverc_krt_vma_read_remote(struct task_struct *task,
				unsigned long addr,
				void *buf, size_t len)
{
	if (_neverc_krt_access_task_vm)
		return _neverc_krt_access_task_vm(task, addr, buf, (int)len, 0);

	if (_neverc_krt_access_mm_vm) {
		int mm_referenced = 0;
		long ret;
		void *mm = _neverc_krt_task_mm(task, &mm_referenced);

		if (!mm) return -1;
		ret = _neverc_krt_access_mm_vm(mm, addr, buf, (int)len, 0);
		_neverc_krt_task_mm_put(mm, mm_referenced);
		return ret;
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
		int mm_referenced = 0;
		long ret;
		void *mm = _neverc_krt_task_mm(task, &mm_referenced);

		if (!mm) return -1;
		ret = _neverc_krt_access_mm_vm(mm, addr, (void *)buf,
					      (int)len, 1);
		_neverc_krt_task_mm_put(mm, mm_referenced);
		return ret;
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
