#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_acosh(double x) {
    if (nc_isnan(x) || x < 1.0) return nc_nan();
    if (nc_f64_to_bits(x) == NC_UV_INF) return x;
    if (x == 1.0) return 0.0;
    return neverc_math_log(x + neverc_math_sqrt(x * x - 1.0));
}
