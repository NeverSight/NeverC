#include "neverc/math.h"
#include "_math_internal.h"

static const double _sin_c[] = {
    1.58962301576546568060e-10, -2.50507477628578072866e-8,
    2.75573136213857245213e-6,  -1.98412698295895385996e-4,
    8.33333333332211858878e-3,  -1.66666666666666307295e-1,
};
static const double _cos_c[] = {
    -1.13585365213876817300e-11, 2.08757008419747316778e-9,
    -2.75573141792967388112e-7,  2.48015872888517045348e-5,
    -1.38888888888730564116e-3,  4.16666666666665929218e-2,
};

#define PI4A 7.85398125648498535156e-1
#define PI4B 3.77489470793079817668e-8
#define PI4C 2.69515142907905952645e-15

double neverc_math_cos(double x) {
    if (nc_isnan(x) || nc_isinf_any(x)) return nc_nan();

    int sign = 0;
    x = nc_abs(x);

    uint64_t j = (uint64_t)(x * (4.0 / NEVERC_MATH_PI));
    double y = (double)j;
    if (j & 1) { j++; y += 1.0; }
    j &= 7;
    double z = ((x - y * PI4A) - y * PI4B) - y * PI4C;

    if (j > 3) { j -= 4; sign = !sign; }
    if (j > 1) sign = !sign;

    double zz = z * z;
    if (j == 1 || j == 2) {
        y = z + z*zz*(((((_sin_c[0]*zz + _sin_c[1])*zz + _sin_c[2])*zz + _sin_c[3])*zz + _sin_c[4])*zz + _sin_c[5]);
    } else {
        y = 1.0 - 0.5*zz + zz*zz*(((((_cos_c[0]*zz + _cos_c[1])*zz + _cos_c[2])*zz + _cos_c[3])*zz + _cos_c[4])*zz + _cos_c[5]);
    }
    return sign ? -y : y;
}
