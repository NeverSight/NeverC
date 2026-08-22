#include "neverc/std/mime.h"
#include "neverc/std/mime/rfc2047_safe.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    const char *ext;
    const char *mime;
} mime_entry_t;

static const mime_entry_t mime_table[] = {
    {".aac",   "audio/aac"},
    {".avi",   "video/x-msvideo"},
    {".bin",   "application/octet-stream"},
    {".bmp",   "image/bmp"},
    {".bz",    "application/x-bzip"},
    {".bz2",   "application/x-bzip2"},
    {".c",     "text/x-c"},
    {".cpp",   "text/x-c++src"},
    {".css",   "text/css"},
    {".csv",   "text/csv"},
    {".doc",   "application/msword"},
    {".docx",  "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".eot",   "application/vnd.ms-fontobject"},
    {".epub",  "application/epub+zip"},
    {".flac",  "audio/flac"},
    {".gif",   "image/gif"},
    {".go",    "text/x-go"},
    {".gz",    "application/gzip"},
    {".h",     "text/x-c"},
    {".htm",   "text/html"},
    {".html",  "text/html"},
    {".ico",   "image/vnd.microsoft.icon"},
    {".jar",   "application/java-archive"},
    {".java",  "text/x-java-source"},
    {".jpeg",  "image/jpeg"},
    {".jpg",   "image/jpeg"},
    {".js",    "text/javascript"},
    {".json",  "application/json"},
    {".m4a",   "audio/mp4"},
    {".md",    "text/markdown"},
    {".mid",   "audio/midi"},
    {".midi",  "audio/midi"},
    {".mjs",   "text/javascript"},
    {".mkv",   "video/x-matroska"},
    {".mov",   "video/quicktime"},
    {".mp3",   "audio/mpeg"},
    {".mp4",   "video/mp4"},
    {".mpeg",  "video/mpeg"},
    {".mpg",   "video/mpeg"},
    {".ogg",   "audio/ogg"},
    {".ogv",   "video/ogg"},
    {".otf",   "font/otf"},
    {".pdf",   "application/pdf"},
    {".php",   "application/x-httpd-php"},
    {".png",   "image/png"},
    {".ppt",   "application/vnd.ms-powerpoint"},
    {".py",    "text/x-python"},
    {".rar",   "application/vnd.rar"},
    {".rb",    "text/x-ruby"},
    {".rs",    "text/x-rust"},
    {".rtf",   "application/rtf"},
    {".sh",    "application/x-sh"},
    {".sql",   "application/x-sql"},
    {".svg",   "image/svg+xml"},
    {".tar",   "application/x-tar"},
    {".tif",   "image/tiff"},
    {".tiff",  "image/tiff"},
    {".ts",    "text/typescript"},
    {".ttf",   "font/ttf"},
    {".txt",   "text/plain"},
    {".wasm",  "application/wasm"},
    {".wav",   "audio/wav"},
    {".weba",  "audio/webm"},
    {".webm",  "video/webm"},
    {".webp",  "image/webp"},
    {".woff",  "font/woff"},
    {".woff2", "font/woff2"},
    {".xhtml", "application/xhtml+xml"},
    {".xls",   "application/vnd.ms-excel"},
    {".xlsx",  "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".xml",   "application/xml"},
    {".yaml",  "text/yaml"},
    {".yml",   "text/yaml"},
    {".zip",   "application/zip"},
    {".7z",    "application/x-7z-compressed"},
    {NULL, NULL}
};

static int nc_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int mime_is_ows(int c) {
    return c == ' ' || c == '\t';
}

static int mime_is_token_char(unsigned char c) {
    static const char specials[] = "()<>@,;:\\\"/[]?=";
    return c > 32 && c < 127 && strchr(specials, (int)c) == NULL;
}

/* RFC 2046: 1..70 bchars, last must not be space. Used so format cannot
 * emit a multipart boundary the reader would reject. */
static int mime_is_rfc2046_boundary(const char *s) {
    if (!s || *s == '\0') return 0;
    size_t n = 0;
    while (s[n] != '\0') {
        unsigned char c = (unsigned char)s[n];
        int valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '\'' || c == '(' || c == ')' || c == '+' ||
                    c == '_' || c == ',' || c == '-' || c == '.' ||
                    c == '/' || c == ':' || c == '=' || c == '?' ||
                    c == ' ';
        if (!valid || n == 70) return 0;
        n++;
    }
    return s[n - 1] != ' ';
}

/* RFC 2045 tspecials. Go's consumeValue only treats `\` as an escape when
 * the next byte is in this set, so `C:\dev\file` keeps the backslashes. */
static int mime_is_tspecial(unsigned char c) {
    return c > 32 && c < 127 && !mime_is_token_char(c);
}

static int mime_is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static int mime_span_case_equal(const char *a, size_t alen,
                                const char *b, size_t blen) {
    if (alen != blen) return 0;
    for (size_t i = 0; i < alen; i++) {
        if (nc_tolower((unsigned char)a[i]) !=
            nc_tolower((unsigned char)b[i]))
            return 0;
    }
    return 1;
}

static int strcasecmp_local(const char *a, const char *b) {
    while (*a && *b) {
        int ca = nc_tolower((unsigned char)*a);
        int cb = nc_tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

const char *neverc_mime_type_by_extension(const char *ext) {
    if (!ext) return NULL;
    for (const mime_entry_t *e = mime_table; e->ext; e++) {
        if (strcasecmp_local(e->ext, ext) == 0)
            return e->mime;
    }
    return "application/octet-stream";
}

const char *neverc_mime_extension_by_type(const char *mime_type) {
    if (!mime_type) return NULL;
    while (mime_is_ows((unsigned char)*mime_type)) mime_type++;
    const char *end = strchr(mime_type, ';');
    if (!end) end = mime_type + strlen(mime_type);
    while (end > mime_type && mime_is_ows((unsigned char)end[-1])) end--;
    size_t input_len = (size_t)(end - mime_type);
    for (const mime_entry_t *e = mime_table; e->ext; e++) {
        size_t mlen = strlen(e->mime);
        if (mime_span_case_equal(e->mime, mlen, mime_type, input_len))
            return e->ext;
    }
    return NULL;
}

static void mime_free_params(char *keys[], char *vals[], int count) {
    for (int i = 0; i < count; i++) {
        free(keys[i]);
        free(vals[i]);
        keys[i] = NULL;
        vals[i] = NULL;
    }
}

static int mime_hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *mime_percent_unescape(const char *s, size_t n, int *oom) {
    if (oom) *oom = 0;
    size_t out_len = 0;
    for (size_t i = 0; i < n; ) {
        if (s[i] == '%') {
            if (i + 2 >= n || mime_hex_digit((unsigned char)s[i + 1]) < 0 ||
                mime_hex_digit((unsigned char)s[i + 2]) < 0)
                return NULL;
            i += 3;
            out_len++;
        } else {
            i++;
            out_len++;
        }
    }
    char *out = (char *)malloc(out_len + 1);
    if (!out) {
        if (oom) *oom = 1;
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 0; i < n; ) {
        if (s[i] == '%') {
            out[j++] = (char)((mime_hex_digit((unsigned char)s[i + 1]) << 4) |
                              mime_hex_digit((unsigned char)s[i + 2]));
            i += 3;
        } else {
            out[j++] = s[i++];
        }
    }
    out[j] = '\0';
    for (size_t k = 0; k < j; k++) {
        unsigned char c = (unsigned char)out[k];
        if (c < 0x20 || c == 0x7f) {
            free(out);
            return NULL;
        }
    }
    /* Overlong UTF-8 of CR/LF (%c0%8d) is not a C0 byte; reject it the
     * same way RFC 2047 already rejects =C0=8D. */
    if (!nci_2047_utf8_ok((const unsigned char *)out, j)) {
        free(out);
        return NULL;
    }
    return out;
}

static int mime_2231_charset_ok(const char *s, size_t n) {
    return n == 0 ||
           mime_span_case_equal(s, n, "us-ascii", 8) ||
           mime_span_case_equal(s, n, "utf-8", 5);
}

static char *mime_decode_2231_value(const char *v, int *oom) {
    if (oom) *oom = 0;
    if (!v) return NULL;
    const char *q1 = strchr(v, '\'');
    if (!q1 || !mime_2231_charset_ok(v, (size_t)(q1 - v))) return NULL;
    const char *q2 = strchr(q1 + 1, '\'');
    if (!q2) return NULL;
    return mime_percent_unescape(q2 + 1, strlen(q2 + 1), oom);
}

/* name* (single RFC 2231 / 5987 parameter), not name*0 continuations. */
static int mime_is_2231_single(const char *key, size_t *base_len) {
    const char *star = strchr(key, '*');
    if (!star || star == key || star[1] != '\0') return 0;
    *base_len = (size_t)(star - key);
    return 1;
}

/* name*N or name*N*. N has no leading zero unless N is 0 (Go: *01 != *1). */
static int mime_is_2231_cont(const char *key, size_t *base_len,
                             int *idx, int *encoded) {
    const char *star = strchr(key, '*');
    if (!star || star == key || !mime_is_digit((unsigned char)star[1]))
        return 0;
    if (star[1] == '0' && mime_is_digit((unsigned char)star[2]))
        return 0;
    const char *p = star + 1;
    int n = 0;
    while (mime_is_digit((unsigned char)*p)) {
        if (n > (INT_MAX - (*p - '0')) / 10) return 0;
        n = n * 10 + (*p - '0');
        p++;
    }
    if (*p == '*') {
        if (p[1] != '\0') return 0;
        *encoded = 1;
    } else if (*p == '\0') {
        *encoded = 0;
    } else {
        return 0;
    }
    *base_len = (size_t)(star - key);
    *idx = n;
    return 1;
}

static int mime_key_base_is(const char *key, const char *base, size_t blen) {
    size_t slen = 0;
    if (mime_is_2231_single(key, &slen))
        return slen == blen && memcmp(key, base, blen) == 0;
    return strlen(key) == blen && memcmp(key, base, blen) == 0;
}

static int mime_find_2231_cont(char *keys[], int n, const char *base,
                               size_t blen, int want_idx, int *encoded) {
    for (int j = 0; j < n; j++) {
        if (!keys[j]) continue;
        size_t jb = 0;
        int jidx = 0, jenc = 0;
        if (!mime_is_2231_cont(keys[j], &jb, &jidx, &jenc)) continue;
        if (jb == blen && memcmp(keys[j], base, blen) == 0 &&
            jidx == want_idx) {
            if (encoded) *encoded = jenc;
            return j;
        }
    }
    return -1;
}

static int mime_apply_rfc2231_fail(char *keys[], char *vals[], int n,
                                   int *nparams) {
    mime_free_params(keys, vals, n);
    *nparams = 0;
    return -1;
}

static int mime_apply_rfc2231(char *keys[], char *vals[], int *nparams) {
    int n = *nparams;
    if (n <= 0) return 0;

    int any_star = 0;
    for (int i = 0; i < n; i++) {
        if (keys[i] && strchr(keys[i], '*')) {
            any_star = 1;
            break;
        }
    }
    if (!any_star) return 0;

    unsigned char *had_single = (unsigned char *)calloc((size_t)n, 1);
    if (!had_single)
        return mime_apply_rfc2231_fail(keys, vals, n, nparams);

    /* Decode name*=charset''value. A failed decode drops that parameter
     * after stitching (the raw name* key is kept until then so name*0
     * pieces for the same base are not promoted). OOM fails the parse. */
    for (int i = 0; i < n; i++) {
        if (!keys[i]) continue;
        size_t blen = 0;
        if (!mime_is_2231_single(keys[i], &blen)) continue;
        had_single[i] = 1;
        int oom = 0;
        char *decoded = mime_decode_2231_value(vals[i], &oom);
        if (!decoded) {
            if (oom) {
                free(had_single);
                return mime_apply_rfc2231_fail(keys, vals, n, nparams);
            }
            continue;
        }
        char *newkey = (char *)malloc(blen + 1);
        if (!newkey) {
            free(decoded);
            free(had_single);
            return mime_apply_rfc2231_fail(keys, vals, n, nparams);
        }
        memcpy(newkey, keys[i], blen);
        newkey[blen] = '\0';
        free(keys[i]);
        free(vals[i]);
        keys[i] = newkey;
        vals[i] = decoded;

        for (int j = 0; j < n; j++) {
            if (j == i || !keys[j]) continue;
            if (strcmp(keys[j], newkey) == 0) {
                free(keys[j]);
                free(vals[j]);
                keys[j] = vals[j] = NULL;
            }
        }
    }

    /* name*0 / name*1 / name*0* continuations. A name* single, success or
     * failure, suppresses stitching for that base (Go ParseMediaType). */
    for (int i = 0; i < n; i++) {
        if (!keys[i]) continue;
        size_t blen = 0;
        int idx = 0, encoded = 0;
        if (!mime_is_2231_cont(keys[i], &blen, &idx, &encoded)) continue;

        int skip = 0;
        for (int j = 0; j < n; j++) {
            if (!had_single[j] || !keys[j]) continue;
            if (mime_key_base_is(keys[j], keys[i], blen)) {
                skip = 1;
                break;
            }
        }
        if (skip) continue;

        int enc0 = 0;
        int p0 = mime_find_2231_cont(keys, n, keys[i], blen, 0, &enc0);
        if (p0 < 0) continue;

        int nidx = 0;
        for (;; nidx++) {
            if (mime_find_2231_cont(keys, n, keys[i], blen, nidx,
                                    NULL) < 0)
                break;
        }

        char **parts = (char **)calloc((size_t)nidx, sizeof(char *));
        unsigned char *owned = (unsigned char *)calloc((size_t)nidx, 1);
        if (!parts || !owned) {
            free(parts);
            free(owned);
            free(had_single);
            return mime_apply_rfc2231_fail(keys, vals, n, nparams);
        }

        size_t total = 0;
        int failed = 0;
        int stitch_ok = 1;
        for (int k = 0; k < nidx && !failed && stitch_ok; k++) {
            int enc = 0;
            int found = mime_find_2231_cont(keys, n, keys[i], blen, k, &enc);
            if (found < 0) {
                failed = 1;
                break;
            }
            if (enc && k == 0) {
                int oom = 0;
                parts[k] = mime_decode_2231_value(vals[found], &oom);
                owned[k] = 1;
                if (oom) failed = 1;
                else if (!parts[k]) stitch_ok = 0;
            } else if (enc) {
                int oom = 0;
                parts[k] = mime_percent_unescape(
                    vals[found], strlen(vals[found]), &oom);
                owned[k] = 1;
                if (oom) failed = 1;
                else if (!parts[k]) stitch_ok = 0;
            } else {
                parts[k] = vals[found];
            }
            if (parts[k]) total += strlen(parts[k]);
        }
        if (failed) {
            for (int k = 0; k < nidx; k++)
                if (owned[k]) free(parts[k]);
            free(parts);
            free(owned);
            free(had_single);
            return mime_apply_rfc2231_fail(keys, vals, n, nparams);
        }
        /* Bad charset, percent sequence, or CTL: drop the continuation
         * (same as a failed name*) instead of succeeding with "" / a
         * truncated stitch of the pieces that happened to decode. */
        if (!stitch_ok) {
            for (int k = 0; k < nidx; k++)
                if (owned[k]) free(parts[k]);
            free(parts);
            free(owned);
            continue;
        }

        char *combined = (char *)malloc(total + 1);
        char *newkey = (char *)malloc(blen + 1);
        if (!combined || !newkey) {
            free(combined);
            free(newkey);
            for (int k = 0; k < nidx; k++)
                if (owned[k]) free(parts[k]);
            free(parts);
            free(owned);
            free(had_single);
            return mime_apply_rfc2231_fail(keys, vals, n, nparams);
        }
        size_t pos = 0;
        for (int k = 0; k < nidx; k++) {
            if (parts[k]) {
                size_t plen = strlen(parts[k]);
                memcpy(combined + pos, parts[k], plen);
                pos += plen;
            }
            if (owned[k]) free(parts[k]);
        }
        combined[pos] = '\0';
        memcpy(newkey, keys[i], blen);
        newkey[blen] = '\0';
        free(parts);
        free(owned);

        for (int j = 0; j < n; j++) {
            if (j == p0 || !keys[j]) continue;
            if (strcmp(keys[j], newkey) == 0) {
                free(keys[j]);
                free(vals[j]);
                keys[j] = vals[j] = NULL;
            }
        }
        free(keys[p0]);
        free(vals[p0]);
        keys[p0] = newkey;
        vals[p0] = combined;
    }

    /* Unused * keys (failed name*, leftover pieces, name*01) are dropped. */
    for (int i = 0; i < n; i++) {
        if (keys[i] && strchr(keys[i], '*')) {
            free(keys[i]);
            free(vals[i]);
            keys[i] = vals[i] = NULL;
        }
    }
    free(had_single);

    int w = 0;
    for (int i = 0; i < n; i++) {
        if (!keys[i]) continue;
        keys[w] = keys[i];
        vals[w] = vals[i];
        w++;
    }
    *nparams = w;
    return 0;
}

int neverc_mime_parse_media_type(const char *v,
                                 char *media_type, size_t mt_cap,
                                 char *params_keys[], char *params_vals[],
                                 int max_params, int *nparams) {
    int count = 0;
    if (!nparams) {
        if (media_type && mt_cap) media_type[0] = '\0';
        return -1;
    }
    *nparams = 0;
    if (!v || !media_type || mt_cap == 0 || max_params < 0 ||
        (max_params > 0 && (!params_keys || !params_vals ||
                            params_keys == params_vals))) {
        if (media_type && mt_cap) media_type[0] = '\0';
        return -1;
    }

    const char *p = v;
    while (mime_is_ows((unsigned char)*p)) p++;
    const char *mt_start = p;
    while (mime_is_token_char((unsigned char)*p)) p++;
    if (p == mt_start || *p != '/') goto fail;
    p++;
    const char *subtype = p;
    while (mime_is_token_char((unsigned char)*p)) p++;
    if (p == subtype) goto fail;
    const char *mt_end = p;
    while (mime_is_ows((unsigned char)*p)) p++;
    if (*p != '\0' && *p != ';') goto fail;

    size_t mt_len = (size_t)(mt_end - mt_start);
    if (mt_len >= mt_cap) goto fail;

    while (*p == ';') {
        p++;
        while (mime_is_ows((unsigned char)*p)) p++;
        /* Go mime.ParseMediaType ignores a trailing semicolon. */
        if (!*p) break;

        const char *key_start = p;
        while (mime_is_token_char((unsigned char)*p)) p++;
        const char *key_end = p;
        if (key_end == key_start) goto fail;
        while (mime_is_ows((unsigned char)*p)) p++;
        if (*p != '=') goto fail;
        p++;
        while (mime_is_ows((unsigned char)*p)) p++;

        const char *value_start;
        const char *value_end;
        size_t value_len = 0;
        int quoted = 0;
        if (*p == '"') {
            quoted = 1;
            value_start = ++p;
            while (*p && *p != '"') {
                unsigned char c = (unsigned char)*p;
                if (c == '\r' || c == '\n') goto fail;
                if (c == '\\' && p[1] &&
                    mime_is_tspecial((unsigned char)p[1])) {
                    p++;
                    c = (unsigned char)*p++;
                    if (c == '\0' || c == '\r' || c == '\n') goto fail;
                } else {
                    p++;
                    if ((c < 32 && c != '\t') || c == 127) goto fail;
                }
                if (value_len == SIZE_MAX) goto fail;
                value_len++;
            }
            if (*p != '"') goto fail;
            value_end = p++;
        } else {
            value_start = p;
            while (mime_is_token_char((unsigned char)*p)) p++;
            value_end = p;
            if (value_end == value_start) goto fail;
            value_len = (size_t)(value_end - value_start);
        }

        while (mime_is_ows((unsigned char)*p)) p++;
        if (*p != '\0' && *p != ';') goto fail;
        if (count >= max_params) goto fail;

        size_t key_len = (size_t)(key_end - key_start);
        if (key_len == SIZE_MAX || value_len == SIZE_MAX) goto fail;
        char *key = (char *)malloc(key_len + 1);
        if (!key) goto fail;
        char *value = (char *)malloc(value_len + 1);
        if (!value) {
            free(key);
            goto fail;
        }

        for (size_t i = 0; i < key_len; i++)
            key[i] = (char)nc_tolower((unsigned char)key_start[i]);
        key[key_len] = '\0';
        if (quoted) {
            const char *from = value_start;
            size_t to = 0;
            while (from < value_end) {
                if (*from == '\\' && from + 1 < value_end &&
                    mime_is_tspecial((unsigned char)from[1]))
                    from++;
                value[to++] = *from++;
            }
            value[to] = '\0';
        } else {
            memcpy(value, value_start, value_len);
            value[value_len] = '\0';
        }
        int dup = -1;
        for (int i = 0; i < count; i++) {
            if (strcmp(params_keys[i], key) == 0) {
                dup = i;
                break;
            }
        }
        if (dup >= 0) {
            /* Go allows a repeated parameter when the values are identical. */
            int same = strcmp(params_vals[dup], value) == 0;
            free(key);
            free(value);
            if (!same) goto fail;
            continue;
        }
        params_keys[count] = key;
        params_vals[count] = value;
        count++;
    }

    if (*p != '\0') goto fail;
    if (mime_apply_rfc2231(params_keys, params_vals, &count) != 0) {
        media_type[0] = '\0';
        *nparams = 0;
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (!nci_rfc2047_header_safe(params_vals[i], strlen(params_vals[i])))
            goto fail;
    }
    memmove(media_type, mt_start, mt_len);
    for (size_t i = 0; i < mt_len; i++)
        media_type[i] = (char)nc_tolower((unsigned char)media_type[i]);
    media_type[mt_len] = '\0';
    *nparams = count;
    return 0;

fail:
    if (params_keys && params_vals) mime_free_params(params_keys, params_vals, count);
    media_type[0] = '\0';
    *nparams = 0;
    return -1;
}

static int mime_size_add(size_t *total, size_t amount) {
    if (amount > SIZE_MAX - *total) return -1;
    *total += amount;
    return 0;
}

int neverc_mime_format_media_type(const char *media_type,
                                  const char *param_keys[],
                                  const char *param_vals[],
                                  int nparams,
                                  char *out, size_t out_cap) {
    if (!out || out_cap == 0) return -1;
    if (!media_type || nparams < 0 ||
        (nparams > 0 && (!param_keys || !param_vals)))
        goto fail;

    const char *slash = media_type;
    while (mime_is_token_char((unsigned char)*slash)) slash++;
    if (slash == media_type || *slash != '/') goto fail;
    const char *subtype = slash + 1;
    const char *end = subtype;
    while (mime_is_token_char((unsigned char)*end)) end++;
    if (end == subtype || *end != '\0') goto fail;

    size_t total = (size_t)(end - media_type);
    for (int i = 0; i < nparams; i++) {
        const char *key = param_keys[i];
        const char *value = param_vals[i];
        if (!key || !value || !*key) goto fail;
        size_t key_len = 0;
        while (mime_is_token_char((unsigned char)key[key_len])) key_len++;
        if (key[key_len] != '\0') goto fail;
        if (key_len == 8 && strcasecmp_local(key, "boundary") == 0 &&
            !mime_is_rfc2046_boundary(value))
            goto fail;
        if (!nci_rfc2047_header_safe(value, strlen(value)))
            goto fail;
        for (int j = 0; j < i; j++) {
            if (strcasecmp_local(key, param_keys[j]) == 0) goto fail;
        }

        size_t value_len = 0;
        int quote = *value == '\0';
        for (const unsigned char *s = (const unsigned char *)value; *s; s++) {
            if ((*s < 32 && *s != '\t') || *s == 127) goto fail;
            if (!mime_is_token_char(*s)) quote = 1;
            if (*s == '"' || *s == '\\') {
                if (value_len == SIZE_MAX) goto fail;
                value_len++;
            }
            if (value_len == SIZE_MAX) goto fail;
            value_len++;
        }

        if (mime_size_add(&total, 2) != 0 ||
            mime_size_add(&total, key_len) != 0 ||
            mime_size_add(&total, 1) != 0 ||
            mime_size_add(&total, value_len) != 0 ||
            (quote && mime_size_add(&total, 2) != 0))
            goto fail;
    }
    if (total >= out_cap || total > (size_t)INT_MAX) goto fail;

    size_t pos = 0;
    for (const char *s = media_type; *s; s++)
        out[pos++] = (char)nc_tolower((unsigned char)*s);
    for (int i = 0; i < nparams; i++) {
        const char *key = param_keys[i];
        const char *value = param_vals[i];
        int quote = *value == '\0';
        for (const unsigned char *s = (const unsigned char *)value; *s; s++)
            if (!mime_is_token_char(*s)) quote = 1;

        out[pos++] = ';';
        out[pos++] = ' ';
        while (*key)
            out[pos++] = (char)nc_tolower((unsigned char)*key++);
        out[pos++] = '=';
        if (quote) out[pos++] = '"';
        while (*value) {
            if (*value == '"' || *value == '\\') out[pos++] = '\\';
            out[pos++] = *value++;
        }
        if (quote) out[pos++] = '"';
    }
    out[pos] = '\0';
    return (int)pos;

fail:
    out[0] = '\0';
    return -1;
}

/* ===== RFC 2047 encoded-words ============================================ */

static int mime_room(size_t used, size_t need, size_t cap) {
    return used <= cap && need <= cap - used;
}

static int mime_output_has_ctl(const char *s, size_t n) {
    return nci_2047_has_header_break((const unsigned char *)s, n, 1);
}

static size_t mime_find(const char *s, size_t n, const char *needle, size_t nn) {
    if (nn == 0 || n < nn) return SIZE_MAX;
    for (size_t i = 0; i + nn <= n; i++) {
        if (memcmp(s + i, needle, nn) == 0) return i;
    }
    return SIZE_MAX;
}

static int mime_2047_charset(const char *s, size_t n) {
    if (mime_span_case_equal(s, n, "utf-8", 5)) return 1;
    if (mime_span_case_equal(s, n, "us-ascii", 8)) return 2;
    if (mime_span_case_equal(s, n, "iso-8859-1", 10)) return 3;
    return 0;
}

static int mime_2047_has_non_wsp(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            return 1;
    }
    return 0;
}

static int mime_2047_q_decode(const char *s, size_t n,
                              unsigned char *out, size_t cap, size_t *olen) {
    size_t di = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '_') {
            c = ' ';
        } else if (c == '=') {
            if (i + 2 >= n) return -1;
            int hi = mime_hex_digit((unsigned char)s[i + 1]);
            int lo = mime_hex_digit((unsigned char)s[i + 2]);
            if (hi < 0 || lo < 0) return -1;
            c = (unsigned char)((hi << 4) | lo);
            i += 2;
        } else if (c > 126 || (c < 32 && c != '\t')) {
            return -1;
        }
        if (di >= cap) return -2;
        out[di++] = c;
    }
    *olen = di;
    return 0;
}

static int mime_2047_b64(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int mime_2047_b_decode(const char *s, size_t n,
                              unsigned char *out, size_t cap, size_t *olen) {
    char pad_stack[260];
    char *pad_heap = NULL;
    const char *src = s;
    size_t nsrc = n;
    if (n % 4 == 1) return -1;
    if (n % 4 != 0) {
        size_t pad = 4 - (n % 4);
        if (n > SIZE_MAX - pad) return -1;
        if (n + pad <= sizeof(pad_stack)) {
            memcpy(pad_stack, s, n);
            memset(pad_stack + n, '=', pad);
            src = pad_stack;
        } else {
            pad_heap = (char *)malloc(n + pad);
            if (!pad_heap) return -2;
            memcpy(pad_heap, s, n);
            memset(pad_heap + n, '=', pad);
            src = pad_heap;
        }
        nsrc = n + pad;
    }
    size_t di = 0;
    for (size_t i = 0; i < nsrc; i += 4) {
        int last = (i + 4 >= nsrc);
        if (!last && (src[i + 2] == '=' || src[i + 3] == '=')) {
            free(pad_heap);
            return -1;
        }
        if (src[i + 2] == '=' && src[i + 3] != '=') {
            free(pad_heap);
            return -1;
        }
        int v0 = mime_2047_b64((unsigned char)src[i]);
        int v1 = mime_2047_b64((unsigned char)src[i + 1]);
        if (v0 < 0 || v1 < 0) {
            free(pad_heap);
            return -1;
        }
        int v2 = 0, v3 = 0;
        if (src[i + 2] != '=') {
            v2 = mime_2047_b64((unsigned char)src[i + 2]);
            if (v2 < 0) {
                free(pad_heap);
                return -1;
            }
        }
        if (src[i + 3] != '=') {
            v3 = mime_2047_b64((unsigned char)src[i + 3]);
            if (v3 < 0) {
                free(pad_heap);
                return -1;
            }
        }
        if (!mime_room(di, 1, cap)) {
            free(pad_heap);
            return -2;
        }
        out[di++] = (unsigned char)((v0 << 2) | (v1 >> 4));
        if (src[i + 2] != '=') {
            if (!mime_room(di, 1, cap)) {
                free(pad_heap);
                return -2;
            }
            out[di++] = (unsigned char)((v1 << 4) | (v2 >> 2));
        }
        if (src[i + 3] != '=') {
            if (!mime_room(di, 1, cap)) {
                free(pad_heap);
                return -2;
            }
            out[di++] = (unsigned char)((v2 << 6) | v3);
        }
    }
    free(pad_heap);
    *olen = di;
    return 0;
}

static int mime_2047_emit(int cs, const unsigned char *in, size_t n,
                          char *out, size_t cap, size_t *di) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = in[i];
        if (cs == 1) {
            if (!mime_room(*di, 1, cap)) return -1;
            out[(*di)++] = (char)c;
        } else if (cs == 2) {
            if (c >= 0x80) {
                if (!mime_room(*di, 3, cap)) return -1;
                out[(*di)++] = (char)0xEF;
                out[(*di)++] = (char)0xBF;
                out[(*di)++] = (char)0xBD;
            } else {
                if (!mime_room(*di, 1, cap)) return -1;
                out[(*di)++] = (char)c;
            }
        } else {
            if (c < 0x80) {
                if (!mime_room(*di, 1, cap)) return -1;
                out[(*di)++] = (char)c;
            } else {
                if (!mime_room(*di, 2, cap)) return -1;
                out[(*di)++] = (char)(0xC0 | (c >> 6));
                out[(*di)++] = (char)(0x80 | (c & 0x3F));
            }
        }
    }
    return 0;
}

int neverc_mime_decode_header(const char *src, size_t src_len,
                              char *out, size_t out_cap, size_t *out_len) {
    if (out_len) *out_len = 0;
    if ((!src || !out) && src_len != 0) return -1;
    if (src_len == 0) return 0;

    size_t di = 0;
    size_t mark = mime_find(src, src_len, "=?", 2);
    if (mark == SIZE_MAX) {
        if (mime_output_has_ctl(src, src_len) ||
            !nci_2047_utf8_ok((const unsigned char *)src, src_len))
            return -1;
        if (!mime_room(0, src_len, out_cap)) return -1;
        if (src_len) memcpy(out, src, src_len);
        if (out_len) *out_len = src_len;
        return 0;
    }

    unsigned char stack_dec[128];
    size_t pos = 0;
    int between_words = 0;
    while (pos < src_len) {
        size_t start = mime_find(src + pos, src_len - pos, "=?", 2);
        if (start == SIZE_MAX) break;
        start += pos;
        size_t cur = start + 2;
        size_t q1 = mime_find(src + cur, src_len - cur, "?", 1);
        if (q1 == SIZE_MAX) {
            size_t lit = start + 1 - pos;
            if (!mime_room(di, lit, out_cap)) return -1;
            memcpy(out + di, src + pos, lit);
            di += lit;
            pos = start + 1;
            between_words = 0;
            continue;
        }
        const char *charset = src + cur;
        size_t clen = q1;
        cur += q1 + 1;
        if (cur + 4 > src_len) {
            size_t lit = start + 1 - pos;
            if (!mime_room(di, lit, out_cap)) return -1;
            memcpy(out + di, src + pos, lit);
            di += lit;
            pos = start + 1;
            between_words = 0;
            continue;
        }
        unsigned char enc = (unsigned char)src[cur];
        cur++;
        if (src[cur] != '?') {
            size_t lit = start + 1 - pos;
            if (!mime_room(di, lit, out_cap)) return -1;
            memcpy(out + di, src + pos, lit);
            di += lit;
            pos = start + 1;
            between_words = 0;
            continue;
        }
        cur++;
        size_t qe = mime_find(src + cur, src_len - cur, "?=", 2);
        if (qe == SIZE_MAX) {
            size_t lit = start + 1 - pos;
            if (!mime_room(di, lit, out_cap)) return -1;
            memcpy(out + di, src + pos, lit);
            di += lit;
            pos = start + 1;
            between_words = 0;
            continue;
        }
        const char *text = src + cur;
        size_t tlen = qe;
        size_t end = cur + qe + 2;

        unsigned char *decoded = stack_dec;
        unsigned char *heap_dec = NULL;
        size_t dec_cap = sizeof(stack_dec);
        if (tlen > dec_cap) {
            if (tlen == SIZE_MAX) return -1;
            heap_dec = (unsigned char *)malloc(tlen + 1);
            if (!heap_dec) return -1;
            decoded = heap_dec;
            dec_cap = tlen + 1;
        }

        size_t dlen = 0;
        int dec_rc = 1;
        if (enc == 'Q' || enc == 'q')
            dec_rc = mime_2047_q_decode(text, tlen, decoded, dec_cap, &dlen);
        else if (enc == 'B' || enc == 'b')
            dec_rc = mime_2047_b_decode(text, tlen, decoded, dec_cap, &dlen);
        if (dec_rc == -2) {
            free(heap_dec);
            return -1;
        }
        if (dec_rc != 0) {
            free(heap_dec);
            size_t lit = end - pos;
            if (!mime_room(di, lit, out_cap)) return -1;
            memcpy(out + di, src + pos, lit);
            di += lit;
            pos = end;
            between_words = 0;
            continue;
        }
        int cs = mime_2047_charset(charset, clen);
        if (!cs || (cs == 1 && !nci_2047_utf8_ok(decoded, dlen))) {
            free(heap_dec);
            return -1;
        }
        for (size_t k = 0; k + 1 < dlen; k++) {
            if (decoded[k] == '=' && decoded[k + 1] == '?') {
                free(heap_dec);
                return -1;
            }
        }
        if (start > pos &&
            (!between_words || mime_2047_has_non_wsp(src + pos, start - pos))) {
            size_t lit = start - pos;
            if (!mime_room(di, lit, out_cap)) {
                free(heap_dec);
                return -1;
            }
            memcpy(out + di, src + pos, lit);
            di += lit;
        }
        if (mime_2047_emit(cs, decoded, dlen, out, out_cap, &di) != 0) {
            free(heap_dec);
            return -1;
        }
        free(heap_dec);
        pos = end;
        between_words = 1;
    }
    if (pos < src_len) {
        size_t lit = src_len - pos;
        if (!mime_room(di, lit, out_cap)) return -1;
        memcpy(out + di, src + pos, lit);
        di += lit;
    }
    if (mime_output_has_ctl(out, di) ||
        !nci_2047_utf8_ok((const unsigned char *)out, di))
        return -1;
    /* Adjacent encoded-words are concatenated after the per-word `=?`
     * check. Reject a stitch that rebuilds a nested encoded-word. */
    for (size_t k = 0; k + 1 < di; k++) {
        if (out[k] == '=' && out[k + 1] == '?')
            return -1;
    }
    if (out_len) *out_len = di;
    return 0;
}

/* RFC 2045 line ending for soft breaks / trailing WSP: LF, CRLF, or a
 * CR only at EOF. A bare CR in the middle of a line is not a break. */
static int mime_qp_is_line_end(const char *src, size_t src_len, size_t j) {
    if (j >= src_len) return 1;
    if (src[j] == '\n') return 1;
    if (src[j] == '\r')
        return j + 1 >= src_len || src[j + 1] == '\n';
    return 0;
}

static int mime_qp_need(size_t di, size_t n, size_t cap) {
    return di > cap || n > cap - di;
}

/* Hex value per byte, -1 for non-hex. A compile-time constant table: it is
 * immutable and shared, so the decoder is reentrant/thread-safe with no
 * lazy-init data race (a lazily built table can be observed half-initialized
 * by another thread on weakly-ordered targets such as arm64). */
static const signed char mime_qp_hex[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
     0, 1, 2, 3, 4, 5, 6, 7,  8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
};

int neverc_mime_qp_decode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len) {
    if (out_len) *out_len = 0;
    if ((!src || !dst) && src_len != 0) return -1;

    size_t si = 0, di = 0;
    int line_has_content = 0;
    while (si < src_len) {
        unsigned char c = (unsigned char)src[si];

        if (c == '=') {
            if (si + 2 < src_len) {
                int high = mime_qp_hex[(unsigned char)src[si + 1]];
                int low = mime_qp_hex[(unsigned char)src[si + 2]];
                if (high >= 0 && low >= 0) {
                    if (di >= dst_cap) return -1;
                    dst[di++] = (char)((high << 4) | low);
                    line_has_content = 1;
                    si += 3;
                    continue;
                }
            }
            /* Soft break: '=' WSP* (CRLF | LF | CR-at-EOF | EOF). */
            size_t j = si + 1;
            while (j < src_len && (src[j] == ' ' || src[j] == '\t'))
                j++;
            if (!mime_qp_is_line_end(src, src_len, j))
                return -1;
            if (j >= src_len) {
                /* Go issue 15486: leftover '=' at EOF is a soft break
                 * only on a non-empty line. Lone "=" is invalid. */
                if (!line_has_content) return -1;
                si = j;
                continue;
            }
            if (src[j] == '\n') {
                si = j + 1;
                line_has_content = 0;
                continue;
            }
            si = j + 1;
            if (si < src_len && src[si] == '\n')
                si++;
            line_has_content = 0;
            continue;
        }

        if (c == ' ' || c == '\t') {
            size_t j = si;
            while (j < src_len && (src[j] == ' ' || src[j] == '\t'))
                j++;
            if (mime_qp_is_line_end(src, src_len, j)) {
                si = j;
                continue;
            }
            if (mime_qp_need(di, j - si, dst_cap)) return -1;
            memcpy(dst + di, src + si, j - si);
            di += j - si;
            line_has_content = 1;
            si = j;
            continue;
        }

        if ((c < 0x20 && c != '\t' && c != '\r' && c != '\n') || c > 0x7e)
            return -1;
        if (di >= dst_cap) return -1;
        dst[di++] = src[si++];
        /* Mid-line CR is content (Go issue 13219). Leftover '=' at EOF
         * is a soft break only on that same non-empty line (issue 15486). */
        if (c == '\n' ||
            (c == '\r' && mime_qp_is_line_end(src, src_len, si - 1)))
            line_has_content = 0;
        else
            line_has_content = 1;
    }
    if (out_len) *out_len = di;
    return 0;
}

static const char hex_chars[] = "0123456789ABCDEF";

/* RFC 2045: encoded lines are at most 76 chars, not counting the CRLF.
 * Soft break `=\r\n` uses the 76th column for `=`. */
#define MIME_QP_MAX_LINE 76

static size_t mime_qp_token_len(unsigned char c, int trailing_ws,
                                int encode_wsp) {
    if ((c >= 33 && c <= 126 && c != '=') ||
        ((c == '\t' || c == ' ') && !trailing_ws && !encode_wsp))
        return 1;
    return 3;
}

int neverc_mime_qp_encode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len) {
    if (out_len) *out_len = 0;
    if ((!src || !dst) && src_len != 0) return -1;

    const size_t line_cap = (size_t)(MIME_QP_MAX_LINE - 1);
    size_t required = 0, line_len = 0;
    for (size_t i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\r' && i + 1 < src_len && src[i + 1] == '\n') {
            if (required > SIZE_MAX - 2U) return -1;
            required += 2U;
            line_len = 0;
            i++;
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (line_len + 3U > line_cap) {
                if (required > SIZE_MAX - 3U) return -1;
                required += 3U;
                line_len = 0;
            }
            if (required > SIZE_MAX - 3U) return -1;
            required += 3U;
            line_len += 3U;
            continue;
        }
        int trailing_ws = (c == ' ' || c == '\t') &&
                          (i + 1 == src_len ||
                           src[i + 1] == '\r' || src[i + 1] == '\n');
        int encode_wsp = (c == ' ' || c == '\t') && !trailing_ws &&
                         line_len + 1 >= line_cap;
        size_t amount = mime_qp_token_len(c, trailing_ws, encode_wsp);
        if (line_len + amount > line_cap) {
            if (required > SIZE_MAX - 3U) return -1;
            required += 3U;
            line_len = 0;
            encode_wsp = (c == ' ' || c == '\t') && !trailing_ws &&
                         line_len + 1 >= line_cap;
            amount = mime_qp_token_len(c, trailing_ws, encode_wsp);
        }
        if (amount > SIZE_MAX - required) return -1;
        required += amount;
        line_len += amount;
    }
    if (required > dst_cap) return -1;

    size_t di = 0;
    line_len = 0;
    for (size_t si = 0; si < src_len; si++) {
        unsigned char c = (unsigned char)src[si];
        if (c == '\r' && si + 1 < src_len && src[si + 1] == '\n') {
            if (mime_qp_need(di, 2, dst_cap)) return -1;
            dst[di++] = '\r';
            dst[di++] = '\n';
            line_len = 0;
            si++;
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (line_len + 3U > line_cap) {
                if (mime_qp_need(di, 3, dst_cap)) return -1;
                dst[di++] = '=';
                dst[di++] = '\r';
                dst[di++] = '\n';
                line_len = 0;
            }
            if (mime_qp_need(di, 3, dst_cap)) return -1;
            dst[di++] = '=';
            dst[di++] = hex_chars[c >> 4];
            dst[di++] = hex_chars[c & 0x0F];
            line_len += 3;
            continue;
        }
        int trailing_ws = (c == ' ' || c == '\t') &&
                          (si + 1 == src_len ||
                           src[si + 1] == '\r' || src[si + 1] == '\n');
        int encode_wsp = (c == ' ' || c == '\t') && !trailing_ws &&
                         line_len + 1 >= line_cap;
        size_t amount = mime_qp_token_len(c, trailing_ws, encode_wsp);
        if (line_len + amount > line_cap) {
            if (mime_qp_need(di, 3, dst_cap)) return -1;
            dst[di++] = '=';
            dst[di++] = '\r';
            dst[di++] = '\n';
            line_len = 0;
            encode_wsp = (c == ' ' || c == '\t') && !trailing_ws &&
                         line_len + 1 >= line_cap;
            amount = mime_qp_token_len(c, trailing_ws, encode_wsp);
        }
        if (amount == 1) {
            if (mime_qp_need(di, 1, dst_cap)) return -1;
            dst[di++] = (char)c;
        } else {
            if (mime_qp_need(di, 3, dst_cap)) return -1;
            dst[di++] = '=';
            dst[di++] = hex_chars[c >> 4];
            dst[di++] = hex_chars[c & 0x0F];
        }
        line_len += amount;
    }
    if (out_len) *out_len = di;
    return 0;
}
