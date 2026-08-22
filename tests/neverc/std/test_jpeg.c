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

static void assert_jpeg_rejected(const uint8_t *data, size_t len) {
    neverc_jpeg_image_t img;
    memset(&img, 0xA5, sizeof(img));
    ASSERT_EQ(neverc_jpeg_decode(data, len, &img), -1);
    ASSERT_TRUE(img.pixels == NULL);
    ASSERT_EQ(img.width, 0);
    ASSERT_EQ(img.height, 0);
}

static void test_invalid_data(void) {
    printf("[invalid_data]\n");
    neverc_jpeg_image_t img;
    memset(&img, 0xA5, sizeof(img));
    ASSERT_EQ(neverc_jpeg_decode(NULL, 0, &img), -1);
    ASSERT_TRUE(img.pixels == NULL);
    ASSERT_EQ(img.width, 0);
    memset(&img, 0xA5, sizeof(img));
    ASSERT_EQ(neverc_jpeg_decode((const uint8_t *)"xx", 2, &img), -1);
    ASSERT_TRUE(img.pixels == NULL);
    ASSERT_EQ(img.width, 0);
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

static void test_rejects_trailing_bytes(void) {
    printf("[rejects_trailing_bytes]\n");
    neverc_jpeg_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 8;
    img.height = 8;
    img.channels = 3;
    img.stride = 24;
    img.pixels = (uint8_t *)calloc(1, img.height * img.stride);
    ASSERT_TRUE(img.pixels != NULL);

    uint8_t *jpeg_data = NULL;
    size_t jpeg_len = 0;
    ASSERT_EQ(neverc_jpeg_encode(&img, 90, &jpeg_data, &jpeg_len), 0);
    ASSERT_TRUE(jpeg_data != NULL);

    uint8_t *padded = (uint8_t *)malloc(jpeg_len + 1);
    ASSERT_TRUE(padded != NULL);
    memcpy(padded, jpeg_data, jpeg_len);
    padded[jpeg_len] = 0x00;
    neverc_jpeg_image_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_jpeg_decode(padded, jpeg_len + 1, &decoded), -1);

    free(padded);
    free(jpeg_data);
    free(img.pixels);
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

static int patch_sof_sampling(uint8_t *data, size_t length, uint8_t sampling) {
    size_t sof = find_marker(data, length, 0xC0);
    if (sof == SIZE_MAX || sof + 11 >= length) return -1;
    data[sof + 11] = sampling; /* SOF0: id at +10, H/V at +11 */
    return 0;
}

static void test_sof_grayscale_ignores_sampling(void) {
    printf("[sof_grayscale_ignores_sampling]\n");
    uint8_t pixels[64];
    memset(pixels, 128, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 8
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 95, &encoded, &encoded_length), 0);
    if (!encoded) return;

    /* 2x2: MCU geometry would become 16x16 / 4 blocks without the SOF rule. */
    ASSERT_EQ(patch_sof_sampling(encoded, encoded_length, 0x22), 0);
    neverc_jpeg_image_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length, &decoded), 0);
    ASSERT_EQ(decoded.width, 8);
    ASSERT_EQ(decoded.height, 8);
    ASSERT_EQ(decoded.channels, 1);
    ASSERT_NEAR(decoded.pixels[4 * 8 + 4], 128, 10);
    neverc_jpeg_free(&decoded);

    /* 4x4: interleaved 10-block cap would reject a legal 1-block grayscale MCU. */
    ASSERT_EQ(patch_sof_sampling(encoded, encoded_length, 0x44), 0);
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length, &decoded), 0);
    ASSERT_EQ(decoded.width, 8);
    ASSERT_EQ(decoded.height, 8);
    neverc_jpeg_free(&decoded);

    /* Factor 3 is rejected before the grayscale 1x1 override (Go processSOF). */
    ASSERT_EQ(patch_sof_sampling(encoded, encoded_length, 0x31), 0);
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length, &decoded), -1);
    ASSERT_TRUE(decoded.pixels == NULL);

    free(encoded);
}

static void test_quality100_checkerboard(void) {
    printf("[quality100_checkerboard]\n");
    uint8_t pixels[64];
    for (int i = 0; i < 64; i++)
        pixels[i] = (uint8_t)(((i & 1) ^ ((i / 8) & 1)) ? 255 : 0);
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 8
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 100, &encoded, &encoded_length), 0);
    neverc_jpeg_image_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length, &decoded), 0);
    ASSERT_EQ(decoded.width, 8);
    ASSERT_EQ(decoded.height, 8);
    neverc_jpeg_free(&decoded);
    free(encoded);
}

static int insert_dri(const uint8_t *src, size_t src_len, uint16_t interval,
                      uint8_t **out, size_t *out_len) {
    size_t sos = find_marker(src, src_len, 0xDA);
    if (sos == SIZE_MAX || sos > src_len) return -1;
    uint8_t *dst = (uint8_t *)malloc(src_len + 6);
    if (!dst) return -1;
    memcpy(dst, src, sos);
    dst[sos + 0] = 0xFF;
    dst[sos + 1] = 0xDD;
    dst[sos + 2] = 0x00;
    dst[sos + 3] = 0x04;
    dst[sos + 4] = (uint8_t)(interval >> 8);
    dst[sos + 5] = (uint8_t)interval;
    memcpy(dst + sos + 6, src + sos, src_len - sos);
    *out = dst;
    *out_len = src_len + 6;
    return 0;
}

static void test_restart_interval_required(void) {
    printf("[restart_interval_required]\n");
    uint8_t pixels[16 * 8];
    memset(pixels, 128, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 16, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 16
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 90, &encoded, &encoded_length), 0);
    if (!encoded) return;

    uint8_t *with_dri = NULL;
    size_t with_dri_len = 0;
    ASSERT_EQ(insert_dri(encoded, encoded_length, 1, &with_dri, &with_dri_len), 0);
    neverc_jpeg_image_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    /* DRI=1 with no RST markers must fail — proves restart is not ignored. */
    ASSERT_EQ(neverc_jpeg_decode(with_dri, with_dri_len, &decoded), -1);
    ASSERT_TRUE(decoded.pixels == NULL);
    ASSERT_EQ(decoded.width, 0);
    free(with_dri);
    free(encoded);
}

/* Two 8x8 grayscale tiles stitched into a 16x8 baseline JPEG with DRI=1
 * and a single RST0 between the tiles. *rst_off is the index of the 0xFF
 * of that RST0. */
static int make_dri1_two_mcu(uint8_t **out, size_t *out_len, size_t *rst_off) {
    uint8_t left_pix[64], right_pix[64];
    memset(left_pix, 40, sizeof(left_pix));
    memset(right_pix, 200, sizeof(right_pix));
    neverc_jpeg_image_t left = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = left_pix, .stride = 8
    };
    neverc_jpeg_image_t right = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = right_pix, .stride = 8
    };
    uint8_t *left_j = NULL, *right_j = NULL;
    size_t left_n = 0, right_n = 0;
    if (neverc_jpeg_encode(&left, 95, &left_j, &left_n) != 0 ||
        neverc_jpeg_encode(&right, 95, &right_j, &right_n) != 0) {
        free(left_j);
        free(right_j);
        return -1;
    }

    size_t sof = find_marker(left_j, left_n, 0xC0);
    size_t sos = find_marker(left_j, left_n, 0xDA);
    size_t eoi_l = find_marker(left_j, left_n, 0xD9);
    size_t sos_r = find_marker(right_j, right_n, 0xDA);
    size_t eoi_r = find_marker(right_j, right_n, 0xD9);
    if (sof == SIZE_MAX || sos == SIZE_MAX || eoi_l == SIZE_MAX ||
        sos_r == SIZE_MAX || eoi_r == SIZE_MAX) {
        free(left_j);
        free(right_j);
        return -1;
    }
    size_t sos_len = ((size_t)left_j[sos + 2] << 8) | left_j[sos + 3];
    size_t ent0 = sos + 2 + sos_len;
    size_t sos_len_r = ((size_t)right_j[sos_r + 2] << 8) | right_j[sos_r + 3];
    size_t ent1 = sos_r + 2 + sos_len_r;
    if (ent0 > eoi_l || ent1 > eoi_r) {
        free(left_j);
        free(right_j);
        return -1;
    }

    size_t n = sos + 6 + (2 + sos_len) + (eoi_l - ent0) + 2 +
               (eoi_r - ent1) + 2;
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) {
        free(left_j);
        free(right_j);
        return -1;
    }

    size_t p = 0;
    memcpy(buf + p, left_j, sos); p += sos;
    buf[sof + 7] = 0;
    buf[sof + 8] = 16; /* SOF width 16 */
    buf[p++] = 0xFF; buf[p++] = 0xDD; buf[p++] = 0x00; buf[p++] = 0x04;
    buf[p++] = 0x00; buf[p++] = 0x01; /* DRI interval 1 */
    memcpy(buf + p, left_j + sos, 2 + sos_len); p += 2 + sos_len;
    memcpy(buf + p, left_j + ent0, eoi_l - ent0); p += eoi_l - ent0;
    *rst_off = p;
    buf[p++] = 0xFF; buf[p++] = 0xD0; /* RST0 */
    memcpy(buf + p, right_j + ent1, eoi_r - ent1); p += eoi_r - ent1;
    buf[p++] = 0xFF; buf[p++] = 0xD9;

    free(left_j);
    free(right_j);
    *out = buf;
    *out_len = p;
    return 0;
}

static uint8_t *insert_bytes_at(const uint8_t *src, size_t src_len,
                                size_t at, const uint8_t *extra, size_t extra_len) {
    if (!src || at > src_len || (extra_len && !extra)) return NULL;
    uint8_t *dst = (uint8_t *)malloc(src_len + extra_len);
    if (!dst) return NULL;
    memcpy(dst, src, at);
    if (extra_len) memcpy(dst + at, extra, extra_len);
    memcpy(dst + at + extra_len, src + at, src_len - at);
    return dst;
}

static void test_restart_roundtrip(void) {
    printf("[restart_roundtrip]\n");
    uint8_t *out = NULL;
    size_t p = 0, rst_off = 0;
    ASSERT_EQ(make_dri1_two_mcu(&out, &p, &rst_off), 0);
    if (!out) return;

    neverc_jpeg_image_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_jpeg_decode(out, p, &decoded), 0);
    ASSERT_EQ(decoded.width, 16);
    ASSERT_EQ(decoded.height, 8);
    ASSERT_EQ(decoded.channels, 1);
    ASSERT_NEAR(decoded.pixels[4 * 16 + 4], 40, 20);
    ASSERT_NEAR(decoded.pixels[4 * 16 + 12], 200, 20);
    neverc_jpeg_free(&decoded);
    free(out);
}

/* golang.org/issue/28717: some encoders emit a stuffed 0xFF 0x00 immediately
 * before RST even when the previous MCU ended on a byte boundary. ITU T.81
 * F.1.2.3 also stuffs 0x00 when 1-bit padding produces 0xFF. golang.org/issue/40130:
 * garbage bytes before RST must be skipped the way Go's findRST does. A
 * different marker (not RST / fill / stuffed 00) is still fatal. */
static void test_restart_stuffed_and_garbage(void) {
    printf("[restart_stuffed_and_garbage]\n");
    uint8_t *base = NULL;
    size_t n = 0, rst_off = 0;
    ASSERT_EQ(make_dri1_two_mcu(&base, &n, &rst_off), 0);
    if (!base) return;
    ASSERT_TRUE(rst_off + 1 < n && base[rst_off] == 0xFF &&
                base[rst_off + 1] == 0xD0);

    static const uint8_t stuffed[] = {0xFF, 0x00};
    uint8_t *with_ff00 = insert_bytes_at(base, n, rst_off, stuffed, 2);
    ASSERT_TRUE(with_ff00 != NULL);
    if (with_ff00) {
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(with_ff00, n + 2, &decoded), 0);
        ASSERT_EQ(decoded.width, 16);
        ASSERT_NEAR(decoded.pixels[4 * 16 + 4], 40, 20);
        ASSERT_NEAR(decoded.pixels[4 * 16 + 12], 200, 20);
        neverc_jpeg_free(&decoded);
        free(with_ff00);
    }

    static const uint8_t fill[] = {0xFF, 0xFF};
    uint8_t *with_fill = insert_bytes_at(base, n, rst_off, fill, 2);
    ASSERT_TRUE(with_fill != NULL);
    if (with_fill) {
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(with_fill, n + 2, &decoded), 0);
        ASSERT_EQ(decoded.width, 16);
        neverc_jpeg_free(&decoded);
        free(with_fill);
    }

    static const uint8_t garbage[] = {0x7F};
    uint8_t *with_junk = insert_bytes_at(base, n, rst_off, garbage, 1);
    ASSERT_TRUE(with_junk != NULL);
    if (with_junk) {
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(with_junk, n + 1, &decoded), 0);
        ASSERT_EQ(decoded.width, 16);
        ASSERT_NEAR(decoded.pixels[4 * 16 + 12], 200, 20);
        neverc_jpeg_free(&decoded);
        free(with_junk);
    }

    /* RST1 where RST0 is required — a different marker must not be skipped. */
    uint8_t *wrong = (uint8_t *)malloc(n);
    ASSERT_TRUE(wrong != NULL);
    if (wrong) {
        memcpy(wrong, base, n);
        wrong[rst_off + 1] = 0xD1;
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(wrong, n, &decoded), -1);
        ASSERT_TRUE(decoded.pixels == NULL);
        ASSERT_EQ(decoded.width, 0);
        free(wrong);
    }

    /* EOI (FF D9) in place of RST is a marker, not garbage. */
    uint8_t *eoi = (uint8_t *)malloc(n);
    ASSERT_TRUE(eoi != NULL);
    if (eoi) {
        memcpy(eoi, base, n);
        eoi[rst_off + 1] = 0xD9;
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(eoi, n, &decoded), -1);
        ASSERT_TRUE(decoded.pixels == NULL);
        free(eoi);
    }

    free(base);
}

static void test_rejects_huge_sof(void) {
    printf("[rejects_huge_sof]\n");
    uint8_t pixels[64];
    memset(pixels, 128, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 8
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 90, &encoded, &encoded_length), 0);
    if (!encoded) return;
    size_t sof = find_marker(encoded, encoded_length, 0xC0);
    ASSERT_TRUE(sof != SIZE_MAX && sof + 9 < encoded_length);
    if (sof != SIZE_MAX && sof + 9 < encoded_length) {
        encoded[sof + 5] = 0xFF;
        encoded[sof + 6] = 0xFF;
        encoded[sof + 7] = 0xFF;
        encoded[sof + 8] = 0xFF;
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length, &decoded), -1);
        ASSERT_TRUE(decoded.pixels == NULL);
        ASSERT_EQ(decoded.width, 0);
        ASSERT_EQ(decoded.height, 0);
    }
    free(encoded);
}

static void test_rejects_truncated_eoi(void) {
    printf("[rejects_truncated_eoi]\n");
    uint8_t pixels[64];
    memset(pixels, 128, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 8
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 90, &encoded, &encoded_length), 0);
    if (!encoded || encoded_length < 2) { free(encoded); return; }

    neverc_jpeg_image_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length - 1, &decoded), -1);
    ASSERT_TRUE(decoded.pixels == NULL);

    /* A truncated EOI is FF D9 with the 0xFF dropped, leaving a bare 0xD9.
     * That used to be accepted as success. */
    size_t eoi = find_marker(encoded, encoded_length, 0xD9);
    ASSERT_TRUE(eoi != SIZE_MAX && eoi + 1 < encoded_length);
    if (eoi != SIZE_MAX && eoi + 1 < encoded_length && encoded[eoi] == 0xFF) {
        memmove(encoded + eoi, encoded + eoi + 1, encoded_length - eoi - 1);
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length - 1, &decoded), -1);
        ASSERT_TRUE(decoded.pixels == NULL);
        ASSERT_EQ(decoded.width, 0);
    }
    free(encoded);
}

static void test_eoi_fill_bytes_ok(void) {
    printf("[eoi_fill_bytes_ok]\n");
    uint8_t pixels[64];
    memset(pixels, 128, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 8
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 90, &encoded, &encoded_length), 0);
    if (!encoded) return;
    size_t eoi = find_marker(encoded, encoded_length, 0xD9);
    ASSERT_TRUE(eoi != SIZE_MAX);
    if (eoi == SIZE_MAX) { free(encoded); return; }
    uint8_t *filled = (uint8_t *)malloc(encoded_length + 1);
    ASSERT_TRUE(filled != NULL);
    if (filled) {
        memcpy(filled, encoded, eoi);
        filled[eoi] = 0xFF; /* extra fill byte before EOI */
        memcpy(filled + eoi + 1, encoded + eoi, encoded_length - eoi);
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(filled, encoded_length + 1, &decoded), 0);
        ASSERT_EQ(decoded.width, 8);
        ASSERT_EQ(decoded.height, 8);
        neverc_jpeg_free(&decoded);
        free(filled);
    }
    free(encoded);
}

static void test_rejects_complete_huffman_table(void) {
    printf("[rejects_complete_huffman_table]\n");
    uint8_t pixels[64];
    memset(pixels, 128, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 8
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 90, &encoded, &encoded_length), 0);
    if (!encoded) return;

    size_t dht = find_marker(encoded, encoded_length, 0xC4);
    ASSERT_TRUE(dht != SIZE_MAX && dht + 4 <= encoded_length);
    if (dht == SIZE_MAX || dht + 4 > encoded_length) {
        free(encoded);
        return;
    }
    size_t dht_len = ((size_t)encoded[dht + 2] << 8) | encoded[dht + 3];
    size_t dht_end = dht + 2 + dht_len;
    ASSERT_TRUE(dht_end <= encoded_length && dht_len >= 2);
    if (dht_end > encoded_length) {
        free(encoded);
        return;
    }

    /* Two length-1 codes occupy both 0 and 1, so the all-1s code is used.
     * Annex C forbids that; the previous `code > 2^L` check accepted it. */
    static const uint8_t complete_dht[] = {
        0xFF, 0xC4,
        0x00, 21,
        0x00,
        2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1
    };
    size_t new_len = encoded_length - (dht_end - dht) + sizeof(complete_dht);
    uint8_t *patched = (uint8_t *)malloc(new_len);
    ASSERT_TRUE(patched != NULL);
    if (patched) {
        memcpy(patched, encoded, dht);
        memcpy(patched + dht, complete_dht, sizeof(complete_dht));
        memcpy(patched + dht + sizeof(complete_dht), encoded + dht_end,
               encoded_length - dht_end);
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(patched, new_len, &decoded), -1);
        ASSERT_TRUE(decoded.pixels == NULL);
        ASSERT_EQ(decoded.width, 0);
        free(patched);
    }
    free(encoded);
}

static void test_tiny_malformed_jpeg(void) {
    printf("[tiny_malformed_jpeg]\n");

    /* Truncated SOI — used to leave a poisoned img untouched. */
    static const uint8_t trunc_soi[] = {0xFF, 0xD8};
    assert_jpeg_rejected(trunc_soi, sizeof(trunc_soi));
    assert_jpeg_rejected(NULL, 0);

    /* SOF0 65535x65535 grayscale. Must fail at the header, not after a
     * multi-GB allocation. */
    static const uint8_t huge_sof[] = {
        0xFF, 0xD8,
        0xFF, 0xC0, 0x00, 0x0B, 0x08, 0xFF, 0xFF, 0xFF, 0xFF,
        0x01, 0x01, 0x11, 0x00,
        0xFF, 0xD9
    };
    assert_jpeg_rejected(huge_sof, sizeof(huge_sof));

    /* Progressive SOF2. Skipping this as "unknown" would hide it. */
    static const uint8_t sof2[] = {
        0xFF, 0xD8,
        0xFF, 0xC2, 0x00, 0x0B, 0x08, 0x00, 0x08, 0x00, 0x08,
        0x01, 0x01, 0x11, 0x00,
        0xFF, 0xD9
    };
    assert_jpeg_rejected(sof2, sizeof(sof2));

    /* Extended sequential SOF1 and arithmetic DAC (0xCC). */
    static const uint8_t sof1[] = {
        0xFF, 0xD8,
        0xFF, 0xC1, 0x00, 0x0B, 0x08, 0x00, 0x08, 0x00, 0x08,
        0x01, 0x01, 0x11, 0x00,
        0xFF, 0xD9
    };
    assert_jpeg_rejected(sof1, sizeof(sof1));
    static const uint8_t dac[] = {
        0xFF, 0xD8, 0xFF, 0xCC, 0x00, 0x04, 0x00, 0x00, 0xFF, 0xD9
    };
    assert_jpeg_rejected(dac, sizeof(dac));

    /* Over-subscribed DC table: 3 codes of length 1 (only 2 slots).
     * The lookahead fill would write past look_nbits[256]. */
    static const uint8_t oversub_dht[] = {
        0xFF, 0xD8,
        0xFF, 0xC4, 0x00, 0x16, 0x00,
        3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 2,
        0xFF, 0xD9
    };
    assert_jpeg_rejected(oversub_dht, sizeof(oversub_dht));

    /* Complete length-1 table (codes 0 and 1). Annex C forbids this. */
    static const uint8_t complete_dht[] = {
        0xFF, 0xD8,
        0xFF, 0xC4, 0x00, 0x15, 0x00,
        2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1,
        0xFF, 0xD9
    };
    assert_jpeg_rejected(complete_dht, sizeof(complete_dht));

    /* DRI declared with no interval payload. */
    static const uint8_t short_dri[] = {
        0xFF, 0xD8, 0xFF, 0xDD, 0x00, 0x02, 0xFF, 0xD9
    };
    assert_jpeg_rejected(short_dri, sizeof(short_dri));

    /* SOS without SOF — truncated / incomplete baseline stream. */
    static const uint8_t sos_only[] = {
        0xFF, 0xD8,
        0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00,
        0xFF, 0xD9
    };
    assert_jpeg_rejected(sos_only, sizeof(sos_only));

    /* SOF0 then EOI: no scan. A truncated JPEG must not succeed. */
    static const uint8_t sof_no_scan[] = {
        0xFF, 0xD8,
        0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x08, 0x00, 0x08,
        0x01, 0x01, 0x11, 0x00,
        0xFF, 0xD9
    };
    assert_jpeg_rejected(sof_no_scan, sizeof(sof_no_scan));

    /* SOF2 in front of a valid baseline stream must not be skipped. */
    uint8_t pixels[64];
    memset(pixels, 128, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 8
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 90, &encoded, &encoded_length), 0);
    if (encoded && encoded_length >= 2) {
        static const uint8_t sof2_seg[] = {
            0xFF, 0xC2, 0x00, 0x0B, 0x08, 0x00, 0x08, 0x00, 0x08,
            0x01, 0x01, 0x11, 0x00
        };
        static const uint8_t reserved[] = { 0xFF, 0x02, 0x00, 0x02 };
        static const uint8_t dnl[] = { 0xFF, 0xDC, 0x00, 0x02 };
        static const uint8_t jpeg_ext[] = { 0xFF, 0xF0, 0x00, 0x02 };
        static const uint8_t com[] = { 0xFF, 0xFE, 0x00, 0x02 };
        const uint8_t *reject_segs[] = { sof2_seg, reserved, dnl, jpeg_ext };
        const size_t reject_lens[] = {
            sizeof(sof2_seg), sizeof(reserved), sizeof(dnl), sizeof(jpeg_ext)
        };
        size_t i;
        for (i = 0; i < sizeof(reject_lens) / sizeof(reject_lens[0]); i++) {
            size_t n = encoded_length + reject_lens[i];
            uint8_t *mixed = (uint8_t *)malloc(n);
            ASSERT_TRUE(mixed != NULL);
            if (!mixed)
                break;
            memcpy(mixed, encoded, 2); /* SOI */
            memcpy(mixed + 2, reject_segs[i], reject_lens[i]);
            memcpy(mixed + 2 + reject_lens[i], encoded + 2,
                   encoded_length - 2);
            assert_jpeg_rejected(mixed, n);
            free(mixed);
        }
        /* COM after SOI is ignored, same as Go image/jpeg. */
        {
            size_t n = encoded_length + sizeof(com);
            uint8_t *mixed = (uint8_t *)malloc(n);
            ASSERT_TRUE(mixed != NULL);
            if (mixed) {
                neverc_jpeg_image_t decoded;
                memcpy(mixed, encoded, 2);
                memcpy(mixed + 2, com, sizeof(com));
                memcpy(mixed + 2 + sizeof(com), encoded + 2,
                       encoded_length - 2);
                memset(&decoded, 0, sizeof(decoded));
                ASSERT_EQ(neverc_jpeg_decode(mixed, n, &decoded), 0);
                ASSERT_EQ(decoded.width, 8);
                ASSERT_EQ(decoded.height, 8);
                neverc_jpeg_free(&decoded);
                free(mixed);
            }
        }
        free(encoded);
    }
}

static void test_rejects_baseline_sos_table_and_factor3(void) {
    printf("[rejects_baseline_sos_table_and_factor3]\n");
    uint8_t pixels[8 * 8 * 3];
    memset(pixels, 80, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 3,
        .pixels = pixels, .stride = 24
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 90, &encoded, &encoded_length), 0);
    if (!encoded) return;

    size_t sos = find_marker(encoded, encoded_length, 0xDA);
    ASSERT_TRUE(sos != SIZE_MAX && sos + 7 < encoded_length);
    if (sos != SIZE_MAX && sos + 7 < encoded_length) {
        /* Y's Td/Ta at SOS+6 after FF DA len ns. Baseline forbids Td=2. */
        encoded[sos + 6] = 0x20;
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length, &decoded), -1);
        ASSERT_TRUE(decoded.pixels == NULL);
        encoded[sos + 6] = 0x00;
    }

    ASSERT_EQ(patch_sof_sampling(encoded, encoded_length, 0x31), 0);
    neverc_jpeg_image_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length, &decoded), -1);
    ASSERT_TRUE(decoded.pixels == NULL);
    free(encoded);
}

static void test_rejects_duplicate_sof(void) {
    printf("[rejects_duplicate_sof]\n");
    uint8_t pixels[64];
    memset(pixels, 128, sizeof(pixels));
    neverc_jpeg_image_t source = {
        .width = 8, .height = 8, .channels = 1,
        .pixels = pixels, .stride = 8
    };
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&source, 90, &encoded, &encoded_length), 0);
    if (!encoded) return;

    size_t sof = find_marker(encoded, encoded_length, 0xC0);
    size_t sos = find_marker(encoded, encoded_length, 0xDA);
    ASSERT_TRUE(sof != SIZE_MAX && sos != SIZE_MAX && sof + 4 <= encoded_length);
    if (sof == SIZE_MAX || sos == SIZE_MAX || sof + 4 > encoded_length) {
        free(encoded);
        return;
    }
    size_t sof_len = 2 + ((size_t)encoded[sof + 2] << 8) + encoded[sof + 3];
    ASSERT_TRUE(sof + sof_len <= encoded_length && sof_len >= 2);
    if (sof + sof_len > encoded_length) {
        free(encoded);
        return;
    }

    uint8_t *dup = (uint8_t *)malloc(encoded_length + sof_len);
    ASSERT_TRUE(dup != NULL);
    if (dup) {
        memcpy(dup, encoded, sos);
        memcpy(dup + sos, encoded + sof, sof_len);
        memcpy(dup + sos + sof_len, encoded + sos, encoded_length - sos);
        neverc_jpeg_image_t decoded;
        memset(&decoded, 0, sizeof(decoded));
        ASSERT_EQ(neverc_jpeg_decode(dup, encoded_length + sof_len, &decoded), -1);
        ASSERT_TRUE(decoded.pixels == NULL);
        free(dup);
    }
    free(encoded);
}

static void test_sos_scan_order_swaps_chroma(void) {
    printf("[sos_scan_order_swaps_chroma]\n");
    neverc_jpeg_image_t img;
    memset(&img, 0, sizeof(img));
    img.width = 16;
    img.height = 16;
    img.channels = 3;
    img.stride = 16 * 3;
    img.pixels = (uint8_t *)calloc(1, img.height * img.stride);
    ASSERT_TRUE(img.pixels != NULL);
    if (!img.pixels) return;
    for (uint32_t y = 0; y < img.height; y++) {
        for (uint32_t x = 0; x < img.width; x++) {
            uint8_t *p = img.pixels + y * img.stride + x * 3;
            p[0] = 255; p[1] = 0; p[2] = 0;
        }
    }
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_jpeg_encode(&img, 90, &encoded, &encoded_length), 0);
    ASSERT_TRUE(encoded != NULL);
    if (!encoded) {
        free(img.pixels);
        return;
    }
    neverc_jpeg_image_t original;
    memset(&original, 0, sizeof(original));
    ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length, &original), 0);

    size_t sos = find_marker(encoded, encoded_length, 0xDA);
    ASSERT_TRUE(sos != SIZE_MAX && sos + 10 < encoded_length);
    if (sos != SIZE_MAX && sos + 10 < encoded_length) {
        uint8_t tmp = encoded[sos + 7];
        encoded[sos + 7] = encoded[sos + 9];
        encoded[sos + 9] = tmp;
    }
    neverc_jpeg_image_t swapped;
    memset(&swapped, 0, sizeof(swapped));
    ASSERT_EQ(neverc_jpeg_decode(encoded, encoded_length, &swapped), 0);
    ASSERT_TRUE(original.pixels && swapped.pixels);
    if (original.pixels && swapped.pixels &&
        original.width == swapped.width &&
        original.height == swapped.height &&
        original.channels == swapped.channels) {
        int differ = 0;
        size_t nbytes = (size_t)original.height * original.stride;
        for (size_t i = 0; i < nbytes; i++) {
            if (original.pixels[i] != swapped.pixels[i]) {
                differ = 1;
                break;
            }
        }
        ASSERT_TRUE(differ);
    }
    neverc_jpeg_free(&original);
    neverc_jpeg_free(&swapped);
    free(encoded);
    free(img.pixels);
}

int main(void) {
    printf("NeverC image/jpeg tests\n");
    test_encode_decode_rgb();
    test_encode_decode_grayscale();
    test_gradient();
    test_invalid_data();
    test_rejects_trailing_bytes();
    test_rejects_malformed_streams();
    test_quality_levels();
    test_sof_chroma_420();
    test_sof_grayscale_ignores_sampling();
    test_quality100_checkerboard();
    test_restart_interval_required();
    test_restart_roundtrip();
    test_restart_stuffed_and_garbage();
    test_rejects_huge_sof();
    test_rejects_truncated_eoi();
    test_eoi_fill_bytes_ok();
    test_rejects_complete_huffman_table();
    test_tiny_malformed_jpeg();
    test_rejects_baseline_sos_table_and_factor3();
    test_rejects_duplicate_sof();
    test_sos_scan_order_swaps_chroma();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
