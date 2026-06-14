/*
 * A/B benchmark + correctness check: fmt Sprintf core.
 *
 *  - old_vsprintf — the previous library engine, reproduced verbatim: it
 *      copies the literal text between verbs one byte at a time through
 *      buf_putc (a capacity check per byte), copies %s output byte by byte,
 *      and formats integers one decimal digit per 64-bit divide.
 *
 *  - neverc_fmt_vsprintf (library) — the new engine: literal runs are located
 *      with memchr and bulk-copied with memcpy, %s output is memcpy'd, and
 *      base-10 integers emit two digits per divide via a 200-entry pair table.
 *      The float path is unchanged (both delegate to strconv), so it is shared.
 *
 * Every case is formatted with both engines from the SAME argument list (via
 * va_copy) and asserted byte-for-byte identical before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/fmt_bench \
 *      tests/neverc/std/fmt_bench.c std/src/fmt/fmt.c \
 *      std/src/strconv/format_float.c std/src/strconv/format_int.c \
 *      std/src/strconv/parse_float.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

#include "neverc/std/fmt.h"
#include "neverc/std/strconv.h"

/* ============================================================
 * OLD engine — verbatim reproduction of the previous fmt core
 * ============================================================ */
typedef struct { char *data; size_t len; size_t cap; } obuf_t;

static void obuf_init(obuf_t *b) { b->cap = 128; b->data = (char *)malloc(b->cap); b->len = 0; }
static void obuf_grow(obuf_t *b, size_t need) {
    while (b->len + need >= b->cap) { b->cap *= 2; b->data = (char *)realloc(b->data, b->cap); }
}
static void obuf_putc(obuf_t *b, char c) { obuf_grow(b, 1); b->data[b->len++] = c; }
static void obuf_puts(obuf_t *b, const char *s, size_t n) {
    obuf_grow(b, n);
    for (size_t i = 0; i < n; i++) b->data[b->len++] = s[i];   /* byte-at-a-time */
}
static void obuf_pad(obuf_t *b, char c, int count) { for (int i = 0; i < count; i++) obuf_putc(b, c); }

static size_t o_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static int o_fmt_int(char *buf, int64_t val, int base, int uppercase) {
    if (val == 0) { buf[0] = '0'; return 1; }
    int neg = 0; uint64_t uval;
    if (val < 0) { neg = 1; uval = (uint64_t)(-val); } else { uval = (uint64_t)val; }
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[72]; int pos = 0;
    while (uval > 0) { tmp[pos++] = digits[uval % base]; uval /= base; }   /* one digit/divide */
    int wi = 0;
    if (neg) buf[wi++] = '-';
    for (int i = pos - 1; i >= 0; i--) buf[wi++] = tmp[i];
    return wi;
}
static int o_fmt_uint(char *buf, uint64_t val, int base, int uppercase) {
    if (val == 0) { buf[0] = '0'; return 1; }
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[72]; int pos = 0;
    while (val > 0) { tmp[pos++] = digits[val % base]; val /= base; }      /* one digit/divide */
    int wi = 0;
    for (int i = pos - 1; i >= 0; i--) buf[wi++] = tmp[i];
    return wi;
}

static uint64_t o_f64_to_bits(double x) { union { double d; uint64_t u; } u; u.d = x; return u.u; }
static int o_f64_isnan(double x) { return x != x; }
/* unchanged float path: delegate to strconv, exactly like the library */
static int o_fmt_float_f(char *buf, size_t cap, double val, int prec) {
    if (prec < 0) prec = 6;
    int n = neverc_strconv_format_float(val, 'f', prec, buf, cap);
    return n < 0 ? 0 : n;
}
static int o_fmt_float_e(char *buf, size_t cap, double val, int prec, int up) {
    if (prec < 0) prec = 6;
    int n = neverc_strconv_format_float(val, up ? 'E' : 'e', prec, buf, cap);
    return n < 0 ? 0 : n;
}
static int o_fmt_float_g(char *buf, size_t cap, double val, int prec, int up) {
    int n = neverc_strconv_format_float(val, up ? 'G' : 'g', prec, buf, cap);
    return n < 0 ? 0 : n;
}

static char *old_vsprintf(const char *format, va_list args) {
    obuf_t buf; obuf_init(&buf);
    size_t flen = o_strlen(format);
    for (size_t i = 0; i < flen; i++) {
        if (format[i] != '%') { obuf_putc(&buf, format[i]); continue; }   /* per-char literal */
        i++;
        if (i >= flen) break;
        int flag_minus = 0, flag_plus = 0, flag_zero = 0, flag_space = 0, flag_hash = 0;
        while (i < flen) {
            if      (format[i] == '-') { flag_minus = 1; i++; }
            else if (format[i] == '+') { flag_plus  = 1; i++; }
            else if (format[i] == '0') { flag_zero  = 1; i++; }
            else if (format[i] == ' ') { flag_space = 1; i++; }
            else if (format[i] == '#') { flag_hash  = 1; i++; }
            else break;
        }
        (void)flag_hash;
        int width = 0, has_width = 0;
        if (i < flen && format[i] == '*') { width = va_arg(args, int); has_width = 1; i++; }
        else { while (i < flen && format[i] >= '0' && format[i] <= '9') { width = width * 10 + (format[i] - '0'); has_width = 1; i++; } }
        int prec = -1;
        if (i < flen && format[i] == '.') {
            i++; prec = 0;
            if (i < flen && format[i] == '*') { prec = va_arg(args, int); i++; }
            else { while (i < flen && format[i] >= '0' && format[i] <= '9') { prec = prec * 10 + (format[i] - '0'); i++; } }
        }
        int is_long = 0, is_longlong = 0;
        if (i < flen && format[i] == 'l') { is_long = 1; i++; if (i < flen && format[i] == 'l') { is_longlong = 1; i++; } }
        (void)is_long;
        if (i >= flen) break;
        char verb = format[i];
        char tmp[512]; int tlen = 0; int is_negative = 0;
        switch (verb) {
        case '%': obuf_putc(&buf, '%'); continue;
        case 'd': case 'i': {
            int64_t val = is_longlong ? va_arg(args, long long) : is_long ? (int64_t)va_arg(args, long) : (int64_t)va_arg(args, int);
            is_negative = val < 0; tlen = o_fmt_int(tmp, val, 10, 0); break;
        }
        case 'u': {
            uint64_t val = is_longlong ? va_arg(args, unsigned long long) : is_long ? (uint64_t)va_arg(args, unsigned long) : (uint64_t)va_arg(args, unsigned int);
            tlen = o_fmt_uint(tmp, val, 10, 0); break;
        }
        case 'x': case 'X': {
            uint64_t val = is_longlong ? va_arg(args, unsigned long long) : is_long ? (uint64_t)va_arg(args, unsigned long) : (uint64_t)va_arg(args, unsigned int);
            tlen = o_fmt_uint(tmp, val, 16, verb == 'X'); break;
        }
        case 'o': {
            uint64_t val = is_longlong ? va_arg(args, unsigned long long) : is_long ? (uint64_t)va_arg(args, unsigned long) : (uint64_t)va_arg(args, unsigned int);
            tlen = o_fmt_uint(tmp, val, 8, 0); break;
        }
        case 'b': {
            uint64_t val = is_longlong ? va_arg(args, unsigned long long) : is_long ? (uint64_t)va_arg(args, unsigned long) : (uint64_t)va_arg(args, unsigned int);
            tlen = o_fmt_uint(tmp, val, 2, 0); break;
        }
        case 'c': { int ch = va_arg(args, int); tmp[0] = (char)ch; tlen = 1; break; }
        case 's': {
            const char *s = va_arg(args, const char *);
            if (!s) s = "(null)";
            size_t slen = o_strlen(s);
            if (prec >= 0 && (size_t)prec < slen) slen = (size_t)prec;
            int pad = (has_width && width > (int)slen) ? width - (int)slen : 0;
            if (!flag_minus) obuf_pad(&buf, ' ', pad);
            obuf_puts(&buf, s, slen);
            if (flag_minus) obuf_pad(&buf, ' ', pad);
            continue;
        }
        case 'f': { double val = va_arg(args, double); tlen = o_fmt_float_f(tmp, sizeof tmp, val, prec); is_negative = (o_f64_to_bits(val) >> 63) && !o_f64_isnan(val); break; }
        case 'e': case 'E': { double val = va_arg(args, double); tlen = o_fmt_float_e(tmp, sizeof tmp, val, prec, verb == 'E'); break; }
        case 'g': case 'G': { double val = va_arg(args, double); tlen = o_fmt_float_g(tmp, sizeof tmp, val, prec, verb == 'G'); break; }
        case 'p': { void *ptr = va_arg(args, void *); tmp[0] = '0'; tmp[1] = 'x'; tlen = 2 + o_fmt_uint(tmp + 2, (uint64_t)(uintptr_t)ptr, 16, 0); break; }
        default: obuf_putc(&buf, '%'); obuf_putc(&buf, verb); continue;
        }
        int prefix_len = 0;
        if (flag_plus && !is_negative && (verb == 'd' || verb == 'i' || verb == 'f')) prefix_len = 1;
        else if (flag_space && !is_negative && (verb == 'd' || verb == 'i')) prefix_len = 1;
        int total = tlen + prefix_len;
        int pad = (has_width && width > total) ? width - total : 0;
        if (!flag_minus && !flag_zero) obuf_pad(&buf, ' ', pad);
        if (flag_plus && !is_negative && (verb == 'd' || verb == 'i' || verb == 'f')) obuf_putc(&buf, '+');
        else if (flag_space && !is_negative && (verb == 'd' || verb == 'i')) obuf_putc(&buf, ' ');
        if (!flag_minus && flag_zero) obuf_pad(&buf, '0', pad);
        obuf_puts(&buf, tmp, tlen);
        if (flag_minus) obuf_pad(&buf, ' ', pad);
    }
    obuf_putc(&buf, '\0');
    return buf.data;
}

/* ============================================================
 * Harness
 * ============================================================ */
static double now_sec(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static volatile size_t sink;
static int fail_count;

static char *make_repeat(const char *pattern, size_t target_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    return s;
}

/* Format `format` with both engines from the same args, assert identical, time. */
static void run_case(const char *label, const char *format, ...) {
    va_list ap, ap2;
    va_start(ap, format); va_copy(ap2, ap);
    char *o = old_vsprintf(format, ap);
    char *n = neverc_fmt_vsprintf(format, ap2);
    va_end(ap); va_end(ap2);

    size_t olen = o ? strlen(o) : 0, nlen = n ? strlen(n) : 0;
    if (!o || !n || olen != nlen || memcmp(o, n, olen) != 0) {
        printf("%-14s  CORRECTNESS FAIL\n", label);
        if (o) printf("   old[%zu]=\"%.80s\"\n", olen, o);
        if (n) printf("   new[%zu]=\"%.80s\"\n", nlen, n);
        fail_count++;
        free(o); free(n);
        return;
    }
    size_t out_len = nlen;
    free(o); free(n);

    int iters = (int)(40000000 / (out_len + 1)); if (iters < 1000) iters = 1000;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { va_start(ap, format); char *q = old_vsprintf(format, ap); va_end(ap); sink = (size_t)q[0]; free(q); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { va_start(ap, format); char *q = neverc_fmt_vsprintf(format, ap); va_end(ap); sink = (size_t)q[0]; free(q); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-14s  %8.1f ms  %8.1f ms  %6.2fx   (-> %zu B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, out_len);
}

/* Pure correctness probe (not timed) for tricky / edge inputs. */
static void eq_case(const char *format, ...) {
    va_list ap, ap2;
    va_start(ap, format); va_copy(ap2, ap);
    char *o = old_vsprintf(format, ap);
    char *n = neverc_fmt_vsprintf(format, ap2);
    va_end(ap); va_end(ap2);
    size_t olen = o ? strlen(o) : 0, nlen = n ? strlen(n) : 0;
    if (!o || !n || olen != nlen || memcmp(o, n, olen) != 0) {
        printf("  EDGE FAIL: fmt=\"%s\"  old=\"%s\"  new=\"%s\"\n", format, o ? o : "(null)", n ? n : "(null)");
        fail_count++;
    }
    free(o); free(n);
}

int main(void) {
    printf("=== fmt Sprintf: memchr/memcpy + 2-digit ints (new) vs per-byte (old) ===\n");
    printf("%-14s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    /* Literal-heavy: long run of plain text with no verbs. */
    char *lit = make_repeat("The quick brown fox jumps over the lazy dog. ", 4096);
    run_case("literal_4k", lit);
    free(lit);

    /* Single big %s (exercises the bulk %s memcpy). */
    char *big = make_repeat("payload-", 4096);
    run_case("one_big_str", "%s", big);
    free(big);

    /* Many medium %s joined by literal separators. */
    run_case("str_join", "%s/%s/%s/%s/%s/%s",
             "alpha", "bravo", "charlie", "delta", "echo", "foxtrot");

    /* Integer-heavy, small magnitudes. */
    run_case("ints_small", "%d %d %d %d %d %d %d %d",
             1, 22, 333, 7, 89, 4, 56, 0);

    /* Integer-heavy, large 64-bit magnitudes (more digits = bigger 2-digit win). */
    run_case("ints_big", "%lld %lld %lld %lld",
             1234567890123456789LL, 9876543210LL, -5555555555555LL, 42LL);

    /* Hex (base 16, unchanged path) — guards against regression. */
    run_case("hex", "%x %x %x %x", 0xdeadbeefu, 0x1234u, 0xfu, 0u);

    /* Realistic mixed line: literal + %s + %d (e.g. an HTTP request line). */
    run_case("http_line",
             "GET %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\nConnection: %s\r\n\r\n",
             "/api/v1/resource?id=12345", "example.com", 4096, "keep-alive");

    printf("\n");
    /* Edge / corner correctness (not timed). */
    eq_case("");
    eq_case("no verbs here");
    eq_case("trailing percent %% sign");
    eq_case("%d", 0);
    eq_case("%d", -1);
    eq_case("%d", 2147483647);
    eq_case("%d", -2147483648);
    eq_case("%u", 4294967295u);
    eq_case("%lld", 9223372036854775807LL);
    eq_case("%lld", -9223372036854775807LL);
    eq_case("%x %X", 0xabcdef12u, 0xABCDEF12u);
    eq_case("%o %b", 0755u, 5u);
    eq_case("%5d|%-5d|%05d", 42, 42, 42);
    eq_case("%+d % d", 7, 7);
    eq_case("%.3s|%10s|%-10s|", "truncated", "right", "left");
    eq_case("%c%c%c", 'a', 'b', 'c');
    eq_case("%f %e %g", 3.14159, 12345.678, 0.0001);
    eq_case("%.2f|%+.1f", 2.5, -2.5);
    eq_case("100%% done: %d/%d items, %s", 50, 100, "ok");
    eq_case("%d%s%d%s%d", 1, "-", 2, "-", 3);
    if (fail_count == 0) printf("edge cases: all identical\n");
    else printf("edge cases: %d FAILED\n", fail_count);

    printf("\n=== Done ===\n");
    return fail_count == 0 ? 0 : 1;
}
