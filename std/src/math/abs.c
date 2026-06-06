#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_abs(double x) {
    return nc_f64_from_bits(nc_f64_to_bits(x) & ~NC_SIGN_MASK);
}
