#include "neverc/image/jpeg.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * JPEG Encoder — Baseline DCT, Huffman coding, YCbCr color space
 * Implements JFIF subset: 8-bit, 4:2:0 chroma subsampling
 * No libc math dependency — all trig via precomputed tables.
 * ========================================================================= */

/* Precomputed: cos((2*x+1)*u*PI/16) for x,u in [0,7] */
static const double dct_cos[8][8] = {
    { 1.000000000000000, 0.980785280403230, 0.923879532511287, 0.831469612302545,
      0.707106781186548, 0.555570233019602, 0.382683432365090, 0.195090322016128},
    { 1.000000000000000, 0.831469612302545, 0.382683432365090,-0.195090322016128,
     -0.707106781186548,-0.980785280403230,-0.923879532511287,-0.555570233019602},
    { 1.000000000000000, 0.555570233019602,-0.382683432365090,-0.980785280403230,
     -0.707106781186548, 0.195090322016128, 0.923879532511287, 0.831469612302545},
    { 1.000000000000000, 0.195090322016128,-0.923879532511287,-0.555570233019602,
      0.707106781186548, 0.831469612302545,-0.382683432365090,-0.980785280403230},
    { 1.000000000000000,-0.195090322016128,-0.923879532511287, 0.555570233019602,
      0.707106781186548,-0.831469612302545,-0.382683432365090, 0.980785280403230},
    { 1.000000000000000,-0.555570233019602,-0.382683432365090, 0.980785280403230,
     -0.707106781186548,-0.195090322016128, 0.923879532511287,-0.831469612302545},
    { 1.000000000000000,-0.831469612302545, 0.382683432365090, 0.195090322016128,
     -0.707106781186548, 0.980785280403230,-0.923879532511287, 0.555570233019602},
    { 1.000000000000000,-0.980785280403230, 0.923879532511287,-0.831469612302545,
      0.707106781186548,-0.555570233019602, 0.382683432365090,-0.195090322016128},
};
static const double INV_SQRT2 = 0.707106781186548;

/* Standard JPEG luminance quantization table (quality-scaled) */
static const uint8_t std_lum_quant[64] = {
    16,11,10,16,24,40,51,61, 12,12,14,19,26,58,60,55,
    14,13,16,24,40,57,69,56, 14,17,22,29,51,87,80,62,
    18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92,
    49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99
};
static const uint8_t std_chrom_quant[64] = {
    17,18,24,47,99,99,99,99, 18,21,26,66,99,99,99,99,
    24,26,56,99,99,99,99,99, 47,66,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99
};

/* Zigzag ordering */
static const int zigzag[64] = {
     0, 1, 5, 6,14,15,27,28,
     2, 4, 7,13,16,26,29,42,
     3, 8,12,17,25,30,41,43,
     9,11,18,24,31,40,44,53,
    10,19,23,32,39,45,52,54,
    20,22,33,38,46,51,55,60,
    21,34,37,47,50,56,59,61,
    35,36,48,49,57,58,62,63
};

/* Standard Huffman tables (from JPEG spec, Annex K) */
static const uint8_t dc_lum_bits[17] = {0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t dc_lum_val[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t dc_chrom_bits[17] = {0,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t dc_chrom_val[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

static const uint8_t ac_lum_bits[17] = {0,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
static const uint8_t ac_lum_val[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,
    0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,
    0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,
    0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,
    0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,
    0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,
    0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
    0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
    0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,
    0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,
    0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};

static const uint8_t ac_chrom_bits[17] = {0,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
static const uint8_t ac_chrom_val[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,
    0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,
    0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,
    0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,
    0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,
    0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,
    0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,
    0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
    0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,
    0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,
    0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};

typedef struct {
    uint16_t code;
    uint8_t  length;
} huff_entry_t;

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
    uint32_t bit_buf;
    int      bit_cnt;
} bitwriter_t;

static void bw_init(bitwriter_t *bw, size_t initial_cap) {
    bw->buf = (uint8_t *)malloc(initial_cap);
    bw->cap = initial_cap;
    bw->pos = 0;
    bw->bit_buf = 0;
    bw->bit_cnt = 0;
}

static void bw_ensure(bitwriter_t *bw, size_t need) {
    if (bw->pos + need > bw->cap) {
        bw->cap = (bw->pos + need) * 2;
        bw->buf = (uint8_t *)realloc(bw->buf, bw->cap);
    }
}

static void bw_write_byte(bitwriter_t *bw, uint8_t b) {
    bw_ensure(bw, 2);
    bw->buf[bw->pos++] = b;
    if (b == 0xFF) bw->buf[bw->pos++] = 0x00; /* byte stuffing */
}

static void bw_write_raw(bitwriter_t *bw, const uint8_t *data, size_t len) {
    bw_ensure(bw, len);
    memcpy(bw->buf + bw->pos, data, len);
    bw->pos += len;
}

static void bw_write_bits(bitwriter_t *bw, uint16_t code, int len) {
    bw->bit_buf = (bw->bit_buf << len) | (code & ((1 << len) - 1));
    bw->bit_cnt += len;
    while (bw->bit_cnt >= 8) {
        bw->bit_cnt -= 8;
        bw_write_byte(bw, (uint8_t)(bw->bit_buf >> bw->bit_cnt));
    }
}

static void bw_flush_bits(bitwriter_t *bw) {
    if (bw->bit_cnt > 0) {
        bw_write_byte(bw, (uint8_t)(bw->bit_buf << (8 - bw->bit_cnt)));
        bw->bit_cnt = 0;
        bw->bit_buf = 0;
    }
}

static void build_huffman_table(const uint8_t *bits, const uint8_t *vals,
                                huff_entry_t *table, int max_val) {
    memset(table, 0, (size_t)max_val * sizeof(huff_entry_t));
    uint16_t code = 0;
    int idx = 0;
    for (int len = 1; len <= 16; len++) {
        for (int i = 0; i < bits[len]; i++) {
            if (idx < max_val && vals[idx] < max_val) {
                table[vals[idx]].code = code;
                table[vals[idx]].length = (uint8_t)len;
            }
            idx++;
            code++;
        }
        code <<= 1;
    }
}

static void scale_quant_table(uint8_t *out, const uint8_t *base, int quality) {
    int q = quality < 1 ? 1 : (quality > 100 ? 100 : quality);
    int scale = q < 50 ? (5000 / q) : (200 - q * 2);
    for (int i = 0; i < 64; i++) {
        int val = ((int)base[i] * scale + 50) / 100;
        if (val < 1) val = 1;
        if (val > 255) val = 255;
        out[i] = (uint8_t)val;
    }
}

static void fdct_block(const int *input, int *output) {
    double tmp[64];
    for (int i = 0; i < 8; i++) {
        for (int u = 0; u < 8; u++) {
            double sum = 0.0;
            for (int x = 0; x < 8; x++)
                sum += input[i * 8 + x] * dct_cos[x][u];
            double cu = (u == 0) ? INV_SQRT2 : 1.0;
            tmp[i * 8 + u] = cu * sum / 2.0;
        }
    }
    for (int u = 0; u < 8; u++) {
        for (int v = 0; v < 8; v++) {
            double sum = 0.0;
            for (int y = 0; y < 8; y++)
                sum += tmp[y * 8 + u] * dct_cos[y][v];
            double cv = (v == 0) ? INV_SQRT2 : 1.0;
            output[v * 8 + u] = (int)(cv * sum / 2.0);
        }
    }
}

static int bit_length(int val) {
    if (val < 0) val = -val;
    int n = 0;
    while (val > 0) { n++; val >>= 1; }
    return n;
}

static void encode_block(bitwriter_t *bw, int *block, const uint8_t *quant,
                         int *prev_dc, const huff_entry_t *dc_table,
                         const huff_entry_t *ac_table) {
    int dct[64];
    fdct_block(block, dct);

    /* Quantize */
    int quantized[64];
    for (int i = 0; i < 64; i++) {
        double val = (double)dct[zigzag[i]] / quant[i];
        quantized[i] = (int)(val > 0 ? val + 0.5 : val - 0.5);
    }

    /* DC coefficient */
    int dc_diff = quantized[0] - *prev_dc;
    *prev_dc = quantized[0];
    int dc_bits = bit_length(dc_diff);
    bw_write_bits(bw, dc_table[dc_bits].code, dc_table[dc_bits].length);
    if (dc_bits > 0) {
        int dc_val = dc_diff < 0 ? dc_diff - 1 : dc_diff;
        bw_write_bits(bw, (uint16_t)(dc_val & ((1 << dc_bits) - 1)), dc_bits);
    }

    /* AC coefficients */
    int zero_count = 0;
    for (int i = 1; i < 64; i++) {
        if (quantized[i] == 0) {
            zero_count++;
        } else {
            while (zero_count >= 16) {
                bw_write_bits(bw, ac_table[0xF0].code, ac_table[0xF0].length);
                zero_count -= 16;
            }
            int ac_bits = bit_length(quantized[i]);
            int symbol = (zero_count << 4) | ac_bits;
            bw_write_bits(bw, ac_table[symbol].code, ac_table[symbol].length);
            int ac_val = quantized[i] < 0 ? quantized[i] - 1 : quantized[i];
            bw_write_bits(bw, (uint16_t)(ac_val & ((1 << ac_bits) - 1)), ac_bits);
            zero_count = 0;
        }
    }
    if (zero_count > 0) {
        bw_write_bits(bw, ac_table[0x00].code, ac_table[0x00].length);
    }
}

static void write_marker(bitwriter_t *bw, uint8_t marker) {
    bw_ensure(bw, 2);
    bw->buf[bw->pos++] = 0xFF;
    bw->buf[bw->pos++] = marker;
}

static void write_dht(bitwriter_t *bw, uint8_t cls_id, const uint8_t *bits, const uint8_t *vals) {
    int total = 0;
    for (int i = 1; i <= 16; i++) total += bits[i];
    int len = 2 + 1 + 16 + total;
    write_marker(bw, 0xC4);
    bw_ensure(bw, (size_t)len);
    bw->buf[bw->pos++] = (uint8_t)(len >> 8);
    bw->buf[bw->pos++] = (uint8_t)(len);
    bw->buf[bw->pos++] = cls_id;
    memcpy(bw->buf + bw->pos, bits + 1, 16);
    bw->pos += 16;
    memcpy(bw->buf + bw->pos, vals, (size_t)total);
    bw->pos += (size_t)total;
}

int neverc_jpeg_encode(const neverc_jpeg_image_t *img, int quality,
                       uint8_t **out_data, size_t *out_len) {
    if (!img || !img->pixels || !out_data || !out_len) return -1;
    if (img->width == 0 || img->height == 0) return -1;
    if (img->channels != 1 && img->channels != 3) return -1;

    uint8_t lum_quant[64], chrom_quant[64];
    scale_quant_table(lum_quant, std_lum_quant, quality);
    scale_quant_table(chrom_quant, std_chrom_quant, quality);

    huff_entry_t dc_lum[256], ac_lum[256], dc_chr[256], ac_chr[256];
    build_huffman_table(dc_lum_bits, dc_lum_val, dc_lum, 256);
    build_huffman_table(ac_lum_bits, ac_lum_val, ac_lum, 256);
    build_huffman_table(dc_chrom_bits, dc_chrom_val, dc_chr, 256);
    build_huffman_table(ac_chrom_bits, ac_chrom_val, ac_chr, 256);

    bitwriter_t bw;
    bw_init(&bw, img->width * img->height * 2);

    /* SOI */
    write_marker(&bw, 0xD8);

    /* APP0 (JFIF) */
    write_marker(&bw, 0xE0);
    uint8_t app0[] = {0,16, 'J','F','I','F',0, 1,1, 0, 0,1,0,1, 0,0};
    bw_write_raw(&bw, app0, sizeof(app0));

    /* DQT - luminance */
    write_marker(&bw, 0xDB);
    bw_ensure(&bw, 69);
    bw.buf[bw.pos++] = 0; bw.buf[bw.pos++] = 67; /* length */
    bw.buf[bw.pos++] = 0; /* table 0 */
    memcpy(bw.buf + bw.pos, lum_quant, 64); bw.pos += 64;

    /* DQT - chrominance (only for color) */
    if (img->channels == 3) {
        write_marker(&bw, 0xDB);
        bw_ensure(&bw, 69);
        bw.buf[bw.pos++] = 0; bw.buf[bw.pos++] = 67;
        bw.buf[bw.pos++] = 1; /* table 1 */
        memcpy(bw.buf + bw.pos, chrom_quant, 64); bw.pos += 64;
    }

    /* SOF0 (baseline DCT) */
    int ncomp = img->channels;
    int sof_len = 8 + 3 * ncomp;
    write_marker(&bw, 0xC0);
    bw_ensure(&bw, (size_t)sof_len);
    bw.buf[bw.pos++] = (uint8_t)(sof_len >> 8);
    bw.buf[bw.pos++] = (uint8_t)(sof_len);
    bw.buf[bw.pos++] = 8; /* precision */
    bw.buf[bw.pos++] = (uint8_t)(img->height >> 8);
    bw.buf[bw.pos++] = (uint8_t)(img->height);
    bw.buf[bw.pos++] = (uint8_t)(img->width >> 8);
    bw.buf[bw.pos++] = (uint8_t)(img->width);
    bw.buf[bw.pos++] = (uint8_t)ncomp;
    if (ncomp == 1) {
        bw.buf[bw.pos++] = 1; bw.buf[bw.pos++] = 0x11; bw.buf[bw.pos++] = 0;
    } else {
        bw.buf[bw.pos++] = 1; bw.buf[bw.pos++] = 0x11; bw.buf[bw.pos++] = 0; /* Y */
        bw.buf[bw.pos++] = 2; bw.buf[bw.pos++] = 0x11; bw.buf[bw.pos++] = 1; /* Cb */
        bw.buf[bw.pos++] = 3; bw.buf[bw.pos++] = 0x11; bw.buf[bw.pos++] = 1; /* Cr */
    }

    /* DHT */
    write_dht(&bw, 0x00, dc_lum_bits, dc_lum_val);
    write_dht(&bw, 0x10, ac_lum_bits, ac_lum_val);
    if (ncomp == 3) {
        write_dht(&bw, 0x01, dc_chrom_bits, dc_chrom_val);
        write_dht(&bw, 0x11, ac_chrom_bits, ac_chrom_val);
    }

    /* SOS */
    int sos_len = 6 + 2 * ncomp;
    write_marker(&bw, 0xDA);
    bw_ensure(&bw, (size_t)sos_len);
    bw.buf[bw.pos++] = (uint8_t)(sos_len >> 8);
    bw.buf[bw.pos++] = (uint8_t)(sos_len);
    bw.buf[bw.pos++] = (uint8_t)ncomp;
    if (ncomp == 1) {
        bw.buf[bw.pos++] = 1; bw.buf[bw.pos++] = 0x00;
    } else {
        bw.buf[bw.pos++] = 1; bw.buf[bw.pos++] = 0x00; /* Y: DC=0, AC=0 */
        bw.buf[bw.pos++] = 2; bw.buf[bw.pos++] = 0x11; /* Cb: DC=1, AC=1 */
        bw.buf[bw.pos++] = 3; bw.buf[bw.pos++] = 0x11; /* Cr: DC=1, AC=1 */
    }
    bw.buf[bw.pos++] = 0; bw.buf[bw.pos++] = 63; bw.buf[bw.pos++] = 0;

    /* Encode MCUs */
    uint32_t mcu_w = (img->width + 7) / 8;
    uint32_t mcu_h = (img->height + 7) / 8;
    int prev_dc_y = 0, prev_dc_cb = 0, prev_dc_cr = 0;

    for (uint32_t my = 0; my < mcu_h; my++) {
        for (uint32_t mx = 0; mx < mcu_w; mx++) {
            if (ncomp == 1) {
                int block[64];
                for (int by = 0; by < 8; by++) {
                    for (int bx = 0; bx < 8; bx++) {
                        uint32_t px = mx * 8 + (uint32_t)bx;
                        uint32_t py = my * 8 + (uint32_t)by;
                        if (px >= img->width) px = img->width - 1;
                        if (py >= img->height) py = img->height - 1;
                        block[by * 8 + bx] = (int)img->pixels[py * img->stride + px] - 128;
                    }
                }
                encode_block(&bw, block, lum_quant, &prev_dc_y, dc_lum, ac_lum);
            } else {
                int y_block[64], cb_block[64], cr_block[64];
                for (int by = 0; by < 8; by++) {
                    for (int bx = 0; bx < 8; bx++) {
                        uint32_t px = mx * 8 + (uint32_t)bx;
                        uint32_t py = my * 8 + (uint32_t)by;
                        if (px >= img->width) px = img->width - 1;
                        if (py >= img->height) py = img->height - 1;
                        const uint8_t *p = img->pixels + py * img->stride + px * 3;
                        int r = p[0], g = p[1], b = p[2];
                        y_block[by * 8 + bx]  = (int)( 0.299*r + 0.587*g + 0.114*b) - 128;
                        cb_block[by * 8 + bx] = (int)(-0.1687*r - 0.3313*g + 0.5*b + 128) - 128;
                        cr_block[by * 8 + bx] = (int)( 0.5*r - 0.4187*g - 0.0813*b + 128) - 128;
                    }
                }
                encode_block(&bw, y_block, lum_quant, &prev_dc_y, dc_lum, ac_lum);
                encode_block(&bw, cb_block, chrom_quant, &prev_dc_cb, dc_chr, ac_chr);
                encode_block(&bw, cr_block, chrom_quant, &prev_dc_cr, dc_chr, ac_chr);
            }
        }
    }

    bw_flush_bits(&bw);

    /* EOI */
    write_marker(&bw, 0xD9);

    *out_data = bw.buf;
    *out_len = bw.pos;
    return 0;
}

/* =========================================================================
 * JPEG Decoder — Baseline DCT
 * ========================================================================= */

typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
    uint32_t       bit_buf;
    int            bit_cnt;
} bitreader_t;

static void br_init(bitreader_t *br, const uint8_t *data, size_t len) {
    br->data = data; br->len = len; br->pos = 0;
    br->bit_buf = 0; br->bit_cnt = 0;
}

static uint8_t br_read_byte_raw(bitreader_t *br) {
    if (br->pos >= br->len) return 0;
    return br->data[br->pos++];
}

static uint16_t br_read_u16(bitreader_t *br) {
    uint8_t h = br_read_byte_raw(br);
    uint8_t l = br_read_byte_raw(br);
    return ((uint16_t)h << 8) | l;
}

static int br_read_bit(bitreader_t *br) {
    if (br->bit_cnt == 0) {
        uint8_t b = br_read_byte_raw(br);
        if (b == 0xFF) {
            uint8_t next = br_read_byte_raw(br);
            (void)next; /* skip stuffed 0x00 */
        }
        br->bit_buf = b;
        br->bit_cnt = 8;
    }
    br->bit_cnt--;
    return (br->bit_buf >> br->bit_cnt) & 1;
}

static int br_read_bits(bitreader_t *br, int n) {
    int val = 0;
    for (int i = 0; i < n; i++)
        val = (val << 1) | br_read_bit(br);
    return val;
}

typedef struct {
    uint8_t symbol;
    uint8_t length;
} huff_decode_entry_t;

typedef struct {
    int mincode[17];
    int maxcode[17];
    int valptr[17];
    uint8_t *vals;
    int num_vals;
} huff_decode_table_t;

static void build_decode_table(huff_decode_table_t *t, const uint8_t *bits, const uint8_t *vals) {
    int total = 0;
    for (int i = 1; i <= 16; i++) total += bits[i];
    t->vals = (uint8_t *)malloc((size_t)total);
    t->num_vals = total;
    memcpy(t->vals, vals, (size_t)total);

    int code = 0;
    int idx = 0;
    for (int len = 1; len <= 16; len++) {
        t->valptr[len] = idx;
        if (bits[len] > 0) {
            t->mincode[len] = code;
            code += bits[len];
            t->maxcode[len] = code - 1;
        } else {
            t->mincode[len] = -1;
            t->maxcode[len] = -1;
        }
        idx += bits[len];
        code <<= 1;
    }
}

static int huff_decode(bitreader_t *br, const huff_decode_table_t *t) {
    int code = 0;
    for (int len = 1; len <= 16; len++) {
        code = (code << 1) | br_read_bit(br);
        if (t->maxcode[len] >= 0 && code <= t->maxcode[len]) {
            int idx = t->valptr[len] + code - t->mincode[len];
            if (idx < t->num_vals) return t->vals[idx];
            return -1;
        }
    }
    return -1;
}

static int decode_value(bitreader_t *br, int bits) {
    if (bits == 0) return 0;
    int val = br_read_bits(br, bits);
    if (val < (1 << (bits - 1)))
        val = val - (1 << bits) + 1;
    return val;
}

static void idct_block(const int *input, int *output) {
    double tmp[64];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            double sum = 0.0;
            for (int u = 0; u < 8; u++) {
                double cu = (u == 0) ? INV_SQRT2 : 1.0;
                sum += cu * input[y * 8 + u] * dct_cos[x][u];
            }
            tmp[y * 8 + x] = sum / 2.0;
        }
    }
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            double sum = 0.0;
            for (int v = 0; v < 8; v++) {
                double cv = (v == 0) ? INV_SQRT2 : 1.0;
                sum += cv * tmp[v * 8 + x] * dct_cos[y][v];
            }
            int val = (int)(sum / 2.0 + 128.5);
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            output[y * 8 + x] = val;
        }
    }
}

int neverc_jpeg_decode(const uint8_t *data, size_t len, neverc_jpeg_image_t *img) {
    if (!data || len < 4 || !img) return -1;
    memset(img, 0, sizeof(*img));

    if (data[0] != 0xFF || data[1] != 0xD8) return -1;

    bitreader_t br;
    br_init(&br, data, len);
    br.pos = 2;

    uint32_t width = 0, height = 0;
    int ncomp = 0;
    uint8_t quant_tables[4][64];
    memset(quant_tables, 0, sizeof(quant_tables));
    int comp_quant[4] = {0};
    int comp_dc_table[4] = {0};
    int comp_ac_table[4] = {0};

    huff_decode_table_t dc_tables[4], ac_tables[4];
    memset(dc_tables, 0, sizeof(dc_tables));
    memset(ac_tables, 0, sizeof(ac_tables));

    int scan_found = 0;

    while (br.pos < br.len - 1) {
        if (br.data[br.pos] != 0xFF) { br.pos++; continue; }
        uint8_t marker = br.data[br.pos + 1];
        br.pos += 2;

        if (marker == 0xD9) break; /* EOI */
        if (marker == 0x00 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;

        uint16_t seg_len = br_read_u16(&br);
        size_t seg_start = br.pos;

        if (marker == 0xDB) { /* DQT */
            while (br.pos < seg_start + seg_len - 2) {
                uint8_t info = br_read_byte_raw(&br);
                int table_id = info & 0x0F;
                if (table_id >= 4) break;
                for (int i = 0; i < 64; i++)
                    quant_tables[table_id][i] = br_read_byte_raw(&br);
            }
        } else if (marker == 0xC0) { /* SOF0 */
            br_read_byte_raw(&br); /* precision */
            height = br_read_u16(&br);
            width = br_read_u16(&br);
            ncomp = br_read_byte_raw(&br);
            if (ncomp != 1 && ncomp != 3) goto fail;
            for (int i = 0; i < ncomp; i++) {
                br_read_byte_raw(&br); /* component id */
                br_read_byte_raw(&br); /* sampling factors */
                comp_quant[i] = br_read_byte_raw(&br);
            }
        } else if (marker == 0xC4) { /* DHT */
            while (br.pos < seg_start + seg_len - 2) {
                uint8_t info = br_read_byte_raw(&br);
                int cls = (info >> 4) & 1;
                int table_id = info & 0x0F;
                if (table_id >= 4) break;
                uint8_t bits[17] = {0};
                int total = 0;
                for (int i = 1; i <= 16; i++) {
                    bits[i] = br_read_byte_raw(&br);
                    total += bits[i];
                }
                uint8_t *vals = (uint8_t *)malloc((size_t)total);
                for (int i = 0; i < total; i++)
                    vals[i] = br_read_byte_raw(&br);
                huff_decode_table_t *tbl = cls ? &ac_tables[table_id] : &dc_tables[table_id];
                if (tbl->vals) free(tbl->vals);
                build_decode_table(tbl, bits, vals);
                free(vals);
            }
        } else if (marker == 0xDA) { /* SOS */
            int ns = br_read_byte_raw(&br);
            for (int i = 0; i < ns; i++) {
                br_read_byte_raw(&br); /* component selector */
                uint8_t td_ta = br_read_byte_raw(&br);
                comp_dc_table[i] = (td_ta >> 4) & 0x0F;
                comp_ac_table[i] = td_ta & 0x0F;
            }
            br_read_byte_raw(&br); br_read_byte_raw(&br); br_read_byte_raw(&br);
            scan_found = 1;
            break;
        } else {
            br.pos = seg_start + seg_len - 2;
        }
    }

    if (!scan_found || width == 0 || height == 0) goto fail;

    img->width = width;
    img->height = height;
    img->channels = (ncomp == 1) ? 1 : 3;
    img->stride = (size_t)width * img->channels;
    img->pixels = (uint8_t *)calloc(1, img->stride * height);
    if (!img->pixels) goto fail;

    uint32_t mcu_w = (width + 7) / 8;
    uint32_t mcu_h = (height + 7) / 8;
    int prev_dc[4] = {0};

    br.bit_cnt = 0;

    for (uint32_t my = 0; my < mcu_h; my++) {
        for (uint32_t mx = 0; mx < mcu_w; mx++) {
            int blocks[3][64];
            for (int c = 0; c < ncomp; c++) {
                int quantized[64] = {0};
                /* DC */
                int dc_sym = huff_decode(&br, &dc_tables[comp_dc_table[c]]);
                if (dc_sym < 0) dc_sym = 0;
                int dc_val = decode_value(&br, dc_sym);
                prev_dc[c] += dc_val;
                quantized[0] = prev_dc[c];

                /* AC */
                int idx = 1;
                while (idx < 64) {
                    int ac_sym = huff_decode(&br, &ac_tables[comp_ac_table[c]]);
                    if (ac_sym < 0) break;
                    if (ac_sym == 0x00) break; /* EOB */
                    if (ac_sym == 0xF0) { idx += 16; continue; }
                    int run = (ac_sym >> 4) & 0x0F;
                    int size = ac_sym & 0x0F;
                    idx += run;
                    if (idx < 64) {
                        quantized[idx] = decode_value(&br, size);
                        idx++;
                    }
                }

                /* Dequantize */
                int dequant[64];
                int qt = comp_quant[c];
                if (qt >= 4) qt = 0;
                for (int i = 0; i < 64; i++)
                    dequant[zigzag[i]] = quantized[i] * (int)quant_tables[qt][i];

                /* IDCT */
                idct_block(dequant, blocks[c]);
            }

            /* Write pixels */
            for (int by = 0; by < 8; by++) {
                for (int bx = 0; bx < 8; bx++) {
                    uint32_t px = mx * 8 + (uint32_t)bx;
                    uint32_t py = my * 8 + (uint32_t)by;
                    if (px >= width || py >= height) continue;

                    if (ncomp == 1) {
                        img->pixels[py * img->stride + px] = (uint8_t)blocks[0][by * 8 + bx];
                    } else {
                        int Y  = blocks[0][by * 8 + bx];
                        int Cb = blocks[1][by * 8 + bx] - 128;
                        int Cr = blocks[2][by * 8 + bx] - 128;
                        int r = (int)(Y + 1.402 * Cr);
                        int g = (int)(Y - 0.34414 * Cb - 0.71414 * Cr);
                        int b = (int)(Y + 1.772 * Cb);
                        if (r < 0) r = 0; if (r > 255) r = 255;
                        if (g < 0) g = 0; if (g > 255) g = 255;
                        if (b < 0) b = 0; if (b > 255) b = 255;
                        uint8_t *p = img->pixels + py * img->stride + px * 3;
                        p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b;
                    }
                }
            }
        }
    }

    for (int i = 0; i < 4; i++) { free(dc_tables[i].vals); free(ac_tables[i].vals); }
    return 0;

fail:
    for (int i = 0; i < 4; i++) { free(dc_tables[i].vals); free(ac_tables[i].vals); }
    free(img->pixels);
    img->pixels = NULL;
    return -1;
}

void neverc_jpeg_free(neverc_jpeg_image_t *img) {
    if (img && img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
}
