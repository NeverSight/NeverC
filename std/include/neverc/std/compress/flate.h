#ifndef NEVERC_COMPRESS_FLATE_H
#define NEVERC_COMPRESS_FLATE_H

/*
 * NeverC compress/flate — DEFLATE compression (RFC 1951).
 * Mirrors Go compress/flate package.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_FLATE_NO_COMPRESSION   0
#define NEVERC_FLATE_BEST_SPEED       1
#define NEVERC_FLATE_DEFAULT          6
#define NEVERC_FLATE_BEST_COMPRESSION 9

/*
 * DEFLATE compress `src` into `dst`.
 * `level`: 0 = no compression (stored), 1-9 = compression levels.
 * Compressed levels reject inputs larger than UINT32_MAX bytes.
 * Returns 0 on success, -1 on error.
 */
int neverc_flate_compress(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t *dst_len, int level);

/*
 * DEFLATE decompress (inflate) `src` into `dst`.
 * The input must be exactly one DEFLATE stream: leftover whole bytes after
 * the final block are an error. Returns 0 on success, -1 on error.
 */
int neverc_flate_decompress(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t *dst_len);

/*
 * Like neverc_flate_decompress, but stops at the end of the DEFLATE stream
 * and writes how many source bytes were consumed to *src_consumed. Trailing
 * bytes (gzip/zlib trailer, the next gzip member) are left unconsumed.
 * *src_consumed must be non-NULL.
 */
int neverc_flate_decompress_consumed(const uint8_t *src, size_t src_len,
                                     uint8_t *dst, size_t *dst_len,
                                     size_t *src_consumed);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/compress.h>
#endif


#endif
