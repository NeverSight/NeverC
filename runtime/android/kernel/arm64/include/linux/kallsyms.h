/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_KALLSYMS_H
#define _NEVERC_KRT_LINUX_KALLSYMS_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <neverc/xorstr/xorstr.h>

typedef unsigned long (*neverc_krt_kallsyms_lookup_name_fn)(const char *name);
extern neverc_krt_kallsyms_lookup_name_fn neverc_krt_kallsyms_lookup_name;

/*
 * Pluggable resolver: set to neverc_krt_kallsyms_lookup_name on non-CFI kernels,
 * or to the kprobe-based resolver on CFI/GKI kernels where
 * kallsyms_lookup_name is stubbed.  Initialised by neverc_krt_ksym_bootstrap().
 *
 * Defined in the precompiled NVK runtime bitcode (neverc_krt_runtime_bc.c).
 * NvkKernelRuntimeLinkerPass links the bitcode into each TU so all
 * translation units share a single copy: NEVERC_KRT_BOOTSTRAP() only needs
 * to be called once in module_init.
 */
typedef unsigned long (*_neverc_krt_sym_resolver_fn)(const char *name);
NEVERC_KRT_RT_VAR _neverc_krt_sym_resolver_fn _neverc_krt_sym_resolver;

#define _NEVERC_KRT_SYM_CACHE_BITS  7
#define _NEVERC_KRT_SYM_CACHE_SIZE  (1 << _NEVERC_KRT_SYM_CACHE_BITS)
#define _NEVERC_KRT_SYM_CACHE_MASK  (_NEVERC_KRT_SYM_CACHE_SIZE - 1)

struct _neverc_krt_sym_entry {
	u32           hash;
	unsigned long enc;
};

NEVERC_KRT_RT_VAR struct _neverc_krt_sym_entry _neverc_krt_sym_cache[_NEVERC_KRT_SYM_CACHE_SIZE];
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_cache_key;

/*
 * Compile-time pluggable primitives — override by #define before #include.
 *
 * Usage (in main.c, before #include <nvkmod.h>):
 *
 *   __always_inline unsigned long my_xor(unsigned long a, unsigned long b) {
 *       unsigned long r = a + b;
 *       unsigned long m = a & b;
 *       r = r - m - m;
 *       return r;
 *   }
 *   #define _neverc_krt_xor_opaque my_xor
 *   #include <nvkmod.h>
 *
 * Overriding _neverc_krt_xor_opaque is enough: _neverc_krt_ptr_enc/_neverc_krt_ptr_dec call
 * through it automatically.  Override _neverc_krt_ptr_enc/_neverc_krt_ptr_dec directly
 * to replace the full encrypt/decrypt logic.
 *
 * Everything is __always_inline — no function pointers, no globals,
 * no symbols in the final binary.
 */

#ifndef _neverc_krt_xor_opaque
static __always_inline unsigned long _neverc_krt_xor_opaque(unsigned long a,
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
 *   1. -DNEVERC_KRT_CACHE_SEED=0x...  (build system injects explicit seed)
 *   2. __builtin_neverc_random_u64() (NeverC compiler: full 64-bit entropy,
 *      unique per call site per compilation)
 *   3. __TIME__+__DATE__ fallback (~17 bits, changes every second)
 */
#ifndef NEVERC_KRT_CACHE_SEED
#  if __has_builtin(__builtin_neverc_random_u64)
#    define NEVERC_KRT_CACHE_SEED ((unsigned long)__builtin_neverc_random_u64())
#  else
#    define _NEVERC_KRT_CT(s, i) ((unsigned long)((unsigned char)(s)[i]))
#    define NEVERC_KRT_CACHE_SEED (                                                  \
	(_NEVERC_KRT_CT(__TIME__, 0) << 56) | (_NEVERC_KRT_CT(__TIME__, 1) << 48) |        \
	(_NEVERC_KRT_CT(__TIME__, 3) << 40) | (_NEVERC_KRT_CT(__TIME__, 4) << 32) |        \
	(_NEVERC_KRT_CT(__TIME__, 6) << 24) | (_NEVERC_KRT_CT(__TIME__, 7) << 16) |        \
	(_NEVERC_KRT_CT(__DATE__, 4) <<  8) | (_NEVERC_KRT_CT(__DATE__, 5)      ))
#  endif
#endif

static __always_inline void _neverc_krt_cache_key_init(void)
{
	unsigned long k, expected;
	if (__atomic_load_n(&_neverc_krt_cache_key, __ATOMIC_ACQUIRE))
		return;
	k = _neverc_krt_xor_opaque(
		(unsigned long)(void *)_neverc_krt_sym_cache + (unsigned long)NEVERC_KRT_CACHE_SEED,
		(unsigned long)(void *)&_neverc_krt_cache_key);
	expected = 0;
	__atomic_compare_exchange_n(&_neverc_krt_cache_key, &expected, k,
				    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

#ifndef _neverc_krt_ptr_enc
static __always_inline unsigned long _neverc_krt_ptr_enc(unsigned long addr)
{
	return _neverc_krt_xor_opaque(addr,
		__atomic_load_n(&_neverc_krt_cache_key, __ATOMIC_RELAXED));
}
#endif

#ifndef _neverc_krt_ptr_dec
static __always_inline unsigned long _neverc_krt_ptr_dec(unsigned long enc)
{
	return _neverc_krt_xor_opaque(enc,
		__atomic_load_n(&_neverc_krt_cache_key, __ATOMIC_RELAXED));
}
#endif

static __always_inline u32 _neverc_krt_sym_hash(const char *s)
{
	u32 h = 0;
	while (*s) {
		unsigned long c = (unsigned char)*s++;
		h = (h + (c << 4) + (c >> 4)) * 11;
	}
	return h;
}

static __always_inline unsigned long _neverc_krt_sym_cached(const char *name)
{
	u32 h = _neverc_krt_sym_hash(name);
	u32 idx = h & _NEVERC_KRT_SYM_CACHE_MASK;
	unsigned long e;

	if (__atomic_load_n(&_neverc_krt_sym_cache[idx].hash, __ATOMIC_ACQUIRE) == h) {
		e = __atomic_load_n(&_neverc_krt_sym_cache[idx].enc, __ATOMIC_ACQUIRE);
		if (e)
			return _neverc_krt_ptr_dec(e);
	}

	if (!_neverc_krt_sym_resolver)
		return 0;

	unsigned long addr = _neverc_krt_sym_resolver(name);
	if (addr) {
		__atomic_store_n(&_neverc_krt_sym_cache[idx].enc,
				 _neverc_krt_ptr_enc(addr), __ATOMIC_RELEASE);
		__atomic_store_n(&_neverc_krt_sym_cache[idx].hash,
				 h, __ATOMIC_RELEASE);
	}
	return addr;
}

#define kallsyms_lookup_name(name) _neverc_krt_sym_cached(name)

#define NEVERC_KRT_LOOKUP(sym)                                                       \
	((void *)kallsyms_lookup_name(NC_XORSTR(sym)))

#define NEVERC_KRT_RESOLVE(fnptr, sym)                                                \
	((fnptr) ? (fnptr) : ((fnptr) = (__typeof__(fnptr))NEVERC_KRT_LOOKUP(sym)))

#define NEVERC_KRT_LOOKUP_OR_FAIL(var, sym, errval)                                   \
	do {                                                                   \
		(var) = (__typeof__(var))NEVERC_KRT_LOOKUP(sym);                      \
		if (!(var)) return (errval);                                   \
	} while (0)

#define NEVERC_KRT_LOOKUP_FN(fnptr, sym)                                              \
	((fnptr) = (__typeof__(fnptr))NEVERC_KRT_LOOKUP(sym))

#define NEVERC_KRT_LOOKUP2(sym, alt)                                                  \
	({ void *__p = NEVERC_KRT_LOOKUP(sym);                                       \
	   if (!__p) __p = NEVERC_KRT_LOOKUP(alt);                                   \
	   __p; })

static __always_inline void neverc_krt_sym_cache_clear(void)
{
	unsigned long i;
	for (i = 0; i < _NEVERC_KRT_SYM_CACHE_SIZE; i++) {
		__atomic_store_n(&_neverc_krt_sym_cache[i].enc, 0, __ATOMIC_RELEASE);
		__atomic_store_n(&_neverc_krt_sym_cache[i].hash, 0, __ATOMIC_RELEASE);
	}
	__atomic_store_n(&_neverc_krt_cache_key, 0, __ATOMIC_RELEASE);
}

#endif /* _NEVERC_KRT_LINUX_KALLSYMS_H */
