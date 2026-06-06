#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_lgamma(double x) {
    if (nc_isnan(x)) return x;
    if (nc_isinf_any(x)) return nc_inf(1);
    if (x <= 0.0 && neverc_math_floor(x) == x) return nc_inf(1);
    return neverc_math_log(nc_abs(neverc_math_gamma(x)));
}
