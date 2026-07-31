#ifndef NEVERC_CRYPTO_RSA_H
#define NEVERC_CRYPTO_RSA_H

#include "neverc/std/math/big.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    neverc_bigint_t n;
    neverc_bigint_t e;
} neverc_rsa_public_key_t;

typedef struct {
    neverc_rsa_public_key_t pub;
    neverc_bigint_t d;
    neverc_bigint_t p;
    neverc_bigint_t q;
    neverc_bigint_t dp;
    neverc_bigint_t dq;
    neverc_bigint_t qinv;
} neverc_rsa_private_key_t;

void neverc_rsa_public_key_init(neverc_rsa_public_key_t *k);
void neverc_rsa_public_key_free(neverc_rsa_public_key_t *k);
void neverc_rsa_private_key_init(neverc_rsa_private_key_t *k);
void neverc_rsa_private_key_free(neverc_rsa_private_key_t *k);

int  neverc_rsa_generate_key(neverc_rsa_private_key_t *key, int bits);

int  neverc_rsa_encrypt_pkcs1v15(const neverc_rsa_public_key_t *pub,
                                  const unsigned char *msg, size_t msg_len,
                                  unsigned char *out, size_t out_cap, size_t *out_len);

int  neverc_rsa_decrypt_pkcs1v15(const neverc_rsa_private_key_t *priv,
                                  const unsigned char *ct, size_t ct_len,
                                  unsigned char *out, size_t out_cap, size_t *out_len);

int  neverc_rsa_sign_pkcs1v15_sha256(const neverc_rsa_private_key_t *priv,
                                      const unsigned char *hash, size_t hash_len,
                                      unsigned char *sig, size_t sig_cap, size_t *sig_len);

int  neverc_rsa_verify_pkcs1v15_sha256(const neverc_rsa_public_key_t *pub,
                                        const unsigned char *hash, size_t hash_len,
                                        const unsigned char *sig, size_t sig_len);

/* Verify RSASSA-PSS with SHA-256, MGF1-SHA256, and a 32-byte salt.
 * This is the profile required by TLS 1.3 rsa_pss_*_sha256 schemes. */
int  neverc_rsa_verify_pss_sha256(const neverc_rsa_public_key_t *pub,
                                   const unsigned char *hash, size_t hash_len,
                                   const unsigned char *sig, size_t sig_len);

int  neverc_rsa_key_size(const neverc_rsa_public_key_t *pub);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif


#endif
