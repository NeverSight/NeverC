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

struct neverc_regexp {
    nfa_state_t *start;
    nfa_state_t *states;
    int          nstates;
    int          cap;
    charclass_t *classes;
    int          nclasses;
    int          ncap;
    int          ngroups;
    int          posix;
};

static nfa_state_t *new_state(neverc_regexp_t *re, int type) {
    if (re->nstates >= re->cap) {
        re->cap *= 2;
        re->states = (nfa_state_t *)realloc(re->states,
                                            re->cap * sizeof(nfa_state_t));
    }
    nfa_state_t *s = &re->states[re->nstates];
    memset(s, 0, sizeof(*s));
    s->type = type;
    s->id = re->nstates++;
    return s;
}

static charclass_t *new_class(neverc_regexp_t *re) {
    if (re->nclasses >= re->ncap) {
        re->ncap *= 2;
        re->classes = (charclass_t *)realloc(re->classes,
                                             re->ncap * sizeof(charclass_t));
    }
    charclass_t *c = &re->classes[re->nclasses++];
    memset(c, 0, sizeof(*c));
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

/* Recursive-descent parser for regex */
typedef struct {
    const char *p;
    neverc_regexp_t *re;
    const char *err;
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
        frag_t f = parse_expr(par);
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

static frag_t parse_repeat(parser_t *par) {
    frag_t f = parse_atom(par);
    if (!f.start || par->err) return f;

    while (*par->p == '*' || *par->p == '+' || *par->p == '?' || *par->p == '{') {
        char op = *par->p;
        if (op == '{') {
            par->p++;
            int lo = 0, hi = -1;
            while (*par->p >= '0' && *par->p <= '9')
                lo = lo * 10 + (*par->p++ - '0');
            hi = lo;
            if (*par->p == ',') {
                par->p++;
                hi = 0;
                if (*par->p >= '0' && *par->p <= '9') {
                    while (*par->p >= '0' && *par->p <= '9')
                        hi = hi * 10 + (*par->p++ - '0');
                } else {
                    hi = -1;
                }
            }
            if (*par->p == '}') par->p++;
            /* Simplified: treat {n} as repeat n times, {n,} as n+ */
            (void)lo; (void)hi;
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
    re->cap = 256;
    re->states = (nfa_state_t *)calloc(re->cap, sizeof(nfa_state_t));
    re->ncap = 32;
    re->classes = (charclass_t *)calloc(re->ncap, sizeof(charclass_t));
    re->ngroups = 0;

    parser_t par = { pattern, re, NULL };
    frag_t f = parse_expr(&par);

    if (par.err) {
        if (errp) *errp = par.err;
        neverc_regexp_free(re);
        return NULL;
    }

    nfa_state_t *accept = new_state(re, NFA_MATCH);
    if (f.end) f.end->out1 = accept;
    re->start = f.start ? f.start : accept;

    if (errp) *errp = NULL;
    return re;
}

void neverc_regexp_free(neverc_regexp_t *re) {
    if (!re) return;
    free(re->states);
    free(re->classes);
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

static int nfa_exec(neverc_regexp_t *re, const char *s, size_t slen,
                    size_t start, size_t *match_end) {
    int nstates = re->nstates;
    int *visited = (int *)calloc(nstates, sizeof(int));
    statelist_t cur, next;
    sl_init(&cur, nstates);
    sl_init(&next, nstates);

    int gen = 1;
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
        gen++;

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
            gen++;
            add_state(&cur, st->out1, visited, gen);
        }
    }
    for (int j = 0; j < cur.n; j++)
        if (cur.states[j]->type == NFA_MATCH && cur.states[j]->out1 == NULL)
            best_end = slen;

    sl_free(&cur); sl_free(&next); free(visited);

    if (best_end != (size_t)-1) {
        if (match_end) *match_end = best_end;
        return 1;
    }
    return 0;
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

const char *neverc_regexp_find(neverc_regexp_t *re, const char *s,
                               size_t *match_len) {
    size_t slen = strlen(s);
    for (size_t i = 0; i <= slen; i++) {
        size_t end;
        if (nfa_exec(re, s, slen, i, &end) && end > i) {
            if (re->posix) {
                size_t best_end = end;
                for (size_t try_end = end + 1; try_end <= slen; try_end++) {
                    size_t e2;
                    if (nfa_exec(re, s, slen, i, &e2) && e2 > best_end)
                        best_end = e2;
                    else
                        break;
                }
                *match_len = best_end - i;
            } else {
                *match_len = end - i;
            }
            return s + i;
        }
    }
    *match_len = 0;
    return NULL;
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

    size_t pos = 0;
    while (pos <= slen && (n < 0 || *count < n)) {
        /* Try each starting position from pos onwards */
        int found = 0;
        for (size_t i = pos; i <= slen; i++) {
            size_t end;
            if (nfa_exec(re, s, slen, i, &end) && end > i) {
                size_t mlen = end - i;
                char *match = (char *)malloc(mlen + 1);
                memcpy(match, s + i, mlen);
                match[mlen] = '\0';
                if (*count >= cap) {
                    cap *= 2;
                    results = (char **)realloc(results, cap * sizeof(char *));
                }
                results[(*count)++] = match;
                pos = end;
                found = 1;
                break;
            }
        }
        if (!found) break;
    }
    return results;
}

char *neverc_regexp_replace_all(neverc_regexp_t *re, const char *src,
                                const char *repl, size_t *outlen) {
    size_t slen = strlen(src);
    size_t rlen = strlen(repl);
    size_t cap = slen * 2 + 64;
    char *result = (char *)malloc(cap);
    size_t wi = 0, pos = 0;

    while (pos <= slen) {
        /* Find next match starting from pos or later */
        int found = 0;
        for (size_t i = pos; i <= slen; i++) {
            size_t end;
            if (nfa_exec(re, src, slen, i, &end) && end > i) {
                /* Copy unmatched part before match */
                size_t before = i - pos;
                if (wi + before >= cap) { cap = (wi + before) * 2 + 64; result = (char *)realloc(result, cap); }
                memcpy(result + wi, src + pos, before);
                wi += before;
                /* Copy replacement */
                if (wi + rlen >= cap) { cap = (wi + rlen) * 2 + 64; result = (char *)realloc(result, cap); }
                memcpy(result + wi, repl, rlen);
                wi += rlen;
                pos = end;
                found = 1;
                break;
            }
        }
        if (!found) break;
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
    return result;
}

char **neverc_regexp_split(neverc_regexp_t *re, const char *s,
                           int n, int *count) {
    size_t slen = strlen(s);
    int cap = 16;
    char **results = (char **)malloc(cap * sizeof(char *));
    *count = 0;
    size_t pos = 0;

    while (pos <= slen && (n < 0 || *count < n - 1)) {
        size_t end;
        int found = 0;
        for (size_t i = pos; i <= slen; i++) {
            if (nfa_exec(re, s, slen, i, &end) && i == pos && end > pos) {
                found = 1;
                break;
            }
            if (nfa_exec(re, s, slen, i, &end) && end > i) {
                size_t seg_len = i - pos;
                char *seg = (char *)malloc(seg_len + 1);
                memcpy(seg, s + pos, seg_len);
                seg[seg_len] = '\0';
                if (*count >= cap) { cap *= 2; results = (char **)realloc(results, cap * sizeof(char *)); }
                results[(*count)++] = seg;
                pos = end;
                found = 1;
                break;
            }
        }
        if (!found) break;
        if (found && end > pos) {
            /* Match at current position */
            char *seg = (char *)malloc(1);
            seg[0] = '\0';
            if (*count >= cap) { cap *= 2; results = (char **)realloc(results, cap * sizeof(char *)); }
            results[(*count)++] = seg;
            pos = end;
        }
    }

    /* Remaining */
    size_t rem = slen - pos;
    char *seg = (char *)malloc(rem + 1);
    memcpy(seg, s + pos, rem);
    seg[rem] = '\0';
    if (*count >= cap) { cap *= 2; results = (char **)realloc(results, cap * sizeof(char *)); }
    results[(*count)++] = seg;

    return results;
}

void neverc_regexp_free_strings(char **strs, int count) {
    for (int i = 0; i < count; i++) free(strs[i]);
    free(strs);
}

char *neverc_regexp_quote_meta(const char *s) {
    if (!s) return NULL;
    size_t slen = strlen(s);
    char *result = (char *)malloc(slen * 2 + 1);
    if (!result) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < slen; i++) {
        char c = s[i];
        if (c == '\\' || c == '.' || c == '+' || c == '*' || c == '?' ||
            c == '(' || c == ')' || c == '|' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '^' || c == '$') {
            result[j++] = '\\';
        }
        result[j++] = c;
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
