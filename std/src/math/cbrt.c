#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * Cube root — ported from Go math.Cbrt.
 * Originally from Sun Microsystems fdlibm (s_cbrt.c).
 *
 * Uses bit manipulation for rough 5-bit estimate, one specialized
 * Halley-like iteration to 23 bits, then one Newton step to 53 bits.
 * No dependency on exp/log.
 */
double neverc_math_cbrt(double x) {
    const uint64_t B1 = 715094163;
    const uint64_t B2 = 696219795;
    const double C  =  5.42857142857142815906e-01;
    const double D  = -7.05306122448979611050e-01;
    const double E  =  1.41428571428571436819e+00;
    const double F  =  1.60714285714285720630e+00;
    const double G  =  3.57142857142857150787e-01;
    const double SmallestNormal = 2.22507385850720138309e-308;

    if (x == 0.0 || nc_isnan(x) || nc_isinf_any(x)) return x;

    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }

    double t = nc_f64_from_bits(nc_f64_to_bits(x) / 3 + (B1 << 32));
    if (x < SmallestNormal) {
        t = (double)(1ULL << 54);
        t *= x;
        t = nc_f64_from_bits(nc_f64_to_bits(t) / 3 + (B2 << 32));
    }

    double r = t * t / x;
    double s = C + r * t;
    t *= G + F / (s + E + D / s);

    t = nc_f64_from_bits((nc_f64_to_bits(t) & (0xFFFFFFFFC0000000ULL)) + (1ULL << 30));

    s = t * t;
    r = x / s;
    double w = t + t;
    r = (r - t) / (w + r);
    t = t + t * r;

    return sign ? -t : t;
}
