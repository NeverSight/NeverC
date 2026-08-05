/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Loader-offset smoke module.  Deliberately do not call the NeverC runtime or
 * any kernel API: QEMU only proves that the pinned GKI loader finds and calls
 * the init/exit pointers at the configured struct module offsets.  The build
 * helper also writes the release-derived KCFI type-id word immediately before
 * each entry symbol on profiles whose pinned module carries that entry ABI.
 */
#include <nvkmod.h>

int init_module(void)
{
	return 0;
}

void cleanup_module(void)
{
}

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NeverC GKI loader offset smoke module");
NEVERC_KRT_DEFINE_MODULE("neverc_gki_smoke");
