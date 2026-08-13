#ifndef NEVERC_HEX_H
#define NEVERC_HEX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hexadecimal encoding/decoding (mirrors Go encoding/hex package).
 *
 * neverc_hex_encode  — binary to lowercase hex string
 * neverc_hex_decode  — hex string to binary
 *
 * Callers are responsible for providing sufficiently sized dst buffers.
 * Use neverc_hex_encoded_len / neverc_hex_decoded_len to compute sizes.
 * encoded_len returns SIZE_MAX when the result is not representable. Encode
 * writes exactly the returned payload bytes and does not append a NUL byte.
 */

size_t neverc_hex_encoded_len(size_t n);
size_t neverc_hex_decoded_len(size_t n);

size_t neverc_hex_encode(char *dst, const uint8_t *src, size_t src_len);
int    neverc_hex_decode(uint8_t *dst, const char *src, size_t src_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif

#endif /* NEVERC_HEX_H */
