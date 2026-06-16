/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_INJECT_H
#define NVK_INJECT_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_vma.h>
#include <nvk_addr.h>

typedef void *(*nvk_do_mmap_fn)(void *file, unsigned long addr,
				unsigned long len, unsigned long prot,
				unsigned long flags, unsigned long pgoff,
				unsigned long *populate, void *uf);
typedef int   (*nvk_vm_mmap_fn)(void *file, unsigned long addr,
				unsigned long len, unsigned long prot,
				unsigned long flags, unsigned long pgoff);
typedef int   (*nvk_do_munmap_fn)(void *mm, unsigned long start,
				  size_t len, void *uf);

static nvk_do_mmap_fn   _nvk_do_mmap;
static nvk_vm_mmap_fn   _nvk_vm_mmap;
static nvk_do_munmap_fn _nvk_do_munmap;
static int              _nvk_inject_inited;

#define NVK_INJECT_PROT_READ  0x1
#define NVK_INJECT_PROT_WRITE 0x2
#define NVK_INJECT_PROT_EXEC  0x4
#define NVK_INJECT_MAP_ANON   0x20
#define NVK_INJECT_MAP_PRIVATE 0x02

static int nvk_inject_init(void)
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

static long nvk_inject_write(struct task_struct *task,
			     unsigned long addr,
			     const void *data, size_t len)
{
	return nvk_vma_write_remote(task, addr, data, len);
}

static long nvk_inject_read(struct task_struct *task,
			    unsigned long addr,
			    void *buf, size_t len)
{
	return nvk_vma_read_remote(task, addr, buf, len);
}

struct nvk_shellcode {
	const u32   *code;
	int          insn_count;
	unsigned long entry_offset;
};

/*
 * ARM64 cache coherence for cross-process code injection:
 * DC CIVAC (clean+invalidate by VA to PoC) broadcasts across the
 * inner-shareable domain so the physical memory is updated for ALL cores.
 * IC IALLU invalidates the entire I-cache on the executing core; combined
 * with DSB ISH + ISB this ensures the target process sees new instructions
 * when it next runs on any core (the scheduler's context-switch path
 * issues ISB / IC IALLU on ARM64 GKI kernels).
 */
static void _nvk_inject_flush_code(unsigned long addr, size_t len)
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

static int nvk_inject_shellcode(struct task_struct *task,
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

static unsigned long nvk_inject_find_cave(struct task_struct *task,
					  unsigned long min_size)
{
	struct nvk_vma_info info;
	int ret = nvk_vma_find_exec(task, min_size + 0x1000, &info);
	if (ret) return 0;
	return info.end - min_size - 64;
}


/* ------------------------------------------------------------------ */
/*  Remote mmap — allocate memory in target process address space     */
/* ------------------------------------------------------------------ */

typedef void *(*nvk_get_task_mm_fn)(struct task_struct *task);
typedef void  (*nvk_mmput_fn)(void *mm);
typedef void  (*nvk_mmap_write_lock_fn)(void *mm);
typedef void  (*nvk_mmap_write_unlock_fn)(void *mm);

static nvk_get_task_mm_fn      _nvk_get_task_mm;
static nvk_mmput_fn            _nvk_mmput;
static nvk_mmap_write_lock_fn  _nvk_mmap_wlock;
static nvk_mmap_write_unlock_fn _nvk_mmap_wunlock;

/*
 * do_mmap operates on current->mm. To map into a remote process we must
 * temporarily adopt its mm via kthread_use_mm (5.10+) or use_mm (older).
 */
typedef void (*nvk_use_mm_fn)(void *mm);
typedef void (*nvk_unuse_mm_fn)(void *mm);

static nvk_use_mm_fn   _nvk_use_mm;
static nvk_unuse_mm_fn _nvk_unuse_mm;

static void _nvk_inject_resolve_mm(void)
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

static unsigned long nvk_inject_mmap(struct task_struct *task,
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

static int nvk_inject_munmap(struct task_struct *task,
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


/* ------------------------------------------------------------------ */
/*  Remote code execution via user thread hijack                      */
/* ------------------------------------------------------------------ */

/*
 * ARM64 pt_regs layout (stable across GKI):
 *   regs[0..30]  = X0–X30 (each 8 bytes, offset 0–248)
 *   sp           = offset 248 (X31/SP)
 *   pc           = offset 256
 *   pstate       = offset 264
 */
#define NVK_PTREGS_PC_OFF     256
#define NVK_PTREGS_SP_OFF     248
#define NVK_PTREGS_LR_OFF     240

struct nvk_thread_hijack {
	unsigned long saved_pc;
	unsigned long saved_sp;
	unsigned long saved_lr;
	unsigned long saved_x0;
	unsigned long code_addr;
	size_t        code_size;
	int           active;
};

static int nvk_inject_hijack_setup(struct nvk_thread_hijack *hj,
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


/* ------------------------------------------------------------------ */
/*  Simple ELF segment loading for shared library injection           */
/* ------------------------------------------------------------------ */

#define NVK_ELF_MAGIC  0x464C457FU  /* "\x7fELF" */
#define NVK_ET_DYN     3
#define NVK_PT_LOAD    1

struct nvk_elf64_hdr {
	u32 magic;
	u8  ei_class, ei_data, ei_version, ei_osabi;
	u8  _pad[8];
	u16 e_type, e_machine;
	u32 e_version;
	u64 e_entry;
	u64 e_phoff;
	u64 e_shoff;
	u32 e_flags;
	u16 e_ehsize, e_phentsize, e_phnum;
	u16 e_shentsize, e_shnum, e_shstrndx;
};

struct nvk_elf64_phdr {
	u32 p_type;
	u32 p_flags;
	u64 p_offset;
	u64 p_vaddr;
	u64 p_paddr;
	u64 p_filesz;
	u64 p_memsz;
	u64 p_align;
};

struct nvk_elf_load_info {
	unsigned long base;
	unsigned long entry;
	int           num_segments;
	unsigned long total_size;
};

static int nvk_inject_elf(struct task_struct *task,
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

#endif /* NVK_INJECT_H */
