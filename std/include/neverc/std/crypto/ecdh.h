#ifndef NEVERC_CRYPTO_ECDH_H
#define NEVERC_CRYPTO_ECDH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_ECDH_P256_PRIVKEY_SIZE  32
#define NEVERC_ECDH_P256_PUBKEY_SIZE   65
#define NEVERC_ECDH_P256_SHARED_SIZE   32

#define NEVERC_ECDH_P384_PRIVKEY_SIZE  48
#define NEVERC_ECDH_P384_PUBKEY_SIZE   97
#define NEVERC_ECDH_P384_SHARED_SIZE   48

#define NEVERC_ECDH_X25519_PRIVKEY_SIZE 32
#define NEVERC_ECDH_X25519_PUBKEY_SIZE  32
#define NEVERC_ECDH_X25519_SHARED_SIZE  32

typedef enum {
    NEVERC_ECDH_CURVE_P256,
    NEVERC_ECDH_CURVE_P384,
    NEVERC_ECDH_CURVE_X25519,
} neverc_ecdh_curve_t;

typedef struct {
    neverc_ecdh_curve_t curve;
    unsigned char       private_key[48];
    unsigned char       public_key[97];
    int                 privkey_len;
    int                 pubkey_len;
} neverc_ecdh_key_t;

/* Generate a new ECDH key pair. Returns 0 on success, -1 on error.
 * The output is cleared if the curve is invalid or entropy generation fails. */
int neverc_ecdh_generate_key(neverc_ecdh_curve_t curve, neverc_ecdh_key_t *key);

/* Import a private key from raw bytes. Returns 0 on success, -1 on error. */
int neverc_ecdh_new_private_key(neverc_ecdh_curve_t curve,
                                const unsigned char *privkey, size_t len,
                                neverc_ecdh_key_t *key);

/* Import a public key from raw bytes (uncompressed for NIST).
 * Returns 0 on success, -1 on error. */
int neverc_ecdh_new_public_key(neverc_ecdh_curve_t curve,
                               const unsigned char *pubkey, size_t len,
                               neverc_ecdh_key_t *key);

/* Perform ECDH: compute shared secret from local private + remote public.
 * out must have room for the curve's shared secret size.
 * Returns the number of bytes written, or -1 on error. */
int neverc_ecdh_compute(const neverc_ecdh_key_t *local_private,
                        const unsigned char *remote_pubkey, size_t remote_len,
                        unsigned char *out, size_t out_cap);

/* Get the public key bytes. Returns length written, or -1. */
int neverc_ecdh_public_key_bytes(const neverc_ecdh_key_t *key,
                                 unsigned char *out, size_t out_cap);

/* Get the private key bytes. Returns length written, or -1. */
int neverc_ecdh_private_key_bytes(const neverc_ecdh_key_t *key,
                                  unsigned char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif


#endif
