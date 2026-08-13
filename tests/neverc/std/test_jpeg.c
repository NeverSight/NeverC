#include "neverc/std/image/jpeg.h"
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
    uint8_t pixel[3] = {0};
    neverc_jpeg_image_t too_wide = {
        .width = 65536, .height = 1, .channels = 3,
        .pixels = pixel, .stride = 3
    };
    uint8_t *output = NULL;
    size_t output_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&too_wide, 90, &output, &output_length), -1);
    neverc_jpeg_image_t short_stride = {
        .width = 2, .height = 1, .channels = 3,
        .pixels = pixel, .stride = 3
    };
    ASSERT_EQ(neverc_jpeg_encode(&short_stride, 90, &output, &output_length), -1);
}

static size_t find_marker(const uint8_t *data, size_t length, uint8_t marker) {
    for (size_t i = 0; i + 1 < length; i++)
        if (data[i] == 0xFF && data[i + 1] == marker)
            return i;
    return SIZE_MAX;
}

static void test_rejects_malformed_streams(void) {
    printf("[rejects_malformed_streams]\n");
    uint8_t pixels[64];
    memset(pixels, 128, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 8
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    int encode_result =
        neverc_jpeg_encode(&source, 90, &encoded, &encoded_length);
    ASSERT_EQ(encode_result, 0);
    if (encode_result != 0) return;

    size_t sos = find_marker(encoded, encoded_length, 0xDA);
    ASSERT_TRUE(sos != SIZE_MAX && sos + 4 <= encoded_length);
    if (sos != SIZE_MAX && sos + 4 <= encoded_length) {
        size_t entropy_start =
            sos + 2 + ((size_t)encoded[sos + 2] << 8) + encoded[sos + 3];
        ASSERT_TRUE(entropy_start <= encoded_length);
        if (entropy_start <= encoded_length) {
            neverc_jpeg_image_t decoded;
            ASSERT_EQ(neverc_jpeg_decode(
                          encoded, entropy_start, &decoded), -1);
            ASSERT_TRUE(decoded.pixels == NULL);
        }
    }
    if (encoded_length >= 2) {
        neverc_jpeg_image_t decoded;
        ASSERT_EQ(neverc_jpeg_decode(
                      encoded, encoded_length - 2, &decoded), -1);
        ASSERT_TRUE(decoded.pixels == NULL);
    }

    uint8_t *malformed = (uint8_t *)malloc(encoded_length);
    ASSERT_TRUE(malformed != NULL);
    if (malformed) {
        memcpy(malformed, encoded, encoded_length);
        size_t dqt = find_marker(malformed, encoded_length, 0xDB);
        ASSERT_TRUE(dqt != SIZE_MAX && dqt + 4 <= encoded_length);
        if (dqt != SIZE_MAX && dqt + 4 <= encoded_length) {
            malformed[dqt + 2] = 0xFF;
            malformed[dqt + 3] = 0xFF;
            neverc_jpeg_image_t decoded;
            ASSERT_EQ(neverc_jpeg_decode(
                          malformed, encoded_length, &decoded), -1);
            ASSERT_TRUE(decoded.pixels == NULL);
        }

        memcpy(malformed, encoded, encoded_length);
        size_t dht = find_marker(malformed, encoded_length, 0xC4);
        ASSERT_TRUE(dht != SIZE_MAX && dht + 22 <= encoded_length);
        if (dht != SIZE_MAX && dht + 22 <= encoded_length) {
            malformed[dht + 21] = 12; /* invalid baseline DC category */
            neverc_jpeg_image_t decoded;
            ASSERT_EQ(neverc_jpeg_decode(
                          malformed, encoded_length, &decoded), -1);
            ASSERT_TRUE(decoded.pixels == NULL);
        }
        free(malformed);
    }
    free(encoded);
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

static void test_sof_chroma_420(void) {
    printf("[sof_chroma_420]\n");
    neverc_jpeg_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 32; img.height = 32; img.channels = 3;
    img.stride = 32 * 3;
    img.pixels = (uint8_t *)malloc(img.height * img.stride);
    memset(img.pixels, 80, img.height * img.stride);

    uint8_t *jpeg_data = NULL;
    size_t jpeg_len = 0;
    ASSERT_EQ(neverc_jpeg_encode(&img, 85, &jpeg_data, &jpeg_len), 0);

    /* Locate SOF0 (FF C0) and verify 4:2:0 sampling factors (Y=0x22, Cb/Cr=0x11). */
    int saw_sof = 0;
    for (size_t i = 0; i + 1 < jpeg_len; i++) {
        if (jpeg_data[i] == 0xFF && jpeg_data[i + 1] == 0xC0) {
            size_t p = i + 10; /* SOF0: marker(2)+len(2)+prec(1)+dims(4)+ncomp(1) */
            ASSERT_EQ(jpeg_data[p + 0], 1); ASSERT_EQ(jpeg_data[p + 1], 0x22);
            ASSERT_EQ(jpeg_data[p + 3], 2); ASSERT_EQ(jpeg_data[p + 4], 0x11);
            ASSERT_EQ(jpeg_data[p + 6], 3); ASSERT_EQ(jpeg_data[p + 7], 0x11);
            saw_sof = 1;
            break;
        }
    }
    ASSERT_TRUE(saw_sof);

    neverc_jpeg_image_t decoded;
    ASSERT_EQ(neverc_jpeg_decode(jpeg_data, jpeg_len, &decoded), 0);
    ASSERT_EQ(decoded.width, 32);
    ASSERT_EQ(decoded.height, 32);

    free(jpeg_data);
    neverc_jpeg_free(&decoded);
    free(img.pixels);
}

int main(void) {
    printf("NeverC image/jpeg tests\n");
    test_encode_decode_rgb();
    test_encode_decode_grayscale();
    test_gradient();
    test_invalid_data();
    test_rejects_malformed_streams();
    test_quality_levels();
    test_sof_chroma_420();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
