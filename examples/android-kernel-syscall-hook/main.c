/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
#include <nvk_mem.h>
#include <nvk_syscall.h>

#define NEVERC_KRT_LOG_TAG "neverc_krt_syscall"
#include <nvk_log.h>

#if defined(NEVERC_KRT_SYSCALL_INLINE_HOOK) || defined(NEVERC_KRT_SYSCALL_CONTEXT_HOOK)
#include <nvk_hook.h>
#endif

typedef int (*task_pid_nr_fn)(struct task_struct *);
static task_pid_nr_fn fn_task_pid_nr;

static void resolve_common(void)
{
	fn_task_pid_nr = (task_pid_nr_fn)NEVERC_KRT_LOOKUP("task_pid_nr");
}

#ifdef NEVERC_KRT_SYSCALL_CONTEXT_HOOK

static struct neverc_krt_hook_ctx openat_ctx;

static void hook_sys_openat_ctx(neverc_krt_reg_ctx *ctx)
{
	char buf[256];
	int pid = -1;
	const struct pt_regs *regs =
		(const struct pt_regs *)NEVERC_KRT_CTX_ARG(ctx, 0);
	const char __user *user_filename =
		(const char __user *)regs->regs[1];

	buf[0] = '\0';
	if (user_filename)
		neverc_krt_mem_read_user(buf, user_filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	neverc_krt_log_ratelimit("[ctx] pid=%d openat(%s)\n", pid, buf);
}

static int hook_init(void)
{
	void *target;
	int ret;

	ret = neverc_krt_hook_init();
	if (ret) {
		neverc_krt_log_err("neverc_krt_hook_init failed\n");
		return ret;
	}

	target = NEVERC_KRT_LOOKUP("__arm64_sys_openat");
	if (!target) {
		neverc_krt_log_err("__arm64_sys_openat not found\n");
		return -1;
	}

	ret = neverc_krt_hook_install_ctx(&openat_ctx, target,
				    hook_sys_openat_ctx, (void *)0);
	if (ret) {
		neverc_krt_log_err("ctx hook failed: %d\n", ret);
		return ret;
	}

	neverc_krt_log_info("[ctx] openat hooked (patched %d insns)\n",
		     openat_ctx.base.patch_count);
	return 0;
}

static void hook_exit(void)
{
	neverc_krt_hook_remove_ctx(&openat_ctx);
}

#elif defined(NEVERC_KRT_SYSCALL_INLINE_HOOK)

static struct neverc_krt_hook openat_hook;
static neverc_krt_syscall_fn_t orig_sys_openat;

static long hook_sys_openat(const struct pt_regs *regs)
{
	char buf[256];
	long ret;
	int pid = -1;
	const char __user *user_filename = (const char __user *)regs->regs[1];

	buf[0] = '\0';
	if (user_filename)
		neverc_krt_mem_read_user(buf, user_filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	ret = orig_sys_openat(regs);

	neverc_krt_log_ratelimit("[inline] pid=%d openat(%s) = %ld\n", pid, buf, ret);
	return ret;
}

static int hook_init(void)
{
	void *target;
	int ret;

	ret = neverc_krt_hook_init();
	if (ret) {
		neverc_krt_log_err("neverc_krt_hook_init failed\n");
		return ret;
	}

	target = NEVERC_KRT_LOOKUP("__arm64_sys_openat");
	if (!target) {
		neverc_krt_log_err("__arm64_sys_openat not found\n");
		return -1;
	}

	neverc_krt_log_dbg("__arm64_sys_openat @ %lx\n", (unsigned long)target);

	ret = neverc_krt_hook_install(&openat_hook, target,
			       (void *)hook_sys_openat,
			       (void **)&orig_sys_openat);
	if (ret) {
		neverc_krt_log_err("inline hook failed: %d\n", ret);
		return ret;
	}

	neverc_krt_log_info("[inline] openat hooked (patched %d insns)\n",
		     openat_hook.patch_count);
	return 0;
}

static void hook_exit(void)
{
	neverc_krt_hook_remove(&openat_hook);
}

#else /* sys_call_table replacement via neverc_krt_syscall.h */

static neverc_krt_syscall_fn_t orig_openat;

static long hook_openat(const struct pt_regs *regs)
{
	char buf[256];
	long ret;
	int pid = -1;
	const char __user *user_filename = (const char __user *)regs->regs[1];

	buf[0] = '\0';
	if (user_filename)
		neverc_krt_mem_read_user(buf, user_filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	ret = orig_openat(regs);

	neverc_krt_log_ratelimit("[table] pid=%d openat(%s) = %ld\n", pid, buf, ret);
	return ret;
}

static int hook_init(void)
{
	neverc_krt_mem_init();

	int ret = neverc_krt_syscall_init();
	if (ret) {
		neverc_krt_log_err("syscall init failed\n");
		return ret;
	}

	neverc_krt_log_dbg("sys_call_table @ %lx\n",
		     (unsigned long)neverc_krt_syscall_table());

	ret = neverc_krt_syscall_replace(__NR_openat,
				  (neverc_krt_syscall_fn_t)hook_openat, &orig_openat);
	if (ret) {
		neverc_krt_log_err("syscall replace failed: %d\n", ret);
		return ret;
	}

	neverc_krt_log_info("[table] openat hooked\n");
	return 0;
}

static void hook_exit(void)
{
	if (orig_openat)
		neverc_krt_syscall_restore(__NR_openat, orig_openat);
}

#endif /* NEVERC_KRT_SYSCALL_CONTEXT_HOOK / NEVERC_KRT_SYSCALL_INLINE_HOOK */

static int neverc_krt_syscall_hook_init(void)
{
	int ret;

	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret)
		return ret;

	neverc_krt_log_info("init on %s\n", NEVERC_KRT_KERNEL_STR);
	resolve_common();

	return hook_init();
}

static void neverc_krt_syscall_hook_exit(void)
{
	hook_exit();
	neverc_krt_log_info("unloaded\n");
}

module_init(neverc_krt_syscall_hook_init);
module_exit(neverc_krt_syscall_hook_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC syscall-hook demo (openat)");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_syscall_hook");
