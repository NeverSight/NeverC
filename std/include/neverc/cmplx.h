#ifndef NEVERC_CMPLX_H
#define NEVERC_CMPLX_H

#include "neverc/math.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double re;
    double im;
} neverc_cmplx_t;

static inline neverc_cmplx_t neverc_cmplx(double re, double im) {
    neverc_cmplx_t z = { re, im };
    return z;
}

/* Basic operations */
double          neverc_cmplx_abs(neverc_cmplx_t z);
double          neverc_cmplx_phase(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_conj(neverc_cmplx_t z);
void            neverc_cmplx_polar(neverc_cmplx_t z, double *r, double *theta);
neverc_cmplx_t  neverc_cmplx_rect(double r, double theta);

/* Exponential & logarithmic */
neverc_cmplx_t  neverc_cmplx_exp(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_log(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_log10(neverc_cmplx_t z);

/* Power & root */
neverc_cmplx_t  neverc_cmplx_sqrt(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_pow(neverc_cmplx_t x, neverc_cmplx_t y);

/* Trigonometric */
neverc_cmplx_t  neverc_cmplx_sin(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_cos(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_tan(neverc_cmplx_t z);

/* Hyperbolic */
neverc_cmplx_t  neverc_cmplx_sinh(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_cosh(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_tanh(neverc_cmplx_t z);

/* Inverse trigonometric */
neverc_cmplx_t  neverc_cmplx_asin(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_acos(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_atan(neverc_cmplx_t z);

/* Special values */
int             neverc_cmplx_isnan(neverc_cmplx_t z);
int             neverc_cmplx_isinf(neverc_cmplx_t z);
neverc_cmplx_t  neverc_cmplx_nan_val(void);
neverc_cmplx_t  neverc_cmplx_inf_val(void);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_CMPLX_H */
