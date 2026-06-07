#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * Gamma function — ported from Go math.Gamma.
 * Originally from Cephes Math Library (tgamma.c) by Stephen L. Moshier.
 * Uses rational polynomial in [2,3] + recurrence reduction + Stirling
 * for large arguments. Peak relative error: 9.4e-16 for |x| <= 33.
 */

static const double _gamP[] = {
    1.60119522476751861407e-04,
    1.19135147006586384913e-03,
    1.04213797561761569935e-02,
    4.76367800457137231464e-02,
    2.07448227648435975150e-01,
    4.94214826801497100753e-01,
    9.99999999999999996796e-01,
};

static const double _gamQ[] = {
    -2.31581873324120129819e-05,
    5.39605580493303397842e-04,
    -4.45641913851797240494e-03,
    1.18139785222060435552e-02,
    3.58236398605498653373e-02,
    -2.34591795718243348568e-01,
    7.14304917030273074085e-02,
    1.00000000000000000320e+00,
};

static const double _gamS[] = {
    7.87311395793093628397e-04,
    -2.29549961613378126380e-04,
    -2.68132617805781232825e-03,
    3.47222221605458667310e-03,
    8.33333333333482257126e-02,
};

static void stirling(double x, double *y1out, double *y2out) {
    const double SqrtTwoPi = 2.506628274631000502417;
    const double MaxStirling = 143.01608;

    if (x > 200.0) { *y1out = nc_inf(1); *y2out = 1.0; return; }

    double w = 1.0 / x;
    w = 1.0 + w * ((((_gamS[0]*w + _gamS[1])*w + _gamS[2])*w + _gamS[3])*w + _gamS[4]);

    double y1 = neverc_math_exp(x);
    double y2 = 1.0;
    if (x > MaxStirling) {
        double v = neverc_math_pow(x, 0.5*x - 0.25);
        y1 = v;
        y2 = v / neverc_math_exp(x);
    } else {
        y1 = neverc_math_pow(x, x - 0.5) / y1;
    }
    *y1out = y1;
    *y2out = SqrtTwoPi * w * y2;
}

double neverc_math_gamma(double x) {
    const double Euler = 0.57721566490153286060651209008240243104215933593992;

    if (nc_isnan(x)) return x;
    uint64_t bits = nc_f64_to_bits(x);
    if (bits == NC_UV_INF) return x;
    if (bits == NC_UV_NEGINF) return nc_nan();
    if (x == 0.0) return nc_copysign(nc_inf(1), x);

    /* negative integer check */
    if (x < 0.0) {
        double ipart;
        neverc_math_modf(x, &ipart);
        if (x == ipart) return nc_nan();
    }

    double q = nc_abs(x);
    double p = neverc_math_floor(q);

    if (q > 33.0) {
        if (x >= 0.0) {
            double y1, y2;
            stirling(x, &y1, &y2);
            return y1 * y2;
        }
        int signgam = 1;
        long long ip = (long long)p;
        if ((ip & 1) == 0) signgam = -1;

        double z = q - p;
        if (z > 0.5) { p = p + 1.0; z = q - p; }
        z = q * neverc_math_sin(NEVERC_MATH_PI * z);
        if (z == 0.0) return nc_inf(signgam);

        double sq1, sq2;
        stirling(q, &sq1, &sq2);
        double absz = nc_abs(z);
        double d = absz * sq1 * sq2;
        if (nc_isinf_any(d))
            z = NEVERC_MATH_PI / absz / sq1 / sq2;
        else
            z = NEVERC_MATH_PI / d;
        return (double)signgam * z;
    }

    double z = 1.0;
    while (x >= 3.0) { x -= 1.0; z *= x; }
    while (x < 0.0) {
        if (x > -1e-09) goto small;
        z /= x;
        x += 1.0;
    }
    while (x < 2.0) {
        if (x < 1e-09) goto small;
        z /= x;
        x += 1.0;
    }
    if (x == 2.0) return z;

    x -= 2.0;
    p = (((((x*_gamP[0]+_gamP[1])*x+_gamP[2])*x+_gamP[3])*x+_gamP[4])*x+_gamP[5])*x + _gamP[6];
    q = ((((((x*_gamQ[0]+_gamQ[1])*x+_gamQ[2])*x+_gamQ[3])*x+_gamQ[4])*x+_gamQ[5])*x+_gamQ[6])*x + _gamQ[7];
    return z * p / q;

small:
    if (x == 0.0) return nc_inf(1);
    return z / ((1.0 + Euler * x) * x);
}
