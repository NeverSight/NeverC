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

    /* Go GrayModel: RGBA{1,2,3,255} → Y 1, not the 8-bit JFIF shortcut (2). */
    g = neverc_color_rgba_to_gray(neverc_color_rgba(1, 2, 3, 255));
    ASSERT_INT_EQ(g.y, 1);
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

    /* Go NRGBA.RGBA + RGBA model: 200*257*200/255 >> 8 == 157, not 156
     * from the 8-bit (r*a/255) shortcut. */
    neverc_color_rgba_t go_pre = neverc_color_nrgba_to_rgba(
        neverc_color_nrgba(200, 200, 200, 200));
    ASSERT_INT_EQ(go_pre.r, 157);
    ASSERT_INT_EQ(go_pre.g, 157);
    ASSERT_INT_EQ(go_pre.b, 157);
    ASSERT_INT_EQ(go_pre.a, 200);

    /* Go nrgbaModel: r16=r*257, a16=a*257, (r16*0xffff)/a16 >> 8.
     * 50*257*65535/ (51*257) >> 8 == 250, not 249 from (r*255/a). */
    neverc_color_nrgba_t go_unpre = neverc_color_rgba_to_nrgba(
        neverc_color_rgba(50, 0, 0, 51));
    ASSERT_INT_EQ(go_unpre.r, 250);
    ASSERT_INT_EQ(go_unpre.g, 0);
    ASSERT_INT_EQ(go_unpre.b, 0);
    ASSERT_INT_EQ(go_unpre.a, 51);

    /* Non-premultiplied (r > a) must clamp, not wrap the uint8_t cast. */
    neverc_color_nrgba_t wrapped = neverc_color_rgba_to_nrgba(
        neverc_color_rgba(255, 0, 0, 1));
    ASSERT_INT_EQ(wrapped.r, 255);
    ASSERT_INT_EQ(wrapped.a, 1);

    /* a=0 must not divide; channels stay 0 (no uint8 wrap of r*a). */
    neverc_color_rgba_t clear = neverc_color_nrgba_to_rgba(
        neverc_color_nrgba(255, 128, 64, 0));
    ASSERT_INT_EQ(clear.r, 0);
    ASSERT_INT_EQ(clear.g, 0);
    ASSERT_INT_EQ(clear.b, 0);
    ASSERT_INT_EQ(clear.a, 0);
    neverc_color_nrgba_t from_clear = neverc_color_rgba_to_nrgba(
        neverc_color_rgba(255, 128, 64, 0));
    ASSERT_INT_EQ(from_clear.r, 0);
    ASSERT_INT_EQ(from_clear.a, 0);
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

    /* k=255, c=m=y=255: w=0, w*(255-c)/255 must stay 0 (no uint8 wrap). */
    neverc_color_rgba_t black = neverc_color_cmyk_to_rgba(
        (neverc_color_cmyk_t){255, 255, 255, 255});
    ASSERT_INT_EQ(black.r, 0);
    ASSERT_INT_EQ(black.g, 0);
    ASSERT_INT_EQ(black.b, 0);

    /* Go CMYKToRGB: {100,0,0,50} → R 125, not 8-bit 124. */
    neverc_color_rgba_t go_cmyk = neverc_color_cmyk_to_rgba(
        (neverc_color_cmyk_t){100, 0, 0, 50});
    ASSERT_INT_EQ(go_cmyk.r, 125);
    ASSERT_INT_EQ(go_cmyk.g, 205);
    ASSERT_INT_EQ(go_cmyk.b, 205);
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

    neverc_color_rgba_t white_l = neverc_color_hsl_to_rgba(
        (neverc_color_hsl_t){0.0f, 0.0f, 1.0f});
    ASSERT_INT_EQ(white_l.r, 255);
    ASSERT_INT_EQ(white_l.g, 255);
    ASSERT_INT_EQ(white_l.b, 255);
    neverc_color_rgba_t white_sat = neverc_color_hsl_to_rgba(
        (neverc_color_hsl_t){0.3f, 1.0f, 1.0f});
    ASSERT_INT_EQ(white_sat.r, 255);
    ASSERT_INT_EQ(white_sat.g, 255);
    ASSERT_INT_EQ(white_sat.b, 255);
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

    /* +0.5 rounding of a 255 channel used to produce 255.5, which is
     * undefined as uint8_t (and wrapped to 0 on some targets). */
    neverc_color_rgba_t white = neverc_color_rgba(255, 255, 255, 255);
    neverc_color_rgba_t same = neverc_color_lerp(white, white, 0.5f);
    ASSERT_TRUE(neverc_color_equal(same, white));
    neverc_color_rgba_t near = neverc_color_lerp(white, a, 0.001f);
    ASSERT_TRUE(near.r > 250 && near.g > 250 && near.b > 250);

    /* NaN is treated as t=0 (return a), not a uint8 conversion of NaN. */
    neverc_color_rgba_t nan_t = neverc_color_lerp(a, b, NAN);
    ASSERT_TRUE(neverc_color_equal(nan_t, a));
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
