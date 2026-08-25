#ifndef NEVERC_DES_H
#define NEVERC_DES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DES and Triple DES (3DES/TDEA) block ciphers.
 * FIPS 46-3, ported from Go crypto/des.
 *
 * WARNING: DES is cryptographically broken. TDEA/Triple DES was withdrawn by
 * NIST and is provided only for decrypting or verifying legacy data. Do not use
 * DES, two-key TDEA, or three-key TDEA to protect new data; use AES instead.
 */

#define NEVERC_DES_BLOCK_SIZE 8

typedef struct {
    uint64_t subkeys[16];
} neverc_des_cipher_t;

typedef struct {
    neverc_des_cipher_t c1, c2, c3;
} neverc_3des_cipher_t;

/* NULL cipher/key returns -1 and wipes the cipher so a previous key cannot
 * keep encrypting. Encrypt/decrypt are no-ops until a successful init.
 * The all-zero DES key is valid and must still encrypt after init. */
int  neverc_des_init(neverc_des_cipher_t *c, const uint8_t key[8]);
void neverc_des_encrypt_block(const neverc_des_cipher_t *c,
                              uint8_t dst[8], const uint8_t src[8]);
void neverc_des_decrypt_block(const neverc_des_cipher_t *c,
                              uint8_t dst[8], const uint8_t src[8]);

/* 1 if key is one of the 16 FIPS 46-3 weak/semi-weak keys (parity bits
 * ignored), 0 if not, -1 if key is NULL. Init still accepts weak keys. */
int  neverc_des_is_weak_key(const uint8_t key[8]);

int  neverc_3des_init(neverc_3des_cipher_t *c, const uint8_t key[24]);
/* Structural weak-key test only; returning 0 does not make TDEA suitable for
 * new encryption. Returns 1 if any 8-byte component is weak/semi-weak, or if
 * K1 equals K2 or K2 equals K3 (parity ignored). K1==K3 is not a structural
 * weak-key match, but two-key TDEA is still legacy-only. */
int  neverc_3des_is_weak_key(const uint8_t key[24]);
void neverc_3des_encrypt_block(const neverc_3des_cipher_t *c,
                               uint8_t dst[8], const uint8_t src[8]);
void neverc_3des_decrypt_block(const neverc_3des_cipher_t *c,
                               uint8_t dst[8], const uint8_t src[8]);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_DES_H */
