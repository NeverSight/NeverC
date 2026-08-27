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

#if 0
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
#endif

/* HTML numeric references 0x80..0x9F use the WHATWG "numeric character
 * reference end state" table, like Go. That table has no row for 0x81,
 * 0x8D, 0x8F, 0x90, or 0x9D, so those references keep their own code
 * point instead of becoming U+FFFD. */
static const uint32_t html_win1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
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
    uint8_t     name_len;
    uint32_t    rune1;
    uint32_t    rune2;
} html_named_entity_t;

#include "html_entities.h"

#if 0
/* Longer names first so &notin; is not consumed as &not;.
 * need_semi: 0 = HTML4 (semicolon optional), 1 = HTML5 (required). */
static const html_named_entity_t html_named[] = {
    {"NewLine", 0x000A, 1},
    {"ltimes",  0x22C9, 1},
    {"notin",   0x2209, 1},
    {"hellip",  0x2026, 1},
    {"plusmn",  0x00B1, 0},
    {"divide",  0x00F7, 0},
    {"percnt",  0x0025, 1},
    {"equals",  0x003D, 1},
    {"dollar",  0x0024, 1},
    {"Aacute",  0x00C1, 0},
    {"Agrave",  0x00C0, 0},
    {"Atilde",  0x00C3, 0},
    {"Ccedil",  0x00C7, 0},
    {"Eacute",  0x00C9, 0},
    {"Egrave",  0x00C8, 0},
    {"Iacute",  0x00CD, 0},
    {"Igrave",  0x00CC, 0},
    {"Ntilde",  0x00D1, 0},
    {"Oacute",  0x00D3, 0},
    {"Ograve",  0x00D2, 0},
    {"Oslash",  0x00D8, 0},
    {"Otilde",  0x00D5, 0},
    {"Uacute",  0x00DA, 0},
    {"Ugrave",  0x00D9, 0},
    {"Yacute",  0x00DD, 0},
    {"aacute",  0x00E1, 0},
    {"agrave",  0x00E0, 0},
    {"atilde",  0x00E3, 0},
    {"brvbar",  0x00A6, 0},
    {"ccedil",  0x00E7, 0},
    {"curren",  0x00A4, 0},
    {"eacute",  0x00E9, 0},
    {"egrave",  0x00E8, 0},
    {"frac12",  0x00BD, 0},
    {"frac14",  0x00BC, 0},
    {"frac34",  0x00BE, 0},
    {"iacute",  0x00ED, 0},
    {"igrave",  0x00EC, 0},
    {"iquest",  0x00BF, 0},
    {"middot",  0x00B7, 0},
    {"ntilde",  0x00F1, 0},
    {"oacute",  0x00F3, 0},
    {"ograve",  0x00F2, 0},
    {"oslash",  0x00F8, 0},
    {"otilde",  0x00F5, 0},
    {"uacute",  0x00FA, 0},
    {"ugrave",  0x00F9, 0},
    {"yacute",  0x00FD, 0},
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
    {"AElig",   0x00C6, 0},
    {"Acirc",   0x00C2, 0},
    {"Aring",   0x00C5, 0},
    {"Ecirc",   0x00CA, 0},
    {"Icirc",   0x00CE, 0},
    {"Ocirc",   0x00D4, 0},
    {"THORN",   0x00DE, 0},
    {"Ucirc",   0x00DB, 0},
    {"aelig",   0x00E6, 0},
    {"acirc",   0x00E2, 0},
    {"acute",   0x00B4, 0},
    {"aring",   0x00E5, 0},
    {"cedil",   0x00B8, 0},
    {"ecirc",   0x00EA, 0},
    {"icirc",   0x00EE, 0},
    {"iexcl",   0x00A1, 0},
    {"macr",    0x00AF, 0},
    {"ocirc",   0x00F4, 0},
    {"ordf",    0x00AA, 0},
    {"ordm",    0x00BA, 0},
    {"szlig",   0x00DF, 0},
    {"thorn",   0x00FE, 0},
    {"ucirc",   0x00FB, 0},
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
    {"Auml",    0x00C4, 0},
    {"ETH",     0x00D0, 0},
    {"Euml",    0x00CB, 0},
    {"Iuml",    0x00CF, 0},
    {"Ouml",    0x00D6, 0},
    {"Uuml",    0x00DC, 0},
    {"auml",    0x00E4, 0},
    {"eth",     0x00F0, 0},
    {"euml",    0x00EB, 0},
    {"iuml",    0x00EF, 0},
    {"ouml",    0x00F6, 0},
    {"sup1",    0x00B9, 0},
    {"sup2",    0x00B2, 0},
    {"sup3",    0x00B3, 0},
    {"uuml",    0x00FC, 0},
    {"yuml",    0x00FF, 0},
    {"shy",     0x00AD, 0},
    {"uml",     0x00A8, 0},
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
#endif

static int html_entity_name_compare(
    const char *name, size_t name_len, const html_named_entity_t *entry) {
    size_t common = name_len < entry->name_len ? name_len : entry->name_len;
    int compared = memcmp(name, entry->name, common);
    if (compared != 0) return compared;
    if (name_len < entry->name_len) return -1;
    if (name_len > entry->name_len) return 1;
    return 0;
}

static const html_named_entity_t *html_entity_lookup(
    const char *name, size_t name_len) {
    size_t lo = 0;
    size_t hi = sizeof(html_named_entities) / sizeof(html_named_entities[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        int compared = html_entity_name_compare(
            name, name_len, &html_named_entities[mid]);
        if (compared == 0) return &html_named_entities[mid];
        if (compared < 0) hi = mid;
        else lo = mid + 1U;
    }
    return NULL;
}

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
                size_t name_start = i + 1U;
                size_t name_end = name_start;
                while (name_end < slen &&
                       html_is_name_char((unsigned char)s[name_end]))
                    name_end++;
                if (name_end < slen && s[name_end] == ';')
                    name_end++;
                size_t matched_len = name_end - name_start;
                const html_named_entity_t *entity = matched_len > 0
                    ? html_entity_lookup(s + name_start, matched_len) : NULL;
                if (!entity && matched_len > 1U) {
                    /* Go html.UnescapeString falls back to the longest legacy
                     * semicolon-less prefix, whose maximum length is six. */
                    size_t candidate_len = matched_len - 1U;
                    if (candidate_len > 6U) candidate_len = 6U;
                    while (candidate_len > 1U) {
                        entity = html_entity_lookup(
                            s + name_start, candidate_len);
                        if (entity) {
                            matched_len = candidate_len;
                            break;
                        }
                        candidate_len--;
                    }
                }
                if (entity) {
                    if (html_append_rune(
                            r, &wi, cap, entity->rune1) != 0 ||
                        (entity->rune2 != 0 && html_append_rune(
                            r, &wi, cap, entity->rune2) != 0)) {
                        free(r);
                        return NULL;
                    }
                    i += matched_len + 1U;
                    continue;
                }
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
