#include "neverc/std/net/textproto.h"
#include <stdlib.h>
#include <string.h>

static int nc_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int nc_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int nc_isdigit(int c) { return c >= '0' && c <= '9'; }

void neverc_mime_header_init(neverc_mime_header_t *h) {
    memset(h, 0, sizeof(*h));
    h->capacity = 8;
    h->keys   = (char **)calloc(h->capacity, sizeof(char *));
    h->values = (char **)calloc(h->capacity, sizeof(char *));
}

void neverc_mime_header_free(neverc_mime_header_t *h) {
    for (size_t i = 0; i < h->count; i++) {
        free(h->keys[i]); free(h->values[i]);
    }
    free(h->keys); free(h->values);
    memset(h, 0, sizeof(*h));
}

char *neverc_textproto_canonical_mime_header_key(const char *key) {
    if (!key) return NULL;
    size_t len = strlen(key);
    char *out = (char *)malloc(len + 1);
    int upper = 1;
    for (size_t i = 0; i < len; i++) {
        if (key[i] == '-') {
            out[i] = '-';
            upper = 1;
        } else if (upper) {
            out[i] = (char)nc_toupper((unsigned char)key[i]);
            upper = 0;
        } else {
            out[i] = (char)nc_tolower((unsigned char)key[i]);
        }
    }
    out[len] = '\0';
    return out;
}

/*
 * Compare an already-canonical stored key against an arbitrary lookup key,
 * canonicalizing the lookup key on the fly. Returns 1 on match.
 *
 * The previous canon_key_cmp() canonicalized *both* sides into freshly
 * malloc'd buffers on every comparison — 2 mallocs + 2 frees per stored
 * header, per get/set/del. Since stored keys are canonical at insertion time,
 * only the lookup key needs normalizing, and that can be done allocation-free.
 */
static int canon_eq(const char *canonical, const char *key) {
    int upper = 1;
    size_t i = 0;
    for (; key[i]; i++) {
        char kc = key[i], c;
        if (kc == '-')      { c = '-'; upper = 1; }
        else if (upper)     { c = (char)nc_toupper((unsigned char)kc); upper = 0; }
        else                { c = (char)nc_tolower((unsigned char)kc); }
        if (canonical[i] != c) return 0;
    }
    return canonical[i] == '\0';
}

void neverc_mime_header_add(neverc_mime_header_t *h, const char *key, const char *value) {
    if (h->count >= h->capacity) {
        h->capacity *= 2;
        h->keys   = (char **)realloc(h->keys, h->capacity * sizeof(char *));
        h->values = (char **)realloc(h->values, h->capacity * sizeof(char *));
    }
    h->keys[h->count]   = neverc_textproto_canonical_mime_header_key(key);
    h->values[h->count]  = strdup(value ? value : "");
    h->count++;
}

void neverc_mime_header_set(neverc_mime_header_t *h, const char *key, const char *value) {
    for (size_t i = 0; i < h->count; i++) {
        if (canon_eq(h->keys[i], key)) {
            free(h->values[i]);
            h->values[i] = strdup(value ? value : "");
            return;
        }
    }
    neverc_mime_header_add(h, key, value);
}

const char *neverc_mime_header_get(const neverc_mime_header_t *h, const char *key) {
    for (size_t i = 0; i < h->count; i++)
        if (canon_eq(h->keys[i], key)) return h->values[i];
    return NULL;
}

void neverc_mime_header_del(neverc_mime_header_t *h, const char *key) {
    for (size_t i = 0; i < h->count; i++) {
        if (canon_eq(h->keys[i], key)) {
            free(h->keys[i]); free(h->values[i]);
            h->count--;
            if (i < h->count) {
                h->keys[i]   = h->keys[h->count];
                h->values[i] = h->values[h->count];
            }
            return;
        }
    }
}

size_t neverc_mime_header_len(const neverc_mime_header_t *h) {
    return h->count;
}

int neverc_textproto_read_line(const char *data, size_t len,
                                char *line, size_t line_cap, size_t *consumed) {
    if (!data || len == 0) return -1;
    size_t i = 0;
    while (i < len && data[i] != '\n') i++;
    size_t line_len = i;
    if (line_len > 0 && data[line_len - 1] == '\r') line_len--;
    if (line_len >= line_cap) line_len = line_cap - 1;
    memcpy(line, data, line_len);
    line[line_len] = '\0';
    if (consumed) *consumed = (i < len) ? i + 1 : i;
    return 0;
}

int neverc_textproto_read_mime_header(const char *data, size_t len,
                                      neverc_mime_header_t *h, size_t *consumed) {
    if (!data || !h) return -1;
    size_t pos = 0;
    char line[4096];

    while (pos < len) {
        size_t ate = 0;
        if (neverc_textproto_read_line(data + pos, len - pos, line, sizeof(line), &ate) != 0)
            break;
        pos += ate;
        if (line[0] == '\0') break;

        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        const char *val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        neverc_mime_header_add(h, line, val);
    }
    if (consumed) *consumed = pos;
    return 0;
}

int neverc_textproto_read_dot_lines(const char *data, size_t len,
                                     char **lines, size_t max_lines,
                                     size_t *nlines, size_t *consumed) {
    if (!data || !lines || !nlines) return -1;
    *nlines = 0;
    size_t pos = 0;
    char line[4096];

    while (pos < len && *nlines < max_lines) {
        size_t ate = 0;
        if (neverc_textproto_read_line(data + pos, len - pos, line, sizeof(line), &ate) != 0)
            break;
        pos += ate;
        if (strcmp(line, ".") == 0) break;
        const char *src = line;
        if (src[0] == '.' && src[1] == '.') src++;
        lines[*nlines] = strdup(src);
        (*nlines)++;
    }
    if (consumed) *consumed = pos;
    return 0;
}

int neverc_textproto_read_code_line(const char *line, int *code,
                                     const char **msg) {
    if (!line || strlen(line) < 3) return -1;
    if (!nc_isdigit((unsigned char)line[0]) || !nc_isdigit((unsigned char)line[1]) ||
        !nc_isdigit((unsigned char)line[2])) return -1;
    *code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
    if (line[3] == ' ' || line[3] == '-') {
        if (msg) *msg = line + 4;
    } else if (line[3] == '\0') {
        if (msg) *msg = "";
    } else {
        return -1;
    }
    return (line[3] == '-') ? 1 : 0;
}

int neverc_textproto_trim_string(const char *s, char *out, size_t cap) {
    if (!s || !out) return -1;
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    if (len >= cap) len = cap - 1;
    memcpy(out, s, len);
    out[len] = '\0';
    return 0;
}
