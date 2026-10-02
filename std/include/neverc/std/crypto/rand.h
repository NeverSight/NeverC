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

/* Fills buf with OS entropy. On failure the buffer is wiped so a
 * partial fill or leftover secret cannot be used as random. */
int neverc_crypto_rand_read(uint8_t *buf, size_t len);
/* Uniform in [0, max). max==0 or entropy failure wipes *out and
 * returns -1. */
int neverc_crypto_rand_int(uint64_t *out, uint64_t max);
/* Generates a random prime of exactly bits bits, for any bits >= 2 as in
 * Go, and writes it to out as (bits + 7) / 8 bytes, least-significant byte
 * first.  As in Go, the top two bits are set, so the product of two such
 * primes has exactly 2*bits bits.  Up to 64 bits primality is decided
 * exactly; wider candidates must pass the checks of Go's ProbablyPrime(20):
 * a Baillie-PSW test plus 20 Miller-Rabin rounds with random bases.
 * Returns 0 on success.  Returns -1 without touching out if out is NULL or
 * bits < 2; returns -1 and wipes the (bits + 7) / 8 output bytes if entropy
 * or memory fails, or if no prime turns up (only a defective entropy source
 * makes that happen). */
int neverc_crypto_rand_prime(uint8_t *out, size_t bits);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif


#endif /* NEVERC_CRYPTO_RAND_H */
