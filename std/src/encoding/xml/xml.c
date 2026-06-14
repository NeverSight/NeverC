#include "neverc/std/encoding/xml.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
    /* Allocate lazily: attribute-less tags (the common case) return NULL with
     * count 0 and pay no malloc/free. All consumers already guard on a NULL
     * attrs pointer (token_free, node_free, node_attr, the DOM builder). */
    int cap = 0;
    neverc_xml_attr_t *attrs = NULL;
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
            cap = cap ? cap * 2 : 4;
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
        /* Character data runs until the next '<'; memchr scans it in bulk
         * instead of byte-by-byte (text content dominates most documents). */
        const char *lt = (const char *)memchr(d->src + d->pos, '<', d->len - d->pos);
        d->pos = lt ? (size_t)(lt - d->src) : d->len;
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

/*
 * Per-byte expansion table: xml_esc_extra[c] is the extra bytes c's escape adds
 * beyond the original byte (0 for self-representing bytes), doubling as the
 * "is special" predicate.  & -> &amp; (4),  < > -> 4-char (3),  " ' -> 6-char (5).
 */
static const uint8_t xml_esc_extra[256] = {
    ['&'] = 4, ['<'] = 3, ['>'] = 3, ['"'] = 5, ['\''] = 5,
};

char *neverc_xml_escape(const char *s, size_t *outlen) {
    size_t slen = strlen(s);

    /* Branchless pass to size the output exactly (no realloc, no slack). */
    size_t extra = 0;
    for (size_t i = 0; i < slen; i++)
        extra += xml_esc_extra[(unsigned char)s[i]];

    char *r = (char *)malloc(slen + extra + 1);
    if (!r) { *outlen = 0; return NULL; }

    /* Fast path: nothing needs escaping, copy the whole string in one go. */
    if (extra == 0) {
        memcpy(r, s, slen);
        r[slen] = '\0';
        *outlen = slen;
        return r;
    }

    /* Single read: store self-representing bytes directly (buffer is exact, no
     * bounds check); specials expand via constant-size memcpy the compiler
     * inlines. */
    size_t wi = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char c = (unsigned char)s[i];
        if (xml_esc_extra[c] == 0) { r[wi++] = (char)c; continue; }
        switch (c) {
            case '&':  memcpy(r + wi, "&amp;",  5); wi += 5; break;
            case '<':  memcpy(r + wi, "&lt;",   4); wi += 4; break;
            case '>':  memcpy(r + wi, "&gt;",   4); wi += 4; break;
            case '"':  memcpy(r + wi, "&quot;", 6); wi += 6; break;
            case '\'': memcpy(r + wi, "&apos;", 6); wi += 6; break;
        }
    }
    r[wi] = '\0';
    *outlen = wi;
    return r;
}
