/*===---- neverc/strhash/strhash.h - compile-time string hashing for C ----===*\
 *
 * Wraps the __builtin_neverc_strhash compiler builtin so that plain C
 * (and C++) source can compute string-literal hashes at compile time.
 *
 * Usage:
 *     #include <neverc/strhash/strhash.h>
 *
 *     // Compile-time hash (resolves to an integer literal):
 *     uint64_t h = NC_STRHASH("hello");
 *
 *     // Runtime hash (matches the same algorithm selected by -fstrhash-algo):
 *     uint64_t r = neverc_strhash_rt(name, len);
 *
 * The default algorithm is FNV-1a 64-bit.  Use -fstrhash-algo=<algo> to
 * select a different one (fnv32a, fnv64a, xxhash64).  Both the builtin
 * and the runtime function will use the selected algorithm, ensuring
 * compile-time and runtime hashes always match.
 *
 * Custom hash function:
 *     #define NC_STRHASH_HASH_FN(data, len) my_hash(data, len)
 *     #include <neverc/strhash/strhash.h>
 *
 *     This overrides the runtime hash to use your own function.
 *     NC_STRHASH() (compile-time) still uses the builtin algorithm.
 *
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_STRHASH_H_
#define _NEVERC_STRHASH_H_

#include <stddef.h>
#include <stdint.h>

/* Compile-time hash — replaced by a constant during compilation.
 * Only accepts string literals; use NC_STRHASH_AUTO for variables. */
#define NC_STRHASH(s) __builtin_neverc_strhash(s)
#define NEVERC_STRHASH(s) __builtin_neverc_strhash(s)

/* Auto-dispatch hash — accepts both string literals and variables.
 * When the argument is a string literal and -fstrhash-fold is enabled,
 * the IR pass folds this to a compile-time constant automatically.
 * When the argument is a variable, it remains a runtime call. */
#define NC_STRHASH_AUTO(s) neverc_strhash_rt((s), __builtin_strlen(s))
#define NEVERC_STRHASH_AUTO(s) neverc_strhash_rt((s), __builtin_strlen(s))

#include "strhash_impl.inc"

#endif /* _NEVERC_STRHASH_H_ */
