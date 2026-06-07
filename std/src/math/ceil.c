#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_ceil(double x) {
    return -neverc_math_floor(-x);
}
