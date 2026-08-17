#include "neverc/std/net/textproto.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef NC_TEXTPROTO_MALLOC
#define NC_TEXTPROTO_MALLOC malloc
#endif
#ifndef NC_TEXTPROTO_CALLOC
#define NC_TEXTPROTO_CALLOC calloc
#endif

static int nc_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int nc_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int nc_isdigit(int c) { return c >= '0' && c <= '9'; }

/* RFC 7230 tchar / RFC 5322 ftext: printable token except ':'. */
static int textproto_is_tchar(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9'))
        return 1;
    switch (c) {
    case '!': case '#': case '$': case '%': case '&': case '\'':
    case '*': case '+': case '-': case '.': case '^': case '_':
    case '`': case '|': case '~':
        return 1;
    default:
        return 0;
    }
}

static int textproto_field_name_ok(const char *s) {
    if (!s || !s[0]) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (!textproto_is_tchar(*p)) return 0;
    }
    return 1;
}

/* HTAB / SP / VCHAR / obs-text. Reject other CTL, including CR/LF/NUL. */
static int textproto_field_value_ok(const char *s) {
    if (!s) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if ((c < 0x20 && c != '\t') || c == 0x7f) return 0;
    }
    return 1;
}

void neverc_mime_header_init(neverc_mime_header_t *h) {
    if (!h) return;
    memset(h, 0, sizeof(*h));
}

void neverc_mime_header_free(neverc_mime_header_t *h) {
    if (!h) return;
    if (h->keys && h->values) {
        for (size_t i = 0; i < h->count; i++) {
            free(h->keys[i]); free(h->values[i]);
        }
    }
    free(h->keys); free(h->values);
    memset(h, 0, sizeof(*h));
}

static char *textproto_dup(const char *s) {
    if (!s) s = "";
    size_t len = strlen(s);
    if (len == SIZE_MAX) return NULL;
    char *copy = (char *)NC_TEXTPROTO_MALLOC(len + 1U);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1U);
    return copy;
}

static int textproto_is_mime_version(const char *s) {
    static const char want[] = "mime-version";
    size_t i = 0;
    for (; s[i] && want[i]; i++) {
        if (nc_tolower((unsigned char)s[i]) != (int)want[i])
            return 0;
    }
    return s[i] == '\0' && want[i] == '\0';
}

char *neverc_textproto_canonical_mime_header_key(const char *key) {
    if (!key) return NULL;
    size_t len = strlen(key);
    if (len == SIZE_MAX) return NULL;
    /* Go: invalid field names (space, CTL, ...) are returned unmodified. */
    if (!textproto_field_name_ok(key))
        return textproto_dup(key);
    char *out = (char *)NC_TEXTPROTO_MALLOC(len + 1U);
    if (!out) return NULL;
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
    /* RFC 2045 writes MIME-Version, not the title-case Mime-Version. */
    if (len == 12 && memcmp(out, "Mime-Version", 12) == 0)
        memcpy(out, "MIME-Version", 12);
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
    if (!canonical || !key) return 0;
    if (textproto_is_mime_version(canonical) &&
        textproto_is_mime_version(key))
        return 1;
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

static int mime_header_grow(neverc_mime_header_t *h) {
    if (h->capacity > SIZE_MAX / 2U) return -1;
    size_t new_capacity = h->capacity == 0 ? 8U : h->capacity * 2U;
    if (new_capacity > SIZE_MAX / sizeof(char *)) return -1;
    char **new_keys = (char **)NC_TEXTPROTO_CALLOC(
        new_capacity, sizeof(char *));
    if (!new_keys) return -1;
    char **new_values = (char **)NC_TEXTPROTO_CALLOC(
        new_capacity, sizeof(char *));
    if (!new_values) { free(new_keys); return -1; }
    if (h->count > 0) {
        memcpy(new_keys, h->keys, h->count * sizeof(char *));
        memcpy(new_values, h->values, h->count * sizeof(char *));
    }
    free(h->keys);
    free(h->values);
    h->keys = new_keys;
    h->values = new_values;
    h->capacity = new_capacity;
    return 0;
}

static int mime_header_add_checked(neverc_mime_header_t *h, const char *key,
                                   const char *value) {
    if (!h || !key || !textproto_field_name_ok(key) ||
        !textproto_field_value_ok(value ? value : "") ||
        h->count > h->capacity ||
        (h->capacity > 0 && (!h->keys || !h->values))) return -1;
    char *canonical = neverc_textproto_canonical_mime_header_key(key);
    if (!canonical) return -1;
    char *value_copy = textproto_dup(value);
    if (!value_copy) { free(canonical); return -1; }
    if (h->count >= h->capacity && mime_header_grow(h) != 0) {
        free(canonical);
        free(value_copy);
        return -1;
    }
    h->keys[h->count] = canonical;
    h->values[h->count] = value_copy;
    h->count++;
    return 0;
}

void neverc_mime_header_add(neverc_mime_header_t *h, const char *key,
                            const char *value) {
    (void)mime_header_add_checked(h, key, value);
}

void neverc_mime_header_set(neverc_mime_header_t *h, const char *key, const char *value) {
    if (!h || !key || !textproto_field_name_ok(key) ||
        !textproto_field_value_ok(value ? value : "") ||
        h->count > h->capacity ||
        (h->capacity > 0 && (!h->keys || !h->values))) return;
    int replaced = 0;
    for (size_t i = 0; i < h->count; ) {
        if (!canon_eq(h->keys[i], key)) {
            i++;
            continue;
        }
        if (!replaced) {
            char *value_copy = textproto_dup(value);
            if (!value_copy) return;
            free(h->values[i]);
            h->values[i] = value_copy;
            replaced = 1;
            i++;
            continue;
        }
        free(h->keys[i]);
        free(h->values[i]);
        h->count--;
        if (i < h->count) {
            h->keys[i] = h->keys[h->count];
            h->values[i] = h->values[h->count];
        }
    }
    if (!replaced)
        neverc_mime_header_add(h, key, value);
}

const char *neverc_mime_header_get(const neverc_mime_header_t *h, const char *key) {
    if (!h || !key || h->count > h->capacity ||
        (h->count > 0 && (!h->keys || !h->values))) return NULL;
    for (size_t i = 0; i < h->count; i++)
        if (canon_eq(h->keys[i], key)) return h->values[i];
    return NULL;
}

void neverc_mime_header_del(neverc_mime_header_t *h, const char *key) {
    if (!h || !key || h->count > h->capacity ||
        (h->count > 0 && (!h->keys || !h->values))) return;
    for (size_t i = 0; i < h->count; ) {
        if (!canon_eq(h->keys[i], key)) {
            i++;
            continue;
        }
        free(h->keys[i]);
        free(h->values[i]);
        h->count--;
        if (i < h->count) {
            h->keys[i] = h->keys[h->count];
            h->values[i] = h->values[h->count];
        }
    }
}

size_t neverc_mime_header_len(const neverc_mime_header_t *h) {
    return h ? h->count : 0;
}

int neverc_textproto_read_line(const char *data, size_t len,
                                char *line, size_t line_cap, size_t *consumed) {
    if (consumed) *consumed = 0;
    if (!data || len == 0 || !line || line_cap == 0) return -1;
    /* Find the '\n' terminator with memchr rather than a byte-at-a-time scan.
       libc's memchr is vectorized, so long lines and large header/body blocks
       fed through this primitive (read_mime_header, read_dot_lines) locate the
       line boundary several times faster. */
    const char *nl = (const char *)memchr(data, '\n', len);
    size_t i = nl ? (size_t)(nl - data) : len;
    size_t line_len = i;
    if (line_len > 0 && data[line_len - 1] == '\r') line_len--;
    if (line_len >= line_cap) {
        if (consumed) *consumed = 0;
        return -1;
    }
    if (!nl) {
        if (consumed) *consumed = 0;
        return -1;
    }
    /* A CR or NUL inside the line is not a terminator. Keeping it in the
     * field name/value lets "Name: a\rInjected: b" re-serialize as two
     * headers; strlen-based copies would also hide the suffix after NUL. */
    if (memchr(data, '\r', line_len) || memchr(data, '\0', line_len)) {
        if (consumed) *consumed = 0;
        return -1;
    }
    memcpy(line, data, line_len);
    line[line_len] = '\0';
    if (consumed) *consumed = i + 1;
    return 0;
}

int neverc_textproto_read_mime_header(const char *data, size_t len,
                                      neverc_mime_header_t *h, size_t *consumed) {
    if (!data || !h) return -1;
    size_t pos = 0;
    char line[4096];
    char name[256];
    char value[4096];
    int have = 0;
    int saw_blank = 0;

    while (pos < len) {
        size_t ate = 0;
        if (neverc_textproto_read_line(data + pos, len - pos, line, sizeof(line), &ate) != 0) {
            if (consumed) *consumed = pos;
            return -1;
        }
        pos += ate;
        if (line[0] == '\0') {
            saw_blank = 1;
            break;
        }

        if (line[0] == ' ' || line[0] == '\t') {
            if (!have) {
                if (consumed) *consumed = pos;
                return -1;
            }
            const char *cont = line;
            while (*cont == ' ' || *cont == '\t') cont++;
            size_t used = strlen(value);
            size_t add = strlen(cont);
            if (used + 1 + add >= sizeof(value)) {
                if (consumed) *consumed = pos;
                return -1;
            }
            value[used] = ' ';
            memcpy(value + used + 1, cont, add + 1);
            continue;
        }

        if (have) {
            if (mime_header_add_checked(h, name, value) != 0) {
                if (consumed) *consumed = pos;
                return -1;
            }
            have = 0;
        }

        char *colon = strchr(line, ':');
        if (!colon) {
            if (consumed) *consumed = pos;
            return -1;
        }
        if (colon == line) {
            if (consumed) *consumed = pos;
            return -1;
        }
        *colon = '\0';
        const char *val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        if (strlen(line) >= sizeof(name) || strlen(val) >= sizeof(value)) {
            if (consumed) *consumed = pos;
            return -1;
        }
        memcpy(name, line, strlen(line) + 1);
        memcpy(value, val, strlen(val) + 1);
        have = 1;
    }
    if (have && mime_header_add_checked(h, name, value) != 0) {
        if (consumed) *consumed = pos;
        return -1;
    }
    if (consumed) *consumed = pos;
    if (!saw_blank) return -1;
    return 0;
}

int neverc_textproto_read_dot_lines(const char *data, size_t len,
                                     char **lines, size_t max_lines,
                                     size_t *nlines, size_t *consumed) {
    if (!data || !lines || !nlines) return -1;
    *nlines = 0;
    size_t pos = 0;
    char line[4096];
    int saw_dot = 0;

    while (pos < len && *nlines < max_lines) {
        size_t ate = 0;
        if (neverc_textproto_read_line(data + pos, len - pos, line, sizeof(line), &ate) != 0) {
            if (consumed) *consumed = pos;
            return -1;
        }
        pos += ate;
        if (strcmp(line, ".") == 0) {
            saw_dot = 1;
            break;
        }
        const char *src = line;
        if (src[0] == '.') src++;
        lines[*nlines] = textproto_dup(src);
        if (!lines[*nlines]) {
            for (size_t i = 0; i < *nlines; i++) free(lines[i]);
            *nlines = 0;
            if (consumed) *consumed = pos;
            return -1;
        }
        (*nlines)++;
    }
    if (consumed) *consumed = pos;
    if (!saw_dot) {
        for (size_t i = 0; i < *nlines; i++) free(lines[i]);
        *nlines = 0;
        return -1;
    }
    return 0;
}

int neverc_textproto_read_code_line(const char *line, int *code,
                                     const char **msg) {
    if (!line || !code || strlen(line) < 3) return -1;
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
    if (!s || !out || cap == 0) return -1;
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    if (len >= cap) len = cap - 1;
    memcpy(out, s, len);
    out[len] = '\0';
    return 0;
}
