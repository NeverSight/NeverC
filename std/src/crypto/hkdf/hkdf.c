/*
 * HKDF — HMAC-based Key Derivation Function (RFC 5869).
 * Uses HMAC-SHA256 / HMAC-SHA512 as the underlying PRF.
 *
 * Performance: HKDF-Expand calls HMAC(prk, ...) once per output block, and
 * the PRK never changes.  Rather than re-absorbing the (prk^ipad)/(prk^opad)
 * blocks on every block, we precompute the two hash midstates once and resume
 * from them, halving the compressions per output block.
 */
#include "neverc/std/crypto/hkdf.h"
#include "neverc/std/crypto/hmac.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/sha512.h"
#include <string.h>

/* ---- HMAC-SHA256 with precomputed key midstates ---- */

typedef struct {
    neverc_sha256_ctx ipad;
    neverc_sha256_ctx opad;
} hmac256_pre;

static void hmac256_pre_init(hmac256_pre *p, const uint8_t *key, size_t key_len) {
    uint8_t k[64];
    memset(k, 0, sizeof(k));
    if (key_len > 64)
        neverc_sha256_sum(key, key_len, k);
    else
        memcpy(k, key, key_len);

    uint8_t pad[64];
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    neverc_sha256_init(&p->ipad);
    neverc_sha256_update(&p->ipad, pad, 64);

    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    neverc_sha256_init(&p->opad);
    neverc_sha256_update(&p->opad, pad, 64);
}

static void hmac256_pre_compute(const hmac256_pre *p,
                                const uint8_t *previous, size_t previous_len,
                                const uint8_t *info, size_t info_len,
                                uint8_t counter,
                                uint8_t out[32]) {
    neverc_sha256_ctx c = p->ipad;
    if (previous_len > 0)
        neverc_sha256_update(&c, previous, previous_len);
    if (info != NULL && info_len > 0)
        neverc_sha256_update(&c, info, info_len);
    neverc_sha256_update(&c, &counter, 1);
    uint8_t inner[32];
    neverc_sha256_final(&c, inner);

    c = p->opad;
    neverc_sha256_update(&c, inner, 32);
    neverc_sha256_final(&c, out);
}

/* ---- HMAC-SHA512 with precomputed key midstates ---- */

typedef struct {
    neverc_sha512_ctx ipad;
    neverc_sha512_ctx opad;
} hmac512_pre;

static void hmac512_pre_init(hmac512_pre *p, const uint8_t *key, size_t key_len) {
    uint8_t k[128];
    memset(k, 0, sizeof(k));
    if (key_len > 128)
        neverc_sha512_sum(key, key_len, k);
    else
        memcpy(k, key, key_len);

    uint8_t pad[128];
    for (int i = 0; i < 128; i++) pad[i] = k[i] ^ 0x36;
    neverc_sha512_init(&p->ipad);
    neverc_sha512_update(&p->ipad, pad, 128);

    for (int i = 0; i < 128; i++) pad[i] = k[i] ^ 0x5c;
    neverc_sha512_init(&p->opad);
    neverc_sha512_update(&p->opad, pad, 128);
}

static void hmac512_pre_compute(const hmac512_pre *p,
                                const uint8_t *previous, size_t previous_len,
                                const uint8_t *info, size_t info_len,
                                uint8_t counter,
                                uint8_t out[64]) {
    neverc_sha512_ctx c = p->ipad;
    if (previous_len > 0)
        neverc_sha512_update(&c, previous, previous_len);
    if (info != NULL && info_len > 0)
        neverc_sha512_update(&c, info, info_len);
    neverc_sha512_update(&c, &counter, 1);
    uint8_t inner[64];
    neverc_sha512_final(&c, inner);

    c = p->opad;
    neverc_sha512_update(&c, inner, 64);
    neverc_sha512_final(&c, out);
}

/* ---- HKDF-SHA256 ---- */

int neverc_hkdf_extract_sha256(uint8_t prk[32],
                               const uint8_t *salt, size_t salt_len,
                               const uint8_t *ikm, size_t ikm_len) {
    uint8_t default_salt[32];
    if (salt == NULL || salt_len == 0) {
        memset(default_salt, 0, 32);
        salt = default_salt;
        salt_len = 32;
    }
    neverc_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
    return 0;
}

int neverc_hkdf_expand_sha256(uint8_t *okm, size_t okm_len,
                              const uint8_t prk[32],
                              const uint8_t *info, size_t info_len) {
    if (okm_len > 255 * 32) return -1;

    hmac256_pre pre;
    hmac256_pre_init(&pre, prk, 32);

    uint8_t t[32];
    size_t t_len = 0;
    size_t off = 0;
    uint8_t counter = 1;

    while (off < okm_len) {
        hmac256_pre_compute(&pre, t, t_len, info, info_len, counter, t);
        t_len = 32;

        size_t n = okm_len - off;
        if (n > 32) n = 32;
        memcpy(okm + off, t, n);
        off += n;
        counter++;
    }

    return 0;
}

int neverc_hkdf_sha256(uint8_t *okm, size_t okm_len,
                       const uint8_t *ikm, size_t ikm_len,
                       const uint8_t *salt, size_t salt_len,
                       const uint8_t *info, size_t info_len) {
    uint8_t prk[32];
    neverc_hkdf_extract_sha256(prk, salt, salt_len, ikm, ikm_len);
    return neverc_hkdf_expand_sha256(okm, okm_len, prk, info, info_len);
}

/* ---- HKDF-SHA512 ---- */

int neverc_hkdf_extract_sha512(uint8_t prk[64],
                               const uint8_t *salt, size_t salt_len,
                               const uint8_t *ikm, size_t ikm_len) {
    uint8_t default_salt[64];
    if (salt == NULL || salt_len == 0) {
        memset(default_salt, 0, 64);
        salt = default_salt;
        salt_len = 64;
    }
    neverc_hmac_sha512(salt, salt_len, ikm, ikm_len, prk);
    return 0;
}

int neverc_hkdf_expand_sha512(uint8_t *okm, size_t okm_len,
                              const uint8_t prk[64],
                              const uint8_t *info, size_t info_len) {
    if (okm_len > 255 * 64) return -1;

    hmac512_pre pre;
    hmac512_pre_init(&pre, prk, 64);

    uint8_t t[64];
    size_t t_len = 0;
    size_t off = 0;
    uint8_t counter = 1;

    while (off < okm_len) {
        hmac512_pre_compute(&pre, t, t_len, info, info_len, counter, t);
        t_len = 64;

        size_t n = okm_len - off;
        if (n > 64) n = 64;
        memcpy(okm + off, t, n);
        off += n;
        counter++;
    }

    return 0;
}

int neverc_hkdf_sha512(uint8_t *okm, size_t okm_len,
                       const uint8_t *ikm, size_t ikm_len,
                       const uint8_t *salt, size_t salt_len,
                       const uint8_t *info, size_t info_len) {
    uint8_t prk[64];
    neverc_hkdf_extract_sha512(prk, salt, salt_len, ikm, ikm_len);
    return neverc_hkdf_expand_sha512(okm, okm_len, prk, info, info_len);
}
