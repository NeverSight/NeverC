#include "neverc/std/mime.h"
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
        if (!*p) goto fail;

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
                unsigned char c = (unsigned char)*p++;
                if (c == '\\') {
                    c = (unsigned char)*p++;
                    if (c == '\0' || c == '\r' || c == '\n') goto fail;
                } else if ((c < 32 && c != '\t') || c == 127) {
                    goto fail;
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
        for (int i = 0; i < count; i++) {
            if (mime_span_case_equal(params_keys[i], strlen(params_keys[i]),
                                     key_start, key_len))
                goto fail;
        }
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
                if (*from == '\\') from++;
                value[to++] = *from++;
            }
            value[to] = '\0';
        } else {
            memcpy(value, value_start, value_len);
            value[value_len] = '\0';
        }
        params_keys[count] = key;
        params_vals[count] = value;
        count++;
    }

    if (*p != '\0') goto fail;
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

static size_t mime_qp_decoded_size(const char *src, size_t src_len) {
    size_t si = 0, size = 0;
    while (si < src_len) {
        size_t remaining = src_len - si;
        if (src[si] == '=' && remaining >= 3 &&
            src[si + 1] == '\r' && src[si + 2] == '\n') {
            si += 3;
        } else if (src[si] == '=' && remaining >= 2 &&
                   src[si + 1] == '\n') {
            si += 2;
        } else if (src[si] == '=' && remaining >= 2 &&
                   src[si + 1] == '\r') {
            si += 2;
            if (si < src_len && src[si] == '\n')
                si++;
        } else if (src[si] == '=' && remaining == 1) {
            si += 1;
        } else if (src[si] == '=' && remaining >= 3 &&
                   mime_qp_hex[(unsigned char)src[si + 1]] >= 0 &&
                   mime_qp_hex[(unsigned char)src[si + 2]] >= 0) {
            si += 3;
            size++;
        } else if (src[si] == '=') {
            /* Invalid / truncated escape: decode will fail. Overestimate. */
            size += remaining;
            break;
        } else {
            si++;
            size++;
        }
    }
    return size;
}

int neverc_mime_qp_decode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len) {
    if (out_len) *out_len = 0;
    if ((!src || !dst) && src_len != 0) return -1;
    size_t required = mime_qp_decoded_size(src, src_len);
    if (required > dst_cap) return -1;

    size_t si = 0, di = 0;
    while (si < src_len) {
        size_t remaining = src_len - si;
        if (src[si] == '=' && remaining >= 3 &&
            src[si + 1] == '\r' && src[si + 2] == '\n') {
            si += 3;
        } else if (src[si] == '=' && remaining >= 2 &&
                   src[si + 1] == '\n') {
            si += 2;
        } else if (src[si] == '=' && remaining >= 2 &&
                   src[si + 1] == '\r') {
            si += 2;
            if (si < src_len && src[si] == '\n')
                si++;
        } else if (src[si] == '=' && remaining == 1) {
            si += 1;
        } else if (src[si] == '=' && remaining >= 3) {
            int high = mime_qp_hex[(unsigned char)src[si + 1]];
            int low = mime_qp_hex[(unsigned char)src[si + 2]];
            if (high >= 0 && low >= 0) {
                dst[di++] = (char)((high << 4) | low);
                si += 3;
            } else {
                return -1;
            }
        } else if (src[si] == '=') {
            return -1;
        } else {
            dst[di++] = src[si++];
        }
    }
    if (out_len) *out_len = di;
    return 0;
}

static const char hex_chars[] = "0123456789ABCDEF";

int neverc_mime_qp_encode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len) {
    if (out_len) *out_len = 0;
    if ((!src || !dst) && src_len != 0) return -1;

    size_t required = 0;
    for (size_t i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        int trailing_ws = (c == ' ' || c == '\t') &&
                          (i + 1 == src_len ||
                           src[i + 1] == '\r' || src[i + 1] == '\n');
        size_t amount = ((c >= 33 && c <= 126 && c != '=') ||
                         ((c == '\t' || c == ' ') && !trailing_ws) ||
                         c == '\r' || c == '\n')
                            ? 1U : 3U;
        if (amount > SIZE_MAX - required) return -1;
        required += amount;
    }
    if (required > dst_cap) return -1;

    size_t di = 0;
    for (size_t si = 0; si < src_len; si++) {
        unsigned char c = (unsigned char)src[si];
        int trailing_ws = (c == ' ' || c == '\t') &&
                          (si + 1 == src_len ||
                           src[si + 1] == '\r' || src[si + 1] == '\n');
        if ((c >= 33 && c <= 126 && c != '=') ||
            ((c == '\t' || c == ' ') && !trailing_ws) ||
            c == '\r' || c == '\n') {
            dst[di++] = (char)c;
        } else {
            dst[di++] = '=';
            dst[di++] = hex_chars[c >> 4];
            dst[di++] = hex_chars[c & 0x0F];
        }
    }
    if (out_len) *out_len = di;
    return 0;
}
