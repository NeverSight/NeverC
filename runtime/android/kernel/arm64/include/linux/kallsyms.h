/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_KALLSYMS_H
#define _NVK_LINUX_KALLSYMS_H

#include <linux/types.h>
#include <neverc/xorstr/xorstr.h>

typedef unsigned long (*nvk_kallsyms_lookup_name_fn)(const char *name);
extern nvk_kallsyms_lookup_name_fn nvk_kallsyms_lookup_name;

/* Raw lookup (caller manages string encryption). */
#define kallsyms_lookup_name(name)                                            \
	(nvk_kallsyms_lookup_name ? nvk_kallsyms_lookup_name(name) : 0UL)

/* Resolve a symbol to a typed pointer.  String is XOR-encrypted. */
#define NVK_LOOKUP(sym)                                                       \
	((void *)kallsyms_lookup_name(NC_XORSTR(sym)))

/* Declare + lazily resolve a kernel function pointer on first use. */
#define NVK_RESOLVE(fnptr, sym)                                                \
	((fnptr) ? (fnptr) : ((fnptr) = (__typeof__(fnptr))NVK_LOOKUP(sym)))

#endif /* _NVK_LINUX_KALLSYMS_H */
