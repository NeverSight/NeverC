#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_modf(double x, double *iptr) {
    if (nc_isnan(x)) {
        *iptr = nc_nan();
        return nc_nan();
    }
    if (nc_isinf_any(x)) {
        *iptr = x;
        return nc_copysign(0.0, x);
    }

    double t = neverc_math_trunc(x);
    *iptr = t;
    double frac = x - t;
    return nc_copysign(frac, x);
}
