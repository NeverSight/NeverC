#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_floor(double x) {
    uint64_t bits = nc_f64_to_bits(x);
    uint64_t magnitude = bits & ~NC_SIGN_MASK;
    if (magnitude >= NC_UV_INF) {
        /* Arithmetic quiets a signaling NaN and raises invalid, while keeping
         * the sign/payload. Infinity needs no floating operation. */
        if (magnitude > NC_UV_INF)
            return x + x;
        return x;
    }
    int exponent = (int)(magnitude >> NC_EXP_SHIFT) - NC_EXP_BIAS;
    if (exponent >= 52 || magnitude == 0)
        return x;
    if (exponent < 0)
        return (bits & NC_SIGN_MASK) ? -1.0 : 0.0;

    /* Round the magnitude using integer bits. Floating comparisons and the
     * old modf/subtraction path exposed denormal, inexact and overflow flags
     * for inputs whose floor is exact, and depended on the rounding mode. */
    uint64_t fractional = NC_FRAC_MASK >> exponent;
    if ((bits & fractional) == 0)
        return x;
    if (bits & NC_SIGN_MASK)
        bits += fractional;
    return nc_f64_from_bits(bits & ~fractional);
}
