#ifndef NEVERC_SHA224_H
#define NEVERC_SHA224_H

#include <stdint.h>
#include <stddef.h>
#include "neverc/crypto/sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SHA224_DIGEST_SIZE 28
#define NEVERC_SHA224_BLOCK_SIZE  64

typedef neverc_sha256_ctx neverc_sha224_ctx;

void neverc_sha224_init(neverc_sha224_ctx *ctx);
void neverc_sha224_update(neverc_sha224_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha224_final(neverc_sha224_ctx *ctx, uint8_t digest[28]);
void neverc_sha224_sum(const uint8_t *data, size_t len, uint8_t digest[28]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/crypto.h>
#endif

#endif /* NEVERC_SHA224_H */
