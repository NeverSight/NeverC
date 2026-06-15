/*
 * test_gif_quant.c — regression guard for neverc_gif_from_rgba color quality.
 *
 * neverc_gif_from_rgba was changed from a fixed 216-color web-safe cube to Wu's
 * variance-minimizing quantizer. That is a *quality* improvement (better palette
 * chosen from the image), so this test locks in the property that motivated it:
 * the live quantizer must reconstruct images with lower error than the old
 * uniform cube. Error is measured as mean squared error (no math lib needed);
 * lower MSE == higher PSNR == better fidelity.
 *
 * The old cube is reproduced verbatim below so the comparison survives even if
 * the library's fallback path later changes.
 */
#include "neverc/std/image/gif.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(expr) do { \
    tests_run++; \
    if (expr) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s\n", __LINE__, #expr); } \
} while(0)

/* Old quantizer: 216-color web-safe cube (verbatim from the pre-change code). */
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

/* Mean squared error of the reconstructed (palette[index]) image vs original. */
static double mse_of(const uint8_t *rgba, const neverc_gif_frame_t *f) {
    size_t n = (size_t)f->width * f->height;
    double se = 0.0;
    for (size_t i = 0; i < n; i++) {
        const neverc_gif_color_t *c = &f->palette[f->indices[i]];
        int dr = (int)rgba[i * 4 + 0] - c->r;
        int dg = (int)rgba[i * 4 + 1] - c->g;
        int db = (int)rgba[i * 4 + 2] - c->b;
        se += (double)(dr * dr + dg * dg + db * db);
    }
    return se / ((double)n * 3.0);
}

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

/* 16 colors deliberately off the web-safe grid; each channel is one of
 * {10,90,170,250}, which land in distinct 5-bit histogram bins, so Wu can
 * separate them exactly while the uniform cube must round each to a coarse cell. */
static void make_lowcolor(uint8_t *rgba, uint32_t w, uint32_t h) {
    static const int base[4] = {10, 90, 170, 250};
    neverc_gif_color_t pal[16];
    for (int k = 0; k < 16; k++) {
        pal[k].r = (uint8_t)base[k & 3];
        pal[k].g = (uint8_t)base[(k >> 2) & 3];
        pal[k].b = (uint8_t)base[(k ^ (k >> 1)) & 3];
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            size_t i = ((size_t)y * w + x) * 4;
            int k = (int)(((x >> 4) + (y >> 4)) & 15);
            rgba[i + 0] = pal[k].r;
            rgba[i + 1] = pal[k].g;
            rgba[i + 2] = pal[k].b;
            rgba[i + 3] = 255;
        }
}

/* On a smooth gradient (thousands of colors) Wu must beat the web-safe cube by
 * a wide margin — the whole point of the change. */
static void test_gradient_beats_uniform(void) {
    printf("[gradient_beats_uniform]\n");
    uint32_t w = 256, h = 256;
    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
    ASSERT_TRUE(rgba != NULL);
    if (!rgba) return;
    make_gradient(rgba, w, h);

    neverc_gif_frame_t fo, fn;
    ASSERT_TRUE(quantize_uniform_old(rgba, w, h, &fo) == 0);
    ASSERT_TRUE(neverc_gif_from_rgba(rgba, w, h, &fn) == 0);

    double mo = mse_of(rgba, &fo);
    double mn = mse_of(rgba, &fn);
    printf("  uniform MSE=%.2f  Wu MSE=%.2f\n", mo, mn);
    ASSERT_TRUE(mn < mo);            /* Wu is better */
    ASSERT_TRUE(mn * 2.0 < mo);      /* and by a clear margin */

    free(fo.indices);
    free(fn.indices);
    free(rgba);
}

/* On a small fixed palette that is off the web-safe grid, Wu should reproduce
 * the image essentially losslessly, while the cube cannot. */
static void test_lowcolor_near_lossless(void) {
    printf("[lowcolor_near_lossless]\n");
    uint32_t w = 128, h = 128;
    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
    ASSERT_TRUE(rgba != NULL);
    if (!rgba) return;
    make_lowcolor(rgba, w, h);

    neverc_gif_frame_t fo, fn;
    ASSERT_TRUE(quantize_uniform_old(rgba, w, h, &fo) == 0);
    ASSERT_TRUE(neverc_gif_from_rgba(rgba, w, h, &fn) == 0);

    double mo = mse_of(rgba, &fo);
    double mn = mse_of(rgba, &fn);
    printf("  uniform MSE=%.2f  Wu MSE=%.2f\n", mo, mn);
    ASSERT_TRUE(mo > 1.0);           /* the cube genuinely distorts these colors */
    ASSERT_TRUE(mn < 1.0);           /* Wu is essentially lossless */
    ASSERT_TRUE(mn < mo);

    free(fo.indices);
    free(fn.indices);
    free(rgba);
}

/* Every emitted index must address a real palette entry, and the palette must
 * satisfy the GIF >=2 entry rule and the 256 cap. */
static void test_indices_and_palette_valid(void) {
    printf("[indices_and_palette_valid]\n");
    uint32_t w = 200, h = 150;
    uint8_t *rgba = (uint8_t *)malloc((size_t)w * h * 4);
    ASSERT_TRUE(rgba != NULL);
    if (!rgba) return;
    make_gradient(rgba, w, h);

    neverc_gif_frame_t f;
    ASSERT_TRUE(neverc_gif_from_rgba(rgba, w, h, &f) == 0);
    ASSERT_TRUE(f.palette_size >= 2);
    ASSERT_TRUE(f.palette_size <= 256);

    int in_range = 1;
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++)
        if (f.indices[i] >= f.palette_size) { in_range = 0; break; }
    ASSERT_TRUE(in_range);

    free(f.indices);
    free(rgba);
}

int main(void) {
    printf("NeverC image/gif quantization tests\n");
    test_gradient_beats_uniform();
    test_lowcolor_near_lossless();
    test_indices_and_palette_valid();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
