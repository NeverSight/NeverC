/*
 * AES-GCM authenticated encryption — NIST SP 800-38D.
 * Pure C implementation with GHASH (GF(2^128) multiplication).
 */
#include "neverc/std/crypto/gcm.h"
#include "neverc/std/crypto/subtle.h"
#include "neverc/std/_platform.h"
#include <stdint.h>
#include <string.h>

#define NEVERC_GCM_MAX_TEXT_BYTES ((UINT64_C(1) << 36) - 32)
#define NEVERC_GCM_MAX_AAD_BYTES  ((UINT64_C(1) << 61) - 1)

static void xor_block(uint8_t *dst, const uint8_t *a, const uint8_t *b, size_t len) {
    for (size_t i = 0; i < len; i++) dst[i] = a[i] ^ b[i];
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

/* GHASH multiplication in GF(2^128), with polynomial
 * x^128 + x^7 + x^2 + x + 1. This fixed 128-step portable fallback uses no
 * secret-indexed table and branches only on public loop counters. */
static void ghash_mul(const neverc_gcm_ctx *ctx, uint8_t result[16],
                      const uint8_t x[16]) {
    uint8_t z[16] = {0};
    uint8_t v[16];
    memcpy(v, ctx->h, sizeof(v));
    for (unsigned bit = 0; bit < 128; bit++) {
        uint8_t selected = (uint8_t)(0u -
            ((x[bit >> 3] >> (7 - (bit & 7))) & 1u));
        for (int i = 0; i < 16; i++)
            z[i] ^= v[i] & selected;

        uint8_t reduce = (uint8_t)(0u - (v[15] & 1u));
        for (int i = 15; i > 0; i--)
            v[i] = (uint8_t)((v[i] >> 1) | (v[i - 1] << 7));
        v[0] = (uint8_t)((v[0] >> 1) ^ (0xe1u & reduce));
    }
    memcpy(result, z, sizeof(z));
    neverc_platform_secure_zero(z, sizeof(z));
    neverc_platform_secure_zero(v, sizeof(v));
}

static void ghash_update(const neverc_gcm_ctx *ctx, uint8_t tag[16],
                         const uint8_t *data, size_t len) {
    uint8_t block[16];
    size_t i = 0;
    while (len - i >= 16) {
        xor_block(block, tag, data + i, 16);
        ghash_mul(ctx, tag, block);
        i += 16;
    }
    if (i < len) {
        memset(block, 0, 16);
        memcpy(block, data + i, len - i);
        xor_block(block, tag, block, 16);
        ghash_mul(ctx, tag, block);
    }
}

static int gcm_ctx_ready(const neverc_gcm_ctx *ctx) {
    int nr = ctx->aes.rounds;
    return nr == 10 || nr == 12 || nr == 14;
}

static int gcm_spans_overlap(const void *a, size_t a_len,
                             const void *b, size_t b_len) {
    if (a_len == 0 || b_len == 0)
        return 0;
    uintptr_t ap = (uintptr_t)a;
    uintptr_t bp = (uintptr_t)b;
    return ap <= bp ? bp - ap < a_len : ap - bp < b_len;
}

int neverc_gcm_init(neverc_gcm_ctx *ctx, const uint8_t *key, int key_len) {
    if (!ctx) return -1;
    if (!key) {
        neverc_platform_secure_zero(ctx, sizeof(*ctx));
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    if (neverc_aes_init(&ctx->aes, key, key_len) != 0) {
        neverc_platform_secure_zero(ctx, sizeof(*ctx));
        return -1;
    }
    uint8_t zero[16] = {0};
    neverc_aes_encrypt_block(&ctx->aes, ctx->h, zero);
    return 0;
}

/* Finish GHASH with the length block and XOR E(K, J0). ghash_val is the
 * running GHASH of AAD || C. */
static void gcm_finish_tag(const neverc_gcm_ctx *ctx,
                           const uint8_t nonce[12],
                           uint8_t ghash_val[16],
                           size_t aad_len, size_t ct_len,
                           uint8_t tag[16]) {
    uint8_t len_block[16];
    put_be64(len_block, (uint64_t)aad_len * 8);
    put_be64(len_block + 8, (uint64_t)ct_len * 8);
    uint8_t tmp[16];
    xor_block(tmp, ghash_val, len_block, 16);
    ghash_mul(ctx, ghash_val, tmp);

    /* T = GHASH ^ E(K, J0) where J0 = nonce || 0x00000001 */
    uint8_t j0[16];
    memcpy(j0, nonce, 12);
    put_be32(j0 + 12, 1);
    uint8_t ej0[16];
    neverc_aes_encrypt_block(&ctx->aes, ej0, j0);
    xor_block(tag, ghash_val, ej0, 16);
    neverc_platform_secure_zero(len_block, sizeof(len_block));
    neverc_platform_secure_zero(tmp, sizeof(tmp));
    neverc_platform_secure_zero(j0, sizeof(j0));
    neverc_platform_secure_zero(ej0, sizeof(ej0));
}

static void gcm_compute_tag(const neverc_gcm_ctx *ctx,
                            const uint8_t nonce[12],
                            const uint8_t *ciphertext, size_t ct_len,
                            const uint8_t *aad, size_t aad_len,
                            uint8_t tag[16]) {
    uint8_t ghash_val[16];
    memset(ghash_val, 0, 16);

    if (aad_len > 0)
        ghash_update(ctx, ghash_val, aad, aad_len);
    if (ct_len > 0)
        ghash_update(ctx, ghash_val, ciphertext, ct_len);
    gcm_finish_tag(ctx, nonce, ghash_val, aad_len, ct_len, tag);
    neverc_platform_secure_zero(ghash_val, sizeof(ghash_val));
}

static int gcm_ctr_encrypt(const neverc_gcm_ctx *ctx,
                           const uint8_t nonce[12],
                           const uint8_t *in, size_t len, uint8_t *out) {
    /* Counters 2..2^32-1: at most 2^32-2 blocks. Check before writing so a
     * wrap cannot leave a partial plaintext (open) or ciphertext (seal). */
    uint64_t blocks = ((uint64_t)len + 15) / 16;
    if (blocks > (UINT64_C(1) << 32) - 2)
        return -1;

    /* dest-after-src: a later 16-byte XOR would read bytes an earlier write
     * already replaced. Slide into out first, then treat as in-place. */
    if (len > 0) {
        uintptr_t d = (uintptr_t)out;
        uintptr_t s = (uintptr_t)in;
        if (d > s && (d - s) < (uintptr_t)len) {
            memmove(out, in, len);
            in = out;
        }
    }

    uint8_t counter_block[16], keystream[16];
    memcpy(counter_block, nonce, 12);
    size_t offset = 0;
    uint32_t ctr = 2;
    while (offset < len) {
        if (ctr < 2) {
            neverc_platform_secure_zero(counter_block, sizeof(counter_block));
            neverc_platform_secure_zero(keystream, sizeof(keystream));
            return -1;
        }
        put_be32(counter_block + 12, ctr);
        neverc_aes_encrypt_block(&ctx->aes, keystream, counter_block);
        size_t block_len = len - offset;
        if (block_len > 16) block_len = 16;
        xor_block(out + offset, in + offset, keystream, block_len);
        offset += block_len;
        ctr++;
    }
    neverc_platform_secure_zero(counter_block, sizeof(counter_block));
    neverc_platform_secure_zero(keystream, sizeof(keystream));
    return 0;
}

int neverc_gcm_seal(const neverc_gcm_ctx *ctx,
                    const uint8_t nonce[12],
                    const uint8_t *plaintext, size_t pt_len,
                    const uint8_t *aad, size_t aad_len,
                    uint8_t *ciphertext,
                    uint8_t tag[16]) {
    if (!ctx || !gcm_ctx_ready(ctx) || !nonce || !tag ||
        (!plaintext && pt_len != 0) || (!ciphertext && pt_len != 0) ||
        (!aad && aad_len != 0) ||
        (uint64_t)pt_len > NEVERC_GCM_MAX_TEXT_BYTES ||
        (uint64_t)aad_len > NEVERC_GCM_MAX_AAD_BYTES ||
        gcm_spans_overlap(ciphertext, pt_len, tag, 16))
        return -1;

    /* Hash AAD before writing ciphertext so an overlapping AAD/output
     * layout cannot clobber the bytes that must be authenticated. */
    uint8_t ghash_val[16];
    memset(ghash_val, 0, 16);
    if (aad_len > 0)
        ghash_update(ctx, ghash_val, aad, aad_len);

    if (pt_len > 0) {
        if (gcm_ctr_encrypt(ctx, nonce, plaintext, pt_len, ciphertext) != 0) {
            neverc_platform_secure_zero(ghash_val, sizeof(ghash_val));
            return -1;
        }
        ghash_update(ctx, ghash_val, ciphertext, pt_len);
    }
    gcm_finish_tag(ctx, nonce, ghash_val, aad_len, pt_len, tag);
    neverc_platform_secure_zero(ghash_val, sizeof(ghash_val));
    return 0;
}

int neverc_gcm_open(const neverc_gcm_ctx *ctx,
                    const uint8_t nonce[12],
                    const uint8_t *ciphertext, size_t ct_len,
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t tag[16],
                    uint8_t *plaintext) {
    if (!ctx || !gcm_ctx_ready(ctx) || !nonce || !tag ||
        (!ciphertext && ct_len != 0) || (!plaintext && ct_len != 0) ||
        (!aad && aad_len != 0) ||
        (uint64_t)ct_len > NEVERC_GCM_MAX_TEXT_BYTES ||
        (uint64_t)aad_len > NEVERC_GCM_MAX_AAD_BYTES)
        return -1;

    uint8_t nonce_copy[12];
    memcpy(nonce_copy, nonce, sizeof(nonce_copy));

    uint8_t computed_tag[16];
    gcm_compute_tag(ctx, nonce_copy, ciphertext, ct_len,
                    aad, aad_len, computed_tag);

    int tag_ok = neverc_subtle_constant_time_compare(computed_tag, tag, 16);
    neverc_platform_secure_zero(computed_tag, sizeof(computed_tag));
    if (tag_ok != 1) {
        neverc_platform_secure_zero(nonce_copy, sizeof(nonce_copy));
        return -1;
    }

    int rc = 0;
    if (ct_len > 0 && gcm_ctr_encrypt(ctx, nonce_copy, ciphertext,
                                     ct_len, plaintext) != 0)
        rc = -1;
    neverc_platform_secure_zero(nonce_copy, sizeof(nonce_copy));
    return rc;
}
