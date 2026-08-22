#include "neverc/std/strconv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_ll(const char *name, long long got, long long expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %lld, expected %lld\n", name, got, expected); }
}

static void check_ull(const char *name, unsigned long long got, unsigned long long expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %llu, expected %llu\n", name, got, expected); }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: expected true\n", name); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) { tests_passed++; }
    else {
        tests_failed++;
        printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name,
               got ? got : "(null)", expected ? expected : "(null)");
    }
}

static void check_double_approx(const char *name, double got, double expected, double eps) {
    tests_run++;
    double diff = got - expected;
    if (diff < 0) diff = -diff;
    if (diff < eps) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %.17g, expected %.17g\n", name, got, expected); }
}

/* ===== Parse Bool ===== */
static void test_parse_bool(void) {
    printf("[parse_bool]\n");
    int v;
    check_int("parse true",  neverc_strconv_parse_bool("true", &v), 0);  check_int("val", v, 1);
    check_int("parse True",  neverc_strconv_parse_bool("True", &v), 0);  check_int("val", v, 1);
    check_int("parse 1",     neverc_strconv_parse_bool("1", &v), 0);     check_int("val", v, 1);
    check_int("parse false", neverc_strconv_parse_bool("false", &v), 0); check_int("val", v, 0);
    check_int("parse False", neverc_strconv_parse_bool("False", &v), 0); check_int("val", v, 0);
    check_int("parse 0",     neverc_strconv_parse_bool("0", &v), 0);     check_int("val", v, 0);
    check_int("parse bad",   neverc_strconv_parse_bool("yes", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("parse empty", neverc_strconv_parse_bool("", &v), NEVERC_STRCONV_ERR_SYNTAX);
}

/* ===== Atoi ===== */
static void test_atoi(void) {
    printf("[atoi]\n");
    int v;
    check_int("atoi 42",      neverc_strconv_atoi("42", &v), 0);     check_int("val", v, 42);
    check_int("atoi -42",     neverc_strconv_atoi("-42", &v), 0);    check_int("val", v, -42);
    check_int("atoi 0",       neverc_strconv_atoi("0", &v), 0);      check_int("val", v, 0);
    check_int("atoi +123",    neverc_strconv_atoi("+123", &v), 0);   check_int("val", v, 123);
    check_int("atoi bad",     neverc_strconv_atoi("abc", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("atoi empty",   neverc_strconv_atoi("", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("atoi positive overflow",
              neverc_strconv_atoi("18446744073709551616", &v),
              NEVERC_STRCONV_ERR_RANGE);
    check_int("atoi positive overflow clamp", v, INT_MAX);
    check_int("atoi negative overflow",
              neverc_strconv_atoi("-18446744073709551616", &v),
              NEVERC_STRCONV_ERR_RANGE);
    check_int("atoi negative overflow clamp", v, INT_MIN);
    /* Classic wrap: values that fit in int64 but not int32. */
    check_int("atoi INT_MAX exact", neverc_strconv_atoi("2147483647", &v), 0);
    check_int("atoi INT_MAX val", v, INT_MAX);
    check_int("atoi INT_MAX+1 wrap",
              neverc_strconv_atoi("2147483648", &v), NEVERC_STRCONV_ERR_RANGE);
    check_int("atoi INT_MAX+1 clamp", v, INT_MAX);
    check_int("atoi INT_MIN exact", neverc_strconv_atoi("-2147483648", &v), 0);
    check_int("atoi INT_MIN val", v, INT_MIN);
    check_int("atoi INT_MIN-1 wrap",
              neverc_strconv_atoi("-2147483649", &v), NEVERC_STRCONV_ERR_RANGE);
    check_int("atoi INT_MIN-1 clamp", v, INT_MIN);
    check_int("atoi leading zeros overflow",
              neverc_strconv_atoi("0002147483648", &v), NEVERC_STRCONV_ERR_RANGE);
    check_int("atoi leading zeros overflow clamp", v, INT_MAX);

    long long ll = 0;
    check_int("atol 42", neverc_strconv_atol("42", &ll), 0);
    check_ll("atol val", ll, 42LL);
    check_int("atol -99", neverc_strconv_atol("-99", &ll), 0);
    check_ll("atol neg", ll, -99LL);
    check_int("atol bad", neverc_strconv_atol("xyz", &ll), NEVERC_STRCONV_ERR_SYNTAX);
}

/* ===== ParseInt ===== */
static void test_parse_int(void) {
    printf("[parse_int]\n");
    long long v;
    check_int("base10",  neverc_strconv_parse_int("255", 10, &v), 0);   check_ll("val", v, 255);
    check_int("base16",  neverc_strconv_parse_int("ff", 16, &v), 0);    check_ll("val", v, 255);
    check_int("base2",   neverc_strconv_parse_int("11111111", 2, &v), 0); check_ll("val", v, 255);
    check_int("base8",   neverc_strconv_parse_int("377", 8, &v), 0);    check_ll("val", v, 255);
    check_int("auto hex", neverc_strconv_parse_int("0xff", 0, &v), 0);  check_ll("val", v, 255);
    check_int("auto bin", neverc_strconv_parse_int("0b11111111", 0, &v), 0); check_ll("val", v, 255);
    check_int("auto oct", neverc_strconv_parse_int("0377", 0, &v), 0);  check_ll("val", v, 255);
    check_int("negative", neverc_strconv_parse_int("-100", 10, &v), 0); check_ll("val", v, -100);
    check_int("negative auto underscore",
              neverc_strconv_parse_int("-0x_ff", 0, &v), 0);
    check_ll("negative auto underscore val", v, -255);
    check_int("bad base", neverc_strconv_parse_int("42", 1, &v), NEVERC_STRCONV_ERR_BASE);
    check_int("positive magnitude overflow",
              neverc_strconv_parse_int("18446744073709551616", 10, &v),
              NEVERC_STRCONV_ERR_RANGE);
    check_ll("positive magnitude overflow clamp", v, LLONG_MAX);
    check_int("negative magnitude overflow",
              neverc_strconv_parse_int("-18446744073709551616", 10, &v),
              NEVERC_STRCONV_ERR_RANGE);
    check_ll("negative magnitude overflow clamp", v, LLONG_MIN);
    check_int("explicit hex prefix signed is syntax",
              neverc_strconv_parse_int("0x10", 16, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("explicit hex digits still parse",
              neverc_strconv_parse_int("10", 16, &v), 0);
    check_ll("explicit hex digits val", v, 16);
    check_int("overflow trailing junk signed is range",
              neverc_strconv_parse_int("18446744073709551616x", 10, &v),
              NEVERC_STRCONV_ERR_RANGE);
    check_ll("overflow trailing junk signed clamp", v, LLONG_MAX);
    check_int("min int64 exact",
              neverc_strconv_parse_int("-9223372036854775808", 10, &v), 0);
    check_ll("min int64 exact val", v, LLONG_MIN);
    check_int("min int64 minus one",
              neverc_strconv_parse_int("-9223372036854775809", 10, &v),
              NEVERC_STRCONV_ERR_RANGE);
    check_ll("min int64 minus one clamp", v, LLONG_MIN);
    check_int("min int64 hex",
              neverc_strconv_parse_int("-0x8000000000000000", 0, &v), 0);
    check_ll("min int64 hex val", v, LLONG_MIN);
    check_int("sign after prefix is syntax",
              neverc_strconv_parse_int("0x+f", 0, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
}

/* ===== ParseUint ===== */
static void test_parse_uint(void) {
    printf("[parse_uint]\n");
    unsigned long long v;
    check_int("uint10",  neverc_strconv_parse_uint("1000", 10, &v), 0); check_ull("val", v, 1000);
    check_int("uint16",  neverc_strconv_parse_uint("DEAD", 16, &v), 0); check_ull("val", v, 0xDEAD);
    check_int("uint36",  neverc_strconv_parse_uint("zz", 36, &v), 0);  check_ull("val", v, 35*36+35);
    check_int("auto underscore", neverc_strconv_parse_uint("1_000", 0, &v), 0);
    check_ull("auto underscore val", v, 1000);
    check_int("prefix underscore", neverc_strconv_parse_uint("0x_ff", 0, &v), 0);
    check_ull("prefix underscore val", v, 255);
    check_int("explicit base underscore rejected",
              neverc_strconv_parse_uint("1_0", 10, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("leading underscore rejected",
              neverc_strconv_parse_uint("_1", 0, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("trailing underscore rejected",
              neverc_strconv_parse_uint("1_", 0, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("consecutive underscores rejected",
              neverc_strconv_parse_uint("1__2", 0, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("prefix-only underscore rejected",
              neverc_strconv_parse_uint("0x_", 0, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("invalid implicit octal rejected",
              neverc_strconv_parse_uint("08", 0, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("explicit hex prefix is syntax",
              neverc_strconv_parse_uint("0xff", 16, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("explicit hex digits still parse",
              neverc_strconv_parse_uint("ff", 16, &v), 0);
    check_ull("explicit hex digits val", v, 255);
    check_int("explicit bin prefix is syntax",
              neverc_strconv_parse_uint("0b11111111", 2, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("explicit oct prefix is syntax",
              neverc_strconv_parse_uint("0o377", 8, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("uint rejects leading plus",
              neverc_strconv_parse_uint("+1", 10, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("implicit octal underscores",
              neverc_strconv_parse_uint("0_1_2_3_4_5", 0, &v), 0);
    check_ull("implicit octal underscores val", v, 012345);
    check_int("non-ascii digit rejected",
              neverc_strconv_parse_uint("\x96" "B", 16, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("overflow trailing junk is range",
              neverc_strconv_parse_uint("18446744073709551616x", 10, &v),
              NEVERC_STRCONV_ERR_RANGE);
    check_ull("overflow trailing junk clamp", v, ULLONG_MAX);
    check_int("overflow trailing underscore is range",
              neverc_strconv_parse_uint("18446744073709551616_", 0, &v),
              NEVERC_STRCONV_ERR_RANGE);
    check_ull("overflow trailing underscore clamp", v, ULLONG_MAX);
    check_int("uint max trailing junk is syntax",
              neverc_strconv_parse_uint("18446744073709551615x", 10, &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("overflow still range without junk",
              neverc_strconv_parse_uint("18446744073709551616", 10, &v),
              NEVERC_STRCONV_ERR_RANGE);
    check_ull("overflow clamp", v, ULLONG_MAX);
    check_int("uint max exact",
              neverc_strconv_parse_uint("18446744073709551615", 10, &v), 0);
    check_ull("uint max exact val", v, ULLONG_MAX);
}

/* ===== ParseFloat ===== */
static void test_parse_float(void) {
    printf("[parse_float]\n");
    double v;
    check_int("float 3.14",   neverc_strconv_parse_float("3.14", &v), 0);
    check_double_approx("val", v, 3.14, 1e-15);

    check_int("float -2.5",   neverc_strconv_parse_float("-2.5", &v), 0);
    check_double_approx("val", v, -2.5, 1e-15);

    check_int("float 1e10",   neverc_strconv_parse_float("1e10", &v), 0);
    check_double_approx("val", v, 1e10, 1.0);

    check_int("float bad",    neverc_strconv_parse_float("abc", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float empty",  neverc_strconv_parse_float("", &v), NEVERC_STRCONV_ERR_SYNTAX);

    /* Regression: trailing chars after inf/nan were not detected */
    check_int("float inf",       neverc_strconv_parse_float("inf", &v), 0);
    check_int("float Inf",       neverc_strconv_parse_float("Inf", &v), 0);
    check_int("float -inf",      neverc_strconv_parse_float("-inf", &v), 0);
    check_int("float infinity",  neverc_strconv_parse_float("infinity", &v), 0);
    check_int("float Infinity",  neverc_strconv_parse_float("Infinity", &v), 0);
    check_int("float NaN",       neverc_strconv_parse_float("NaN", &v), 0);
    check_int("float nan",       neverc_strconv_parse_float("nan", &v), 0);
    check_true("float NaN is nan", v != v);
    check_int("float -NaN",      neverc_strconv_parse_float("-NaN", &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float +NaN",      neverc_strconv_parse_float("+NaN", &v),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float info=ERR",  neverc_strconv_parse_float("info", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float nan123=ERR",neverc_strconv_parse_float("nan123", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float infix=ERR", neverc_strconv_parse_float("infix", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float nanny=ERR", neverc_strconv_parse_float("nanny", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float +inf",     neverc_strconv_parse_float("+inf", &v), 0);
    check_int("float +Infinity",neverc_strconv_parse_float("+Infinity", &v), 0);

    /* Overflow reports RANGE (Go ErrRange). Underflow to ±0 is success. */
    check_int("float 1e308 finite",
              neverc_strconv_parse_float("1e308", &v), 0);
    check_true("float 1e308 not Inf", v > 1e307 && v + v != v);
    check_int("float 1e309 overflow",
              neverc_strconv_parse_float("1e309", &v), NEVERC_STRCONV_ERR_RANGE);
    check_true("float 1e309 is +Inf", v > 1e308 && v == v && v + v == v);
    check_int("float 2e308 overflow",
              neverc_strconv_parse_float("2e308", &v), NEVERC_STRCONV_ERR_RANGE);
    check_true("float 2e308 is +Inf", v > 1e308 && v == v && v + v == v);
    check_int("float +1e309 overflow",
              neverc_strconv_parse_float("+1e309", &v), NEVERC_STRCONV_ERR_RANGE);
    check_true("float +1e309 is +Inf", v > 1e308 && v + v == v);
    check_int("float 1.8e308 overflow",
              neverc_strconv_parse_float("1.8e308", &v), NEVERC_STRCONV_ERR_RANGE);
    check_true("float 1.8e308 is +Inf", v > 1e308 && v + v == v);
    check_int("float -1e309 overflow",
              neverc_strconv_parse_float("-1e309", &v), NEVERC_STRCONV_ERR_RANGE);
    check_true("float -1e309 is -Inf", v < -1e308 && v + v == v);
    check_int("float 1e-400 underflow",
              neverc_strconv_parse_float("1e-400", &v), 0);
    check_true("float 1e-400 is 0", v == 0.0);
    check_int("float -1e-400 underflow",
              neverc_strconv_parse_float("-1e-400", &v), 0);
    check_true("float -1e-400 is -0", v == 0.0 && signbit(v) != 0);
    check_int("float 2e-324 flush",
              neverc_strconv_parse_float("2e-324", &v), 0);
    check_true("float 2e-324 is 0", v == 0.0);
    check_int("float 0e999 is exact zero",
              neverc_strconv_parse_float("0e999", &v), 0);
    check_true("float 0e999 value", v == 0.0);
    check_int("float max finite ok",
              neverc_strconv_parse_float("1.7976931348623157e+308", &v), 0);
    check_true("float max finite value", v > 1e308);
    check_int("float min subnormal ok",
              neverc_strconv_parse_float("4.9406564584124654e-324", &v), 0);
    check_true("float min subnormal nonzero", v != 0.0);

    /* Go ParseFloat rejects surrounding whitespace (ParseInt/ParseBool too). */
    check_int("float leading space",
              neverc_strconv_parse_float(" 1.0", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float trailing space",
              neverc_strconv_parse_float("1.0 ", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float inf trailing space",
              neverc_strconv_parse_float("inf ", &v), NEVERC_STRCONV_ERR_SYNTAX);

    /* Go hex floats: 0x mantissa + required p/P power-of-two exponent. */
    check_int("hex 0x1p0", neverc_strconv_parse_float("0x1p0", &v), 0);
    check_true("hex 0x1p0 val", v == 1.0);
    check_int("hex 0x1.8p0", neverc_strconv_parse_float("0x1.8p0", &v), 0);
    check_true("hex 0x1.8p0 val", v == 1.5);
    check_int("hex 0x.8p0", neverc_strconv_parse_float("0x.8p0", &v), 0);
    check_true("hex 0x.8p0 val", v == 0.5);
    check_int("hex 0x1p4", neverc_strconv_parse_float("0x1p4", &v), 0);
    check_true("hex 0x1p4 val", v == 16.0);
    check_int("hex 0x10p0", neverc_strconv_parse_float("0x10p0", &v), 0);
    check_true("hex 0x10p0 val", v == 16.0);
    check_int("hex 0X1P0", neverc_strconv_parse_float("0X1P0", &v), 0);
    check_true("hex 0X1P0 val", v == 1.0);
    check_int("hex -0x1p0", neverc_strconv_parse_float("-0x1p0", &v), 0);
    check_true("hex -0x1p0 val", v == -1.0);
    check_int("hex missing exponent",
              neverc_strconv_parse_float("0x1", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("hex overflow",
              neverc_strconv_parse_float("0x1p1024", &v), NEVERC_STRCONV_ERR_RANGE);
    check_true("hex overflow is +Inf", v > 1e308 && v == v && v + v == v);
    check_int("hex zero huge exponent",
              neverc_strconv_parse_float("0x0p999", &v), 0);
    check_true("hex zero huge exponent val", v == 0.0);
    check_int("hex underflow",
              neverc_strconv_parse_float("0x1p-2000", &v), 0);
    check_true("hex underflow is zero", v == 0.0);
    check_int("hex signed underflow",
              neverc_strconv_parse_float("-0x1p-2000", &v), 0);
    check_true("hex signed underflow is -0", v == 0.0 && signbit(v) != 0);
    /* Subnormals are in range; flush-to-zero is a successful ±0. */
    check_int("hex min subnormal ok",
              neverc_strconv_parse_float("0x1p-1074", &v), 0);
    check_true("hex min subnormal nonzero", v != 0.0);
    check_true("hex min subnormal is min", v / 2.0 == 0.0);
    check_int("hex just-under min is OK",
              neverc_strconv_parse_float("0x1p-1075", &v), 0);
    check_true("hex just-under min is 0", v == 0.0);

    check_int("float underscore", neverc_strconv_parse_float("1_000", &v), 0);
    check_true("float underscore val", v == 1000.0);
    check_int("float frac underscore",
              neverc_strconv_parse_float("1.2_3e4", &v), 0);
    check_true("float frac underscore val", v == 12300.0);
    check_int("hex underscore",
              neverc_strconv_parse_float("-0x1_ep-1", &v), 0);
    check_true("hex underscore val", v == -15.0);
    check_int("hex underscore after 0x",
              neverc_strconv_parse_float("0x_1p0", &v), 0);
    check_true("hex underscore after 0x val", v == 1.0);
    check_int("hex underscore after 0X",
              neverc_strconv_parse_float("0X_1P0", &v), 0);
    check_true("hex underscore after 0X val", v == 1.0);
    check_int("hex underscore after +0x",
              neverc_strconv_parse_float("+0x_1p0", &v), 0);
    check_true("hex underscore after +0x val", v == 1.0);
    check_int("hex underscore before p still syntax",
              neverc_strconv_parse_float("0x1_p0", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float double underscore",
              neverc_strconv_parse_float("1__0", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float leading underscore",
              neverc_strconv_parse_float("_1.0", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float trailing underscore",
              neverc_strconv_parse_float("1.0_", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float underscore after dot",
              neverc_strconv_parse_float("1._0", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float underscore after e",
              neverc_strconv_parse_float("1e_2", &v), NEVERC_STRCONV_ERR_SYNTAX);

    /* Correctly-rounded hard cases (same bits as IEEE-754 nearest-even). */
    {
        double libc, ours;
        uint64_t b1, b2;
        char *end = NULL;
        const char *cases[] = {
            "1e23",
            "9007199254740993",
            "7.2057594037927933e+16",
            "1.0000000000000002",
            "0.1",
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            ours = 0;
            check_int("rounding parse ok",
                      neverc_strconv_parse_float(cases[i], &ours), 0);
            libc = strtod(cases[i], &end);
            memcpy(&b1, &ours, 8);
            memcpy(&b2, &libc, 8);
            check_true("rounding bits match libc", b1 == b2);
        }
    }
}

/* ===== Itoa ===== */
static void test_itoa(void) {
    printf("[itoa]\n");
    char buf[64];
    check_int("itoa 42",   neverc_strconv_itoa(42, buf, sizeof(buf)), 2);    check_str("val", buf, "42");
    check_int("itoa -42",  neverc_strconv_itoa(-42, buf, sizeof(buf)), 3);   check_str("val", buf, "-42");
    check_int("itoa 0",    neverc_strconv_itoa(0, buf, sizeof(buf)), 1);     check_str("val", buf, "0");
    check_int("ltoa 42",   neverc_strconv_ltoa(42LL, buf, sizeof(buf)), 2);  check_str("ltoa val", buf, "42");
    check_int("ltoa -7",   neverc_strconv_ltoa(-7LL, buf, sizeof(buf)), 2);  check_str("ltoa neg", buf, "-7");
}

/* ===== FormatInt ===== */
static void test_format_int(void) {
    printf("[format_int]\n");
    char buf[128];
    neverc_strconv_format_int(255, 16, buf, sizeof(buf));
    check_str("hex 255", buf, "ff");

    neverc_strconv_format_int(255, 2, buf, sizeof(buf));
    check_str("bin 255", buf, "11111111");

    neverc_strconv_format_int(-42, 10, buf, sizeof(buf));
    check_str("dec -42", buf, "-42");

    neverc_strconv_format_int(255, 8, buf, sizeof(buf));
    check_str("oct 255", buf, "377");
}

/* ===== FormatUint ===== */
static void test_format_uint(void) {
    printf("[format_uint]\n");
    char buf[128];
    neverc_strconv_format_uint(0xDEADBEEF, 16, buf, sizeof(buf));
    check_str("hex deadbeef", buf, "deadbeef");

    neverc_strconv_format_uint(0, 10, buf, sizeof(buf));
    check_str("zero", buf, "0");
}

/* ===== FormatFloat ===== */
static void test_format_float(void) {
    printf("[format_float]\n");
    char buf[128];

    neverc_strconv_format_float(3.14159, 'f', 2, buf, sizeof(buf));
    check_str("f2 pi", buf, "3.14");

    neverc_strconv_format_float(1000.0, 'e', 2, buf, sizeof(buf));
    check_str("e2 1000", buf, "1.00e+03");

    neverc_strconv_format_float(0.0, 'g', -1, buf, sizeof(buf));
    check_str("g 0", buf, "0");

    /* Regression: large values that exceed uint64_t range */
    neverc_strconv_format_float(1e20, 'g', -1, buf, sizeof(buf));
    check_true("g 1e20 not empty", strlen(buf) > 0);
    check_true("g 1e20 starts with 1", buf[0] == '1');

    neverc_strconv_format_float(1e20, 'e', 2, buf, sizeof(buf));
    check_str("e 1e20", buf, "1.00e+20");

    neverc_strconv_format_float(-1e20, 'e', 2, buf, sizeof(buf));
    check_str("e -1e20", buf, "-1.00e+20");

    neverc_strconv_format_float(1.23456789e15, 'f', 0, buf, sizeof(buf));
    check_true("f 1.23e15 starts with 1", buf[0] == '1');
    check_true("f 1.23e15 length >= 16", strlen(buf) >= 16);

    check_int("huge fixed precision rejected",
              neverc_strconv_format_float(
                  1.0, 'f', INT_MAX, buf, sizeof(buf)), -1);
    check_int("huge exponent precision rejected",
              neverc_strconv_format_float(
                  1.0, 'e', INT_MAX, buf, sizeof(buf)), -1);

    neverc_strconv_format_float(0.1, 'g', -1, buf, sizeof(buf));
    check_str("g shortest 0.1", buf, "0.1");

    neverc_strconv_format_float(1.0, 'b', -1, buf, sizeof(buf));
    check_str("b 1.0", buf, "4503599627370496p-52");
    neverc_strconv_format_float(0.0, 'b', -1, buf, sizeof(buf));
    check_str("b 0.0", buf, "0p-1074");
    neverc_strconv_format_float(1.0, 'x', -1, buf, sizeof(buf));
    check_str("x 1.0", buf, "0x1p+00");
    neverc_strconv_format_float(1.0, 'X', -1, buf, sizeof(buf));
    check_str("X 1.0", buf, "0X1P+00");
    neverc_strconv_format_float(-1.5, 'x', -1, buf, sizeof(buf));
    check_str("x -1.5", buf, "-0x1.8p+00");
    check_int("unknown fmt rejected",
              neverc_strconv_format_float(1.0, 'q', -1, buf, sizeof(buf)), -1);

    neverc_strconv_format_float(2.5, 'f', 0, buf, sizeof(buf));
    check_str("f round even 2.5", buf, "2");
    neverc_strconv_format_float(3.5, 'f', 0, buf, sizeof(buf));
    check_str("f round even 3.5", buf, "4");

    neverc_strconv_format_float(-0.0, 'f', 1, buf, sizeof(buf));
    check_str("f signed zero", buf, "-0.0");
}

/* ===== FormatBool ===== */
static void test_format_bool(void) {
    printf("[format_bool]\n");
    char buf[16];
    neverc_strconv_format_bool(1, buf, sizeof(buf));
    check_str("true", buf, "true");

    neverc_strconv_format_bool(0, buf, sizeof(buf));
    check_str("false", buf, "false");
}

/* ===== Quote ===== */
static void test_quote(void) {
    printf("[quote]\n");
    char *q;

    q = neverc_strconv_quote("hello");
    check_str("quote hello", q, "\"hello\"");
    free(q);

    q = neverc_strconv_quote("hello\nworld");
    check_str("quote newline", q, "\"hello\\nworld\"");
    free(q);

    q = neverc_strconv_quote("tab\there");
    check_str("quote tab", q, "\"tab\\there\"");
    free(q);

    q = neverc_strconv_quote("say \"hi\"");
    check_str("quote dquote", q, "\"say \\\"hi\\\"\"");
    free(q);

    q = neverc_strconv_quote("back\\slash");
    check_str("quote backslash", q, "\"back\\\\slash\"");
    free(q);

    q = neverc_strconv_quote("");
    check_str("quote empty", q, "\"\"");
    free(q);

    q = neverc_strconv_quote("\x01\x02\x03");
    check_str("quote control", q, "\"\\x01\\x02\\x03\"");
    free(q);

    q = neverc_strconv_quote("\xff");
    check_str("quote invalid UTF-8 byte", q, "\"\\xff\"");
    free(q);

    q = neverc_strconv_quote("\xc0\x80");
    check_str("quote overlong UTF-8", q, "\"\\xc0\\x80\"");
    free(q);

    q = neverc_strconv_quote("a\xfe" "b");
    check_str("quote invalid UTF-8 mid-string", q, "\"a\\xfe" "b\"");
    free(q);

    q = neverc_strconv_quote("\xe4\xb8");
    check_str("quote incomplete UTF-8", q, "\"\\xe4\\xb8\"");
    free(q);

    q = neverc_strconv_quote("\xed\xa0\x80");
    check_str("quote UTF-8 surrogate bytes", q, "\"\\xed\\xa0\\x80\"");
    free(q);

    q = neverc_strconv_quote_to_ascii("\xff");
    check_str("quote_to_ascii invalid UTF-8", q, "\"\\xff\"");
    free(q);

    q = neverc_strconv_quote(NULL);
    check_true("quote null rejected", q == NULL);
    free(q);

    q = neverc_strconv_quote_rune(0xD800);
    check_str("quote invalid rune surrogate", q, "'\xef\xbf\xbd'");
    free(q);

    q = neverc_strconv_quote_to_graphic("hello world");
    check_str("quote graphic", q, "\"hello world\"");
    free(q);
}

/* ===== QuoteRune ===== */
static void test_quote_rune(void) {
    printf("[quote_rune]\n");
    char *q;

    q = neverc_strconv_quote_rune('A');
    check_str("rune A", q, "'A'");
    free(q);

    q = neverc_strconv_quote_rune('\n');
    check_str("rune newline", q, "'\\n'");
    free(q);

    q = neverc_strconv_quote_rune('\'');
    check_str("rune squote", q, "'\\''");
    free(q);

    q = neverc_strconv_quote_rune(0x4e16);
    check_str("rune unicode", q, "'\xe4\xb8\x96'");
    free(q);

    q = neverc_strconv_quote_rune_to_ascii(0x4e16);
    check_str("rune unicode ascii", q, "'\\u4e16'");
    free(q);

    q = neverc_strconv_quote_rune_to_graphic(0x4e16);
    check_str("rune unicode graphic", q, "'\xe4\xb8\x96'");
    free(q);
}

/* ===== QuoteToASCII ===== */
static void test_quote_to_ascii(void) {
    printf("[quote_to_ascii]\n");
    char *q;

    q = neverc_strconv_quote_to_ascii("hello");
    check_str("ascii hello", q, "\"hello\"");
    free(q);

    q = neverc_strconv_quote_to_ascii("\xe4\xb8\x96");
    check_str("ascii unicode", q, "\"\\u4e16\"");
    free(q);
}

/* ===== Unquote ===== */
static void test_unquote(void) {
    printf("[unquote]\n");
    char buf[256];
    int n;
    uint32_t rune = 0;
    int multibyte = 0;

    n = neverc_strconv_unquote_char("A", 1, '"', &rune, &multibyte);
    check_int("unquote_char A len", n, 1);
    check_int("unquote_char A rune", (int)rune, 'A');
    check_int("unquote_char A multibyte", multibyte, 0);

    n = neverc_strconv_unquote_char("\\n", 2, '"', &rune, &multibyte);
    check_int("unquote_char nl len", n, 2);
    check_int("unquote_char nl rune", (int)rune, '\n');

    n = neverc_strconv_unquote("\"hello\"", buf, sizeof(buf));
    check_int("unquote hello len", n, 5);
    check_str("unquote hello val", buf, "hello");

    n = neverc_strconv_unquote("\"hello\\nworld\"", buf, sizeof(buf));
    check_int("unquote newline len", n, 11);
    check_str("unquote newline val", buf, "hello\nworld");

    n = neverc_strconv_unquote("\"tab\\there\"", buf, sizeof(buf));
    check_int("unquote tab len", n, 8);
    check_str("unquote tab val", buf, "tab\there");

    n = neverc_strconv_unquote("\"say \\\"hi\\\"\"", buf, sizeof(buf));
    check_int("unquote dquote len", n, 8);
    check_str("unquote dquote val", buf, "say \"hi\"");

    n = neverc_strconv_unquote("`raw string`", buf, sizeof(buf));
    check_int("unquote backtick len", n, 10);
    check_str("unquote backtick val", buf, "raw string");

    n = neverc_strconv_unquote("\"\\x41\"", buf, sizeof(buf));
    check_int("unquote hex len", n, 1);
    check_str("unquote hex val", buf, "A");

    n = neverc_strconv_unquote("\"\\007\"", buf, sizeof(buf));
    check_int("unquote octal len", n, 1);
    check_int("unquote octal value", (unsigned char)buf[0], 7);
    check_int("reject short octal",
              neverc_strconv_unquote("\"\\7\"", buf, sizeof(buf)), -1);

    n = neverc_strconv_unquote("\"\\xff\"", buf, sizeof(buf));
    check_int("unquote byte escape len", n, 1);
    check_int("unquote byte escape value", (unsigned char)buf[0], 0xff);
    check_int("unquote byte escape terminator", buf[1], '\0');

    n = neverc_strconv_unquote("\"\\u4e16\"", buf, sizeof(buf));
    check_int("unquote unicode len", n, 3);

    n = neverc_strconv_unquote("'a'", buf, sizeof(buf));
    check_int("unquote rune len", n, 1);
    check_str("unquote rune value", buf, "a");
    n = neverc_strconv_unquote("'\\xff'", buf, sizeof(buf));
    check_int("unquote escaped rune len", n, 1);
    check_int("unquote escaped rune value", (unsigned char)buf[0], 0xff);
    n = neverc_strconv_unquote("'\\377'", buf, sizeof(buf));
    check_int("unquote octal rune len", n, 1);
    check_int("unquote octal rune value", (unsigned char)buf[0], 0xff);
    n = neverc_strconv_unquote("'\\u00ff'", buf, sizeof(buf));
    check_int("unquote unicode rune len", n, 2);
    check_str("unquote unicode rune value", buf, "\xc3\xbf");
    n = neverc_strconv_unquote("''", buf, sizeof(buf));
    check_int("empty rune is empty string", n, 0);
    check_str("empty rune value", buf, "");
    check_int("reject multiple runes",
              neverc_strconv_unquote("'ab'", buf, sizeof(buf)), -1);

    char invalid_utf8[] = {'"', (char)0xc0, '"', '\0'};
    n = neverc_strconv_unquote(invalid_utf8, buf, sizeof(buf));
    check_int("invalid UTF-8 becomes FFFD len", n, 3);
    check_str("invalid UTF-8 becomes FFFD", buf, "\xef\xbf\xbd");

    n = neverc_strconv_unquote("`a\rb`", buf, sizeof(buf));
    check_int("raw string discards carriage return len", n, 2);
    check_str("raw string discards carriage return", buf, "ab");
    check_int("reject interior raw delimiter",
              neverc_strconv_unquote("`a`b`", buf, sizeof(buf)), -1);

    n = neverc_strconv_unquote("bad", buf, sizeof(buf));
    check_int("unquote bad", n, -1);

    n = neverc_strconv_unquote("\"", buf, sizeof(buf));
    check_int("unquote single quote", n, -1);

    /* Go interpreted string/rune literals cannot contain a raw newline. */
    char raw_nl_string[] = {'"', 'h', 'i', '\n', 'x', '"', '\0'};
    check_int("reject raw newline in interpreted string",
              neverc_strconv_unquote(raw_nl_string, buf, sizeof(buf)), -1);
    char raw_nl_rune[] = {'\'', '\n', '\'', '\0'};
    check_int("reject raw newline in rune literal",
              neverc_strconv_unquote(raw_nl_rune, buf, sizeof(buf)), -1);
    char raw_nl_backtick[] = {'`', 'a', '\n', 'b', '`', '\0'};
    n = neverc_strconv_unquote(raw_nl_backtick, buf, sizeof(buf));
    check_int("raw string keeps newline len", n, 3);
    check_str("raw string keeps newline", buf, "a\nb");

    n = neverc_strconv_unquote("\"\"", buf, sizeof(buf));
    check_int("unquote empty len", n, 0);
    check_str("unquote empty val", buf, "");
    check_int("reject surrogate escape",
              neverc_strconv_unquote("\"\\uD800\"", buf, sizeof(buf)), -1);
    check_int("reject out-of-range U escape",
              neverc_strconv_unquote("\"\\U00110000\"", buf, sizeof(buf)), -1);
}

/* ===== CanBackquote ===== */
static void test_can_backquote(void) {
    printf("[can_backquote]\n");
    check_int("normal", neverc_strconv_can_backquote("hello world"), 1);
    check_int("tab ok", neverc_strconv_can_backquote("hello\tworld"), 1);
    check_int("newline bad", neverc_strconv_can_backquote("hello\nworld"), 0);
    check_int("backtick bad", neverc_strconv_can_backquote("back`tick"), 0);
    check_int("control bad", neverc_strconv_can_backquote("\x01"), 0);
    check_int("invalid UTF-8 rejected", neverc_strconv_can_backquote("\xff"), 0);
    check_int("overlong UTF-8 rejected", neverc_strconv_can_backquote("\xc0\x80"), 0);
    check_int("incomplete UTF-8 rejected", neverc_strconv_can_backquote("\xe4\xb8"), 0);
    check_int("U+FFFD encoding allowed", neverc_strconv_can_backquote("\xef\xbf\xbd"), 1);
    check_int("empty ok", neverc_strconv_can_backquote(""), 1);
    check_int("null rejected", neverc_strconv_can_backquote(NULL), 0);
}

/* ===== IsPrint / IsGraphic ===== */
static void test_is_print_graphic(void) {
    printf("[is_print_graphic]\n");
    check_int("print A", neverc_strconv_is_print('A'), 1);
    check_int("print space", neverc_strconv_is_print(' '), 1);
    check_int("print ctrl", neverc_strconv_is_print('\n'), 0);
    check_int("print del", neverc_strconv_is_print(0x7F), 0);
    check_int("graphic A", neverc_strconv_is_graphic('A'), 1);
    check_int("graphic ctrl", neverc_strconv_is_graphic('\n'), 0);
}

/* ===== Append variants ===== */
static void test_append_variants(void) {
    printf("[append_variants]\n");
    char buf[256];

    check_int("append_bool true", neverc_strconv_append_bool(buf, sizeof(buf), 1), 4);
    check_str("val", buf, "true");

    check_int("append_bool false", neverc_strconv_append_bool(buf, sizeof(buf), 0), 5);
    check_str("val", buf, "false");

    check_int("append_int 42", neverc_strconv_append_int(buf, sizeof(buf), 42, 10), 2);
    check_str("val", buf, "42");

    check_int("append_int -99", neverc_strconv_append_int(buf, sizeof(buf), -99, 10), 3);
    check_str("val", buf, "-99");

    check_int("append_uint ff", neverc_strconv_append_uint(buf, sizeof(buf), 255, 16), 2);
    check_str("val", buf, "ff");

    check_int("append_float pi", neverc_strconv_append_float(buf, sizeof(buf), 3.14, 'f', 2), 4);
    check_str("val", buf, "3.14");

    check_int("append_quote hello",
              neverc_strconv_append_quote(buf, sizeof(buf), "hello"), 7);
    check_str("val", buf, "\"hello\"");

    check_int("append_quote_to_ascii",
              neverc_strconv_append_quote_to_ascii(buf, sizeof(buf), "hi"), 4);
    check_str("val", buf, "\"hi\"");

    check_int("append_quote_to_graphic",
              neverc_strconv_append_quote_to_graphic(buf, sizeof(buf), "hi"), 4);
    check_str("val", buf, "\"hi\"");

    check_int("append_quote_rune A",
              neverc_strconv_append_quote_rune(buf, sizeof(buf), 'A'), 3);
    check_str("val", buf, "'A'");

    check_int("append_quote_rune_to_ascii A",
              neverc_strconv_append_quote_rune_to_ascii(buf, sizeof(buf), 'A'), 3);
    check_str("val", buf, "'A'");

    check_int("append_quote_rune_to_graphic A",
              neverc_strconv_append_quote_rune_to_graphic(buf, sizeof(buf), 'A'), 3);
    check_str("val", buf, "'A'");

    check_int("append quote null target",
              neverc_strconv_append_quote(NULL, sizeof(buf), "x"), -1);
}

/* ===== QuotedPrefix ===== */
static void test_quoted_prefix(void) {
    printf("[quoted_prefix]\n");
    size_t plen;

    check_int("dquote prefix", neverc_strconv_quoted_prefix("\"hello\" world", &plen), 0);
    check_int("dquote plen", (int)plen, 7);

    check_int("squote prefix", neverc_strconv_quoted_prefix("'A' rest", &plen), 0);
    check_int("squote plen", (int)plen, 3);
    check_int("squote accepts empty rune",
              neverc_strconv_quoted_prefix("'' rest", &plen), 0);
    check_int("squote empty prefix length", (int)plen, 2);
    check_int("squote rejects multiple runes",
              neverc_strconv_quoted_prefix("'AB'", &plen), -1);
    check_int("squote accepts UTF-8 rune",
              neverc_strconv_quoted_prefix(
                  "'\xC3\xA9' rest", &plen), 0);
    check_int("squote UTF-8 prefix length", (int)plen, 4);
    check_int("squote accepts escaped rune",
              neverc_strconv_quoted_prefix("'\\n' rest", &plen), 0);
    check_int("squote rejects escaped plus literal rune",
              neverc_strconv_quoted_prefix("'\\nA'", &plen), -1);

    check_int("backtick prefix", neverc_strconv_quoted_prefix("`raw`+more", &plen), 0);
    check_int("backtick plen", (int)plen, 5);

    check_int("bad prefix", neverc_strconv_quoted_prefix("not quoted", &plen), -1);
    check_int("unclosed", neverc_strconv_quoted_prefix("\"unclosed", &plen), -1);

    char raw_nl_prefix[] = {'"', 'h', 'i', '\n', 'x', '"', '\0'};
    check_int("quoted_prefix rejects raw newline",
              neverc_strconv_quoted_prefix(raw_nl_prefix, &plen), -1);
    check_int("quoted_prefix accepts escaped newline",
              neverc_strconv_quoted_prefix("\"hello\\nworld\" rest", &plen), 0);
    check_int("quoted_prefix escaped newline length", (int)plen, 14);
}

/* ===== FormatComplex / ParseComplex ===== */
static void test_complex(void) {
    printf("[complex]\n");
    char buf[128];

    int n = neverc_strconv_format_complex(1.0, 2.0, 'f', 1, buf, sizeof(buf));
    check_int("format_complex len", n, 10);
    check_str("format_complex value", buf, "(1.0+2.0i)");

    char short_buf[10];
    check_int("format_complex exact short buffer",
              neverc_strconv_format_complex(
                  1.0, 2.0, 'f', 1, short_buf, sizeof(short_buf)), -1);
    char exact_buf[11];
    check_int("format_complex exact buffer",
              neverc_strconv_format_complex(
                  1.0, 2.0, 'f', 1, exact_buf, sizeof(exact_buf)), 10);
    check_str("format_complex exact value", exact_buf, "(1.0+2.0i)");

    double inf = 1.0 / 0.0;
    check_int("format_complex infinity",
              neverc_strconv_format_complex(
                  1.0, inf, 'f', 1, buf, sizeof(buf)), 10);
    check_str("format_complex infinity value", buf, "(1.0+Infi)");
    check_int("format_complex negative zero",
              neverc_strconv_format_complex(
                  1.0, -0.0, 'f', 1, buf, sizeof(buf)), 10);
    check_str("format_complex negative zero value", buf, "(1.0-0.0i)");

    double re, im;
    check_int("parse_complex basic",
              neverc_strconv_parse_complex("(1.5+2.5i)", &re, &im), 0);
    check_double_approx("re", re, 1.5, 1e-10);
    check_double_approx("im", im, 2.5, 1e-10);

    check_int("parse_complex negative im",
              neverc_strconv_parse_complex("(3.0-1.0i)", &re, &im), 0);
    check_double_approx("re", re, 3.0, 1e-10);
    check_double_approx("im", im, -1.0, 1e-10);

    check_int("parse_complex bad", neverc_strconv_parse_complex("abc", &re, &im),
              NEVERC_STRCONV_ERR_SYNTAX);

    check_int("parse_complex pure imag 1i",
              neverc_strconv_parse_complex("1i", &re, &im), 0);
    check_double_approx("1i re", re, 0.0, 1e-15);
    check_double_approx("1i im", im, 1.0, 1e-15);
    check_int("parse_complex bare i",
              neverc_strconv_parse_complex("i", &re, &im),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("parse_complex +i",
              neverc_strconv_parse_complex("+i", &re, &im),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("parse_complex -i",
              neverc_strconv_parse_complex("-i", &re, &im),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("parse_complex 1+i",
              neverc_strconv_parse_complex("1+i", &re, &im),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("parse_complex pure real",
              neverc_strconv_parse_complex("1.5", &re, &im), 0);
    check_double_approx("pure real re", re, 1.5, 1e-15);
    check_double_approx("pure real im", im, 0.0, 1e-15);
    check_int("parse_complex exp imag",
              neverc_strconv_parse_complex("1e+10i", &re, &im), 0);
    check_double_approx("1e+10i re", re, 0.0, 1e-15);
    check_double_approx("1e+10i im", im, 1e10, 1.0);

    /* Hex-float p+/p- is an exponent, not a real/imag split. */
    check_int("parse_complex hex real p+",
              neverc_strconv_parse_complex("0x1p+1", &re, &im), 0);
    check_double_approx("hex real p+ re", re, 2.0, 1e-15);
    check_double_approx("hex real p+ im", im, 0.0, 1e-15);
    check_int("parse_complex hex real p-",
              neverc_strconv_parse_complex("0x1p-1", &re, &im), 0);
    check_double_approx("hex real p- re", re, 0.5, 1e-15);
    check_double_approx("hex real p- im", im, 0.0, 1e-15);
    check_int("parse_complex hex imag p+",
              neverc_strconv_parse_complex("0x1p+1i", &re, &im), 0);
    check_double_approx("hex imag p+ re", re, 0.0, 1e-15);
    check_double_approx("hex imag p+ im", im, 2.0, 1e-15);
    check_int("parse_complex hex both",
              neverc_strconv_parse_complex("0x1p+1+0x1p-1i", &re, &im), 0);
    check_double_approx("hex both re", re, 2.0, 1e-15);
    check_double_approx("hex both im", im, 0.5, 1e-15);
    check_int("parse_complex +NaNi",
              neverc_strconv_parse_complex("1+NaNi", &re, &im), 0);
    check_true("parse_complex +NaNi imag", im != im);
    check_int("parse_complex -NaNi",
              neverc_strconv_parse_complex("1-NaNi", &re, &im),
              NEVERC_STRCONV_ERR_SYNTAX);
    check_int("parse_complex NaNi",
              neverc_strconv_parse_complex("NaNi", &re, &im), 0);
    check_double_approx("NaNi re", re, 0.0, 1e-15);
    check_true("parse_complex NaNi imag", im != im);
    check_int("parse_complex +NaNi",
              neverc_strconv_parse_complex("+NaNi", &re, &im),
              NEVERC_STRCONV_ERR_SYNTAX);

    /* 'f' of 1e100 is ~104 chars; the old 64-byte scratch buffers failed. */
    char large[512];
    n = neverc_strconv_format_complex(1e100, 2e100, 'f', 1, large, sizeof(large));
    check_true("format_complex 1e100 fits", n > 0);
    check_true("format_complex 1e100 paren", large[0] == '(');
    check_true("format_complex 1e100 starts 1", large[1] == '1');
    check_true("format_complex 1e100 has +2", strstr(large, "+2") != NULL);

    /* 'f' of 1e308 with prec 200 is ~510 chars per part; a 512-byte
     * scratch still failed after the 64-byte bump. */
    char huge[2048];
    n = neverc_strconv_format_complex(1e308, -1e308, 'f', 200, huge, sizeof(huge));
    check_true("format_complex 1e308 prec200 fits", n > 500);
    check_true("format_complex 1e308 prec200 paren", huge[0] == '(');
    check_true("format_complex 1e308 prec200 minus", strchr(huge, '-') != NULL);
    check_true("format_complex 1e308 prec200 imag",
               n > 2 && huge[n - 2] == 'i' && huge[n - 1] == ')');
}

int main(void) {
    printf("=== NeverC Strconv Library Tests ===\n\n");

    test_parse_bool();
    test_atoi();
    test_parse_int();
    test_parse_uint();
    test_parse_float();
    test_itoa();
    test_format_int();
    test_format_uint();
    test_format_float();
    test_format_bool();
    test_quote();
    test_quote_rune();
    test_quote_to_ascii();
    test_unquote();
    test_can_backquote();
    test_is_print_graphic();
    test_append_variants();
    test_quoted_prefix();
    test_complex();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0) puts("passed");

    return tests_failed > 0 ? 1 : 0;
}
