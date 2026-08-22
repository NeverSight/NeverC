#include "neverc/std/image/png.h"
#include "neverc/std/hash/crc32.h"
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

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void wr_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint8_t *insert_chunk(const uint8_t *png, size_t png_len, size_t offset,
                             const char type[4], const uint8_t *payload,
                             uint32_t payload_len, size_t *result_len) {
    if (!png || !result_len || offset > png_len ||
        (payload_len > 0 && !payload) ||
        png_len > SIZE_MAX - 12U - payload_len)
        return NULL;
    size_t extra = 12U + payload_len;
    uint8_t *result = (uint8_t *)malloc(png_len + extra);
    if (!result) return NULL;
    memcpy(result, png, offset);
    wr_be32(result + offset, payload_len);
    memcpy(result + offset + 4U, type, 4);
    if (payload_len)
        memcpy(result + offset + 8U, payload, payload_len);
    wr_be32(result + offset + 8U + payload_len,
            neverc_crc32_ieee(result + offset + 4U, 4U + payload_len));
    memcpy(result + offset + extra, png + offset, png_len - offset);
    *result_len = png_len + extra;
    return result;
}

static uint8_t *insert_empty_chunk(const uint8_t *png, size_t png_len,
                                   size_t offset, const char type[4],
                                   size_t *result_len) {
    return insert_chunk(png, png_len, offset, type, NULL, 0, result_len);
}

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

static void test_pixel_offset_overflow(void) {
    printf("[pixel_offset_overflow]\n");
    uint8_t pix[8];
    memset(pix, 0x5A, sizeof(pix));
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 2;
    img.height = 3;
    img.channels = 1;
    img.stride = (SIZE_MAX / 2) + 1;
    img.pixels = pix;

    ASSERT_TRUE(neverc_png_pixel_at(&img, 0, 0) == pix);
    /* y * stride wraps size_t; must not return a wild pointer. */
    ASSERT_TRUE(neverc_png_pixel_at(&img, 0, 2) == NULL);
    uint8_t src = 0x11;
    neverc_png_pixel_set(&img, 0, 2, &src);
    ASSERT_EQ(pix[0], 0x5A);

    img.channels = 0;
    ASSERT_TRUE(neverc_png_pixel_at(&img, 0, 0) == NULL);
}

static void test_invalid_data(void) {
    printf("[invalid_data]\n");
    neverc_png_image_t img;
    ASSERT_EQ(neverc_png_decode(NULL, 0, &img), -1);
    ASSERT_EQ(neverc_png_decode((const uint8_t *)"notpng", 6, &img), -1);

    uint8_t short_data[] = {137, 80, 78, 71, 13, 10, 26, 10};
    ASSERT_EQ(neverc_png_decode(short_data, 8, &img), -1);

    uint8_t huge_chunk[] = {
        137, 80, 78, 71, 13, 10, 26, 10,
        0xff, 0xff, 0xff, 0xff, 'I', 'D', 'A', 'T', 0, 0, 0, 0
    };
    ASSERT_EQ(neverc_png_decode(
                  huge_chunk, sizeof(huge_chunk), &img), -1);
}

static void test_rejects_trailing_bytes(void) {
    printf("[rejects_trailing_bytes]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 1;
    img.height = 1;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_TRUECOLOR_ALPHA;
    img.channels = 4;
    img.stride = 4;
    img.pixels = (uint8_t *)calloc(1, 4);
    ASSERT_TRUE(img.pixels != NULL);

    uint8_t *png_data = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&img, &png_data, &png_len), 0);
    ASSERT_TRUE(png_data != NULL);

    uint8_t *padded = (uint8_t *)malloc(png_len + 1);
    ASSERT_TRUE(padded != NULL);
    memcpy(padded, png_data, png_len);
    padded[png_len] = 0x00;
    neverc_png_image_t decoded;
    ASSERT_EQ(neverc_png_decode(padded, png_len + 1, &decoded), -1);

    free(padded);
    free(png_data);
    free(img.pixels);
}

static void test_encode_rejects_unsafe_geometry(void) {
    printf("[encode_rejects_unsafe_geometry]\n");
    uint8_t pixel = 0;
    uint8_t *out = (uint8_t *)(uintptr_t)1;
    size_t out_len = 123;
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = UINT32_MAX;
    img.height = UINT32_MAX;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_GRAYSCALE;
    img.channels = 1;
    img.stride = SIZE_MAX;
    img.pixels = &pixel;
    ASSERT_EQ(neverc_png_encode(&img, &out, &out_len), -1);
    ASSERT_TRUE(out == NULL);
    ASSERT_TRUE(out_len == 0);

    img.width = 2;
    img.height = 1;
    img.stride = 1;
    ASSERT_EQ(neverc_png_encode(&img, &out, &out_len), -1);

    img.stride = 2;
    img.channels = 4;
    ASSERT_EQ(neverc_png_encode(&img, &out, &out_len), -1);

    /* Valid 2x2 geometry but stride so large that y*stride wraps size_t. */
    uint8_t pix[8];
    memset(pix, 0, sizeof(pix));
    img.width = 2;
    img.height = 2;
    img.channels = 1;
    img.color_type = NEVERC_PNG_COLOR_GRAYSCALE;
    img.stride = (SIZE_MAX / 2) + 1;
    img.pixels = pix;
    out = (uint8_t *)(uintptr_t)1;
    out_len = 123;
    ASSERT_EQ(neverc_png_encode(&img, &out, &out_len), -1);
    ASSERT_TRUE(out == NULL);
    ASSERT_TRUE(out_len == 0);

    out = (uint8_t *)(uintptr_t)1;
    out_len = 123;
    ASSERT_EQ(neverc_png_encode(NULL, &out, &out_len), -1);
    ASSERT_TRUE(out == NULL);
    ASSERT_TRUE(out_len == 0);

    img.pixels = NULL;
    out = (uint8_t *)(uintptr_t)1;
    out_len = 123;
    ASSERT_EQ(neverc_png_encode(&img, &out, &out_len), -1);
    ASSERT_TRUE(out == NULL);
    ASSERT_TRUE(out_len == 0);
}

static void test_padded_stride_and_crc_rejection(void) {
    printf("[padded_stride_and_crc_rejection]\n");
    uint8_t pixels[16] = {
        255, 0, 0, 0, 255, 0, 0xaa, 0xbb,
        0, 0, 255, 255, 255, 255, 0xcc, 0xdd
    };
    static const uint8_t expected[12] = {
        255, 0, 0, 0, 255, 0,
        0, 0, 255, 255, 255, 255
    };
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 2;
    img.height = 2;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_TRUECOLOR;
    img.channels = 3;
    img.stride = 8;
    img.pixels = pixels;

    uint8_t *png = NULL;
    size_t png_len = 0;
    int rc = neverc_png_encode(&img, &png, &png_len);
    ASSERT_EQ(rc, 0);
    if (rc != 0 || !png) return;

    neverc_png_image_t decoded;
    rc = neverc_png_decode(png, png_len, &decoded);
    ASSERT_EQ(rc, 0);
    if (rc != 0) { free(png); return; }
    ASSERT_TRUE(decoded.stride == 6);
    ASSERT_TRUE(memcmp(decoded.pixels, expected, sizeof(expected)) == 0);
    neverc_png_free(&decoded);

    size_t mutated_len = 0;
    uint8_t *mutated = insert_empty_chunk(
        png, png_len, 8, "aBCD", &mutated_len);
    ASSERT_TRUE(mutated != NULL);
    if (mutated) {
        ASSERT_EQ(neverc_png_decode(mutated, mutated_len, &decoded), -1);
        free(mutated);
    }
    mutated = insert_empty_chunk(
        png, png_len, 33, "ABCD", &mutated_len);
    ASSERT_TRUE(mutated != NULL);
    if (mutated) {
        ASSERT_EQ(neverc_png_decode(mutated, mutated_len, &decoded), -1);
        free(mutated);
    }

    png[16] ^= 1U; /* corrupt IHDR width without updating its CRC */
    ASSERT_EQ(neverc_png_decode(png, png_len, &decoded), -1);

    png[16] ^= 1U;
    size_t pos = 8;
    int corrupted_adler = 0;
    while (pos + 12U <= png_len) {
        uint32_t chunk_len = rd_be32(png + pos);
        if ((size_t)chunk_len > png_len - pos - 12U) break;
        if (memcmp(png + pos + 4U, "IDAT", 4) == 0 && chunk_len >= 6U) {
            png[pos + 8U + chunk_len - 1U] ^= 1U;
            uint32_t crc = neverc_crc32_ieee(
                png + pos + 4U, (size_t)chunk_len + 4U);
            wr_be32(png + pos + 8U + chunk_len, crc);
            corrupted_adler = 1;
            break;
        }
        pos += 12U + chunk_len;
    }
    ASSERT_TRUE(corrupted_adler);
    if (!corrupted_adler) { free(png); return; }
    ASSERT_EQ(neverc_png_decode(png, png_len, &decoded), -1);
    free(png);
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

/* Every emitted chunk must carry a valid CRC-32/IEEE over its type+data, or a
 * conformant decoder (libpng, browsers) rejects the file. Walk the chunks and
 * recompute. The IEND CRC is the universal constant 0xAE426082, a strong canary
 * for the checksum being correct end to end. */
static void test_chunk_crc_valid(void) {
    printf("[chunk_crc_valid]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 23; img.height = 19; img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_TRUECOLOR_ALPHA;
    img.channels = 4; img.stride = (size_t)img.width * 4;
    img.pixels = (uint8_t *)malloc(img.height * img.stride);
    for (size_t i = 0; i < img.height * img.stride; i++)
        img.pixels[i] = (uint8_t)(i * 53 + 11);

    uint8_t *png = NULL; size_t len = 0;
    ASSERT_EQ(neverc_png_encode(&img, &png, &len), 0);

    size_t pos = 8; int saw_iend = 0;
    while (pos + 12 <= len) {
        uint32_t clen = rd_be32(png + pos);
        if (pos + 12 + clen > len) break;
        uint32_t stored = rd_be32(png + pos + 8 + clen);
        uint32_t calc = neverc_crc32_ieee(png + pos + 4, 4 + clen);
        ASSERT_EQ(stored, calc);
        if (memcmp(png + pos + 4, "IEND", 4) == 0) {
            ASSERT_EQ(stored, 0xAE426082u);   /* universal PNG IEND CRC */
            saw_iend = 1;
            break;
        }
        pos += 12 + clen;
    }
    ASSERT_TRUE(saw_iend);

    free(png);
    free(img.pixels);
}

/* IDAT carries a zlib wrapper (RFC 1950). FCHECK must make (CMF*256+FLG) a
 * multiple of 31 and FDICT must be clear — otherwise libpng/browsers reject the
 * file. The old encoder always added (31 - rem) even when rem==0, flipping FDICT. */
static void test_zlib_fcheck_valid(void) {
    printf("[zlib_fcheck_valid]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 3; img.height = 3; img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_TRUECOLOR;
    img.channels = 3; img.stride = (size_t)img.width * 3;
    img.pixels = (uint8_t *)calloc(1, img.height * img.stride);

    uint8_t *png = NULL; size_t len = 0;
    ASSERT_EQ(neverc_png_encode(&img, &png, &len), 0);

    size_t pos = 8; int saw_idat = 0;
    while (pos + 12 <= len) {
        uint32_t clen = rd_be32(png + pos);
        if (pos + 12 + clen > len) break;
        if (memcmp(png + pos + 4, "IDAT", 4) == 0 && clen >= 2) {
            uint8_t cmf = png[pos + 8];
            uint8_t flg = png[pos + 9];
            unsigned check = (unsigned)cmf * 256u + (unsigned)flg;
            ASSERT_EQ(check % 31, 0);
            ASSERT_EQ(flg & 0x20, 0);   /* no preset dictionary */
            saw_idat = 1;
            break;
        }
        pos += 12 + clen;
    }
    ASSERT_TRUE(saw_idat);

    free(png);
    free(img.pixels);
}

static void test_encode_decode_grayscale_alpha(void) {
    printf("[encode_decode_grayscale_alpha]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 2;
    img.height = 1;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_GRAYSCALE_ALPHA;
    img.channels = 2;
    img.stride = 4;
    img.pixels = (uint8_t *)calloc(1, 4);
    ASSERT_TRUE(img.pixels != NULL);
    img.pixels[0] = 64; img.pixels[1] = 255;
    img.pixels[2] = 192; img.pixels[3] = 128;

    uint8_t *png = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&img, &png, &png_len), 0);
    neverc_png_image_t decoded;
    ASSERT_EQ(neverc_png_decode(png, png_len, &decoded), 0);
    ASSERT_EQ(decoded.channels, 2);
    ASSERT_EQ(*neverc_png_pixel_at(&decoded, 0, 0), 64);
    ASSERT_EQ(neverc_png_pixel_at(&decoded, 0, 0)[1], 255);
    ASSERT_EQ(*neverc_png_pixel_at(&decoded, 1, 0), 192);
    ASSERT_EQ(neverc_png_pixel_at(&decoded, 1, 0)[1], 128);
    neverc_png_free(&decoded);
    free(png);
    free(img.pixels);
}

static void test_rejects_huge_ihdr(void) {
    printf("[rejects_huge_ihdr]\n");
    uint8_t png[45];
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    memcpy(png, sig, 8);
    wr_be32(png + 8, 13);
    memcpy(png + 12, "IHDR", 4);
    wr_be32(png + 16, 65535);
    wr_be32(png + 20, 65535);
    png[24] = 8;
    png[25] = NEVERC_PNG_COLOR_TRUECOLOR;
    png[26] = 0;
    png[27] = 0;
    png[28] = 0;
    wr_be32(png + 29, neverc_crc32_ieee(png + 12, 17));
    wr_be32(png + 33, 0);
    memcpy(png + 37, "IEND", 4);
    wr_be32(png + 41, 0xAE426082u);

    neverc_png_image_t img;
    memset(&img, 0xA5, sizeof(img));
    ASSERT_EQ(neverc_png_decode(png, sizeof(png), &img), -1);
    ASSERT_TRUE(img.pixels == NULL);
    ASSERT_EQ(img.width, 0);
    ASSERT_EQ(img.height, 0);
}

static void test_rejects_truncated_stream(void) {
    printf("[rejects_truncated_stream]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 2;
    img.height = 2;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_TRUECOLOR;
    img.channels = 3;
    img.stride = 6;
    img.pixels = (uint8_t *)calloc(1, 12);
    ASSERT_TRUE(img.pixels != NULL);

    uint8_t *png = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&img, &png, &png_len), 0);
    ASSERT_TRUE(png != NULL && png_len > 12);

    neverc_png_image_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_png_decode(png, png_len - 1, &decoded), -1);
    ASSERT_TRUE(decoded.pixels == NULL);
    ASSERT_EQ(decoded.width, 0);

    /* Drop the IEND CRC byte so the last chunk is truncated mid-header. */
    ASSERT_EQ(neverc_png_decode(png, png_len - 5, &decoded), -1);
    ASSERT_TRUE(decoded.pixels == NULL);

    free(png);
    free(img.pixels);
}

static void test_truncated_header_clears_geometry(void) {
    printf("[truncated_header_clears_geometry]\n");
    neverc_png_image_t img;
    memset(&img, 0xA5, sizeof(img));
    static const uint8_t trunc[] = {137, 80, 78, 71, 13, 10, 26};
    ASSERT_EQ(neverc_png_decode(trunc, sizeof(trunc), &img), -1);
    ASSERT_TRUE(img.pixels == NULL);
    ASSERT_EQ(img.width, 0);
    ASSERT_EQ(img.height, 0);

    memset(&img, 0xA5, sizeof(img));
    ASSERT_EQ(neverc_png_decode(NULL, 0, &img), -1);
    ASSERT_TRUE(img.pixels == NULL);
    ASSERT_EQ(img.width, 0);
    ASSERT_EQ(img.height, 0);
}

static void test_rejects_iend_crc_corruption(void) {
    printf("[rejects_iend_crc_corruption]\n");
    neverc_png_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 1;
    img.height = 1;
    img.bit_depth = 8;
    img.color_type = NEVERC_PNG_COLOR_GRAYSCALE;
    img.channels = 1;
    img.stride = 1;
    uint8_t pixel = 128;
    img.pixels = &pixel;

    uint8_t *png = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&img, &png, &png_len), 0);
    ASSERT_TRUE(png != NULL && png_len >= 12);

    png[png_len - 1U] ^= 1U; /* corrupt IEND CRC */
    neverc_png_image_t decoded;
    memset(&decoded, 0xA5, sizeof(decoded));
    ASSERT_EQ(neverc_png_decode(png, png_len, &decoded), -1);
    ASSERT_TRUE(decoded.pixels == NULL);
    ASSERT_EQ(decoded.width, 0);
    free(png);
}

static void test_rejects_illegal_and_duplicate_plte(void) {
    printf("[rejects_illegal_and_duplicate_plte]\n");
    uint8_t gray_px = 128;
    neverc_png_image_t gray;
    memset(&gray, 0, sizeof(gray));
    gray.width = 1;
    gray.height = 1;
    gray.bit_depth = 8;
    gray.color_type = NEVERC_PNG_COLOR_GRAYSCALE;
    gray.channels = 1;
    gray.stride = 1;
    gray.pixels = &gray_px;

    uint8_t *png = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&gray, &png, &png_len), 0);
    ASSERT_TRUE(png != NULL && png_len > 33);

    uint8_t pal[3] = {10, 20, 30};
    size_t with_plte_len = 0;
    uint8_t *with_plte =
        insert_chunk(png, png_len, 33, "PLTE", pal, 3, &with_plte_len);
    ASSERT_TRUE(with_plte != NULL);
    if (with_plte) {
        neverc_png_image_t decoded;
        ASSERT_EQ(neverc_png_decode(with_plte, with_plte_len, &decoded), -1);
        free(with_plte);
    }
    free(png);

    uint8_t rgb_px[3] = {255, 0, 0};
    neverc_png_image_t rgb;
    memset(&rgb, 0, sizeof(rgb));
    rgb.width = 1;
    rgb.height = 1;
    rgb.bit_depth = 8;
    rgb.color_type = NEVERC_PNG_COLOR_TRUECOLOR;
    rgb.channels = 3;
    rgb.stride = 3;
    rgb.pixels = rgb_px;

    png = NULL;
    png_len = 0;
    ASSERT_EQ(neverc_png_encode(&rgb, &png, &png_len), 0);
    ASSERT_TRUE(png != NULL);

    size_t once_len = 0;
    uint8_t *once = insert_chunk(png, png_len, 33, "PLTE", pal, 3, &once_len);
    ASSERT_TRUE(once != NULL);
    if (once) {
        neverc_png_image_t decoded;
        ASSERT_EQ(neverc_png_decode(once, once_len, &decoded), 0);
        neverc_png_free(&decoded);

        size_t twice_len = 0;
        uint8_t *twice =
            insert_chunk(once, once_len, 33, "PLTE", pal, 3, &twice_len);
        ASSERT_TRUE(twice != NULL);
        if (twice) {
            ASSERT_EQ(neverc_png_decode(twice, twice_len, &decoded), -1);
            free(twice);
        }
        free(once);
    }
    free(png);
}

static void test_trns_grayscale_and_truecolor(void) {
    printf("[trns_grayscale_and_truecolor]\n");
    uint8_t gray_px = 128;
    neverc_png_image_t gray;
    memset(&gray, 0, sizeof(gray));
    gray.width = 1;
    gray.height = 1;
    gray.bit_depth = 8;
    gray.color_type = NEVERC_PNG_COLOR_GRAYSCALE;
    gray.channels = 1;
    gray.stride = 1;
    gray.pixels = &gray_px;

    uint8_t *png = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&gray, &png, &png_len), 0);
    ASSERT_TRUE(png != NULL && png_len > 33);

    uint8_t trns_gray[2] = {0, 128};
    size_t with_len = 0;
    uint8_t *with = insert_chunk(
        png, png_len, 33, "tRNS", trns_gray, 2, &with_len);
    ASSERT_TRUE(with != NULL);
    if (with) {
        neverc_png_image_t decoded;
        ASSERT_EQ(neverc_png_decode(with, with_len, &decoded), 0);
        ASSERT_EQ(decoded.channels, 2);
        ASSERT_EQ(decoded.color_type, NEVERC_PNG_COLOR_GRAYSCALE_ALPHA);
        const uint8_t *p = neverc_png_pixel_at(&decoded, 0, 0);
        ASSERT_TRUE(p != NULL);
        if (p) {
            ASSERT_EQ(p[0], 128);
            ASSERT_EQ(p[1], 0);
        }
        neverc_png_free(&decoded);
        free(with);
    }

    uint8_t bad_len[1] = {128};
    with = insert_chunk(png, png_len, 33, "tRNS", bad_len, 1, &with_len);
    ASSERT_TRUE(with != NULL);
    if (with) {
        neverc_png_image_t decoded;
        ASSERT_EQ(neverc_png_decode(with, with_len, &decoded), -1);
        free(with);
    }
    free(png);

    uint8_t rgb_px[3] = {10, 20, 30};
    neverc_png_image_t rgb;
    memset(&rgb, 0, sizeof(rgb));
    rgb.width = 1;
    rgb.height = 1;
    rgb.bit_depth = 8;
    rgb.color_type = NEVERC_PNG_COLOR_TRUECOLOR;
    rgb.channels = 3;
    rgb.stride = 3;
    rgb.pixels = rgb_px;

    png = NULL;
    png_len = 0;
    ASSERT_EQ(neverc_png_encode(&rgb, &png, &png_len), 0);
    ASSERT_TRUE(png != NULL);

    uint8_t trns_rgb[6] = {0, 10, 0, 20, 0, 30};
    with = insert_chunk(png, png_len, 33, "tRNS", trns_rgb, 6, &with_len);
    ASSERT_TRUE(with != NULL);
    if (with) {
        neverc_png_image_t decoded;
        ASSERT_EQ(neverc_png_decode(with, with_len, &decoded), 0);
        ASSERT_EQ(decoded.channels, 4);
        ASSERT_EQ(decoded.color_type, NEVERC_PNG_COLOR_TRUECOLOR_ALPHA);
        const uint8_t *p = neverc_png_pixel_at(&decoded, 0, 0);
        ASSERT_TRUE(p != NULL);
        if (p) {
            ASSERT_EQ(p[0], 10);
            ASSERT_EQ(p[1], 20);
            ASSERT_EQ(p[2], 30);
            ASSERT_EQ(p[3], 0);
        }
        neverc_png_free(&decoded);
        free(with);
    }
    free(png);

    uint8_t rgba_px[4] = {1, 2, 3, 255};
    neverc_png_image_t rgba;
    memset(&rgba, 0, sizeof(rgba));
    rgba.width = 1;
    rgba.height = 1;
    rgba.bit_depth = 8;
    rgba.color_type = NEVERC_PNG_COLOR_TRUECOLOR_ALPHA;
    rgba.channels = 4;
    rgba.stride = 4;
    rgba.pixels = rgba_px;
    png = NULL;
    png_len = 0;
    ASSERT_EQ(neverc_png_encode(&rgba, &png, &png_len), 0);
    ASSERT_TRUE(png != NULL);
    with = insert_chunk(png, png_len, 33, "tRNS", trns_rgb, 6, &with_len);
    ASSERT_TRUE(with != NULL);
    if (with) {
        neverc_png_image_t decoded;
        ASSERT_EQ(neverc_png_decode(with, with_len, &decoded), -1);
        free(with);
    }
    free(png);
}

static void test_rejects_plte_after_trns(void) {
    printf("[rejects_plte_after_trns]\n");
    uint8_t rgb_px[3] = {255, 0, 0};
    neverc_png_image_t rgb;
    memset(&rgb, 0, sizeof(rgb));
    rgb.width = 1;
    rgb.height = 1;
    rgb.bit_depth = 8;
    rgb.color_type = NEVERC_PNG_COLOR_TRUECOLOR;
    rgb.channels = 3;
    rgb.stride = 3;
    rgb.pixels = rgb_px;

    uint8_t *png = NULL;
    size_t png_len = 0;
    ASSERT_EQ(neverc_png_encode(&rgb, &png, &png_len), 0);
    ASSERT_TRUE(png != NULL);

    uint8_t pal[3] = {10, 20, 30};
    size_t plte_len = 0;
    uint8_t *with_plte =
        insert_chunk(png, png_len, 33, "PLTE", pal, 3, &plte_len);
    ASSERT_TRUE(with_plte != NULL);
    uint8_t trns[6] = {0, 255, 0, 0, 0, 0};
    size_t both_len = 0;
    uint8_t *with_both = with_plte
        ? insert_chunk(with_plte, plte_len, 33, "tRNS", trns, 6, &both_len)
        : NULL;
    ASSERT_TRUE(with_both != NULL);
    if (with_both) {
        neverc_png_image_t decoded;
        /* IHDR, tRNS, PLTE, IDAT: Go chunkOrderError. */
        ASSERT_EQ(neverc_png_decode(with_both, both_len, &decoded), -1);
        free(with_both);
    }
    free(with_plte);
    free(png);
}

int main(void) {
    printf("NeverC image/png tests\n");
    test_encode_decode_rgba();
    test_encode_decode_rgb();
    test_encode_decode_grayscale();
    test_encode_decode_grayscale_alpha();
    test_pixel_at_bounds();
    test_pixel_offset_overflow();
    test_invalid_data();
    test_rejects_trailing_bytes();
    test_encode_rejects_unsafe_geometry();
    test_padded_stride_and_crc_rejection();
    test_large_image();
    test_chunk_crc_valid();
    test_zlib_fcheck_valid();
    test_rejects_huge_ihdr();
    test_rejects_truncated_stream();
    test_truncated_header_clears_geometry();
    test_rejects_iend_crc_corruption();
    test_rejects_illegal_and_duplicate_plte();
    test_trns_grayscale_and_truecolor();
    test_rejects_plte_after_trns();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
