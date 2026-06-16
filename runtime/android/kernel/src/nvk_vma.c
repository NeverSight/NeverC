/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_vma.c — implementations extracted from neverc_krt_vma.h. */
#include <nvk.h>

int neverc_krt_vma_init(void)
{
	if (_neverc_krt_vma_inited) return 0;

	if (!_neverc_krt_mem_inited)
		neverc_krt_mem_init();
	if (!_neverc_krt_proc_inited)
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
	if (!_neverc_krt_mmap_read_lock)
		_neverc_krt_mmap_read_lock =
			(neverc_krt_mmap_read_lock_fn)NEVERC_KRT_LOOKUP("down_read");
	_neverc_krt_mmap_read_unlock =
		(neverc_krt_mmap_read_unlock_fn)NEVERC_KRT_LOOKUP("mmap_read_unlock");
	if (!_neverc_krt_mmap_read_unlock)
		_neverc_krt_mmap_read_unlock =
			(neverc_krt_mmap_read_unlock_fn)NEVERC_KRT_LOOKUP("up_read");

	_neverc_krt_vma_inited = 1;
	return _neverc_krt_find_vma ? 0 :
	       (_neverc_krt_access_task_vm || _neverc_krt_access_mm_vm) ? 0 : -1;
}

void *_neverc_krt_task_mm(struct task_struct *task)
{
	if (!task) return (void *)0;

	if (__atomic_load_n(&_neverc_krt_off_mm, __ATOMIC_ACQUIRE) == 0) {
		const unsigned char *p = (const unsigned char *)task;
		unsigned long i;
		for (i = 0x200; i < 0xE00; i += 8) {
			unsigned long v = *(unsigned long *)(p + i);
			if (v < 0xFFFF000000000000UL || v == 0)
				continue;
			unsigned long pgd = *(unsigned long *)v;
			if (pgd > 0xFFFF000000000000UL) {
				unsigned long mmap_ptr =
					*(unsigned long *)(v + 8);
				if (mmap_ptr > 0xFFFF000000000000UL ||
				    mmap_ptr == 0) {
					__atomic_store_n(&_neverc_krt_off_mm, i, __ATOMIC_RELEASE);
					break;
				}
			}
		}
	}

	if (!_neverc_krt_off_mm) return (void *)0;
	return *(void **)((unsigned long)task + _neverc_krt_off_mm);
}

void _neverc_krt_detect_vm_flags_off(const void *vma)
{
	if (__atomic_load_n(&_neverc_krt_off_vm_flags, __ATOMIC_ACQUIRE))
		return;

	const unsigned long *v = (const unsigned long *)vma;
	unsigned long start = v[0], end = v[1];
	if (end <= start || start == 0) return;

	unsigned long i;
	for (i = 2; i < 16; i++) {
		unsigned long val = v[i];
		if (val == 0 || val >= 0xFFFF000000000000UL) continue;
		if (val > 0x100000) continue;
		if (val & NEVERC_KRT_VM_READ) {
			__atomic_store_n(&_neverc_krt_off_vm_flags, i * 8,
					 __ATOMIC_RELEASE);
			return;
		}
	}
#if NEVERC_KRT_KERNEL >= 601
	__atomic_store_n(&_neverc_krt_off_vm_flags, 32, __ATOMIC_RELEASE);
#else
	__atomic_store_n(&_neverc_krt_off_vm_flags, 80, __ATOMIC_RELEASE);
#endif
}

void _neverc_krt_read_vma_info(const void *vma, struct neverc_krt_vma_info *info)
{
	const unsigned long *v = (const unsigned long *)vma;
	info->start = v[0];
	info->end   = v[1];
	_neverc_krt_detect_vm_flags_off(vma);
	info->flags = *(unsigned long *)((unsigned long)vma + _neverc_krt_off_vm_flags)
		      & 0xFFFF;
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

int _neverc_krt_find_map_cb(const struct neverc_krt_vma_info *vma, void *data)
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

