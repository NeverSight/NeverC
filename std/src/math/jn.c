#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * Bessel function of the first and second kinds of order n.
 * Ported from Go math.Jn/Yn, originally from FreeBSD libm (e_jn.c).
 *
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 */

#define INV_SQRT_PI 0.56418958354775628694807945156077258584405062932899885684408

double neverc_math_jn(int n, double x) {
    const double TwoM29 = 1.0 / (1 << 29);
    const double Two302 = 8.148143905337944345073782753637512644205e+90;

    if (nc_isnan(x)) return x;

    if (n < 0) {
        /* -INT_MIN overflows; |n| is even and so large Jn underflows to 0. */
        if (n == NEVERC_MATH_MIN_INT)
            return 0.0;
        n = -n;
        x = -x;
    }
    if (n == 0) return neverc_math_j0(x);
    /* After reflecting negative n (Jn(-n,x)=(-1)^n Jn(n,-x)), odd order is
     * odd in x: Jn(±0)=±0 and Jn(±Inf)=±0. Even order stays +0. */
    if (nc_isinf_any(x) || x == 0.0)
        return (n & 1) ? nc_copysign(0.0, x) : 0.0;
    if (n == 1) return neverc_math_j1(x);

    int sign = 0;
    if (x < 0) {
        x = -x;
        if (n & 1) sign = 1;
    }

    double b;
    if ((double)n <= x) {
        if (x >= Two302) {
            double s = neverc_math_sin(x);
            double c = neverc_math_cos(x);
            double temp;
            switch (n & 3) {
            case 0: temp =  c + s; break;
            case 1: temp = -c + s; break;
            case 2: temp = -c - s; break;
            case 3: temp =  c - s; break;
            default: temp = 0; break;
            }
            b = INV_SQRT_PI * temp / neverc_math_sqrt(x);
        } else {
            b = neverc_math_j1(x);
            double a = neverc_math_j0(x);
            for (int i = 1; i < n; i++) {
                double tmp = b;
                b = b * ((2.0 * (double)i) / x) - a;
                a = tmp;
            }
        }
    } else {
        if (x < TwoM29) {
            if (n > 33) {
                b = 0;
            } else {
                double temp = x * 0.5;
                b = temp;
                double a = 1.0;
                for (int i = 2; i <= n; i++) {
                    a *= (double)i;
                    b *= temp;
                }
                b /= a;
            }
        } else {
            /* 2n in int overflows for n > INT_MAX/2; keep 2n in double/int64.
             * n that cannot be doubled in int64 is past exact double anyway. */
            if ((int64_t)n > NEVERC_MATH_MAX_INT64 / 4)
                return sign ? -0.0 : 0.0;
            double w = (2.0 * (double)n) / x;
            double h = 2.0 / x;
            double q0 = w;
            double z = w + h;
            double q1 = w * z - 1.0;
            int k = 1;
            while (q1 < 1e9) {
                k++;
                z += h;
                double tmp = q1;
                q1 = z * q1 - q0;
                q0 = tmp;
            }
            int64_t m = (int64_t)n * 2;
            double t = 0.0;
            for (int64_t i = ((int64_t)n + k) * 2; i >= m; i -= 2)
                t = 1.0 / ((double)i / x - t);

            double a = t;
            b = 1.0;

            double tmp = (double)n;
            double v = 2.0 / x;
            tmp = tmp * neverc_math_log(nc_abs(v * tmp));
            if (tmp < 7.09782712893383973096e+02) {
                for (int i = n - 1; i > 0; i--) {
                    double di = 2.0 * (double)i;
                    double tmpa = b;
                    b = b * di / x - a;
                    a = tmpa;
                }
            } else {
                for (int i = n - 1; i > 0; i--) {
                    double di = 2.0 * (double)i;
                    double tmpa = b;
                    b = b * di / x - a;
                    a = tmpa;
                    if (b > 1e100) {
                        a /= b;
                        t /= b;
                        b = 1.0;
                    }
                }
            }
            b = t * neverc_math_j0(x) / b;
        }
    }
    return sign ? -b : b;
}

double neverc_math_yn(int n, double x) {
    const double Two302 = 8.148143905337944345073782753637512644205e+90;

    if (x < 0 || nc_isnan(x)) return nc_nan();
    if (nc_isinf_any(x) && x > 0) return 0.0;

    if (n == 0) return neverc_math_y0(x);
    if (x == 0.0) {
        if (n < 0 && (n & 1) == 1) return nc_inf(1);
        return nc_inf(-1);
    }

    int sign = 0;
    if (n < 0) {
        /* -INT_MIN overflows. Huge-order Yn(n, x>0) overflows to -Inf
         * (even n, no sign flip), not 0. Matches Go math.Yn on amd64. */
        if (n == NEVERC_MATH_MIN_INT)
            return nc_inf(-1);
        n = -n;
        if (n & 1) sign = 1;
    }
    if (n == 1) {
        double r = neverc_math_y1(x);
        return sign ? -r : r;
    }

    double b;
    if (x >= Two302) {
        double s = neverc_math_sin(x);
        double c = neverc_math_cos(x);
        double temp;
        switch (n & 3) {
        case 0: temp =  s - c; break;
        case 1: temp = -s - c; break;
        case 2: temp = -s + c; break;
        case 3: temp =  s + c; break;
        default: temp = 0; break;
        }
        b = INV_SQRT_PI * temp / neverc_math_sqrt(x);
    } else {
        double a = neverc_math_y0(x);
        b = neverc_math_y1(x);
        for (int i = 1; i < n && !nc_isinf_any(b); i++) {
            double tmp = b;
            b = ((2.0 * (double)i) / x) * b - a;
            a = tmp;
        }
    }
    return sign ? -b : b;
}
