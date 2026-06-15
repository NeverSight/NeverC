/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeverC Android kernel syscall-hook demo (GKI .ko).
 *
 * Two hook methods (selectable at compile time):
 *
 *   Method A (default): sys_call_table pointer swap
 *     - Resolves sys_call_table via kallsyms
 *     - Makes the page writable via update_mapping_prot (kimage_voffset
 *       trick for PA) or falls back to set_memory_rw
 *     - Replaces __NR_openat with our hook
 *
 *   Method B (-DNVK_SYSCALL_INLINE_HOOK): inline hook
 *     - Resolves __arm64_sys_openat via kallsyms
 *     - Uses nvk_hook.h to patch the function entry
 *     - No page-protection hacking needed (more portable)
 *
 * Build:  make                    (default: method A)
 *         make EXTRA=-DNVK_SYSCALL_INLINE_HOOK   (method B)
 * Deploy: adb push nvk_syscall_hook.ko /data/local/tests/
 *         adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
 *         adb shell su -c 'dmesg | grep nvk_syscall'
 *         adb shell su -c 'rmmod nvk_syscall_hook'
 */
#include <nvkmod.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <asm/ptrace.h>

#ifdef NVK_SYSCALL_INLINE_HOOK
#include <nvk_hook.h>
#endif

/* arm64 syscall numbers (from <asm-generic/unistd.h>) */
#define __NR_openat 56

/* ---- common helpers --------------------------------------------------- */

typedef unsigned long (*copy_from_user_fn)(void *to, const void __user *from,
					   unsigned long n);
typedef int (*task_pid_nr_fn)(struct task_struct *);

static copy_from_user_fn fn_copy_from_user;
static task_pid_nr_fn    fn_task_pid_nr;

static void resolve_common(void)
{
	fn_copy_from_user =
		(copy_from_user_fn)NVK_LOOKUP("_copy_from_user");
	if (!fn_copy_from_user)
		fn_copy_from_user =
			(copy_from_user_fn)NVK_LOOKUP("raw_copy_from_user");
	fn_task_pid_nr = (task_pid_nr_fn)NVK_LOOKUP("task_pid_nr");
}

/* ===================================================================== */
/* Method B: inline hook on __arm64_sys_openat                           */
/* ===================================================================== */
#ifdef NVK_SYSCALL_INLINE_HOOK

typedef long (*sys_openat_fn)(const struct pt_regs *regs);

static struct nvk_hook openat_hook;
static sys_openat_fn   orig_sys_openat;

static long hook_sys_openat(const struct pt_regs *regs)
{
	char buf[256];
	long ret;
	int pid = -1;
	const char __user *user_filename = (const char __user *)regs->regs[1];

	if (fn_copy_from_user && user_filename) {
		fn_copy_from_user(buf, user_filename, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
	} else {
		buf[0] = '?';
		buf[1] = '\0';
	}
	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	ret = orig_sys_openat(regs);

	pr_info("nvk_syscall: [inline] pid=%d openat(%s) = %ld\n",
		pid, buf, ret);
	return ret;
}

static int hook_init(void)
{
	void *target;
	int ret;

	ret = nvk_hook_init();
	if (ret) {
		pr_info("nvk_syscall: nvk_hook_init failed\n");
		return ret;
	}

	target = NVK_LOOKUP("__arm64_sys_openat");
	if (!target) {
		pr_info("nvk_syscall: __arm64_sys_openat not found\n");
		return -1;
	}

	pr_info("nvk_syscall: __arm64_sys_openat @ %lx\n",
		(unsigned long)target);

	ret = nvk_hook_install(&openat_hook, target,
			       (void *)hook_sys_openat,
			       (void **)&orig_sys_openat);
	if (ret) {
		pr_info("nvk_syscall: inline hook failed: %d\n", ret);
		return ret;
	}

	pr_info("nvk_syscall: [inline] openat hooked (patched %d insns)\n",
		openat_hook.patch_count);
	return 0;
}

static void hook_exit(void)
{
	nvk_hook_remove(&openat_hook);
}

/* ===================================================================== */
/* Method A: sys_call_table pointer swap                                 */
/* ===================================================================== */
#else /* !NVK_SYSCALL_INLINE_HOOK */

typedef long (*syscall_fn_t)(const struct pt_regs *regs);

static syscall_fn_t *sys_call_table;
static syscall_fn_t  orig_openat;

/* ARM64 PTE bits for update_mapping_prot. */
#define A64_PTE_TYPE_PAGE (3UL << 0)
#define A64_PTE_AF        (1UL << 10)
#define A64_PTE_SH_IS     (3UL << 8)
#define A64_PTE_RDONLY    (1UL << 7)
#define A64_PTE_ATTRINDX(x) ((unsigned long)(x) << 2)
#define A64_PTE_UXN       (1UL << 54)
#define A64_PTE_PXN       (1UL << 53)

#define NVK_PAGE_KERNEL                                                       \
	(A64_PTE_TYPE_PAGE | A64_PTE_AF | A64_PTE_SH_IS |                    \
	 A64_PTE_ATTRINDX(0) | A64_PTE_UXN)
#define NVK_PAGE_KERNEL_RO (NVK_PAGE_KERNEL | A64_PTE_RDONLY)

typedef int  (*set_memory_fn)(unsigned long addr, int numpages);
typedef void (*update_mapping_prot_fn)(u64 phys, unsigned long virt,
				       u64 size, u64 prot);

static set_memory_fn           fn_set_memory_rw;
static set_memory_fn           fn_set_memory_ro;
static update_mapping_prot_fn  fn_update_prot;
static unsigned long          *p_kimage_voffset;

/*
 * make_rw / make_ro: try update_mapping_prot first (the approach most
 * Android root tools use), then fall back to set_memory_rw/ro.
 */
static int make_rw(unsigned long addr)
{
	unsigned long page = addr & ~0xFFFUL;

	if (fn_update_prot && p_kimage_voffset) {
		u64 phys = page - *p_kimage_voffset;
		fn_update_prot(phys, page, 0x1000, NVK_PAGE_KERNEL);
		return 0;
	}

	if (fn_set_memory_rw)
		return fn_set_memory_rw(page, 1);

	return -1;
}

static int make_ro(unsigned long addr)
{
	unsigned long page = addr & ~0xFFFUL;

	if (fn_update_prot && p_kimage_voffset) {
		u64 phys = page - *p_kimage_voffset;
		fn_update_prot(phys, page, 0x1000, NVK_PAGE_KERNEL_RO);
		return 0;
	}

	if (fn_set_memory_ro)
		return fn_set_memory_ro(page, 1);

	return -1;
}

static long hook_openat(const struct pt_regs *regs)
{
	char buf[256];
	long ret;
	int pid = -1;
	const char __user *user_filename = (const char __user *)regs->regs[1];

	if (fn_copy_from_user && user_filename) {
		fn_copy_from_user(buf, user_filename, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
	} else {
		buf[0] = '?';
		buf[1] = '\0';
	}
	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	ret = orig_openat(regs);

	pr_info("nvk_syscall: [table] pid=%d openat(%s) = %ld\n",
		pid, buf, ret);
	return ret;
}

static int hook_init(void)
{
	unsigned long sct_addr, entry_addr;
	int ret;

	fn_set_memory_rw = (set_memory_fn)NVK_LOOKUP("set_memory_rw");
	fn_set_memory_ro = (set_memory_fn)NVK_LOOKUP("set_memory_ro");
	fn_update_prot =
		(update_mapping_prot_fn)NVK_LOOKUP("update_mapping_prot");
	p_kimage_voffset =
		(unsigned long *)NVK_LOOKUP("kimage_voffset");

	pr_info("nvk_syscall: update_mapping_prot=%lx kimage_voffset=%lx "
		"set_memory_rw=%lx\n",
		(unsigned long)fn_update_prot,
		(unsigned long)p_kimage_voffset,
		(unsigned long)fn_set_memory_rw);

	sct_addr = (unsigned long)NVK_LOOKUP("sys_call_table");
	if (!sct_addr) {
		pr_info("nvk_syscall: sys_call_table not found\n");
		return -1;
	}
	sys_call_table = (syscall_fn_t *)sct_addr;
	pr_info("nvk_syscall: sys_call_table @ %lx\n", sct_addr);

	orig_openat = sys_call_table[__NR_openat];
	pr_info("nvk_syscall: original openat @ %lx\n",
		(unsigned long)orig_openat);

	entry_addr = (unsigned long)&sys_call_table[__NR_openat];
	ret = make_rw(entry_addr);
	if (ret) {
		pr_info("nvk_syscall: make_rw failed (%d), "
			"trying direct write\n", ret);
	}

	sys_call_table[__NR_openat] = hook_openat;
	make_ro(entry_addr);

	pr_info("nvk_syscall: [table] openat hooked\n");
	return 0;
}

static void hook_exit(void)
{
	unsigned long entry_addr;

	if (sys_call_table && orig_openat) {
		entry_addr = (unsigned long)&sys_call_table[__NR_openat];
		make_rw(entry_addr);
		sys_call_table[__NR_openat] = orig_openat;
		make_ro(entry_addr);
		pr_info("nvk_syscall: [table] openat restored\n");
	}
}

#endif /* NVK_SYSCALL_INLINE_HOOK */

/* ---- module init / exit ----------------------------------------------- */

static int nvk_syscall_hook_init(void)
{
	int ret;

	ret = NVK_BOOTSTRAP();
	if (ret)
		return ret;

	pr_info("nvk_syscall: init on %s\n", NVK_KERNEL_STR);
	resolve_common();

	return hook_init();
}

static void nvk_syscall_hook_exit(void)
{
	hook_exit();
	pr_info("nvk_syscall: unloaded\n");
}

module_init(nvk_syscall_hook_init);
module_exit(nvk_syscall_hook_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC Android kernel syscall-hook demo (openat)");

NVK_DEFINE_MODULE("nvk_syscall_hook");
