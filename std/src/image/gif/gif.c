#include "neverc/std/image/gif.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define GIF_MAX_PIXELS (UINT64_C(1) << 28)
#define GIF_MAX_FRAMES 1024

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
    int failed;
} gif_writer_t;

static void gw_init(gif_writer_t *w, size_t cap) {
    w->buf = (uint8_t *)malloc(cap);
    w->cap = cap; w->pos = 0;
    w->bit_buf = 0; w->bit_cnt = 0;
    w->failed = w->buf == NULL;
}

static int gw_ensure(gif_writer_t *w, size_t n) {
    if (w->failed) return -1;
    if (n > SIZE_MAX - w->pos) { w->failed = 1; return -1; }
    size_t required = w->pos + n;
    if (required <= w->cap) return 0;
    size_t capacity = w->cap ? w->cap : 256;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) { capacity = required; break; }
        capacity *= 2;
    }
    uint8_t *grown = (uint8_t *)realloc(w->buf, capacity);
    if (!grown) { w->failed = 1; return -1; }
    w->buf = grown;
    w->cap = capacity;
    return 0;
}

static void gw_byte(gif_writer_t *w, uint8_t b) {
    if (gw_ensure(w, 1) != 0) return;
    w->buf[w->pos++] = b;
}

static void gw_bytes(gif_writer_t *w, const uint8_t *d, size_t n) {
    if ((!d && n != 0) || gw_ensure(w, n) != 0) {
        w->failed = 1;
        return;
    }
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

static int gif_lzw_compress(gif_writer_t *out, const uint8_t *data, size_t len,
                            int min_code_size) {
    int clear_code = 1 << min_code_size;
    int eoi_code = clear_code + 1;
    int next_code = eoi_code + 1;
    int code_size = min_code_size + 1;

    lzw_entry_t *table = (lzw_entry_t *)calloc(LZW_MAX_CODE, sizeof(lzw_entry_t));
    if (!table) { out->failed = 1; return -1; }
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
    /* The decoder adds one final dictionary entry after this data code before
       it can know EOI follows. Keep EOI's width in sync at that boundary. */
    if (next_code >= (1 << code_size) && code_size < 12) code_size++;
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
    return out->failed ? -1 : 0;
}

int neverc_gif_encode(const neverc_gif_frame_t *frame,
                      uint8_t **out_data, size_t *out_len) {
    if (!out_data || !out_len) return -1;
    *out_data = NULL;
    *out_len = 0;
    if (!frame || !frame->indices) return -1;
    if (frame->width == 0 || frame->height == 0) return -1;
    uint64_t screen_width = (uint64_t)frame->left + frame->width;
    uint64_t screen_height = (uint64_t)frame->top + frame->height;
    if (screen_width == 0 || screen_width > UINT16_MAX ||
        screen_height == 0 || screen_height > UINT16_MAX)
        return -1;
    if (frame->palette_size < 2 || frame->palette_size > 256) return -1;
    if (frame->delay_centiseconds < 0 ||
        frame->delay_centiseconds > UINT16_MAX ||
        frame->disposal_method < 0 || frame->disposal_method > 3)
        return -1;
    if (frame->has_transparency &&
        (int)frame->transparent_index >= frame->palette_size)
        return -1;
    if ((uint64_t)frame->width * frame->height > GIF_MAX_PIXELS) return -1;
    size_t pixel_count = (size_t)frame->width * frame->height;
    if (pixel_count > SIZE_MAX - 1024) return -1;
    /* Indices outside the palette encode as illegal LZW codes (or decode as
     * a clamped 0). Reject them so a successful encode always round-trips. */
    for (size_t i = 0; i < pixel_count; i++) {
        if ((int)frame->indices[i] >= frame->palette_size)
            return -1;
    }

    int color_bits = 1;
    while ((1 << color_bits) < frame->palette_size) color_bits++;
    int palette_entries = 1 << color_bits;

    gif_writer_t w;
    gw_init(&w, pixel_count + 1024);
    if (w.failed) return -1;

    /* Header */
    gw_bytes(&w, (const uint8_t *)"GIF89a", 6);

    /* Logical Screen Descriptor */
    gw_u16le(&w, (uint16_t)screen_width);
    gw_u16le(&w, (uint16_t)screen_height);
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
    if (frame->has_transparency || frame->delay_centiseconds > 0 ||
        frame->disposal_method != 0) {
        gw_byte(&w, 0x21); /* extension introducer */
        gw_byte(&w, 0xF9); /* graphic control */
        gw_byte(&w, 4);    /* block size */
        uint8_t gce_packed =
            (uint8_t)((frame->disposal_method << 2) |
                      (frame->has_transparency ? 0x01 : 0x00));
        gw_byte(&w, gce_packed);
        gw_u16le(&w, (uint16_t)frame->delay_centiseconds);
        gw_byte(&w, frame->has_transparency ? frame->transparent_index : 0);
        gw_byte(&w, 0); /* block terminator */
    }

    /* Image Descriptor */
    gw_byte(&w, 0x2C);
    gw_u16le(&w, (uint16_t)frame->left);
    gw_u16le(&w, (uint16_t)frame->top);
    gw_u16le(&w, (uint16_t)frame->width);
    gw_u16le(&w, (uint16_t)frame->height);
    gw_byte(&w, 0); /* packed (no local color table) */

    /* LZW Minimum Code Size */
    int min_code_size = color_bits;
    if (min_code_size < 2) min_code_size = 2;
    gw_byte(&w, (uint8_t)min_code_size);

    /* LZW compressed data */
    if (gif_lzw_compress(&w, frame->indices, pixel_count, min_code_size) != 0) {
        free(w.buf);
        return -1;
    }

    /* Trailer */
    gw_byte(&w, 0x3B);

    if (w.failed) { free(w.buf); return -1; }

    *out_data = w.buf;
    *out_len = w.pos;
    return 0;
}

/* GIF LZW decoder */
int neverc_gif_decode(const uint8_t *data, size_t len, neverc_gif_image_t *img) {
    if (!img) return -1;
    memset(img, 0, sizeof(*img));
    if (!data || len < 13) return -1;

    if (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)
        return -1;

    size_t pos = 6;
    uint32_t width = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8); pos += 2;
    uint32_t height = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8); pos += 2;
    uint8_t packed = data[pos++];
    uint8_t bg_index = data[pos++];
    pos++; /* pixel aspect ratio */
    if (width == 0 || height == 0 ||
        (uint64_t)width * height > GIF_MAX_PIXELS)
        return -1;

    int has_gct = (packed >> 7) & 1;
    int gct_size = has_gct ? (1 << ((packed & 7) + 1)) : 0;

    neverc_gif_color_t gct[256];
    memset(gct, 0, sizeof(gct));
    if (has_gct) {
        if ((size_t)gct_size > (len - pos) / 3) return -1;
        for (int i = 0; i < gct_size; i++) {
            gct[i].r = data[pos++];
            gct[i].g = data[pos++];
            gct[i].b = data[pos++];
        }
    }
    neverc_gif_color_t background = {0, 0, 0};
    if (bg_index < gct_size)
        background = gct[bg_index];

    /* Parse blocks */
    int frame_cap = 4;
    img->frames = (neverc_gif_frame_t *)calloc((size_t)frame_cap, sizeof(neverc_gif_frame_t));
    img->num_frames = 0;
    if (!img->frames) return -1;
    uint64_t decoded_pixels = 0;

    int pending_delay = 0;
    int pending_transparent = -1;
    int pending_disposal = 0;
    int saw_trailer = 0;

    while (pos < len) {
        uint8_t block = data[pos++];

        if (block == 0x3B) { saw_trailer = 1; break; } /* Trailer */

        if (block == 0x21) { /* Extension */
            if (pos >= len) goto decode_failed;
            uint8_t label = data[pos++];
            if (label == 0xF9) { /* Graphic Control Extension */
                if (len - pos < 6 || data[pos++] != 4) goto decode_failed;
                uint8_t gce_packed = data[pos++];
                pending_delay = (int)data[pos] | ((int)data[pos+1] << 8); pos += 2;
                uint8_t trans_idx = data[pos++];
                pending_transparent = (gce_packed & 1) ? trans_idx : -1;
                pending_disposal = (gce_packed >> 2) & 7;
                if (data[pos++] != 0) goto decode_failed;
            } else {
                int terminated = 0;
                int app_idx = 0;
                int is_netscape = 0;
                while (pos < len) {
                    uint8_t bs = data[pos++];
                    if (bs == 0) { terminated = 1; break; }
                    if ((size_t)bs > len - pos) goto decode_failed;
                    /* Netscape 2.0 / ANIMEXTS 1.0 loop count lives in the
                     * application extension; the public loop_count field was
                     * never filled, so every animated GIF reported "infinite". */
                    if (label == 0xFF && app_idx == 0 && bs == 11 &&
                        (memcmp(data + pos, "NETSCAPE2.0", 11) == 0 ||
                         memcmp(data + pos, "ANIMEXTS1.0", 11) == 0))
                        is_netscape = 1;
                    else if (is_netscape && app_idx >= 1 && bs >= 3 &&
                             data[pos] == 1)
                        img->loop_count = (int)data[pos + 1] |
                                          ((int)data[pos + 2] << 8);
                    pos += bs;
                    app_idx++;
                }
                if (!terminated) goto decode_failed;
            }
            continue;
        }

        if (block == 0x2C) { /* Image Descriptor */
            if (len - pos < 9) goto decode_failed;
            uint16_t left = (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8); pos += 2;
            uint16_t top  = (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8); pos += 2;
            uint16_t fw   = (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8); pos += 2;
            uint16_t fh   = (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8); pos += 2;
            uint8_t img_packed = data[pos++];
            if ((uint64_t)left + fw > width ||
                (uint64_t)top + fh > height)
                goto decode_failed;

            int has_lct = (img_packed >> 7) & 1;
            int interlaced = (img_packed >> 6) & 1;
            int lct_size = has_lct ? (1 << ((img_packed & 7) + 1)) : 0;

            neverc_gif_color_t *palette = gct;
            int pal_size = gct_size;
            neverc_gif_color_t lct[256];
            if (has_lct) {
                memset(lct, 0, sizeof(lct));
                if ((size_t)lct_size > (len - pos) / 3) goto decode_failed;
                for (int i = 0; i < lct_size; i++) {
                    lct[i].r = data[pos++];
                    lct[i].g = data[pos++];
                    lct[i].b = data[pos++];
                }
                palette = lct;
                pal_size = lct_size;
            }
            if (pending_transparent >= pal_size) goto decode_failed;
            uint64_t frame_pixels = (uint64_t)fw * fh;
            if (fw == 0 || fh == 0 || pal_size < 2 ||
                frame_pixels > GIF_MAX_PIXELS ||
                decoded_pixels > GIF_MAX_PIXELS - frame_pixels ||
                img->num_frames >= GIF_MAX_FRAMES)
                goto decode_failed;
            decoded_pixels += frame_pixels;

            /* LZW decode */
            if (pos >= len) goto decode_failed;
            int min_code_size = data[pos++];
            if (min_code_size < 2 || min_code_size > 8) goto decode_failed;

            /* Collect sub-blocks */
            uint8_t *lzw_data = NULL;
            size_t lzw_len = 0, lzw_cap = 0;
            int sub_blocks_terminated = 0;
            while (pos < len) {
                uint8_t bs = data[pos++];
                if (bs == 0) { sub_blocks_terminated = 1; break; }
                if ((size_t)bs > len - pos || (size_t)bs > SIZE_MAX - lzw_len) {
                    free(lzw_data);
                    goto decode_failed;
                }
                size_t required = lzw_len + bs;
                size_t lzw_limit = (size_t)frame_pixels * 4u;
                if (lzw_limit < 4096u) lzw_limit = 4096u;
                if (lzw_limit > 8u * 1024u * 1024u)
                    lzw_limit = 8u * 1024u * 1024u;
                if (required > lzw_limit) {
                    free(lzw_data);
                    goto decode_failed;
                }
                if (required > lzw_cap) {
                    size_t capacity = lzw_cap ? lzw_cap : 256;
                    while (capacity < required) {
                        if (capacity > SIZE_MAX / 2) { capacity = required; break; }
                        capacity *= 2;
                    }
                    uint8_t *grown = (uint8_t *)realloc(lzw_data, capacity);
                    if (!grown) { free(lzw_data); goto decode_failed; }
                    lzw_data = grown;
                    lzw_cap = capacity;
                }
                memcpy(lzw_data + lzw_len, data + pos, bs);
                lzw_len += bs;
                pos += bs;
            }
            if (!sub_blocks_terminated || lzw_len == 0) {
                free(lzw_data);
                goto decode_failed;
            }

            /* LZW decompress */
            int clear_code = 1 << min_code_size;
            int eoi_code = clear_code + 1;
            int code_size = min_code_size + 1;

            size_t pixel_count = (size_t)frame_pixels;
            uint8_t *indices = (uint8_t *)calloc(1, pixel_count);
            if (!indices) { free(lzw_data); goto decode_failed; }
            size_t pix_pos = 0;

            /* LZW dictionary as prefix/suffix chains. The previous version
             * stored each entry's fully expanded byte string and malloc'd +
             * memcpy'd a growing copy for every new code — O(sum of entry
             * lengths) time and memory. Prefix-chain entries are fixed-size
             * (one prefix index + one suffix byte) with zero per-entry
             * allocation; each code's expansion is then written backwards
             * straight into the pixel buffer (length[] gives the exact span),
             * skipping the stack + reverse-copy the textbook decoder needs. */
            #define LZW_TABLE_SIZE 4096
            uint16_t prefix[LZW_TABLE_SIZE];
            uint8_t  suffix[LZW_TABLE_SIZE];
            uint16_t length[LZW_TABLE_SIZE];   /* code -> expanded byte length */
            int next_code_d = eoi_code + 1;

            for (int i = 0; i < clear_code; i++) {
                prefix[i] = 0;
                suffix[i] = (uint8_t)i;
                length[i] = 1;
            }

            uint32_t bit_buf = 0;
            int bit_cnt = 0;
            size_t byte_pos = 0;

            int prev_code = -1;
            uint8_t first_byte = 0;
            int saw_eoi = 0;
            int lzw_error = 0;
            /*
             * Decode every *complete* code, not just while input bytes remain.
             * The final sub-block byte(s) usually pack several codes — including
             * the last data code and the End-Of-Information marker — so stopping
             * once `byte_pos == lzw_len` discarded whatever was still buffered in
             * bit_buf, truncating the trailing pixel run on tightly-packed (small
             * or just unlucky) images. Refill from the remaining bytes, then stop
             * only when fewer than code_size bits are left to form a code.
             */
            for (;;) {
                while (bit_cnt < code_size && byte_pos < lzw_len) {
                    bit_buf |= ((uint32_t)lzw_data[byte_pos++]) << bit_cnt;
                    bit_cnt += 8;
                }
                if (bit_cnt < code_size) break;        /* no complete code left */
                int code = (int)(bit_buf & ((1u << code_size) - 1));
                bit_buf >>= code_size;
                bit_cnt -= code_size;

                if (code == eoi_code) { saw_eoi = 1; break; }
                if (code == clear_code) {
                    next_code_d = eoi_code + 1;
                    code_size = min_code_size + 1;
                    prev_code = -1;
                    continue;
                }

                /* Emit S(code) backwards directly into indices[], clamping each
                 * byte to the palette and keeping only the leading run that fits
                 * pixel_count — byte-for-byte what the old stack-then-forward-copy
                 * produced. length[code] gives the precise span; code ==
                 * next_code_d is the KwKwK case (entry not yet defined) = prev
                 * string + prev's first byte. prefix[c] < c, so the walk strictly
                 * decreases and is self-bounding. */
                size_t L;
                int walk;
                int is_kwkwk = 0;
                if (code < next_code_d) {
                    L = length[code];
                    walk = code;
                } else if (code == next_code_d && prev_code >= 0) {
                    L = (size_t)length[prev_code] + 1;
                    walk = prev_code;
                    is_kwkwk = 1;
                } else {
                    lzw_error = 1;
                    break;
                }

                size_t avail = pixel_count - pix_pos;
                size_t emit = (L <= avail) ? L : avail;
                size_t w = pix_pos + L;
                if (is_kwkwk) {
                    w--;
                    if (w < pix_pos + emit)
                        indices[w] = (first_byte < pal_size) ? first_byte : 0;
                }
                int c = walk;
                while (c >= clear_code) {
                    w--;
                    if (w < pix_pos + emit) {
                        uint8_t v = suffix[c];
                        indices[w] = (v < pal_size) ? v : 0;
                    }
                    c = prefix[c];
                }
                w--;
                if (w < pix_pos + emit)
                    indices[w] = ((uint8_t)c < pal_size) ? (uint8_t)c : 0;
                first_byte = (uint8_t)c;
                pix_pos += emit;

                /* New dictionary entry = prev string followed by this string's
                 * first byte. */
                if (prev_code >= 0 && next_code_d < LZW_TABLE_SIZE) {
                    prefix[next_code_d] = (uint16_t)prev_code;
                    suffix[next_code_d] = first_byte;
                    length[next_code_d] = (uint16_t)(length[prev_code] + 1);
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

            free(lzw_data);
            if (lzw_error || !saw_eoi || pix_pos != pixel_count) {
                free(indices);
                goto decode_failed;
            }

            /* De-interlace. A GIF marked interlaced stores its rows in four
             * passes (start/step: 0/8, 4/8, 2/4, 1/2); the LZW output above is
             * those rows back-to-back in pass order, so scatter each decoded
             * row to its true position. indices is calloc'd, so any rows left
             * undecoded on truncated input stay zero — same as before. */
            if (interlaced && fw > 0 && fh > 0) {
                uint8_t *deint = (uint8_t *)malloc(pixel_count);
                if (!deint) { free(indices); goto decode_failed; }
                {
                    static const int pass_start[4] = {0, 4, 2, 1};
                    static const int pass_step[4]  = {8, 8, 4, 2};
                    size_t src = 0;
                    for (int p = 0; p < 4; p++) {
                        for (size_t row = (size_t)pass_start[p]; row < fh;
                             row += (size_t)pass_step[p]) {
                            memcpy(deint + row * (size_t)fw,
                                   indices + src * (size_t)fw, fw);
                            src++;
                        }
                    }
                    free(indices);
                    indices = deint;
                }
            }

            /* Store frame */
            if (img->num_frames >= frame_cap) {
                if (frame_cap > INT_MAX / 2 ||
                    (size_t)frame_cap * 2 > SIZE_MAX / sizeof(*img->frames)) {
                    free(indices);
                    goto decode_failed;
                }
                int new_cap = frame_cap * 2;
                neverc_gif_frame_t *grown = (neverc_gif_frame_t *)realloc(
                    img->frames, (size_t)new_cap * sizeof(*img->frames));
                if (!grown) { free(indices); goto decode_failed; }
                img->frames = grown;
                frame_cap = new_cap;
            }
            neverc_gif_frame_t *f = &img->frames[img->num_frames++];
            memset(f, 0, sizeof(*f));
            f->left = left;
            f->top = top;
            f->width = fw;
            f->height = fh;
            f->indices = indices;
            f->palette_size = pal_size;
            memcpy(f->palette, palette, sizeof(neverc_gif_color_t) * (size_t)(pal_size < 256 ? pal_size : 256));
            f->delay_centiseconds = pending_delay;
            f->disposal_method = pending_disposal;
            if (pending_transparent >= 0) {
                f->has_transparency = 1;
                f->transparent_index = (uint8_t)pending_transparent;
            }
            pending_delay = 0;
            pending_transparent = -1;
            pending_disposal = 0;
            continue;
        }

        goto decode_failed;
    }

    if (!saw_trailer || img->num_frames == 0) goto decode_failed;
    if (pos != len) goto decode_failed;

    img->width = width;
    img->height = height;
    img->background = background;
    return 0;

decode_failed:
    neverc_gif_free(img);
    return -1;
}

void neverc_gif_free(neverc_gif_image_t *img) {
    if (!img) return;
    for (int i = 0; i < img->num_frames; i++)
        free(img->frames[i].indices);
    free(img->frames);
    memset(img, 0, sizeof(*img));
}

int neverc_gif_frame_to_rgba(const neverc_gif_frame_t *frame,
                             uint8_t **out_rgba, size_t *out_len) {
    if (out_rgba) *out_rgba = NULL;
    if (out_len) *out_len = 0;
    if (!frame || !frame->indices || !out_rgba || !out_len) return -1;
    if (frame->width == 0 || frame->height == 0) return -1;
    if (frame->palette_size < 1 || frame->palette_size > NEVERC_GIF_MAX_PALETTE)
        return -1;
    if (frame->has_transparency &&
        (int)frame->transparent_index >= frame->palette_size)
        return -1;
    if ((uint64_t)frame->width * frame->height > GIF_MAX_PIXELS) return -1;
    if (frame->height > SIZE_MAX / 4 / frame->width) return -1;

    size_t npixels = (size_t)frame->width * frame->height;
    size_t nbytes = npixels * 4u;
    uint8_t *rgba = (uint8_t *)malloc(nbytes);
    if (!rgba) return -1;

    for (size_t i = 0; i < npixels; i++) {
        int idx = frame->indices[i];
        if (idx >= frame->palette_size) idx = 0;
        uint8_t *p = rgba + i * 4u;
        p[0] = frame->palette[idx].r;
        p[1] = frame->palette[idx].g;
        p[2] = frame->palette[idx].b;
        p[3] = (frame->has_transparency &&
                idx == (int)frame->transparent_index) ? 0 : 255;
    }

    *out_rgba = rgba;
    *out_len = nbytes;
    return 0;
}

/* =========================================================================
 * Wu's color quantizer — Xiaolin Wu, "Efficient Statistical Computation of
 * Greedy Binary Partitioning" (Graphics Gems vol. II, 1991).
 *
 * The previous from_rgba used a fixed 216-entry web-safe cube and rounded each
 * channel to the nearest 1/5, so every image — however few colors it actually
 * contained — was forced onto the same coarse grid (visible banding, wasted
 * palette slots). Wu instead chooses up to 256 palette colors *from the image*:
 * it builds a 3D color histogram (5 bits/channel), computes cumulative moments
 * so any axis-aligned box's count / colour sums / squared error are O(1), then
 * greedily splits the box of largest weighted variance until 256 boxes remain.
 * Each box's mean becomes a palette entry; pixels map through a per-cell tag.
 *
 * This is the same non-iterative, deterministic statistical method used by
 * ImageMagick / libimagequant's fast path and is strictly better quality than
 * median-cut at the same cost. Pure standard C (heap-allocated moment arrays,
 * no platform APIs / SIMD / VLAs), so it is identical on every target.
 * ========================================================================= */

#define WU_SIDE   33          /* 32 histogram levels (5-bit) + 1 moment base   */
#define WU_TARGET 256         /* maximum palette entries (GIF limit)           */
enum { WU_DIR_R = 0, WU_DIR_G = 1, WU_DIR_B = 2 };

typedef struct { int r0, r1, g0, g1, b0, b1; long vol; } wu_box;
typedef struct { int64_t *wt, *mr, *mg, *mb; double *m2; } wu_moments;

static long wu_ind(int r, int g, int b) {
    return ((long)r * WU_SIDE + g) * WU_SIDE + b;
}

/* Convert the raw histogram into cumulative moments so that any box sum is an
 * 8-corner inclusion-exclusion lookup (the summed-area-table trick in 3D). */
static void wu_m3d(wu_moments *m) {
    int64_t area[WU_SIDE], area_r[WU_SIDE], area_g[WU_SIDE], area_b[WU_SIDE];
    double  area2[WU_SIDE];
    for (int r = 1; r < WU_SIDE; r++) {
        for (int i = 0; i < WU_SIDE; i++) {
            area[i] = area_r[i] = area_g[i] = area_b[i] = 0;
            area2[i] = 0.0;
        }
        for (int g = 1; g < WU_SIDE; g++) {
            int64_t line = 0, line_r = 0, line_g = 0, line_b = 0;
            double  line2 = 0.0;
            for (int b = 1; b < WU_SIDE; b++) {
                long i1 = wu_ind(r, g, b);
                line   += m->wt[i1]; line_r += m->mr[i1];
                line_g += m->mg[i1]; line_b += m->mb[i1]; line2 += m->m2[i1];
                area[b]   += line;   area_r[b] += line_r;
                area_g[b] += line_g; area_b[b] += line_b; area2[b] += line2;
                long i0 = wu_ind(r - 1, g, b);
                m->wt[i1] = m->wt[i0] + area[b];
                m->mr[i1] = m->mr[i0] + area_r[b];
                m->mg[i1] = m->mg[i0] + area_g[b];
                m->mb[i1] = m->mb[i0] + area_b[b];
                m->m2[i1] = m->m2[i0] + area2[b];
            }
        }
    }
}

static int64_t wu_vol(const wu_box *c, const int64_t *mmt) {
    return mmt[wu_ind(c->r1, c->g1, c->b1)] - mmt[wu_ind(c->r1, c->g1, c->b0)]
         - mmt[wu_ind(c->r1, c->g0, c->b1)] + mmt[wu_ind(c->r1, c->g0, c->b0)]
         - mmt[wu_ind(c->r0, c->g1, c->b1)] + mmt[wu_ind(c->r0, c->g1, c->b0)]
         + mmt[wu_ind(c->r0, c->g0, c->b1)] - mmt[wu_ind(c->r0, c->g0, c->b0)];
}

static double wu_vol_d(const wu_box *c, const double *mmt) {
    return mmt[wu_ind(c->r1, c->g1, c->b1)] - mmt[wu_ind(c->r1, c->g1, c->b0)]
         - mmt[wu_ind(c->r1, c->g0, c->b1)] + mmt[wu_ind(c->r1, c->g0, c->b0)]
         - mmt[wu_ind(c->r0, c->g1, c->b1)] + mmt[wu_ind(c->r0, c->g1, c->b0)]
         + mmt[wu_ind(c->r0, c->g0, c->b1)] - mmt[wu_ind(c->r0, c->g0, c->b0)];
}

/* Sum over the box face opposite the cut direction (the part independent of the
 * cut position). */
static int64_t wu_bottom(const wu_box *c, int dir, const int64_t *mmt) {
    switch (dir) {
    case WU_DIR_R:
        return -mmt[wu_ind(c->r0, c->g1, c->b1)] + mmt[wu_ind(c->r0, c->g1, c->b0)]
             + mmt[wu_ind(c->r0, c->g0, c->b1)] - mmt[wu_ind(c->r0, c->g0, c->b0)];
    case WU_DIR_G:
        return -mmt[wu_ind(c->r1, c->g0, c->b1)] + mmt[wu_ind(c->r1, c->g0, c->b0)]
             + mmt[wu_ind(c->r0, c->g0, c->b1)] - mmt[wu_ind(c->r0, c->g0, c->b0)];
    default: /* WU_DIR_B */
        return -mmt[wu_ind(c->r1, c->g1, c->b0)] + mmt[wu_ind(c->r1, c->g0, c->b0)]
             + mmt[wu_ind(c->r0, c->g1, c->b0)] - mmt[wu_ind(c->r0, c->g0, c->b0)];
    }
}

/* Sum over the box up to plane `pos` along the cut direction. */
static int64_t wu_top(const wu_box *c, int dir, int pos, const int64_t *mmt) {
    switch (dir) {
    case WU_DIR_R:
        return mmt[wu_ind(pos, c->g1, c->b1)] - mmt[wu_ind(pos, c->g1, c->b0)]
             - mmt[wu_ind(pos, c->g0, c->b1)] + mmt[wu_ind(pos, c->g0, c->b0)];
    case WU_DIR_G:
        return mmt[wu_ind(c->r1, pos, c->b1)] - mmt[wu_ind(c->r1, pos, c->b0)]
             - mmt[wu_ind(c->r0, pos, c->b1)] + mmt[wu_ind(c->r0, pos, c->b0)];
    default: /* WU_DIR_B */
        return mmt[wu_ind(c->r1, c->g1, pos)] - mmt[wu_ind(c->r1, c->g0, pos)]
             - mmt[wu_ind(c->r0, c->g1, pos)] + mmt[wu_ind(c->r0, c->g0, pos)];
    }
}

/* Total squared error of a box (the quantity each cut tries to reduce). */
static double wu_var(const wu_box *c, const wu_moments *m) {
    double dr = (double)wu_vol(c, m->mr);
    double dg = (double)wu_vol(c, m->mg);
    double db = (double)wu_vol(c, m->mb);
    double xx = wu_vol_d(c, m->m2);
    int64_t w = wu_vol(c, m->wt);
    if (w == 0) return 0.0;
    return xx - (dr * dr + dg * dg + db * db) / (double)w;
}

/* Best cut plane along one axis: maximizes the sum of the two halves' weighted
 * squared means (equivalently minimizes the combined variance). */
static double wu_maximize(const wu_box *c, int dir, int first, int last, int *cut,
                          int64_t whole_w, int64_t whole_r, int64_t whole_g,
                          int64_t whole_b, const wu_moments *m) {
    int64_t base_r = wu_bottom(c, dir, m->mr);
    int64_t base_g = wu_bottom(c, dir, m->mg);
    int64_t base_b = wu_bottom(c, dir, m->mb);
    int64_t base_w = wu_bottom(c, dir, m->wt);
    double max = 0.0;
    *cut = -1;
    for (int i = first; i < last; i++) {
        int64_t half_r = base_r + wu_top(c, dir, i, m->mr);
        int64_t half_g = base_g + wu_top(c, dir, i, m->mg);
        int64_t half_b = base_b + wu_top(c, dir, i, m->mb);
        int64_t half_w = base_w + wu_top(c, dir, i, m->wt);
        if (half_w == 0) continue;                 /* nothing below the plane */
        int64_t other_w = whole_w - half_w;
        if (other_w == 0) continue;                /* nothing above the plane */
        double temp = ((double)half_r * (double)half_r
                     + (double)half_g * (double)half_g
                     + (double)half_b * (double)half_b) / (double)half_w;
        double or_ = (double)(whole_r - half_r);
        double og_ = (double)(whole_g - half_g);
        double ob_ = (double)(whole_b - half_b);
        temp += (or_ * or_ + og_ * og_ + ob_ * ob_) / (double)other_w;
        if (temp > max) { max = temp; *cut = i; }
    }
    return max;
}

/* Split set1 in two; the new half is written to set2. Returns 0 if the box is a
 * single point that cannot be cut. */
static int wu_cut(wu_box *set1, wu_box *set2, const wu_moments *m) {
    int64_t whole_w = wu_vol(set1, m->wt);
    int64_t whole_r = wu_vol(set1, m->mr);
    int64_t whole_g = wu_vol(set1, m->mg);
    int64_t whole_b = wu_vol(set1, m->mb);
    int cutr, cutg, cutb;
    double maxr = wu_maximize(set1, WU_DIR_R, set1->r0 + 1, set1->r1, &cutr,
                              whole_w, whole_r, whole_g, whole_b, m);
    double maxg = wu_maximize(set1, WU_DIR_G, set1->g0 + 1, set1->g1, &cutg,
                              whole_w, whole_r, whole_g, whole_b, m);
    double maxb = wu_maximize(set1, WU_DIR_B, set1->b0 + 1, set1->b1, &cutb,
                              whole_w, whole_r, whole_g, whole_b, m);
    int dir;
    if (maxr >= maxg && maxr >= maxb) {
        dir = WU_DIR_R;
        if (cutr < 0) return 0;                    /* cannot split on any axis */
    } else if (maxg >= maxr && maxg >= maxb) {
        dir = WU_DIR_G;
    } else {
        dir = WU_DIR_B;
    }
    *set2 = *set1;
    switch (dir) {
    case WU_DIR_R: set2->r0 = set1->r1 = cutr; break;
    case WU_DIR_G: set2->g0 = set1->g1 = cutg; break;
    default:       set2->b0 = set1->b1 = cutb; break;
    }
    set1->vol = (long)(set1->r1 - set1->r0) * (set1->g1 - set1->g0)
              * (set1->b1 - set1->b0);
    set2->vol = (long)(set2->r1 - set2->r0) * (set2->g1 - set2->g0)
              * (set2->b1 - set2->b0);
    return 1;
}

/* Fallback used when the (~1.5 MB) moment arrays cannot be allocated: the old
 * fixed 216-color web-safe cube. Always succeeds without large scratch. */
static int gif_quantize_uniform(const uint8_t *rgba, size_t npixels,
                                neverc_gif_frame_t *frame) {
    frame->palette_size = 216;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                int idx = r * 36 + g * 6 + b;
                frame->palette[idx].r = (uint8_t)(r * 51);
                frame->palette[idx].g = (uint8_t)(g * 51);
                frame->palette[idx].b = (uint8_t)(b * 51);
            }
    frame->indices = (uint8_t *)malloc(npixels ? npixels : 1);
    if (!frame->indices) return -1;
    for (size_t i = 0; i < npixels; i++) {
        int ri = (rgba[i * 4 + 0] + 25) / 51; if (ri > 5) ri = 5;
        int gi = (rgba[i * 4 + 1] + 25) / 51; if (gi > 5) gi = 5;
        int bi = (rgba[i * 4 + 2] + 25) / 51; if (bi > 5) bi = 5;
        frame->indices[i] = (uint8_t)(ri * 36 + gi * 6 + bi);
    }
    return 0;
}

/* Mark alpha-0 source pixels as a GIF transparent index. Adds a palette
 * slot when one is free; otherwise prefers an index unused by opaque
 * pixels. Stealing a used slot remaps those opaque pixels first so they
 * do not become transparent. */
static void gif_apply_rgba_transparency(const uint8_t *rgba, size_t npixels,
                                        neverc_gif_frame_t *frame) {
    int saw_trans = 0;
    for (size_t i = 0; i < npixels; i++) {
        if (rgba[i * 4u + 3u] == 0) { saw_trans = 1; break; }
    }
    if (!saw_trans) return;

    int used[NEVERC_GIF_MAX_PALETTE];
    memset(used, 0, sizeof(used));
    for (size_t i = 0; i < npixels; i++) {
        if (rgba[i * 4u + 3u] != 0)
            used[frame->indices[i]] = 1;
    }

    int tidx = -1;
    if (frame->palette_size < NEVERC_GIF_MAX_PALETTE) {
        tidx = frame->palette_size;
        frame->palette[tidx].r = 0;
        frame->palette[tidx].g = 0;
        frame->palette[tidx].b = 0;
        frame->palette_size++;
    } else {
        for (int i = 0; i < NEVERC_GIF_MAX_PALETTE; i++) {
            if (!used[i]) { tidx = i; break; }
        }
        if (tidx < 0) {
            tidx = NEVERC_GIF_MAX_PALETTE - 1;
            uint8_t remap = 0;
            for (int i = 0; i < NEVERC_GIF_MAX_PALETTE; i++) {
                if (i != tidx && used[i]) {
                    remap = (uint8_t)i;
                    break;
                }
            }
            for (size_t i = 0; i < npixels; i++) {
                if (rgba[i * 4u + 3u] != 0 &&
                    frame->indices[i] == (uint8_t)tidx)
                    frame->indices[i] = remap;
            }
        }
    }
    frame->has_transparency = 1;
    frame->transparent_index = (uint8_t)tidx;
    for (size_t i = 0; i < npixels; i++) {
        if (rgba[i * 4u + 3u] == 0)
            frame->indices[i] = (uint8_t)tidx;
    }
}

int neverc_gif_from_rgba(const uint8_t *rgba, uint32_t width, uint32_t height,
                         neverc_gif_frame_t *frame) {
    if (!rgba || !frame || width == 0 || height == 0 ||
        width > UINT16_MAX || height > UINT16_MAX ||
        (uint64_t)width * height > GIF_MAX_PIXELS)
        return -1;
    memset(frame, 0, sizeof(*frame));
    frame->width = width;
    frame->height = height;

    size_t npixels = (size_t)width * height;
    size_t cells = (size_t)WU_SIDE * WU_SIDE * WU_SIDE;

    wu_moments m;
    m.wt = (int64_t *)calloc(cells, sizeof(int64_t));
    m.mr = (int64_t *)calloc(cells, sizeof(int64_t));
    m.mg = (int64_t *)calloc(cells, sizeof(int64_t));
    m.mb = (int64_t *)calloc(cells, sizeof(int64_t));
    m.m2 = (double  *)calloc(cells, sizeof(double));
    unsigned char *tag = (unsigned char *)malloc(cells);
    if (!m.wt || !m.mr || !m.mg || !m.mb || !m.m2 || !tag) {
        free(m.wt); free(m.mr); free(m.mg); free(m.mb); free(m.m2); free(tag);
        if (gif_quantize_uniform(rgba, npixels, frame) != 0) return -1;
        gif_apply_rgba_transparency(rgba, npixels, frame);
        return 0;
    }

    /* 3D histogram at 5-bit precision; cells are 1-indexed (0 = moment base). */
    for (size_t i = 0; i < npixels; i++) {
        int r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
        long idx = wu_ind((r >> 3) + 1, (g >> 3) + 1, (b >> 3) + 1);
        m.wt[idx] += 1;
        m.mr[idx] += r;
        m.mg[idx] += g;
        m.mb[idx] += b;
        m.m2[idx] += (double)(r * r + g * g + b * b);
    }
    wu_m3d(&m);

    wu_box cube[WU_TARGET];
    double  vv[WU_TARGET];
    memset(&cube[0], 0, sizeof(cube[0]));
    cube[0].r1 = cube[0].g1 = cube[0].b1 = WU_SIDE - 1;
    int ncolors = WU_TARGET, next = 0;
    for (int i = 1; i < WU_TARGET; i++) {
        if (wu_cut(&cube[next], &cube[i], &m)) {
            vv[next] = cube[next].vol > 1 ? wu_var(&cube[next], &m) : 0.0;
            vv[i]    = cube[i].vol    > 1 ? wu_var(&cube[i], &m)    : 0.0;
        } else {
            vv[next] = 0.0;     /* this box is a single point; never revisit it */
            i--;                /* reuse the slot for the next candidate        */
        }
        double temp = 0.0;
        next = 0;
        for (int j = 0; j <= i; j++)
            if (vv[j] > temp) { temp = vv[j]; next = j; }
        if (temp <= 0.0) { ncolors = i + 1; break; }
    }

    /* Tag every histogram cell with the box (palette index) that owns it, then
     * derive each box's mean colour. */
    for (int k = 0; k < ncolors; k++) {
        int64_t w = wu_vol(&cube[k], m.wt);
        if (w > 0) {
            frame->palette[k].r = (uint8_t)(wu_vol(&cube[k], m.mr) / w);
            frame->palette[k].g = (uint8_t)(wu_vol(&cube[k], m.mg) / w);
            frame->palette[k].b = (uint8_t)(wu_vol(&cube[k], m.mb) / w);
        } else {
            frame->palette[k].r = frame->palette[k].g = frame->palette[k].b = 0;
        }
        for (int r = cube[k].r0 + 1; r <= cube[k].r1; r++)
            for (int g = cube[k].g0 + 1; g <= cube[k].g1; g++)
                for (int b = cube[k].b0 + 1; b <= cube[k].b1; b++)
                    tag[wu_ind(r, g, b)] = (unsigned char)k;
    }

    /* GIF local color tables need >= 2 entries; pad a degenerate single-color
     * image so the encoder accepts it. */
    if (ncolors < 2) {
        frame->palette[1] = frame->palette[0];
        ncolors = 2;
    }
    frame->palette_size = ncolors;

    frame->indices = (uint8_t *)malloc(npixels);
    if (!frame->indices) {
        free(m.wt); free(m.mr); free(m.mg); free(m.mb); free(m.m2); free(tag);
        return -1;
    }
    for (size_t i = 0; i < npixels; i++) {
        int r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
        frame->indices[i] = tag[wu_ind((r >> 3) + 1, (g >> 3) + 1, (b >> 3) + 1)];
    }

    free(m.wt); free(m.mr); free(m.mg); free(m.mb); free(m.m2); free(tag);
    gif_apply_rgba_transparency(rgba, npixels, frame);
    return 0;
}
