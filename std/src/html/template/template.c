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

/* \ ' " \n \r escape to 2 chars (extra 1); < > & ` $ escape to \u00XX (extra 5). */
static const uint8_t js_esc_extra[256] = {
    ['\\'] = 1, ['\''] = 1, ['"'] = 1, ['\n'] = 1, ['\r'] = 1,
    ['<'] = 5, ['>'] = 5, ['&'] = 5, ['`'] = 5, ['$'] = 5,
};

char *neverc_html_js_escape(const char *s) {
    if (!s) return dup_cstr("");
    size_t slen = strlen(s), extra = 0;
    for (size_t i = 0; i < slen; i++) {
        size_t add = js_esc_extra[(unsigned char)s[i]];
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
        if (js_esc_extra[c] == 0) { r[wi++] = (char)c; continue; }
        switch (c) {
            case '\\': memcpy(r + wi, "\\\\",   2); wi += 2; break;
            case '\'': memcpy(r + wi, "\\'",    2); wi += 2; break;
            case '"':  memcpy(r + wi, "\\\"",   2); wi += 2; break;
            case '\n': memcpy(r + wi, "\\n",    2); wi += 2; break;
            case '\r': memcpy(r + wi, "\\r",    2); wi += 2; break;
            case '<':  memcpy(r + wi, "\\u003c", 6); wi += 6; break;
            case '>':  memcpy(r + wi, "\\u003e", 6); wi += 6; break;
            case '&':  memcpy(r + wi, "\\u0026", 6); wi += 6; break;
            case '`':  memcpy(r + wi, "\\u0060", 6); wi += 6; break;
            case '$':  memcpy(r + wi, "\\u0024", 6); wi += 6; break;
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
    if (trimlen == 4 && memcmp(start, "else", 4) == 0) {
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

static int html_in_named_tag(const char *buf, size_t len,
                             const char *open, size_t open_len,
                             const char *close, size_t close_len) {
    if (!buf || len == 0) return 0;
    int in = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != '<') continue;
        if (i + close_len <= len && html_ci_eq_n(buf + i, close, close_len))
            in = 0;
        else if (i + open_len <= len && html_ci_eq_n(buf + i, open, open_len) &&
                 (i + open_len == len ||
                  !nc_isalnum((unsigned char)buf[i + open_len])))
            in = 1;
    }
    return in;
}

static int html_in_script(const char *buf, size_t len) {
    return html_in_named_tag(buf, len, "<script", 7, "</script", 8);
}

static int html_in_style_tag(const char *buf, size_t len) {
    return html_in_named_tag(buf, len, "<style", 6, "</style", 7);
}

static int html_attr_name_eq(const char *name, size_t nlen, const char *want) {
    size_t wlen = strlen(want);
    return nlen == wlen && html_ci_eq_n(name, want, nlen);
}

static int html_is_url_attr_name(const char *name, size_t nlen) {
    static const char *urls[] = {
        "href", "src", "action", "formaction", "cite", "poster",
        "background", "data", "srcset", "ping", "xlink:href",
        "longdesc", "usemap", "icon", "manifest", "archive",
        "classid", "codebase", "profile", NULL
    };
    for (int i = 0; urls[i]; i++)
        if (html_attr_name_eq(name, nlen, urls[i])) return 1;
    return 0;
}

static int html_parse_attr_name(const char *buf, size_t end,
                                const char **name, size_t *nlen) {
    while (end > 0 && html_is_ascii_ws((unsigned char)buf[end - 1])) end--;
    if (end == 0 || buf[end - 1] != '=') return 0;
    end--;
    while (end > 0 && html_is_ascii_ws((unsigned char)buf[end - 1])) end--;
    size_t i = end;
    while (i > 0 && (nc_isalnum((unsigned char)buf[i - 1]) ||
                     buf[i - 1] == '-' || buf[i - 1] == ':'))
        i--;
    if (i == end) return 0;
    *name = buf + i;
    *nlen = end - i;
    return 1;
}

/*
 * Locate `name=` / `name="` / `name='` even with whitespace around '=' or
 * after the opening quote (`href=" {{.X}}"`), and when the interpolation
 * continues an already-started value (`href="java{{.X}}"`).
 */
static int html_trailing_attr(const char *buf, size_t len, int *quoted,
                              const char **name, size_t *nlen,
                              const char **prefix, size_t *plen) {
    *quoted = 0;
    *prefix = NULL;
    *plen = 0;
    if (!buf || len == 0) return 0;

    size_t end = len;
    while (end > 0 && html_is_ascii_ws((unsigned char)buf[end - 1])) end--;
    if (end == 0) return 0;

    if (buf[end - 1] == '"' || buf[end - 1] == '\'') {
        *quoted = 1;
        return html_parse_attr_name(buf, end - 1, name, nlen);
    }

    size_t i = end;
    while (i > 0) {
        char c = buf[i - 1];
        if (c == '"' || c == '\'' || c == '=' || c == '<' || c == '>')
            break;
        i--;
    }
    if (i == 0) return 0;
    if (buf[i - 1] == '"' || buf[i - 1] == '\'') {
        *quoted = 1;
        *prefix = buf + i;
        *plen = end - i;
        return html_parse_attr_name(buf, i - 1, name, nlen);
    }
    if (buf[i - 1] == '=') {
        *quoted = 0;
        *prefix = buf + i;
        *plen = end - i;
        return html_parse_attr_name(buf, i, name, nlen);
    }
    return 0;
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

static int html_span_done(const html_span_t *a, const html_span_t *b) {
    return a->i >= a->n && b->i >= b->n;
}

static unsigned char html_span_peek(const html_span_t *a, const html_span_t *b) {
    if (a->i < a->n) return (unsigned char)a->s[a->i];
    if (b->i < b->n) return (unsigned char)b->s[b->i];
    return 0;
}

static void html_span_next(html_span_t *a, html_span_t *b) {
    if (a->i < a->n) a->i++;
    else if (b->i < b->n) b->i++;
}

static void html_span_skip_ws(html_span_t *a, html_span_t *b) {
    while (!html_span_done(a, b) && html_is_ascii_ws(html_span_peek(a, b)))
        html_span_next(a, b);
}

/* Match a scheme across a static prefix plus the interpolated value, even
 * when ASCII whitespace is sprinkled through it (`java\tscript:`). */
static int html_scheme_is_parts(const char *prefix, size_t plen,
                                const char *s, const char *scheme) {
    if (!scheme) return 0;
    html_span_t a = { prefix ? prefix : "", plen, 0 };
    html_span_t b = { s ? s : "", s ? strlen(s) : 0, 0 };
    html_span_skip_ws(&a, &b);
    while (*scheme) {
        html_span_skip_ws(&a, &b);
        if (html_span_done(&a, &b)) return 0;
        char ch = (char)html_span_peek(&a, &b);
        html_span_next(&a, &b);
        char want = *scheme++;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        if (want >= 'A' && want <= 'Z') want = (char)(want + 32);
        if (ch != want) return 0;
    }
    html_span_skip_ws(&a, &b);
    return !html_span_done(&a, &b) && html_span_peek(&a, &b) == ':';
}

static int html_dangerous_url_parts(const char *prefix, size_t plen,
                                    const char *s) {
    static const char *schemes[] = { "javascript", "vbscript", "data", NULL };
    for (int i = 0; schemes[i]; i++) {
        if (html_scheme_is_parts(NULL, 0, s, schemes[i])) return 1;
        if (plen > 0 && html_scheme_is_parts(prefix, plen, s, schemes[i]))
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
                int quoted = 0;
                const char *aname = NULL;
                size_t nlen = 0;
                const char *aprefix = NULL;
                size_t aplen = 0;
                int in_attr = html_trailing_attr(
                    *buf, *len, &quoted, &aname, &nlen, &aprefix, &aplen);
                const char *uprefix = NULL;
                size_t uplen = 0;
                int in_css_url = html_in_css_url(*buf, *len, &uprefix, &uplen);
                int in_url = in_css_url ||
                    (in_attr && html_is_url_attr_name(aname, nlen));
                int in_event = in_attr && nlen > 2 &&
                    html_ci_eq_n(aname, "on", 2);
                int in_style_attr = in_attr &&
                    html_attr_name_eq(aname, nlen, "style");
                int unquoted = in_attr && !quoted;
                const char *dprefix = in_css_url ? uprefix :
                    (in_url ? aprefix : NULL);
                size_t dplen = in_css_url ? uplen : (in_url ? aplen : 0);
                char *escaped;
                /* URL context wins over <script src="..."> so javascript:/data:
                 * are neutralized instead of JS-escaped (a no-op for those). */
                if (in_url && html_dangerous_url_parts(dprefix, dplen, val))
                    escaped = neverc_html_escape("#");
                else if (in_event)
                    escaped = html_escape_event(val);
                else if (html_in_script(*buf, *len))
                    escaped = neverc_html_js_escape(val);
                else if (html_in_style_tag(*buf, *len) || in_style_attr)
                    escaped = neverc_html_css_escape(val);
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
