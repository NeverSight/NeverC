#ifndef NEVERC_SHA384_H
#define NEVERC_SHA384_H

#include <stdint.h>
#include <stddef.h>
#include "neverc/crypto/sha512.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SHA384_DIGEST_SIZE 48
#define NEVERC_SHA384_BLOCK_SIZE  128

typedef neverc_sha512_ctx neverc_sha384_ctx;

void neverc_sha384_init(neverc_sha384_ctx *ctx);
void neverc_sha384_update(neverc_sha384_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha384_final(neverc_sha384_ctx *ctx, uint8_t digest[48]);
void neverc_sha384_sum(const uint8_t *data, size_t len, uint8_t digest[48]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/crypto.h>
#endif

#endif /* NEVERC_SHA384_H */
