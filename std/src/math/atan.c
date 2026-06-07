#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * Arctangent, ported from Go math.Atan (Cephes Math Library).
 */

static const double
    aP0 = -8.750608600031904122785e-01,
    aP1 = -1.615753718733365076637e+01,
    aP2 = -7.500855792314704667340e+01,
    aP3 = -1.228866684490136173410e+02,
    aP4 = -6.485021904942025371773e+01,
    aQ0 =  2.485846490142306297962e+01,
    aQ1 =  1.650270098316988542046e+02,
    aQ2 =  4.328810604912902668951e+02,
    aQ3 =  4.853903996359136964868e+02,
    aQ4 =  1.945506571482613964425e+02;

static double xatan(double x) {
    double z = x * x;
    z = z * ((((aP0*z+aP1)*z+aP2)*z+aP3)*z + aP4) /
            (((((z+aQ0)*z+aQ1)*z+aQ2)*z+aQ3)*z + aQ4);
    return x * z + x;
}

static double satan(double x) {
    const double Morebits = 6.123233995736765886130e-17;
    const double Tan3pio8 = 2.41421356237309504880;

    if (x <= 0.66) return xatan(x);
    if (x > Tan3pio8)
        return NEVERC_MATH_PI / 2.0 - xatan(1.0 / x) + Morebits;
    return NEVERC_MATH_PI / 4.0 + xatan((x - 1.0) / (x + 1.0)) + 0.5 * Morebits;
}

double neverc_math_atan(double x) {
    if (x == 0.0) return x;
    if (nc_isinf_any(x))
        return nc_copysign(NEVERC_MATH_PI / 2.0, x);
    if (nc_isnan(x)) return x;
    if (x > 0.0) return satan(x);
    return -satan(-x);
}
