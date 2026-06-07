#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_ldexp(double frac, int exp) {
    if (frac == 0.0) return frac;
    if (nc_isinf_any(frac) || nc_isnan(frac)) return frac;

    int norm_exp;
    frac = nc_normalize(frac, &norm_exp);
    exp += norm_exp;

    uint64_t x = nc_f64_to_bits(frac);
    exp += (int)((x >> NC_EXP_SHIFT) & 0x7FF) - NC_EXP_BIAS;

    if (exp < -1075)
        return nc_copysign(0.0, frac);
    if (exp > 1023)
        return frac < 0.0 ? nc_inf(-1) : nc_inf(1);

    double m = 1.0;
    if (exp < -1022) {
        exp += 53;
        m = 1.0 / (double)(1ULL << 53);
    }
    x &= ~NC_EXP_MASK;
    x |= (uint64_t)(exp + NC_EXP_BIAS) << NC_EXP_SHIFT;
    return m * nc_f64_from_bits(x);
}
