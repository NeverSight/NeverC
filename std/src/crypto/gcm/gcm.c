/*
 * AES-GCM authenticated encryption — NIST SP 800-38D.
 * Pure C implementation with GHASH (GF(2^128) multiplication).
 */
#include "neverc/std/crypto/gcm.h"
#include "neverc/std/crypto/subtle.h"
#include "neverc/std/_platform.h"
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

/*
 * GHASH: multiplication in GF(2^128) with reducing polynomial
 * x^128 + x^7 + x^2 + x + 1 (0xE1 in MSB-first representation).
 *
 * The reference algorithm shifts the 128-bit accumulator right one bit per step
 * (128 iterations per block). This implementation uses Shoup's 4-bit table
 * method (as in OpenSSL / BoringSSL's portable path): precompute htab[i] = i*H
 * for every 4-bit value i, then consume two nibbles per input byte with a
 * table XOR and a 4-bit reduction — 32 steps per block instead of 128, roughly
 * an order of magnitude faster, the way introsort drops constant factors.
 *
 * Everything is derived from one trusted primitive, mulx1() (one "multiply by
 * x" = the reference's right-shift-plus-0xE1-reduction). htab and the 4-bit
 * reduction table rem4 are both built from mulx1 at init, so no magic field
 * constants are hardcoded — correctness is checked against the bit-serial
 * reference on random inputs.
 */

static uint64_t load_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] <<  8) | ((uint64_t)p[7]      );
}

static void store_be64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >>  8); p[7] = (uint8_t)(v      );
}

/* One GF(2^128) "multiply by x": right-shift the 128-bit (hi:lo) value one bit
 * and fold 0xE1 into the top byte when a 1 falls off the bottom. Identical to
 * the reference loop body's V update. */
static void mulx1(uint64_t *hi, uint64_t *lo) {
    uint64_t h = *hi, l = *lo;
    uint64_t carry = l & 1;
    *lo = (l >> 1) | (h << 63);
    *hi = (h >> 1) ^ (carry ? 0xE100000000000000ULL : 0);
}

/* Multiply by x^4 using the precomputed 4-bit reduction table. The four bits
 * that fall off (the low nibble of lo) only feed the high word, so rem4 stores
 * just that high-word contribution. */
static void mulx4(uint64_t *hi, uint64_t *lo, const uint64_t rem4[16]) {
    uint64_t h = *hi, l = *lo;
    uint64_t rem = l & 0xf;
    *lo = (l >> 4) | (h << 60);
    *hi = (h >> 4) ^ rem4[rem];
}

/* result = x * H, with H supplied via the context's precomputed tables. */
static void ghash_mul(const neverc_gcm_ctx *ctx, uint8_t result[16],
                      const uint8_t x[16]) {
    uint64_t zh = 0, zl = 0;
    for (int b = 15; b >= 0; b--) {
        unsigned byte = x[b];
        unsigned lo = byte & 0xf, hi = byte >> 4;
        mulx4(&zh, &zl, ctx->rem4);          /* nibble q = 2b+1 (low) */
        zh ^= ctx->htab[lo][0];
        zl ^= ctx->htab[lo][1];
        mulx4(&zh, &zl, ctx->rem4);          /* nibble q = 2b   (high) */
        zh ^= ctx->htab[hi][0];
        zl ^= ctx->htab[hi][1];
    }
    store_be64(result,     zh);
    store_be64(result + 8, zl);
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

/* Build the Shoup 4-bit tables from the hash subkey H. */
static void ghash_init_tables(neverc_gcm_ctx *ctx) {
    /* rem4[r] = high word of (x^4) applied to the bare nibble r (low word is 0
     * since r < 16 shifts out completely), derived from mulx1. */
    for (int r = 0; r < 16; r++) {
        uint64_t h = 0, l = (uint64_t)r;
        mulx1(&h, &l); mulx1(&h, &l); mulx1(&h, &l); mulx1(&h, &l);
        ctx->rem4[r] = h;
    }

    /* htab[2^k] = x^(3-k) * H; htab[0] = 0. */
    ctx->htab[0][0] = 0; ctx->htab[0][1] = 0;
    ctx->htab[8][0] = load_be64(ctx->h);
    ctx->htab[8][1] = load_be64(ctx->h + 8);
    for (int j = 8; j > 1; j >>= 1) {
        uint64_t h = ctx->htab[j][0], l = ctx->htab[j][1];
        mulx1(&h, &l);
        ctx->htab[j >> 1][0] = h;
        ctx->htab[j >> 1][1] = l;
    }
    /* Fill composite entries by linearity: htab[j+k] = htab[j] ^ htab[k]. */
    for (int j = 2; j < 16; j <<= 1)
        for (int k = 1; k < j; k++) {
            ctx->htab[j + k][0] = ctx->htab[j][0] ^ ctx->htab[k][0];
            ctx->htab[j + k][1] = ctx->htab[j][1] ^ ctx->htab[k][1];
        }
}

static int gcm_ctx_ready(const neverc_gcm_ctx *ctx) {
    int nr = ctx->aes.rounds;
    return nr == 10 || nr == 12 || nr == 14;
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
    ghash_init_tables(ctx);
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
        (uint64_t)aad_len > NEVERC_GCM_MAX_AAD_BYTES)
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

    uint8_t computed_tag[16];
    gcm_compute_tag(ctx, nonce, ciphertext, ct_len, aad, aad_len, computed_tag);

    int tag_ok = neverc_subtle_constant_time_compare(computed_tag, tag, 16);
    neverc_platform_secure_zero(computed_tag, sizeof(computed_tag));
    if (tag_ok != 1) return -1;

    if (ct_len > 0 &&
        gcm_ctr_encrypt(ctx, nonce, ciphertext, ct_len, plaintext) != 0)
        return -1;
    return 0;
}
