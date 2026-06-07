/*
 * SHA-384 — FIPS 180-4.
 * Same compression as SHA-512, different IV, output truncated to 48 bytes.
 */
#include "neverc/crypto/sha384.h"
#include <string.h>

void neverc_sha384_init(neverc_sha384_ctx *ctx) {
    ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->state[1] = 0x629a292a367cd507ULL;
    ctx->state[2] = 0x9159015a3070dd17ULL;
    ctx->state[3] = 0x152fecd8f70e5939ULL;
    ctx->state[4] = 0x67332667ffc00b31ULL;
    ctx->state[5] = 0x8eb44a8768581511ULL;
    ctx->state[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->state[7] = 0x47b5481dbefa4fa4ULL;
    ctx->count = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
}

void neverc_sha384_update(neverc_sha384_ctx *ctx, const uint8_t *data, size_t len) {
    neverc_sha512_update(ctx, data, len);
}

void neverc_sha384_final(neverc_sha384_ctx *ctx, uint8_t digest[48]) {
    uint8_t full[64];
    neverc_sha512_final(ctx, full);
    memcpy(digest, full, 48);
}

void neverc_sha384_sum(const uint8_t *data, size_t len, uint8_t digest[48]) {
    neverc_sha384_ctx ctx;
    neverc_sha384_init(&ctx);
    neverc_sha384_update(&ctx, data, len);
    neverc_sha384_final(&ctx, digest);
}
