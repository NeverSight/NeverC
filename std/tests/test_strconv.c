#include "neverc/std/strconv.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

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
    if (strcmp(got, expected) == 0) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
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
    check_int("bad base", neverc_strconv_parse_int("42", 1, &v), NEVERC_STRCONV_ERR_BASE);
}

/* ===== ParseUint ===== */
static void test_parse_uint(void) {
    printf("[parse_uint]\n");
    unsigned long long v;
    check_int("uint10",  neverc_strconv_parse_uint("1000", 10, &v), 0); check_ull("val", v, 1000);
    check_int("uint16",  neverc_strconv_parse_uint("DEAD", 16, &v), 0); check_ull("val", v, 0xDEAD);
    check_int("uint36",  neverc_strconv_parse_uint("zz", 36, &v), 0);  check_ull("val", v, 35*36+35);
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
    check_int("float info=ERR",  neverc_strconv_parse_float("info", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float nan123=ERR",neverc_strconv_parse_float("nan123", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float infix=ERR", neverc_strconv_parse_float("infix", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float nanny=ERR", neverc_strconv_parse_float("nanny", &v), NEVERC_STRCONV_ERR_SYNTAX);
    check_int("float +inf",     neverc_strconv_parse_float("+inf", &v), 0);
    check_int("float +Infinity",neverc_strconv_parse_float("+Infinity", &v), 0);
}

/* ===== Itoa ===== */
static void test_itoa(void) {
    printf("[itoa]\n");
    char buf[64];
    check_int("itoa 42",   neverc_strconv_itoa(42, buf, sizeof(buf)), 2);    check_str("val", buf, "42");
    check_int("itoa -42",  neverc_strconv_itoa(-42, buf, sizeof(buf)), 3);   check_str("val", buf, "-42");
    check_int("itoa 0",    neverc_strconv_itoa(0, buf, sizeof(buf)), 1);     check_str("val", buf, "0");
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

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
