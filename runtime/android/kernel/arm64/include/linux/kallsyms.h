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

/* Resolve into a typed local var, or return error code on failure. */
#define NVK_LOOKUP_OR_FAIL(var, sym, errval)                                   \
	do {                                                                   \
		(var) = (__typeof__(var))NVK_LOOKUP(sym);                      \
		if (!(var)) return (errval);                                   \
	} while (0)

/* Cast-assign a function pointer from kallsyms in one statement. */
#define NVK_LOOKUP_FN(fnptr, sym)                                              \
	((fnptr) = (__typeof__(fnptr))NVK_LOOKUP(sym))

/* Try sym first, fallback to alt if not found. */
#define NVK_LOOKUP2(sym, alt)                                                  \
	({ void *__p = NVK_LOOKUP(sym);                                       \
	   if (!__p) __p = NVK_LOOKUP(alt);                                   \
	   __p; })

#endif /* _NVK_LINUX_KALLSYMS_H */
