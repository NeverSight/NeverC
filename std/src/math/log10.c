#include "neverc/std/math.h"
#include "_math_internal.h"

double neverc_math_log10(double x) {
    return neverc_math_log(x) * NEVERC_MATH_LOG10E;
}
