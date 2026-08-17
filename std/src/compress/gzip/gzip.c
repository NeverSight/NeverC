/*
 * NeverC compress/gzip — gzip format (RFC 1952).
 * Wraps DEFLATE with gzip header (10 bytes) and trailer (CRC32 + ISIZE).
 */

#include "neverc/std/compress/gzip.h"
#include "neverc/std/compress/flate.h"
#include "neverc/std/hash/crc32.h"

int neverc_gzip_compress(const uint8_t *src, size_t src_len,
                         uint8_t *dst, size_t *dst_len, int level) {
    if (!dst_len || (!src && src_len != 0) ||
        (!dst && *dst_len != 0))
        return -1;
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

/* Parse one gzip member header at src[0..len). On success, *payload_off is
 * the offset of the DEFLATE payload. Header CRC (FHCRC) is computed from
 * this member's start, not from an outer buffer. */
static int gzip_parse_header(const uint8_t *src, size_t len, size_t *payload_off) {
    if (len < 10) return -1;
    if (src[0] != 0x1F || src[1] != 0x8B) return -1;
    if (src[2] != 0x08) return -1;

    uint8_t flg = src[3];
    if (flg & 0xE0) return -1; /* Reserved flags must be zero. */
    size_t pos = 10;

    if (flg & 0x04) { /* FEXTRA */
        if (len - pos < 2) return -1;
        uint16_t xlen = (uint16_t)src[pos] | ((uint16_t)src[pos + 1] << 8);
        pos += 2;
        if ((size_t)xlen > len - pos) return -1;
        pos += xlen;
    }
    if (flg & 0x08) { /* FNAME */
        while (pos < len && src[pos] != 0) pos++;
        if (pos == len) return -1;
        pos++;
    }
    if (flg & 0x10) { /* FCOMMENT */
        while (pos < len && src[pos] != 0) pos++;
        if (pos == len) return -1;
        pos++;
    }
    if (flg & 0x02) { /* FHCRC */
        if (len - pos < 2) return -1;
        uint16_t expected_header_crc =
            (uint16_t)src[pos] | ((uint16_t)src[pos + 1] << 8);
        uint16_t actual_header_crc =
            (uint16_t)(neverc_crc32_ieee(src, pos) & UINT32_C(0xffff));
        if (actual_header_crc != expected_header_crc) return -1;
        pos += 2;
    }
    /* Trailer is 8 bytes after the DEFLATE stream, not at the end of the
     * caller's buffer (which may hold further gzip members). */
    if (len - pos < 8) return -1;
    *payload_off = pos;
    return 0;
}

int neverc_gzip_decompress(const uint8_t *src, size_t src_len,
                           uint8_t *dst, size_t *dst_len) {
    if (!dst_len || (!src && src_len != 0) ||
        (!dst && *dst_len != 0))
        return -1;

    size_t in = 0;
    size_t out_pos = 0;
    size_t out_cap = *dst_len;
    int saw_member = 0;

    while (in < src_len) {
        size_t payload_off;
        if (gzip_parse_header(src + in, src_len - in, &payload_off) < 0)
            return -1;

        size_t remain_src = src_len - in - payload_off;
        size_t remain_dst = out_cap - out_pos;
        size_t produced = remain_dst;
        size_t used = 0;
        /* ISIZE is uncompressed length mod 2^32, not the inflate cap. Inflate
         * into the remaining caller buffer, then check this member's trailer. */
        if (neverc_flate_decompress_consumed(src + in + payload_off, remain_src,
                                             dst ? dst + out_pos : NULL,
                                             &produced, &used) < 0)
            return -1;
        if (used > remain_src || remain_src - used < 8)
            return -1;

        const uint8_t *tr = src + in + payload_off + used;
        uint32_t expected_crc = (uint32_t)tr[0]
                              | ((uint32_t)tr[1] << 8)
                              | ((uint32_t)tr[2] << 16)
                              | ((uint32_t)tr[3] << 24);
        uint32_t expected_size = (uint32_t)tr[4]
                               | ((uint32_t)tr[5] << 8)
                               | ((uint32_t)tr[6] << 16)
                               | ((uint32_t)tr[7] << 24);

        /* CRC 0 is a real checksum (empty members), not a skip. Hashing a
         * NULL buffer would collapse to the empty CRC and fail open. */
        if (produced > 0 && !dst) return -1;
        uint32_t actual_crc = neverc_crc32_ieee(dst ? dst + out_pos
                                                    : (const uint8_t *)"",
                                               produced);
        if (actual_crc != expected_crc) return -1;
        if ((uint32_t)(produced & 0xFFFFFFFF) != expected_size) return -1;

        out_pos += produced;
        in += payload_off + used + 8;
        saw_member = 1;
    }
    if (!saw_member) return -1;

    *dst_len = out_pos;
    return 0;
}
