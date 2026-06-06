#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_cbrt(double x) {
    if (x == 0.0 || nc_isnan(x) || nc_isinf_any(x)) return x;

    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }

    /* Initial approximation via exp/log */
    double t = neverc_math_exp(neverc_math_log(x) / 3.0);

    /* Two Newton-Raphson refinements: t = t - (t^3 - x) / (3*t^2) */
    t = t - (t*t*t - x) / (3.0 * t * t);
    t = t - (t*t*t - x) / (3.0 * t * t);

    return sign ? -t : t;
}
