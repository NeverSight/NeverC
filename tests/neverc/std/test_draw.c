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

static void test_draw_gray_offset_view_overlap(void) {
    printf("[draw_gray_offset_view_overlap]\n");
    uint8_t backing[8] = {0, 255, 255, 0, 0, 0, 0, 0};
    neverc_image_rgba_t dst = {
        backing, 8, {{0, 0}, {2, 1}}
    };
    neverc_image_gray_t mask = {
        backing + 1, 2, {{0, 0}, {2, 1}}
    };
    neverc_draw_gray_over(&dst, dst.rect, &mask, neverc_pt(0, 0),
                           255, 0, 0, 255);
    check("gray alias first pixel",
          backing[0] == 255 && backing[1] == 0 &&
          backing[2] == 0 && backing[3] == 255);
    check("gray alias second pixel uses original mask",
          backing[4] == 255 && backing[5] == 0 &&
          backing[6] == 0 && backing[7] == 255);
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

/* Different image views may share one allocation while their pix pointers
 * differ.  A top-to-bottom copy would overwrite the second source row before
 * reading it. */
static void test_draw_offset_view_overlap(void) {
    printf("[draw_offset_view_overlap]\n");
    uint8_t backing[3 * 8] = {
        10, 1, 2, 255, 11, 1, 2, 255,
        20, 3, 4, 255, 21, 3, 4, 255,
        30, 5, 6, 255, 31, 5, 6, 255,
    };
    neverc_image_rgba_t src = {
        backing, 8, {{0, 0}, {2, 2}}
    };
    neverc_image_rgba_t dst = {
        backing + 8, 8, {{0, 1}, {2, 3}}
    };

    neverc_draw(&dst, dst.rect, &src, neverc_pt(0, 0), NEVERC_DRAW_SRC);
    check("offset_src_row1", backing[8] == 10 && backing[12] == 11);
    check("offset_src_row2", backing[16] == 20 && backing[20] == 21);

    const uint8_t over_initial[3 * 8] = {
        20, 0, 0, 128, 21, 0, 0, 128,
        40, 0, 0, 128, 41, 0, 0, 128,
        200, 0, 0, 255, 201, 0, 0, 255,
    };
    memcpy(backing, over_initial, sizeof(backing));
    neverc_draw(&dst, dst.rect, &src, neverc_pt(0, 0), NEVERC_DRAW_OVER);
    check("offset_over_row2_uses_original_source",
          backing[16] == 140 && backing[20] == 141);
}

/* Opaque OVER is a copy; same-buffer shift-down/right must not reread
 * pixels already written (Go drawCopyOver walks bottom-to-top / right-to-left). */
static void test_draw_over_self_overlap(void) {
    printf("[draw_over_self_overlap]\n");
    neverc_image_rgba_t img;
    neverc_image_rgba_init(&img, neverc_rect(0, 0, 4, 4));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            neverc_image_rgba_set(&img, x, y, (uint8_t)(y * 10), (uint8_t)x, 0, 255);

    neverc_draw(&img, neverc_rect(0, 1, 4, 4), &img, neverc_pt(0, 0),
                NEVERC_DRAW_OVER);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&img, 0, 1, &r, &g, &b, &a);
    check("over_self_overlap_row1", r == 0);
    neverc_image_rgba_at(&img, 0, 2, &r, &g, &b, &a);
    check("over_self_overlap_row2", r == 10);
    neverc_image_rgba_at(&img, 0, 3, &r, &g, &b, &a);
    check("over_self_overlap_row3", r == 20);
    neverc_image_rgba_at(&img, 0, 0, &r, &g, &b, &a);
    check("over_self_overlap_row0_unchanged", r == 0);

    neverc_image_rgba_free(&img);

    neverc_image_rgba_init(&img, neverc_rect(0, 0, 4, 1));
    neverc_image_rgba_set(&img, 0, 0, 10, 0, 0, 255);
    neverc_image_rgba_set(&img, 1, 0, 20, 0, 0, 255);
    neverc_image_rgba_set(&img, 2, 0, 30, 0, 0, 255);
    neverc_image_rgba_set(&img, 3, 0, 40, 0, 0, 255);
    neverc_draw(&img, neverc_rect(1, 0, 4, 1), &img, neverc_pt(0, 0),
                NEVERC_DRAW_OVER);
    neverc_image_rgba_at(&img, 0, 0, &r, &g, &b, &a);
    check("over_self_overlap_col0", r == 10);
    neverc_image_rgba_at(&img, 1, 0, &r, &g, &b, &a);
    check("over_self_overlap_col1", r == 10);
    neverc_image_rgba_at(&img, 2, 0, &r, &g, &b, &a);
    check("over_self_overlap_col2", r == 20);
    neverc_image_rgba_at(&img, 3, 0, &r, &g, &b, &a);
    check("over_self_overlap_col3", r == 30);

    neverc_image_rgba_free(&img);
}

/* OVER of (255,255,255,128) onto opaque white used to wrap: the unclamped
 * Porter-Duff sum is 382, which truncated to uint8 126. */
static void test_draw_over_src_exceeds_alpha(void) {
    printf("[draw_over_src_exceeds_alpha]\n");
    neverc_image_rgba_t dst;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 4, 4));
    neverc_draw_uniform(&dst, neverc_rect(0, 0, 4, 4),
                        255, 255, 255, 255, NEVERC_DRAW_SRC);
    neverc_draw_uniform(&dst, neverc_rect(0, 0, 4, 4),
                        255, 255, 255, 128, NEVERC_DRAW_OVER);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 1, 1, &r, &g, &b, &a);
    check("over_white_no_wrap_r", r == 255);
    check("over_white_no_wrap_g", g == 255);
    check("over_white_no_wrap_b", b == 255);
    check("over_white_no_wrap_a", a == 255);

    neverc_image_rgba_t src;
    neverc_image_rgba_init(&src, neverc_rect(0, 0, 4, 4));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            neverc_image_rgba_set(&src, x, y, 255, 255, 255, 128);
    neverc_draw_uniform(&dst, neverc_rect(0, 0, 4, 4),
                        255, 255, 255, 255, NEVERC_DRAW_SRC);
    neverc_draw(&dst, neverc_rect(0, 0, 4, 4), &src, neverc_pt(0, 0),
                NEVERC_DRAW_OVER);
    neverc_image_rgba_at(&dst, 2, 2, &r, &g, &b, &a);
    check("over_pixel_white_no_wrap", r == 255 && g == 255 && b == 255 && a == 255);

    neverc_image_rgba_free(&src);
    neverc_image_rgba_free(&dst);
}

/* ca=0 must not mutate dest even when the mask is fully opaque. */
static void test_draw_gray_over_transparent(void) {
    printf("[draw_gray_over_transparent]\n");
    neverc_image_rgba_t dst;
    neverc_image_gray_t mask;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 4, 4));
    neverc_image_gray_init(&mask, neverc_rect(0, 0, 4, 4));
    neverc_draw_uniform(&dst, neverc_rect(0, 0, 4, 4),
                        10, 20, 30, 255, NEVERC_DRAW_SRC);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            neverc_image_gray_set(&mask, x, y, 255);

    neverc_draw_gray_over(&dst, neverc_rect(0, 0, 4, 4), &mask, neverc_pt(0, 0),
                          255, 255, 255, 0);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 1, 1, &r, &g, &b, &a);
    check("gray_over_ca0_unchanged", r == 10 && g == 20 && b == 30 && a == 255);

    neverc_image_gray_free(&mask);
    neverc_image_rgba_free(&dst);
}

static void test_draw_zero_stride_noop(void) {
    printf("[draw_zero_stride_noop]\n");
    uint8_t dst_pix[16];
    uint8_t src_pix[16];
    memset(dst_pix, 0, sizeof(dst_pix));
    memset(src_pix, 9, sizeof(src_pix));
    neverc_image_rgba_t dst = {
        .pix = dst_pix, .stride = 0, .rect = {{0, 0}, {2, 2}}
    };
    neverc_image_rgba_t src = {
        .pix = src_pix, .stride = 8, .rect = {{0, 0}, {2, 2}}
    };
    neverc_draw(&dst, neverc_rect(0, 0, 2, 2), &src, neverc_pt(0, 0),
                NEVERC_DRAW_SRC);
    check("zero dst stride is a no-op", dst_pix[0] == 0);

    dst.stride = 8;
    src.stride = 0;
    neverc_draw(&dst, neverc_rect(0, 0, 2, 2), &src, neverc_pt(0, 0),
                NEVERC_DRAW_OVER);
    check("zero src stride is a no-op", dst_pix[0] == 0);

    neverc_draw_uniform(&dst, neverc_rect(0, 0, 2, 2),
                        1, 2, 3, 255, NEVERC_DRAW_SRC);
    check("uniform wrote with valid stride", dst_pix[0] == 1);
    dst.stride = 0;
    neverc_draw_uniform(&dst, neverc_rect(0, 0, 2, 2),
                        9, 9, 9, 255, NEVERC_DRAW_SRC);
    check("uniform zero stride is a no-op", dst_pix[0] == 1);
}

static void test_draw_clip_wider_than_stride(void) {
    printf("[draw_clip_wider_than_stride]\n");
    uint8_t dst_pix[16];
    uint8_t src_pix[16];
    memset(dst_pix, 0, sizeof(dst_pix));
    memset(src_pix, 9, sizeof(src_pix));
    /* INT_MAX-INT_MIN pixels * 4 bytes exceeds stride 8, so a row copy
     * would walk off pix if the stride check were missing. */
    neverc_image_rgba_t dst = {
        .pix = dst_pix, .stride = 8,
        .rect = {{INT_MIN, 0}, {INT_MAX, 1}}
    };
    neverc_image_rgba_t src = {
        .pix = src_pix, .stride = 8,
        .rect = {{INT_MIN, 0}, {INT_MAX, 1}}
    };
    neverc_draw(&dst, dst.rect, &src, neverc_pt(INT_MIN, 0), NEVERC_DRAW_SRC);
    check("wide src blit is a no-op", dst_pix[0] == 0);

    neverc_draw_uniform(&dst, dst.rect, 9, 9, 9, 255, NEVERC_DRAW_SRC);
    check("wide uniform is a no-op", dst_pix[0] == 0);

    neverc_image_gray_t mask = {
        .pix = src_pix, .stride = 1,
        .rect = {{INT_MIN, 0}, {INT_MAX, 1}}
    };
    neverc_draw_gray_over(&dst, dst.rect, &mask, neverc_pt(INT_MIN, 0),
                          9, 9, 9, 255);
    check("wide gray-over is a no-op", dst_pix[0] == 0);
}

/* stride < clip_width catches a full-span blit, but a *narrow* clip at a
 * large x of a rect wider than stride/4 used to pass, then write at
 * pix+(x-min.x)*4 past the pitch (and off a stride*height buffer). */
static void test_draw_clip_past_stride(void) {
    printf("[draw_clip_past_stride]\n");
    uint8_t dst_pix[64];
    uint8_t src_pix[16];
    memset(dst_pix, 0xAA, sizeof(dst_pix));
    memset(src_pix, 0x09, sizeof(src_pix));

    neverc_image_rgba_t dst = {
        .pix = dst_pix, .stride = 16, .rect = {{0, 0}, {8, 1}}
    };
    neverc_draw_uniform(&dst, neverc_rect(4, 0, 6, 1),
                        1, 2, 3, 255, NEVERC_DRAW_SRC);
    check("uniform past pitch is a no-op", dst_pix[0] == 0xAA);
    check("uniform did not write past pitch", dst_pix[16] == 0xAA);

    neverc_draw_uniform(&dst, neverc_rect(2, 0, 4, 1),
                        1, 2, 3, 255, NEVERC_DRAW_SRC);
    check("uniform within pitch writes", dst_pix[8] == 1 && dst_pix[11] == 255);
    check("uniform within pitch leaves canary", dst_pix[16] == 0xAA);

    memset(dst_pix, 0xAA, sizeof(dst_pix));
    neverc_image_rgba_t src = {
        .pix = src_pix, .stride = 16, .rect = {{0, 0}, {8, 1}}
    };
    neverc_draw(&dst, neverc_rect(4, 0, 6, 1), &src, neverc_pt(4, 0),
                NEVERC_DRAW_SRC);
    check("src blit past pitch is a no-op", dst_pix[0] == 0xAA);
    check("src blit did not write past pitch", dst_pix[16] == 0xAA);

    neverc_image_gray_t mask = {
        .pix = src_pix, .stride = 4, .rect = {{0, 0}, {8, 1}}
    };
    neverc_draw_gray_over(&dst, neverc_rect(4, 0, 6, 1), &mask,
                          neverc_pt(4, 0), 9, 9, 9, 255);
    check("gray-over past pitch is a no-op", dst_pix[0] == 0xAA);
    check("gray-over did not write past pitch", dst_pix[16] == 0xAA);

    /* Far-right 1-pixel clip of INT_MIN..INT_MAX with a tiny stride: column
     * offset is ~2^32 pixels, which used to look like an in-pitch write. */
    memset(dst_pix, 0xAA, sizeof(dst_pix));
    dst.stride = 8;
    dst.rect = (neverc_rect_t){{INT_MIN, 0}, {INT_MAX, 1}};
    neverc_draw_uniform(&dst, neverc_rect(INT_MAX - 1, 0, INT_MAX, 1),
                        9, 9, 9, 255, NEVERC_DRAW_SRC);
    check("far clip of huge rect is a no-op", dst_pix[0] == 0xAA);
    int far_clean = 1;
    for (int i = 0; i < 64; i++)
        if (dst_pix[i] != 0xAA) far_clean = 0;
    check("far clip did not mutate buffer", far_clean);
}

/* Src replaces dest (including alpha). Over of a fully transparent
 * premultiplied color is a no-op. Matches Go draw.Src vs draw.Over on RGBA. */
static void test_draw_src_vs_over_alpha(void) {
    printf("[draw_src_vs_over_alpha]\n");
    neverc_image_rgba_t dst;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 2, 2));
    neverc_draw_uniform(&dst, neverc_rect(0, 0, 2, 2),
                        10, 20, 30, 255, NEVERC_DRAW_SRC);
    neverc_draw_uniform(&dst, neverc_rect(0, 0, 2, 2),
                        40, 50, 60, 128, NEVERC_DRAW_SRC);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&dst, 0, 0, &r, &g, &b, &a);
    check("src replaces including alpha",
          r == 40 && g == 50 && b == 60 && a == 128);

    neverc_draw_uniform(&dst, neverc_rect(0, 0, 2, 2),
                        10, 20, 30, 255, NEVERC_DRAW_SRC);
    neverc_draw_uniform(&dst, neverc_rect(0, 0, 2, 2),
                        0, 0, 0, 0, NEVERC_DRAW_OVER);
    neverc_image_rgba_at(&dst, 0, 0, &r, &g, &b, &a);
    check("over ca0 is a no-op",
          r == 10 && g == 20 && b == 30 && a == 255);

    neverc_image_rgba_free(&dst);
}

static void test_draw_null(void) {
    printf("[draw_null]\n");
    neverc_image_rgba_t dst;
    neverc_image_rgba_init(&dst, neverc_rect(0, 0, 2, 2));
    neverc_draw(NULL, neverc_rect(0, 0, 2, 2), &dst, neverc_pt(0, 0),
                NEVERC_DRAW_SRC);
    neverc_draw(&dst, neverc_rect(0, 0, 2, 2), NULL, neverc_pt(0, 0),
                NEVERC_DRAW_SRC);
    neverc_draw_uniform(NULL, neverc_rect(0, 0, 2, 2), 1, 2, 3, 4,
                        NEVERC_DRAW_SRC);
    neverc_draw_gray_over(NULL, neverc_rect(0, 0, 2, 2), NULL, neverc_pt(0, 0),
                          1, 2, 3, 4);
    check("null draw is a no-op", 1);
    neverc_image_rgba_free(&dst);
}

int main(void) {
    test_draw_src();
    test_draw_uniform();
    test_draw_over_opaque();
    test_draw_clipping();
    test_draw_source_clip();
    test_draw_gray_clip();
    test_draw_gray_offset_view_overlap();
    test_draw_clip_int_overflow();
    test_draw_src_self_overlap();
    test_draw_offset_view_overlap();
    test_draw_over_self_overlap();
    test_draw_over_src_exceeds_alpha();
    test_draw_gray_over_transparent();
    test_draw_zero_stride_noop();
    test_draw_clip_wider_than_stride();
    test_draw_clip_past_stride();
    test_draw_src_vs_over_alpha();
    test_draw_null();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
