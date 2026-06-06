#ifndef NEVERC_RAND_H
#define NEVERC_RAND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pseudo-random number generation (mirrors Go math/rand package).
 * Uses xoshiro256** internally — fast, high quality, not cryptographic.
 */

void     neverc_rand_seed(uint64_t seed);
uint32_t neverc_rand_uint32(void);
uint64_t neverc_rand_uint64(void);
int64_t  neverc_rand_int63(void);
int64_t  neverc_rand_intn(int64_t n);
double   neverc_rand_float64(void);
float    neverc_rand_float32(void);
void     neverc_rand_shuffle(int n, void (*swap)(int i, int j));

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_RAND_H */
