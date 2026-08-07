// SPDX-License-Identifier: GPL-2.0
/* Compile-only coverage for configuration-dependent public SDK layouts. */

#include <asm/page.h>
#include <asm/pgtable-types.h>
#include <asm/ptrace.h>
#include <asm/smp.h>
#include <linux/cpumask.h>
#include <linux/hrtimer.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/preempt.h>
#include <linux/regmap.h>
#include <linux/rwsem.h>
#include <linux/semaphore.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

_Static_assert(WORK_DATA_INIT() == WORK_STRUCT_NO_POOL,
	       "work_struct initializer must encode the no-pool sentinel");
_Static_assert(NR_CPUS == NEVERC_KRT_NR_CPUS,
	       "cpumask NR_CPUS must match the selected GKI profile");
_Static_assert(PAGE_SHIFT == NEVERC_KRT_PAGE_SHIFT,
	       "page size must match the selected GKI profile");
_Static_assert(NEVERC_KRT_VA_BITS == 39,
	       "official arm64 GKI profiles use 39-bit virtual addresses");
_Static_assert(NEVERC_KRT_PA_BITS == 48,
	       "official arm64 GKI profiles use 48-bit physical addresses");
_Static_assert(NEVERC_KRT_PGTABLE_LEVELS == 3,
	       "official arm64 GKI profiles use three page-table levels");
_Static_assert(sizeof(pgprot_t) == sizeof(u64),
	       "arm64 pgprot_t must remain one 64-bit descriptor");
_Static_assert(__builtin_offsetof(pgprot_t, pgprot) == 0,
	       "arm64 pgprot_t descriptor must remain at offset zero");
#if NEVERC_KRT_LINUX_BEFORE(6, 6, 0)
_Static_assert(GFP_ATOMIC == 0xA20,
	       "pre-6.6 GFP_ATOMIC flags must match GKI");
#else
_Static_assert(GFP_ATOMIC == 0x820,
	       "6.6+ GFP_ATOMIC must not set the removed atomic bit");
#endif
#if NEVERC_KRT_LINUX_AT_LEAST(6, 12, 0)
_Static_assert(GFP_NOWAIT == 0x2800,
	       "6.12+ GFP_NOWAIT must include __GFP_NOWARN");
#else
_Static_assert(GFP_NOWAIT == 0x800,
	       "pre-6.12 GFP_NOWAIT flags must match GKI");
#endif
_Static_assert(GFP_USER == 0x100CC0,
	       "GFP_USER must include the GKI hardwall bit");
#if NEVERC_KRT_LINUX_AT_LEAST(6, 12, 0)
_Static_assert(NEVERC_KRT_TASK_CPU == 40,
	       "6.12+ task_struct.thread_info.cpu offset must match GKI");
#endif

static int nvk_test_pm_callback(struct device *device)
{
	(void)device;
	return 0;
}

static unsigned int nvk_test_nf_hook(void *private_data, struct sk_buff *skb,
				     const struct nf_hook_state *state)
{
	(void)private_data;
	(void)skb;
	return state->hook == NF_INET_LOCAL_IN ? NF_ACCEPT : NF_DROP;
}

static void nvk_test_work_callback(struct work_struct *work)
{
	(void)work;
}

static enum hrtimer_restart nvk_test_hrtimer_callback(struct hrtimer *timer)
{
	(void)timer;
	return HRTIMER_NORESTART;
}

static const struct dev_pm_ops nvk_test_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(nvk_test_pm_callback, nvk_test_pm_callback)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(nvk_test_pm_callback, nvk_test_pm_callback)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(nvk_test_pm_callback, nvk_test_pm_callback)
	SET_RUNTIME_PM_OPS(nvk_test_pm_callback, nvk_test_pm_callback,
			   nvk_test_pm_callback)
};

static const struct regmap_config nvk_test_regmap_config = {
	.name = "nvk-layout-test",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = 0x1000,
	.reg_defaults = (const void *)0,
	.num_reg_defaults = 0,
};

static const struct nf_hook_ops nvk_test_nf_ops = {
	.hook = nvk_test_nf_hook,
	.dev = (struct net_device *)0,
	.priv = (void *)0,
	.pf = 2,
	.hook_ops_type = NF_HOOK_OP_UNDEFINED,
	.hooknum = NF_INET_LOCAL_IN,
	.priority = NF_INET_PRI_FILTER,
};

int nvk_test_sdk_layouts(void)
{
	struct delayed_work delayed;
	struct semaphore semaphore;
	struct mutex mutex;
	struct rw_semaphore rwsem;
	struct hrtimer hrtimer = { .function = nvk_test_hrtimer_callback };
	struct resource resource = { .start = 0, .end = 1 };
	struct pt_regs regs = { 0 };
	struct kprobe kprobe = { 0 };
	struct netlink_kernel_cfg netlink = { 0 };
	struct idr idr;
	struct tasklet_struct tasklet = { 0 };
	struct attribute_group sysfs_group = { .attrs = (void *)0 };
	cpumask_t cpumask = { 0 };
	void *mapping;
	int cpu;
	int saved_preempt_count;

	mapping = ioremap((phys_addr_t)0, PAGE_SIZE);
	saved_preempt_count = preempt_count();
	preempt_disable();
	preempt_enable();
	INIT_DELAYED_WORK(&delayed, nvk_test_work_callback);
	sema_init(&semaphore, 1);
	mutex_init(&mutex);
	init_rwsem(&rwsem);
	idr_init(&idr);
	for_each_online_cpu(cpu) {
		if (cpumask_test_cpu((unsigned int)cpu, &cpumask))
			break;
	}

	return nvk_test_pm_ops.runtime_suspend((struct device *)0) +
	       (int)nvk_test_regmap_config.reg_bits +
	       (int)nvk_test_nf_ops.hooknum + (int)resource.end +
	       (int)regs.syscallno + (int)kprobe.offset +
	       (int)netlink.groups + (int)hrtimer.function(&hrtimer) +
	       (int)sizeof(tasklet) + (int)idr.__idr_base +
	       (sysfs_group.attrs == (void *)0) + (mapping == (void *)0) +
	       (int)sizeof(cpumask) + saved_preempt_count;
}
