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
    if (!dst_len || (!src && src_len != 0) ||
        (!dst && *dst_len != 0)) return -1;
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

    if (src[0] >= clear_code) return -1;
    uint32_t code = (uint32_t)src[0];
    for (size_t i = 1; i < src_len; i++) {
        uint32_t literal = (uint32_t)src[i];
        if (literal >= clear_code) return -1;
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
    if (hi == overflow) width++;
    if (bw_write_code(&bw, eof_code, width) < 0) return -1;
    if (bw_flush(&bw) < 0) return -1;

    *dst_len = bw.pos;
    return 0;
}

/* ---- LZW Decompress ---- */

int neverc_lzw_decompress(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t *dst_len,
                          int order, int lit_width) {
    if (!dst_len || (!src && src_len != 0) ||
        (!dst && *dst_len != 0)) return -1;
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

    /*
     * Decode tables. A non-literal code expands to a byte string by walking its
     * prefix chain (prefix[c] is always < c, so the walk strictly decreases and
     * can never loop). Recording length[code] lets us reserve the exact output
     * span and emit the string by writing it *backwards directly into dst* — the
     * scheme Go's compress/lzw uses — instead of the textbook
     * push-onto-a-stack-then-reverse-copy, which touches every emitted byte ~3x
     * (stack write + stack read + dst write) versus one dst write here. Since
     * length[code] equals the chain length exactly (length[c] = length[prefix]+1
     * by construction), the backward cursor lands precisely on out_pos, so the
     * single up-front bound check is sufficient and writes stay in range even on
     * corrupt input. The first byte of the previous code's string — needed for
     * the new dictionary entry and the KwKwK self-referential case — is the only
     * first-byte ever read, so it is carried in a scalar instead of a table.
     */
    uint8_t  suffix[1 << MAX_WIDTH];   /* last byte of each code's string */
    uint16_t prefix[1 << MAX_WIDTH];   /* parent code (strictly smaller)  */
    uint16_t length[1 << MAX_WIDTH];   /* byte length of each code's string */

    for (uint16_t c = 0; c < clear_code; c++) length[c] = 1;   /* literals */
    uint8_t last_first = 0;

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

        /* Emit S(code) into dst and capture its first byte (cur_first). */
        uint8_t cur_first;
        if (code < clear_code) {
            /* literal */
            if (out_pos >= out_cap) return -1;
            dst[out_pos++] = (uint8_t)code;
            cur_first = (uint8_t)code;
        } else if (code < hi || (code == hi && last == INVALID_CODE16)) {
            /* Already-defined entry: write the chain backwards into dst.
             * `code == hi` is reachable here only in the frozen-dictionary
             * state (table full at MAX_WIDTH, `last` reset to INVALID): the
             * top entry `hi` was defined just before the freeze, so its chain
             * is valid. This mirrors the textbook `code <= hi` acceptance and
             * is required to decode TIFF/GIF "deferred clear" streams that keep
             * using the highest code instead of emitting a clear. */
            size_t L = length[code];
            if (L > out_cap - out_pos) return -1;
            size_t w = out_pos + L;
            uint16_t c = code;
            while (c >= clear_code) { dst[--w] = suffix[c]; c = prefix[c]; }
            dst[--w] = (uint8_t)c;                 /* w now == out_pos */
            cur_first = (uint8_t)c;
            out_pos += L;
        } else if (code == hi && last != INVALID_CODE16) {
            /* KwKwK: S(code) = S(last) + firstByte(S(last)) */
            size_t L = (size_t)length[last] + 1;
            if (L > out_cap - out_pos) return -1;
            size_t w = out_pos + L;
            dst[--w] = last_first;                 /* the self-referential byte */
            uint16_t c = last;
            while (c >= clear_code) { dst[--w] = suffix[c]; c = prefix[c]; }
            dst[--w] = (uint8_t)c;                 /* w now == out_pos */
            cur_first = (uint8_t)c;
            out_pos += L;
        } else {
            return -1;                             /* code > hi, or hi w/o last */
        }

        /* Define the new entry S(last)+cur_first as code `hi`. */
        if (last != INVALID_CODE16) {
            prefix[hi] = last;
            suffix[hi] = cur_first;
            length[hi] = (uint16_t)(length[last] + 1);
        }

        last = code;
        last_first = cur_first;
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
