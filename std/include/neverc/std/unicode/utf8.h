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
   On error: *r = RUNE_ERROR, *size = 1. */
void neverc_utf8_decode_rune(const uint8_t *buf, size_t len,
                              uint32_t *r, int *size);

/* Returns the number of runes in the UTF-8 byte string buf[0..len). */
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


#endif /* NEVERC_UTF8_H */
