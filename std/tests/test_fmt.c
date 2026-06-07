#include "neverc/std/fmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got \"%s\", expected \"%s\"\n",
               name, got ? got : "(null)", expected ? expected : "(null)");
    }
}

static void test_integers(void) {
    printf("[integers]\n");
    char *r;

    r = neverc_fmt_sprintf("%d", 42);
    check_str("int 42", r, "42"); free(r);

    r = neverc_fmt_sprintf("%d", -17);
    check_str("int -17", r, "-17"); free(r);

    r = neverc_fmt_sprintf("%d", 0);
    check_str("int 0", r, "0"); free(r);

    r = neverc_fmt_sprintf("%u", 4294967295u);
    check_str("uint max", r, "4294967295"); free(r);

    r = neverc_fmt_sprintf("%x", 255);
    check_str("hex ff", r, "ff"); free(r);

    r = neverc_fmt_sprintf("%X", 255);
    check_str("hex FF", r, "FF"); free(r);

    r = neverc_fmt_sprintf("%o", 8);
    check_str("octal 10", r, "10"); free(r);

    r = neverc_fmt_sprintf("%b", 10);
    check_str("binary 1010", r, "1010"); free(r);

    r = neverc_fmt_sprintf("%lld", (long long)1234567890123LL);
    check_str("long long", r, "1234567890123"); free(r);
}

static void test_strings(void) {
    printf("[strings]\n");
    char *r;

    r = neverc_fmt_sprintf("%s", "hello");
    check_str("simple string", r, "hello"); free(r);

    r = neverc_fmt_sprintf("%.3s", "hello");
    check_str("precision string", r, "hel"); free(r);

    r = neverc_fmt_sprintf("%10s", "hi");
    check_str("width right", r, "        hi"); free(r);

    r = neverc_fmt_sprintf("%-10s", "hi");
    check_str("width left", r, "hi        "); free(r);

    r = neverc_fmt_sprintf("%s", (const char *)0);
    check_str("null string", r, "(null)"); free(r);

    r = neverc_fmt_sprintf("%c", 'A');
    check_str("char", r, "A"); free(r);
}

static void test_floats(void) {
    printf("[floats]\n");
    char *r;

    r = neverc_fmt_sprintf("%f", 3.14);
    check_str("float 3.14", r, "3.140000"); free(r);

    r = neverc_fmt_sprintf("%.2f", 3.14159);
    check_str("prec 2", r, "3.14"); free(r);

    r = neverc_fmt_sprintf("%.0f", 3.7);
    check_str("prec 0", r, "4"); free(r);

    r = neverc_fmt_sprintf("%e", 12345.6789);
    check_str("sci notation", r, "1.234568e+04"); free(r);

    r = neverc_fmt_sprintf("%e", 0.001);
    check_str("sci small", r, "1.000000e-03"); free(r);

    r = neverc_fmt_sprintf("%f", 0.0);
    check_str("zero", r, "0.000000"); free(r);
}

static void test_width_padding(void) {
    printf("[width/padding]\n");
    char *r;

    r = neverc_fmt_sprintf("%10d", 42);
    check_str("right pad int", r, "        42"); free(r);

    r = neverc_fmt_sprintf("%-10d", 42);
    check_str("left pad int", r, "42        "); free(r);

    r = neverc_fmt_sprintf("%010d", 42);
    check_str("zero pad int", r, "0000000042"); free(r);

    r = neverc_fmt_sprintf("%+d", 42);
    check_str("plus sign", r, "+42"); free(r);

    r = neverc_fmt_sprintf("%+d", -42);
    check_str("plus neg", r, "-42"); free(r);

    r = neverc_fmt_sprintf("% d", 42);
    check_str("space sign", r, " 42"); free(r);

    r = neverc_fmt_sprintf("%*d", 8, 42);
    check_str("star width", r, "      42"); free(r);
}

static void test_mixed(void) {
    printf("[mixed]\n");
    char *r;

    r = neverc_fmt_sprintf("hello %s, you are %d years old", "Alice", 30);
    check_str("mixed 1", r, "hello Alice, you are 30 years old"); free(r);

    r = neverc_fmt_sprintf("%%");
    check_str("literal percent", r, "%"); free(r);

    r = neverc_fmt_sprintf("0x%08x", 0xDEAD);
    check_str("hex padded", r, "0x0000dead"); free(r);

    r = neverc_fmt_sprintf("%d + %d = %d", 1, 2, 3);
    check_str("sum", r, "1 + 2 = 3"); free(r);

    r = neverc_fmt_sprintf("");
    check_str("empty format", r, ""); free(r);
}

static void test_special_floats(void) {
    printf("[special floats]\n");
    char *r;
    double nan_val = 0.0 / 0.0;
    double inf_val = 1.0 / 0.0;

    r = neverc_fmt_sprintf("%f", nan_val);
    check_str("NaN", r, "NaN"); free(r);

    r = neverc_fmt_sprintf("%f", inf_val);
    check_str("+Inf", r, "+Inf"); free(r);

    r = neverc_fmt_sprintf("%f", -inf_val);
    check_str("-Inf", r, "-Inf"); free(r);
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got %d, expected %d\n", name, got, expected);
    }
}

static void test_sscanf(void) {
    printf("[sscanf]\n");
    int a, b;
    double f;
    char s[64];

    int n = neverc_fmt_sscanf("42", "%d", &a);
    check_int("sscanf int", n, 1);
    check_int("sscanf int val", a, 42);

    n = neverc_fmt_sscanf("  -17  3.14", "%d %f", &a, &f);
    check_int("sscanf int+float count", n, 2);
    check_int("sscanf int val2", a, -17);
    check_int("sscanf float approx", (int)(f * 100), 314);

    n = neverc_fmt_sscanf("hello world", "%s", s);
    check_int("sscanf string count", n, 1);
    check_str("sscanf string val", s, "hello");

    n = neverc_fmt_sscanf("10 20 30", "%d %d", &a, &b);
    check_int("sscanf two ints", n, 2);
    check_int("sscanf first", a, 10);
    check_int("sscanf second", b, 20);

    n = neverc_fmt_sscanf("0xff", "%x", (unsigned int *)&a);
    check_int("sscanf hex", n, 1);
    check_int("sscanf hex val", a, 255);

    n = neverc_fmt_sscanf("", "%d", &a);
    check_int("sscanf empty", n, 0);

    n = neverc_fmt_sscanf("abc", "%d", &a);
    check_int("sscanf no match", n, 0);
}

static void test_appendf(void) {
    printf("[appendf]\n");
    char buf[64] = "prefix:";
    int n = neverc_fmt_appendf(buf, sizeof(buf), "%d-%s", 42, "ok");
    check_str("appendf result", buf, "prefix:42-ok");
    check_int("appendf return", n > 0, 1);

    char small[10] = "hi";
    neverc_fmt_appendf(small, sizeof(small), "%d%d%d", 111, 222, 333);
    check_int("appendf truncate", (int)strlen(small) < 10, 1);
}

static void test_errorf(void) {
    printf("[errorf]\n");
    char *e = neverc_fmt_errorf("error: %s at line %d", "null ptr", 42);
    check_str("errorf msg", e, "error: null ptr at line 42");
    free(e);
}

static void test_sprint_family(void) {
    printf("[sprint family]\n");
    char *r;

    r = neverc_fmt_sprint("hello");
    check_str("sprint", r, "hello"); free(r);

    r = neverc_fmt_sprintln("hello");
    check_str("sprintln", r, "hello\n"); free(r);
}

int main(void) {
    printf("=== NeverC Fmt Module Tests ===\n\n");
    test_integers();
    test_strings();
    test_floats();
    test_width_padding();
    test_mixed();
    test_special_floats();
    test_sscanf();
    test_appendf();
    test_errorf();
    test_sprint_family();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
