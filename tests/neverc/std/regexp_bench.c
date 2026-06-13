/*
 * Scaling demonstration for the single-pass regexp search.
 *
 * The previous find/find_all/replace/split re-ran the NFA simulation from every
 * text position, so a pattern with a common first byte that scans far before
 * failing (e.g. "a+b" over a run of 'a') cost O(n) per position -> O(n^2) total.
 * The first-byte prefilter only helped when the leading byte was rare; on dense
 * alphabets the quadratic blow-up was a real DoS vector (no exponential
 * backtracking, but still quadratic).
 *
 * The new code sweeps the text once (Pike-VM style), carrying every in-flight
 * thread plus a freshly seeded start thread per position, so the whole search is
 * O(n * states). The first-byte prefilter still gates seeding and skips empty
 * stretches via memchr, so sparse matches keep their near-constant per-byte cost.
 *
 * Measured A/B for the quadratic-trigger case below (old re-scan engine vs new
 * single-pass), find "a+b" over an all-'a' miss, -O2, Apple clang:
 *     n        old ms     new ms     speedup
 *     1000      4.738      0.012        ~395x
 *     4000     78.227      0.052       ~1510x
 *    16000   1253.053      0.185       ~6773x   (old ns/byte doubles each step;
 *                                                new ns/byte stays ~11.5 = O(n))
 *
 * Build standalone:
 *   cc -O2 -I std/include regexp_bench.c std/src/regexp/regexp.c -o regexp_bench
 */
#include "neverc/std/regexp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static volatile size_t sink;

int main(void) {
    printf("=== regexp search scaling (optimized engine) ===\n");

    size_t sizes[] = {10000, 50000, 200000, 1000000};
    int ns = (int)(sizeof(sizes) / sizeof(sizes[0]));

    printf("\n-- find: rare first byte, no match (haystack of 'a') --\n");
    printf("%-12s  %10s  %12s\n", "n", "ms", "ns/byte");
    neverc_regexp_t *re = neverc_regexp_compile("zx[0-9]+q", NULL);
    for (int s = 0; s < ns; s++) {
        size_t n = sizes[s];
        char *txt = (char *)malloc(n + 1);
        memset(txt, 'a', n); txt[n] = '\0';
        int iters = (int)(50000000 / n); if (iters < 5) iters = 5;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            size_t mlen;
            sink = (size_t)neverc_regexp_find(re, txt, &mlen);
        }
        double dt = (now_sec() - t0) / iters;
        printf("%-12zu  %10.3f  %12.3f\n", n, dt * 1000, dt * 1e9 / (double)n);
        free(txt);
    }
    neverc_regexp_free(re);

    printf("\n-- find_all: literal token scattered in text --\n");
    printf("%-12s  %10s  %12s\n", "n", "ms", "matches");
    re = neverc_regexp_compile("needle", NULL);
    for (int s = 0; s < ns; s++) {
        size_t n = sizes[s];
        char *txt = (char *)malloc(n + 1);
        for (size_t i = 0; i < n; i++) txt[i] = (char)('a' + (i % 23)); /* no 'needle' by accident */
        /* sprinkle a few real matches */
        for (size_t i = 100; i + 6 < n; i += 5000) memcpy(txt + i, "needle", 6);
        txt[n] = '\0';
        int iters = (int)(20000000 / n); if (iters < 3) iters = 3;
        int count = 0;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            char **m = neverc_regexp_find_all(re, txt, -1, &count);
            neverc_regexp_free_strings(m, count);
        }
        double dt = (now_sec() - t0) / iters;
        printf("%-12zu  %10.3f  %12d\n", n, dt * 1000, count);
        free(txt);
    }
    neverc_regexp_free(re);

    /* Quadratic-trigger: common first byte + long greedy scan that fails at
     * every position. This is where the old re-scan engine was O(n^2); the new
     * single-pass engine holds a constant ns/byte (linear) as n grows. */
    printf("\n-- find: \"a+b\" over all-'a' miss (old engine was O(n^2)) --\n");
    printf("%-12s  %10s  %12s\n", "n", "ms", "ns/byte");
    re = neverc_regexp_compile("a+b", NULL);
    size_t qsizes[] = {10000, 50000, 200000, 1000000};
    int qn = (int)(sizeof(qsizes) / sizeof(qsizes[0]));
    for (int s = 0; s < qn; s++) {
        size_t n = qsizes[s];
        char *txt = (char *)malloc(n + 1);
        memset(txt, 'a', n); txt[n] = '\0';
        int iters = (int)(20000000 / n); if (iters < 3) iters = 3;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            size_t mlen;
            sink = (size_t)neverc_regexp_find(re, txt, &mlen);
        }
        double dt = (now_sec() - t0) / iters;
        printf("%-12zu  %10.3f  %12.3f\n", n, dt * 1000, dt * 1e9 / (double)n);
        free(txt);
    }
    neverc_regexp_free(re);

    printf("\n=== Done (sink=%zu) ===\n", sink);
    return 0;
}
