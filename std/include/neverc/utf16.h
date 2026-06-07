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

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
struct __neverc_std_utf16_t { char __tag; };
extern struct __neverc_std_utf16_t utf16;
#endif

#endif /* NEVERC_UTF16_H */
