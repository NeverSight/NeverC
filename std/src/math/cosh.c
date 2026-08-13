#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * Hyperbolic cosine, ported from Go math.Cosh.
 */
double neverc_math_cosh(double x) {
    if (nc_isnan(x)) return x;
    x = nc_abs(x);
    if (nc_isinf_any(x)) return nc_inf(1);
    if (x > 21.0) {
        const double max_log = 7.09782712893383973096e+02;
        if (x > max_log) {
            double half_exp = neverc_math_exp(0.5 * x);
            return (0.5 * half_exp) * half_exp;
        }
        return neverc_math_exp(x) * 0.5;
    }
    double ex = neverc_math_exp(x);
    return (ex + 1.0 / ex) * 0.5;
}
