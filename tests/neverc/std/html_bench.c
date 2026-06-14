/*
 * A/B benchmark + correctness check: html EscapeString / UnescapeString.
 *
 *  - old_escape / old_unescape — the previous library routines, reproduced
 *      verbatim: escape walks one byte at a time through a switch, growing the
 *      output with per-byte bounds checks and realloc; unescape probes for an
 *      entity at every single byte.
 *
 *  - neverc_html_escape_string / _unescape_string (library) — the new
 *      routines: escape sizes the output exactly in one counting pass (no
 *      realloc) and bulk-copies runs of self-representing bytes with memcpy;
 *      unescape uses memchr to jump to the next '&' and bulk-copies the run
 *      before it. Entity decoding itself is unchanged.
 *
 * Both fast paths are behavior-preserving, so every case asserts the new
 * output is byte-for-byte identical to the old output before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/html_bench \
 *      tests/neverc/std/html_bench.c std/src/html/html.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/html.h"

/* ============================================================
 * OLD routines — verbatim reproduction of the previous library
 * ============================================================ */
static char *old_escape(const char *s, size_t *outlen) {
    size_t slen = strlen(s);
    size_t cap = slen * 2;
    char *r = (char *)malloc(cap + 1);
    if (!r) { *outlen = 0; return NULL; }

    size_t wi = 0;
    for (size_t i = 0; i < slen; i++) {
        const char *esc = NULL;
        size_t elen = 0;
        switch (s[i]) {
            case '&':  esc = "&amp;";  elen = 5; break;
            case '<':  esc = "&lt;";   elen = 4; break;
            case '>':  esc = "&gt;";   elen = 4; break;
            case '"':  esc = "&#34;";  elen = 5; break;
            case '\'': esc = "&#39;";  elen = 5; break;
            default: break;
        }
        if (esc) {
            if (wi + elen >= cap) {
                cap = (wi + elen) * 2;
                r = (char *)realloc(r, cap + 1);
            }
            for (size_t j = 0; j < elen; j++) r[wi++] = esc[j];
        } else {
            if (wi + 1 >= cap) {
                cap *= 2;
                r = (char *)realloc(r, cap + 1);
            }
            r[wi++] = s[i];
        }
    }
    r[wi] = '\0';
    *outlen = wi;
    return r;
}

static int o_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

static char *old_unescape(const char *s, size_t *outlen) {
    size_t slen = strlen(s);
    char *r = (char *)malloc(slen + 1);
    if (!r) { *outlen = 0; return NULL; }

    size_t wi = 0, i = 0;
    while (i < slen) {
        if (s[i] == '&') {
            if (o_starts_with(s + i, "&amp;"))  { r[wi++] = '&';  i += 5; continue; }
            if (o_starts_with(s + i, "&lt;"))   { r[wi++] = '<';  i += 4; continue; }
            if (o_starts_with(s + i, "&gt;"))   { r[wi++] = '>';  i += 4; continue; }
            if (o_starts_with(s + i, "&#34;"))  { r[wi++] = '"';  i += 5; continue; }
            if (o_starts_with(s + i, "&quot;")) { r[wi++] = '"';  i += 6; continue; }
            if (o_starts_with(s + i, "&#39;"))  { r[wi++] = '\''; i += 5; continue; }
            if (o_starts_with(s + i, "&apos;")) { r[wi++] = '\''; i += 6; continue; }

            if (o_starts_with(s + i, "&#")) {
                i += 2;
                int base = 10;
                if (i < slen && (s[i] == 'x' || s[i] == 'X')) {
                    base = 16; i++;
                }
                unsigned long val = 0;
                while (i < slen && s[i] != ';') {
                    char c = s[i];
                    if (base == 16) {
                        if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
                        else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
                        else break;
                    } else {
                        if (c >= '0' && c <= '9') val = val * 10 + (c - '0');
                        else break;
                    }
                    i++;
                }
                if (i < slen && s[i] == ';') i++;
                if (val < 128) r[wi++] = (char)val;
                else r[wi++] = '?';
                continue;
            }
        }
        r[wi++] = s[i++];
    }
    r[wi] = '\0';
    *outlen = wi;
    return r;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile size_t sink;

static char *make_repeat(const char *pattern, size_t target_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    return s;
}

typedef char *(*escfn)(const char *, size_t *);

static void bench_case(const char *label, const char *input,
                       escfn oldf, escfn newf) {
    size_t in_len = strlen(input);
    size_t ol = 0, nl = 0;
    char *o = oldf(input, &ol);
    char *n = newf(input, &nl);
    if (!o || !n || ol != nl || memcmp(o, n, ol) != 0) {
        printf("%-18s  CORRECTNESS FAIL\n", label);
        free(o); free(n); return;
    }
    size_t out_len = nl;
    free(o); free(n);

    int iters = (int)(200000000 / (in_len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { size_t L; char *q = oldf(input, &L); sink = (size_t)q[0]; free(q); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { size_t L; char *q = newf(input, &L); sink = (size_t)q[0]; free(q); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %zu B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, in_len, out_len);
}

/* Extra edge-case correctness coverage beyond the timed cases. */
static int eq(escfn a, escfn b, const char *in) {
    size_t la = 0, lb = 0;
    char *x = a(in, &la), *y = b(in, &lb);
    int ok = x && y && la == lb && memcmp(x, y, la) == 0;
    if (!ok) printf("  EDGE FAIL: in=\"%s\"\n", in);
    free(x); free(y);
    return ok;
}

static void correctness_extra(void) {
    const char *cases[] = {
        "", "a", "&", "<", ">", "\"", "'",
        "&&&", "<<<", "a<b>c&d\"e'f",
        "no specials at all here",
        "&amp;&lt;&gt;&#34;&#39;&quot;&apos;",
        "&#65;&#x42;&#;&#x;&#999;",
        "lone & ampersand", "trailing &", "&unknown; entity",
        "mix &amp; <tag> \"q\" 'a' & done",
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    int ok_e = 0, ok_u = 0;
    for (int i = 0; i < n; i++) {
        ok_e += eq(old_escape, neverc_html_escape_string, cases[i]);
        ok_u += eq(old_unescape, neverc_html_unescape_string, cases[i]);
    }
    printf("edge cases: escape %d/%d, unescape %d/%d identical\n", ok_e, n, ok_u, n);
}

int main(void) {
    printf("=== html EscapeString: count+bulk-copy (new) vs per-byte switch (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    char *plain = make_repeat("The quick brown fox jumps over the lazy dog. ", 4096);
    bench_case("esc_plain", plain, old_escape, neverc_html_escape_string);

    char *light = make_repeat("Tom & Jerry went to <the park> at 5 o'clock. ", 4096);
    bench_case("esc_light", light, old_escape, neverc_html_escape_string);

    char *heavy = make_repeat("<a href=\"x\">A&B</a> ", 4096);
    bench_case("esc_heavy", heavy, old_escape, neverc_html_escape_string);

    free(plain); free(light); free(heavy);

    printf("\n=== html UnescapeString: memchr bulk-copy (new) vs per-byte probe (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    char *uplain = make_repeat("The quick brown fox jumps over the lazy dog. ", 4096);
    bench_case("unesc_plain", uplain, old_unescape, neverc_html_unescape_string);

    char *ulight = make_repeat("Tom &amp; Jerry &lt;3 the &gt; sign. ", 4096);
    bench_case("unesc_light", ulight, old_unescape, neverc_html_unescape_string);

    char *uheavy = make_repeat("&lt;&gt;&amp;&#34;&#39;", 4096);
    bench_case("unesc_heavy", uheavy, old_unescape, neverc_html_unescape_string);

    free(uplain); free(ulight); free(uheavy);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
