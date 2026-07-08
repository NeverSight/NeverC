#ifndef NEVERC_XXHASH_H
#define NEVERC_XXHASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * xxHash64 — extremely fast non-cryptographic hash.
 * Reference: https://github.com/Cyan4973/xxHash
 *
 * This is a minimal standalone implementation for the NeverC std library.
 * Produces results identical to XXH64() with the given seed.
 */
uint64_t neverc_xxhash64(const void *data, size_t len, uint64_t seed);

#ifdef __cplusplus
}
#endif

/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/hash.h>
#endif

#endif /* NEVERC_XXHASH_H */
