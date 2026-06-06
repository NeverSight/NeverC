#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (y == 1.0) return x;
    if (nc_isnan(x) || nc_isnan(y)) return nc_nan();
    if (x == 1.0) return 1.0;

    if (nc_isinf_any(y)) {
        double ax = nc_abs(x);
        if (ax == 1.0) return 1.0;
        if (y > 0) return (ax > 1.0) ? nc_inf(1) : 0.0;
        return (ax > 1.0) ? 0.0 : nc_inf(1);
    }

    if (nc_isinf_any(x)) {
        if (x > 0) return (y > 0) ? nc_inf(1) : 0.0;
        int yi = (int)y;
        if ((double)yi == y && (yi & 1))
            return (y > 0) ? nc_inf(-1) : -0.0;
        return (y > 0) ? nc_inf(1) : 0.0;
    }

    if (x == 0.0) {
        if (y > 0) return 0.0;
        return nc_inf(1);
    }

    if (x < 0.0) {
        int yi = (int)y;
        if ((double)yi != y) return nc_nan();
        double result = neverc_math_exp(y * neverc_math_log(-x));
        if (yi & 1) return -result;
        return result;
    }

    return neverc_math_exp(y * neverc_math_log(x));
}
