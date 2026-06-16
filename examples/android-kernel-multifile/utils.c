/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Utility functions for the multi-file module.
 *
 * Demonstrates that NEVERC_KRT_LOOKUP works from any TU without a local
 * NEVERC_KRT_BOOTSTRAP() call — the shared resolver state is already
 * initialised by module_init in main.c.
 */
#include <nvkmod.h>
#include <nvk_process.h>
#include <nvk_addr.h>
#include <nvk_compat.h>
#include <nvk_cpu.h>

#define NEVERC_KRT_LOG_TAG "neverc_krt_multi"
#include <nvk_log.h>

void utils_log_kernel_info(void)
{
	const struct neverc_krt_kernel_info *ki = neverc_krt_kernel_version();

	neverc_krt_log_info("kernel %u.%u.%u android%u  VA=%llu PAGE=%llu\n",
		     ki->major, ki->minor, ki->patch,
		     ki->android_version,
		     (unsigned long long)neverc_krt_va_bits(),
		     (unsigned long long)neverc_krt_page_size());

	neverc_krt_log_info("PAC=%d BTI=%d MTE=%d\n",
		     neverc_krt_has_pac(), neverc_krt_has_bti(), neverc_krt_has_mte());
}
