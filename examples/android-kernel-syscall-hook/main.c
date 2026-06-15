/* SPDX-License-Identifier: GPL-2.0 */
#include <nvkmod.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
#include <nvk_mem.h>
#include <nvk_syscall.h>

#define NVK_LOG_TAG "nvk_syscall"
#include <nvk_log.h>

#ifdef NVK_SYSCALL_INLINE_HOOK
#include <nvk_hook.h>
#endif

typedef int (*task_pid_nr_fn)(struct task_struct *);
static task_pid_nr_fn fn_task_pid_nr;

static void resolve_common(void)
{
	fn_task_pid_nr = (task_pid_nr_fn)NVK_LOOKUP("task_pid_nr");
}

#ifdef NVK_SYSCALL_INLINE_HOOK

static struct nvk_hook openat_hook;
static nvk_syscall_fn_t orig_sys_openat;

static long hook_sys_openat(const struct pt_regs *regs)
{
	char buf[256];
	long ret;
	int pid = -1;
	const char __user *user_filename = (const char __user *)regs->regs[1];

	buf[0] = '\0';
	if (user_filename)
		nvk_mem_read_user(buf, user_filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	ret = orig_sys_openat(regs);

	nvk_log_ratelimit("[inline] pid=%d openat(%s) = %ld\n", pid, buf, ret);
	return ret;
}

static int hook_init(void)
{
	void *target;
	int ret;

	ret = nvk_hook_init();
	if (ret) {
		nvk_log_err("nvk_hook_init failed\n");
		return ret;
	}

	target = NVK_LOOKUP("__arm64_sys_openat");
	if (!target) {
		nvk_log_err("__arm64_sys_openat not found\n");
		return -1;
	}

	nvk_log_dbg("__arm64_sys_openat @ %lx\n", (unsigned long)target);

	ret = nvk_hook_install(&openat_hook, target,
			       (void *)hook_sys_openat,
			       (void **)&orig_sys_openat);
	if (ret) {
		nvk_log_err("inline hook failed: %d\n", ret);
		return ret;
	}

	nvk_log_info("[inline] openat hooked (patched %d insns)\n",
		     openat_hook.patch_count);
	return 0;
}

static void hook_exit(void)
{
	nvk_hook_remove(&openat_hook);
}

#else /* sys_call_table replacement via nvk_syscall.h */

static nvk_syscall_fn_t orig_openat;

static long hook_openat(const struct pt_regs *regs)
{
	char buf[256];
	long ret;
	int pid = -1;
	const char __user *user_filename = (const char __user *)regs->regs[1];

	buf[0] = '\0';
	if (user_filename)
		nvk_mem_read_user(buf, user_filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	if (fn_task_pid_nr)
		pid = fn_task_pid_nr(current);

	ret = orig_openat(regs);

	nvk_log_ratelimit("[table] pid=%d openat(%s) = %ld\n", pid, buf, ret);
	return ret;
}

static int hook_init(void)
{
	nvk_mem_init();

	int ret = nvk_syscall_init();
	if (ret) {
		nvk_log_err("syscall init failed\n");
		return ret;
	}

	nvk_log_dbg("sys_call_table @ %lx\n",
		     (unsigned long)nvk_syscall_table());

	ret = nvk_syscall_replace(__NR_openat,
				  (nvk_syscall_fn_t)hook_openat, &orig_openat);
	if (ret) {
		nvk_log_err("syscall replace failed: %d\n", ret);
		return ret;
	}

	nvk_log_info("[table] openat hooked\n");
	return 0;
}

static void hook_exit(void)
{
	if (orig_openat)
		nvk_syscall_restore(__NR_openat, orig_openat);
}

#endif /* NVK_SYSCALL_INLINE_HOOK */

static int nvk_syscall_hook_init(void)
{
	int ret;

	ret = NVK_BOOTSTRAP();
	if (ret)
		return ret;

	nvk_log_info("init on %s\n", NVK_KERNEL_STR);
	resolve_common();

	return hook_init();
}

static void nvk_syscall_hook_exit(void)
{
	hook_exit();
	nvk_log_info("unloaded\n");
}

module_init(nvk_syscall_hook_init);
module_exit(nvk_syscall_hook_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC syscall-hook demo (openat)");

NVK_DEFINE_MODULE("nvk_syscall_hook");
