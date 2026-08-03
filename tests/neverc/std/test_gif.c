#include "neverc/std/image/gif.h"
#include "neverc/std/compress/lzw.h"
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
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    uint8_t pixel = 0;
    frame.indices = &pixel;
    frame.width = 65536;
    frame.height = 1;
    frame.palette_size = 2;
    uint8_t *output = NULL;
    size_t output_length = 0;
    ASSERT_EQ(neverc_gif_encode(&frame, &output, &output_length), -1);
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

/* Large patterned images exercise LZW dictionary growth, code-size boundaries,
 * and the KwKwK case — none of which the small 4x4/8x8 cases above reach. This
 * is the regression guard for the encoder child/sibling list and the decoder
 * code-width timing (both previously corrupted any non-trivial GIF). */
static void rt_check(const char *name, int w, int h, int pal,
                     uint8_t (*pat)(int, int, int)) {
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = w;
    frame.height = h;
    frame.palette_size = pal;
    for (int i = 0; i < pal; i++)
        frame.palette[i] = (neverc_gif_color_t){(uint8_t)(i*5), (uint8_t)(i*3), (uint8_t)(i*7)};
    size_t n = (size_t)w * h;
    frame.indices = (uint8_t *)malloc(n);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            frame.indices[y*w+x] = pat(x, y, pal);

    uint8_t *gif = NULL; size_t glen = 0;
    ASSERT_EQ(neverc_gif_encode(&frame, &gif, &glen), 0);
    neverc_gif_image_t img;
    ASSERT_EQ(neverc_gif_decode(gif, glen, &img), 0);
    ASSERT_TRUE(img.num_frames >= 1);
    ASSERT_EQ(img.frames[0].width, w);
    ASSERT_EQ(img.frames[0].height, h);
    int ok = 1;
    for (size_t i = 0; i < n; i++)
        if (img.frames[0].indices[i] != frame.indices[i]) { ok = 0; break; }
    if (!ok) printf("  %s roundtrip mismatch\n", name);
    ASSERT_TRUE(ok);

    free(gif); neverc_gif_free(&img); free(frame.indices);
}

static uint8_t rt_gradient(int x, int y, int ps){ return (uint8_t)((x + y) % ps); }
static uint8_t rt_runs(int x, int y, int ps){ (void)y; return (uint8_t)((x / 17) % ps); }
static uint8_t rt_mix(int x, int y, int ps){ return (uint8_t)((x*7 + y*13 + (x/3)*(y/5)) % ps); }
static uint8_t rt_checker(int x, int y, int ps){ return (uint8_t)(((x ^ y) & 1) ? (1 % ps) : 0); }

static void test_large_roundtrip(void) {
    printf("[large_roundtrip]\n");
    rt_check("gradient", 200, 200, 64,  rt_gradient);
    rt_check("runs",     300, 120, 32,  rt_runs);
    rt_check("mix",      256, 256, 128, rt_mix);
    rt_check("mix256",   200, 200, 256, rt_mix);
    rt_check("checker",  150, 150, 4,   rt_checker);
    rt_check("grad2",    64,  64,  2,   rt_gradient);
}

/* Interlaced GIFs store rows in four passes (0/8, 4/8, 2/4, 1/2). Build a
 * minimal 4x4 fixture with pass-ordered LZW payload and verify de-interlace. */
static void gw_put(uint8_t **buf, size_t *cap, size_t *pos, uint8_t b) {
    if (*pos >= *cap) {
        *cap *= 2;
        *buf = (uint8_t *)realloc(*buf, *cap);
    }
    (*buf)[(*pos)++] = b;
}

static void test_interlaced_decode(void) {
    printf("[interlaced_decode]\n");
    const int fw = 4, fh = 4;
    uint8_t pass_rows[4][4] = {
        {0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {1,1,1,1}  /* raster rows 0..3 */
    };
    static const int pass_start[4] = {0, 4, 2, 1};
    static const int pass_step[4]  = {8, 8, 4, 2};
    uint8_t lzw_in[16];
    size_t li = 0;
    for (int p = 0; p < 4; p++)
        for (int row = pass_start[p]; row < fh; row += pass_step[p])
            memcpy(lzw_in + li, pass_rows[row], (size_t)fw), li += (size_t)fw;

    uint8_t lzw[256];
    size_t lzw_len = sizeof(lzw);
    ASSERT_EQ(neverc_lzw_compress(lzw_in, sizeof(lzw_in), lzw, &lzw_len,
                                  NEVERC_LZW_LSB, 2), 0);

    size_t cap = 512, pos = 0;
    uint8_t *gif = (uint8_t *)malloc(cap);
    const uint8_t hdr[] = "GIF89a";
    for (size_t i = 0; i < sizeof(hdr) - 1; i++) gw_put(&gif, &cap, &pos, hdr[i]);
    gw_put(&gif, &cap, &pos, (uint8_t)fw); gw_put(&gif, &cap, &pos, 0);
    gw_put(&gif, &cap, &pos, (uint8_t)fh); gw_put(&gif, &cap, &pos, 0);
    gw_put(&gif, &cap, &pos, 0x90); /* GCT, 2-color */
    gw_put(&gif, &cap, &pos, 0); gw_put(&gif, &cap, &pos, 0);
    for (int i = 0; i < 2; i++) { gw_put(&gif, &cap, &pos, 0); gw_put(&gif, &cap, &pos, 0); gw_put(&gif, &cap, &pos, 0); }
    gw_put(&gif, &cap, &pos, 0x2C);
    gw_put(&gif, &cap, &pos, 0); gw_put(&gif, &cap, &pos, 0);
    gw_put(&gif, &cap, &pos, 0); gw_put(&gif, &cap, &pos, 0);
    gw_put(&gif, &cap, &pos, (uint8_t)fw); gw_put(&gif, &cap, &pos, 0);
    gw_put(&gif, &cap, &pos, (uint8_t)fh); gw_put(&gif, &cap, &pos, 0);
    gw_put(&gif, &cap, &pos, 0x40); /* interlaced, no local palette */
    gw_put(&gif, &cap, &pos, 2);    /* LZW min code size */
    size_t off = 0;
    while (off < lzw_len) {
        uint8_t n = (uint8_t)((lzw_len - off > 255) ? 255 : (lzw_len - off));
        gw_put(&gif, &cap, &pos, n);
        for (uint8_t i = 0; i < n; i++) gw_put(&gif, &cap, &pos, lzw[off++]);
    }
    gw_put(&gif, &cap, &pos, 0);
    gw_put(&gif, &cap, &pos, 0x3B);

    neverc_gif_image_t img;
    ASSERT_EQ(neverc_gif_decode(gif, pos, &img), 0);
    ASSERT_EQ(img.num_frames, 1);
    ASSERT_EQ(img.frames[0].width, (uint32_t)fw);
    ASSERT_EQ(img.frames[0].height, (uint32_t)fh);
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++)
            ASSERT_EQ(img.frames[0].indices[y * fw + x], (uint8_t)(y & 1));

    neverc_gif_free(&img);
    free(gif);
}

int main(void) {
    printf("NeverC image/gif tests\n");
    test_encode_decode();
    test_from_rgba();
    test_invalid_data();
    test_single_color();
    test_large_roundtrip();
    test_interlaced_decode();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
