/*
 * SHA-1 implementation per FIPS 180-4.
 * Pure C, no libc dependency beyond stdint/string.
 */
#include "neverc/sha1.h"
#include <string.h>

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 |
           (uint32_t)p[2]<<8  | (uint32_t)p[3];
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

static void sha1_block(uint32_t state[5], const uint8_t block[64]) {
    uint32_t W[80];
    for (int i = 0; i < 16; i++)
        W[i] = be32(block + 4 * i);
    for (int i = 16; i < 80; i++)
        W[i] = ROTL(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = ROTL(a, 5) + f + e + k + W[i];
        e = d; d = c; c = ROTL(b, 30); b = a; a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void neverc_sha1_init(neverc_sha1_ctx *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

void neverc_sha1_update(neverc_sha1_ctx *ctx, const uint8_t *data, size_t len) {
    size_t buffered = (size_t)(ctx->count & 63);
    ctx->count += len;
    if (buffered > 0) {
        size_t need = 64 - buffered;
        if (len < need) { memcpy(ctx->buf + buffered, data, len); return; }
        memcpy(ctx->buf + buffered, data, need);
        sha1_block(ctx->state, ctx->buf);
        data += need; len -= need;
    }
    while (len >= 64) { sha1_block(ctx->state, data); data += 64; len -= 64; }
    if (len > 0) memcpy(ctx->buf, data, len);
}

void neverc_sha1_final(neverc_sha1_ctx *ctx, uint8_t digest[20]) {
    uint64_t bits = ctx->count * 8;
    size_t buffered = (size_t)(ctx->count & 63);
    ctx->buf[buffered++] = 0x80;
    if (buffered > 56) {
        memset(ctx->buf + buffered, 0, 64 - buffered);
        sha1_block(ctx->state, ctx->buf);
        buffered = 0;
    }
    memset(ctx->buf + buffered, 0, 56 - buffered);
    put_be64(ctx->buf + 56, bits);
    sha1_block(ctx->state, ctx->buf);
    for (int i = 0; i < 5; i++)
        put_be32(digest + 4 * i, ctx->state[i]);
}

void neverc_sha1_sum(const uint8_t *data, size_t len, uint8_t digest[20]) {
    neverc_sha1_ctx ctx;
    neverc_sha1_init(&ctx);
    neverc_sha1_update(&ctx, data, len);
    neverc_sha1_final(&ctx, digest);
}
