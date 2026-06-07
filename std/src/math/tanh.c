#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_tanh(double x) {
    if (nc_isnan(x)) return x;
    if (nc_isinf_any(x)) return nc_copysign(1.0, x);
    if (x == 0.0) return x;
    double ax = nc_abs(x);
    if (ax < 1e-7) return x;
    if (ax >= 20.0) return nc_copysign(1.0, x);
    double e2x = neverc_math_exp(2.0 * ax);
    double result = (e2x - 1.0) / (e2x + 1.0);
    return nc_copysign(result, x);
}
