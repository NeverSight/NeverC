#ifndef NEVERC_CRYPTO_MLKEM_H
#define NEVERC_CRYPTO_MLKEM_H

/*
 * ML-KEM (Module-Lattice-based Key Encapsulation Mechanism) — NIST FIPS 203.
 *
 * Supports ML-KEM-768 (recommended) and ML-KEM-1024 parameter sets.
 * Post-quantum key encapsulation: generates a 32-byte shared secret.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_MLKEM_SHARED_KEY_SIZE    32
#define NEVERC_MLKEM_SEED_SIZE          64

#define NEVERC_MLKEM768_EK_SIZE        1184
#define NEVERC_MLKEM768_CT_SIZE        1088
#define NEVERC_MLKEM768_DK_SIZE          64

#define NEVERC_MLKEM1024_EK_SIZE       1568
#define NEVERC_MLKEM1024_CT_SIZE       1568
#define NEVERC_MLKEM1024_DK_SIZE         64

/* ── ML-KEM-768 ──────────────────────────────────────────── */

typedef struct {
    uint8_t seed[NEVERC_MLKEM_SEED_SIZE];
    uint8_t ek[NEVERC_MLKEM768_EK_SIZE];
} neverc_mlkem768_dk_t;

typedef struct {
    uint8_t ek[NEVERC_MLKEM768_EK_SIZE];
} neverc_mlkem768_ek_t;

/*
 * Generate a new ML-KEM-768 key pair.
 * Returns 0 on success or -1 if an argument or the entropy source is invalid.
 * On entropy failure, dk is securely cleared.
 */
int neverc_mlkem768_generate_key(neverc_mlkem768_dk_t *dk);

/* Create decapsulation key from 64-byte seed (d || z). Returns 0 on success. */
int neverc_mlkem768_new_dk(neverc_mlkem768_dk_t *dk, const uint8_t seed[64]);

/* Get the encapsulation (public) key from a decapsulation key. */
void neverc_mlkem768_dk_encapsulation_key(const neverc_mlkem768_dk_t *dk,
                                          neverc_mlkem768_ek_t *ek);

/* Parse an encapsulation key from encoded bytes. Returns 0 on success. */
int neverc_mlkem768_new_ek(neverc_mlkem768_ek_t *ek,
                           const uint8_t *encoded, size_t len);

/*
 * Encapsulate: produce shared key + ciphertext.
 * Returns 0 on success or -1 for invalid input or entropy failure. Output
 * buffers are securely cleared on failure.
 */
int neverc_mlkem768_encapsulate(const neverc_mlkem768_ek_t *ek,
                                uint8_t shared_key[32],
                                uint8_t ciphertext[NEVERC_MLKEM768_CT_SIZE]);

/* Decapsulate: recover shared key from ciphertext. Returns 0 on success. */
int neverc_mlkem768_decapsulate(const neverc_mlkem768_dk_t *dk,
                                const uint8_t ciphertext[NEVERC_MLKEM768_CT_SIZE],
                                uint8_t shared_key[32]);

/* Get seed bytes from decapsulation key. */
void neverc_mlkem768_dk_bytes(const neverc_mlkem768_dk_t *dk,
                              uint8_t seed[64]);

/* Get encapsulation key bytes. */
void neverc_mlkem768_ek_bytes(const neverc_mlkem768_ek_t *ek,
                              uint8_t *out, size_t *out_len);

/* ── ML-KEM-1024 ─────────────────────────────────────────── */

typedef struct {
    uint8_t seed[NEVERC_MLKEM_SEED_SIZE];
    uint8_t ek[NEVERC_MLKEM1024_EK_SIZE];
} neverc_mlkem1024_dk_t;

typedef struct {
    uint8_t ek[NEVERC_MLKEM1024_EK_SIZE];
} neverc_mlkem1024_ek_t;

/*
 * Generate a new ML-KEM-1024 key pair.
 * Returns 0 on success or -1 if an argument or the entropy source is invalid.
 * On entropy failure, dk is securely cleared.
 */
int neverc_mlkem1024_generate_key(neverc_mlkem1024_dk_t *dk);
int neverc_mlkem1024_new_dk(neverc_mlkem1024_dk_t *dk, const uint8_t seed[64]);
void neverc_mlkem1024_dk_encapsulation_key(const neverc_mlkem1024_dk_t *dk,
                                           neverc_mlkem1024_ek_t *ek);
int neverc_mlkem1024_new_ek(neverc_mlkem1024_ek_t *ek,
                            const uint8_t *encoded, size_t len);
/* Outputs are securely cleared if validation or entropy acquisition fails. */
int neverc_mlkem1024_encapsulate(const neverc_mlkem1024_ek_t *ek,
                                 uint8_t shared_key[32],
                                 uint8_t ciphertext[NEVERC_MLKEM1024_CT_SIZE]);
int neverc_mlkem1024_decapsulate(const neverc_mlkem1024_dk_t *dk,
                                 const uint8_t ciphertext[NEVERC_MLKEM1024_CT_SIZE],
                                 uint8_t shared_key[32]);
void neverc_mlkem1024_dk_bytes(const neverc_mlkem1024_dk_t *dk, uint8_t seed[64]);
void neverc_mlkem1024_ek_bytes(const neverc_mlkem1024_ek_t *ek,
                               uint8_t *out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_CRYPTO_MLKEM_H */
