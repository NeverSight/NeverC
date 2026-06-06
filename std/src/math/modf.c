#include "neverc/math.h"

double neverc_math_modf(double x, double *iptr) {
    return modf(x, iptr);
}
