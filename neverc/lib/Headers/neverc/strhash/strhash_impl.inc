/*===---- neverc/strhash/strhash_impl.inc - runtime hash dispatch ----------===*\
 *
 * Runtime hash function implementation for NC_STRHASH / NC_STRHASH_AUTO.
 * Included by <neverc/strhash/strhash.h> -- do not include directly.
 *
 * Custom hash function:
 *   Define NC_STRHASH_HASH_FN(data, len) before including strhash.h
 *   to override the default algorithm.  The macro must expand to a
 *   uint64_t expression.
 *
 *   Example:
 *     #define NC_STRHASH_HASH_FN(data, len) my_custom_hash(data, len)
 *     #include <neverc/strhash/strhash.h>
 *
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_STRHASH_IMPL_INC_
#define _NEVERC_STRHASH_IMPL_INC_

#ifdef __neverc__

#ifdef NC_STRHASH_HASH_FN

static inline __attribute__((always_inline)) uint64_t
neverc_strhash_rt(const void *data, size_t len) {
  return (uint64_t)NC_STRHASH_HASH_FN(data, len);
}

#elif __NEVERC_STRHASH_ALGO__ == 1

static inline __attribute__((always_inline)) uint64_t
neverc_strhash_rt(const void *data, size_t len) {
  extern uint32_t neverc_fnv_sum32a(const void *, size_t);
  return (uint64_t)neverc_fnv_sum32a(data, len);
}

#elif __NEVERC_STRHASH_ALGO__ == 3

static inline __attribute__((always_inline)) uint64_t
neverc_strhash_rt(const void *data, size_t len) {
  extern uint64_t neverc_xxhash64(const void *, size_t, uint64_t);
  return neverc_xxhash64(data, len, 0);
}

#else

static inline __attribute__((always_inline)) uint64_t
neverc_strhash_rt(const void *data, size_t len) {
  extern uint64_t neverc_fnv_sum64a(const void *, size_t);
  return neverc_fnv_sum64a(data, len);
}

#endif /* NC_STRHASH_HASH_FN / __NEVERC_STRHASH_ALGO__ */

#endif /* __neverc__ */

#endif /* _NEVERC_STRHASH_IMPL_INC_ */
