#include "neverc/std/unicode/utf16.h"
#include <stddef.h>
#include <stdint.h>

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

size_t neverc_utf16_encode(const int32_t *src, size_t nsrc,
                           uint16_t *dst, size_t ndst) {
    size_t need = 0;
    if (nsrc > 0 && !src) return 0;
    for (size_t i = 0; i < nsrc; i++) {
        int n = neverc_utf16_rune_len(src[i]);
        if (n < 0) n = 1; /* U+FFFD */
        if (need > SIZE_MAX - (size_t)n) return SIZE_MAX;
        need += (size_t)n;
    }
    if (!dst) return need;
    size_t o = 0;
    for (size_t i = 0; i < nsrc && o < ndst; i++) {
        int n = neverc_utf16_rune_len(src[i]);
        if (n == 1) {
            dst[o++] = (uint16_t)src[i];
        } else if (n == 2) {
            if (ndst - o < 2) break; /* do not emit a lone high surrogate */
            int32_t r1, r2;
            neverc_utf16_encode_rune(src[i], &r1, &r2);
            dst[o++] = (uint16_t)r1;
            dst[o++] = (uint16_t)r2;
        } else {
            dst[o++] = (uint16_t)NEVERC_UTF16_REPLACEMENT_CHAR;
        }
    }
    return need;
}

size_t neverc_utf16_decode(const uint16_t *src, size_t nsrc,
                           int32_t *dst, size_t ndst) {
    size_t need = 0;
    if (nsrc > 0 && !src) return 0;
    for (size_t i = 0; i < nsrc; i++) {
        uint16_t r = src[i];
        if (SURR1 <= r && r < SURR2 && i + 1 < nsrc &&
            SURR2 <= src[i + 1] && src[i + 1] < SURR3)
            i++;
        if (need == SIZE_MAX) return SIZE_MAX;
        need++;
    }
    if (!dst) return need;
    size_t o = 0;
    for (size_t i = 0; i < nsrc && o < ndst; i++) {
        uint16_t r = src[i];
        if (r < SURR1 || r >= SURR3) {
            dst[o++] = (int32_t)r;
        } else if (SURR1 <= r && r < SURR2 && i + 1 < nsrc &&
                   SURR2 <= src[i + 1] && src[i + 1] < SURR3) {
            dst[o++] = neverc_utf16_decode_rune((int32_t)r, (int32_t)src[i + 1]);
            i++;
        } else {
            dst[o++] = NEVERC_UTF16_REPLACEMENT_CHAR;
        }
    }
    return need;
}
