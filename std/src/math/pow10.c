#include "neverc/math.h"

static const double pow10tab[23] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
    1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
    1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

double neverc_math_pow10(int n) {
    if (n >= 0 && n <= 22)
        return pow10tab[n];
    if (n >= -22 && n <= -1)
        return 1.0 / pow10tab[-n];
    return pow(10.0, (double)n);
}
