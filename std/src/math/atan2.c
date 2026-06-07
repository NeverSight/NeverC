#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_atan2(double y, double x) {
    if (nc_isnan(y) || nc_isnan(x)) return nc_nan();

    if (y == 0.0) {
        if (x >= 0.0 && !neverc_math_signbit(x)) return nc_copysign(0.0, y);
        return nc_copysign(NEVERC_MATH_PI, y);
    }
    if (x == 0.0)
        return nc_copysign(NEVERC_MATH_PI / 2.0, y);

    if (nc_isinf_any(x)) {
        if (nc_f64_to_bits(x) == NC_UV_INF) {
            if (nc_isinf_any(y))
                return nc_copysign(NEVERC_MATH_PI / 4.0, y);
            return nc_copysign(0.0, y);
        }
        if (nc_isinf_any(y))
            return nc_copysign(3.0 * NEVERC_MATH_PI / 4.0, y);
        return nc_copysign(NEVERC_MATH_PI, y);
    }
    if (nc_isinf_any(y))
        return nc_copysign(NEVERC_MATH_PI / 2.0, y);

    double q = neverc_math_atan(y / x);
    if (x < 0.0) {
        if (q <= 0.0) return q + NEVERC_MATH_PI;
        return q - NEVERC_MATH_PI;
    }
    return q;
}
