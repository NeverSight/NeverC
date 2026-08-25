#ifndef NEVERC_GCM_H
#define NEVERC_GCM_H

#include <stdint.h>
#include <stddef.h>
#include "neverc/std/crypto/aes.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AES-GCM authenticated encryption — NIST SP 800-38D.
 * Supports AES-128/192/256-GCM with 12-byte nonces and 16-byte tags.
 * Nonces must be unique per key; reuse of (key, nonce) discloses plaintext
 * XOR and forges tags. Open compares the tag in constant time.
 */

typedef struct {
    neverc_aes_ctx_t aes;
    uint8_t h[16]; /* hash subkey H = AES_K(0^128) */
    /* Retained for v3389 ABI/source compatibility. The constant-time GHASH
     * implementation does not index these legacy tables; init clears them. */
    uint64_t htab[16][2];
    uint64_t rem4[16];
} neverc_gcm_ctx;

int neverc_gcm_init(neverc_gcm_ctx *ctx, const uint8_t *key, int key_len);

/*
 * Seal: encrypt plaintext and produce ciphertext + 16-byte authentication tag.
 * NULL plaintext/AAD pointers are accepted only for zero-length spans.
 * Plaintext is limited to 2^36-32 bytes by SP 800-38D.
 * Requires a context from a successful neverc_gcm_init.
 * AAD is hashed before ciphertext is written, so AAD may overlap the output.
 * dest-after-src plaintext/ciphertext overlap is slid before CTR XOR.
 * Returns 0 on success, -1 on error.
 */
int neverc_gcm_seal(const neverc_gcm_ctx *ctx,
                    const uint8_t nonce[12],
                    const uint8_t *plaintext, size_t pt_len,
                    const uint8_t *aad, size_t aad_len,
                    uint8_t *ciphertext,
                    uint8_t tag[16]);

/*
 * Open: decrypt ciphertext and verify authentication tag.
 * NULL ciphertext/AAD pointers are accepted only for zero-length spans.
 * Requires a context from a successful neverc_gcm_init.
 * Returns 0 on success (tag valid), -1 on authentication failure.
 */
int neverc_gcm_open(const neverc_gcm_ctx *ctx,
                    const uint8_t nonce[12],
                    const uint8_t *ciphertext, size_t ct_len,
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t tag[16],
                    uint8_t *plaintext);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_GCM_H */
