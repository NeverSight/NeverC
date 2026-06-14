/*
 * A/B benchmark + correctness check: html/template context escapers.
 *
 *  - old_* — the previous library routines, reproduced verbatim: each walks one
 *    byte at a time appending through buf_append (grow-by-doubling realloc), and
 *    css/url call snprintf per escaped byte.
 *
 *  - neverc_html_* (library) — the new routines: html/js size the output with a
 *    branchless counting pass then copy in a single read (constant-size entity
 *    memcpy); css/url bulk-copy unescaped runs with memcpy and format hex by
 *    hand instead of snprintf.
 *
 * Each case asserts the new output is byte-for-byte identical to the old output
 * before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/tpl_bench \
 *      tests/neverc/std/template_escape_bench.c std/src/html/template/template.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/html/template.h"

static int o_isalnum(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/* ============================================================
 * OLD routines — verbatim reproduction of the previous library
 * ============================================================ */
static void buf_append(char **buf, size_t *len, size_t *cap,
                       const char *s, size_t slen) {
    while (*len + slen + 1 > *cap) { *cap *= 2; *buf = (char *)realloc(*buf, *cap); }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

static char *old_html_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = 0, cap = strlen(s) * 2 + 16;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '&':  buf_append(&buf, &len, &cap, "&amp;", 5); break;
        case '<':  buf_append(&buf, &len, &cap, "&lt;", 4); break;
        case '>':  buf_append(&buf, &len, &cap, "&gt;", 4); break;
        case '"':  buf_append(&buf, &len, &cap, "&#34;", 5); break;
        case '\'': buf_append(&buf, &len, &cap, "&#39;", 5); break;
        default:   buf_append(&buf, &len, &cap, p, 1); break;
        }
    }
    return buf;
}

static char *old_js_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = 0, cap = strlen(s) * 2 + 16;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '\\': buf_append(&buf, &len, &cap, "\\\\", 2); break;
        case '\'': buf_append(&buf, &len, &cap, "\\'", 2); break;
        case '"':  buf_append(&buf, &len, &cap, "\\\"", 2); break;
        case '\n': buf_append(&buf, &len, &cap, "\\n", 2); break;
        case '\r': buf_append(&buf, &len, &cap, "\\r", 2); break;
        case '<':  buf_append(&buf, &len, &cap, "\\u003c", 6); break;
        case '>':  buf_append(&buf, &len, &cap, "\\u003e", 6); break;
        case '&':  buf_append(&buf, &len, &cap, "\\u0026", 6); break;
        default:   buf_append(&buf, &len, &cap, p, 1); break;
        }
    }
    return buf;
}

static char *old_css_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = 0, cap = strlen(s) * 6 + 16;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    for (const char *p = s; *p; p++) {
        if (o_isalnum((unsigned char)*p) || *p == '-' || *p == '_') {
            buf_append(&buf, &len, &cap, p, 1);
        } else {
            char esc[12];
            snprintf(esc, sizeof(esc), "\\%02X", (unsigned char)*p);
            buf_append(&buf, &len, &cap, esc, strlen(esc));
        }
    }
    return buf;
}

static char *old_url_query_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = 0, cap = strlen(s) * 3 + 16;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    for (const char *p = s; *p; p++) {
        if (o_isalnum((unsigned char)*p) || *p == '-' || *p == '_' ||
            *p == '.' || *p == '~') {
            buf_append(&buf, &len, &cap, p, 1);
        } else {
            char esc[4];
            snprintf(esc, sizeof(esc), "%%%02X", (unsigned char)*p);
            buf_append(&buf, &len, &cap, esc, 3);
        }
    }
    return buf;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile uint64_t sink;

static char *make_repeat(const char *pattern, size_t target_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    return s;
}

typedef char *(*efn)(const char *);

static void bench_case(const char *label, const char *input, efn oldf, efn newf) {
    size_t in_len = strlen(input);
    char *o = oldf(input), *n = newf(input);
    if (!o || !n || strcmp(o, n) != 0) {
        printf("%-20s  CORRECTNESS FAIL\n", label);
        if (o && n) printf("    old=\"%.40s\"\n    new=\"%.40s\"\n", o, n);
        free(o); free(n); return;
    }
    size_t out_len = strlen(n);
    free(o); free(n);

    int iters = (int)(150000000 / (in_len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { char *q = oldf(input); sink = (uint64_t)q[0]; free(q); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { char *q = newf(input); sink = (uint64_t)q[0]; free(q); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-20s  %8.1f ms  %8.1f ms  %6.2fx   (%zu -> %zu B)\n",
           label, t_old*1000, t_new*1000, t_old/t_new, in_len, out_len);
}

static void correctness_extra(void) {
    const char *cases[] = {
        "", "a", "&<>\"'", "plain text here", "a & b < c",
        "hello \"world\" <script>", "line1\nline2\tend",
        "name=value&x=1 2", "path/to/file.ext?q=a b",
        "\x01\x02\x7f\x80\xff weird", "100% sure",
    };
    int n = (int)(sizeof(cases)/sizeof(cases[0]));
    int ok = 0, total = 0;
    for (int i = 0; i < n; i++) {
        char *a, *b;
        a = old_html_escape(cases[i]);      b = neverc_html_escape(cases[i]);      total++; if (a&&b&&!strcmp(a,b)) ok++; else printf("  html FAIL [%d]\n", i); free(a); free(b);
        a = old_js_escape(cases[i]);        b = neverc_html_js_escape(cases[i]);   total++; if (a&&b&&!strcmp(a,b)) ok++; else printf("  js   FAIL [%d]\n", i); free(a); free(b);
        a = old_css_escape(cases[i]);       b = neverc_html_css_escape(cases[i]);  total++; if (a&&b&&!strcmp(a,b)) ok++; else printf("  css  FAIL [%d]\n", i); free(a); free(b);
        a = old_url_query_escape(cases[i]); b = neverc_html_url_query_escape(cases[i]); total++; if (a&&b&&!strcmp(a,b)) ok++; else printf("  url  FAIL [%d]\n", i); free(a); free(b);
    }
    printf("edge cases: %d/%d identical\n", ok, total);
}

int main(void) {
    printf("=== html/template escapers: new vs old (per-byte + snprintf) ===\n");
    printf("%-20s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    char *plain = make_repeat("The quick brown fox jumps over the lazy dog. ", 4096);
    char *htmlish = make_repeat("Tom & Jerry <ran> said \"go\" it's 5. ", 4096);
    char *urlish = make_repeat("name=value & x=1 2 / path?q ", 4096);

    bench_case("html_escape/plain", plain, old_html_escape, neverc_html_escape);
    bench_case("html_escape/mixed", htmlish, old_html_escape, neverc_html_escape);
    bench_case("js_escape/plain", plain, old_js_escape, neverc_html_js_escape);
    bench_case("js_escape/mixed", htmlish, old_js_escape, neverc_html_js_escape);
    bench_case("css_escape/plain", plain, old_css_escape, neverc_html_css_escape);
    bench_case("css_escape/mixed", urlish, old_css_escape, neverc_html_css_escape);
    bench_case("url_query/plain", plain, old_url_query_escape, neverc_html_url_query_escape);
    bench_case("url_query/mixed", urlish, old_url_query_escape, neverc_html_url_query_escape);

    free(plain); free(htmlish); free(urlish);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
