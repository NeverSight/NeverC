#include "neverc/std/regexp.h"
#include "neverc/std/unicode.h"
#include "neverc/std/unicode/utf8.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#ifndef NC_REGEXP_MALLOC
#define NC_REGEXP_MALLOC malloc
#endif
#ifndef NC_REGEXP_CALLOC
#define NC_REGEXP_CALLOC calloc
#endif
#ifndef NC_REGEXP_REALLOC
#define NC_REGEXP_REALLOC realloc
#endif

/* NFA state types. CAP_OPEN/CLOSE are epsilon edges that record a group.
 * ANCHOR_END: ch==0 is `$` (end or before a final newline); ch==1 is `\z`. */
enum {
    NFA_MATCH, NFA_CHAR, NFA_ANY, NFA_SPLIT, NFA_CLASS,
    NFA_ANCHOR_START, NFA_ANCHOR_END, NFA_CAP_OPEN, NFA_CAP_CLOSE,
    NFA_WORD_BOUND, NFA_NO_WORD_BOUND
};

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
#define NFA_MAX_STATES 100000       /* cap compile size: (a{1000}){1000} */

struct neverc_regexp {
    nfa_state_t  *start;
    nfa_state_t **sblk;             /* state blocks */
    int           nsblk, sblkcap, nstates;
    charclass_t **cblk;             /* class blocks */
    int           ncblk, cblkcap, nclasses;
    int           ngroups;
    char        **gnames;           /* [i] name of group i, or NULL; may be short */
    int           gnames_cap;
    int           posix;
    int           oom;              /* sticky: a block allocation failed */
    nfa_state_t   dummy;            /* returned on OOM so callers never deref NULL */
    charclass_t   dummy_class;
};

static nfa_state_t *state_at(neverc_regexp_t *re, int idx) {
    return &re->sblk[idx / NFA_BLK][idx % NFA_BLK];
}

static nfa_state_t *new_state(neverc_regexp_t *re, int type) {
    if (re->nstates >= NFA_MAX_STATES || re->nstates == INT_MAX) {
        re->oom = 1;
        return &re->dummy;
    }
    if (re->nstates % NFA_BLK == 0) {           /* current block full */
        if (re->nsblk == re->sblkcap) {
            if (re->sblkcap > INT_MAX / 2) { re->oom = 1; return &re->dummy; }
            int nc = re->sblkcap ? re->sblkcap * 2 : 8;
            if ((size_t)nc > SIZE_MAX / sizeof(*re->sblk)) {
                re->oom = 1;
                return &re->dummy;
            }
            nfa_state_t **nb = (nfa_state_t **)NC_REGEXP_REALLOC(
                re->sblk, (size_t)nc * sizeof(*nb));
            if (!nb) { re->oom = 1; return &re->dummy; }
            re->sblk = nb; re->sblkcap = nc;
        }
        nfa_state_t *blk = (nfa_state_t *)NC_REGEXP_CALLOC(
            NFA_BLK, sizeof(nfa_state_t));
        if (!blk) { re->oom = 1; return &re->dummy; }
        re->sblk[re->nsblk++] = blk;
    }
    nfa_state_t *s = state_at(re, re->nstates);
    s->type = type;
    s->id = re->nstates++;
    return s;
}

static charclass_t *new_class(neverc_regexp_t *re) {
    if (re->nclasses == INT_MAX) { re->oom = 1; return &re->dummy_class; }
    if (re->nclasses % NFA_BLK == 0) {
        if (re->ncblk == re->cblkcap) {
            if (re->cblkcap > INT_MAX / 2) { re->oom = 1; return &re->dummy_class; }
            int nc = re->cblkcap ? re->cblkcap * 2 : 4;
            if ((size_t)nc > SIZE_MAX / sizeof(*re->cblk)) {
                re->oom = 1;
                return &re->dummy_class;
            }
            charclass_t **nb = (charclass_t **)NC_REGEXP_REALLOC(
                re->cblk, (size_t)nc * sizeof(*nb));
            if (!nb) { re->oom = 1; return &re->dummy_class; }
            re->cblk = nb; re->cblkcap = nc;
        }
        charclass_t *blk = (charclass_t *)NC_REGEXP_CALLOC(
            NFA_BLK, sizeof(charclass_t));
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
    if (!cc) return 0;
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
    int posix;
} parser_t;

static frag_t parse_expr(parser_t *par);
static frag_t parse_concat(parser_t *par);
static frag_t parse_repeat(parser_t *par);
static frag_t parse_atom(parser_t *par);

static int is_meta(char c) {
    return c == '|' || c == '*' || c == '+' || c == '?' ||
           c == '(' || c == ')' || c == '[';
}

static int decode_char_esc(char esc, int *out) {
    switch (esc) {
    case 'a': *out = '\a'; return 1;
    case 'b': *out = '\b'; return 1;   /* backspace; `\b` outside a class is a bound */
    case 'n': *out = '\n'; return 1;
    case 't': *out = '\t'; return 1;
    case 'r': *out = '\r'; return 1;
    case 'f': *out = '\f'; return 1;
    case 'v': *out = '\v'; return 1;
    default: return 0;
    }
}

static int is_word_byte(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_';
}

static int word_bound_at(const char *s, size_t slen, size_t pos) {
    int prev = (pos > 0) && is_word_byte((unsigned char)s[pos - 1]);
    int cur = (pos < slen) && is_word_byte((unsigned char)s[pos]);
    return prev != cur;
}

static int valid_cap_name(const char *s, int nlen) {
    if (!s || nlen < 1) return 0;
    for (int i = 0; i < nlen; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

static int set_group_name(neverc_regexp_t *re, int cap, const char *name, int nlen) {
    if (!re || cap <= 0) return 1;
    if (re->gnames_cap <= cap) {
        if (re->gnames_cap > INT_MAX / 2) { re->oom = 1; return 0; }
        int nc = re->gnames_cap ? re->gnames_cap * 2 : 4;
        while (nc <= cap) {
            if (nc > INT_MAX / 2) { re->oom = 1; return 0; }
            nc *= 2;
        }
        if ((size_t)nc > SIZE_MAX / sizeof(*re->gnames)) { re->oom = 1; return 0; }
        char **nb = (char **)NC_REGEXP_REALLOC(re->gnames, (size_t)nc * sizeof(*nb));
        if (!nb) { re->oom = 1; return 0; }
        for (int i = re->gnames_cap; i < nc; i++) nb[i] = NULL;
        re->gnames = nb;
        re->gnames_cap = nc;
    }
    if (nlen > 0) {
        char *copy = (char *)NC_REGEXP_MALLOC((size_t)nlen + 1U);
        if (!copy) { re->oom = 1; return 0; }
        memcpy(copy, name, (size_t)nlen);
        copy[nlen] = '\0';
        free(re->gnames[cap]);
        re->gnames[cap] = copy;
    }
    return 1;
}

static int hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

#ifndef NCI_RE_MAX_RUNE
#define NCI_RE_MAX_RUNE 0x10FFFF
#endif

/* Go regexp/syntax nextRune: invalid UTF-8 in the pattern is ErrInvalidUTF8. */
static int utf8_decode(const unsigned char *s, size_t n, int *rune) {
    if (n < 1) return 0;
    unsigned c = s[0];
    if (c < 0x80) {
        *rune = (int)c;
        return 1;
    }
    if (c < 0xC2 || c >= 0xF5) return 0;
    if (c < 0xE0) {
        if (n < 2 || (s[1] & 0xC0) != 0x80) return 0;
        *rune = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if (c < 0xF0) {
        if (n < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        int r = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        if (r < 0x800 || (r >= 0xD800 && r <= 0xDFFF)) return 0;
        *rune = r;
        return 3;
    }
    if (n < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 ||
        (s[3] & 0xC0) != 0x80)
        return 0;
    int r = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
            ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    if (r < 0x10000 || r > NCI_RE_MAX_RUNE) return 0;
    *rune = r;
    return 4;
}

static int utf8_valid_cstr(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    size_t n = strlen(s), i = 0;
    while (i < n) {
        int r, k = utf8_decode(p + i, n - i, &r);
        if (k < 1) return 0;
        i += (size_t)k;
    }
    return 1;
}

/* Go parseInt: a leading zero followed by another digit is not an integer, so
 * `{01}` / `{0,01}` are literals rather than repeats. */
static int brace_repeat_leading_zeros(const char *s) {
    if (!s || *s != '{') return 0;
    s++;
    if (*s == '0' && s[1] >= '0' && s[1] <= '9') return 1;
    while (*s >= '0' && *s <= '9') s++;
    if (*s == ',') {
        s++;
        if (*s == '0' && s[1] >= '0' && s[1] <= '9') return 1;
    }
    return 0;
}

static int cap_slot_count(int ngroups, int *nslots) {
    if (ngroups < 0 || ngroups > (INT_MAX / 2) - 1) return 0;
    *nslots = 2 * (ngroups + 1);
    return 1;
}

/* After consuming `\x`. NeverC: `\xHH` is a raw byte; `\x{H+}` is a rune.
 * Go/RE2 treat both as runes. *braced is set for the `{H+}` form. */
static int parse_hex_escape(parser_t *par, int *out, int *braced) {
    if (braced) *braced = 0;
    if (*par->p == '{') {
        if (braced) *braced = 1;
        par->p++;
        int v = 0, n = 0;
        while (*par->p && *par->p != '}') {
            int d = hex_digit((unsigned char)*par->p);
            if (d < 0) {
                par->err = "invalid escape sequence";
                return 0;
            }
            if (v > (NCI_RE_MAX_RUNE - d) / 16) {
                par->err = "invalid escape sequence";
                return 0;
            }
            v = v * 16 + d;
            n++;
            par->p++;
        }
        if (*par->p != '}' || n == 0) {
            par->err = "invalid escape sequence";
            return 0;
        }
        par->p++;
        *out = v;
        return 1;
    }
    int x = hex_digit((unsigned char)*par->p);
    if (x < 0 || !*par->p) {
        par->err = "invalid escape sequence";
        return 0;
    }
    par->p++;
    int y = hex_digit((unsigned char)*par->p);
    if (y < 0 || !*par->p) {
        par->err = "invalid escape sequence";
        return 0;
    }
    par->p++;
    *out = x * 16 + y;
    return 1;
}

static int rune_utf8(int r, unsigned char out[4]) {
    if (r < 0 || r > NCI_RE_MAX_RUNE) return 0;
    if (r >= 0xD800 && r <= 0xDFFF) return 0;
    if (r < 0x80) {
        out[0] = (unsigned char)r;
        return 1;
    }
    if (r < 0x800) {
        out[0] = (unsigned char)(0xC0 | (r >> 6));
        out[1] = (unsigned char)(0x80 | (r & 0x3F));
        return 2;
    }
    if (r < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (r >> 12));
        out[1] = (unsigned char)(0x80 | ((r >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (r & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | (r >> 18));
    out[1] = (unsigned char)(0x80 | ((r >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((r >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (r & 0x3F));
    return 4;
}

static frag_t mk_concat(frag_t a, frag_t b);

static frag_t frag_rune(neverc_regexp_t *re, int r) {
    unsigned char b[4];
    int n = rune_utf8(r, b);
    if (n <= 0) {
        frag_t z = { NULL, NULL };
        return z;
    }
    frag_t rfrag = { NULL, NULL };
    for (int i = 0; i < n; i++) {
        nfa_state_t *s = new_state(re, NFA_CHAR);
        nfa_state_t *e = new_state(re, NFA_MATCH);
        s->ch = b[i];
        s->out1 = e;
        rfrag = mk_concat(rfrag, frag(s, e));
    }
    return rfrag;
}

static frag_t wrap_cap(neverc_regexp_t *re, int cap, frag_t inner) {
    nfa_state_t *open = new_state(re, NFA_CAP_OPEN);
    nfa_state_t *close = new_state(re, NFA_CAP_CLOSE);
    nfa_state_t *end = new_state(re, NFA_MATCH);
    open->ch = cap;
    close->ch = cap;
    if (inner.start) {
        open->out1 = inner.start;
        if (inner.end) inner.end->out1 = close;
    } else {
        open->out1 = close;
    }
    close->out1 = end;
    return frag(open, end);
}

static int is_perl_class_escape(char esc) {
    return esc == 'd' || esc == 'D' || esc == 'w' || esc == 'W' ||
           esc == 's' || esc == 'S';
}

/* Go/RE2: letter and digit escapes that are not a known sequence are errors
 * (no backreferences, no silent `\q` -> `q`). Metacharacters may still be
 * escaped. */
static int is_unknown_ident_escape(int esc) {
    return (esc >= '0' && esc <= '9') ||
           (esc >= 'A' && esc <= 'Z') || (esc >= 'a' && esc <= 'z');
}

/* RE2 doc/syntax.txt: "$ at end of text (like \z not \Z)". Perl's \Z, which
 * also matches before a final newline, is deliberately not supported, and the
 * parser already maps both `$` and `\z` onto END_TEXT. */
static int anchor_end_at(const char *s, size_t slen, size_t pos) {
    (void)s;
    return pos == slen;
}

static void cc_set_ws(charclass_t *cc) {
    cc_set(cc, ' ');
    cc_set(cc, '\t');
    cc_set(cc, '\n');
    cc_set(cc, '\r');
    cc_set(cc, '\f');
}

/* POSIX/Go [:space:] is [\t\n\v\f\r ]. Perl \s dropped VT (issue 22057). */
static void cc_set_posix_space(charclass_t *cc) {
    cc_set_ws(cc);
    cc_set(cc, '\v');
}

static void cc_set_word(charclass_t *cc) {
    for (int i = 'a'; i <= 'z'; i++) cc_set(cc, i);
    for (int i = 'A'; i <= 'Z'; i++) cc_set(cc, i);
    for (int i = '0'; i <= '9'; i++) cc_set(cc, i);
    cc_set(cc, '_');
}

static int cc_set_posix(charclass_t *cc, const char *name, int nlen) {
    if (!cc || !name || nlen <= 0) return 0;
    int complement = 0;
    if (nlen > 1 && name[0] == '^') {
        complement = 1;
        name++;
        nlen--;
    }
    charclass_t local;
    charclass_t *dst = cc;
    if (complement) {
        memset(&local, 0, sizeof(local));
        dst = &local;
    }
#define NCI_RE_POSIX(s) (nlen == (int)(sizeof(s) - 1) && memcmp(name, s, (size_t)nlen) == 0)
    int ok = 0;
    if (NCI_RE_POSIX("alnum")) {
        for (int i = '0'; i <= '9'; i++) cc_set(dst, i);
        for (int i = 'A'; i <= 'Z'; i++) cc_set(dst, i);
        for (int i = 'a'; i <= 'z'; i++) cc_set(dst, i);
        ok = 1;
    } else if (NCI_RE_POSIX("alpha")) {
        for (int i = 'A'; i <= 'Z'; i++) cc_set(dst, i);
        for (int i = 'a'; i <= 'z'; i++) cc_set(dst, i);
        ok = 1;
    } else if (NCI_RE_POSIX("ascii")) {
        for (int i = 0; i < 128; i++) cc_set(dst, i);
        ok = 1;
    } else if (NCI_RE_POSIX("blank")) {
        cc_set(dst, ' ');
        cc_set(dst, '\t');
        ok = 1;
    } else if (NCI_RE_POSIX("cntrl")) {
        for (int i = 0; i < 32; i++) cc_set(dst, i);
        cc_set(dst, 127);
        ok = 1;
    } else if (NCI_RE_POSIX("digit")) {
        for (int i = '0'; i <= '9'; i++) cc_set(dst, i);
        ok = 1;
    } else if (NCI_RE_POSIX("graph")) {
        for (int i = 0x21; i <= 0x7E; i++) cc_set(dst, i);
        ok = 1;
    } else if (NCI_RE_POSIX("lower")) {
        for (int i = 'a'; i <= 'z'; i++) cc_set(dst, i);
        ok = 1;
    } else if (NCI_RE_POSIX("print")) {
        for (int i = 0x20; i <= 0x7E; i++) cc_set(dst, i);
        ok = 1;
    } else if (NCI_RE_POSIX("punct")) {
        const char *p = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
        while (*p) cc_set(dst, (unsigned char)*p++);
        ok = 1;
    } else if (NCI_RE_POSIX("space")) {
        cc_set_posix_space(dst);
        ok = 1;
    } else if (NCI_RE_POSIX("upper")) {
        for (int i = 'A'; i <= 'Z'; i++) cc_set(dst, i);
        ok = 1;
    } else if (NCI_RE_POSIX("word")) {
        cc_set_word(dst);
        ok = 1;
    } else if (NCI_RE_POSIX("xdigit")) {
        for (int i = '0'; i <= '9'; i++) cc_set(dst, i);
        for (int i = 'A'; i <= 'F'; i++) cc_set(dst, i);
        for (int i = 'a'; i <= 'f'; i++) cc_set(dst, i);
        ok = 1;
    }
#undef NCI_RE_POSIX
    if (!ok) return 0;
    if (complement) {
        for (int i = 0; i < 256; i++)
            if (!cc_test(&local, i)) cc_set(cc, i);
    }
    return 1;
}

static int cc_bitmap_empty(const charclass_t *cc) {
    if (!cc) return 1;
    for (int i = 0; i < 32; i++)
        if (cc->bitmap[i]) return 0;
    return 1;
}

static frag_t mk_alt(neverc_regexp_t *re, frag_t a, frag_t b) {
    if (!a.start) return b;
    if (!b.start) return a;
    nfa_state_t *split = new_state(re, NFA_SPLIT);
    nfa_state_t *end = new_state(re, NFA_MATCH);
    split->out1 = a.start;
    split->out2 = b.start;
    if (a.end) a.end->out1 = end;
    if (b.end) b.end->out1 = end;
    return frag(split, end);
}

#define NCI_RE_CLASS_RUNE_CAP 256

static int class_read_utf8_atom(parser_t *par, int *out) {
    const unsigned char *s = (const unsigned char *)par->p;
    size_t n = 0;
    int r, k;
    while (s[n]) n++;
    k = utf8_decode(s, n, &r);
    if (k < 1) {
        par->err = "invalid UTF-8";
        return 0;
    }
    par->p += k;
    *out = r;
    return 1;
}

static int class_add_rune(parser_t *par, frag_t **extras, int *nextras,
                          int *extras_cap, int r) {
    if (*nextras >= NCI_RE_CLASS_RUNE_CAP) {
        par->err = "character class too large";
        return 0;
    }
    if (*nextras == *extras_cap) {
        int nc = *extras_cap ? *extras_cap * 2 : 8;
        if (nc > NCI_RE_CLASS_RUNE_CAP) nc = NCI_RE_CLASS_RUNE_CAP;
        frag_t *nb = (frag_t *)NC_REGEXP_REALLOC(*extras, (size_t)nc * sizeof(*nb));
        if (!nb) {
            par->err = "out of memory";
            par->re->oom = 1;
            return 0;
        }
        *extras = nb;
        *extras_cap = nc;
    }
    frag_t rf = frag_rune(par->re, r);
    if (!rf.start) {
        par->err = "invalid escape sequence";
        return 0;
    }
    (*extras)[(*nextras)++] = rf;
    return 1;
}

static frag_t parse_atom(parser_t *par) {
    if (par->err) return frag(NULL, NULL);
    char c = *par->p;

    if (c == '(') {
        par->p++;
        int capturing = 1;
        int cap = 0;
        if (par->posix && par->p[0] == '?') {
            par->err = "invalid POSIX syntax";
            return frag(NULL, NULL);
        }
        if (par->p[0] == '?' && par->p[1] == ':') {
            par->p += 2;
            capturing = 0;
        } else if ((par->p[0] == '?' && par->p[1] == 'P' && par->p[2] == '<') ||
                   (par->p[0] == '?' && par->p[1] == '<') ||
                   (par->p[0] == '?' && par->p[1] == '\'')) {
            char close_ch = '>';
            if (par->p[0] == '?' && par->p[1] == 'P' && par->p[2] == '<')
                par->p += 3;
            else {
                close_ch = (par->p[1] == '\'') ? '\'' : '>';
                par->p += 2;
            }
            const char *ns = par->p;
            while (*par->p && *par->p != close_ch) par->p++;
            if (*par->p != close_ch) {
                par->err = "invalid named capture";
                return frag(NULL, NULL);
            }
            int nlen = (int)(par->p - ns);
            par->p++;
            if (!valid_cap_name(ns, nlen)) {
                par->err = "invalid named capture";
                return frag(NULL, NULL);
            }
            if (par->re->ngroups == INT_MAX) {
                par->err = "too many capturing groups";
                return frag(NULL, NULL);
            }
            cap = ++par->re->ngroups;
            if (!set_group_name(par->re, cap, ns, nlen)) {
                par->err = "out of memory";
                return frag(NULL, NULL);
            }
        } else if (par->p[0] == '?') {
            par->err = "invalid or unsupported Perl syntax";
            return frag(NULL, NULL);
        } else if (capturing) {
            if (par->re->ngroups == INT_MAX) {
                par->err = "too many capturing groups";
                return frag(NULL, NULL);
            }
            cap = ++par->re->ngroups;
        }
        if (++par->depth > NCI_REGEXP_MAX_DEPTH) {
            par->err = "expression nested too deeply";
            par->depth--;
            return frag(NULL, NULL);
        }
        frag_t f = parse_expr(par);
        par->depth--;
        if (*par->p == ')') par->p++;
        else par->err = "missing )";
        if (par->err || !capturing) return f;
        return wrap_cap(par->re, cap, f);
    }

    if (c == '[') {
        par->p++;
        charclass_t *cc = new_class(par->re);
        if (*par->p == '^') { cc->negated = 1; par->p++; }
        int entries = 0;
        int first = 1;
        int nextras = 0, extras_cap = 0;
        frag_t *extras = NULL;
        frag_t f = { NULL, NULL };
        /* ']' is literal as the first class byte (or first after '^'), matching
         * POSIX/Go: []] and [^]] are valid. */
        while (*par->p && (*par->p != ']' || first)) {
            first = 0;
            if (*par->p == '[' && par->p[1] == ':') {
                const char *ns = par->p + 2;
                const char *q = ns;
                while (*q && !(*q == ':' && q[1] == ']')) q++;
                if (*q) {
                    if (!cc_set_posix(cc, ns, (int)(q - ns))) {
                        par->err = "invalid POSIX class";
                        goto class_fail;
                    }
                    par->p = q + 2;
                    entries++;
                    continue;
                }
                /* Go: missing ":]" is not a POSIX class; '[' is literal. */
            }
            int lo;
            int lo_utf8 = 0;
            int lo_braced = 0;
            if ((unsigned char)*par->p >= 0x80) {
                if (!class_read_utf8_atom(par, &lo)) goto class_fail;
                lo_utf8 = 1;
            } else {
                lo = (uint8_t)*par->p++;
            }
            if (lo == '\\' && *par->p) {
                char esc = *par->p++;
                if (par->posix &&
                    (esc == 'b' || is_perl_class_escape(esc))) {
                    par->err = "invalid POSIX escape";
                    goto class_fail;
                }
                if (esc == 'd') {
                    for (int i = '0'; i <= '9'; i++) cc_set(cc, i);
                    entries++; continue;
                }
                if (esc == 'D') {
                    for (int i = 0; i < 256; i++)
                        if (i < '0' || i > '9') cc_set(cc, i);
                    entries++; continue;
                }
                if (esc == 'w') { cc_set_word(cc); entries++; continue; }
                if (esc == 'W') {
                    charclass_t word;
                    memset(&word, 0, sizeof(word));
                    cc_set_word(&word);
                    for (int i = 0; i < 256; i++)
                        if (!cc_test(&word, i)) cc_set(cc, i);
                    entries++; continue;
                }
                if (esc == 's') { cc_set_ws(cc); entries++; continue; }
                if (esc == 'S') {
                    charclass_t ws;
                    memset(&ws, 0, sizeof(ws));
                    cc_set_ws(&ws);
                    for (int i = 0; i < 256; i++)
                        if (!cc_test(&ws, i)) cc_set(cc, i);
                    entries++; continue;
                }
                if (esc == 'x') {
                    if (!parse_hex_escape(par, &lo, &lo_braced)) goto class_fail;
                    if (lo_braced && lo > 127) {
                        if (cc->negated) {
                            par->err = "invalid escape sequence";
                            goto class_fail;
                        }
                        if (*par->p == '-' && par->p[1] && par->p[1] != ']') {
                            par->p++;
                            int hi, hi_braced = 0;
                            if (*par->p == '\\' && par->p[1] == 'x') {
                                par->p += 2;
                                if (!parse_hex_escape(par, &hi, &hi_braced))
                                    goto class_fail;
                            } else {
                                par->err = "invalid character class range";
                                goto class_fail;
                            }
                            if (hi < lo) {
                                par->err = "invalid character class range";
                                goto class_fail;
                            }
                            if (hi - lo >= NCI_RE_CLASS_RUNE_CAP - nextras) {
                                par->err = "character class too large";
                                goto class_fail;
                            }
                            for (int r = lo; r <= hi; r++) {
                                if (r < 128) cc_set(cc, r);
                                else if (!class_add_rune(par, &extras, &nextras,
                                                         &extras_cap, r))
                                    goto class_fail;
                            }
                            entries++;
                            continue;
                        }
                        if (!class_add_rune(par, &extras, &nextras, &extras_cap, lo))
                            goto class_fail;
                        entries++;
                        continue;
                    }
                    if (lo < 0 || lo > 255) {
                        par->err = "invalid escape sequence";
                        goto class_fail;
                    }
                } else {
                    int decoded;
                    if (decode_char_esc(esc, &decoded)) lo = decoded;
                    else if (is_unknown_ident_escape((unsigned char)esc)) {
                        par->err = "invalid escape sequence";
                        goto class_fail;
                    } else {
                        lo = (uint8_t)esc;
                    }
                }
            }
            if (*par->p == '-' && par->p[1] && par->p[1] != ']') {
                par->p++;
                int hi = 0, hi_braced = 0, hi_utf8 = 0;
                if (*par->p == '\\' && par->p[1]) {
                    par->p++;
                    char esc = *par->p++;
                    if (par->posix &&
                        (esc == 'b' || is_perl_class_escape(esc))) {
                        par->err = "invalid POSIX escape";
                        goto class_fail;
                    }
                    if (is_perl_class_escape(esc)) {
                        par->err = "invalid character class range";
                        goto class_fail;
                    }
                    if (esc == 'x') {
                        if (!parse_hex_escape(par, &hi, &hi_braced)) goto class_fail;
                        if (hi_braced && hi > 127) {
                            if (cc->negated) {
                                par->err = "invalid escape sequence";
                                goto class_fail;
                            }
                            if (hi < lo) {
                                par->err = "invalid character class range";
                                goto class_fail;
                            }
                            if (hi - lo >= NCI_RE_CLASS_RUNE_CAP - nextras) {
                                par->err = "character class too large";
                                goto class_fail;
                            }
                            for (int r = lo; r <= hi; r++) {
                                if (r < 128) cc_set(cc, r);
                                else if (!class_add_rune(par, &extras, &nextras,
                                                         &extras_cap, r))
                                    goto class_fail;
                            }
                            entries++;
                            continue;
                        }
                        if (hi < 0 || hi > 255) {
                            par->err = "invalid escape sequence";
                            goto class_fail;
                        }
                    } else {
                        int decoded;
                        if (decode_char_esc(esc, &decoded)) hi = decoded;
                        else if (is_unknown_ident_escape((unsigned char)esc)) {
                            par->err = "invalid escape sequence";
                            goto class_fail;
                        } else {
                            hi = (uint8_t)esc;
                        }
                    }
                } else if ((unsigned char)*par->p >= 0x80) {
                    if (!class_read_utf8_atom(par, &hi)) goto class_fail;
                    hi_utf8 = 1;
                } else {
                    hi = (uint8_t)*par->p++;
                }
                if (lo_utf8 || hi_utf8) {
                    if (cc->negated) {
                        par->err = "invalid escape sequence";
                        goto class_fail;
                    }
                    if (hi < lo) {
                        par->err = "invalid character class range";
                        goto class_fail;
                    }
                    if (hi - lo >= NCI_RE_CLASS_RUNE_CAP - nextras) {
                        par->err = "character class too large";
                        goto class_fail;
                    }
                    for (int r = lo; r <= hi; r++) {
                        if (r < 128) cc_set(cc, r);
                        else if (!class_add_rune(par, &extras, &nextras,
                                                 &extras_cap, r))
                            goto class_fail;
                    }
                    entries++;
                    continue;
                }
                if (hi < lo) {
                    par->err = "invalid character class range";
                    goto class_fail;
                }
                for (int i = lo; i <= hi; i++) cc_set(cc, i);
            } else if (lo_utf8) {
                if (cc->negated) {
                    par->err = "invalid escape sequence";
                    goto class_fail;
                }
                if (!class_add_rune(par, &extras, &nextras, &extras_cap, lo))
                    goto class_fail;
            } else {
                cc_set(cc, lo);
            }
            entries++;
        }
        if (*par->p != ']') {
            par->err = "missing ]";
            goto class_fail;
        }
        par->p++;
        if (entries == 0) {
            par->err = "empty character class";
            goto class_fail;
        }
        if (!cc_bitmap_empty(cc) || cc->negated || nextras == 0) {
            nfa_state_t *s = new_state(par->re, NFA_CLASS);
            s->cls = cc;
            nfa_state_t *e = new_state(par->re, NFA_MATCH);
            s->out1 = e;
            f = frag(s, e);
        }
        for (int i = 0; i < nextras; i++)
            f = mk_alt(par->re, f, extras[i]);
        free(extras);
        return f;
    class_fail:
        free(extras);
        return frag(NULL, NULL);
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
        if (par->posix &&
            (esc == 'A' || esc == 'b' || esc == 'B' || esc == 'z' ||
             is_perl_class_escape(esc))) {
            par->err = "invalid POSIX escape";
            return frag(NULL, NULL);
        }
        if (esc == 'x') {
            int r, braced = 0;
            if (!parse_hex_escape(par, &r, &braced)) return frag(NULL, NULL);
            if (braced) {
                frag_t hf = frag_rune(par->re, r);
                if (!hf.start) {
                    par->err = "invalid escape sequence";
                    return frag(NULL, NULL);
                }
                return hf;
            }
            /* `\xHH` is a single raw byte; `\x{H+}` above is a UTF-8 rune.
             * Go/RE2 treat both as runes; this split is intentional. */
            nfa_state_t *s = new_state(par->re, NFA_CHAR);
            nfa_state_t *e = new_state(par->re, NFA_MATCH);
            s->ch = r & 0xFF;
            s->out1 = e;
            return frag(s, e);
        }
        if (esc == 'b' || esc == 'B' || esc == 'A' || esc == 'z') {
            int type = NFA_ANCHOR_START;
            if (esc == 'b') type = NFA_WORD_BOUND;
            else if (esc == 'B') type = NFA_NO_WORD_BOUND;
            else if (esc == 'z') type = NFA_ANCHOR_END;
            nfa_state_t *s = new_state(par->re, type);
            nfa_state_t *e = new_state(par->re, NFA_MATCH);
            if (esc == 'z') s->ch = 1;          /* `\z`: end of text only */
            s->out1 = e;
            return frag(s, e);
        }
        charclass_t *cc = new_class(par->re);
        if (esc == 'd') { for (int i = '0'; i <= '9'; i++) cc_set(cc, i); }
        else if (esc == 'D') { for (int i = '0'; i <= '9'; i++) cc_set(cc, i); cc->negated = 1; }
        else if (esc == 'w') { cc_set_word(cc); }
        else if (esc == 'W') { cc_set_word(cc); cc->negated = 1; }
        else if (esc == 's') { cc_set_ws(cc); }
        else if (esc == 'S') { cc_set_ws(cc); cc->negated = 1; }
        else {
            int decoded;
            if (decode_char_esc(esc, &decoded)) {
                nfa_state_t *s = new_state(par->re, NFA_CHAR);
                s->ch = decoded;
                nfa_state_t *e = new_state(par->re, NFA_MATCH);
                s->out1 = e;
                return frag(s, e);
            }
            /* Metacharacters may be escaped; letter/digit unknowns are errors
             * (Go/RE2: no backreferences, no silent `\q` -> `q`). */
            if (is_unknown_ident_escape((unsigned char)esc)) {
                par->err = "invalid escape sequence";
                return frag(NULL, NULL);
            }
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

    if (c == '\\') {
        par->err = "trailing backslash";
        return frag(NULL, NULL);
    }

    if (c && !is_meta(c) && c != ')') {
        /* Quantifiers bind to a rune, not the last UTF-8 byte: 中{2} is 中中. */
        if ((unsigned char)c >= 0x80) {
            int r;
            size_t n = 0;
            while (par->p[n]) n++;
            int k = utf8_decode((const unsigned char *)par->p, n, &r);
            if (k < 1) {
                par->err = "invalid UTF-8";
                return frag(NULL, NULL);
            }
            par->p += k;
            frag_t rf = frag_rune(par->re, r);
            if (!rf.start) {
                par->err = "invalid UTF-8";
                return frag(NULL, NULL);
            }
            return rf;
        }
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
    frag_t z = { NULL, NULL };
    if (!f.start || !f.end || lo < 0 || hi <= lo ||
        f.start->id < lo || f.start->id >= hi ||
        f.end->id < lo || f.end->id >= hi)
        return z;
    int count = hi - lo, newbase = re->nstates;
    for (int i = 0; i < count; i++) {
        nfa_state_t *src = state_at(re, lo + i);
        int ch = src->ch;
        charclass_t *cls = src->cls;         /* classes are read-only: share */
        nfa_state_t *dst = new_state(re, src->type);
        if (re->oom) return z;
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
    frag_t *cp = (frag_t *)NC_REGEXP_CALLOC((size_t)total, sizeof(frag_t));
    if (!cp) { re->oom = 1; return f; }
    cp[0] = f;
    for (int i = 1; i < total; i++) {        /* clone before any gluing mutates f */
        cp[i] = clone_range(re, range_lo, range_hi, f);
        if (re->oom || !cp[i].start) { re->oom = 1; free(cp); return f; }
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

static int frag_ok(parser_t *par, frag_t f) {
    return f.start && f.start != &par->re->dummy && !par->err && !par->re->oom;
}

static frag_t parse_repeat(parser_t *par) {
    int atom_base = par->re->nstates;
    frag_t f = parse_atom(par);
    if (!frag_ok(par, f)) {
        if (par->re->oom && !par->err) par->err = "out of memory";
        return f;
    }

    int repeated = 0;
    while (*par->p == '*' || *par->p == '+' || *par->p == '?' || *par->p == '{') {
        char op = *par->p;
        if (op == '{') {
            const char *save = par->p;
            if (brace_repeat_leading_zeros(par->p)) {
                /* Leave '{' for the next atom, like Go regexp/syntax. */
                par->p = save;
                break;
            }
            par->p++;
            int lo = 0, hi, have = 0, too_large = 0;
            while (*par->p >= '0' && *par->p <= '9') {
                int digit = *par->p++ - '0';
                have = 1;
                if (!too_large) {
                    if (lo > (NFA_MAX_REPEAT - digit) / 10) too_large = 1;
                    else lo = lo * 10 + digit;
                }
            }
            hi = lo;
            if (*par->p == ',') {
                par->p++;
                if (*par->p >= '0' && *par->p <= '9') {
                    hi = 0;
                    while (*par->p >= '0' && *par->p <= '9') {
                        int digit = *par->p++ - '0';
                        if (!too_large) {
                            if (hi > (NFA_MAX_REPEAT - digit) / 10) too_large = 1;
                            else hi = hi * 10 + digit;
                        }
                    }
                } else {
                    hi = -1;                 /* {n,} -> n or more */
                }
            }
            if (!have) {   /* `{` not followed by a digit: literal, like Go */
                par->p = save;
                break;
            }
            if (*par->p != '}') {
                /* Go parseRepeat: a malformed `{` is a literal, not an error. */
                par->p = save;
                break;
            }
            par->p++;                        /* consume '}' */
            if (repeated) {
                par->err = "invalid nested repetition operator";
                return f;
            }
            if (too_large) {
                par->err = "repeat count too large";
                return f;
            }
            if (hi != -1 && hi < lo) {
                par->err = "invalid repeat range";
                return f;
            }
            {
                int unit = par->re->nstates - atom_base;
                int copies = (hi == -1) ? (lo == 0 ? 1 : lo + 1) : hi;
                if (unit > 0 && copies > 0 &&
                    copies > (NFA_MAX_STATES - par->re->nstates) / unit) {
                    par->err = "pattern too large";
                    return f;
                }
            }
            f = expand_repeat(par->re, f, atom_base, par->re->nstates, lo, hi);
            if (par->re->oom) { par->err = "out of memory"; return f; }
            repeated = 1;
            /* Go: a single '?' after a quantifier is the non-greedy flag. */
            if (*par->p == '?') {
                if (par->posix) {
                    par->err = "invalid POSIX repetition";
                    return f;
                }
                par->p++;
            }
            continue;
        }
        if (repeated) {
            par->err = "invalid nested repetition operator";
            return f;
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
        repeated = 1;
        if (*par->p == '?') {
            if (par->posix) {
                par->err = "invalid POSIX repetition";
                return f;
            }
            par->p++;
        }
    }
    return f;
}

static frag_t parse_concat(parser_t *par) {
    frag_t result = { NULL, NULL };
    while (*par->p && *par->p != '|' && *par->p != ')' && !par->err) {
        frag_t f = parse_repeat(par);
        if (!frag_ok(par, f)) break;
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

static neverc_regexp_t *regexp_compile(const char *pattern, const char **errp,
                                       int posix) {
    if (!pattern) {
        if (errp) *errp = "invalid pattern";
        return NULL;
    }
    if (!utf8_valid_cstr(pattern)) {
        if (errp) *errp = "invalid UTF-8";
        return NULL;
    }
    neverc_regexp_t *re = (neverc_regexp_t *)NC_REGEXP_CALLOC(
        1, sizeof(neverc_regexp_t));
    if (!re) { if (errp) *errp = "out of memory"; return NULL; }
    re->dummy.id = -1;                          /* never collide with state 0 */

    re->posix = posix;
    parser_t par = { pattern, re, NULL, 0, posix };
    frag_t f = parse_expr(&par);

    if (!par.err && *par.p != '\0') par.err = "unexpected character";

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

neverc_regexp_t *neverc_regexp_compile(const char *pattern, const char **errp) {
    return regexp_compile(pattern, errp, 0);
}

void neverc_regexp_free(neverc_regexp_t *re) {
    if (!re) return;
    for (int i = 0; i < re->nsblk; i++) free(re->sblk[i]);
    free(re->sblk);
    for (int i = 0; i < re->ncblk; i++) free(re->cblk[i]);
    free(re->cblk);
    if (re->gnames) {
        for (int i = 0; i < re->gnames_cap; i++) free(re->gnames[i]);
        free(re->gnames);
    }
    free(re);
}

static int next_generation(int *gen, int *visited, int nstates) {
    if (*gen == INT_MAX) {
        memset(visited, 0, (size_t)nstates * sizeof(*visited));
        *gen = 1;
    } else {
        (*gen)++;
    }
    return *gen;
}

static int nfa_full_match(neverc_regexp_t *re, const char *s, size_t slen);

static void search_push(nfa_state_t **wst, int *wn, int *visited, int gen,
                        int nstates, nfa_state_t *s);

/* First-byte prefilter: collect the set of bytes that can begin a non-empty
 * match by walking the start state's epsilon-closure. Lets the search loop skip
 * positions whose byte can never start a match (the literal-prefix optimization
 * Go/RE2 use). Returns 0 (disable) when an anchor is reachable or the set would
 * be universal, so semantics are never affected — only impossible starts skip. */
static void fb_walk(nfa_state_t *s, int *vis, int gen, uint8_t fb[32],
                    int *full, int *unsafe, int nstates) {
    if (nstates < 1) { *unsafe = 1; return; }
    nfa_state_t **wst = (nfa_state_t **)NC_REGEXP_MALLOC(
        (size_t)nstates * sizeof(*wst));
    if (!wst) { *unsafe = 1; return; }
    int wn = 0;
    search_push(wst, &wn, vis, gen, nstates, s);
    while (wn > 0) {
        s = wst[--wn];
        switch (s->type) {
        case NFA_SPLIT:
            search_push(wst, &wn, vis, gen, nstates, s->out1);
            search_push(wst, &wn, vis, gen, nstates, s->out2);
            break;
        case NFA_MATCH:
            if (s->out1)
                search_push(wst, &wn, vis, gen, nstates, s->out1);
            break;
        case NFA_CAP_OPEN:
        case NFA_CAP_CLOSE:
        case NFA_WORD_BOUND:
        case NFA_NO_WORD_BOUND:
            search_push(wst, &wn, vis, gen, nstates, s->out1);
            break;
        case NFA_ANCHOR_START:
        case NFA_ANCHOR_END:
            *unsafe = 1;              /* let the normal scan handle anchors */
            break;
        case NFA_CHAR:
            fb[(s->ch & 0xff) >> 3] |= (uint8_t)(1u << (s->ch & 7));
            break;
        case NFA_ANY:
            *full = 1;                /* matches (almost) any byte */
            break;
        case NFA_CLASS:
            if (!s->cls) { *unsafe = 1; break; }
            for (int c = 0; c < 256; c++)
                if (cc_test(s->cls, c)) fb[c >> 3] |= (uint8_t)(1u << (c & 7));
            break;
        default:
            *unsafe = 1;
            break;
        }
    }
    free(wst);
}

static int compute_first_set(neverc_regexp_t *re, uint8_t fb[32]) {
    memset(fb, 0, 32);
    int nstates = re->nstates > 0 ? re->nstates : 1;
    int *vis = (int *)NC_REGEXP_CALLOC((size_t)nstates, sizeof(int));
    if (!vis) return 0;
    int full = 0, unsafe = 0;
    fb_walk(re->start, vis, 1, fb, &full, &unsafe, nstates);
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

int neverc_regexp_match(neverc_regexp_t *re, const char *s) {
    if (!re || !s) return 0;
    return nfa_full_match(re, s, strlen(s));
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
    int     nstates;
    nfa_state_t **wst;      /* explicit epsilon-closure stack (neverc frames are large) */
} search_ctx;

static int search_ctx_init(search_ctx *c, int nstates) {
    if (nstates < 1) nstates = 1;
    c->visited     = (int *)NC_REGEXP_CALLOC((size_t)nstates, sizeof(int));
    c->cur.st      = (nfa_state_t **)NC_REGEXP_MALLOC(
        (size_t)nstates * sizeof(nfa_state_t *));
    c->cur.start   = (size_t *)NC_REGEXP_MALLOC(
        (size_t)nstates * sizeof(size_t));
    c->next.st     = (nfa_state_t **)NC_REGEXP_MALLOC(
        (size_t)nstates * sizeof(nfa_state_t *));
    c->next.start  = (size_t *)NC_REGEXP_MALLOC(
        (size_t)nstates * sizeof(size_t));
    c->wst         = (nfa_state_t **)NC_REGEXP_MALLOC(
        (size_t)nstates * sizeof(nfa_state_t *));
    c->cur.n = c->next.n = 0;
    c->gen = 0;
    c->nstates = nstates;
    if (!c->visited || !c->cur.st || !c->cur.start || !c->next.st ||
        !c->next.start || !c->wst) {
        free(c->visited); free(c->cur.st); free(c->cur.start);
        free(c->next.st); free(c->next.start); free(c->wst);
        return -1;
    }
    return 0;
}

static void search_ctx_free(search_ctx *c) {
    free(c->visited);
    free(c->cur.st); free(c->cur.start);
    free(c->next.st); free(c->next.start);
    free(c->wst);
}

static void search_push(nfa_state_t **wst, int *wn, int *visited, int gen,
                        int nstates, nfa_state_t *s) {
    if (!s || s->id < 0 || s->id >= nstates || visited[s->id] == gen) return;
    if (*wn >= nstates) return;
    visited[s->id] = gen;
    wst[(*wn)++] = s;
}

/* Add state s (and its epsilon-closure) to tl, tagged with start offset. pos is
 * the current text offset, used to resolve anchors: ^ only crosses at offset 0,
 * $ at end of text or before a final newline. Dedup by state keeps the first
 * (leftmost) thread. Walk is iterative: neverc's aarch64 frames for a 9-arg
 * recursive closure overflowed the linux-arm64 stack on find_submatch. */
static void search_add(search_ctx *ctx, tlist_t *tl, nfa_state_t *s,
                       size_t start, size_t pos, const char *text, size_t slen) {
    int nstates = ctx->nstates;
    int *visited = ctx->visited;
    int gen = ctx->gen;
    nfa_state_t **wst = ctx->wst;
    int wn = 0;

    search_push(wst, &wn, visited, gen, nstates, s);
    while (wn > 0) {
        s = wst[--wn];
        if (s->type == NFA_SPLIT) {
            /* Stack is LIFO: push out2 first so out1 (left alternative) is
             * visited first. Dedup-by-state then keeps leftmost-first captures
             * when both sides of `|` reach the same join. */
            search_push(wst, &wn, visited, gen, nstates, s->out2);
            search_push(wst, &wn, visited, gen, nstates, s->out1);
            continue;
        }
        if (s->type == NFA_MATCH && s->out1 != NULL) {   /* epsilon link */
            search_push(wst, &wn, visited, gen, nstates, s->out1);
            continue;
        }
        if (s->type == NFA_CAP_OPEN || s->type == NFA_CAP_CLOSE) {
            search_push(wst, &wn, visited, gen, nstates, s->out1);
            continue;
        }
        if (s->type == NFA_ANCHOR_START) {
            if (pos == 0)
                search_push(wst, &wn, visited, gen, nstates, s->out1);
            continue;
        }
        if (s->type == NFA_ANCHOR_END) {
            int ok = s->ch ? (pos == slen) : anchor_end_at(text, slen, pos);
            if (ok)
                search_push(wst, &wn, visited, gen, nstates, s->out1);
            continue;
        }
        if (s->type == NFA_WORD_BOUND) {
            if (word_bound_at(text, slen, pos))
                search_push(wst, &wn, visited, gen, nstates, s->out1);
            continue;
        }
        if (s->type == NFA_NO_WORD_BOUND) {
            if (!word_bound_at(text, slen, pos))
                search_push(wst, &wn, visited, gen, nstates, s->out1);
            continue;
        }
        if (tl->n >= nstates) continue;
        tl->st[tl->n] = s;                               /* CHAR/ANY/CLASS/accept */
        tl->start[tl->n] = start;
        tl->n++;
    }
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

    int nstates = ctx->nstates;
    size_t pos = next_cand(s, slen, from, fb, use_skip, first_byte);
    next_generation(&ctx->gen, visited, nstates);
    clist.n = 0;
    if (pos < slen && (!use_skip || fb_has(fb, (unsigned char)s[pos])))
        search_add(ctx, &clist, re->start, pos, pos, s, slen);
    else if (!use_skip)                                  /* offset slen: anchors only */
        search_add(ctx, &clist, re->start, pos, pos, s, slen);

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
        next_generation(&ctx->gen, visited, nstates);
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
            if (ok)
                search_add(ctx, &nlist, st->out1, clist.start[j], pos + 1, s, slen);
        }

        size_t npos = pos + 1;
        if (!matched) {
            if (nlist.n == 0) {                          /* no in-flight: jump to next start */
                npos = next_cand(s, slen, npos, fb, use_skip, first_byte);
                next_generation(&ctx->gen, visited, nstates);
            }
            if (npos < slen && (!use_skip || fb_has(fb, (unsigned char)s[npos])))
                search_add(ctx, &nlist, re->start, npos, npos, s, slen);
        }

        tlist_t tmp = clist; clist = nlist; nlist = tmp; /* swap, reuse buffers */
        pos = npos;
        if (clist.n == 0 && !matched && pos >= slen) break;
    }

    ctx->cur = clist; ctx->next = nlist;                 /* persist swap for reuse */
    if (matched) { *mstart = b_start; *mend = b_end; return 1; }
    return 0;
}

/* Whole-string match. Uses the same epsilon-closure as find() (search_add),
 * including anchors, so match and search cannot diverge on ^ / $ / classes. */
static int nfa_full_match(neverc_regexp_t *re, const char *s, size_t slen) {
    search_ctx ctx;
    if (search_ctx_init(&ctx, re->nstates) != 0) return 0;
    tlist_t clist = ctx.cur, nlist = ctx.next;
    int *visited = ctx.visited;
    int nstates = ctx.nstates;
    next_generation(&ctx.gen, visited, nstates);
    clist.n = 0;
    search_add(&ctx, &clist, re->start, 0, 0, s, slen);

    size_t pos = 0;
    int matched = 0;
    for (;;) {
        for (int j = 0; j < clist.n; j++) {
            nfa_state_t *st = clist.st[j];
            if (st->type == NFA_MATCH && st->out1 == NULL && pos == slen)
                matched = 1;
        }
        if (matched || pos >= slen || clist.n == 0) break;

        int ch = (unsigned char)s[pos];
        next_generation(&ctx.gen, visited, nstates);
        nlist.n = 0;
        for (int j = 0; j < clist.n; j++) {
            nfa_state_t *st = clist.st[j];
            int ok = 0;
            switch (st->type) {
            case NFA_CHAR:  ok = (st->ch == ch); break;
            case NFA_ANY:   ok = (ch != '\n'); break;
            case NFA_CLASS: ok = cc_test(st->cls, ch); break;
            default: break;
            }
            if (ok)
                search_add(&ctx, &nlist, st->out1, 0, pos + 1, s, slen);
        }
        tlist_t tmp = clist; clist = nlist; nlist = tmp;
        pos++;
    }
    search_ctx_free(&ctx);
    return matched;
}

const char *neverc_regexp_find(neverc_regexp_t *re, const char *s,
                               size_t *match_len) {
    if (match_len) *match_len = 0;
    if (!re || !s) return NULL;
    size_t slen = strlen(s);
    search_ctx ctx;
    if (search_ctx_init(&ctx, re->nstates) != 0) return NULL;
    uint8_t fb[32];
    int use_skip = compute_first_set(re, fb);

    int fbyte = use_skip ? first_set_single(fb) : -1;
    const char *res = NULL;
    size_t ms, me;
    if (nfa_search(&ctx, re, s, slen, 0, fb, use_skip, fbyte, &ms, &me)) {
        if (match_len) *match_len = me - ms;
        res = s + ms;
    }
    search_ctx_free(&ctx);
    return res;
}

/* Iterative Pike-style epsilon walk. Capture slots live on an explicit work
 * stack so the closure does not recurse through CAP/SPLIT cycles. */
typedef struct {
    nfa_state_t **st;
    size_t       *capstore;
    int          *n;
    int           capn;
    int           nslots;
    const char   *text;
    size_t        slen;
    int          *visited;
    int           gen;
    nfa_state_t **wst;
    size_t       *wcap;
    int           wn;
    size_t       *scratch;
} cap_env_t;

static void cap_push(cap_env_t *e, nfa_state_t *s, const size_t *caps) {
    if (!s || s->id < 0 || s->id >= e->capn || e->visited[s->id] == e->gen)
        return;
    e->visited[s->id] = e->gen;
    if (e->wn >= e->capn) return;
    e->wst[e->wn] = s;
    memcpy(e->wcap + (size_t)e->wn * (size_t)e->nslots, caps,
           (size_t)e->nslots * sizeof(size_t));
    e->wn++;
}

static void cap_closure(cap_env_t *e, nfa_state_t *start, const size_t *init,
                        size_t pos) {
    e->wn = 0;
    cap_push(e, start, init);
    while (e->wn > 0) {
        e->wn--;
        nfa_state_t *s = e->wst[e->wn];
        memcpy(e->scratch, e->wcap + (size_t)e->wn * (size_t)e->nslots,
               (size_t)e->nslots * sizeof(size_t));
        if (s->type == NFA_SPLIT) {
            /* Same LIFO as search_add: left alternative must reach a shared
             * join first, or `(a)|a` / `a|(a)` record the wrong groups. */
            cap_push(e, s->out2, e->scratch);
            cap_push(e, s->out1, e->scratch);
            continue;
        }
        if (s->type == NFA_MATCH && s->out1 != NULL) {
            cap_push(e, s->out1, e->scratch);
            continue;
        }
        if (s->type == NFA_CAP_OPEN || s->type == NFA_CAP_CLOSE) {
            int g = s->ch;
            if (g > 0 && e->nslots / 2 > g && 2 * g + 1 < e->nslots) {
                if (s->type == NFA_CAP_OPEN) {
                    e->scratch[2 * g] = pos;
                    e->scratch[2 * g + 1] = (size_t)-1;
                } else {
                    e->scratch[2 * g + 1] = pos;
                }
            }
            cap_push(e, s->out1, e->scratch);
            continue;
        }
        if (s->type == NFA_ANCHOR_START) {
            if (pos == 0) cap_push(e, s->out1, e->scratch);
            continue;
        }
        if (s->type == NFA_ANCHOR_END) {
            int ok = s->ch ? (pos == e->slen) : anchor_end_at(e->text, e->slen, pos);
            if (ok) cap_push(e, s->out1, e->scratch);
            continue;
        }
        if (s->type == NFA_WORD_BOUND) {
            if (word_bound_at(e->text, e->slen, pos))
                cap_push(e, s->out1, e->scratch);
            continue;
        }
        if (s->type == NFA_NO_WORD_BOUND) {
            if (!word_bound_at(e->text, e->slen, pos))
                cap_push(e, s->out1, e->scratch);
            continue;
        }
        if (*e->n < e->capn) {
            e->st[*e->n] = s;
            memcpy(e->capstore + (size_t)(*e->n) * (size_t)e->nslots, e->scratch,
                   (size_t)e->nslots * sizeof(size_t));
            (*e->n)++;
        }
    }
}

/* Longest match from a fixed start, recording capturing groups (Go FindSubmatch). */
static int nfa_exec_caps(neverc_regexp_t *re, const char *s, size_t slen,
                         size_t start, size_t *match_end,
                         size_t *out_caps, int nslots) {
    if (!re || !s || !out_caps || nslots < 2) return 0;
    int nstates = re->nstates > 0 ? re->nstates : 1;
    if ((size_t)nslots > SIZE_MAX / sizeof(size_t) ||
        (size_t)nstates > SIZE_MAX / sizeof(size_t) / (size_t)nslots)
        return 0;
    nfa_state_t **cur_st = (nfa_state_t **)NC_REGEXP_MALLOC(
        (size_t)nstates * sizeof(*cur_st));
    nfa_state_t **next_st = (nfa_state_t **)NC_REGEXP_MALLOC(
        (size_t)nstates * sizeof(*next_st));
    size_t *cur_cap = (size_t *)NC_REGEXP_MALLOC(
        (size_t)nstates * (size_t)nslots * sizeof(size_t));
    size_t *next_cap = (size_t *)NC_REGEXP_MALLOC(
        (size_t)nstates * (size_t)nslots * sizeof(size_t));
    int *visited = (int *)NC_REGEXP_CALLOC((size_t)nstates, sizeof(int));
    size_t *init = (size_t *)NC_REGEXP_MALLOC((size_t)nslots * sizeof(size_t));
    nfa_state_t **wst = (nfa_state_t **)NC_REGEXP_MALLOC(
        (size_t)nstates * sizeof(*wst));
    size_t *wcap = (size_t *)NC_REGEXP_MALLOC(
        (size_t)nstates * (size_t)nslots * sizeof(size_t));
    size_t *scratch = (size_t *)NC_REGEXP_MALLOC((size_t)nslots * sizeof(size_t));
    if (!cur_st || !next_st || !cur_cap || !next_cap || !visited || !init ||
        !wst || !wcap || !scratch) {
        free(cur_st); free(next_st); free(cur_cap); free(next_cap);
        free(visited); free(init); free(wst); free(wcap); free(scratch);
        return 0;
    }
    for (int i = 0; i < nslots; i++) init[i] = (size_t)-1;
    init[0] = start;

    cap_env_t e;
    memset(&e, 0, sizeof(e));
    e.st = cur_st;
    e.capstore = cur_cap;
    e.capn = nstates;
    e.nslots = nslots;
    e.text = s;
    e.slen = slen;
    e.visited = visited;
    e.wst = wst;
    e.wcap = wcap;
    e.scratch = scratch;

    int gen = 1, cur_n = 0;
    e.n = &cur_n;
    e.gen = gen;
    cap_closure(&e, re->start, init, start);

    size_t best_end = (size_t)-1;
    for (int i = 0; i < cur_n; i++) {
        if (cur_st[i]->type == NFA_MATCH && cur_st[i]->out1 == NULL) {
            /* First accept at this length wins (left alternative). */
            if (best_end == (size_t)-1) {
                best_end = start;
                memcpy(out_caps, cur_cap + (size_t)i * (size_t)nslots,
                       (size_t)nslots * sizeof(size_t));
                out_caps[0] = start;
                out_caps[1] = start;
            }
        }
    }

    for (size_t i = start; i < slen; i++) {
        int ch = (unsigned char)s[i];
        if (gen == INT_MAX) {
            memset(visited, 0, (size_t)nstates * sizeof(*visited));
            gen = 1;
        } else {
            gen++;
        }
        int next_n = 0;
        e.st = next_st;
        e.capstore = next_cap;
        e.n = &next_n;
        e.gen = gen;
        for (int j = 0; j < cur_n; j++) {
            nfa_state_t *st = cur_st[j];
            int ok = 0;
            switch (st->type) {
            case NFA_CHAR:  ok = (st->ch == ch); break;
            case NFA_ANY:   ok = (ch != '\n'); break;
            case NFA_CLASS: ok = st->cls && cc_test(st->cls, ch); break;
            default: break;
            }
            if (ok)
                cap_closure(&e, st->out1,
                            cur_cap + (size_t)j * (size_t)nslots, i + 1);
        }
        nfa_state_t **tmp_st = cur_st; cur_st = next_st; next_st = tmp_st;
        size_t *tmp_cap = cur_cap; cur_cap = next_cap; next_cap = tmp_cap;
        cur_n = next_n;

        for (int j = 0; j < cur_n; j++) {
            if (cur_st[j]->type == NFA_MATCH && cur_st[j]->out1 == NULL) {
                size_t end = i + 1;
                /* Longer match wins; equal length keeps the first thread
                 * (left alternative). Overwriting on `end == best_end` used
                 * to swap `(a)|a` / `a|(a)` group slots. */
                if (best_end == (size_t)-1 || end > best_end) {
                    best_end = end;
                    memcpy(out_caps, cur_cap + (size_t)j * (size_t)nslots,
                           (size_t)nslots * sizeof(size_t));
                    out_caps[0] = start;
                    out_caps[1] = best_end;
                }
            }
        }
        if (cur_n == 0) break;
    }

    free(cur_st); free(next_st); free(cur_cap); free(next_cap);
    free(visited); free(init); free(wst); free(wcap); free(scratch);
    if (best_end != (size_t)-1) {
        if (match_end) *match_end = best_end;
        return 1;
    }
    return 0;
}

int neverc_regexp_find_submatch(neverc_regexp_t *re, const char *s,
                                neverc_regexp_match_t *matches, int max_matches) {
    size_t match_len;
    const char *found = neverc_regexp_find(re, s, &match_len);
    if (!found) return 0;
    if (max_matches > 0 && matches) {
        matches[0].start = found;
        matches[0].len = match_len;
        int nfill = max_matches;
        int ng = 0;
        if (re->ngroups >= 0 && re->ngroups < INT_MAX)
            ng = re->ngroups + 1;
        if (nfill > ng) nfill = ng;
        for (int i = 1; i < max_matches; i++) {
            matches[i].start = NULL;
            matches[i].len = 0;
        }
        int nslots = 0;
        if (re->ngroups > 0 && nfill > 1 && cap_slot_count(re->ngroups, &nslots)) {
            size_t *caps = (size_t *)NC_REGEXP_MALLOC((size_t)nslots * sizeof(size_t));
            if (caps) {
                size_t slen = strlen(s);
                size_t ms = (size_t)(found - s), end = 0;
                if (nfa_exec_caps(re, s, slen, ms, &end, caps, nslots) &&
                    end == ms + match_len) {
                    for (int g = 1; g < nfill && g < nslots / 2; g++) {
                        if (caps[2 * g] != (size_t)-1 &&
                            caps[2 * g + 1] != (size_t)-1 &&
                            caps[2 * g + 1] >= caps[2 * g]) {
                            matches[g].start = s + caps[2 * g];
                            matches[g].len = caps[2 * g + 1] - caps[2 * g];
                        }
                    }
                }
                free(caps);
            }
        }
    }
    return 1;
}

static int regexp_string_array_grow(char ***items, size_t *cap) {
    if (*cap > (size_t)INT_MAX / 2U || *cap > SIZE_MAX / 2U / sizeof(**items))
        return -1;
    size_t next_cap = *cap * 2U;
    char **next = (char **)NC_REGEXP_REALLOC(
        *items, next_cap * sizeof(*next));
    if (!next) return -1;
    *items = next;
    *cap = next_cap;
    return 0;
}

char **neverc_regexp_find_all(neverc_regexp_t *re, const char *s,
                              int n, int *count) {
    if (!count) return NULL;
    *count = 0;
    if (!re || !s || n == 0) return NULL;
    size_t slen = strlen(s);
    size_t cap = 16;
    char **results = (char **)NC_REGEXP_MALLOC(cap * sizeof(*results));
    if (!results) return NULL;

    search_ctx ctx;
    if (search_ctx_init(&ctx, re->nstates) != 0) {
        free(results);
        return NULL;
    }
    uint8_t fb[32];
    int use_skip = compute_first_set(re, fb);

    int fbyte = use_skip ? first_set_single(fb) : -1;
    size_t pos = 0;
    while (pos <= slen && (n < 0 || *count < n)) {
        size_t ms, me;
        if (!nfa_search(&ctx, re, s, slen, pos, fb, use_skip, fbyte, &ms, &me)) break;
        size_t mlen = me - ms;
        char *match = (char *)NC_REGEXP_MALLOC(mlen + 1U);
        if (!match) goto oom;
        memcpy(match, s + ms, mlen);
        match[mlen] = '\0';
        if ((size_t)*count >= cap) {
            if (regexp_string_array_grow(&results, &cap) != 0) {
                free(match);
                goto oom;
            }
        }
        results[(*count)++] = match;
        /* Always advance: empty-width matches would otherwise loop. */
        pos = (me > pos) ? me : pos + 1;
    }
    search_ctx_free(&ctx);
    return results;

oom:
    search_ctx_free(&ctx);
    neverc_regexp_free_strings(results, *count);
    *count = 0;
    return NULL;
}

static int regexp_append(char **buffer, size_t *length, size_t *capacity,
                         const char *data, size_t data_len) {
    if (data_len > SIZE_MAX - *length - 1U) return -1;
    size_t needed = *length + data_len + 1U;
    if (needed > *capacity) {
        size_t next_capacity = *capacity;
        while (next_capacity < needed) {
            if (next_capacity > SIZE_MAX / 2U) {
                next_capacity = needed;
                break;
            }
            next_capacity *= 2U;
        }
        char *next = (char *)NC_REGEXP_REALLOC(*buffer, next_capacity);
        if (!next) return -1;
        *buffer = next;
        *capacity = next_capacity;
    }
    if (data_len != 0) memcpy(*buffer + *length, data, data_len);
    *length += data_len;
    return 0;
}

/* Go/RE2 Expand: $0 $1 ${name} $$ ; unknown `$` is emitted literally. */
static int regexp_append_expand(char **buffer, size_t *length, size_t *capacity,
                                const char *repl,
                                const neverc_regexp_match_t *m, int nm,
                                neverc_regexp_t *re) {
    const char *p = repl;
    while (*p) {
        if (*p != '$') {
            const char *start = p;
            while (*p && *p != '$') p++;
            if (regexp_append(buffer, length, capacity, start, (size_t)(p - start)) != 0)
                return -1;
            continue;
        }
        p++;
        if (*p == '$') {
            if (regexp_append(buffer, length, capacity, "$", 1) != 0) return -1;
            p++;
            continue;
        }
        /* Go extract(): malformed ${ leaves '{' in the template and only
         * consumes the '$'. Leading-zero digit names ($01) are names. */
        const char *after_dollar = p;
        int braced = 0;
        if (*p == '{') { braced = 1; p++; }
        /* Go extract(): one unicode.IsLetter/IsDigit/_ token, then
         * number vs name. $1a is the name "1a", not group 1 plus 'a'. */
        const char *ns = p;
        while (*p) {
            uint32_t rune = 0;
            int size = 0;
            neverc_utf8_decode_rune((const uint8_t *)p, strlen(p),
                                    &rune, &size);
            if (size <= 0) break;
            if (!neverc_unicode_is_letter(rune) &&
                !neverc_unicode_is_digit(rune) && rune != '_')
                break;
            p += size;
        }
        int nlen = (int)(p - ns);
        if (nlen <= 0 || (braced && *p != '}')) {
            if (regexp_append(buffer, length, capacity, "$", 1) != 0) return -1;
            p = after_dollar;
            continue;
        }
        if (braced) p++;
        int gi = -1, digits = 1;
        for (int k = 0; k < nlen; k++)
            if (ns[k] < '0' || ns[k] > '9') { digits = 0; break; }
        if (digits && !(ns[0] == '0' && nlen > 1)) {
            int v = 0;
            for (int k = 0; k < nlen; k++) {
                if (v > (INT_MAX - (ns[k] - '0')) / 10) { v = -1; break; }
                v = v * 10 + (ns[k] - '0');
            }
            gi = v;
        } else {
            char name[64];
            if (nlen >= (int)sizeof(name)) {
                char *tmp = (char *)NC_REGEXP_MALLOC((size_t)nlen + 1U);
                if (!tmp) return -1;
                memcpy(tmp, ns, (size_t)nlen);
                tmp[nlen] = '\0';
                gi = neverc_regexp_subexp_index(re, tmp);
                free(tmp);
            } else {
                memcpy(name, ns, (size_t)nlen);
                name[nlen] = '\0';
                gi = neverc_regexp_subexp_index(re, name);
            }
        }
        if (gi >= 0 && gi < nm && m[gi].start)
            if (regexp_append(buffer, length, capacity, m[gi].start, m[gi].len) != 0)
                return -1;
    }
    return 0;
}

static int fill_replace_caps(neverc_regexp_t *re, const char *src, size_t slen,
                             size_t ms, size_t me, neverc_regexp_match_t *m, int nm) {
    m[0].start = src + ms;
    m[0].len = me - ms;
    for (int i = 1; i < nm; i++) {
        m[i].start = NULL;
        m[i].len = 0;
    }
    if (re->ngroups <= 0 || nm <= 1) return 0;
    int nslots = 0;
    if (!cap_slot_count(re->ngroups, &nslots)) return -1;
    size_t *caps = (size_t *)NC_REGEXP_MALLOC((size_t)nslots * sizeof(size_t));
    if (!caps) return -1;
    size_t end = 0;
    if (nfa_exec_caps(re, src, slen, ms, &end, caps, nslots) && end == me) {
        int nfill = nm < re->ngroups + 1 ? nm : re->ngroups + 1;
        for (int g = 1; g < nfill && g < nslots / 2; g++) {
            if (caps[2 * g] != (size_t)-1 &&
                caps[2 * g + 1] != (size_t)-1 &&
                caps[2 * g + 1] >= caps[2 * g]) {
                m[g].start = src + caps[2 * g];
                m[g].len = caps[2 * g + 1] - caps[2 * g];
            }
        }
    }
    free(caps);
    return 0;
}

char *neverc_regexp_replace_all(neverc_regexp_t *re, const char *src,
                                const char *repl, size_t *outlen) {
    if (outlen) *outlen = 0;
    if (!re || !src || !repl) return NULL;
    size_t slen = strlen(src);
    size_t rlen = strlen(repl);
    int expand = (strchr(repl, '$') != NULL);
    int nm = 0;
    neverc_regexp_match_t *mslots = NULL;
    if (expand) {
        if (re->ngroups < 0 || re->ngroups == INT_MAX) return NULL;
        nm = re->ngroups + 1;
        mslots = (neverc_regexp_match_t *)NC_REGEXP_CALLOC((size_t)nm, sizeof(*mslots));
        if (!mslots) return NULL;
    }
    size_t cap = 64;
    char *result = (char *)NC_REGEXP_MALLOC(cap);
    if (!result) { free(mslots); return NULL; }
    size_t wi = 0, pos = 0;

    search_ctx ctx;
    if (search_ctx_init(&ctx, re->nstates) != 0) {
        free(result);
        free(mslots);
        return NULL;
    }
    uint8_t fb[32];
    int use_skip = compute_first_set(re, fb);
    int fbyte = use_skip ? first_set_single(fb) : -1;

    while (pos <= slen) {
        size_t i, end;
        if (!nfa_search(&ctx, re, src, slen, pos, fb, use_skip, fbyte, &i, &end)) break;
        /* Copy unmatched part before match */
        size_t before = i - pos;
        if (regexp_append(&result, &wi, &cap, src + pos, before) != 0)
            goto oom;
        if (expand) {
            if (fill_replace_caps(re, src, slen, i, end, mslots, nm) != 0) goto oom;
            if (regexp_append_expand(&result, &wi, &cap, repl, mslots, nm, re) != 0)
                goto oom;
        } else if (regexp_append(&result, &wi, &cap, repl, rlen) != 0) {
            goto oom;
        }
        pos = (end > pos) ? end : pos + 1;      /* empty-width: advance one byte */
    }

    /* Copy remaining */
    if (pos < slen) {
        size_t rem = slen - pos;
        if (regexp_append(&result, &wi, &cap, src + pos, rem) != 0) goto oom;
    }

    result[wi] = '\0';
    if (outlen) *outlen = wi;
    search_ctx_free(&ctx);
    free(mslots);
    return result;

oom:
    search_ctx_free(&ctx);
    free(mslots);
    free(result);
    return NULL;
}

char **neverc_regexp_split(neverc_regexp_t *re, const char *s,
                           int n, int *count) {
    if (!count) return NULL;
    *count = 0;
    if (!re || !s || n == 0) return NULL;
    size_t slen = strlen(s);
    size_t cap = 16;
    char **results = (char **)NC_REGEXP_MALLOC(cap * sizeof(*results));
    if (!results) return NULL;
    size_t pos = 0;

    search_ctx ctx;
    if (search_ctx_init(&ctx, re->nstates) != 0) {
        free(results);
        return NULL;
    }
    uint8_t fb[32];
    int use_skip = compute_first_set(re, fb);
    int fbyte = use_skip ? first_set_single(fb) : -1;

    while (pos <= slen && (n < 0 || *count < n - 1)) {
        size_t end = 0, mstart;
        /* Leftmost non-empty match at or after pos, in a single linear sweep. */
        if (!nfa_search(&ctx, re, s, slen, pos, fb, use_skip, fbyte, &mstart, &end)) break;

        size_t seg_len = mstart - pos;          /* 0 when the match is at pos */
        if ((size_t)*count >= cap &&
            regexp_string_array_grow(&results, &cap) != 0)
            goto oom;
        char *seg = (char *)NC_REGEXP_MALLOC(seg_len + 1U);
        if (!seg) goto oom;
        memcpy(seg, s + pos, seg_len);
        seg[seg_len] = '\0';
        results[(*count)++] = seg;
        pos = (end > pos) ? end : pos + 1;
    }

    /* Remaining */
    size_t rem = slen - pos;
    if ((size_t)*count >= cap &&
        regexp_string_array_grow(&results, &cap) != 0)
        goto oom;
    char *seg = (char *)NC_REGEXP_MALLOC(rem + 1U);
    if (!seg) goto oom;
    memcpy(seg, s + pos, rem);
    seg[rem] = '\0';
    results[(*count)++] = seg;

    search_ctx_free(&ctx);
    return results;

oom:
    search_ctx_free(&ctx);
    neverc_regexp_free_strings(results, *count);
    *count = 0;
    return NULL;
}

void neverc_regexp_free_strings(char **strs, int count) {
    if (!strs) return;
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
    if (slen > (SIZE_MAX - 1U) / 2U) return NULL;
    char *result = (char *)NC_REGEXP_MALLOC(slen * 2U + 1U);
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

int neverc_regexp_num_subexp(neverc_regexp_t *re) {
    return re ? re->ngroups : 0;
}

const char *neverc_regexp_subexp_name(neverc_regexp_t *re, int i) {
    if (!re || i < 0 || i > re->ngroups) return NULL;
    if (!re->gnames || i >= re->gnames_cap) return NULL;
    return re->gnames[i];
}

int neverc_regexp_subexp_index(neverc_regexp_t *re, const char *name) {
    if (!re || !name || !re->gnames) return -1;
    int last = re->ngroups < re->gnames_cap ? re->ngroups : re->gnames_cap - 1;
    for (int i = 1; i <= last; i++) {
        if (re->gnames[i] && strcmp(re->gnames[i], name) == 0)
            return i;
    }
    return -1;
}

neverc_regexp_t *neverc_regexp_compile_posix(const char *pattern, const char **errp) {
    return regexp_compile(pattern, errp, 1);
}

neverc_regexp_t *neverc_regexp_must_compile(const char *pattern) {
    const char *err = NULL;
    neverc_regexp_t *re = neverc_regexp_compile(pattern, &err);
    if (!re) {
        fprintf(stderr, "neverc_regexp_must_compile: %s: %s\n",
                pattern ? pattern : "(null)", err ? err : "unknown error");
        abort();
    }
    return re;
}

neverc_regexp_t *neverc_regexp_must_compile_posix(const char *pattern) {
    const char *err = NULL;
    neverc_regexp_t *re = neverc_regexp_compile_posix(pattern, &err);
    if (!re) {
        fprintf(stderr, "neverc_regexp_must_compile_posix: %s: %s\n",
                pattern ? pattern : "(null)", err ? err : "unknown error");
        abort();
    }
    return re;
}
