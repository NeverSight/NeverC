#ifndef NEVERC_SHA512_224_H
#define NEVERC_SHA512_224_H

#include <stdint.h>
#include <stddef.h>
#include "neverc/std/crypto/sha512.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SHA512_224_DIGEST_SIZE 28
#define NEVERC_SHA512_224_BLOCK_SIZE  128

typedef neverc_sha512_ctx neverc_sha512_224_ctx;

void neverc_sha512_224_init(neverc_sha512_224_ctx *ctx);
void neverc_sha512_224_update(neverc_sha512_224_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha512_224_final(neverc_sha512_224_ctx *ctx, uint8_t digest[28]);
void neverc_sha512_224_sum(const uint8_t *data, size_t len, uint8_t digest[28]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_SHA512_224_H */
