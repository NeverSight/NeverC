#ifndef NEVERC_NET_IDNA_INC_H
#define NEVERC_NET_IDNA_INC_H

/*
 * RFC 3492 Punycode + RFC 5890 ToASCII for a single DNS name.
 * Used by url host parsing and DNS lookups. ASCII names are copied as-is
 * (matching Go idnaASCII); non-ASCII labels become xn-- A-labels.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int neverc_idna_utf8_next(const unsigned char **p,
                                 const unsigned char *end, uint32_t *rune) {
    if (*p >= end)
        return -1;
    unsigned char b0 = **p;
    if (b0 < 0x80) {
        *rune = b0;
        (*p)++;
        return 0;
    }
    int n;
    uint32_t rr, lo, hi;
    if (b0 < 0xE0) {
        n = 2;
        rr = (uint32_t)(b0 & 0x1F);
        lo = 0x80;
        hi = 0xBF;
        if (b0 < 0xC2)
            return -1;
    } else if (b0 < 0xF0) {
        n = 3;
        rr = (uint32_t)(b0 & 0x0F);
        lo = (b0 == 0xE0) ? 0xA0 : 0x80;
        hi = (b0 == 0xED) ? 0x9F : 0xBF;
    } else if (b0 < 0xF5) {
        n = 4;
        rr = (uint32_t)(b0 & 0x07);
        lo = (b0 == 0xF0) ? 0x90 : 0x80;
        hi = (b0 == 0xF4) ? 0x8F : 0xBF;
    } else {
        return -1;
    }
    if (*p + n > end)
        return -1;
    unsigned char b1 = (*p)[1];
    if (b1 < lo || b1 > hi)
        return -1;
    rr = (rr << 6) | (uint32_t)(b1 & 0x3F);
    if (n >= 3) {
        unsigned char b2 = (*p)[2];
        if (b2 < 0x80 || b2 > 0xBF)
            return -1;
        rr = (rr << 6) | (uint32_t)(b2 & 0x3F);
    }
    if (n == 4) {
        unsigned char b3 = (*p)[3];
        if (b3 < 0x80 || b3 > 0xBF)
            return -1;
        rr = (rr << 6) | (uint32_t)(b3 & 0x3F);
    }
    if (rr > 0x10FFFF)
        return -1;
    *rune = rr;
    *p += n;
    return 0;
}

static int neverc_idna_puny_adapt(unsigned delta, unsigned numpoints,
                                  int first) {
    delta = first ? delta / 700u : delta / 2u;
    delta += delta / numpoints;
    unsigned k = 0;
    while (delta > ((36u - 1u) * 26u) / 2u) {
        delta /= (36u - 1u);
        k += 36u;
    }
    return (int)(k + ((36u - 1u + 1u) * delta) / (delta + 38u));
}

static char neverc_idna_puny_digit(int d) {
    return (d < 26) ? (char)('a' + d) : (char)('0' + (d - 26));
}

static int neverc_idna_puny_encode(const uint32_t *cps, int n, char *out,
                                   size_t cap, size_t *used) {
    int b = 0;
    for (int i = 0; i < n; i++)
        if (cps[i] < 0x80)
            b++;

    size_t pos = *used;
    for (int i = 0; i < n; i++) {
        if (cps[i] >= 0x80)
            continue;
        if (pos + 1 >= cap)
            return -1;
        out[pos++] = (char)cps[i];
    }
    if (b > 0) {
        if (pos + 1 >= cap)
            return -1;
        out[pos++] = '-';
    }

    unsigned nbase = 128;
    unsigned delta = 0;
    int bias = 72;
    int h = b;
    while (h < n) {
        uint32_t m = 0xFFFFFFFFu;
        for (int i = 0; i < n; i++)
            if (cps[i] >= nbase && cps[i] < m)
                m = cps[i];
        unsigned remaining = (unsigned)(h + 1);
        unsigned step = m - nbase;
        if (step > (0xFFFFFFFFu - delta) / remaining)
            return -1;
        delta += step * remaining;
        nbase = m;
        for (int i = 0; i < n; i++) {
            if (cps[i] < nbase) {
                if (delta == 0xFFFFFFFFu)
                    return -1;
                delta++;
                continue;
            }
            if (cps[i] != nbase)
                continue;
            unsigned q = delta;
            for (int k = 36;; k += 36) {
                int t;
                if (k <= bias)
                    t = 1;
                else if (k >= bias + 26)
                    t = 26;
                else
                    t = k - bias;
                if (q < (unsigned)t)
                    break;
                if (pos + 1 >= cap)
                    return -1;
                out[pos++] = neverc_idna_puny_digit(
                    t + (int)((q - (unsigned)t) % (36u - (unsigned)t)));
                q = (q - (unsigned)t) / (36u - (unsigned)t);
            }
            if (pos + 1 >= cap)
                return -1;
            out[pos++] = neverc_idna_puny_digit((int)q);
            bias = neverc_idna_puny_adapt(delta, (unsigned)(h + 1), h == b);
            delta = 0;
            h++;
        }
        if (delta == 0xFFFFFFFFu)
            return -1;
        delta++;
        nbase++;
    }
    *used = pos;
    return 0;
}

/* Returns 0 on success. out is always NUL-terminated on success. */
static int neverc_idna_to_ascii(const char *in, char *out, size_t cap) {
    if (!in || !out || cap == 0)
        return -1;
    size_t inlen = strlen(in);
    int non_ascii = 0;
    for (size_t i = 0; i < inlen; i++) {
        if ((unsigned char)in[i] >= 0x80) {
            non_ascii = 1;
            break;
        }
    }
    if (!non_ascii) {
        if (inlen >= cap)
            return -1;
        memcpy(out, in, inlen + 1);
        return 0;
    }

    size_t used = 0;
    const unsigned char *p = (const unsigned char *)in;
    const unsigned char *end = p + inlen;
    while (p < end) {
        const unsigned char *label_start = p;
        while (p < end && *p != '.')
            p++;
        const unsigned char *label_end = p;
        int has_unicode = 0;
        const unsigned char *q = label_start;
        uint32_t cps[64];
        int ncp = 0;
        while (q < label_end) {
            uint32_t r;
            if (neverc_idna_utf8_next(&q, label_end, &r) != 0)
                return -1;
            if (r >= 0x80)
                has_unicode = 1;
            if (ncp >= 63)
                return -1;
            cps[ncp++] = r;
        }
        size_t label_out_start = used;
        if (has_unicode) {
            if (used + 4 >= cap)
                return -1;
            memcpy(out + used, "xn--", 4);
            used += 4;
            if (neverc_idna_puny_encode(cps, ncp, out, cap, &used) != 0)
                return -1;
        } else {
            size_t lab = (size_t)(label_end - label_start);
            if (used + lab >= cap)
                return -1;
            memcpy(out + used, label_start, lab);
            used += lab;
        }
        if (has_unicode) {
            size_t alen = used - label_out_start;
            if (alen == 0 || alen > 63)
                return -1;
        }
        if (p < end && *p == '.') {
            if (used + 1 >= cap)
                return -1;
            out[used++] = '.';
            p++;
        }
    }
    if (used >= cap)
        return -1;
    out[used] = '\0';
    return 0;
}

#endif /* NEVERC_NET_IDNA_INC_H */
