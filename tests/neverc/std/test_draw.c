#include "neverc/std/image/draw.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0;

static void check(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else printf("  FAIL: %s\n", name);
}

static void test_draw_src(void) {
    printf("[draw_src]\n");
    neverc_image_rgba_t dst, src;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 10, 10));
    neverc_image_rgba_init(&src, neverc_rect(0, 0, 10, 10));

    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++)
            neverc_image_rgba_set(&src, x, y, 100, 150, 200, 255);

    neverc_draw(&dst, neverc_rect(2, 2, 8, 8), &src, neverc_pt(0, 0), NEVERC_DRAW_SRC);

    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 5, 5, &r, &g, &b, &a);
    check("inside", r == 100 && g == 150 && b == 200 && a == 255);

    neverc_image_rgba_at(&dst, 0, 0, &r, &g, &b, &a);
    check("outside_unchanged", r == 0 && g == 0 && b == 0 && a == 0);

    neverc_image_rgba_free(&dst);
    neverc_image_rgba_free(&src);
}

static void test_draw_uniform(void) {
    printf("[draw_uniform]\n");
    neverc_image_rgba_t dst;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 20, 20));

    neverc_draw_uniform(&dst, neverc_rect(0, 0, 20, 20),
                        255, 0, 0, 255, NEVERC_DRAW_SRC);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 10, 10, &r, &g, &b, &a);
    check("fill_red", r == 255 && g == 0 && b == 0 && a == 255);

    neverc_draw_uniform(&dst, neverc_rect(5, 5, 15, 15),
                        0, 0, 255, 128, NEVERC_DRAW_OVER);
    neverc_image_rgba_at(&dst, 10, 10, &r, &g, &b, &a);
    check("over_blended_r_decreased", r < 255);
    check("over_blended_b_increased", b > 0);

    neverc_image_rgba_free(&dst);
}

static void test_draw_over_opaque(void) {
    printf("[draw_over_opaque]\n");
    neverc_image_rgba_t dst, src;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 5, 5));
    neverc_image_rgba_init(&src, neverc_rect(0, 0, 5, 5));

    neverc_draw_uniform(&dst, neverc_rect(0, 0, 5, 5), 50, 50, 50, 255, NEVERC_DRAW_SRC);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            neverc_image_rgba_set(&src, x, y, 200, 100, 50, 255);

    neverc_draw(&dst, neverc_rect(0, 0, 5, 5), &src, neverc_pt(0, 0), NEVERC_DRAW_OVER);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 2, 2, &r, &g, &b, &a);
    check("opaque_over_replaces", r == 200 && g == 100 && b == 50 && a == 255);

    neverc_image_rgba_free(&dst);
    neverc_image_rgba_free(&src);
}

static void test_draw_clipping(void) {
    printf("[draw_clipping]\n");
    neverc_image_rgba_t dst, src;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 10, 10));
    neverc_image_rgba_init(&src, neverc_rect(0, 0, 5, 5));

    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            neverc_image_rgba_set(&src, x, y, 255, 255, 255, 255);

    neverc_draw(&dst, neverc_rect(-2, -2, 5, 5), &src, neverc_pt(0, 0), NEVERC_DRAW_SRC);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 0, 0, &r, &g, &b, &a);
    check("clipped_visible", r == 255);

    neverc_image_rgba_free(&dst);
    neverc_image_rgba_free(&src);
}

/* Regression: a draw rect larger than the source must clip source reads to
 * src->rect. Before the fix this over-read src->pix on the right/bottom edges
 * (heap-buffer-overflow caught by AddressSanitizer). Exercises the OVER path. */
static void test_draw_source_clip(void) {
    printf("[draw_source_clip]\n");
    neverc_image_rgba_t dst, src;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 10, 10));
    neverc_image_rgba_init(&src, neverc_rect(0, 0, 4, 4));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            neverc_image_rgba_set(&src, x, y, 10, 20, 30, 255);

    neverc_draw(&dst, neverc_rect(0, 0, 8, 8), &src, neverc_pt(0, 0), NEVERC_DRAW_OVER);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 3, 3, &r, &g, &b, &a);   /* inside source */
    check("src_clip_inside", r == 10 && g == 20 && b == 30 && a == 255);
    neverc_image_rgba_at(&dst, 5, 5, &r, &g, &b, &a);   /* beyond source -> untouched */
    check("src_clip_beyond_untouched", r == 0 && g == 0 && b == 0 && a == 0);

    /* Source mapped entirely off the draw region: clean no-op, no read. */
    neverc_draw(&dst, neverc_rect(0, 0, 8, 8), &src, neverc_pt(100, 100), NEVERC_DRAW_SRC);
    check("src_clip_offscreen_noop", 1);

    neverc_image_rgba_free(&dst);
    neverc_image_rgba_free(&src);
}

/* Regression: draw_gray_over must clip mask reads to mask->rect when the mask is
 * smaller than the draw rect (same over-read class as neverc_draw). */
static void test_draw_gray_clip(void) {
    printf("[draw_gray_clip]\n");
    neverc_image_rgba_t dst;
    neverc_image_gray_t mask;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 10, 10));
    neverc_image_gray_init(&mask, neverc_rect(0, 0, 4, 4));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            neverc_image_gray_set(&mask, x, y, 255);

    neverc_draw_gray_over(&dst, neverc_rect(0, 0, 8, 8), &mask, neverc_pt(0, 0),
                          200, 100, 50, 255);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 1, 1, &r, &g, &b, &a);   /* under mask */
    check("gray_clip_masked", r == 200 && g == 100 && b == 50 && a == 255);
    neverc_image_rgba_at(&dst, 6, 6, &r, &g, &b, &a);   /* beyond mask -> untouched */
    check("gray_clip_beyond_untouched", r == 0 && g == 0 && b == 0 && a == 0);

    neverc_image_gray_free(&mask);
    neverc_image_rgba_free(&dst);
}

/* Regression: translating src/mask by (r.min - origin) can overflow 32-bit
 * int and wrap into dst, so a mapping that is actually off-image looks
 * in-bounds and the row loops read off the source buffer. */
static void test_draw_clip_int_overflow(void) {
    printf("[draw_clip_int_overflow]\n");
    neverc_image_rgba_t dst, src;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 8, 8));
    neverc_image_rgba_init(&src, neverc_rect(INT_MIN, 0, INT_MIN + 4, 4));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            neverc_image_rgba_set(&src, INT_MIN + x, y, 9, 9, 9, 255);

    neverc_draw(&dst, neverc_rect(0, 0, 8, 8), &src, neverc_pt(INT_MAX, 0),
                NEVERC_DRAW_OVER);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 1, 0, &r, &g, &b, &a);
    check("overflow_src_clip_noop", r == 0 && g == 0 && b == 0 && a == 0);

    neverc_image_gray_t mask;
    neverc_image_gray_init(&mask, neverc_rect(INT_MIN, 0, INT_MIN + 4, 4));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            neverc_image_gray_set(&mask, INT_MIN + x, y, 255);
    neverc_draw_gray_over(&dst, neverc_rect(0, 0, 8, 8), &mask,
                          neverc_pt(INT_MAX, 0), 200, 100, 50, 255);
    neverc_image_rgba_at(&dst, 1, 0, &r, &g, &b, &a);
    check("overflow_mask_clip_noop", r == 0 && g == 0 && b == 0 && a == 0);

    neverc_image_gray_free(&mask);
    neverc_image_rgba_free(&dst);
    neverc_image_rgba_free(&src);
}

/* Same-image SRC shifted down must copy bottom-to-top; otherwise each row
 * rereads a destination that was already overwritten. */
static void test_draw_src_self_overlap(void) {
    printf("[draw_src_self_overlap]\n");
    neverc_image_rgba_t img;
    neverc_image_rgba_init(&img, neverc_rect(0, 0, 4, 4));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            neverc_image_rgba_set(&img, x, y, (uint8_t)(y * 10), (uint8_t)x, 0, 255);

    neverc_draw(&img, neverc_rect(0, 1, 4, 4), &img, neverc_pt(0, 0),
                NEVERC_DRAW_SRC);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&img, 0, 1, &r, &g, &b, &a);
    check("self_overlap_row1", r == 0);
    neverc_image_rgba_at(&img, 0, 2, &r, &g, &b, &a);
    check("self_overlap_row2", r == 10);
    neverc_image_rgba_at(&img, 0, 3, &r, &g, &b, &a);
    check("self_overlap_row3", r == 20);
    neverc_image_rgba_at(&img, 0, 0, &r, &g, &b, &a);
    check("self_overlap_row0_unchanged", r == 0);

    neverc_image_rgba_free(&img);
}

int main(void) {
    test_draw_src();
    test_draw_uniform();
    test_draw_over_opaque();
    test_draw_clipping();
    test_draw_source_clip();
    test_draw_gray_clip();
    test_draw_clip_int_overflow();
    test_draw_src_self_overlap();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
