/*
 * A/B benchmark + correctness check: net/url percent-encoding (escape).
 *
 *  - old_*_escape  — the previous library encoder, reproduced verbatim: a
 *      per-byte loop that calls should_escape() through a function pointer
 *      (un-inlinable indirect call + a chain of comparisons per byte) and
 *      stores one byte at a time.
 *
 *  - neverc_url_query_escape / neverc_url_path_escape (library) — the new
 *      encoder: a precomputed per-mode escape table, so the per-byte decision
 *      is a single table load instead of an indirect call into should_escape().
 *
 * The optimization is behavior-preserving, so every case asserts the new
 * output is byte-for-byte identical to the old output (and same length)
 * before timing. The A/B speedup therefore isolates exactly the table +
 * run-copy change.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/net/url.h"

/* ============================================================
 * OLD encoder — verbatim reproduction of the previous library
 * ============================================================ */
static int o_isalnum(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
static int o_should_escape_path(unsigned char c) {
    if (o_isalnum(c)) return 0;
    /* Go url.PathEscape / encodePathSegment. */
    if (c == '-' || c == '_' || c == '.' || c == '~' ||
        c == '$' || c == '&' || c == '+' || c == ':' ||
        c == '=' || c == '@')
        return 0;
    return 1;
}
static int o_should_escape_query(unsigned char c) {
    if (o_isalnum(c)) return 0;
    if (c == '-' || c == '_' || c == '.' || c == '~') return 0;
    return 1;
}
static int o_percent_encode(const char *s, char *buf, size_t cap,
                            int (*should_escape)(unsigned char)) {
    size_t si = 0, di = 0;
    while (s[si] && di < cap - 1) {
        unsigned char c = (unsigned char)s[si];
        if (should_escape(c)) {
            if (di + 3 > cap - 1) break;
            buf[di++] = '%';
            buf[di++] = "0123456789ABCDEF"[c >> 4];
            buf[di++] = "0123456789ABCDEF"[c & 0x0F];
        } else {
            buf[di++] = (char)c;
        }
        si++;
    }
    buf[di] = '\0';
    return (int)di;
}
static int old_path_escape(const char *s, char *buf, size_t cap) {
    return o_percent_encode(s, buf, cap, o_should_escape_path);
}
static int old_query_escape(const char *s, char *buf, size_t cap) {
    return o_percent_encode(s, buf, cap, o_should_escape_query);
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile int sink;

/* Build a ~target_len string by repeating `pattern`. Caller frees. */
static char *make_repeat(const char *pattern, size_t target_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    return s;
}

typedef int (*esc_fn)(const char *, char *, size_t);

static void bench_case(const char *label, const char *input,
                       esc_fn old_fn, esc_fn new_fn) {
    size_t in_len = strlen(input);
    size_t cap = in_len * 3 + 16;
    char *ob = (char *)malloc(cap), *nb = (char *)malloc(cap);

    int ro = old_fn(input, ob, cap);
    int rn = new_fn(input, nb, cap);
    if (ro != rn || strcmp(ob, nb) != 0) {
        printf("%-18s  CORRECTNESS FAIL (ro=%d rn=%d)\n", label, ro, rn);
        free(ob); free(nb); return;
    }

    int iters = (int)(400000000 / (in_len + 1)); if (iters < 1000) iters = 1000;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = old_fn(input, ob, cap); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = new_fn(input, nb, cap); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %d B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, in_len, rn);
    free(ob); free(nb);
}

int main(void) {
    printf("=== net/url percent-encode: escape table (new) vs func-ptr per byte (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    /* All-safe alphanumeric: best case, entirely memcpy runs. */
    char *alnum = make_repeat("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 4096);
    bench_case("query_allsafe", alnum, old_query_escape, neverc_url_query_escape);

    /* Realistic query: mostly safe with embedded spaces (one escape per word). */
    char *qtyp = make_repeat("search=hello world&category=books&author=jane doe&year=2024&", 4096);
    bench_case("query_typical", qtyp, old_query_escape, neverc_url_query_escape);

    /* Escape-heavy: many bytes need %XX (worst case for the encoder). */
    char *heavy = make_repeat("a b & c = d / e ? f # g + h", 4096);
    bench_case("query_heavy", heavy, old_query_escape, neverc_url_query_escape);

    /* Path mode: '/' ':' '@' stay literal; spaces escape. */
    char *ptyp = make_repeat("/api/v1/users/john doe/files/report final.pdf:section@2/", 4096);
    bench_case("path_typical", ptyp, old_path_escape, neverc_url_path_escape);

    free(alnum); free(qtyp); free(heavy); free(ptyp);
    printf("\n=== Done ===\n");
    return 0;
}
