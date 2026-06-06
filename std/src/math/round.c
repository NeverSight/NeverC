#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_round(double x) {
    uint64_t bits = nc_f64_to_bits(x);
    unsigned e = (unsigned)(bits >> NC_EXP_SHIFT) & 0x7FF;

    if (e < NC_EXP_BIAS) {
        bits &= NC_SIGN_MASK;
        if (e == NC_EXP_BIAS - 1)
            bits |= NC_UV_ONE;
    } else if (e < NC_EXP_BIAS + NC_EXP_SHIFT) {
        uint64_t half = 1ULL << (NC_EXP_SHIFT - 1);
        uint64_t exp = e - NC_EXP_BIAS;
        bits += (uint64_t)half >> exp;
        bits &= ~(NC_FRAC_MASK >> exp);
    }
    return nc_f64_from_bits(bits);
}
