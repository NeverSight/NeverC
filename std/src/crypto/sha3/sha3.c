/*
 * SHA-3 (Keccak) — FIPS 202.
 * Pure C implementation of Keccak-f[1600] sponge construction.
 * Supports SHA3-224/256/384/512 and SHAKE128/256.
 */
#include "neverc/crypto/sha3.h"
#include <string.h>

static const uint64_t RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808AULL, 0x8000000080008000ULL,
    0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008AULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

#define ROT64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

static void keccak_f1600(uint64_t st[25]) {
    for (int round = 0; round < 24; round++) {
        /* theta */
        uint64_t bc[5];
        for (int i = 0; i < 5; i++)
            bc[i] = st[i] ^ st[i+5] ^ st[i+10] ^ st[i+15] ^ st[i+20];
        for (int i = 0; i < 5; i++) {
            uint64_t t = bc[(i+4)%5] ^ ROT64(bc[(i+1)%5], 1);
            for (int j = 0; j < 25; j += 5) st[j+i] ^= t;
        }

        /* rho + pi */
        uint64_t tmp = st[1];
        static const int piln[24] = {
            10,  7, 11, 17, 18,  3,  5, 16,  8, 21, 24,  4,
            15, 23, 19, 13, 12,  2, 20, 14, 22,  9,  6,  1,
        };
        static const int rotc[24] = {
             1,  3,  6, 10, 15, 21, 28, 36, 45, 55,  2, 14,
            27, 41, 56,  8, 25, 43, 62, 18, 39, 61, 20, 44,
        };
        for (int i = 0; i < 24; i++) {
            int j = piln[i];
            uint64_t t2 = st[j];
            st[j] = ROT64(tmp, rotc[i]);
            tmp = t2;
        }

        /* chi */
        for (int j = 0; j < 25; j += 5) {
            uint64_t t0 = st[j+0], t1 = st[j+1], t2 = st[j+2];
            uint64_t t3 = st[j+3], t4 = st[j+4];
            st[j+0] = t0 ^ (~t1 & t2);
            st[j+1] = t1 ^ (~t2 & t3);
            st[j+2] = t2 ^ (~t3 & t4);
            st[j+3] = t3 ^ (~t4 & t0);
            st[j+4] = t4 ^ (~t0 & t1);
        }

        /* iota */
        st[0] ^= RC[round];
    }
}

static void sha3_init(neverc_sha3_ctx *ctx, size_t capacity_bits, uint8_t suffix) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->rate = 200 - capacity_bits / 8;
    ctx->suffix = suffix;
}

static void sha3_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        size_t room = ctx->rate - ctx->buf_len;
        size_t chunk = (len - i < room) ? len - i : room;
        memcpy(ctx->buf + ctx->buf_len, data + i, chunk);
        ctx->buf_len += chunk;
        i += chunk;

        if (ctx->buf_len == ctx->rate) {
            for (size_t j = 0; j < ctx->rate / 8; j++) {
                uint64_t w;
                memcpy(&w, ctx->buf + j * 8, 8);
                ctx->state[j] ^= w;
            }
            keccak_f1600(ctx->state);
            ctx->buf_len = 0;
        }
    }
}

static void sha3_pad_and_squeeze(neverc_sha3_ctx *ctx, uint8_t *out, size_t outlen) {
    /* Apply domain separation suffix and multi-rate padding */
    ctx->buf[ctx->buf_len] = ctx->suffix;
    memset(ctx->buf + ctx->buf_len + 1, 0, ctx->rate - ctx->buf_len - 1);
    ctx->buf[ctx->rate - 1] |= 0x80;

    for (size_t j = 0; j < ctx->rate / 8; j++) {
        uint64_t w;
        memcpy(&w, ctx->buf + j * 8, 8);
        ctx->state[j] ^= w;
    }
    keccak_f1600(ctx->state);

    /* Squeeze output */
    size_t offset = 0;
    while (offset < outlen) {
        size_t block = ctx->rate;
        if (outlen - offset < block) block = outlen - offset;
        uint8_t lane_bytes[200];
        for (size_t j = 0; j < ctx->rate / 8; j++)
            memcpy(lane_bytes + j * 8, &ctx->state[j], 8);
        memcpy(out + offset, lane_bytes, block);
        offset += block;
        if (offset < outlen)
            keccak_f1600(ctx->state);
    }
}

/* ===== SHA3-224 ===== */

void neverc_sha3_224_init(neverc_sha3_ctx *ctx)  { sha3_init(ctx, 448, 0x06); }
void neverc_sha3_224_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len) { sha3_update(ctx, data, len); }
void neverc_sha3_224_final(neverc_sha3_ctx *ctx, uint8_t digest[28])  { sha3_pad_and_squeeze(ctx, digest, 28); }
void neverc_sha3_224_sum(const uint8_t *data, size_t len, uint8_t digest[28]) {
    neverc_sha3_ctx ctx; neverc_sha3_224_init(&ctx);
    neverc_sha3_224_update(&ctx, data, len);
    neverc_sha3_224_final(&ctx, digest);
}

/* ===== SHA3-256 ===== */

void neverc_sha3_256_init(neverc_sha3_ctx *ctx)  { sha3_init(ctx, 512, 0x06); }
void neverc_sha3_256_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len) { sha3_update(ctx, data, len); }
void neverc_sha3_256_final(neverc_sha3_ctx *ctx, uint8_t digest[32])  { sha3_pad_and_squeeze(ctx, digest, 32); }
void neverc_sha3_256_sum(const uint8_t *data, size_t len, uint8_t digest[32]) {
    neverc_sha3_ctx ctx; neverc_sha3_256_init(&ctx);
    neverc_sha3_256_update(&ctx, data, len);
    neverc_sha3_256_final(&ctx, digest);
}

/* ===== SHA3-384 ===== */

void neverc_sha3_384_init(neverc_sha3_ctx *ctx)  { sha3_init(ctx, 768, 0x06); }
void neverc_sha3_384_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len) { sha3_update(ctx, data, len); }
void neverc_sha3_384_final(neverc_sha3_ctx *ctx, uint8_t digest[48])  { sha3_pad_and_squeeze(ctx, digest, 48); }
void neverc_sha3_384_sum(const uint8_t *data, size_t len, uint8_t digest[48]) {
    neverc_sha3_ctx ctx; neverc_sha3_384_init(&ctx);
    neverc_sha3_384_update(&ctx, data, len);
    neverc_sha3_384_final(&ctx, digest);
}

/* ===== SHA3-512 ===== */

void neverc_sha3_512_init(neverc_sha3_ctx *ctx)  { sha3_init(ctx, 1024, 0x06); }
void neverc_sha3_512_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len) { sha3_update(ctx, data, len); }
void neverc_sha3_512_final(neverc_sha3_ctx *ctx, uint8_t digest[64])  { sha3_pad_and_squeeze(ctx, digest, 64); }
void neverc_sha3_512_sum(const uint8_t *data, size_t len, uint8_t digest[64]) {
    neverc_sha3_ctx ctx; neverc_sha3_512_init(&ctx);
    neverc_sha3_512_update(&ctx, data, len);
    neverc_sha3_512_final(&ctx, digest);
}

/* ===== SHAKE128 ===== */

void neverc_shake128_init(neverc_sha3_ctx *ctx)   { sha3_init(ctx, 256, 0x1F); }
void neverc_shake128_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len) { sha3_update(ctx, data, len); }
void neverc_shake128_squeeze(neverc_sha3_ctx *ctx, uint8_t *out, size_t outlen) {
    if (!ctx->squeezed) {
        sha3_pad_and_squeeze(ctx, out, outlen);
        ctx->squeezed = 1;
    } else {
        size_t offset = 0;
        while (offset < outlen) {
            keccak_f1600(ctx->state);
            size_t block = ctx->rate;
            if (outlen - offset < block) block = outlen - offset;
            uint8_t lane_bytes[200];
            for (size_t j = 0; j < ctx->rate / 8; j++)
                memcpy(lane_bytes + j * 8, &ctx->state[j], 8);
            memcpy(out + offset, lane_bytes, block);
            offset += block;
        }
    }
}

/* ===== SHAKE256 ===== */

void neverc_shake256_init(neverc_sha3_ctx *ctx)   { sha3_init(ctx, 512, 0x1F); }
void neverc_shake256_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len) { sha3_update(ctx, data, len); }
void neverc_shake256_squeeze(neverc_sha3_ctx *ctx, uint8_t *out, size_t outlen) {
    if (!ctx->squeezed) {
        sha3_pad_and_squeeze(ctx, out, outlen);
        ctx->squeezed = 1;
    } else {
        size_t offset = 0;
        while (offset < outlen) {
            keccak_f1600(ctx->state);
            size_t block = ctx->rate;
            if (outlen - offset < block) block = outlen - offset;
            uint8_t lane_bytes[200];
            for (size_t j = 0; j < ctx->rate / 8; j++)
                memcpy(lane_bytes + j * 8, &ctx->state[j], 8);
            memcpy(out + offset, lane_bytes, block);
            offset += block;
        }
    }
}
