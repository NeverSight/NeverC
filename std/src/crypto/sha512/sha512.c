/*
 * SHA-512 implementation per FIPS 180-4.
 * Pure C, no libc dependency beyond stdint/string.
 */
#include "neverc/std/crypto/sha512.h"
#include <string.h>

static const uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

#define ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR64(x, 28) ^ ROTR64(x, 34) ^ ROTR64(x, 39))
#define EP1(x) (ROTR64(x, 14) ^ ROTR64(x, 18) ^ ROTR64(x, 41))
#define SIG0(x) (ROTR64(x, 1) ^ ROTR64(x, 8) ^ ((x) >> 7))
#define SIG1(x) (ROTR64(x, 19) ^ ROTR64(x, 61) ^ ((x) >> 6))

static uint64_t be64(const uint8_t *p) {
    return (uint64_t)p[0]<<56 | (uint64_t)p[1]<<48 | (uint64_t)p[2]<<40 | (uint64_t)p[3]<<32 |
           (uint64_t)p[4]<<24 | (uint64_t)p[5]<<16 | (uint64_t)p[6]<<8  | (uint64_t)p[7];
}

static void put_be64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);  p[7] = (uint8_t)v;
}

static void sha512_block(uint64_t state[8], const uint8_t block[128]) {
    uint64_t W[80];
    for (int i = 0; i < 16; i++)
        W[i] = be64(block + 8 * i);
    for (int i = 16; i < 80; i++)
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];

    uint64_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint64_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint64_t t1, t2;

    /* Eight rounds unrolled with rotation-by-renaming (see sha256.c). */
#define NCI_SHA512_R(a, b, c, d, e, f, g, h, i) \
        t1 = (h) + EP1(e) + CH(e, f, g) + K[i] + W[i]; \
        t2 = EP0(a) + MAJ(a, b, c); \
        (d) += t1; (h) = t1 + t2;
    for (int i = 0; i < 80; i += 8) {
        NCI_SHA512_R(a, b, c, d, e, f, g, h, i + 0)
        NCI_SHA512_R(h, a, b, c, d, e, f, g, i + 1)
        NCI_SHA512_R(g, h, a, b, c, d, e, f, i + 2)
        NCI_SHA512_R(f, g, h, a, b, c, d, e, i + 3)
        NCI_SHA512_R(e, f, g, h, a, b, c, d, i + 4)
        NCI_SHA512_R(d, e, f, g, h, a, b, c, i + 5)
        NCI_SHA512_R(c, d, e, f, g, h, a, b, i + 6)
        NCI_SHA512_R(b, c, d, e, f, g, h, a, i + 7)
    }
#undef NCI_SHA512_R

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void neverc_sha512_init(neverc_sha512_ctx *ctx) {
    if (!ctx) return;
    ctx->state[0] = 0x6a09e667f3bcc908ULL; ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL; ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL; ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL; ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->count = 0;
    ctx->finalized = 0;
}

void neverc_sha512_update(neverc_sha512_ctx *ctx, const uint8_t *data, size_t len) {
    if (!ctx || ctx->finalized || len == 0) return;
    if (!data) return;
    if (len > UINT64_MAX - ctx->count) {
        memset(ctx->state, 0, sizeof(ctx->state));
        ctx->finalized = 1;
        return;
    }
    size_t buffered = (size_t)(ctx->count & 127);
    ctx->count += len;
    if (buffered > 0) {
        size_t need = 128 - buffered;
        if (len < need) { memcpy(ctx->buf + buffered, data, len); return; }
        memcpy(ctx->buf + buffered, data, need);
        sha512_block(ctx->state, ctx->buf);
        data += need; len -= need;
    }
    while (len >= 128) { sha512_block(ctx->state, data); data += 128; len -= 128; }
    if (len > 0) memcpy(ctx->buf, data, len);
}

void neverc_sha512_final(neverc_sha512_ctx *ctx, uint8_t digest[64]) {
    if (!ctx || !digest) return;
    if (ctx->finalized) {
        for (int i = 0; i < 8; i++)
            put_be64(digest + 8 * i, ctx->state[i]);
        return;
    }
    /* FIPS 180-4: 128-bit bit-length. count is bytes; bits = count * 8
     * may exceed 2^64, so the high word is count >> 61, not always zero. */
    uint64_t bits_hi = ctx->count >> 61;
    uint64_t bits_lo = ctx->count << 3;
    size_t buffered = (size_t)(ctx->count & 127);
    ctx->buf[buffered++] = 0x80;
    if (buffered > 112) {
        memset(ctx->buf + buffered, 0, 128 - buffered);
        sha512_block(ctx->state, ctx->buf);
        buffered = 0;
    }
    memset(ctx->buf + buffered, 0, 112 - buffered);
    put_be64(ctx->buf + 112, bits_hi);
    put_be64(ctx->buf + 120, bits_lo);
    sha512_block(ctx->state, ctx->buf);
    for (int i = 0; i < 8; i++)
        put_be64(digest + 8 * i, ctx->state[i]);
    ctx->finalized = 1;
}

void neverc_sha512_sum(const uint8_t *data, size_t len, uint8_t digest[64]) {
    neverc_sha512_ctx ctx;
    neverc_sha512_init(&ctx);
    neverc_sha512_update(&ctx, data, len);
    neverc_sha512_final(&ctx, digest);
}
