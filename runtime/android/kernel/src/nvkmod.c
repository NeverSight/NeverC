/* SPDX-License-Identifier: GPL-2.0 */
/* nvkmod.c — implementations extracted from nvkmod.h. */
#include <nvk.h>

int neverc_krt_kp_stub(struct kprobe *p, void *regs)
{
	(void)p;
	(void)regs;
	return 0;
}

void *neverc_krt_kprobe_lookup(const char *name)
{
	struct kprobe kp;
	unsigned char *p = (unsigned char *)&kp;
	unsigned long i;
	int ret;

	for (i = 0; i < sizeof(kp); i++)
		p[i] = 0;

	kp.symbol_name = name;
	kp.offset = 0;
	kp.pre_handler = neverc_krt_kp_stub;

	ret = register_kprobe(&kp);
	if (ret == 0) {
		void *addr = (void *)kp.addr;
		unregister_kprobe(&kp);
		return addr;
	}
	return (void *)kp.addr;
}

unsigned long neverc_krt_kprobe_resolve_sym(const char *name)
{
	return (unsigned long)neverc_krt_kprobe_lookup(name);
}

int neverc_krt_ksym_bootstrap(int cfi)
{
	neverc_krt_kallsyms_lookup_name_fn resolved;

	if (_neverc_krt_sym_resolver)
		return 0;

	_neverc_krt_cache_key_init();

	if (!cfi) {
		resolved = (neverc_krt_kallsyms_lookup_name_fn)NEVERC_KRT_KPROBE_LOOKUP(
				"kallsyms_lookup_name");
		if (resolved && !neverc_krt_is_stub((void *)resolved)) {
			neverc_krt_kallsyms_lookup_name = resolved;
			_neverc_krt_sym_resolver = resolved;
			return 0;
		}
	}

	neverc_krt_kallsyms_lookup_name = neverc_krt_kprobe_resolve_sym;
	_neverc_krt_sym_resolver = neverc_krt_kprobe_resolve_sym;
	return 0;
}

int neverc_krt_log_bootstrap(void)
{
	if (neverc_krt_printk)
		return 0;
	neverc_krt_printk = (neverc_krt_printk_fn)NEVERC_KRT_KPROBE_LOOKUP("_printk");
	if (!neverc_krt_printk)
		neverc_krt_printk = (neverc_krt_printk_fn)NEVERC_KRT_KPROBE_LOOKUP("printk");
	return neverc_krt_printk ? 0 : -1;
}

