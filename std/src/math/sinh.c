#include "neverc/math.h"
#include "_math_internal.h"

/*
 * Hyperbolic sine, ported from Go math.Sinh.
 * Coefficients are #2029 from Hart & Cheney (20.36D).
 */
double neverc_math_sinh(double x) {
    static const double
        P0 = -0.6307673640497716991184787251e+6,
        P1 = -0.8991272022039509355398013511e+5,
        P2 = -0.2894211355989563807284660366e+4,
        P3 = -0.2630563213397497062819489e+2,
        Q0 = -0.6307673640497716991212077277e+6,
        Q1 =  0.1521517378790019070696485176e+5,
        Q2 = -0.173678953558233699533450911e+3;

    if (nc_isnan(x) || x == 0.0) return x;
    if (nc_isinf_any(x)) return x;

    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }

    double temp;
    if (x > 21.0) {
        temp = neverc_math_exp(x) * 0.5;
    } else if (x > 0.5) {
        double ex = neverc_math_exp(x);
        temp = (ex - 1.0 / ex) * 0.5;
    } else {
        double sq = x * x;
        temp = (((P3 * sq + P2) * sq + P1) * sq + P0) * x;
        temp = temp / (((sq + Q2) * sq + Q1) * sq + Q0);
    }

    return sign ? -temp : temp;
}
