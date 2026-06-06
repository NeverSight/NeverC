#include "neverc/math.h"
#include "_math_internal.h"

int neverc_math_ilogb(double x) {
    if (x == 0.0) return NEVERC_MATH_MIN_INT32;
    if (nc_isnan(x)) return NEVERC_MATH_MAX_INT32;
    if (nc_isinf_any(x)) return NEVERC_MATH_MAX_INT32;

    int norm_exp;
    x = nc_normalize(x, &norm_exp);
    return (int)((nc_f64_to_bits(x) >> NC_EXP_SHIFT) & 0x7FF) - NC_EXP_BIAS + norm_exp;
}
