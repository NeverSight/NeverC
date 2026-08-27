#ifndef NEVERC_FNV_H
#define NEVERC_FNV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FNV-0, FNV-1, and FNV-1a non-cryptographic hash functions.
 *
 * FNV-0:  FNV-1 with an all-zero initial state
 * FNV-1:  hash = hash * prime; hash ^= byte
 * FNV-1a: hash ^= byte; hash = hash * prime
 *
 * Supported widths are 32, 64, 128, 256, 512, and 1024 bits.
 */

#define NEVERC_FNV32_OFFSET_BASIS UINT32_C(2166136261)
#define NEVERC_FNV64_OFFSET_BASIS UINT64_C(14695981039346656037)
#define NEVERC_FNV128_OFFSET_BASIS_HI UINT64_C(0x6c62272e07bb0142)
#define NEVERC_FNV128_OFFSET_BASIS_LO UINT64_C(0x62b821756295c58d)
#define NEVERC_FNV128_OFFSET_BASIS_INITIALIZER                         \
    {NEVERC_FNV128_OFFSET_BASIS_HI, NEVERC_FNV128_OFFSET_BASIS_LO}

typedef struct {
    uint64_t hi;
    uint64_t lo;
} neverc_fnv_128_t;

/* Wide values are numeric integers with the most-significant word first. */
typedef struct {
    uint64_t words[4];
} neverc_fnv_256_t;

typedef struct {
    uint64_t words[8];
} neverc_fnv_512_t;

typedef struct {
    uint64_t words[16];
} neverc_fnv_1024_t;

#define NEVERC_FNV256_OFFSET_BASIS_INITIALIZER                         \
    {{UINT64_C(0xdd268dbcaac55036), UINT64_C(0x2d98c384c4e576cc),      \
      UINT64_C(0xc8b1536847b6bbb3), UINT64_C(0x1023b4c8caee0535)}}

#define NEVERC_FNV512_OFFSET_BASIS_INITIALIZER                         \
    {{UINT64_C(0xb86db0b1171f4416), UINT64_C(0xdca1e50f309990ac),      \
      UINT64_C(0xac87d059c9000000), UINT64_C(0x0000000000000d21),      \
      UINT64_C(0xe948f68a34c192f6), UINT64_C(0x2ea79bc942dbe7ce),      \
      UINT64_C(0x182036415f56e34b), UINT64_C(0xac982aac4afe9fd9)}}

#define NEVERC_FNV1024_OFFSET_BASIS_INITIALIZER                        \
    {{UINT64_C(0x0000000000000000), UINT64_C(0x005f7a76758ecc4d),      \
      UINT64_C(0x32e56d5a591028b7), UINT64_C(0x4b29fc4223fdada1),      \
      UINT64_C(0x6c3bf34eda3674da), UINT64_C(0x9a21d90000000000),      \
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),      \
      UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000),      \
      UINT64_C(0x0000000000000000), UINT64_C(0x000000000004c6d7),      \
      UINT64_C(0xeb6e73802734510a), UINT64_C(0x555f256cc005ae55),      \
      UINT64_C(0x6bde8cc9c6a93b21), UINT64_C(0xaff4b16c71ee90b3)}}

/*
 * Continue a hash from an existing state.  Initialize the first chunk with
 * the matching offset basis above, then pass each returned value to the next
 * update. Start FNV-0 from an all-zero value and use the FNV-1 updater.
 * A null data pointer consumes no bytes.
 */
uint32_t neverc_fnv_update32(uint32_t hash, const void *data, size_t len);
uint32_t neverc_fnv_update32a(uint32_t hash, const void *data, size_t len);
uint64_t neverc_fnv_update64(uint64_t hash, const void *data, size_t len);
uint64_t neverc_fnv_update64a(uint64_t hash, const void *data, size_t len);
neverc_fnv_128_t neverc_fnv_update128(neverc_fnv_128_t hash,
                                      const void *data, size_t len);
neverc_fnv_128_t neverc_fnv_update128a(neverc_fnv_128_t hash,
                                       const void *data, size_t len);
neverc_fnv_256_t neverc_fnv_update256(neverc_fnv_256_t hash,
                                      const void *data, size_t len);
neverc_fnv_256_t neverc_fnv_update256a(neverc_fnv_256_t hash,
                                       const void *data, size_t len);
neverc_fnv_512_t neverc_fnv_update512(neverc_fnv_512_t hash,
                                      const void *data, size_t len);
neverc_fnv_512_t neverc_fnv_update512a(neverc_fnv_512_t hash,
                                       const void *data, size_t len);
neverc_fnv_1024_t neverc_fnv_update1024(neverc_fnv_1024_t hash,
                                        const void *data, size_t len);
neverc_fnv_1024_t neverc_fnv_update1024a(neverc_fnv_1024_t hash,
                                         const void *data, size_t len);

/* Hash one complete byte sequence from the standard offset basis. */
uint32_t neverc_fnv_sum32(const void *data, size_t len);
uint32_t neverc_fnv_sum32a(const void *data, size_t len);
uint64_t neverc_fnv_sum64(const void *data, size_t len);
uint64_t neverc_fnv_sum64a(const void *data, size_t len);
neverc_fnv_128_t neverc_fnv_sum128(const void *data, size_t len);
neverc_fnv_128_t neverc_fnv_sum128a(const void *data, size_t len);
neverc_fnv_256_t neverc_fnv_sum256(const void *data, size_t len);
neverc_fnv_256_t neverc_fnv_sum256a(const void *data, size_t len);
neverc_fnv_512_t neverc_fnv_sum512(const void *data, size_t len);
neverc_fnv_512_t neverc_fnv_sum512a(const void *data, size_t len);
neverc_fnv_1024_t neverc_fnv_sum1024(const void *data, size_t len);
neverc_fnv_1024_t neverc_fnv_sum1024a(const void *data, size_t len);

/* FNV-0 one-shot hashes. */
uint32_t neverc_fnv0_sum32(const void *data, size_t len);
uint64_t neverc_fnv0_sum64(const void *data, size_t len);
neverc_fnv_128_t neverc_fnv0_sum128(const void *data, size_t len);
neverc_fnv_256_t neverc_fnv0_sum256(const void *data, size_t len);
neverc_fnv_512_t neverc_fnv0_sum512(const void *data, size_t len);
neverc_fnv_1024_t neverc_fnv0_sum1024(const void *data, size_t len);

/*
 * Store numeric hash values in an explicit byte order.
 * A null output pointer is ignored.
 */
void neverc_fnv_store32_be(uint8_t out[4], uint32_t hash);
void neverc_fnv_store32_le(uint8_t out[4], uint32_t hash);
void neverc_fnv_store64_be(uint8_t out[8], uint64_t hash);
void neverc_fnv_store64_le(uint8_t out[8], uint64_t hash);
void neverc_fnv_store128_be(uint8_t out[16], neverc_fnv_128_t hash);
void neverc_fnv_store128_le(uint8_t out[16], neverc_fnv_128_t hash);
void neverc_fnv_store256_be(uint8_t out[32], neverc_fnv_256_t hash);
void neverc_fnv_store256_le(uint8_t out[32], neverc_fnv_256_t hash);
void neverc_fnv_store512_be(uint8_t out[64], neverc_fnv_512_t hash);
void neverc_fnv_store512_le(uint8_t out[64], neverc_fnv_512_t hash);
void neverc_fnv_store1024_be(uint8_t out[128], neverc_fnv_1024_t hash);
void neverc_fnv_store1024_le(uint8_t out[128], neverc_fnv_1024_t hash);

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
