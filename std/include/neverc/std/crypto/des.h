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
 * WARNING: DES is cryptographically broken. Use AES instead.
 * Triple DES provides adequate security but is slow.
 */

#define NEVERC_DES_BLOCK_SIZE 8

typedef struct {
    uint64_t subkeys[16];
} neverc_des_cipher_t;

typedef struct {
    neverc_des_cipher_t c1, c2, c3;
} neverc_3des_cipher_t;

int  neverc_des_init(neverc_des_cipher_t *c, const uint8_t key[8]);
void neverc_des_encrypt_block(const neverc_des_cipher_t *c,
                              uint8_t dst[8], const uint8_t src[8]);
void neverc_des_decrypt_block(const neverc_des_cipher_t *c,
                              uint8_t dst[8], const uint8_t src[8]);

int  neverc_3des_init(neverc_3des_cipher_t *c, const uint8_t key[24]);
void neverc_3des_encrypt_block(const neverc_3des_cipher_t *c,
                               uint8_t dst[8], const uint8_t src[8]);
void neverc_3des_decrypt_block(const neverc_3des_cipher_t *c,
                               uint8_t dst[8], const uint8_t src[8]);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/crypto.h>
#endif

#endif /* NEVERC_DES_H */
