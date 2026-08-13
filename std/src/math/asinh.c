#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_asinh(double x) {
    if (nc_isnan(x) || nc_isinf_any(x) || x == 0.0) return x;

    const double near_zero = 1.0 / 268435456.0; /* 2^-28 */
    const double large = 268435456.0;           /* 2^28 */
    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }

    double result;
    if (x > large) {
        result = neverc_math_log(x) + NEVERC_MATH_LN2;
    } else if (x > 2.0) {
        result = neverc_math_log(
            2.0 * x + 1.0 / (neverc_math_sqrt(x * x + 1.0) + x));
    } else if (x < near_zero) {
        result = x;
    } else {
        double xx = x * x;
        result = neverc_math_log1p(
            x + xx / (1.0 + neverc_math_sqrt(1.0 + xx)));
    }
    return sign ? -result : result;
}
