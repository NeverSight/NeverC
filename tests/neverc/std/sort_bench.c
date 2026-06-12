/*
 * sort_bench.c — A/B benchmark: old introsort vs new PDQSort / Timsort.
 *
 * Build:  cc -O2 -o sort_bench sort_bench.c
 * Run:    ./sort_bench
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 *  OLD implementation (baseline): classic introsort + naive merge sort
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef int (*old_cmp_fn)(const void *, const void *);

#define OLD_ELEM(base, i, sz) ((char *)(base) + (i) * (sz))

static void old_swap(char *a, char *b, size_t sz, char *tmp) {
    memcpy(tmp, a, sz); memcpy(a, b, sz); memcpy(b, tmp, sz);
}

static void old_insertion_sort(char *base, size_t n, size_t sz,
                                old_cmp_fn cmp, char *tmp) {
    for (size_t i = 1; i < n; i++) {
        memcpy(tmp, OLD_ELEM(base, i, sz), sz);
        size_t j = i;
        while (j > 0 && cmp(OLD_ELEM(base, j - 1, sz), tmp) > 0) {
            memcpy(OLD_ELEM(base, j, sz), OLD_ELEM(base, j - 1, sz), sz);
            j--;
        }
        memcpy(OLD_ELEM(base, j, sz), tmp, sz);
    }
}

static void old_sift_down(char *base, size_t n, size_t sz,
                            old_cmp_fn cmp, char *tmp, size_t node) {
    while (1) {
        size_t child = 2 * node + 1;
        if (child >= n) break;
        if (child + 1 < n &&
            cmp(OLD_ELEM(base, child, sz), OLD_ELEM(base, child + 1, sz)) < 0)
            child++;
        if (cmp(OLD_ELEM(base, node, sz), OLD_ELEM(base, child, sz)) >= 0)
            break;
        old_swap(OLD_ELEM(base, node, sz), OLD_ELEM(base, child, sz), sz, tmp);
        node = child;
    }
}

static void old_heapsort(char *base, size_t n, size_t sz,
                           old_cmp_fn cmp, char *tmp) {
    if (n <= 1) return;
    for (size_t i = n / 2; i > 0; i--)
        old_sift_down(base, n, sz, cmp, tmp, i - 1);
    for (size_t i = n - 1; i > 0; i--) {
        old_swap(base, OLD_ELEM(base, i, sz), sz, tmp);
        old_sift_down(base, i, sz, cmp, tmp, 0);
    }
}

static size_t old_partition(char *base, size_t n, size_t sz,
                             old_cmp_fn cmp, char *tmp) {
    size_t mid = n / 2;
    if (n > 8) {
        if (cmp(base, base + mid * sz) > 0) old_swap(base, base + mid * sz, sz, tmp);
        if (cmp(base + mid * sz, base + (n-1) * sz) > 0) {
            old_swap(base + mid * sz, base + (n-1) * sz, sz, tmp);
            if (cmp(base, base + mid * sz) > 0)
                old_swap(base, base + mid * sz, sz, tmp);
        }
    }
    old_swap(base + mid * sz, base + (n-1) * sz, sz, tmp);
    size_t i = 0;
    for (size_t j = 0; j < n - 1; j++) {
        if (cmp(base + j * sz, base + (n-1) * sz) < 0) {
            old_swap(base + i * sz, base + j * sz, sz, tmp);
            i++;
        }
    }
    old_swap(base + i * sz, base + (n-1) * sz, sz, tmp);
    return i;
}

static void old_introsort(char *base, size_t n, size_t sz,
                            old_cmp_fn cmp, char *tmp, int depth) {
    while (n > 16) {
        if (depth == 0) { old_heapsort(base, n, sz, cmp, tmp); return; }
        depth--;
        size_t p = old_partition(base, n, sz, cmp, tmp);
        old_introsort(OLD_ELEM(base, p + 1, sz), n - p - 1, sz, cmp, tmp, depth);
        n = p;
    }
    old_insertion_sort(base, n, sz, cmp, tmp);
}

static int old_depth_for(size_t n) {
    int d = 0; while (n > 1) { n >>= 1; d++; } return d * 2;
}

static void old_sort(void *base, size_t n, size_t sz, old_cmp_fn cmp) {
    if (n < 2) return;
    char *tmp = (char *)malloc(sz);
    old_introsort((char *)base, n, sz, cmp, tmp, old_depth_for(n));
    free(tmp);
}

static void old_merge(uint8_t *base, uint8_t *tmp, size_t lo, size_t mid,
                       size_t hi, size_t es, old_cmp_fn cmp) {
    memcpy(tmp + lo * es, base + lo * es, (hi - lo) * es);
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (cmp(tmp + i * es, tmp + j * es) <= 0) memcpy(base + k * es, tmp + (i++) * es, es);
        else memcpy(base + k * es, tmp + (j++) * es, es);
        k++;
    }
    while (i < mid) { memcpy(base + (k++) * es, tmp + (i++) * es, es); }
    while (j < hi)  { memcpy(base + (k++) * es, tmp + (j++) * es, es); }
}

static void old_merge_sort(uint8_t *base, uint8_t *tmp, size_t lo, size_t hi,
                             size_t es, old_cmp_fn cmp) {
    if (hi - lo <= 1) return;
    size_t mid = lo + (hi - lo) / 2;
    old_merge_sort(base, tmp, lo, mid, es, cmp);
    old_merge_sort(base, tmp, mid, hi, es, cmp);
    old_merge(base, tmp, lo, mid, hi, es, cmp);
}

static void old_stable_sort(void *base, size_t n, size_t es, old_cmp_fn cmp) {
    if (n <= 1) return;
    uint8_t *tmp = (uint8_t *)malloc(n * es);
    if (!tmp) return;
    old_merge_sort((uint8_t *)base, tmp, 0, n, es, cmp);
    free(tmp);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  NEW implementation: PDQSort + Timsort
 * ═══════════════════════════════════════════════════════════════════════════ */

#include "../../../std/src/sort/sort_impl.h"

/* ═══════════════════════════════════════════════════════════════════════════
 *  Benchmark infrastructure
 * ═══════════════════════════════════════════════════════════════════════════ */

static int cmp_int(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static unsigned xor_state;
static unsigned xrand(void) {
    xor_state ^= xor_state << 13;
    xor_state ^= xor_state >> 17;
    xor_state ^= xor_state << 5;
    return xor_state;
}

typedef void (*sort_fn)(void *, size_t, size_t, old_cmp_fn);

static void gen_random(int *arr, size_t n) {
    for (size_t i = 0; i < n; i++) arr[i] = (int)(xrand() % (unsigned)(n * 10));
}
static void gen_sorted(int *arr, size_t n) {
    for (size_t i = 0; i < n; i++) arr[i] = (int)i;
}
static void gen_reverse(int *arr, size_t n) {
    for (size_t i = 0; i < n; i++) arr[i] = (int)(n - i);
}
static void gen_nearly_sorted(int *arr, size_t n) {
    for (size_t i = 0; i < n; i++) arr[i] = (int)i;
    for (size_t i = 0; i < n / 20; i++) {
        size_t a = xrand() % n, b = xrand() % n;
        int t = arr[a]; arr[a] = arr[b]; arr[b] = t;
    }
}
static void gen_pipe_organ(int *arr, size_t n) {
    for (size_t i = 0; i < n / 2; i++) arr[i] = (int)i;
    for (size_t i = n / 2; i < n; i++) arr[i] = (int)(n - 1 - i);
}
static void gen_all_equal(int *arr, size_t n) {
    for (size_t i = 0; i < n; i++) arr[i] = 42;
}
static void gen_few_unique(int *arr, size_t n) {
    for (size_t i = 0; i < n; i++) arr[i] = (int)(xrand() % 10);
}
static void gen_sawtooth(int *arr, size_t n) {
    for (size_t i = 0; i < n; i++) arr[i] = (int)(i % 100);
}

typedef struct {
    const char *name;
    void (*gen)(int *, size_t);
} pattern_t;

static const pattern_t patterns[] = {
    {"random",        gen_random},
    {"sorted",        gen_sorted},
    {"reverse",       gen_reverse},
    {"nearly_sorted", gen_nearly_sorted},
    {"pipe_organ",    gen_pipe_organ},
    {"all_equal",     gen_all_equal},
    {"few_unique",    gen_few_unique},
    {"sawtooth",      gen_sawtooth},
};
#define NUM_PATTERNS (sizeof(patterns) / sizeof(patterns[0]))

static double time_sort(sort_fn fn, int *data, size_t n, int iters) {
    int *buf = (int *)malloc(n * sizeof(int));
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int it = 0; it < iters; it++) {
        memcpy(buf, data, n * sizeof(int));
        fn(buf, n, sizeof(int), cmp_int);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(buf);
    double elapsed = (double)(t1.tv_sec - t0.tv_sec) +
                     (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    return elapsed / iters * 1e6; /* microseconds per sort */
}

static void new_sort_wrapper(void *base, size_t n, size_t es, old_cmp_fn cmp) {
    nci_pdqsort(base, n, es, (nci_cmp_fn)cmp);
}

static void new_stable_wrapper(void *base, size_t n, size_t es, old_cmp_fn cmp) {
    nci_timsort(base, n, es, (nci_cmp_fn)cmp);
}

int main(void) {
    xor_state = 42;

    static const size_t sizes[] = {100, 1000, 10000, 100000};
    int num_sizes = (int)(sizeof(sizes) / sizeof(sizes[0]));

    printf("╔══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║          NeverC Sort Benchmark: Old Introsort vs New PDQSort               ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════════════╝\n\n");

    for (int si = 0; si < num_sizes; si++) {
        size_t n = sizes[si];
        int iters = (n <= 1000) ? 2000 : (n <= 10000) ? 200 : 20;

        printf("━━━ n = %zu  (%d iterations) ━━━\n", n, iters);
        printf("%-16s  %12s  %12s  %8s\n", "pattern", "old (µs)", "new (µs)", "speedup");
        printf("%-16s  %12s  %12s  %8s\n", "───────────────", "───────────", "───────────", "───────");

        int *data = (int *)malloc(n * sizeof(int));

        for (size_t pi = 0; pi < NUM_PATTERNS; pi++) {
            xor_state = 12345;
            patterns[pi].gen(data, n);

            double old_us = time_sort(old_sort, data, n, iters);

            xor_state = 12345;
            patterns[pi].gen(data, n);

            double new_us = time_sort(new_sort_wrapper, data, n, iters);

            printf("%-16s  %10.1f µs  %10.1f µs  %6.2fx\n",
                   patterns[pi].name, old_us, new_us, old_us / new_us);
        }
        free(data);
        printf("\n");
    }

    printf("╔══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║        Stable Sort Benchmark: Old Merge Sort vs New Timsort                ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════════════╝\n\n");

    for (int si = 0; si < num_sizes; si++) {
        size_t n = sizes[si];
        int iters = (n <= 1000) ? 2000 : (n <= 10000) ? 200 : 20;

        printf("━━━ n = %zu  (%d iterations) ━━━\n", n, iters);
        printf("%-16s  %12s  %12s  %8s\n", "pattern", "old (µs)", "new (µs)", "speedup");
        printf("%-16s  %12s  %12s  %8s\n", "───────────────", "───────────", "───────────", "───────");

        int *data = (int *)malloc(n * sizeof(int));

        for (size_t pi = 0; pi < NUM_PATTERNS; pi++) {
            xor_state = 12345;
            patterns[pi].gen(data, n);

            double old_us = time_sort(old_stable_sort, data, n, iters);

            xor_state = 12345;
            patterns[pi].gen(data, n);

            double new_us = time_sort(new_stable_wrapper, data, n, iters);

            printf("%-16s  %10.1f µs  %10.1f µs  %6.2fx\n",
                   patterns[pi].name, old_us, new_us, old_us / new_us);
        }
        free(data);
        printf("\n");
    }

    printf("╔══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║     Type-Specialized: Generic PDQSort vs Typed nci_pdqsort_int             ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════════════╝\n\n");

    for (int si = 0; si < num_sizes; si++) {
        size_t n = sizes[si];
        int iters = (n <= 1000) ? 2000 : (n <= 10000) ? 200 : 20;

        printf("━━━ n = %zu  (%d iterations) ━━━\n", n, iters);
        printf("%-16s  %12s  %12s  %8s\n", "pattern", "generic", "typed", "speedup");
        printf("%-16s  %12s  %12s  %8s\n", "───────────────", "───────────", "───────────", "───────");

        int *data = (int *)malloc(n * sizeof(int));
        int *buf  = (int *)malloc(n * sizeof(int));

        for (size_t pi = 0; pi < NUM_PATTERNS; pi++) {
            xor_state = 12345;
            patterns[pi].gen(data, n);

            double gen_us = time_sort(new_sort_wrapper, data, n, iters);

            xor_state = 12345;
            patterns[pi].gen(data, n);

            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int it = 0; it < iters; it++) {
                memcpy(buf, data, n * sizeof(int));
                nci_pdqsort_int(buf, n);
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double typed_us = ((double)(t1.tv_sec - t0.tv_sec) +
                               (double)(t1.tv_nsec - t0.tv_nsec) / 1e9)
                              / iters * 1e6;

            printf("%-16s  %10.1f µs  %10.1f µs  %6.2fx\n",
                   patterns[pi].name, gen_us, typed_us, gen_us / typed_us);
        }
        free(data);
        free(buf);
        printf("\n");
    }

    return 0;
}
