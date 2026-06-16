/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_KALLSYMS_H
#define _NVK_LINUX_KALLSYMS_H

#include <linux/types.h>
#include <neverc/xorstr/xorstr.h>

typedef unsigned long (*nvk_kallsyms_lookup_name_fn)(const char *name);
extern nvk_kallsyms_lookup_name_fn nvk_kallsyms_lookup_name;

/*
 * Pluggable resolver: set to nvk_kallsyms_lookup_name on non-CFI kernels,
 * or to the kprobe-based resolver on CFI/GKI kernels where
 * kallsyms_lookup_name is stubbed.  Initialised by nvk_ksym_bootstrap().
 */
typedef unsigned long (*_nvk_sym_resolver_fn)(const char *name);
static _nvk_sym_resolver_fn _nvk_sym_resolver;

#define _NVK_SYM_CACHE_BITS  7
#define _NVK_SYM_CACHE_SIZE  (1 << _NVK_SYM_CACHE_BITS)
#define _NVK_SYM_CACHE_MASK  (_NVK_SYM_CACHE_SIZE - 1)

struct _nvk_sym_entry {
	u32           hash;
	unsigned long addr;
};

static struct _nvk_sym_entry _nvk_sym_cache[_NVK_SYM_CACHE_SIZE];

static __always_inline u32 _nvk_sym_hash(const char *s)
{
	u32 h = 0x811C9DC5U;
	while (*s) {
		h ^= (unsigned char)*s++;
		h *= 0x01000193U;
	}
	return h;
}

static __always_inline unsigned long _nvk_sym_cached(const char *name)
{
	u32 h = _nvk_sym_hash(name);
	u32 idx = h & _NVK_SYM_CACHE_MASK;

	if (_nvk_sym_cache[idx].hash == h && _nvk_sym_cache[idx].addr)
		return _nvk_sym_cache[idx].addr;

	if (!_nvk_sym_resolver)
		return 0;

	unsigned long addr = _nvk_sym_resolver(name);
	if (addr) {
		_nvk_sym_cache[idx].hash = h;
		_nvk_sym_cache[idx].addr = addr;
	}
	return addr;
}

#define kallsyms_lookup_name(name) _nvk_sym_cached(name)

#define NVK_LOOKUP(sym)                                                       \
	((void *)kallsyms_lookup_name(NC_XORSTR(sym)))

#define NVK_RESOLVE(fnptr, sym)                                                \
	((fnptr) ? (fnptr) : ((fnptr) = (__typeof__(fnptr))NVK_LOOKUP(sym)))

#define NVK_LOOKUP_OR_FAIL(var, sym, errval)                                   \
	do {                                                                   \
		(var) = (__typeof__(var))NVK_LOOKUP(sym);                      \
		if (!(var)) return (errval);                                   \
	} while (0)

#define NVK_LOOKUP_FN(fnptr, sym)                                              \
	((fnptr) = (__typeof__(fnptr))NVK_LOOKUP(sym))

#define NVK_LOOKUP2(sym, alt)                                                  \
	({ void *__p = NVK_LOOKUP(sym);                                       \
	   if (!__p) __p = NVK_LOOKUP(alt);                                   \
	   __p; })

static __always_inline void nvk_sym_cache_clear(void)
{
	unsigned long i;
	for (i = 0; i < _NVK_SYM_CACHE_SIZE; i++) {
		_nvk_sym_cache[i].hash = 0;
		_nvk_sym_cache[i].addr = 0;
	}
}

#endif /* _NVK_LINUX_KALLSYMS_H */
