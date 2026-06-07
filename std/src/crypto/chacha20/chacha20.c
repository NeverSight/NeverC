/*
 * ChaCha20 stream cipher — RFC 7539.
 * Pure C implementation.
 */
#include "neverc/crypto/chacha20.h"
#include <string.h>

static uint32_t get_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);       p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

#define QR(a, b, c, d) do {            \
    a += b; d ^= a; d = ROTL32(d, 16); \
    c += d; b ^= c; b = ROTL32(b, 12); \
    a += b; d ^= a; d = ROTL32(d, 8);  \
    c += d; b ^= c; b = ROTL32(b, 7);  \
} while (0)

void neverc_chacha20_block(const uint32_t state[16], uint8_t out[64]) {
    uint32_t x[16];
    memcpy(x, state, 64);

    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[ 8], x[12]);
        QR(x[1], x[5], x[ 9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[ 8], x[13]);
        QR(x[3], x[4], x[ 9], x[14]);
    }

    for (int i = 0; i < 16; i++)
        put_u32le(out + 4 * i, x[i] + state[i]);
}

void neverc_chacha20_init(neverc_chacha20_ctx *ctx,
                          const uint8_t key[32],
                          const uint8_t nonce[12],
                          uint32_t counter) {
    ctx->state[0]  = 0x61707865;
    ctx->state[1]  = 0x3320646e;
    ctx->state[2]  = 0x79622d32;
    ctx->state[3]  = 0x6b206574;
    for (int i = 0; i < 8; i++)
        ctx->state[4 + i] = get_u32le(key + 4 * i);
    ctx->state[12] = counter;
    ctx->state[13] = get_u32le(nonce);
    ctx->state[14] = get_u32le(nonce + 4);
    ctx->state[15] = get_u32le(nonce + 8);
    ctx->buf_used = 64;
}

void neverc_chacha20_xor(neverc_chacha20_ctx *ctx,
                         uint8_t *dst, const uint8_t *src, size_t len) {
    size_t off = 0;
    while (off < len) {
        if (ctx->buf_used >= 64) {
            neverc_chacha20_block(ctx->state, ctx->buf);
            ctx->state[12]++;
            ctx->buf_used = 0;
        }
        size_t avail = 64 - (size_t)ctx->buf_used;
        size_t n = (len - off < avail) ? (len - off) : avail;
        for (size_t i = 0; i < n; i++)
            dst[off + i] = src[off + i] ^ ctx->buf[ctx->buf_used + (int)i];
        ctx->buf_used += (int)n;
        off += n;
    }
}
