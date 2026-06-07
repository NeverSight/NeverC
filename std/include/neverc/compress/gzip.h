#ifndef NEVERC_COMPRESS_GZIP_H
#define NEVERC_COMPRESS_GZIP_H

/*
 * NeverC compress/gzip — gzip format (RFC 1952).
 * Mirrors Go compress/gzip package.
 * Uses DEFLATE + CRC32.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int neverc_gzip_compress(const uint8_t *src, size_t src_len,
                         uint8_t *dst, size_t *dst_len, int level);

int neverc_gzip_decompress(const uint8_t *src, size_t src_len,
                           uint8_t *dst, size_t *dst_len);

#ifdef __cplusplus
}
#endif

#endif
