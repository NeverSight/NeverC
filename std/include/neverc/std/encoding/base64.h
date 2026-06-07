#ifndef NEVERC_BASE64_H
#define NEVERC_BASE64_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Base64 encoding/decoding (mirrors Go encoding/base64 package).
 * Supports standard (RFC 4648) and URL-safe alphabets.
 */

size_t neverc_base64_encoded_len(size_t n);
size_t neverc_base64_decoded_len(size_t n);

size_t neverc_base64_encode(char *dst, const uint8_t *src, size_t src_len);
int    neverc_base64_decode(uint8_t *dst, const char *src, size_t src_len);

size_t neverc_base64_url_encode(char *dst, const uint8_t *src, size_t src_len);
int    neverc_base64_url_decode(uint8_t *dst, const char *src, size_t src_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif

#endif /* NEVERC_BASE64_H */
