/*
 * A/B benchmark: LCP-LR accelerated suffix-array search (new) vs the previous
 * compare-from-zero binary search (old).
 *
 * Both variants run over the *same* SA-IS-built suffix array, so this isolates
 * the query cost. The old path re-compares the pattern from byte 0 at each of
 * the log n probes -> O(m log n) (the same naive search Go's suffixarray uses).
 * The new path uses precomputed LCP-LR arrays (Manber & Myers): each probe is
 * decided either with no character comparisons or by resuming from max(l, r),
 * and the matched prefix only grows, giving O(m + log n) total. Crucially it
 * never re-scans a shared prefix, so unlike a "track min(l,r)" shortcut (which
 * collapses to a slow scalar re-scan and *loses* to SIMD memcmp when the answer
 * sits near an end), it never regresses on adversarial low-entropy inputs.
 *
 * Measured A/B (-O2, Apple clang, count() query):
 *   case                       n        m      old ms    new ms   speedup
 *   random a-z (hit)           200000   8      735.4      163.0     4.5x
 *   DNA {ACGT} (hit)           500000   40     358.2      115.1     3.1x
 *   binary {0,1} (hit)         500000   128    191.0       86.2     2.2x
 *   periodic-7 (hit)           500000   200    229.7      140.4     1.6x
 *   all-'a', a^1023.b (miss)   500000   1024   260.6      223.4     1.2x
 *   all-'a', a^1024 (hit)      500000   1024   122.0      120.6     1.0x
 * (the simple min(l,r) variant scored 0.37x / 0.09x / 0.12x on the last three
 *  — the LCP-LR version turns those regressions into parity-or-better.)
 *
 * Build standalone:
 *   cc -O2 -I std/include suffixarray_bench.c \
 *      std/src/index/suffixarray/suffixarray.c -o suffixarray_bench
 */
#include "neverc/std/index/suffixarray.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static volatile size_t sink;

/* ---- OLD: compare-from-zero binary search (reproduced verbatim) ---- */
__attribute__((noinline))
static int old_suffix_cmp(const unsigned char *data, size_t dlen, int32_t pos,
                          const unsigned char *pat, size_t plen) {
    size_t rem = dlen - (size_t)pos;
    size_t clen = rem < plen ? rem : plen;
    int r = memcmp(data + pos, pat, clen);
    if (r != 0) return r;
    if (clen < plen) return -1;
    return 0;
}

__attribute__((noinline))
static size_t old_count(const neverc_suffixarray_t *idx,
                        const unsigned char *pat, size_t plen) {
    if (!idx->sa || idx->sa_len == 0 || plen == 0) return 0;
    int32_t lo = 0, hi = (int32_t)idx->sa_len;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if (old_suffix_cmp(idx->data, idx->data_len, idx->sa[mid], pat, plen) < 0)
            lo = mid + 1;
        else hi = mid;
    }
    int32_t lower = lo;
    lo = 0; hi = (int32_t)idx->sa_len;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        if (old_suffix_cmp(idx->data, idx->data_len, idx->sa[mid], pat, plen) <= 0)
            lo = mid + 1;
        else hi = mid;
    }
    return (size_t)(lo - lower);
}

static void run_case(const char *label, const unsigned char *text, size_t n,
                     const unsigned char *pat, size_t m, int iters) {
    neverc_suffixarray_t idx;
    if (neverc_suffixarray_new(&idx, text, n) != 0) { printf("build failed\n"); return; }

    size_t a = old_count(&idx, pat, m);
    size_t b = neverc_suffixarray_count(&idx, pat, m);

    double t0 = now_sec();
    for (int i = 0; i < iters; i++) sink = old_count(&idx, pat, m);
    double t_old = now_sec() - t0;

    t0 = now_sec();
    for (int i = 0; i < iters; i++) sink = neverc_suffixarray_count(&idx, pat, m);
    double t_new = now_sec() - t0;

    printf("%-30s n=%-8zu m=%-5zu  %8.2f ms  %8.2f ms  %6.2fx%s\n",
           label, n, m, t_old * 1000, t_new * 1000, t_old / t_new,
           a == b ? "" : "  <-- MISMATCH!");
    neverc_suffixarray_free(&idx);
}

int main(void) {
    printf("=== suffix-array search: LCP-accelerated (new) vs compare-from-zero (old) ===\n");
    printf("%-30s %-11s %-7s  %10s  %10s  %8s\n",
           "case", "n", "m", "old", "new", "speedup");

    srand(42);

    /* 1) Random text, short patterns (regression guard: must not get slower). */
    {
        size_t n = 200000;
        unsigned char *t = (unsigned char *)malloc(n);
        for (size_t i = 0; i < n; i++) t[i] = (unsigned char)('a' + rand() % 26);
        unsigned char pat[8];
        memcpy(pat, t + n / 2, 8);
        run_case("random a-z, m=8 (hit)", t, n, pat, 8, 2000000);
        free(t);
    }

    /* 2) DNA-like alphabet (4 symbols), medium patterns: more shared prefixes. */
    {
        size_t n = 500000;
        unsigned char *t = (unsigned char *)malloc(n);
        const char *sym = "ACGT";
        for (size_t i = 0; i < n; i++) t[i] = (unsigned char)sym[rand() % 4];
        unsigned char pat[40];
        memcpy(pat, t + n / 3, 40);
        run_case("DNA {ACGT}, m=40 (hit)", t, n, pat, 40, 1000000);
        free(t);
    }

    /* 3) Binary alphabet, long patterns: long shared prefixes everywhere. */
    {
        size_t n = 500000;
        unsigned char *t = (unsigned char *)malloc(n);
        for (size_t i = 0; i < n; i++) t[i] = (unsigned char)('a' + (rand() & 1));
        unsigned char pat[128];
        memcpy(pat, t + 1000, 128);
        run_case("binary {0,1}, m=128 (hit)", t, n, pat, 128, 500000);
        free(t);
    }

    /* 4) Highly periodic text (period 7), long pattern: the LCP sweet spot. */
    {
        size_t n = 500000;
        unsigned char *t = (unsigned char *)malloc(n);
        for (size_t i = 0; i < n; i++) t[i] = (unsigned char)('a' + (i % 7));
        unsigned char pat[200];
        for (size_t i = 0; i < 200; i++) pat[i] = (unsigned char)('a' + ((i + 3) % 7));
        run_case("periodic-7, m=200 (hit)", t, n, pat, 200, 500000);
        free(t);
    }

    /* 5) Worst case for compare-from-zero: all-'a' text, near-miss long pattern
     *    (a^(m-1) + 'b') -> every probe re-scans the whole 'a' run in the old code. */
    {
        size_t n = 500000;
        unsigned char *t = (unsigned char *)malloc(n);
        memset(t, 'a', n);
        size_t m = 1024;
        unsigned char *pat = (unsigned char *)malloc(m);
        memset(pat, 'a', m); pat[m - 1] = 'b';     /* present? no -> miss after full scan */
        run_case("all-'a', a^1023.b (miss)", t, n, pat, m, 200000);
        free(pat); free(t);
    }

    /* 6) Same all-'a' text, long all-'a' pattern (huge hit set, deep matches). */
    {
        size_t n = 500000;
        unsigned char *t = (unsigned char *)malloc(n);
        memset(t, 'a', n);
        size_t m = 1024;
        unsigned char *pat = (unsigned char *)malloc(m);
        memset(pat, 'a', m);
        run_case("all-'a', a^1024 (hit)", t, n, pat, m, 100000);
        free(pat); free(t);
    }

    printf("\n=== Done ===\n");
    return 0;
}
