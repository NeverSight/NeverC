#ifndef NEVERC_COMPRESS_LZW_H
#define NEVERC_COMPRESS_LZW_H

/*
 * NeverC compress/lzw — LZW compression (mirrors Go compress/lzw package).
 * Supports GIF-style (LSB), standard late-change MSB, and TIFF's distinct
 * early-change MSB profile. Variable-width codes are limited to 12 bits.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_LZW_LSB 0
#define NEVERC_LZW_MSB 1
#define NEVERC_LZW_TIFF_MSB 2

/*
 * Compress `src` (src_len bytes) into `dst` (at most *dst_len bytes).
 * On success returns 0 and sets *dst_len to actual compressed size.
 * `order` selects both bit packing and the code-width transition profile:
 *   NEVERC_LZW_LSB      — LSB-first, late-change (GIF)
 *   NEVERC_LZW_MSB      — MSB-first, late-change (Go compress/lzw semantics)
 *   NEVERC_LZW_TIFF_MSB — MSB-first, TIFF/Aldus early-change semantics
 * `lit_width` is the literal code width in bits [2..8], typically 8.
 * NEVERC_LZW_TIFF_MSB requires lit_width == 8, as required by TIFF LZW.
 * NEVERC_LZW_MSB is intentionally not an alias for TIFF: changing its
 * long-standing late-change behavior would silently break existing streams.
 * Returns -1 on error (output buffer too small, bad params).
 */
int neverc_lzw_compress(const uint8_t *src, size_t src_len,
                        uint8_t *dst, size_t *dst_len,
                        int order, int lit_width);

/*
 * Decompress LZW-compressed `src` into `dst` using the same `order` profile
 * and `lit_width` rules as neverc_lzw_compress.
 * On success returns 0 and sets *dst_len to decompressed size.
 * Returns -1 on error.
 */
int neverc_lzw_decompress(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t *dst_len,
                          int order, int lit_width);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/compress.h>
#endif


#endif
