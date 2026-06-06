#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_trunc(double x) {
    if (nc_abs(x) < 1.0)
        return nc_copysign(0.0, x);

    uint64_t b = nc_f64_to_bits(x);
    int e = (int)((b >> NC_EXP_SHIFT) & 0x7FF) - NC_EXP_BIAS;

    if (e < 52)
        b &= ~(NC_FRAC_MASK >> e);
    return nc_f64_from_bits(b);
}
