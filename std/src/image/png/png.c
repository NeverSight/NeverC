#include "neverc/std/image/png.h"
#include "neverc/std/compress/flate.h"
#include "neverc/std/hash/crc32.h"
#include "neverc/std/hash/adler32.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t PNG_SIGNATURE[8] = {137, 80, 78, 71, 13, 10, 26, 10};
#define PNG_MAX_PIXELS (UINT64_C(1) << 28)

static uint32_t read_u32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_u32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

/* Chunk CRC is the standard CRC-32/IEEE over the chunk's type+data (RFC 2083
 * §5). Use the shared slicing-by-8 implementation instead of a private
 * byte-at-a-time table: it is both faster and correct. (The previous private
 * table was corrupted — duplicated rows — so every emitted chunk carried an
 * invalid CRC that conformant decoders such as libpng reject; only this
 * library's own decoder, which skips CRC verification, accepted them.) */
static uint32_t png_chunk_crc(const uint8_t *type_and_data, size_t len) {
    return neverc_crc32_ieee(type_and_data, len);
}

static uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a; if (pa < 0) pa = -pa;
    int pb = p - (int)b; if (pb < 0) pb = -pb;
    int pc = p - (int)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static void png_decode_fail(neverc_png_image_t *img, uint8_t *idat_buf, uint8_t *raw) {
    free(idat_buf);
    free(raw);
    if (img) {
        free(img->pixels);
        memset(img, 0, sizeof(*img));
    }
}

static int channels_for_color_type(uint8_t ct) {
    switch (ct) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

/*
 * Reverse one scanline's PNG filter (RFC 2083 §6) into dst.
 *
 * The previous decoder ran a per-byte switch on filter_type plus a per-byte
 * `x >= bpp` / `prev != NULL` test. Both are loop-invariant, so this hoists the
 * filter and the prev/no-prev cases out: a None/Up row with no left neighbour
 * collapses to memcpy / a tight vectorizable add, and Sub/Average/Paeth split
 * into a `bpp`-byte prefix (left neighbour = 0) and a branch-free main loop.
 * The arithmetic per byte is identical to the old switch, so output is
 * bit-for-bit unchanged. Returns 0, or -1 on an unknown filter type.
 */
static int png_unfilter_row(uint8_t *dst, const uint8_t *src,
                            const uint8_t *prev, size_t stride, size_t bpp,
                            uint8_t filter_type) {
    size_t x;
    switch (filter_type) {
    case 0:                                   /* None */
        memcpy(dst, src, stride);
        return 0;
    case 1:                                   /* Sub: a = left */
        for (x = 0; x < bpp; x++) dst[x] = src[x];
        for (x = bpp; x < stride; x++) dst[x] = (uint8_t)(src[x] + dst[x - bpp]);
        return 0;
    case 2:                                   /* Up: b = above */
        if (prev) for (x = 0; x < stride; x++) dst[x] = (uint8_t)(src[x] + prev[x]);
        else      memcpy(dst, src, stride);
        return 0;
    case 3:                                   /* Average: (a + b) / 2 */
        if (prev) {
            for (x = 0; x < bpp; x++)
                dst[x] = (uint8_t)(src[x] + (uint8_t)((int)prev[x] / 2));
            for (x = bpp; x < stride; x++)
                dst[x] = (uint8_t)(src[x] + (uint8_t)(((int)dst[x - bpp] + (int)prev[x]) / 2));
        } else {
            for (x = 0; x < bpp; x++) dst[x] = src[x];
            for (x = bpp; x < stride; x++)
                dst[x] = (uint8_t)(src[x] + (uint8_t)((int)dst[x - bpp] / 2));
        }
        return 0;
    case 4:                                   /* Paeth */
        if (prev) {
            for (x = 0; x < bpp; x++)
                dst[x] = (uint8_t)(src[x] + paeth_predictor(0, prev[x], 0));
            for (x = bpp; x < stride; x++)
                dst[x] = (uint8_t)(src[x] + paeth_predictor(dst[x - bpp], prev[x], prev[x - bpp]));
        } else {
            for (x = 0; x < bpp; x++) dst[x] = src[x];
            for (x = bpp; x < stride; x++)
                dst[x] = (uint8_t)(src[x] + paeth_predictor(dst[x - bpp], 0, 0));
        }
        return 0;
    default:
        return -1;
    }
}

int neverc_png_decode(const uint8_t *data, size_t len, neverc_png_image_t *img) {
    if (!data || len < 8 || !img) return -1;
    memset(img, 0, sizeof(*img));

    if (memcmp(data, PNG_SIGNATURE, 8) != 0) return -1;

    size_t pos = 8;
    int ihdr_found = 0;
    int plte_found = 0;
    int idat_seen = 0;
    int idat_ended = 0;
    int iend_found = 0;
    uint8_t *idat_buf = NULL;
    size_t idat_len = 0, idat_cap = 0;

    while (pos <= len && len - pos >= 12) {
        uint32_t chunk_len = read_u32be(data + pos);
        const uint8_t *chunk_type = data + pos + 4;
        const uint8_t *chunk_data = data + pos + 8;

        if ((size_t)chunk_len > len - pos - 12) {
            png_decode_fail(img, idat_buf, NULL);
            return -1;
        }
        if (read_u32be(chunk_data + chunk_len) !=
            png_chunk_crc(chunk_type, (size_t)chunk_len + 4U)) {
            png_decode_fail(img, idat_buf, NULL);
            return -1;
        }
        if (!ihdr_found && memcmp(chunk_type, "IHDR", 4) != 0) {
            png_decode_fail(img, idat_buf, NULL);
            return -1;
        }

        if (memcmp(chunk_type, "IHDR", 4) == 0) {
            if (ihdr_found || chunk_len != 13 ||
                chunk_data[10] != 0 || chunk_data[11] != 0 ||
                chunk_data[12] != 0) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            img->width = read_u32be(chunk_data);
            img->height = read_u32be(chunk_data + 4);
            img->bit_depth = chunk_data[8];
            img->color_type = chunk_data[9];
            if (img->bit_depth != 8) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            img->channels = (uint8_t)channels_for_color_type(img->color_type);
            if (img->channels == 0 ||
                img->color_type == NEVERC_PNG_COLOR_INDEXED) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            if (img->width == 0 || img->height == 0 ||
                (uint64_t)img->width * (uint64_t)img->height >
                    PNG_MAX_PIXELS) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            if (img->width > SIZE_MAX / img->channels) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            img->stride = (size_t)img->width * img->channels;
            if (img->stride > SIZE_MAX - 1U ||
                img->height > SIZE_MAX / (img->stride + 1U)) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            ihdr_found = 1;
        } else if (memcmp(chunk_type, "PLTE", 4) == 0) {
            /* PLTE is optional for truecolor / truecolor-alpha (suggested
             * palette) and forbidden for grayscale / grayscale-alpha
             * (ISO 15948 §11.2.3). At most one PLTE is allowed. */
            if (plte_found || idat_seen || chunk_len == 0 || chunk_len > 768 ||
                chunk_len % 3 != 0 ||
                img->color_type == NEVERC_PNG_COLOR_GRAYSCALE ||
                img->color_type == NEVERC_PNG_COLOR_GRAYSCALE_ALPHA) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            plte_found = 1;
        } else if (memcmp(chunk_type, "IDAT", 4) == 0) {
            if (idat_ended) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            idat_seen = 1;
            /* Skip empty IDAT chunks: a zero-length chunk is legal, but with no
             * data accumulated yet idat_buf is still NULL and `idat_buf + 0`
             * (and the zero-length memcpy onto it) is undefined behavior. */
            if (chunk_len) {
                if ((size_t)chunk_len > SIZE_MAX - idat_len) {
                    png_decode_fail(img, idat_buf, NULL);
                    return -1;
                }
                size_t need = idat_len + chunk_len;
                uint64_t raw_need =
                    ((uint64_t)img->stride + 1u) * (uint64_t)img->height;
                uint64_t idat_limit = raw_need * 2u + 64u;
                if (need > idat_limit) {
                    png_decode_fail(img, idat_buf, NULL);
                    return -1;
                }
                if (need > idat_cap) {
                    size_t new_cap =
                        need > SIZE_MAX / 2 ? need : need * 2;
                    uint8_t *nb = (uint8_t *)realloc(idat_buf, new_cap);
                    if (!nb) {
                        png_decode_fail(img, idat_buf, NULL);
                        return -1;
                    }
                    idat_buf = nb;
                    idat_cap = new_cap;
                }
                memcpy(idat_buf + idat_len, chunk_data, chunk_len);
                idat_len += chunk_len;
            }
        } else if (memcmp(chunk_type, "IEND", 4) == 0) {
            if (chunk_len != 0) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            iend_found = 1;
            pos += 12U;
            if (pos != len) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            break;
        } else {
            if ((chunk_type[0] & 0x20U) == 0) {
                png_decode_fail(img, idat_buf, NULL);
                return -1;
            }
            if (idat_seen) idat_ended = 1;
        }

        pos += 12U + (size_t)chunk_len;
    }

    if (!ihdr_found || !idat_seen || !iend_found || idat_len < 6) {
        png_decode_fail(img, idat_buf, NULL);
        return -1;
    }

    /* Validate the zlib wrapper before passing the raw DEFLATE payload down.
     * Preset dictionaries are unsupported and the Adler-32 trailer is checked
     * after decompression below. */
    uint8_t cmf = idat_buf[0];
    uint8_t flg = idat_buf[1];
    if ((cmf & 0x0fU) != 8U || (cmf >> 4) > 7U ||
        (((unsigned)cmf << 8) | flg) % 31U != 0U ||
        (flg & 0x20U) != 0U) {
        png_decode_fail(img, idat_buf, NULL);
        return -1;
    }
    uint32_t expected_adler = read_u32be(idat_buf + idat_len - 4U);

    /* Skip zlib header (2 bytes) and checksum (4 bytes at end). */
    if (img->stride > SIZE_MAX - 1U ||
        img->height > SIZE_MAX / (img->stride + 1U) ||
        (img->stride != 0 && img->height > SIZE_MAX / img->stride)) {
        png_decode_fail(img, idat_buf, NULL);
        return -1;
    }
    size_t raw_size = (img->stride + 1) * img->height;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) {
        png_decode_fail(img, idat_buf, NULL);
        return -1;
    }

    size_t decompressed_len = raw_size;
    int rc = neverc_flate_decompress(idat_buf + 2, idat_len - 6, raw, &decompressed_len);
    free(idat_buf);
    idat_buf = NULL;

    if (rc != 0 || decompressed_len != raw_size ||
        neverc_adler32_checksum(raw, raw_size) != expected_adler) {
        png_decode_fail(img, NULL, raw);
        return -1;
    }

    /* Reverse filters */
    img->pixels = (uint8_t *)calloc(1, img->height * img->stride);
    if (!img->pixels) {
        png_decode_fail(img, NULL, raw);
        return -1;
    }

    size_t bpp = img->channels;
    for (uint32_t y = 0; y < img->height; y++) {
        uint8_t *scanline = raw + y * (img->stride + 1);
        uint8_t filter_type = scanline[0];
        uint8_t *src = scanline + 1;
        uint8_t *dst = img->pixels + y * img->stride;
        uint8_t *prev = (y > 0) ? img->pixels + (y - 1) * img->stride : NULL;

        if (png_unfilter_row(dst, src, prev, img->stride, bpp, filter_type) != 0) {
            png_decode_fail(img, NULL, raw);
            return -1;
        }
    }

    free(raw);
    return 0;
}

/*
 * Filter one scanline with the PNG spec's recommended minimum-sum-of-absolute-
 * residuals heuristic: try all five filter types and keep the one whose signed
 * residuals are smallest, which leaves DEFLATE the most redundancy to exploit.
 * Writes the chosen type to *out_filter and the filtered bytes to out_row
 * (stride bytes); scratch is stride scratch bytes. a/b/c follow RFC 2083:
 * a = left, b = above, c = upper-left, in the raw (unfiltered) image.
 */
/* Sum of |signed residual| for one candidate row in `scratch`, tracking the
 * best filter so far. Same minimum-residual decision and identical residual
 * bytes as the per-byte version; only the loop shape changed. */
#define PNG_SCORE_BYTE(v) (score += (unsigned)((v) < 128 ? (v) : 256u - (v)))

static void png_filter_row(const uint8_t *cur, const uint8_t *prev,
                           size_t stride, size_t bpp,
                           uint8_t *out_filter, uint8_t *out_row,
                           uint8_t *scratch) {
    uint64_t best_score = UINT64_MAX;
    int best = 0;
    size_t x;

    /* The filter type and the prev/no-prev case are loop-invariant, so they are
     * hoisted out of the per-byte loop (mirrors png_unfilter_row on decode): a
     * `bpp`-byte prefix handles the missing left neighbour, then a branch-free
     * main loop the compiler can vectorize. Arithmetic per byte is unchanged. */
    for (int f = 0; f < 5; f++) {
        uint64_t score = 0;
        switch (f) {
        case 0:                                   /* None */
            for (x = 0; x < stride; x++) { uint8_t v = cur[x]; scratch[x] = v; PNG_SCORE_BYTE(v); }
            break;
        case 1:                                   /* Sub: a = left */
            for (x = 0; x < bpp; x++)    { uint8_t v = cur[x]; scratch[x] = v; PNG_SCORE_BYTE(v); }
            for (x = bpp; x < stride; x++) { uint8_t v = (uint8_t)(cur[x] - cur[x - bpp]); scratch[x] = v; PNG_SCORE_BYTE(v); }
            break;
        case 2:                                   /* Up: b = above */
            if (prev) for (x = 0; x < stride; x++) { uint8_t v = (uint8_t)(cur[x] - prev[x]); scratch[x] = v; PNG_SCORE_BYTE(v); }
            else      for (x = 0; x < stride; x++) { uint8_t v = cur[x]; scratch[x] = v; PNG_SCORE_BYTE(v); }
            break;
        case 3:                                   /* Average: (a + b) / 2 */
            if (prev) {
                for (x = 0; x < bpp; x++)    { uint8_t v = (uint8_t)(cur[x] - (uint8_t)((unsigned)prev[x] / 2)); scratch[x] = v; PNG_SCORE_BYTE(v); }
                for (x = bpp; x < stride; x++) { uint8_t v = (uint8_t)(cur[x] - (uint8_t)(((unsigned)cur[x - bpp] + prev[x]) / 2)); scratch[x] = v; PNG_SCORE_BYTE(v); }
            } else {
                for (x = 0; x < bpp; x++)    { uint8_t v = cur[x]; scratch[x] = v; PNG_SCORE_BYTE(v); }
                for (x = bpp; x < stride; x++) { uint8_t v = (uint8_t)(cur[x] - (uint8_t)((unsigned)cur[x - bpp] / 2)); scratch[x] = v; PNG_SCORE_BYTE(v); }
            }
            break;
        default:                                  /* 4: Paeth */
            if (prev) {
                for (x = 0; x < bpp; x++)    { uint8_t v = (uint8_t)(cur[x] - paeth_predictor(0, prev[x], 0)); scratch[x] = v; PNG_SCORE_BYTE(v); }
                for (x = bpp; x < stride; x++) { uint8_t v = (uint8_t)(cur[x] - paeth_predictor(cur[x - bpp], prev[x], prev[x - bpp])); scratch[x] = v; PNG_SCORE_BYTE(v); }
            } else {
                for (x = 0; x < bpp; x++)    { uint8_t v = cur[x]; scratch[x] = v; PNG_SCORE_BYTE(v); }
                for (x = bpp; x < stride; x++) { uint8_t v = (uint8_t)(cur[x] - paeth_predictor(cur[x - bpp], 0, 0)); scratch[x] = v; PNG_SCORE_BYTE(v); }
            }
            break;
        }
        if (score < best_score) {
            best_score = score; best = f;
            memcpy(out_row, scratch, stride);
        }
    }
    *out_filter = (uint8_t)best;
}

#undef PNG_SCORE_BYTE

int neverc_png_encode(const neverc_png_image_t *img, uint8_t **out_data, size_t *out_len) {
    if (!out_data || !out_len) return -1;
    *out_data = NULL;
    *out_len = 0;
    if (!img || !img->pixels) return -1;

    int expected_channels = channels_for_color_type(img->color_type);
    if (img->width == 0 || img->height == 0 ||
        (img->bit_depth != 0 && img->bit_depth != 8) ||
        expected_channels == 0 ||
        img->color_type == NEVERC_PNG_COLOR_INDEXED ||
        img->channels != (uint8_t)expected_channels ||
        (uint64_t)img->width * (uint64_t)img->height > PNG_MAX_PIXELS)
        return -1;

    size_t row_bytes = (size_t)img->width * img->channels;
    if (img->stride < row_bytes ||
        (img->stride != 0 && img->height > SIZE_MAX / img->stride) ||
        row_bytes == SIZE_MAX ||
        img->height > SIZE_MAX / (row_bytes + 1U))
        return -1;

    size_t raw_size = (row_bytes + 1U) * img->height;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    uint8_t *scratch = (uint8_t *)malloc(row_bytes);
    if (!raw || !scratch) { free(raw); free(scratch); return -1; }

    /* Adaptive per-scanline filtering (decoder reverses all five types). */
    size_t bpp = img->channels ? img->channels : 1;
    for (uint32_t y = 0; y < img->height; y++) {
        const uint8_t *cur = img->pixels + (size_t)y * img->stride;
        const uint8_t *prev = (y > 0)
            ? img->pixels + (size_t)(y - 1) * img->stride : NULL;
        uint8_t *row = raw + (size_t)y * (row_bytes + 1U);
        png_filter_row(cur, prev, row_bytes, bpp,
                       &row[0], &row[1], scratch);
    }
    free(scratch);

    /* zlib's adler32 is over the filtered bytes that DEFLATE compresses; use the
     * shared unrolled implementation instead of a byte-at-a-time loop. */
    uint32_t adler = neverc_adler32_checksum(raw, raw_size);

    /* DEFLATE compress */
    size_t overhead = raw_size / 100U + 64U;
    if (overhead < 64U || overhead > SIZE_MAX - raw_size) {
        free(raw);
        return -1;
    }
    size_t comp_cap = raw_size + overhead;
    uint8_t *comp = (uint8_t *)malloc(comp_cap);
    if (!comp) { free(raw); return -1; }

    size_t comp_len = comp_cap;
    int rc = neverc_flate_compress(raw, raw_size, comp, &comp_len, NEVERC_FLATE_DEFAULT);
    free(raw);
    if (rc != 0) { free(comp); return -1; }

    if (comp_len > UINT32_MAX - 6U) { free(comp); return -1; }
    size_t zlib_len = 2U + comp_len + 4U;
    uint8_t *zlib_data = (uint8_t *)malloc(zlib_len);
    if (!zlib_data) { free(comp); return -1; }
    zlib_data[0] = 0x78; /* CMF: CM=8 (deflate), CINFO=7 (32K window) */
    zlib_data[1] = 0x00; /* FLG: FLEVEL=0, FDICT=0; FCHECK filled in below */
    /* FCHECK: bump FLG to the next value making (CMF*256+FLG) a multiple of 31.
     * Must NOT add a full 31 when already aligned (that would overflow into the
     * FDICT bit and make strict zlib decoders expect a preset-dictionary id,
     * which is exactly the corruption this replaces). */
    uint16_t check = (uint16_t)zlib_data[0] * 256 + zlib_data[1];
    uint8_t rem = (uint8_t)(check % 31);
    if (rem != 0) zlib_data[1] += (uint8_t)(31 - rem);
    memcpy(zlib_data + 2, comp, comp_len);
    free(comp);
    write_u32be(zlib_data + 2 + comp_len, adler);

    /* Assemble PNG file */
    size_t total = 8 + (12 + 13) + (12 + zlib_len) + 12; /* sig + IHDR + IDAT + IEND */
    uint8_t *out = (uint8_t *)malloc(total);
    if (!out) { free(zlib_data); return -1; }
    size_t p = 0;

    /* Signature */
    memcpy(out + p, PNG_SIGNATURE, 8); p += 8;

    /* IHDR chunk */
    write_u32be(out + p, 13); p += 4;
    memcpy(out + p, "IHDR", 4);
    write_u32be(out + p + 4, img->width);
    write_u32be(out + p + 8, img->height);
    out[p + 12] = img->bit_depth ? img->bit_depth : 8;
    out[p + 13] = img->color_type;
    out[p + 14] = 0; /* compression */
    out[p + 15] = 0; /* filter */
    out[p + 16] = 0; /* interlace */
    uint32_t ihdr_crc = png_chunk_crc(out + p, 17);
    p += 17;
    write_u32be(out + p, ihdr_crc); p += 4;

    /* IDAT chunk */
    write_u32be(out + p, (uint32_t)zlib_len); p += 4;
    memcpy(out + p, "IDAT", 4);
    memcpy(out + p + 4, zlib_data, zlib_len);
    uint32_t idat_crc = png_chunk_crc(out + p, 4 + zlib_len);
    p += 4 + zlib_len;
    write_u32be(out + p, idat_crc); p += 4;
    free(zlib_data);

    /* IEND chunk */
    write_u32be(out + p, 0); p += 4;
    memcpy(out + p, "IEND", 4);
    uint32_t iend_crc = png_chunk_crc(out + p, 4);
    p += 4;
    write_u32be(out + p, iend_crc); p += 4;

    *out_data = out;
    *out_len = p;
    return 0;
}

void neverc_png_free(neverc_png_image_t *img) {
    if (!img) return;
    free(img->pixels);
    memset(img, 0, sizeof(*img));
}

static int png_pixel_offset(const neverc_png_image_t *img, uint32_t x, uint32_t y,
                            size_t *out_off) {
    if (!img || !img->pixels || img->channels == 0 ||
        x >= img->width || y >= img->height)
        return -1;
    if (x > SIZE_MAX / img->channels)
        return -1;
    size_t xoff = (size_t)x * img->channels;
    if (img->stride == 0) {
        if (y != 0) return -1;
        *out_off = xoff;
        return 0;
    }
    if (y > (SIZE_MAX - xoff) / img->stride)
        return -1;
    *out_off = (size_t)y * img->stride + xoff;
    return 0;
}

const uint8_t *neverc_png_pixel_at(const neverc_png_image_t *img, uint32_t x, uint32_t y) {
    size_t off;
    if (png_pixel_offset(img, x, y, &off) != 0) return NULL;
    return img->pixels + off;
}

void neverc_png_pixel_set(neverc_png_image_t *img, uint32_t x, uint32_t y, const uint8_t *src) {
    size_t off;
    if (!src || png_pixel_offset(img, x, y, &off) != 0) return;
    memcpy(img->pixels + off, src, img->channels);
}
