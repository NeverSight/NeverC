#include "neverc/std/math.h"
#include "_math_internal.h"

/* Go math.Pow10: 10**i for i < 32. */
static const double pow10tab[] = {
    1e00, 1e01, 1e02, 1e03, 1e04, 1e05, 1e06, 1e07, 1e08, 1e09,
    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
    1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29,
    1e30, 1e31,
};

/* 10**(i*32) at index i. */
static const double pow10postab32[] = {
    1e00, 1e32, 1e64, 1e96, 1e128, 1e160, 1e192, 1e224, 1e256, 1e288,
};

/* 10**(-i*32) at index i. */
static const double pow10negtab32[] = {
    1e-00, 1e-32, 1e-64, 1e-96, 1e-128, 1e-160, 1e-192, 1e-224, 1e-256,
    1e-288, 1e-320,
};

double neverc_math_pow10(int n) {
    /* Table strides cover n in [-323, 308]. Outside that: +Inf or +0.
     * Do not fall back to Pow(10, n) — successive squaring is several ULPs
     * off (e.g. 1e100, 1e308). Do not negate n when n == INT_MIN. */
    if (n >= 0 && n <= 308) {
        unsigned un = (unsigned)n;
        return pow10postab32[un / 32] * pow10tab[un % 32];
    }
    if (n >= -323 && n < 0) {
        unsigned un = (unsigned)(-n);
        return pow10negtab32[un / 32] / pow10tab[un % 32];
    }
    if (n > 0)
        return nc_inf(1);
    return 0.0;
}
