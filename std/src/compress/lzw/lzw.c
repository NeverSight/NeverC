/*
 * NeverC compress/lzw — LZW compression & decompression.
 * Ported from Go compress/lzw (GIF/TIFF/PDF compatible).
 * Variable-width codes up to 12 bits, with clear and EOF codes.
 */

#include "neverc/std/compress/lzw.h"
#include <string.h>

#define MAX_WIDTH       12
#define MAX_CODE        ((1 << MAX_WIDTH) - 1)
#define INVALID_CODE16  0xFFFF
#define INVALID_CODE32  0xFFFFFFFF
#define TABLE_SIZE      (4 * (1 << MAX_WIDTH))
#define TABLE_MASK      (TABLE_SIZE - 1)
#define FLUSH_BUF       (1 << MAX_WIDTH)

/* ---- Bit writer ---- */

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
    uint32_t bits;
    unsigned nbits;
    int      order;
} bit_writer_t;

static int bw_init(bit_writer_t *w, uint8_t *dst, size_t cap, int order) {
    w->buf = dst;
    w->cap = cap;
    w->pos = 0;
    w->bits = 0;
    w->nbits = 0;
    w->order = order;
    return 0;
}

static int bw_write_byte(bit_writer_t *w, uint8_t b) {
    if (w->pos >= w->cap) return -1;
    w->buf[w->pos++] = b;
    return 0;
}

static int bw_write_code(bit_writer_t *w, uint32_t code, unsigned width) {
    if (w->order == NEVERC_LZW_LSB) {
        w->bits |= code << w->nbits;
        w->nbits += width;
        while (w->nbits >= 8) {
            if (bw_write_byte(w, (uint8_t)w->bits) < 0) return -1;
            w->bits >>= 8;
            w->nbits -= 8;
        }
    } else {
        w->bits |= code << (32 - width - w->nbits);
        w->nbits += width;
        while (w->nbits >= 8) {
            if (bw_write_byte(w, (uint8_t)(w->bits >> 24)) < 0) return -1;
            w->bits <<= 8;
            w->nbits -= 8;
        }
    }
    return 0;
}

static int bw_flush(bit_writer_t *w) {
    if (w->nbits > 0) {
        uint8_t b;
        if (w->order == NEVERC_LZW_MSB)
            b = (uint8_t)(w->bits >> 24);
        else
            b = (uint8_t)w->bits;
        if (bw_write_byte(w, b) < 0) return -1;
        w->nbits = 0;
        w->bits = 0;
    }
    return 0;
}

/* ---- Bit reader ---- */

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         pos;
    uint32_t       bits;
    unsigned       nbits;
    int            order;
} bit_reader_t;

static void br_init(bit_reader_t *r, const uint8_t *src, size_t len, int order) {
    r->buf = src;
    r->len = len;
    r->pos = 0;
    r->bits = 0;
    r->nbits = 0;
    r->order = order;
}

static int br_read_code(bit_reader_t *r, unsigned width, uint16_t *out) {
    if (r->order == NEVERC_LZW_LSB) {
        while (r->nbits < width) {
            if (r->pos >= r->len) return -1;
            r->bits |= (uint32_t)r->buf[r->pos++] << r->nbits;
            r->nbits += 8;
        }
        *out = (uint16_t)(r->bits & ((1u << width) - 1));
        r->bits >>= width;
        r->nbits -= width;
    } else {
        while (r->nbits < width) {
            if (r->pos >= r->len) return -1;
            r->bits |= (uint32_t)r->buf[r->pos++] << (24 - r->nbits);
            r->nbits += 8;
        }
        *out = (uint16_t)(r->bits >> (32 - width));
        r->bits <<= width;
        r->nbits -= width;
    }
    return 0;
}

/* ---- LZW Compress ---- */

int neverc_lzw_compress(const uint8_t *src, size_t src_len,
                        uint8_t *dst, size_t *dst_len,
                        int order, int lit_width) {
    if (lit_width < 2 || lit_width > 8) return -1;
    if (order != NEVERC_LZW_LSB && order != NEVERC_LZW_MSB) return -1;

    bit_writer_t bw;
    bw_init(&bw, dst, *dst_len, order);

    unsigned lw = (unsigned)lit_width;
    uint32_t clear_code = 1u << lw;
    uint32_t eof_code = clear_code + 1;
    unsigned width = lw + 1;
    uint32_t hi = eof_code;
    uint32_t overflow = clear_code << 1;

    uint32_t table[TABLE_SIZE];
    memset(table, 0, sizeof(table));

    if (bw_write_code(&bw, clear_code, width) < 0) return -1;

    if (src_len == 0) {
        if (bw_write_code(&bw, eof_code, width) < 0) return -1;
        if (bw_flush(&bw) < 0) return -1;
        *dst_len = bw.pos;
        return 0;
    }

    uint32_t code = (uint32_t)src[0];
    for (size_t i = 1; i < src_len; i++) {
        uint32_t literal = (uint32_t)src[i];
        uint32_t key = (code << 8) | literal;
        uint32_t hash = ((key >> 12) ^ key) & TABLE_MASK;

        int found = 0;
        for (;;) {
            uint32_t t = table[hash];
            if (t == 0) break;
            if ((t >> 12) == key) {
                code = t & MAX_CODE;
                found = 1;
                break;
            }
            hash = (hash + 1) & TABLE_MASK;
        }
        if (found) continue;

        if (bw_write_code(&bw, code, width) < 0) return -1;
        code = literal;

        hi++;
        if (hi == overflow) {
            width++;
            overflow <<= 1;
        }
        if (hi == (uint32_t)MAX_CODE) {
            if (bw_write_code(&bw, clear_code, width) < 0) return -1;
            width = lw + 1;
            hi = eof_code;
            overflow = clear_code << 1;
            memset(table, 0, sizeof(table));
            continue;
        }
        /* insert key -> hi */
        uint32_t h2 = ((key >> 12) ^ key) & TABLE_MASK;
        while (table[h2] != 0)
            h2 = (h2 + 1) & TABLE_MASK;
        table[h2] = (key << 12) | hi;
    }

    if (bw_write_code(&bw, code, width) < 0) return -1;
    hi++;
    if (hi == overflow) { width++; overflow <<= 1; }
    if (bw_write_code(&bw, eof_code, width) < 0) return -1;
    if (bw_flush(&bw) < 0) return -1;

    *dst_len = bw.pos;
    return 0;
}

/* ---- LZW Decompress ---- */

int neverc_lzw_decompress(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t *dst_len,
                          int order, int lit_width) {
    if (lit_width < 2 || lit_width > 8) return -1;
    if (order != NEVERC_LZW_LSB && order != NEVERC_LZW_MSB) return -1;

    bit_reader_t br;
    br_init(&br, src, src_len, order);

    unsigned lw = (unsigned)lit_width;
    uint16_t clear_code = (uint16_t)(1u << lw);
    uint16_t eof_code = clear_code + 1;
    unsigned width = lw + 1;
    uint16_t hi = eof_code;
    uint16_t overflow_val = (uint16_t)(1u << width);
    uint16_t last = INVALID_CODE16;

    uint8_t  suffix[1 << MAX_WIDTH];
    uint16_t prefix[1 << MAX_WIDTH];
    uint8_t  stack[1 << MAX_WIDTH];

    size_t out_pos = 0;
    size_t out_cap = *dst_len;

    for (;;) {
        uint16_t code;
        if (br_read_code(&br, width, &code) < 0) return -1;

        if (code == eof_code) break;

        if (code == clear_code) {
            width = lw + 1;
            hi = eof_code;
            overflow_val = (uint16_t)(1u << width);
            last = INVALID_CODE16;
            continue;
        }

        if (code < clear_code) {
            /* literal */
            if (out_pos >= out_cap) return -1;
            dst[out_pos++] = (uint8_t)code;
            if (last != INVALID_CODE16) {
                suffix[hi] = (uint8_t)code;
                prefix[hi] = last;
            }
        } else if (code <= hi) {
            /* non-literal: decode suffix chain into stack (reverse order) */
            int sp = 0;
            uint16_t c = code;

            if (code == hi && last != INVALID_CODE16) {
                /* special KwKwK case */
                uint16_t t = last;
                while (t >= clear_code)
                    t = prefix[t];
                stack[sp++] = (uint8_t)t;
                c = last;
            }

            while (c >= clear_code) {
                if (sp >= (1 << MAX_WIDTH)) return -1;
                stack[sp++] = suffix[c];
                c = prefix[c];
            }
            stack[sp++] = (uint8_t)c;

            /* write stack in reverse to output */
            if (out_pos + (size_t)sp > out_cap) return -1;
            for (int j = sp - 1; j >= 0; j--)
                dst[out_pos++] = stack[j];

            if (last != INVALID_CODE16) {
                suffix[hi] = (uint8_t)c;
                prefix[hi] = last;
            }
        } else {
            return -1;
        }

        last = code;
        hi++;
        if (hi >= overflow_val) {
            if (width == MAX_WIDTH) {
                last = INVALID_CODE16;
                hi--;
            } else {
                width++;
                overflow_val = (uint16_t)(1u << width);
            }
        }
    }

    *dst_len = out_pos;
    return 0;
}
