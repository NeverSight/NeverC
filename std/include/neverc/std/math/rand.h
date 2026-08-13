#ifndef NEVERC_RAND_H
#define NEVERC_RAND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pseudo-random number generation (mirrors Go math/rand + rand/v2 packages).
 * Uses xoshiro256** internally — fast, high quality, not cryptographic.
 */

void     neverc_rand_seed(uint64_t seed);
uint32_t neverc_rand_uint32(void);
uint64_t neverc_rand_uint64(void);
int32_t  neverc_rand_int32(void);
int64_t  neverc_rand_int63(void);
int64_t  neverc_rand_intn(int64_t n);
int32_t  neverc_rand_int32n(int32_t n);
uint32_t neverc_rand_uint32n(uint32_t n);
uint64_t neverc_rand_uint64n(uint64_t n);
int64_t  neverc_rand_int63n(int64_t n);
double   neverc_rand_float64(void);
float    neverc_rand_float32(void);
double   neverc_rand_norm_float64(void);
double   neverc_rand_exp_float64(void);
/* Writes a deterministic little-endian byte stream. A NULL buffer is ignored. */
void     neverc_rand_read(void *buf, size_t len);
void     neverc_rand_perm(int n, int *out);
void     neverc_rand_shuffle(int n, void (*swap)(int i, int j));

/* Go rand/v2 compatible — platform-width int variants */
int      neverc_rand_int(void);
unsigned int neverc_rand_uint(void);
int      neverc_rand_intn_int(int n);
unsigned int neverc_rand_uintn(unsigned int n);

/*
 * Zipf distribution: generates values k in [0, imax] where
 * P(k) is proportional to (v + k)^(-s).
 * Requirements: s > 1, v >= 1.
 * Based on Hormann & Derflinger rejection-inversion method.
 */
typedef struct {
    double imax;
    double v;
    double q;
    double s_param;
    double oneminusQ;
    double oneminusQinv;
    double hxm;
    double hx0minusHxm;
} neverc_rand_zipf_t;

int      neverc_rand_zipf_init(neverc_rand_zipf_t *z, double s, double v, uint64_t imax);
uint64_t neverc_rand_zipf_uint64(neverc_rand_zipf_t *z);

#ifdef __cplusplus
}
#endif



/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/math.h>
#endif


#endif /* NEVERC_RAND_H */
