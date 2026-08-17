/*
 * SHA-1 implementation per FIPS 180-4.
 * Pure C, no libc dependency beyond stdint/string.
 */
#include "neverc/std/crypto/sha1.h"
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

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], t;

    /* Four fixed-function phases, five rounds unrolled with rotation-by-renaming.
     * This drops the per-round f/k branch (80 mispredict-free but dependency-
     * adding selects) and the working-variable shuffle. */
#define NCI_SHA1_R(a, b, c, d, e, f, k, i) \
        t = ROTL(a, 5) + (f) + (e) + (k) + W[i]; (e) = t; (b) = ROTL(b, 30);
#define NCI_SHA1_F1(b, c, d) (((b) & (c)) | (~(b) & (d)))
#define NCI_SHA1_F2(b, c, d) ((b) ^ (c) ^ (d))
#define NCI_SHA1_F3(b, c, d) (((b) & (c)) | ((b) & (d)) | ((c) & (d)))
    for (int i = 0; i < 20; i += 5) {
        NCI_SHA1_R(a,b,c,d,e, NCI_SHA1_F1(b,c,d), 0x5A827999, i+0)
        NCI_SHA1_R(e,a,b,c,d, NCI_SHA1_F1(a,b,c), 0x5A827999, i+1)
        NCI_SHA1_R(d,e,a,b,c, NCI_SHA1_F1(e,a,b), 0x5A827999, i+2)
        NCI_SHA1_R(c,d,e,a,b, NCI_SHA1_F1(d,e,a), 0x5A827999, i+3)
        NCI_SHA1_R(b,c,d,e,a, NCI_SHA1_F1(c,d,e), 0x5A827999, i+4)
    }
    for (int i = 20; i < 40; i += 5) {
        NCI_SHA1_R(a,b,c,d,e, NCI_SHA1_F2(b,c,d), 0x6ED9EBA1, i+0)
        NCI_SHA1_R(e,a,b,c,d, NCI_SHA1_F2(a,b,c), 0x6ED9EBA1, i+1)
        NCI_SHA1_R(d,e,a,b,c, NCI_SHA1_F2(e,a,b), 0x6ED9EBA1, i+2)
        NCI_SHA1_R(c,d,e,a,b, NCI_SHA1_F2(d,e,a), 0x6ED9EBA1, i+3)
        NCI_SHA1_R(b,c,d,e,a, NCI_SHA1_F2(c,d,e), 0x6ED9EBA1, i+4)
    }
    for (int i = 40; i < 60; i += 5) {
        NCI_SHA1_R(a,b,c,d,e, NCI_SHA1_F3(b,c,d), 0x8F1BBCDC, i+0)
        NCI_SHA1_R(e,a,b,c,d, NCI_SHA1_F3(a,b,c), 0x8F1BBCDC, i+1)
        NCI_SHA1_R(d,e,a,b,c, NCI_SHA1_F3(e,a,b), 0x8F1BBCDC, i+2)
        NCI_SHA1_R(c,d,e,a,b, NCI_SHA1_F3(d,e,a), 0x8F1BBCDC, i+3)
        NCI_SHA1_R(b,c,d,e,a, NCI_SHA1_F3(c,d,e), 0x8F1BBCDC, i+4)
    }
    for (int i = 60; i < 80; i += 5) {
        NCI_SHA1_R(a,b,c,d,e, NCI_SHA1_F2(b,c,d), 0xCA62C1D6, i+0)
        NCI_SHA1_R(e,a,b,c,d, NCI_SHA1_F2(a,b,c), 0xCA62C1D6, i+1)
        NCI_SHA1_R(d,e,a,b,c, NCI_SHA1_F2(e,a,b), 0xCA62C1D6, i+2)
        NCI_SHA1_R(c,d,e,a,b, NCI_SHA1_F2(d,e,a), 0xCA62C1D6, i+3)
        NCI_SHA1_R(b,c,d,e,a, NCI_SHA1_F2(c,d,e), 0xCA62C1D6, i+4)
    }
#undef NCI_SHA1_R
#undef NCI_SHA1_F1
#undef NCI_SHA1_F2
#undef NCI_SHA1_F3

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void neverc_sha1_init(neverc_sha1_ctx *ctx) {
    if (!ctx) return;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
    ctx->finalized = 0;
}

void neverc_sha1_update(neverc_sha1_ctx *ctx, const uint8_t *data, size_t len) {
    if (!ctx || ctx->finalized || len == 0) return;
    if (!data) return;
    /* SHA-1's length field is 64 bits (max 2^64-1 bits = 2^61-1 bytes).
     * Wrapping count*8 would make a huge message collide with a short one. */
    if (ctx->count > UINT64_MAX / 8 ||
        len > UINT64_MAX / 8 - ctx->count) {
        memset(ctx, 0, sizeof(*ctx));
        ctx->finalized = 1;
        return;
    }
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
    if (!ctx || !digest) return;
    if (ctx->finalized) {
        for (int i = 0; i < 5; i++)
            put_be32(digest + 4 * i, ctx->state[i]);
        return;
    }
    if (ctx->count > UINT64_MAX / 8) {
        memset(digest, 0, 20);
        memset(ctx, 0, sizeof(*ctx));
        ctx->finalized = 1;
        return;
    }
    uint64_t bits = ctx->count << 3;
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
    ctx->finalized = 1;
}

void neverc_sha1_sum(const uint8_t *data, size_t len, uint8_t digest[20]) {
    neverc_sha1_ctx ctx;
    neverc_sha1_init(&ctx);
    neverc_sha1_update(&ctx, data, len);
    neverc_sha1_final(&ctx, digest);
}
