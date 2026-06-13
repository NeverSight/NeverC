#include "neverc/std/image/png.h"
#include "neverc/std/compress/flate.h"
#include "neverc/std/hash/crc32.h"
#include "neverc/std/hash/adler32.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t PNG_SIGNATURE[8] = {137, 80, 78, 71, 13, 10, 26, 10};

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
    uint8_t *idat_buf = NULL;
    size_t idat_len = 0, idat_cap = 0;

    while (pos + 12 <= len) {
        uint32_t chunk_len = read_u32be(data + pos);
        const uint8_t *chunk_type = data + pos + 4;
        const uint8_t *chunk_data = data + pos + 8;

        if (pos + 12 + chunk_len > len) break;

        if (memcmp(chunk_type, "IHDR", 4) == 0 && chunk_len >= 13) {
            img->width = read_u32be(chunk_data);
            img->height = read_u32be(chunk_data + 4);
            img->bit_depth = chunk_data[8];
            img->color_type = chunk_data[9];
            if (img->bit_depth != 8) { free(idat_buf); return -1; }
            img->channels = (uint8_t)channels_for_color_type(img->color_type);
            if (img->channels == 0) { free(idat_buf); return -1; }
            img->stride = (size_t)img->width * img->channels;
            ihdr_found = 1;
        } else if (memcmp(chunk_type, "IDAT", 4) == 0) {
            size_t need = idat_len + chunk_len;
            if (need > idat_cap) {
                idat_cap = need * 2;
                uint8_t *nb = (uint8_t *)realloc(idat_buf, idat_cap);
                if (!nb) { free(idat_buf); return -1; }
                idat_buf = nb;
            }
            memcpy(idat_buf + idat_len, chunk_data, chunk_len);
            idat_len += chunk_len;
        } else if (memcmp(chunk_type, "IEND", 4) == 0) {
            break;
        }

        pos += 12 + chunk_len;
    }

    if (!ihdr_found || idat_len < 6) { free(idat_buf); return -1; }

    /* Skip zlib header (2 bytes) and checksum (4 bytes at end) */
    size_t raw_size = (img->stride + 1) * img->height;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) { free(idat_buf); return -1; }

    size_t decompressed_len = raw_size;
    int rc = neverc_flate_decompress(idat_buf + 2, idat_len - 6, raw, &decompressed_len);
    free(idat_buf);

    if (rc != 0 || decompressed_len != raw_size) { free(raw); return -1; }

    /* Reverse filters */
    img->pixels = (uint8_t *)calloc(1, img->height * img->stride);
    if (!img->pixels) { free(raw); return -1; }

    size_t bpp = img->channels;
    for (uint32_t y = 0; y < img->height; y++) {
        uint8_t *scanline = raw + y * (img->stride + 1);
        uint8_t filter_type = scanline[0];
        uint8_t *src = scanline + 1;
        uint8_t *dst = img->pixels + y * img->stride;
        uint8_t *prev = (y > 0) ? img->pixels + (y - 1) * img->stride : NULL;

        if (png_unfilter_row(dst, src, prev, img->stride, bpp, filter_type) != 0) {
            free(raw); free(img->pixels); img->pixels = NULL; return -1;
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
static void png_filter_row(const uint8_t *cur, const uint8_t *prev,
                           size_t stride, size_t bpp,
                           uint8_t *out_filter, uint8_t *out_row,
                           uint8_t *scratch) {
    unsigned long best_score = ~0UL;
    int best = 0;
    for (int f = 0; f < 5; f++) {
        unsigned long score = 0;
        for (size_t x = 0; x < stride; x++) {
            uint8_t a = (x >= bpp) ? cur[x - bpp] : 0;
            uint8_t b = prev ? prev[x] : 0;
            uint8_t c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            uint8_t v;
            switch (f) {
                case 0:  v = cur[x]; break;
                case 1:  v = (uint8_t)(cur[x] - a); break;
                case 2:  v = (uint8_t)(cur[x] - b); break;
                case 3:  v = (uint8_t)(cur[x] - (uint8_t)(((unsigned)a + b) / 2)); break;
                default: v = (uint8_t)(cur[x] - paeth_predictor(a, b, c)); break;
            }
            scratch[x] = v;
            score += (v < 128) ? v : (256u - v);   /* |signed byte| */
        }
        if (score < best_score) {
            best_score = score; best = f;
            memcpy(out_row, scratch, stride);
        }
    }
    *out_filter = (uint8_t)best;
}

int neverc_png_encode(const neverc_png_image_t *img, uint8_t **out_data, size_t *out_len) {
    if (!img || !img->pixels || !out_data || !out_len) return -1;
    if (img->width == 0 || img->height == 0) return -1;

    size_t raw_size = (img->stride + 1) * img->height;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    uint8_t *scratch = (uint8_t *)malloc(img->stride ? img->stride : 1);
    if (!raw || !scratch) { free(raw); free(scratch); return -1; }

    /* Adaptive per-scanline filtering (decoder reverses all five types). */
    size_t bpp = img->channels ? img->channels : 1;
    for (uint32_t y = 0; y < img->height; y++) {
        const uint8_t *cur = img->pixels + (size_t)y * img->stride;
        const uint8_t *prev = (y > 0)
            ? img->pixels + (size_t)(y - 1) * img->stride : NULL;
        uint8_t *row = raw + (size_t)y * (img->stride + 1);
        png_filter_row(cur, prev, img->stride, bpp, &row[0], &row[1], scratch);
    }
    free(scratch);

    /* zlib's adler32 is over the filtered bytes that DEFLATE compresses; use the
     * shared unrolled implementation instead of a byte-at-a-time loop. */
    uint32_t adler = neverc_adler32_checksum(raw, raw_size);

    /* DEFLATE compress */
    size_t comp_cap = raw_size + raw_size / 100 + 64;
    uint8_t *comp = (uint8_t *)malloc(comp_cap);
    if (!comp) { free(raw); return -1; }

    size_t comp_len = comp_cap;
    int rc = neverc_flate_compress(raw, raw_size, comp, &comp_len, NEVERC_FLATE_DEFAULT);
    free(raw);
    if (rc != 0) { free(comp); return -1; }

    size_t zlib_len = 2 + comp_len + 4;
    uint8_t *zlib_data = (uint8_t *)malloc(zlib_len);
    if (!zlib_data) { free(comp); return -1; }
    zlib_data[0] = 0x78; /* CMF: CM=8 (deflate), CINFO=7 (32K window) */
    zlib_data[1] = 0x01; /* FLG: FCHECK so (CMF*256+FLG) % 31 == 0 */
    /* Fix FCHECK */
    uint16_t check = (uint16_t)zlib_data[0] * 256 + zlib_data[1];
    zlib_data[1] += (uint8_t)(31 - (check % 31));
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
    if (img && img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
}

const uint8_t *neverc_png_pixel_at(const neverc_png_image_t *img, uint32_t x, uint32_t y) {
    if (!img || !img->pixels || x >= img->width || y >= img->height) return NULL;
    return img->pixels + (size_t)y * img->stride + (size_t)x * img->channels;
}

void neverc_png_pixel_set(neverc_png_image_t *img, uint32_t x, uint32_t y, const uint8_t *src) {
    if (!img || !img->pixels || !src || x >= img->width || y >= img->height) return;
    memcpy(img->pixels + (size_t)y * img->stride + (size_t)x * img->channels,
           src, img->channels);
}
