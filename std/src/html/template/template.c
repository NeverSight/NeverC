#include "neverc/std/html/template.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#ifndef NC_HTML_TEMPLATE_MALLOC
#define NC_HTML_TEMPLATE_MALLOC malloc
#endif
#ifndef NC_HTML_TEMPLATE_CALLOC
#define NC_HTML_TEMPLATE_CALLOC calloc
#endif
#ifndef NC_HTML_TEMPLATE_REALLOC
#define NC_HTML_TEMPLATE_REALLOC realloc
#endif

static int nc_isalnum(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/* --- Data dictionary --- */

static char *dup_cstr(const char *s) {
    if (!s) s = "";
    size_t len = strlen(s);
    if (len == SIZE_MAX) return NULL;
    char *copy = (char *)NC_HTML_TEMPLATE_MALLOC(len + 1U);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1U);
    return copy;
}

void neverc_html_template_data_init(neverc_html_template_data_t *d) {
    if (!d) return;
    memset(d, 0, sizeof(*d));
}

static int data_grow(neverc_html_template_data_t *d) {
    if (d->capacity > SIZE_MAX / 2U) return -1;
    size_t new_capacity = d->capacity == 0 ? 8U : d->capacity * 2U;
    if (new_capacity > SIZE_MAX / sizeof(char *)) return -1;

    const char **new_keys = (const char **)NC_HTML_TEMPLATE_CALLOC(
        new_capacity, sizeof(char *));
    if (!new_keys) return -1;
    const char **new_values = (const char **)NC_HTML_TEMPLATE_CALLOC(
        new_capacity, sizeof(char *));
    if (!new_values) { free(new_keys); return -1; }
    if (d->count > 0) {
        memcpy(new_keys, d->keys, d->count * sizeof(char *));
        memcpy(new_values, d->values, d->count * sizeof(char *));
    }
    free(d->keys);
    free(d->values);
    d->keys = new_keys;
    d->values = new_values;
    d->capacity = new_capacity;
    return 0;
}

void neverc_html_template_data_set(neverc_html_template_data_t *d,
                                    const char *key, const char *value) {
    if (!d || !key || d->count > d->capacity ||
        (d->count > 0 && (!d->keys || !d->values))) return;
    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->keys[i], key) == 0) {
            char *new_value = dup_cstr(value);
            if (!new_value) return;
            free((void *)d->values[i]);
            d->values[i] = new_value;
            return;
        }
    }

    char *new_key = dup_cstr(key);
    if (!new_key) return;
    char *new_value = dup_cstr(value);
    if (!new_value) { free(new_key); return; }
    if (d->count >= d->capacity && data_grow(d) != 0) {
        free(new_key);
        free(new_value);
        return;
    }
    d->keys[d->count] = new_key;
    d->values[d->count] = new_value;
    d->count++;
}

const char *neverc_html_template_data_get(const neverc_html_template_data_t *d,
                                           const char *key) {
    if (!d || !key || d->count > d->capacity ||
        (d->count > 0 && (!d->keys || !d->values))) return NULL;
    for (size_t i = 0; i < d->count; i++)
        if (strcmp(d->keys[i], key) == 0) return d->values[i];
    return NULL;
}

void neverc_html_template_data_free(neverc_html_template_data_t *d) {
    if (!d) return;
    for (size_t i = 0; i < d->count; i++) {
        free((void *)d->keys[i]);
        free((void *)d->values[i]);
    }
    free(d->keys); free(d->values);
    memset(d, 0, sizeof(*d));
}

/* --- Escape functions (context-aware) --- */

static int buf_append(char **buf, size_t *len, size_t *cap,
                      const char *s, size_t slen) {
    if ((!s && slen > 0) || *len > SIZE_MAX - slen - 1U) return -1;
    size_t required = *len + slen + 1U;
    if (required > *cap) {
        size_t new_cap = *cap;
        while (new_cap < required) {
            if (new_cap > SIZE_MAX / 2U) { new_cap = required; break; }
            new_cap *= 2U;
        }
        char *new_buf = (char *)NC_HTML_TEMPLATE_REALLOC(*buf, new_cap);
        if (!new_buf) return -1;
        *buf = new_buf;
        *cap = new_cap;
    }
    if (slen > 0) memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
    return 0;
}

static const char nc_uphex[] = "0123456789ABCDEF";

/* Extra bytes each escaped byte adds beyond itself (0 == self-representing),
 * doubling as the "is special" predicate.  &#34; / &#39; / &amp; add 4,
 * &lt; / &gt; add 3. */
static const uint8_t html_esc_extra[256] = {
    ['&'] = 4, ['<'] = 3, ['>'] = 3, ['"'] = 4, ['\''] = 4,
};

char *neverc_html_escape(const char *s) {
    if (!s) return dup_cstr("");
    size_t slen = strlen(s), extra = 0;
    for (size_t i = 0; i < slen; i++) {
        size_t add = html_esc_extra[(unsigned char)s[i]];
        if (extra > SIZE_MAX - add) return NULL;
        extra += add;
    }
    if (slen == SIZE_MAX || extra > SIZE_MAX - slen - 1U) return NULL;

    char *r = (char *)NC_HTML_TEMPLATE_MALLOC(slen + extra + 1U);
    if (!r) return NULL;
    if (extra == 0) { memcpy(r, s, slen); r[slen] = '\0'; return r; }

    size_t wi = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char c = (unsigned char)s[i];
        if (html_esc_extra[c] == 0) { r[wi++] = (char)c; continue; }
        switch (c) {
            case '&':  memcpy(r + wi, "&amp;", 5); wi += 5; break;
            case '<':  memcpy(r + wi, "&lt;",  4); wi += 4; break;
            case '>':  memcpy(r + wi, "&gt;",  4); wi += 4; break;
            case '"':  memcpy(r + wi, "&#34;", 5); wi += 5; break;
            case '\'': memcpy(r + wi, "&#39;", 5); wi += 5; break;
        }
    }
    r[wi] = '\0';
    return r;
}

char *neverc_html_attr_escape(const char *s) {
    return neverc_html_escape(s);
}

/* \ ' " / \n \r \t escape to 2 chars (extra 1); < > & ` $ = + and other
 * controls escape to \u00XX (extra 5). U+2028/U+2029 are handled in the
 * scanner: 3 UTF-8 bytes become 6-char \u2028/\u2029 (extra 3). */
static const uint8_t js_esc_extra[256] = {
    ['\\'] = 1, ['\''] = 1, ['"'] = 1, ['/'] = 1,
    ['\n'] = 1, ['\r'] = 1, ['\t'] = 1,
    ['<'] = 5, ['>'] = 5, ['&'] = 5, ['`'] = 5, ['$'] = 5,
    ['='] = 5, ['+'] = 5,
};

static int js_is_line_sep(const char *s, size_t i, size_t slen, int *ps) {
    if (i + 2 >= slen) return 0;
    if ((unsigned char)s[i] != 0xE2 || (unsigned char)s[i + 1] != 0x80)
        return 0;
    unsigned char c2 = (unsigned char)s[i + 2];
    if (c2 == 0xA8) { if (ps) *ps = 0; return 1; }
    if (c2 == 0xA9) { if (ps) *ps = 1; return 1; }
    return 0;
}

char *neverc_html_js_escape(const char *s) {
    if (!s) return dup_cstr("");
    size_t slen = strlen(s), extra = 0;
    for (size_t i = 0; i < slen; ) {
        if (js_is_line_sep(s, i, slen, NULL)) {
            if (extra > SIZE_MAX - 3U) return NULL;
            extra += 3U;
            i += 3;
            continue;
        }
        unsigned char c = (unsigned char)s[i++];
        size_t add = js_esc_extra[c];
        if (add == 0 && c < 0x20) add = 5;
        if (extra > SIZE_MAX - add) return NULL;
        extra += add;
    }
    if (slen == SIZE_MAX || extra > SIZE_MAX - slen - 1U) return NULL;

    char *r = (char *)NC_HTML_TEMPLATE_MALLOC(slen + extra + 1U);
    if (!r) return NULL;
    if (extra == 0) { memcpy(r, s, slen); r[slen] = '\0'; return r; }

    size_t wi = 0;
    for (size_t i = 0; i < slen; ) {
        int ps = 0;
        if (js_is_line_sep(s, i, slen, &ps)) {
            memcpy(r + wi, ps ? "\\u2029" : "\\u2028", 6);
            wi += 6;
            i += 3;
            continue;
        }
        unsigned char c = (unsigned char)s[i++];
        if (js_esc_extra[c] == 0 && c >= 0x20) { r[wi++] = (char)c; continue; }
        switch (c) {
            case '\\': memcpy(r + wi, "\\\\",   2); wi += 2; break;
            case '\'': memcpy(r + wi, "\\'",    2); wi += 2; break;
            case '"':  memcpy(r + wi, "\\\"",   2); wi += 2; break;
            case '/':  memcpy(r + wi, "\\/",    2); wi += 2; break;
            case '\n': memcpy(r + wi, "\\n",    2); wi += 2; break;
            case '\r': memcpy(r + wi, "\\r",    2); wi += 2; break;
            case '\t': memcpy(r + wi, "\\t",    2); wi += 2; break;
            case '<':  memcpy(r + wi, "\\u003c", 6); wi += 6; break;
            case '>':  memcpy(r + wi, "\\u003e", 6); wi += 6; break;
            case '&':  memcpy(r + wi, "\\u0026", 6); wi += 6; break;
            case '`':  memcpy(r + wi, "\\u0060", 6); wi += 6; break;
            case '$':  memcpy(r + wi, "\\u0024", 6); wi += 6; break;
            case '=':  memcpy(r + wi, "\\u003d", 6); wi += 6; break;
            case '+':  memcpy(r + wi, "\\u002b", 6); wi += 6; break;
            default:
                r[wi++] = '\\'; r[wi++] = 'u'; r[wi++] = '0'; r[wi++] = '0';
                r[wi++] = nc_uphex[c >> 4];
                r[wi++] = nc_uphex[c & 0x0f];
                break;
        }
    }
    r[wi] = '\0';
    return r;
}

char *neverc_html_css_escape(const char *s) {
    if (!s) return dup_cstr("");
    size_t slen = strlen(s);
    if (slen > (SIZE_MAX - 1U) / 3U) return NULL;
    /* Worst case is "\\XX" (3 bytes) per input byte; allocate the bound once. */
    char *r = (char *)NC_HTML_TEMPLATE_MALLOC(slen * 3U + 1U);
    if (!r) return NULL;
    size_t wi = 0, i = 0;
    while (i < slen) {
        /* Bulk-copy a run of bytes that pass through unescaped. */
        size_t start = i;
        while (i < slen) {
            unsigned char c = (unsigned char)s[i];
            if (!(nc_isalnum(c) || c == '-' || c == '_')) break;
            i++;
        }
        if (i > start) { memcpy(r + wi, s + start, i - start); wi += i - start; }
        if (i >= slen) break;
        unsigned char c = (unsigned char)s[i++];
        r[wi++] = '\\';
        r[wi++] = nc_uphex[c >> 4];
        r[wi++] = nc_uphex[c & 0x0f];
        /* Terminate a short CSS hex escape before a following hex digit.
         * The following digit passes through as one byte, so the existing
         * three-bytes-per-input allocation bound still covers this space. */
        if (i < slen &&
            ((s[i] >= '0' && s[i] <= '9') ||
             (s[i] >= 'A' && s[i] <= 'F') ||
             (s[i] >= 'a' && s[i] <= 'f')))
            r[wi++] = ' ';
    }
    r[wi] = '\0';
    return r;
}

char *neverc_html_url_query_escape(const char *s) {
    if (!s) return dup_cstr("");
    size_t slen = strlen(s);
    if (slen > (SIZE_MAX - 1U) / 3U) return NULL;
    /* Worst case is "%XX" (3 bytes) per input byte; allocate the bound once. */
    char *r = (char *)NC_HTML_TEMPLATE_MALLOC(slen * 3U + 1U);
    if (!r) return NULL;
    size_t wi = 0, i = 0;
    while (i < slen) {
        /* Bulk-copy a run of unreserved bytes. */
        size_t start = i;
        while (i < slen) {
            unsigned char c = (unsigned char)s[i];
            if (!(nc_isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')) break;
            i++;
        }
        if (i > start) { memcpy(r + wi, s + start, i - start); wi += i - start; }
        if (i >= slen) break;
        unsigned char c = (unsigned char)s[i++];
        r[wi++] = '%';
        r[wi++] = nc_uphex[c >> 4];
        r[wi++] = nc_uphex[c & 0x0f];
    }
    r[wi] = '\0';
    return r;
}

/* --- Template parser --- */

typedef enum {
    NODE_TEXT, NODE_VAR, NODE_IF, NODE_ELSE, NODE_END, NODE_RANGE
} node_type_t;

typedef struct node {
    node_type_t type;
    char *text;
    size_t text_len;
    struct node *next;
    struct node *children;
    struct node *else_branch;
} node_t;

struct neverc_html_template {
    node_t *root;
};

static node_t *new_node(node_type_t type, const char *text, size_t tlen) {
    if ((!text && tlen > 0) || tlen == SIZE_MAX) return NULL;
    node_t *n = (node_t *)NC_HTML_TEMPLATE_CALLOC(1, sizeof(node_t));
    if (!n) return NULL;
    n->type = type;
    if (text && tlen > 0) {
        n->text = (char *)NC_HTML_TEMPLATE_MALLOC(tlen + 1U);
        if (!n->text) { free(n); return NULL; }
        memcpy(n->text, text, tlen);
        n->text[tlen] = '\0';
        n->text_len = tlen;
    }
    return n;
}

static void free_nodes(node_t *n) {
    while (n) {
        node_t *next = n->next;
        free(n->text);
        free_nodes(n->children);
        free_nodes(n->else_branch);
        free(n);
        n = next;
    }
}

static int is_template_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Parsing and execution both recurse once per block. */
#define NCI_HTML_TEMPLATE_MAX_DEPTH 128

enum { PARSE_ERROR = -1, PARSE_DONE, PARSE_END, PARSE_ELSE };

static node_t *new_key_node(node_type_t type, const char *start,
                            const char *end, int *error) {
    while (start < end && is_template_ws(*start)) start++;
    while (end > start && is_template_ws(end[-1])) end--;
    if (start < end && *start == '.') start++;
    if (start >= end) { *error = 1; return NULL; }
    const char *p = start;
    if (!nc_isalnum((unsigned char)*p) && *p != '_') {
        *error = 1;
        return NULL;
    }
    while (p < end && (nc_isalnum((unsigned char)*p) || *p == '_' ||
                       *p == '-'))
        p++;
    if (p != end) {
        *error = 1;
        return NULL;
    }
    node_t *node = new_node(type, start, (size_t)(end - start));
    if (!node) *error = 1;
    return node;
}

static node_t *parse_tag(const char *inner, size_t len, int *error) {
    const char *start = inner;
    while (start < inner + len && is_template_ws(*start)) start++;
    const char *end = inner + len;
    while (end > start && is_template_ws(end[-1])) end--;
    size_t trimlen = (size_t)(end - start);

    if (trimlen == 0 || (trimlen == 2 && memcmp(start, "if", 2) == 0) ||
        (trimlen == 5 && memcmp(start, "range", 5) == 0)) {
        *error = 1;
        return NULL;
    }
    if (trimlen >= 4 && memcmp(start, "else", 4) == 0) {
        if (trimlen != 4) {
            *error = 1;
            return NULL;
        }
        node_t *node = new_node(NODE_ELSE, NULL, 0);
        if (!node) *error = 1;
        return node;
    }
    if (trimlen == 3 && memcmp(start, "end", 3) == 0) {
        node_t *node = new_node(NODE_END, NULL, 0);
        if (!node) *error = 1;
        return node;
    }
    if (trimlen > 2 && memcmp(start, "if", 2) == 0 &&
        is_template_ws(start[2])) {
        return new_key_node(NODE_IF, start + 2, end, error);
    }
    if (trimlen > 5 && memcmp(start, "range", 5) == 0 &&
        is_template_ws(start[5])) {
        return new_key_node(NODE_RANGE, start + 5, end, error);
    }
    return new_key_node(NODE_VAR, start, end, error);
}

static void append_node(node_t **head, node_t **tail, node_t *node) {
    if (*tail) (*tail)->next = node;
    else *head = node;
    *tail = node;
}

static int parse_nodes(const char **src, int depth, int expect_end,
                       int allow_else, node_t **out) {
    node_t *head = NULL;
    node_t *tail = NULL;
    if (depth > NCI_HTML_TEMPLATE_MAX_DEPTH) goto fail;

    while (**src) {
        const char *open = strstr(*src, "{{");
        if (!open) {
            size_t rem = strlen(*src);
            if (rem > 0) {
                node_t *text = new_node(NODE_TEXT, *src, rem);
                if (!text) goto fail;
                append_node(&head, &tail, text);
            }
            *src += rem;
            if (expect_end) goto fail;
            *out = head;
            return PARSE_DONE;
        }
        if (open > *src) {
            node_t *text = new_node(
                NODE_TEXT, *src, (size_t)(open - *src));
            if (!text) goto fail;
            append_node(&head, &tail, text);
        }
        const char *close = strstr(open + 2, "}}");
        if (!close) goto fail;
        const char *inner = open + 2;
        size_t ilen = (size_t)(close - inner);
        *src = close + 2;

        int tag_error = 0;
        node_t *tag = parse_tag(inner, ilen, &tag_error);
        if (tag_error || !tag) goto fail;

        if (tag->type == NODE_END) {
            free_nodes(tag);
            if (!expect_end) goto fail;
            *out = head;
            return PARSE_END;
        }
        if (tag->type == NODE_ELSE) {
            free_nodes(tag);
            if (!allow_else) goto fail;
            *out = head;
            return PARSE_ELSE;
        }
        if (tag->type == NODE_IF) {
            int child_status = parse_nodes(
                src, depth + 1, 1, 1, &tag->children);
            if (child_status == PARSE_ELSE) {
                int else_status = parse_nodes(
                    src, depth + 1, 1, 0, &tag->else_branch);
                if (else_status != PARSE_END) {
                    free_nodes(tag);
                    goto fail;
                }
            } else if (child_status != PARSE_END) {
                free_nodes(tag);
                goto fail;
            }
        } else if (tag->type == NODE_RANGE) {
            int child_status = parse_nodes(
                src, depth + 1, 1, 0, &tag->children);
            if (child_status != PARSE_END) {
                free_nodes(tag);
                goto fail;
            }
        }
        append_node(&head, &tail, tag);
    }

    if (expect_end) goto fail;
    *out = head;
    return PARSE_DONE;

fail:
    free_nodes(head);
    *out = NULL;
    return PARSE_ERROR;
}

neverc_html_template_t *neverc_html_template_parse(const char *src) {
    if (!src) return NULL;
    neverc_html_template_t *t = (neverc_html_template_t *)
        NC_HTML_TEMPLATE_CALLOC(1, sizeof(*t));
    if (!t) return NULL;
    const char *p = src;
    if (parse_nodes(&p, 0, 0, 0, &t->root) != PARSE_DONE) {
        neverc_html_template_free(t);
        return NULL;
    }
    return t;
}

void neverc_html_template_free(neverc_html_template_t *t) {
    if (!t) return;
    free_nodes(t->root);
    free(t);
}

static int html_ci_eq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
    }
    return 1;
}

static int html_is_ascii_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

static int html_attr_name_eq(const char *name, size_t nlen, const char *want) {
    size_t wlen = strlen(want);
    return nlen == wlen && html_ci_eq_n(name, want, nlen);
}

static int html_is_url_attr_name(const char *name, size_t nlen) {
    static const char *urls[] = {
        "href", "src", "action", "formaction", "cite", "poster",
        "background", "data", "srcset", "imagesrcset", "ping", "xlink:href",
        "longdesc", "usemap", "icon", "manifest", "archive",
        "classid", "codebase", "profile",
        "dynsrc", "lowsrc", "code", "pluginspage", "pluginurl",
        /* SVG SMIL can animate href via to/from/values/by (html5sec #10/#11). */
        "to", "from", "values", "by",
        /* Go html/template: xmlns is a URL; xml:base local-name is "base". */
        "xmlns", "base", NULL
    };
    for (int i = 0; urls[i]; i++)
        if (html_attr_name_eq(name, nlen, urls[i])) return 1;
    return 0;
}

/* Tag names stop at the first character that cannot appear in an HTML
 * name. Hyphens keep `<script-foo>` from being treated as `<script`. */
static int html_is_tag_name_char(unsigned char c) {
    return nc_isalnum(c) || c == '-' || c == ':';
}

static int html_tag_is(const char *tag, size_t tlen, const char *want) {
    size_t wlen = strlen(want);
    return tlen == wlen && html_ci_eq_n(tag, want, tlen);
}

static int html_contains_ci_n(const char *s, size_t n, const char *needle) {
    size_t k = strlen(needle);
    if (!s || k == 0 || k > n) return 0;
    for (size_t i = 0; i + k <= n; i++)
        if (html_ci_eq_n(s + i, needle, k)) return 1;
    return 0;
}

static int html_value_is_refresh(const char *buf, size_t start, size_t end) {
    while (start < end && html_is_ascii_ws((unsigned char)buf[start]))
        start++;
    while (end > start && html_is_ascii_ws((unsigned char)buf[end - 1]))
        end--;
    return end > start && html_tag_is(buf + start, end - start, "refresh");
}

static void html_note_http_equiv(const char *buf, size_t value_start,
                                 size_t value_end, const char *attr,
                                 size_t alen, int *saw_refresh) {
    if (!saw_refresh || *saw_refresh || !attr) return;
    if (html_attr_name_eq(attr, alen, "http-equiv") &&
        html_value_is_refresh(buf, value_start, value_end))
        *saw_refresh = 1;
}

static int html_raw_end_tag(const char *buf, size_t i, size_t len,
                            const char *close, size_t clen) {
    if (i + clen > len || !html_ci_eq_n(buf + i, close, clen)) return 0;
    if (i + clen == len) return 1;
    unsigned char n = (unsigned char)buf[i + clen];
    /* HTML5: a script/style end tag name is followed by whitespace, '/',
     * or '>'. A hyphen keeps `</script-foo>` inside the raw text element,
     * so interpolation after it must stay JS/CSS-escaped. */
    return n == ' ' || n == '\t' || n == '\n' || n == '\r' || n == '\f' ||
           n == '/' || n == '>';
}

enum {
    HS_TEXT, HS_TAG, HS_ATTR_DQ, HS_ATTR_SQ, HS_ATTR_UQ,
    HS_COMMENT, HS_SCRIPT, HS_STYLE, HS_MARKUP
};

/*
 * Quote- and comment-aware scan of the already-emitted HTML. `<script>` /
 * `<style>` inside an attribute or comment must not switch the interpolation
 * into JS/CSS escaping (JS escaping leaves `"` as `\"`, which closes an HTML
 * attribute). `>` inside a quoted attribute must not look like the tag ended,
 * or `{{.X}}` in attribute-name position would be HTML-escaped instead of
 * replaced with ZgotmplZ.
 */
static int html_js_is_line_term(const char *buf, size_t i, size_t len) {
    unsigned char c = (unsigned char)buf[i];
    if (c == '\n' || c == '\r') return 1;
    /* U+2028 / U+2029 also terminate a JS line comment. */
    return i + 2 < len && c == 0xE2 &&
           (unsigned char)buf[i + 1] == 0x80 &&
           ((unsigned char)buf[i + 2] == 0xA8 ||
            (unsigned char)buf[i + 2] == 0xA9);
}

static void html_scan_doc(const char *buf, size_t len,
                          int *in_script, int *in_style,
                          int *in_script_comment,
                          int *in_open_tag,
                          int *in_attr, int *quoted,
                          const char **aname, size_t *nlen,
                          const char **aprefix, size_t *aplen,
                          int *in_meta, int *meta_refresh) {
    *in_script = 0;
    *in_style = 0;
    *in_script_comment = 0;
    *in_open_tag = 0;
    *in_attr = 0;
    *quoted = 0;
    *aname = NULL;
    *nlen = 0;
    *aprefix = NULL;
    *aplen = 0;
    *in_meta = 0;
    *meta_refresh = 0;
    if (!buf || len == 0) return;

    int state = HS_TEXT;
    enum { JS_CODE, JS_SQ, JS_DQ, JS_TPL, JS_LINE, JS_BLOCK, JS_HTML };
    int js = JS_CODE;
    char tag[16];
    size_t tlen = 0;
    int is_end = 0;
    int seen_name = 0;
    int saw_refresh = 0;
    const char *attr = NULL;
    size_t alen = 0;
    size_t value_start = 0;
    size_t i = 0;

    while (i < len) {
        unsigned char c = (unsigned char)buf[i];
        switch (state) {
        case HS_TEXT:
            if (c != '<') { i++; break; }
            if (i + 3 < len && buf[i + 1] == '!' &&
                buf[i + 2] == '-' && buf[i + 3] == '-') {
                state = HS_COMMENT;
                i += 4;
                break;
            }
            if (i + 1 < len && (buf[i + 1] == '!' || buf[i + 1] == '?')) {
                state = HS_MARKUP;
                i += 2;
                break;
            }
            if (i + 1 >= len ||
                buf[i + 1] == '/' ||
                html_is_tag_name_char((unsigned char)buf[i + 1])) {
                /* `<` at EOF is an incomplete tag: `<{{.X}}>` interpolates
                 * the tag name and must not HTML-escape into `<script>`. */
                state = HS_TAG;
                tlen = 0;
                is_end = 0;
                seen_name = 0;
                saw_refresh = 0;
                attr = NULL;
                alen = 0;
                i++;
                break;
            }
            i++;
            break;

        case HS_COMMENT:
            if (c == '-' && i + 2 < len &&
                buf[i + 1] == '-' && buf[i + 2] == '>') {
                state = HS_TEXT;
                i += 3;
                break;
            }
            i++;
            break;

        case HS_MARKUP:
            if (c == '>') state = HS_TEXT;
            i++;
            break;

        case HS_SCRIPT:
            if (html_raw_end_tag(buf, i, len, "</script", 8)) {
                state = HS_TAG;
                js = JS_CODE;
                is_end = 1;
                seen_name = 1;
                memcpy(tag, "script", 6);
                tlen = 6;
                attr = NULL;
                alen = 0;
                i += 8;
                break;
            }
            /* Track JS block comments and HTML <!-- ... --> inside script
             * so interpolation cannot break out. Wrapping as a JS string is
             * a no-op inside a comment: a closer in the value still ends it. */
            switch (js) {
            case JS_CODE:
                if (c == '"') { js = JS_DQ; i++; break; }
                if (c == '\'') { js = JS_SQ; i++; break; }
                if (c == '`') { js = JS_TPL; i++; break; }
                if (c == '/' && i + 1 < len && buf[i + 1] == '/') {
                    js = JS_LINE;
                    i += 2;
                    break;
                }
                if (c == '/' && i + 1 < len && buf[i + 1] == '*') {
                    js = JS_BLOCK;
                    i += 2;
                    break;
                }
                if (c == '<' && i + 3 < len &&
                    buf[i + 1] == '!' && buf[i + 2] == '-' &&
                    buf[i + 3] == '-') {
                    js = JS_HTML;
                    i += 4;
                    break;
                }
                i++;
                break;
            case JS_SQ:
            case JS_DQ:
            case JS_TPL:
                if (c == '\\' && i + 1 < len) { i += 2; break; }
                if ((js == JS_SQ && c == '\'') ||
                    (js == JS_DQ && c == '"') ||
                    (js == JS_TPL && c == '`'))
                    js = JS_CODE;
                i++;
                break;
            case JS_LINE:
                if (html_js_is_line_term(buf, i, len)) js = JS_CODE;
                i++;
                break;
            case JS_BLOCK:
                if (c == '*' && i + 1 < len && buf[i + 1] == '/') {
                    js = JS_CODE;
                    i += 2;
                    break;
                }
                i++;
                break;
            case JS_HTML:
                if (c == '-' && i + 2 < len &&
                    buf[i + 1] == '-' && buf[i + 2] == '>') {
                    js = JS_CODE;
                    i += 3;
                    break;
                }
                i++;
                break;
            }
            break;

        case HS_STYLE:
            if (html_raw_end_tag(buf, i, len, "</style", 7)) {
                state = HS_TAG;
                is_end = 1;
                seen_name = 1;
                memcpy(tag, "style", 5);
                tlen = 5;
                attr = NULL;
                alen = 0;
                i += 7;
                break;
            }
            i++;
            break;

        case HS_ATTR_DQ:
            if (c == '"') {
                html_note_http_equiv(buf, value_start, i, attr, alen,
                                     &saw_refresh);
                state = HS_TAG;
                attr = NULL;
                alen = 0;
            }
            i++;
            break;

        case HS_ATTR_SQ:
            if (c == '\'') {
                html_note_http_equiv(buf, value_start, i, attr, alen,
                                     &saw_refresh);
                state = HS_TAG;
                attr = NULL;
                alen = 0;
            }
            i++;
            break;

        case HS_ATTR_UQ:
            if (html_is_ascii_ws(c)) {
                html_note_http_equiv(buf, value_start, i, attr, alen,
                                     &saw_refresh);
                state = HS_TAG;
                attr = NULL;
                alen = 0;
                i++;
                break;
            }
            if (c == '>') {
                html_note_http_equiv(buf, value_start, i, attr, alen,
                                     &saw_refresh);
                state = HS_TAG;
                attr = NULL;
                alen = 0;
                break;
            }
            i++;
            break;

        case HS_TAG:
            if (!seen_name) {
                if (tlen == 0 && html_is_ascii_ws(c)) { i++; break; }
                if (tlen == 0 && c == '/') { is_end = 1; i++; break; }
                if (html_is_tag_name_char(c)) {
                    if (tlen < sizeof(tag) - 1U) tag[tlen++] = (char)c;
                    else tlen = sizeof(tag);
                    i++;
                    break;
                }
                seen_name = 1;
                break;
            }
            if (html_is_ascii_ws(c)) { i++; break; }
            if (c == '/') { i++; break; }
            if (c == '>') {
                if (!is_end && tlen > 0 && tlen < sizeof(tag) &&
                    html_tag_is(tag, tlen, "script")) {
                    state = HS_SCRIPT;
                    js = JS_CODE;
                } else if (!is_end && tlen > 0 && tlen < sizeof(tag) &&
                         html_tag_is(tag, tlen, "style"))
                    state = HS_STYLE;
                else
                    state = HS_TEXT;
                attr = NULL;
                alen = 0;
                i++;
                break;
            }
            if (c == '"') { state = HS_ATTR_DQ; value_start = i + 1; i++; break; }
            if (c == '\'') { state = HS_ATTR_SQ; value_start = i + 1; i++; break; }
            if (c == '=') {
                size_t e = i;
                while (e > 0 && html_is_ascii_ws((unsigned char)buf[e - 1]))
                    e--;
                size_t b = e;
                while (b > 0 && (nc_isalnum((unsigned char)buf[b - 1]) ||
                                 buf[b - 1] == '-' || buf[b - 1] == ':'))
                    b--;
                if (e > b) { attr = buf + b; alen = e - b; }
                i++;
                while (i < len && html_is_ascii_ws((unsigned char)buf[i])) i++;
                if (i >= len) { state = HS_ATTR_UQ; value_start = i; break; }
                c = (unsigned char)buf[i];
                if (c == '"') { state = HS_ATTR_DQ; i++; value_start = i; break; }
                if (c == '\'') { state = HS_ATTR_SQ; i++; value_start = i; break; }
                state = HS_ATTR_UQ;
                value_start = i;
                break;
            }
            i++;
            break;
        }
    }

    *in_script = (state == HS_SCRIPT);
    *in_style = (state == HS_STYLE);
    *in_script_comment = (state == HS_SCRIPT &&
                          (js == JS_LINE || js == JS_BLOCK || js == JS_HTML));
    *in_open_tag = (state == HS_TAG || state == HS_ATTR_DQ ||
                    state == HS_ATTR_SQ || state == HS_ATTR_UQ ||
                    state == HS_MARKUP);
    if (state == HS_ATTR_DQ || state == HS_ATTR_SQ || state == HS_ATTR_UQ) {
        *in_attr = 1;
        *quoted = (state != HS_ATTR_UQ);
        *aname = attr;
        *nlen = alen;
        if (value_start <= len) {
            *aprefix = buf + value_start;
            *aplen = len - value_start;
        }
    }
    if (state == HS_TAG || state == HS_ATTR_DQ ||
        state == HS_ATTR_SQ || state == HS_ATTR_UQ) {
        if (tlen > 0 && tlen < sizeof(tag) && html_tag_is(tag, tlen, "meta")) {
            *in_meta = 1;
            *meta_refresh = saw_refresh;
        }
    }
}

static int html_prev_non_ws_is_quote(const char *buf, size_t len) {
    while (len > 0 && html_is_ascii_ws((unsigned char)buf[len - 1]))
        len--;
    if (len == 0) return 0;
    return buf[len - 1] == '"' || buf[len - 1] == '\'';
}

static int html_prev_non_ws_is_digit(const char *buf, size_t len) {
    while (len > 0 && html_is_ascii_ws((unsigned char)buf[len - 1]))
        len--;
    if (len == 0) return 0;
    unsigned char c = (unsigned char)buf[len - 1];
    return c >= '0' && c <= '9';
}

static int html_unquoted_value_is_unsafe(const char *s) {
    if (!s) return 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c <= 0x20 || c == '"' || c == '\'' || c == '=' ||
            c == '<' || c == '>' || c == '`')
            return 1;
    }
    return 0;
}

static char *html_escape_srcdoc(const char *s) {
    char *inner = neverc_html_escape(s);
    if (!inner) return NULL;
    char *outer = neverc_html_escape(inner);
    free(inner);
    return outer;
}

static char *html_js_expr(const char *s) {
    char *js = neverc_html_js_escape(s);
    if (!js) return NULL;
    size_t n = strlen(js);
    if (n > SIZE_MAX - 3U) { free(js); return NULL; }
    char *lit = (char *)NC_HTML_TEMPLATE_MALLOC(n + 3U);
    if (!lit) { free(js); return NULL; }
    lit[0] = '"';
    memcpy(lit + 1, js, n);
    lit[n + 1] = '"';
    lit[n + 2] = '\0';
    free(js);
    return lit;
}

/* `url({{.X}})`, `url("{{.X}}")`, and the same with a value prefix. */
static int html_in_css_url(const char *buf, size_t len,
                           const char **prefix, size_t *plen) {
    *prefix = NULL;
    *plen = 0;
    if (!buf || len == 0) return 0;
    size_t i = len;
    while (i > 0) {
        char c = buf[i - 1];
        if (c == '(' || c == '"' || c == '\'' || c == ';' ||
            c == '{' || c == '}' || c == '<' || c == '>')
            break;
        i--;
    }
    if (i == 0) return 0;
    if (buf[i - 1] == '"' || buf[i - 1] == '\'') {
        if (i < 5 || !html_ci_eq_n(buf + i - 5, "url(", 4)) return 0;
        *prefix = buf + i;
        *plen = len - i;
        return 1;
    }
    if (buf[i - 1] == '(' && i >= 4 && html_ci_eq_n(buf + i - 4, "url(", 4)) {
        *prefix = buf + i;
        *plen = len - i;
        return 1;
    }
    return 0;
}

typedef struct {
    const char *s;
    size_t n;
    size_t i;
} html_span_t;

static int html_spans_done(const html_span_t *sp, int n) {
    for (int i = 0; i < n; i++)
        if (sp[i].i < sp[i].n) return 0;
    return 1;
}

static unsigned char html_spans_peek(const html_span_t *sp, int n) {
    for (int i = 0; i < n; i++)
        if (sp[i].i < sp[i].n) return (unsigned char)sp[i].s[sp[i].i];
    return 0;
}

static void html_spans_next(html_span_t *sp, int n) {
    for (int i = 0; i < n; i++) {
        if (sp[i].i < sp[i].n) { sp[i].i++; return; }
    }
}

static void html_spans_skip_ws(html_span_t *sp, int n) {
    while (!html_spans_done(sp, n) && html_is_ascii_ws(html_spans_peek(sp, n)))
        html_spans_next(sp, n);
}

/* Match a scheme in one string, even when ASCII whitespace is sprinkled
 * through it (`java\tscript:`). Used for CSS, where `color:red` must not
 * be treated as a URL. */
static int html_scheme_is_parts(const char *prefix, size_t plen,
                                const char *s, const char *scheme) {
    if (!scheme) return 0;
    html_span_t sp[2];
    int n = 0;
    if (prefix && plen) { sp[n].s = prefix; sp[n].n = plen; sp[n].i = 0; n++; }
    sp[n].s = s ? s : "";
    sp[n].n = s ? strlen(s) : 0;
    sp[n].i = 0;
    n++;
    html_spans_skip_ws(sp, n);
    while (*scheme) {
        html_spans_skip_ws(sp, n);
        if (html_spans_done(sp, n)) return 0;
        char ch = (char)html_spans_peek(sp, n);
        html_spans_next(sp, n);
        char want = *scheme++;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        if (want >= 'A' && want <= 'Z') want = (char)(want + 32);
        if (ch != want) return 0;
    }
    html_spans_skip_ws(sp, n);
    return !html_spans_done(sp, n) && html_spans_peek(sp, n) == ':';
}

/*
 * Go html/template isSafeURL: relative (no ':' before '/') or scheme in
 * {http, https, mailto}. Copies internally so the caller can keep walking.
 * stop_at_comma ends the URL at `,` (srcset elements).
 */
static int html_is_safe_url_spans(const html_span_t *in, int n,
                                  int stop_at_comma) {
    html_span_t sp[3];
    if (n <= 0) return 1;
    if (n > 3) n = 3;
    memcpy(sp, in, (size_t)n * sizeof(sp[0]));
    char proto[16];
    size_t pn = 0;
    int saw_colon = 0, saw_slash = 0;
    while (!html_spans_done(sp, n)) {
        unsigned char c = html_spans_peek(sp, n);
        html_spans_next(sp, n);
        if (stop_at_comma && c == ',') break;
        if (c == ':') { saw_colon = 1; break; }
        if (c == '/') { saw_slash = 1; break; }
        if (pn < sizeof(proto) - 1U) proto[pn++] = (char)c;
        else pn = sizeof(proto);
    }
    if (!saw_colon || saw_slash) return 1;
    if (pn == 0 || pn >= sizeof(proto)) return 0;
    proto[pn] = '\0';
    return html_tag_is(proto, pn, "http") ||
           html_tag_is(proto, pn, "https") ||
           html_tag_is(proto, pn, "mailto");
}

static int html_is_safe_srcset_spans(const html_span_t *in, int n) {
    html_span_t sp[3];
    if (n <= 0) return 1;
    if (n > 3) n = 3;
    memcpy(sp, in, (size_t)n * sizeof(sp[0]));
    while (!html_spans_done(sp, n)) {
        html_spans_skip_ws(sp, n);
        if (html_spans_done(sp, n)) return 1;
        if (!html_is_safe_url_spans(sp, n, 1)) return 0;
        while (!html_spans_done(sp, n) && html_spans_peek(sp, n) != ',')
            html_spans_next(sp, n);
        if (!html_spans_done(sp, n)) html_spans_next(sp, n);
    }
    return 1;
}

static size_t html_url_follow_len(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && html_is_ascii_ws((unsigned char)s[i])) i++;
    for (; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\'' || c == '>' || c == '<' ||
            html_is_ascii_ws(c))
            break;
    }
    return i;
}

static int html_url_is_query_or_frag(const char *p, size_t n) {
    if (!p) return 0;
    for (size_t i = 0; i < n; i++)
        if (p[i] == '?' || p[i] == '#') return 1;
    return 0;
}

static int html_is_srcset_attr(const char *name, size_t nlen) {
    return html_attr_name_eq(name, nlen, "srcset") ||
           html_attr_name_eq(name, nlen, "imagesrcset");
}

typedef enum {
    HTML_ATTR_PLAIN,
    HTML_ATTR_URL,
    HTML_ATTR_SRCSET,
    HTML_ATTR_STYLE,
    HTML_ATTR_EVENT,
    HTML_ATTR_SRCDOC,
    HTML_ATTR_UNSAFE
} html_attr_kind_t;

/*
 * Go html/template attrType: strip a data- prefix, or a namespace (xmlns:*
 * is always a URL; otherwise use the local name). Then classify srcset,
 * srcdoc, style, sandbox/http-equiv, on*, the URL allowlist, and names
 * containing src/uri/url. srcdoc is checked before the src heuristic so it
 * stays double-escaped HTML rather than a URL.
 */
static html_attr_kind_t html_attr_kind(const char *name, size_t nlen) {
    if (!name || nlen == 0) return HTML_ATTR_PLAIN;

    const char *n = name;
    size_t len = nlen;
    if (len >= 5 && html_ci_eq_n(n, "data-", 5)) {
        n += 5;
        len -= 5;
    } else {
        size_t i;
        for (i = 0; i < len; i++)
            if (n[i] == ':') break;
        if (i < len) {
            if (i == 5 && html_ci_eq_n(n, "xmlns", 5))
                return HTML_ATTR_URL;
            n += i + 1;
            len -= i + 1;
        }
    }
    if (len == 0) return HTML_ATTR_PLAIN;

    if (html_is_srcset_attr(n, len))
        return HTML_ATTR_SRCSET;
    if (html_attr_name_eq(n, len, "srcdoc"))
        return HTML_ATTR_SRCDOC;
    if (html_attr_name_eq(n, len, "style"))
        return HTML_ATTR_STYLE;
    if (html_attr_name_eq(n, len, "sandbox") ||
        html_attr_name_eq(n, len, "http-equiv"))
        return HTML_ATTR_UNSAFE;
    if (len > 2 && html_ci_eq_n(n, "on", 2))
        return HTML_ATTR_EVENT;
    if (html_is_url_attr_name(n, len))
        return HTML_ATTR_URL;
    if (html_contains_ci_n(n, len, "src") ||
        html_contains_ci_n(n, len, "uri") ||
        html_contains_ci_n(n, len, "url"))
        return HTML_ATTR_URL;
    return HTML_ATTR_PLAIN;
}

/* Assemble prefix + value + following static text and apply Go's URL
 * allowlist. Suffix catches `href="{{.X}}:alert(1)"` / `java{{.X}}cript:`. */
static int html_url_parts_unsafe(const char *prefix, size_t plen,
                                 const char *val,
                                 const char *suffix, size_t slen,
                                 int srcset) {
    html_span_t sp[3];
    int n = 0;
    if (prefix && plen) { sp[n].s = prefix; sp[n].n = plen; sp[n].i = 0; n++; }
    if (val && val[0]) {
        sp[n].s = val;
        sp[n].n = strlen(val);
        sp[n].i = 0;
        n++;
    }
    if (suffix && slen) { sp[n].s = suffix; sp[n].n = slen; sp[n].i = 0; n++; }
    if (n == 0) return 0;
    return srcset ? !html_is_safe_srcset_spans(sp, n)
                  : !html_is_safe_url_spans(sp, n, 0);
}

static int html_css_contains_ci(const char *s, const char *needle) {
    size_t n = strlen(s), k = strlen(needle);
    if (k == 0 || k > n) return 0;
    for (size_t i = 0; i + k <= n; i++)
        if (html_ci_eq_n(s + i, needle, k)) return 1;
    return 0;
}

/* url() argument: allowlist like href, stopped at ) / quotes / ';'. */
static int html_css_url_arg_unsafe(const char *p) {
    size_t n = 0;
    while (p[n] && p[n] != ')' && p[n] != '"' && p[n] != '\'' &&
           p[n] != ';')
        n++;
    html_span_t sp = { p, n, 0 };
    return !html_is_safe_url_spans(&sp, 1, 0);
}

static int html_css_has_unsafe_url(const char *s) {
    if (!s) return 0;
    size_t n = strlen(s);
    for (size_t i = 0; i + 4 <= n; i++) {
        if (!html_ci_eq_n(s + i, "url(", 4)) continue;
        const char *p = s + i + 4;
        while (*p && html_is_ascii_ws((unsigned char)*p)) p++;
        if (*p == '"' || *p == '\'') p++;
        if (html_css_url_arg_unsafe(p)) return 1;
    }
    return 0;
}

/*
 * CSS hex-escaping is undone by the browser, so the raw value is what
 * matters. Reject @import / expression / -moz-binding, CSS escapes that
 * hide schemes, javascript:/data:/vbscript: anywhere, and unsafe url().
 * CSS slash-star comments hide schemes: java + comment + script: is a
 * relative URL (the slash trips the allowlist) and becomes javascript:
 * after the comment is stripped.
 */
static int html_css_is_unsafe(const char *s) {
    if (!s) return 0;
    size_t n = strlen(s);
    if (memchr(s, '\\', n)) return 1;
    if (strstr(s, "/*") || strstr(s, "*/")) return 1;
    if (html_css_has_unsafe_url(s)) return 1;
    if (html_css_contains_ci(s, "@import")) return 1;
    if (html_css_contains_ci(s, "expression")) return 1;
    if (html_css_contains_ci(s, "moz-binding")) return 1;
    for (size_t i = 0; i < n; i++) {
        if (html_scheme_is_parts(NULL, 0, s + i, "javascript") ||
            html_scheme_is_parts(NULL, 0, s + i, "vbscript") ||
            html_scheme_is_parts(NULL, 0, s + i, "data"))
            return 1;
    }
    return 0;
}

/* Event handlers: wrap as a JS string so `onclick="{{.X}}"` / `onclick={{.X}}`
 * with X=alert(1) cannot run. */
static char *html_escape_event(const char *s) {
    char *js = neverc_html_js_escape(s);
    if (!js) return NULL;
    size_t n = strlen(js);
    if (n > SIZE_MAX - 3U) { free(js); return NULL; }
    char *lit = (char *)NC_HTML_TEMPLATE_MALLOC(n + 3U);
    if (!lit) { free(js); return NULL; }
    lit[0] = '\'';
    memcpy(lit + 1, js, n);
    lit[n + 1] = '\'';
    lit[n + 2] = '\0';
    free(js);
    char *escaped = neverc_html_escape(lit);
    free(lit);
    return escaped;
}

static int execute_nodes(const node_t *n,
                         const neverc_html_template_data_t *data,
                         char **buf, size_t *len, size_t *cap) {
    while (n) {
        switch (n->type) {
        case NODE_TEXT:
            if (buf_append(buf, len, cap, n->text, n->text_len) != 0)
                return -1;
            break;
        case NODE_VAR: {
            const char *val = neverc_html_template_data_get(data, n->text);
            if (val) {
                int quoted = 0, in_attr = 0;
                const char *aname = NULL;
                size_t nlen = 0;
                const char *aprefix = NULL;
                size_t aplen = 0;
                int in_script = 0, in_style_tag = 0, in_script_comment = 0;
                int in_open_tag = 0;
                int in_meta = 0, meta_refresh = 0;
                html_scan_doc(*buf, *len, &in_script, &in_style_tag,
                              &in_script_comment,
                              &in_open_tag, &in_attr, &quoted,
                              &aname, &nlen, &aprefix, &aplen,
                              &in_meta, &meta_refresh);
                const char *uprefix = NULL;
                size_t uplen = 0;
                int in_css_url = html_in_css_url(*buf, *len, &uprefix, &uplen);
                int in_refresh_url = in_attr && in_meta &&
                    html_attr_name_eq(aname, nlen, "content") &&
                    (meta_refresh ||
                     html_contains_ci_n(aprefix, aplen, "url=") ||
                     (n->next && n->next->type == NODE_TEXT &&
                      n->next->text &&
                      html_contains_ci_n(n->next->text, n->next->text_len,
                                         "http-equiv") &&
                      html_contains_ci_n(n->next->text, n->next->text_len,
                                         "refresh")));
                html_attr_kind_t akind = in_attr
                    ? html_attr_kind(aname, nlen) : HTML_ATTR_PLAIN;
                int in_url = in_css_url ||
                    (in_attr && (akind == HTML_ATTR_URL ||
                                 akind == HTML_ATTR_SRCSET)) ||
                    in_refresh_url;
                int in_event = in_attr && akind == HTML_ATTR_EVENT;
                int in_style_attr = in_attr && akind == HTML_ATTR_STYLE;
                int in_srcdoc = in_attr && akind == HTML_ATTR_SRCDOC;
                int in_unsafe_attr = in_attr && akind == HTML_ATTR_UNSAFE;
                int unquoted = in_attr && !quoted;
                const char *dprefix = in_css_url ? uprefix :
                    (in_url ? aprefix : NULL);
                size_t dplen = in_css_url ? uplen : (in_url ? aplen : 0);
                const char *dsuffix = NULL;
                size_t dslen = 0;
                if (in_url && n->next && n->next->type == NODE_TEXT &&
                    n->next->text)
                    dslen = html_url_follow_len(n->next->text,
                                                n->next->text_len);
                if (dslen) dsuffix = n->next->text;
                int is_srcset = in_attr && !in_css_url &&
                    akind == HTML_ATTR_SRCSET;
                int in_query = in_attr && in_url && !in_css_url &&
                    html_url_is_query_or_frag(aprefix, aplen);
                char *escaped;
                /* URL context wins over <script src="..."> so javascript:/data:
                 * are neutralized instead of JS-escaped (a no-op for those). */
                if (in_unsafe_attr)
                    escaped = neverc_html_escape("ZgotmplZ");
                else if (in_url && in_query)
                    escaped = neverc_html_url_query_escape(val);
                else if (in_url && html_url_parts_unsafe(
                             dprefix, dplen, val, dsuffix, dslen, is_srcset))
                    escaped = neverc_html_escape("#");
                else if (in_event && unquoted && aplen > 0)
                    escaped = neverc_html_escape("ZgotmplZ");
                else if (in_event)
                    escaped = html_escape_event(val);
                else if (in_srcdoc)
                    escaped = html_escape_srcdoc(val);
                else if (in_script && !in_attr && in_script_comment)
                    escaped = neverc_html_escape("ZgotmplZ");
                else if (in_script && !in_attr &&
                         !html_prev_non_ws_is_quote(*buf, *len) &&
                         html_prev_non_ws_is_digit(*buf, *len))
                    escaped = neverc_html_escape("ZgotmplZ");
                else if (in_script && !in_attr)
                    escaped = html_prev_non_ws_is_quote(*buf, *len)
                        ? neverc_html_js_escape(val)
                        : html_js_expr(val);
                else if (in_style_tag || in_style_attr)
                    escaped = html_css_is_unsafe(val)
                        ? neverc_html_escape("#")
                        : neverc_html_css_escape(val);
                else if (!in_attr && in_open_tag)
                    escaped = dup_cstr("ZgotmplZ");
                else if (unquoted && aplen > 0 &&
                         html_unquoted_value_is_unsafe(val))
                    escaped = neverc_html_escape("ZgotmplZ");
                else
                    escaped = neverc_html_escape(val);
                if (!escaped) return -1;
                /* Quote a whole unquoted value so spaces cannot start a new
                 * attribute. Skip when a prefix is already in the value. */
                if (unquoted && aplen == 0 &&
                    buf_append(buf, len, cap, "\"", 1) != 0) {
                    free(escaped);
                    return -1;
                }
                if (buf_append(buf, len, cap, escaped, strlen(escaped)) != 0) {
                    free(escaped);
                    return -1;
                }
                free(escaped);
                if (unquoted && aplen == 0 &&
                    buf_append(buf, len, cap, "\"", 1) != 0)
                    return -1;
            }
            break;
        }
        case NODE_IF: {
            const char *val = neverc_html_template_data_get(data, n->text);
            int truthy = (val && val[0] != '\0' &&
                          strcmp(val, "0") != 0 &&
                          strcmp(val, "false") != 0);
            if (truthy)
                { if (execute_nodes(n->children, data, buf, len, cap) != 0) return -1; }
            else if (n->else_branch)
                { if (execute_nodes(n->else_branch, data, buf, len, cap) != 0) return -1; }
            break;
        }
        case NODE_RANGE:
            if (neverc_html_template_data_get(data, n->text) &&
                execute_nodes(n->children, data, buf, len, cap) != 0)
                return -1;
            break;
        default:
            break;
        }
        n = n->next;
    }
    return 0;
}

char *neverc_html_template_execute(const neverc_html_template_t *t,
                                    const neverc_html_template_data_t *data) {
    if (!t) return NULL;
    size_t len = 0, cap = 256U;
    char *buf = (char *)NC_HTML_TEMPLATE_MALLOC(cap);
    if (!buf) return NULL;
    buf[0] = '\0';
    if (execute_nodes(t->root, data, &buf, &len, &cap) != 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

char *neverc_html_template_render(const char *src,
                                   const neverc_html_template_data_t *data) {
    neverc_html_template_t *t = neverc_html_template_parse(src);
    if (!t) return NULL;
    char *result = neverc_html_template_execute(t, data);
    neverc_html_template_free(t);
    return result;
}
