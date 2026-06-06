#include "neverc/math.h"

double neverc_math_dim(double x, double y) {
    double v = x - y;
    if (v <= 0)
        return 0;
    return v;
}
