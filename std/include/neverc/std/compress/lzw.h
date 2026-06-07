#ifndef NEVERC_COMPRESS_LZW_H
#define NEVERC_COMPRESS_LZW_H

/*
 * NeverC compress/lzw — LZW compression (mirrors Go compress/lzw package).
 * Supports GIF-style (LSB) and TIFF/PDF-style (MSB) bit ordering.
 * Variable-width codes up to 12 bits.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_LZW_LSB 0
#define NEVERC_LZW_MSB 1

/*
 * Compress `src` (src_len bytes) into `dst` (at most *dst_len bytes).
 * On success returns 0 and sets *dst_len to actual compressed size.
 * `order` is NEVERC_LZW_LSB or NEVERC_LZW_MSB.
 * `lit_width` is literal code width in bits [2..8], typically 8.
 * Returns -1 on error (output buffer too small, bad params).
 */
int neverc_lzw_compress(const uint8_t *src, size_t src_len,
                        uint8_t *dst, size_t *dst_len,
                        int order, int lit_width);

/*
 * Decompress LZW-compressed `src` into `dst`.
 * On success returns 0 and sets *dst_len to decompressed size.
 * Returns -1 on error.
 */
int neverc_lzw_decompress(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t *dst_len,
                          int order, int lit_width);

#ifdef __cplusplus
}
#endif

#endif
