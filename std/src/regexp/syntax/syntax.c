#include "neverc/std/regexp_syntax.h"
#include <stdlib.h>

/* ======================================================================
 * Internal helpers
 * ====================================================================== */

static size_t nc_slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void nc_mcpy(void *dst, const void *src, size_t n) {
    const char *s = (const char *)src;
    char *d = (char *)dst;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static neverc_regexp_syntax_node_t *mk_node(neverc_regexp_op_t op) {
    neverc_regexp_syntax_node_t *n =
        (neverc_regexp_syntax_node_t *)calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->op = op;
    n->max = -1;
    return n;
}

static void add_sub(neverc_regexp_syntax_node_t *parent,
                    neverc_regexp_syntax_node_t *child) {
    parent->nsubs++;
    parent->subs = (neverc_regexp_syntax_node_t **)realloc(
        parent->subs, (size_t)parent->nsubs * sizeof(neverc_regexp_syntax_node_t *));
    parent->subs[parent->nsubs - 1] = child;
}

static void add_rune(neverc_regexp_syntax_node_t *n, int r) {
    n->nrunes++;
    n->runes = (int *)realloc(n->runes, (size_t)n->nrunes * sizeof(int));
    n->runes[n->nrunes - 1] = r;
}

/* ======================================================================
 * Parser
 * ====================================================================== */

typedef struct {
    const char *src;
    int         pos;
    int         len;
    int         flags;
    int         ncap;
    const char *err;
} parser_t;

static neverc_regexp_syntax_node_t *parse_alternation(parser_t *p);
static neverc_regexp_syntax_node_t *parse_concat(parser_t *p);
static neverc_regexp_syntax_node_t *parse_repeat(parser_t *p);
static neverc_regexp_syntax_node_t *parse_atom(parser_t *p);

static int peek(parser_t *p) {
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->src[p->pos];
}

static int next(parser_t *p) {
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->src[p->pos++];
}

static int parse_int(parser_t *p) {
    int val = 0;
    while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') {
        val = val * 10 + (p->src[p->pos] - '0');
        p->pos++;
    }
    return val;
}

static neverc_regexp_syntax_node_t *parse_escape(parser_t *p) {
    int c = next(p);
    if (c < 0) { p->err = "trailing backslash"; return NULL; }

    switch (c) {
    case 'd': case 'D': case 'w': case 'W': case 's': case 'S': {
        neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_CHAR_CLASS);
        int negate = (c == 'D' || c == 'W' || c == 'S');
        switch (c | 0x20) {
        case 'd': add_rune(n, '0'); add_rune(n, '9'); break;
        case 'w':
            add_rune(n, '0'); add_rune(n, '9');
            add_rune(n, 'A'); add_rune(n, 'Z');
            add_rune(n, 'a'); add_rune(n, 'z');
            add_rune(n, '_'); add_rune(n, '_');
            break;
        case 's':
            add_rune(n, '\t'); add_rune(n, '\t');
            add_rune(n, '\n'); add_rune(n, '\n');
            add_rune(n, '\f'); add_rune(n, '\f');
            add_rune(n, '\r'); add_rune(n, '\r');
            add_rune(n, ' ');  add_rune(n, ' ');
            break;
        }
        if (negate) n->flags |= NC_RE_FLAG_FOLD_CASE; /* reuse flag to mark negation */
        return n;
    }
    case 'b': return mk_node(NC_RE_OP_WORD_BOUNDARY);
    case 'B': return mk_node(NC_RE_OP_NO_WORD_BOUNDARY);
    case 'A': return mk_node(NC_RE_OP_BEGIN_TEXT);
    case 'z': return mk_node(NC_RE_OP_END_TEXT);
    case 'a': { neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_LITERAL); add_rune(n, '\a'); return n; }
    case 'f': { neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_LITERAL); add_rune(n, '\f'); return n; }
    case 't': { neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_LITERAL); add_rune(n, '\t'); return n; }
    case 'n': { neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_LITERAL); add_rune(n, '\n'); return n; }
    case 'r': { neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_LITERAL); add_rune(n, '\r'); return n; }
    case 'v': { neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_LITERAL); add_rune(n, '\v'); return n; }
    default: {
        neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_LITERAL);
        add_rune(n, c);
        return n;
    }
    }
}

static neverc_regexp_syntax_node_t *parse_char_class(parser_t *p) {
    neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_CHAR_CLASS);
    int negate = 0;
    if (peek(p) == '^') { next(p); negate = 1; }
    if (negate) n->flags |= NC_RE_FLAG_FOLD_CASE;

    int first = 1;
    while (p->pos < p->len) {
        int c = peek(p);
        if (c == ']' && !first) { next(p); return n; }
        first = 0;

        if (c == '\\') {
            next(p);
            int esc = next(p);
            if (esc < 0) { p->err = "bad escape in char class"; neverc_regexp_syntax_free(n); return NULL; }
            switch (esc) {
            case 'd': add_rune(n, '0'); add_rune(n, '9'); continue;
            case 'w':
                add_rune(n, '0'); add_rune(n, '9');
                add_rune(n, 'A'); add_rune(n, 'Z');
                add_rune(n, 'a'); add_rune(n, 'z');
                add_rune(n, '_'); add_rune(n, '_');
                continue;
            case 's':
                add_rune(n, '\t'); add_rune(n, '\t');
                add_rune(n, '\n'); add_rune(n, '\n');
                add_rune(n, '\f'); add_rune(n, '\f');
                add_rune(n, '\r'); add_rune(n, '\r');
                add_rune(n, ' ');  add_rune(n, ' ');
                continue;
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            default:  c = esc; break;
            }
        } else {
            c = next(p);
        }

        if (p->pos + 1 < p->len && p->src[p->pos] == '-' && p->src[p->pos + 1] != ']') {
            next(p); /* consume '-' */
            int hi;
            if (peek(p) == '\\') {
                next(p);
                hi = next(p);
                if (hi == 'n') hi = '\n';
                else if (hi == 't') hi = '\t';
                else if (hi == 'r') hi = '\r';
            } else {
                hi = next(p);
            }
            add_rune(n, c);
            add_rune(n, hi);
        } else {
            add_rune(n, c);
            add_rune(n, c);
        }
    }
    p->err = "unclosed character class";
    neverc_regexp_syntax_free(n);
    return NULL;
}

static neverc_regexp_syntax_node_t *parse_group(parser_t *p) {
    if (p->pos + 1 < p->len && p->src[p->pos] == '?' && p->src[p->pos + 1] == ':') {
        p->pos += 2;
        neverc_regexp_syntax_node_t *inner = parse_alternation(p);
        if (!inner) return NULL;
        if (next(p) != ')') { p->err = "unclosed group"; neverc_regexp_syntax_free(inner); return NULL; }
        return inner;
    }

    if (p->pos + 2 < p->len && p->src[p->pos] == '?' &&
        (p->src[p->pos + 1] == 'P' || p->src[p->pos + 1] == '<' || p->src[p->pos + 1] == '\'')) {
        /* Named capture: (?P<name>...) or (?<name>...) */
        int skip = (p->src[p->pos + 1] == 'P') ? 3 : 2;
        p->pos += skip;
        int name_start = p->pos;
        char close_ch = (p->src[p->pos - 1] == '\'') ? '\'' : '>';
        while (p->pos < p->len && p->src[p->pos] != close_ch) p->pos++;
        int name_len = p->pos - name_start;
        p->pos++; /* skip > or ' */

        neverc_regexp_syntax_node_t *cap = mk_node(NC_RE_OP_CAPTURE);
        cap->cap = ++p->ncap;
        cap->name = (char *)malloc((size_t)name_len + 1);
        nc_mcpy(cap->name, p->src + name_start, (size_t)name_len);
        cap->name[name_len] = '\0';

        neverc_regexp_syntax_node_t *inner = parse_alternation(p);
        if (!inner) { neverc_regexp_syntax_free(cap); return NULL; }
        add_sub(cap, inner);
        if (next(p) != ')') { p->err = "unclosed group"; neverc_regexp_syntax_free(cap); return NULL; }
        return cap;
    }

    /* Regular capturing group */
    neverc_regexp_syntax_node_t *cap = mk_node(NC_RE_OP_CAPTURE);
    cap->cap = ++p->ncap;
    neverc_regexp_syntax_node_t *inner = parse_alternation(p);
    if (!inner) { neverc_regexp_syntax_free(cap); return NULL; }
    add_sub(cap, inner);
    if (next(p) != ')') { p->err = "unclosed group"; neverc_regexp_syntax_free(cap); return NULL; }
    return cap;
}

static neverc_regexp_syntax_node_t *parse_atom(parser_t *p) {
    int c = peek(p);
    if (c < 0) return NULL;

    switch (c) {
    case '(': next(p); return parse_group(p);
    case '[': next(p); return parse_char_class(p);
    case '.':
        next(p);
        return mk_node((p->flags & NC_RE_FLAG_DOT_NL) ?
                        NC_RE_OP_ANY_CHAR : NC_RE_OP_ANY_CHAR_NOT_NL);
    case '^':
        next(p);
        return mk_node((p->flags & NC_RE_FLAG_MULTI_LINE) ?
                        NC_RE_OP_BEGIN_LINE : NC_RE_OP_BEGIN_TEXT);
    case '$':
        next(p);
        {
            neverc_regexp_syntax_node_t *n = mk_node(
                (p->flags & NC_RE_FLAG_MULTI_LINE) ? NC_RE_OP_END_LINE : NC_RE_OP_END_TEXT);
            n->flags |= NC_RE_FLAG_WAS_DOLLAR;
            return n;
        }
    case '\\':
        next(p);
        return parse_escape(p);
    case ')': case '|':
        return NULL;
    case '*': case '+': case '?': case '{':
        p->err = "unexpected repetition operator";
        return NULL;
    default:
        next(p);
        {
            neverc_regexp_syntax_node_t *n = mk_node(NC_RE_OP_LITERAL);
            add_rune(n, c);
            return n;
        }
    }
}

static neverc_regexp_syntax_node_t *parse_repeat(parser_t *p) {
    neverc_regexp_syntax_node_t *atom = parse_atom(p);
    if (!atom) return NULL;

    int c = peek(p);
    neverc_regexp_syntax_node_t *rep = NULL;

    switch (c) {
    case '*': next(p); rep = mk_node(NC_RE_OP_STAR); break;
    case '+': next(p); rep = mk_node(NC_RE_OP_PLUS); break;
    case '?': next(p); rep = mk_node(NC_RE_OP_QUEST); break;
    case '{': {
        next(p);
        int min_val = parse_int(p);
        int max_val = min_val;
        if (peek(p) == ',') {
            next(p);
            if (peek(p) == '}')
                max_val = -1;
            else
                max_val = parse_int(p);
        }
        if (next(p) != '}') { p->err = "bad repeat syntax"; neverc_regexp_syntax_free(atom); return NULL; }
        rep = mk_node(NC_RE_OP_REPEAT);
        rep->min = min_val;
        rep->max = max_val;
        break;
    }
    default:
        return atom;
    }

    if (peek(p) == '?') {
        next(p);
        rep->flags |= NC_RE_FLAG_NON_GREEDY;
    }

    add_sub(rep, atom);
    return rep;
}

static neverc_regexp_syntax_node_t *parse_concat(parser_t *p) {
    neverc_regexp_syntax_node_t *cat = mk_node(NC_RE_OP_CONCAT);

    while (p->pos < p->len && peek(p) != '|' && peek(p) != ')') {
        neverc_regexp_syntax_node_t *sub = parse_repeat(p);
        if (!sub) {
            if (p->err) { neverc_regexp_syntax_free(cat); return NULL; }
            break;
        }
        add_sub(cat, sub);
    }

    if (cat->nsubs == 0) {
        neverc_regexp_syntax_free(cat);
        return mk_node(NC_RE_OP_EMPTY_MATCH);
    }
    if (cat->nsubs == 1) {
        neverc_regexp_syntax_node_t *only = cat->subs[0];
        cat->nsubs = 0;
        free(cat->subs);
        cat->subs = NULL;
        free(cat);
        return only;
    }
    return cat;
}

static neverc_regexp_syntax_node_t *parse_alternation(parser_t *p) {
    neverc_regexp_syntax_node_t *left = parse_concat(p);
    if (!left) return NULL;
    if (peek(p) != '|') return left;

    neverc_regexp_syntax_node_t *alt = mk_node(NC_RE_OP_ALTERNATE);
    add_sub(alt, left);

    while (peek(p) == '|') {
        next(p);
        neverc_regexp_syntax_node_t *branch = parse_concat(p);
        if (!branch) { if (p->err) { neverc_regexp_syntax_free(alt); return NULL; } break; }
        add_sub(alt, branch);
    }

    if (alt->nsubs == 1) {
        neverc_regexp_syntax_node_t *only = alt->subs[0];
        alt->nsubs = 0;
        free(alt->subs);
        alt->subs = NULL;
        free(alt);
        return only;
    }
    return alt;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

neverc_regexp_syntax_node_t *neverc_regexp_syntax_parse(
    const char *pattern, int flags, const char **errp) {
    parser_t p;
    p.src = pattern;
    p.pos = 0;
    p.len = (int)nc_slen(pattern);
    p.flags = flags;
    p.ncap = 0;
    p.err = NULL;

    neverc_regexp_syntax_node_t *tree = parse_alternation(&p);
    if (p.err) {
        if (errp) *errp = p.err;
        neverc_regexp_syntax_free(tree);
        return NULL;
    }
    if (p.pos < p.len) {
        if (errp) *errp = "unexpected characters at end";
        neverc_regexp_syntax_free(tree);
        return NULL;
    }
    if (errp) *errp = NULL;
    return tree;
}

void neverc_regexp_syntax_free(neverc_regexp_syntax_node_t *node) {
    if (!node) return;
    for (int i = 0; i < node->nsubs; i++)
        neverc_regexp_syntax_free(node->subs[i]);
    free(node->subs);
    free(node->runes);
    free(node->name);
    free(node);
}

/* ======================================================================
 * String conversion
 * ====================================================================== */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} strbuf_t;

static void sb_init(strbuf_t *sb) { sb->buf = NULL; sb->len = 0; sb->cap = 0; }

static void sb_grow(strbuf_t *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return;
    size_t nc = sb->cap < 16 ? 16 : sb->cap;
    while (nc < need) nc *= 2;
    sb->buf = (char *)realloc(sb->buf, nc);
    sb->cap = nc;
}

static void sb_putc(strbuf_t *sb, char c) {
    sb_grow(sb, 1);
    sb->buf[sb->len++] = c;
}

static void sb_puts(strbuf_t *sb, const char *s) {
    while (*s) sb_putc(sb, *s++);
}

static void sb_putint(strbuf_t *sb, int v) {
    char tmp[16];
    int i = 0;
    if (v == 0) { sb_putc(sb, '0'); return; }
    while (v > 0) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) sb_putc(sb, tmp[--i]);
}

static char *sb_finish(strbuf_t *sb) {
    sb_grow(sb, 0);
    sb->buf[sb->len] = '\0';
    return sb->buf;
}

static void node_to_str(const neverc_regexp_syntax_node_t *n, strbuf_t *sb);

static int is_meta(int c) {
    return c == '\\' || c == '.' || c == '*' || c == '+' || c == '?' ||
           c == '(' || c == ')' || c == '[' || c == ']' || c == '{' ||
           c == '}' || c == '|' || c == '^' || c == '$';
}

static void node_to_str(const neverc_regexp_syntax_node_t *n, strbuf_t *sb) {
    if (!n) return;
    switch (n->op) {
    case NC_RE_OP_NO_MATCH: sb_puts(sb, "[^\\x00-\\x{10FFFF}]"); break;
    case NC_RE_OP_EMPTY_MATCH: break;
    case NC_RE_OP_LITERAL:
        for (int i = 0; i < n->nrunes; i++) {
            int r = n->runes[i];
            if (is_meta(r)) sb_putc(sb, '\\');
            sb_putc(sb, (char)r);
        }
        break;
    case NC_RE_OP_CHAR_CLASS: {
        int negate = (n->flags & NC_RE_FLAG_FOLD_CASE) != 0;
        sb_putc(sb, '[');
        if (negate) sb_putc(sb, '^');
        for (int i = 0; i + 1 < n->nrunes; i += 2) {
            int lo = n->runes[i], hi = n->runes[i + 1];
            if (lo == hi) {
                if (lo == ']' || lo == '\\' || lo == '-' || lo == '^')
                    sb_putc(sb, '\\');
                sb_putc(sb, (char)lo);
            } else {
                sb_putc(sb, (char)lo);
                sb_putc(sb, '-');
                sb_putc(sb, (char)hi);
            }
        }
        sb_putc(sb, ']');
        break;
    }
    case NC_RE_OP_ANY_CHAR_NOT_NL: sb_putc(sb, '.'); break;
    case NC_RE_OP_ANY_CHAR: sb_puts(sb, "(?s:.)"); break;
    case NC_RE_OP_BEGIN_LINE: sb_putc(sb, '^'); break;
    case NC_RE_OP_END_LINE: sb_putc(sb, '$'); break;
    case NC_RE_OP_BEGIN_TEXT: sb_puts(sb, "\\A"); break;
    case NC_RE_OP_END_TEXT:
        sb_puts(sb, (n->flags & NC_RE_FLAG_WAS_DOLLAR) ? "$" : "\\z");
        break;
    case NC_RE_OP_WORD_BOUNDARY: sb_puts(sb, "\\b"); break;
    case NC_RE_OP_NO_WORD_BOUNDARY: sb_puts(sb, "\\B"); break;
    case NC_RE_OP_CAPTURE:
        sb_putc(sb, '(');
        if (n->name && n->name[0]) {
            sb_puts(sb, "?P<");
            sb_puts(sb, n->name);
            sb_putc(sb, '>');
        }
        if (n->nsubs > 0) node_to_str(n->subs[0], sb);
        sb_putc(sb, ')');
        break;
    case NC_RE_OP_STAR:
        if (n->nsubs > 0) {
            int need_paren = (n->subs[0]->op == NC_RE_OP_CONCAT || n->subs[0]->op == NC_RE_OP_ALTERNATE);
            if (need_paren) sb_putc(sb, '(');
            node_to_str(n->subs[0], sb);
            if (need_paren) sb_putc(sb, ')');
        }
        sb_putc(sb, '*');
        if (n->flags & NC_RE_FLAG_NON_GREEDY) sb_putc(sb, '?');
        break;
    case NC_RE_OP_PLUS:
        if (n->nsubs > 0) {
            int need_paren = (n->subs[0]->op == NC_RE_OP_CONCAT || n->subs[0]->op == NC_RE_OP_ALTERNATE);
            if (need_paren) sb_putc(sb, '(');
            node_to_str(n->subs[0], sb);
            if (need_paren) sb_putc(sb, ')');
        }
        sb_putc(sb, '+');
        if (n->flags & NC_RE_FLAG_NON_GREEDY) sb_putc(sb, '?');
        break;
    case NC_RE_OP_QUEST:
        if (n->nsubs > 0) {
            int need_paren = (n->subs[0]->op == NC_RE_OP_CONCAT || n->subs[0]->op == NC_RE_OP_ALTERNATE);
            if (need_paren) sb_putc(sb, '(');
            node_to_str(n->subs[0], sb);
            if (need_paren) sb_putc(sb, ')');
        }
        sb_putc(sb, '?');
        if (n->flags & NC_RE_FLAG_NON_GREEDY) sb_putc(sb, '?');
        break;
    case NC_RE_OP_REPEAT:
        if (n->nsubs > 0) {
            int need_paren = (n->subs[0]->op == NC_RE_OP_CONCAT || n->subs[0]->op == NC_RE_OP_ALTERNATE);
            if (need_paren) sb_putc(sb, '(');
            node_to_str(n->subs[0], sb);
            if (need_paren) sb_putc(sb, ')');
        }
        sb_putc(sb, '{');
        sb_putint(sb, n->min);
        if (n->max != n->min) {
            sb_putc(sb, ',');
            if (n->max >= 0) sb_putint(sb, n->max);
        }
        sb_putc(sb, '}');
        if (n->flags & NC_RE_FLAG_NON_GREEDY) sb_putc(sb, '?');
        break;
    case NC_RE_OP_CONCAT:
        for (int i = 0; i < n->nsubs; i++)
            node_to_str(n->subs[i], sb);
        break;
    case NC_RE_OP_ALTERNATE:
        for (int i = 0; i < n->nsubs; i++) {
            if (i > 0) sb_putc(sb, '|');
            node_to_str(n->subs[i], sb);
        }
        break;
    }
}

char *neverc_regexp_syntax_string(const neverc_regexp_syntax_node_t *node) {
    if (!node) {
        char *r = (char *)malloc(1);
        r[0] = '\0';
        return r;
    }
    strbuf_t sb;
    sb_init(&sb);
    node_to_str(node, &sb);
    return sb_finish(&sb);
}

/* ======================================================================
 * Equality
 * ====================================================================== */

int neverc_regexp_syntax_equal(const neverc_regexp_syntax_node_t *a,
                                const neverc_regexp_syntax_node_t *b) {
    if (a == NULL || b == NULL) return a == b;
    if (a->op != b->op) return 0;

    switch (a->op) {
    case NC_RE_OP_LITERAL:
    case NC_RE_OP_CHAR_CLASS:
        if (a->nrunes != b->nrunes) return 0;
        for (int i = 0; i < a->nrunes; i++)
            if (a->runes[i] != b->runes[i]) return 0;
        if ((a->flags & NC_RE_FLAG_FOLD_CASE) != (b->flags & NC_RE_FLAG_FOLD_CASE)) return 0;
        return 1;
    case NC_RE_OP_END_TEXT:
        return (a->flags & NC_RE_FLAG_WAS_DOLLAR) == (b->flags & NC_RE_FLAG_WAS_DOLLAR);
    case NC_RE_OP_STAR: case NC_RE_OP_PLUS: case NC_RE_OP_QUEST:
        if ((a->flags & NC_RE_FLAG_NON_GREEDY) != (b->flags & NC_RE_FLAG_NON_GREEDY)) return 0;
        return a->nsubs == 1 && b->nsubs == 1 &&
               neverc_regexp_syntax_equal(a->subs[0], b->subs[0]);
    case NC_RE_OP_REPEAT:
        if (a->min != b->min || a->max != b->max) return 0;
        if ((a->flags & NC_RE_FLAG_NON_GREEDY) != (b->flags & NC_RE_FLAG_NON_GREEDY)) return 0;
        return a->nsubs == 1 && b->nsubs == 1 &&
               neverc_regexp_syntax_equal(a->subs[0], b->subs[0]);
    case NC_RE_OP_CAPTURE: {
        if (a->cap != b->cap) return 0;
        int aname = a->name && a->name[0];
        int bname = b->name && b->name[0];
        if (aname != bname) return 0;
        if (aname) {
            const char *pa = a->name, *pb = b->name;
            while (*pa && *pb && *pa == *pb) { pa++; pb++; }
            if (*pa || *pb) return 0;
        }
        return a->nsubs == 1 && b->nsubs == 1 &&
               neverc_regexp_syntax_equal(a->subs[0], b->subs[0]);
    }
    case NC_RE_OP_CONCAT: case NC_RE_OP_ALTERNATE:
        if (a->nsubs != b->nsubs) return 0;
        for (int i = 0; i < a->nsubs; i++)
            if (!neverc_regexp_syntax_equal(a->subs[i], b->subs[i])) return 0;
        return 1;
    default:
        return 1;
    }
}

/* ======================================================================
 * Op string
 * ====================================================================== */

const char *neverc_regexp_syntax_op_string(neverc_regexp_op_t op) {
    switch (op) {
    case NC_RE_OP_NO_MATCH:       return "NoMatch";
    case NC_RE_OP_EMPTY_MATCH:    return "EmptyMatch";
    case NC_RE_OP_LITERAL:        return "Literal";
    case NC_RE_OP_CHAR_CLASS:     return "CharClass";
    case NC_RE_OP_ANY_CHAR_NOT_NL:return "AnyCharNotNL";
    case NC_RE_OP_ANY_CHAR:       return "AnyChar";
    case NC_RE_OP_BEGIN_LINE:     return "BeginLine";
    case NC_RE_OP_END_LINE:       return "EndLine";
    case NC_RE_OP_BEGIN_TEXT:     return "BeginText";
    case NC_RE_OP_END_TEXT:       return "EndText";
    case NC_RE_OP_WORD_BOUNDARY:  return "WordBoundary";
    case NC_RE_OP_NO_WORD_BOUNDARY:return "NoWordBoundary";
    case NC_RE_OP_CAPTURE:        return "Capture";
    case NC_RE_OP_STAR:           return "Star";
    case NC_RE_OP_PLUS:           return "Plus";
    case NC_RE_OP_QUEST:          return "Quest";
    case NC_RE_OP_REPEAT:         return "Repeat";
    case NC_RE_OP_CONCAT:         return "Concat";
    case NC_RE_OP_ALTERNATE:      return "Alternate";
    default:                      return "Unknown";
    }
}

/* ======================================================================
 * Node count
 * ====================================================================== */

int neverc_regexp_syntax_node_count(const neverc_regexp_syntax_node_t *node) {
    if (!node) return 0;
    int count = 1;
    for (int i = 0; i < node->nsubs; i++)
        count += neverc_regexp_syntax_node_count(node->subs[i]);
    return count;
}
