#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_sinh(double x) {
    if (nc_isnan(x) || nc_isinf_any(x) || x == 0.0) return x;
    double ax = nc_abs(x);
    if (ax < 1.0) {
        double ex = neverc_math_expm1(ax);
        return nc_copysign(ex + ex * ex / (2.0 * (1.0 + ex)), x);
    }
    double ex = neverc_math_exp(ax);
    return nc_copysign((ex - 1.0 / ex) * 0.5, x);
}
