#include "neverc/std/strconv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

    n = neverc_strconv_unquote("\"\\u4e16\"", buf, sizeof(buf));
    check_int("unquote unicode len", n, 3);

    n = neverc_strconv_unquote("bad", buf, sizeof(buf));
    check_int("unquote bad", n, -1);

    n = neverc_strconv_unquote("\"", buf, sizeof(buf));
    check_int("unquote single quote", n, -1);
}

/* ===== CanBackquote ===== */
static void test_can_backquote(void) {
    printf("[can_backquote]\n");
    check_int("normal", neverc_strconv_can_backquote("hello world"), 1);
    check_int("tab ok", neverc_strconv_can_backquote("hello\tworld"), 1);
    check_int("newline bad", neverc_strconv_can_backquote("hello\nworld"), 0);
    check_int("backtick bad", neverc_strconv_can_backquote("back`tick"), 0);
    check_int("control bad", neverc_strconv_can_backquote("\x01"), 0);
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

    check_int("backtick prefix", neverc_strconv_quoted_prefix("`raw`+more", &plen), 0);
    check_int("backtick plen", (int)plen, 5);

    check_int("bad prefix", neverc_strconv_quoted_prefix("not quoted", &plen), -1);
    check_int("unclosed", neverc_strconv_quoted_prefix("\"unclosed", &plen), -1);
}

/* ===== FormatComplex / ParseComplex ===== */
static void test_complex(void) {
    printf("[complex]\n");
    char buf[128];

    int n = neverc_strconv_format_complex(1.0, 2.0, 'f', 1, buf, sizeof(buf));
    check_true("format_complex len", n > 0);
    check_true("format_complex has paren", buf[0] == '(');
    check_true("format_complex has i", buf[n-2] == 'i');

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

    return tests_failed > 0 ? 1 : 0;
}
