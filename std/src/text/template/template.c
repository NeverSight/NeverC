#include "neverc/std/text/template.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef NC_TEMPLATE_MALLOC
#define NC_TEMPLATE_MALLOC malloc
#endif
#ifndef NC_TEMPLATE_CALLOC
#define NC_TEMPLATE_CALLOC calloc
#endif
#ifndef NC_TEMPLATE_REALLOC
#define NC_TEMPLATE_REALLOC realloc
#endif

enum { NODE_TEXT, NODE_VAR, NODE_IF, NODE_RANGE };

typedef struct tnode {
    int            type;
    char          *text;
    size_t         text_len;
    char          *key;
    struct tnode  *children;
    int            nchildren;
    int            cap;
    struct tnode  *else_branch;
    int            nelse;
    int            else_cap;
    struct tnode  *next;
} tnode_t;

struct neverc_template {
    tnode_t *nodes;
    int      nnodes;
    int      cap;
    size_t   out_hint;   /* total literal-text bytes, for output presizing */
};

static void free_nodes(tnode_t *nodes, int count);

static void free_node_contents(tnode_t *node) {
    if (!node) return;
    free(node->text);
    free(node->key);
    if (node->children) free_nodes(node->children, node->nchildren);
    if (node->else_branch) free_nodes(node->else_branch, node->nelse);
}

static int add_node(tnode_t **list, int *count, int *cap, tnode_t node) {
    if (*count >= *cap) {
        if (*cap > INT_MAX / 2) return -1;
        int new_cap = (*cap == 0) ? 8 : *cap * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(tnode_t)) return -1;
        tnode_t *new_list = (tnode_t *)NC_TEMPLATE_REALLOC(
            *list, (size_t)new_cap * sizeof(tnode_t));
        if (!new_list) return -1;
        *list = new_list;
        *cap = new_cap;
    }
    (*list)[(*count)++] = node;
    return 0;
}

static char *dup_str(const char *s, size_t n) {
    if ((!s && n > 0) || n == SIZE_MAX) return NULL;
    char *r = (char *)NC_TEMPLATE_MALLOC(n + 1U);
    if (!r) return NULL;
    if (n > 0) memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static int is_action_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *trim_ws(const char *s, size_t len) {
    while (len > 0 && is_action_ws(*s)) { s++; len--; }
    while (len > 0 && is_action_ws(s[len-1])) len--;
    return dup_str(s, len);
}

static int is_key_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* A data selector is ".Ident" or ".Ident.Ident...". Operators, pipelines,
 * and call/index syntax must not be swallowed into the key (a silent no-op). */
static char *parse_dot_key(const char *s) {
    if (!s || s[0] != '.') return NULL;
    s++;
    if (!is_key_char(*s)) return NULL;
    const char *start = s;
    for (;;) {
        while (is_key_char(*s)) s++;
        if (*s == '.') {
            s++;
            if (!is_key_char(*s)) return NULL;
            continue;
        }
        break;
    }
    if (*s != '\0') return NULL;
    return dup_str(start, (size_t)(s - start));
}

/* "if" / "range" plus whitespace (space, tab, or newline), matching Go. */
static int action_keyword(const char *action, const char *kw, const char **rest) {
    size_t n = strlen(kw);
    if (strncmp(action, kw, n) != 0) return 0;
    if (action[n] == '\0') {
        *rest = action + n;
        return 1;
    }
    if (!is_action_ws(action[n])) return 0;
    *rest = action + n;
    return 1;
}

static int parse_nodes(const char **p, const char *end,
                        tnode_t **nodes, int *count, int *cap,
                        int stop_on_end, int stop_on_else, int depth);

static int parse_action(const char **p, const char *end,
                        tnode_t **nodes, int *count, int *cap, int depth) {
    const char *start = *p;
    const char *close = strstr(start, "}}");
    if (!close || close + 2 > end) return -1;

    /* Go: trim markers are "{{- " and " -}}" (dash plus ASCII space).
     * A bare dash is part of the action ("{{-3}}", "{{.Name-}}"). */
    const char *inner = start + 2;
    if (inner + 1 < close && inner[0] == '-' && is_action_ws(inner[1]))
        inner++;
    const char *action_end = close;
    int right_trim = (action_end >= inner + 2 && action_end[-1] == '-' &&
                      is_action_ws(action_end[-2]));
    if (right_trim) action_end--;
    char *action = trim_ws(inner, (size_t)(action_end - inner));
    if (!action) return -1;
    *p = close + 2;
    if (right_trim) {
        while (*p < end && is_action_ws(**p)) (*p)++;
    }

    if (action[0] == '\0') {
        free(action);
        return -1;
    }

    const char *kw_rest = NULL;
    if (action_keyword(action, "if", &kw_rest)) {
        tnode_t node;
        memset(&node, 0, sizeof(node));
        node.type = NODE_IF;
        {
            char *rest = trim_ws(kw_rest, strlen(kw_rest));
            if (!rest) {
                free(action);
                return -1;
            }
            node.key = parse_dot_key(rest);
            free(rest);
            if (!node.key) {
                free(action);
                free_node_contents(&node);
                return -1;
            }
        }

        if (depth >= 128) {
            free(action);
            free_node_contents(&node);
            return -1;
        }
        int child_cap = 0;
        int child_result = parse_nodes(p, end, &node.children,
                                       &node.nchildren, &child_cap, 1, 1,
                                       depth + 1);
        if (child_result < 0) {
            free(action);
            free_node_contents(&node);
            return -1;
        }

        if (child_result == 2) {
            int else_cap = 0;
            int else_result = parse_nodes(
                p, end, &node.else_branch, &node.nelse, &else_cap, 1, 0,
                depth + 1);
            if (else_result != 1) {
                free(action);
                free_node_contents(&node);
                return -1;
            }
        } else if (child_result != 1) {
            free(action);
            free_node_contents(&node);
            return -1;
        }

        free(action);
        if (add_node(nodes, count, cap, node) != 0) {
            free_node_contents(&node);
            return -1;
        }
        return 0;
    }

    if (action_keyword(action, "range", &kw_rest)) {
        tnode_t node;
        memset(&node, 0, sizeof(node));
        node.type = NODE_RANGE;
        {
            char *rest = trim_ws(kw_rest, strlen(kw_rest));
            if (!rest) {
                free(action);
                return -1;
            }
            node.key = parse_dot_key(rest);
            free(rest);
            if (!node.key) {
                free(action);
                free_node_contents(&node);
                return -1;
            }
        }
        if (depth >= 128) {
            free(action);
            free_node_contents(&node);
            return -1;
        }
        int child_cap = 0;
        int child_result = parse_nodes(
            p, end, &node.children, &node.nchildren, &child_cap, 1, 0,
            depth + 1);
        if (child_result != 1) {
            free(action);
            free_node_contents(&node);
            return -1;
        }
        free(action);
        if (add_node(nodes, count, cap, node) != 0) {
            free_node_contents(&node);
            return -1;
        }
        return 0;
    }

    if (strcmp(action, "end") == 0) {
        free(action);
        return 1;
    }

    if (strcmp(action, "else") == 0) {
        free(action);
        return 2;
    }

    /* Variable: {{.Key}} — not a pipeline or function call. */
    tnode_t node;
    memset(&node, 0, sizeof(node));
    node.type = NODE_VAR;
    node.key = parse_dot_key(action);
    free(action);
    if (!node.key || add_node(nodes, count, cap, node) != 0) {
        free_node_contents(&node);
        return -1;
    }
    return 0;
}

static int parse_nodes(const char **p, const char *end,
                        tnode_t **nodes, int *count, int *cap,
                        int stop_on_end, int stop_on_else, int depth) {
    while (*p < end) {
        const char *next = strstr(*p, "{{");
        if (!next) {
            if (*p < end) {
                tnode_t node;
                memset(&node, 0, sizeof(node));
                node.type = NODE_TEXT;
                node.text_len = (size_t)(end - *p);
                node.text = dup_str(*p, node.text_len);
                if (!node.text || add_node(nodes, count, cap, node) != 0) {
                    free_node_contents(&node);
                    return -1;
                }
            }
            *p = end;
            return (stop_on_end || stop_on_else) ? -1 : 0;
        }

        if (next > *p) {
            tnode_t node;
            memset(&node, 0, sizeof(node));
            node.type = NODE_TEXT;
            node.text_len = (size_t)(next - *p);
            node.text = dup_str(*p, node.text_len);
            if (!node.text || add_node(nodes, count, cap, node) != 0) {
                free_node_contents(&node);
                return -1;
            }
        }

        *p = next;
        if (next + 3 < end && next[2] == '-' && is_action_ws(next[3]) &&
            *count > 0) {
            tnode_t *prev = &(*nodes)[*count - 1];
            if (prev->type == NODE_TEXT && prev->text) {
                while (prev->text_len > 0 &&
                       is_action_ws(prev->text[prev->text_len - 1]))
                    prev->text[--prev->text_len] = '\0';
            }
        }
        const char *close = strstr(*p + 2, "}}");
        if (!close) return -1;

        int r = parse_action(p, end, nodes, count, cap, depth);
        if (r < 0) return -1;
        if (r == 1) return stop_on_end ? 1 : -1;
        if (r == 2) return stop_on_else ? 2 : -1;
    }
    return (stop_on_end || stop_on_else) ? -1 : 0;
}

/* Sum every literal-text node's length (including nested branches) so execute()
 * can presize its output buffer and skip the grow-by-doubling reallocs. Unused
 * if/else branches are included — it is only a hint, and over-reserving a render
 * buffer is cheaper than repeatedly reallocating it. */
static size_t sum_text_len(const tnode_t *nodes, int count) {
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        const tnode_t *n = &nodes[i];
        size_t part = n->type == NODE_TEXT ? n->text_len : 0;
        if (part > SIZE_MAX - total) return SIZE_MAX;
        total += part;
        if (n->children) {
            part = sum_text_len(n->children, n->nchildren);
            if (part > SIZE_MAX - total) return SIZE_MAX;
            total += part;
        }
        if (n->else_branch) {
            part = sum_text_len(n->else_branch, n->nelse);
            if (part > SIZE_MAX - total) return SIZE_MAX;
            total += part;
        }
    }
    return total;
}

neverc_template_t *neverc_template_parse(const char *text, const char **errp) {
    static const char parse_error[] = "invalid template or out of memory";
    if (errp) *errp = parse_error;
    if (!text) return NULL;

    neverc_template_t *tmpl =
        (neverc_template_t *)NC_TEMPLATE_CALLOC(1, sizeof(*tmpl));
    if (!tmpl) return NULL;
    const char *p = text;
    const char *end = text + strlen(text);
    if (parse_nodes(&p, end, &tmpl->nodes, &tmpl->nnodes, &tmpl->cap,
                    0, 0, 0) != 0) {
        neverc_template_free(tmpl);
        return NULL;
    }
    tmpl->out_hint = sum_text_len(tmpl->nodes, tmpl->nnodes);
    if (errp) *errp = NULL;
    return tmpl;
}

static void free_nodes(tnode_t *nodes, int count) {
    for (int i = 0; i < count; i++) {
        free(nodes[i].text);
        free(nodes[i].key);
        if (nodes[i].children)
            free_nodes(nodes[i].children, nodes[i].nchildren);
        if (nodes[i].else_branch)
            free_nodes(nodes[i].else_branch, nodes[i].nelse);
    }
    free(nodes);
}

void neverc_template_free(neverc_template_t *tmpl) {
    if (!tmpl) return;
    free_nodes(tmpl->nodes, tmpl->nnodes);
    free(tmpl);
}

/* Template data */
void neverc_template_data_init(neverc_template_data_t *d) {
    if (!d) return;
    d->vars = NULL;
    d->nvars = 0;
    d->cap = 0;
}

void neverc_template_data_set(neverc_template_data_t *d,
                               const char *key, const char *value) {
    if (!d || !key || d->nvars < 0 || d->cap < 0 || d->nvars > d->cap ||
        (d->nvars > 0 && !d->vars)) return;
    for (int i = 0; i < d->nvars; i++) {
        if (strcmp(d->vars[i].key, key) == 0) {
            d->vars[i].value = value;
            return;
        }
    }
    if (d->nvars >= d->cap) {
        if (d->cap > INT_MAX / 2) return;
        int new_cap = d->cap == 0 ? 8 : d->cap * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(neverc_template_var_t)) return;
        neverc_template_var_t *new_vars =
            (neverc_template_var_t *)NC_TEMPLATE_REALLOC(
                d->vars, (size_t)new_cap * sizeof(neverc_template_var_t));
        if (!new_vars) return;
        d->vars = new_vars;
        d->cap = new_cap;
    }
    d->vars[d->nvars].key = key;
    d->vars[d->nvars].value = value;
    d->nvars++;
}

const char *neverc_template_data_get(const neverc_template_data_t *d,
                                     const char *key) {
    if (!d || !key || d->nvars < 0 || d->cap < 0 || d->nvars > d->cap ||
        (d->nvars > 0 && !d->vars)) return NULL;
    for (int i = 0; i < d->nvars; i++)
        if (strcmp(d->vars[i].key, key) == 0)
            return d->vars[i].value;
    return NULL;
}

void neverc_template_data_free(neverc_template_data_t *d) {
    if (!d) return;
    free(d->vars);
    d->vars = NULL;
    d->nvars = d->cap = 0;
}

/* Execution */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} outbuf_t;

static int out_init(outbuf_t *b, size_t hint) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    if (hint > SIZE_MAX - 16U) return -1;
    b->cap = (hint < 64U) ? 64U : hint + 16U;
    b->data = (char *)NC_TEMPLATE_MALLOC(b->cap);
    return b->data ? 0 : -1;
}
static int out_puts(outbuf_t *b, const char *s, size_t n) {
    if ((!s && n > 0) || n > SIZE_MAX - 1U || b->len > SIZE_MAX - n - 1U)
        return -1;
    size_t required = b->len + n + 1U;
    if (required > b->cap) {
        size_t new_cap = b->cap;
        while (new_cap < required) {
            if (new_cap > SIZE_MAX / 2U) {
                new_cap = required;
                break;
            }
            new_cap *= 2U;
        }
        char *new_data = (char *)NC_TEMPLATE_REALLOC(b->data, new_cap);
        if (!new_data) return -1;
        b->data = new_data;
        b->cap = new_cap;
    }
    if (n > 0) memcpy(b->data + b->len, s, n);
    b->len += n;
    return 0;
}

static int exec_nodes(outbuf_t *out, const tnode_t *nodes, int count,
                      const neverc_template_data_t *data) {
    for (int i = 0; i < count; i++) {
        const tnode_t *n = &nodes[i];
        switch (n->type) {
        case NODE_TEXT:
            if (out_puts(out, n->text, n->text_len) != 0) return -1;
            break;
        case NODE_VAR: {
            const char *val = neverc_template_data_get(data, n->key);
            if (val && out_puts(out, val, strlen(val)) != 0) return -1;
            break;
        }
        case NODE_IF: {
            const char *val = neverc_template_data_get(data, n->key);
            int truthy = val && val[0] != '\0' && strcmp(val, "0") != 0 &&
                         strcmp(val, "false") != 0;
            if (truthy)
                { if (exec_nodes(out, n->children, n->nchildren, data) != 0) return -1; }
            else if (n->else_branch)
                { if (exec_nodes(out, n->else_branch, n->nelse, data) != 0) return -1; }
            break;
        }
        case NODE_RANGE:
            /* Simplified: just execute children once if key exists */
            if (neverc_template_data_get(data, n->key))
                { if (exec_nodes(out, n->children, n->nchildren, data) != 0) return -1; }
            break;
        }
    }
    return 0;
}

char *neverc_template_execute(neverc_template_t *tmpl,
                               const neverc_template_data_t *data,
                               size_t *outlen) {
    if (outlen) *outlen = 0;
    if (!tmpl) return NULL;
    outbuf_t out;
    if (out_init(&out, tmpl->out_hint) != 0) return NULL;
    if (exec_nodes(&out, tmpl->nodes, tmpl->nnodes, data) != 0) {
        free(out.data);
        return NULL;
    }
    out.data[out.len] = '\0';
    if (outlen) *outlen = out.len;
    return out.data;
}

char *neverc_template_render(const char *tmpl_text,
                              const neverc_template_data_t *data,
                              size_t *outlen) {
    if (outlen) *outlen = 0;
    neverc_template_t *tmpl = neverc_template_parse(tmpl_text, NULL);
    if (!tmpl) return NULL;
    char *result = neverc_template_execute(tmpl, data, outlen);
    neverc_template_free(tmpl);
    return result;
}
