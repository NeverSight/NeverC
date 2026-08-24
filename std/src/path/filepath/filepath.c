/*
 * NeverC path/filepath — OS-aware file path manipulation.
 * Mirrors Go path/filepath. Uses compile-target separator.
 */

#include "neverc/std/path/filepath.h"
#include "neverc/std/unicode/utf8.h"
#ifdef _WIN32
#include "neverc/std/unicode.h"
#endif
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

#ifdef _WIN32
static char ascii_upper(char c) {
    if (c >= 'a' && c <= 'z')
        return (char)(c - ('a' - 'A'));
    return c;
}

/* Go pathHasPrefixFold: case-insensitive, separators equivalent. */
static int path_has_prefix_fold(const char *s, size_t slen,
                                const char *prefix, size_t plen) {
    size_t i;
    if (slen < plen)
        return 0;
    for (i = 0; i < plen; i++) {
        if (is_sep(prefix[i])) {
            if (!is_sep(s[i]))
                return 0;
        } else if (ascii_upper(s[i]) != ascii_upper(prefix[i])) {
            return 0;
        }
    }
    if (slen > plen && !is_sep(s[plen]))
        return 0;
    return 1;
}

static size_t unc_len(const char *path, size_t len, size_t prefix_len) {
    int count = 0;
    size_t i;
    for (i = prefix_len; i < len; i++) {
        if (is_sep(path[i])) {
            count++;
            if (count == 2)
                return i;
        }
    }
    return len;
}

/* Reject a volume that contains a ".." component (Go validVolumeNameLen). */
static size_t valid_volume_name_len(const char *path, size_t n) {
    size_t i = 0;
    while (i < n) {
        size_t start = i;
        while (i < n && !is_sep(path[i]))
            i++;
        if (i - start == 2 && path[start] == '.' && path[start + 1] == '.')
            return 0;
        if (i < n)
            i++;
    }
    return n;
}
#endif

static size_t volume_name_len(const char *path) {
#ifdef _WIN32
    size_t len = strlen(path);
    /* Go: any path[1]==':' is a drive volume, not only A-Z. */
    if (len >= 2 && path[1] == ':')
        return 2;
    if (len == 0 || !is_sep(path[0]))
        return 0;
    /* Device prefixes: \\.  \\?  \?? */
    if (path_has_prefix_fold(path, len, "\\\\.", 3) ||
        path_has_prefix_fold(path, len, "\\\\?", 3) ||
        path_has_prefix_fold(path, len, "\\??", 3)) {
        size_t i;
        if (len == 3)
            return 3;
        if (path_has_prefix_fold(path + 4, len - 4, "UNC", 3))
            return valid_volume_name_len(path, unc_len(path, len, 8));
        /* Next component after the prefix is part of the volume. */
        for (i = 0; i < len - 4; i++) {
            if (is_sep(path[4 + i]))
                return valid_volume_name_len(path, 4 + i);
        }
        return valid_volume_name_len(path, len);
    }
    if (len >= 2 && is_sep(path[1]))
        return valid_volume_name_len(path, unc_len(path, len, 2));
    return 0;
#else
    (void)path;
    return 0;
#endif
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
    size_t vol = volume_name_len(path);
    if (vol == 0)
        return 0;
    /* UNC / device paths that start with two separators are absolute. */
    if (is_sep(path[0]) && is_sep(path[1]))
        return 1;
    return path[vol] != '\0' && is_sep(path[vol]);
#else
    return path[0] == '/';
#endif
}

#ifdef _WIN32
static int reserved_device_name(const char *name, size_t n) {
    char base[16];
    size_t i, blen = 0;
    for (i = 0; i < n && blen < sizeof(base) - 1; i++) {
        if (name[i] == '.' || name[i] == ':')
            break;
        base[blen++] = ascii_upper(name[i]);
    }
    while (blen > 0 && base[blen - 1] == ' ')
        blen--;
    base[blen] = '\0';
    if (blen == 3 &&
        (memcmp(base, "CON", 3) == 0 || memcmp(base, "PRN", 3) == 0 ||
         memcmp(base, "AUX", 3) == 0 || memcmp(base, "NUL", 3) == 0))
        return 1;
    if (blen >= 4 &&
        (memcmp(base, "COM", 3) == 0 || memcmp(base, "LPT", 3) == 0)) {
        if (blen == 4 && base[3] >= '1' && base[3] <= '9')
            return 1;
        /* CVE-2023-45284: Windows treats ¹ ² ³ as DOS device digits. */
        if (blen == 5 &&
            (unsigned char)base[3] == 0xC2 &&
            ((unsigned char)base[4] == 0xB9 ||
             (unsigned char)base[4] == 0xB2 ||
             (unsigned char)base[4] == 0xB3))
            return 1;
    }
    if (blen == 6 && memcmp(base, "CONIN$", 6) == 0)
        return 1;
    if (blen == 7 && memcmp(base, "CONOUT$", 7) == 0)
        return 1;
    return 0;
}
#endif

int neverc_filepath_is_local(const char *path) {
    if (!path || *path == '\0')
        return 0;
#ifdef _WIN32
    if (is_sep(path[0]))
        return 0;
    {
        const char *c;
        for (c = path; *c; c++) {
            if (*c == ':')
                return 0;
        }
    }
#endif
    if (neverc_filepath_isabs(path))
        return 0;

    int has_dots = 0;
#ifdef _WIN32
    int reserved = 0;
#endif
    const char *p = path;
    while (*p) {
        const char *start = p;
        while (*p && !is_sep(*p))
            p++;
        size_t n = (size_t)(p - start);
#ifdef _WIN32
        if (reserved_device_name(start, n))
            reserved = 1;
#endif
        int is_dot_component =
            (n == 1 && start[0] == '.') ||
            (n == 2 && start[0] == '.' && start[1] == '.');
        if (is_dot_component)
            has_dots = 1;
#ifdef _WIN32
        if (!is_dot_component) {
            /* Win32 strips trailing spaces/dots: "foo\\.. \\x" escapes.
             * Exact "." and ".." are handled by Clean below; only padded
             * forms are rejected here so "a/../b" stays local. */
            size_t stripped = n;
            while (stripped > 0 &&
                   (start[stripped - 1] == ' ' || start[stripped - 1] == '.'))
                stripped--;
            if (stripped != n &&
                (stripped == 0 ||
                 (stripped == 1 && start[0] == '.') ||
                 (stripped == 2 && start[0] == '.' && start[1] == '.')))
                return 0;
        }
#endif
        if (*p)
            p++;
    }
#ifdef _WIN32
    if (reserved)
        return 0;
#endif
    if (!has_dots)
        return 1;

    size_t cap = strlen(path) + 8;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return 0;
    int ok = 0;
    if (neverc_filepath_clean(path, buf, cap) == buf) {
#ifdef _WIN32
        if (strcmp(buf, "..") != 0 && strncmp(buf, "..\\", 3) != 0)
            ok = 1;
#else
        if (strcmp(buf, "..") != 0 && strncmp(buf, "../", 3) != 0)
            ok = 1;
#endif
    }
    free(buf);
    return ok;
}

const char *neverc_filepath_volume_name(const char *path, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0)
        return NULL;
    if (!path)
        path = "";
    size_t vol = volume_name_len(path);
    if (vol + 1 > buf_len)
        return NULL;
#ifdef _WIN32
    {
        size_t i;
        for (i = 0; i < vol; i++)
            buf[i] = (path[i] == '/') ? '\\' : path[i];
    }
#else
    (void)path;
#endif
    buf[vol] = '\0';
    return buf;
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
    /* Go postClean: skipped when lazybuf never allocated (the cleaned
     * bytes are the original path). A colon in an already-clean name
     * such as foo:bar must stay; a rewritten result like a\..\c: still
     * gets .\ so it cannot become a drive volume. */
    if (vol == 0 && opos >= 2) {
        size_t i;
        int rewritten = (opos != len);
        int has_colon = 0;
        if (!rewritten) {
            for (i = 0; i < opos; i++) {
                if (out[i] != path[i]) {
                    rewritten = 1;
                    break;
                }
            }
        }
        if (rewritten) {
            for (i = 0; i < opos && !is_sep(out[i]); i++) {
                if (out[i] == ':') {
                    has_colon = 1;
                    break;
                }
            }
        }
        if (has_colon) {
            if (opos + 2 > out_cap) goto clean_failed;
            memmove(out + 2, out, opos);
            out[0] = '.';
            out[1] = NEVERC_FILEPATH_SEP;
            opos += 2;
        } else if (opos >= 3 && is_sep(out[0]) && out[1] == '?' && out[2] == '?') {
            if (opos + 2 > out_cap) goto clean_failed;
            memmove(out + 2, out, opos);
            out[0] = NEVERC_FILEPATH_SEP;
            out[1] = '.';
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

    size_t alen = strlen(a), blen = strlen(b);
    const char *buse = b;
    size_t buse_len = blen;
    size_t extra = 0;
#ifdef _WIN32
    /* Go path/filepath.Join: later absolute/volume elements are not discarded. */
    {
        char last = a[alen - 1];
        if (is_sep(last)) {
            while (buse_len > 0 && is_sep(buse[0])) {
                buse++;
                buse_len--;
            }
            /* Join(`\`, `??\a`) must be `\.\??\a`, not a Root Local Device path. */
            if (alen == 1 && buse_len >= 2 && buse[0] == '?' && buse[1] == '?' &&
                (buse_len == 2 || is_sep(buse[2])))
                extra = 2;
        } else if (last == ':') {
            /* Join("C:", "foo") is drive-relative "C:foo". */
        } else {
            extra = 1;
        }
    }
#else
    extra = 1;
#endif
    /* Reserve the trailing NUL: joined_len == SIZE_MAX would wrap malloc. */
    if (extra > SIZE_MAX - 1 ||
        buse_len > SIZE_MAX - extra - 1 ||
        alen > SIZE_MAX - buse_len - extra - 1)
        return NULL;
    size_t joined_len = alen + extra + buse_len;
    char *tmp = (char *)malloc(joined_len + 1);
    if (!tmp) return NULL;
    memcpy(tmp, a, alen);
    {
        size_t o = alen;
#ifdef _WIN32
        if (extra == 2) {
            tmp[o++] = '.';
            tmp[o++] = NEVERC_FILEPATH_SEP;
        } else if (extra == 1) {
            tmp[o++] = NEVERC_FILEPATH_SEP;
        }
#else
        tmp[o++] = NEVERC_FILEPATH_SEP;
#endif
        memcpy(tmp + o, buse, buse_len);
    }
    tmp[joined_len] = '\0';

    const char *result = neverc_filepath_clean(tmp, buf, buf_len);
    free(tmp);
    return result;
}

static int same_word_n(const char *a, size_t alen, const char *b, size_t blen) {
#ifdef _WIN32
    /* Go filepath.sameWord: strings.EqualFold (SimpleFold). Lengths
     * need not match (`k` vs U+212A Kelvin). */
    const uint8_t *s = (const uint8_t *)a;
    const uint8_t *t = (const uint8_t *)b;
    while (alen > 0 && blen > 0) {
        uint32_t sr, tr;
        int sw, tw;
        neverc_utf8_decode_rune(s, alen, &sr, &sw);
        neverc_utf8_decode_rune(t, blen, &tr, &tw);
        if (sw < 1) {
            sr = NEVERC_UTF8_RUNE_ERROR;
            sw = 1;
        }
        if (tw < 1) {
            tr = NEVERC_UTF8_RUNE_ERROR;
            tw = 1;
        }
        s += (size_t)sw;
        alen -= (size_t)sw;
        t += (size_t)tw;
        blen -= (size_t)tw;
        if (sr == tr)
            continue;
        if (tr < sr) {
            uint32_t tmp = sr;
            sr = tr;
            tr = tmp;
        }
        if (sr >= 'A' && sr <= 'Z' && tr == sr + ('a' - 'A'))
            continue;
        if (tr < 0x80)
            return 0;
        {
            uint32_t r = neverc_unicode_simple_fold(sr);
            while (r != sr && r < tr)
                r = neverc_unicode_simple_fold(r);
            if (r == tr)
                continue;
        }
        return 0;
    }
    return alen == 0 && blen == 0;
#else
    return alen == blen && memcmp(a, b, alen) == 0;
#endif
}

static int same_word(const char *a, const char *b) {
    return same_word_n(a, strlen(a), b, strlen(b));
}

const char *neverc_filepath_rel(const char *basepath, const char *targpath,
                                char *buf, size_t buf_len) {
    char *base = NULL, *targ = NULL, *built = NULL;
    const char *ret = NULL;
    char unc_base[2];
    size_t bcap, tcap, bvol, tvol, bl, tl, b0, bi, t0, ti;
    const char *b, *t;
    int bslash, tslash;

    if (!buf || buf_len == 0 || !basepath || !targpath)
        return NULL;

    bcap = strlen(basepath) + 8;
    tcap = strlen(targpath) + 8;
    base = (char *)malloc(bcap);
    targ = (char *)malloc(tcap);
    if (!base || !targ)
        goto done;
    if (neverc_filepath_clean(basepath, base, bcap) != base)
        goto done;
    if (neverc_filepath_clean(targpath, targ, tcap) != targ)
        goto done;

    if (same_word(base, targ)) {
        if (buf_len < 2)
            goto done;
        buf[0] = '.';
        buf[1] = '\0';
        ret = buf;
        goto done;
    }

    bvol = volume_name_len(base);
    tvol = volume_name_len(targ);
    if (!same_word_n(base, bvol, targ, tvol))
        goto done;

    b = base + bvol;
    t = targ + tvol;
    unc_base[0] = NEVERC_FILEPATH_SEP;
    unc_base[1] = '\0';
    if (b[0] == '.' && b[1] == '\0')
        b = "";
    else if (b[0] == '\0' && bvol > 2 && is_sep(base[0]))
        b = unc_base;

    bslash = b[0] == NEVERC_FILEPATH_SEP;
    tslash = t[0] == NEVERC_FILEPATH_SEP;
    if (bslash != tslash)
        goto done;

    bl = strlen(b);
    tl = strlen(t);
    b0 = bi = t0 = ti = 0;
    for (;;) {
        while (bi < bl && b[bi] != NEVERC_FILEPATH_SEP)
            bi++;
        while (ti < tl && t[ti] != NEVERC_FILEPATH_SEP)
            ti++;
        if (!same_word_n(b + b0, bi - b0, t + t0, ti - t0))
            break;
        if (bi < bl)
            bi++;
        if (ti < tl)
            ti++;
        if (bi == bl && ti == tl) {
            if (buf_len < 2)
                goto done;
            buf[0] = '.';
            buf[1] = '\0';
            ret = buf;
            goto done;
        }
        b0 = bi;
        t0 = ti;
    }

    /* Go: remaining base element ".." cannot be made relative. */
    if (bi - b0 == 2 && b[b0] == '.' && b[b0 + 1] == '.')
        goto done;

    if (b0 != bl) {
        size_t seps = 0, i, rest, size, n;
        for (i = b0; i < bl; i++) {
            if (b[i] == NEVERC_FILEPATH_SEP)
                seps++;
        }
        rest = (t0 < tl) ? (tl - t0) : 0;
        if (seps > (SIZE_MAX - 3) / 3)
            goto done;
        size = 2 + seps * 3;
        if (rest) {
            if (size > SIZE_MAX - 2 || rest > SIZE_MAX - size - 2)
                goto done;
            size += 1 + rest;
        }
        built = (char *)malloc(size + 1);
        if (!built)
            goto done;
        n = 0;
        built[n++] = '.';
        built[n++] = '.';
        for (i = 0; i < seps; i++) {
            built[n++] = NEVERC_FILEPATH_SEP;
            built[n++] = '.';
            built[n++] = '.';
        }
        if (t0 != tl) {
            built[n++] = NEVERC_FILEPATH_SEP;
            memcpy(built + n, t + t0, tl - t0);
            n += tl - t0;
        }
        built[n] = '\0';
        if (neverc_filepath_clean(built, buf, buf_len) != buf)
            goto done;
    } else if (t0 >= tl) {
        if (buf_len < 2)
            goto done;
        buf[0] = '.';
        buf[1] = '\0';
    } else {
        /* Go filepath.Rel returns the already-cleaned target suffix
         * without a second Clean. On Windows, leftover `c:` is a drive
         * volume and Clean would rewrite it to `c:.`. */
        size_t rest = tl - t0;
        if (rest + 1 > buf_len)
            goto done;
        memcpy(buf, t + t0, rest);
        buf[rest] = '\0';
    }

    /* Rel must stay relative; an absolute result would be an escape. */
    if (neverc_filepath_isabs(buf) || buf[0] == '\0')
        goto done;
    ret = buf;

done:
    free(base);
    free(targ);
    free(built);
    return ret;
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
                            uint32_t *out) {
    if (*i >= clen || chunk[*i] == '-' || chunk[*i] == ']')
        return -1;
    if (FILEPATH_MATCH_ESCAPE && chunk[*i] == '\\') {
        (*i)++;
        if (*i >= clen) return -1;
    }
    uint32_t r;
    int n;
    neverc_utf8_decode_rune((const uint8_t *)chunk + *i, clen - *i, &r, &n);
    if (n <= 0 || (r == NEVERC_UTF8_RUNE_ERROR && n == 1))
        return -1;
    *out = r;
    *i += (size_t)n;
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
            uint32_t r = 0;
            if (!failed) {
                int n;
                neverc_utf8_decode_rune((const uint8_t *)s, strlen(s), &r, &n);
                if (n > 0)
                    s += n;
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
                uint32_t lo, hi;
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
                /* Go filepath.Match: only os.PathSeparator, not every isSlash. */
                if (*s == NEVERC_FILEPATH_SEP)
                    failed = 1;
                uint32_t r;
                int n;
                neverc_utf8_decode_rune((const uint8_t *)s, strlen(s), &r, &n);
                if (n > 0)
                    s += n;
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
                if (*name == NEVERC_FILEPATH_SEP)
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
            /* Go skips one byte at a time, not one rune. */
            for (n = name; *n && *n != NEVERC_FILEPATH_SEP; n++) {
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

        /* Leftover chunks can still be malformed (Go filepath.Match). */
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
    if (!path || !buf || buf_len == 0) return NULL;
    size_t len = strlen(path);
    if (len + 1 > buf_len) return NULL;
    for (size_t i = 0; i < len; i++)
        buf[i] = (path[i] == NEVERC_FILEPATH_SEP) ? '/' : path[i];
    buf[len] = '\0';
    return buf;
}

const char *neverc_filepath_from_slash(const char *path, char *buf, size_t buf_len) {
    if (!path || !buf || buf_len == 0) return NULL;
    size_t len = strlen(path);
    if (len + 1 > buf_len) return NULL;
    for (size_t i = 0; i < len; i++)
        buf[i] = (path[i] == '/') ? NEVERC_FILEPATH_SEP : path[i];
    buf[len] = '\0';
    return buf;
}
