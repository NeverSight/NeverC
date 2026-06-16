/* SPDX-License-Identifier: GPL-2.0 */
/* nvkmod.c — implementations extracted from nvkmod.h. */
#include <nvk.h>

int nvk_kp_stub(struct kprobe *p, void *regs)
{
	(void)p;
	(void)regs;
	return 0;
}

void *nvk_kprobe_lookup(const char *name)
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

unsigned long nvk_kprobe_resolve_sym(const char *name)
{
	return (unsigned long)nvk_kprobe_lookup(name);
}

int nvk_ksym_bootstrap(int cfi)
{
	nvk_kallsyms_lookup_name_fn resolved;

	if (_nvk_sym_resolver)
		return 0;

	_nvk_cache_key_init();

	if (!cfi) {
		resolved = (nvk_kallsyms_lookup_name_fn)NVK_KPROBE_LOOKUP(
				"kallsyms_lookup_name");
		if (resolved && !nvk_is_stub((void *)resolved)) {
			nvk_kallsyms_lookup_name = resolved;
			_nvk_sym_resolver = resolved;
			return 0;
		}
	}

	nvk_kallsyms_lookup_name = nvk_kprobe_resolve_sym;
	_nvk_sym_resolver = nvk_kprobe_resolve_sym;
	return 0;
}

int nvk_log_bootstrap(void)
{
	if (nvk_printk)
		return 0;
	nvk_printk = (nvk_printk_fn)NVK_KPROBE_LOOKUP("_printk");
	if (!nvk_printk)
		nvk_printk = (nvk_printk_fn)NVK_KPROBE_LOOKUP("printk");
	return nvk_printk ? 0 : -1;
}

