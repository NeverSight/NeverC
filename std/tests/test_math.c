#include "neverc/math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define EPSILON 1e-9

static void check_double(const char *name, double got, double expected) {
    tests_run++;
    if (isnan(expected) && isnan(got)) {
        tests_passed++;
        return;
    }
    if (isinf(expected) && isinf(got) && ((expected > 0) == (got > 0))) {
        tests_passed++;
        return;
    }
    if (fabs(got - expected) < EPSILON || (expected != 0 && fabs((got - expected) / expected) < EPSILON)) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got %.17g, expected %.17g\n", name, got, expected);
    }
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got %d, expected %d\n", name, got, expected);
    }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: expected true\n", name);
    }
}

/* ===== Test: Constants ===== */
static void test_constants(void) {
    printf("[constants]\n");
    check_double("E",       NEVERC_MATH_E,       2.718281828459045);
    check_double("PI",      NEVERC_MATH_PI,      3.141592653589793);
    check_double("PHI",     NEVERC_MATH_PHI,     1.618033988749895);
    check_double("SQRT2",   NEVERC_MATH_SQRT2,   1.4142135623730951);
    check_double("LN2",     NEVERC_MATH_LN2,     0.6931471805599453);
    check_double("LOG2E",   NEVERC_MATH_LOG2E,   1.4426950408889634);
    check_double("LN10",    NEVERC_MATH_LN10,    2.302585092994046);
    check_double("LOG10E",  NEVERC_MATH_LOG10E,  0.4342944819032518);
}

/* ===== Test: Basic Arithmetic ===== */
static void test_abs(void) {
    printf("[abs]\n");
    check_double("abs(3.5)",    neverc_math_abs(3.5),    3.5);
    check_double("abs(-3.5)",   neverc_math_abs(-3.5),   3.5);
    check_double("abs(0)",      neverc_math_abs(0.0),    0.0);
    check_double("abs(NaN)",    neverc_math_abs(NAN),    NAN);
    check_double("abs(+Inf)",   neverc_math_abs(INFINITY), INFINITY);
    check_double("abs(-Inf)",   neverc_math_abs(-INFINITY), INFINITY);
}

static void test_dim(void) {
    printf("[dim]\n");
    check_double("dim(5,3)",     neverc_math_dim(5.0, 3.0),     2.0);
    check_double("dim(3,5)",     neverc_math_dim(3.0, 5.0),     0.0);
    check_double("dim(-2,-5)",   neverc_math_dim(-2.0, -5.0),   3.0);
    check_double("dim(+Inf,+Inf)", neverc_math_dim(INFINITY, INFINITY), NAN);
    check_double("dim(NaN,1)",   neverc_math_dim(NAN, 1.0),     NAN);
}

static void test_max(void) {
    printf("[max]\n");
    check_double("max(3,5)",     neverc_math_max(3.0, 5.0),     5.0);
    check_double("max(-1,-2)",   neverc_math_max(-1.0, -2.0),   -1.0);
    check_double("max(NaN,1)",   neverc_math_max(NAN, 1.0),     NAN);
    check_double("max(+Inf,1)",  neverc_math_max(INFINITY, 1.0), INFINITY);
}

static void test_min(void) {
    printf("[min]\n");
    check_double("min(3,5)",     neverc_math_min(3.0, 5.0),     3.0);
    check_double("min(-1,-2)",   neverc_math_min(-1.0, -2.0),   -2.0);
    check_double("min(NaN,1)",   neverc_math_min(NAN, 1.0),     NAN);
    check_double("min(-Inf,1)",  neverc_math_min(-INFINITY, 1.0), -INFINITY);
}

/* ===== Test: Trigonometric ===== */
static void test_sin(void) {
    printf("[sin]\n");
    check_double("sin(0)",       neverc_math_sin(0.0),       0.0);
    check_double("sin(PI/2)",    neverc_math_sin(NEVERC_MATH_PI / 2.0), 1.0);
    check_double("sin(PI)",      neverc_math_sin(NEVERC_MATH_PI),       0.0);
    check_double("sin(NaN)",     neverc_math_sin(NAN),       NAN);
}

static void test_cos(void) {
    printf("[cos]\n");
    check_double("cos(0)",       neverc_math_cos(0.0),       1.0);
    check_double("cos(PI/2)",    neverc_math_cos(NEVERC_MATH_PI / 2.0), 0.0);
    check_double("cos(PI)",      neverc_math_cos(NEVERC_MATH_PI),       -1.0);
    check_double("cos(NaN)",     neverc_math_cos(NAN),       NAN);
}

static void test_tan(void) {
    printf("[tan]\n");
    check_double("tan(0)",       neverc_math_tan(0.0),       0.0);
    check_double("tan(PI/4)",    neverc_math_tan(NEVERC_MATH_PI / 4.0), 1.0);
    check_double("tan(NaN)",     neverc_math_tan(NAN),       NAN);
}

static void test_sincos(void) {
    printf("[sincos]\n");
    double s, c;
    neverc_math_sincos(NEVERC_MATH_PI / 4.0, &s, &c);
    check_double("sincos(PI/4).sin", s, 0.7071067811865476);
    check_double("sincos(PI/4).cos", c, 0.7071067811865476);
}

/* ===== Test: Inverse Trigonometric ===== */
static void test_asin(void) {
    printf("[asin]\n");
    check_double("asin(0)",    neverc_math_asin(0.0),    0.0);
    check_double("asin(1)",    neverc_math_asin(1.0),    NEVERC_MATH_PI / 2.0);
    check_double("asin(NaN)",  neverc_math_asin(NAN),    NAN);
    check_double("asin(2)",    neverc_math_asin(2.0),    NAN);
}

static void test_acos(void) {
    printf("[acos]\n");
    check_double("acos(1)",    neverc_math_acos(1.0),    0.0);
    check_double("acos(0)",    neverc_math_acos(0.0),    NEVERC_MATH_PI / 2.0);
    check_double("acos(-1)",   neverc_math_acos(-1.0),   NEVERC_MATH_PI);
}

static void test_atan(void) {
    printf("[atan]\n");
    check_double("atan(0)",    neverc_math_atan(0.0),    0.0);
    check_double("atan(1)",    neverc_math_atan(1.0),    NEVERC_MATH_PI / 4.0);
    check_double("atan(+Inf)", neverc_math_atan(INFINITY), NEVERC_MATH_PI / 2.0);
}

static void test_atan2(void) {
    printf("[atan2]\n");
    check_double("atan2(0,1)",    neverc_math_atan2(0.0, 1.0),    0.0);
    check_double("atan2(1,0)",    neverc_math_atan2(1.0, 0.0),    NEVERC_MATH_PI / 2.0);
    check_double("atan2(1,1)",    neverc_math_atan2(1.0, 1.0),    NEVERC_MATH_PI / 4.0);
    check_double("atan2(NaN,1)",  neverc_math_atan2(NAN, 1.0),    NAN);
}

/* ===== Test: Hyperbolic ===== */
static void test_sinh(void) {
    printf("[sinh]\n");
    check_double("sinh(0)",    neverc_math_sinh(0.0),    0.0);
    check_double("sinh(1)",    neverc_math_sinh(1.0),    1.1752011936438014);
    check_double("sinh(NaN)",  neverc_math_sinh(NAN),    NAN);
}

static void test_cosh(void) {
    printf("[cosh]\n");
    check_double("cosh(0)",    neverc_math_cosh(0.0),    1.0);
    check_double("cosh(1)",    neverc_math_cosh(1.0),    1.5430806348152437);
}

static void test_tanh(void) {
    printf("[tanh]\n");
    check_double("tanh(0)",    neverc_math_tanh(0.0),    0.0);
    check_double("tanh(+Inf)", neverc_math_tanh(INFINITY), 1.0);
    check_double("tanh(-Inf)", neverc_math_tanh(-INFINITY), -1.0);
}

static void test_asinh(void) {
    printf("[asinh]\n");
    check_double("asinh(0)",    neverc_math_asinh(0.0),    0.0);
    check_double("asinh(1)",    neverc_math_asinh(1.0),    0.88137358701954302);
}

static void test_acosh(void) {
    printf("[acosh]\n");
    check_double("acosh(1)",    neverc_math_acosh(1.0),    0.0);
    check_double("acosh(2)",    neverc_math_acosh(2.0),    1.3169578969248166);
    check_double("acosh(NaN)",  neverc_math_acosh(NAN),    NAN);
}

static void test_atanh(void) {
    printf("[atanh]\n");
    check_double("atanh(0)",    neverc_math_atanh(0.0),    0.0);
    check_double("atanh(0.5)",  neverc_math_atanh(0.5),    0.5493061443340548);
    check_double("atanh(1)",    neverc_math_atanh(1.0),    INFINITY);
    check_double("atanh(-1)",   neverc_math_atanh(-1.0),   -INFINITY);
}

/* ===== Test: Exponential & Logarithmic ===== */
static void test_exp(void) {
    printf("[exp]\n");
    check_double("exp(0)",     neverc_math_exp(0.0),     1.0);
    check_double("exp(1)",     neverc_math_exp(1.0),     NEVERC_MATH_E);
    check_double("exp(NaN)",   neverc_math_exp(NAN),     NAN);
    check_double("exp(+Inf)",  neverc_math_exp(INFINITY), INFINITY);
    check_double("exp(-Inf)",  neverc_math_exp(-INFINITY), 0.0);
}

static void test_exp2(void) {
    printf("[exp2]\n");
    check_double("exp2(0)",    neverc_math_exp2(0.0),    1.0);
    check_double("exp2(3)",    neverc_math_exp2(3.0),    8.0);
    check_double("exp2(10)",   neverc_math_exp2(10.0),   1024.0);
}

static void test_expm1(void) {
    printf("[expm1]\n");
    check_double("expm1(0)",    neverc_math_expm1(0.0),    0.0);
    check_double("expm1(1e-15)", neverc_math_expm1(1e-15), 1e-15);
    check_double("expm1(1)",    neverc_math_expm1(1.0),    NEVERC_MATH_E - 1.0);
}

static void test_log(void) {
    printf("[log]\n");
    check_double("log(1)",     neverc_math_log(1.0),     0.0);
    check_double("log(E)",     neverc_math_log(NEVERC_MATH_E), 1.0);
    check_double("log(0)",     neverc_math_log(0.0),     -INFINITY);
    check_double("log(-1)",    neverc_math_log(-1.0),    NAN);
    check_double("log(NaN)",   neverc_math_log(NAN),     NAN);
    check_double("log(+Inf)",  neverc_math_log(INFINITY), INFINITY);
}

static void test_log2(void) {
    printf("[log2]\n");
    check_double("log2(1)",    neverc_math_log2(1.0),    0.0);
    check_double("log2(2)",    neverc_math_log2(2.0),    1.0);
    check_double("log2(1024)", neverc_math_log2(1024.0), 10.0);
}

static void test_log10(void) {
    printf("[log10]\n");
    check_double("log10(1)",    neverc_math_log10(1.0),    0.0);
    check_double("log10(10)",   neverc_math_log10(10.0),   1.0);
    check_double("log10(100)",  neverc_math_log10(100.0),  2.0);
    check_double("log10(1000)", neverc_math_log10(1000.0), 3.0);
}

static void test_log1p(void) {
    printf("[log1p]\n");
    check_double("log1p(0)",    neverc_math_log1p(0.0),    0.0);
    check_double("log1p(1e-15)", neverc_math_log1p(1e-15), 1e-15);
    check_double("log1p(-1)",   neverc_math_log1p(-1.0),   -INFINITY);
}

static void test_logb(void) {
    printf("[logb]\n");
    check_double("logb(1)",    neverc_math_logb(1.0),    0.0);
    check_double("logb(8)",    neverc_math_logb(8.0),    3.0);
    check_double("logb(0.5)",  neverc_math_logb(0.5),    -1.0);
}

static void test_ilogb(void) {
    printf("[ilogb]\n");
    check_int("ilogb(1)",    neverc_math_ilogb(1.0),    0);
    check_int("ilogb(8)",    neverc_math_ilogb(8.0),    3);
    check_int("ilogb(0.5)",  neverc_math_ilogb(0.5),    -1);
}

/* ===== Test: Power & Root ===== */
static void test_sqrt(void) {
    printf("[sqrt]\n");
    check_double("sqrt(4)",    neverc_math_sqrt(4.0),    2.0);
    check_double("sqrt(2)",    neverc_math_sqrt(2.0),    NEVERC_MATH_SQRT2);
    check_double("sqrt(0)",    neverc_math_sqrt(0.0),    0.0);
    check_double("sqrt(-1)",   neverc_math_sqrt(-1.0),   NAN);
    check_double("sqrt(NaN)",  neverc_math_sqrt(NAN),    NAN);
    check_double("sqrt(+Inf)", neverc_math_sqrt(INFINITY), INFINITY);
}

static void test_cbrt(void) {
    printf("[cbrt]\n");
    check_double("cbrt(27)",   neverc_math_cbrt(27.0),   3.0);
    check_double("cbrt(-8)",   neverc_math_cbrt(-8.0),   -2.0);
    check_double("cbrt(0)",    neverc_math_cbrt(0.0),    0.0);
}

static void test_pow(void) {
    printf("[pow]\n");
    check_double("pow(2,10)",  neverc_math_pow(2.0, 10.0), 1024.0);
    check_double("pow(10,3)",  neverc_math_pow(10.0, 3.0), 1000.0);
    check_double("pow(x,0)",   neverc_math_pow(42.0, 0.0), 1.0);
    check_double("pow(1,y)",   neverc_math_pow(1.0, 999.0), 1.0);
    check_double("pow(NaN,1)", neverc_math_pow(NAN, 1.0), NAN);
}

static void test_pow10(void) {
    printf("[pow10]\n");
    check_double("pow10(0)",   neverc_math_pow10(0),   1.0);
    check_double("pow10(1)",   neverc_math_pow10(1),   10.0);
    check_double("pow10(3)",   neverc_math_pow10(3),   1000.0);
    check_double("pow10(-1)",  neverc_math_pow10(-1),  0.1);
    check_double("pow10(-3)",  neverc_math_pow10(-3),  0.001);
    check_double("pow10(22)",  neverc_math_pow10(22),  1e22);
}

static void test_hypot(void) {
    printf("[hypot]\n");
    check_double("hypot(3,4)",   neverc_math_hypot(3.0, 4.0),   5.0);
    check_double("hypot(5,12)",  neverc_math_hypot(5.0, 12.0),  13.0);
    check_double("hypot(0,0)",   neverc_math_hypot(0.0, 0.0),   0.0);
    check_double("hypot(Inf,1)", neverc_math_hypot(INFINITY, 1.0), INFINITY);
    check_double("hypot(NaN,1)", neverc_math_hypot(NAN, 1.0),   NAN);
}

/* ===== Test: Rounding ===== */
static void test_ceil(void) {
    printf("[ceil]\n");
    check_double("ceil(1.5)",   neverc_math_ceil(1.5),   2.0);
    check_double("ceil(-1.5)",  neverc_math_ceil(-1.5),  -1.0);
    check_double("ceil(0)",     neverc_math_ceil(0.0),   0.0);
    check_double("ceil(NaN)",   neverc_math_ceil(NAN),   NAN);
}

static void test_floor(void) {
    printf("[floor]\n");
    check_double("floor(1.5)",  neverc_math_floor(1.5),  1.0);
    check_double("floor(-1.5)", neverc_math_floor(-1.5), -2.0);
    check_double("floor(0)",    neverc_math_floor(0.0),  0.0);
}

static void test_trunc(void) {
    printf("[trunc]\n");
    check_double("trunc(1.9)",  neverc_math_trunc(1.9),  1.0);
    check_double("trunc(-1.9)", neverc_math_trunc(-1.9), -1.0);
    check_double("trunc(0)",    neverc_math_trunc(0.0),  0.0);
}

static void test_round(void) {
    printf("[round]\n");
    check_double("round(1.5)",  neverc_math_round(1.5),  2.0);
    check_double("round(2.5)",  neverc_math_round(2.5),  3.0);
    check_double("round(-1.5)", neverc_math_round(-1.5), -2.0);
    check_double("round(0.4)",  neverc_math_round(0.4),  0.0);
}

static void test_roundtoeven(void) {
    printf("[roundtoeven]\n");
    check_double("rte(0.5)",  neverc_math_roundtoeven(0.5),  0.0);
    check_double("rte(1.5)",  neverc_math_roundtoeven(1.5),  2.0);
    check_double("rte(2.5)",  neverc_math_roundtoeven(2.5),  2.0);
    check_double("rte(3.5)",  neverc_math_roundtoeven(3.5),  4.0);
    check_double("rte(-0.5)", neverc_math_roundtoeven(-0.5), 0.0);
    check_double("rte(-1.5)", neverc_math_roundtoeven(-1.5), -2.0);
}

static void test_fmod(void) {
    printf("[fmod]\n");
    check_double("fmod(5,3)",   neverc_math_fmod(5.0, 3.0),   2.0);
    check_double("fmod(7,2)",   neverc_math_fmod(7.0, 2.0),   1.0);
    check_double("fmod(-5,3)",  neverc_math_fmod(-5.0, 3.0),  -2.0);
    check_double("fmod(NaN,1)", neverc_math_fmod(NAN, 1.0),   NAN);
    check_double("fmod(1,0)",   neverc_math_fmod(1.0, 0.0),   NAN);
}

static void test_remainder(void) {
    printf("[remainder]\n");
    check_double("rem(5,3)",    neverc_math_remainder(5.0, 3.0),   -1.0);
    check_double("rem(7,2)",    neverc_math_remainder(7.0, 2.0),   -1.0);
    check_double("rem(NaN,1)",  neverc_math_remainder(NAN, 1.0),   NAN);
}

/* ===== Test: Decomposition ===== */
static void test_modf(void) {
    printf("[modf]\n");
    double ipart;
    double fpart = neverc_math_modf(3.75, &ipart);
    check_double("modf(3.75).int",  ipart, 3.0);
    check_double("modf(3.75).frac", fpart, 0.75);

    fpart = neverc_math_modf(-2.25, &ipart);
    check_double("modf(-2.25).int",  ipart, -2.0);
    check_double("modf(-2.25).frac", fpart, -0.25);
}

static void test_frexp(void) {
    printf("[frexp]\n");
    int exp;
    double frac = neverc_math_frexp(8.0, &exp);
    check_double("frexp(8).frac", frac, 0.5);
    check_int("frexp(8).exp",     exp,  4);

    frac = neverc_math_frexp(0.0, &exp);
    check_double("frexp(0).frac", frac, 0.0);
    check_int("frexp(0).exp",     exp,  0);
}

static void test_ldexp(void) {
    printf("[ldexp]\n");
    check_double("ldexp(0.5,4)", neverc_math_ldexp(0.5, 4), 8.0);
    check_double("ldexp(1,10)",  neverc_math_ldexp(1.0, 10), 1024.0);
    check_double("ldexp(0,5)",   neverc_math_ldexp(0.0, 5), 0.0);
}

static void test_nextafter(void) {
    printf("[nextafter]\n");
    double na = neverc_math_nextafter(1.0, 2.0);
    check_true("nextafter(1,2)>1", na > 1.0);
    check_true("nextafter(1,2)<1+eps", na < 1.0 + 1e-10);

    na = neverc_math_nextafter(1.0, 0.0);
    check_true("nextafter(1,0)<1", na < 1.0);
}

/* ===== Test: Sign & Bit ===== */
static void test_copysign(void) {
    printf("[copysign]\n");
    check_double("copysign(3,-1)", neverc_math_copysign(3.0, -1.0), -3.0);
    check_double("copysign(-3,1)", neverc_math_copysign(-3.0, 1.0), 3.0);
    check_double("copysign(0,-1)", neverc_math_copysign(0.0, -1.0), -0.0);
}

static void test_signbit(void) {
    printf("[signbit]\n");
    check_int("signbit(1)",    neverc_math_signbit(1.0),    0);
    check_int("signbit(-1)",   neverc_math_signbit(-1.0),   1);
    check_int("signbit(0)",    neverc_math_signbit(0.0),    0);
    check_int("signbit(-0)",   neverc_math_signbit(-0.0),   1);
}

/* ===== Test: FMA ===== */
static void test_fma(void) {
    printf("[fma]\n");
    check_double("fma(2,3,4)",  neverc_math_fma(2.0, 3.0, 4.0),  10.0);
    check_double("fma(0,0,0)",  neverc_math_fma(0.0, 0.0, 0.0),  0.0);
}

/* ===== Test: Error Functions ===== */
static void test_erf(void) {
    printf("[erf]\n");
    check_double("erf(0)",     neverc_math_erf(0.0),     0.0);
    check_double("erf(1)",     neverc_math_erf(1.0),     0.8427007929497149);
    check_double("erf(+Inf)",  neverc_math_erf(INFINITY), 1.0);
    check_double("erf(-Inf)",  neverc_math_erf(-INFINITY), -1.0);
}

static void test_erfc(void) {
    printf("[erfc]\n");
    check_double("erfc(0)",    neverc_math_erfc(0.0),    1.0);
    check_double("erfc(1)",    neverc_math_erfc(1.0),    0.1572992070502851);
}

static void test_erfinv(void) {
    printf("[erfinv]\n");
    check_double("erfinv(0)",    neverc_math_erfinv(0.0),    0.0);
    check_double("erfinv(1)",    neverc_math_erfinv(1.0),    INFINITY);
    check_double("erfinv(-1)",   neverc_math_erfinv(-1.0),   -INFINITY);
    check_double("erfinv(NaN)",  neverc_math_erfinv(NAN),    NAN);
    check_double("erfinv(2)",    neverc_math_erfinv(2.0),    NAN);

    /* round-trip: erf(erfinv(x)) ≈ x */
    double x = 0.5;
    double rt = neverc_math_erf(neverc_math_erfinv(x));
    check_double("erf(erfinv(0.5))", rt, x);

    x = -0.9;
    rt = neverc_math_erf(neverc_math_erfinv(x));
    check_double("erf(erfinv(-0.9))", rt, x);
}

/* ===== Test: Gamma ===== */
static void test_gamma(void) {
    printf("[gamma]\n");
    check_double("gamma(1)",    neverc_math_gamma(1.0),    1.0);
    check_double("gamma(5)",    neverc_math_gamma(5.0),    24.0);
    check_double("gamma(0.5)",  neverc_math_gamma(0.5),    NEVERC_MATH_SQRT_PI);
    check_double("gamma(NaN)",  neverc_math_gamma(NAN),    NAN);
}

static void test_lgamma(void) {
    printf("[lgamma]\n");
    check_double("lgamma(1)",   neverc_math_lgamma(1.0),   0.0);
    check_double("lgamma(2)",   neverc_math_lgamma(2.0),   0.0);
}

/* ===== Test: Bessel Functions ===== */
static void test_j0(void) {
    printf("[j0]\n");
    check_double("j0(0)",    neverc_math_j0(0.0),    1.0);
    check_double("j0(NaN)",  neverc_math_j0(NAN),    NAN);
    check_double("j0(+Inf)", neverc_math_j0(INFINITY), 0.0);
    check_double("j0(-Inf)", neverc_math_j0(-INFINITY), 0.0);

    /* Go test vectors (vf[i] → expected j0) */
    check_double("j0(4.979)",  neverc_math_j0(4.9790119248836735e+00),
                 -1.8444682230601672018219338e-01);
    check_double("j0(7.739)",  neverc_math_j0(7.7388724745781045e+00),
                 2.27353668906331975435892e-01);
    check_double("j0(-0.277)", neverc_math_j0(-2.7688005719200159e-01),
                 9.809259936157051116270273e-01);
    check_double("j0(-5.011)", neverc_math_j0(-5.0106036182710749e+00),
                 -1.741170131426226587841181e-01);
    check_double("j0(9.636)",  neverc_math_j0(9.6362937071984173e+00),
                 -2.1389448451144143352039069e-01);
}

static void test_y0(void) {
    printf("[y0]\n");
    check_double("y0(+Inf)", neverc_math_y0(INFINITY), 0.0);
    check_double("y0(0)",    neverc_math_y0(0.0),    -INFINITY);
    check_double("y0(-1)",   neverc_math_y0(-1.0),   NAN);
    check_double("y0(NaN)",  neverc_math_y0(NAN),    NAN);

    check_double("y0(4.979)", neverc_math_y0(4.9790119248836735e+00),
                 -3.053399153780788357534855e-01);
    check_double("y0(7.739)", neverc_math_y0(7.7388724745781045e+00),
                 1.7437227649515231515503649e-01);
    check_double("y0(2.926)", neverc_math_y0(2.9263772392439646e+00),
                 4.000004067997901144239363e-01);
}

static void test_j1(void) {
    printf("[j1]\n");
    check_double("j1(0)",    neverc_math_j1(0.0),    0.0);
    check_double("j1(NaN)",  neverc_math_j1(NAN),    NAN);
    check_double("j1(+Inf)", neverc_math_j1(INFINITY), 0.0);
    check_double("j1(-Inf)", neverc_math_j1(-INFINITY), 0.0);

    check_double("j1(4.979)", neverc_math_j1(4.9790119248836735e+00),
                 -3.251526395295203422162967e-01);
    check_double("j1(7.739)", neverc_math_j1(7.7388724745781045e+00),
                 1.893581711430515718062564e-01);
    check_double("j1(-0.277)", neverc_math_j1(-2.7688005719200159e-01),
                 -1.3711761352467242914491514e-01);
    check_double("j1(9.636)", neverc_math_j1(9.6362937071984173e+00),
                 1.3133899188830978473849215e-01);
}

static void test_y1(void) {
    printf("[y1]\n");
    check_double("y1(+Inf)", neverc_math_y1(INFINITY), 0.0);
    check_double("y1(0)",    neverc_math_y1(0.0),    -INFINITY);
    check_double("y1(-1)",   neverc_math_y1(-1.0),   NAN);
    check_double("y1(NaN)",  neverc_math_y1(NAN),    NAN);

    check_double("y1(4.979)", neverc_math_y1(4.9790119248836735e+00),
                 0.15494213737457922210218611);
    check_double("y1(7.739)", neverc_math_y1(7.7388724745781045e+00),
                 -0.2165955142081145245075746);
}

static void test_jn(void) {
    printf("[jn]\n");
    check_double("jn(0,1)", neverc_math_jn(0, 1.0), neverc_math_j0(1.0));
    check_double("jn(1,1)", neverc_math_jn(1, 1.0), neverc_math_j1(1.0));
    check_double("jn(n,NaN)", neverc_math_jn(2, NAN), NAN);
    check_double("jn(n,+Inf)", neverc_math_jn(2, INFINITY), 0.0);

    /* J2 test vectors from Go */
    check_double("jn(2,4.979)", neverc_math_jn(2, 4.9790119248836735e+00),
                 5.3837518920137802565192769e-02);
    check_double("jn(2,7.739)", neverc_math_jn(2, 7.7388724745781045e+00),
                 -1.7841678003393207281244667e-01);

    /* J(-3, x) test vectors from Go */
    check_double("jn(-3,4.979)", neverc_math_jn(-3, 4.9790119248836735e+00),
                 -3.684042080996403091021151e-01);
    check_double("jn(-3,7.739)", neverc_math_jn(-3, 7.7388724745781045e+00),
                 2.8157665936340887268092661e-01);
}

static void test_yn(void) {
    printf("[yn]\n");
    check_double("yn(0,1)",  neverc_math_yn(0, 1.0), neverc_math_y0(1.0));
    check_double("yn(1,1)",  neverc_math_yn(1, 1.0), neverc_math_y1(1.0));
    check_double("yn(n,-1)", neverc_math_yn(2, -1.0), NAN);
    check_double("yn(n,NaN)", neverc_math_yn(2, NAN), NAN);
    check_double("yn(n,+Inf)", neverc_math_yn(2, INFINITY), 0.0);
    check_double("yn(0,0)",  neverc_math_yn(0, 0.0), -INFINITY);
    check_double("yn(1,0)",  neverc_math_yn(1, 0.0), -INFINITY);

    /* Y2 test vectors from Go */
    check_double("yn(2,4.979)", neverc_math_yn(2, 4.9790119248836735e+00),
                 0.3675780219390303613394936);
    check_double("yn(2,7.739)", neverc_math_yn(2, 7.7388724745781045e+00),
                 -0.23034826393250119879267257);
}

/* ===== Test: Special Values ===== */
static void test_special_values(void) {
    printf("[special_values]\n");
    check_true("nan is NaN", neverc_math_isnan(neverc_math_nan()));
    check_true("+inf is Inf", neverc_math_isinf(neverc_math_inf(1), 1));
    check_true("-inf is Inf", neverc_math_isinf(neverc_math_inf(-1), -1));
    check_true("inf(0) is +Inf", neverc_math_isinf(neverc_math_inf(0), 1));
    check_int("isnan(1)", neverc_math_isnan(1.0), 0);
    check_int("isinf(1,0)", neverc_math_isinf(1.0, 0), 0);
}

/* ===== Test: Float Bits ===== */
static void test_float_bits(void) {
    printf("[float_bits]\n");
    double v = 1.0;
    uint64_t bits = neverc_math_float64bits(v);
    double back = neverc_math_float64frombits(bits);
    check_double("float64 roundtrip", back, v);

    float fv = 1.0f;
    uint32_t fbits = neverc_math_float32bits(fv);
    float fback = neverc_math_float32frombits(fbits);
    check_double("float32 roundtrip", (double)fback, (double)fv);

    check_true("float64bits(1.0) == 0x3FF0...",
               neverc_math_float64bits(1.0) == 0x3FF0000000000000ULL);
    check_true("float32bits(1.0f) == 0x3F800000",
               neverc_math_float32bits(1.0f) == 0x3F800000U);
}

/* ===== Main ===== */
int main(void) {
    printf("=== NeverC Math Library Tests ===\n\n");

    test_constants();
    test_abs();
    test_dim();
    test_max();
    test_min();

    test_sin();
    test_cos();
    test_tan();
    test_sincos();

    test_asin();
    test_acos();
    test_atan();
    test_atan2();

    test_sinh();
    test_cosh();
    test_tanh();
    test_asinh();
    test_acosh();
    test_atanh();

    test_exp();
    test_exp2();
    test_expm1();
    test_log();
    test_log2();
    test_log10();
    test_log1p();
    test_logb();
    test_ilogb();

    test_sqrt();
    test_cbrt();
    test_pow();
    test_pow10();
    test_hypot();

    test_ceil();
    test_floor();
    test_trunc();
    test_round();
    test_roundtoeven();
    test_fmod();
    test_remainder();

    test_modf();
    test_frexp();
    test_ldexp();
    test_nextafter();

    test_copysign();
    test_signbit();
    test_fma();

    test_erf();
    test_erfc();
    test_erfinv();

    test_gamma();
    test_lgamma();

    test_j0();
    test_y0();
    test_j1();
    test_y1();
    test_jn();
    test_yn();

    test_special_values();
    test_float_bits();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
