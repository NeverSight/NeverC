/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <nvk_hook.h>
#include <nvk_mem.h>
#include <linux/string.h>
#include <linux/sched.h>

#define NVK_LOG_TAG "nvk_inline"
#include <nvk_log.h>

typedef int (*task_pid_nr_fn)(struct task_struct *);
static task_pid_nr_fn fn_task_pid_nr;

static void resolve_helpers(void)
{
	fn_task_pid_nr = (task_pid_nr_fn)NVK_LOOKUP("task_pid_nr");
}

#ifdef NVK_CONTEXT_HOOK

static struct nvk_hook_ctx faccessat_hook_ctx;

static void hook_faccessat_ctx(nvk_reg_ctx *ctx)
{
	char buf[128];
	int pid = -1;

	const char __user *filename = (const char __user *)ctx->regs[1];
	int mode = (int)ctx->regs[2];

	buf[0] = '\0';
	if (filename)
		nvk_mem_read_user(buf, filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	nvk_log_ratelimit("[ctx] pid=%d faccessat(%s, mode=%d) "
			  "x0=%lx x1=%lx x2=%lx x3=%lx\n",
			  pid, buf, mode,
			  (unsigned long)ctx->regs[0],
			  (unsigned long)ctx->regs[1],
			  (unsigned long)ctx->regs[2],
			  (unsigned long)ctx->regs[3]);
}

static int hook_init(void *target)
{
	int ret = nvk_hook_install_ctx(&faccessat_hook_ctx, target,
				       hook_faccessat_ctx, (void *)0);
	if (ret) {
		nvk_log_err("ctx hook failed: %d\n", ret);
		return ret;
	}
	nvk_log_info("[ctx] hooked (patched %d insns)\n",
		     faccessat_hook_ctx.base.patch_count);
	return 0;
}

static void hook_exit(void)
{
	nvk_hook_remove_ctx(&faccessat_hook_ctx);
}

#else

typedef long (*faccessat_fn)(int dfd, const char __user *filename,
			     int mode, int flags);

static struct nvk_hook faccessat_hook;
static faccessat_fn orig_do_faccessat;

static long hook_do_faccessat(int dfd, const char __user *filename,
			      int mode, int flags)
{
	char buf[128];
	long ret;
	int pid = -1;

	buf[0] = '\0';
	if (filename)
		nvk_mem_read_user(buf, filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	ret = orig_do_faccessat(dfd, filename, mode, flags);

	nvk_log_ratelimit("pid=%d faccessat(%s, %d) = %ld\n",
			  pid, buf, mode, ret);
	return ret;
}

static int hook_init(void *target)
{
	int ret;

	nvk_log_dbg("do_faccessat @ %lx, entry[0..3] = "
		     "%08x %08x %08x %08x\n",
		     (unsigned long)target,
		     ((u32 *)target)[0], ((u32 *)target)[1],
		     ((u32 *)target)[2], ((u32 *)target)[3]);

	ret = nvk_hook_install(&faccessat_hook, target,
			       (void *)hook_do_faccessat,
			       (void **)&orig_do_faccessat);
	if (ret) {
		nvk_log_err("hook install failed: %d\n", ret);
		return ret;
	}

	nvk_log_info("hooked (patched %d insns, trampoline @ %lx)\n",
		     faccessat_hook.patch_count,
		     (unsigned long)faccessat_hook.trampoline);
	return 0;
}

static void hook_exit(void)
{
	nvk_hook_remove(&faccessat_hook);
}

#endif /* NVK_CONTEXT_HOOK */

static int nvk_inline_hook_init(void)
{
	void *target;
	int ret;

	ret = NVK_BOOTSTRAP();
	if (ret)
		return ret;

	nvk_log_info("init on %s\n", NVK_KERNEL_STR);

	nvk_mem_init();

	ret = nvk_hook_init();
	if (ret) {
		nvk_log_err("nvk_hook_init failed\n");
		return ret;
	}

	resolve_helpers();

	target = NVK_LOOKUP("do_faccessat");
	if (!target) {
		nvk_log_err("do_faccessat not found\n");
		return -1;
	}

	return hook_init(target);
}

static void nvk_inline_hook_exit(void)
{
	hook_exit();
	nvk_log_info("unloaded\n");
}

module_init(nvk_inline_hook_init);
module_exit(nvk_inline_hook_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC inline-hook demo (do_faccessat)");

NVK_DEFINE_MODULE("nvk_inline_hook");
