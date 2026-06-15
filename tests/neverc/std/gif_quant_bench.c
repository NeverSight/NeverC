/*
 * gif_quant_bench.c — A/B benchmark for neverc_gif_from_rgba color quantization.
 *
 * Goal: quantify what the recent change actually trades. It replaced a fixed
 * 216-color web-safe cube (uniform quantization) with Wu's variance-minimizing
 * quantizer. This measures BOTH axes so the change can be classified correctly:
 *
 *   quality — per-pixel PSNR (dB) of the quantized image vs the RGBA original.
 *             Higher is better; this is what Wu improves.
 *   cost    — wall-clock time per call and peak scratch memory. This is what
 *             Wu makes WORSE (so the change is a quality/fidelity optimization,
 *             not a performance optimization).
 *
 * old: the previous 216-color web-safe cube, reproduced here verbatim from the
 *      pre-change neverc_gif_from_rgba (that code path was deleted).
 * new: the live neverc_gif_from_rgba from std/src/image/gif/gif.c (Wu).
 *
 * Build (from repo root):
 *   cc -O2 -I std/include tests/neverc/std/gif_quant_bench.c \
 *      std/src/image/gif/gif.c -o /tmp/gif_quant_bench -lm
 */
#include "neverc/std/image/gif.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

/* ---- old quantizer: 216-color web-safe cube (verbatim from old from_rgba) -- */
static int quantize_uniform_old(const uint8_t *rgba, uint32_t width,
                                uint32_t height, neverc_gif_frame_t *frame) {
    memset(frame, 0, sizeof(*frame));
    frame->width = width;
    frame->height = height;
    frame->palette_size = 216;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                int idx = r * 36 + g * 6 + b;
                frame->palette[idx].r = (uint8_t)(r * 51);
                frame->palette[idx].g = (uint8_t)(g * 51);
                frame->palette[idx].b = (uint8_t)(b * 51);
            }
    size_t npixels = (size_t)width * height;
    frame->indices = (uint8_t *)malloc(npixels);
    if (!frame->indices) return -1;
    for (size_t i = 0; i < npixels; i++) {
        int r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
        int ri = (r + 25) / 51; if (ri > 5) ri = 5;
        int gi = (g + 25) / 51; if (gi > 5) gi = 5;
        int bi = (b + 25) / 51; if (bi > 5) bi = 5;
        frame->indices[i] = (uint8_t)(ri * 36 + gi * 6 + bi);
    }
    return 0;
}

/* Mean-squared-error PSNR of the reconstructed (palette[index]) image. */
static double psnr_of(const uint8_t *rgba, const neverc_gif_frame_t *f) {
    size_t n = (size_t)f->width * f->height;
    double se = 0.0;
    for (size_t i = 0; i < n; i++) {
        const neverc_gif_color_t *c = &f->palette[f->indices[i]];
        int dr = (int)rgba[i * 4 + 0] - c->r;
        int dg = (int)rgba[i * 4 + 1] - c->g;
        int db = (int)rgba[i * 4 + 2] - c->b;
        se += (double)(dr * dr + dg * dg + db * db);
    }
    double mse = se / ((double)n * 3.0);
    if (mse <= 0.0) return 1e9;                 /* lossless */
    return 10.0 * log10(255.0 * 255.0 / mse);
}

/* ---- synthetic test images ----------------------------------------------- */

/* Smooth RGB gradient: thousands of distinct colors -> exposes web-safe banding. */
static void make_gradient(uint8_t *rgba, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            size_t i = ((size_t)y * w + x) * 4;
            rgba[i + 0] = (uint8_t)(x * 255 / (w - 1));
            rgba[i + 1] = (uint8_t)(y * 255 / (h - 1));
            rgba[i + 2] = (uint8_t)((x + y) * 255 / (w + h - 2));
            rgba[i + 3] = 255;
        }
}

/* Photo-like: smooth overlapping sinusoids, broad continuous color range. */
static void make_photo(uint8_t *rgba, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            size_t i = ((size_t)y * w + x) * 4;
            rgba[i + 0] = (uint8_t)(128 + 127 * sin(x * 0.031));
            rgba[i + 1] = (uint8_t)(128 + 127 * sin(y * 0.041 + 1.0));
            rgba[i + 2] = (uint8_t)(128 + 127 * sin((x + y) * 0.021 + 2.0));
            rgba[i + 3] = 255;
        }
}

/* Low-color: 32 fixed colors deliberately OFF the web-safe grid. Wu can pick
 * them almost exactly; the uniform cube must round each to a coarse cell. */
static void make_lowcolor(uint8_t *rgba, uint32_t w, uint32_t h) {
    neverc_gif_color_t pal[32];
    unsigned s = 0x1234567u;
    for (int i = 0; i < 32; i++) {
        s = s * 1103515245u + 12345u; pal[i].r = (uint8_t)((s >> 16) & 0xff);
        s = s * 1103515245u + 12345u; pal[i].g = (uint8_t)((s >> 16) & 0xff);
        s = s * 1103515245u + 12345u; pal[i].b = (uint8_t)((s >> 16) & 0xff);
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            size_t i = ((size_t)y * w + x) * 4;
            int k = ((x / 16) + (y / 16)) % 32;
            rgba[i + 0] = pal[k].r;
            rgba[i + 1] = pal[k].g;
            rgba[i + 2] = pal[k].b;
            rgba[i + 3] = 255;
        }
}

static void fmt_psnr(char *buf, size_t cap, double p) {
    if (p >= 1e8) snprintf(buf, cap, "    inf");
    else          snprintf(buf, cap, "%6.2f", p);
}

static void run_one(const char *name, const uint8_t *rgba, uint32_t w,
                    uint32_t h, int reps) {
    neverc_gif_frame_t fo, fn;
    if (quantize_uniform_old(rgba, w, h, &fo) != 0) { printf("alloc fail\n"); return; }
    if (neverc_gif_from_rgba(rgba, w, h, &fn) != 0) { printf("alloc fail\n"); return; }
    double po = psnr_of(rgba, &fo);
    double pn = psnr_of(rgba, &fn);
    int ncolors_new = fn.palette_size;
    free(fo.indices);
    free(fn.indices);

    neverc_gif_frame_t tmp;
    double t0 = now_ms();
    for (int r = 0; r < reps; r++) { quantize_uniform_old(rgba, w, h, &tmp); free(tmp.indices); }
    double told = (now_ms() - t0) / reps;
    t0 = now_ms();
    for (int r = 0; r < reps; r++) { neverc_gif_from_rgba(rgba, w, h, &tmp); free(tmp.indices); }
    double tnew = (now_ms() - t0) / reps;

    char pob[16], pnb[16];
    fmt_psnr(pob, sizeof pob, po);
    fmt_psnr(pnb, sizeof pnb, pn);
    printf("%-16s %4d %4d   %7s %7s   %8.3f %8.3f   %6.2fx\n",
           name, 216, ncolors_new, pob, pnb, told, tnew, tnew / told);
}

int main(void) {
    const uint32_t w = 512, h = 512;
    const int reps = 60;
    size_t bytes = (size_t)w * h * 4;
    uint8_t *img = (uint8_t *)malloc(bytes);
    if (!img) return 1;

    /* peak extra scratch Wu allocates: 33^3 cells * (4*int64 + double) + tag */
    size_t cells = 33u * 33u * 33u;
    double wu_kb = (double)(cells * (4 * sizeof(int64_t) + sizeof(double) + 1)) / 1024.0;

    printf("=== GIF quantization: old (216-color web-safe cube) vs new (Wu) ===\n");
    printf("image %ux%u, %d reps   (higher PSNR = better quality)\n", w, h, reps);
    printf("%-16s %4s %4s   %7s %7s   %8s %8s   %6s\n",
           "image", "oldC", "newC", "oldPSNR", "newPSNR", "old ms", "new ms", "new/old");
    printf("---------------------------------------------------------------------------------\n");

    make_gradient(img, w, h);  run_one("smooth-gradient", img, w, h, reps);
    make_photo(img, w, h);     run_one("photo-like",      img, w, h, reps);
    make_lowcolor(img, w, h);  run_one("low-color(32)",   img, w, h, reps);

    printf("---------------------------------------------------------------------------------\n");
    printf("scratch memory:   old ~0 KB    new ~%.0f KB (Wu moment arrays, freed per call)\n", wu_kb);
    printf("verdict: Wu raises PSNR (better palette) but costs more time + memory\n");
    printf("         => quality/fidelity optimization, NOT a performance optimization.\n");

    free(img);
    return 0;
}
