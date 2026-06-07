#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * log(1+x) with full precision for small x.
 * Ported from Go math.Log1p, originally from FreeBSD libm (s_log1p.c).
 *
 * Uses argument reduction with correction term to avoid catastrophic
 * cancellation that occurs with naive log(1+x).
 * Accuracy: < 1 ULP for all double inputs.
 */
double neverc_math_log1p(double x) {
    const double Sqrt2M1     =  4.142135623730950488017e-01;
    const double Sqrt2HalfM1 = -2.928932188134524755992e-01;
    const double Small       =  1.0 / (1 << 29);
    const double Tiny        =  1.0 / (double)(1ULL << 54);
    const double Two53       =  (double)(1ULL << 53);
    const double Ln2Hi       =  6.93147180369123816490e-01;
    const double Ln2Lo       =  1.90821492927058770002e-10;
    const double Lp1 = 6.666666666666735130e-01;
    const double Lp2 = 3.999999999940941908e-01;
    const double Lp3 = 2.857142874366239149e-01;
    const double Lp4 = 2.222219843214978396e-01;
    const double Lp5 = 1.818357216161805012e-01;
    const double Lp6 = 1.531383769920937332e-01;
    const double Lp7 = 1.479819860511658591e-01;

    if (x < -1.0 || nc_isnan(x)) return nc_nan();
    if (x == -1.0) return nc_inf(-1);
    if (nc_isinf_any(x)) return x;

    double absx = nc_abs(x);
    double f = 0.0;
    uint64_t iu = 0;
    int k = 1;

    if (absx < Sqrt2M1) {
        if (absx < Small) {
            if (absx < Tiny) return x;
            return x - x * x * 0.5;
        }
        if (x > Sqrt2HalfM1) {
            k = 0;
            f = x;
            iu = 1;
        }
    }

    double c = 0.0;
    if (k != 0) {
        double u;
        if (absx < Two53) {
            u = 1.0 + x;
            iu = nc_f64_to_bits(u);
            k = (int)((iu >> 52) - 1023);
            if (k > 0)
                c = 1.0 - (u - x);
            else
                c = x - (u - 1.0);
            c /= u;
        } else {
            u = x;
            iu = nc_f64_to_bits(u);
            k = (int)((iu >> 52) - 1023);
            c = 0;
        }
        iu &= 0x000FFFFFFFFFFFFFULL;
        if (iu < 0x0006A09E667F3BCDULL) {
            u = nc_f64_from_bits(iu | 0x3FF0000000000000ULL);
        } else {
            k++;
            u = nc_f64_from_bits(iu | 0x3FE0000000000000ULL);
            iu = (0x0010000000000000ULL - iu) >> 2;
        }
        f = u - 1.0;
    }

    double hfsq = 0.5 * f * f;
    double s, R, z;

    if (iu == 0) {
        if (f == 0.0) {
            if (k == 0) return 0.0;
            c += (double)k * Ln2Lo;
            return (double)k * Ln2Hi + c;
        }
        R = hfsq * (1.0 - 0.66666666666666666 * f);
        if (k == 0) return f - R;
        return (double)k * Ln2Hi - ((R - ((double)k * Ln2Lo + c)) - f);
    }

    s = f / (2.0 + f);
    z = s * s;
    R = z * (Lp1 + z*(Lp2 + z*(Lp3 + z*(Lp4 + z*(Lp5 + z*(Lp6 + z*Lp7))))));
    if (k == 0)
        return f - (hfsq - s * (hfsq + R));
    return (double)k * Ln2Hi - ((hfsq - (s*(hfsq+R) + ((double)k * Ln2Lo + c))) - f);
}
