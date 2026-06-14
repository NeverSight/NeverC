#include "neverc/std/mime.h"
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

static int nc_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
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
    for (const mime_entry_t *e = mime_table; e->ext; e++) {
        size_t mlen = strlen(e->mime);
        if (strncmp(e->mime, mime_type, mlen) == 0 &&
            (mime_type[mlen] == '\0' || mime_type[mlen] == ';'))
            return e->ext;
    }
    return NULL;
}

int neverc_mime_parse_media_type(const char *v,
                                 char *media_type, size_t mt_cap,
                                 char *params_keys[], char *params_vals[],
                                 int max_params, int *nparams) {
    if (!v || !media_type) return -1;
    *nparams = 0;

    while (*v && nc_isspace((unsigned char)*v)) v++;

    const char *semi = strchr(v, ';');
    size_t mt_len = semi ? (size_t)(semi - v) : strlen(v);
    while (mt_len > 0 && nc_isspace((unsigned char)v[mt_len - 1])) mt_len--;

    if (mt_len >= mt_cap) mt_len = mt_cap - 1;
    memcpy(media_type, v, mt_len);
    media_type[mt_len] = '\0';

    for (size_t i = 0; i < mt_len; i++)
        media_type[i] = (char)nc_tolower((unsigned char)media_type[i]);

    if (!semi) return 0;
    const char *p = semi + 1;

    while (*p && *nparams < max_params) {
        while (*p && nc_isspace((unsigned char)*p)) p++;
        if (!*p) break;

        const char *eq = strchr(p, '=');
        if (!eq) break;

        size_t klen = (size_t)(eq - p);
        while (klen > 0 && nc_isspace((unsigned char)p[klen - 1])) klen--;

        if (params_keys && params_vals) {
            char *key = (char *)malloc(klen + 1);
            memcpy(key, p, klen);
            key[klen] = '\0';
            for (size_t i = 0; i < klen; i++)
                key[i] = (char)nc_tolower((unsigned char)key[i]);

            const char *vs = eq + 1;
            while (*vs && nc_isspace((unsigned char)*vs)) vs++;

            const char *ve;
            if (*vs == '"') {
                vs++;
                ve = strchr(vs, '"');
                if (!ve) ve = vs + strlen(vs);
            } else {
                ve = vs;
                while (*ve && *ve != ';' && !nc_isspace((unsigned char)*ve)) ve++;
            }

            size_t vlen = (size_t)(ve - vs);
            char *val = (char *)malloc(vlen + 1);
            memcpy(val, vs, vlen);
            val[vlen] = '\0';

            params_keys[*nparams] = key;
            params_vals[*nparams] = val;
        }
        (*nparams)++;

        p = strchr(eq + 1, ';');
        if (!p) break;
        p++;
    }

    return 0;
}

int neverc_mime_format_media_type(const char *media_type,
                                  const char *param_keys[],
                                  const char *param_vals[],
                                  int nparams,
                                  char *out, size_t out_cap) {
    if (!media_type || !out || out_cap == 0) return -1;

    size_t pos = 0;
    size_t mt_len = strlen(media_type);
    if (mt_len >= out_cap) return -1;
    memcpy(out, media_type, mt_len);
    pos = mt_len;

    for (int i = 0; i < nparams && param_keys && param_vals; i++) {
        const char *k = param_keys[i];
        const char *v = param_vals[i];
        size_t klen = strlen(k);
        size_t vlen = strlen(v);

        int need_quote = 0;
        for (size_t j = 0; j < vlen; j++) {
            if (v[j] == ' ' || v[j] == ';' || v[j] == '"') {
                need_quote = 1;
                break;
            }
        }

        size_t needed = 2 + klen + 1 + vlen + (need_quote ? 2 : 0);
        if (pos + needed >= out_cap) return -1;

        out[pos++] = ';';
        out[pos++] = ' ';
        memcpy(out + pos, k, klen); pos += klen;
        out[pos++] = '=';
        if (need_quote) out[pos++] = '"';
        memcpy(out + pos, v, vlen); pos += vlen;
        if (need_quote) out[pos++] = '"';
    }

    out[pos] = '\0';
    return (int)pos;
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
    size_t si = 0, di = 0;
    while (si < src_len && di < dst_cap) {
        if (src[si] == '=') {
            /* Tight loop for consecutive =XX hex escapes. */
            if (si + 2 < src_len &&
                src[si + 1] != '\r' && src[si + 1] != '\n') {
                int h = mime_qp_hex[(unsigned char)src[si + 1]];
                int l = mime_qp_hex[(unsigned char)src[si + 2]];
                if (h >= 0 && l >= 0) {
                    do {
                        dst[di++] = (char)((h << 4) | l);
                        si += 3;
                        if (di >= dst_cap ||
                            si + 2 >= src_len || src[si] != '=' ||
                            src[si + 1] == '\r' || src[si + 1] == '\n')
                            break;
                        h = mime_qp_hex[(unsigned char)src[si + 1]];
                        l = mime_qp_hex[(unsigned char)src[si + 2]];
                    } while (h >= 0 && l >= 0);
                    continue;
                }
            }
            if (si + 2 < src_len) {
                if (src[si + 1] == '\r' && si + 3 <= src_len && src[si + 2] == '\n') {
                    si += 3;
                    continue;
                }
                if (src[si + 1] == '\n') {
                    si += 2;
                    continue;
                }
                {
                    int h = mime_qp_hex[(unsigned char)src[si + 1]];
                    int l = mime_qp_hex[(unsigned char)src[si + 2]];
                    if (h >= 0 && l >= 0) {
                        dst[di++] = (char)((h << 4) | l);
                        si += 3;
                        continue;
                    }
                }
            }
            dst[di++] = src[si++];
            continue;
        }

        /* Literal run up to the next '='. */
        {
            const char *from = src + si;
            size_t rem = src_len - si;
            const char *eq = memchr(from, '=', rem);
            size_t run = eq ? (size_t)(eq - from) : rem;
            if (run > dst_cap - di) run = dst_cap - di;
            if (run) {
                memcpy(dst + di, from, run);
                di += run;
                si += run;
            }
        }
    }
    if (out_len) *out_len = di;
    return 0;
}

static const char hex_chars[] = "0123456789ABCDEF";

int neverc_mime_qp_encode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len) {
    size_t si = 0, di = 0;
    while (si < src_len) {
        unsigned char c = (unsigned char)src[si];
        if ((c >= 33 && c <= 126 && c != '=') || c == '\t' || c == ' ') {
            if (di >= dst_cap) break;
            dst[di++] = (char)c;
        } else if (c == '\r' || c == '\n') {
            if (di >= dst_cap) break;
            dst[di++] = (char)c;
        } else {
            if (di + 3 > dst_cap) break;
            dst[di++] = '=';
            dst[di++] = hex_chars[c >> 4];
            dst[di++] = hex_chars[c & 0x0F];
        }
        si++;
    }
    if (out_len) *out_len = di;
    return 0;
}
