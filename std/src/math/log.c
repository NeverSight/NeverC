#include "neverc/math.h"
#include "_math_internal.h"

/*
 * Natural logarithm, ported from Go math.Log.
 * Originally from FreeBSD libm (e_log.c), Sun Microsystems.
 */

double neverc_math_log(double x) {
    const double Ln2Hi = 6.93147180369123816490e-01;
    const double Ln2Lo = 1.90821492927058770002e-10;
    const double L1 = 6.666666666666735130e-01;
    const double L2 = 3.999999999940941908e-01;
    const double L3 = 2.857142874366239149e-01;
    const double L4 = 2.222219843214978396e-01;
    const double L5 = 1.818357216161805012e-01;
    const double L6 = 1.531383769920937332e-01;
    const double L7 = 1.479819860511658591e-01;
    const double Sqrt2Over2 = 0.7071067811865476;

    if (nc_isnan(x)) return x;
    if (nc_f64_to_bits(x) == NC_UV_INF) return x;
    if (x < 0.0) return nc_nan();
    if (x == 0.0) return nc_inf(-1);

    int ki;
    double f1 = neverc_math_frexp(x, &ki);
    if (f1 < Sqrt2Over2) {
        f1 *= 2.0;
        ki--;
    }
    double f = f1 - 1.0;
    double k = (double)ki;

    double s = f / (2.0 + f);
    double s2 = s * s;
    double s4 = s2 * s2;
    double t1 = s2 * (L1 + s4 * (L3 + s4 * (L5 + s4 * L7)));
    double t2 = s4 * (L2 + s4 * (L4 + s4 * L6));
    double R = t1 + t2;
    double hfsq = 0.5 * f * f;
    return k * Ln2Hi - ((hfsq - (s * (hfsq + R) + k * Ln2Lo)) - f);
}
