/*
 * NeverC compress/gzip — gzip format (RFC 1952).
 * Wraps DEFLATE with gzip header (10 bytes) and trailer (CRC32 + ISIZE).
 */

#include "neverc/std/compress/gzip.h"
#include "neverc/std/compress/flate.h"
#include "neverc/std/hash/crc32.h"
#include <string.h>

int neverc_gzip_compress(const uint8_t *src, size_t src_len,
                         uint8_t *dst, size_t *dst_len, int level) {
    if (*dst_len < 18) return -1;

    /* gzip header (10 bytes, minimal) */
    dst[0] = 0x1F;  /* ID1 */
    dst[1] = 0x8B;  /* ID2 */
    dst[2] = 0x08;  /* CM = deflate */
    dst[3] = 0x00;  /* FLG = none */
    dst[4] = dst[5] = dst[6] = dst[7] = 0; /* MTIME */
    dst[8] = 0x00;  /* XFL */
    dst[9] = 0xFF;  /* OS = unknown */

    /* DEFLATE payload */
    size_t payload_cap = *dst_len - 18;
    size_t payload_len = payload_cap;
    if (neverc_flate_compress(src, src_len, dst + 10, &payload_len, level) < 0)
        return -1;

    /* Trailer: CRC32 + ISIZE (each 4 bytes, little-endian) */
    size_t trailer_pos = 10 + payload_len;
    if (trailer_pos + 8 > *dst_len) return -1;

    uint32_t crc = neverc_crc32_ieee(src, src_len);
    uint32_t isize = (uint32_t)(src_len & 0xFFFFFFFF);

    dst[trailer_pos + 0] = (uint8_t)(crc);
    dst[trailer_pos + 1] = (uint8_t)(crc >> 8);
    dst[trailer_pos + 2] = (uint8_t)(crc >> 16);
    dst[trailer_pos + 3] = (uint8_t)(crc >> 24);
    dst[trailer_pos + 4] = (uint8_t)(isize);
    dst[trailer_pos + 5] = (uint8_t)(isize >> 8);
    dst[trailer_pos + 6] = (uint8_t)(isize >> 16);
    dst[trailer_pos + 7] = (uint8_t)(isize >> 24);

    *dst_len = trailer_pos + 8;
    return 0;
}

int neverc_gzip_decompress(const uint8_t *src, size_t src_len,
                           uint8_t *dst, size_t *dst_len) {
    if (src_len < 18) return -1;
    if (src[0] != 0x1F || src[1] != 0x8B) return -1;
    if (src[2] != 0x08) return -1;

    uint8_t flg = src[3];
    size_t pos = 10;

    /* skip optional header fields */
    if (flg & 0x04) { /* FEXTRA */
        if (pos + 2 > src_len) return -1;
        uint16_t xlen = (uint16_t)src[pos] | ((uint16_t)src[pos+1] << 8);
        pos += 2 + xlen;
    }
    if (flg & 0x08) { /* FNAME */
        while (pos < src_len && src[pos] != 0) pos++;
        pos++;
    }
    if (flg & 0x10) { /* FCOMMENT */
        while (pos < src_len && src[pos] != 0) pos++;
        pos++;
    }
    if (flg & 0x02) { /* FHCRC */
        pos += 2;
    }
    if (pos >= src_len || src_len - pos < 8) return -1;

    /* trailer at the end */
    uint32_t expected_crc = (uint32_t)src[src_len - 8]
                          | ((uint32_t)src[src_len - 7] << 8)
                          | ((uint32_t)src[src_len - 6] << 16)
                          | ((uint32_t)src[src_len - 5] << 24);
    uint32_t expected_size = (uint32_t)src[src_len - 4]
                           | ((uint32_t)src[src_len - 3] << 8)
                           | ((uint32_t)src[src_len - 2] << 16)
                           | ((uint32_t)src[src_len - 1] << 24);

    size_t payload_len = src_len - pos - 8;
    size_t out_len = *dst_len;
    if (neverc_flate_decompress(src + pos, payload_len, dst, &out_len) < 0)
        return -1;

    uint32_t actual_crc = neverc_crc32_ieee(dst, out_len);

    if (actual_crc != expected_crc) return -1;
    if ((uint32_t)(out_len & 0xFFFFFFFF) != expected_size) return -1;

    *dst_len = out_len;
    return 0;
}
