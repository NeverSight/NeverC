/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_INJECT_H
#define NEVERC_KRT_INJECT_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_vma.h>
#include <nvk_addr.h>

typedef void *(*neverc_krt_do_mmap_fn)(void *file, unsigned long addr,
				unsigned long len, unsigned long prot,
				unsigned long flags, unsigned long pgoff,
				unsigned long *populate, void *uf);
typedef int   (*neverc_krt_vm_mmap_fn)(void *file, unsigned long addr,
				unsigned long len, unsigned long prot,
				unsigned long flags, unsigned long pgoff);
typedef int   (*neverc_krt_do_munmap_fn)(void *mm, unsigned long start,
				  size_t len, void *uf);

NEVERC_KRT_RT_VAR neverc_krt_do_mmap_fn   _neverc_krt_do_mmap;
NEVERC_KRT_RT_VAR neverc_krt_vm_mmap_fn   _neverc_krt_vm_mmap;
NEVERC_KRT_RT_VAR neverc_krt_do_munmap_fn _neverc_krt_do_munmap;
NEVERC_KRT_RT_VAR int              _neverc_krt_inject_inited;

#define NEVERC_KRT_INJECT_PROT_READ  0x1
#define NEVERC_KRT_INJECT_PROT_WRITE 0x2
#define NEVERC_KRT_INJECT_PROT_EXEC  0x4
#define NEVERC_KRT_INJECT_MAP_ANON   0x20
#define NEVERC_KRT_INJECT_MAP_PRIVATE 0x02

int neverc_krt_inject_init(void);


long neverc_krt_inject_write(struct task_struct *task,
			     unsigned long addr,
			     const void *data, size_t len);


long neverc_krt_inject_read(struct task_struct *task,
			    unsigned long addr,
			    void *buf, size_t len);


struct neverc_krt_shellcode {
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
void _neverc_krt_inject_flush_code(unsigned long addr, size_t len);


int neverc_krt_inject_shellcode(struct task_struct *task,
				const struct neverc_krt_shellcode *sc,
				unsigned long target_addr);


unsigned long neverc_krt_inject_find_cave(struct task_struct *task,
					  unsigned long min_size);



/* ------------------------------------------------------------------ */
/*  Remote mmap — allocate memory in target process address space     */
/* ------------------------------------------------------------------ */

typedef void *(*neverc_krt_get_task_mm_fn)(struct task_struct *task);
typedef void  (*neverc_krt_mmput_fn)(void *mm);
typedef void  (*neverc_krt_mmap_write_lock_fn)(void *mm);
typedef void  (*neverc_krt_mmap_write_unlock_fn)(void *mm);

NEVERC_KRT_RT_VAR neverc_krt_get_task_mm_fn      _neverc_krt_get_task_mm;
NEVERC_KRT_RT_VAR neverc_krt_mmput_fn            _neverc_krt_mmput;
NEVERC_KRT_RT_VAR neverc_krt_mmap_write_lock_fn  _neverc_krt_mmap_wlock;
NEVERC_KRT_RT_VAR neverc_krt_mmap_write_unlock_fn _neverc_krt_mmap_wunlock;

/*
 * do_mmap operates on current->mm. To map into a remote process we must
 * temporarily adopt its mm via kthread_use_mm (5.10+) or use_mm (older).
 */
typedef void (*neverc_krt_use_mm_fn)(void *mm);
typedef void (*neverc_krt_unuse_mm_fn)(void *mm);

NEVERC_KRT_RT_VAR neverc_krt_use_mm_fn   _neverc_krt_use_mm;
NEVERC_KRT_RT_VAR neverc_krt_unuse_mm_fn _neverc_krt_unuse_mm;

void _neverc_krt_inject_resolve_mm(void);


unsigned long neverc_krt_inject_mmap(struct task_struct *task,
				     unsigned long len, unsigned long prot);


int neverc_krt_inject_munmap(struct task_struct *task,
			     unsigned long addr, size_t len);



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
#define NEVERC_KRT_PTREGS_PC_OFF     256
#define NEVERC_KRT_PTREGS_SP_OFF     248
#define NEVERC_KRT_PTREGS_LR_OFF     240

struct neverc_krt_thread_hijack {
	unsigned long saved_pc;
	unsigned long saved_sp;
	unsigned long saved_lr;
	unsigned long saved_x0;
	unsigned long code_addr;
	size_t        code_size;
	int           active;
};

int neverc_krt_inject_hijack_setup(struct neverc_krt_thread_hijack *hj,
				   struct task_struct *task,
				   const struct neverc_krt_shellcode *sc);



/* ------------------------------------------------------------------ */
/*  Simple ELF segment loading for shared library injection           */
/* ------------------------------------------------------------------ */

#define NEVERC_KRT_ELF_MAGIC  0x464C457FU  /* "\x7fELF" */
#define NEVERC_KRT_ET_DYN     3
#define NEVERC_KRT_PT_LOAD    1

struct neverc_krt_elf64_hdr {
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

struct neverc_krt_elf64_phdr {
	u32 p_type;
	u32 p_flags;
	u64 p_offset;
	u64 p_vaddr;
	u64 p_paddr;
	u64 p_filesz;
	u64 p_memsz;
	u64 p_align;
};

struct neverc_krt_elf_load_info {
	unsigned long base;
	unsigned long entry;
	int           num_segments;
	unsigned long total_size;
};

int neverc_krt_inject_elf(struct task_struct *task,
			  const void *elf_data, size_t elf_len,
			  struct neverc_krt_elf_load_info *info);


#endif /* NEVERC_KRT_INJECT_H */
