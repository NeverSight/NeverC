#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_log2(double x) {
    int exp;
    double frac = neverc_math_frexp(x, &exp);
    if (x == 0.0) return nc_inf(-1);
    if (x < 0.0 || nc_isnan(x)) return nc_nan();
    if (nc_f64_to_bits(x) == NC_UV_INF) return x;
    return neverc_math_log(frac) * (1.0 / NEVERC_MATH_LN2) + (double)exp;
}
