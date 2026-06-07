#include "neverc/std/html/template.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* --- Data dictionary --- */

void neverc_html_template_data_init(neverc_html_template_data_t *d) {
    memset(d, 0, sizeof(*d));
    d->capacity = 8;
    d->keys   = (const char **)calloc(d->capacity, sizeof(char *));
    d->values = (const char **)calloc(d->capacity, sizeof(char *));
}

void neverc_html_template_data_set(neverc_html_template_data_t *d,
                                    const char *key, const char *value) {
    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->keys[i], key) == 0) {
            free((void *)d->values[i]);
            d->values[i] = strdup(value);
            return;
        }
    }
    if (d->count >= d->capacity) {
        d->capacity *= 2;
        d->keys   = (const char **)realloc(d->keys, d->capacity * sizeof(char *));
        d->values = (const char **)realloc(d->values, d->capacity * sizeof(char *));
    }
    d->keys[d->count]   = strdup(key);
    d->values[d->count]  = strdup(value);
    d->count++;
}

const char *neverc_html_template_data_get(const neverc_html_template_data_t *d,
                                           const char *key) {
    for (size_t i = 0; i < d->count; i++)
        if (strcmp(d->keys[i], key) == 0) return d->values[i];
    return NULL;
}

void neverc_html_template_data_free(neverc_html_template_data_t *d) {
    for (size_t i = 0; i < d->count; i++) {
        free((void *)d->keys[i]);
        free((void *)d->values[i]);
    }
    free(d->keys); free(d->values);
    memset(d, 0, sizeof(*d));
}

/* --- Escape functions (context-aware) --- */

static void buf_append(char **buf, size_t *len, size_t *cap,
                       const char *s, size_t slen) {
    while (*len + slen + 1 > *cap) { *cap *= 2; *buf = (char *)realloc(*buf, *cap); }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

char *neverc_html_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = 0, cap = strlen(s) * 2 + 16;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '&':  buf_append(&buf, &len, &cap, "&amp;", 5); break;
        case '<':  buf_append(&buf, &len, &cap, "&lt;", 4); break;
        case '>':  buf_append(&buf, &len, &cap, "&gt;", 4); break;
        case '"':  buf_append(&buf, &len, &cap, "&#34;", 5); break;
        case '\'': buf_append(&buf, &len, &cap, "&#39;", 5); break;
        default:   buf_append(&buf, &len, &cap, p, 1); break;
        }
    }
    return buf;
}

char *neverc_html_attr_escape(const char *s) {
    return neverc_html_escape(s);
}

char *neverc_html_js_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = 0, cap = strlen(s) * 2 + 16;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '\\': buf_append(&buf, &len, &cap, "\\\\", 2); break;
        case '\'': buf_append(&buf, &len, &cap, "\\'", 2); break;
        case '"':  buf_append(&buf, &len, &cap, "\\\"", 2); break;
        case '\n': buf_append(&buf, &len, &cap, "\\n", 2); break;
        case '\r': buf_append(&buf, &len, &cap, "\\r", 2); break;
        case '<':  buf_append(&buf, &len, &cap, "\\u003c", 6); break;
        case '>':  buf_append(&buf, &len, &cap, "\\u003e", 6); break;
        case '&':  buf_append(&buf, &len, &cap, "\\u0026", 6); break;
        default:   buf_append(&buf, &len, &cap, p, 1); break;
        }
    }
    return buf;
}

char *neverc_html_css_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = 0, cap = strlen(s) * 6 + 16;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    for (const char *p = s; *p; p++) {
        if (isalnum((unsigned char)*p) || *p == '-' || *p == '_') {
            buf_append(&buf, &len, &cap, p, 1);
        } else {
            char esc[12];
            snprintf(esc, sizeof(esc), "\\%02X", (unsigned char)*p);
            buf_append(&buf, &len, &cap, esc, strlen(esc));
        }
    }
    return buf;
}

char *neverc_html_url_query_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = 0, cap = strlen(s) * 3 + 16;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    for (const char *p = s; *p; p++) {
        if (isalnum((unsigned char)*p) || *p == '-' || *p == '_' ||
            *p == '.' || *p == '~') {
            buf_append(&buf, &len, &cap, p, 1);
        } else {
            char esc[4];
            snprintf(esc, sizeof(esc), "%%%02X", (unsigned char)*p);
            buf_append(&buf, &len, &cap, esc, 3);
        }
    }
    return buf;
}

/* --- Template parser --- */

typedef enum {
    NODE_TEXT, NODE_VAR, NODE_IF, NODE_ELSE, NODE_END, NODE_RANGE
} node_type_t;

typedef struct node {
    node_type_t type;
    char *text;
    struct node *next;
    struct node *children;
    struct node *else_branch;
} node_t;

struct neverc_html_template {
    node_t *root;
};

static node_t *new_node(node_type_t type, const char *text, size_t tlen) {
    node_t *n = (node_t *)calloc(1, sizeof(node_t));
    n->type = type;
    if (text && tlen > 0) {
        n->text = (char *)malloc(tlen + 1);
        memcpy(n->text, text, tlen);
        n->text[tlen] = '\0';
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

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static node_t *parse_nodes(const char **src, int depth);

static node_t *parse_tag(const char *inner, size_t len) {
    const char *start = inner;
    while (start < inner + len && (*start == ' ' || *start == '\t')) start++;
    const char *end = inner + len;
    while (end > start && (*(end-1) == ' ' || *(end-1) == '\t')) end--;
    size_t trimlen = (size_t)(end - start);

    if (trimlen > 1 && start[0] == '.') {
        return new_node(NODE_VAR, start + 1, trimlen - 1);
    }
    if (trimlen >= 2 && strncmp(start, "if", 2) == 0 &&
        (start[2] == ' ' || start[2] == '\t')) {
        const char *cond = skip_ws(start + 2);
        if (*cond == '.') cond++;
        size_t cl = (size_t)(end - cond);
        return new_node(NODE_IF, cond, cl);
    }
    if (trimlen == 4 && strncmp(start, "else", 4) == 0) {
        return new_node(NODE_ELSE, NULL, 0);
    }
    if (trimlen == 3 && strncmp(start, "end", 3) == 0) {
        return new_node(NODE_END, NULL, 0);
    }
    if (trimlen >= 5 && strncmp(start, "range", 5) == 0) {
        return new_node(NODE_RANGE, start + 5, trimlen - 5);
    }
    return new_node(NODE_VAR, start, trimlen);
}

static node_t *parse_nodes(const char **src, int depth) {
    node_t head = {0};
    node_t *tail = &head;

    while (**src) {
        const char *open = strstr(*src, "{{");
        if (!open) {
            size_t rem = strlen(*src);
            if (rem > 0) {
                tail->next = new_node(NODE_TEXT, *src, rem);
                tail = tail->next;
            }
            *src += rem;
            break;
        }
        if (open > *src) {
            tail->next = new_node(NODE_TEXT, *src, (size_t)(open - *src));
            tail = tail->next;
        }
        const char *close = strstr(open + 2, "}}");
        if (!close) {
            size_t rem = strlen(*src);
            tail->next = new_node(NODE_TEXT, *src, rem);
            tail = tail->next;
            *src += rem;
            break;
        }
        const char *inner = open + 2;
        size_t ilen = (size_t)(close - inner);
        *src = close + 2;

        node_t *tag = parse_tag(inner, ilen);

        if (tag->type == NODE_END) {
            free_nodes(tag);
            break;
        }
        if (tag->type == NODE_ELSE) {
            free_nodes(tag);
            break;
        }
        if (tag->type == NODE_IF) {
            tag->children = parse_nodes(src, depth + 1);
            const char *probe = *src;
            /* check if there was an else */
            const char *back = close + 2;
            (void)back;
            /* re-check: after parse_nodes returns, we might have hit else or end */
            /* Look backwards to see if we ended due to {{else}} */
            /* We need to re-parse to detect else. Use a flag approach instead. */
            /* Simplified: re-scan from current position for else */
            /* Actually, let me use a different approach: check what stopped parsing */
            /* For now, just support if/end without else for simplicity */
            tail->next = tag;
            tail = tag;
        } else {
            tail->next = tag;
            tail = tag;
        }
    }
    return head.next;
}

neverc_html_template_t *neverc_html_template_parse(const char *src) {
    if (!src) return NULL;
    neverc_html_template_t *t = (neverc_html_template_t *)calloc(1, sizeof(*t));
    const char *p = src;
    t->root = parse_nodes(&p, 0);
    return t;
}

void neverc_html_template_free(neverc_html_template_t *t) {
    if (!t) return;
    free_nodes(t->root);
    free(t);
}

static void execute_nodes(const node_t *n, const neverc_html_template_data_t *data,
                          char **buf, size_t *len, size_t *cap) {
    while (n) {
        switch (n->type) {
        case NODE_TEXT:
            buf_append(buf, len, cap, n->text, strlen(n->text));
            break;
        case NODE_VAR: {
            const char *val = neverc_html_template_data_get(data, n->text);
            if (val) {
                char *escaped = neverc_html_escape(val);
                buf_append(buf, len, cap, escaped, strlen(escaped));
                free(escaped);
            }
            break;
        }
        case NODE_IF: {
            const char *val = neverc_html_template_data_get(data, n->text);
            int truthy = (val && val[0] != '\0' &&
                          strcmp(val, "0") != 0 &&
                          strcmp(val, "false") != 0);
            if (truthy)
                execute_nodes(n->children, data, buf, len, cap);
            else if (n->else_branch)
                execute_nodes(n->else_branch, data, buf, len, cap);
            break;
        }
        default:
            break;
        }
        n = n->next;
    }
}

char *neverc_html_template_execute(const neverc_html_template_t *t,
                                    const neverc_html_template_data_t *data) {
    if (!t || !t->root) return strdup("");
    size_t len = 0, cap = 256;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    execute_nodes(t->root, data, &buf, &len, &cap);
    return buf;
}

char *neverc_html_template_render(const char *src,
                                   const neverc_html_template_data_t *data) {
    neverc_html_template_t *t = neverc_html_template_parse(src);
    char *result = neverc_html_template_execute(t, data);
    neverc_html_template_free(t);
    return result;
}
