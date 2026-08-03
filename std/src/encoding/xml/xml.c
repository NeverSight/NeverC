#include "neverc/std/encoding/xml.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void neverc_xml_decoder_init(neverc_xml_decoder_t *d, const char *data, size_t len) {
    if (!d) return;
    d->src = data;
    d->len = data || len == 0 ? len : 0;
    d->pos = 0;
}

static char *dup_range(const char *s, size_t n) {
    if ((!s && n != 0) || n == SIZE_MAX) return NULL;
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    if (n > 0) memcpy(r, s, n);
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

static void free_attrs(neverc_xml_attr_t *attrs, int count) {
    if (!attrs) return;
    for (int i = 0; i < count; i++) {
        free(attrs[i].name);
        free(attrs[i].value);
    }
    free(attrs);
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
               d->src[d->pos] != ' ' && d->src[d->pos] != '\t' &&
               d->src[d->pos] != '\n' && d->src[d->pos] != '\r' &&
               d->src[d->pos] != '>' && d->src[d->pos] != '/')
            d->pos++;
        if (d->pos == ns) goto error;
        char *name = dup_range(d->src + ns, d->pos - ns);
        if (!name) goto error;

        skip_ws(d);
        if (d->pos >= d->len || d->src[d->pos] != '=') {
            free(name);
            goto error;
        }
        d->pos++;
        skip_ws(d);
        if (d->pos >= d->len ||
            (d->src[d->pos] != '"' && d->src[d->pos] != '\'')) {
            free(name);
            goto error;
        }
        char q = d->src[d->pos++];
        size_t vs = d->pos;
        while (d->pos < d->len && d->src[d->pos] != q) d->pos++;
        if (d->pos >= d->len) {
            free(name);
            goto error;
        }
        char *value = dup_range(d->src + vs, d->pos - vs);
        d->pos++;
        if (!value) {
            free(name);
            goto error;
        }

        if (*count >= cap) {
            if (cap > INT32_MAX / 2) {
                free(name);
                free(value);
                goto error;
            }
            int next_cap = cap ? cap * 2 : 4;
            neverc_xml_attr_t *grown = (neverc_xml_attr_t *)realloc(
                attrs, (size_t)next_cap * sizeof(*attrs));
            if (!grown) {
                free(name);
                free(value);
                goto error;
            }
            attrs = grown;
            cap = next_cap;
        }
        attrs[*count].name = name;
        attrs[*count].value = value;
        (*count)++;
    }
    return attrs;

error:
    free_attrs(attrs, *count);
    *count = -1;
    return NULL;
}

int neverc_xml_decode_token(neverc_xml_decoder_t *d, neverc_xml_token_t *tok) {
    if (!tok) return -1;
    memset(tok, 0, sizeof(*tok));
    if (!d || (!d->src && d->len != 0) || d->pos > d->len) {
        tok->type = NEVERC_XML_ERROR;
        return -1;
    }

    if (d->pos >= d->len) { tok->type = NEVERC_XML_EOF; return 0; }

    if (d->src[d->pos] != '<') {
        size_t start = d->pos;
        /* Character data runs until the next '<'; memchr scans it in bulk
         * instead of byte-by-byte (text content dominates most documents). */
        const char *lt = (const char *)memchr(d->src + d->pos, '<', d->len - d->pos);
        d->pos = lt ? (size_t)(lt - d->src) : d->len;
        tok->type = NEVERC_XML_CHAR_DATA;
        tok->data = dup_range(d->src + start, d->pos - start);
        if (!tok->data) goto error;
        tok->data_len = d->pos - start;
        return 1;
    }

    d->pos++;
    if (d->pos >= d->len) return -1;

    if (d->src[d->pos] == '/') {
        d->pos++;
        size_t ns = d->pos;
        while (d->pos < d->len && d->src[d->pos] != '>') d->pos++;
        size_t ne = d->pos;
        while (ne > ns && (d->src[ne - 1] == ' ' || d->src[ne - 1] == '\t' ||
                           d->src[ne - 1] == '\n' || d->src[ne - 1] == '\r'))
            ne--;
        tok->type = NEVERC_XML_END_ELEMENT;
        if (ne == ns) goto error;
        tok->name = dup_range(d->src + ns, ne - ns);
        if (!tok->name || d->pos >= d->len) goto error;
        d->pos++;
        return 1;
    }

    if (d->src[d->pos] == '!' && d->pos + 2 < d->len &&
        d->src[d->pos + 1] == '-' && d->src[d->pos + 2] == '-') {
        d->pos += 3;
        size_t cs = d->pos;
        while (d->pos + 2 < d->len &&
               !(d->src[d->pos] == '-' && d->src[d->pos+1] == '-' && d->src[d->pos+2] == '>'))
            d->pos++;
        if (d->pos + 2 >= d->len) goto error;
        tok->type = NEVERC_XML_COMMENT;
        tok->data = dup_range(d->src + cs, d->pos - cs);
        if (!tok->data) goto error;
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
        if (d->pos + 1 >= d->len) goto error;
        tok->type = NEVERC_XML_PROC_INST;
        tok->data = dup_range(d->src + ps, d->pos - ps);
        if (!tok->data) goto error;
        tok->data_len = d->pos - ps;
        d->pos += 2;
        return 1;
    }

    size_t ns = d->pos;
    while (d->pos < d->len && d->src[d->pos] != ' ' &&
           d->src[d->pos] != '\t' && d->src[d->pos] != '\n' &&
           d->src[d->pos] != '\r' && d->src[d->pos] != '>' &&
           d->src[d->pos] != '/')
        d->pos++;
    if (d->pos == ns) goto error;
    tok->type = NEVERC_XML_START_ELEMENT;
    tok->name = dup_range(d->src + ns, d->pos - ns);
    if (!tok->name) goto error;
    tok->attrs = parse_attrs(d, &tok->nattrs);
    if (tok->nattrs < 0) goto error;

    if (d->pos < d->len && d->src[d->pos] == '/') {
        d->pos++;
        tok->self_closing = 1;
    }
    if (d->pos >= d->len || d->src[d->pos] != '>') goto error;
    d->pos++;

    return 1;

error:
    neverc_xml_token_free(tok);
    memset(tok, 0, sizeof(*tok));
    tok->type = NEVERC_XML_ERROR;
    return -1;
}

void neverc_xml_token_free(neverc_xml_token_t *tok) {
    if (!tok) return;
    free(tok->name);
    free(tok->data);
    if (tok->attrs) {
        for (int i = 0; i < tok->nattrs; i++) {
            free(tok->attrs[i].name);
            free(tok->attrs[i].value);
        }
        free(tok->attrs);
    }
    memset(tok, 0, sizeof(*tok));
}

/* DOM parser */
static neverc_xml_node_t *new_node(const char *tag) {
    neverc_xml_node_t *n = (neverc_xml_node_t *)calloc(1, sizeof(neverc_xml_node_t));
    if (!n) return NULL;
    n->tag = tag ? dup_range(tag, strlen(tag)) : NULL;
    if (tag && !n->tag) {
        free(n);
        return NULL;
    }
    n->cap_children = 4;
    n->children = (neverc_xml_node_t **)malloc(n->cap_children * sizeof(void *));
    if (!n->children) {
        free(n->tag);
        free(n);
        return NULL;
    }
    return n;
}

static int add_child(neverc_xml_node_t *parent, neverc_xml_node_t *child) {
    if (!parent || !child || parent->nchildren < 0 ||
        parent->cap_children < 0 || parent->nchildren == INT32_MAX) return 0;
    if (parent->nchildren >= parent->cap_children) {
        if (parent->cap_children > INT32_MAX / 2) return 0;
        int next_cap = parent->cap_children < 4 ? 4
                                                : parent->cap_children * 2;
        neverc_xml_node_t **grown = (neverc_xml_node_t **)realloc(
            parent->children, (size_t)next_cap * sizeof(*grown));
        if (!grown) return 0;
        parent->children = grown;
        parent->cap_children = next_cap;
    }
    parent->children[parent->nchildren++] = child;
    return 1;
}

static int grow_parse_stacks(neverc_xml_node_t ***stack, size_t **tlen,
                             size_t **tcap, int *cap, int used) {
    if (*cap > INT32_MAX / 2 || used < 0) return 0;
    int next_cap = *cap * 2;
    neverc_xml_node_t **new_stack = (neverc_xml_node_t **)malloc(
        (size_t)next_cap * sizeof(*new_stack));
    size_t *new_tlen = (size_t *)malloc((size_t)next_cap * sizeof(*new_tlen));
    size_t *new_tcap = (size_t *)malloc((size_t)next_cap * sizeof(*new_tcap));
    if (!new_stack || !new_tlen || !new_tcap) {
        free(new_stack);
        free(new_tlen);
        free(new_tcap);
        return 0;
    }
    memcpy(new_stack, *stack, (size_t)used * sizeof(*new_stack));
    memcpy(new_tlen, *tlen, (size_t)used * sizeof(*new_tlen));
    memcpy(new_tcap, *tcap, (size_t)used * sizeof(*new_tcap));
    free(*stack);
    free(*tlen);
    free(*tcap);
    *stack = new_stack;
    *tlen = new_tlen;
    *tcap = new_tcap;
    *cap = next_cap;
    return 1;
}

/* Cap nesting depth. The tokenizer/builder loop is iterative (heap stack), but
 * neverc_xml_node_free recurses per child, so an unbounded-depth tree would
 * overflow the C stack when freed. 1000 is well beyond real documents and safe
 * on small (≈512 KiB) thread stacks. */
#define NCI_XML_MAX_DEPTH 1000

neverc_xml_node_t *neverc_xml_parse(const char *data, size_t len) {
    if (!data && len != 0) return NULL;
    neverc_xml_decoder_t d;
    neverc_xml_decoder_init(&d, data, len);

    neverc_xml_node_t *root = new_node("__root__");
    if (!root) return NULL;

    int stack_cap = 32;
    neverc_xml_node_t **stack = (neverc_xml_node_t **)malloc(stack_cap * sizeof(void *));
    /* Parallel per-depth text accumulators. Tracking each open element's text
     * length and buffer capacity lets repeated character-data runs (mixed
     * content like <p>a<b/>c<b/>d...</p>, where each child splits the text) grow
     * the buffer geometrically instead of strlen + realloc-to-exact on every
     * run, which was O(n^2) in the number of runs per element. */
    size_t *tlen = (size_t *)malloc(stack_cap * sizeof(size_t));
    size_t *tcap = (size_t *)malloc(stack_cap * sizeof(size_t));
    if (!stack || !tlen || !tcap) goto parse_fail;
    int stack_top = 0;
    stack[stack_top] = root;
    tlen[stack_top] = 0;
    tcap[stack_top] = 0;
    stack_top++;

    neverc_xml_token_t tok;
    int decode_result;
    while ((decode_result = neverc_xml_decode_token(&d, &tok)) > 0) {
        int failed = 0;
        switch (tok.type) {
        case NEVERC_XML_START_ELEMENT: {
            if (!tok.name) {
                failed = 1;
                break;
            }
            if (!tok.self_closing && stack_top >= NCI_XML_MAX_DEPTH) {
                failed = 1;
                break;
            }
            if (!tok.self_closing && stack_top >= stack_cap &&
                !grow_parse_stacks(&stack, &tlen, &tcap, &stack_cap,
                                   stack_top)) {
                failed = 1;
                break;
            }
            neverc_xml_node_t *child = new_node(tok.name);
            if (!child) {
                failed = 1;
                break;
            }
            child->attrs = tok.attrs;
            child->nattrs = tok.nattrs;
            tok.attrs = NULL; tok.nattrs = 0;
            if (!add_child(stack[stack_top - 1], child)) {
                neverc_xml_node_free(child);
                failed = 1;
                break;
            }
            if (!tok.self_closing) {
                stack[stack_top] = child;
                tlen[stack_top] = 0;
                tcap[stack_top] = 0;
                stack_top++;
            }
            break;
        }
        case NEVERC_XML_END_ELEMENT:
            if (stack_top <= 1 || !tok.name ||
                !stack[stack_top - 1]->tag ||
                strcmp(stack[stack_top - 1]->tag, tok.name) != 0) {
                failed = 1;
            } else {
                stack_top--;
            }
            break;
        case NEVERC_XML_CHAR_DATA: {
            int si = stack_top - 1;
            neverc_xml_node_t *cur = stack[si];
            size_t nlen = tok.data_len;
            if (!cur->text) {
                /* First run for this element: steal the token's buffer
                 * (dup_range already allocated exactly data_len + 1). */
                cur->text = tok.data;
                tok.data = NULL;
                tlen[si] = nlen;
                tcap[si] = nlen + 1;
            } else if (nlen > 0) {
                if (tlen[si] == SIZE_MAX ||
                    nlen > SIZE_MAX - tlen[si] - 1) {
                    failed = 1;
                    break;
                }
                size_t need = tlen[si] + nlen + 1;
                if (need > tcap[si]) {
                    size_t nc = tcap[si] * 2;
                    if (nc < tcap[si] || nc < need) nc = need;
                    char *grown = (char *)realloc(cur->text, nc);
                    if (!grown) {
                        failed = 1;
                        break;
                    }
                    cur->text = grown;
                    tcap[si] = nc;
                }
                memcpy(cur->text + tlen[si], tok.data, nlen);
                tlen[si] += nlen;
                cur->text[tlen[si]] = '\0';
            }
            break;
        }
        default:
            break;
        }
        neverc_xml_token_free(&tok);
        if (failed) goto parse_fail;
    }

    if (decode_result < 0 || stack_top != 1) goto parse_fail;

    free(stack);
    free(tlen);
    free(tcap);
    return root;

parse_fail:
    free(stack);
    free(tlen);
    free(tcap);
    neverc_xml_node_free(root);
    return NULL;
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
    if (!node || !name || node->nattrs < 0) return NULL;
    for (int i = 0; i < node->nattrs; i++)
        if (strcmp(node->attrs[i].name, name) == 0)
            return node->attrs[i].value;
    return NULL;
}

neverc_xml_node_t *neverc_xml_node_child(const neverc_xml_node_t *node,
                                          const char *tag) {
    if (!node || !tag || node->nchildren < 0) return NULL;
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
    if (!outlen) return NULL;
    *outlen = 0;
    if (!s) return NULL;
    size_t slen = strlen(s);

    /* Branchless pass to size the output exactly (no realloc, no slack). */
    size_t extra = 0;
    for (size_t i = 0; i < slen; i++) {
        if (xml_esc_extra[(unsigned char)s[i]] > SIZE_MAX - extra) return NULL;
        extra += xml_esc_extra[(unsigned char)s[i]];
    }

    if (extra > SIZE_MAX - slen - 1) return NULL;
    char *r = (char *)malloc(slen + extra + 1);
    if (!r) return NULL;

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
