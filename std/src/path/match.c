#include "neverc/std/path.h"
#include <string.h>

/* Go path.Match: * / ? / [class] / \escape. Returns 1, 0, or -1 (bad pattern). */

static const char *scan_chunk(const char *pattern, int *star,
                              const char **chunk, size_t *clen) {
    *star = 0;
    while (*pattern == '*') {
        pattern++;
        *star = 1;
    }
    const char *start = pattern;
    int inrange = 0;
    const char *p = pattern;
    for (; *p; p++) {
        if (*p == '\\') {
            if (p[1]) p++;
            continue;
        }
        if (*p == '[') inrange = 1;
        else if (*p == ']') inrange = 0;
        else if (*p == '*' && !inrange) break;
    }
    *chunk = start;
    *clen = (size_t)(p - start);
    return p;
}

static int get_esc(const char *chunk, size_t clen, size_t *i, unsigned char *out) {
    if (*i >= clen || chunk[*i] == '-' || chunk[*i] == ']')
        return -1;
    if (chunk[*i] == '\\') {
        (*i)++;
        if (*i >= clen) return -1;
    }
    *out = (unsigned char)chunk[(*i)++];
    if (*i >= clen) return -1;
    return 0;
}

static int match_chunk(const char *chunk, size_t clen, const char *s,
                       const char **rest) {
    int failed = 0;
    size_t i = 0;
    while (i < clen) {
        if (!failed && *s == '\0')
            failed = 1;
        if (chunk[i] == '[') {
            unsigned char r = 0;
            if (!failed) {
                r = (unsigned char)*s;
                s++;
            }
            i++;
            int negated = 0;
            if (i < clen && chunk[i] == '^') {
                negated = 1;
                i++;
            }
            int matched = 0;
            int nrange = 0;
            for (;;) {
                if (i < clen && chunk[i] == ']' && nrange > 0) {
                    i++;
                    break;
                }
                unsigned char lo, hi;
                if (get_esc(chunk, clen, &i, &lo) != 0)
                    return -1;
                hi = lo;
                if (chunk[i] == '-') {
                    i++;
                    if (get_esc(chunk, clen, &i, &hi) != 0)
                        return -1;
                }
                if (lo <= r && r <= hi)
                    matched = 1;
                nrange++;
            }
            if (matched == negated)
                failed = 1;
        } else if (chunk[i] == '?') {
            if (!failed) {
                if (*s == '/')
                    failed = 1;
                if (*s)
                    s++;
            }
            i++;
        } else {
            if (chunk[i] == '\\') {
                i++;
                if (i >= clen)
                    return -1;
            }
            if (!failed) {
                if ((unsigned char)chunk[i] != (unsigned char)*s)
                    failed = 1;
                if (*s)
                    s++;
            }
            i++;
        }
    }
    if (failed)
        return 0;
    *rest = s;
    return 1;
}

int neverc_path_match(const char *pattern, const char *name) {
    if (!pattern || !name)
        return -1;

    while (*pattern) {
        int star = 0;
        const char *chunk = NULL;
        size_t clen = 0;
        const char *rest_pat = scan_chunk(pattern, &star, &chunk, &clen);
        if (star && clen == 0) {
            while (*name) {
                if (*name == '/')
                    return 0;
                name++;
            }
            return 1;
        }

        const char *t = NULL;
        int ok = match_chunk(chunk, clen, name, &t);
        if (ok < 0)
            return -1;
        if (ok && (*t == '\0' || *rest_pat != '\0')) {
            name = t;
            pattern = rest_pat;
            continue;
        }

        if (star) {
            int advanced = 0;
            const char *n;
            for (n = name; *n && *n != '/'; n++) {
                ok = match_chunk(chunk, clen, n + 1, &t);
                if (ok < 0)
                    return -1;
                if (ok) {
                    if (*rest_pat == '\0' && *t != '\0')
                        continue;
                    name = t;
                    pattern = rest_pat;
                    advanced = 1;
                    break;
                }
            }
            if (advanced)
                continue;
        }

        pattern = rest_pat;
        while (*pattern) {
            rest_pat = scan_chunk(pattern, &star, &chunk, &clen);
            const char *dummy = NULL;
            if (match_chunk(chunk, clen, "", &dummy) < 0)
                return -1;
            pattern = rest_pat;
        }
        return 0;
    }
    return *name == '\0' ? 1 : 0;
}
