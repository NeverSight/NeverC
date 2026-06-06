#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_logb(double x) {
    if (x == 0.0) return nc_inf(-1);
    if (nc_isinf_any(x)) return nc_inf(1);
    if (nc_isnan(x)) return x;
    return (double)neverc_math_ilogb(x);
}
