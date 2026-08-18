#ifndef NEVERC_MIME_RFC2047_SAFE_H
#define NEVERC_MIME_RFC2047_SAFE_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Internal helper: a header token is unsafe if RFC 2047 decoding it yields
 * C0/DEL, or if a utf-8 encoded-word is not well-formed UTF-8 (overlong
 * CR/LF and surrogate halves bypass a raw-byte CTL check).
 *
 * Lives in a header so mail/multipart can fail-closed without linking mime.c
 * (StdLibTests compiles those TUs alone).
 */

static int nci_2047_hex(unsigned char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int)(c - 'A' + 10);
    return -1;
}

static int nci_2047_is_ctl(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = s[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f)
            return 1;
    }
    return 0;
}

static int nci_2047_is_break_cp(uint32_t cp) {
    return cp == 0x85 || cp == 0x2028 || cp == 0x2029;
}

/* Header injection: C0/DEL, plus Unicode line breaks that C0 checks miss.
 * cs: 1=utf-8, 2=us-ascii, 3=iso-8859-1, 0=unknown. */
static int nci_2047_has_header_break(const unsigned char *s, size_t n, int cs) {
    size_t i;
    if (nci_2047_is_ctl(s, n))
        return 1;
    if (cs == 3) {
        for (i = 0; i < n; i++)
            if (s[i] == 0x85)
                return 1;
        return 0;
    }
    if (cs != 1)
        return 0;
    i = 0;
    while (i < n) {
        unsigned char b0 = s[i];
        uint32_t cp;
        size_t need;
        if (b0 < 0x80) {
            i++;
            continue;
        }
        if (b0 < 0xC2) {
            i++;
            continue;
        }
        if (b0 < 0xE0) {
            need = 2;
            cp = (uint32_t)(b0 & 0x1f);
        } else if (b0 < 0xF0) {
            need = 3;
            cp = (uint32_t)(b0 & 0x0f);
        } else if (b0 < 0xF5) {
            need = 4;
            cp = (uint32_t)(b0 & 0x07);
        } else {
            i++;
            continue;
        }
        if (i + need > n) {
            i++;
            continue;
        }
        int bad = 0;
        for (size_t k = 1; k < need; k++) {
            if (s[i + k] < 0x80 || s[i + k] > 0xBF) {
                bad = 1;
                break;
            }
            cp = (cp << 6) | (uint32_t)(s[i + k] & 0x3f);
        }
        if (bad) {
            i++;
            continue;
        }
        if (nci_2047_is_break_cp(cp))
            return 1;
        i += need;
    }
    return 0;
}

/* Same accept ranges as neverc_utf8_decode_rune / RFC 3629. */
static int nci_2047_utf8_ok(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char b0 = s[i];
        if (b0 < 0x80) {
            i++;
            continue;
        }
        size_t need;
        unsigned char lo, hi;
        if (b0 < 0xC2)
            return 0;
        if (b0 < 0xE0) {
            need = 2;
            lo = 0x80;
            hi = 0xBF;
        } else if (b0 < 0xF0) {
            need = 3;
            lo = (unsigned char)((b0 == 0xE0) ? 0xA0 : 0x80);
            hi = (unsigned char)((b0 == 0xED) ? 0x9F : 0xBF);
        } else if (b0 < 0xF5) {
            need = 4;
            lo = (unsigned char)((b0 == 0xF0) ? 0x90 : 0x80);
            hi = (unsigned char)((b0 == 0xF4) ? 0x8F : 0xBF);
        } else {
            return 0;
        }
        if (i + need > n)
            return 0;
        if (s[i + 1] < lo || s[i + 1] > hi)
            return 0;
        for (size_t k = 2; k < need; k++) {
            if (s[i + k] < 0x80 || s[i + k] > 0xBF)
                return 0;
        }
        i += need;
    }
    return 1;
}

static int nci_2047_span_ieq(const char *a, size_t alen,
                             const char *b, size_t blen) {
    if (alen != blen)
        return 0;
    for (size_t i = 0; i < alen; i++) {
        int ca = (unsigned char)a[i];
        int cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return 0;
    }
    return 1;
}

static int nci_2047_charset(const char *s, size_t n) {
    if (nci_2047_span_ieq(s, n, "utf-8", 5))
        return 1;
    if (nci_2047_span_ieq(s, n, "us-ascii", 8))
        return 2;
    if (nci_2047_span_ieq(s, n, "iso-8859-1", 10))
        return 3;
    return 0;
}

static const char *nci_2047_find(const char *s, size_t n,
                                 const char *nd, size_t nn) {
    if (nn == 0 || n < nn)
        return NULL;
    for (size_t i = 0; i + nn <= n; i++) {
        if (memcmp(s + i, nd, nn) == 0)
            return s + i;
    }
    return NULL;
}

/* 0 ok, -1 invalid, -2 output cap exceeded. */
static int nci_2047_q_decode(const char *s, size_t n,
                             unsigned char *out, size_t cap, size_t *olen) {
    size_t di = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '_') {
            c = ' ';
        } else if (c == '=') {
            if (i + 2 >= n)
                return -1;
            int hi = nci_2047_hex((unsigned char)s[i + 1]);
            int lo = nci_2047_hex((unsigned char)s[i + 2]);
            if (hi < 0 || lo < 0)
                return -1;
            c = (unsigned char)((hi << 4) | lo);
            i += 2;
        } else if (c > 126 || (c < 32 && c != '\t')) {
            return -1;
        }
        if (di >= cap)
            return -2;
        out[di++] = c;
    }
    *olen = di;
    return 0;
}

static int nci_2047_b64(unsigned char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static int nci_2047_b_decode(const char *s, size_t n,
                             unsigned char *out, size_t cap, size_t *olen) {
    if (n % 4 != 0)
        return -1;
    size_t di = 0;
    for (size_t i = 0; i < n; i += 4) {
        int last = (i + 4 >= n);
        if (!last && (s[i + 2] == '=' || s[i + 3] == '='))
            return -1;
        if (s[i + 2] == '=' && s[i + 3] != '=')
            return -1;
        int v0 = nci_2047_b64((unsigned char)s[i]);
        int v1 = nci_2047_b64((unsigned char)s[i + 1]);
        if (v0 < 0 || v1 < 0)
            return -1;
        int v2 = 0, v3 = 0;
        if (s[i + 2] != '=') {
            v2 = nci_2047_b64((unsigned char)s[i + 2]);
            if (v2 < 0)
                return -1;
        }
        if (s[i + 3] != '=') {
            v3 = nci_2047_b64((unsigned char)s[i + 3]);
            if (v3 < 0)
                return -1;
        }
        if (di >= cap)
            return -2;
        out[di++] = (unsigned char)((v0 << 2) | (v1 >> 4));
        if (s[i + 2] != '=') {
            if (di >= cap)
                return -2;
            out[di++] = (unsigned char)((v1 << 4) | (v2 >> 2));
        }
        if (s[i + 3] != '=') {
            if (di >= cap)
                return -2;
            out[di++] = (unsigned char)((v2 << 6) | v3);
        }
    }
    *olen = di;
    return 0;
}

/* 1 if s[0..n) is safe as unstructured header text. */
static int nci_rfc2047_header_safe(const char *s, size_t n) {
    if (!s)
        return n == 0;
    if (nci_2047_has_header_break((const unsigned char *)s, n, 1) ||
        nci_2047_has_header_break((const unsigned char *)s, n, 3))
        return 0;

    const char *p = s;
    size_t left = n;
    while (left >= 2) {
        const char *mark = nci_2047_find(p, left, "=?", 2);
        if (!mark)
            break;
        const char *cur = mark + 2;
        size_t after = left - (size_t)(cur - p);
        const char *q1 = nci_2047_find(cur, after, "?", 1);
        if (!q1) {
            left -= (size_t)(mark + 1 - p);
            p = mark + 1;
            continue;
        }
        const char *charset = cur;
        size_t clen = (size_t)(q1 - cur);
        cur = q1 + 1;
        after = left - (size_t)(cur - p);
        if (after < 4) {
            left -= (size_t)(mark + 1 - p);
            p = mark + 1;
            continue;
        }
        unsigned char enc = (unsigned char)*cur++;
        if (*cur != '?') {
            left -= (size_t)(mark + 1 - p);
            p = mark + 1;
            continue;
        }
        cur++;
        after = left - (size_t)(cur - p);
        const char *qe = nci_2047_find(cur, after, "?=", 2);
        if (!qe) {
            left -= (size_t)(mark + 1 - p);
            p = mark + 1;
            continue;
        }
        const char *text = cur;
        size_t tlen = (size_t)(qe - cur);
        const char *end = qe + 2;

        unsigned char stack[256];
        unsigned char *dec = stack;
        unsigned char *heap = NULL;
        size_t cap = sizeof(stack);
        if (tlen > cap) {
            if (tlen == SIZE_MAX)
                return 0;
            heap = (unsigned char *)malloc(tlen + 1);
            if (!heap)
                return 0;
            dec = heap;
            cap = tlen + 1;
        }

        size_t dlen = 0;
        int rc = 1;
        if (enc == 'Q' || enc == 'q')
            rc = nci_2047_q_decode(text, tlen, dec, cap, &dlen);
        else if (enc == 'B' || enc == 'b')
            rc = nci_2047_b_decode(text, tlen, dec, cap, &dlen);

        int cs = (rc == 0) ? nci_2047_charset(charset, clen) : 0;
        if (rc == -2 ||
            (rc == 0 && (!cs || nci_2047_has_header_break(dec, dlen, cs) ||
                         (cs == 1 && !nci_2047_utf8_ok(dec, dlen))))) {
            free(heap);
            return 0;
        }
        free(heap);

        left -= (size_t)(end - p);
        p = end;
    }
    return 1;
}

#endif /* NEVERC_MIME_RFC2047_SAFE_H */
