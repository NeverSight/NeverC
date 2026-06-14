/*
 * A/B benchmark + correctness check: regexp QuoteMeta.
 *
 *  - old_quote_meta — the previous library routine, reproduced verbatim: it
 *      walks one byte at a time and runs a 14-way `c == X || ...` comparison
 *      chain for every byte (every ordinary byte pays the full chain because
 *      none of the disjuncts short-circuit), writing output a byte at a time.
 *
 *  - neverc_regexp_quote_meta (library) — the new routine: a 256-entry lookup
 *      replaces the comparison chain with a single load. The leading run of
 *      ordinary bytes is bulk-copied with one memcpy (so special-free input is
 *      just a scan plus a copy); past the first special byte it writes
 *      branchlessly (emit '\\', advance over it only for specials) so
 *      metacharacter-dense tails never mispredict. The result is a strict win
 *      across plain, mixed, and special-heavy inputs with no regression.
 *
 * The fast path is behaviour-preserving, so every case asserts the new output
 * is byte-for-byte identical to the old output before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/regexp_quotemeta_bench \
 *      tests/neverc/std/regexp_quotemeta_bench.c std/src/regexp/regexp.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/regexp.h"

/* ============================================================
 * OLD routine — verbatim reproduction of the previous library
 * ============================================================ */
static char *old_quote_meta(const char *s) {
    if (!s) return NULL;
    size_t slen = strlen(s);
    char *result = (char *)malloc(slen * 2 + 1);
    if (!result) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < slen; i++) {
        char c = s[i];
        if (c == '\\' || c == '.' || c == '+' || c == '*' || c == '?' ||
            c == '(' || c == ')' || c == '|' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '^' || c == '$') {
            result[j++] = '\\';
        }
        result[j++] = c;
    }
    result[j] = '\0';
    return result;
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

typedef char *(*qmfn)(const char *);

static void bench_case(const char *label, const char *input,
                       qmfn oldf, qmfn newf) {
    size_t in_len = strlen(input);
    char *o = oldf(input);
    char *n = newf(input);
    size_t ol = o ? strlen(o) : 0, nl = n ? strlen(n) : 0;
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
        for (int i = 0; i < iters; i++) { char *q = oldf(input); sink = (size_t)q[0]; free(q); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { char *q = newf(input); sink = (size_t)q[0]; free(q); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %zu B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, in_len, out_len);
}

/* Extra edge-case correctness coverage beyond the timed cases. */
static int eq(qmfn a, qmfn b, const char *in) {
    char *x = a(in), *y = b(in);
    size_t la = x ? strlen(x) : 0, lb = y ? strlen(y) : 0;
    int ok = x && y && la == lb && memcmp(x, y, la) == 0;
    if (!ok) printf("  EDGE FAIL: in=\"%s\"\n", in);
    free(x); free(y);
    return ok;
}

static void correctness_extra(void) {
    const char *cases[] = {
        "", "a", "\\", ".", "+", "*", "?", "(", ")", "|",
        "[", "]", "{", "}", "^", "$",
        "a.b+c*d?e", "[foo](bar){baz}", "^start|end$",
        "no specials at all here",
        "\\\\\\", "....", "a\\b.c+d",
        "mix .* and (literal) text $end^",
        "tab\tand newline\nhere", "unicode: \xc3\xa9\xe4\xb8\xad",
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    int ok = 0;
    for (int i = 0; i < n; i++)
        ok += eq(old_quote_meta, neverc_regexp_quote_meta, cases[i]);
    printf("edge cases: %d/%d identical\n", ok, n);
}

int main(void) {
    printf("=== regexp QuoteMeta: LUT + bulk-copy (new) vs 14-way compare chain (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    char *plain = make_repeat("the quick brown fox jumps over the lazy dog ", 4096);
    bench_case("qm_plain", plain, old_quote_meta, neverc_regexp_quote_meta);

    char *light = make_repeat("file name (v2.1) [draft] ", 4096);
    bench_case("qm_light", light, old_quote_meta, neverc_regexp_quote_meta);

    char *heavy = make_repeat("a.b+c*d?e(f)g|h[i]", 4096);
    bench_case("qm_heavy", heavy, old_quote_meta, neverc_regexp_quote_meta);

    free(plain); free(light); free(heavy);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
