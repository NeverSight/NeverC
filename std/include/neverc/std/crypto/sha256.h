#ifndef NEVERC_SHA256_H
#define NEVERC_SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SHA256_DIGEST_SIZE 32
#define NEVERC_SHA256_BLOCK_SIZE  64

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
} neverc_sha256_ctx;

void neverc_sha256_init(neverc_sha256_ctx *ctx);
void neverc_sha256_update(neverc_sha256_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha256_final(neverc_sha256_ctx *ctx, uint8_t digest[32]);

/* One-shot: compute SHA-256 of data into digest */
void neverc_sha256_sum(const uint8_t *data, size_t len, uint8_t digest[32]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_SHA256_H */
