#include "neverc/std/math.h"
#include <string.h>

uint64_t neverc_math_float64bits(double f) {
    uint64_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

double neverc_math_float64frombits(uint64_t bits) {
    double f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}
