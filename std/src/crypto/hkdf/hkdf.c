/*
 * HKDF — HMAC-based Key Derivation Function (RFC 5869).
 * Uses HMAC-SHA256 as the underlying PRF.
 */
#include "neverc/std/crypto/hkdf.h"
#include "neverc/std/crypto/hmac.h"
#include <string.h>

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

    uint8_t t[32];
    size_t t_len = 0;
    size_t off = 0;
    uint8_t counter = 1;

    while (off < okm_len) {
        uint8_t hmac_input[32 + 256 + 1];
        size_t input_len = 0;

        if (t_len > 0) {
            memcpy(hmac_input, t, t_len);
            input_len += t_len;
        }
        if (info != NULL && info_len > 0) {
            memcpy(hmac_input + input_len, info, info_len);
            input_len += info_len;
        }
        hmac_input[input_len++] = counter;

        neverc_hmac_sha256(prk, 32, hmac_input, input_len, t);
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

    uint8_t t[64];
    size_t t_len = 0;
    size_t off = 0;
    uint8_t counter = 1;

    while (off < okm_len) {
        uint8_t hmac_input[64 + 256 + 1];
        size_t input_len = 0;

        if (t_len > 0) {
            memcpy(hmac_input, t, t_len);
            input_len += t_len;
        }
        if (info != NULL && info_len > 0) {
            memcpy(hmac_input + input_len, info, info_len);
            input_len += info_len;
        }
        hmac_input[input_len++] = counter;

        neverc_hmac_sha512(prk, 64, hmac_input, input_len, t);
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
