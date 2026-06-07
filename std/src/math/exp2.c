#include "neverc/std/math.h"
#include "_math_internal.h"

static const double
    ep1 =  1.66666666666666657415e-01,
    ep2 = -2.77777777770155933842e-03,
    ep3 =  6.61375632143793436117e-05,
    ep4 = -1.65339022054652515390e-06,
    ep5 =  4.13813679705723846039e-08;

static double expmulti2(double hi, double lo, int k) {
    double r = hi - lo;
    double t = r * r;
    double c = r - t * (ep1 + t * (ep2 + t * (ep3 + t * (ep4 + t * ep5))));
    double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
    return neverc_math_ldexp(y, k);
}

double neverc_math_exp2(double x) {
    const double Ln2Hi = 6.93147180369123816490e-01;
    const double Ln2Lo = 1.90821492927058770002e-10;
    const double Overflow = 1.0239999999999999e+03;
    const double Underflow = -1.0740e+03;

    if (nc_isnan(x)) return x;
    if (x > Overflow) return nc_inf(1);
    if (x < Underflow) return 0.0;

    int k;
    if (x > 0) k = (int)(x + 0.5);
    else if (x < 0) k = (int)(x - 0.5);
    else k = 0;

    double t = x - (double)k;
    double hi = t * Ln2Hi;
    double lo = -t * Ln2Lo;
    return expmulti2(hi, lo, k);
}
