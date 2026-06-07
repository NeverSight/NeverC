#include "neverc/image/png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(expr) do { \
    tests_run++; \
    if (expr) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s\n", __LINE__, #expr); } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    int _a = (int)(a), _b = (int)(b); tests_run++; \
    if (_a == _b) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s = %d, expected %d\n", __LINE__, #a, _a, _b); } \
} while(0)

static void test_encode_decode_rgba(void) {
    printf("[encode_decode_rgba]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 4;
    img.height = 4;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_TRUECOLOR_ALPHA;
    img.channels = 4;
    img.stride = 4 * 4;
    img.pixels = (uint8_t *)calloc(1, img.height * img.stride);

    /* Red pixel at (0,0), green at (1,0), blue at (2,0), white at (3,0) */
    uint8_t red[] = {255, 0, 0, 255};
    uint8_t green[] = {0, 255, 0, 255};
    uint8_t blue[] = {0, 0, 255, 255};
    uint8_t white[] = {255, 255, 255, 255};
    neverc_png_pixel_set(&img, 0, 0, red);
    neverc_png_pixel_set(&img, 1, 0, green);
    neverc_png_pixel_set(&img, 2, 0, blue);
    neverc_png_pixel_set(&img, 3, 0, white);

    uint8_t *png_data = NULL;
    size_t png_len = 0;
    int rc = neverc_png_encode(&img, &png_data, &png_len);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(png_data != NULL);
    ASSERT_TRUE(png_len > 0);

    /* Verify PNG signature */
    ASSERT_EQ(png_data[0], 137);
    ASSERT_EQ(png_data[1], 80);
    ASSERT_EQ(png_data[2], 78);
    ASSERT_EQ(png_data[3], 71);

    /* Decode it back */
    neverc_png_image_t decoded;
    rc = neverc_png_decode(png_data, png_len, &decoded);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(decoded.width, 4);
    ASSERT_EQ(decoded.height, 4);
    ASSERT_EQ(decoded.channels, 4);

    /* Verify pixels */
    const uint8_t *p;
    p = neverc_png_pixel_at(&decoded, 0, 0);
    ASSERT_TRUE(p != NULL);
    ASSERT_EQ(p[0], 255); ASSERT_EQ(p[1], 0); ASSERT_EQ(p[2], 0); ASSERT_EQ(p[3], 255);

    p = neverc_png_pixel_at(&decoded, 1, 0);
    ASSERT_EQ(p[0], 0); ASSERT_EQ(p[1], 255); ASSERT_EQ(p[2], 0); ASSERT_EQ(p[3], 255);

    p = neverc_png_pixel_at(&decoded, 2, 0);
    ASSERT_EQ(p[0], 0); ASSERT_EQ(p[1], 0); ASSERT_EQ(p[2], 255); ASSERT_EQ(p[3], 255);

    p = neverc_png_pixel_at(&decoded, 3, 0);
    ASSERT_EQ(p[0], 255); ASSERT_EQ(p[1], 255); ASSERT_EQ(p[2], 255); ASSERT_EQ(p[3], 255);

    /* Black pixel at (0,1) - was initialized to zero */
    p = neverc_png_pixel_at(&decoded, 0, 1);
    ASSERT_EQ(p[0], 0); ASSERT_EQ(p[1], 0); ASSERT_EQ(p[2], 0); ASSERT_EQ(p[3], 0);

    free(png_data);
    neverc_png_free(&decoded);
    free(img.pixels);
}

static void test_encode_decode_rgb(void) {
    printf("[encode_decode_rgb]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 2;
    img.height = 2;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_TRUECOLOR;
    img.channels = 3;
    img.stride = 2 * 3;
    img.pixels = (uint8_t *)calloc(1, img.height * img.stride);

    uint8_t red[] = {255, 0, 0};
    uint8_t blue[] = {0, 0, 255};
    neverc_png_pixel_set(&img, 0, 0, red);
    neverc_png_pixel_set(&img, 1, 1, blue);

    uint8_t *png_data = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&img, &png_data, &png_len), 0);

    neverc_png_image_t decoded;
    ASSERT_EQ(neverc_png_decode(png_data, png_len, &decoded), 0);
    ASSERT_EQ(decoded.width, 2);
    ASSERT_EQ(decoded.height, 2);
    ASSERT_EQ(decoded.channels, 3);

    const uint8_t *p = neverc_png_pixel_at(&decoded, 0, 0);
    ASSERT_EQ(p[0], 255); ASSERT_EQ(p[1], 0); ASSERT_EQ(p[2], 0);

    p = neverc_png_pixel_at(&decoded, 1, 1);
    ASSERT_EQ(p[0], 0); ASSERT_EQ(p[1], 0); ASSERT_EQ(p[2], 255);

    free(png_data);
    neverc_png_free(&decoded);
    free(img.pixels);
}

static void test_encode_decode_grayscale(void) {
    printf("[encode_decode_grayscale]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 3;
    img.height = 3;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_GRAYSCALE;
    img.channels = 1;
    img.stride = 3;
    img.pixels = (uint8_t *)calloc(1, img.height * img.stride);

    uint8_t v128 = 128, v255 = 255, v64 = 64;
    neverc_png_pixel_set(&img, 0, 0, &v128);
    neverc_png_pixel_set(&img, 1, 1, &v255);
    neverc_png_pixel_set(&img, 2, 2, &v64);

    uint8_t *png_data = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&img, &png_data, &png_len), 0);

    neverc_png_image_t decoded;
    ASSERT_EQ(neverc_png_decode(png_data, png_len, &decoded), 0);
    ASSERT_EQ(decoded.channels, 1);

    ASSERT_EQ(*neverc_png_pixel_at(&decoded, 0, 0), 128);
    ASSERT_EQ(*neverc_png_pixel_at(&decoded, 1, 1), 255);
    ASSERT_EQ(*neverc_png_pixel_at(&decoded, 2, 2), 64);

    free(png_data);
    neverc_png_free(&decoded);
    free(img.pixels);
}

static void test_pixel_at_bounds(void) {
    printf("[pixel_at_bounds]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 2;
    img.height = 2;
    img.channels = 4;
    img.stride = 8;
    img.pixels = (uint8_t *)calloc(1, 16);

    ASSERT_TRUE(neverc_png_pixel_at(&img, 0, 0) != NULL);
    ASSERT_TRUE(neverc_png_pixel_at(&img, 1, 1) != NULL);
    ASSERT_TRUE(neverc_png_pixel_at(&img, 2, 0) == NULL);
    ASSERT_TRUE(neverc_png_pixel_at(&img, 0, 2) == NULL);
    ASSERT_TRUE(neverc_png_pixel_at(NULL, 0, 0) == NULL);

    free(img.pixels);
}

static void test_invalid_data(void) {
    printf("[invalid_data]\n");
    neverc_png_image_t img;
    ASSERT_EQ(neverc_png_decode(NULL, 0, &img), -1);
    ASSERT_EQ(neverc_png_decode((const uint8_t *)"notpng", 6, &img), -1);

    uint8_t short_data[] = {137, 80, 78, 71, 13, 10, 26, 10};
    ASSERT_EQ(neverc_png_decode(short_data, 8, &img), -1);
}

static void test_large_image(void) {
    printf("[large_image]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 100;
    img.height = 100;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_TRUECOLOR_ALPHA;
    img.channels = 4;
    img.stride = 400;
    img.pixels = (uint8_t *)malloc(img.height * img.stride);

    /* Gradient fill */
    for (uint32_t y = 0; y < img.height; y++) {
        for (uint32_t x = 0; x < img.width; x++) {
            uint8_t *p = img.pixels + y * img.stride + x * 4;
            p[0] = (uint8_t)(x * 255 / 99);
            p[1] = (uint8_t)(y * 255 / 99);
            p[2] = 128;
            p[3] = 255;
        }
    }

    uint8_t *png_data = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&img, &png_data, &png_len), 0);
    ASSERT_TRUE(png_len > 0);

    neverc_png_image_t decoded;
    ASSERT_EQ(neverc_png_decode(png_data, png_len, &decoded), 0);
    ASSERT_EQ(decoded.width, 100);
    ASSERT_EQ(decoded.height, 100);

    /* Spot check some pixels */
    const uint8_t *p = neverc_png_pixel_at(&decoded, 0, 0);
    ASSERT_EQ(p[0], 0); ASSERT_EQ(p[1], 0); ASSERT_EQ(p[2], 128);

    p = neverc_png_pixel_at(&decoded, 99, 99);
    ASSERT_EQ(p[0], 255); ASSERT_EQ(p[1], 255); ASSERT_EQ(p[2], 128);

    free(png_data);
    neverc_png_free(&decoded);
    free(img.pixels);
}

int main(void) {
    printf("NeverC image/png tests\n");
    test_encode_decode_rgba();
    test_encode_decode_rgb();
    test_encode_decode_grayscale();
    test_pixel_at_bounds();
    test_invalid_data();
    test_large_image();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
