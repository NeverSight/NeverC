#include "neverc/math.h"
#include "_math_internal.h"

/*
 * RoundToEven — nearest integer, ties to even.
 * Ported from Go math.RoundToEven (pure bit manipulation).
 */
double neverc_math_roundtoeven(double x) {
    uint64_t bits = nc_f64_to_bits(x);
    unsigned e = (unsigned)((bits >> NC_EXP_SHIFT) & 0x7FF);

    if (e >= NC_EXP_BIAS + NC_EXP_SHIFT) {
        return x;
    }
    if (e >= NC_EXP_BIAS) {
        const uint64_t halfMinusULP = (1ULL << (NC_EXP_SHIFT - 1)) - 1;
        unsigned e2 = e - NC_EXP_BIAS;
        bits += (halfMinusULP + ((bits >> (NC_EXP_SHIFT - e2)) & 1)) >> e2;
        bits &= ~(NC_FRAC_MASK >> e2);
    } else if (e == NC_EXP_BIAS - 1 && (bits & NC_FRAC_MASK) != 0) {
        bits = (bits & NC_SIGN_MASK) | NC_UV_ONE;
    } else {
        bits &= NC_SIGN_MASK;
    }
    return nc_f64_from_bits(bits);
}
