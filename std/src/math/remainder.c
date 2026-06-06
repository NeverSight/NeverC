#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_remainder(double x, double y) {
    const double HalfMax = NEVERC_MATH_MAX_FLOAT64 / 2.0;
    const double Tiny = 4.45014771701440276618e-308;

    if (nc_isnan(x) || nc_isnan(y) || nc_isinf_any(x) || y == 0.0)
        return nc_nan();
    if (nc_isinf_any(y))
        return x;

    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }
    if (y < 0.0) y = -y;

    if (x == y) return sign ? -0.0 : 0.0;

    if (y <= HalfMax)
        x = neverc_math_fmod(x, y + y);

    if (y < Tiny) {
        if (x + x > y) {
            x -= y;
            if (x + x >= y) x -= y;
        }
    } else {
        double yHalf = 0.5 * y;
        if (x > yHalf) {
            x -= y;
            if (x >= yHalf) x -= y;
        }
    }
    return sign ? -x : x;
}
