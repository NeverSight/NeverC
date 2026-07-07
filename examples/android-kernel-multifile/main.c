/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multi-file NeverC kernel module — demonstrates that NEVERC_KRT_BOOTSTRAP()
 * only needs to be called once in module_init.
 *
 * The NeverC compiler automatically promotes all neverc_krt_* / _neverc_krt_* static
 * state to weak_odr linkage, so interposes.c and utils.c share the same
 * resolver, cache, and subsystem state as main.c.
 */
#include <nvkmod.h>
#include <nvk_interpose.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_cred.h>

#define NEVERC_KRT_LOG_TAG "neverc_krt_multi"
#include <nvk_log.h>

/* Defined in interposes.c */
int interposes_init(void);
void interposes_cleanup(void);

/* Defined in utils.c */
void utils_log_kernel_info(void);

static int neverc_krt_multi_init(void)
{
	int ret;

	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret) return ret;

	neverc_krt_log_info("init on %s\n", NEVERC_KRT_KERNEL_STR);

	neverc_krt_mem_init();
	neverc_krt_process_init();
	neverc_krt_cred_init();

	ret = neverc_krt_interpose_init();
	if (ret) {
		neverc_krt_log_err("interpose init failed: %d\n", ret);
		return ret;
	}

	ret = interposes_init();
	if (ret) {
		neverc_krt_log_err("interposes_init failed: %d\n", ret);
		return ret;
	}

	utils_log_kernel_info();

	neverc_krt_log_info("loaded successfully\n");
	return 0;
}

static void neverc_krt_multi_exit(void)
{
	interposes_cleanup();
	neverc_krt_log_info("unloaded\n");
}

module_init(neverc_krt_multi_init);
module_exit(neverc_krt_multi_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("Multi-file NeverC kernel module demo");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_multi");
