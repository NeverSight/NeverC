#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_modf(double x, double *iptr) {
    double dummy;
    if (!iptr) iptr = &dummy;
    if (nc_isnan(x)) {
        *iptr = nc_nan();
        return nc_nan();
    }

    /* Go math.Modf: Modf(±Inf) = ±Inf, NaN (Inf-Inf after Trunc). */
    double t = neverc_math_trunc(x);
    *iptr = t;
    return nc_copysign(x - t, x);
}
