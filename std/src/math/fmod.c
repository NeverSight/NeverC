#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_fmod(double x, double y) {
    if (y == 0.0 || nc_isinf_any(x) || nc_isnan(x) || nc_isnan(y))
        return nc_nan();

    y = nc_abs(y);

    int yfr_exp;
    double yfr = neverc_math_frexp(y, &yfr_exp);

    /* IEEE/Go: Mod(±0, y) keeps the sign of x. `x < 0` is false for -0. */
    int xneg = neverc_math_signbit(x);
    double r = nc_abs(x);
    while (r >= y) {
        int rfr_exp;
        double rfr = neverc_math_frexp(r, &rfr_exp);
        if (rfr < yfr)
            rfr_exp--;
        r = r - neverc_math_ldexp(y, rfr_exp - yfr_exp);
    }
    if (xneg)
        r = -r;
    return r;
}

double neverc_math_mod(double x, double y) {
    return neverc_math_fmod(x, y);
}
