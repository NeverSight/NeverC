#ifndef NEVERC_MATH_BIG_H
#define NEVERC_MATH_BIG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t *digits;
    size_t    len;
    size_t    cap;
    int       neg;
} neverc_bigint_t;

void neverc_bigint_init(neverc_bigint_t *z);
void neverc_bigint_free(neverc_bigint_t *z);

void neverc_bigint_set_int64(neverc_bigint_t *z, int64_t x);
void neverc_bigint_set_uint64(neverc_bigint_t *z, uint64_t x);
void neverc_bigint_set(neverc_bigint_t *z, const neverc_bigint_t *x);
int  neverc_bigint_set_string(neverc_bigint_t *z, const char *s, int base);

int64_t  neverc_bigint_int64(const neverc_bigint_t *x);
uint64_t neverc_bigint_uint64(const neverc_bigint_t *x);

int  neverc_bigint_sign(const neverc_bigint_t *x);
int  neverc_bigint_cmp(const neverc_bigint_t *x, const neverc_bigint_t *y);
int  neverc_bigint_is_zero(const neverc_bigint_t *x);
int  neverc_bigint_bit_len(const neverc_bigint_t *x);

void neverc_bigint_add(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y);
void neverc_bigint_sub(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y);
void neverc_bigint_mul(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y);
void neverc_bigint_div(neverc_bigint_t *q, neverc_bigint_t *r,
                       const neverc_bigint_t *x, const neverc_bigint_t *y);
void neverc_bigint_mod(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *m);
void neverc_bigint_neg(neverc_bigint_t *z, const neverc_bigint_t *x);
void neverc_bigint_abs(neverc_bigint_t *z, const neverc_bigint_t *x);

void neverc_bigint_lsh(neverc_bigint_t *z, const neverc_bigint_t *x, unsigned n);
void neverc_bigint_rsh(neverc_bigint_t *z, const neverc_bigint_t *x, unsigned n);
void neverc_bigint_and(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y);
void neverc_bigint_or(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y);
void neverc_bigint_xor(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y);
int  neverc_bigint_bit(const neverc_bigint_t *x, unsigned i);

void neverc_bigint_exp(neverc_bigint_t *z, const neverc_bigint_t *x,
                       const neverc_bigint_t *y, const neverc_bigint_t *m);
void neverc_bigint_gcd(neverc_bigint_t *z, const neverc_bigint_t *x,
                       const neverc_bigint_t *y);

int  neverc_bigint_string(const neverc_bigint_t *x, int base, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
