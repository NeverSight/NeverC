#ifndef NEVERC_CRYPTO_MLDSA_H
#define NEVERC_CRYPTO_MLDSA_H

/*
 * ML-DSA — Module-Lattice-based Digital Signature Algorithm (NIST FIPS 204).
 *
 * Implements the ML-DSA-44 parameter set with 128-bit security. Size
 * constants for ML-DSA-65 and ML-DSA-87 are reserved for future APIs.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_MLDSA_SEED_SIZE          32

#define NEVERC_MLDSA44_PK_SIZE        1312
#define NEVERC_MLDSA44_SIG_SIZE       2420

#define NEVERC_MLDSA65_PK_SIZE        1952
#define NEVERC_MLDSA65_SIG_SIZE       3309

#define NEVERC_MLDSA87_PK_SIZE        2592
#define NEVERC_MLDSA87_SIG_SIZE       4627

/* ── ML-DSA-44 ───────────────────────────────────────────── */

typedef struct {
    uint8_t seed[32];
    uint8_t pk[NEVERC_MLDSA44_PK_SIZE];
} neverc_mldsa44_sk_t;

typedef struct {
    uint8_t pk[NEVERC_MLDSA44_PK_SIZE];
} neverc_mldsa44_pk_t;

/*
 * Generate a new ML-DSA-44 key pair.
 * Returns 0 on success or -1 if an argument or the entropy source is invalid.
 * On entropy failure, sk is securely cleared.
 */
int neverc_mldsa44_generate_key(neverc_mldsa44_sk_t *sk);
int neverc_mldsa44_new_sk(neverc_mldsa44_sk_t *sk, const uint8_t seed[32]);

void neverc_mldsa44_sk_public_key(const neverc_mldsa44_sk_t *sk,
                                   neverc_mldsa44_pk_t *pk);

int neverc_mldsa44_new_pk(neverc_mldsa44_pk_t *pk,
                           const uint8_t *encoded, size_t len);

int neverc_mldsa44_sign(const neverc_mldsa44_sk_t *sk,
                         const uint8_t *message, size_t msg_len,
                         uint8_t sig[NEVERC_MLDSA44_SIG_SIZE]);

int neverc_mldsa44_verify(const neverc_mldsa44_pk_t *pk,
                           const uint8_t *message, size_t msg_len,
                           const uint8_t sig[NEVERC_MLDSA44_SIG_SIZE]);

void neverc_mldsa44_sk_bytes(const neverc_mldsa44_sk_t *sk, uint8_t seed[32]);
void neverc_mldsa44_pk_bytes(const neverc_mldsa44_pk_t *pk,
                              uint8_t *out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_CRYPTO_MLDSA_H */
