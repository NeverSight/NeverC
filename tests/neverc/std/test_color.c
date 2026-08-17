#include "neverc/std/image/color.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)
#define ASSERT_TRUE(expr) do { tests_run++; if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } } while(0)

static void test_rgba_basic(void) {
    printf("[rgba_basic]\n");
    neverc_color_rgba_t c = neverc_color_rgba(255, 128, 0, 255);
    ASSERT_INT_EQ(c.r, 255);
    ASSERT_INT_EQ(c.g, 128);
    ASSERT_INT_EQ(c.b, 0);
    ASSERT_INT_EQ(c.a, 255);
}

static void test_gray_conversion(void) {
    printf("[gray_conversion]\n");
    neverc_color_rgba_t white = neverc_color_rgba(255, 255, 255, 255);
    neverc_color_gray_t g = neverc_color_rgba_to_gray(white);
    ASSERT_INT_EQ(g.y, 255);

    neverc_color_rgba_t black = neverc_color_rgba(0, 0, 0, 255);
    g = neverc_color_rgba_to_gray(black);
    ASSERT_INT_EQ(g.y, 0);

    neverc_color_rgba_t back = neverc_color_gray_to_rgba(g);
    ASSERT_INT_EQ(back.r, 0);
    ASSERT_INT_EQ(back.a, 255);
}

static void test_nrgba_conversion(void) {
    printf("[nrgba_conversion]\n");
    neverc_color_nrgba_t n = neverc_color_nrgba(255, 128, 0, 128);
    neverc_color_rgba_t r = neverc_color_nrgba_to_rgba(n);
    ASSERT_TRUE(r.r > 120 && r.r < 135);
    ASSERT_INT_EQ(r.a, 128);

    neverc_color_rgba_t opaque = neverc_color_rgba(100, 200, 50, 255);
    neverc_color_nrgba_t nr = neverc_color_rgba_to_nrgba(opaque);
    ASSERT_INT_EQ(nr.r, 100);
    ASSERT_INT_EQ(nr.g, 200);
    ASSERT_INT_EQ(nr.a, 255);

    /* Non-premultiplied (r > a) must clamp, not wrap the uint8_t cast. */
    neverc_color_nrgba_t wrapped = neverc_color_rgba_to_nrgba(
        neverc_color_rgba(255, 0, 0, 1));
    ASSERT_INT_EQ(wrapped.r, 255);
    ASSERT_INT_EQ(wrapped.a, 1);
}

static void test_cmyk_conversion(void) {
    printf("[cmyk_conversion]\n");
    neverc_color_rgba_t red = neverc_color_rgba(255, 0, 0, 255);
    neverc_color_cmyk_t cmyk = neverc_color_rgba_to_cmyk(red);
    ASSERT_INT_EQ(cmyk.c, 0);
    ASSERT_INT_EQ(cmyk.m, 255);
    ASSERT_INT_EQ(cmyk.y, 255);
    ASSERT_INT_EQ(cmyk.k, 0);

    neverc_color_rgba_t back = neverc_color_cmyk_to_rgba(cmyk);
    ASSERT_INT_EQ(back.r, 255);
    ASSERT_INT_EQ(back.g, 0);
    ASSERT_INT_EQ(back.b, 0);
}

static void test_hsl_conversion(void) {
    printf("[hsl_conversion]\n");
    neverc_color_rgba_t red = neverc_color_rgba(255, 0, 0, 255);
    neverc_color_hsl_t hsl = neverc_color_rgba_to_hsl(red);
    ASSERT_TRUE(hsl.h < 0.01f || hsl.h > 0.99f);
    ASSERT_TRUE(hsl.s > 0.99f);
    ASSERT_TRUE(hsl.l > 0.49f && hsl.l < 0.51f);

    neverc_color_rgba_t back = neverc_color_hsl_to_rgba(hsl);
    ASSERT_TRUE(back.r > 250);
    ASSERT_TRUE(back.g < 5);
    ASSERT_TRUE(back.b < 5);

    neverc_color_rgba_t clamped = neverc_color_hsl_to_rgba(
        (neverc_color_hsl_t){2.0f, 2.0f, 2.0f});
    ASSERT_INT_EQ(clamped.r, 255);
    ASSERT_INT_EQ(clamped.g, 255);
    ASSERT_INT_EQ(clamped.b, 255);
}

static void test_hex_conversion(void) {
    printf("[hex_conversion]\n");
    neverc_color_rgba_t c = neverc_color_rgba(0xFF, 0x80, 0x00, 0xFF);
    uint32_t hex = neverc_color_rgba_to_hex(c);
    ASSERT_TRUE(hex == 0xFF8000FF);

    neverc_color_rgba_t back = neverc_color_hex_to_rgba(hex);
    ASSERT_TRUE(neverc_color_equal(c, back));
}

static void test_parse_hex(void) {
    printf("[parse_hex]\n");
    neverc_color_rgba_t c;
    ASSERT_INT_EQ(neverc_color_parse_hex("#FF8000", &c), 0);
    ASSERT_INT_EQ(c.r, 0xFF);
    ASSERT_INT_EQ(c.g, 0x80);
    ASSERT_INT_EQ(c.b, 0x00);
    ASSERT_INT_EQ(c.a, 0xFF);

    ASSERT_INT_EQ(neverc_color_parse_hex("FF000080", &c), 0);
    ASSERT_INT_EQ(c.r, 0xFF);
    ASSERT_INT_EQ(c.a, 0x80);

    ASSERT_INT_EQ(neverc_color_parse_hex("#F00", &c), 0);
    ASSERT_INT_EQ(c.r, 0xFF);
    ASSERT_INT_EQ(c.g, 0x00);
    ASSERT_INT_EQ(c.b, 0x00);

    c = neverc_color_rgba(1, 2, 3, 4);
    ASSERT_INT_EQ(neverc_color_parse_hex("#GGGGGG", &c), -1);
    ASSERT_TRUE(neverc_color_equal(c, neverc_color_rgba(1, 2, 3, 4)));
    ASSERT_INT_EQ(neverc_color_parse_hex("#FF00GG", &c), -1);
    ASSERT_TRUE(neverc_color_equal(c, neverc_color_rgba(1, 2, 3, 4)));
    ASSERT_INT_EQ(neverc_color_parse_hex("#xyz", &c), -1);
    ASSERT_TRUE(neverc_color_equal(c, neverc_color_rgba(1, 2, 3, 4)));
    ASSERT_INT_EQ(neverc_color_parse_hex("#FF8000", NULL), -1);
}

static void test_lerp(void) {
    printf("[lerp]\n");
    neverc_color_rgba_t a = neverc_color_rgba(0, 0, 0, 255);
    neverc_color_rgba_t b = neverc_color_rgba(255, 255, 255, 255);
    neverc_color_rgba_t mid = neverc_color_lerp(a, b, 0.5f);
    ASSERT_TRUE(mid.r > 125 && mid.r < 130);
    ASSERT_TRUE(mid.g > 125 && mid.g < 130);

    neverc_color_rgba_t below = neverc_color_lerp(a, b, -1.0f);
    ASSERT_TRUE(neverc_color_equal(below, a));
    neverc_color_rgba_t above = neverc_color_lerp(a, b, 2.0f);
    ASSERT_TRUE(neverc_color_equal(above, b));
}

static void test_equal(void) {
    printf("[equal]\n");
    neverc_color_rgba_t a = neverc_color_rgba(1, 2, 3, 4);
    neverc_color_rgba_t b = neverc_color_rgba(1, 2, 3, 4);
    neverc_color_rgba_t c = neverc_color_rgba(1, 2, 3, 5);
    ASSERT_TRUE(neverc_color_equal(a, b));
    ASSERT_TRUE(!neverc_color_equal(a, c));
}

int main(void) {
    printf("=== NeverC image/color Tests ===\n");
    test_rgba_basic();
    test_gray_conversion();
    test_nrgba_conversion();
    test_cmyk_conversion();
    test_hsl_conversion();
    test_hex_conversion();
    test_parse_hex();
    test_lerp();
    test_equal();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
