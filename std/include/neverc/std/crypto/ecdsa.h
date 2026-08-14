#ifndef NEVERC_CRYPTO_ECDSA_H
#define NEVERC_CRYPTO_ECDSA_H

#include "neverc/std/crypto/elliptic.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const neverc_elliptic_curve_t *curve;
    neverc_elliptic_point_t pub;
} neverc_ecdsa_public_key_t;

typedef struct {
    neverc_ecdsa_public_key_t pub;
    neverc_bigint_t d;
} neverc_ecdsa_private_key_t;

typedef struct {
    neverc_bigint_t r;
    neverc_bigint_t s;
} neverc_ecdsa_signature_t;

void neverc_ecdsa_public_key_init(neverc_ecdsa_public_key_t *k);
void neverc_ecdsa_public_key_free(neverc_ecdsa_public_key_t *k);
void neverc_ecdsa_private_key_init(neverc_ecdsa_private_key_t *k);
void neverc_ecdsa_private_key_free(neverc_ecdsa_private_key_t *k);
void neverc_ecdsa_signature_init(neverc_ecdsa_signature_t *sig);
void neverc_ecdsa_signature_free(neverc_ecdsa_signature_t *sig);

int  neverc_ecdsa_generate_key(neverc_ecdsa_private_key_t *key,
                                const neverc_elliptic_curve_t *curve);

/* sig is set to zero on every failure, including invalid keys. */
int  neverc_ecdsa_sign(const neverc_ecdsa_private_key_t *key,
                        const unsigned char *hash, size_t hash_len,
                        neverc_ecdsa_signature_t *sig);

int  neverc_ecdsa_verify(const neverc_ecdsa_public_key_t *key,
                          const unsigned char *hash, size_t hash_len,
                          const neverc_ecdsa_signature_t *sig);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif


#endif
