#include "neverc/math.h"
#include "_math_internal.h"

int neverc_math_isinf(double x, int sign) {
    uint64_t b = nc_f64_to_bits(x);
    if (sign > 0)
        return b == NC_UV_INF;
    if (sign < 0)
        return b == NC_UV_NEGINF;
    return b == NC_UV_INF || b == NC_UV_NEGINF;
}
