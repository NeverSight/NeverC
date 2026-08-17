/*
 * SHA-224 — FIPS 180-4.
 * Same compression as SHA-256, different IV, output truncated to 28 bytes.
 */
#include "neverc/std/crypto/sha224.h"
#include "neverc/std/_platform.h"
#include <string.h>

void neverc_sha224_init(neverc_sha224_ctx *ctx) {
    if (!ctx) return;
    ctx->state[0] = 0xc1059ed8; ctx->state[1] = 0x367cd507;
    ctx->state[2] = 0x3070dd17; ctx->state[3] = 0xf70e5939;
    ctx->state[4] = 0xffc00b31; ctx->state[5] = 0x68581511;
    ctx->state[6] = 0x64f98fa7; ctx->state[7] = 0xbefa4fa4;
    ctx->count = 0;
}

void neverc_sha224_update(neverc_sha224_ctx *ctx, const uint8_t *data, size_t len) {
    neverc_sha256_update(ctx, data, len);
}

void neverc_sha224_final(neverc_sha224_ctx *ctx, uint8_t digest[28]) {
    if (!ctx || !digest) return;
    uint8_t full[32];
    neverc_sha256_final(ctx, full);
    memcpy(digest, full, 28);
    neverc_platform_secure_zero(full, sizeof(full));
}

void neverc_sha224_sum(const uint8_t *data, size_t len, uint8_t digest[28]) {
    neverc_sha224_ctx ctx;
    neverc_sha224_init(&ctx);
    neverc_sha224_update(&ctx, data, len);
    neverc_sha224_final(&ctx, digest);
}
