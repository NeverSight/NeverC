#include "neverc/std/math.h"

/*
 * Inverse complementary error function.
 * erfcinv(x) = erfinv(1 - x)
 *
 * Special cases:
 *   erfcinv(0)  = +Inf
 *   erfcinv(2)  = -Inf
 *   erfcinv(x)  = NaN if x < 0 or x > 2
 *   erfcinv(NaN) = NaN
 */
double neverc_math_erfcinv(double x) {
    return neverc_math_erfinv(1.0 - x);
}
