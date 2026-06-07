#ifndef NEVERC_SHA512_256_H
#define NEVERC_SHA512_256_H

#include <stdint.h>
#include <stddef.h>
#include "neverc/crypto/sha512.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SHA512_256_DIGEST_SIZE 32
#define NEVERC_SHA512_256_BLOCK_SIZE  128

typedef neverc_sha512_ctx neverc_sha512_256_ctx;

void neverc_sha512_256_init(neverc_sha512_256_ctx *ctx);
void neverc_sha512_256_update(neverc_sha512_256_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha512_256_final(neverc_sha512_256_ctx *ctx, uint8_t digest[32]);
void neverc_sha512_256_sum(const uint8_t *data, size_t len, uint8_t digest[32]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/crypto.h>
#endif

#endif /* NEVERC_SHA512_256_H */
