#include "neverc/std/encoding/xml.h"
#include <stdlib.h>
#include <string.h>

void neverc_xml_decoder_init(neverc_xml_decoder_t *d, const char *data, size_t len) {
    d->src = data;
    d->len = len;
    d->pos = 0;
}

static char *dup_range(const char *s, size_t n) {
    char *r = (char *)malloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static void skip_ws(neverc_xml_decoder_t *d) {
    while (d->pos < d->len) {
        char c = d->src[d->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') d->pos++;
        else break;
    }
}

static neverc_xml_attr_t *parse_attrs(neverc_xml_decoder_t *d, int *count) {
    int cap = 4;
    neverc_xml_attr_t *attrs = (neverc_xml_attr_t *)malloc(cap * sizeof(neverc_xml_attr_t));
    *count = 0;

    while (d->pos < d->len) {
        skip_ws(d);
        if (d->pos >= d->len || d->src[d->pos] == '>' ||
            d->src[d->pos] == '/' || d->src[d->pos] == '?')
            break;

        size_t ns = d->pos;
        while (d->pos < d->len && d->src[d->pos] != '=' &&
               d->src[d->pos] != ' ' && d->src[d->pos] != '>')
            d->pos++;
        char *name = dup_range(d->src + ns, d->pos - ns);

        char *value = NULL;
        if (d->pos < d->len && d->src[d->pos] == '=') {
            d->pos++;
            if (d->pos < d->len && (d->src[d->pos] == '"' || d->src[d->pos] == '\'')) {
                char q = d->src[d->pos++];
                size_t vs = d->pos;
                while (d->pos < d->len && d->src[d->pos] != q) d->pos++;
                value = dup_range(d->src + vs, d->pos - vs);
                if (d->pos < d->len) d->pos++;
            }
        }

        if (*count >= cap) {
            cap *= 2;
            attrs = (neverc_xml_attr_t *)realloc(attrs, cap * sizeof(neverc_xml_attr_t));
        }
        attrs[*count].name = name;
        attrs[*count].value = value ? value : dup_range("", 0);
        (*count)++;
    }
    return attrs;
}

int neverc_xml_decode_token(neverc_xml_decoder_t *d, neverc_xml_token_t *tok) {
    memset(tok, 0, sizeof(*tok));

    if (d->pos >= d->len) { tok->type = NEVERC_XML_EOF; return 0; }

    if (d->src[d->pos] != '<') {
        size_t start = d->pos;
        while (d->pos < d->len && d->src[d->pos] != '<') d->pos++;
        tok->type = NEVERC_XML_CHAR_DATA;
        tok->data = dup_range(d->src + start, d->pos - start);
        tok->data_len = d->pos - start;
        return 1;
    }

    d->pos++;
    if (d->pos >= d->len) return -1;

    if (d->src[d->pos] == '/') {
        d->pos++;
        size_t ns = d->pos;
        while (d->pos < d->len && d->src[d->pos] != '>') d->pos++;
        tok->type = NEVERC_XML_END_ELEMENT;
        tok->name = dup_range(d->src + ns, d->pos - ns);
        if (d->pos < d->len) d->pos++;
        return 1;
    }

    if (d->src[d->pos] == '!' && d->pos + 1 < d->len && d->src[d->pos+1] == '-') {
        d->pos += 3;
        size_t cs = d->pos;
        while (d->pos + 2 < d->len &&
               !(d->src[d->pos] == '-' && d->src[d->pos+1] == '-' && d->src[d->pos+2] == '>'))
            d->pos++;
        tok->type = NEVERC_XML_COMMENT;
        tok->data = dup_range(d->src + cs, d->pos - cs);
        tok->data_len = d->pos - cs;
        d->pos += 3;
        return 1;
    }

    if (d->src[d->pos] == '?') {
        d->pos++;
        size_t ps = d->pos;
        while (d->pos + 1 < d->len &&
               !(d->src[d->pos] == '?' && d->src[d->pos+1] == '>'))
            d->pos++;
        tok->type = NEVERC_XML_PROC_INST;
        tok->data = dup_range(d->src + ps, d->pos - ps);
        tok->data_len = d->pos - ps;
        d->pos += 2;
        return 1;
    }

    size_t ns = d->pos;
    while (d->pos < d->len && d->src[d->pos] != ' ' &&
           d->src[d->pos] != '>' && d->src[d->pos] != '/')
        d->pos++;
    tok->type = NEVERC_XML_START_ELEMENT;
    tok->name = dup_range(d->src + ns, d->pos - ns);
    tok->attrs = parse_attrs(d, &tok->nattrs);

    if (d->pos < d->len && d->src[d->pos] == '/') {
        d->pos++;
        tok->type = NEVERC_XML_START_ELEMENT;
    }
    if (d->pos < d->len && d->src[d->pos] == '>') d->pos++;

    return 1;
}

void neverc_xml_token_free(neverc_xml_token_t *tok) {
    free(tok->name);
    free(tok->data);
    if (tok->attrs) {
        for (int i = 0; i < tok->nattrs; i++) {
            free(tok->attrs[i].name);
            free(tok->attrs[i].value);
        }
        free(tok->attrs);
    }
}

/* DOM parser */
static neverc_xml_node_t *new_node(const char *tag) {
    neverc_xml_node_t *n = (neverc_xml_node_t *)calloc(1, sizeof(neverc_xml_node_t));
    n->tag = tag ? dup_range(tag, strlen(tag)) : NULL;
    n->cap_children = 4;
    n->children = (neverc_xml_node_t **)malloc(n->cap_children * sizeof(void *));
    return n;
}

static void add_child(neverc_xml_node_t *parent, neverc_xml_node_t *child) {
    if (parent->nchildren >= parent->cap_children) {
        parent->cap_children *= 2;
        parent->children = (neverc_xml_node_t **)realloc(
            parent->children, parent->cap_children * sizeof(void *));
    }
    parent->children[parent->nchildren++] = child;
}

neverc_xml_node_t *neverc_xml_parse(const char *data, size_t len) {
    neverc_xml_decoder_t d;
    neverc_xml_decoder_init(&d, data, len);

    neverc_xml_node_t *root = new_node("__root__");

    int stack_cap = 32;
    neverc_xml_node_t **stack = (neverc_xml_node_t **)malloc(stack_cap * sizeof(void *));
    int stack_top = 0;
    stack[stack_top++] = root;

    neverc_xml_token_t tok;
    while (neverc_xml_decode_token(&d, &tok) > 0) {
        switch (tok.type) {
        case NEVERC_XML_START_ELEMENT: {
            neverc_xml_node_t *child = new_node(tok.name);
            child->attrs = tok.attrs;
            child->nattrs = tok.nattrs;
            tok.attrs = NULL; tok.nattrs = 0;
            add_child(stack[stack_top - 1], child);
            if (stack_top >= stack_cap) {
                stack_cap *= 2;
                stack = (neverc_xml_node_t **)realloc(stack, stack_cap * sizeof(void *));
            }
            stack[stack_top++] = child;
            break;
        }
        case NEVERC_XML_END_ELEMENT:
            if (stack_top > 1) stack_top--;
            break;
        case NEVERC_XML_CHAR_DATA: {
            neverc_xml_node_t *cur = stack[stack_top - 1];
            if (!cur->text) {
                cur->text = tok.data;
                tok.data = NULL;
            } else {
                size_t olen = strlen(cur->text);
                size_t nlen = tok.data_len;
                cur->text = (char *)realloc(cur->text, olen + nlen + 1);
                memcpy(cur->text + olen, tok.data, nlen);
                cur->text[olen + nlen] = '\0';
            }
            break;
        }
        default:
            break;
        }
        neverc_xml_token_free(&tok);
    }

    free(stack);
    return root;
}

void neverc_xml_node_free(neverc_xml_node_t *node) {
    if (!node) return;
    free(node->tag);
    free(node->text);
    if (node->attrs) {
        for (int i = 0; i < node->nattrs; i++) {
            free(node->attrs[i].name);
            free(node->attrs[i].value);
        }
        free(node->attrs);
    }
    for (int i = 0; i < node->nchildren; i++)
        neverc_xml_node_free(node->children[i]);
    free(node->children);
    free(node);
}

const char *neverc_xml_node_attr(const neverc_xml_node_t *node, const char *name) {
    for (int i = 0; i < node->nattrs; i++)
        if (strcmp(node->attrs[i].name, name) == 0)
            return node->attrs[i].value;
    return NULL;
}

neverc_xml_node_t *neverc_xml_node_child(const neverc_xml_node_t *node,
                                          const char *tag) {
    for (int i = 0; i < node->nchildren; i++)
        if (node->children[i]->tag && strcmp(node->children[i]->tag, tag) == 0)
            return node->children[i];
    return NULL;
}

char *neverc_xml_escape(const char *s, size_t *outlen) {
    size_t slen = strlen(s);
    size_t cap = slen * 2;
    char *r = (char *)malloc(cap + 1);
    size_t wi = 0;
    for (size_t i = 0; i < slen; i++) {
        const char *esc = NULL; size_t elen = 0;
        switch (s[i]) {
            case '&': esc = "&amp;"; elen = 5; break;
            case '<': esc = "&lt;";  elen = 4; break;
            case '>': esc = "&gt;";  elen = 4; break;
            case '"': esc = "&quot;"; elen = 6; break;
            case '\'': esc = "&apos;"; elen = 6; break;
            default: break;
        }
        if (esc) {
            if (wi + elen >= cap) { cap = (wi + elen) * 2; r = (char *)realloc(r, cap + 1); }
            memcpy(r + wi, esc, elen); wi += elen;
        } else {
            if (wi + 1 >= cap) { cap *= 2; r = (char *)realloc(r, cap + 1); }
            r[wi++] = s[i];
        }
    }
    r[wi] = '\0';
    *outlen = wi;
    return r;
}
