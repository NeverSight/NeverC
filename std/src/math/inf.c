#include "neverc/math.h"

double neverc_math_inf(int sign) {
    if (sign >= 0)
        return INFINITY;
    return -INFINITY;
}
