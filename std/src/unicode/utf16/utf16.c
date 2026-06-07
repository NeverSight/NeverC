#include "neverc/unicode/utf16.h"

/*
 * UTF-16 encoding/decoding — ported from Go unicode/utf16.
 * Implements surrogate pair handling per Unicode Standard.
 */

#define SURR1 0xD800
#define SURR2 0xDC00
#define SURR3 0xE000

int neverc_utf16_is_surrogate(int32_t r) {
    return SURR1 <= r && r < SURR3;
}

int32_t neverc_utf16_decode_rune(int32_t r1, int32_t r2) {
    if (SURR1 <= r1 && r1 < SURR2 && SURR2 <= r2 && r2 < SURR3)
        return (((r1 - SURR1) << 10) | (r2 - SURR2)) + NEVERC_UTF16_SURR_SELF;
    return NEVERC_UTF16_REPLACEMENT_CHAR;
}

void neverc_utf16_encode_rune(int32_t r, int32_t *r1, int32_t *r2) {
    if (r < NEVERC_UTF16_SURR_SELF || r > NEVERC_UTF16_MAX_RUNE) {
        *r1 = NEVERC_UTF16_REPLACEMENT_CHAR;
        *r2 = NEVERC_UTF16_REPLACEMENT_CHAR;
        return;
    }
    r -= NEVERC_UTF16_SURR_SELF;
    *r1 = SURR1 + ((r >> 10) & 0x3FF);
    *r2 = SURR2 + (r & 0x3FF);
}

int neverc_utf16_rune_len(int32_t r) {
    if ((0 <= r && r < SURR1) || (SURR3 <= r && r < NEVERC_UTF16_SURR_SELF))
        return 1;
    if (NEVERC_UTF16_SURR_SELF <= r && r <= NEVERC_UTF16_MAX_RUNE)
        return 2;
    return -1;
}
