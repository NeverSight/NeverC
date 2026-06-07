#ifndef NEVERC_COMPRESS_ZLIB_H
#define NEVERC_COMPRESS_ZLIB_H

/*
 * NeverC compress/zlib — zlib format (RFC 1950).
 * Mirrors Go compress/zlib package.
 * Uses DEFLATE + Adler-32.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int neverc_zlib_compress(const uint8_t *src, size_t src_len,
                         uint8_t *dst, size_t *dst_len, int level);

int neverc_zlib_decompress(const uint8_t *src, size_t src_len,
                           uint8_t *dst, size_t *dst_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/compress.h>
#endif


#endif
