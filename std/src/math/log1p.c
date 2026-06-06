#include "neverc/math.h"
#include "_math_internal.h"

/*
 * log(1+x) with improved precision for small x.
 * Ported from Go math.Log1p, originally from FreeBSD libm.
 */
double neverc_math_log1p(double x) {
    if (x < -1.0 || nc_isnan(x)) return nc_nan();
    if (x == -1.0) return nc_inf(-1);
    if (nc_isinf_any(x)) return x;
    if (nc_abs(x) < 1e-15) return x;
    return neverc_math_log(1.0 + x);
}
