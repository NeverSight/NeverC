#include "neverc/math.h"

int neverc_math_signbit(double x) {
    return signbit(x) != 0;
}
