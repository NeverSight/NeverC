/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeverC Android kernel inline-hook demo (GKI .ko).
 *
 * Hooks do_faccessat via arm64 instruction patching using <nvk_hook.h>:
 *   - Automatic BTI/PAC preamble handling
 *   - PC-relative instruction relocation (ADRP, B, CBZ, etc.)
 *   - Short-range (B) and long-range (LDR+BR) patching
 *   - Trampoline in module region for calling the original
 *
 * Build:  make                    (or: make KERNEL=601 etc.)
 * Deploy: adb push nvk_inline_hook.ko /data/local/tests/
 *         adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
 *         adb shell su -c 'dmesg | grep nvk_inline'
 *         adb shell su -c 'rmmod nvk_inline_hook'
 */
#include <nvkmod.h>
#include <nvk_hook.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

/* ---- hook target: do_faccessat ---------------------------------------- */

typedef long (*faccessat_fn)(int dfd, const char __user *filename,
			     int mode, int flags);

static struct nvk_hook faccessat_hook;
static faccessat_fn orig_do_faccessat;

typedef unsigned long (*copy_from_user_fn)(void *to, const void __user *from,
					   unsigned long n);
typedef int (*task_pid_nr_fn)(struct task_struct *);

static copy_from_user_fn fn_copy_from_user;
static task_pid_nr_fn    fn_task_pid_nr;

static long hook_do_faccessat(int dfd, const char __user *filename,
			      int mode, int flags)
{
	char buf[128];
	long ret;
	int pid = -1;

	buf[0] = '\0';
	if (fn_copy_from_user && filename) {
		fn_copy_from_user(buf, filename, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
	}

	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	ret = orig_do_faccessat(dfd, filename, mode, flags);

	pr_info("nvk_inline: pid=%d faccessat(%s, %d) = %ld\n",
		pid, buf, mode, ret);
	return ret;
}

/* ---- module init / exit ----------------------------------------------- */

static int nvk_inline_hook_init(void)
{
	void *target;
	int ret;

	ret = NVK_BOOTSTRAP();
	if (ret)
		return ret;

	pr_info("nvk_inline: init on %s\n", NVK_KERNEL_STR);

	ret = nvk_hook_init();
	if (ret) {
		pr_info("nvk_inline: nvk_hook_init failed\n");
		return ret;
	}

	fn_copy_from_user =
		(copy_from_user_fn)NVK_LOOKUP("_copy_from_user");
	if (!fn_copy_from_user)
		fn_copy_from_user =
			(copy_from_user_fn)NVK_LOOKUP("raw_copy_from_user");
	fn_task_pid_nr = (task_pid_nr_fn)NVK_LOOKUP("task_pid_nr");

	target = NVK_LOOKUP("do_faccessat");
	if (!target) {
		pr_info("nvk_inline: do_faccessat not found\n");
		return -1;
	}

	pr_info("nvk_inline: do_faccessat @ %lx, entry[0..3] = "
		"%08x %08x %08x %08x\n",
		(unsigned long)target,
		((u32 *)target)[0], ((u32 *)target)[1],
		((u32 *)target)[2], ((u32 *)target)[3]);

	ret = nvk_hook_install(&faccessat_hook, target,
			       (void *)hook_do_faccessat,
			       (void **)&orig_do_faccessat);
	if (ret) {
		pr_info("nvk_inline: hook install failed: %d\n", ret);
		return ret;
	}

	pr_info("nvk_inline: hooked (patched %d insns, trampoline @ %lx)\n",
		faccessat_hook.patch_count,
		(unsigned long)faccessat_hook.trampoline);
	return 0;
}

static void nvk_inline_hook_exit(void)
{
	nvk_hook_remove(&faccessat_hook);
	pr_info("nvk_inline: unloaded\n");
}

module_init(nvk_inline_hook_init);
module_exit(nvk_inline_hook_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC Android kernel inline-hook demo (do_faccessat)");

NVK_DEFINE_MODULE("nvk_inline_hook");
