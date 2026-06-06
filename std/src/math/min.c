#include "neverc/math.h"

double neverc_math_min(double x, double y) {
    if (isinf(x) && x < 0) return x;
    if (isinf(y) && y < 0) return y;
    if (isnan(x) || isnan(y)) return NAN;
    if (x == 0 && y == 0) {
        if (signbit(x))
            return x;
        return y;
    }
    if (x < y) return x;
    return y;
}
