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
#include <linux/kprobes.h>

#define NEVERC_KRT_KSYM_STORAGE                                                       \
	neverc_krt_kallsyms_lookup_name_fn neverc_krt_kallsyms_lookup_name = (void *)0
#define NEVERC_KRT_PRINTK_STORAGE neverc_krt_printk_fn neverc_krt_printk = (void *)0

int neverc_krt_kp_stub(struct kprobe *p, void *regs);

/*
 * Resolve a kernel symbol via kprobe — register, grab kp.addr, unregister.
 * Works for both code and data symbols: register_kprobe sets kp.addr via the
 * kernel-internal kallsyms_lookup_name (not the module-visible stub) before
 * any code-address checks.  For data symbols register_kprobe returns an error
 * but kp.addr is already populated.
 */
void *neverc_krt_kprobe_lookup(const char *name);

#define NEVERC_KRT_KPROBE_LOOKUP(sym) neverc_krt_kprobe_lookup(NC_XORSTR(sym))

/*
 * kprobe-based symbol resolver with the same signature as
 * kallsyms_lookup_name.  Used as drop-in replacement when the kernel's
 * kallsyms_lookup_name is stubbed (CFI/GKI).
 */
unsigned long neverc_krt_kprobe_resolve_sym(const char *name);

/*
 * Detect a CFI/GKI no-op stub or a trivial return-0 function.
 *
 * Covered patterns (with/without BTI C prefix):
 *   [BTI C;] PACIASP; AUTIASP; RET                  pass-through (returns x0)
 *   [BTI C;] PACIASP; MOV X0,#0; AUTIASP; RET       return 0
 *   [BTI C;] PACIASP; MOV X0,XZR; AUTIASP; RET      return 0 (alt encoding)
 *   [BTI C;] MOV X0,#0; RET                          return 0 (no PAC)
 *   [BTI C;] MOV X0,XZR; RET                         return 0 (no PAC, alt)
 *   RET                                               bare return
 */
int neverc_krt_is_stub(void *addr);

/*
 * @cfi: assume CFI/GKI kernel (kallsyms_lookup_name is stubbed).
 *       true  → always use kprobe resolver (safe default for 5.10+ GKI).
 *       false → probe kallsyms_lookup_name first; fall back to kprobe
 *               only if it is a stub.
 */
int neverc_krt_ksym_bootstrap(int cfi);
int neverc_krt_log_bootstrap(void);

#define NEVERC_KRT_BOOTSTRAP()       _neverc_krt_do_bootstrap(1)
#define NEVERC_KRT_BOOTSTRAP_EX(cfi) _neverc_krt_do_bootstrap(cfi)

__always_inline int _neverc_krt_do_bootstrap(int cfi)
{
	int r = neverc_krt_ksym_bootstrap(cfi);
	if (r == 0)
		r = neverc_krt_log_bootstrap();
	return r;
}

/* __versions section is handled by -fandroid-kernel-driver-mode. */

#define NEVERC_KRT_DEFINE_MODULE(modname)                                            \
	MODULE_INFO(name, modname);                                           \
	MODULE_INFO(vermagic, NEVERC_KRT_VERMAGIC);                                  \
	MODULE_INFO(depends, "");                                             \
	NEVERC_KRT_KSYM_STORAGE;                                                     \
	NEVERC_KRT_PRINTK_STORAGE;                                                   \
	__attribute__((section(".gnu.linkonce.this_module"), used,            \
		       aligned(64)))                                          \
	struct neverc_krt_this_module __this_module = {                              \
	    .name = modname,                                                 \
	    .init = init_module,                                             \
	    .exit = cleanup_module,                                          \
	}

#endif /* NVKMOD_H */
