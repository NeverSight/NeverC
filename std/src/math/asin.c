#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_asin(double x) {
    if (nc_isnan(x) || x < -1.0 || x > 1.0) return nc_nan();
    if (x == 0.0) return x;
    if (x == 1.0) return NEVERC_MATH_PI / 2.0;
    if (x == -1.0) return -NEVERC_MATH_PI / 2.0;

    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }

    /* Go math.Asin: above 0.7 take the complement of the small angle
     * atan(sqrt(1-x*x)/x), so Acos = Pi/2 - Asin cancels back to that angle
     * exactly instead of magnifying the rounding of a value near Pi/2. */
    double t = neverc_math_sqrt(1.0 - x * x);
    double result;
    if (x > 0.7)
        result = NEVERC_MATH_PI / 2.0 - neverc_math_atan(t / x);
    else
        result = neverc_math_atan(x / t);
    return sign ? -result : result;
}
