/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_inject.c — implementations extracted from neverc_krt_inject.h. */
#include <nvk.h>

int neverc_krt_inject_init(void)
{
	if (_neverc_krt_inject_inited) return 0;

	_neverc_krt_do_mmap = (neverc_krt_do_mmap_fn)NEVERC_KRT_LOOKUP("do_mmap");
	_neverc_krt_vm_mmap = (neverc_krt_vm_mmap_fn)NEVERC_KRT_LOOKUP("vm_mmap");
	_neverc_krt_do_munmap = (neverc_krt_do_munmap_fn)NEVERC_KRT_LOOKUP("do_munmap");
	if (!_neverc_krt_do_munmap)
		_neverc_krt_do_munmap = (neverc_krt_do_munmap_fn)NEVERC_KRT_LOOKUP("__do_munmap");

	_neverc_krt_inject_inited = 1;
	return (_neverc_krt_do_mmap || _neverc_krt_vm_mmap) ? 0 : -1;
}

long neverc_krt_inject_write(struct task_struct *task,
			     unsigned long addr,
			     const void *data, size_t len)
{
	return neverc_krt_vma_write_remote(task, addr, data, len);
}

long neverc_krt_inject_read(struct task_struct *task,
			    unsigned long addr,
			    void *buf, size_t len)
{
	return neverc_krt_vma_read_remote(task, addr, buf, len);
}

static void _neverc_krt_inject_flush_code(unsigned long addr, size_t len)
{
	unsigned long line;
	unsigned long end = addr + len;

	for (line = addr & ~63UL; line < end; line += 64)
		__asm__ __volatile__("dc civac, %0" :: "r"(line) : "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("ic iallu" ::: "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
}

int neverc_krt_inject_shellcode(struct task_struct *task,
				const struct neverc_krt_shellcode *sc,
				unsigned long target_addr)
{
	if (!task || !sc || !sc->code) return -1;
	if (sc->insn_count <= 0 || sc->insn_count > 1024)
		return -2;

	size_t code_sz = (size_t)sc->insn_count * 4;
	long ret = neverc_krt_inject_write(task, target_addr,
				     sc->code, code_sz);
	if (ret) return (int)ret;

	_neverc_krt_inject_flush_code(target_addr, code_sz);
	return 0;
}

unsigned long neverc_krt_inject_find_cave(struct task_struct *task,
					  unsigned long min_size)
{
	struct neverc_krt_vma_info info;
	int ret = neverc_krt_vma_find_exec(task, min_size + 0x1000, &info);
	if (ret) return 0;
	return info.end - min_size - 64;
}

static void _neverc_krt_inject_resolve_mm(void)
{
	if (!_neverc_krt_get_task_mm) {
		_neverc_krt_get_task_mm =
			(neverc_krt_get_task_mm_fn)NEVERC_KRT_LOOKUP("get_task_mm");
		_neverc_krt_mmput = (neverc_krt_mmput_fn)NEVERC_KRT_LOOKUP("mmput");
		_neverc_krt_mmap_wlock =
			(neverc_krt_mmap_write_lock_fn)NEVERC_KRT_LOOKUP("mmap_write_lock");
		if (!_neverc_krt_mmap_wlock)
			_neverc_krt_mmap_wlock =
				(neverc_krt_mmap_write_lock_fn)NEVERC_KRT_LOOKUP("down_write");
		_neverc_krt_mmap_wunlock =
			(neverc_krt_mmap_write_unlock_fn)NEVERC_KRT_LOOKUP("mmap_write_unlock");
		if (!_neverc_krt_mmap_wunlock)
			_neverc_krt_mmap_wunlock =
				(neverc_krt_mmap_write_unlock_fn)NEVERC_KRT_LOOKUP("up_write");
		_neverc_krt_use_mm =
			(neverc_krt_use_mm_fn)NEVERC_KRT_LOOKUP("kthread_use_mm");
		if (!_neverc_krt_use_mm)
			_neverc_krt_use_mm =
				(neverc_krt_use_mm_fn)NEVERC_KRT_LOOKUP("use_mm");
		_neverc_krt_unuse_mm =
			(neverc_krt_unuse_mm_fn)NEVERC_KRT_LOOKUP("kthread_unuse_mm");
		if (!_neverc_krt_unuse_mm)
			_neverc_krt_unuse_mm =
				(neverc_krt_unuse_mm_fn)NEVERC_KRT_LOOKUP("unuse_mm");
	}
}

unsigned long neverc_krt_inject_mmap(struct task_struct *task,
				     unsigned long len, unsigned long prot)
{
	unsigned long addr = 0;
	void *mm;

	if (!__atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE))
		return 0;

	_neverc_krt_inject_resolve_mm();
	if (!_neverc_krt_get_task_mm || !_neverc_krt_do_mmap) return 0;
	if (!_neverc_krt_use_mm || !_neverc_krt_unuse_mm) return 0;

	mm = _neverc_krt_get_task_mm(task);
	if (!mm) return 0;

	unsigned long flags = NEVERC_KRT_INJECT_MAP_PRIVATE | NEVERC_KRT_INJECT_MAP_ANON;
	unsigned long populate = 0;
	unsigned long result;

	_neverc_krt_use_mm(mm);
	if (_neverc_krt_mmap_wlock) _neverc_krt_mmap_wlock(mm);

	if (__atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE) >= 606) {
		neverc_krt_do_mmap_v2_fn fn =
			(neverc_krt_do_mmap_v2_fn)(void *)_neverc_krt_do_mmap;
		result = fn((void *)0, 0, len, prot, flags,
			    0, 0, &populate, (void *)0);
	} else {
		result = (unsigned long)_neverc_krt_do_mmap(
			(void *)0, 0, len, prot, flags,
			0, &populate, (void *)0);
	}

	if (_neverc_krt_mmap_wunlock) _neverc_krt_mmap_wunlock(mm);
	_neverc_krt_unuse_mm(mm);

	if (result < 0x1000UL)
		addr = 0;
	else
		addr = result;

	if (_neverc_krt_mmput) _neverc_krt_mmput(mm);
	return addr;
}

int neverc_krt_inject_munmap(struct task_struct *task,
			     unsigned long addr, size_t len)
{
	void *mm;
	int ret = -1;

	_neverc_krt_inject_resolve_mm();
	if (!_neverc_krt_get_task_mm || !_neverc_krt_do_munmap) return -1;
	if (!_neverc_krt_use_mm || !_neverc_krt_unuse_mm) return -1;

	mm = _neverc_krt_get_task_mm(task);
	if (!mm) return -1;

	_neverc_krt_use_mm(mm);
	if (_neverc_krt_mmap_wlock) _neverc_krt_mmap_wlock(mm);
	ret = _neverc_krt_do_munmap(mm, addr, len, (void *)0);
	if (_neverc_krt_mmap_wunlock) _neverc_krt_mmap_wunlock(mm);
	_neverc_krt_unuse_mm(mm);

	if (_neverc_krt_mmput) _neverc_krt_mmput(mm);
	return ret;
}

int neverc_krt_inject_hijack_setup(struct neverc_krt_thread_hijack *hj,
				   struct task_struct *task,
				   const struct neverc_krt_shellcode *sc)
{
	if (!hj || !task || !sc || !sc->code) return -1;
	__builtin_memset(hj, 0, sizeof(*hj));

	size_t code_sz = (size_t)sc->insn_count * 4;
	unsigned long prot = NEVERC_KRT_INJECT_PROT_READ | NEVERC_KRT_INJECT_PROT_EXEC;
	unsigned long alloc_sz = (code_sz + 0xFFF) & ~0xFFFUL;

	unsigned long code_addr = neverc_krt_inject_mmap(task, alloc_sz, prot);
	if (!code_addr) return -2;

	long ret = neverc_krt_inject_write(task, code_addr, sc->code, code_sz);
	if (ret) {
		neverc_krt_inject_munmap(task, code_addr, alloc_sz);
		return -3;
	}

	_neverc_krt_inject_flush_code(code_addr, code_sz);

	hj->code_addr = code_addr + sc->entry_offset;
	hj->code_size = alloc_sz;
	hj->active = 1;
	return 0;
}

int neverc_krt_inject_elf(struct task_struct *task,
			  const void *elf_data, size_t elf_len,
			  struct neverc_krt_elf_load_info *info)
{
	const struct neverc_krt_elf64_hdr *ehdr;
	const struct neverc_krt_elf64_phdr *phdr;
	unsigned long lo = ~0UL, hi = 0;
	int i, load_count = 0;

	if (!task || !elf_data || elf_len < sizeof(*ehdr) || !info)
		return -1;

	ehdr = (const struct neverc_krt_elf64_hdr *)elf_data;
	if (ehdr->magic != NEVERC_KRT_ELF_MAGIC) return -2;
	if (ehdr->ei_class != 2) return -3;
	if (ehdr->e_type != NEVERC_KRT_ET_DYN) return -4;
	if (ehdr->e_phoff + (u64)ehdr->e_phnum * ehdr->e_phentsize > elf_len)
		return -5;

	phdr = (const struct neverc_krt_elf64_phdr *)
		((const u8 *)elf_data + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type != NEVERC_KRT_PT_LOAD) continue;
		unsigned long seg_lo = phdr[i].p_vaddr;
		unsigned long seg_hi = seg_lo + phdr[i].p_memsz;
		if (seg_lo < lo) lo = seg_lo;
		if (seg_hi > hi) hi = seg_hi;
		load_count++;
	}

	if (load_count == 0) return -6;

	unsigned long total = ((hi - lo) + 0xFFF) & ~0xFFFUL;
	unsigned long prot = NEVERC_KRT_INJECT_PROT_READ | NEVERC_KRT_INJECT_PROT_WRITE |
			     NEVERC_KRT_INJECT_PROT_EXEC;
	unsigned long base = neverc_krt_inject_mmap(task, total, prot);
	if (!base) return -7;

	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type != NEVERC_KRT_PT_LOAD) continue;
		if (phdr[i].p_offset + phdr[i].p_filesz > elf_len) {
			neverc_krt_inject_munmap(task, base, total);
			return -8;
		}

		unsigned long dst = base + (phdr[i].p_vaddr - lo);
		const void *src = (const u8 *)elf_data + phdr[i].p_offset;
		long ret = neverc_krt_inject_write(task, dst, src,
					     (size_t)phdr[i].p_filesz);
		if (ret) {
			neverc_krt_inject_munmap(task, base, total);
			return -9;
		}

		if (phdr[i].p_flags & 1)
			_neverc_krt_inject_flush_code(dst, (size_t)phdr[i].p_filesz);
	}

	info->base = base;
	info->entry = base + (ehdr->e_entry - lo);
	info->num_segments = load_count;
	info->total_size = total;
	return 0;
}

