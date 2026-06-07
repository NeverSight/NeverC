#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_max(double x, double y) {
    if (nc_isinf_any(x) && x > 0) return x;
    if (nc_isinf_any(y) && y > 0) return y;
    if (nc_isnan(x) || nc_isnan(y)) return nc_nan();
    if (x == 0 && y == 0) {
        if (neverc_math_signbit(x))
            return y;
        return x;
    }
    if (x > y) return x;
    return y;
}
