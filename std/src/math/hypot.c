#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_hypot(double p, double q) {
    if (nc_isinf_any(p) || nc_isinf_any(q)) return nc_inf(1);
    if (nc_isnan(p) || nc_isnan(q)) return nc_nan();

    p = nc_abs(p);
    q = nc_abs(q);
    if (p < q) { double t = p; p = q; q = t; }
    if (p == 0.0) return 0.0;
    double r = q / p;
    return p * neverc_math_sqrt(1.0 + r * r);
}
