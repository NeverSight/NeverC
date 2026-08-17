#ifndef NEVERC_HMAC_H
#define NEVERC_HMAC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HMAC — Keyed-Hash Message Authentication Code (FIPS 198-1).
 * Uses our self-implemented SHA-256/SHA-1/MD5/SHA-512 hash functions.
 *
 * All functions write the MAC to `out`, which must have at least
 * the hash digest length bytes available:
 *   HMAC-SHA256: 32 bytes
 *   HMAC-SHA512: 64 bytes
 *   HMAC-SHA1:   20 bytes
 *   HMAC-MD5:    16 bytes
 * NULL key/data pointers are accepted only for zero-length spans. An invalid
 * non-empty span, or a key/data length that would overflow the hash length
 * field, clears `out`; a NULL output pointer is ignored. SHA-1 and MD5 are
 * provided for protocol compatibility, not new cryptographic designs.
 */

void neverc_hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32]);

void neverc_hmac_sha512(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[64]);

void neverc_hmac_sha1(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len,
                      uint8_t out[20]);

void neverc_hmac_md5(const uint8_t *key, size_t key_len,
                     const uint8_t *data, size_t data_len,
                     uint8_t out[16]);

int neverc_hmac_equal(const uint8_t *mac1, const uint8_t *mac2, size_t len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_HMAC_H */
