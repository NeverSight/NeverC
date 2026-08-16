#include "neverc/std/image/jpeg.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * JPEG Encoder — Baseline DCT, Huffman coding, YCbCr color space
 * Implements JFIF subset: 8-bit, 4:2:0 chroma subsampling
 * No libc math dependency — all trig via precomputed tables.
 * ========================================================================= */

/* AAN (Arai-Agui-Nakajima) per-coefficient scale factors, shared by the forward
 * DCT (encoder) and the inverse DCT (decoder). The fast transforms emit/consume
 * coefficients pre-scaled by aanscale[row]*aanscale[col]; that factor is folded
 * into the quantization divisor (encoder) and the dequantization multiplier
 * (decoder), so only ~5 multiplies are needed per 8-point 1-D transform instead
 * of 64. (Same approach as libjpeg's float DCT/IDCT, jfdctflt/jidctflt.) */
static const double aanscale[8] = {
    1.0, 1.387039845322148, 1.306562964876377, 1.175875602419359,
    1.0, 0.785694958387102, 0.541196100146197, 0.275899379282943
};

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

/* JPEG zigzag scan order: jpeg_natural_order[s] is the natural (row-major) 8x8
 * index of the coefficient carried at zigzag scan position s. Entropy-coded
 * coefficients arrive in scan order; this maps each to its row-major slot for
 * the DCT/IDCT (and back). This is the standard JFIF / libjpeg ordering and is
 * mandatory for interop with every other JPEG codec. NOTE: the previous table
 * here was the *inverse* permutation (natural -> scan) and was applied as if it
 * were scan -> natural in both the encoder and decoder; the two cancelled in
 * NeverC's own round-trip but produced a stream no other decoder (browsers,
 * libjpeg, cameras) could read, and likewise garbled real-world JPEGs. */
static const int jpeg_natural_order[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
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
    int      failed;
} bitwriter_t;

static void bw_init(bitwriter_t *bw, size_t initial_cap) {
    bw->buf = (uint8_t *)malloc(initial_cap);
    bw->cap = initial_cap;
    bw->pos = 0;
    bw->bit_buf = 0;
    bw->bit_cnt = 0;
    bw->failed = bw->buf == NULL;
}

static int bw_ensure(bitwriter_t *bw, size_t need) {
    if (bw->failed) return -1;
    if (need > SIZE_MAX - bw->pos) { bw->failed = 1; return -1; }
    size_t required = bw->pos + need;
    if (required <= bw->cap) return 0;
    size_t capacity = bw->cap ? bw->cap : 256;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) { capacity = required; break; }
        capacity *= 2;
    }
    uint8_t *grown = (uint8_t *)realloc(bw->buf, capacity);
    if (!grown) { bw->failed = 1; return -1; }
    bw->buf = grown;
    bw->cap = capacity;
    return 0;
}

static void bw_write_byte(bitwriter_t *bw, uint8_t b) {
    if (bw_ensure(bw, 2) != 0) return;
    bw->buf[bw->pos++] = b;
    if (b == 0xFF) bw->buf[bw->pos++] = 0x00; /* byte stuffing */
}

static void bw_write_raw(bitwriter_t *bw, const uint8_t *data, size_t len) {
    if ((!data && len != 0) || bw_ensure(bw, len) != 0) {
        bw->failed = 1;
        return;
    }
    memcpy(bw->buf + bw->pos, data, len);
    bw->pos += len;
}

static void bw_write_bits(bitwriter_t *bw, uint16_t code, int len) {
    bw->bit_buf = (bw->bit_buf << len) | (code & ((1u << len) - 1u));
    bw->bit_cnt += len;
    while (bw->bit_cnt >= 8) {
        bw->bit_cnt -= 8;
        bw_write_byte(bw, (uint8_t)(bw->bit_buf >> bw->bit_cnt));
    }
}

static void bw_flush_bits(bitwriter_t *bw) {
    if (bw->bit_cnt > 0) {
        int padding = 8 - bw->bit_cnt;
        uint8_t fill = (uint8_t)((1U << padding) - 1U);
        bw_write_byte(
            bw, (uint8_t)((bw->bit_buf << padding) | fill));
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

/*
 * AAN fast forward DCT (after Arai, Agui & Nakajima 1988; port of libjpeg
 * jfdctflt). In-place on a row-major 8x8 float block; output[row*8+col] is the
 * DCT coefficient scaled by aanscale[row]*aanscale[col] (descaled during
 * quantization). 8 row + 8 column 1-D transforms, 5 multiplies each.
 */
static void fdct_aan(float *data) {
    for (int pass = 0; pass < 2; pass++) {
        int stride = pass == 0 ? 1 : 8;   /* rows first, then columns */
        int step   = pass == 0 ? 8 : 1;
        for (int k = 0; k < 8; k++) {
            float *d = data + k * step;
            float t0 = d[0*stride] + d[7*stride], t7 = d[0*stride] - d[7*stride];
            float t1 = d[1*stride] + d[6*stride], t6 = d[1*stride] - d[6*stride];
            float t2 = d[2*stride] + d[5*stride], t5 = d[2*stride] - d[5*stride];
            float t3 = d[3*stride] + d[4*stride], t4 = d[3*stride] - d[4*stride];

            float t10 = t0 + t3, t13 = t0 - t3;
            float t11 = t1 + t2, t12 = t1 - t2;
            d[0*stride] = t10 + t11;
            d[4*stride] = t10 - t11;
            float z1 = (t12 + t13) * 0.707106781f;
            d[2*stride] = t13 + z1;
            d[6*stride] = t13 - z1;

            t10 = t4 + t5; t11 = t5 + t6; t12 = t6 + t7;
            float z5 = (t10 - t12) * 0.382683433f;
            float z2 = 0.541196100f * t10 + z5;
            float z4 = 1.306562965f * t12 + z5;
            float z3 = t11 * 0.707106781f;
            float z11 = t7 + z3, z13 = t7 - z3;
            d[5*stride] = z13 + z2;
            d[3*stride] = z13 - z2;
            d[1*stride] = z11 + z4;
            d[7*stride] = z11 - z4;
        }
    }
}

static int bit_length(int val) {
    if (val < 0) val = -val;
    int n = 0;
    while (val > 0) { n++; val >>= 1; }
    return n;
}

/* Precompute the per-coefficient quantization reciprocals once per table. The
 * AAN fast DCT leaves coefficient nat scaled by aanscale[row]*aanscale[col];
 * folding that, the 1/8 DCT normalization and 1/quant into a single reciprocal
 * (scan-ordered to match the emit loop) turns encode_block's hot path from a
 * per-block divisor rebuild + true divide into one multiply — the libjpeg float
 * quantization. recip[i] pairs with fdata[jpeg_natural_order[i]]. */
static void jpeg_recip_table(double *recip, const uint8_t *quant) {
    for (int i = 0; i < 64; i++) {
        int nat = jpeg_natural_order[i];   /* natural slot for scan position i */
        double div = (double)quant[nat] * 8.0 * aanscale[nat >> 3] * aanscale[nat & 7];
        recip[i] = 1.0 / div;
    }
}

static void encode_block(bitwriter_t *bw, int *block, const double *recip,
                         int *prev_dc, const huff_entry_t *dc_table,
                         const huff_entry_t *ac_table) {
    float fdata[64];
    for (int i = 0; i < 64; i++) fdata[i] = (float)block[i];
    fdct_aan(fdata);

    int quantized[64];
    for (int i = 0; i < 64; i++) {
        double val = (double)fdata[jpeg_natural_order[i]] * recip[i];
        quantized[i] = (int)(val > 0 ? val + 0.5 : val - 0.5);
    }

    /* DC coefficient */
    int dc_diff = quantized[0] - *prev_dc;
    *prev_dc = quantized[0];
    int dc_bits = bit_length(dc_diff);
    bw_write_bits(bw, dc_table[dc_bits].code, dc_table[dc_bits].length);
    if (dc_bits > 0) {
        int dc_val = dc_diff < 0 ? dc_diff - 1 : dc_diff;
        bw_write_bits(bw, (uint16_t)(dc_val & ((1u << dc_bits) - 1u)), dc_bits);
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
            bw_write_bits(bw, (uint16_t)(ac_val & ((1u << ac_bits) - 1u)), ac_bits);
            zero_count = 0;
        }
    }
    if (zero_count > 0) {
        bw_write_bits(bw, ac_table[0x00].code, ac_table[0x00].length);
    }
}

static void write_marker(bitwriter_t *bw, uint8_t marker) {
    if (bw_ensure(bw, 2) != 0) return;
    bw->buf[bw->pos++] = 0xFF;
    bw->buf[bw->pos++] = marker;
}

static void write_dht(bitwriter_t *bw, uint8_t cls_id, const uint8_t *bits, const uint8_t *vals) {
    int total = 0;
    for (int i = 1; i <= 16; i++) total += bits[i];
    int len = 2 + 1 + 16 + total;
    write_marker(bw, 0xC4);
    if (bw_ensure(bw, (size_t)len) != 0) return;
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
    if (!out_data || !out_len) return -1;
    *out_data = NULL;
    *out_len = 0;
    if (!img || !img->pixels) return -1;
    if (img->width == 0 || img->height == 0) return -1;
    if (img->width > UINT16_MAX || img->height > UINT16_MAX) return -1;
    if (img->channels != 1 && img->channels != 3) return -1;
    size_t row_size = (size_t)img->width * img->channels;
    if (img->stride < row_size ||
        (img->height > 1 &&
         img->stride > (SIZE_MAX - row_size) / (img->height - 1))) return -1;
    uint64_t pixel_count = (uint64_t)img->width * img->height;
    if (pixel_count > (1u << 28) || pixel_count > SIZE_MAX / 2) return -1;

    uint8_t lum_quant[64], chrom_quant[64];
    scale_quant_table(lum_quant, std_lum_quant, quality);
    scale_quant_table(chrom_quant, std_chrom_quant, quality);

    /* One reciprocal table per quant table, reused by every block (the divisor
     * is constant across blocks of the same component). */
    double lum_recip[64], chrom_recip[64];
    jpeg_recip_table(lum_recip, lum_quant);
    jpeg_recip_table(chrom_recip, chrom_quant);

    huff_entry_t dc_lum[256], ac_lum[256], dc_chr[256], ac_chr[256];
    build_huffman_table(dc_lum_bits, dc_lum_val, dc_lum, 256);
    build_huffman_table(ac_lum_bits, ac_lum_val, ac_lum, 256);
    build_huffman_table(dc_chrom_bits, dc_chrom_val, dc_chr, 256);
    build_huffman_table(ac_chrom_bits, ac_chrom_val, ac_chr, 256);

    bitwriter_t bw;
    size_t initial_capacity = (size_t)pixel_count * 2;
    if (initial_capacity < 256) initial_capacity = 256;
    bw_init(&bw, initial_capacity);
    if (bw.failed) return -1;

    /* SOI */
    write_marker(&bw, 0xD8);

    /* APP0 (JFIF) */
    write_marker(&bw, 0xE0);
    uint8_t app0[] = {0,16, 'J','F','I','F',0, 1,1, 0, 0,1,0,1, 0,0};
    bw_write_raw(&bw, app0, sizeof(app0));

    /* DQT - luminance. The DQT segment carries the 64 quant values in zigzag
     * scan order (ITU T.81 B.2.4.1), so reorder our natural-order table. */
    write_marker(&bw, 0xDB);
    if (bw_ensure(&bw, 69) != 0) goto encode_failed;
    bw.buf[bw.pos++] = 0; bw.buf[bw.pos++] = 67; /* length */
    bw.buf[bw.pos++] = 0; /* table 0 */
    for (int s = 0; s < 64; s++) bw.buf[bw.pos++] = lum_quant[jpeg_natural_order[s]];

    /* DQT - chrominance (only for color) */
    if (img->channels == 3) {
        write_marker(&bw, 0xDB);
        if (bw_ensure(&bw, 69) != 0) goto encode_failed;
        bw.buf[bw.pos++] = 0; bw.buf[bw.pos++] = 67;
        bw.buf[bw.pos++] = 1; /* table 1 */
        for (int s = 0; s < 64; s++) bw.buf[bw.pos++] = chrom_quant[jpeg_natural_order[s]];
    }

    /* SOF0 (baseline DCT) */
    int ncomp = img->channels;
    int sof_len = 8 + 3 * ncomp;
    write_marker(&bw, 0xC0);
    if (bw_ensure(&bw, (size_t)sof_len) != 0) goto encode_failed;
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
        bw.buf[bw.pos++] = 1; bw.buf[bw.pos++] = 0x22; bw.buf[bw.pos++] = 0; /* Y 4:2:0 */
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
    if (bw_ensure(&bw, (size_t)sos_len) != 0) goto encode_failed;
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

    /* Encode MCUs. Color images use 4:2:0 (16x16 luma MCU = four 8x8 Y blocks
     * plus one 8x8 Cb and one 8x8 Cr, each chroma block covering the MCU). */
    uint32_t mcu_w = (ncomp == 1) ? (img->width + 7) / 8 : (img->width + 15) / 16;
    uint32_t mcu_h = (ncomp == 1) ? (img->height + 7) / 8 : (img->height + 15) / 16;
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
                encode_block(&bw, block, lum_recip, &prev_dc_y, dc_lum, ac_lum);
            } else {
                int y_block[64], cb_block[64], cr_block[64];
                for (int sby = 0; sby < 2; sby++) {
                    for (int sbx = 0; sbx < 2; sbx++) {
                        for (int by = 0; by < 8; by++) {
                            for (int bx = 0; bx < 8; bx++) {
                                uint32_t px = mx * 16 + (uint32_t)sbx * 8 + (uint32_t)bx;
                                uint32_t py = my * 16 + (uint32_t)sby * 8 + (uint32_t)by;
                                if (px >= img->width) px = img->width - 1;
                                if (py >= img->height) py = img->height - 1;
                                const uint8_t *p = img->pixels + py * img->stride + px * 3;
                                int r = p[0], g = p[1], b = p[2];
                                int yy =  19595*r + 38470*g +  7471*b + 32768;
                                y_block[by * 8 + bx] = (yy >> 16) - 128;
                            }
                        }
                        encode_block(&bw, y_block, lum_recip, &prev_dc_y, dc_lum, ac_lum);
                    }
                }
                for (int by = 0; by < 8; by++) {
                    for (int bx = 0; bx < 8; bx++) {
                        int sum_cb = 0, sum_cr = 0;
                        for (int dy = 0; dy < 2; dy++) {
                            for (int dx = 0; dx < 2; dx++) {
                                uint32_t px = mx * 16 + (uint32_t)bx * 2 + (uint32_t)dx;
                                uint32_t py = my * 16 + (uint32_t)by * 2 + (uint32_t)dy;
                                if (px >= img->width) px = img->width - 1;
                                if (py >= img->height) py = img->height - 1;
                                const uint8_t *p = img->pixels + py * img->stride + px * 3;
                                int r = p[0], g = p[1], b = p[2];
                                int cb = -11056*r - 21712*g + 32768*b + (257 << 15);
                                int cr =  32768*r - 27440*g -  5328*b + (257 << 15);
                                if (cb < 0) cb = 0; else if (cb > (255 << 16)) cb = 255 << 16;
                                if (cr < 0) cr = 0; else if (cr > (255 << 16)) cr = 255 << 16;
                                sum_cb += (cb >> 16) - 128;
                                sum_cr += (cr >> 16) - 128;
                            }
                        }
                        cb_block[by * 8 + bx] = sum_cb / 4;
                        cr_block[by * 8 + bx] = sum_cr / 4;
                    }
                }
                encode_block(&bw, cb_block, chrom_recip, &prev_dc_cb, dc_chr, ac_chr);
                encode_block(&bw, cr_block, chrom_recip, &prev_dc_cr, dc_chr, ac_chr);
            }
        }
    }

    bw_flush_bits(&bw);

    /* EOI */
    write_marker(&bw, 0xD9);

    if (bw.failed) goto encode_failed;

    *out_data = bw.buf;
    *out_len = bw.pos;
    return 0;

encode_failed:
    free(bw.buf);
    return -1;
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
    int            failed;
} bitreader_t;

static void br_init(bitreader_t *br, const uint8_t *data, size_t len) {
    br->data = data; br->len = len; br->pos = 0;
    br->bit_buf = 0; br->bit_cnt = 0;
    br->failed = 0;
}

static uint8_t br_read_byte_raw(bitreader_t *br) {
    if (br->pos >= br->len) {
        br->failed = 1;
        return 0;
    }
    return br->data[br->pos++];
}

static uint16_t br_read_u16(bitreader_t *br) {
    uint8_t h = br_read_byte_raw(br);
    uint8_t l = br_read_byte_raw(br);
    return ((uint16_t)h << 8) | l;
}

/* Refill the MSB-first bit buffer so the low `bit_cnt` bits are valid (up to 32
 * after the call). Inside entropy data
 * 0xFF is followed by a stuffed 0x00 (emit the 0xFF, drop the 0x00); 0xFF
 * followed by anything else is a marker (RST restart, EOI, ...), so we stop
 * consuming and leave pos parked on the 0xFF for the restart handler / scan
 * loop. We never synthesize zero bits: callers can therefore distinguish a
 * truncated scan or an early marker from valid entropy data. */
static void br_refill(bitreader_t *br) {
    while (br->bit_cnt <= 24 && br->pos < br->len) {
        uint8_t b;
        if (br->data[br->pos] == 0xFF) {
            uint8_t nx =
                (br->pos + 1 < br->len) ? br->data[br->pos + 1] : 0xFF;
            if (nx == 0x00) {
                b = 0xFF;
                br->pos += 2;                 /* stuffed 0xFF 0x00 -> 0xFF */
            } else {
                break;
            }
        } else {
            b = br->data[br->pos++];
        }
        br->bit_buf = (br->bit_buf << 8) | b;
        br->bit_cnt += 8;
    }
}

/* Resynchronize at a restart-marker (FF D0..D7) boundary: drop the buffered
 * (now padding) bits and consume the marker so entropy decoding resumes byte-
 * aligned on the next interval. refill leaves pos on the marker's 0xFF; any
 * 0xFF fill bytes that legally precede a marker are skipped. The caller resets
 * the per-component DC predictors. */
static int br_restart(bitreader_t *br, uint8_t expected_marker) {
    br->bit_buf = 0;
    br->bit_cnt = 0;
    while (br->pos + 1 < br->len && br->data[br->pos] == 0xFF) {
        uint8_t m = br->data[br->pos + 1];
        if (m == 0xFF) { br->pos++; continue; }       /* fill byte */
        if (m != expected_marker) {
            br->failed = 1;
            return -1;
        }
        br->pos += 2;
        return 0;
    }
    br->failed = 1;
    return -1;
}

static int br_read_bits(bitreader_t *br, int n) {
    if (n == 0) return 0;
    if (n < 0 || n > 16) {
        br->failed = 1;
        return 0;
    }
    if (br->bit_cnt < n) br_refill(br);
    if (br->bit_cnt < n) {
        br->failed = 1;
        return 0;
    }
    br->bit_cnt -= n;
    return (int)((br->bit_buf >> br->bit_cnt) & (((uint32_t)1 << n) - 1));
}

static int br_read_bit(bitreader_t *br) {
    if (br->bit_cnt < 1) br_refill(br);
    if (br->bit_cnt < 1) {
        br->failed = 1;
        return 0;
    }
    br->bit_cnt -= 1;
    return (int)((br->bit_buf >> br->bit_cnt) & 1u);
}

typedef struct {
    uint8_t symbol;
    uint8_t length;
} huff_decode_entry_t;

/* Look up codes this many bits or shorter in one table hit; longer codes (rare)
 * fall back to the canonical bit-by-bit walk. 8 covers the vast majority of
 * JPEG symbols. (Same fast-decode strategy as the flate decoder.) */
#define JPEG_HUFF_LOOKAHEAD 8

typedef struct {
    int mincode[17];
    int maxcode[17];
    int valptr[17];
    uint8_t *vals;
    int num_vals;
    uint8_t look_nbits[1 << JPEG_HUFF_LOOKAHEAD];   /* code length, 0 => slow path */
    uint8_t look_sym[1 << JPEG_HUFF_LOOKAHEAD];     /* decoded symbol */
} huff_decode_table_t;

/* Returns 0 on success, -1 if `bits` describes an invalid (over-subscribed)
 * Huffman table. Rejecting those is a safety requirement, not just hygiene: an
 * over-subscribed table pushes a code past its bit-width, so the lookahead fill
 * below (lookbits = codeword << (LOOKAHEAD-len)) would index past the 2^LOOKAHEAD
 * look_* arrays and corrupt memory on crafted JPEGs. A valid canonical table
 * obeys Kraft (codes through length L number <= 2^L), keeping lookbits < 2^LOOKAHEAD. */
static int build_decode_table(huff_decode_table_t *t, const uint8_t *bits, const uint8_t *vals) {
    int total = 0;
    for (int i = 1; i <= 16; i++) total += bits[i];
    t->vals = (uint8_t *)malloc(total ? (size_t)total : 1);
    if (!t->vals) return -1;
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
        if (code > (int)(1u << len)) return -1;   /* over-subscribed → invalid table */
        idx += bits[len];
        code <<= 1;
    }

    /* Lookahead table: for every code of length <= LOOKAHEAD, fill all entries
     * whose top `len` bits equal the code with (length, symbol). The remaining
     * entries stay 0 (== "code is longer than LOOKAHEAD, use the slow path"). */
    memset(t->look_nbits, 0, sizeof(t->look_nbits));
    for (int len = 1; len <= JPEG_HUFF_LOOKAHEAD; len++) {
        if (bits[len] == 0) continue;
        for (int i = 0; i < bits[len]; i++) {
            int codeword = t->mincode[len] + i;
            int sym = t->vals[t->valptr[len] + i];
            int lookbits = codeword << (JPEG_HUFF_LOOKAHEAD - len);
            for (int ctr = 1 << (JPEG_HUFF_LOOKAHEAD - len); ctr > 0; ctr--) {
                t->look_nbits[lookbits] = (uint8_t)len;
                t->look_sym[lookbits] = (uint8_t)sym;
                lookbits++;
            }
        }
    }
    return 0;
}

static int huff_decode(bitreader_t *br, const huff_decode_table_t *t) {
    if (!t || !t->vals || t->num_vals <= 0)
        return -1;
    if (br->bit_cnt < JPEG_HUFF_LOOKAHEAD) br_refill(br);

    /* Fast path: one table hit resolves any code <= LOOKAHEAD bits. */
    if (br->bit_cnt >= JPEG_HUFF_LOOKAHEAD) {
        int look =
            (int)((br->bit_buf >> (br->bit_cnt - JPEG_HUFF_LOOKAHEAD))
                  & ((1 << JPEG_HUFF_LOOKAHEAD) - 1));
        int nb = t->look_nbits[look];
        if (nb) {
            br->bit_cnt -= nb;
            return t->look_sym[look];
        }
    }

    /* Slow path: walk from the first bit. This also handles a valid short code
     * immediately before a marker when fewer than LOOKAHEAD bits remain. */
    int code = 0;
    for (int len = 1; len <= 16; len++) {
        code = (code << 1) | br_read_bit(br);
        if (br->failed) return -1;
        if (t->maxcode[len] >= 0 &&
            code >= t->mincode[len] && code <= t->maxcode[len]) {
            int idx = t->valptr[len] + code - t->mincode[len];
            if (idx >= 0 && idx < t->num_vals) return t->vals[idx];
            return -1;
        }
    }
    return -1;
}

static int decode_value(bitreader_t *br, int bits, int *out) {
    if (!out || bits < 0 || bits > 16) return -1;
    if (bits == 0) {
        *out = 0;
        return 0;
    }
    int val = br_read_bits(br, bits);
    if (br->failed) return -1;
    if (val < (1 << (bits - 1)))
        val = val - (1 << bits) + 1;
    *out = val;
    return 0;
}

/*
 * AAN fast inverse DCT (after Arai, Agui & Nakajima 1988; port of libjpeg
 * jidctflt). Replaces the O(8^3) reference IDCT (~1024 multiplies per 8x8 block)
 * with 8 column + 8 row 1-D transforms of ~5 multiplies each (~80 total), the
 * inverse-direction counterpart of the encoder's fdct_aan.
 *
 * `input` holds the dequantized coefficients in natural (row-major) order,
 * already pre-scaled by aanscale[row]*aanscale[col] (folded into the dequant
 * multiplier, exactly as the encoder folds the forward scale into quantization).
 * `output` is the spatial 8x8 block, descaled by 8, level-shifted by +128 and
 * clamped to [0,255]. The all-AC-zero column shortcut (the common case, since
 * most coefficients quantize to zero) skips a full butterfly.
 */
static void idct_block(const float *input, int *output) {
    float ws[64];

    /* Pass 1: columns -> workspace. */
    for (int c = 0; c < 8; c++) {
        const float *in = input + c;
        float *w = ws + c;

        if (in[8] == 0.0f && in[16] == 0.0f && in[24] == 0.0f && in[32] == 0.0f &&
            in[40] == 0.0f && in[48] == 0.0f && in[56] == 0.0f) {
            float dc = in[0];
            w[0] = w[8] = w[16] = w[24] = w[32] = w[40] = w[48] = w[56] = dc;
            continue;
        }

        /* Even part */
        float tmp0 = in[0], tmp1 = in[16], tmp2 = in[32], tmp3 = in[48];
        float t10 = tmp0 + tmp2, t11 = tmp0 - tmp2;
        float t13 = tmp1 + tmp3;
        float t12 = (tmp1 - tmp3) * 1.414213562f - t13;
        tmp0 = t10 + t13; tmp3 = t10 - t13;
        tmp1 = t11 + t12; tmp2 = t11 - t12;

        /* Odd part */
        float tmp4 = in[8], tmp5 = in[24], tmp6 = in[40], tmp7 = in[56];
        float z13 = tmp6 + tmp5, z10 = tmp6 - tmp5;
        float z11 = tmp4 + tmp7, z12 = tmp4 - tmp7;
        tmp7 = z11 + z13;
        t11 = (z11 - z13) * 1.414213562f;
        float z5 = (z10 + z12) * 1.847759065f;
        t10 = 1.082392200f * z12 - z5;
        t12 = -2.613125930f * z10 + z5;
        tmp6 = t12 - tmp7;
        tmp5 = t11 - tmp6;
        tmp4 = t10 + tmp5;

        w[0]  = tmp0 + tmp7; w[56] = tmp0 - tmp7;
        w[8]  = tmp1 + tmp6; w[48] = tmp1 - tmp6;
        w[16] = tmp2 + tmp5; w[40] = tmp2 - tmp5;
        w[32] = tmp3 + tmp4; w[24] = tmp3 - tmp4;
    }

    /* Pass 2: rows -> descale by 8, level-shift +128, clamp. */
    for (int r = 0; r < 8; r++) {
        const float *w = ws + r * 8;
        int *o = output + r * 8;

        /* Even part */
        float t10 = w[0] + w[4], t11 = w[0] - w[4];
        float t13 = w[2] + w[6];
        float t12 = (w[2] - w[6]) * 1.414213562f - t13;
        float tmp0 = t10 + t13, tmp3 = t10 - t13;
        float tmp1 = t11 + t12, tmp2 = t11 - t12;

        /* Odd part */
        float z13 = w[5] + w[3], z10 = w[5] - w[3];
        float z11 = w[1] + w[7], z12 = w[1] - w[7];
        float tmp7 = z11 + z13;
        t11 = (z11 - z13) * 1.414213562f;
        float z5 = (z10 + z12) * 1.847759065f;
        t10 = 1.082392200f * z12 - z5;
        t12 = -2.613125930f * z10 + z5;
        float tmp6 = t12 - tmp7;
        float tmp5 = t11 - tmp6;
        float tmp4 = t10 + tmp5;

#define IDCT_OUT(idx, val) do { \
    int v = (int)((val) * 0.125f + 128.5f); \
    o[idx] = v < 0 ? 0 : (v > 255 ? 255 : v); \
} while (0)
        IDCT_OUT(0, tmp0 + tmp7);
        IDCT_OUT(7, tmp0 - tmp7);
        IDCT_OUT(1, tmp1 + tmp6);
        IDCT_OUT(6, tmp1 - tmp6);
        IDCT_OUT(2, tmp2 + tmp5);
        IDCT_OUT(5, tmp2 - tmp5);
        IDCT_OUT(4, tmp3 + tmp4);
        IDCT_OUT(3, tmp3 - tmp4);
#undef IDCT_OUT
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
    uint32_t restart_interval = 0;   /* MCUs between RST markers (0 = none) */
    uint8_t quant_tables[4][64];
    memset(quant_tables, 0, sizeof(quant_tables));
    int quant_present[4] = {0};
    int comp_id[4] = {0};
    int comp_quant[4] = {0};
    int comp_dc_table[4] = {0};
    int comp_ac_table[4] = {0};
    int comp_h[4] = {1, 1, 1, 1};   /* horizontal sampling factor per component */
    int comp_v[4] = {1, 1, 1, 1};   /* vertical sampling factor per component   */

    huff_decode_table_t dc_tables[4], ac_tables[4];
    memset(dc_tables, 0, sizeof(dc_tables));
    memset(ac_tables, 0, sizeof(ac_tables));

    int scan_found = 0;

    while (br.pos < br.len - 1) {
        if (br.data[br.pos] != 0xFF) { br.pos++; continue; }
        while (br.pos < br.len && br.data[br.pos] == 0xFF) br.pos++;
        if (br.pos >= br.len) goto fail;
        uint8_t marker = br.data[br.pos++];

        if (marker == 0xD9) break; /* EOI */
        if (marker == 0x00 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;

        uint16_t seg_len = br_read_u16(&br);
        if (br.failed || seg_len < 2) goto fail; /* length counts itself */
        size_t seg_start = br.pos;
        size_t seg_end = seg_start + (size_t)(seg_len - 2);
        if (seg_end > br.len) goto fail;

        if (marker == 0xDB) { /* DQT */
            while (br.pos < seg_end) {
                if (seg_end - br.pos < 65) goto fail;
                uint8_t info = br_read_byte_raw(&br);
                if ((info >> 4) != 0) goto fail; /* only 8-bit baseline DQT */
                int table_id = info & 0x0F;
                if (table_id >= 4) goto fail;
                for (int i = 0; i < 64; i++) {
                    quant_tables[table_id][i] = br_read_byte_raw(&br);
                    if (quant_tables[table_id][i] == 0) goto fail;
                }
                quant_present[table_id] = 1;
            }
        } else if (marker == 0xC0) { /* SOF0 */
            if (seg_end - br.pos < 6) goto fail;
            if (br_read_byte_raw(&br) != 8) goto fail; /* precision */
            height = br_read_u16(&br);
            width = br_read_u16(&br);
            ncomp = br_read_byte_raw(&br);
            if (ncomp != 1 && ncomp != 3) goto fail;
            if (seg_end - br.pos != (size_t)ncomp * 3U) goto fail;
            int blocks_per_mcu = 0;
            for (int i = 0; i < ncomp; i++) {
                comp_id[i] = br_read_byte_raw(&br);
                for (int j = 0; j < i; j++)
                    if (comp_id[j] == comp_id[i]) goto fail;
                uint8_t samp = br_read_byte_raw(&br);
                comp_h[i] = (samp >> 4) & 0x0F;
                comp_v[i] = samp & 0x0F;
                if (comp_h[i] < 1 || comp_h[i] > 4 ||
                    comp_v[i] < 1 || comp_v[i] > 4) goto fail;
                /* ITU T.81 A.2.2 / 4.8.2: a one-component scan is
                 * non-interleaved; the MCU is one 8x8 data unit regardless of
                 * the nominal H/V in SOF. Honoring 2x2 here would expect four
                 * blocks per 16x16 MCU (and 4x4 would trip the interleaved
                 * 10-block cap) and reject valid grayscale streams. */
                if (ncomp == 1) {
                    comp_h[i] = 1;
                    comp_v[i] = 1;
                }
                blocks_per_mcu += comp_h[i] * comp_v[i];
                comp_quant[i] = br_read_byte_raw(&br);
                if (comp_quant[i] >= 4) goto fail;
            }
            if (blocks_per_mcu > 10) goto fail;
        } else if (marker == 0xC4) { /* DHT */
            while (br.pos < seg_end) {
                if (seg_end - br.pos < 17) goto fail;
                uint8_t info = br_read_byte_raw(&br);
                if ((info & 0xE0) != 0) goto fail;
                int cls = (info >> 4) & 1;
                int table_id = info & 0x0F;
                if (table_id >= 4) goto fail;
                uint8_t bits[17] = {0};
                int total = 0;
                for (int i = 1; i <= 16; i++) {
                    bits[i] = br_read_byte_raw(&br);
                    total += bits[i];
                }
                if (total <= 0 || total > 256 ||
                    (size_t)total > seg_end - br.pos)
                    goto fail;
                uint8_t *vals = (uint8_t *)malloc(total ? (size_t)total : 1);
                if (!vals) goto fail;
                for (int i = 0; i < total; i++)
                    vals[i] = br_read_byte_raw(&br);
                huff_decode_table_t *tbl = cls ? &ac_tables[table_id] : &dc_tables[table_id];
                if (tbl->vals) { free(tbl->vals); tbl->vals = NULL; }
                int bdt = build_decode_table(tbl, bits, vals);
                free(vals);
                if (bdt != 0) goto fail;   /* reject malformed Huffman table */
            }
        } else if (marker == 0xDA) { /* SOS */
            if (br.pos >= seg_end) goto fail;
            int ns = br_read_byte_raw(&br);
            if (ncomp == 0 || ns != ncomp ||
                seg_end - br.pos != (size_t)ns * 2U + 3U)
                goto fail;
            int seen[4] = {0};
            for (int i = 0; i < ns; i++) {
                int selector = br_read_byte_raw(&br);
                uint8_t td_ta = br_read_byte_raw(&br);
                int component = -1;
                for (int c = 0; c < ncomp; c++)
                    if (comp_id[c] == selector) component = c;
                if (component < 0 || seen[component]) goto fail;
                seen[component] = 1;
                comp_dc_table[component] = (td_ta >> 4) & 0x0F;
                comp_ac_table[component] = td_ta & 0x0F;
                if (comp_dc_table[component] >= 4 ||
                    comp_ac_table[component] >= 4)
                    goto fail;
            }
            int spectral_start = br_read_byte_raw(&br);
            int spectral_end = br_read_byte_raw(&br);
            int approximation = br_read_byte_raw(&br);
            if (spectral_start != 0 || spectral_end != 63 ||
                approximation != 0)
                goto fail;
            for (int c = 0; c < ncomp; c++) {
                if (!quant_present[comp_quant[c]] ||
                    !dc_tables[comp_dc_table[c]].vals ||
                    !ac_tables[comp_ac_table[c]].vals)
                    goto fail;
            }
            scan_found = 1;
            break;
        } else if (marker == 0xDD) { /* DRI — define restart interval */
            if (seg_end - br.pos != 2) goto fail;
            restart_interval = br_read_u16(&br);
        } else {
            br.pos = seg_end;
        }
        if (br.failed || br.pos != seg_end) goto fail;
    }

    if (!scan_found || width == 0 || height == 0) goto fail;
    /* Reject implausibly large geometry before allocating. A 16-bit SOF can
     * claim up to 65535x65535 (~4 gigapixels); honoring that from a tiny
     * malformed header would (a) overflow the size_t buffer/plane arithmetic on
     * 32-bit targets — a heap overflow — and (b) force multi-GB allocations plus
     * a multi-minute grind (DoS). 2^28 px (e.g. 16384x16384) sits far above any
     * real photo yet keeps every allocation < 4 GiB even on 32-bit. The product
     * is done in 64-bit so the check itself never overflows. (libjpeg/stb-image
     * apply equivalent caps.) */
    if ((uint64_t)width * (uint64_t)height > (1u << 28)) goto fail;

    img->width = width;
    img->height = height;
    img->channels = (ncomp == 1) ? 1 : 3;
    img->stride = (size_t)width * img->channels;
    img->pixels = (uint8_t *)calloc(1, img->stride * height);
    if (!img->pixels) goto fail;

    /* Sampling geometry. Hmax/Vmax define the MCU footprint (8*Hmax x 8*Vmax
     * pixels); each component contributes comp_h[c]*comp_v[c] blocks per MCU and
     * is decoded into its own (MCU-aligned) plane, then box-upsampled to full
     * resolution. This is what lets the decoder read the 4:2:0 / 4:2:2 chroma
     * subsampling that virtually every real-world JPEG uses (cameras, browsers,
     * libjpeg's default); the previous decoder assumed one block per component
     * per 8x8 MCU and so produced garbage on any non-4:4:4 file. */
    int Hmax = 1, Vmax = 1;
    for (int c = 0; c < ncomp; c++) {
        if (comp_h[c] > Hmax) Hmax = comp_h[c];
        if (comp_v[c] > Vmax) Vmax = comp_v[c];
    }
    uint32_t mcu_pw = (uint32_t)Hmax * 8, mcu_ph = (uint32_t)Vmax * 8;
    uint32_t mcus_x = (width + mcu_pw - 1) / mcu_pw;
    uint32_t mcus_y = (height + mcu_ph - 1) / mcu_ph;
    int prev_dc[4] = {0};

    /* Per-table float dequant multipliers, AAN-prescaled by the row*col factor
     * the fast inverse DCT expects. Built once so the MCU loop's dequantize
     * stays a single multiply (the scale rides along for free), the decode-side
     * mirror of the encoder folding aanscale into its quantization divisor. */
    float fquant[4][64];
    for (int t = 0; t < 4; t++)
        for (int i = 0; i < 64; i++) {
            int nat = jpeg_natural_order[i];   /* DQT is in scan order */
            fquant[t][i] = (float)((double)quant_tables[t][i]
                         * aanscale[nat >> 3] * aanscale[nat & 7]);
        }

    /* YCbCr->RGB integer LUTs (libjpeg jdcolor): turn 6 float multiplies per
     * pixel into table lookups + adds. Indexed by the raw 0..255 sample, with
     * the -128 level shift and 16-bit fixed-point scaling baked in. */
    int ycc_cr_r[256], ycc_cb_b[256], ycc_cr_g[256], ycc_cb_g[256];
    for (int i = 0; i < 256; i++) {
        int x = i - 128;
        ycc_cr_r[i] = (91881 * x + 32768) >> 16;     /*  1.40200 * Cr */
        ycc_cb_b[i] = (116130 * x + 32768) >> 16;    /*  1.77200 * Cb */
        ycc_cr_g[i] = -46802 * x;                    /* -0.71414 * Cr (scaled) */
        ycc_cb_g[i] = -22554 * x + 32768;            /* -0.34414 * Cb (+rounding) */
    }

    br.bit_buf = 0;
    br.bit_cnt = 0;

    /* Per-component sample planes at each component's own (MCU-aligned)
     * resolution; chroma planes are smaller when subsampled. */
    uint8_t *plane[3] = {0};
    uint32_t plane_w[3] = {0}, plane_h[3] = {0};
    for (int c = 0; c < ncomp; c++) {
        plane_w[c] = mcus_x * (uint32_t)comp_h[c] * 8;
        plane_h[c] = mcus_y * (uint32_t)comp_v[c] * 8;
        if (plane_w[c] == 0 || plane_h[c] > SIZE_MAX / plane_w[c]) {
            for (int k = 0; k < c; k++) free(plane[k]);
            goto fail;
        }
        uint64_t plane_bytes = (uint64_t)plane_w[c] * (uint64_t)plane_h[c];
        if (plane_bytes > (UINT64_C(1) << 30)) {
            for (int k = 0; k < c; k++) free(plane[k]);
            goto fail;
        }
        plane[c] = (uint8_t *)malloc((size_t)plane_bytes);
        if (!plane[c]) { for (int k = 0; k < c; k++) free(plane[k]); goto fail; }
    }

    uint32_t mcu_idx = 0;
    for (uint32_t my = 0; my < mcus_y; my++) {
        for (uint32_t mx = 0; mx < mcus_x; mx++) {
            /* At each restart interval the encoder byte-aligned the bitstream
             * and emitted an RST marker, resetting the DC predictors. Mirror it
             * so cameras' / libjpeg's restart-enabled JPEGs decode correctly. */
            if (restart_interval && mcu_idx && mcu_idx % restart_interval == 0) {
                uint8_t expected_restart = (uint8_t)(
                    0xD0U + ((mcu_idx / restart_interval - 1U) & 7U));
                if (br_restart(&br, expected_restart) != 0)
                    goto decode_fail;
                prev_dc[0] = prev_dc[1] = prev_dc[2] = prev_dc[3] = 0;
            }
            mcu_idx++;
            for (int c = 0; c < ncomp; c++) {
                int qt = comp_quant[c];
                if (qt >= 4) qt = 0;
                /* Blocks within an MCU are interleaved component-by-component,
                 * each component's comp_v[c] x comp_h[c] blocks in raster order. */
                for (int sby = 0; sby < comp_v[c]; sby++) {
                    for (int sbx = 0; sbx < comp_h[c]; sbx++) {
                        int quantized[64] = {0};
                        /* DC (differential within the component) */
                        int dc_sym = huff_decode(&br, &dc_tables[comp_dc_table[c]]);
                        if (dc_sym < 0 || dc_sym > 11)
                            goto decode_fail;
                        int dc_delta;
                        if (decode_value(&br, dc_sym, &dc_delta) != 0)
                            goto decode_fail;
                        int next_dc = prev_dc[c] + dc_delta;
                        if (next_dc < -2048 || next_dc > 2047)
                            goto decode_fail;
                        prev_dc[c] = next_dc;
                        quantized[0] = prev_dc[c];

                        /* AC */
                        int idx = 1;
                        while (idx < 64) {
                            int ac_sym = huff_decode(&br, &ac_tables[comp_ac_table[c]]);
                            if (ac_sym < 0) goto decode_fail;
                            if (ac_sym == 0x00) break;            /* EOB */
                            if (ac_sym == 0xF0) {
                                idx += 16;
                                if (idx > 64) goto decode_fail;
                                continue;
                            }
                            int run = (ac_sym >> 4) & 0x0F;
                            int size = ac_sym & 0x0F;
                            idx += run;
                            if (size < 1 || size > 10 || idx >= 64)
                                goto decode_fail;
                            if (decode_value(
                                    &br, size, &quantized[idx]) != 0)
                                goto decode_fail;
                            idx++;
                        }

                        /* Dequantize into AAN-scaled floats, then IDCT. The
                         * coefficients are in zigzag scan order; scatter each to
                         * its natural row-major slot for the IDCT. */
                        float dequant[64];
                        for (int i = 0; i < 64; i++)
                            dequant[jpeg_natural_order[i]] = (float)quantized[i] * fquant[qt][i];
                        int blk[64];
                        idct_block(dequant, blk);

                        /* Scatter the 8x8 block into the component plane. */
                        uint32_t ox = (mx * (uint32_t)comp_h[c] + (uint32_t)sbx) * 8;
                        uint32_t oy = (my * (uint32_t)comp_v[c] + (uint32_t)sby) * 8;
                        for (int yy = 0; yy < 8; yy++) {
                            uint8_t *prow = plane[c] + (size_t)(oy + yy) * plane_w[c] + ox;
                            for (int xx = 0; xx < 8; xx++) {
                                int v = blk[yy * 8 + xx];
                                prow[xx] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
                            }
                        }
                    }
                }
            }
        }
    }

    /* A complete baseline scan must end on a byte boundary followed by EOI.
     * At most seven fill bits may remain in the bit buffer. Accept either fill
     * bit value for compatibility, but never silently accept a truncated scan
     * or unconsumed entropy bytes. */
    if (br.failed || br.bit_cnt < 0 || br.bit_cnt > 7)
        goto decode_fail;
    size_t marker_pos = br.pos;
    while (marker_pos < br.len && br.data[marker_pos] == 0xFF)
        marker_pos++;
    if (marker_pos >= br.len || br.data[marker_pos] != 0xD9)
        goto decode_fail;
    if (marker_pos + 1 != br.len)
        goto decode_fail;

    /* Compose the output: box-upsample each component to full resolution (sample
     * c at px*comp_h[c]/Hmax, py*comp_v[c]/Vmax), then YCbCr->RGB. The row index
     * is hoisted out of the inner loop. */
    for (uint32_t py = 0; py < height; py++) {
        if (ncomp == 1) {
            memcpy(img->pixels + (size_t)py * img->stride,
                   plane[0] + (size_t)py * plane_w[0], width);
            continue;
        }
        const uint8_t *yrow  = plane[0] + (size_t)(py * (uint32_t)comp_v[0] / (uint32_t)Vmax) * plane_w[0];
        const uint8_t *cbrow = plane[1] + (size_t)(py * (uint32_t)comp_v[1] / (uint32_t)Vmax) * plane_w[1];
        const uint8_t *crrow = plane[2] + (size_t)(py * (uint32_t)comp_v[2] / (uint32_t)Vmax) * plane_w[2];
        uint8_t *out = img->pixels + (size_t)py * img->stride;
        for (uint32_t px = 0; px < width; px++) {
            int Y  = yrow [px * (uint32_t)comp_h[0] / (uint32_t)Hmax];
            int Cb = cbrow[px * (uint32_t)comp_h[1] / (uint32_t)Hmax];
            int Cr = crrow[px * (uint32_t)comp_h[2] / (uint32_t)Hmax];
            int r = Y + ycc_cr_r[Cr];
            int g = Y + ((ycc_cb_g[Cb] + ycc_cr_g[Cr]) >> 16);
            int b = Y + ycc_cb_b[Cb];
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            out[px * 3 + 0] = (uint8_t)r;
            out[px * 3 + 1] = (uint8_t)g;
            out[px * 3 + 2] = (uint8_t)b;
        }
    }

    for (int c = 0; c < ncomp; c++) free(plane[c]);
    for (int i = 0; i < 4; i++) { free(dc_tables[i].vals); free(ac_tables[i].vals); }
    return 0;

decode_fail:
    for (int c = 0; c < ncomp; c++) free(plane[c]);
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
