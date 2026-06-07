#ifndef NEVERC_HASH_MAPHASH_H
#define NEVERC_HASH_MAPHASH_H

/*
 * NeverC hash/maphash — fast non-cryptographic hash for hash tables
 * (mirrors Go hash/maphash package).
 *
 * Uses wyhash internally — fast, well-distributed, passes SMHasher.
 * NOT suitable for cryptographic purposes.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_MAPHASH_BUF_SIZE 128

typedef struct {
    uint64_t seed;
    uint64_t state;
    uint8_t  buf[NEVERC_MAPHASH_BUF_SIZE];
    int      n;
} neverc_maphash_t;

uint64_t neverc_maphash_make_seed(void);

void     neverc_maphash_init(neverc_maphash_t *h, uint64_t seed);
void     neverc_maphash_reset(neverc_maphash_t *h);
void     neverc_maphash_write(neverc_maphash_t *h, const void *data, size_t len);
void     neverc_maphash_write_byte(neverc_maphash_t *h, uint8_t b);
void     neverc_maphash_write_string(neverc_maphash_t *h, const char *s);
uint64_t neverc_maphash_sum64(const neverc_maphash_t *h);

uint64_t neverc_maphash_bytes(uint64_t seed, const void *data, size_t len);
uint64_t neverc_maphash_string(uint64_t seed, const char *s);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/hash.h>
#endif


#endif
