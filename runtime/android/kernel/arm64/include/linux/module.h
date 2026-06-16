/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_MODULE_H
#define _NEVERC_KRT_LINUX_MODULE_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/printk.h>
#include <linux/kallsyms.h>
#include <nvkmod_version.h>

#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "nvkmod"
#endif

struct module; /* opaque to modules */

/* ---- .modinfo ---------------------------------------------------------- */
#define _NEVERC_KRT_CAT2(a, b) a##b
#define _NEVERC_KRT_CAT(a, b) _NEVERC_KRT_CAT2(a, b)
#define MODULE_INFO(tag, val)                                                 \
	static const char _NEVERC_KRT_CAT(__neverc_krt_mi_, __COUNTER__)[] __attribute__(    \
	    (section(".modinfo"), used, aligned(1))) = #tag "=" val
#define MODULE_LICENSE(s) MODULE_INFO(license, s)
#define MODULE_AUTHOR(s) MODULE_INFO(author, s)
#define MODULE_DESCRIPTION(s) MODULE_INFO(description, s)
#define MODULE_VERSION(s) MODULE_INFO(version, s)
#define MODULE_ALIAS(s) MODULE_INFO(alias, s)
#define MODULE_DEVICE_TABLE(type, name)
#define MODULE_PARM_DESC(p, desc)

/* ---- init / exit aliases ---------------------------------------------- */
#define module_init(fn)                                                       \
	int init_module(void) __attribute__((alias(#fn), used, visibility("default")))
#define module_exit(fn)                                                       \
	void cleanup_module(void)                                              \
	    __attribute__((alias(#fn), used, visibility("default")))

/* ---- this_module blob ------------------------------------------------- */
struct neverc_krt_this_module {
	unsigned char _pre_name[NEVERC_KRT_OFF_NAME];
	char name[NEVERC_KRT_OFF_INIT - NEVERC_KRT_OFF_NAME];
	int (*init)(void);
	unsigned char _mid[NEVERC_KRT_OFF_EXIT - NEVERC_KRT_OFF_INIT - sizeof(void *)];
	void (*exit)(void);
	unsigned char _post[NEVERC_KRT_MODULE_SIZE - NEVERC_KRT_OFF_EXIT - sizeof(void *)];
} __attribute__((aligned(8)));

_Static_assert(NEVERC_KRT_OFF_NAME < NEVERC_KRT_OFF_INIT, "name must precede init");
_Static_assert(NEVERC_KRT_OFF_INIT + sizeof(void *) <= NEVERC_KRT_OFF_EXIT,
	       "init must precede exit");
_Static_assert(NEVERC_KRT_OFF_EXIT + sizeof(void *) <= NEVERC_KRT_MODULE_SIZE,
	       "NEVERC_KRT_MODULE_SIZE too small");

extern struct neverc_krt_this_module __this_module;
#define THIS_MODULE ((struct module *)&__this_module)

#endif /* _NEVERC_KRT_LINUX_MODULE_H */
