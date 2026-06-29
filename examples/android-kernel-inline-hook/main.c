/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeverC function-hook demo.
 *
 * Demonstrates neverc_krt_hook_register:
 *  - Hook do_faccessat at function entry
 *  - Call the original, log the result
 *  - Multiple handlers auto-chained by priority
 */
#include <nvkmod.h>
#include <nvk_hook.h>
#include <nvk_mem.h>
#include <linux/string.h>
#include <linux/sched.h>

#define NEVERC_KRT_LOG_TAG "neverc_krt_hook_demo"
#include <nvk_log.h>

typedef int (*task_pid_nr_ns_fn)(struct task_struct *, int, void *);
static task_pid_nr_ns_fn fn_task_pid_nr_ns;

static int get_pid(void)
{
	if (fn_task_pid_nr_ns)
		return fn_task_pid_nr_ns(current, 0, (void *)0);
	return -1;
}

/* ----------------------------------------------------------------
 * Hook handler: logs faccessat calls and forwards to original.
 * ---------------------------------------------------------------- */

static struct neverc_krt_hook_ref faccessat_ref;
static void *orig_faccessat;

static long hook_faccessat(void *orig, void *a0, void *a1,
			   void *a2, void *a3, void *a4, void *a5)
{
	typedef long (*fn_t)(void *, void *, void *, void *, void *, void *);
	char buf[128];
	long ret;
	int pid;
	const char __user *filename = (const char __user *)a1;

	{
		int i;
		for (i = 0; i < (int)sizeof(buf); i++) buf[i] = 0;
	}
	if (filename)
		neverc_krt_mem_read_user(buf, filename, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	pid = get_pid();
	ret = ((fn_t)orig)(a0, a1, a2, a3, a4, a5);

	neverc_krt_log_ratelimit("pid=%d faccessat(%s) = %ld\n", pid, buf, ret);
	return ret;
}

/* ---------------------------------------------------------------- */

static int neverc_krt_hook_demo_init(void)
{
	void *target;
	int ret;

	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret)
		return ret;

	neverc_krt_log_info("init on %s\n", NEVERC_KRT_KERNEL_STR);
	neverc_krt_mem_init();

	ret = neverc_krt_hook_init();
	if (ret) {
		neverc_krt_log_err("hook_init failed: %d\n", ret);
		return ret;
	}

	fn_task_pid_nr_ns = (task_pid_nr_ns_fn)NEVERC_KRT_LOOKUP("__task_pid_nr_ns");

	target = NEVERC_KRT_LOOKUP("do_faccessat");
	if (!target) {
		neverc_krt_log_err("do_faccessat not found\n");
		return -1;
	}

	ret = neverc_krt_hook_register(target, (void *)hook_faccessat,
				       0, &orig_faccessat, &faccessat_ref);
	if (ret) {
		neverc_krt_log_err("hook_register failed: %d\n", ret);
		return ret;
	}

	neverc_krt_log_info("hooked do_faccessat (priority=0)\n");
	return 0;
}

static void neverc_krt_hook_demo_exit(void)
{
	neverc_krt_hook_unregister(&faccessat_ref);
	neverc_krt_log_info("unloaded\n");
}

module_init(neverc_krt_hook_demo_init);
module_exit(neverc_krt_hook_demo_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC function-hook demo");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_hook_demo");
