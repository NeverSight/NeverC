/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeverC probe demo — hook any instruction at any address.
 *
 * Demonstrates neverc_krt_probe_register:
 *  - Hook an instruction inside do_faccessat (not the entry)
 *  - Read/modify registers via neverc_krt_reg_ctx
 *  - Multiple handlers auto-chained on the same address
 *  - force_jump to skip execution
 */
#include <nvkmod.h>
#include <nvk_hook.h>
#include <nvk_mem.h>
#include <linux/string.h>
#include <linux/sched.h>

#define NEVERC_KRT_LOG_TAG "neverc_krt_probe_demo"
#include <nvk_log.h>

typedef int (*task_pid_nr_ns_fn)(struct task_struct *, int, void *);
static task_pid_nr_ns_fn fn_task_pid_nr_ns;

static int get_pid(void)
{
	if (fn_task_pid_nr_ns)
		return fn_task_pid_nr_ns(current, 0, (void *)0);
	return -1;
}

static struct neverc_krt_probe_ref probe_ref_log;
static struct neverc_krt_probe_ref probe_ref_filter;

/*
 * Handler A (priority 10): log register state at the probe point.
 */
static void probe_log(neverc_krt_reg_ctx *ctx)
{
	int pid = get_pid();
	neverc_krt_log_ratelimit("pid=%d X0=0x%lx X1=0x%lx LR=0x%lx\n",
				 pid,
				 (unsigned long)NEVERC_KRT_CTX_X0(ctx),
				 (unsigned long)NEVERC_KRT_CTX_X1(ctx),
				 (unsigned long)NEVERC_KRT_CTX_LR(ctx));
}

/*
 * Handler B (priority 20): skip execution if X2 == 0x1234.
 */
static void probe_filter(neverc_krt_reg_ctx *ctx)
{
	if (NEVERC_KRT_CTX_X(ctx, 2) == 0x1234) {
		neverc_krt_log_ratelimit("mode=0x1234 detected, skipping\n");
		NEVERC_KRT_CTX_SKIP(ctx, -1);
	}
}

/*
 * Find a probeable instruction after the function prologue.
 */
static void *find_probe_point(void *fn)
{
	u32 buf[16];
	int i;

	fn = (void *)neverc_krt_strip_pac((unsigned long)fn);
	if (neverc_krt_mem_read(buf, fn, sizeof(buf)))
		return (void *)0;

	for (i = 0; i < 12; i++) {
		u32 insn = buf[i];
		if ((insn & 0xFFFFF01F) == 0xD503201F)  continue; /* HINT */
		if (insn == 0xD503201F)                  continue; /* NOP  */
		if ((insn & 0xFFC07FFF) == 0xA9807BFD)  continue; /* STP x29,x30 */
		if ((insn & 0xFFC07FFF) == 0xA9007BFD)  continue; /* STP signed  */
		if ((insn & 0xFF8003FF) == 0xD10003FF)  continue; /* SUB SP      */
		if ((insn & 0xFFFFFC1F) == 0x910003FD)  continue; /* MOV x29,SP  */
		if ((insn & 0xFFE0FFE0) == 0xAA0003E0)  continue; /* MOV Xd,Xn   */
		return (void *)((unsigned long)fn + i * 4);
	}
	return (void *)((unsigned long)fn + 6 * 4);
}

static int neverc_krt_probe_demo_init(void)
{
	void *target, *probe_point;
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

	probe_point = find_probe_point(target);
	if (!probe_point) {
		neverc_krt_log_err("no probeable instruction found\n");
		return -1;
	}

	neverc_krt_log_info("target=%lx probe=%lx (offset +%ld)\n",
			    (unsigned long)target,
			    (unsigned long)probe_point,
			    (long)((unsigned long)probe_point - (unsigned long)target));

	ret = neverc_krt_probe_register(probe_point, probe_log, 10, &probe_ref_log);
	if (ret) {
		neverc_krt_log_err("probe_register(log) failed: %d\n", ret);
		return ret;
	}

	ret = neverc_krt_probe_register(probe_point, probe_filter, 20, &probe_ref_filter);
	if (ret) {
		neverc_krt_log_err("probe_register(filter) failed: %d\n", ret);
		neverc_krt_probe_unregister(&probe_ref_log);
		return ret;
	}

	neverc_krt_log_info("probes installed: %d handlers\n",
			    neverc_krt_probe_count(probe_point));
	return 0;
}

static void neverc_krt_probe_demo_exit(void)
{
	neverc_krt_probe_unregister(&probe_ref_filter);
	neverc_krt_probe_unregister(&probe_ref_log);
	neverc_krt_log_info("unloaded\n");
}

module_init(neverc_krt_probe_demo_init);
module_exit(neverc_krt_probe_demo_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC probe demo — arbitrary-instruction hook");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_probe_demo");
