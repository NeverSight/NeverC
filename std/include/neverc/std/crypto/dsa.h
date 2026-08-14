#ifndef NEVERC_CRYPTO_DSA_H
#define NEVERC_CRYPTO_DSA_H

#include "neverc/std/math/big.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEVERC_DSA_L1024_N160,
    NEVERC_DSA_L2048_N224,
    NEVERC_DSA_L2048_N256,
    NEVERC_DSA_L3072_N256
} neverc_dsa_param_size_t;

typedef struct {
    neverc_bigint_t p;
    neverc_bigint_t q;
    neverc_bigint_t g;
    neverc_bigint_t y;
} neverc_dsa_public_key_t;

typedef struct {
    neverc_dsa_public_key_t pub;
    neverc_bigint_t x;
} neverc_dsa_private_key_t;

typedef struct {
    neverc_bigint_t r;
    neverc_bigint_t s;
} neverc_dsa_signature_t;

void neverc_dsa_public_key_init(neverc_dsa_public_key_t *k);
void neverc_dsa_public_key_free(neverc_dsa_public_key_t *k);
void neverc_dsa_private_key_init(neverc_dsa_private_key_t *k);
void neverc_dsa_private_key_free(neverc_dsa_private_key_t *k);
void neverc_dsa_signature_init(neverc_dsa_signature_t *sig);
void neverc_dsa_signature_free(neverc_dsa_signature_t *sig);

/*
 * Sign a non-empty message digest. Returns -1 for invalid inputs, allocation
 * failure, or entropy-source failure; sig is set to zero on failure.
 */
int  neverc_dsa_sign(const neverc_dsa_private_key_t *key,
                     const unsigned char *hash, size_t hash_len,
                     neverc_dsa_signature_t *sig);

/* Rejects r,s outside (0,q) and degenerate keys (g or y <= 1 or >= p). */
int  neverc_dsa_verify(const neverc_dsa_public_key_t *key,
                        const unsigned char *hash, size_t hash_len,
                        const neverc_dsa_signature_t *sig);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif


#endif
