/*
 * NeverC path/filepath — OS-aware file path manipulation.
 * Mirrors Go path/filepath. Uses compile-target separator.
 */

#include "neverc/std/path/filepath.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int is_sep(char c) {
#ifdef _WIN32
    return c == '\\' || c == '/';
#else
    return c == '/';
#endif
}

static size_t volume_name_len(const char *path) {
#ifdef _WIN32
    size_t len = strlen(path);
    if (len >= 2 && path[1] == ':' &&
        ((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')))
        return 2;
    /* UNC: \\host\share or incomplete \\host (Go uncLen). */
    if (len >= 2 && is_sep(path[0]) && is_sep(path[1])) {
        int slashes = 0;
        size_t i;
        for (i = 2; i < len; i++) {
            if (is_sep(path[i])) {
                slashes++;
                if (slashes == 2)
                    return i;
            }
        }
        return len;
    }
#endif
    (void)path;
    return 0;
}

const char *neverc_filepath_base(const char *path, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) return NULL;
    if (!path || *path == '\0') {
        if (buf_len < 2) return NULL;
        buf[0] = '.';
        buf[1] = '\0';
        return buf;
    }
    size_t len = strlen(path);
    while (len > 0 && is_sep(path[len - 1]))
        len--;
    size_t vol = volume_name_len(path);
    if (len < vol)
        len = vol;
    /* Volume-only paths (C:\, \\host\share) have an empty last element. */
    if (len == vol) {
        if (buf_len < 2) return NULL;
        buf[0] = NEVERC_FILEPATH_SEP;
        buf[1] = '\0';
        return buf;
    }
    size_t i = len;
    while (i > vol && !is_sep(path[i - 1]))
        i--;
    size_t blen = len - i;
    if (blen + 1 > buf_len) return NULL;
    memcpy(buf, path + i, blen);
    buf[blen] = '\0';
    return buf;
}

const char *neverc_filepath_dir(const char *path, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) return NULL;
    if (!path || *path == '\0')
        return neverc_filepath_clean("", buf, buf_len);

    size_t len = strlen(path);
    size_t vol = volume_name_len(path);
    size_t i = len;
    while (i > vol && !is_sep(path[i - 1]))
        i--;

    char *tmp = (char *)malloc(i + 1);
    if (!tmp) return NULL;
    memcpy(tmp, path, i);
    tmp[i] = '\0';
    const char *result = neverc_filepath_clean(tmp, buf, buf_len);
    free(tmp);
    return result;
}

const char *neverc_filepath_ext(const char *path) {
    if (!path) return "";
    size_t len = strlen(path);
    size_t i = len;
    while (i > 0) {
        i--;
        if (path[i] == '.') return path + i;
        if (is_sep(path[i])) break;
    }
    return "";
}

int neverc_filepath_isabs(const char *path) {
    if (!path) return 0;
#ifdef _WIN32
    if (is_sep(path[0]) && is_sep(path[1]))
        return 1;
    size_t vol = volume_name_len(path);
    return vol > 0 && path[vol] != '\0' && is_sep(path[vol]);
#else
    return path[0] == '/';
#endif
}

const char *neverc_filepath_clean(const char *path, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) return NULL;
    if (!path || *path == '\0') {
        if (buf_len < 2) return NULL;
        buf[0] = '.';
        buf[1] = '\0';
        return buf;
    }
    size_t len = strlen(path);
    size_t vol = volume_name_len(path);
    int rooted = (len > vol && is_sep(path[vol]));

    /* Extra room: '.' after a drive volume (C: -> C:.) and '.\' postClean. */
    if (len > SIZE_MAX - 4) return NULL;
    size_t out_cap = len + 4;
    char *out = (char *)malloc(out_cap);
    if (!out) return NULL;
    size_t opos = 0;

#ifdef _WIN32
    {
        size_t i;
        for (i = 0; i < vol; i++)
            out[i] = (path[i] == '/') ? '\\' : path[i];
    }
#else
    memcpy(out, path, vol);
#endif
    opos = vol;

    if (rooted) {
        if (opos >= out_cap) goto clean_failed;
        out[opos++] = NEVERC_FILEPATH_SEP;
    }

    size_t r = vol;
    if (rooted) r++;
    size_t dotdot = opos;

    while (r < len) {
        if (is_sep(path[r])) {
            r++;
        } else if (path[r] == '.' && (r + 1 >= len || is_sep(path[r + 1]))) {
            r++;
        } else if (path[r] == '.' && r + 1 < len && path[r + 1] == '.' &&
                   (r + 2 >= len || is_sep(path[r + 2]))) {
            r += 2;
            if (opos > dotdot) {
                opos--;
                while (opos > dotdot && !is_sep(out[opos]))
                    opos--;
            } else if (!rooted) {
                if (opos > vol) {
                    if (opos >= out_cap) goto clean_failed;
                    out[opos++] = NEVERC_FILEPATH_SEP;
                }
                if (opos + 2 > out_cap) goto clean_failed;
                out[opos++] = '.';
                out[opos++] = '.';
                dotdot = opos;
            }
        } else {
            if ((rooted && opos != vol + 1) || (!rooted && opos > vol)) {
                if (opos >= out_cap) goto clean_failed;
                out[opos++] = NEVERC_FILEPATH_SEP;
            }
            while (r < len && !is_sep(path[r])) {
                if (opos >= out_cap) goto clean_failed;
                out[opos++] = path[r++];
            }
        }
    }

    if (opos == vol) {
        /* Empty after volume: C: -> C:.; UNC volume stays as-is. */
        if (!(vol > 1 && is_sep(path[0]))) {
            if (opos >= out_cap) goto clean_failed;
            out[opos++] = '.';
        }
    } else if (opos == 0) {
        if (out_cap < 1) goto clean_failed;
        out[opos++] = '.';
    }

#ifdef _WIN32
    /* postClean: a/../c: must not become a drive-relative path. */
    if (vol == 0 && opos >= 2) {
        size_t i;
        int has_colon = 0;
        for (i = 0; i < opos && !is_sep(out[i]); i++) {
            if (out[i] == ':') {
                has_colon = 1;
                break;
            }
        }
        if (has_colon) {
            if (opos + 2 > out_cap) goto clean_failed;
            memmove(out + 2, out, opos);
            out[0] = '.';
            out[1] = NEVERC_FILEPATH_SEP;
            opos += 2;
        }
    }
#endif

    if (opos >= buf_len) goto clean_failed;
    memcpy(buf, out, opos);
    buf[opos] = '\0';
    free(out);
    return buf;

clean_failed:
    free(out);
    return NULL;
}

const char *neverc_filepath_join(const char *a, const char *b, char *buf, size_t buf_len) {
    int a_empty = (!a || *a == '\0');
    int b_empty = (!b || *b == '\0');
    if (a_empty && b_empty) {
        if (!buf || buf_len == 0) return NULL;
        buf[0] = '\0';
        return buf;
    }
    if (a_empty) return neverc_filepath_clean(b, buf, buf_len);
    if (b_empty) return neverc_filepath_clean(a, buf, buf_len);
    if (neverc_filepath_isabs(b) || volume_name_len(b) > 0)
        return neverc_filepath_clean(b, buf, buf_len);
#ifdef _WIN32
    if (is_sep(b[0])) {
        size_t avol = volume_name_len(a);
        size_t blen = strlen(b);
        if (avol > SIZE_MAX - blen - 1) return NULL;
        char *rooted = (char *)malloc(avol + blen + 1);
        if (!rooted) return NULL;
        memcpy(rooted, a, avol);
        memcpy(rooted + avol, b, blen + 1);
        const char *result = neverc_filepath_clean(rooted, buf, buf_len);
        free(rooted);
        return result;
    }
#endif

    size_t alen = strlen(a), blen = strlen(b);
    int skip_sep = 0;
#ifdef _WIN32
    /* Join("C:", "foo") is drive-relative "C:foo", not "C:\foo". */
    skip_sep = (alen > 0 && a[alen - 1] == ':');
#endif
    if (blen > SIZE_MAX - 2 || alen > SIZE_MAX - blen - 2) return NULL;
    size_t joined_len = alen + (skip_sep ? 0 : 1) + blen;
    char *tmp = (char *)malloc(joined_len + 1);
    if (!tmp) return NULL;
    memcpy(tmp, a, alen);
    if (skip_sep) {
        memcpy(tmp + alen, b, blen);
    } else {
        tmp[alen] = NEVERC_FILEPATH_SEP;
        memcpy(tmp + alen + 1, b, blen);
    }
    tmp[joined_len] = '\0';

    const char *result = neverc_filepath_clean(tmp, buf, buf_len);
    free(tmp);
    return result;
}

void neverc_filepath_split(const char *path, const char **dir, size_t *dir_len,
                            const char **file) {
    if (!path) {
        if (dir) *dir = "";
        if (dir_len) *dir_len = 0;
        if (file) *file = "";
        return;
    }
    size_t len = strlen(path);
    size_t vol = volume_name_len(path);
    size_t i = len;
    while (i > vol && !is_sep(path[i - 1]))
        i--;
    if (dir) *dir = path;
    if (dir_len) *dir_len = i;
    if (file) *file = path + i;
}

#ifdef _WIN32
#define FILEPATH_MATCH_ESCAPE 0
#else
#define FILEPATH_MATCH_ESCAPE 1
#endif

static const char *filepath_scan_chunk(const char *pattern, int *star,
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
        if (FILEPATH_MATCH_ESCAPE && *p == '\\') {
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

static int filepath_get_esc(const char *chunk, size_t clen, size_t *i,
                            unsigned char *out) {
    if (*i >= clen || chunk[*i] == '-' || chunk[*i] == ']')
        return -1;
    if (FILEPATH_MATCH_ESCAPE && chunk[*i] == '\\') {
        (*i)++;
        if (*i >= clen) return -1;
    }
    *out = (unsigned char)chunk[(*i)++];
    if (*i >= clen) return -1;
    return 0;
}

static int filepath_match_chunk(const char *chunk, size_t clen, const char *s,
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
                if (filepath_get_esc(chunk, clen, &i, &lo) != 0)
                    return -1;
                hi = lo;
                if (chunk[i] == '-') {
                    i++;
                    if (filepath_get_esc(chunk, clen, &i, &hi) != 0)
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
                if (is_sep(*s))
                    failed = 1;
                if (*s)
                    s++;
            }
            i++;
        } else {
            if (FILEPATH_MATCH_ESCAPE && chunk[i] == '\\') {
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

int neverc_filepath_match(const char *pattern, const char *name) {
    if (!pattern || !name)
        return -1;

    while (*pattern) {
        int star = 0;
        const char *chunk = NULL;
        size_t clen = 0;
        const char *rest_pat = filepath_scan_chunk(pattern, &star, &chunk, &clen);
        if (star && clen == 0) {
            while (*name) {
                if (is_sep(*name))
                    return 0;
                name++;
            }
            return 1;
        }

        const char *t = NULL;
        int ok = filepath_match_chunk(chunk, clen, name, &t);
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
            for (n = name; *n && !is_sep(*n); n++) {
                ok = filepath_match_chunk(chunk, clen, n + 1, &t);
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
            rest_pat = filepath_scan_chunk(pattern, &star, &chunk, &clen);
            const char *dummy = NULL;
            if (filepath_match_chunk(chunk, clen, "", &dummy) < 0)
                return -1;
            pattern = rest_pat;
        }
        return 0;
    }
    return *name == '\0' ? 1 : 0;
}

const char *neverc_filepath_to_slash(const char *path, char *buf, size_t buf_len) {
    size_t len = strlen(path);
    if (len + 1 > buf_len) return NULL;
    for (size_t i = 0; i < len; i++)
        buf[i] = (path[i] == '\\') ? '/' : path[i];
    buf[len] = '\0';
    return buf;
}

const char *neverc_filepath_from_slash(const char *path, char *buf, size_t buf_len) {
    size_t len = strlen(path);
    if (len + 1 > buf_len) return NULL;
    for (size_t i = 0; i < len; i++)
        buf[i] = (path[i] == '/') ? NEVERC_FILEPATH_SEP : path[i];
    buf[len] = '\0';
    return buf;
}
