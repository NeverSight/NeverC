#include "neverc/math.h"
#include "_math_internal.h"
#include <limits.h>

int neverc_math_ilogb(double x) {
    if (x == 0.0) return INT_MIN;
    if (nc_isnan(x)) return INT_MAX;
    if (nc_isinf_any(x)) return INT_MAX;

    int norm_exp;
    x = nc_normalize(x, &norm_exp);
    return (int)((nc_f64_to_bits(x) >> NC_EXP_SHIFT) & 0x7FF) - NC_EXP_BIAS + norm_exp;
}
