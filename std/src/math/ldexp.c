#include "neverc/math.h"

double neverc_math_ldexp(double frac, int exp) {
    return ldexp(frac, exp);
}
