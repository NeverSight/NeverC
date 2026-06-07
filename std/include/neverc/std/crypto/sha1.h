#ifndef NEVERC_SHA1_H
#define NEVERC_SHA1_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SHA1_DIGEST_SIZE 20
#define NEVERC_SHA1_BLOCK_SIZE  64

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buf[64];
} neverc_sha1_ctx;

void neverc_sha1_init(neverc_sha1_ctx *ctx);
void neverc_sha1_update(neverc_sha1_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha1_final(neverc_sha1_ctx *ctx, uint8_t digest[20]);
void neverc_sha1_sum(const uint8_t *data, size_t len, uint8_t digest[20]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_SHA1_H */
