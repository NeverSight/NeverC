/*
 * NeverC compress/zlib — zlib format (RFC 1950).
 * Wraps DEFLATE with zlib header (2 bytes) and Adler-32 checksum (4 bytes).
 */

#include "neverc/std/compress/zlib.h"
#include "neverc/std/compress/flate.h"
#include "neverc/std/hash/adler32.h"
#include <string.h>

int neverc_zlib_compress(const uint8_t *src, size_t src_len,
                         uint8_t *dst, size_t *dst_len, int level) {
    if (!dst_len || (!src && src_len != 0) ||
        (!dst && *dst_len != 0))
        return -1;
    if (*dst_len < 6) return -1;

    /* zlib header: CMF + FLG */
    uint8_t cmf = 0x78; /* CM=8 (deflate), CINFO=7 (32K window) */
    uint8_t flg;
    if (level <= 1) flg = 0x01;
    else if (level <= 5) flg = 0x5E;
    else if (level <= 6) flg = 0x9C;
    else flg = 0xDA;

    /* adjust FLG so (CMF*256 + FLG) % 31 == 0 */
    unsigned check = ((unsigned)cmf * 256 + (unsigned)flg) % 31;
    if (check != 0) flg += (uint8_t)(31 - check);

    dst[0] = cmf;
    dst[1] = flg;

    /* DEFLATE payload */
    size_t payload_cap = *dst_len - 6;
    size_t payload_len = payload_cap;
    if (neverc_flate_compress(src, src_len, dst + 2, &payload_len, level) < 0)
        return -1;

    /* Adler-32 checksum (big-endian) */
    size_t ck_pos = 2 + payload_len;
    if (ck_pos + 4 > *dst_len) return -1;
    uint32_t adler = neverc_adler32_checksum(src, src_len);
    dst[ck_pos + 0] = (uint8_t)(adler >> 24);
    dst[ck_pos + 1] = (uint8_t)(adler >> 16);
    dst[ck_pos + 2] = (uint8_t)(adler >> 8);
    dst[ck_pos + 3] = (uint8_t)(adler);

    *dst_len = ck_pos + 4;
    return 0;
}

int neverc_zlib_decompress(const uint8_t *src, size_t src_len,
                           uint8_t *dst, size_t *dst_len) {
    if (!dst_len || (!src && src_len != 0) ||
        (!dst && *dst_len != 0))
        return -1;
    if (src_len < 6) return -1;

    /* validate header */
    uint8_t cmf = src[0], flg = src[1];
    if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7) return -1;
    if (((unsigned)cmf * 256 + (unsigned)flg) % 31 != 0) return -1;
    if (flg & 0x20) return -1; /* FDICT not supported */

    /* Adler-32 follows the DEFLATE stream immediately. Do not treat the last
     * 4 bytes of the caller's buffer as the checksum — that is the gzip ISIZE
     * mistake: a wrapper trailer is not an inflate cap or an end-of-buffer
     * marker when extra bytes may follow the payload. */
    size_t out_len = *dst_len;
    size_t used = 0;
    if (neverc_flate_decompress_consumed(src + 2, src_len - 2, dst, &out_len,
                                         &used) < 0)
        return -1;
    if (used > src_len - 2 || (src_len - 2) - used < 4) return -1;
    if (2 + used + 4 != src_len) return -1; /* trailing junk */

    const uint8_t *adler = src + 2 + used;
    uint32_t expected = ((uint32_t)adler[0] << 24)
                      | ((uint32_t)adler[1] << 16)
                      | ((uint32_t)adler[2] << 8)
                      | (uint32_t)adler[3];

    uint32_t actual = neverc_adler32_checksum(dst, out_len);
    if (actual != expected) return -1;

    *dst_len = out_len;
    return 0;
}
