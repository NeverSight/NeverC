#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "neverc/std/fmt.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

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

    r = neverc_fmt_sprintf("%.5d", 42);
    check_str("int precision", r, "00042"); free(r);

    r = neverc_fmt_sprintf("%.0d", 0);
    check_str("zero prec zero", r, ""); free(r);

    r = neverc_fmt_sprintf("%+.0d", 0);
    check_str("plus zero prec zero", r, ""); free(r);

    r = neverc_fmt_sprintf("%+8.0d", 0);
    check_str("plus width zero prec zero", r, "        "); free(r);

    r = neverc_fmt_sprintf("%8.5d", 42);
    check_str("width and precision", r, "   00042"); free(r);

    r = neverc_fmt_sprintf("%08.5d", 42);
    check_str("zero flag ignored with prec", r, "   00042"); free(r);

    r = neverc_fmt_sprintf("%.5x", 42);
    check_str("hex precision", r, "0002a"); free(r);

    r = neverc_fmt_sprintf("%#.0x", 0);
    check_str("alt hex zero prec", r, ""); free(r);

    r = neverc_fmt_sprintf("%#8.0x", 0);
    check_str("alt hex width zero prec", r, "        "); free(r);

    r = neverc_fmt_sprintf("%#.0o", 0);
    check_str("alt octal zero prec", r, ""); free(r);
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

    r = neverc_fmt_sprintf("%10s", (const char *)0);
    check_str("null string width", r, "    (null)"); free(r);

    r = neverc_fmt_sprintf("%-10s", (const char *)0);
    check_str("null string left", r, "(null)    "); free(r);

    r = neverc_fmt_sprintf("%.3s", (const char *)0);
    check_str("null string prec", r, "(nu"); free(r);

    r = neverc_fmt_sprintf("x%s", (const char *)0);
    check_str("null string concat", r, "x(null)"); free(r);

    r = neverc_fmt_sprintf("%c", 'A');
    check_str("char", r, "A"); free(r);

    r = neverc_fmt_sprintf("%c", 0x4e16);
    check_str("rune utf-8", r, "\xe4\xb8\x96"); free(r);

    r = neverc_fmt_sprintf("%5c", 0x4e16);
    check_str("rune width", r, "    \xe4\xb8\x96"); free(r);

    /* Zero flag pads %c runes (Go writePadding / fmtC). */
    r = neverc_fmt_sprintf("%010c", 'A');
    check_str("zero flag pads char", r, "000000000A"); free(r);
    r = neverc_fmt_sprintf("%05c", 0x4e16);
    check_str("zero flag pads rune", r, "0000\xe4\xb8\x96"); free(r);
    r = neverc_fmt_sprintf("%0*c", 4, 'Z');
    check_str("zero flag star char", r, "000Z"); free(r);

    r = neverc_fmt_sprintf("%05s", "abc");
    check_str("zero flag pads string", r, "00abc"); free(r);

    r = neverc_fmt_sprintf("%.1s", "\xe4\xb8\x96\xe7\x95\x8c");
    check_str("string prec runes", r, "\xe4\xb8\x96"); free(r);

    r = neverc_fmt_sprintf("%5s", "\xe4\xb8\x96");
    check_str("string width runes", r, "    \xe4\xb8\x96"); free(r);

    /* Invalid leading bytes are one rune each and must not consume the next. */
    r = neverc_fmt_sprintf("%.1s", "\xc0" "A");
    check_str("string prec invalid utf8", r, "\xc0"); free(r);
    r = neverc_fmt_sprintf("%5s", "\xc0" "A");
    check_str("string width invalid utf8", r, "   \xc0" "A"); free(r);
    r = neverc_fmt_sprintf("%.1s", "\xc0\x80");
    check_str("string prec overlong utf8", r, "\xc0"); free(r);
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

    r = neverc_fmt_sprintf("%g", 1.23456789);
    check_str("g default is shortest", r, "1.23456789"); free(r);

    r = neverc_fmt_sprintf("%G", 1.23456789);
    check_str("G default is shortest", r, "1.23456789"); free(r);

    r = neverc_fmt_sprintf("%.6g", 1.23456789);
    check_str("g explicit prec 6", r, "1.23457"); free(r);

    r = neverc_fmt_sprintf("%g", 1.0);
    check_str("g one", r, "1"); free(r);

    r = neverc_fmt_sprintf("%#.0f", 1.0);
    check_str("sharp f forces point", r, "1."); free(r);

    r = neverc_fmt_sprintf("%#g", 1.0);
    check_str("sharp g keeps zeros", r, "1.00000"); free(r);

    r = neverc_fmt_sprintf("%#g", 0.1);
    check_str("sharp g fraction zeros", r, "0.100000"); free(r);

    r = neverc_fmt_sprintf("%#g", 1e10);
    check_str("sharp g exponent zeros", r, "1.00000e+10"); free(r);

    /* Sharp does not truncate a shortest value that already has >6 digits. */
    r = neverc_fmt_sprintf("%#g", 1.23456789);
    check_str("sharp g keeps extra shortest digits", r, "1.23456789"); free(r);

    r = neverc_fmt_sprintf("%#f", 1.0);
    check_str("sharp f default unchanged", r, "1.000000"); free(r);

    r = neverc_fmt_sprintf("%#.0e", 1.0);
    check_str("sharp e forces point", r, "1.e+00"); free(r);
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

    r = neverc_fmt_sprintf("%+x", 15);
    check_str("plus hex", r, "+f"); free(r);

    r = neverc_fmt_sprintf("%+u", 7u);
    check_str("plus uint", r, "+7"); free(r);

    r = neverc_fmt_sprintf("% x", 15);
    check_str("space hex", r, " f"); free(r);

    r = neverc_fmt_sprintf("%+o", 8);
    check_str("plus octal", r, "+10"); free(r);

    r = neverc_fmt_sprintf("%+b", 10);
    check_str("plus binary", r, "+1010"); free(r);

    r = neverc_fmt_sprintf("%*s", -5, "hi");
    check_str("negative star width", r, "hi   "); free(r);

    r = neverc_fmt_sprintf("%*d", -8, 42);
    check_str("negative star int", r, "42      "); free(r);

    r = neverc_fmt_sprintf("%*d", 8, 42);
    check_str("star width", r, "      42"); free(r);

    r = neverc_fmt_sprintf("%p", (void *)0);
    check_str("nil pointer", r, "0x0"); free(r);

    r = neverc_fmt_sprintf("%+p", (void *)0);
    check_str("plus pointer", r, "+0x0"); free(r);

    r = neverc_fmt_sprintf("% p", (void *)0);
    check_str("space pointer", r, " 0x0"); free(r);

    r = neverc_fmt_sprintf("%#p", (void *)0);
    check_str("sharp pointer omits 0x", r, "0"); free(r);

    r = neverc_fmt_sprintf("%010p", (void *)0);
    check_str("zero pad pointer keeps 0x", r, "0x00000000"); free(r);

    r = neverc_fmt_sprintf("%p", (void *)0x1b);
    check_str("pointer hex", r, "0x1b"); free(r);

    r = neverc_fmt_sprintf("%#p", (void *)0x1b);
    check_str("sharp pointer hex", r, "1b"); free(r);

    r = neverc_fmt_sprintf("%.4p", (void *)0);
    check_str("pointer precision", r, "0x0000"); free(r);
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

    int     n = neverc_fmt_sscanf("42", "%d", &a);
    check_int("sscanf int", n, 1);
    check_int("sscanf int val", a, 42);

    n = neverc_fmt_sscanf("42 % 7", "%d%%%d", &a, &b);
    check_int("percent skip ws count", n, 2);
    check_int("percent skip ws a", a, 42);
    check_int("percent skip ws b", b, 7);

    n = neverc_fmt_sscanf("1 2", "%d\xc2\xa0%d", &a, &b);
    check_int("nbsp format count", n, 2);
    check_int("nbsp format a", a, 1);
    check_int("nbsp format b", b, 2);

    n = neverc_fmt_sscanf("  -17  3.14", "%d %f", &a, &f);
    check_int("sscanf int+float count", n, 2);
    check_int("sscanf int val2", a, -17);
    check_int("sscanf float approx", (int)(f * 100), 314);

    n = neverc_fmt_sscanf("hello world", "%63s", s);
    check_int("sscanf string count", n, 1);
    check_str("sscanf string val", s, "hello");

    {
        char ch = 0;
        n = neverc_fmt_sscanf("AB", "%1c", &ch);
        check_int("sscanf %1c count", n, 1);
        check_int("sscanf %1c val", (int)(unsigned char)ch, 'A');
        ch = 0;
        n = neverc_fmt_sscanf(" Z", "%c", &ch);
        check_int("sscanf %c keeps space", n, 1);
        check_int("sscanf %c space val", (int)(unsigned char)ch, ' ');
    }

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

    n = neverc_fmt_sscanf("x3", "x %d", &a);
    check_int("sscanf format space required", n, 0);
    n = neverc_fmt_sscanf("1\n2", "%d %d", &a, &b);
    check_int("sscanf format space does not eat newline", n, 1);
    n = neverc_fmt_sscanf("1 2", "%d\n%d", &a, &b);
    check_int("sscanf format newline does not match space", n, 1);
    n = neverc_fmt_sscanf("1\n2", "%d\n%d", &a, &b);
    check_int("sscanf format newline matches newline", n, 2);
    check_int("sscanf format newline first", a, 1);
    check_int("sscanf format newline second", b, 2);

    {
        FILE *tmp = tmpfile();
        check_true("fscanf fixture", tmp != NULL);
        if (tmp) {
            fputs("1\n2\n", tmp);
            rewind(tmp);
            a = 0;
            b = 0;
            n = neverc_fmt_fscanf(tmp, "%d\n%d", &a, &b);
            check_int("fscanf format newline count", n, 2);
            check_int("fscanf format newline first", a, 1);
            check_int("fscanf format newline second", b, 2);
            fclose(tmp);
        }
    }

    {
        FILE *tmp = tmpfile();
        check_true("fscanf leftover fixture", tmp != NULL);
        if (tmp) {
            fputs("10 20\n", tmp);
            rewind(tmp);
            a = 0;
            b = 0;
            n = neverc_fmt_fscanf(tmp, "%d", &a);
            check_int("fscanf first leftover", n, 1);
            check_int("fscanf leftover val", a, 10);
            n = neverc_fmt_fscanf(tmp, "%d", &b);
            check_int("fscanf second leftover", n, 1);
            check_int("fscanf leftover second val", b, 20);
            fclose(tmp);
        }
    }

    /* A seekability probe must not use fseek: ISO C makes any successful
     * positioning operation discard bytes supplied by ungetc. */
    {
        FILE *tmp = tmpfile();
        check_true("fscanf caller pushback fixture", tmp != NULL);
        if (tmp) {
            fputs("x", tmp);
            rewind(tmp);
            check_int("fscanf caller pushback underlying", getc(tmp), 'x');
            check_int("fscanf caller pushback install", ungetc('7', tmp), '7');
            a = 0;
            n = neverc_fmt_fscanf(tmp, "%d", &a);
            check_int("fscanf caller pushback count", n, 1);
            check_int("fscanf caller pushback value", a, 7);
            fclose(tmp);
        }
    }

    /* Go accept(sign) consumes a lone '+' / '-'; leftover starts after it. */
    {
        FILE *tmp = tmpfile();
        check_true("fscanf lone plus leftover fixture", tmp != NULL);
        if (tmp) {
            fputs("+ 2\n", tmp);
            rewind(tmp);
            a = 77;
            b = 77;
            n = neverc_fmt_fscanf(tmp, "%d", &a);
            check_int("fscanf lone plus first count", n, 0);
            check_int("fscanf lone plus dest unchanged", a, 77);
            n = neverc_fmt_fscanf(tmp, "%d", &b);
            check_int("fscanf after lone plus count", n, 1);
            check_int("fscanf after lone plus val", b, 2);
            fclose(tmp);
        }
    }

    /* Go scanNumber consumes an overflowing token; leftover starts after it. */
    {
        FILE *tmp = tmpfile();
        check_true("fscanf overflow leftover fixture", tmp != NULL);
        if (tmp) {
            fputs("9223372036854775808 2\n", tmp);
            rewind(tmp);
            long long ll = 77;
            a = 77;
            check_int("fscanf int64 overflow leftover count",
                      neverc_fmt_fscanf(tmp, "%lld", &ll), 0);
            check_true("fscanf int64 overflow leftover dest", ll == 77);
            check_int("fscanf after int64 overflow leftover",
                      neverc_fmt_fscanf(tmp, "%d", &a), 1);
            check_int("fscanf after int64 overflow leftover val", a, 2);
            fclose(tmp);
        }
    }
    {
        FILE *tmp = tmpfile();
        check_true("fscanf uint overflow leftover fixture", tmp != NULL);
        if (tmp) {
            fputs("18446744073709551616 2\n", tmp);
            rewind(tmp);
            unsigned long long ull = 77;
            a = 77;
            check_int("fscanf uint64 overflow leftover count",
                      neverc_fmt_fscanf(tmp, "%llu", &ull), 0);
            check_true("fscanf uint64 overflow leftover dest", ull == 77);
            check_int("fscanf after uint64 overflow leftover",
                      neverc_fmt_fscanf(tmp, "%d", &a), 1);
            check_int("fscanf after uint64 overflow leftover val", a, 2);
            fclose(tmp);
        }
    }
    {
        FILE *tmp = tmpfile();
        check_true("fscanf float overflow leftover fixture", tmp != NULL);
        if (tmp) {
            fputs("1e309 2\n", tmp);
            rewind(tmp);
            double fv = 77;
            a = 77;
            check_int("fscanf float overflow leftover count",
                      neverc_fmt_fscanf(tmp, "%f", &fv), 0);
            check_true("fscanf float overflow leftover dest", fv == 77);
            check_int("fscanf after float overflow leftover",
                      neverc_fmt_fscanf(tmp, "%d", &a), 1);
            check_int("fscanf after float overflow leftover val", a, 2);
            fclose(tmp);
        }
    }
    {
        FILE *tmp = tmpfile();
        check_true("fscanf float syntax leftover fixture", tmp != NULL);
        if (tmp) {
            fputs("1e 2\n", tmp);
            rewind(tmp);
            double fv = 77;
            a = 77;
            check_int("fscanf float syntax leftover count",
                      neverc_fmt_fscanf(tmp, "%f", &fv), 0);
            check_true("fscanf float syntax leftover dest", fv == 77);
            check_int("fscanf after float syntax leftover",
                      neverc_fmt_fscanf(tmp, "%d", &a), 1);
            check_int("fscanf after float syntax leftover val", a, 2);
            fclose(tmp);
        }
    }
    {
        FILE *tmp = tmpfile();
        check_true("fscanf hex float syntax leftover fixture", tmp != NULL);
        if (tmp) {
            fputs("0x1 2\n", tmp);
            rewind(tmp);
            double fv = 77;
            a = 77;
            check_int("fscanf hex float syntax leftover count",
                      neverc_fmt_fscanf(tmp, "%f", &fv), 0);
            check_true("fscanf hex float syntax leftover dest", fv == 77);
            check_int("fscanf after hex float syntax leftover",
                      neverc_fmt_fscanf(tmp, "%d", &a), 1);
            check_int("fscanf after hex float syntax leftover val", a, 2);
            fclose(tmp);
        }
    }
    {
        FILE *tmp = tmpfile();
        check_true("fscan utf8 nbsp fixture", tmp != NULL);
        if (tmp) {
            fputs("\xc2\xa0""42", tmp);
            rewind(tmp);
            a = 77;
            check_int("fscan utf8 nbsp count", neverc_fmt_fscan(tmp, &a), 1);
            check_int("fscan utf8 nbsp val", a, 42);
            fclose(tmp);
        }
    }

    /* A line longer than the 4096-byte fgets chunk must still count as one
     * format record, or the second conversion never sees its input. */
    {
        FILE *tmp = tmpfile();
        char *longline = (char *)malloc(5002);
        char word[8192];
        check_true("fscanf long line fixture", tmp != NULL && longline != NULL);
        if (tmp && longline) {
            memset(longline, 'A', 5000);
            longline[5000] = '\n';
            longline[5001] = '\0';
            fputs(longline, tmp);
            fputs("42\n", tmp);
            rewind(tmp);
            memset(word, 0, sizeof(word));
            a = 0;
            n = neverc_fmt_fscanf(tmp, "%8191s\n%d", word, &a);
            check_int("fscanf long line count", n, 2);
            check_int("fscanf long line len", (int)strlen(word), 5000);
            check_int("fscanf long line second", a, 42);
            fclose(tmp);
        }
        free(longline);
    }

    /* Go Fscanf: nlIsSpace=false, so a leading/interior newline is not
     * skipped as value whitespace. \v/\f are isSpace and are skipped.
     * https://github.com/golang/go/blob/master/src/fmt/scan.go */
    a = 77;
    n = neverc_fmt_sscanf("\n42", "%d", &a);
    check_int("sscanf leading newline rejected", n, 0);
    check_int("sscanf leading newline leaves dest", a, 77);
    a = 77;
    n = neverc_fmt_sscanf("42\n43", "%d%d", &a, &b);
    check_int("sscanf interior newline not skipped", n, 1);
    check_int("sscanf interior newline first", a, 42);
    n = neverc_fmt_sscanf("\n42", "\n%d", &a);
    check_int("sscanf format newline then value", n, 1);
    check_int("sscanf format newline then value val", a, 42);
    n = neverc_fmt_sscanf("\v42", "%d", &a);
    check_int("sscanf vertical tab skipped", n, 1);
    check_int("sscanf vertical tab val", a, 42);
    n = neverc_fmt_sscanf("\f42", "%d", &a);
    check_int("sscanf form feed skipped", n, 1);
    check_int("sscanf form feed val", a, 42);
    n = neverc_fmt_sscanf("hello\vworld", "%63s", s);
    check_int("sscanf string stops at vertical tab", n, 1);
    check_str("sscanf string vertical tab val", s, "hello");
    n = neverc_fmt_sscanf("hello\fworld", "%63s", s);
    check_int("sscanf string stops at form feed", n, 1);
    check_str("sscanf string form feed val", s, "hello");

    n = neverc_fmt_sscanf("ff", "%x", (unsigned int *)&a);
    check_int("sscanf hex", n, 1);
    check_int("sscanf hex val", a, 255);
    n = neverc_fmt_sscanf("0xff", "%x", (unsigned int *)&a);
    check_int("sscanf hex does not swallow 0x prefix", n, 1);
    check_int("sscanf hex 0x prefix value is 0", a, 0);

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

    /* Go fmt.Scan overflowTests: "1e500" into float64 must fail. */
    special = 7.0;
    n = neverc_fmt_sscanf("1e309", "%f", &special);
    check_int("sscanf float overflow rejected", n, 0);
    check_true("sscanf float overflow leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("-1e309", "%f", &special);
    check_int("sscanf float underflow rejected", n, 0);
    check_true("sscanf float underflow leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("1e500", "%f", &special);
    check_int("sscanf float 1e500 rejected", n, 0);
    check_true("sscanf float 1e500 leaves output", special == 7.0);

    special = 7.0;
    n = neverc_fmt_sscanf("0x1p0", "%f", &special);
    check_int("sscanf hex float", n, 1);
    check_true("sscanf hex float value", special == 1.0);
    n = neverc_fmt_sscanf("0x1.8p0", "%f", &special);
    check_int("sscanf hex float frac", n, 1);
    check_true("sscanf hex float frac value", special == 1.5);
    n = neverc_fmt_sscanf("-0x1p+1", "%f", &special);
    check_int("sscanf hex float signed exp", n, 1);
    check_true("sscanf hex float signed exp value", special == -2.0);

    /* Incomplete hex-float tokens must not silently succeed as 0. */
    special = 7.0;
    n = neverc_fmt_sscanf("0x", "%f", &special);
    check_int("sscanf incomplete hex 0x rejected", n, 0);
    check_true("sscanf incomplete hex 0x leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("0x1", "%f", &special);
    check_int("sscanf incomplete hex 0x1 rejected", n, 0);
    check_true("sscanf incomplete hex 0x1 leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("0x1p", "%f", &special);
    check_int("sscanf incomplete hex 0x1p rejected", n, 0);
    check_true("sscanf incomplete hex 0x1p leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("+0X", "%f", &special);
    check_int("sscanf incomplete hex +0X rejected", n, 0);
    check_true("sscanf incomplete hex +0X leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("0x.p0", "%f", &special);
    check_int("sscanf incomplete hex 0x.p0 rejected", n, 0);
    check_true("sscanf incomplete hex 0x.p0 leaves output", special == 7.0);

    special = 7.0;
    n = neverc_fmt_sscanf("1e", "%f", &special);
    check_int("sscanf incomplete decimal 1e rejected", n, 0);
    check_true("sscanf incomplete decimal 1e leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("1e+", "%f", &special);
    check_int("sscanf incomplete decimal 1e+ rejected", n, 0);
    check_true("sscanf incomplete decimal 1e+ leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("1e-", "%f", &special);
    check_int("sscanf incomplete decimal 1e- rejected", n, 0);
    check_true("sscanf incomplete decimal 1e- leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("1e_", "%f", &special);
    check_int("sscanf incomplete decimal 1e_ rejected", n, 0);
    check_true("sscanf incomplete decimal 1e_ leaves output", special == 7.0);
    special = 7.0;
    n = neverc_fmt_sscanf("1e+foo", "%f", &special);
    check_int("sscanf incomplete decimal 1e+foo rejected", n, 0);
    check_true("sscanf incomplete decimal 1e+foo leaves output", special == 7.0);

    n = neverc_fmt_sscanf("2.3p", "%f", &special);
    check_int("sscanf incomplete decimal 2.3p rejected", n, 0);
    check_true("sscanf incomplete decimal 2.3p leaves output", special == 7.0);
    n = neverc_fmt_sscanf("2.3P+", "%f", &special);
    check_int("sscanf incomplete decimal 2.3P+ rejected", n, 0);
    check_true("sscanf incomplete decimal 2.3P+ leaves output", special == 7.0);

    special = 7.0;
    n = neverc_fmt_sscanf("0x_1p0", "%f", &special);
    check_int("sscanf hex underscore", n, 1);
    check_true("sscanf hex underscore value", special == 1.0);
    n = neverc_fmt_sscanf("1_000", "%f", &special);
    check_int("sscanf decimal underscore", n, 1);
    check_true("sscanf decimal underscore value", special == 1000.0);
    n = neverc_fmt_sscanf("0x1_0p0", "%f", &special);
    check_int("sscanf hex mid underscore", n, 1);
    check_true("sscanf hex mid underscore value", special == 16.0);

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

    /* Go unformatted Scan accepts prefixes and underscores; %d does not. */
    one = 0;
    check_int("sscan underscore count", neverc_fmt_sscan("2_1", &one), 1);
    check_int("sscan underscore val", one, 21);
    one = 0;
    check_int("sscan hex prefix count", neverc_fmt_sscan("0x10", &one), 1);
    check_int("sscan hex prefix val", one, 16);
    one = 0;
    check_int("sscan octal 07 count", neverc_fmt_sscan("07", &one), 1);
    check_int("sscan octal 07 val", one, 7);
    one = 77;
    check_int("sscan octal 08 count", neverc_fmt_sscan("08", &one), 1);
    check_int("sscan octal 08 val", one, 0);
    a = 0;
    n = neverc_fmt_sscanf("08", "%d", &a);
    check_int("sscanf %%d 08 count", n, 1);
    check_int("sscanf %%d 08 val", a, 8);
    one = 0;
    check_int("sscan thousands count", neverc_fmt_sscan("1_000", &one), 1);
    check_int("sscan thousands val", one, 1000);
    one = 0;
    check_int("sscan newline is space count", neverc_fmt_sscan("\n9", &one), 1);
    check_int("sscan newline is space val", one, 9);
    one = 0;
    check_int("sscan vertical tab is space count",
              neverc_fmt_sscan("\v8", &one), 1);
    check_int("sscan vertical tab is space val", one, 8);
    one = 0;
    check_int("sscan nbsp is space count",
              neverc_fmt_sscan("\xC2\xA0""9", &one), 1);
    check_int("sscan nbsp is space val", one, 9);
    a = 0;
    n = neverc_fmt_sscanf("\xc2\xa0""42", "%d", &a);
    check_int("sscanf utf8 nbsp skipped", n, 1);
    check_int("sscanf utf8 nbsp val", a, 42);
    n = neverc_fmt_sscanf("\xc2\x85""42", "%d", &a);
    check_int("sscanf utf8 nel skipped", n, 1);
    check_int("sscanf utf8 nel val", a, 42);
    /* Go fmt/scan.go: invalid UTF-8 is U+FFFD, which is not isSpace. */
    a = 77;
    n = neverc_fmt_sscanf("\xa0""42", "%d", &a);
    check_int("sscanf lone 0xa0 is not space", n, 0);
    check_int("sscanf lone 0xa0 leaves dest", a, 77);
    a = 77;
    n = neverc_fmt_sscanf("\x85""42", "%d", &a);
    check_int("sscanf lone 0x85 is not space", n, 0);
    check_int("sscanf lone 0x85 leaves dest", a, 77);
    n = neverc_fmt_sscanf("hello\xa0world", "%63s", s);
    check_int("sscanf string keeps lone 0xa0 count", n, 1);
    check_str("sscanf string keeps lone 0xa0", s, "hello\xa0world");
    one = 77;
    check_int("sscan lone 0xa0 is not space count",
              neverc_fmt_sscan("\xa0""9", &one), 0);
    check_int("sscan lone 0xa0 leaves dest", one, 77);
    a = 0;
    n = neverc_fmt_sscanf("7_2", "%d", &a);
    check_int("sscanf %%d underscore stops", n, 1);
    check_int("sscanf %%d underscore val", a, 7);

    n = neverc_fmt_sscanf("2.3p2", "%g", &special);
    check_int("sscanf decimal binary exp", n, 1);
    check_true("sscanf decimal binary exp value", special == 9.2);
    special = 7.0;
    n = neverc_fmt_sscanf("2.3P2", "%f", &special);
    check_int("sscanf decimal P is not binary exp", n, 0);
    check_true("sscanf decimal P leaves output", special == 7.0);

    {
        int width_d = 0;
        n = neverc_fmt_sscanf("123", "%2d", &width_d);
        check_int("sscanf %%2d count", n, 1);
        check_int("sscanf %%2d val", width_d, 12);
    }
    {
        unsigned oct = 0, bin = 0;
        n = neverc_fmt_sscanf("10", "%o", &oct);
        check_int("sscanf %%o count", n, 1);
        check_int("sscanf %%o val", (int)oct, 8);
        n = neverc_fmt_sscanf("1010", "%b", &bin);
        check_int("sscanf %%b count", n, 1);
        check_int("sscanf %%b val", (int)bin, 10);
    }
    special = 0.0;
    n = neverc_fmt_sscanf("1.5", "%F", &special);
    check_int("sscanf %%F count", n, 1);
    check_true("sscanf %%F val", special == 1.5);
    n = neverc_fmt_sscanf("2.5e1", "%E", &special);
    check_int("sscanf %%E count", n, 1);
    check_true("sscanf %%E val", special == 25.0);
    n = neverc_fmt_sscanf("3.5", "%G", &special);
    check_int("sscanf %%G count", n, 1);
    check_true("sscanf %%G val", special == 3.5);
}

static void test_appendf(void) {
    printf("[appendf]\n");
    char buf[64] = "prefix:";
    int n = neverc_fmt_appendf(buf, sizeof(buf), "%d-%s", 42, "ok");
    check_str("appendf result", buf, "prefix:42-ok");
    check_int("appendf return", n > 0, 1);

    char tiny_buf[10] = "hi";
    neverc_fmt_appendf(tiny_buf, sizeof(tiny_buf), "%d%d%d", 111, 222, 333);
    check_int("appendf truncate", (int)strlen(tiny_buf) < 10, 1);

    /* appendf scans for an existing C string; 0x7f fill without a terminator
     * is not a destination (returns 0). Start empty so interior %c 0 is
     * visible in the written bytes. */
    char nul_buf[8];
    memset(nul_buf, 0x7f, sizeof(nul_buf));
    nul_buf[0] = '\0';
    n = neverc_fmt_appendf(nul_buf, sizeof(nul_buf), "a%cb", 0);
    check_int("appendf %%c 0 length", n, 3);
    check_true("appendf %%c 0 bytes",
               nul_buf[0] == 'a' && nul_buf[1] == '\0' && nul_buf[2] == 'b' &&
               nul_buf[3] == '\0');

    char *nul_s = neverc_fmt_sprintf("a%cb", 0);
    check_true("sprintf %%c 0 bytes",
               nul_s && nul_s[0] == 'a' && nul_s[1] == '\0' &&
               nul_s[2] == 'b' && nul_s[3] == '\0');
    free(nul_s);
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

    r = neverc_fmt_sprint(NULL);
    check_str("sprint null", r, ""); free(r);

    r = neverc_fmt_sprintln("hello");
    check_str("sprintln", r, "hello\n"); free(r);

    r = neverc_fmt_sprintln(NULL);
    check_str("sprintln null", r, "\n"); free(r);

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

    result = neverc_fmt_sprintf("%.*d", INT_MAX, -1);
    check_true("dynamic int precision overflow rejected", result == NULL);
    free(result);

    result = neverc_fmt_sprintf("%*s", 1000001, "x");
    check_true("star width > 1e6 rejected", result == NULL);
    free(result);

    result = neverc_fmt_sprintf("%*s", INT_MAX, "x");
    check_true("INT_MAX star width rejected", result == NULL);
    free(result);

    result = neverc_fmt_sprintf("%100000000s", "x");
    check_true("1e8 literal width rejected", result == NULL);
    free(result);

    result = neverc_fmt_sprintf("%*s", 8, "hi");
    check_str("width under cap", result, "      hi");
    free(result);

    result = neverc_fmt_sprintf("%z %s", "safe");
    check_true("unknown verb fails safely", result == NULL);
    free(result);

    result = neverc_fmt_sprintf("%n", 0);
    check_true("percent-n write gadget rejected", result == NULL);
    free(result);

    int written = 77;
    check_int("sscanf percent-n rejected",
              neverc_fmt_sscanf("hello", "%n", &written), 0);
    check_int("sscanf percent-n leaves dest", written, 77);

    char scan_buf[4] = "XXX";
    check_int("sscanf s without width rejected",
              neverc_fmt_sscanf("abcdef", "%s", scan_buf), 0);
    check_true("sscanf s without width leaves dest",
               scan_buf[0] == 'X' && scan_buf[1] == 'X' && scan_buf[2] == 'X');

    char bounded[8];
    memset(bounded, 'Q', sizeof(bounded));
    check_int("sscanf bounded s",
              neverc_fmt_sscanf("abcdef", "%7s", bounded), 1);
    check_str("sscanf bounded s value", bounded, "abcdef");

    check_int("scan null dest", neverc_fmt_scan(NULL), 0);

    result = neverc_fmt_sprintf("hello%");
    check_str("trailing percent kept", result, "hello%");
    free(result);

    check_int("fprintf null file", neverc_fmt_fprintf(NULL, "x"), -1);

    {
        char path[512];
#ifdef _WIN32
        char tmp[MAX_PATH];
        DWORD n = GetTempPathA((DWORD)sizeof(tmp), tmp);
        if (n > 0 && n < sizeof(tmp))
            snprintf(path, sizeof(path), "%sneverc_fmt_ro_%u", tmp,
                     (unsigned)_getpid());
        else
            path[0] = '\0';
#else
        snprintf(path, sizeof(path), "/tmp/neverc_fmt_ro_%d", (int)getpid());
#endif
        FILE *w = path[0] ? fopen(path, "w") : NULL;
        if (w) {
            fputs("x", w);
            fclose(w);
        }
        FILE *rd = path[0] ? fopen(path, "r") : NULL;
        check_true("write-error fixture", rd != NULL);
        if (rd) {
            check_int("fprintf read-only fails closed",
                      neverc_fmt_fprintf(rd, "hello"), -1);
            check_int("fprint read-only fails closed",
                      neverc_fmt_fprint(rd, "hello"), -1);
            check_int("fprintln read-only fails closed",
                      neverc_fmt_fprintln(rd, "hello"), -1);
            fclose(rd);
        }
#ifdef _WIN32
        if (path[0]) DeleteFileA(path);
#else
        if (path[0]) unlink(path);
#endif
    }
}

static void test_sscanln(void) {
    printf("[sscanln]\n");
    int a = 0;
    int n = neverc_fmt_sscanln("42 hello\nmore", "%d", &a);
    check_int("sscanln matched", n, 1);
    check_int("sscanln val", a, 42);

    /* Former 4096-byte cap leftover after fscanf grew. */
    {
        char *long_line = (char *)malloc(4104);
        check_true("sscanln long fixture", long_line != NULL);
        if (long_line) {
            memset(long_line, ' ', 4096);
            memcpy(long_line + 4096, "42", 2);
            long_line[4098] = '\0';
            a = 0;
            n = neverc_fmt_sscanln(long_line, "%d", &a);
            check_int("sscanln long leading space", n, 1);
            check_int("sscanln long val", a, 42);
            free(long_line);
        }
    }
}

enum { FMT_WIDE_SCAN_INPUT_SIZE = 300 };

static void make_wide_scan_input(unsigned char *input) {
    input[0] = '1';
    memset(input + 1, 'x', FMT_WIDE_SCAN_INPUT_SIZE - 1);
}

static FILE *open_fmt_input_pipe(const unsigned char *data, size_t len) {
    int fds[2];
    size_t offset = 0;
    FILE *input;
    if (!data || len > 4096U) return NULL;
#ifdef _WIN32
    if (_pipe(fds, 4096, _O_BINARY) != 0) return NULL;
    while (offset < len) {
        int written = _write(fds[1], data + offset,
                             (unsigned int)(len - offset));
        if (written <= 0) {
            _close(fds[0]);
            _close(fds[1]);
            return NULL;
        }
        offset += (size_t)written;
    }
    _close(fds[1]);
    input = _fdopen(fds[0], "rb");
    if (!input) _close(fds[0]);
#else
    if (pipe(fds) != 0) return NULL;
    while (offset < len) {
        ssize_t written = write(fds[1], data + offset, len - offset);
        if (written <= 0) {
            close(fds[0]);
            close(fds[1]);
            return NULL;
        }
        offset += (size_t)written;
    }
    close(fds[1]);
    input = fdopen(fds[0], "rb");
    if (!input) close(fds[0]);
#endif
    return input;
}

static void test_nonseekable_fscanf(void) {
    FILE *input;
    int a, b;

    input = open_fmt_input_pipe((const unsigned char *)"10 20\n", 6);
    check_true("fscanf pipe fixture", input != NULL);
    if (input) {
        a = b = 0;
        check_int("fscanf pipe first count",
                  neverc_fmt_fscanf(input, "%d", &a), 1);
        check_int("fscanf pipe first value", a, 10);
        check_int("fscanf pipe second count",
                  neverc_fmt_fscanf(input, "%d", &b), 1);
        check_int("fscanf pipe second value", b, 20);
        check_int("fscanf pipe leaves newline for getc", getc(input), '\n');
        fclose(input);
    }

    input = open_fmt_input_pipe((const unsigned char *)"7xyz", 4);
    check_true("fscanf pipe suffix fixture", input != NULL);
    if (input) {
        a = 0;
        check_int("fscanf pipe suffix count",
                  neverc_fmt_fscanf(input, "%d", &a), 1);
        check_int("fscanf pipe suffix value", a, 7);
        check_int("fscanf pipe native getc x", getc(input), 'x');
        check_int("fscanf pipe native getc y", getc(input), 'y');
        check_int("fscanf pipe native getc z", getc(input), 'z');
        fclose(input);
    }

    {
        unsigned char wide_input[FMT_WIDE_SCAN_INPUT_SIZE];
        char suffix = '\0';
        make_wide_scan_input(wide_input);
        input = open_fmt_input_pipe(wide_input, sizeof(wide_input));
        check_true("fscanf pipe wide-width fixture", input != NULL);
        if (input) {
            a = 0;
            check_int("fscanf pipe wide-width count",
                      neverc_fmt_fscanf(input, "%300d%c", &a, &suffix), 2);
            check_int("fscanf pipe wide-width value", a, 1);
            check_int("fscanf pipe wide-width suffix", suffix, 'x');
            fclose(input);
        }
    }

    {
        const unsigned char high_bytes[] = {' ', 0xC2, 0xA1, '9'};
        input = open_fmt_input_pipe(high_bytes, sizeof(high_bytes));
        check_true("fscanf pipe high-byte fixture", input != NULL);
        if (input) {
            a = 77;
            check_int("fscanf pipe high-byte rejected",
                      neverc_fmt_fscanf(input, "%d", &a), 0);
            check_int("fscanf pipe high-byte leaves output", a, 77);
            check_int("fscanf pipe preserves first high byte",
                      getc(input), 0xC2);
            check_int("fscanf pipe preserves high-byte suffix",
                      getc(input), 0xA1);
            check_int("fscanf pipe preserves suffix digit",
                      getc(input), '9');
            fclose(input);
        }
    }

    {
        const unsigned char high_bytes[] = {'\t', 0xC2, 0xA1, '8'};
        input = open_fmt_input_pipe(high_bytes, sizeof(high_bytes));
        check_true("fscan pipe high-byte fixture", input != NULL);
        if (input) {
            a = 77;
            check_int("fscan pipe high-byte rejected",
                      neverc_fmt_fscan(input, &a), 0);
            check_int("fscan pipe high-byte leaves output", a, 77);
            check_int("fscan pipe preserves first high byte",
                      getc(input), 0xC2);
            check_int("fscan pipe preserves high-byte suffix",
                      getc(input), 0xA1);
            check_int("fscan pipe preserves suffix digit", getc(input), '8');
            fclose(input);
        }
    }

    input = open_fmt_input_pipe(
        (const unsigned char *)"12 1.5 hi !\n34Z",
        strlen("12 1.5 hi !\n34Z"));
    check_true("fscanf pipe formatted fixture", input != NULL);
    if (input) {
        double f = 0.0;
        char word[3] = {0};
        char mark = '\0';
        a = b = 0;
        check_int("fscanf pipe formatted count",
                  neverc_fmt_fscanf(input, "%2d %f %2s %c\n%d",
                                    &a, &f, word, &mark, &b), 5);
        check_true("fscanf pipe formatted values",
                   a == 12 && f == 1.5 && strcmp(word, "hi") == 0 &&
                   mark == '!' && b == 34);
        check_int("fscanf pipe formatted suffix", getc(input), 'Z');
        fclose(input);
    }

    input = open_fmt_input_pipe((const unsigned char *)"  %abQ", 6);
    check_true("fscanf pipe literal fixture", input != NULL);
    if (input) {
        char word[3] = {0};
        check_int("fscanf pipe percent width count",
                  neverc_fmt_fscanf(input, "%%%2sX", word), 1);
        check_str("fscanf pipe percent width value", word, "ab");
        check_int("fscanf pipe literal mismatch suffix", getc(input), 'Q');
        fclose(input);
    }

    {
        const char *all_numbers =
            "-12 34 ff 10 11 9223372036854775807 "
            "18446744073709551615X";
        long lv = 0;
        unsigned int uv = 0, xv = 0, ov = 0, bv = 0;
        long long llv = 0;
        unsigned long long ullv = 0;
        input = open_fmt_input_pipe((const unsigned char *)all_numbers,
                                    strlen(all_numbers));
        check_true("fscanf pipe numeric verbs fixture", input != NULL);
        if (input) {
            check_int("fscanf pipe numeric verbs count",
                      neverc_fmt_fscanf(input,
                          "%li %u %x %o %b %lld %llu",
                          &lv, &uv, &xv, &ov, &bv, &llv, &ullv), 7);
            check_true("fscanf pipe numeric verbs values",
                       lv == -12L && uv == 34U && xv == 255U && ov == 8U &&
                       bv == 3U && llv == LLONG_MAX && ullv == ULLONG_MAX);
            check_int("fscanf pipe numeric verbs suffix", getc(input), 'X');
            fclose(input);
        }
    }

    input = open_fmt_input_pipe(
        (const unsigned char *)"2147483648 9", 12);
    check_true("fscanf pipe overflow fixture", input != NULL);
    if (input) {
        a = 77;
        b = 0;
        check_int("fscanf pipe overflow rejected",
                  neverc_fmt_fscanf(input, "%d", &a), 0);
        check_int("fscanf pipe overflow leaves output", a, 77);
        check_int("fscanf pipe overflow consumes token",
                  neverc_fmt_fscanf(input, "%d", &b), 1);
        check_int("fscanf pipe overflow suffix value", b, 9);
        fclose(input);
    }

    input = open_fmt_input_pipe((const unsigned char *)"1e+ 9", 5);
    check_true("fscanf pipe float-syntax fixture", input != NULL);
    if (input) {
        double f = 7.0;
        b = 0;
        check_int("fscanf pipe float syntax rejected",
                  neverc_fmt_fscanf(input, "%f", &f), 0);
        check_true("fscanf pipe float syntax leaves output", f == 7.0);
        check_int("fscanf pipe float syntax consumes token",
                  neverc_fmt_fscanf(input, "%d", &b), 1);
        check_int("fscanf pipe float syntax suffix value", b, 9);
        fclose(input);
    }
}

static void test_stream_scan(void) {
    printf("[stream scan]\n");
    test_nonseekable_fscanf();
    {
        unsigned char wide_input[FMT_WIDE_SCAN_INPUT_SIZE];
        FILE *wide_tmp = tmpfile();
        char suffix = '\0';
        int wide_value = 0;
        make_wide_scan_input(wide_input);
        check_true("fscanf seekable wide-width fixture", wide_tmp != NULL);
        if (wide_tmp) {
            check_true("fscanf seekable wide-width write",
                       fwrite(wide_input, 1, sizeof(wide_input), wide_tmp) ==
                           sizeof(wide_input));
            rewind(wide_tmp);
            check_int("fscanf seekable wide-width count",
                      neverc_fmt_fscanf(wide_tmp, "%300d%c",
                                        &wide_value, &suffix), 2);
            check_int("fscanf seekable wide-width value", wide_value, 1);
            check_int("fscanf seekable wide-width suffix", suffix, 'x');
            fclose(wide_tmp);
        }
    }
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

    tmp = tmpfile();
    check_true("fscan long space fixture", tmp != NULL);
    if (tmp) {
        char pad[260];
        memset(pad, ' ', 255);
        pad[255] = '7';
        pad[256] = '\n';
        pad[257] = '\0';
        fputs(pad, tmp);
        rewind(tmp);
        value = 0;
        check_int("fscan long leading space", neverc_fmt_fscan(tmp, &value), 1);
        check_int("fscan long leading space val", value, 7);
        fclose(tmp);
    }

    tmp = tmpfile();
    check_true("fscan leftover fixture", tmp != NULL);
    if (tmp) {
        fputs("08 9\n", tmp);
        rewind(tmp);
        int a = 0, b = 0;
        check_int("fscan leftover first", neverc_fmt_fscan(tmp, &a), 1);
        check_int("fscan leftover first val", a, 0);
        check_int("fscan leftover second", neverc_fmt_fscan(tmp, &b), 1);
        check_int("fscan leftover second val", b, 8);
        fclose(tmp);
    }

    tmp = tmpfile();
    check_true("fscan long-token leftover fixture", tmp != NULL);
    if (tmp) {
        int i, a = 77, b = 77;
        for (i = 0; i < 127; i++)
            fputc('9', tmp);
        fputs("8 2", tmp);
        rewind(tmp);
        check_int("fscan 128-digit overflow fails", neverc_fmt_fscan(tmp, &a), 0);
        check_int("fscan 128-digit leaves dest", a, 77);
        check_int("fscan leftover after full token", neverc_fmt_fscan(tmp, &b), 1);
        check_int("fscan leftover is 2 not 8", b, 2);
        fclose(tmp);
    }

    tmp = tmpfile();
    check_true("fscanln long space fixture", tmp != NULL);
    if (tmp) {
        char pad[4104];
        memset(pad, ' ', 4096);
        memcpy(pad + 4096, "42\n", 3);
        pad[4099] = '\0';
        fputs(pad, tmp);
        rewind(tmp);
        value = 0;
        check_int("fscanln long leading space",
                  neverc_fmt_fscanln(tmp, "%d", &value), 1);
        check_int("fscanln long leading space val", value, 42);
        fclose(tmp);
    }
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
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
