#include "neverc/std/image/image.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0;

static void check(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else printf("  FAIL: %s\n", name);
}

static void test_point(void) {
    printf("[point]\n");
    neverc_point_t p = neverc_pt(3, 4);
    check("pt", p.x == 3 && p.y == 4);
    neverc_point_t q = neverc_point_add(p, neverc_pt(1, 2));
    check("add", q.x == 4 && q.y == 6);
    q = neverc_point_sub(p, neverc_pt(1, 1));
    check("sub", q.x == 2 && q.y == 3);
    q = neverc_point_mul(p, 3);
    check("mul", q.x == 9 && q.y == 12);
    q = neverc_point_div(neverc_pt(10, 6), 2);
    check("div", q.x == 5 && q.y == 3);
    q = neverc_point_div(neverc_pt(10, 6), 0);
    check("div by zero", q.x == 10 && q.y == 6);
    q = neverc_point_div(neverc_pt(INT_MIN, INT_MIN), -1);
    check("div INT_MIN / -1", q.x == INT_MIN && q.y == INT_MIN);
    q = neverc_point_mul(neverc_pt(INT_MIN, 2), -1);
    check("mul INT_MIN * -1 saturates", q.x == INT_MAX && q.y == -2);
    q = neverc_point_add(neverc_pt(INT_MAX, INT_MIN), neverc_pt(1, -1));
    check("add saturates", q.x == INT_MAX && q.y == INT_MIN);
    q = neverc_point_sub(neverc_pt(INT_MIN, INT_MAX), neverc_pt(1, -1));
    check("sub saturates", q.x == INT_MIN && q.y == INT_MAX);
    check("eq_true", neverc_point_eq(neverc_pt(1,2), neverc_pt(1,2)));
    check("eq_false", !neverc_point_eq(neverc_pt(1,2), neverc_pt(1,3)));
    check("in_true", neverc_point_in(neverc_pt(5,5), neverc_rect(0,0,10,10)));
    check("in_false", !neverc_point_in(neverc_pt(10,5), neverc_rect(0,0,10,10)));
    check("in_edge", neverc_point_in(neverc_pt(0,0), neverc_rect(0,0,10,10)));
    neverc_point_t m = neverc_point_mod(neverc_pt(15, 7), neverc_rect(0,0,10,5));
    check("mod", m.x == 5 && m.y == 2);
    m = neverc_point_mod(neverc_pt(15, 7), neverc_rect(5,5,5,9));
    check("mod empty rectangle", m.x == 5 && m.y == 5);
    neverc_rect_t huge = {{INT_MIN, INT_MIN}, {INT_MAX, INT_MAX}};
    m = neverc_point_mod(neverc_pt(INT_MAX, INT_MAX), huge);
    check("mod wide rectangle", m.x == INT_MIN && m.y == INT_MIN);
}

static void test_rect(void) {
    printf("[rectangle]\n");
    neverc_rect_t r = neverc_rect(1, 2, 11, 12);
    check("dx", neverc_rect_dx(r) == 10);
    check("dy", neverc_rect_dy(r) == 10);
    check("empty_false", !neverc_rect_empty(r));
    check("empty_true", neverc_rect_empty(neverc_rect(5,5,5,5)));

    neverc_rect_t moved = neverc_rect_add(r, neverc_pt(5, 5));
    check("add", moved.min.x == 6 && moved.min.y == 7 &&
                 moved.max.x == 16 && moved.max.y == 17);

    neverc_rect_t back = neverc_rect_sub(moved, neverc_pt(5, 5));
    check("sub", back.min.x == 1 && back.min.y == 2 &&
                 back.max.x == 11 && back.max.y == 12);

    neverc_rect_t inset = neverc_rect_inset(neverc_rect(0,0,20,20), 5);
    check("inset", inset.min.x == 5 && inset.max.x == 15);

    neverc_rect_t a = neverc_rect(0,0,10,10);
    neverc_rect_t b = neverc_rect(5,5,15,15);
    neverc_rect_t inter = neverc_rect_intersect(a, b);
    check("intersect", inter.min.x == 5 && inter.min.y == 5 &&
                        inter.max.x == 10 && inter.max.y == 10);

    neverc_rect_t uni = neverc_rect_union(a, b);
    check("union", uni.min.x == 0 && uni.min.y == 0 &&
                    uni.max.x == 15 && uni.max.y == 15);

    check("overlaps_true", neverc_rect_overlaps(a, b));
    check("overlaps_false", !neverc_rect_overlaps(neverc_rect(0,0,5,5), neverc_rect(5,5,10,10)));

    check("in_true", neverc_rect_in(neverc_rect(2,2,5,5), neverc_rect(0,0,10,10)));
    check("in_false", !neverc_rect_in(neverc_rect(0,0,15,15), neverc_rect(0,0,10,10)));

    check("eq_true", neverc_rect_eq(neverc_rect(1,2,3,4), neverc_rect(1,2,3,4)));
    check("eq_empty", neverc_rect_eq(neverc_rect(0,0,0,0), neverc_rect(5,5,5,5)));

    neverc_rect_t canon = neverc_rect_canon((neverc_rect_t){{10,10},{0,0}});
    check("canon", canon.min.x == 0 && canon.max.x == 10);

    neverc_rect_t no_overlap = neverc_rect_intersect(neverc_rect(0,0,5,5), neverc_rect(10,10,20,20));
    check("intersect_empty", neverc_rect_empty(no_overlap));

    neverc_rect_t overflowed = neverc_rect_add(neverc_rect(INT_MAX - 2, 0, INT_MAX, 4),
                                              neverc_pt(8, 0));
    check("rect add saturates",
          overflowed.min.x == INT_MAX && overflowed.max.x == INT_MAX);
    neverc_rect_t collapsed = neverc_rect_inset(neverc_rect(0, 0, 10, 10), 100);
    check("inset collapse is empty", neverc_rect_empty(collapsed));
    neverc_rect_t outset = neverc_rect_inset(neverc_rect(0, 0, 10, 10), INT_MIN);
    check("inset INT_MIN does not wrap",
          outset.min.x == INT_MIN && outset.max.x == INT_MAX);
}

static void test_rgba_image(void) {
    printf("[rgba_image]\n");
    neverc_image_rgba_t img;
    int rc = neverc_image_rgba_init(&img, neverc_rect(0, 0, 100, 100));
    check("init", rc == 0);
    check("bounds", neverc_rect_dx(img.rect) == 100 && neverc_rect_dy(img.rect) == 100);
    check("bounds accessor",
          neverc_rect_eq(neverc_image_rgba_bounds(&img), img.rect));

    neverc_image_rgba_set(&img, 10, 20, 255, 128, 64, 200);
    uint8_t r, g, b, a;
    neverc_image_rgba_at(&img, 10, 20, &r, &g, &b, &a);
    check("set_get_r", r == 255);
    check("set_get_g", g == 128);
    check("set_get_b", b == 64);
    check("set_get_a", a == 200);

    neverc_image_rgba_at(&img, 0, 0, &r, &g, &b, &a);
    check("default_zero", r == 0 && g == 0 && b == 0 && a == 0);

    neverc_image_rgba_at(&img, -1, -1, &r, &g, &b, &a);
    check("out_of_bounds", r == 0 && g == 0 && b == 0 && a == 0);

    check("pixel_offset", neverc_image_rgba_pixel_offset(&img, 10, 20) == 20 * 400 + 10 * 4);

    neverc_image_rgba_at(NULL, 0, 0, &r, &g, &b, &a);
    check("at null image", r == 0 && g == 0 && b == 0 && a == 0);
    neverc_image_rgba_set(NULL, 0, 0, 1, 2, 3, 4);
    check("set null image", 1);
    check("null bounds empty", neverc_rect_empty(neverc_image_rgba_bounds(NULL)));

    neverc_image_rgba_free(&img);
    check("free", img.pix == NULL);
}

static void test_gray_image(void) {
    printf("[gray_image]\n");
    neverc_image_gray_t img;
    int rc = neverc_image_gray_init(&img, neverc_rect(0, 0, 50, 50));
    check("init", rc == 0);
    check("gray bounds accessor",
          neverc_rect_eq(neverc_image_gray_bounds(&img), img.rect));

    neverc_image_gray_set(&img, 10, 10, 128);
    check("set_get", neverc_image_gray_at(&img, 10, 10) == 128);
    check("gray pixel_offset",
          neverc_image_gray_pixel_offset(&img, 10, 10) == 10 * 50 + 10);
    check("default_zero", neverc_image_gray_at(&img, 0, 0) == 0);
    check("out_of_bounds", neverc_image_gray_at(&img, -1, -1) == 0);

    neverc_image_gray_free(&img);
    check("free", img.pix == NULL);
}

static void test_invalid_image_sizes(void) {
    printf("[invalid image sizes]\n");

    neverc_rect_t huge = {{INT_MIN, 0}, {INT_MAX, 1}};
    check("dx saturates overflowing width",
          neverc_rect_dx(huge) == INT_MAX);
    check("dy of inverted extremes saturates",
          neverc_rect_dy((neverc_rect_t){{0, INT_MAX}, {1, INT_MIN}}) == INT_MIN);
    neverc_image_rgba_t rgba;
    check("rgba rejects overflowing width",
          neverc_image_rgba_init(&rgba, huge) == -1);
    check("rgba failure clears image",
          rgba.pix == NULL && rgba.stride == 0);

    neverc_image_gray_t gray;
    check("gray rejects overflowing width",
          neverc_image_gray_init(&gray, huge) == -1);
    check("gray failure clears image",
          gray.pix == NULL && gray.stride == 0);
}

int main(void) {
    test_point();
    test_rect();
    test_rgba_image();
    test_gray_image();
    test_invalid_image_sizes();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
