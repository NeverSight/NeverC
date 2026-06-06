#include "neverc/math.h"
#include <string.h>

uint32_t neverc_math_float32bits(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

float neverc_math_float32frombits(uint32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}
