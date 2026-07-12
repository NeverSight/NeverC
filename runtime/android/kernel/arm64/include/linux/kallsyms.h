/* SPDX-License-Identifier: GPL-2.0 */
/* kallsyms.h — public symbol-lookup interface and caller-side macros. */
#ifndef _NEVERC_KRT_LINUX_KALLSYMS_H
#define _NEVERC_KRT_LINUX_KALLSYMS_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <neverc/xorstr/xorstr.h>

typedef unsigned long (*neverc_krt_kallsyms_lookup_name_fn)(const char *name);
unsigned long neverc_krt_lookup_name(const char *name);

/* Compatibility spelling retained for existing NeverC modules. */
#define neverc_krt_kallsyms_lookup_name neverc_krt_lookup_name
#define kallsyms_lookup_name(name) neverc_krt_lookup_name(name)

#define NEVERC_KRT_LOOKUP(sym) \
	((void *)kallsyms_lookup_name(NC_XORSTR(sym)))

#define NEVERC_KRT_RESOLVE(fnptr, sym) \
	((fnptr) ? (fnptr) : ((fnptr) = (__typeof__(fnptr))NEVERC_KRT_LOOKUP(sym)))

#define NEVERC_KRT_LOOKUP_OR_FAIL(var, sym, errval)                            \
	do {                                                                   \
		(var) = (__typeof__(var))NEVERC_KRT_LOOKUP(sym);               \
		if (!(var)) return (errval);                                   \
	} while (0)

#define NEVERC_KRT_LOOKUP_FN(fnptr, sym) \
	((fnptr) = (__typeof__(fnptr))NEVERC_KRT_LOOKUP(sym))

#define NEVERC_KRT_LOOKUP2(sym, alt)                                           \
	({ void *__p = NEVERC_KRT_LOOKUP(sym);                                 \
	   if (!__p) __p = NEVERC_KRT_LOOKUP(alt);                             \
	   __p; })

void neverc_krt_sym_cache_clear(void);

#endif /* _NEVERC_KRT_LINUX_KALLSYMS_H */
