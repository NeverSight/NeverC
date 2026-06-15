/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeverC Android kernel driver template (GKI .ko) with dynamic symbol
 * resolution.
 *
 * The driver imports only register_kprobe / unregister_kprobe (exported and
 * ABI-stable across all current GKI kernels) and resolves everything else at
 * runtime via kallsyms_lookup_name().  This keeps a single source / single
 * binary working across android12-5.10 ... android15-6.6.
 *
 * Build:  neverc make            (or: make ; make KERNEL=601 for 6.1, etc.)
 * Deploy: adb push nvk_driver.ko /data/local/tests/
 *         adb shell su -c 'insmod /data/local/tests/nvk_driver.ko'
 *         adb shell su -c 'dmesg | tail'
 *         adb shell su -c 'rmmod nvk_driver'
 */
#include <nvkmod.h>

static int nvk_driver_init(void)
{
	void *init_task_addr;
	int ret;

	/* Resolve kallsyms_lookup_name (via kprobe) and printk. */
	ret = NVK_BOOTSTRAP();
	if (ret) {
		/* Nothing resolved yet, so no printk available to report it. */
		return ret;
	}

	pr_info("nvk_driver: loaded on %s\n", NVK_KERNEL_STR);
	pr_info("nvk_driver: kallsyms_lookup_name @ %lx\n",
		(unsigned long)nvk_kallsyms_lookup_name);

	/* Example: dynamically resolve an arbitrary kernel symbol. */
	init_task_addr = NVK_LOOKUP("init_task");
	pr_info("nvk_driver: &init_task = %lx\n",
		(unsigned long)init_task_addr);

	return 0;
}

static void nvk_driver_exit(void)
{
	pr_info("nvk_driver: unloaded\n");
}

module_init(nvk_driver_init);
module_exit(nvk_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC Android kernel driver template (dynamic kallsyms)");

NVK_DEFINE_MODULE("nvk_driver");
