#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_floor(double x) {
    if (x == 0.0 || nc_isnan(x) || nc_isinf_any(x))
        return x;

    if (x < 0.0) {
        double ipart;
        double frac = neverc_math_modf(-x, &ipart);
        if (frac != 0.0)
            ipart = ipart + 1.0;
        return -ipart;
    }
    double ipart;
    neverc_math_modf(x, &ipart);
    return ipart;
}
