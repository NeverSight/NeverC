/*
 * A/B benchmark + correctness check: strconv Unquote.
 *
 *  - old_unquote — the previous library unquoter, reproduced verbatim. It
 *      calls neverc_strconv_unquote_char (an unchanged public helper) and
 *      re-encodes one rune at a time, even for plain ASCII that decodes to
 *      itself.
 *
 *  - neverc_strconv_unquote (library) — the new unquoter: same slow path, but
 *      a fast path first bulk-copies runs of plain single-byte characters
 *      (ASCII, not a backslash escape or the quote char) with one memcpy.
 *
 * The fast path is behavior-preserving, so every case asserts the new output
 * (length + bytes) is identical to the old output before timing.
 *
 * Build (macOS dead-strip drops the unused strconv<->math deps of quote.c):
 *   cc -O2 -std=c11 -I std/include -Wl,-dead_strip -o /tmp/unquote_bench \
 *      tests/neverc/std/unquote_bench.c std/src/strconv/quote.c \
 *      std/src/unicode/utf8/utf8.c std/src/unicode/unicode.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/strconv.h"
#include "neverc/std/unicode/utf8.h"

/* ============================================================
 * OLD unquoter — verbatim reproduction of the previous library
 * (neverc_strconv_unquote_char is a public helper, unchanged)
 * ============================================================ */
static int old_unquote(const char *s, char *buf, size_t bufsize) {
    if (!s || !buf || bufsize == 0) return -1;
    size_t slen = strlen(s);
    if (slen < 2) return -1;

    char quote = s[0];
    if ((quote != '"' && quote != '\'' && quote != '`') || s[slen - 1] != quote)
        return -1;

    if (quote == '`') {
        size_t inner = slen - 2;
        if (inner + 1 > bufsize) return -1;
        memcpy(buf, s + 1, inner);
        buf[inner] = '\0';
        return (int)inner;
    }

    const char *src = s + 1;
    size_t src_len = slen - 2;
    size_t out = 0;
    while (src_len > 0) {
        uint32_t r;
        int mb;
        int consumed = neverc_strconv_unquote_char(src, src_len, quote, &r, &mb);
        if (consumed < 0) return -1;
        uint8_t enc[4];
        int n = neverc_utf8_encode_rune(enc, r);
        if (out + (size_t)n >= bufsize) return -1;
        memcpy(buf + out, enc, (size_t)n);
        out += (size_t)n;
        src += consumed;
        src_len -= (size_t)consumed;
    }
    buf[out] = '\0';
    return (int)out;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile int sink;

static char *make_repeat(const char *pattern, size_t target_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    return s;
}

/* Wrap inner text (repeated to ~target) in double quotes -> unquote input. */
static char *make_quoted(const char *inner_pattern, size_t target_len) {
    char *inner = make_repeat(inner_pattern, target_len);
    size_t il = strlen(inner);
    char *q = (char *)malloc(il + 3);
    q[0] = '"';
    memcpy(q + 1, inner, il);
    q[il + 1] = '"';
    q[il + 2] = '\0';
    free(inner);
    return q;
}

static void bench_case(const char *label, const char *input) {
    size_t in_len = strlen(input);
    size_t cap = in_len + 1;
    char *ob = (char *)malloc(cap), *nb = (char *)malloc(cap);

    int ro = old_unquote(input, ob, cap);
    int rn = neverc_strconv_unquote(input, nb, cap);
    if (ro != rn || ro < 0 || memcmp(ob, nb, (size_t)rn) != 0) {
        printf("%-18s  CORRECTNESS FAIL (ro=%d rn=%d)\n", label, ro, rn);
        free(ob); free(nb); return;
    }

    int iters = (int)(200000000 / (in_len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = old_unquote(input, ob, cap); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = neverc_strconv_unquote(input, nb, cap); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %d B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, in_len, rn);
    free(ob); free(nb);
}

static void correctness_extra(void) {
    const char *cases[] = {
        "\"\"", "\"a\"", "\"~\"", "\" \"",
        "\"\\n\"", "\"\\t\\\"\\\\\"",     /* \n  and  \t \" \\ */
        "\"\\x41\\x42\"",                  /* \x41\x42 -> AB */
        "\"\\u4e16\\u754c\"",             /* \u4e16\u754c -> 世界 */
        "'\\''",                          /* single-quoted escaped apostrophe */
        "`raw\\nstring`",                 /* backquoted: no escape processing */
        "\"caf\xc3\xa9 \xe4\xb8\x96\"",    /* embedded UTF-8 */
        "\"plain text with spaces\"",
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    int ok = 0;
    for (int i = 0; i < n; i++) {
        char ob[256], nb[256];
        int ro = old_unquote(cases[i], ob, sizeof(ob));
        int rn = neverc_strconv_unquote(cases[i], nb, sizeof(nb));
        if (ro == rn && (ro < 0 || memcmp(ob, nb, (size_t)rn) == 0)) ok++;
        else printf("  EDGE FAIL [%d]: ro=%d rn=%d\n", i, ro, rn);
    }
    printf("edge cases: %d/%d identical\n", ok, n);
}

int main(void) {
    printf("=== strconv Unquote: ASCII fast-path bulk-copy (new) vs per-byte decode (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    char *plain = make_quoted("The quick brown fox jumps over the lazy dog ", 4096);
    bench_case("ascii_plain", plain);

    char *escapes = make_quoted("key=\\\"val\\\" path=/x/y n=42 ", 4096);
    bench_case("ascii_light_esc", escapes);

    char *heavy = make_quoted("\\n\\t\\\"\\\\", 4096);
    bench_case("ascii_heavy_esc", heavy);

    char *utf8 = make_quoted("caf\xc3\xa9 \xe4\xb8\x96\xe7\x95\x8c ", 4096);
    bench_case("utf8_mixed", utf8);

    free(plain); free(escapes); free(heavy); free(utf8);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
