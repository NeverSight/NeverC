#ifndef NEVERC_CRYPTO_HPKE_H
#define NEVERC_CRYPTO_HPKE_H

/*
 * Hybrid Public Key Encryption (HPKE) — RFC 9180.
 *
 * Supported cipher suites:
 *   KEM:  DHKEM(X25519, HKDF-SHA256) [0x0020]
 *         DHKEM(P-256,  HKDF-SHA256) [0x0010]
 *   KDF:  HKDF-SHA256 [0x0001]
 *         HKDF-SHA512 [0x0003]
 *   AEAD: AES-128-GCM [0x0001]
 *         AES-256-GCM [0x0002]
 *         ChaCha20Poly1305 [0x0003]
 *         Export-only [0xFFFF]
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_HPKE_KEM_X25519_SHA256  0x0020
#define NEVERC_HPKE_KEM_P256_SHA256    0x0010

#define NEVERC_HPKE_KDF_SHA256         0x0001
#define NEVERC_HPKE_KDF_SHA512         0x0003

#define NEVERC_HPKE_AEAD_AES128GCM         0x0001
#define NEVERC_HPKE_AEAD_AES256GCM         0x0002
#define NEVERC_HPKE_AEAD_CHACHA20POLY1305  0x0003
#define NEVERC_HPKE_AEAD_EXPORT_ONLY       0xFFFF

#define NEVERC_HPKE_MAX_ENC_SIZE  65
#define NEVERC_HPKE_MAX_KEY_SIZE  32
#define NEVERC_HPKE_MAX_NONCE_SIZE 12

typedef struct {
    uint16_t kem_id;
    uint16_t kdf_id;
    uint16_t aead_id;

    uint8_t key[32];
    int     key_len;
    uint8_t base_nonce[12];
    int     nonce_len;
    uint8_t exp_secret[64];
    int     exp_len;
    uint64_t seq_num;
} neverc_hpke_ctx_t;

typedef struct {
    neverc_hpke_ctx_t ctx;
} neverc_hpke_sender_t;

typedef struct {
    neverc_hpke_ctx_t ctx;
} neverc_hpke_recipient_t;

/*
 * Create a sending HPKE context. Performs KEM encapsulation.
 * enc:     output encapsulated key (caller must provide buffer of at least
 *          NEVERC_HPKE_MAX_ENC_SIZE bytes)
 * enc_len: output length of enc
 * Returns 0 on success, -1 on error.
 */
int neverc_hpke_sender_new(neverc_hpke_sender_t *s,
                           uint8_t *enc, size_t *enc_len,
                           uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                           const uint8_t *pubkey, size_t pubkey_len,
                           const uint8_t *info, size_t info_len);

/*
 * Create a receiving HPKE context. Performs KEM decapsulation.
 * Returns 0 on success, -1 on error.
 */
int neverc_hpke_recipient_new(neverc_hpke_recipient_t *r,
                              const uint8_t *enc, size_t enc_len,
                              uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                              const uint8_t *privkey, size_t privkey_len,
                              const uint8_t *info, size_t info_len);

/* Encrypt with sender context. ciphertext must hold pt_len + 16 bytes.
 * Returns total ciphertext length on success, -1 on error. */
int neverc_hpke_sender_seal(neverc_hpke_sender_t *s,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *plaintext, size_t pt_len,
                            uint8_t *ciphertext);

/* Decrypt with recipient context. plaintext must hold ct_len - 16 bytes.
 * Returns plaintext length on success, -1 on error. */
int neverc_hpke_recipient_open(neverc_hpke_recipient_t *r,
                               const uint8_t *aad, size_t aad_len,
                               const uint8_t *ciphertext, size_t ct_len,
                               uint8_t *plaintext);

/* Export a secret from sender/recipient context.
 * Returns 0 on success, -1 on error. */
int neverc_hpke_sender_export(const neverc_hpke_sender_t *s,
                              const uint8_t *exporter_ctx, size_t ctx_len,
                              uint8_t *out, size_t out_len);

int neverc_hpke_recipient_export(const neverc_hpke_recipient_t *r,
                                 const uint8_t *exporter_ctx, size_t ctx_len,
                                 uint8_t *out, size_t out_len);

/*
 * One-shot Seal/Open (convenience wrappers).
 * output must hold enc_size + pt_len + 16 bytes for seal,
 * or ct_len - enc_size - 16 bytes for open.
 */
int neverc_hpke_seal(uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                     const uint8_t *pubkey, size_t pubkey_len,
                     const uint8_t *info, size_t info_len,
                     const uint8_t *plaintext, size_t pt_len,
                     uint8_t *output, size_t *output_len);

int neverc_hpke_open(uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                     const uint8_t *privkey, size_t privkey_len,
                     const uint8_t *info, size_t info_len,
                     const uint8_t *ciphertext, size_t ct_len,
                     uint8_t *plaintext, size_t *pt_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_CRYPTO_HPKE_H */
