#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_acos(double x) {
    if (nc_isnan(x) || x < -1.0 || x > 1.0) return nc_nan();
    if (x == 1.0) return 0.0;
    return NEVERC_MATH_PI / 2.0 - neverc_math_asin(x);
}
