#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_cosh(double x) {
    if (nc_isnan(x)) return x;
    x = nc_abs(x);
    if (nc_isinf_any(x)) return nc_inf(1);
    double ex = neverc_math_exp(x);
    return (ex + 1.0 / ex) * 0.5;
}
