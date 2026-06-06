#ifndef _NEVERC_MATH_INTERNAL_H
#define _NEVERC_MATH_INTERNAL_H

/*
 * IEEE 754 double-precision bit manipulation helpers.
 * Used by all self-implemented math functions — NO libc math dependency.
 */

#include <stdint.h>
#include <string.h>

#define NC_SIGN_MASK   0x8000000000000000ULL
#define NC_EXP_MASK    0x7FF0000000000000ULL
#define NC_FRAC_MASK   0x000FFFFFFFFFFFFFULL
#define NC_EXP_SHIFT   52
#define NC_EXP_BIAS    1023
#define NC_UV_NAN      0x7FF8000000000001ULL
#define NC_UV_INF      0x7FF0000000000000ULL
#define NC_UV_NEGINF   0xFFF0000000000000ULL
#define NC_UV_ONE      0x3FF0000000000000ULL

static inline uint64_t nc_f64_to_bits(double f) {
    uint64_t b;
    memcpy(&b, &f, 8);
    return b;
}

static inline double nc_f64_from_bits(uint64_t b) {
    double f;
    memcpy(&f, &b, 8);
    return f;
}

static inline uint32_t nc_f32_to_bits(float f) {
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static inline float nc_f32_from_bits(uint32_t b) {
    float f;
    memcpy(&f, &b, 4);
    return f;
}

static inline int nc_isnan(double f) {
    uint64_t b = nc_f64_to_bits(f);
    return (b & NC_EXP_MASK) == NC_EXP_MASK && (b & NC_FRAC_MASK) != 0;
}

static inline int nc_isinf_any(double f) {
    uint64_t b = nc_f64_to_bits(f);
    return b == NC_UV_INF || b == NC_UV_NEGINF;
}

static inline double nc_abs(double x) {
    return nc_f64_from_bits(nc_f64_to_bits(x) & ~NC_SIGN_MASK);
}

static inline double nc_copysign(double f, double sign) {
    return nc_f64_from_bits((nc_f64_to_bits(f) & ~NC_SIGN_MASK) |
                            (nc_f64_to_bits(sign) & NC_SIGN_MASK));
}

static inline double nc_inf(int sign) {
    return nc_f64_from_bits(sign >= 0 ? NC_UV_INF : NC_UV_NEGINF);
}

static inline double nc_nan(void) {
    return nc_f64_from_bits(NC_UV_NAN);
}

/* normalize: for subnormal x, return y,exp s.t. x == y * 2^exp, y is normal */
static inline double nc_normalize(double x, int *exp) {
    const double SmallestNormal = 2.2250738585072014e-308;
    if (nc_abs(x) < SmallestNormal) {
        *exp = -52;
        return x * (double)(1ULL << 52);
    }
    *exp = 0;
    return x;
}

#endif /* _NEVERC_MATH_INTERNAL_H */
