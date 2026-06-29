/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeverC syscall-hook demo.
 *
 * Three modes (compile-time selection):
 *  1. NEVERC_KRT_SYSCALL_CONTEXT_HOOK — probe at function entry (ctx mode)
 *  2. NEVERC_KRT_SYSCALL_INLINE_HOOK  — neverc_krt_hook_register (auto-chain)
 *  3. default                         — sys_call_table pointer replacement
 */
#include <nvkmod.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
#include <nvk_hook.h>
#include <nvk_mem.h>
#include <nvk_syscall.h>

#define NEVERC_KRT_LOG_TAG "neverc_krt_syscall"
#include <nvk_log.h>

typedef int (*task_pid_nr_ns_fn)(struct task_struct *, int, void *);
static task_pid_nr_ns_fn fn_task_pid_nr_ns;

static int get_pid(void)
{
	if (fn_task_pid_nr_ns)
		return fn_task_pid_nr_ns(current, 0, (void *)0);
	return -1;
}

static void resolve_common(void)
{
	fn_task_pid_nr_ns = (task_pid_nr_ns_fn)NEVERC_KRT_LOOKUP("__task_pid_nr_ns");
}

/* ================================================================
 * Mode 1: Probe at function entry — full register access, CTX_SKIP.
 * ================================================================ */
#ifdef NEVERC_KRT_SYSCALL_CONTEXT_HOOK

static struct neverc_krt_probe_ref openat_probe_ref;

static void hook_sys_openat_ctx(neverc_krt_reg_ctx *ctx)
{
	NEVERC_KRT_CTX_FP_GUARD_BEGIN;

	char buf[256];
	int pid;
	const struct pt_regs *regs =
		(const struct pt_regs *)NEVERC_KRT_CTX_ARG(ctx, 0);
	const char __user *user_filename =
		(const char __user *)regs->regs[1];

	{
		int i;
		for (i = 0; i < (int)sizeof(buf); i++) buf[i] = 0;
	}
	if (user_filename)
		neverc_krt_mem_read_user(buf, user_filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	pid = get_pid();
	neverc_krt_log_ratelimit("[ctx] pid=%d openat(%s)\n", pid, buf);

	NEVERC_KRT_CTX_FP_GUARD_END;
}

static int hook_init(void)
{
	void *target;
	int ret;

	ret = neverc_krt_hook_init();
	if (ret) {
		neverc_krt_log_err("hook_init failed\n");
		return ret;
	}

	target = NEVERC_KRT_LOOKUP("__arm64_sys_openat");
	if (!target) {
		neverc_krt_log_err("__arm64_sys_openat not found\n");
		return -1;
	}

	ret = neverc_krt_probe_register(target, hook_sys_openat_ctx,
					0, &openat_probe_ref);
	if (ret) {
		neverc_krt_log_err("probe_register failed: %d\n", ret);
		return ret;
	}

	neverc_krt_log_info("[ctx] openat hooked\n");
	return 0;
}

static void hook_exit(void)
{
	neverc_krt_probe_unregister(&openat_probe_ref);
}

/* ================================================================
 * Mode 2: hook_register — auto-chain, call-original pattern.
 * ================================================================ */
#elif defined(NEVERC_KRT_SYSCALL_INLINE_HOOK)

static struct neverc_krt_hook_ref openat_ref;
static void *orig_sys_openat;

static long hook_sys_openat(void *orig, void *a0, void *a1,
			    void *a2, void *a3, void *a4, void *a5)
{
	typedef long (*sys_fn_t)(void *, void *, void *, void *, void *, void *);
	char buf[256];
	long ret;
	int pid;
	const struct pt_regs *regs = (const struct pt_regs *)a0;
	const char __user *user_filename = (const char __user *)regs->regs[1];

	{
		int i;
		for (i = 0; i < (int)sizeof(buf); i++) buf[i] = 0;
	}
	if (user_filename)
		neverc_krt_mem_read_user(buf, user_filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	pid = get_pid();
	ret = ((sys_fn_t)orig)(a0, a1, a2, a3, a4, a5);

	neverc_krt_log_ratelimit("[register] pid=%d openat(%s) = %ld\n",
				 pid, buf, ret);
	return ret;
}

static int hook_init(void)
{
	void *target;
	int ret;

	ret = neverc_krt_hook_init();
	if (ret) {
		neverc_krt_log_err("hook_init failed\n");
		return ret;
	}

	target = NEVERC_KRT_LOOKUP("__arm64_sys_openat");
	if (!target) {
		neverc_krt_log_err("__arm64_sys_openat not found\n");
		return -1;
	}

	ret = neverc_krt_hook_register(target, (void *)hook_sys_openat,
				       0, &orig_sys_openat, &openat_ref);
	if (ret) {
		neverc_krt_log_err("hook_register failed: %d\n", ret);
		return ret;
	}

	neverc_krt_log_info("[register] openat hooked\n");
	return 0;
}

static void hook_exit(void)
{
	neverc_krt_hook_unregister(&openat_ref);
}

/* ================================================================
 * Mode 3: sys_call_table pointer replacement (no inline patching).
 * ================================================================ */
#else

static neverc_krt_syscall_fn_t orig_openat;

static long hook_openat(const struct pt_regs *regs)
{
	char buf[256];
	long ret;
	int pid;
	const char __user *user_filename = (const char __user *)regs->regs[1];

	{
		int i;
		for (i = 0; i < (int)sizeof(buf); i++) buf[i] = 0;
	}
	if (user_filename)
		neverc_krt_mem_read_user(buf, user_filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	pid = get_pid();
	ret = orig_openat(regs);

	neverc_krt_log_ratelimit("[table] pid=%d openat(%s) = %ld\n", pid, buf, ret);
	return ret;
}

static int hook_init(void)
{
	int ret;

	neverc_krt_mem_init();

	ret = neverc_krt_syscall_init();
	if (ret) {
		neverc_krt_log_err("syscall init failed\n");
		return ret;
	}

	neverc_krt_log_info("sys_call_table @ %lx\n",
			    (unsigned long)neverc_krt_syscall_table());

	ret = neverc_krt_syscall_replace(__NR_openat,
					 (neverc_krt_syscall_fn_t)hook_openat,
					 &orig_openat);
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

#endif

/* ================================================================ */

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
