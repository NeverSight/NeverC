#include "neverc/std/image/png.h"
#include "neverc/std/compress/flate.h"
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

static const uint32_t crc_table[256] = {
    0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
    0x0EDB8832,0x79DCB8A4,0xE0D5E91B,0x97D2D988,0x09B64C2B,0x7EB17CBE,0xE7B82D09,0x90BF1D9F,
    0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
    0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
    0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
    0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
    0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
    0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
    0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
    0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
    0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
    0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
    0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
    0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7822,
    0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
    0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
    0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F6B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
    0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
    0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
    0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
    0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
    0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
    0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
    0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7822,
    0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
    0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
    0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
    0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
    0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
    0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
    0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
    0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,0x316E8EEF,0x4669BE79,
};

static uint32_t png_crc32(const uint8_t *data, size_t len) {
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

static uint32_t png_chunk_crc(const uint8_t *type_and_data, size_t len) {
    return png_crc32(type_and_data, len);
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

        for (size_t x = 0; x < img->stride; x++) {
            uint8_t a = (x >= bpp) ? dst[x - bpp] : 0;
            uint8_t b = prev ? prev[x] : 0;
            uint8_t c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            uint8_t raw_byte = src[x];

            switch (filter_type) {
                case 0: dst[x] = raw_byte; break;
                case 1: dst[x] = raw_byte + a; break;
                case 2: dst[x] = raw_byte + b; break;
                case 3: dst[x] = raw_byte + (uint8_t)(((int)a + (int)b) / 2); break;
                case 4: dst[x] = raw_byte + paeth_predictor(a, b, c); break;
                default: free(raw); free(img->pixels); img->pixels = NULL; return -1;
            }
        }
    }

    free(raw);
    return 0;
}

int neverc_png_encode(const neverc_png_image_t *img, uint8_t **out_data, size_t *out_len) {
    if (!img || !img->pixels || !out_data || !out_len) return -1;
    if (img->width == 0 || img->height == 0) return -1;

    size_t raw_size = (img->stride + 1) * img->height;
    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) return -1;

    /* Apply no filter (type 0) for simplicity */
    for (uint32_t y = 0; y < img->height; y++) {
        raw[y * (img->stride + 1)] = 0; /* filter None */
        memcpy(raw + y * (img->stride + 1) + 1,
               img->pixels + y * img->stride, img->stride);
    }

    /* DEFLATE compress */
    size_t comp_cap = raw_size + raw_size / 100 + 64;
    uint8_t *comp = (uint8_t *)malloc(comp_cap);
    if (!comp) { free(raw); return -1; }

    size_t comp_len = comp_cap;
    int rc = neverc_flate_compress(raw, raw_size, comp, &comp_len, NEVERC_FLATE_DEFAULT);
    free(raw);
    if (rc != 0) { free(comp); return -1; }

    /* Build zlib wrapper: 2-byte header + deflate data + 4-byte adler32 */
    /* Calculate adler32 of original raw data for zlib */
    /* Re-create raw for adler32 calc */
    raw = (uint8_t *)malloc(raw_size);
    if (!raw) { free(comp); return -1; }
    for (uint32_t y = 0; y < img->height; y++) {
        raw[y * (img->stride + 1)] = 0;
        memcpy(raw + y * (img->stride + 1) + 1,
               img->pixels + y * img->stride, img->stride);
    }
    uint32_t a32_s1 = 1, a32_s2 = 0;
    for (size_t i = 0; i < raw_size; i++) {
        a32_s1 = (a32_s1 + raw[i]) % 65521;
        a32_s2 = (a32_s2 + a32_s1) % 65521;
    }
    uint32_t adler = (a32_s2 << 16) | a32_s1;
    free(raw);

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
