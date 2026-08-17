/*
 * SHA-256 implementation per FIPS 180-4.
 * Pure C, no libc dependency beyond stdint/string.
 */
#include "neverc/std/crypto/sha256.h"
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

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

static void sha256_block(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    for (int i = 0; i < 16; i++)
        W[i] = be32(block + 4 * i);
    for (int i = 16; i < 64; i++)
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint32_t t1, t2;

    /* Eight rounds unrolled with rotation-by-renaming: the a..h working set is
     * advanced by argument order rather than copied each round, so there is no
     * per-round 8-way shuffle for the compiler to schedule. */
#define NCI_SHA256_R(a, b, c, d, e, f, g, h, i) \
        t1 = (h) + EP1(e) + CH(e, f, g) + K[i] + W[i]; \
        t2 = EP0(a) + MAJ(a, b, c); \
        (d) += t1; (h) = t1 + t2;
    for (int i = 0; i < 64; i += 8) {
        NCI_SHA256_R(a, b, c, d, e, f, g, h, i + 0)
        NCI_SHA256_R(h, a, b, c, d, e, f, g, i + 1)
        NCI_SHA256_R(g, h, a, b, c, d, e, f, i + 2)
        NCI_SHA256_R(f, g, h, a, b, c, d, e, i + 3)
        NCI_SHA256_R(e, f, g, h, a, b, c, d, i + 4)
        NCI_SHA256_R(d, e, f, g, h, a, b, c, i + 5)
        NCI_SHA256_R(c, d, e, f, g, h, a, b, i + 6)
        NCI_SHA256_R(b, c, d, e, f, g, h, a, i + 7)
    }
#undef NCI_SHA256_R

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void neverc_sha256_init(neverc_sha256_ctx *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

void neverc_sha256_update(neverc_sha256_ctx *ctx, const uint8_t *data, size_t len) {
    if (!ctx || ctx->finalized || len == 0) return;
    if (!data) return;
    /* SHA-256's length field is 64 bits (max 2^64-1 bits = 2^61-1 bytes).
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
        if (len < need) {
            memcpy(ctx->buf + buffered, data, len);
            return;
        }
        memcpy(ctx->buf + buffered, data, need);
        sha256_block(ctx->state, ctx->buf);
        data += need;
        len -= need;
    }
    while (len >= 64) {
        sha256_block(ctx->state, data);
        data += 64;
        len -= 64;
    }
    if (len > 0)
        memcpy(ctx->buf, data, len);
}

void neverc_sha256_final(neverc_sha256_ctx *ctx, uint8_t digest[32]) {
    if (!digest) return;
    if (!ctx) {
        memset(digest, 0, 32);
        return;
    }
    if (ctx->finalized) {
        for (int i = 0; i < 8; i++)
            put_be32(digest + 4 * i, ctx->state[i]);
        return;
    }
    if (ctx->count > UINT64_MAX / 8) {
        memset(digest, 0, 32);
        memset(ctx, 0, sizeof(*ctx));
        ctx->finalized = 1;
        return;
    }
    uint64_t bits = ctx->count << 3;
    size_t buffered = (size_t)(ctx->count & 63);

    ctx->buf[buffered++] = 0x80;
    if (buffered > 56) {
        memset(ctx->buf + buffered, 0, 64 - buffered);
        sha256_block(ctx->state, ctx->buf);
        buffered = 0;
    }
    memset(ctx->buf + buffered, 0, 56 - buffered);
    put_be64(ctx->buf + 56, bits);
    sha256_block(ctx->state, ctx->buf);

    for (int i = 0; i < 8; i++)
        put_be32(digest + 4 * i, ctx->state[i]);
    ctx->finalized = 1;
}

void neverc_sha256_sum(const uint8_t *data, size_t len, uint8_t digest[32]) {
    if (!digest) return;
    if (len > 0 && !data) {
        memset(digest, 0, 32);
        return;
    }
    neverc_sha256_ctx ctx;
    neverc_sha256_init(&ctx);
    neverc_sha256_update(&ctx, data, len);
    neverc_sha256_final(&ctx, digest);
}
