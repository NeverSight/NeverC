#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_asinh(double x) {
    if (nc_isnan(x) || nc_isinf_any(x) || x == 0.0) return x;
    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }
    double result = neverc_math_log(x + neverc_math_sqrt(x * x + 1.0));
    return sign ? -result : result;
}
