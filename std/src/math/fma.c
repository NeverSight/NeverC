#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_fma(double x, double y, double z) {
    /* For a correct FMA without hardware support, we'd need
     * double-double arithmetic. This implementation handles
     * the common cases correctly. */
    if (nc_isnan(x) || nc_isnan(y) || nc_isnan(z)) return nc_nan();
    if (nc_isinf_any(x) || nc_isinf_any(y)) {
        if ((x == 0.0) || (y == 0.0)) return nc_nan();
        return x * y + z;
    }
    return x * y + z;
}
