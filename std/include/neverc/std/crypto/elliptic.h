#ifndef NEVERC_CRYPTO_ELLIPTIC_H
#define NEVERC_CRYPTO_ELLIPTIC_H

#include "neverc/std/math/big.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    neverc_bigint_t p;
    neverc_bigint_t n;
    neverc_bigint_t b;
    neverc_bigint_t gx;
    neverc_bigint_t gy;
    int             bit_size;
    const char     *name;
} neverc_elliptic_curve_t;

typedef struct {
    neverc_bigint_t x;
    neverc_bigint_t y;
} neverc_elliptic_point_t;

const neverc_elliptic_curve_t *neverc_elliptic_p256(void);
const neverc_elliptic_curve_t *neverc_elliptic_p384(void);

void neverc_elliptic_point_init(neverc_elliptic_point_t *pt);
void neverc_elliptic_point_free(neverc_elliptic_point_t *pt);

int  neverc_elliptic_is_on_curve(const neverc_elliptic_curve_t *curve,
                                  const neverc_elliptic_point_t *pt);

void neverc_elliptic_add(const neverc_elliptic_curve_t *curve,
                          neverc_elliptic_point_t *r,
                          const neverc_elliptic_point_t *p1,
                          const neverc_elliptic_point_t *p2);

void neverc_elliptic_double(const neverc_elliptic_curve_t *curve,
                             neverc_elliptic_point_t *r,
                             const neverc_elliptic_point_t *p);

void neverc_elliptic_scalar_mult(const neverc_elliptic_curve_t *curve,
                                  neverc_elliptic_point_t *r,
                                  const neverc_elliptic_point_t *p,
                                  const neverc_bigint_t *k);

void neverc_elliptic_scalar_base_mult(const neverc_elliptic_curve_t *curve,
                                       neverc_elliptic_point_t *r,
                                       const neverc_bigint_t *k);

int  neverc_elliptic_marshal(const neverc_elliptic_curve_t *curve,
                              const neverc_elliptic_point_t *pt,
                              unsigned char *out, size_t out_cap, size_t *out_len);

int  neverc_elliptic_unmarshal(const neverc_elliptic_curve_t *curve,
                                neverc_elliptic_point_t *pt,
                                const unsigned char *data, size_t data_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif


#endif
