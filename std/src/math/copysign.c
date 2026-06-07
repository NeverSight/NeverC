#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_copysign(double f, double sign) {
    return nc_f64_from_bits((nc_f64_to_bits(f) & ~NC_SIGN_MASK) |
                            (nc_f64_to_bits(sign) & NC_SIGN_MASK));
}
