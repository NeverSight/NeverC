#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_asin(double x) {
    if (nc_isnan(x) || x < -1.0 || x > 1.0) return nc_nan();
    if (x == 0.0) return x;
    if (x == 1.0) return NEVERC_MATH_PI / 2.0;
    if (x == -1.0) return -NEVERC_MATH_PI / 2.0;
    return neverc_math_atan2(x, neverc_math_sqrt(1.0 - x * x));
}
