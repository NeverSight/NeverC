#include "neverc/image/jpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/* Allow +/- tolerance for lossy JPEG */
#define ASSERT_NEAR(a, b, tol) do { \
    int _a = (int)(a), _b = (int)(b), _t = (int)(tol); tests_run++; \
    if (abs(_a - _b) <= _t) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s = %d, expected %d +/-%d\n", __LINE__, #a, _a, _b, _t); } \
} while(0)

static void test_encode_decode_rgb(void) {
    printf("[encode_decode_rgb]\n");
    neverc_jpeg_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 16;
    img.height = 16;
    img.channels = 3;
    img.stride = 16 * 3;
    img.pixels = (uint8_t *)calloc(1, img.height * img.stride);

    /* Fill with solid red */
    for (uint32_t y = 0; y < img.height; y++) {
        for (uint32_t x = 0; x < img.width; x++) {
            uint8_t *p = img.pixels + y * img.stride + x * 3;
            p[0] = 255; p[1] = 0; p[2] = 0;
        }
    }

    uint8_t *jpeg_data = NULL;
    size_t jpeg_len = 0;
    int rc = neverc_jpeg_encode(&img, 90, &jpeg_data, &jpeg_len);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(jpeg_data != NULL);
    ASSERT_TRUE(jpeg_len > 0);

    /* Check JPEG signature */
    ASSERT_EQ(jpeg_data[0], 0xFF);
    ASSERT_EQ(jpeg_data[1], 0xD8);

    /* Decode */
    neverc_jpeg_image_t decoded;
    rc = neverc_jpeg_decode(jpeg_data, jpeg_len, &decoded);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(decoded.width, 16);
    ASSERT_EQ(decoded.height, 16);
    ASSERT_EQ(decoded.channels, 3);

    /* JPEG is lossy — check center pixel is approximately red */
    uint8_t *p = decoded.pixels + 8 * decoded.stride + 8 * 3;
    ASSERT_NEAR(p[0], 255, 20);
    ASSERT_NEAR(p[1], 0, 20);
    ASSERT_NEAR(p[2], 0, 20);

    free(jpeg_data);
    neverc_jpeg_free(&decoded);
    free(img.pixels);
}

static void test_encode_decode_grayscale(void) {
    printf("[encode_decode_grayscale]\n");
    neverc_jpeg_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 8;
    img.height = 8;
    img.channels = 1;
    img.stride = 8;
    img.pixels = (uint8_t *)calloc(1, 64);

    /* Fill with value 128 */
    memset(img.pixels, 128, 64);

    uint8_t *jpeg_data = NULL;
    size_t jpeg_len = 0;
    ASSERT_EQ(neverc_jpeg_encode(&img, 95, &jpeg_data, &jpeg_len), 0);

    neverc_jpeg_image_t decoded;
    ASSERT_EQ(neverc_jpeg_decode(jpeg_data, jpeg_len, &decoded), 0);
    ASSERT_EQ(decoded.width, 8);
    ASSERT_EQ(decoded.height, 8);
    ASSERT_EQ(decoded.channels, 1);

    /* Check center pixel */
    ASSERT_NEAR(decoded.pixels[4 * 8 + 4], 128, 10);

    free(jpeg_data);
    neverc_jpeg_free(&decoded);
    free(img.pixels);
}

static void test_gradient(void) {
    printf("[gradient]\n");
    neverc_jpeg_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 32;
    img.height = 32;
    img.channels = 3;
    img.stride = 32 * 3;
    img.pixels = (uint8_t *)malloc(img.height * img.stride);

    for (uint32_t y = 0; y < 32; y++) {
        for (uint32_t x = 0; x < 32; x++) {
            uint8_t *p = img.pixels + y * img.stride + x * 3;
            p[0] = (uint8_t)(x * 255 / 31);
            p[1] = (uint8_t)(y * 255 / 31);
            p[2] = 128;
        }
    }

    uint8_t *jpeg_data = NULL;
    size_t jpeg_len = 0;
    ASSERT_EQ(neverc_jpeg_encode(&img, 85, &jpeg_data, &jpeg_len), 0);

    neverc_jpeg_image_t decoded;
    ASSERT_EQ(neverc_jpeg_decode(jpeg_data, jpeg_len, &decoded), 0);

    /* Corners should be approximately correct */
    uint8_t *tl = decoded.pixels;
    ASSERT_NEAR(tl[0], 0, 30);
    ASSERT_NEAR(tl[1], 0, 30);

    uint8_t *br_pix = decoded.pixels + 31 * decoded.stride + 31 * 3;
    ASSERT_NEAR(br_pix[0], 255, 30);
    ASSERT_NEAR(br_pix[1], 255, 30);

    free(jpeg_data);
    neverc_jpeg_free(&decoded);
    free(img.pixels);
}

static void test_invalid_data(void) {
    printf("[invalid_data]\n");
    neverc_jpeg_image_t img;
    ASSERT_EQ(neverc_jpeg_decode(NULL, 0, &img), -1);
    ASSERT_EQ(neverc_jpeg_decode((const uint8_t *)"xx", 2, &img), -1);
    ASSERT_EQ(neverc_jpeg_encode(NULL, 90, NULL, NULL), -1);
}

static void test_quality_levels(void) {
    printf("[quality_levels]\n");
    neverc_jpeg_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 16;
    img.height = 16;
    img.channels = 3;
    img.stride = 48;
    img.pixels = (uint8_t *)calloc(1, img.height * img.stride);
    memset(img.pixels, 100, img.height * img.stride);

    /* Low quality should produce smaller file than high quality */
    uint8_t *low_data = NULL, *high_data = NULL;
    size_t low_len = 0, high_len = 0;

    ASSERT_EQ(neverc_jpeg_encode(&img, 10, &low_data, &low_len), 0);
    ASSERT_EQ(neverc_jpeg_encode(&img, 95, &high_data, &high_len), 0);

    ASSERT_TRUE(low_len > 0);
    ASSERT_TRUE(high_len > 0);
    ASSERT_TRUE(low_len <= high_len);

    free(low_data);
    free(high_data);
    free(img.pixels);
}

int main(void) {
    printf("NeverC image/jpeg tests\n");
    test_encode_decode_rgb();
    test_encode_decode_grayscale();
    test_gradient();
    test_invalid_data();
    test_quality_levels();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
