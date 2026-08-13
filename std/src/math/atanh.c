#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_atanh(double x) {
    if (nc_isnan(x) || x < -1.0 || x > 1.0) return nc_nan();
    if (x == 1.0) return nc_inf(1);
    if (x == -1.0) return nc_inf(-1);

    const double near_zero = 1.0 / 268435456.0; /* 2^-28 */
    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }

    double result;
    if (x < near_zero) {
        result = x;
    } else if (x < 0.5) {
        double twice = x + x;
        result = 0.5 * neverc_math_log1p(
            twice + twice * x / (1.0 - x));
    } else {
        result = 0.5 * neverc_math_log1p((x + x) / (1.0 - x));
    }
    return sign ? -result : result;
}
