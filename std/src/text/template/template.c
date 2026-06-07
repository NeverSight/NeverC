#include "neverc/std/text/template.h"
#include <stdlib.h>
#include <string.h>

enum { NODE_TEXT, NODE_VAR, NODE_IF, NODE_RANGE };

typedef struct tnode {
    int            type;
    char          *text;
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
};

static void add_node(tnode_t **list, int *count, int *cap, tnode_t node) {
    if (*count >= *cap) {
        *cap = (*cap == 0) ? 8 : *cap * 2;
        *list = (tnode_t *)realloc(*list, *cap * sizeof(tnode_t));
    }
    (*list)[(*count)++] = node;
}

static char *dup_str(const char *s, size_t n) {
    char *r = (char *)malloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static char *trim_ws(const char *s, size_t len) {
    while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    return dup_str(s, len);
}

static int parse_nodes(const char **p, const char *end,
                        tnode_t **nodes, int *count, int *cap,
                        int stop_on_end, int stop_on_else);

static int parse_action(const char **p, const char *end,
                        tnode_t **nodes, int *count, int *cap) {
    const char *start = *p;
    const char *close = strstr(start, "}}");
    if (!close) return -1;

    const char *inner = start + 2;
    size_t ilen = close - inner;
    char *action = trim_ws(inner, ilen);
    *p = close + 2;

    if (strncmp(action, "if ", 3) == 0) {
        tnode_t node;
        memset(&node, 0, sizeof(node));
        node.type = NODE_IF;
        node.key = trim_ws(action + 3, strlen(action + 3));
        if (node.key[0] == '.') {
            char *nk = dup_str(node.key + 1, strlen(node.key + 1));
            free(node.key);
            node.key = nk;
        }

        int child_cap = 0;
        parse_nodes(p, end, &node.children, &node.nchildren, &child_cap, 1, 1);

        if (*p + 2 < end && strncmp(*p, "{{", 2) == 0) {
            const char *check = *p + 2;
            while (*check == ' ') check++;
            if (strncmp(check, "else", 4) == 0) {
                const char *ec = strstr(*p, "}}");
                if (ec) *p = ec + 2;
                int else_cap = 0;
                parse_nodes(p, end, &node.else_branch, &node.nelse, &else_cap, 1, 0);
            }
        }

        free(action);
        add_node(nodes, count, cap, node);
        return 0;
    }

    if (strncmp(action, "range ", 6) == 0) {
        tnode_t node;
        memset(&node, 0, sizeof(node));
        node.type = NODE_RANGE;
        node.key = trim_ws(action + 6, strlen(action + 6));
        if (node.key[0] == '.') {
            char *nk = dup_str(node.key + 1, strlen(node.key + 1));
            free(node.key);
            node.key = nk;
        }
        int child_cap = 0;
        parse_nodes(p, end, &node.children, &node.nchildren, &child_cap, 1, 0);
        free(action);
        add_node(nodes, count, cap, node);
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

    /* Variable: {{.Key}} */
    tnode_t node;
    memset(&node, 0, sizeof(node));
    node.type = NODE_VAR;
    if (action[0] == '.') {
        node.key = dup_str(action + 1, strlen(action + 1));
    } else {
        node.key = dup_str(action, strlen(action));
    }
    free(action);
    add_node(nodes, count, cap, node);
    return 0;
}

static int parse_nodes(const char **p, const char *end,
                        tnode_t **nodes, int *count, int *cap,
                        int stop_on_end, int stop_on_else) {
    while (*p < end) {
        const char *next = strstr(*p, "{{");
        if (!next) {
            if (*p < end) {
                tnode_t node;
                memset(&node, 0, sizeof(node));
                node.type = NODE_TEXT;
                node.text = dup_str(*p, end - *p);
                add_node(nodes, count, cap, node);
            }
            *p = end;
            return 0;
        }

        if (next > *p) {
            tnode_t node;
            memset(&node, 0, sizeof(node));
            node.type = NODE_TEXT;
            node.text = dup_str(*p, next - *p);
            add_node(nodes, count, cap, node);
        }

        *p = next;
        const char *save = *p;
        const char *close = strstr(*p + 2, "}}");
        if (!close) { *p = end; return 0; }

        const char *inner = *p + 2;
        while (*inner == ' ') inner++;

        if (stop_on_end && strncmp(inner, "end", 3) == 0) {
            *p = close + 2;
            return 1;
        }
        if (stop_on_else && strncmp(inner, "else", 4) == 0) {
            return 2;
        }

        int r = parse_action(p, end, nodes, count, cap);
        if (r == 1 && stop_on_end) return 1;
        if (r == 2 && stop_on_else) return 2;
    }
    return 0;
}

neverc_template_t *neverc_template_parse(const char *text, const char **errp) {
    neverc_template_t *tmpl = (neverc_template_t *)calloc(1, sizeof(*tmpl));
    const char *p = text;
    const char *end = text + strlen(text);
    parse_nodes(&p, end, &tmpl->nodes, &tmpl->nnodes, &tmpl->cap, 0, 0);
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
    d->vars = NULL;
    d->nvars = 0;
    d->cap = 0;
}

void neverc_template_data_set(neverc_template_data_t *d,
                               const char *key, const char *value) {
    for (int i = 0; i < d->nvars; i++) {
        if (strcmp(d->vars[i].key, key) == 0) {
            d->vars[i].value = value;
            return;
        }
    }
    if (d->nvars >= d->cap) {
        d->cap = d->cap == 0 ? 8 : d->cap * 2;
        d->vars = (neverc_template_var_t *)realloc(d->vars,
            d->cap * sizeof(neverc_template_var_t));
    }
    d->vars[d->nvars].key = key;
    d->vars[d->nvars].value = value;
    d->nvars++;
}

const char *neverc_template_data_get(const neverc_template_data_t *d,
                                     const char *key) {
    for (int i = 0; i < d->nvars; i++)
        if (strcmp(d->vars[i].key, key) == 0)
            return d->vars[i].value;
    return NULL;
}

void neverc_template_data_free(neverc_template_data_t *d) {
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

static void out_init(outbuf_t *b) { b->cap = 256; b->data = (char *)malloc(b->cap); b->len = 0; }
static void out_puts(outbuf_t *b, const char *s, size_t n) {
    while (b->len + n >= b->cap) { b->cap *= 2; b->data = (char *)realloc(b->data, b->cap); }
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void exec_nodes(outbuf_t *out, tnode_t *nodes, int count,
                        const neverc_template_data_t *data) {
    for (int i = 0; i < count; i++) {
        tnode_t *n = &nodes[i];
        switch (n->type) {
        case NODE_TEXT:
            out_puts(out, n->text, strlen(n->text));
            break;
        case NODE_VAR: {
            const char *val = neverc_template_data_get(data, n->key);
            if (val) out_puts(out, val, strlen(val));
            break;
        }
        case NODE_IF: {
            const char *val = neverc_template_data_get(data, n->key);
            int truthy = val && val[0] != '\0' && strcmp(val, "0") != 0 &&
                         strcmp(val, "false") != 0;
            if (truthy)
                exec_nodes(out, n->children, n->nchildren, data);
            else if (n->else_branch)
                exec_nodes(out, n->else_branch, n->nelse, data);
            break;
        }
        case NODE_RANGE:
            /* Simplified: just execute children once if key exists */
            if (neverc_template_data_get(data, n->key))
                exec_nodes(out, n->children, n->nchildren, data);
            break;
        }
    }
}

char *neverc_template_execute(neverc_template_t *tmpl,
                               const neverc_template_data_t *data,
                               size_t *outlen) {
    outbuf_t out;
    out_init(&out);
    exec_nodes(&out, tmpl->nodes, tmpl->nnodes, data);
    out.data[out.len] = '\0';
    *outlen = out.len;
    return out.data;
}

char *neverc_template_render(const char *tmpl_text,
                              const neverc_template_data_t *data,
                              size_t *outlen) {
    neverc_template_t *tmpl = neverc_template_parse(tmpl_text, NULL);
    char *result = neverc_template_execute(tmpl, data, outlen);
    neverc_template_free(tmpl);
    return result;
}
