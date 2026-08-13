#ifndef NEVERC_CRYPTO_RAND_H
#define NEVERC_CRYPTO_RAND_H

/*
 * NeverC crypto/rand — cryptographic random number generator
 * (mirrors Go crypto/rand package).
 *
 * Uses OS entropy source (getentropy/getrandom).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int neverc_crypto_rand_read(uint8_t *buf, size_t len);
int neverc_crypto_rand_int(uint64_t *out, uint64_t max);
/* Generates an exact-width prime encoded least-significant byte first. */
int neverc_crypto_rand_prime(uint8_t *out, size_t bits);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif


#endif /* NEVERC_CRYPTO_RAND_H */
