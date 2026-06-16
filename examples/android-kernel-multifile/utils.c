/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Utility functions for the multi-file module.
 *
 * Demonstrates that NVK_LOOKUP works from any TU without a local
 * NVK_BOOTSTRAP() call — the shared resolver state is already
 * initialised by module_init in main.c.
 */
#include <nvkmod.h>
#include <nvk_process.h>
#include <nvk_addr.h>
#include <nvk_compat.h>
#include <nvk_cpu.h>

#define NVK_LOG_TAG "nvk_multi"
#include <nvk_log.h>

void utils_log_kernel_info(void)
{
	const struct nvk_kernel_info *ki = nvk_kernel_version();

	nvk_log_info("kernel %u.%u.%u android%u  VA=%llu PAGE=%llu\n",
		     ki->major, ki->minor, ki->patch,
		     ki->android_version,
		     (unsigned long long)nvk_va_bits(),
		     (unsigned long long)nvk_page_size());

	nvk_log_info("PAC=%d BTI=%d MTE=%d\n",
		     nvk_has_pac(), nvk_has_bti(), nvk_has_mte());
}
