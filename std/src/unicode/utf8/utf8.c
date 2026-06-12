#include "neverc/std/unicode/utf8.h"

/*
 * UTF-8 encoding/decoding — mirrors Go unicode/utf8 package.
 * Pure computation, no libc dependency.
 *
 * UTF-8 encoding:
 *   U+0000..U+007F:    0xxxxxxx
 *   U+0080..U+07FF:    110xxxxx 10xxxxxx
 *   U+0800..U+FFFF:    1110xxxx 10xxxxxx 10xxxxxx
 *   U+10000..U+10FFFF: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 */

#define SURROGATE_MIN 0xD800U
#define SURROGATE_MAX 0xDFFFU
#define T1  0x00U
#define TX  0x80U
#define T2  0xC0U
#define T3  0xE0U
#define T4  0xF0U
#define MASKX 0x3FU
#define MASK2 0x1FU
#define MASK3 0x0FU
#define MASK4 0x07U
#define RUNE1MAX 0x7FU
#define RUNE2MAX 0x7FFU
#define RUNE3MAX 0xFFFFU

int neverc_utf8_valid_rune(uint32_t r) {
    if (r >= SURROGATE_MIN && r <= SURROGATE_MAX) return 0;
    if (r > NEVERC_UTF8_MAX_RUNE) return 0;
    return 1;
}

int neverc_utf8_rune_start(uint8_t b) {
    return (b & 0xC0) != 0x80;
}

int neverc_utf8_rune_len(uint32_t r) {
    if (r <= RUNE1MAX) return 1;
    if (r <= RUNE2MAX) return 2;
    if (r >= SURROGATE_MIN && r <= SURROGATE_MAX) return -1;
    if (r <= RUNE3MAX) return 3;
    if (r <= NEVERC_UTF8_MAX_RUNE) return 4;
    return -1;
}

int neverc_utf8_encode_rune(uint8_t *buf, uint32_t r) {
    if (r <= RUNE1MAX) {
        buf[0] = (uint8_t)r;
        return 1;
    }
    if (r <= RUNE2MAX) {
        buf[0] = (uint8_t)(T2 | (r >> 6));
        buf[1] = (uint8_t)(TX | (r & MASKX));
        return 2;
    }
    if (r >= SURROGATE_MIN && r <= SURROGATE_MAX) {
        r = NEVERC_UTF8_RUNE_ERROR;
    }
    if (r <= RUNE3MAX) {
        buf[0] = (uint8_t)(T3 | (r >> 12));
        buf[1] = (uint8_t)(TX | ((r >> 6) & MASKX));
        buf[2] = (uint8_t)(TX | (r & MASKX));
        return 3;
    }
    if (r <= NEVERC_UTF8_MAX_RUNE) {
        buf[0] = (uint8_t)(T4 | (r >> 18));
        buf[1] = (uint8_t)(TX | ((r >> 12) & MASKX));
        buf[2] = (uint8_t)(TX | ((r >> 6) & MASKX));
        buf[3] = (uint8_t)(TX | (r & MASKX));
        return 4;
    }
    buf[0] = (uint8_t)(T3 | (NEVERC_UTF8_RUNE_ERROR >> 12));
    buf[1] = (uint8_t)(TX | ((NEVERC_UTF8_RUNE_ERROR >> 6) & MASKX));
    buf[2] = (uint8_t)(TX | (NEVERC_UTF8_RUNE_ERROR & MASKX));
    return 3;
}

void neverc_utf8_decode_rune(const uint8_t *buf, size_t len,
                              uint32_t *r, int *size) {
    if (len == 0) {
        *r = NEVERC_UTF8_RUNE_ERROR;
        *size = 0;
        return;
    }

    uint8_t b0 = buf[0];
    if (b0 < TX) {
        *r = (uint32_t)b0;
        *size = 1;
        return;
    }

    int n;
    uint32_t accept_lo, accept_hi;
    uint32_t rr;

    if (b0 < T3) {
        n = 2; rr = (uint32_t)(b0 & MASK2);
        accept_lo = 0x80; accept_hi = 0xBF;
        if (b0 < 0xC2) goto bad;
    } else if (b0 < T4) {
        n = 3; rr = (uint32_t)(b0 & MASK3);
        accept_lo = (b0 == 0xE0) ? 0xA0 : 0x80;
        accept_hi = (b0 == 0xED) ? 0x9F : 0xBF;
    } else if (b0 < 0xF5) {
        n = 4; rr = (uint32_t)(b0 & MASK4);
        accept_lo = (b0 == 0xF0) ? 0x90 : 0x80;
        accept_hi = (b0 == 0xF4) ? 0x8F : 0xBF;
    } else {
        goto bad;
    }

    if ((size_t)n > len) goto bad;

    uint8_t b1 = buf[1];
    if (b1 < accept_lo || b1 > accept_hi) goto bad;
    rr = (rr << 6) | (uint32_t)(b1 & MASKX);

    if (n == 2) {
        *r = rr; *size = 2; return;
    }

    uint8_t b2 = buf[2];
    if (b2 < 0x80 || b2 > 0xBF) goto bad;
    rr = (rr << 6) | (uint32_t)(b2 & MASKX);

    if (n == 3) {
        *r = rr; *size = 3; return;
    }

    uint8_t b3 = buf[3];
    if (b3 < 0x80 || b3 > 0xBF) goto bad;
    rr = (rr << 6) | (uint32_t)(b3 & MASKX);

    *r = rr; *size = 4; return;

bad:
    *r = NEVERC_UTF8_RUNE_ERROR;
    *size = 1;
}

/*
 * Word-at-a-time ASCII fast path for rune_count and valid.
 * Skips 8 pure-ASCII bytes per iteration (~8x faster on ASCII text).
 * High-bit check: if (word & 0x8080808080808080) == 0, all 8 bytes are ASCII.
 */

#include <string.h>

#define NCI_ASCII_MASK ((uint64_t)0x8080808080808080ULL)

size_t neverc_utf8_rune_count(const uint8_t *buf, size_t len) {
    size_t count = 0;
    size_t i = 0;

    while (i < len) {
        if (buf[i] < TX) {
            while (i + 8 <= len) {
                uint64_t w;
                memcpy(&w, buf + i, 8);
                if ((w & NCI_ASCII_MASK) != 0) break;
                count += 8;
                i += 8;
            }
            while (i < len && buf[i] < TX) { i++; count++; }
            continue;
        }
        uint32_t r; int sz;
        neverc_utf8_decode_rune(buf + i, len - i, &r, &sz);
        i += (size_t)(sz > 0 ? sz : 1);
        count++;
    }
    return count;
}

int neverc_utf8_valid(const uint8_t *buf, size_t len) {
    size_t i = 0;

    while (i < len) {
        if (buf[i] < TX) {
            while (i + 8 <= len) {
                uint64_t w;
                memcpy(&w, buf + i, 8);
                if ((w & NCI_ASCII_MASK) != 0) break;
                i += 8;
            }
            while (i < len && buf[i] < TX) i++;
            continue;
        }
        uint32_t r; int sz;
        neverc_utf8_decode_rune(buf + i, len - i, &r, &sz);
        if (r == NEVERC_UTF8_RUNE_ERROR && sz <= 1) return 0;
        i += (size_t)sz;
    }
    return 1;
}
