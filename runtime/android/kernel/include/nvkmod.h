/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVKMOD_H
#define NVKMOD_H

#ifndef __KERNEL__
#error "<nvkmod.h> requires -fandroid-kernel-driver-mode (missing __KERNEL__)"
#endif
#ifndef MODULE
#error "<nvkmod.h> requires -fandroid-kernel-driver-mode (missing MODULE)"
#endif

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kallsyms.h>

#define NEVERC_KRT_PRINTK_STORAGE neverc_krt_printk_fn neverc_krt_printk = (void *)0

/*
 * Initialize symbol resolution and logging for the selected GKI profile.
 * Prefer NEVERC_KRT_BOOTSTRAP() so the caller's NEVERC_KRT_KERNEL value is
 * passed into the version-neutral embedded runtime.
 */
int neverc_krt_bootstrap(int cfi, int kernel_profile);

#define NEVERC_KRT_BOOTSTRAP() \
	neverc_krt_bootstrap(1, NEVERC_KRT_KERNEL)
#define NEVERC_KRT_BOOTSTRAP_EX(cfi) \
	neverc_krt_bootstrap((cfi), NEVERC_KRT_KERNEL)

/* __versions section is handled by -fandroid-kernel-driver-mode. */

#define NEVERC_KRT_DEFINE_MODULE(modname)                                            \
	MODULE_INFO(name, modname);                                           \
	MODULE_INFO(vermagic, NEVERC_KRT_VERMAGIC);                                  \
	MODULE_INFO(depends, "");                                             \
	NEVERC_KRT_PRINTK_STORAGE;                                                   \
	__attribute__((section(".gnu.linkonce.this_module"), used,            \
		       aligned(64)))                                          \
	struct neverc_krt_this_module __this_module = {                              \
	    .name = modname,                                                 \
	    .init = init_module,                                             \
	    .exit = cleanup_module,                                          \
	}

#endif /* NVKMOD_H */
