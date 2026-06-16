/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hook installation for the multi-file module.
 *
 * This file does NOT call NEVERC_KRT_BOOTSTRAP() — the bootstrap was already
 * done in main.c's module_init.  Thanks to the compiler's automatic
 * linkage promotion, NEVERC_KRT_LOOKUP / kallsyms_lookup_name and all hook
 * infrastructure work seamlessly here.
 */
#include <nvkmod.h>
#include <nvk_hook.h>

typedef long (*faccessat_fn)(int dfd, const char __user *filename,
			     int mode, int flags);

static struct neverc_krt_hook faccessat_hook;
static faccessat_fn orig_do_faccessat;

static long hook_do_faccessat(int dfd, const char __user *filename,
			      int mode, int flags)
{
	if (!neverc_krt_hook_enter(&faccessat_hook))
		return orig_do_faccessat(dfd, filename, mode, flags);

	long ret = orig_do_faccessat(dfd, filename, mode, flags);
	neverc_krt_hook_leave(&faccessat_hook);
	return ret;
}

int hooks_init(void)
{
	void *target = NEVERC_KRT_LOOKUP("do_faccessat");
	if (!target)
		return -1;

	return neverc_krt_hook_install(&faccessat_hook, target,
				(void *)hook_do_faccessat,
				(void **)&orig_do_faccessat);
}

void hooks_cleanup(void)
{
	if (faccessat_hook.active)
		neverc_krt_hook_remove(&faccessat_hook);
}
