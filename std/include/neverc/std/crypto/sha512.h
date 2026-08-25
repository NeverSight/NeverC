#ifndef NEVERC_SHA512_H
#define NEVERC_SHA512_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SHA512_DIGEST_SIZE 64
#define NEVERC_SHA512_BLOCK_SIZE  128

typedef struct {
    uint64_t state[8];
    uint64_t count; /* byte count; UINT64_MAX is reserved after final/failure */
    uint8_t  buf[128];
} neverc_sha512_ctx;

void neverc_sha512_init(neverc_sha512_ctx *ctx);
/* Cumulative input must stay below UINT64_MAX bytes. Reaching that reserved
 * value wipes ctx and fails closed: later updates are ignored and final emits
 * an all-zero digest until ctx is reinitialized. */
void neverc_sha512_update(neverc_sha512_ctx *ctx, const uint8_t *data, size_t len);
/* final consumes ctx; later updates are ignored and repeated final calls return
 * the same digest. count becomes UINT64_MAX. The digest output must not
 * overlap ctx. */
void neverc_sha512_final(neverc_sha512_ctx *ctx, uint8_t digest[64]);
void neverc_sha512_sum(const uint8_t *data, size_t len, uint8_t digest[64]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_SHA512_H */
