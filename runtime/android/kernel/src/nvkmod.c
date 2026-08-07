/* SPDX-License-Identifier: GPL-2.0 */
/* nvkmod.c — implementations extracted from nvkmod.h. */
#include <nvk.h>
#include <linux/kprobes.h>
#include "nvk_internal.h"

#define _NEVERC_KRT_SYM_KALLSYMS_LOOKUP "kallsyms_lookup_name"
#define _NEVERC_KRT_SYM_PRINTK_PRIMARY  "_printk"
#define _NEVERC_KRT_SYM_PRINTK_FALLBACK "printk"

static int _neverc_krt_kp_stub(struct kprobe *p, struct pt_regs *regs);
static void *_neverc_krt_kprobe_lookup(const char *name);
static unsigned long _neverc_krt_kprobe_resolve_sym(const char *name);
static int _neverc_krt_is_stub(void *addr);
static int _neverc_krt_ksym_bootstrap(int cfi);
static int _neverc_krt_log_bootstrap(void);

static __attribute__((naked))
int _neverc_krt_kp_stub(struct kprobe *p, struct pt_regs *regs)
{
	__asm__ __volatile__(
		"hint #34\n"
		"mov x0, #0\n"
		"ret\n"
	);
}

static void *_neverc_krt_kprobe_lookup(const char *name)
{
	struct kprobe kp;
	unsigned char *p = (unsigned char *)&kp;
	unsigned long i;
	int ret;

	for (i = 0; i < sizeof(kp); i++)
		p[i] = 0;

	kp.symbol_name = name;
	kp.offset = 0;
	kp.pre_handler = _neverc_krt_kp_stub;

	ret = register_kprobe(&kp);
	if (ret == 0) {
		void *addr = (void *)kp.addr;
		unregister_kprobe(&kp);
		return addr;
	}
	return (void *)kp.addr;
}

static __attribute__((naked))
unsigned long _neverc_krt_kprobe_resolve_sym(const char *name)
{
	__asm__ __volatile__(
		"hint #34\n"
		"stp x29, x30, [sp, #-16]!\n"
		"mov x29, sp\n"
		"bl %c0\n"
		"ldp x29, x30, [sp], #16\n"
		"ret\n"
		:
		: "S"(_neverc_krt_kprobe_lookup)
	);
}

static int _neverc_krt_is_stub(void *addr)
{
	u32 *p = (u32 *)addr;
	int off = 0;

	if (p[0] == NEVERC_KRT_A64_RET)
		return 1;

	if (p[0] == NEVERC_KRT_A64_BTI_C)
		off = 1;

	/* [bti c;] paciasp; autiasp; ret */
	if (p[off] == NEVERC_KRT_A64_PACIASP &&
	    p[off + 1] == NEVERC_KRT_A64_AUTIASP &&
	    p[off + 2] == NEVERC_KRT_A64_RET)
		return 1;

	/* [bti c;] paciasp; mov x0,#0|xzr; autiasp; ret */
	if (p[off] == NEVERC_KRT_A64_PACIASP &&
	    (p[off + 1] == NEVERC_KRT_A64_MOV_X0_0 ||
	     p[off + 1] == NEVERC_KRT_A64_MOV_X0_XZR) &&
	    p[off + 2] == NEVERC_KRT_A64_AUTIASP &&
	    p[off + 3] == NEVERC_KRT_A64_RET)
		return 1;

	/* [bti c;] mov x0,#0|xzr; ret  (no PAC) */
	if ((p[off] == NEVERC_KRT_A64_MOV_X0_0 ||
	     p[off] == NEVERC_KRT_A64_MOV_X0_XZR) &&
	    p[off + 1] == NEVERC_KRT_A64_RET)
		return 1;

	return 0;
}

static int _neverc_krt_ksym_bootstrap(int cfi)
{
	neverc_krt_kallsyms_lookup_name_fn resolved;

	if (_neverc_krt_sym_resolver)
		return 0;

	_neverc_krt_cache_key_init();

	if (!cfi) {
		resolved = (neverc_krt_kallsyms_lookup_name_fn)
			_neverc_krt_kprobe_lookup(NC_XORSTR(
				_NEVERC_KRT_SYM_KALLSYMS_LOOKUP));
		if (resolved && !_neverc_krt_is_stub((void *)resolved)) {
			_neverc_krt_sym_resolver = resolved;
			return 0;
		}
	}

	_neverc_krt_sym_resolver = _neverc_krt_kprobe_resolve_sym;
	return 0;
}

static int _neverc_krt_log_bootstrap(void)
{
	if (neverc_krt_printk)
		return 0;
	neverc_krt_printk = (neverc_krt_printk_fn)
		_neverc_krt_kprobe_lookup(NC_XORSTR(
			_NEVERC_KRT_SYM_PRINTK_PRIMARY));
	if (!neverc_krt_printk)
		neverc_krt_printk = (neverc_krt_printk_fn)
			_neverc_krt_kprobe_lookup(NC_XORSTR(
				_NEVERC_KRT_SYM_PRINTK_FALLBACK));
	return neverc_krt_printk ? 0 : -1;
}

int neverc_krt_bootstrap(int cfi, int kernel_profile)
{
	int ret;

	if (kernel_profile)
		_neverc_krt_version_setup(kernel_profile);

	ret = _neverc_krt_ksym_bootstrap(cfi);
	if (!ret)
		ret = _neverc_krt_log_bootstrap();
	return ret;
}
