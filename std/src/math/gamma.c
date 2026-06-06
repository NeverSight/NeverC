#include "neverc/math.h"
#include "_math_internal.h"

/*
 * Gamma function using Stirling's approximation + Lanczos coefficients.
 */

static const double lanczos_g = 7.0;
static const double lanczos_coeff[] = {
    0.99999999999980993,
    676.5203681218851,
    -1259.1392167224028,
    771.32342877765313,
    -176.61502916214059,
    12.507343278686905,
    -0.13857109526572012,
    9.9843695780195716e-6,
    1.5056327351493116e-7
};

double neverc_math_gamma(double x) {
    if (nc_isnan(x)) return x;
    if (nc_isinf_any(x)) {
        if (x > 0) return x;
        return nc_nan();
    }
    if (x == 0.0) return nc_copysign(nc_inf(1), x);

    /* Negative integers → NaN */
    if (x < 0.0) {
        double t = neverc_math_floor(-x);
        if (t == -x) return nc_nan();
    }

    /* Reflection formula for x < 0.5 */
    if (x < 0.5) {
        return NEVERC_MATH_PI /
               (neverc_math_sin(NEVERC_MATH_PI * x) * neverc_math_gamma(1.0 - x));
    }

    x -= 1.0;
    double a = lanczos_coeff[0];
    double t = x + lanczos_g + 0.5;
    for (int i = 1; i < 9; i++)
        a += lanczos_coeff[i] / (x + (double)i);

    return NEVERC_MATH_SQRT_PI * NEVERC_MATH_SQRT2 *
           neverc_math_pow(t, x + 0.5) * neverc_math_exp(-t) * a;
}
