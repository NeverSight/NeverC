/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeverC syscall-hook demo — sys_call_table pointer replacement.
 *
 * Hooks __NR_openat by swapping the function pointer in sys_call_table.
 * This is the classic approach for syscall interception on ARM64 GKI.
 */
#include <nvkmod.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
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

	neverc_krt_log_ratelimit("pid=%d openat(%s) = %ld\n", pid, buf, ret);
	return ret;
}

static int neverc_krt_syscall_hook_init(void)
{
	int ret;

	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret)
		return ret;

	neverc_krt_log_info("init on %s\n", NEVERC_KRT_KERNEL_STR);

	fn_task_pid_nr_ns = (task_pid_nr_ns_fn)NEVERC_KRT_LOOKUP("__task_pid_nr_ns");

	neverc_krt_mem_init();

	ret = neverc_krt_syscall_init();
	if (ret) {
		neverc_krt_log_err("syscall init failed: %d\n", ret);
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

	neverc_krt_log_info("openat hooked\n");
	return 0;
}

static void neverc_krt_syscall_hook_exit(void)
{
	if (orig_openat)
		neverc_krt_syscall_restore(__NR_openat, orig_openat);
	neverc_krt_log_info("unloaded\n");
}

module_init(neverc_krt_syscall_hook_init);
module_exit(neverc_krt_syscall_hook_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC syscall-hook demo (sys_call_table)");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_syscall_hook");
