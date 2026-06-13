#include "neverc/std/image/gif.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * GIF Encoder/Decoder
 * Implements GIF87a/89a with LZW compression
 * ========================================================================= */

/* LZW compression for GIF */
typedef struct {
    uint8_t *buf;
    size_t cap, pos;
    uint32_t bit_buf;
    int bit_cnt;
} gif_writer_t;

static void gw_init(gif_writer_t *w, size_t cap) {
    w->buf = (uint8_t *)malloc(cap);
    w->cap = cap; w->pos = 0;
    w->bit_buf = 0; w->bit_cnt = 0;
}

static void gw_ensure(gif_writer_t *w, size_t n) {
    if (w->pos + n > w->cap) {
        w->cap = (w->pos + n) * 2;
        w->buf = (uint8_t *)realloc(w->buf, w->cap);
    }
}

static void gw_byte(gif_writer_t *w, uint8_t b) {
    gw_ensure(w, 1);
    w->buf[w->pos++] = b;
}

static void gw_bytes(gif_writer_t *w, const uint8_t *d, size_t n) {
    gw_ensure(w, n);
    memcpy(w->buf + w->pos, d, n);
    w->pos += n;
}

static void gw_u16le(gif_writer_t *w, uint16_t v) {
    gw_byte(w, (uint8_t)(v & 0xFF));
    gw_byte(w, (uint8_t)(v >> 8));
}

/* GIF LZW encoder */
#define LZW_MAX_CODE 4096

typedef struct {
    int prefix;
    int suffix;
    int child;  /* first child string (this entry + one more byte) */
    int next;   /* next sibling sharing the same prefix */
} lzw_entry_t;

static void gif_lzw_compress(gif_writer_t *out, const uint8_t *data, size_t len,
                             int min_code_size) {
    int clear_code = 1 << min_code_size;
    int eoi_code = clear_code + 1;
    int next_code = eoi_code + 1;
    int code_size = min_code_size + 1;

    lzw_entry_t *table = (lzw_entry_t *)calloc(LZW_MAX_CODE, sizeof(lzw_entry_t));
    for (int i = 0; i < LZW_MAX_CODE; i++) {
        table[i].prefix = -1;
        table[i].suffix = -1;
        table[i].child = -1;
        table[i].next = -1;
    }

    /* Sub-block buffer */
    uint8_t sub_block[256];
    int sub_pos = 0;
    uint32_t bit_buf = 0;
    int bit_cnt = 0;

    #define EMIT_CODE(code) do { \
        bit_buf |= ((uint32_t)(code)) << bit_cnt; \
        bit_cnt += code_size; \
        while (bit_cnt >= 8) { \
            sub_block[sub_pos++] = (uint8_t)(bit_buf & 0xFF); \
            bit_buf >>= 8; \
            bit_cnt -= 8; \
            if (sub_pos == 255) { \
                gw_byte(out, (uint8_t)sub_pos); \
                gw_bytes(out, sub_block, (size_t)sub_pos); \
                sub_pos = 0; \
            } \
        } \
    } while(0)

    EMIT_CODE(clear_code);

    if (len == 0) {
        EMIT_CODE(eoi_code);
        goto flush;
    }

    int current = data[0];
    for (size_t i = 1; i < len; i++) {
        int pixel = data[i];

        /* Find child (current + pixel) by walking current's child/sibling list.
         * The list previously overloaded a single `next` field for both the
         * first-child link and the sibling link, so adding a child to a node
         * clobbered that node's sibling pointer — truncating the parent's child
         * list, defeating string reuse and emitting a corrupt code stream for
         * any image whose dictionary actually grew. Separate child/next fix it. */
        int found = -1;
        for (int e = table[current].child; e != -1; e = table[e].next) {
            if (table[e].suffix == pixel) { found = e; break; }
        }

        if (found >= 0) {
            current = found;
        } else {
            EMIT_CODE(current);

            if (next_code < LZW_MAX_CODE) {
                table[next_code].prefix = current;
                table[next_code].suffix = pixel;
                table[next_code].child = -1;
                table[next_code].next = table[current].child;
                table[current].child = next_code;
                next_code++;
            }

            if (next_code > (1 << code_size) && code_size < 12)
                code_size++;

            if (next_code >= LZW_MAX_CODE - 1) {
                EMIT_CODE(clear_code);
                for (int j = 0; j < LZW_MAX_CODE; j++) {
                    table[j].prefix = -1;
                    table[j].suffix = -1;
                    table[j].child = -1;
                    table[j].next = -1;
                }
                next_code = eoi_code + 1;
                code_size = min_code_size + 1;
            }

            current = pixel;
        }
    }

    EMIT_CODE(current);
    EMIT_CODE(eoi_code);

flush:
    if (bit_cnt > 0) {
        sub_block[sub_pos++] = (uint8_t)(bit_buf & 0xFF);
    }
    if (sub_pos > 0) {
        gw_byte(out, (uint8_t)sub_pos);
        gw_bytes(out, sub_block, (size_t)sub_pos);
    }
    gw_byte(out, 0); /* block terminator */

    #undef EMIT_CODE
    free(table);
}

int neverc_gif_encode(const neverc_gif_frame_t *frame,
                      uint8_t **out_data, size_t *out_len) {
    if (!frame || !frame->indices || !out_data || !out_len) return -1;
    if (frame->width == 0 || frame->height == 0) return -1;
    if (frame->palette_size < 2 || frame->palette_size > 256) return -1;

    int color_bits = 1;
    while ((1 << color_bits) < frame->palette_size) color_bits++;
    int palette_entries = 1 << color_bits;

    gif_writer_t w;
    gw_init(&w, frame->width * frame->height + 1024);

    /* Header */
    gw_bytes(&w, (const uint8_t *)"GIF89a", 6);

    /* Logical Screen Descriptor */
    gw_u16le(&w, (uint16_t)frame->width);
    gw_u16le(&w, (uint16_t)frame->height);
    uint8_t packed = (uint8_t)(0x80 | ((color_bits - 1) << 4) | (color_bits - 1));
    gw_byte(&w, packed);
    gw_byte(&w, 0); /* background color index */
    gw_byte(&w, 0); /* pixel aspect ratio */

    /* Global Color Table */
    for (int i = 0; i < palette_entries; i++) {
        if (i < frame->palette_size) {
            gw_byte(&w, frame->palette[i].r);
            gw_byte(&w, frame->palette[i].g);
            gw_byte(&w, frame->palette[i].b);
        } else {
            gw_byte(&w, 0); gw_byte(&w, 0); gw_byte(&w, 0);
        }
    }

    /* Graphic Control Extension (for transparency) */
    if (frame->has_transparency || frame->delay_centiseconds > 0) {
        gw_byte(&w, 0x21); /* extension introducer */
        gw_byte(&w, 0xF9); /* graphic control */
        gw_byte(&w, 4);    /* block size */
        uint8_t gce_packed = frame->has_transparency ? 0x01 : 0x00;
        gw_byte(&w, gce_packed);
        gw_u16le(&w, (uint16_t)frame->delay_centiseconds);
        gw_byte(&w, frame->has_transparency ? frame->transparent_index : 0);
        gw_byte(&w, 0); /* block terminator */
    }

    /* Image Descriptor */
    gw_byte(&w, 0x2C);
    gw_u16le(&w, 0); /* left */
    gw_u16le(&w, 0); /* top */
    gw_u16le(&w, (uint16_t)frame->width);
    gw_u16le(&w, (uint16_t)frame->height);
    gw_byte(&w, 0); /* packed (no local color table) */

    /* LZW Minimum Code Size */
    int min_code_size = color_bits;
    if (min_code_size < 2) min_code_size = 2;
    gw_byte(&w, (uint8_t)min_code_size);

    /* LZW compressed data */
    gif_lzw_compress(&w, frame->indices, (size_t)frame->width * frame->height, min_code_size);

    /* Trailer */
    gw_byte(&w, 0x3B);

    *out_data = w.buf;
    *out_len = w.pos;
    return 0;
}

/* GIF LZW decoder */
int neverc_gif_decode(const uint8_t *data, size_t len, neverc_gif_image_t *img) {
    if (!data || len < 13 || !img) return -1;
    memset(img, 0, sizeof(*img));

    if (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)
        return -1;

    size_t pos = 6;
    img->width = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8); pos += 2;
    img->height = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8); pos += 2;
    uint8_t packed = data[pos++];
    uint8_t bg_index = data[pos++];
    pos++; /* pixel aspect ratio */

    int has_gct = (packed >> 7) & 1;
    int gct_size = has_gct ? (1 << ((packed & 7) + 1)) : 0;

    neverc_gif_color_t gct[256];
    memset(gct, 0, sizeof(gct));
    if (has_gct) {
        for (int i = 0; i < gct_size && pos + 2 < len; i++) {
            gct[i].r = data[pos++];
            gct[i].g = data[pos++];
            gct[i].b = data[pos++];
        }
    }
    if (bg_index < gct_size)
        img->background = gct[bg_index];

    /* Parse blocks */
    int frame_cap = 4;
    img->frames = (neverc_gif_frame_t *)calloc((size_t)frame_cap, sizeof(neverc_gif_frame_t));
    img->num_frames = 0;

    int pending_delay = 0;
    int pending_transparent = -1;

    while (pos < len) {
        uint8_t block = data[pos++];

        if (block == 0x3B) break; /* Trailer */

        if (block == 0x21) { /* Extension */
            if (pos >= len) break;
            uint8_t label = data[pos++];
            if (label == 0xF9 && pos + 5 <= len) { /* Graphic Control Extension */
                pos++; /* block size */
                uint8_t gce_packed = data[pos++];
                pending_delay = (int)data[pos] | ((int)data[pos+1] << 8); pos += 2;
                uint8_t trans_idx = data[pos++];
                if (gce_packed & 1) pending_transparent = trans_idx;
                pos++; /* block terminator */
            } else {
                while (pos < len) {
                    uint8_t bs = data[pos++];
                    if (bs == 0) break;
                    pos += bs;
                }
            }
            continue;
        }

        if (block == 0x2C) { /* Image Descriptor */
            if (pos + 8 >= len) break;
            uint16_t left = (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8); pos += 2;
            uint16_t top  = (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8); pos += 2;
            uint16_t fw   = (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8); pos += 2;
            uint16_t fh   = (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8); pos += 2;
            uint8_t img_packed = data[pos++];
            (void)left; (void)top;

            int has_lct = (img_packed >> 7) & 1;
            int lct_size = has_lct ? (1 << ((img_packed & 7) + 1)) : 0;

            neverc_gif_color_t *palette = gct;
            int pal_size = gct_size;
            neverc_gif_color_t lct[256];
            if (has_lct) {
                memset(lct, 0, sizeof(lct));
                for (int i = 0; i < lct_size && pos + 2 < len; i++) {
                    lct[i].r = data[pos++];
                    lct[i].g = data[pos++];
                    lct[i].b = data[pos++];
                }
                palette = lct;
                pal_size = lct_size;
            }

            /* LZW decode */
            if (pos >= len) break;
            int min_code_size = data[pos++];
            if (min_code_size < 2 || min_code_size > 11) break;

            /* Collect sub-blocks */
            uint8_t *lzw_data = NULL;
            size_t lzw_len = 0, lzw_cap = 0;
            while (pos < len) {
                uint8_t bs = data[pos++];
                if (bs == 0) break;
                if (pos + bs > len) break;
                if (lzw_len + bs > lzw_cap) {
                    lzw_cap = (lzw_len + bs) * 2;
                    lzw_data = (uint8_t *)realloc(lzw_data, lzw_cap);
                }
                memcpy(lzw_data + lzw_len, data + pos, bs);
                lzw_len += bs;
                pos += bs;
            }

            /* LZW decompress */
            int clear_code = 1 << min_code_size;
            int eoi_code = clear_code + 1;
            int code_size = min_code_size + 1;

            size_t pixel_count = (size_t)fw * fh;
            uint8_t *indices = (uint8_t *)calloc(1, pixel_count);
            size_t pix_pos = 0;

            /* LZW dictionary as prefix/suffix chains. The previous version
             * stored each entry's fully expanded byte string and malloc'd +
             * memcpy'd a growing copy for every new code — O(sum of entry
             * lengths) time and memory. Prefix-chain entries are fixed-size
             * (one prefix index + one suffix byte) with zero per-entry
             * allocation; output is reconstructed through a small stack. */
            #define LZW_TABLE_SIZE 4096
            uint16_t prefix[LZW_TABLE_SIZE];
            uint8_t  suffix[LZW_TABLE_SIZE];
            uint8_t  estack[LZW_TABLE_SIZE];
            int next_code_d = eoi_code + 1;

            for (int i = 0; i < clear_code; i++) {
                prefix[i] = 0;
                suffix[i] = (uint8_t)i;
            }

            uint32_t bit_buf = 0;
            int bit_cnt = 0;
            size_t byte_pos = 0;

            #define READ_CODE() ({ \
                while (bit_cnt < code_size && byte_pos < lzw_len) { \
                    bit_buf |= ((uint32_t)lzw_data[byte_pos++]) << bit_cnt; \
                    bit_cnt += 8; \
                } \
                int _c = (int)(bit_buf & ((1u << code_size) - 1)); \
                bit_buf >>= code_size; \
                bit_cnt -= code_size; \
                _c; \
            })

            int prev_code = -1;
            uint8_t first_byte = 0;
            int done = 0;
            while (!done && byte_pos < lzw_len) {
                int code = READ_CODE();

                if (code == eoi_code) { done = 1; break; }
                if (code == clear_code) {
                    next_code_d = eoi_code + 1;
                    code_size = min_code_size + 1;
                    prev_code = -1;
                    continue;
                }

                /* Reconstruct `code` onto estack (reversed); the root literal is
                 * the string's first byte. code == next_code_d is the KwKwK case
                 * (entry not yet defined) = prev string + prev's first byte. */
                int sp = 0;
                int c;
                if (code < next_code_d) {
                    c = code;
                } else if (code == next_code_d && prev_code >= 0) {
                    estack[sp++] = first_byte;
                    c = prev_code;
                } else {
                    break;
                }

                while (c >= clear_code) {
                    if (c >= LZW_TABLE_SIZE || sp >= LZW_TABLE_SIZE) { done = 1; break; }
                    estack[sp++] = suffix[c];
                    c = prefix[c];
                }
                if (done) break;
                estack[sp++] = (uint8_t)c;
                first_byte = (uint8_t)c;

                for (int i = sp - 1; i >= 0 && pix_pos < pixel_count; i--) {
                    uint8_t idx_val = estack[i];
                    if (idx_val >= pal_size) idx_val = 0;
                    indices[pix_pos++] = idx_val;
                }

                /* New dictionary entry = prev string followed by this string's
                 * first byte. */
                if (prev_code >= 0 && next_code_d < LZW_TABLE_SIZE) {
                    prefix[next_code_d] = (uint16_t)prev_code;
                    suffix[next_code_d] = first_byte;
                    next_code_d++;
                    /* The decoder's dictionary trails the encoder's by one
                     * entry, so it must widen the code one step earlier than the
                     * encoder's `next_code > 2^code_size`: use `>=` so the read
                     * width matches the write width (classic GIF early change).
                     * The old `>` desynced as soon as the table grew, corrupting
                     * every non-trivial / third-party GIF. */
                    if (next_code_d >= (1 << code_size) && code_size < 12) code_size++;
                }

                prev_code = code;
            }

            #undef READ_CODE

            free(lzw_data);

            /* Store frame */
            if (img->num_frames >= frame_cap) {
                frame_cap *= 2;
                img->frames = (neverc_gif_frame_t *)realloc(img->frames,
                    (size_t)frame_cap * sizeof(neverc_gif_frame_t));
            }
            neverc_gif_frame_t *f = &img->frames[img->num_frames++];
            memset(f, 0, sizeof(*f));
            f->width = fw;
            f->height = fh;
            f->indices = indices;
            f->palette_size = pal_size;
            memcpy(f->palette, palette, sizeof(neverc_gif_color_t) * (size_t)(pal_size < 256 ? pal_size : 256));
            f->delay_centiseconds = pending_delay;
            if (pending_transparent >= 0) {
                f->has_transparency = 1;
                f->transparent_index = (uint8_t)pending_transparent;
            }
            pending_delay = 0;
            pending_transparent = -1;
        }
    }

    if (img->num_frames == 0) {
        free(img->frames);
        img->frames = NULL;
        return -1;
    }

    return 0;
}

void neverc_gif_free(neverc_gif_image_t *img) {
    if (!img) return;
    for (int i = 0; i < img->num_frames; i++)
        free(img->frames[i].indices);
    free(img->frames);
    img->frames = NULL;
    img->num_frames = 0;
}

int neverc_gif_from_rgba(const uint8_t *rgba, uint32_t width, uint32_t height,
                         neverc_gif_frame_t *frame) {
    if (!rgba || !frame || width == 0 || height == 0) return -1;
    memset(frame, 0, sizeof(*frame));
    frame->width = width;
    frame->height = height;

    /* Simple uniform quantization to 216 web-safe colors (6x6x6 cube) */
    frame->palette_size = 216;
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                int idx = r * 36 + g * 6 + b;
                frame->palette[idx].r = (uint8_t)(r * 51);
                frame->palette[idx].g = (uint8_t)(g * 51);
                frame->palette[idx].b = (uint8_t)(b * 51);
            }
        }
    }

    size_t npixels = (size_t)width * height;
    frame->indices = (uint8_t *)malloc(npixels);

    for (size_t i = 0; i < npixels; i++) {
        int r = rgba[i * 4 + 0];
        int g = rgba[i * 4 + 1];
        int b = rgba[i * 4 + 2];
        int ri = (r + 25) / 51; if (ri > 5) ri = 5;
        int gi = (g + 25) / 51; if (gi > 5) gi = 5;
        int bi = (b + 25) / 51; if (bi > 5) bi = 5;
        frame->indices[i] = (uint8_t)(ri * 36 + gi * 6 + bi);
    }

    return 0;
}
