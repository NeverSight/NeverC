#include "neverc/math.h"

int neverc_math_isinf(double x, int sign) {
    if (sign > 0)
        return isinf(x) && x > 0;
    if (sign < 0)
        return isinf(x) && x < 0;
    return isinf(x) != 0;
}
