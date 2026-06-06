#ifndef NEVERC_CHACHA20POLY1305_H
#define NEVERC_CHACHA20POLY1305_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_AEAD_KEY_SIZE   32
#define NEVERC_AEAD_NONCE_SIZE 12
#define NEVERC_AEAD_TAG_SIZE   16

/*
 * ChaCha20-Poly1305 AEAD (RFC 8439).
 *
 * Seal: encrypts plaintext and produces authentication tag.
 *   dst must have room for plaintext_len + 16 bytes.
 *   Returns total output length (plaintext_len + 16).
 *
 * Open: authenticates and decrypts ciphertext.
 *   ciphertext_len includes the 16-byte tag (minimum 16).
 *   dst receives the decrypted plaintext (ciphertext_len - 16 bytes).
 *   Returns plaintext length on success, -1 on authentication failure.
 */
size_t neverc_chacha20poly1305_seal(
    uint8_t *dst,
    const uint8_t key[32],
    const uint8_t nonce[12],
    const uint8_t *plaintext, size_t plaintext_len,
    const uint8_t *aad, size_t aad_len);

int neverc_chacha20poly1305_open(
    uint8_t *dst,
    const uint8_t key[32],
    const uint8_t nonce[12],
    const uint8_t *ciphertext, size_t ciphertext_len,
    const uint8_t *aad, size_t aad_len);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_CHACHA20POLY1305_H */
