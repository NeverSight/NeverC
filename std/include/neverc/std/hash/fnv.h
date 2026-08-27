#ifndef NEVERC_FNV_H
#define NEVERC_FNV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FNV-1 and FNV-1a non-cryptographic hash functions.
 *
 * FNV-1:  hash = hash * prime; hash ^= byte
 * FNV-1a: hash ^= byte; hash = hash * prime
 */

#define NEVERC_FNV32_OFFSET_BASIS UINT32_C(2166136261)
#define NEVERC_FNV64_OFFSET_BASIS UINT64_C(14695981039346656037)
#define NEVERC_FNV128_OFFSET_BASIS_HI UINT64_C(0x6c62272e07bb0142)
#define NEVERC_FNV128_OFFSET_BASIS_LO UINT64_C(0x62b821756295c58d)

typedef struct {
    uint64_t hi;
    uint64_t lo;
} neverc_fnv_128_t;

/*
 * Continue a hash from an existing state.  Initialize the first chunk with
 * the matching offset basis above, then pass each returned value to the next
 * update.  A null data pointer consumes no bytes.
 */
uint32_t neverc_fnv_update32(uint32_t hash, const void *data, size_t len);
uint32_t neverc_fnv_update32a(uint32_t hash, const void *data, size_t len);
uint64_t neverc_fnv_update64(uint64_t hash, const void *data, size_t len);
uint64_t neverc_fnv_update64a(uint64_t hash, const void *data, size_t len);
neverc_fnv_128_t neverc_fnv_update128(neverc_fnv_128_t hash,
                                      const void *data, size_t len);
neverc_fnv_128_t neverc_fnv_update128a(neverc_fnv_128_t hash,
                                       const void *data, size_t len);

/* Hash one complete byte sequence from the standard offset basis. */
uint32_t neverc_fnv_sum32(const void *data, size_t len);
uint32_t neverc_fnv_sum32a(const void *data, size_t len);
uint64_t neverc_fnv_sum64(const void *data, size_t len);
uint64_t neverc_fnv_sum64a(const void *data, size_t len);
neverc_fnv_128_t neverc_fnv_sum128(const void *data, size_t len);
neverc_fnv_128_t neverc_fnv_sum128a(const void *data, size_t len);

/* Backward-compat aliases */
#define neverc_fnv_32  neverc_fnv_sum32
#define neverc_fnv_32a neverc_fnv_sum32a
#define neverc_fnv_64  neverc_fnv_sum64
#define neverc_fnv_64a neverc_fnv_sum64a

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/hash.h>
#endif

#endif /* NEVERC_FNV_H */
