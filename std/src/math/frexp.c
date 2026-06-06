#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_frexp(double f, int *exp) {
    if (f == 0.0) { *exp = 0; return f; }
    if (nc_isinf_any(f) || nc_isnan(f)) { *exp = 0; return f; }

    int norm_exp;
    f = nc_normalize(f, &norm_exp);

    uint64_t x = nc_f64_to_bits(f);
    *exp = (int)((x >> NC_EXP_SHIFT) & 0x7FF) - NC_EXP_BIAS + 1 + norm_exp;

    x &= ~NC_EXP_MASK;
    x |= (uint64_t)(NC_EXP_BIAS - 1) << NC_EXP_SHIFT;
    return nc_f64_from_bits(x);
}
