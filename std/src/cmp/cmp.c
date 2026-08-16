/*
 * NeverC cmp — ordered comparison utilities.
 * Mirrors Go cmp package: Compare/Less handle NaN properly.
 * NaN is considered less than any non-NaN, and NaN == NaN.
 */

#include "neverc/std/cmp.h"

static int isnan_f32(float x)  { return x != x; }
static int isnan_f64(double x) { return x != x; }

static int is_negzero_f32(float x) {
    union { float f; uint32_t u; } v;
    v.f = x;
    return v.u == 0x80000000u;
}

static int is_negzero_f64(double x) {
    union { double d; uint64_t u; } v;
    v.d = x;
    return v.u == UINT64_C(0x8000000000000000);
}

int neverc_cmp_compare_int(int x, int y) {
    if (x < y) return -1;
    if (x > y) return +1;
    return 0;
}

int neverc_cmp_compare_int64(int64_t x, int64_t y) {
    if (x < y) return -1;
    if (x > y) return +1;
    return 0;
}

int neverc_cmp_compare_uint64(uint64_t x, uint64_t y) {
    if (x < y) return -1;
    if (x > y) return +1;
    return 0;
}

int neverc_cmp_compare_float32(float x, float y) {
    int xn = isnan_f32(x);
    int yn = isnan_f32(y);
    if (xn) return yn ? 0 : -1;
    if (yn) return +1;
    if (x < y) return -1;
    if (x > y) return +1;
    return 0;
}

int neverc_cmp_compare_float64(double x, double y) {
    int xn = isnan_f64(x);
    int yn = isnan_f64(y);
    if (xn) return yn ? 0 : -1;
    if (yn) return +1;
    if (x < y) return -1;
    if (x > y) return +1;
    return 0;
}

int neverc_cmp_less_float32(float x, float y) {
    return (isnan_f32(x) && !isnan_f32(y)) || x < y;
}

int neverc_cmp_less_float64(double x, double y) {
    return (isnan_f64(x) && !isnan_f64(y)) || x < y;
}

int neverc_cmp_isnan_float32(float x)  { return isnan_f32(x); }
int neverc_cmp_isnan_float64(double x) { return isnan_f64(x); }

int neverc_cmp_min_int(int x, int y) { return x < y ? x : y; }
int neverc_cmp_max_int(int x, int y) { return x > y ? x : y; }
int64_t neverc_cmp_min_int64(int64_t x, int64_t y) { return x < y ? x : y; }
int64_t neverc_cmp_max_int64(int64_t x, int64_t y) { return x > y ? x : y; }

float neverc_cmp_min_float32(float x, float y) {
    if (isnan_f32(x)) return x;
    if (isnan_f32(y)) return y;
    if (x < y) return x;
    if (y < x) return y;
    return (is_negzero_f32(y) && !is_negzero_f32(x)) ? y : x;
}

float neverc_cmp_max_float32(float x, float y) {
    if (isnan_f32(x)) return x;
    if (isnan_f32(y)) return y;
    if (x > y) return x;
    if (y > x) return y;
    return (is_negzero_f32(x) && !is_negzero_f32(y)) ? y : x;
}

double neverc_cmp_min_float64(double x, double y) {
    if (isnan_f64(x)) return x;
    if (isnan_f64(y)) return y;
    if (x < y) return x;
    if (y < x) return y;
    return (is_negzero_f64(y) && !is_negzero_f64(x)) ? y : x;
}

double neverc_cmp_max_float64(double x, double y) {
    if (isnan_f64(x)) return x;
    if (isnan_f64(y)) return y;
    if (x > y) return x;
    if (y > x) return y;
    return (is_negzero_f64(x) && !is_negzero_f64(y)) ? y : x;
}

int neverc_cmp_clamp_int(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

int64_t neverc_cmp_clamp_int64(int64_t x, int64_t lo, int64_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

double neverc_cmp_clamp_float64(double x, double lo, double hi) {
    if (isnan_f64(x) || isnan_f64(lo) || isnan_f64(hi)) return x;
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

int neverc_cmp_or_int(int a, int b) { return a != 0 ? a : b; }
int64_t neverc_cmp_or_int64(int64_t a, int64_t b) { return a != 0 ? a : b; }
double neverc_cmp_or_float64(double a, double b) { return a != 0.0 ? a : b; }
