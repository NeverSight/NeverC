#ifndef NEVERC_AES_H
#define NEVERC_AES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_AES_BLOCK_SIZE 16
#define NEVERC_AES_MAX_ROUNDS 14
#define NEVERC_AES_MAX_RK    60

typedef struct {
    uint32_t enc_key[NEVERC_AES_MAX_RK];
    uint32_t dec_key[NEVERC_AES_MAX_RK];
    int      rounds;
} neverc_aes_ctx_t;

int  neverc_aes_init(neverc_aes_ctx_t *ctx, const uint8_t *key, int key_len);
void neverc_aes_encrypt_block(const neverc_aes_ctx_t *ctx, uint8_t dst[16], const uint8_t src[16]);
void neverc_aes_decrypt_block(const neverc_aes_ctx_t *ctx, uint8_t dst[16], const uint8_t src[16]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/crypto.h>
#endif

#endif /* NEVERC_AES_H */
