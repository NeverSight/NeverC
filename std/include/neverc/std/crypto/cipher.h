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
 * Invalid keys, spans, or a request that would consume/wrap the terminal
 * 32-bit counter value leave IV and output unchanged. Because IV is the full
 * caller-visible state, counter 0xffffffff is retained as the terminal IV and
 * is not consumed; this avoids reserving a legitimate IV as a hidden sentinel.
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

/*
 * PKCS#7 pad/unpad for a 16-byte block cipher (RFC 5652).
 * Unpad is fail-closed: every invalid encoding returns -1, does not write
 * dst, and reports unpadded_len=0. The last 16 bytes are scanned in a
 * fixed number of steps so n=0, n=17, and mismatched pad bytes are not
 * distinguishable by error code or early exit.
 */
int neverc_cipher_pkcs7_pad(uint8_t *dst, size_t dst_cap,
                            const uint8_t *src, size_t src_len,
                            size_t *padded_len);
int neverc_cipher_pkcs7_unpad(uint8_t *dst, size_t dst_cap,
                              const uint8_t *src, size_t src_len,
                              size_t *unpadded_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_CIPHER_H */
