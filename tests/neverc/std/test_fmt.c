#include "neverc/std/fmt.h"
#include <limits.h>
#include <math.h>
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

    r = neverc_fmt_sprintf("%#x", 255);
    check_str("alt hex", r, "0xff"); free(r);

    r = neverc_fmt_sprintf("%#X", 255);
    check_str("alt HEX", r, "0XFF"); free(r);

    r = neverc_fmt_sprintf("%#x", 0);
    check_str("alt hex zero", r, "0x0"); free(r);

    r = neverc_fmt_sprintf("%#08x", 255);
    check_str("alt hex padded", r, "0x0000ff"); free(r);

    r = neverc_fmt_sprintf("%#o", 8);
    check_str("alt octal", r, "010"); free(r);

    r = neverc_fmt_sprintf("%#o", 0);
    check_str("alt octal zero", r, "0"); free(r);

    r = neverc_fmt_sprintf("%#b", 10);
    check_str("alt binary", r, "0b1010"); free(r);

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

    r = neverc_fmt_sprintf("%*s", -5, "hi");
    check_str("negative star width", r, "hi   "); free(r);

    r = neverc_fmt_sprintf("%*d", -8, 42);
    check_str("negative star int", r, "42      "); free(r);

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

    r = neverc_fmt_sprintf("%+f", inf_val);
    check_str("%+f +Inf has one sign", r, "+Inf"); free(r);

    r = neverc_fmt_sprintf("%+f", nan_val);
    check_str("%+f NaN", r, "+NaN"); free(r);

    r = neverc_fmt_sprintf("%+f", 1.5);
    check_str("%+f finite", r, "+1.500000"); free(r);

    r = neverc_fmt_sprintf("%+e", 1.0);
    check_str("%+e finite", r, "+1.000000e+00"); free(r);

    r = neverc_fmt_sprintf("%+08f", inf_val);
    check_str("zero flag ignored for Inf", r, "    +Inf"); free(r);

    r = neverc_fmt_sprintf("% f", inf_val);
    check_str("space flag replaces positive Inf sign", r, " Inf"); free(r);

    r = neverc_fmt_sprintf("%08.2f", -1.5);
    check_str("negative float sign before zeros", r, "-0001.50"); free(r);

    r = neverc_fmt_sprintf("%+08.2f", 1.5);
    check_str("positive float sign before zeros", r, "+0001.50"); free(r);

    r = neverc_fmt_sprintf("%05d", -12);
    check_str("negative integer sign before zeros", r, "-0012"); free(r);
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got %d, expected %d\n", name, got, expected);
    }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: expected true\n", name); }
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

    n = neverc_fmt_sscanf("hello world", "%63s", s);
    check_int("sscanf string count", n, 1);
    check_str("sscanf string val", s, "hello");

    struct {
        char text[4];
        unsigned char canary;
    } bounded = {{0}, 0xa5};
    n = neverc_fmt_sscanf("abcdef", "%3s", bounded.text);
    check_int("sscanf bounded string count", n, 1);
    check_str("sscanf bounded string value", bounded.text, "abc");
    check_int("sscanf bounded string preserves canary",
              bounded.canary, 0xa5);
    bounded.text[0] = 'Q';
    n = neverc_fmt_sscanf("abcdef", "%s", bounded.text);
    check_int("sscanf rejects unbounded string", n, 0);
    check_int("sscanf unbounded string leaves output",
              bounded.text[0], 'Q');

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

    double special = 0.0;
    n = neverc_fmt_sscanf("Inf", "%f", &special);
    check_int("sscanf Inf", n, 1);
    check_true("sscanf Inf value", isinf(special) && special > 0.0);

    n = neverc_fmt_sscanf("-Infinity", "%f", &special);
    check_int("sscanf -Infinity", n, 1);
    check_true("sscanf -Infinity value", isinf(special) && special < 0.0);

    n = neverc_fmt_sscanf("NaN", "%f", &special);
    check_int("sscanf NaN", n, 1);
    check_true("sscanf NaN value", special != special);

    special = 7.0;
    n = neverc_fmt_sscanf("infix", "%f", &special);
    check_int("sscanf malformed Inf rejected", n, 0);
    check_true("sscanf malformed Inf leaves output", special == 7.0);

    a = 77;
    n = neverc_fmt_sscanf("2147483648", "%d", &a);
    check_int("sscanf int overflow rejected", n, 0);
    check_int("sscanf int overflow leaves output", a, 77);
    n = neverc_fmt_sscanf("-2147483649", "%d", &a);
    check_int("sscanf int underflow rejected", n, 0);
    check_int("sscanf int underflow leaves output", a, 77);

    long long ll = 0;
    n = neverc_fmt_sscanf("9223372036854775807", "%lld", &ll);
    check_int("sscanf int64 max", n, 1);
    check_true("sscanf int64 max value", ll == LLONG_MAX);
    ll = 77;
    n = neverc_fmt_sscanf("9223372036854775808", "%lld", &ll);
    check_int("sscanf int64 overflow rejected", n, 0);
    check_true("sscanf int64 overflow leaves output", ll == 77);
    n = neverc_fmt_sscanf("-9223372036854775809", "%lld", &ll);
    check_int("sscanf int64 underflow rejected", n, 0);
    check_true("sscanf int64 underflow leaves output", ll == 77);

    unsigned int u = 77;
    n = neverc_fmt_sscanf("4294967296", "%u", &u);
    check_int("sscanf uint overflow rejected", n, 0);
    check_true("sscanf uint overflow leaves output", u == 77);

    unsigned long long ull = 0;
    n = neverc_fmt_sscanf("18446744073709551615", "%llu", &ull);
    check_int("sscanf uint64 max", n, 1);
    check_true("sscanf uint64 max value", ull == ULLONG_MAX);
    ull = 77;
    n = neverc_fmt_sscanf("18446744073709551616", "%llu", &ull);
    check_int("sscanf uint64 overflow rejected", n, 0);
    check_true("sscanf uint64 overflow leaves output", ull == 77);
    n = neverc_fmt_sscanf("10000000000000000", "%llx", &ull);
    check_int("sscanf hex64 overflow rejected", n, 0);
    check_true("sscanf hex64 overflow leaves output", ull == 77);

    struct {
        long value;
        unsigned int canary;
    } long_target = {0, 0xa5a5a5a5U};
    n = neverc_fmt_sscanf("-123", "%ld", &long_target.value);
    check_int("sscanf long count", n, 1);
    check_true("sscanf long value", long_target.value == -123L);
    check_true("sscanf long preserves adjacent storage",
               long_target.canary == 0xa5a5a5a5U);

    int one = 0;
    check_int("sscan one value",
              neverc_fmt_sscan("9 10", &one), 1);
    check_int("sscan one result", one, 9);
    int ignored = 77;
    one = 0;
    check_int("sscan legacy trailing args are safe",
              neverc_fmt_sscan("9 10", &one, &ignored), 1);
    check_int("sscan legacy first result", one, 9);
    check_int("sscan legacy trailing output ignored", ignored, 77);
    int values[2] = {0, 0};
    check_int("sscan counted values",
              neverc_fmt_sscan_ints("11 12 13", values, 2), 2);
    check_true("sscan counted results",
               values[0] == 11 && values[1] == 12);
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

    r = neverc_fmt_sprintfln("%d", 42);
    check_str("sprintfln", r, "42\n"); free(r);
}

static void test_append_family(void) {
    printf("[append family]\n");
    char buf[64] = "hello";

    int n = neverc_fmt_append(buf, sizeof(buf), " world");
    check_int("append wrote", n, 6);
    check_str("append result", buf, "hello world");

    char buf2[64] = "line1";
    n = neverc_fmt_appendln(buf2, sizeof(buf2), ": data");
    check_true("appendln wrote", n > 0);
    check_true("appendln has newline", buf2[strlen(buf2)-1] == '\n');
}

static void test_append_boundaries(void) {
    printf("[append boundaries]\n");

    char guard = 'Q';
    check_int("append zero capacity",
              neverc_fmt_append(&guard, 0, "data"), 0);
    check_int("appendln zero capacity",
              neverc_fmt_appendln(&guard, 0, "data"), 0);
    check_int("appendf zero capacity",
              neverc_fmt_appendf(&guard, 0, "%s", "data"), 0);
    check_int("zero capacity leaves target alone", guard, 'Q');

    check_int("append null target", neverc_fmt_append(NULL, 4, "data"), 0);
    check_int("appendf null target", neverc_fmt_appendf(NULL, 4, "%s", "data"), 0);

    char unterminated[4] = {'a', 'b', 'c', 'd'};
    check_int("append unterminated target",
              neverc_fmt_append(unterminated, sizeof(unterminated), "x"), 0);
    check_true("unterminated target unchanged",
               memcmp(unterminated, "abcd", sizeof(unterminated)) == 0);
}

static void test_invalid_formats(void) {
    printf("[invalid formats]\n");
    char *result = neverc_fmt_sprintf(NULL);
    check_true("null format rejected", result == NULL);
    free(result);

    result = neverc_fmt_sprintf("%999999999999999999999999d", 42);
    check_true("overflowing width rejected", result == NULL);
    free(result);

    result = neverc_fmt_sprintf("%.999999999999999999999999d", 42);
    check_true("overflowing precision rejected", result == NULL);
    free(result);

    result = neverc_fmt_sprintf("%.1000f", 1.0);
    check_true("unsupported float precision rejected", result == NULL);
    free(result);

    result = neverc_fmt_sprintf("%.*e", INT_MAX, 1.0);
    check_true("dynamic float precision rejected", result == NULL);
    free(result);

    check_int("scan null dest", neverc_fmt_scan(NULL), 0);

    result = neverc_fmt_sprintf("hello%");
    check_str("trailing percent kept", result, "hello%");
    free(result);

    check_int("fprintf null file", neverc_fmt_fprintf(NULL, "x"), -1);
}

static void test_sscanln(void) {
    printf("[sscanln]\n");
    int a = 0;
    int n = neverc_fmt_sscanln("42 hello\nmore", "%d", &a);
    check_int("sscanln matched", n, 1);
    check_int("sscanln val", a, 42);
}

static void test_stream_scan(void) {
    printf("[stream scan]\n");
    FILE *tmp = tmpfile();
    check_true("stream fixture", tmp != NULL);
    if (!tmp) return;
    fputs("17\n-2 word\n", tmp);
    rewind(tmp);

    int value = 0;
    check_int("fscan matched", neverc_fmt_fscan(tmp, &value), 1);
    check_int("fscan value", value, 17);
    char word[256] = {0};
    check_int("fscanln matched",
              neverc_fmt_fscanln(tmp, "%d %255s", &value, word), 2);
    check_int("fscanln value", value, -2);
    check_str("fscanln word", word, "word");
    fclose(tmp);
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
    test_append_family();
    test_append_boundaries();
    test_invalid_formats();
    test_sscanln();
    test_stream_scan();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
