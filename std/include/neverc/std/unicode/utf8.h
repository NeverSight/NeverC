#ifndef NEVERC_UTF8_H
#define NEVERC_UTF8_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_UTF8_RUNE_ERROR  0xFFFD
#define NEVERC_UTF8_MAX_RUNE    0x10FFFF
#define NEVERC_UTF8_RUNE_SELF   0x80
#define NEVERC_UTF8_UTF_MAX     4

/* Returns how many bytes are needed to encode rune r. Returns -1 if invalid. */
int neverc_utf8_rune_len(uint32_t r);

/* Encodes rune r into buf (must have >= 4 bytes). Returns bytes written. */
int neverc_utf8_encode_rune(uint8_t *buf, uint32_t r);

/* Decodes one rune from buf[0..len). Returns rune in *r, byte count in *size.
   On error: *r = RUNE_ERROR, *size = 1 (or 0 if len == 0). */
void neverc_utf8_decode_rune(const uint8_t *buf, size_t len,
                              uint32_t *r, int *size);

/* Decodes the last rune in buf[0..len). Same error contract as decode_rune.
   Incomplete or invalid tails yield *size = 1 (the last byte). */
void neverc_utf8_decode_last_rune(const uint8_t *buf, size_t len,
                                  uint32_t *r, int *size);

/* Returns 1 if buf begins with a complete encoding. Invalid encodings that
   convert as a width-1 error rune are complete; truncated sequences are not. */
int neverc_utf8_full_rune(const uint8_t *buf, size_t len);

/* Returns the number of runes in the UTF-8 byte string buf[0..len).
   Invalid and short encodings count as one rune of width 1. */
size_t neverc_utf8_rune_count(const uint8_t *buf, size_t len);

/* Returns 1 if buf[0..len) is valid UTF-8, 0 otherwise. */
int neverc_utf8_valid(const uint8_t *buf, size_t len);

/* Returns 1 if byte b is the first byte of a UTF-8 encoding (not a continuation). */
int neverc_utf8_rune_start(uint8_t b);

/* Returns 1 if r is a valid Unicode code point, 0 otherwise. */
int neverc_utf8_valid_rune(uint32_t r);

#ifdef __cplusplus
}
#endif



/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/unicode.h>
#endif


#endif /* NEVERC_UTF8_H */
