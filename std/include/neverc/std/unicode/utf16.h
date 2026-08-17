#ifndef NEVERC_UTF16_H
#define NEVERC_UTF16_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_UTF16_REPLACEMENT_CHAR  0xFFFD
#define NEVERC_UTF16_MAX_RUNE          0x10FFFF
#define NEVERC_UTF16_SURR_SELF         0x10000

int neverc_utf16_is_surrogate(int32_t r);
int32_t neverc_utf16_decode_rune(int32_t r1, int32_t r2);
void neverc_utf16_encode_rune(int32_t r, int32_t *r1, int32_t *r2);
int neverc_utf16_rune_len(int32_t r);

/* Encode runes to UTF-16. Invalid runes and lone surrogates become U+FFFD.
 * If dst is NULL, only the required unit count is returned. */
size_t neverc_utf16_encode(const int32_t *src, size_t nsrc,
                           uint16_t *dst, size_t ndst);

/* Decode UTF-16 to runes. Unpaired surrogates become U+FFFD.
 * If dst is NULL, only the required rune count is returned. */
size_t neverc_utf16_decode(const uint16_t *src, size_t nsrc,
                           int32_t *dst, size_t ndst);

#ifdef __cplusplus
}
#endif



/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/unicode.h>
#endif


#endif /* NEVERC_UTF16_H */
