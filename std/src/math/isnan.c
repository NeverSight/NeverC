#include "neverc/std/math.h"
#include "_math_internal.h"

int neverc_math_isnan(double x) {
    uint64_t b = nc_f64_to_bits(x);
    return (b & NC_EXP_MASK) == NC_EXP_MASK && (b & NC_FRAC_MASK) != 0;
}
