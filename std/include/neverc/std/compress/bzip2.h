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

/* Decompresses one bzip2 stream and verifies every block CRC and the combined
 * stream CRC. Returns 0 on success and -1 for malformed or corrupt input.
 * Bytes after the stream's final padding are rejected; use
 * neverc_bzip2_decompress_multistream for concatenated streams. */
int neverc_bzip2_decompress(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t *dst_len);

/* Like neverc_bzip2_decompress, but `src` may hold several bzip2 streams
 * concatenated back to back, as Go compress/bzip2 reads them (for example
 * files joined end to end, or the output of parallel compressors). The
 * streams' outputs are written to `dst` in order and *dst_len receives their
 * total length. Each stream may declare its own block size, and every block
 * CRC and each stream's combined CRC are verified. After a stream ends, the
 * input must either end or start another complete stream: any other trailing
 * bytes, including a single stray byte or zero padding, make the call return
 * -1, matching the error Go reports for them. */
int neverc_bzip2_decompress_multistream(const uint8_t *src, size_t src_len,
                                        uint8_t *dst, size_t *dst_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/compress.h>
#endif


#endif
