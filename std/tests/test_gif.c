#include "neverc/image/gif.h"
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

static void test_encode_decode(void) {
    printf("[encode_decode]\n");
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = 4;
    frame.height = 4;
    frame.palette_size = 4;
    frame.palette[0] = (neverc_gif_color_t){255, 0, 0};   /* red */
    frame.palette[1] = (neverc_gif_color_t){0, 255, 0};   /* green */
    frame.palette[2] = (neverc_gif_color_t){0, 0, 255};   /* blue */
    frame.palette[3] = (neverc_gif_color_t){255, 255, 255}; /* white */
    frame.indices = (uint8_t *)calloc(1, 16);
    frame.indices[0] = 0; /* red */
    frame.indices[1] = 1; /* green */
    frame.indices[2] = 2; /* blue */
    frame.indices[3] = 3; /* white */

    uint8_t *gif_data = NULL;
    size_t gif_len = 0;
    int rc = neverc_gif_encode(&frame, &gif_data, &gif_len);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(gif_data != NULL);
    ASSERT_TRUE(gif_len > 0);

    /* Verify GIF signature */
    ASSERT_TRUE(memcmp(gif_data, "GIF89a", 6) == 0);

    /* Decode */
    neverc_gif_image_t img;
    rc = neverc_gif_decode(gif_data, gif_len, &img);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(img.width, 4);
    ASSERT_EQ(img.height, 4);
    ASSERT_EQ(img.num_frames, 1);

    neverc_gif_frame_t *f = &img.frames[0];
    ASSERT_EQ(f->width, 4);
    ASSERT_EQ(f->height, 4);

    /* Verify first 4 pixels map to correct colors */
    ASSERT_EQ(f->indices[0], 0);
    ASSERT_EQ(f->palette[0].r, 255);
    ASSERT_EQ(f->palette[0].g, 0);

    ASSERT_EQ(f->indices[1], 1);
    ASSERT_EQ(f->palette[1].g, 255);

    ASSERT_EQ(f->indices[2], 2);
    ASSERT_EQ(f->palette[2].b, 255);

    free(gif_data);
    neverc_gif_free(&img);
    free(frame.indices);
}

static void test_from_rgba(void) {
    printf("[from_rgba]\n");
    uint32_t w = 4, h = 4;
    uint8_t *rgba = (uint8_t *)calloc(1, (size_t)w * h * 4);

    /* Red pixel */
    rgba[0] = 255; rgba[1] = 0; rgba[2] = 0; rgba[3] = 255;
    /* Green pixel */
    rgba[4] = 0; rgba[5] = 255; rgba[6] = 0; rgba[7] = 255;
    /* Blue pixel */
    rgba[8] = 0; rgba[9] = 0; rgba[10] = 255; rgba[11] = 255;

    neverc_gif_frame_t frame;
    ASSERT_EQ(neverc_gif_from_rgba(rgba, w, h, &frame), 0);
    ASSERT_EQ(frame.width, 4);
    ASSERT_EQ(frame.height, 4);
    ASSERT_TRUE(frame.palette_size > 0);
    ASSERT_TRUE(frame.indices != NULL);

    /* Encode and verify it produces valid GIF */
    uint8_t *gif_data = NULL;
    size_t gif_len = 0;
    ASSERT_EQ(neverc_gif_encode(&frame, &gif_data, &gif_len), 0);
    ASSERT_TRUE(memcmp(gif_data, "GIF89a", 6) == 0);

    /* Decode back */
    neverc_gif_image_t img;
    ASSERT_EQ(neverc_gif_decode(gif_data, gif_len, &img), 0);
    ASSERT_EQ(img.num_frames, 1);

    free(gif_data);
    neverc_gif_free(&img);
    free(frame.indices);
    free(rgba);
}

static void test_invalid_data(void) {
    printf("[invalid_data]\n");
    neverc_gif_image_t img;
    ASSERT_EQ(neverc_gif_decode(NULL, 0, &img), -1);
    ASSERT_EQ(neverc_gif_decode((const uint8_t *)"notgif", 6, &img), -1);
    ASSERT_EQ(neverc_gif_encode(NULL, NULL, NULL), -1);
}

static void test_single_color(void) {
    printf("[single_color]\n");
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = 8;
    frame.height = 8;
    frame.palette_size = 2;
    frame.palette[0] = (neverc_gif_color_t){0, 0, 0};
    frame.palette[1] = (neverc_gif_color_t){255, 255, 255};
    frame.indices = (uint8_t *)calloc(1, 64); /* all zeros = all black */

    uint8_t *gif_data = NULL;
    size_t gif_len = 0;
    ASSERT_EQ(neverc_gif_encode(&frame, &gif_data, &gif_len), 0);

    neverc_gif_image_t img;
    ASSERT_EQ(neverc_gif_decode(gif_data, gif_len, &img), 0);
    ASSERT_EQ(img.num_frames, 1);

    /* All pixels should be index 0 */
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ(img.frames[0].indices[i], 0);
    }

    free(gif_data);
    neverc_gif_free(&img);
    free(frame.indices);
}

int main(void) {
    printf("NeverC image/gif tests\n");
    test_encode_decode();
    test_from_rgba();
    test_invalid_data();
    test_single_color();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
