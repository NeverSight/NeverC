#include "neverc/math.h"

void neverc_math_sincos(double x, double *sin_val, double *cos_val) {
#if defined(__GLIBC__) || defined(__APPLE__)
    __sincos(x, sin_val, cos_val);
#else
    *sin_val = sin(x);
    *cos_val = cos(x);
#endif
}
