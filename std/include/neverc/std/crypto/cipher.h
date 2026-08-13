#ifndef NEVERC_CIPHER_H
#define NEVERC_CIPHER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AES-CBC: Cipher Block Chaining mode.
 * plaintext/ciphertext length must be a multiple of 16.
 * IV is 16 bytes; modified in-place (caller must copy if needed for reuse).
 *
 * Returns 0 on success, -1 for an invalid key, span, or length.
 */
int neverc_cipher_cbc_encrypt(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len);

int neverc_cipher_cbc_decrypt(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len);

/*
 * AES-CTR: Counter mode.
 * Encrypts/decrypts arbitrary-length data (CTR is its own inverse).
 * IV is 16 bytes (nonce || counter); modified in-place.
 * Invalid keys or spans leave IV and output unchanged.
 */
void neverc_cipher_ctr(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len);

/* Checked CTR entry point. Returns 0 on success and -1 for invalid input. */
int neverc_cipher_ctr_checked(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_CIPHER_H */
