#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_acosh(double x) {
    if (nc_isnan(x) || x < 1.0) return nc_nan();
    if (x == 1.0) return 0.0;

    const double large = 268435456.0; /* 2^28 */
    if (x >= large)
        return neverc_math_log(x) + NEVERC_MATH_LN2;
    if (x > 2.0)
        return neverc_math_log(
            2.0 * x - 1.0 / (x + neverc_math_sqrt(x * x - 1.0)));

    double t = x - 1.0;
    return neverc_math_log1p(t + neverc_math_sqrt(2.0 * t + t * t));
}
