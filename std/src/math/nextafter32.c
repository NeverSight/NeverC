#include "neverc/std/math.h"
#include "_math_internal.h"

float neverc_math_nextafter32(float x, float y) {
    if (nc_isnan((double)x) || nc_isnan((double)y))
        return nc_f32_from_bits(0x7FC00001U);
    if (x == y) return x;
    if (x == 0.0f) {
        uint32_t r = 1;
        if (y < 0.0f) r |= 0x80000000U;
        return nc_f32_from_bits(r);
    }
    if ((y > x) == (x > 0.0f))
        return nc_f32_from_bits(nc_f32_to_bits(x) + 1);
    return nc_f32_from_bits(nc_f32_to_bits(x) - 1);
}
