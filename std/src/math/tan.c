#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_tan(double x) {
    if (x == 0.0 || nc_isnan(x)) return x;
    if (nc_isinf_any(x)) return nc_nan();
    return neverc_math_sin(x) / neverc_math_cos(x);
}
