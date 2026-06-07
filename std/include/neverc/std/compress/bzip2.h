#ifndef NEVERC_COMPRESS_BZIP2_H
#define NEVERC_COMPRESS_BZIP2_H

/*
 * NeverC compress/bzip2 — bzip2 decompression.
 * Mirrors Go compress/bzip2 package (decompression only).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int neverc_bzip2_decompress(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t *dst_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/compress.h>
#endif


#endif
