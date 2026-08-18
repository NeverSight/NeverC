/*
 * ChaCha20 stream cipher — RFC 7539.
 * Pure C implementation.
 */
#include "neverc/std/crypto/chacha20.h"
#include "neverc/std/_platform.h"
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

/*
 * Keep the whole 16-word working state in named locals so it stays in
 * registers across all 20 rounds even at -O1 (an x[16] array reliably spills).
 */
void neverc_chacha20_block(const uint32_t state[16], uint8_t out[64]) {
    uint32_t x0  = state[0],  x1  = state[1],  x2  = state[2],  x3  = state[3];
    uint32_t x4  = state[4],  x5  = state[5],  x6  = state[6],  x7  = state[7];
    uint32_t x8  = state[8],  x9  = state[9],  x10 = state[10], x11 = state[11];
    uint32_t x12 = state[12], x13 = state[13], x14 = state[14], x15 = state[15];

    for (int i = 0; i < 10; i++) {
        QR(x0, x4, x8,  x12);
        QR(x1, x5, x9,  x13);
        QR(x2, x6, x10, x14);
        QR(x3, x7, x11, x15);
        QR(x0, x5, x10, x15);
        QR(x1, x6, x11, x12);
        QR(x2, x7, x8,  x13);
        QR(x3, x4, x9,  x14);
    }

    put_u32le(out +  0, x0  + state[0]);  put_u32le(out +  4, x1  + state[1]);
    put_u32le(out +  8, x2  + state[2]);  put_u32le(out + 12, x3  + state[3]);
    put_u32le(out + 16, x4  + state[4]);  put_u32le(out + 20, x5  + state[5]);
    put_u32le(out + 24, x6  + state[6]);  put_u32le(out + 28, x7  + state[7]);
    put_u32le(out + 32, x8  + state[8]);  put_u32le(out + 36, x9  + state[9]);
    put_u32le(out + 40, x10 + state[10]); put_u32le(out + 44, x11 + state[11]);
    put_u32le(out + 48, x12 + state[12]); put_u32le(out + 52, x13 + state[13]);
    put_u32le(out + 56, x14 + state[14]); put_u32le(out + 60, x15 + state[15]);
}

void neverc_chacha20_init(neverc_chacha20_ctx *ctx,
                          const uint8_t key[32],
                          const uint8_t nonce[12],
                          uint32_t counter) {
    if (!ctx) return;
    if (!key || !nonce) {
        neverc_platform_secure_zero(ctx, sizeof(*ctx));
        ctx->buf_used = -1;
        return;
    }
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

#if defined(__GNUC__) || defined(__clang__)
#define NCI_CHACHA_SIMD 1
/*
 * 4-way SIMD keystream: compute four consecutive blocks at once. Lane j of each
 * vector holds word i of the (counter+j)-th block, so the 20 rounds run as
 * plain vector add/xor/rotate with NO cross-lane shuffles (the four blocks are
 * independent). This is the layout that actually scales — unlike a single-block
 * row layout, whose diagonalization shuffles serialize the pipeline.
 */
typedef uint32_t nci_u32x4 __attribute__((vector_size(16)));
#define VROT(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#define VQR(a, b, c, d) do {       \
    a += b; d ^= a; d = VROT(d,16);\
    c += d; b ^= c; b = VROT(b,12);\
    a += b; d ^= a; d = VROT(d,8); \
    c += d; b ^= c; b = VROT(b,7); \
} while (0)

/* Produce 4 keystream blocks (256 bytes) for counters state[12]+0..+3. */
static void chacha20_4block(const uint32_t state[16], uint8_t out[256]) {
    nci_u32x4 v[16];
    for (int i = 0; i < 16; i++) {
        uint32_t s = state[i];
        v[i] = (nci_u32x4){ s, s, s, s };
    }
    uint32_t c0 = state[12];
    v[12] = (nci_u32x4){ c0, c0 + 1, c0 + 2, c0 + 3 };

    nci_u32x4 o[16];
    for (int i = 0; i < 16; i++) o[i] = v[i];

    for (int i = 0; i < 10; i++) {
        VQR(v[0], v[4], v[ 8], v[12]);
        VQR(v[1], v[5], v[ 9], v[13]);
        VQR(v[2], v[6], v[10], v[14]);
        VQR(v[3], v[7], v[11], v[15]);
        VQR(v[0], v[5], v[10], v[15]);
        VQR(v[1], v[6], v[11], v[12]);
        VQR(v[2], v[7], v[ 8], v[13]);
        VQR(v[3], v[4], v[ 9], v[14]);
    }
    for (int i = 0; i < 16; i++) v[i] += o[i];

    /* De-interleave: lane j of v[i] is word i of block j. */
    for (int i = 0; i < 16; i++) {
        uint32_t w[4];
        memcpy(w, &v[i], 16);
        put_u32le(out +  0 + 4 * i, w[0]);
        put_u32le(out + 64 + 4 * i, w[1]);
        put_u32le(out + 128 + 4 * i, w[2]);
        put_u32le(out + 192 + 4 * i, w[3]);
    }
}
#endif

/* dst[i] = src[i] ^ ks[i] for n bytes, 8 bytes at a time.
 * dst == src (in-place) and dst before src are safe with the forward path.
 * dst after src with overlap must walk backwards so we do not clobber unread src. */
static void xor_keystream(uint8_t *dst, const uint8_t *src,
                          const uint8_t *ks, size_t n) {
    if (n == 0)
        return;
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    if (d > s && (d - s) < (uintptr_t)n) {
        while (n-- > 0)
            dst[n] = src[n] ^ ks[n];
        return;
    }
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t s8, k;
        memcpy(&s8, src + i, 8);
        memcpy(&k, ks + i, 8);
        s8 ^= k;
        memcpy(dst + i, &s8, 8);
    }
    for (; i < n; i++) dst[i] = src[i] ^ ks[i];
}

int neverc_chacha20_xor_checked(neverc_chacha20_ctx *ctx,
                                uint8_t *dst, const uint8_t *src, size_t len) {
    size_t off = 0;
    if (!ctx || ctx->buf_used <= 0) return -1;
    if (len == 0) return 0;
    if (!dst || !src) return -1;

    /* Refuse a request that cannot be fully satisfied without wrapping the
     * 32-bit block counter. A partial XOR would leave plaintext in dst. */
    {
        int leftover = ctx->buf_used < 64;
        uint64_t avail = leftover ? (64u - (size_t)ctx->buf_used) : 0;
        if (!(leftover && ctx->state[12] == 0)) {
            uint64_t blocks =
                (uint64_t)(0xFFFFFFFFu - ctx->state[12]) + 1u;
            avail += blocks * 64u;
        }
        if ((uint64_t)len > avail)
            return -1;
    }

    /*
     * dst after src with overlap: leftover / 256-byte SIMD / 64-byte chunks
     * each call xor_keystream on a slice. A backward walk inside one slice
     * cannot save source bytes that an earlier slice already overwrote
     * (dst=src+4 and a 256-byte SIMD pass clobbers src[256..259]).
     * Slide the input into dst first, then XOR in place.
     */
    if (len > 0) {
        uintptr_t d = (uintptr_t)dst;
        uintptr_t s = (uintptr_t)src;
        if (d > s && (d - s) < (uintptr_t)len) {
            memmove(dst, src, len);
            src = dst;
        }
    }

    /* Consume any keystream left over from a previous partial block. */
    if (ctx->buf_used < 64) {
        size_t avail = 64 - (size_t)ctx->buf_used;
        size_t n = (len < avail) ? len : avail;
        xor_keystream(dst, src, ctx->buf + ctx->buf_used, n);
        ctx->buf_used += (int)n;
        off += n;
        /*
         * Increment-on-generate means leftover always belongs to counter-1.
         * state[12] == 0 after a leftover block therefore means that block
         * was the last valid counter (0xFFFFFFFF). Refuse further blocks
         * once those leftover bytes are gone, but not before.
         */
        if (ctx->buf_used == 64 && ctx->state[12] == 0)
            ctx->buf_used = -1;
        if (ctx->buf_used < 0)
            return 0;
    }

#ifdef NCI_CHACHA_SIMD
    /* Bulk path: 4 blocks (256 bytes) per SIMD pass.
     * Stay scalar when the counter cannot advance by 4 without wrapping,
     * so we never emit a reused counter-0 block inside the 4-block kernel. */
    while (len - off >= 256 && ctx->state[12] <= 0xFFFFFFFCu) {
        uint8_t ks[256];
        uint32_t before = ctx->state[12];
        chacha20_4block(ctx->state, ks);
        ctx->state[12] += 4;
        xor_keystream(dst + off, src + off, ks, 256);
        off += 256;
        if (ctx->state[12] < before) {
            ctx->buf_used = -1;
            return 0;
        }
    }
#endif

    /* Whole 64-byte blocks: generate keystream and XOR straight into dst,
     * skipping the per-byte loop and the intermediate-buffer round trip. */
    while (len - off >= 64) {
        uint8_t ks[64];
        uint32_t before = ctx->state[12];
        neverc_chacha20_block(ctx->state, ks);
        ctx->state[12]++;
        xor_keystream(dst + off, src + off, ks, 64);
        off += 64;
        if (ctx->state[12] < before) {
            ctx->buf_used = -1;
            return 0;
        }
    }

    /* Final partial block: keep the unused keystream in ctx for next call.
     * Even if the 32-bit counter wraps here, the rest of this block is still
     * valid keystream (RFC 7539); wrap is enforced when leftover is consumed. */
    if (off < len) {
        neverc_chacha20_block(ctx->state, ctx->buf);
        ctx->state[12]++;
        size_t n = len - off;
        xor_keystream(dst + off, src + off, ctx->buf, n);
        ctx->buf_used = (int)n;
    }
    return 0;
}

void neverc_chacha20_xor(neverc_chacha20_ctx *ctx,
                         uint8_t *dst, const uint8_t *src, size_t len) {
    (void)neverc_chacha20_xor_checked(ctx, dst, src, len);
}
