/*
 * A/B benchmark + exhaustive correctness check: image/color/palette WebSafe
 * nearest-color index.
 *
 *  - old_websafe_index — the previous library routine, reproduced verbatim: a
 *      linear scan over all 216 WebSafe entries computing the squared Euclidean
 *      distance and keeping the first (lowest-index) minimum.
 *
 *  - neverc_palette_websafe_index (library) — the new routine: WebSafe is an
 *      axis-aligned 6x6x6 cube, so the squared distance is separable and the
 *      nearest entry is found directly as level(r)*36 + level(g)*6 + level(b),
 *      where level(c) = round(c / 0x33). O(1) instead of O(216).
 *
 * The cube's 51-wide (odd) gaps give every channel a unique nearest level, so
 * the result is identical to the old first-match scan. The harness proves this
 * exhaustively over all 2^24 RGB inputs before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -I std/include -o /tmp/palette_bench \
 *      tests/neverc/std/palette_bench.c std/src/image/color/palette/palette.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/image/color/palette.h"

/* Timing iteration count; override (e.g. -DBENCH_ITERS=50000) for fast runs. */
#ifndef BENCH_ITERS
#define BENCH_ITERS 2000000
#endif

/* Stride for the exhaustive correctness sweep; default 1 (all 2^24 triples).
 * Sanitizer runs can pass a larger step (e.g. -DSWEEP_STEP=5) to sample. */
#ifndef SWEEP_STEP
#define SWEEP_STEP 1
#endif

#define NC_NOINLINE __attribute__((noinline))

/* ============================================================
 * OLD routine — verbatim reproduction of the previous library
 * ============================================================ */
static int old_color_dist_sq(int r1, int g1, int b1, int r2, int g2, int b2) {
    int dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
    return dr*dr + dg*dg + db*db;
}

static NC_NOINLINE int old_websafe_index(uint8_t r, uint8_t g, uint8_t b) {
    int best = 0, best_dist = old_color_dist_sq(r, g, b,
        neverc_palette_websafe[0].r, neverc_palette_websafe[0].g, neverc_palette_websafe[0].b);
    for (int i = 1; i < NEVERC_PALETTE_WEBSAFE_LEN; i++) {
        int d = old_color_dist_sq(r, g, b,
            neverc_palette_websafe[i].r, neverc_palette_websafe[i].g, neverc_palette_websafe[i].b);
        if (d < best_dist) { best_dist = d; best = i; }
    }
    return best;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile unsigned long long sink;

/* ============================================================
 * Exhaustive correctness: new must equal old for every RGB triple
 * ============================================================ */
static void verify(void) {
    long long total = 0, ok = 0;
    int first_r = -1, first_g = -1, first_b = -1, first_o = 0, first_n = 0;

    for (int r = 0; r < 256; r += SWEEP_STEP)
        for (int g = 0; g < 256; g += SWEEP_STEP)
            for (int b = 0; b < 256; b += SWEEP_STEP) {
                int o = old_websafe_index((uint8_t)r, (uint8_t)g, (uint8_t)b);
                int n = neverc_palette_websafe_index((uint8_t)r, (uint8_t)g, (uint8_t)b);
                total++;
                if (o == n) ok++;
                else if (first_r < 0) {
                    first_r = r; first_g = g; first_b = b; first_o = o; first_n = n;
                }
            }

    printf("exhaustive check (step %d): %lld/%lld identical (old vs new)\n",
           SWEEP_STEP, ok, total);
    if (ok != total)
        printf("  *** MISMATCH at (%d,%d,%d): old=%d new=%d ***\n",
               first_r, first_g, first_b, first_o, first_n);
}

/* ============================================================
 * Timing
 * ============================================================ */
static int opaque_int(int v) { __asm__ volatile("" : "+r"(v)); return v; }

static void bench(void) {
    /* A fixed pseudo-random spread of inputs, reused by both timers. */
    enum { N = 4096 };
    static uint8_t rgb[N][3];
    uint64_t s = 0x123456789abcdefULL;
    for (int i = 0; i < N; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        rgb[i][0] = (uint8_t)s;
        rgb[i][1] = (uint8_t)(s >> 8);
        rgb[i][2] = (uint8_t)(s >> 16);
    }

    const int iters = BENCH_ITERS;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            const uint8_t *p = rgb[i & (N - 1)];
            sink += (unsigned)old_websafe_index(
                (uint8_t)opaque_int(p[0]), (uint8_t)opaque_int(p[1]), (uint8_t)opaque_int(p[2]));
        }
        double e = now_sec() - t0; if (e < t_old) t_old = e;

        t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            const uint8_t *p = rgb[i & (N - 1)];
            sink += (unsigned)neverc_palette_websafe_index(
                (uint8_t)opaque_int(p[0]), (uint8_t)opaque_int(p[1]), (uint8_t)opaque_int(p[2]));
        }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-16s  %8.1f ms  %8.1f ms  %7.1fx\n",
           "websafe_index", t_old * 1000, t_new * 1000, t_old / t_new);
}

int main(void) {
    printf("=== palette WebSafe index: separable O(1) (new) vs O(216) scan (old) ===\n\n");

    verify();

    printf("\n%-16s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");
    bench();

    printf("\n=== Done ===\n");
    return 0;
}
