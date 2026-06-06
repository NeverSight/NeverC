#include "neverc/math.h"
#include "_math_internal.h"
#include "_trig_reduce.h"

/*
 * sin(x) using Cephes polynomial approximation.
 * Ported from Go math.Sin, originally from Cephes Math Library.
 * Range reduction into intervals of pi/4, then polynomial evaluation.
 * For |x| >= 2^29, uses Payne-Hanek exact reduction.
 */

static const double _sin_coeff[] = {
    1.58962301576546568060e-10,
    -2.50507477628578072866e-8,
    2.75573136213857245213e-6,
    -1.98412698295895385996e-4,
    8.33333333332211858878e-3,
    -1.66666666666666307295e-1,
};

static const double _cos_coeff[] = {
    -1.13585365213876817300e-11,
    2.08757008419747316778e-9,
    -2.75573141792967388112e-7,
    2.48015872888517045348e-5,
    -1.38888888888730564116e-3,
    4.16666666666665929218e-2,
};

#define PI4A 7.85398125648498535156e-1
#define PI4B 3.77489470793079817668e-8
#define PI4C 2.69515142907905952645e-15

double neverc_math_sin(double x) {
    if (x == 0.0 || nc_isnan(x)) return x;
    if (nc_isinf_any(x)) return nc_nan();

    int sign = 0;
    if (x < 0.0) { x = -x; sign = 1; }

    uint64_t j;
    double z;

    if (x >= TRIG_REDUCE_THRESHOLD) {
        nc_trig_reduce(x, &j, &z);
    } else {
        j = (uint64_t)(x * (4.0 / NEVERC_MATH_PI));
        double y = (double)j;
        if (j & 1) { j++; y += 1.0; }
        j &= 7;
        z = ((x - y * PI4A) - y * PI4B) - y * PI4C;
    }

    if (j > 3) { sign = !sign; j -= 4; }

    double zz = z * z;
    double result;
    if (j == 1 || j == 2) {
        result = 1.0 - 0.5*zz + zz*zz*(((((_cos_coeff[0]*zz + _cos_coeff[1])*zz + _cos_coeff[2])*zz + _cos_coeff[3])*zz + _cos_coeff[4])*zz + _cos_coeff[5]);
    } else {
        result = z + z*zz*(((((_sin_coeff[0]*zz + _sin_coeff[1])*zz + _sin_coeff[2])*zz + _sin_coeff[3])*zz + _sin_coeff[4])*zz + _sin_coeff[5]);
    }
    return sign ? -result : result;
}
