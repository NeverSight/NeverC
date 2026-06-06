#include "neverc/math.h"
#include "_math_internal.h"

/*
 * expm1(x) = exp(x) - 1, ported from Go math.Expm1.
 * Originally from FreeBSD libm (s_expm1.c), Sun Microsystems.
 *
 * Accurate to < 1 ULP for all double inputs.
 * Uses argument reduction + rational polynomial, avoids catastrophic
 * cancellation that occurs with naive exp(x)-1 for small x.
 */
double neverc_math_expm1(double x) {
    const double Othreshold = 7.09782712893383973096e+02;
    const double Ln2X56     = 3.88162421113569373274e+01;
    const double Ln2HalfX3  = 1.03972077083991796413e+00;
    const double Ln2Half    = 3.46573590279972654709e-01;
    const double Ln2Hi      = 6.93147180369123816490e-01;
    const double Ln2Lo      = 1.90821492927058770002e-10;
    const double InvLn2     = 1.44269504088896338700e+00;
    const double Tiny       = 1.0 / (double)(1ULL << 54);
    const double Q1 = -3.33333333333331316428e-02;
    const double Q2 =  1.58730158725481460165e-03;
    const double Q3 = -7.93650757867487942473e-05;
    const double Q4 =  4.00821782732936239552e-06;
    const double Q5 = -2.01099218183624371326e-07;

    if (nc_isinf_any(x)) {
        if (x > 0) return x;
        return -1.0;
    }
    if (nc_isnan(x)) return x;

    double absx = x;
    int sign = 0;
    if (x < 0) { absx = -x; sign = 1; }

    if (absx >= Ln2X56) {
        if (sign) return -1.0;
        if (absx >= Othreshold) return nc_inf(1);
    }

    double c = 0.0;
    int k = 0;
    if (absx > Ln2Half) {
        double hi, lo;
        if (absx < Ln2HalfX3) {
            if (!sign) {
                hi = x - Ln2Hi; lo = Ln2Lo; k = 1;
            } else {
                hi = x + Ln2Hi; lo = -Ln2Lo; k = -1;
            }
        } else {
            if (!sign)
                k = (int)(InvLn2 * x + 0.5);
            else
                k = (int)(InvLn2 * x - 0.5);
            double t = (double)k;
            hi = x - t * Ln2Hi;
            lo = t * Ln2Lo;
        }
        x = hi - lo;
        c = (hi - x) - lo;
    } else if (absx < Tiny) {
        return x;
    }

    double hfx = 0.5 * x;
    double hxs = x * hfx;
    double r1 = 1.0 + hxs*(Q1 + hxs*(Q2 + hxs*(Q3 + hxs*(Q4 + hxs*Q5))));
    double t = 3.0 - r1 * hfx;
    double e = hxs * ((r1 - t) / (6.0 - x * t));

    if (k == 0) return x - (x * e - hxs);

    e = (x * (e - c) - c);
    e -= hxs;

    if (k == -1) return 0.5 * (x - e) - 0.5;
    if (k == 1) {
        if (x < -0.25)
            return -2.0 * (e - (x + 0.5));
        return 1.0 + 2.0 * (x - e);
    }
    if (k <= -2 || k > 56) {
        double y = 1.0 - (e - x);
        y = nc_f64_from_bits(nc_f64_to_bits(y) + ((uint64_t)k << 52));
        return y - 1.0;
    }
    if (k < 20) {
        t = nc_f64_from_bits(0x3FF0000000000000ULL - (0x20000000000000ULL >> (unsigned)k));
        double y = t - (e - x);
        y = nc_f64_from_bits(nc_f64_to_bits(y) + ((uint64_t)k << 52));
        return y;
    }
    t = nc_f64_from_bits((uint64_t)(0x3FF - k) << 52);
    double y = x - (e + t);
    y += 1.0;
    y = nc_f64_from_bits(nc_f64_to_bits(y) + ((uint64_t)k << 52));
    return y;
}
