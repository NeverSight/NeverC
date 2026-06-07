#ifndef NEVERC_BASE32_H
#define NEVERC_BASE32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Base32 encoding/decoding (RFC 4648).
 * Supports standard (A-Z2-7) and hex (0-9A-V) alphabets.
 */

size_t neverc_base32_encoded_len(size_t n);
size_t neverc_base32_decoded_len(size_t n);

size_t neverc_base32_encode(char *dst, const uint8_t *src, size_t src_len);
int    neverc_base32_decode(uint8_t *dst, const char *src, size_t src_len);

size_t neverc_base32_hex_encode(char *dst, const uint8_t *src, size_t src_len);
int    neverc_base32_hex_decode(uint8_t *dst, const char *src, size_t src_len);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif

#endif /* NEVERC_BASE32_H */
