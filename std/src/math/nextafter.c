#include "neverc/math.h"
#include "_math_internal.h"

double neverc_math_nextafter(double x, double y) {
    if (nc_isnan(x) || nc_isnan(y)) return nc_nan();
    if (x == y) return x;
    if (x == 0.0)
        return nc_copysign(nc_f64_from_bits(1), y);

    if ((y > x) == (x > 0.0))
        return nc_f64_from_bits(nc_f64_to_bits(x) + 1);
    return nc_f64_from_bits(nc_f64_to_bits(x) - 1);
}
