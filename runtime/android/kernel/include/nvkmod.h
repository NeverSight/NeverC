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

static int nvk_ksym_bootstrap(void)
{
	struct kprobe kp;
	unsigned char *p = (unsigned char *)&kp;
	unsigned long i;
	int ret;

	if (nvk_kallsyms_lookup_name)
		return 0;

	for (i = 0; i < sizeof(kp); i++)
		p[i] = 0;

	kp.symbol_name = NC_XORSTR("kallsyms_lookup_name");
	kp.offset = 0;
	kp.pre_handler = nvk_kp_stub;

	ret = register_kprobe(&kp);
	if (ret < 0)
		return ret;

	nvk_kallsyms_lookup_name = (nvk_kallsyms_lookup_name_fn)kp.addr;
	unregister_kprobe(&kp);

	return nvk_kallsyms_lookup_name ? 0 : -1;
}

static int nvk_log_bootstrap(void)
{
	if (nvk_printk)
		return 0;
	nvk_printk = (nvk_printk_fn)NVK_LOOKUP("_printk"); /* 5.15+ */
	if (!nvk_printk)
		nvk_printk = (nvk_printk_fn)NVK_LOOKUP("printk"); /* 5.10 */
	return nvk_printk ? 0 : -1;
}

static __always_inline int NVK_BOOTSTRAP(void)
{
	int r = nvk_ksym_bootstrap();
	if (r == 0)
		r = nvk_log_bootstrap();
	return r;
}

#define NVK_DEFINE_MODULE(modname)                                            \
	MODULE_INFO(name, modname);                                           \
	MODULE_INFO(vermagic, NVK_VERMAGIC);                                  \
	NVK_KSYM_STORAGE;                                                     \
	NVK_PRINTK_STORAGE;                                                   \
	__attribute__((section(".gnu.linkonce.this_module"), used,           \
		       aligned(8))) struct nvk_this_module __this_module = {  \
	    .name = modname,                                                 \
	    .init = init_module,                                             \
	    .exit = cleanup_module,                                          \
	}

#endif /* NVKMOD_H */
