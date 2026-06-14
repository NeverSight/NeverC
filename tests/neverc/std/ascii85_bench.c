/*
 * A/B benchmark + correctness check: encoding/ascii85 encode.
 *
 *  - old_encode — the previous library encoder, reproduced verbatim: each
 *      4-byte group's five base-85 digits were produced by five serial
 *      `v /= 85` / `v % 85` steps, written via a temporary then copied out.
 *      The division chain is a strict data dependency, serializing the group.
 *
 *  - neverc_ascii85_encode (library) — extracts the five digits with
 *      independent divisions by powers of 85 (each a multiply-high), breaking
 *      the dependency chain, and writes digits straight into the destination.
 *
 * The fast path is behavior-preserving, so every case asserts the new output is
 * byte-for-byte identical to the old output before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -I std/include -o /tmp/ascii85_bench \
 *      tests/neverc/std/ascii85_bench.c std/src/encoding/ascii85/ascii85.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/encoding/ascii85.h"

/* ============================================================
 * OLD encoder — verbatim reproduction of the previous library
 * ============================================================ */
static int old_encode(unsigned char *dst, const unsigned char *src, size_t src_len) {
    if (src_len == 0) return 0;
    int n = 0;
    size_t off = 0;
    while (off < src_len) {
        unsigned int v = 0;
        size_t remain = src_len - off;
        switch (remain >= 4 ? 4 : remain) {
        case 4: v |= (unsigned int)src[off + 3]; /* fall through */
        case 3: v |= (unsigned int)src[off + 2] << 8;  /* fall through */
        case 2: v |= (unsigned int)src[off + 1] << 16; /* fall through */
        case 1: v |= (unsigned int)src[off + 0] << 24; break;
        }
        if (v == 0 && remain >= 4) { dst[n++] = 'z'; off += 4; continue; }
        unsigned char tmp[5];
        for (int i = 4; i >= 0; i--) {
            tmp[i] = (unsigned char)('!' + (unsigned char)(v % 85));
            v /= 85;
        }
        int m = 5;
        if (remain < 4) m = (int)remain + 1;
        for (int i = 0; i < m; i++) dst[n++] = tmp[i];
        if (remain < 4) break;
        off += 4;
    }
    return n;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile int sink;

static uint64_t rng = 0x243f6a8885a308d3ULL;
static uint32_t xr(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (uint32_t)(rng >> 32);
}

/* ============================================================
 * Correctness sweep
 * ============================================================ */
static int sweep(void) {
    int mism = 0;
    unsigned char src[300], ob[400], nb[400];
    /* every length 0..256, random + zero-heavy content, several trials each */
    for (int len = 0; len <= 256; len++) {
        for (int trial = 0; trial < 200; trial++) {
            for (int i = 0; i < len; i++) {
                uint32_t r = xr();
                /* mix: ~1/3 zero bytes to exercise the 'z' shorthand */
                src[i] = (r % 3 == 0) ? 0 : (unsigned char)r;
            }
            int o = old_encode(ob, src, (size_t)len);
            int n = neverc_ascii85_encode(nb, src, (size_t)len);
            if (o != n || memcmp(ob, nb, (size_t)o) != 0) {
                if (mism < 6) printf("  MISMATCH len=%d trial=%d old=%d new=%d\n", len, trial, o, n);
                mism++;
            }
        }
    }
    /* all-zero buffers of various sizes ('z' shorthand on every full group) */
    memset(src, 0, sizeof src);
    for (int len = 0; len <= 256; len++) {
        int o = old_encode(ob, src, (size_t)len);
        int n = neverc_ascii85_encode(nb, src, (size_t)len);
        if (o != n || memcmp(ob, nb, (size_t)o) != 0) {
            if (mism < 6) printf("  ZERO MISMATCH len=%d old=%d new=%d\n", len, o, n);
            mism++;
        }
    }
    return mism;
}

/* ============================================================
 * Timing
 * ============================================================ */
static void bench_case(const char *label, const unsigned char *src, size_t len) {
    unsigned char ob[1 << 16], nb[1 << 16];
    int o = old_encode(ob, src, len);
    int n = neverc_ascii85_encode(nb, src, len);
    if (o != n || memcmp(ob, nb, (size_t)o) != 0) {
        printf("%-18s  CORRECTNESS FAIL (old=%d new=%d)\n", label, o, n);
        return;
    }
#ifndef BSCALE
#define BSCALE 1
#endif
    int iters = (int)(800000000u / (len + 1)) / BSCALE; if (iters < 1) iters = 1;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) sink = old_encode(ob, src, len);
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) sink = neverc_ascii85_encode(nb, src, len);
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %d B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, len, n);
}

int main(void) {
    printf("=== ascii85 encode: independent power-of-85 divisions (new) vs serial v/=85 (old) ===\n\n");

    int m = sweep();
    printf("correctness sweep (len 0..256, random+zeros): %s (%d mismatches)\n\n",
           m ? "FAIL" : "all identical", m);

    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    static unsigned char rnd[8192], zeros[8192], mixed[8192];
    for (int i = 0; i < 8192; i++) rnd[i] = (unsigned char)xr();
    memset(zeros, 0, sizeof zeros);
    for (int i = 0; i < 8192; i++) { uint32_t r = xr(); mixed[i] = (r % 4 == 0) ? 0 : (unsigned char)r; }

    bench_case("random_8K", rnd, sizeof rnd);
    bench_case("mixed_8K", mixed, sizeof mixed);
    bench_case("zeros_8K", zeros, sizeof zeros);

    printf("\n=== Done ===\n");
    return m ? 1 : 0;
}
