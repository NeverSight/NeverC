/*
 * NeverC path/filepath — OS-aware file path manipulation.
 * Mirrors Go path/filepath. Uses compile-target separator.
 */

#include "neverc/std/path/filepath.h"
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
#endif
    (void)path;
    return 0;
}

const char *neverc_filepath_base(const char *path, char *buf, size_t buf_len) {
    if (!path || *path == '\0') {
        if (buf_len >= 2) { buf[0] = '.'; buf[1] = '\0'; }
        return buf;
    }
    size_t len = strlen(path);
    /* strip trailing separators */
    while (len > 0 && is_sep(path[len - 1]))
        len--;
    if (len == 0) {
        if (buf_len >= 2) { buf[0] = NEVERC_FILEPATH_SEP; buf[1] = '\0'; }
        return buf;
    }
    /* find last separator */
    size_t i = len;
    while (i > 0 && !is_sep(path[i - 1]))
        i--;
    size_t blen = len - i;
    if (blen + 1 > buf_len) return NULL;
    memcpy(buf, path + i, blen);
    buf[blen] = '\0';
    return buf;
}

const char *neverc_filepath_dir(const char *path, char *buf, size_t buf_len) {
    if (!path || *path == '\0') {
        if (buf_len >= 2) { buf[0] = '.'; buf[1] = '\0'; }
        return buf;
    }
    size_t len = strlen(path);
    size_t vol = volume_name_len(path);

    /* find last separator */
    size_t i = len;
    while (i > vol && !is_sep(path[i - 1]))
        i--;
    /* strip trailing separators (but keep root) */
    while (i > vol + 1 && is_sep(path[i - 1]))
        i--;
    if (i == 0) {
        if (buf_len >= 2) { buf[0] = '.'; buf[1] = '\0'; }
        return buf;
    }
    if (i + 1 > buf_len) return NULL;
    memcpy(buf, path, i);
    buf[i] = '\0';
    return buf;
}

const char *neverc_filepath_ext(const char *path) {
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
#ifdef _WIN32
    size_t vol = volume_name_len(path);
    return vol > 0 && strlen(path) > vol && is_sep(path[vol]);
#else
    return path[0] == '/';
#endif
}

const char *neverc_filepath_clean(const char *path, char *buf, size_t buf_len) {
    if (!path || *path == '\0') {
        if (buf_len >= 2) { buf[0] = '.'; buf[1] = '\0'; }
        return buf;
    }
    size_t len = strlen(path);
    size_t vol = volume_name_len(path);
    int rooted = (len > vol && is_sep(path[vol]));

    char out[4096];
    size_t opos = 0;

    /* copy volume name */
    for (size_t i = 0; i < vol; i++) out[opos++] = path[i];

    if (rooted) out[opos++] = NEVERC_FILEPATH_SEP;

    size_t r = vol;
    if (rooted) r++;
    int dotdot = (int)opos;

    while (r < len) {
        if (is_sep(path[r])) {
            r++;
        } else if (path[r] == '.' && (r + 1 >= len || is_sep(path[r + 1]))) {
            r++;
        } else if (path[r] == '.' && r + 1 < len && path[r + 1] == '.' &&
                   (r + 2 >= len || is_sep(path[r + 2]))) {
            r += 2;
            if ((int)opos > dotdot) {
                opos--;
                while ((int)opos > dotdot && !is_sep(out[opos]))
                    opos--;
            } else if (!rooted) {
                if (opos > 0) out[opos++] = NEVERC_FILEPATH_SEP;
                out[opos++] = '.'; out[opos++] = '.';
                dotdot = (int)opos;
            }
        } else {
            if ((rooted && opos != vol + 1) || (!rooted && opos > 0))
                out[opos++] = NEVERC_FILEPATH_SEP;
            while (r < len && !is_sep(path[r]))
                out[opos++] = path[r++];
        }
    }

    if (opos == 0) { out[opos++] = '.'; }

    if (opos + 1 > buf_len) return NULL;
    memcpy(buf, out, opos);
    buf[opos] = '\0';
    return buf;
}

const char *neverc_filepath_join(const char *a, const char *b, char *buf, size_t buf_len) {
    if (!a || *a == '\0') return neverc_filepath_clean(b, buf, buf_len);
    if (!b || *b == '\0') return neverc_filepath_clean(a, buf, buf_len);

    char tmp[4096];
    size_t alen = strlen(a), blen = strlen(b);
    if (alen + 1 + blen + 1 > sizeof(tmp)) return NULL;
    memcpy(tmp, a, alen);
    tmp[alen] = NEVERC_FILEPATH_SEP;
    memcpy(tmp + alen + 1, b, blen);
    tmp[alen + 1 + blen] = '\0';

    return neverc_filepath_clean(tmp, buf, buf_len);
}

void neverc_filepath_split(const char *path, const char **dir, size_t *dir_len,
                            const char **file) {
    size_t len = strlen(path);
    size_t vol = volume_name_len(path);
    size_t i = len;
    while (i > vol && !is_sep(path[i - 1]))
        i--;
    *dir = path;
    *dir_len = i;
    *file = path + i;
}

int neverc_filepath_match(const char *pattern, const char *name) {
    while (*pattern && *name) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return 1;
            while (*name) {
                if (neverc_filepath_match(pattern, name)) return 1;
                if (is_sep(*name)) return 0;
                name++;
            }
            return 0;
        } else if (*pattern == '?') {
            if (is_sep(*name)) return 0;
            pattern++; name++;
        } else {
            if (*pattern != *name) return 0;
            pattern++; name++;
        }
    }
    while (*pattern == '*') pattern++;
    return *pattern == '\0' && *name == '\0';
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
