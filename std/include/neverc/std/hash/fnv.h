#ifndef NEVERC_FNV_H
#define NEVERC_FNV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FNV-1 and FNV-1a non-cryptographic hash functions
 * (mirrors Go hash/fnv package).
 *
 * FNV-1:  hash = hash * prime; hash ^= byte
 * FNV-1a: hash ^= byte; hash = hash * prime   (better avalanche)
 */

uint32_t neverc_fnv_32(const void *data, size_t len);
uint32_t neverc_fnv_32a(const void *data, size_t len);
uint64_t neverc_fnv_64(const void *data, size_t len);
uint64_t neverc_fnv_64a(const void *data, size_t len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/hash.h>
#endif

#endif /* NEVERC_FNV_H */
