#include "neverc/html.h"
#include <stdlib.h>
#include <string.h>

char *neverc_html_escape_string(const char *s, size_t *outlen) {
    size_t slen = strlen(s);
    size_t cap = slen * 2;
    char *r = (char *)malloc(cap + 1);
    if (!r) { *outlen = 0; return NULL; }

    size_t wi = 0;
    for (size_t i = 0; i < slen; i++) {
        const char *esc = NULL;
        size_t elen = 0;
        switch (s[i]) {
            case '&':  esc = "&amp;";  elen = 5; break;
            case '<':  esc = "&lt;";   elen = 4; break;
            case '>':  esc = "&gt;";   elen = 4; break;
            case '"':  esc = "&#34;";  elen = 5; break;
            case '\'': esc = "&#39;";  elen = 5; break;
            default: break;
        }
        if (esc) {
            if (wi + elen >= cap) {
                cap = (wi + elen) * 2;
                r = (char *)realloc(r, cap + 1);
            }
            for (size_t j = 0; j < elen; j++) r[wi++] = esc[j];
        } else {
            if (wi + 1 >= cap) {
                cap *= 2;
                r = (char *)realloc(r, cap + 1);
            }
            r[wi++] = s[i];
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

    size_t wi = 0, i = 0;
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
