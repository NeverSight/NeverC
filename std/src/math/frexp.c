#include "neverc/math.h"

double neverc_math_frexp(double x, int *exp) {
    return frexp(x, exp);
}
