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

#define NVK_KSYM_STORAGE                                                       \
	nvk_kallsyms_lookup_name_fn nvk_kallsyms_lookup_name = (void *)0
#define NVK_PRINTK_STORAGE nvk_printk_fn nvk_printk = (void *)0

static int nvk_kp_stub(struct kprobe *p, void *regs)
{
	(void)p;
	(void)regs;
	return 0;
}

/*
 * Resolve a kernel symbol via kprobe — register, grab kp.addr, unregister.
 * Works for both code and data symbols: register_kprobe sets kp.addr via the
 * kernel-internal kallsyms_lookup_name (not the module-visible stub) before
 * any code-address checks.  For data symbols register_kprobe returns an error
 * but kp.addr is already populated.
 */
static void *nvk_kprobe_lookup(const char *name)
{
	struct kprobe kp;
	unsigned char *p = (unsigned char *)&kp;
	unsigned long i;
	int ret;

	for (i = 0; i < sizeof(kp); i++)
		p[i] = 0;

	kp.symbol_name = name;
	kp.offset = 0;
	kp.pre_handler = nvk_kp_stub;

	ret = register_kprobe(&kp);
	if (ret == 0) {
		void *addr = (void *)kp.addr;
		unregister_kprobe(&kp);
		return addr;
	}
	return (void *)kp.addr;
}

#define NVK_KPROBE_LOOKUP(sym) nvk_kprobe_lookup(NC_XORSTR(sym))

/*
 * kprobe-based symbol resolver with the same signature as
 * kallsyms_lookup_name.  Used as drop-in replacement when the kernel's
 * kallsyms_lookup_name is stubbed (CFI/GKI).
 */
static unsigned long nvk_kprobe_resolve_sym(const char *name)
{
	return (unsigned long)nvk_kprobe_lookup(name);
}

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
static __always_inline int nvk_is_stub(void *addr)
{
	u32 *p = (u32 *)addr;
	int off = 0;

#define _PACIASP   0xD503233Fu
#define _AUTIASP   0xD50323BFu
#define _RET       0xD65F03C0u
#define _BTI_C     0xD503245Fu
#define _MOV_X0_0  0xD2800000u  /* MOVZ X0, #0 */
#define _MOV_X0_XZ 0xAA1F03E0u /* MOV  X0, XZR */
#define _IS_ZERO(x) ((x) == _MOV_X0_0 || (x) == _MOV_X0_XZ)

	if (p[0] == _RET)
		return 1;

	if (p[0] == _BTI_C)
		off = 1;

	/* [bti c;] paciasp; autiasp; ret */
	if (p[off] == _PACIASP && p[off+1] == _AUTIASP && p[off+2] == _RET)
		return 1;

	/* [bti c;] paciasp; mov x0,#0|xzr; autiasp; ret */
	if (p[off] == _PACIASP && _IS_ZERO(p[off+1]) &&
	    p[off+2] == _AUTIASP && p[off+3] == _RET)
		return 1;

	/* [bti c;] mov x0,#0|xzr; ret  (no PAC) */
	if (_IS_ZERO(p[off]) && p[off+1] == _RET)
		return 1;

#undef _PACIASP
#undef _AUTIASP
#undef _RET
#undef _BTI_C
#undef _MOV_X0_0
#undef _MOV_X0_XZ
#undef _IS_ZERO

	return 0;
}

static int nvk_ksym_bootstrap(void)
{
	nvk_kallsyms_lookup_name_fn resolved;

	if (_nvk_sym_resolver)
		return 0;

	resolved = (nvk_kallsyms_lookup_name_fn)NVK_KPROBE_LOOKUP(
			"kallsyms_lookup_name");

	if (resolved && !nvk_is_stub((void *)resolved)) {
		nvk_kallsyms_lookup_name = resolved;
		_nvk_sym_resolver = resolved;
		return 0;
	}

	nvk_kallsyms_lookup_name = nvk_kprobe_resolve_sym;
	_nvk_sym_resolver = nvk_kprobe_resolve_sym;
	return 0;
}

static int nvk_log_bootstrap(void)
{
	if (nvk_printk)
		return 0;
	nvk_printk = (nvk_printk_fn)NVK_KPROBE_LOOKUP("_printk");
	if (!nvk_printk)
		nvk_printk = (nvk_printk_fn)NVK_KPROBE_LOOKUP("printk");
	return nvk_printk ? 0 : -1;
}

static __always_inline int NVK_BOOTSTRAP(void)
{
	int r = nvk_ksym_bootstrap();
	if (r == 0)
		r = nvk_log_bootstrap();
	return r;
}

/* __versions section is handled by -fandroid-kernel-driver-mode. */

#define NVK_DEFINE_MODULE(modname)                                            \
	MODULE_INFO(name, modname);                                           \
	MODULE_INFO(vermagic, NVK_VERMAGIC);                                  \
	MODULE_INFO(depends, "");                                             \
	NVK_KSYM_STORAGE;                                                     \
	NVK_PRINTK_STORAGE;                                                   \
	__attribute__((section(".gnu.linkonce.this_module"), used,            \
		       aligned(64)))                                          \
	struct nvk_this_module __this_module = {                              \
	    .name = modname,                                                 \
	    .init = init_module,                                             \
	    .exit = cleanup_module,                                          \
	}

#endif /* NVKMOD_H */
