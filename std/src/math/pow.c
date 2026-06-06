#include "neverc/math.h"
#include "_math_internal.h"

/*
 * pow(x, y) — ported from Go math.Pow.
 * Originally from FreeBSD's /usr/src/lib/msun/src/e_pow.c,
 * updated by IEEE Std. 754-2008 "Section 9.2.1 Special values".
 *
 * Uses successive-squaring algorithm with frexp/ldexp for precision,
 * not the naive exp(y*log(x)) which loses significant digits.
 */

static int isOddInt(double x) {
    if (nc_abs(x) >= (double)(1ULL << 53))
        return 0;
    double ipart;
    double fpart = neverc_math_modf(x, &ipart);
    if (fpart != 0.0) return 0;
    int64_t ii = (int64_t)ipart;
    return (ii & 1) == 1;
}

double neverc_math_pow(double x, double y) {
    /* Pow(x, ±0) = 1 for any x */
    if (y == 0.0 || x == 1.0) return 1.0;
    /* Pow(x, 1) = x for any x */
    if (y == 1.0) return x;
    /* Pow(NaN, y) = NaN; Pow(x, NaN) = NaN */
    if (nc_isnan(x) || nc_isnan(y)) return nc_nan();

    /* Pow(±0, y) */
    if (x == 0.0) {
        if (y < 0.0) {
            if (neverc_math_signbit(x) && isOddInt(y))
                return nc_inf(-1);
            return nc_inf(1);
        }
        /* y > 0 */
        if (neverc_math_signbit(x) && isOddInt(y))
            return x; /* -0 */
        return 0.0;
    }

    /* Pow(x, ±Inf) */
    if (nc_isinf_any(y)) {
        if (x == -1.0) return 1.0;
        if ((nc_abs(x) < 1.0) == (y > 0.0))
            return 0.0;
        return nc_inf(1);
    }

    /* Pow(±Inf, y) */
    if (nc_isinf_any(x)) {
        if (x < 0.0) {
            /* Pow(-Inf, y) = Pow(-0, -y) */
            return neverc_math_pow(1.0 / x, -y);
        }
        if (y < 0.0) return 0.0;
        return nc_inf(1);
    }

    /* Pow(x, 0.5) = Sqrt(x) */
    if (y == 0.5) return neverc_math_sqrt(x);
    /* Pow(x, -0.5) = 1/Sqrt(x) */
    if (y == -0.5) return 1.0 / neverc_math_sqrt(x);

    double absy = nc_abs(y);
    double yi_d, yf;
    yf = neverc_math_modf(absy, &yi_d);

    if (yf != 0.0 && x < 0.0) return nc_nan();

    if (yi_d >= (double)(1ULL << 63)) {
        if (x == -1.0) return 1.0;
        if ((nc_abs(x) < 1.0) == (y > 0.0))
            return 0.0;
        return nc_inf(1);
    }

    /* ans = a1 * 2^ae */
    double a1 = 1.0;
    int ae = 0;

    /* ans *= x^yf */
    if (yf != 0.0) {
        if (yf > 0.5) {
            yf -= 1.0;
            yi_d += 1.0;
        }
        a1 = neverc_math_exp(yf * neverc_math_log(x));
    }

    /* ans *= x^yi by successive squaring */
    int xe;
    double x1 = neverc_math_frexp(x, &xe);
    int64_t i = (int64_t)yi_d;
    for (; i != 0; i >>= 1) {
        if (xe < -(1 << 12) || (1 << 12) < xe) {
            ae += xe;
            break;
        }
        if (i & 1) {
            a1 *= x1;
            ae += xe;
        }
        x1 *= x1;
        xe *= 2;
        if (x1 < 0.5) {
            x1 += x1;
            xe--;
        }
    }

    if (y < 0.0) {
        a1 = 1.0 / a1;
        ae = -ae;
    }
    return neverc_math_ldexp(a1, ae);
}
