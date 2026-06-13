/*
 * A/B benchmark + correctness check: PNG scanline unfiltering (decode side).
 *
 * old_unfilter — the previous per-byte reconstruction: a switch on filter_type
 *                and an `x >= bpp` / prev test on every byte.
 * new_unfilter — a copy of png_unfilter_row from std/src/image/png/png.c, which
 *                hoists the filter and prev/no-prev cases out of the loop so
 *                None/Up collapse to memcpy / a vectorizable add and the rest
 *                split into a bpp prefix + branch-free main loop.
 *
 * Both are reproduced here verbatim and asserted byte-for-byte identical before
 * timing (the live decoder is covered separately by test_png.c).
 *
 * Build standalone:
 *   cc -O2 -I std/include png_bench.c -o png_bench
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a; if (pa < 0) pa = -pa;
    int pb = p - (int)b; if (pb < 0) pb = -pb;
    int pc = p - (int)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static int old_unfilter(uint8_t *dst, const uint8_t *src, const uint8_t *prev,
                        size_t stride, size_t bpp, uint8_t ft) {
    for (size_t x = 0; x < stride; x++) {
        uint8_t a = (x >= bpp) ? dst[x - bpp] : 0;
        uint8_t b = prev ? prev[x] : 0;
        uint8_t c = (prev && x >= bpp) ? prev[x - bpp] : 0;
        uint8_t rb = src[x];
        switch (ft) {
            case 0: dst[x] = rb; break;
            case 1: dst[x] = rb + a; break;
            case 2: dst[x] = rb + b; break;
            case 3: dst[x] = rb + (uint8_t)(((int)a + (int)b) / 2); break;
            case 4: dst[x] = rb + paeth_predictor(a, b, c); break;
            default: return -1;
        }
    }
    return 0;
}

static int new_unfilter(uint8_t *dst, const uint8_t *src, const uint8_t *prev,
                        size_t stride, size_t bpp, uint8_t ft) {
    size_t x;
    switch (ft) {
    case 0: memcpy(dst, src, stride); return 0;
    case 1:
        for (x = 0; x < bpp; x++) dst[x] = src[x];
        for (x = bpp; x < stride; x++) dst[x] = (uint8_t)(src[x] + dst[x - bpp]);
        return 0;
    case 2:
        if (prev) for (x = 0; x < stride; x++) dst[x] = (uint8_t)(src[x] + prev[x]);
        else memcpy(dst, src, stride);
        return 0;
    case 3:
        if (prev) {
            for (x = 0; x < bpp; x++) dst[x] = (uint8_t)(src[x] + (uint8_t)((int)prev[x] / 2));
            for (x = bpp; x < stride; x++) dst[x] = (uint8_t)(src[x] + (uint8_t)(((int)dst[x - bpp] + (int)prev[x]) / 2));
        } else {
            for (x = 0; x < bpp; x++) dst[x] = src[x];
            for (x = bpp; x < stride; x++) dst[x] = (uint8_t)(src[x] + (uint8_t)((int)dst[x - bpp] / 2));
        }
        return 0;
    case 4:
        if (prev) {
            for (x = 0; x < bpp; x++) dst[x] = (uint8_t)(src[x] + paeth_predictor(0, prev[x], 0));
            for (x = bpp; x < stride; x++) dst[x] = (uint8_t)(src[x] + paeth_predictor(dst[x - bpp], prev[x], prev[x - bpp]));
        } else {
            for (x = 0; x < bpp; x++) dst[x] = src[x];
            for (x = bpp; x < stride; x++) dst[x] = (uint8_t)(src[x] + paeth_predictor(dst[x - bpp], 0, 0));
        }
        return 0;
    default: return -1;
    }
}

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile uint8_t sink;

int main(void) {
    printf("=== PNG unfilter: specialized (new) vs per-byte switch (old) ===\n");
    printf("%-14s  %10s  %10s  %8s\n", "filter", "old", "new", "speedup");

    const size_t W = 1920, H = 1080, bpp = 4;     /* 1080p RGBA */
    const size_t stride = W * bpp;
    uint8_t *filtered = (uint8_t *)malloc(stride * H);   /* filtered scanlines */
    uint8_t *cur = (uint8_t *)malloc(stride);
    uint8_t *out_o = (uint8_t *)malloc(stride * H);
    uint8_t *out_n = (uint8_t *)malloc(stride * H);
    srand(42);
    for (size_t i = 0; i < stride * H; i++) filtered[i] = (uint8_t)rand();

    const char *names[5] = {"None(0)", "Sub(1)", "Up(2)", "Average(3)", "Paeth(4)"};
    for (uint8_t ft = 0; ft < 5; ft++) {
        /* correctness over the whole image first */
        for (size_t y = 0; y < H; y++) {
            const uint8_t *src = filtered + y * stride;
            const uint8_t *prev_o = y ? out_o + (y - 1) * stride : NULL;
            const uint8_t *prev_n = y ? out_n + (y - 1) * stride : NULL;
            old_unfilter(out_o + y * stride, src, prev_o, stride, bpp, ft);
            new_unfilter(out_n + y * stride, src, prev_n, stride, bpp, ft);
        }
        if (memcmp(out_o, out_n, stride * H) != 0) {
            printf("%-14s  CORRECTNESS FAIL\n", names[ft]); continue;
        }

        int iters = 60;
        double t_old = 1e30, t_new = 1e30;
        for (int rep = 0; rep < 3; rep++) {
            double t0 = now_sec();
            for (int it = 0; it < iters; it++)
                for (size_t y = 0; y < H; y++)
                    old_unfilter(out_o + y * stride, filtered + y * stride,
                                 y ? out_o + (y - 1) * stride : NULL, stride, bpp, ft);
            double e = now_sec() - t0; if (e < t_old) t_old = e;
            sink = out_o[stride * H - 1];
            t0 = now_sec();
            for (int it = 0; it < iters; it++)
                for (size_t y = 0; y < H; y++)
                    new_unfilter(out_n + y * stride, filtered + y * stride,
                                 y ? out_n + (y - 1) * stride : NULL, stride, bpp, ft);
            e = now_sec() - t0; if (e < t_new) t_new = e;
            sink = out_n[stride * H - 1];
        }
        printf("%-14s  %8.2f ms  %8.2f ms  %6.2fx\n",
               names[ft], t_old * 1000, t_new * 1000, t_old / t_new);
    }

    free(filtered); free(cur); free(out_o); free(out_n);
    printf("\n=== Done ===\n");
    return 0;
}
