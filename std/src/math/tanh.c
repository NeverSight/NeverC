#include "neverc/std/math.h"
#include "_math_internal.h"

/* Cephes tanh.c rational approximation, matching Go's math.tanhP/tanhQ. */
static const double tanh_p[3] = {
    -9.64399179425052238628e-1,
    -9.92877231001918586564e+1,
    -1.61468768441708447952e+3,
};
static const double tanh_q[3] = {
    1.12811678491632931402e+2,
    2.23548839060100448583e+3,
    4.84406305325125486048e+3,
};

double neverc_math_tanh(double x) {
    const double max_log = 8.8029691931113054295988e+01; /* log(2**127) */

    if (nc_isnan(x)) return x;
    if (nc_isinf_any(x)) return nc_copysign(1.0, x);
    if (x == 0.0) return x;

    double z = nc_abs(x);
    if (z > 0.5 * max_log)
        return nc_copysign(1.0, x);
    if (z >= 0.625) {
        double s = neverc_math_exp(2.0 * z);
        return nc_copysign(1.0 - 2.0 / (s + 1.0), x);
    }
    /* exp(2x) rounds to the grid around 1.0, so (e2x-1)/(e2x+1) loses every
     * significant digit of a small argument. */
    double s = x * x;
    return x + x * s *
                   ((tanh_p[0] * s + tanh_p[1]) * s + tanh_p[2]) /
                   (((s + tanh_q[0]) * s + tanh_q[1]) * s + tanh_q[2]);
}
