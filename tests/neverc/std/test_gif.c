#include "neverc/std/image/gif.h"
#include "neverc/std/compress/lzw.h"
#include <stdint.h>
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
    neverc_gif_frame_info_t frame_info = {2, 3, 2};
    memset(&frame, 0, sizeof(frame));
    frame.width = 4;
    frame.height = 4;
    frame.delay_centiseconds = 5;
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
    int rc = neverc_gif_encode_ex(
        &frame, &frame_info, &gif_data, &gif_len);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(gif_data != NULL);
    ASSERT_TRUE(gif_len > 0);

    /* Verify GIF signature */
    ASSERT_TRUE(memcmp(gif_data, "GIF89a", 6) == 0);

    /* Decode */
    neverc_gif_image_t img;
    rc = neverc_gif_decode(gif_data, gif_len, &img);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(img.width, 6);
    ASSERT_EQ(img.height, 7);
    ASSERT_EQ(img.num_frames, 1);

    neverc_gif_frame_t *f = &img.frames[0];
    neverc_gif_frame_info_t decoded_info = {0, 0, 0};
    ASSERT_EQ(neverc_gif_frame_info(&img, 0, &decoded_info), 0);
    ASSERT_EQ(decoded_info.left, 2);
    ASSERT_EQ(decoded_info.top, 3);
    ASSERT_EQ(f->width, 4);
    ASSERT_EQ(f->height, 4);
    ASSERT_EQ(f->delay_centiseconds, 5);
    ASSERT_EQ(decoded_info.disposal_method, 2);

    /* Verify first 4 pixels map to correct colors */
    ASSERT_EQ(f->indices[0], 0);
    ASSERT_EQ(f->palette[0].r, 255);
    ASSERT_EQ(f->palette[0].g, 0);

    ASSERT_EQ(f->indices[1], 1);
    ASSERT_EQ(f->palette[1].g, 255);

    ASSERT_EQ(f->indices[2], 2);
    ASSERT_EQ(f->palette[2].b, 255);

    /* A later GCE replaces the complete pending control state. In particular,
     * a transparency flag of zero must clear an earlier transparent index. */
    size_t gce_pos = 0;
    while (gce_pos + 2 < gif_len &&
           !(gif_data[gce_pos] == 0x21 &&
             gif_data[gce_pos + 1] == 0xf9 &&
             gif_data[gce_pos + 2] == 0x04))
        gce_pos++;
    ASSERT_TRUE(gce_pos + 2 < gif_len);
    if (gce_pos + 2 < gif_len) {
        static const uint8_t transparent_gce[] = {
            0x21, 0xf9, 0x04, 0x01, 0x00, 0x00, 0x01, 0x00
        };
        uint8_t *double_gce =
            (uint8_t *)malloc(gif_len + sizeof(transparent_gce));
        ASSERT_TRUE(double_gce != NULL);
        if (double_gce) {
            memcpy(double_gce, gif_data, gce_pos);
            memcpy(double_gce + gce_pos, transparent_gce,
                   sizeof(transparent_gce));
            memcpy(double_gce + gce_pos + sizeof(transparent_gce),
                   gif_data + gce_pos, gif_len - gce_pos);
            neverc_gif_image_t replaced;
            ASSERT_EQ(neverc_gif_decode(
                          double_gce,
                          gif_len + sizeof(transparent_gce),
                          &replaced), 0);
            if (replaced.num_frames == 1) {
                neverc_gif_frame_info_t replaced_info = {0, 0, 0};
                ASSERT_EQ(replaced.frames[0].has_transparency, 0);
                ASSERT_EQ(neverc_gif_frame_info(
                              &replaced, 0, &replaced_info), 0);
                ASSERT_EQ(replaced_info.disposal_method, 2);
            }
            neverc_gif_free(&replaced);
            free(double_gce);
        }
    }

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

    frame.width = 1;
    neverc_gif_frame_info_t info = {UINT16_MAX, 0, 0};
    ASSERT_EQ(neverc_gif_encode_ex(
                  &frame, &info, &output, &output_length), -1);
    info.left = 0;
    info.disposal_method = 4;
    ASSERT_EQ(neverc_gif_encode_ex(
                  &frame, &info, &output, &output_length), -1);
    frame.width = 1;
    frame.height = 1;
    frame.palette_size = 2;
    pixel = 2; /* index outside the 2-color palette */
    ASSERT_EQ(neverc_gif_encode(&frame, &output, &output_length), -1);

    uint8_t rgba_px[4] = {0, 0, 0, 255};
    neverc_gif_frame_t from;
    memset(&from, 0xA5, sizeof(from));
    ASSERT_EQ(neverc_gif_from_rgba(rgba_px, 65536, 1, &from), -1);
    ASSERT_EQ(neverc_gif_from_rgba(rgba_px, UINT16_MAX, UINT16_MAX, &from), -1);
}

static void test_rejects_trailing_bytes(void) {
    printf("[rejects_trailing_bytes]\n");
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = 2;
    frame.height = 2;
    frame.palette_size = 2;
    frame.palette[0] = (neverc_gif_color_t){0, 0, 0};
    frame.palette[1] = (neverc_gif_color_t){255, 255, 255};
    frame.indices = (uint8_t *)calloc(1, 4);
    ASSERT_TRUE(frame.indices != NULL);

    uint8_t *gif_data = NULL;
    size_t gif_len = 0;
    ASSERT_EQ(neverc_gif_encode(&frame, &gif_data, &gif_len), 0);
    ASSERT_TRUE(gif_data != NULL);

    uint8_t *padded = (uint8_t *)malloc(gif_len + 1);
    ASSERT_TRUE(padded != NULL);
    memcpy(padded, gif_data, gif_len);
    padded[gif_len] = 0x00;
    neverc_gif_image_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(neverc_gif_decode(padded, gif_len + 1, &decoded), -1);

    free(padded);
    free(gif_data);
    free(frame.indices);
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

static void test_netscape_loop_count(void) {
    printf("[netscape_loop_count]\n");
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = 2;
    frame.height = 2;
    frame.palette_size = 2;
    frame.palette[0] = (neverc_gif_color_t){0, 0, 0};
    frame.palette[1] = (neverc_gif_color_t){255, 255, 255};
    frame.indices = (uint8_t *)calloc(1, 4);
    ASSERT_TRUE(frame.indices != NULL);
    if (!frame.indices) return;

    uint8_t *gif = NULL;
    size_t glen = 0;
    ASSERT_EQ(neverc_gif_encode(&frame, &gif, &glen), 0);
    ASSERT_TRUE(gif != NULL && glen > 13);
    if (!gif) { free(frame.indices); return; }

    neverc_gif_image_t plain;
    ASSERT_EQ(neverc_gif_decode(gif, glen, &plain), 0);
    ASSERT_EQ(plain.loop_count, -1);
    neverc_gif_free(&plain);

    uint8_t packed = gif[10];
    int gct = (packed & 0x80) ? (1 << ((packed & 7) + 1)) : 0;
    size_t insert = 13 + (size_t)gct * 3;
    ASSERT_TRUE(insert <= glen);
    static const uint8_t netscape[] = {
        0x21, 0xFF, 0x0B,
        'N','E','T','S','C','A','P','E','2','.','0',
        0x03, 0x01, 0x04, 0x00, 0x00
    };
    uint8_t *looped = (uint8_t *)malloc(glen + sizeof(netscape));
    ASSERT_TRUE(looped != NULL);
    if (looped) {
        memcpy(looped, gif, insert);
        memcpy(looped + insert, netscape, sizeof(netscape));
        memcpy(looped + insert + sizeof(netscape), gif + insert, glen - insert);
        neverc_gif_image_t img;
        ASSERT_EQ(neverc_gif_decode(looped, glen + sizeof(netscape), &img), 0);
        ASSERT_EQ(img.loop_count, 4);
        neverc_gif_free(&img);
        free(looped);
    }

    static const uint8_t netscape_inf[] = {
        0x21, 0xFF, 0x0B,
        'N','E','T','S','C','A','P','E','2','.','0',
        0x03, 0x01, 0x00, 0x00, 0x00
    };
    uint8_t *infinite = (uint8_t *)malloc(glen + sizeof(netscape_inf));
    ASSERT_TRUE(infinite != NULL);
    if (infinite) {
        memcpy(infinite, gif, insert);
        memcpy(infinite + insert, netscape_inf, sizeof(netscape_inf));
        memcpy(infinite + insert + sizeof(netscape_inf), gif + insert,
               glen - insert);
        neverc_gif_image_t inf;
        ASSERT_EQ(neverc_gif_decode(
                      infinite, glen + sizeof(netscape_inf), &inf), 0);
        ASSERT_EQ(inf.loop_count, 0);
        neverc_gif_free(&inf);
        free(infinite);
    }

    static const uint8_t animexts[] = {
        0x21, 0xFF, 0x0B,
        'A','N','I','M','E','X','T','S','1','.','0',
        0x03, 0x01, 0x02, 0x00, 0x00
    };
    uint8_t *ext = (uint8_t *)malloc(glen + sizeof(animexts));
    ASSERT_TRUE(ext != NULL);
    if (ext) {
        memcpy(ext, gif, insert);
        memcpy(ext + insert, animexts, sizeof(animexts));
        memcpy(ext + insert + sizeof(animexts), gif + insert, glen - insert);
        neverc_gif_image_t img;
        ASSERT_EQ(neverc_gif_decode(ext, glen + sizeof(animexts), &img), 0);
        ASSERT_EQ(img.loop_count, 2);
        neverc_gif_free(&img);
        free(ext);
    }
    free(gif);
    free(frame.indices);
}

static void test_plain_text_consumes_gce(void) {
    printf("[plain_text_consumes_gce]\n");
    neverc_gif_frame_t frame;
    neverc_gif_frame_info_t frame_info = {0, 0, 2};
    memset(&frame, 0, sizeof(frame));
    frame.width = 1;
    frame.height = 1;
    frame.delay_centiseconds = 7;
    frame.has_transparency = 1;
    frame.transparent_index = 1;
    frame.palette_size = 2;
    frame.palette[0] = (neverc_gif_color_t){0, 0, 0};
    frame.palette[1] = (neverc_gif_color_t){255, 255, 255};
    frame.indices = (uint8_t *)calloc(1, 1);
    ASSERT_TRUE(frame.indices != NULL);
    if (!frame.indices) return;

    uint8_t *gif = NULL;
    size_t gif_len = 0;
    ASSERT_EQ(neverc_gif_encode_ex(
                  &frame, &frame_info, &gif, &gif_len), 0);
    ASSERT_TRUE(gif != NULL);
    if (!gif) { free(frame.indices); return; }

    size_t descriptor = 13U + 2U * 3U;
    while (descriptor < gif_len && gif[descriptor] != 0x2C)
        descriptor++;
    ASSERT_TRUE(descriptor < gif_len);

    /* GIF89a Plain Text Extension: fixed 12-byte grid header, one byte of
     * text data, then the sub-block terminator. It is a graphic-rendering
     * block, so the preceding GCE belongs to it rather than the image. */
    static const uint8_t plain_text[] = {
        0x21, 0x01, 0x0c,
        0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 0, 1,
        0x01, 'A', 0x00
    };
    if (descriptor < gif_len) {
        uint8_t *with_text =
            (uint8_t *)malloc(gif_len + sizeof(plain_text));
        ASSERT_TRUE(with_text != NULL);
        if (with_text) {
            memcpy(with_text, gif, descriptor);
            memcpy(with_text + descriptor, plain_text, sizeof(plain_text));
            memcpy(with_text + descriptor + sizeof(plain_text),
                   gif + descriptor, gif_len - descriptor);

            neverc_gif_image_t decoded;
            ASSERT_EQ(neverc_gif_decode(
                          with_text, gif_len + sizeof(plain_text), &decoded), 0);
            ASSERT_EQ(decoded.num_frames, 1);
            if (decoded.num_frames == 1) {
                neverc_gif_frame_info_t decoded_info = {0, 0, 0};
                ASSERT_EQ(decoded.frames[0].delay_centiseconds, 0);
                ASSERT_EQ(neverc_gif_frame_info(
                              &decoded, 0, &decoded_info), 0);
                ASSERT_EQ(decoded_info.disposal_method, 0);
                ASSERT_EQ(decoded.frames[0].has_transparency, 0);
            }
            neverc_gif_free(&decoded);
            free(with_text);
        }
    }

    free(gif);
    free(frame.indices);
}

static void test_rejects_truncated_and_oversize_lzw(void) {
    printf("[rejects_truncated_and_oversize_lzw]\n");
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = 1;
    frame.height = 2;
    frame.palette_size = 2;
    frame.palette[0] = (neverc_gif_color_t){0, 0, 0};
    frame.palette[1] = (neverc_gif_color_t){255, 255, 255};
    frame.indices = (uint8_t *)calloc(1, 2);
    ASSERT_TRUE(frame.indices != NULL);
    if (!frame.indices) return;
    frame.indices[0] = 0;
    frame.indices[1] = 1;

    uint8_t *gif = NULL;
    size_t glen = 0;
    ASSERT_EQ(neverc_gif_encode(&frame, &gif, &glen), 0);
    ASSERT_TRUE(gif != NULL && glen > 1);

    neverc_gif_image_t truncated;
    memset(&truncated, 0, sizeof(truncated));
    ASSERT_EQ(neverc_gif_decode(gif, glen - 1, &truncated), -1);
    ASSERT_TRUE(truncated.frames == NULL);

    /* Image descriptor height 1 with a 2-pixel LZW payload used to clamp and
     * succeed. The logical screen stays 1x2 so the 1-row frame still fits. */
    size_t desc = 0;
    while (desc + 10 < glen && gif[desc] != 0x2C) desc++;
    ASSERT_TRUE(desc + 10 < glen && gif[desc] == 0x2C);
    if (desc + 10 < glen) {
        gif[desc + 7] = 1;
        gif[desc + 8] = 0; /* frame height = 1 */
        neverc_gif_image_t oversize;
        memset(&oversize, 0, sizeof(oversize));
        ASSERT_EQ(neverc_gif_decode(gif, glen, &oversize), -1);
        ASSERT_TRUE(oversize.frames == NULL);
    }

    free(gif);
    free(frame.indices);
}

static void test_failed_decode_clears_geometry(void) {
    printf("[failed_decode_clears_geometry]\n");
    /* GCT flag set (2 entries) but only 3 of 6 color bytes are present. */
    static const uint8_t truncated_gct[] = {
        'G', 'I', 'F', '8', '9', 'a',
        2, 0, 2, 0,
        0x80, 0, 0,
        0, 0, 0
    };
    neverc_gif_image_t img;
    memset(&img, 0xA5, sizeof(img));
    ASSERT_EQ(neverc_gif_decode(
                  truncated_gct, sizeof(truncated_gct), &img), -1);
    ASSERT_EQ(img.width, 0);
    ASSERT_EQ(img.height, 0);
    ASSERT_TRUE(img.frames == NULL);
}

static void test_frame_to_rgba_and_transparency(void) {
    printf("[frame_to_rgba_and_transparency]\n");
    uint8_t rgba[4 * 4 * 4];
    memset(rgba, 0, sizeof(rgba));
    /* Opaque red, opaque green, fully transparent, opaque blue */
    rgba[0] = 255; rgba[1] = 0; rgba[2] = 0; rgba[3] = 255;
    rgba[4] = 0; rgba[5] = 255; rgba[6] = 0; rgba[7] = 255;
    rgba[8] = 10; rgba[9] = 20; rgba[10] = 30; rgba[11] = 0;
    rgba[12] = 0; rgba[13] = 0; rgba[14] = 255; rgba[15] = 255;

    neverc_gif_frame_t frame;
    ASSERT_EQ(neverc_gif_from_rgba(rgba, 4, 1, &frame), 0);
    ASSERT_EQ(frame.has_transparency, 1);
    ASSERT_TRUE(frame.indices[2] == frame.transparent_index);

    uint8_t *out = NULL;
    size_t out_len = 0;
    ASSERT_EQ(neverc_gif_frame_to_rgba(&frame, &out, &out_len), 0);
    ASSERT_TRUE(out != NULL);
    ASSERT_EQ(out_len, 16);
    ASSERT_EQ(out[11], 0); /* transparent pixel alpha */
    ASSERT_EQ(out[3], 255);
    ASSERT_EQ(out[7], 255);
    ASSERT_EQ(out[15], 255);

    uint8_t *gif = NULL;
    size_t glen = 0;
    ASSERT_EQ(neverc_gif_encode(&frame, &gif, &glen), 0);
    neverc_gif_image_t img;
    ASSERT_EQ(neverc_gif_decode(gif, glen, &img), 0);
    ASSERT_EQ(img.num_frames, 1);
    ASSERT_EQ(img.frames[0].has_transparency, 1);
    ASSERT_EQ(img.width, 4);
    ASSERT_EQ(img.height, 1);

    uint8_t *again = NULL;
    size_t again_len = 0;
    ASSERT_EQ(neverc_gif_frame_to_rgba(&img.frames[0], &again, &again_len), 0);
    ASSERT_EQ(again[11], 0);

    free(again);
    neverc_gif_free(&img);
    free(gif);
    free(out);
    free(frame.indices);

    ASSERT_EQ(neverc_gif_frame_to_rgba(NULL, &out, &out_len), -1);
    ASSERT_TRUE(out == NULL);
}

static void test_from_rgba_full_palette_transparency(void) {
    printf("[from_rgba_full_palette_transparency]\n");
    const uint32_t w = 16, h = 17;
    size_t np = (size_t)w * h;
    uint8_t *rgba = (uint8_t *)calloc(np, 4);
    ASSERT_TRUE(rgba != NULL);
    for (int i = 0; i < 256; i++) {
        rgba[i * 4 + 0] = (uint8_t)((i & 7) << 5);
        rgba[i * 4 + 1] = (uint8_t)(((i >> 3) & 7) << 5);
        rgba[i * 4 + 2] = (uint8_t)(((i >> 6) & 3) << 5);
        rgba[i * 4 + 3] = 255;
    }

    neverc_gif_frame_t frame;
    ASSERT_EQ(neverc_gif_from_rgba(rgba, w, h, &frame), 0);
    ASSERT_EQ(frame.has_transparency, 1);

    uint8_t *out = NULL;
    size_t out_len = 0;
    ASSERT_EQ(neverc_gif_frame_to_rgba(&frame, &out, &out_len), 0);
    ASSERT_TRUE(out != NULL);
    for (int i = 0; i < 256; i++)
        ASSERT_EQ(out[i * 4 + 3], 255);
    for (int i = 256; i < (int)np; i++)
        ASSERT_EQ(out[i * 4 + 3], 0);

    uint8_t *gif = NULL;
    size_t glen = 0;
    ASSERT_EQ(neverc_gif_encode(&frame, &gif, &glen), 0);
    neverc_gif_image_t img;
    ASSERT_EQ(neverc_gif_decode(gif, glen, &img), 0);
    ASSERT_EQ(img.num_frames, 1);
    uint8_t *again = NULL;
    size_t again_len = 0;
    ASSERT_EQ(neverc_gif_frame_to_rgba(&img.frames[0], &again, &again_len), 0);
    for (int i = 0; i < 256; i++)
        ASSERT_EQ(again[i * 4 + 3], 255);
    for (int i = 256; i < (int)np; i++)
        ASSERT_EQ(again[i * 4 + 3], 0);

    free(again);
    neverc_gif_free(&img);
    free(gif);
    free(out);
    free(frame.indices);
    free(rgba);
}

static void test_reserved_disposal_treated_as_none(void) {
    printf("[reserved_disposal_treated_as_none]\n");
    neverc_gif_frame_t frame;
    neverc_gif_frame_info_t frame_info = {0, 0, 2};
    memset(&frame, 0, sizeof(frame));
    frame.width = 2;
    frame.height = 2;
    frame.palette_size = 2;
    frame.palette[0] = (neverc_gif_color_t){0, 0, 0};
    frame.palette[1] = (neverc_gif_color_t){255, 255, 255};
    frame.indices = (uint8_t *)calloc(1, 4);
    ASSERT_TRUE(frame.indices != NULL);

    uint8_t *gif_data = NULL;
    size_t gif_len = 0;
    ASSERT_EQ(neverc_gif_encode_ex(
                  &frame, &frame_info, &gif_data, &gif_len), 0);
    ASSERT_TRUE(gif_data != NULL);
    if (!gif_data) {
        free(frame.indices);
        return;
    }

    size_t gce_pos = 0;
    while (gce_pos + 3 < gif_len &&
           !(gif_data[gce_pos] == 0x21 &&
             gif_data[gce_pos + 1] == 0xf9 &&
             gif_data[gce_pos + 2] == 0x04))
        gce_pos++;
    ASSERT_TRUE(gce_pos + 3 < gif_len);
    if (gce_pos + 3 < gif_len) {
        uint8_t packed = gif_data[gce_pos + 3];
        ASSERT_EQ((packed >> 2) & 7, 2);
        gif_data[gce_pos + 3] = (uint8_t)((packed & (uint8_t)~0x1C) | (4 << 2));
        neverc_gif_image_t img;
        ASSERT_EQ(neverc_gif_decode(gif_data, gif_len, &img), 0);
        ASSERT_EQ(img.num_frames, 1);
        if (img.num_frames == 1) {
            neverc_gif_frame_info_t decoded_info = {0, 0, 0};
            ASSERT_EQ(neverc_gif_frame_info(&img, 0, &decoded_info), 0);
            ASSERT_EQ(decoded_info.disposal_method, 0);
        }
        neverc_gif_free(&img);
    }

    free(gif_data);
    free(frame.indices);
}

int main(void) {
    printf("NeverC image/gif tests\n");
    test_encode_decode();
    test_from_rgba();
    test_invalid_data();
    test_rejects_trailing_bytes();
    test_single_color();
    test_large_roundtrip();
    test_interlaced_decode();
    test_netscape_loop_count();
    test_plain_text_consumes_gce();
    test_rejects_truncated_and_oversize_lzw();
    test_failed_decode_clears_geometry();
    test_frame_to_rgba_and_transparency();
    test_from_rgba_full_palette_transparency();
    test_reserved_disposal_treated_as_none();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
