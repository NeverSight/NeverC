#include "neverc/std/math.h"
#include "_math_internal.h"

int neverc_math_signbit(double x) {
    return (nc_f64_to_bits(x) & NC_SIGN_MASK) != 0;
}
