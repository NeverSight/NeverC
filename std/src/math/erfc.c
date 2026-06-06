#include "neverc/math.h"
#include "_math_internal.h"

/*
 * Complementary error function.
 */

static const double
    ra0  = -9.86494403484714822705e-03,
    ra1  = -6.93858572707181764372e-01,
    ra2  = -1.05586262253232909814e+01,
    ra3  = -6.23753324503260060396e+01,
    ra4  = -1.62396669462573071767e+02,
    ra5  = -1.84605092906711035994e+02,
    ra6  = -8.12874355063065934246e+01,
    ra7  = -9.81432934416914548592e+00,
    sa1  =  1.96512716674392571292e+01,
    sa2  =  1.37657754143519702237e+02,
    sa3  =  4.34565877475229228608e+02,
    sa4  =  6.45387271733267880594e+02,
    sa5  =  4.29008140027567833386e+02,
    sa6  =  1.08635005541779435134e+02,
    sa7  =  6.57024977031928170135e+00,
    sa8  = -6.04244152148580987438e-02,
    rb0  = -9.86494292470009928597e-03,
    rb1  = -7.99283237680523006574e-01,
    rb2  = -1.77579549177547519889e+01,
    rb3  = -1.60636384855557935030e+02,
    rb4  = -6.37566443368389085394e+02,
    rb5  = -1.02509513161107724954e+03,
    rb6  = -4.83519191608651397019e+02,
    sb1  =  3.03380607875625778203e+01,
    sb2  =  3.25792512996573918826e+02,
    sb3  =  1.53672958608443695994e+03,
    sb4  =  3.19985821950859553908e+03,
    sb5  =  2.55305040643316442583e+03,
    sb6  =  4.74528541206955367215e+02,
    sb7  = -2.24409524465858183362e+01;

double neverc_math_erfc(double x) {
    if (nc_isnan(x)) return x;
    if (nc_isinf_any(x)) return x > 0 ? 0.0 : 2.0;

    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }

    double result;
    if (x < 0.84375) {
        result = 1.0 - neverc_math_erf(sign ? -x : x);
        return result;
    } else if (x < 1.25) {
        result = 1.0 - neverc_math_erf(sign ? -x : x);
        return result;
    } else if (x < 28.0) {
        double s = 1.0 / (x * x);
        double R, S;
        if (x < 2.857142857142857) {
            R = ra0 + s*(ra1 + s*(ra2 + s*(ra3 + s*(ra4 + s*(ra5 + s*(ra6 + s*ra7))))));
            S = 1.0 + s*(sa1 + s*(sa2 + s*(sa3 + s*(sa4 + s*(sa5 + s*(sa6 + s*(sa7 + s*sa8)))))));
        } else {
            R = rb0 + s*(rb1 + s*(rb2 + s*(rb3 + s*(rb4 + s*(rb5 + s*rb6)))));
            S = 1.0 + s*(sb1 + s*(sb2 + s*(sb3 + s*(sb4 + s*(sb5 + s*(sb6 + s*sb7))))));
        }
        result = neverc_math_exp(-x * x) * (R / S + 0.5) / x;
    } else {
        result = 0.0;
    }

    return sign ? 2.0 - result : result;
}
