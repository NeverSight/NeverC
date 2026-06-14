#include "neverc/std/regexp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* NFA state types */
enum { NFA_MATCH, NFA_CHAR, NFA_ANY, NFA_SPLIT, NFA_CLASS, NFA_ANCHOR_START, NFA_ANCHOR_END };

typedef struct {
    uint8_t bitmap[32];
    int     negated;
} charclass_t;

typedef struct nfa_state {
    int type;
    int ch;
    charclass_t *cls;
    struct nfa_state *out1;
    struct nfa_state *out2;
    int id;
} nfa_state_t;

/*
 * States and classes are handed out in fixed-size blocks instead of one growable
 * array. realloc on a growable array would move it and dangle every out1/out2
 * pointer already wired up (and every frag_t held on the parser's call stack),
 * which silently corrupted any pattern needing more than the initial capacity
 * (e.g. a long literal, or {n,m} expansion below). Block addresses never move,
 * so a state/class pointer is valid for the lifetime of the regexp.
 */
#define NFA_BLK 512                 /* states (and classes) per block */
#define NFA_MAX_REPEAT 1000         /* cap {n,m} expansion (matches Go's limit) */

struct neverc_regexp {
    nfa_state_t  *start;
    nfa_state_t **sblk;             /* state blocks */
    int           nsblk, sblkcap, nstates;
    charclass_t **cblk;             /* class blocks */
    int           ncblk, cblkcap, nclasses;
    int           ngroups;
    int           posix;
    int           oom;              /* sticky: a block allocation failed */
    nfa_state_t   dummy;            /* returned on OOM so callers never deref NULL */
    charclass_t   dummy_class;
};

static nfa_state_t *state_at(neverc_regexp_t *re, int idx) {
    return &re->sblk[idx / NFA_BLK][idx % NFA_BLK];
}

static nfa_state_t *new_state(neverc_regexp_t *re, int type) {
    if (re->nstates % NFA_BLK == 0) {           /* current block full */
        if (re->nsblk == re->sblkcap) {
            int nc = re->sblkcap ? re->sblkcap * 2 : 8;
            nfa_state_t **nb = (nfa_state_t **)realloc(re->sblk, (size_t)nc * sizeof(*nb));
            if (!nb) { re->oom = 1; return &re->dummy; }
            re->sblk = nb; re->sblkcap = nc;
        }
        nfa_state_t *blk = (nfa_state_t *)calloc(NFA_BLK, sizeof(nfa_state_t));
        if (!blk) { re->oom = 1; return &re->dummy; }
        re->sblk[re->nsblk++] = blk;
    }
    nfa_state_t *s = state_at(re, re->nstates);
    s->type = type;
    s->id = re->nstates++;
    return s;
}

static charclass_t *new_class(neverc_regexp_t *re) {
    if (re->nclasses % NFA_BLK == 0) {
        if (re->ncblk == re->cblkcap) {
            int nc = re->cblkcap ? re->cblkcap * 2 : 4;
            charclass_t **nb = (charclass_t **)realloc(re->cblk, (size_t)nc * sizeof(*nb));
            if (!nb) { re->oom = 1; return &re->dummy_class; }
            re->cblk = nb; re->cblkcap = nc;
        }
        charclass_t *blk = (charclass_t *)calloc(NFA_BLK, sizeof(charclass_t));
        if (!blk) { re->oom = 1; return &re->dummy_class; }
        re->cblk[re->ncblk++] = blk;
    }
    charclass_t *c = &re->cblk[re->nclasses / NFA_BLK][re->nclasses % NFA_BLK];
    re->nclasses++;
    return c;
}

static void cc_set(charclass_t *cc, int ch) {
    if (ch >= 0 && ch < 256)
        cc->bitmap[ch / 8] |= (1 << (ch % 8));
}

static int cc_test(const charclass_t *cc, int ch) {
    if (ch < 0 || ch >= 256) return cc->negated;
    int r = (cc->bitmap[ch / 8] >> (ch % 8)) & 1;
    return cc->negated ? !r : r;
}

typedef struct { nfa_state_t *start, *end; } frag_t;

static frag_t frag(nfa_state_t *s, nfa_state_t *e) {
    frag_t f = { s, e };
    return f;
}

/* Cap parenthesis nesting: parse_atom recurses into parse_expr for each '(', so
 * an adversarial pattern of many '(' would overflow the C stack at compile time
 * (a DoS). 400 is far past any real pattern and safe on small thread stacks
 * (this cycle is ~4 frames deep per level). */
#define NCI_REGEXP_MAX_DEPTH 400

/* Recursive-descent parser for regex */
typedef struct {
    const char *p;
    neverc_regexp_t *re;
    const char *err;
    int depth;
} parser_t;

static frag_t parse_expr(parser_t *par);
static frag_t parse_concat(parser_t *par);
static frag_t parse_repeat(parser_t *par);
static frag_t parse_atom(parser_t *par);

static int is_meta(char c) {
    return c == '|' || c == '*' || c == '+' || c == '?' ||
           c == '(' || c == ')' || c == '[' || c == '{';
}

static frag_t parse_atom(parser_t *par) {
    if (par->err) return frag(NULL, NULL);
    char c = *par->p;

    if (c == '(') {
        par->p++;
        par->re->ngroups++;
        if (++par->depth > NCI_REGEXP_MAX_DEPTH) {
            par->err = "expression nested too deeply";
            par->depth--;
            return frag(NULL, NULL);
        }
        frag_t f = parse_expr(par);
        par->depth--;
        if (*par->p == ')') par->p++;
        else par->err = "missing )";
        return f;
    }

    if (c == '[') {
        par->p++;
        charclass_t *cc = new_class(par->re);
        if (*par->p == '^') { cc->negated = 1; par->p++; }
        while (*par->p && *par->p != ']') {
            int lo = (uint8_t)*par->p++;
            if (lo == '\\' && *par->p) {
                char esc = *par->p++;
                if (esc == 'd') { for (int i = '0'; i <= '9'; i++) cc_set(cc, i); continue; }
                if (esc == 'w') {
                    for (int i = 'a'; i <= 'z'; i++) cc_set(cc, i);
                    for (int i = 'A'; i <= 'Z'; i++) cc_set(cc, i);
                    for (int i = '0'; i <= '9'; i++) cc_set(cc, i);
                    cc_set(cc, '_'); continue;
                }
                if (esc == 's') { cc_set(cc, ' '); cc_set(cc, '\t'); cc_set(cc, '\n'); cc_set(cc, '\r'); continue; }
                lo = (uint8_t)esc;
            }
            if (*par->p == '-' && par->p[1] && par->p[1] != ']') {
                par->p++;
                int hi = (uint8_t)*par->p++;
                for (int i = lo; i <= hi; i++) cc_set(cc, i);
            } else {
                cc_set(cc, lo);
            }
        }
        if (*par->p == ']') par->p++;
        nfa_state_t *s = new_state(par->re, NFA_CLASS);
        s->cls = cc;
        nfa_state_t *e = new_state(par->re, NFA_MATCH);
        s->out1 = e;
        return frag(s, e);
    }

    if (c == '.') {
        par->p++;
        nfa_state_t *s = new_state(par->re, NFA_ANY);
        nfa_state_t *e = new_state(par->re, NFA_MATCH);
        s->out1 = e;
        return frag(s, e);
    }

    if (c == '^') {
        par->p++;
        nfa_state_t *s = new_state(par->re, NFA_ANCHOR_START);
        nfa_state_t *e = new_state(par->re, NFA_MATCH);
        s->out1 = e;
        return frag(s, e);
    }

    if (c == '$') {
        par->p++;
        nfa_state_t *s = new_state(par->re, NFA_ANCHOR_END);
        nfa_state_t *e = new_state(par->re, NFA_MATCH);
        s->out1 = e;
        return frag(s, e);
    }

    if (c == '\\' && par->p[1]) {
        par->p++;
        char esc = *par->p++;
        charclass_t *cc = new_class(par->re);
        if (esc == 'd') { for (int i = '0'; i <= '9'; i++) cc_set(cc, i); }
        else if (esc == 'D') { for (int i = '0'; i <= '9'; i++) cc_set(cc, i); cc->negated = 1; }
        else if (esc == 'w') {
            for (int i = 'a'; i <= 'z'; i++) cc_set(cc, i);
            for (int i = 'A'; i <= 'Z'; i++) cc_set(cc, i);
            for (int i = '0'; i <= '9'; i++) cc_set(cc, i);
            cc_set(cc, '_');
        }
        else if (esc == 'W') {
            for (int i = 'a'; i <= 'z'; i++) cc_set(cc, i);
            for (int i = 'A'; i <= 'Z'; i++) cc_set(cc, i);
            for (int i = '0'; i <= '9'; i++) cc_set(cc, i);
            cc_set(cc, '_'); cc->negated = 1;
        }
        else if (esc == 's') { cc_set(cc, ' '); cc_set(cc, '\t'); cc_set(cc, '\n'); cc_set(cc, '\r'); cc_set(cc, '\f'); cc_set(cc, '\v'); }
        else if (esc == 'S') { cc_set(cc, ' '); cc_set(cc, '\t'); cc_set(cc, '\n'); cc_set(cc, '\r'); cc_set(cc, '\f'); cc_set(cc, '\v'); cc->negated = 1; }
        else {
            nfa_state_t *s = new_state(par->re, NFA_CHAR);
            s->ch = (uint8_t)esc;
            nfa_state_t *e = new_state(par->re, NFA_MATCH);
            s->out1 = e;
            return frag(s, e);
        }
        nfa_state_t *s = new_state(par->re, NFA_CLASS);
        s->cls = cc;
        nfa_state_t *e = new_state(par->re, NFA_MATCH);
        s->out1 = e;
        return frag(s, e);
    }

    if (c && !is_meta(c) && c != ')' && c != ']') {
        par->p++;
        nfa_state_t *s = new_state(par->re, NFA_CHAR);
        s->ch = (uint8_t)c;
        nfa_state_t *e = new_state(par->re, NFA_MATCH);
        s->out1 = e;
        return frag(s, e);
    }

    return frag(NULL, NULL);
}

/* Translate a pointer into the [lo,hi) state range to the matching pointer in a
 * clone that starts at newbase; pointers outside the range pass through. */
static nfa_state_t *clone_ptr(neverc_regexp_t *re, nfa_state_t *p,
                              int lo, int hi, int newbase) {
    if (!p) return NULL;
    if (p->id >= lo && p->id < hi) return state_at(re, newbase + (p->id - lo));
    return p;
}

/* Deep-copy the self-contained state range [lo,hi) (one repetition unit) to a
 * freshly appended range, sharing immutable char classes. Returns the clone of
 * fragment f. The source range must not be mutated until all clones are made. */
static frag_t clone_range(neverc_regexp_t *re, int lo, int hi, frag_t f) {
    int count = hi - lo, newbase = re->nstates;
    for (int i = 0; i < count; i++) {
        nfa_state_t *src = state_at(re, lo + i);
        int ch = src->ch;
        charclass_t *cls = src->cls;         /* classes are read-only: share */
        nfa_state_t *dst = new_state(re, src->type);
        if (re->oom) { frag_t z = { NULL, NULL }; return z; }
        dst->ch = ch;
        dst->cls = cls;
    }
    for (int i = 0; i < count; i++) {        /* fix links once all ids exist */
        nfa_state_t *src = state_at(re, lo + i);
        nfa_state_t *dst = state_at(re, newbase + i);
        dst->out1 = clone_ptr(re, src->out1, lo, hi, newbase);
        dst->out2 = clone_ptr(re, src->out2, lo, hi, newbase);
    }
    return frag(state_at(re, newbase + (f.start->id - lo)),
                state_at(re, newbase + (f.end->id - lo)));
}

static frag_t mk_concat(frag_t a, frag_t b) {
    if (!a.start) return b;
    if (!b.start) return a;
    a.end->out1 = b.start;
    return frag(a.start, b.end);
}

static frag_t mk_star(neverc_regexp_t *re, frag_t fr) {
    nfa_state_t *split = new_state(re, NFA_SPLIT);
    nfa_state_t *end = new_state(re, NFA_MATCH);
    split->out1 = fr.start; split->out2 = end;
    if (fr.end) fr.end->out1 = split;
    return frag(split, end);
}

static frag_t mk_opt(neverc_regexp_t *re, frag_t fr) {
    nfa_state_t *split = new_state(re, NFA_SPLIT);
    nfa_state_t *end = new_state(re, NFA_MATCH);
    split->out1 = fr.start; split->out2 = end;
    if (fr.end) fr.end->out1 = end;
    return frag(split, end);
}

/* Expand X{lo,hi} into lo mandatory copies of X plus the optional tail:
 * a star copy when hi is unbounded (-1), else (hi-lo) copies each made optional.
 * X is the range [range_lo,range_hi); copies share X's classes and are wired up
 * with the same split/MATCH glue used by *, +, ?. */
static frag_t expand_repeat(neverc_regexp_t *re, frag_t f,
                            int range_lo, int range_hi, int lo, int hi) {
    if (lo == 0 && hi == 0) {                /* {0}: matches empty */
        nfa_state_t *e = new_state(re, NFA_MATCH);
        return frag(e, e);
    }
    int total = (hi == -1) ? (lo == 0 ? 1 : lo + 1) : hi;
    frag_t *cp = (frag_t *)malloc((size_t)total * sizeof(frag_t));
    if (!cp) { re->oom = 1; return f; }
    cp[0] = f;
    for (int i = 1; i < total; i++) {        /* clone before any gluing mutates f */
        cp[i] = clone_range(re, range_lo, range_hi, f);
        if (re->oom) { free(cp); return f; }
    }

    frag_t r;
    if (lo == 0) {
        if (hi == -1) {
            r = mk_star(re, cp[0]);
        } else {
            r = mk_opt(re, cp[0]);
            for (int i = 1; i < hi; i++) r = mk_concat(r, mk_opt(re, cp[i]));
        }
    } else {
        r = cp[0];
        for (int i = 1; i < lo; i++) r = mk_concat(r, cp[i]);
        if (hi == -1) r = mk_concat(r, mk_star(re, cp[lo]));
        else for (int i = lo; i < hi; i++) r = mk_concat(r, mk_opt(re, cp[i]));
    }
    free(cp);
    return r;
}

static frag_t parse_repeat(parser_t *par) {
    int atom_base = par->re->nstates;
    frag_t f = parse_atom(par);
    if (!f.start || par->err) return f;

    while (*par->p == '*' || *par->p == '+' || *par->p == '?' || *par->p == '{') {
        char op = *par->p;
        if (op == '{') {
            const char *save = par->p;
            par->p++;
            int lo = 0, hi, have = 0;
            while (*par->p >= '0' && *par->p <= '9') { lo = lo * 10 + (*par->p++ - '0'); have = 1; }
            hi = lo;
            if (*par->p == ',') {
                par->p++;
                if (*par->p >= '0' && *par->p <= '9') {
                    hi = 0;
                    while (*par->p >= '0' && *par->p <= '9') hi = hi * 10 + (*par->p++ - '0');
                } else {
                    hi = -1;                 /* {n,} -> n or more */
                }
            }
            if (*par->p != '}' || !have) {   /* not a valid repeat: treat { literally */
                par->p = save + 1;
                nfa_state_t *s = new_state(par->re, NFA_CHAR);
                nfa_state_t *e = new_state(par->re, NFA_MATCH);
                s->ch = '{'; s->out1 = e;
                f = mk_concat(f, frag(s, e));
                continue;
            }
            par->p++;                        /* consume '}' */
            if (hi != -1 && hi < lo) hi = lo;
            if (lo > NFA_MAX_REPEAT || (hi != -1 && hi > NFA_MAX_REPEAT)) {
                par->err = "repeat count too large";
                return f;
            }
            f = expand_repeat(par->re, f, atom_base, par->re->nstates, lo, hi);
            if (par->re->oom) { par->err = "out of memory"; return f; }
            continue;
        }
        par->p++;
        nfa_state_t *split = new_state(par->re, NFA_SPLIT);
        nfa_state_t *end = new_state(par->re, NFA_MATCH);

        if (op == '*') {
            split->out1 = f.start;
            split->out2 = end;
            f.end->out1 = split;
            f = frag(split, end);
        } else if (op == '+') {
            split->out1 = f.start;
            split->out2 = end;
            f.end->out1 = split;
            f = frag(f.start, end);
        } else { /* ? */
            split->out1 = f.start;
            split->out2 = end;
            f.end->out1 = end;
            f = frag(split, end);
        }
    }
    return f;
}

static frag_t parse_concat(parser_t *par) {
    frag_t result = { NULL, NULL };
    while (*par->p && *par->p != '|' && *par->p != ')' && !par->err) {
        frag_t f = parse_repeat(par);
        if (!f.start) break;
        if (!result.start) {
            result = f;
        } else {
            result.end->out1 = f.start;
            nfa_state_t *new_end = f.end;
            result.end = new_end;
        }
    }
    if (!result.start) {
        nfa_state_t *e = new_state(par->re, NFA_MATCH);
        result = frag(e, e);
    }
    return result;
}

static frag_t parse_expr(parser_t *par) {
    frag_t f = parse_concat(par);
    while (*par->p == '|' && !par->err) {
        par->p++;
        frag_t f2 = parse_concat(par);
        nfa_state_t *split = new_state(par->re, NFA_SPLIT);
        nfa_state_t *end = new_state(par->re, NFA_MATCH);
        split->out1 = f.start;
        split->out2 = f2.start;
        f.end->out1 = end;
        f2.end->out1 = end;
        f = frag(split, end);
    }
    return f;
}

neverc_regexp_t *neverc_regexp_compile(const char *pattern, const char **errp) {
    neverc_regexp_t *re = (neverc_regexp_t *)calloc(1, sizeof(neverc_regexp_t));
    if (!re) { if (errp) *errp = "out of memory"; return NULL; }

    parser_t par = { pattern, re, NULL, 0 };
    frag_t f = parse_expr(&par);

    if (par.err || re->oom) {
        if (errp) *errp = par.err ? par.err : "out of memory";
        neverc_regexp_free(re);
        return NULL;
    }

    nfa_state_t *accept = new_state(re, NFA_MATCH);
    if (f.end) f.end->out1 = accept;
    re->start = f.start ? f.start : accept;

    if (re->oom) {                              /* allocation failed at the end */
        if (errp) *errp = "out of memory";
        neverc_regexp_free(re);
        return NULL;
    }
    if (errp) *errp = NULL;
    return re;
}

void neverc_regexp_free(neverc_regexp_t *re) {
    if (!re) return;
    for (int i = 0; i < re->nsblk; i++) free(re->sblk[i]);
    free(re->sblk);
    for (int i = 0; i < re->ncblk; i++) free(re->cblk[i]);
    free(re->cblk);
    free(re);
}

/* NFA simulation with state list */
typedef struct {
    nfa_state_t **states;
    int           n;
    int           cap;
} statelist_t;

static void sl_init(statelist_t *sl, int cap) {
    sl->cap = cap;
    sl->states = (nfa_state_t **)malloc(cap * sizeof(nfa_state_t *));
    sl->n = 0;
}

static void sl_free(statelist_t *sl) { free(sl->states); }

static void add_state(statelist_t *sl, nfa_state_t *s, int *visited, int gen) {
    if (!s || visited[s->id] == gen) return;
    visited[s->id] = gen;
    if (s->type == NFA_SPLIT) {
        add_state(sl, s->out1, visited, gen);
        add_state(sl, s->out2, visited, gen);
        return;
    }
    /* Intermediate NFA_MATCH nodes (with out1 set) are epsilon transitions */
    if (s->type == NFA_MATCH && s->out1 != NULL) {
        add_state(sl, s->out1, visited, gen);
        return;
    }
    if (sl->n < sl->cap)
        sl->states[sl->n++] = s;
}

/* Reusable execution context: the previous code allocated the visited array and
 * both state lists on every nfa_exec call, and find/replace/split call nfa_exec
 * once per text position — O(n) malloc/free churn. Sharing one context across
 * all positions removes that, and a monotonically increasing generation counter
 * means the visited array never needs re-zeroing between calls. */
typedef struct {
    int         *visited;
    statelist_t  cur, next;
    int          gen;
} nfa_ctx;

static int nfa_ctx_init(nfa_ctx *c, int nstates) {
    if (nstates < 1) nstates = 1;
    c->visited = (int *)calloc((size_t)nstates, sizeof(int));
    sl_init(&c->cur, nstates);
    sl_init(&c->next, nstates);
    c->gen = 0;
    if (!c->visited || !c->cur.states || !c->next.states) {
        free(c->visited); free(c->cur.states); free(c->next.states);
        return -1;
    }
    return 0;
}

static void nfa_ctx_free(nfa_ctx *c) {
    free(c->visited);
    sl_free(&c->cur);
    sl_free(&c->next);
}

/* First-byte prefilter: collect the set of bytes that can begin a non-empty
 * match by walking the start state's epsilon-closure. Lets the search loop skip
 * positions whose byte can never start a match (the literal-prefix optimization
 * Go/RE2 use). Returns 0 (disable) when an anchor is reachable or the set would
 * be universal, so semantics are never affected — only impossible starts skip. */
static void fb_walk(nfa_state_t *s, int *vis, int gen, uint8_t fb[32],
                    int *full, int *unsafe) {
    if (!s || vis[s->id] == gen) return;
    vis[s->id] = gen;
    switch (s->type) {
    case NFA_SPLIT:
        fb_walk(s->out1, vis, gen, fb, full, unsafe);
        fb_walk(s->out2, vis, gen, fb, full, unsafe);
        return;
    case NFA_MATCH:
        if (s->out1) fb_walk(s->out1, vis, gen, fb, full, unsafe);
        return;                       /* accept node: empty-match is filtered out anyway */
    case NFA_ANCHOR_START:
    case NFA_ANCHOR_END:
        *unsafe = 1;                  /* let the normal scan handle anchors */
        return;
    case NFA_CHAR:
        fb[(s->ch & 0xff) >> 3] |= (uint8_t)(1u << (s->ch & 7));
        return;
    case NFA_ANY:
        *full = 1;                    /* matches (almost) any byte */
        return;
    case NFA_CLASS:
        for (int c = 0; c < 256; c++)
            if (cc_test(s->cls, c)) fb[c >> 3] |= (uint8_t)(1u << (c & 7));
        return;
    default:
        *unsafe = 1;
        return;
    }
}

static int compute_first_set(neverc_regexp_t *re, uint8_t fb[32]) {
    memset(fb, 0, 32);
    int *vis = (int *)calloc((size_t)(re->nstates > 0 ? re->nstates : 1), sizeof(int));
    if (!vis) return 0;
    int full = 0, unsafe = 0;
    fb_walk(re->start, vis, 1, fb, &full, &unsafe);
    free(vis);
    if (full || unsafe) return 0;
    for (int i = 0; i < 32; i++) if (fb[i]) return 1;
    return 0;                         /* empty set -> nothing to skip on */
}

/* If the first-byte set contains exactly one byte, return it; else -1. A single
 * required first byte (the common literal-prefix case: "error", "=\d+", ...)
 * lets next_cand jump with one SIMD memchr instead of a scalar bitset walk. */
static int first_set_single(const uint8_t fb[32]) {
    int found = -1;
    for (int i = 0; i < 256; i++)
        if (fb[i >> 3] & (1u << (i & 7))) {
            if (found >= 0) return -1;          /* more than one candidate byte */
            found = i;
        }
    return found;
}

/* Next position >= pos whose byte can start a match (or slen if none). When the
 * first-byte set is a single byte, scan it with memchr (SIMD) the same way the
 * substring engine jumps to needle[0]; otherwise fall back to the bitset walk. */
static size_t next_cand(const char *s, size_t slen, size_t pos,
                        const uint8_t fb[32], int use_skip, int first_byte) {
    if (!use_skip || pos >= slen) return pos;
    if (first_byte >= 0) {
        const void *hit = memchr(s + pos, first_byte, slen - pos);
        return hit ? (size_t)((const char *)hit - s) : slen;
    }
    while (pos < slen) {
        uint8_t c = (uint8_t)s[pos];
        if (fb[c >> 3] & (1u << (c & 7))) return pos;
        pos++;
    }
    return pos;   /* == slen */
}

/* NFA simulation from a fixed start position, reusing a shared context. Returns
 * the longest match length via *match_end (logic identical to the original; only
 * the per-call allocations and gen reset were lifted out). */
static int nfa_exec_ctx(nfa_ctx *ctx, neverc_regexp_t *re, const char *s,
                        size_t slen, size_t start, size_t *match_end) {
    int *visited = ctx->visited;
    statelist_t cur = ctx->cur, next = ctx->next;   /* share buffers, reset n */
    cur.n = 0;

    int gen = ++ctx->gen;
    add_state(&cur, re->start, visited, gen);

    /* Handle anchor-start states */
    for (int i = 0; i < cur.n; i++) {
        nfa_state_t *st = cur.states[i];
        if (st->type == NFA_ANCHOR_START) {
            if (start == 0)
                add_state(&cur, st->out1, visited, gen);
        }
    }

    size_t best_end = (size_t)-1;
    for (int i = 0; i < cur.n; i++)
        if (cur.states[i]->type == NFA_MATCH && cur.states[i]->out1 == NULL)
            best_end = start;

    for (size_t i = start; i < slen; i++) {
        int ch = (uint8_t)s[i];
        next.n = 0;
        gen = ++ctx->gen;

        for (int j = 0; j < cur.n; j++) {
            nfa_state_t *st = cur.states[j];
            int ok = 0;
            switch (st->type) {
            case NFA_CHAR:
                ok = (st->ch == ch);
                break;
            case NFA_ANY:
                ok = (ch != '\n');
                break;
            case NFA_CLASS:
                ok = cc_test(st->cls, ch);
                break;
            case NFA_ANCHOR_END:
                break;
            default:
                break;
            }
            if (ok) add_state(&next, st->out1, visited, gen);
        }

        /* Handle anchor states in next */
        for (int j = 0; j < next.n; j++) {
            nfa_state_t *st = next.states[j];
            if (st->type == NFA_ANCHOR_END && i + 1 == slen)
                add_state(&next, st->out1, visited, gen);
            if (st->type == NFA_ANCHOR_START)
                {}; /* Start anchor only at position 0 */
        }

        statelist_t tmp = cur; cur = next; next = tmp;

        for (int j = 0; j < cur.n; j++)
            if (cur.states[j]->type == NFA_MATCH && cur.states[j]->out1 == NULL)
                best_end = i + 1;

        if (cur.n == 0) break;
    }

    /* Check end anchors at string end */
    for (int j = 0; j < cur.n; j++) {
        nfa_state_t *st = cur.states[j];
        if (st->type == NFA_ANCHOR_END) {
            gen = ++ctx->gen;
            add_state(&cur, st->out1, visited, gen);
        }
    }
    for (int j = 0; j < cur.n; j++)
        if (cur.states[j]->type == NFA_MATCH && cur.states[j]->out1 == NULL)
            best_end = slen;

    if (best_end != (size_t)-1) {
        if (match_end) *match_end = best_end;
        return 1;
    }
    return 0;
}

/* Convenience wrapper for callers that run a single simulation (e.g. match). */
static int nfa_exec(neverc_regexp_t *re, const char *s, size_t slen,
                    size_t start, size_t *match_end) {
    nfa_ctx ctx;
    if (nfa_ctx_init(&ctx, re->nstates) != 0) return 0;
    int r = nfa_exec_ctx(&ctx, re, s, slen, start, match_end);
    nfa_ctx_free(&ctx);
    return r;
}

int neverc_regexp_match(neverc_regexp_t *re, const char *s) {
    size_t slen = strlen(s);
    size_t end;
    if (nfa_exec(re, s, slen, 0, &end))
        return end == slen;
    return 0;
}

int neverc_regexp_match_string(const char *pattern, const char *s) {
    neverc_regexp_t *re = neverc_regexp_compile(pattern, NULL);
    if (!re) return 0;
    int r = neverc_regexp_match(re, s);
    neverc_regexp_free(re);
    return r;
}

/* ------------------------------------------------------------------ *
 * Single-pass leftmost-longest search (Pike-VM style).
 *
 * The old find/find_all/replace/split re-ran nfa_exec_ctx from every candidate
 * text position, so a common-first-byte pattern that scans far before failing
 * (e.g. "a+b" over a run of 'a') cost O(n) per position -> O(n^2) overall
 * (and O(n*m) for adversarial inputs). RE2/Go instead sweep the text once,
 * carrying every in-flight thread simultaneously and seeding a fresh start
 * thread at each position until a match is locked in. That bounds the whole
 * search at O(n * states) the same way introsort bounds its worst case.
 *
 * Each thread carries the text offset it began at. The epsilon-closure dedups
 * by NFA state, and because earlier-start threads are always enqueued before
 * the freshly seeded start, the surviving thread per state is the leftmost one
 * -- which gives leftmost-longest semantics identical to the old code:
 *   - the smallest start that reaches a non-empty accept wins (leftmost);
 *   - for that start, the largest accept offset wins (longest).
 * The first-byte prefilter still gates seeding and lets empty stretches be
 * skipped via memchr, so sparse matches keep their near-constant per-byte cost.
 * ------------------------------------------------------------------ */
typedef struct {
    nfa_state_t **st;       /* state per thread */
    size_t       *start;    /* text offset each thread began matching at */
    int           n;
} tlist_t;

typedef struct {
    int    *visited;        /* gen-stamped, so no per-call re-zeroing */
    tlist_t cur, next;
    int     gen;
} search_ctx;

static int search_ctx_init(search_ctx *c, int nstates) {
    if (nstates < 1) nstates = 1;
    c->visited     = (int *)calloc((size_t)nstates, sizeof(int));
    c->cur.st      = (nfa_state_t **)malloc((size_t)nstates * sizeof(nfa_state_t *));
    c->cur.start   = (size_t *)malloc((size_t)nstates * sizeof(size_t));
    c->next.st     = (nfa_state_t **)malloc((size_t)nstates * sizeof(nfa_state_t *));
    c->next.start  = (size_t *)malloc((size_t)nstates * sizeof(size_t));
    c->cur.n = c->next.n = 0;
    c->gen = 0;
    if (!c->visited || !c->cur.st || !c->cur.start || !c->next.st || !c->next.start) {
        free(c->visited); free(c->cur.st); free(c->cur.start);
        free(c->next.st); free(c->next.start);
        return -1;
    }
    return 0;
}

static void search_ctx_free(search_ctx *c) {
    free(c->visited);
    free(c->cur.st); free(c->cur.start);
    free(c->next.st); free(c->next.start);
}

/* Add state s (and its epsilon-closure) to tl, tagged with start offset. pos is
 * the current text offset, used to resolve anchors: ^ only crosses at offset 0,
 * $ only at offset slen. Dedup by state keeps the first (leftmost) thread. */
static void search_add(tlist_t *tl, nfa_state_t *s, size_t start, size_t pos,
                       size_t slen, int *visited, int gen) {
    if (!s || visited[s->id] == gen) return;
    visited[s->id] = gen;
    if (s->type == NFA_SPLIT) {
        search_add(tl, s->out1, start, pos, slen, visited, gen);
        search_add(tl, s->out2, start, pos, slen, visited, gen);
        return;
    }
    if (s->type == NFA_MATCH && s->out1 != NULL) {       /* epsilon link */
        search_add(tl, s->out1, start, pos, slen, visited, gen);
        return;
    }
    if (s->type == NFA_ANCHOR_START) {
        if (pos == 0) search_add(tl, s->out1, start, pos, slen, visited, gen);
        return;
    }
    if (s->type == NFA_ANCHOR_END) {
        if (pos == slen) search_add(tl, s->out1, start, pos, slen, visited, gen);
        return;
    }
    tl->st[tl->n] = s;                                   /* CHAR/ANY/CLASS/accept */
    tl->start[tl->n] = start;
    tl->n++;
}

static int fb_has(const uint8_t fb[32], unsigned char c) {
    return (fb[c >> 3] >> (c & 7)) & 1;
}

/* Leftmost-longest non-empty match at or after `from`. Returns 1 and sets
 * [*mstart,*mend) on success. Buffers in ctx are reused across calls. */
static int nfa_search(search_ctx *ctx, neverc_regexp_t *re, const char *s,
                      size_t slen, size_t from, const uint8_t fb[32],
                      int use_skip, int first_byte, size_t *mstart, size_t *mend) {
    tlist_t clist = ctx->cur, nlist = ctx->next;
    int *visited = ctx->visited;
    int matched = 0;
    size_t b_start = 0, b_end = 0;

    size_t pos = next_cand(s, slen, from, fb, use_skip, first_byte);
    int gen = ++ctx->gen;
    clist.n = 0;
    if (pos < slen && (!use_skip || fb_has(fb, (unsigned char)s[pos])))
        search_add(&clist, re->start, pos, pos, slen, visited, gen);
    else if (!use_skip)                                  /* offset slen: anchors only */
        search_add(&clist, re->start, pos, pos, slen, visited, gen);

    for (;;) {
        for (int j = 0; j < clist.n; j++) {              /* record accepts at pos */
            nfa_state_t *st = clist.st[j];
            if (st->type == NFA_MATCH && st->out1 == NULL) {
                size_t s0 = clist.start[j];
                if (pos > s0) {                          /* non-empty match only */
                    if (!matched || s0 < b_start) { matched = 1; b_start = s0; b_end = pos; }
                    else if (s0 == b_start && pos > b_end) b_end = pos;
                }
            }
        }
        if (matched) {                                   /* drop hopeless later starts */
            int w = 0;
            for (int j = 0; j < clist.n; j++)
                if (clist.start[j] <= b_start) {
                    clist.st[w] = clist.st[j];
                    clist.start[w] = clist.start[j];
                    w++;
                }
            clist.n = w;
        }
        if (pos >= slen) break;
        if (clist.n == 0 && matched) break;

        int ch = (unsigned char)s[pos];
        gen = ++ctx->gen;
        nlist.n = 0;
        for (int j = 0; j < clist.n; j++) {
            nfa_state_t *st = clist.st[j];
            int ok = 0;
            switch (st->type) {
            case NFA_CHAR:  ok = (st->ch == ch); break;
            case NFA_ANY:   ok = (ch != '\n');   break;
            case NFA_CLASS: ok = cc_test(st->cls, ch); break;
            default: break;
            }
            if (ok) search_add(&nlist, st->out1, clist.start[j], pos + 1, slen, visited, gen);
        }

        size_t npos = pos + 1;
        if (!matched) {
            if (nlist.n == 0) {                          /* no in-flight: jump to next start */
                npos = next_cand(s, slen, npos, fb, use_skip, first_byte);
                gen = ++ctx->gen;                        /* fresh stamp for clean seed */
            }
            if (npos < slen && (!use_skip || fb_has(fb, (unsigned char)s[npos])))
                search_add(&nlist, re->start, npos, npos, slen, visited, gen);
        }

        tlist_t tmp = clist; clist = nlist; nlist = tmp; /* swap, reuse buffers */
        pos = npos;
        if (clist.n == 0 && !matched && pos >= slen) break;
    }

    ctx->cur = clist; ctx->next = nlist;                 /* persist swap for reuse */
    if (matched) { *mstart = b_start; *mend = b_end; return 1; }
    return 0;
}

const char *neverc_regexp_find(neverc_regexp_t *re, const char *s,
                               size_t *match_len) {
    size_t slen = strlen(s);
    search_ctx ctx;
    if (search_ctx_init(&ctx, re->nstates) != 0) { *match_len = 0; return NULL; }
    uint8_t fb[32];
    int use_skip = compute_first_set(re, fb);

    int fbyte = use_skip ? first_set_single(fb) : -1;
    const char *res = NULL;
    size_t ms, me;
    if (nfa_search(&ctx, re, s, slen, 0, fb, use_skip, fbyte, &ms, &me)) {
        *match_len = me - ms;
        res = s + ms;
    } else {
        *match_len = 0;
    }
    search_ctx_free(&ctx);
    return res;
}

int neverc_regexp_find_submatch(neverc_regexp_t *re, const char *s,
                                neverc_regexp_match_t *matches, int max_matches) {
    size_t match_len;
    const char *found = neverc_regexp_find(re, s, &match_len);
    if (!found) return 0;
    if (max_matches > 0) {
        matches[0].start = found;
        matches[0].len = match_len;
    }
    return 1;
}

char **neverc_regexp_find_all(neverc_regexp_t *re, const char *s,
                              int n, int *count) {
    size_t slen = strlen(s);
    int cap = 16;
    char **results = (char **)malloc(cap * sizeof(char *));
    *count = 0;

    search_ctx ctx;
    if (search_ctx_init(&ctx, re->nstates) != 0) return results;
    uint8_t fb[32];
    int use_skip = compute_first_set(re, fb);

    int fbyte = use_skip ? first_set_single(fb) : -1;
    size_t pos = 0;
    while (pos <= slen && (n < 0 || *count < n)) {
        size_t ms, me;
        if (!nfa_search(&ctx, re, s, slen, pos, fb, use_skip, fbyte, &ms, &me)) break;
        size_t mlen = me - ms;
        char *match = (char *)malloc(mlen + 1);
        memcpy(match, s + ms, mlen);
        match[mlen] = '\0';
        if (*count >= cap) {
            cap *= 2;
            results = (char **)realloc(results, cap * sizeof(char *));
        }
        results[(*count)++] = match;
        pos = me;                               /* non-empty match -> always advances */
    }
    search_ctx_free(&ctx);
    return results;
}

char *neverc_regexp_replace_all(neverc_regexp_t *re, const char *src,
                                const char *repl, size_t *outlen) {
    size_t slen = strlen(src);
    size_t rlen = strlen(repl);
    size_t cap = slen * 2 + 64;
    char *result = (char *)malloc(cap);
    size_t wi = 0, pos = 0;

    search_ctx ctx;
    if (search_ctx_init(&ctx, re->nstates) != 0) {
        memcpy(result, src, slen); result[slen] = '\0';
        *outlen = slen; return result;
    }
    uint8_t fb[32];
    int use_skip = compute_first_set(re, fb);
    int fbyte = use_skip ? first_set_single(fb) : -1;

    while (pos <= slen) {
        size_t i, end;
        if (!nfa_search(&ctx, re, src, slen, pos, fb, use_skip, fbyte, &i, &end)) break;
        /* Copy unmatched part before match */
        size_t before = i - pos;
        if (wi + before >= cap) { cap = (wi + before) * 2 + 64; result = (char *)realloc(result, cap); }
        memcpy(result + wi, src + pos, before);
        wi += before;
        /* Copy replacement */
        if (wi + rlen >= cap) { cap = (wi + rlen) * 2 + 64; result = (char *)realloc(result, cap); }
        memcpy(result + wi, repl, rlen);
        wi += rlen;
        pos = end;                              /* non-empty match -> always advances */
    }

    /* Copy remaining */
    if (pos < slen) {
        size_t rem = slen - pos;
        if (wi + rem >= cap) { cap = (wi + rem) * 2; result = (char *)realloc(result, cap); }
        memcpy(result + wi, src + pos, rem);
        wi += rem;
    }

    result[wi] = '\0';
    *outlen = wi;
    search_ctx_free(&ctx);
    return result;
}

char **neverc_regexp_split(neverc_regexp_t *re, const char *s,
                           int n, int *count) {
    size_t slen = strlen(s);
    int cap = 16;
    char **results = (char **)malloc(cap * sizeof(char *));
    *count = 0;
    size_t pos = 0;

    search_ctx ctx;
    if (search_ctx_init(&ctx, re->nstates) != 0) {
        char *whole = (char *)malloc(slen + 1);
        memcpy(whole, s, slen); whole[slen] = '\0';
        results[(*count)++] = whole;
        return results;
    }
    uint8_t fb[32];
    int use_skip = compute_first_set(re, fb);
    int fbyte = use_skip ? first_set_single(fb) : -1;

    while (pos <= slen && (n < 0 || *count < n - 1)) {
        size_t end = 0, mstart;
        /* Leftmost non-empty match at or after pos, in a single linear sweep. */
        if (!nfa_search(&ctx, re, s, slen, pos, fb, use_skip, fbyte, &mstart, &end)) break;

        size_t seg_len = mstart - pos;          /* 0 when the match is at pos */
        char *seg = (char *)malloc(seg_len + 1);
        memcpy(seg, s + pos, seg_len);
        seg[seg_len] = '\0';
        if (*count >= cap) { cap *= 2; results = (char **)realloc(results, cap * sizeof(char *)); }
        results[(*count)++] = seg;
        pos = end;
    }

    /* Remaining */
    size_t rem = slen - pos;
    char *seg = (char *)malloc(rem + 1);
    memcpy(seg, s + pos, rem);
    seg[rem] = '\0';
    if (*count >= cap) { cap *= 2; results = (char **)realloc(results, cap * sizeof(char *)); }
    results[(*count)++] = seg;

    search_ctx_free(&ctx);
    return results;
}

void neverc_regexp_free_strings(char **strs, int count) {
    for (int i = 0; i < count; i++) free(strs[i]);
    free(strs);
}

/* RE2/POSIX special bytes that QuoteMeta must backslash-escape. A 256-entry
 * lookup collapses the previous 14-way `c == X || ...` comparison chain — which
 * every ordinary byte ran in full because none of the disjuncts short-circuit —
 * into a single branch-predictable load per byte. The flat single loop keeps
 * branch prediction stable on special-dense input, so the table is a strict win
 * across both ordinary and metacharacter-heavy strings. */
static const uint8_t regexp_meta_tbl[256] = {
    ['\\'] = 1, ['.'] = 1, ['+'] = 1, ['*'] = 1, ['?'] = 1,
    ['(']  = 1, [')'] = 1, ['|'] = 1, ['['] = 1, [']'] = 1,
    ['{']  = 1, ['}'] = 1, ['^'] = 1, ['$'] = 1,
};

char *neverc_regexp_quote_meta(const char *s) {
    if (!s) return NULL;
    size_t slen = strlen(s);
    char *result = (char *)malloc(slen * 2 + 1);
    if (!result) return NULL;

    /* Copy the leading run of ordinary bytes in one shot. A string with no
     * special bytes (the common case for QuoteMeta — escaping literal text)
     * thus reduces to a single scan plus one memcpy. */
    size_t i = 0;
    while (i < slen && !regexp_meta_tbl[(unsigned char)s[i]]) i++;
    memcpy(result, s, i);
    size_t j = i;

    /* Past the first special byte, emit branchlessly: write the backslash
     * unconditionally, then advance over it only for special bytes. Avoiding a
     * data-dependent branch keeps special-dense tails from mispredicting. */
    for (; i < slen; i++) {
        unsigned char c = (unsigned char)s[i];
        result[j] = '\\';
        j += regexp_meta_tbl[c];
        result[j++] = (char)c;
    }
    result[j] = '\0';
    return result;
}

neverc_regexp_t *neverc_regexp_compile_posix(const char *pattern, const char **errp) {
    neverc_regexp_t *re = neverc_regexp_compile(pattern, errp);
    if (re) re->posix = 1;
    return re;
}

neverc_regexp_t *neverc_regexp_must_compile(const char *pattern) {
    const char *err = NULL;
    neverc_regexp_t *re = neverc_regexp_compile(pattern, &err);
    if (!re) {
        fprintf(stderr, "neverc_regexp_must_compile: %s: %s\n", pattern, err ? err : "unknown error");
        abort();
    }
    return re;
}

neverc_regexp_t *neverc_regexp_must_compile_posix(const char *pattern) {
    const char *err = NULL;
    neverc_regexp_t *re = neverc_regexp_compile_posix(pattern, &err);
    if (!re) {
        fprintf(stderr, "neverc_regexp_must_compile_posix: %s: %s\n", pattern, err ? err : "unknown error");
        abort();
    }
    return re;
}
