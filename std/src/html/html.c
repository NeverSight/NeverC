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
    size_t slen = strlen(s);

    /* Pass 1: branchless sum of the extra bytes escaping will add. With no early
     * exit this pipelines well, so the common "nothing to escape" check is cheap
     * and the result lets us allocate the output exactly (no realloc, no slack). */
    size_t extra = 0;
    for (size_t i = 0; i < slen; i++)
        extra += html_esc_extra[(unsigned char)s[i]];

    char *r = (char *)malloc(slen + extra + 1);
    if (!r) { *outlen = 0; return NULL; }

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

char *neverc_html_unescape_string(const char *s, size_t *outlen) {
    size_t slen = strlen(s);
    char *r = (char *)malloc(slen + 1);
    if (!r) { *outlen = 0; return NULL; }

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
            if (starts_with(s + i, "&amp;"))  { r[wi++] = '&';  i += 5; continue; }
            if (starts_with(s + i, "&lt;"))   { r[wi++] = '<';  i += 4; continue; }
            if (starts_with(s + i, "&gt;"))   { r[wi++] = '>';  i += 4; continue; }
            if (starts_with(s + i, "&#34;"))  { r[wi++] = '"';  i += 5; continue; }
            if (starts_with(s + i, "&quot;")) { r[wi++] = '"';  i += 6; continue; }
            if (starts_with(s + i, "&#39;"))  { r[wi++] = '\''; i += 5; continue; }
            if (starts_with(s + i, "&apos;")) { r[wi++] = '\''; i += 6; continue; }

            if (starts_with(s + i, "&#")) {
                i += 2;
                int base = 10;
                if (i < slen && (s[i] == 'x' || s[i] == 'X')) {
                    base = 16; i++;
                }
                unsigned long val = 0;
                while (i < slen && s[i] != ';') {
                    char c = s[i];
                    if (base == 16) {
                        if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
                        else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
                        else break;
                    } else {
                        if (c >= '0' && c <= '9') val = val * 10 + (c - '0');
                        else break;
                    }
                    i++;
                }
                if (i < slen && s[i] == ';') i++;
                if (val < 128) r[wi++] = (char)val;
                else r[wi++] = '?';
                continue;
            }
        }
        r[wi++] = s[i++];
    }
    r[wi] = '\0';
    *outlen = wi;
    return r;
}
