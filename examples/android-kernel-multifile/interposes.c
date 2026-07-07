/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interpose installation for the multi-file module.
 *
 * This file does NOT call NEVERC_KRT_BOOTSTRAP() — the bootstrap was already
 * done in main.c's module_init.  Thanks to the compiler's automatic
 * linkage promotion, NEVERC_KRT_LOOKUP / kallsyms_lookup_name and all interpose
 * infrastructure work seamlessly here.
 */
#include <nvkmod.h>
#include <nvk_interpose.h>

static struct neverc_krt_interpose_ref faccessat_ref;
static void *orig_do_faccessat;

static long interpose_do_faccessat(void *orig, void *a0, void *a1,
			      void *a2, void *a3, void *a4, void *a5)
{
	typedef long (*fn_t)(void *, void *, void *, void *, void *, void *);
	return ((fn_t)orig)(a0, a1, a2, a3, a4, a5);
}

int interposes_init(void)
{
	void *target = NEVERC_KRT_LOOKUP("do_faccessat");
	if (!target)
		return -1;

	return neverc_krt_interpose_register(target, (void *)interpose_do_faccessat,
					0, &orig_do_faccessat, &faccessat_ref);
}

void interposes_cleanup(void)
{
	neverc_krt_interpose_unregister(&faccessat_ref);
}
