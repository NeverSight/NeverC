/*
 * AES-GCM authenticated encryption — NIST SP 800-38D.
 * Pure C implementation with GHASH (GF(2^128) multiplication).
 */
#include "neverc/crypto/gcm.h"
#include <string.h>

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

/*
 * GHASH: multiplication in GF(2^128) with reducing polynomial
 * x^128 + x^7 + x^2 + x + 1 (0xE1 in MSB-first representation).
 */
static void ghash_mul(uint8_t result[16], const uint8_t x[16], const uint8_t h[16]) {
    uint8_t v[16];
    memcpy(v, h, 16);
    uint8_t z[16];
    memset(z, 0, 16);

    for (int i = 0; i < 128; i++) {
        if ((x[i / 8] >> (7 - (i % 8))) & 1)
            for (int j = 0; j < 16; j++) z[j] ^= v[j];

        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--)
            v[j] = (v[j] >> 1) | (v[j-1] << 7);
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xE1;
    }
    memcpy(result, z, 16);
}

static void ghash_update(uint8_t tag[16], const uint8_t h[16],
                         const uint8_t *data, size_t len) {
    uint8_t block[16];
    size_t i = 0;
    while (i + 16 <= len) {
        xor_block(block, tag, data + i, 16);
        ghash_mul(tag, block, h);
        i += 16;
    }
    if (i < len) {
        memset(block, 0, 16);
        memcpy(block, data + i, len - i);
        xor_block(block, tag, block, 16);
        ghash_mul(tag, block, h);
    }
}

int neverc_gcm_init(neverc_gcm_ctx *ctx, const uint8_t *key, int key_len) {
    if (neverc_aes_init(&ctx->aes, key, key_len) != 0)
        return -1;
    uint8_t zero[16] = {0};
    neverc_aes_encrypt_block(&ctx->aes, ctx->h, zero);
    return 0;
}

static void gcm_compute_tag(const neverc_gcm_ctx *ctx,
                            const uint8_t nonce[12],
                            const uint8_t *ciphertext, size_t ct_len,
                            const uint8_t *aad, size_t aad_len,
                            uint8_t tag[16]) {
    uint8_t ghash_val[16];
    memset(ghash_val, 0, 16);

    if (aad_len > 0)
        ghash_update(ghash_val, ctx->h, aad, aad_len);
    if (ct_len > 0)
        ghash_update(ghash_val, ctx->h, ciphertext, ct_len);

    /* Append len(A) || len(C) in bits */
    uint8_t len_block[16];
    put_be64(len_block, (uint64_t)aad_len * 8);
    put_be64(len_block + 8, (uint64_t)ct_len * 8);
    uint8_t tmp[16];
    xor_block(tmp, ghash_val, len_block, 16);
    ghash_mul(ghash_val, tmp, ctx->h);

    /* T = GHASH ^ E(K, J0) where J0 = nonce || 0x00000001 */
    uint8_t j0[16];
    memcpy(j0, nonce, 12);
    put_be32(j0 + 12, 1);
    uint8_t ej0[16];
    neverc_aes_encrypt_block(&ctx->aes, ej0, j0);
    xor_block(tag, ghash_val, ej0, 16);
}

static void gcm_ctr_encrypt(const neverc_gcm_ctx *ctx,
                            const uint8_t nonce[12],
                            const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t counter_block[16], keystream[16];
    memcpy(counter_block, nonce, 12);
    size_t offset = 0;
    uint32_t ctr = 2;
    while (offset < len) {
        put_be32(counter_block + 12, ctr);
        neverc_aes_encrypt_block(&ctx->aes, keystream, counter_block);
        size_t block_len = len - offset;
        if (block_len > 16) block_len = 16;
        xor_block(out + offset, in + offset, keystream, block_len);
        offset += block_len;
        ctr++;
    }
}

int neverc_gcm_seal(const neverc_gcm_ctx *ctx,
                    const uint8_t nonce[12],
                    const uint8_t *plaintext, size_t pt_len,
                    const uint8_t *aad, size_t aad_len,
                    uint8_t *ciphertext,
                    uint8_t tag[16]) {
    if (!ctx || !nonce || !tag) return -1;
    if (pt_len > 0)
        gcm_ctr_encrypt(ctx, nonce, plaintext, pt_len, ciphertext);
    gcm_compute_tag(ctx, nonce, ciphertext, pt_len, aad, aad_len, tag);
    return 0;
}

int neverc_gcm_open(const neverc_gcm_ctx *ctx,
                    const uint8_t nonce[12],
                    const uint8_t *ciphertext, size_t ct_len,
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t tag[16],
                    uint8_t *plaintext) {
    if (!ctx || !nonce || !tag) return -1;

    uint8_t computed_tag[16];
    gcm_compute_tag(ctx, nonce, ciphertext, ct_len, aad, aad_len, computed_tag);

    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= computed_tag[i] ^ tag[i];
    if (diff != 0) return -1;

    if (ct_len > 0)
        gcm_ctr_encrypt(ctx, nonce, ciphertext, ct_len, plaintext);
    return 0;
}
