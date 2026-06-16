/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multi-file NeverC kernel module — demonstrates that NVK_BOOTSTRAP()
 * only needs to be called once in module_init.
 *
 * The NeverC compiler automatically promotes all nvk_* / _nvk_* static
 * state to weak_odr linkage, so hooks.c and utils.c share the same
 * resolver, cache, and subsystem state as main.c.
 */
#include <nvkmod.h>
#include <nvk_hook.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_cred.h>

#define NVK_LOG_TAG "nvk_multi"
#include <nvk_log.h>

/* Defined in hooks.c */
int hooks_init(void);
void hooks_cleanup(void);

/* Defined in utils.c */
void utils_log_kernel_info(void);

static int nvk_multi_init(void)
{
	int ret;

	ret = NVK_BOOTSTRAP();
	if (ret) return ret;

	nvk_log_info("init on %s\n", NVK_KERNEL_STR);

	nvk_mem_init();
	nvk_process_init();
	nvk_cred_init();

	ret = nvk_hook_init();
	if (ret) {
		nvk_log_err("hook init failed: %d\n", ret);
		return ret;
	}

	ret = hooks_init();
	if (ret) {
		nvk_log_err("hooks_init failed: %d\n", ret);
		return ret;
	}

	utils_log_kernel_info();

	nvk_log_info("loaded successfully\n");
	return 0;
}

static void nvk_multi_exit(void)
{
	hooks_cleanup();
	nvk_log_info("unloaded\n");
}

module_init(nvk_multi_init);
module_exit(nvk_multi_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("Multi-file NeverC kernel module demo");

NVK_DEFINE_MODULE("nvk_multi");
