#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_expm1(double x) {
    if (nc_isnan(x)) return x;
    if (nc_isinf_any(x)) {
        if (x > 0) return x;
        return -1.0;
    }
    if (nc_abs(x) < 1e-15) return x;
    return neverc_math_exp(x) - 1.0;
}
