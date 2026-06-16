/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_KALLSYMS_H
#define _NVK_LINUX_KALLSYMS_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <neverc/xorstr/xorstr.h>

typedef unsigned long (*nvk_kallsyms_lookup_name_fn)(const char *name);
extern nvk_kallsyms_lookup_name_fn nvk_kallsyms_lookup_name;

/*
 * Pluggable resolver: set to nvk_kallsyms_lookup_name on non-CFI kernels,
 * or to the kprobe-based resolver on CFI/GKI kernels where
 * kallsyms_lookup_name is stubbed.  Initialised by nvk_ksym_bootstrap().
 *
 * Defined in the precompiled NVK runtime bitcode (nvk_runtime_bc.c).
 * NvkKernelRuntimeLinkerPass links the bitcode into each TU so all
 * translation units share a single copy: NVK_BOOTSTRAP() only needs
 * to be called once in module_init.
 */
typedef unsigned long (*_nvk_sym_resolver_fn)(const char *name);
NVK_RT_VAR _nvk_sym_resolver_fn _nvk_sym_resolver;

#define _NVK_SYM_CACHE_BITS  7
#define _NVK_SYM_CACHE_SIZE  (1 << _NVK_SYM_CACHE_BITS)
#define _NVK_SYM_CACHE_MASK  (_NVK_SYM_CACHE_SIZE - 1)

struct _nvk_sym_entry {
	u32           hash;
	unsigned long enc;
};

NVK_RT_VAR struct _nvk_sym_entry _nvk_sym_cache[_NVK_SYM_CACHE_SIZE];
NVK_RT_VAR unsigned long _nvk_cache_key;

/*
 * Compile-time pluggable primitives — override by #define before #include.
 *
 * Usage (in main.c, before #include <nvkmod.h>):
 *
 *   static __always_inline unsigned long my_xor(unsigned long a, unsigned long b) {
 *       unsigned long r = a + b;
 *       unsigned long m = a & b;
 *       r = r - m - m;
 *       return r;
 *   }
 *   #define _nvk_xor_opaque my_xor
 *   #include <nvkmod.h>
 *
 * Overriding _nvk_xor_opaque is enough: _nvk_ptr_enc/_nvk_ptr_dec call
 * through it automatically.  Override _nvk_ptr_enc/_nvk_ptr_dec directly
 * to replace the full encrypt/decrypt logic.
 *
 * Everything is static __always_inline — no function pointers, no globals,
 * no symbols in the final binary.
 */

#ifndef _nvk_xor_opaque
static __always_inline unsigned long _nvk_xor_opaque(unsigned long a,
						     unsigned long b)
{
	unsigned long sum  = a + b;
	unsigned long both = a & b;
	return sum - both - both;
}
#endif

/*
 * Per-build cache seed.
 *
 * Priority:
 *   1. -DNVK_CACHE_SEED=0x...  (build system injects explicit seed)
 *   2. __builtin_neverc_random_u64() (NeverC compiler: full 64-bit entropy,
 *      unique per call site per compilation)
 *   3. __TIME__+__DATE__ fallback (~17 bits, changes every second)
 */
#ifndef NVK_CACHE_SEED
#  if __has_builtin(__builtin_neverc_random_u64)
#    define NVK_CACHE_SEED ((unsigned long)__builtin_neverc_random_u64())
#  else
#    define _NVK_CT(s, i) ((unsigned long)((unsigned char)(s)[i]))
#    define NVK_CACHE_SEED (                                                  \
	(_NVK_CT(__TIME__, 0) << 56) | (_NVK_CT(__TIME__, 1) << 48) |        \
	(_NVK_CT(__TIME__, 3) << 40) | (_NVK_CT(__TIME__, 4) << 32) |        \
	(_NVK_CT(__TIME__, 6) << 24) | (_NVK_CT(__TIME__, 7) << 16) |        \
	(_NVK_CT(__DATE__, 4) <<  8) | (_NVK_CT(__DATE__, 5)      ))
#  endif
#endif

static __always_inline void _nvk_cache_key_init(void)
{
	unsigned long k, expected;
	if (__atomic_load_n(&_nvk_cache_key, __ATOMIC_ACQUIRE))
		return;
	k = _nvk_xor_opaque(
		(unsigned long)(void *)_nvk_sym_cache + (unsigned long)NVK_CACHE_SEED,
		(unsigned long)(void *)&_nvk_cache_key);
	expected = 0;
	__atomic_compare_exchange_n(&_nvk_cache_key, &expected, k,
				    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

#ifndef _nvk_ptr_enc
static __always_inline unsigned long _nvk_ptr_enc(unsigned long addr)
{
	return _nvk_xor_opaque(addr,
		__atomic_load_n(&_nvk_cache_key, __ATOMIC_RELAXED));
}
#endif

#ifndef _nvk_ptr_dec
static __always_inline unsigned long _nvk_ptr_dec(unsigned long enc)
{
	return _nvk_xor_opaque(enc,
		__atomic_load_n(&_nvk_cache_key, __ATOMIC_RELAXED));
}
#endif

static __always_inline u32 _nvk_sym_hash(const char *s)
{
	u32 h = 0;
	while (*s) {
		unsigned long c = (unsigned char)*s++;
		h = (h + (c << 4) + (c >> 4)) * 11;
	}
	return h;
}

static __always_inline unsigned long _nvk_sym_cached(const char *name)
{
	u32 h = _nvk_sym_hash(name);
	u32 idx = h & _NVK_SYM_CACHE_MASK;
	unsigned long e;

	if (__atomic_load_n(&_nvk_sym_cache[idx].hash, __ATOMIC_ACQUIRE) == h) {
		e = __atomic_load_n(&_nvk_sym_cache[idx].enc, __ATOMIC_ACQUIRE);
		if (e)
			return _nvk_ptr_dec(e);
	}

	if (!_nvk_sym_resolver)
		return 0;

	unsigned long addr = _nvk_sym_resolver(name);
	if (addr) {
		__atomic_store_n(&_nvk_sym_cache[idx].enc,
				 _nvk_ptr_enc(addr), __ATOMIC_RELEASE);
		__atomic_store_n(&_nvk_sym_cache[idx].hash,
				 h, __ATOMIC_RELEASE);
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
		__atomic_store_n(&_nvk_sym_cache[i].enc, 0, __ATOMIC_RELEASE);
		__atomic_store_n(&_nvk_sym_cache[i].hash, 0, __ATOMIC_RELEASE);
	}
	__atomic_store_n(&_nvk_cache_key, 0, __ATOMIC_RELEASE);
}

#endif /* _NVK_LINUX_KALLSYMS_H */
