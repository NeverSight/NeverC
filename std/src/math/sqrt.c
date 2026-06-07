#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * Bit-by-bit square root, ported from Go math.Sqrt.
 * Originally from FreeBSD libm (e_sqrt.c), Sun Microsystems.
 */
double neverc_math_sqrt(double x) {
    if (x == 0.0 || nc_isnan(x))
        return x;
    if (nc_f64_to_bits(x) == NC_UV_INF)
        return x;
    if (x < 0.0)
        return nc_nan();

    uint64_t ix = nc_f64_to_bits(x);
    int exp = (int)((ix >> NC_EXP_SHIFT) & 0x7FF);
    if (exp == 0) {
        while ((ix & (1ULL << NC_EXP_SHIFT)) == 0) {
            ix <<= 1;
            exp--;
        }
        exp++;
    }
    exp -= NC_EXP_BIAS;
    ix &= ~NC_EXP_MASK;
    ix |= 1ULL << NC_EXP_SHIFT;
    if (exp & 1)
        ix <<= 1;
    exp >>= 1;

    ix <<= 1;
    uint64_t q = 0, s = 0;
    uint64_t r = 1ULL << (NC_EXP_SHIFT + 1);
    while (r != 0) {
        uint64_t t = s + r;
        if (t <= ix) {
            s = t + r;
            ix -= t;
            q += r;
        }
        ix <<= 1;
        r >>= 1;
    }
    if (ix != 0)
        q += q & 1;
    ix = (q >> 1) + ((uint64_t)(exp - 1 + NC_EXP_BIAS) << NC_EXP_SHIFT);
    return nc_f64_from_bits(ix);
}
