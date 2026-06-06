#include "neverc/math.h"

int neverc_math_isnan(double x) {
    return isnan(x) != 0;
}
