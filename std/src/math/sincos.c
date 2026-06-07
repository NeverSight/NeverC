#include "neverc/std/math.h"
#include "_math_internal.h"

void neverc_math_sincos(double x, double *sin_val, double *cos_val) {
    *sin_val = neverc_math_sin(x);
    *cos_val = neverc_math_cos(x);
}
