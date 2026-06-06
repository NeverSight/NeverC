#include "neverc/math.h"
#include "_math_internal.h"

/*
 * exp(x) — ported from Go math.Exp, originally from FreeBSD libm (e_exp.c).
 */

static const double
    P1 =  1.66666666666666657415e-01,
    P2 = -2.77777777770155933842e-03,
    P3 =  6.61375632143793436117e-05,
    P4 = -1.65339022054652515390e-06,
    P5 =  4.13813679705723846039e-08;

static double expmulti(double hi, double lo, int k) {
    double r = hi - lo;
    double t = r * r;
    double c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
    return neverc_math_ldexp(y, k);
}

double neverc_math_exp(double x) {
    const double Ln2Hi    = 6.93147180369123816490e-01;
    const double Ln2Lo    = 1.90821492927058770002e-10;
    const double Log2e    = 1.44269504088896338700e+00;
    const double Overflow = 7.09782712893383973096e+02;
    const double Underflow = -7.45133219101941108420e+02;
    const double NearZero = 1.0 / (1 << 28);

    if (nc_isnan(x)) return x;
    if (x > Overflow) return nc_inf(1);
    if (x < Underflow) return 0.0;
    if (-NearZero < x && x < NearZero) return 1.0 + x;

    int k;
    if (x < 0)
        k = (int)(Log2e * x - 0.5);
    else
        k = (int)(Log2e * x + 0.5);

    double hi = x - (double)k * Ln2Hi;
    double lo = (double)k * Ln2Lo;
    return expmulti(hi, lo, k);
}
