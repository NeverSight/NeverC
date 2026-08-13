#ifndef NEVERC_CRYPTO_ED25519_H
#define NEVERC_CRYPTO_ED25519_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_ED25519_PUBLIC_KEY_SIZE  32
#define NEVERC_ED25519_PRIVATE_KEY_SIZE 64
#define NEVERC_ED25519_SIGNATURE_SIZE   64
#define NEVERC_ED25519_SEED_SIZE        32

/*
 * Generate a key pair. Returns -1 for invalid arguments or entropy failure;
 * both output buffers are securely cleared when generation fails.
 */
int  neverc_ed25519_generate_key(unsigned char pub[32], unsigned char priv[64]);
int  neverc_ed25519_new_key_from_seed(const unsigned char seed[32],
                                       unsigned char pub[32],
                                       unsigned char priv[64]);
int  neverc_ed25519_sign(const unsigned char priv[64],
                          const unsigned char *msg, size_t msg_len,
                          unsigned char sig[64]);
int  neverc_ed25519_verify(const unsigned char pub[32],
                            const unsigned char *msg, size_t msg_len,
                            const unsigned char sig[64]);

void neverc_ed25519_seed(const unsigned char priv[64], unsigned char seed[32]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif


#endif
