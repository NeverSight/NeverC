#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_atanh(double x) {
    if (nc_isnan(x) || x < -1.0 || x > 1.0) return nc_nan();
    if (x == 1.0) return nc_inf(1);
    if (x == -1.0) return nc_inf(-1);
    if (x == 0.0) return x;
    return 0.5 * neverc_math_log((1.0 + x) / (1.0 - x));
}
