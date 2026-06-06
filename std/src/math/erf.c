#include "neverc/math.h"
#include "_math_internal.h"

/*
 * Error function using rational polynomial approximation (Cody, 1969).
 * Ported/adapted from FreeBSD libm approach.
 * Accuracy: better than 1 ULP for all double inputs.
 */

static const double
    erx  =  8.45062911510467529297e-01,
    efx  =  1.28379167095512586316e-01,
    efx8 =  1.02703333676410069053e+00,
    pp0  =  1.28379167095512558561e-01,
    pp1  = -3.25042107247001499370e-01,
    pp2  = -2.84817495755985104766e-02,
    pp3  = -5.77027029648944159157e-03,
    pp4  = -2.37630166566501626084e-05,
    qq1  =  3.97917223959155352819e-01,
    qq2  =  6.50222499887672944485e-02,
    qq3  =  5.08130628187576562776e-03,
    qq4  =  1.32494738004321644526e-04,
    qq5  = -3.96022827877536812320e-06,
    pa0  = -2.36211856075265944077e-03,
    pa1  =  4.14856118683748331666e-01,
    pa2  = -3.72207876035701323847e-01,
    pa3  =  3.18346619901161753674e-01,
    pa4  = -1.10894694282396677476e-01,
    pa5  =  3.54783043195201877747e-02,
    pa6  = -2.16637559983254089680e-03,
    qa1  =  1.06420880400844228286e-01,
    qa2  =  5.40397917702171048937e-01,
    qa3  =  7.18286544141962539399e-02,
    qa4  =  1.26171219808761642112e-01,
    qa5  =  1.36370839120290507362e-02,
    qa6  =  1.19844998467991074170e-02;

double neverc_math_erf(double x) {
    if (nc_isnan(x)) return x;
    if (nc_isinf_any(x)) return nc_copysign(1.0, x);

    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }

    double result;
    if (x < 0.84375) {
        if (x < 3.7252902984619140625e-09) {
            result = efx8 * x + x;
            result = x + x * efx;
        } else {
            double z = x * x;
            double r = pp0 + z*(pp1 + z*(pp2 + z*(pp3 + z*pp4)));
            double s = 1.0 + z*(qq1 + z*(qq2 + z*(qq3 + z*(qq4 + z*qq5))));
            result = x + x * (r / s);
        }
    } else if (x < 1.25) {
        double s = x - 1.0;
        double P = pa0 + s*(pa1 + s*(pa2 + s*(pa3 + s*(pa4 + s*(pa5 + s*pa6)))));
        double Q = 1.0 + s*(qa1 + s*(qa2 + s*(qa3 + s*(qa4 + s*(qa5 + s*qa6)))));
        result = erx + P / Q;
    } else if (x >= 6.0) {
        result = 1.0;
    } else {
        result = 1.0 - neverc_math_erfc(x);
    }

    return sign ? -result : result;
}
