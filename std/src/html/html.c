#include "neverc/std/html.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * Per-byte expansion table: html_esc_extra[c] is how many *extra* bytes the
 * escaped form of c needs beyond the original byte (0 for bytes that escape to
 * themselves). It doubles as the "is special" predicate (nonzero == escaped).
 *   & " '  -> 5-char entity (extra 4),  < >  -> 4-char entity (extra 3).
 */
static const uint8_t html_esc_extra[256] = {
    ['&'] = 4, ['"'] = 4, ['\''] = 4, ['<'] = 3, ['>'] = 3,
};

char *neverc_html_escape_string(const char *s, size_t *outlen) {
    if (!outlen) return NULL;
    *outlen = 0;
    if (!s) return NULL;
    size_t slen = strlen(s);

    /* Pass 1 computes the exact expansion with checked arithmetic so an
     * attacker-controlled escape-dense string cannot wrap the allocation size. */
    size_t extra = 0;
    for (size_t i = 0; i < slen; i++) {
        size_t added = html_esc_extra[(unsigned char)s[i]];
        if (added > SIZE_MAX - extra) return NULL;
        extra += added;
    }
    if (slen > SIZE_MAX - extra) return NULL;
    size_t escaped_len = slen + extra;
    if (escaped_len == SIZE_MAX) return NULL;

    char *r = (char *)malloc(escaped_len + 1);
    if (!r) return NULL;

    /* Fast path: nothing needs escaping, copy the whole string in one go. */
    if (extra == 0) {
        memcpy(r, s, slen);
        r[slen] = '\0';
        *outlen = slen;
        return r;
    }

    /* Pass 2: single read of the input. Self-representing bytes are stored
     * directly (no bounds check, the buffer is exact); specials expand via a
     * constant-size memcpy the compiler inlines. Reading each byte once keeps
     * escape-dense input from paying a second scan. */
    size_t wi = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char c = (unsigned char)s[i];
        if (html_esc_extra[c] == 0) { r[wi++] = (char)c; continue; }
        switch (c) {
            case '&':  memcpy(r + wi, "&amp;", 5); wi += 5; break;
            case '<':  memcpy(r + wi, "&lt;",  4); wi += 4; break;
            case '>':  memcpy(r + wi, "&gt;",  4); wi += 4; break;
            case '"':  memcpy(r + wi, "&#34;", 5); wi += 5; break;
            case '\'': memcpy(r + wi, "&#39;", 5); wi += 5; break;
        }
    }
    r[wi] = '\0';
    *outlen = wi;
    return r;
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

static int html_is_name_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9');
}

/* Consumed length including '&', or 0.
 * need_semi == 0: HTML4 names may omit ';' even when the next byte is
 * alphanumeric (Go: `&notin` is `&not` + `in`).
 * need_semi != 0: HTML5-only names require a trailing ';'. Omitting it
 * would turn "&apos onclick=..." into a quote breakout. */
static int match_named_entity(const char *s, size_t slen, size_t i,
                              const char *name, size_t nlen, int need_semi) {
    if (i + 1 + nlen > slen) return 0;
    if (memcmp(s + i + 1, name, nlen) != 0) return 0;
    size_t after = i + 1 + nlen;
    if (after < slen && s[after] == ';')
        return (int)(nlen + 2);
    if (need_semi) return 0;
    return (int)(nlen + 1);
}

/* `&amp` / `&AMP` without ';' stay literal when a name character follows
 * (`&AMPfoo`), matching Go html.UnescapeString. Other HTML4 names do not. */
static int match_amp_entity(const char *s, size_t slen, size_t i) {
    int n = match_named_entity(s, slen, i, "amp", 3, 0);
    if (!n) n = match_named_entity(s, slen, i, "AMP", 3, 0);
    if (!n) return 0;
    if (s[i + (size_t)n - 1] != ';' &&
        i + (size_t)n < slen &&
        html_is_name_char((unsigned char)s[i + (size_t)n]))
        return 0;
    return n;
}

/* HTML numeric references 0x80..0x9F are decoded as Windows-1252, like Go. */
static const uint32_t html_win1252[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

/* cap includes the trailing NUL. Returns -1 if the rune would not fit. */
static int html_append_rune(char *r, size_t *wi, size_t cap, uint32_t val) {
    size_t need;
    if (val >= 0x80 && val <= 0x9F)
        val = html_win1252[val - 0x80];
    /* Go html.UnescapeString: NUL, surrogates, and out-of-range become
     * U+FFFD. U+FFFD itself is a valid numeric/named result. */
    if (val == 0 || val > 0x10FFFF ||
        (val >= 0xD800 && val <= 0xDFFF))
        val = 0xFFFD;
    if (val <= 0x7F) need = 1;
    else if (val <= 0x7FF) need = 2;
    else if (val <= 0xFFFF) need = 3;
    else need = 4;
    /* Leave room for the terminating NUL at r[cap-1]. */
    if (!r || cap == 0 || *wi >= cap || need > cap - *wi - 1)
        return -1;
    if (val <= 0x7F) {
        r[(*wi)++] = (char)val;
        return 0;
    }
    if (val <= 0x7FF) {
        r[(*wi)++] = (char)(0xC0 | (val >> 6));
        r[(*wi)++] = (char)(0x80 | (val & 0x3F));
        return 0;
    }
    if (val <= 0xFFFF) {
        r[(*wi)++] = (char)(0xE0 | (val >> 12));
        r[(*wi)++] = (char)(0x80 | ((val >> 6) & 0x3F));
        r[(*wi)++] = (char)(0x80 | (val & 0x3F));
        return 0;
    }
    r[(*wi)++] = (char)(0xF0 | (val >> 18));
    r[(*wi)++] = (char)(0x80 | ((val >> 12) & 0x3F));
    r[(*wi)++] = (char)(0x80 | ((val >> 6) & 0x3F));
    r[(*wi)++] = (char)(0x80 | (val & 0x3F));
    return 0;
}

typedef struct {
    const char *name;
    uint32_t    rune;
    uint8_t     need_semi;
} html_named_entity_t;

/* Longer names first so &notin; is not consumed as &not;.
 * need_semi: 0 = HTML4 (semicolon optional), 1 = HTML5 (required). */
static const html_named_entity_t html_named[] = {
    {"NewLine", 0x000A, 1},
    {"notin",   0x2209, 1},
    {"hellip",  0x2026, 1},
    {"plusmn",  0x00B1, 0},
    {"divide",  0x00F7, 0},
    {"percnt",  0x0025, 1},
    {"equals",  0x003D, 1},
    {"dollar",  0x0024, 1},
    {"mdash",   0x2014, 1},
    {"ndash",   0x2013, 1},
    {"trade",   0x2122, 1},
    {"TRADE",   0x2122, 1},
    {"laquo",   0x00AB, 0},
    {"raquo",   0x00BB, 0},
    {"times",   0x00D7, 0},
    {"micro",   0x00B5, 0},
    {"pound",   0x00A3, 0},
    {"colon",   0x003A, 1},
    {"Colon",   0x2237, 1},
    {"grave",   0x0060, 1},
    {"lpar",    0x0028, 1},
    {"rpar",    0x0029, 1},
    {"nbsp",    0x00A0, 0},
    {"copy",    0x00A9, 0},
    {"COPY",    0x00A9, 0},
    {"euro",    0x20AC, 1},
    {"para",    0x00B6, 0},
    {"sect",    0x00A7, 0},
    {"cent",    0x00A2, 0},
    {"plus",    0x002B, 1},
    {"semi",    0x003B, 1},
    {"excl",    0x0021, 1},
    {"bsol",    0x005C, 1},
    {"deg",     0x00B0, 0},
    {"yen",     0x00A5, 0},
    {"reg",     0x00AE, 0},
    {"REG",     0x00AE, 0},
    {"Tab",     0x0009, 1},
    {"sol",     0x002F, 1},
    {"num",     0x0023, 1},
    {"ast",     0x002A, 1},
    {"not",     0x00AC, 0},
};

char *neverc_html_unescape_string(const char *s, size_t *outlen) {
    if (!outlen) return NULL;
    *outlen = 0;
    if (!s) return NULL;
    size_t slen = strlen(s);
    if (slen == SIZE_MAX) return NULL;
    size_t cap = slen + 1;
    char *r = (char *)malloc(cap);
    if (!r) return NULL;

    /* Entities only begin at '&'. Bulk-copy the clean prefix up to the first
     * one (and short-circuit entirely when there is none); the byte-at-a-time
     * decoder below only runs from the first '&' onward. This keeps dense-entity
     * input at the original speed while making entity-free text a pure memcpy. */
    const char *amp = (const char *)memchr(s, '&', slen);
    if (!amp) {
        memcpy(r, s, slen);
        r[slen] = '\0';
        *outlen = slen;
        return r;
    }
    size_t i = (size_t)(amp - s);
    memcpy(r, s, i);
    size_t wi = i;

    while (i < slen) {
        if (s[i] == '&') {
            {
                int n;
                if ((n = match_amp_entity(s, slen, i))) {
                    r[wi++] = '&'; i += (size_t)n; continue;
                }
                if ((n = match_named_entity(s, slen, i, "lt", 2, 0)) ||
                    (n = match_named_entity(s, slen, i, "LT", 2, 0))) {
                    r[wi++] = '<'; i += (size_t)n; continue;
                }
                if ((n = match_named_entity(s, slen, i, "gt", 2, 0)) ||
                    (n = match_named_entity(s, slen, i, "GT", 2, 0))) {
                    r[wi++] = '>'; i += (size_t)n; continue;
                }
                if ((n = match_named_entity(s, slen, i, "quot", 4, 0)) ||
                    (n = match_named_entity(s, slen, i, "QUOT", 4, 0))) {
                    r[wi++] = '"'; i += (size_t)n; continue;
                }
                if ((n = match_named_entity(s, slen, i, "apos", 4, 1))) {
                    r[wi++] = '\''; i += (size_t)n; continue;
                }
                if (starts_with(s + i, "&#34;"))  { r[wi++] = '"';  i += 5; continue; }
                if (starts_with(s + i, "&#39;"))  { r[wi++] = '\''; i += 5; continue; }

                int named_hit = 0;
                for (size_t e = 0; e < sizeof(html_named) / sizeof(html_named[0]); e++) {
                    const char *name = html_named[e].name;
                    size_t nlen = strlen(name);
                    n = match_named_entity(s, slen, i, name, nlen,
                                           (int)html_named[e].need_semi);
                    if (n) {
                        if (html_append_rune(r, &wi, cap, html_named[e].rune) != 0) {
                            free(r);
                            return NULL;
                        }
                        i += (size_t)n;
                        named_hit = 1;
                        break;
                    }
                }
                if (named_hit) continue;
            }

            if (starts_with(s + i, "&#")) {
                size_t entity_start = i;
                i += 2;
                int base = 10;
                if (i < slen && (s[i] == 'x' || s[i] == 'X')) {
                    base = 16; i++;
                }
                unsigned long val = 0;
                int saw_digit = 0;
                int overflow = 0;
                while (i < slen && s[i] != ';') {
                    char c = s[i];
                    unsigned long digit;
                    if (base == 16) {
                        if (c >= '0' && c <= '9') digit = (unsigned long)(c - '0');
                        else if (c >= 'a' && c <= 'f') digit = (unsigned long)(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') digit = (unsigned long)(c - 'A' + 10);
                        else break;
                    } else {
                        if (c >= '0' && c <= '9') digit = (unsigned long)(c - '0');
                        else break;
                    }
                    saw_digit = 1;
                    if (val > (0x10FFFFUL - digit) / (unsigned long)base)
                        overflow = 1;
                    else if (!overflow)
                        val = val * (unsigned long)base + digit;
                    i++;
                }
                if (!saw_digit) {
                    r[wi++] = '&';
                    i = entity_start + 1;
                    continue;
                }
                if (i < slen && s[i] == ';') i++;
                if (html_append_rune(r, &wi, cap,
                                     overflow ? 0xFFFD : (uint32_t)val) != 0) {
                    free(r);
                    return NULL;
                }
                continue;
            }
        }
        r[wi++] = s[i++];
    }
    r[wi] = '\0';
    *outlen = wi;
    return r;
}
