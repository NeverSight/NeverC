#include "neverc/std/regexp_syntax.h"
#include "neverc/std/unicode.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>

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

#ifndef NCI_REGEXP_SYNTAX_MAX_DEPTH
#define NCI_REGEXP_SYNTAX_MAX_DEPTH 400
#endif

typedef struct {
    const char *src;
    int         pos;
    int         len;
    int         flags;
    int         ncap;
    int         depth;
    const char *err;
} parser_t;

static neverc_regexp_syntax_node_t *mk_node(parser_t *p,
                                             neverc_regexp_op_t op) {
    neverc_regexp_syntax_node_t *n =
        (neverc_regexp_syntax_node_t *)calloc(1, sizeof(*n));
    if (!n) {
        p->err = "out of memory";
        return NULL;
    }
    n->op = op;
    n->max = -1;
    return n;
}

/* Append helpers. Capacity grows by doubling (it is the next power of two >=
 * the count), so building an n-element concat/alternation or an n-rune char
 * class is amortized O(n) instead of the O(n^2) that an exact-size realloc per
 * element caused. nsubs/nrunes stay the exact element count, so the rounded-up
 * allocation is invisible to every reader (count, string, equal) and to free(),
 * which just releases the pointer — no struct/API change. realloc is only
 * issued when the count crosses a power-of-two boundary. */
static int add_sub(parser_t *p, neverc_regexp_syntax_node_t *parent,
                   neverc_regexp_syntax_node_t *child) {
    if (!parent || !child || parent->nsubs < 0 || parent->nsubs == INT_MAX) {
        p->err = "out of memory";
        return 0;
    }
    size_t n = (size_t)parent->nsubs;
    if ((n & (n - 1)) == 0) {
        if (n > SIZE_MAX / 2) {
            p->err = "out of memory";
            return 0;
        }
        size_t cap = n ? n * 2 : 1;
        if (cap > SIZE_MAX / sizeof(*parent->subs)) {
            p->err = "out of memory";
            return 0;
        }
        neverc_regexp_syntax_node_t **grown =
            (neverc_regexp_syntax_node_t **)realloc(
            parent->subs, cap * sizeof(neverc_regexp_syntax_node_t *));
        if (!grown) {
            p->err = "out of memory";
            return 0;
        }
        parent->subs = grown;
    }
    parent->subs[n] = child;
    parent->nsubs++;
    return 1;
}

static int add_rune(parser_t *p, neverc_regexp_syntax_node_t *n, int r) {
    if (!n || n->nrunes < 0 || n->nrunes == INT_MAX) {
        p->err = "out of memory";
        return 0;
    }
    size_t k = (size_t)n->nrunes;
    if ((k & (k - 1)) == 0) {
        if (k > SIZE_MAX / 2) {
            p->err = "out of memory";
            return 0;
        }
        size_t cap = k ? k * 2 : 1;
        if (cap > SIZE_MAX / sizeof(*n->runes)) {
            p->err = "out of memory";
            return 0;
        }
        int *grown = (int *)realloc(n->runes, cap * sizeof(int));
        if (!grown) {
            p->err = "out of memory";
            return 0;
        }
        n->runes = grown;
    }
    n->runes[k] = r;
    n->nrunes++;
    return 1;
}

static int min_fold_rune(int rune);

static neverc_regexp_syntax_node_t *literal_node(parser_t *p, int rune) {
    neverc_regexp_syntax_node_t *n = mk_node(p, NC_RE_OP_LITERAL);
    if (!n) return NULL;
    if (p->flags & NC_RE_FLAG_FOLD_CASE) {
        rune = min_fold_rune(rune);
        n->flags |= NC_RE_FLAG_FOLD_CASE;
    }
    if (!add_rune(p, n, rune)) {
        neverc_regexp_syntax_free(n);
        return NULL;
    }
    return n;
}

static int add_escape_class(parser_t *p, neverc_regexp_syntax_node_t *n,
                            int escape) {
    switch (escape | 0x20) {
    case 'd':
        return add_rune(p, n, '0') && add_rune(p, n, '9');
    case 'w':
        return add_rune(p, n, '0') && add_rune(p, n, '9') &&
               add_rune(p, n, 'A') && add_rune(p, n, 'Z') &&
               add_rune(p, n, 'a') && add_rune(p, n, 'z') &&
               add_rune(p, n, '_') && add_rune(p, n, '_');
    case 's':
        /* RE2/Go: \s is [\t\n\f\r ], not \v (issue 22057). */
        return add_rune(p, n, '\t') && add_rune(p, n, '\t') &&
               add_rune(p, n, '\n') && add_rune(p, n, '\n') &&
               add_rune(p, n, '\f') && add_rune(p, n, '\f') &&
               add_rune(p, n, '\r') && add_rune(p, n, '\r') &&
               add_rune(p, n, ' ') && add_rune(p, n, ' ');
    default:
        return 0;
    }
}

#ifndef NCI_RE_MAX_RUNE
#define NCI_RE_MAX_RUNE 0x10FFFF
#endif
#ifndef NCI_RE_MAX_REPEAT
#define NCI_RE_MAX_REPEAT 1000
#endif

/* Minimum and maximum runes participating in Unicode simple folding. These
 * match Go regexp/syntax for Unicode 17.0.0. Keeping the bounds avoids walking
 * every scalar value for a class such as [\x00-\x{10FFFF}]. */
#define NCI_RE_MIN_FOLD 0x0041
#define NCI_RE_MAX_FOLD 0x1E943

static int add_range(parser_t *p, neverc_regexp_syntax_node_t *n,
                     int lo, int hi) {
    if (lo < 0 || hi < lo || hi > NCI_RE_MAX_RUNE) {
        p->err = "invalid character class range";
        return 0;
    }

    /* As in Go appendRange, checking the last two ranges coalesces the two
     * interleaved alphabets produced by simple folding without quadratic
     * insertion work. A final sort/merge makes the complete class canonical. */
    for (int back = 2; back <= 4; back += 2) {
        if (n->nrunes >= back) {
            int *rlo = &n->runes[n->nrunes - back];
            int *rhi = rlo + 1;
            if (lo <= *rhi + 1 && *rlo <= hi + 1) {
                if (lo < *rlo) *rlo = lo;
                if (hi > *rhi) *rhi = hi;
                return 1;
            }
        }
    }
    return add_rune(p, n, lo) && add_rune(p, n, hi);
}

static int range_pair_compare(const void *ap, const void *bp) {
    const int *a = (const int *)ap;
    const int *b = (const int *)bp;
    if (a[0] < b[0]) return -1;
    if (a[0] > b[0]) return 1;
    if (a[1] > b[1]) return -1;
    if (a[1] < b[1]) return 1;
    return 0;
}

static int clean_char_class(parser_t *p, neverc_regexp_syntax_node_t *n) {
    if (!n || n->nrunes < 0 || (n->nrunes & 1)) {
        p->err = "invalid character class";
        return 0;
    }
    if (n->nrunes < 4) return 1;

    qsort(n->runes, (size_t)n->nrunes / 2, 2 * sizeof(*n->runes),
          range_pair_compare);
    int write = 2;
    for (int read = 2; read < n->nrunes; read += 2) {
        int lo = n->runes[read];
        int hi = n->runes[read + 1];
        if (lo <= n->runes[write - 1] + 1) {
            if (hi > n->runes[write - 1]) n->runes[write - 1] = hi;
        } else {
            n->runes[write++] = lo;
            n->runes[write++] = hi;
        }
    }
    n->nrunes = write;
    return 1;
}

static int min_fold_rune(int rune) {
    if (rune < NCI_RE_MIN_FOLD || rune > NCI_RE_MAX_FOLD) return rune;
    uint32_t start = (uint32_t)rune;
    uint32_t folded = neverc_unicode_simple_fold(start);
    uint32_t minimum = start;
    while (folded != start) {
        if (folded < minimum) minimum = folded;
        folded = neverc_unicode_simple_fold(folded);
    }
    return (int)minimum;
}

static int add_folded_range(parser_t *p, neverc_regexp_syntax_node_t *n,
                            int lo, int hi) {
    if (lo <= NCI_RE_MIN_FOLD && hi >= NCI_RE_MAX_FOLD)
        return add_range(p, n, lo, hi);
    if (hi < NCI_RE_MIN_FOLD || lo > NCI_RE_MAX_FOLD)
        return add_range(p, n, lo, hi);

    if (lo < NCI_RE_MIN_FOLD) {
        if (!add_range(p, n, lo, NCI_RE_MIN_FOLD - 1)) return 0;
        lo = NCI_RE_MIN_FOLD;
    }
    if (hi > NCI_RE_MAX_FOLD) {
        if (!add_range(p, n, NCI_RE_MAX_FOLD + 1, hi)) return 0;
        hi = NCI_RE_MAX_FOLD;
    }

    for (int c = lo; c <= hi; c++) {
        if (!add_range(p, n, c, c)) return 0;
        uint32_t folded = neverc_unicode_simple_fold((uint32_t)c);
        while (folded != (uint32_t)c) {
            if (!add_range(p, n, (int)folded, (int)folded)) return 0;
            folded = neverc_unicode_simple_fold(folded);
        }
    }
    return 1;
}

static int fold_char_class(parser_t *p, neverc_regexp_syntax_node_t *n) {
    neverc_regexp_syntax_node_t folded;
    memset(&folded, 0, sizeof(folded));
    folded.op = NC_RE_OP_CHAR_CLASS;

    for (int i = 0; i + 1 < n->nrunes; i += 2) {
        if (!add_folded_range(p, &folded, n->runes[i], n->runes[i + 1])) {
            free(folded.runes);
            return 0;
        }
    }
    if (!clean_char_class(p, &folded)) {
        free(folded.runes);
        return 0;
    }
    free(n->runes);
    n->runes = folded.runes;
    n->nrunes = folded.nrunes;
    n->flags |= NC_RE_FLAG_FOLD_CASE;
    return 1;
}

static int append_class(parser_t *p, neverc_regexp_syntax_node_t *dst,
                        const neverc_regexp_syntax_node_t *src) {
    for (int i = 0; i + 1 < src->nrunes; i += 2) {
        if (!add_rune(p, dst, src->runes[i]) ||
            !add_rune(p, dst, src->runes[i + 1]))
            return 0;
    }
    return 1;
}

static int append_negated_class(parser_t *p,
                                neverc_regexp_syntax_node_t *dst,
                                const neverc_regexp_syntax_node_t *src) {
    int next_lo = 0;
    for (int i = 0; i + 1 < src->nrunes; i += 2) {
        int lo = src->runes[i];
        int hi = src->runes[i + 1];
        if (next_lo < lo &&
            (!add_rune(p, dst, next_lo) || !add_rune(p, dst, lo - 1)))
            return 0;
        if (hi == NCI_RE_MAX_RUNE) return 1;
        next_lo = hi + 1;
    }
    return next_lo > NCI_RE_MAX_RUNE ||
           (add_rune(p, dst, next_lo) &&
            add_rune(p, dst, NCI_RE_MAX_RUNE));
}

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

static int utf8_valid(const char *s, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        int r, k = utf8_decode(p + i, n - i, &r);
        if (k < 1) return 0;
        i += (size_t)k;
    }
    return 1;
}

/* Go parseInt: a leading zero followed by another digit is not an integer. */
static int brace_repeat_leading_zeros(const char *s, int pos, int len) {
    if (pos >= len || s[pos] != '{') return 0;
    pos++;
    if (pos < len && s[pos] == '0' && pos + 1 < len &&
        s[pos + 1] >= '0' && s[pos + 1] <= '9')
        return 1;
    while (pos < len && s[pos] >= '0' && s[pos] <= '9') pos++;
    if (pos < len && s[pos] == ',') {
        pos++;
        if (pos < len && s[pos] == '0' && pos + 1 < len &&
            s[pos + 1] >= '0' && s[pos + 1] <= '9')
            return 1;
    }
    return 0;
}

/* True when `{` at pos is a complete {n} / {n,} / {n,m} that Go parseRepeat
 * would treat as a quantifier. Leading zeros and unclosed braces are
 * literals, so they must not trip the nested-repeat check after *+?{n}. */
static int complete_brace_repeat(const char *s, int pos, int len) {
    if (pos >= len || s[pos] != '{') return 0;
    if (brace_repeat_leading_zeros(s, pos, len)) return 0;
    pos++;
    if (pos >= len || s[pos] < '0' || s[pos] > '9') return 0;
    while (pos < len && s[pos] >= '0' && s[pos] <= '9') pos++;
    if (pos >= len) return 0;
    if (s[pos] == ',') {
        pos++;
        if (pos >= len) return 0;
        if (s[pos] != '}') {
            if (s[pos] < '0' || s[pos] > '9') return 0;
            while (pos < len && s[pos] >= '0' && s[pos] <= '9') pos++;
        }
    }
    return pos < len && s[pos] == '}';
}

static int peek(parser_t *p);
static int next(parser_t *p);

static int hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* After consuming `\x`. Go: `\xHH` or `\x{H+}`. Go regexp/syntax accepts
 * surrogate code points in hex escapes (`\x{D800}`); they are not UTF-8. */
static int parse_hex_rune(parser_t *p, int *out) {
    int c = peek(p);
    if (c == '{') {
        next(p);
        int v = 0, n = 0;
        for (;;) {
            c = peek(p);
            if (c == '}') {
                if (n == 0) { p->err = "invalid escape sequence"; return 0; }
                next(p);
                *out = v;
                return 1;
            }
            int d = hex_digit(c);
            if (d < 0) { p->err = "invalid escape sequence"; return 0; }
            if (v > (NCI_RE_MAX_RUNE - d) / 16) {
                p->err = "invalid escape sequence";
                return 0;
            }
            v = v * 16 + d;
            n++;
            next(p);
        }
    }
    int x = hex_digit(c);
    if (x < 0) { p->err = "invalid escape sequence"; return 0; }
    next(p);
    int y = hex_digit(peek(p));
    if (y < 0) { p->err = "invalid escape sequence"; return 0; }
    next(p);
    *out = x * 16 + y;
    return 1;
}

static int is_perl_class_escape(int escape) {
    int lower = escape | 0x20;
    return lower == 'd' || lower == 'w' || lower == 's';
}

/* Append a Perl class escape as one member of [...]. Uppercase escapes are
 * complements, so under FoldCase the positive class must be closed over its
 * simple-fold orbits before complementing it. Folding the complement itself
 * would be wrong for \W: Kelvin sign is outside ASCII \w but folds to k. */
static int add_escape_class_member(parser_t *p,
                                   neverc_regexp_syntax_node_t *n,
                                   int escape) {
    neverc_regexp_syntax_node_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.op = NC_RE_OP_CHAR_CLASS;

    if (!add_escape_class(p, &tmp, escape)) {
        free(tmp.runes);
        return 0;
    }
    int negate = (escape == 'D' || escape == 'W' || escape == 'S');
    if (p->flags & NC_RE_FLAG_FOLD_CASE) {
        if (!fold_char_class(p, &tmp)) {
            free(tmp.runes);
            return 0;
        }
    } else if (negate && !clean_char_class(p, &tmp)) {
        free(tmp.runes);
        return 0;
    }

    int ok = negate ? append_negated_class(p, n, &tmp)
                    : append_class(p, n, &tmp);
    free(tmp.runes);
    return ok;
}

/* ======================================================================
 * Parser
 * ====================================================================== */

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

static int next_rune(parser_t *p, int *rune) {
    if (p->pos >= p->len) return 0;
    int n = utf8_decode((const unsigned char *)p->src + p->pos,
                        (size_t)(p->len - p->pos), rune);
    if (n < 1) {
        p->err = "invalid UTF-8";
        return 0;
    }
    p->pos += n;
    return 1;
}

static int parse_int(parser_t *p, int *value) {
    int val = 0;
    int start = p->pos;
    int overflow = 0;
    /* Go parseInt: digits are accepted even when the value overflows; the
     * caller then either treats an unclosed `{n` as a literal or rejects a
     * complete `{n}` whose count is too large. Failing here used to make
     * `a{2147483648` a syntax error instead of the literal `{`. */
    while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') {
        int digit = p->src[p->pos] - '0';
        if (overflow || val > (INT_MAX - digit) / 10)
            overflow = 1;
        else
            val = val * 10 + digit;
        p->pos++;
    }
    if (p->pos == start) {
        p->err = "bad repeat syntax";
        return 0;
    }
    *value = overflow ? INT_MAX : val;
    return 1;
}

static neverc_regexp_syntax_node_t *parse_escape(parser_t *p) {
    int c = next(p);
    if (c < 0) { p->err = "trailing backslash"; return NULL; }

    switch (c) {
    case 'd': case 'D': case 'w': case 'W': case 's': case 'S': {
        neverc_regexp_syntax_node_t *n = mk_node(p, NC_RE_OP_CHAR_CLASS);
        if (!n) return NULL;
        int negate = (c == 'D' || c == 'W' || c == 'S');
        if (!add_escape_class(p, n, c)) {
            neverc_regexp_syntax_free(n);
            return NULL;
        }
        if ((p->flags & NC_RE_FLAG_FOLD_CASE) && !fold_char_class(p, n)) {
            neverc_regexp_syntax_free(n);
            return NULL;
        }
        if (negate) n->flags |= NC_RE_FLAG_CLASS_NEGATED;
        return n;
    }
    case 'b': return mk_node(p, NC_RE_OP_WORD_BOUNDARY);
    case 'B': return mk_node(p, NC_RE_OP_NO_WORD_BOUNDARY);
    case 'A': return mk_node(p, NC_RE_OP_BEGIN_TEXT);
    case 'z': return mk_node(p, NC_RE_OP_END_TEXT);
    case 'a': return literal_node(p, '\a');
    case 'f': return literal_node(p, '\f');
    case 't': return literal_node(p, '\t');
    case 'n': return literal_node(p, '\n');
    case 'r': return literal_node(p, '\r');
    case 'v': return literal_node(p, '\v');
    case 'x': {
        int r;
        if (!parse_hex_rune(p, &r)) return NULL;
        return literal_node(p, r);
    }
    default:
        /* Go/RE2: unknown letter/digit escapes are errors (no backreferences). */
        if ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            p->err = "invalid escape sequence";
            return NULL;
        }
        return literal_node(p, c);
    }
}

static int add_posix_class_base(parser_t *p,
                                neverc_regexp_syntax_node_t *n,
                                const char *name, int nlen) {
    if (!name || nlen <= 0) return 0;
#define NCI_SY_EQ(s) (nlen == (int)(sizeof(s) - 1) && \
                      memcmp(name, s, (size_t)nlen) == 0)
    if (NCI_SY_EQ("alnum"))
        return add_rune(p, n, '0') && add_rune(p, n, '9') &&
               add_rune(p, n, 'A') && add_rune(p, n, 'Z') &&
               add_rune(p, n, 'a') && add_rune(p, n, 'z');
    if (NCI_SY_EQ("alpha"))
        return add_rune(p, n, 'A') && add_rune(p, n, 'Z') &&
               add_rune(p, n, 'a') && add_rune(p, n, 'z');
    if (NCI_SY_EQ("ascii"))
        return add_rune(p, n, 0) && add_rune(p, n, 0x7F);
    if (NCI_SY_EQ("blank"))
        return add_rune(p, n, '\t') && add_rune(p, n, '\t') &&
               add_rune(p, n, ' ') && add_rune(p, n, ' ');
    if (NCI_SY_EQ("cntrl"))
        return add_rune(p, n, 0) && add_rune(p, n, 0x1F) &&
               add_rune(p, n, 0x7F) && add_rune(p, n, 0x7F);
    if (NCI_SY_EQ("digit"))
        return add_rune(p, n, '0') && add_rune(p, n, '9');
    if (NCI_SY_EQ("graph"))
        return add_rune(p, n, 0x21) && add_rune(p, n, 0x7E);
    if (NCI_SY_EQ("lower"))
        return add_rune(p, n, 'a') && add_rune(p, n, 'z');
    if (NCI_SY_EQ("print"))
        return add_rune(p, n, 0x20) && add_rune(p, n, 0x7E);
    if (NCI_SY_EQ("punct")) {
        static const char punct[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
        for (int i = 0; punct[i]; i++)
            if (!add_rune(p, n, (unsigned char)punct[i]) ||
                !add_rune(p, n, (unsigned char)punct[i]))
                return 0;
        return 1;
    }
    if (NCI_SY_EQ("space"))
        /* POSIX/Go [:space:] is [\t-\r ] (includes VT). Do not reuse \s. */
        return add_rune(p, n, '\t') && add_rune(p, n, '\r') &&
               add_rune(p, n, ' ') && add_rune(p, n, ' ');
    if (NCI_SY_EQ("upper"))
        return add_rune(p, n, 'A') && add_rune(p, n, 'Z');
    if (NCI_SY_EQ("word"))
        return add_escape_class(p, n, 'w');
    if (NCI_SY_EQ("xdigit"))
        return add_rune(p, n, '0') && add_rune(p, n, '9') &&
               add_rune(p, n, 'A') && add_rune(p, n, 'F') &&
               add_rune(p, n, 'a') && add_rune(p, n, 'f');
#undef NCI_SY_EQ
    return 0;
}

static int add_posix_class(parser_t *p, neverc_regexp_syntax_node_t *n,
                           const char *name, int nlen) {
    if (!name || nlen <= 0) return 0;
    int negate = nlen > 1 && name[0] == '^';
    if (negate) {
        name++;
        nlen--;
    }

    neverc_regexp_syntax_node_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.op = NC_RE_OP_CHAR_CLASS;
    if (!add_posix_class_base(p, &tmp, name, nlen)) {
        free(tmp.runes);
        return 0;
    }
    if (p->flags & NC_RE_FLAG_FOLD_CASE) {
        if (!fold_char_class(p, &tmp)) {
            free(tmp.runes);
            return 0;
        }
    } else if (negate && !clean_char_class(p, &tmp)) {
        free(tmp.runes);
        return 0;
    }

    int ok = negate ? append_negated_class(p, n, &tmp)
                    : append_class(p, n, &tmp);
    free(tmp.runes);
    return ok;
}

static neverc_regexp_syntax_node_t *parse_char_class(parser_t *p) {
    neverc_regexp_syntax_node_t *n = mk_node(p, NC_RE_OP_CHAR_CLASS);
    if (!n) return NULL;
    int negate = 0;
    if (peek(p) == '^') { next(p); negate = 1; }
    n->flags |= p->flags & NC_RE_FLAG_FOLD_CASE;
    if (negate) n->flags |= NC_RE_FLAG_CLASS_NEGATED;

    int first = 1;
    while (p->pos < p->len) {
        int c = peek(p);
        if (c == ']' && !first) {
            next(p);
            if ((p->flags & NC_RE_FLAG_FOLD_CASE) &&
                !clean_char_class(p, n)) {
                neverc_regexp_syntax_free(n);
                return NULL;
            }
            return n;
        }
        first = 0;

        if (c == '[' && p->pos + 1 < p->len && p->src[p->pos + 1] == ':') {
            int save = p->pos;
            next(p); next(p);
            int ns = p->pos;
            while (p->pos < p->len &&
                   !(peek(p) == ':' && p->pos + 1 < p->len &&
                     p->src[p->pos + 1] == ']'))
                next(p);
            if (p->pos < p->len) {
                int nlen = p->pos - ns;
                next(p); next(p);
                if (!add_posix_class(p, n, p->src + ns, nlen)) {
                    if (!p->err) p->err = "invalid POSIX class";
                    neverc_regexp_syntax_free(n);
                    return NULL;
                }
                continue;
            }
            p->pos = save;
        }

        if (c == '\\') {
            next(p);
            int esc = next(p);
            if (esc < 0) { p->err = "bad escape in char class"; neverc_regexp_syntax_free(n); return NULL; }
            switch (esc) {
            case 'd': case 'w': case 's':
            case 'D': case 'W': case 'S':
                if (!add_escape_class_member(p, n, esc)) {
                    neverc_regexp_syntax_free(n);
                    return NULL;
                }
                continue;
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'f': c = '\f'; break;
            case 'v': c = '\v'; break;
            case 'x':
                if (!parse_hex_rune(p, &c)) {
                    neverc_regexp_syntax_free(n);
                    return NULL;
                }
                break;
            default:
                if ((esc >= '0' && esc <= '9') ||
                    (esc >= 'A' && esc <= 'Z') || (esc >= 'a' && esc <= 'z')) {
                    p->err = "invalid escape sequence";
                    neverc_regexp_syntax_free(n);
                    return NULL;
                }
                c = esc;
                break;
            }
        } else {
            if (!next_rune(p, &c)) {
                neverc_regexp_syntax_free(n);
                return NULL;
            }
        }

        if (p->pos + 1 < p->len && p->src[p->pos] == '-' && p->src[p->pos + 1] != ']') {
            next(p); /* consume '-' */
            int hi;
            if (peek(p) == '\\') {
                next(p);
                hi = next(p);
                if (is_perl_class_escape(hi)) {
                    p->err = "invalid character class range";
                    neverc_regexp_syntax_free(n);
                    return NULL;
                }
                if (hi == 'x') {
                    if (!parse_hex_rune(p, &hi)) {
                        neverc_regexp_syntax_free(n);
                        return NULL;
                    }
                } else if (hi == 'a') hi = '\a';
                else if (hi == 'b') hi = '\b';
                else if (hi == 'n') hi = '\n';
                else if (hi == 't') hi = '\t';
                else if (hi == 'r') hi = '\r';
                else if (hi == 'f') hi = '\f';
                else if (hi == 'v') hi = '\v';
                else if ((hi >= '0' && hi <= '9') ||
                         (hi >= 'A' && hi <= 'Z') || (hi >= 'a' && hi <= 'z')) {
                    /* Go/RE2: `[a-\q]` is an unknown letter escape, not a-q. */
                    p->err = "invalid escape sequence";
                    neverc_regexp_syntax_free(n);
                    return NULL;
                }
            } else {
                if (!next_rune(p, &hi)) {
                    neverc_regexp_syntax_free(n);
                    return NULL;
                }
            }
            if (hi < 0 || hi < c) {
                p->err = "invalid character class range";
                neverc_regexp_syntax_free(n);
                return NULL;
            }
            int ok = (p->flags & NC_RE_FLAG_FOLD_CASE)
                         ? add_folded_range(p, n, c, hi)
                         : (add_rune(p, n, c) && add_rune(p, n, hi));
            if (!ok) {
                neverc_regexp_syntax_free(n);
                return NULL;
            }
        } else {
            int ok = (p->flags & NC_RE_FLAG_FOLD_CASE)
                         ? add_folded_range(p, n, c, c)
                         : (add_rune(p, n, c) && add_rune(p, n, c));
            if (!ok) {
                neverc_regexp_syntax_free(n);
                return NULL;
            }
        }
    }
    p->err = "unclosed character class";
    neverc_regexp_syntax_free(n);
    return NULL;
}

static neverc_regexp_syntax_node_t *parse_group(parser_t *p) {
    if (++p->depth > NCI_REGEXP_SYNTAX_MAX_DEPTH) {
        p->err = "expression nested too deeply";
        p->depth--;
        return NULL;
    }
    neverc_regexp_syntax_node_t *result = NULL;
    if (p->pos + 2 < p->len && p->src[p->pos] == '?' &&
        p->src[p->pos + 1] == 'i' && p->src[p->pos + 2] == ':') {
        int saved_flags = p->flags;
        p->pos += 3;
        p->flags = saved_flags | NC_RE_FLAG_FOLD_CASE;
        neverc_regexp_syntax_node_t *inner = parse_alternation(p);
        /* Parser flags are lexical scope, not state carried past the group.
         * Restore them before every success or error exit from this branch. */
        p->flags = saved_flags;
        if (!inner) goto done;
        if (next(p) != ')') {
            p->err = "unclosed group";
            neverc_regexp_syntax_free(inner);
            goto done;
        }
        result = inner;
        goto done;
    }
    if (p->pos + 1 < p->len && p->src[p->pos] == '?' && p->src[p->pos + 1] == ':') {
        p->pos += 2;
        neverc_regexp_syntax_node_t *inner = parse_alternation(p);
        if (!inner) goto done;
        if (next(p) != ')') {
            p->err = "unclosed group";
            neverc_regexp_syntax_free(inner);
            goto done;
        }
        result = inner;
        goto done;
    }

    /* Named capture: (?P<name>...) (?<name>...) (?'name'...). Go/RE2 require
     * the '<' after ?P; skipping 3 bytes for any `(?P` turned `(?Pname>x)`
     * into a capture named "ame" and `(?P name>x)` into "name". */
    if (p->pos < p->len && p->src[p->pos] == '?') {
        int skip = 0;
        char close_ch = '>';
        if (p->pos + 2 < p->len && p->src[p->pos + 1] == 'P' &&
            p->src[p->pos + 2] == '<') {
            skip = 3;
        } else if (p->pos + 1 < p->len && p->src[p->pos + 1] == '<') {
            skip = 2;
        } else if (p->pos + 1 < p->len && p->src[p->pos + 1] == '\'') {
            skip = 2;
            close_ch = '\'';
        } else {
            p->err = "invalid or unsupported Perl syntax";
            goto done;
        }
        p->pos += skip;
        int name_start = p->pos;
        while (p->pos < p->len && p->src[p->pos] != close_ch) p->pos++;
        if (p->pos >= p->len) {
            p->err = "unclosed capture name";
            goto done;
        }
        int name_len = p->pos - name_start;
        p->pos++; /* skip > or ' */
        if (name_len < 1) {
            p->err = "invalid named capture";
            goto done;
        }
        for (int i = 0; i < name_len; i++) {
            unsigned char ch = (unsigned char)p->src[name_start + i];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                  (ch >= '0' && ch <= '9') || ch == '_')) {
                p->err = "invalid named capture";
                goto done;
            }
        }

        neverc_regexp_syntax_node_t *cap = mk_node(p, NC_RE_OP_CAPTURE);
        if (!cap) goto done;
        if (p->ncap == INT_MAX) {
            p->err = "too many capturing groups";
            neverc_regexp_syntax_free(cap);
            goto done;
        }
        cap->cap = ++p->ncap;
        cap->name = (char *)malloc((size_t)name_len + 1);
        if (!cap->name) {
            p->err = "out of memory";
            neverc_regexp_syntax_free(cap);
            goto done;
        }
        nc_mcpy(cap->name, p->src + name_start, (size_t)name_len);
        cap->name[name_len] = '\0';

        neverc_regexp_syntax_node_t *inner = parse_alternation(p);
        if (!inner) { neverc_regexp_syntax_free(cap); goto done; }
        if (!add_sub(p, cap, inner)) {
            neverc_regexp_syntax_free(inner);
            neverc_regexp_syntax_free(cap);
            goto done;
        }
        if (next(p) != ')') {
            p->err = "unclosed group";
            neverc_regexp_syntax_free(cap);
            goto done;
        }
        result = cap;
        goto done;
    }

    /* Regular capturing group */
    neverc_regexp_syntax_node_t *cap = mk_node(p, NC_RE_OP_CAPTURE);
    if (!cap) goto done;
    if (p->ncap == INT_MAX) {
        p->err = "too many capturing groups";
        neverc_regexp_syntax_free(cap);
        goto done;
    }
    cap->cap = ++p->ncap;
    neverc_regexp_syntax_node_t *inner = parse_alternation(p);
    if (!inner) { neverc_regexp_syntax_free(cap); goto done; }
    if (!add_sub(p, cap, inner)) {
        neverc_regexp_syntax_free(inner);
        neverc_regexp_syntax_free(cap);
        goto done;
    }
    if (next(p) != ')') {
        p->err = "unclosed group";
        neverc_regexp_syntax_free(cap);
        goto done;
    }
    result = cap;
done:
    p->depth--;
    return result;
}

static neverc_regexp_syntax_node_t *parse_atom(parser_t *p) {
    int c = peek(p);
    if (c < 0) return NULL;

    switch (c) {
    case '(': next(p); return parse_group(p);
    case '[': next(p); return parse_char_class(p);
    case '.':
        next(p);
        return mk_node(p, (p->flags & NC_RE_FLAG_DOT_NL) ?
                           NC_RE_OP_ANY_CHAR : NC_RE_OP_ANY_CHAR_NOT_NL);
    case '^':
        next(p);
        {
            neverc_regexp_syntax_node_t *n = mk_node(p,
                (p->flags & NC_RE_FLAG_MULTI_LINE) ?
                    NC_RE_OP_BEGIN_LINE : NC_RE_OP_BEGIN_TEXT);
            if (!n) return NULL;
            if (!(p->flags & NC_RE_FLAG_MULTI_LINE))
                n->flags |= NC_RE_FLAG_WAS_CARET;
            return n;
        }
    case '$':
        next(p);
        {
            neverc_regexp_syntax_node_t *n = mk_node(p,
                (p->flags & NC_RE_FLAG_MULTI_LINE) ? NC_RE_OP_END_LINE : NC_RE_OP_END_TEXT);
            if (!n) return NULL;
            n->flags |= NC_RE_FLAG_WAS_DOLLAR;
            return n;
        }
    case '\\':
        next(p);
        return parse_escape(p);
    case ')': case '|':
        return NULL;
    case '*': case '+': case '?':
        p->err = "unexpected repetition operator";
        return NULL;
    default: {
        int r;
        if (!next_rune(p, &r)) return NULL;
        return literal_node(p, r);
    }
    }
}

static neverc_regexp_syntax_node_t *parse_repeat(parser_t *p) {
    neverc_regexp_syntax_node_t *atom = parse_atom(p);
    if (!atom) return NULL;

    int c = peek(p);
    neverc_regexp_syntax_node_t *rep = NULL;

    switch (c) {
    case '*': next(p); rep = mk_node(p, NC_RE_OP_STAR); break;
    case '+': next(p); rep = mk_node(p, NC_RE_OP_PLUS); break;
    case '?': next(p); rep = mk_node(p, NC_RE_OP_QUEST); break;
    case '{': {
        /* Go/RE2: `{` is a quantifier only when a digit follows; otherwise
         * it is a literal (so `a{}` and `{3}` parse as ordinary text).
         * Leading zeros (`{01}`, `{0,01}`) also make `{` a literal (Go parseInt). */
        if (p->pos + 1 >= p->len ||
            p->src[p->pos + 1] < '0' || p->src[p->pos + 1] > '9' ||
            brace_repeat_leading_zeros(p->src, p->pos, p->len))
            return atom;
        int brace_pos = p->pos;
        next(p);
        int min_val;
        if (!parse_int(p, &min_val)) {
            /* Go: `{` is a literal unless `{min}`, `{min,}`, or `{min,max}`
             * parses completely. `a{3,` and `a{3,x}` are ordinary text. */
            p->err = NULL;
            p->pos = brace_pos;
            return atom;
        }
        int max_val = min_val;
        if (peek(p) == ',') {
            next(p);
            if (peek(p) == '}')
                max_val = -1;
            else if (!parse_int(p, &max_val)) {
                p->err = NULL;
                p->pos = brace_pos;
                return atom;
            }
        }
        if (peek(p) != '}') {
            /* Go: unclosed `{n` is a literal, not a syntax error. */
            p->pos = brace_pos;
            return atom;
        }
        next(p);
        if (min_val > NCI_RE_MAX_REPEAT || max_val > NCI_RE_MAX_REPEAT) {
            p->err = "invalid repeat count";
            neverc_regexp_syntax_free(atom);
            return NULL;
        }
        if (max_val >= 0 && max_val < min_val) {
            p->err = "invalid repeat range";
            neverc_regexp_syntax_free(atom);
            return NULL;
        }
        rep = mk_node(p, NC_RE_OP_REPEAT);
        if (!rep) {
            neverc_regexp_syntax_free(atom);
            return NULL;
        }
        rep->min = min_val;
        rep->max = max_val;
        break;
    }
    default:
        return atom;
    }

    if (!rep) {
        neverc_regexp_syntax_free(atom);
        return NULL;
    }

    rep->flags |= p->flags & NC_RE_FLAG_NON_GREEDY;

    if (peek(p) == '?') {
        next(p);
        rep->flags ^= NC_RE_FLAG_NON_GREEDY;
    }

    /* Go/Perl: a second *+? or a complete {n}/{n,}/{n,m} after a quantifier
     * is invalid (a{2}*, a**). `{01}` and unclosed `{3` are literals
     * (Go parseRepeat), not stacked repeats. A single trailing '?' is the
     * non-greedy flag, already consumed above. */
    {
        int nxt = peek(p);
        if (nxt == '*' || nxt == '+' || nxt == '?' ||
            complete_brace_repeat(p->src, p->pos, p->len)) {
            p->err = "invalid nested repetition operator";
            neverc_regexp_syntax_free(atom);
            neverc_regexp_syntax_free(rep);
            return NULL;
        }
    }

    if (!add_sub(p, rep, atom)) {
        neverc_regexp_syntax_free(atom);
        neverc_regexp_syntax_free(rep);
        return NULL;
    }
    return rep;
}

static neverc_regexp_syntax_node_t *parse_concat(parser_t *p) {
    neverc_regexp_syntax_node_t *cat = mk_node(p, NC_RE_OP_CONCAT);
    if (!cat) return NULL;

    while (p->pos < p->len && peek(p) != '|' && peek(p) != ')') {
        neverc_regexp_syntax_node_t *sub = parse_repeat(p);
        if (!sub) {
            if (p->err) { neverc_regexp_syntax_free(cat); return NULL; }
            break;
        }
        if (!add_sub(p, cat, sub)) {
            neverc_regexp_syntax_free(sub);
            neverc_regexp_syntax_free(cat);
            return NULL;
        }
    }

    if (cat->nsubs == 0) {
        neverc_regexp_syntax_free(cat);
        return mk_node(p, NC_RE_OP_EMPTY_MATCH);
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

    neverc_regexp_syntax_node_t *alt = mk_node(p, NC_RE_OP_ALTERNATE);
    if (!alt) {
        neverc_regexp_syntax_free(left);
        return NULL;
    }
    if (!add_sub(p, alt, left)) {
        neverc_regexp_syntax_free(left);
        neverc_regexp_syntax_free(alt);
        return NULL;
    }

    while (peek(p) == '|') {
        next(p);
        neverc_regexp_syntax_node_t *branch = parse_concat(p);
        if (!branch) { if (p->err) { neverc_regexp_syntax_free(alt); return NULL; } break; }
        if (!add_sub(p, alt, branch)) {
            neverc_regexp_syntax_free(branch);
            neverc_regexp_syntax_free(alt);
            return NULL;
        }
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
    if (errp) *errp = NULL;
    if (!pattern) {
        if (errp) *errp = "null pattern";
        return NULL;
    }
    size_t pattern_len = nc_slen(pattern);
    if (pattern_len > INT_MAX) {
        if (errp) *errp = "pattern too long";
        return NULL;
    }
    if (!utf8_valid(pattern, pattern_len)) {
        if (errp) *errp = "invalid UTF-8";
        return NULL;
    }
    parser_t p;
    p.src = pattern;
    p.pos = 0;
    p.len = (int)pattern_len;
    p.flags = flags & ~NC_RE_FLAG_CLASS_NEGATED;
    p.ncap = 0;
    p.depth = 0;
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
    int failed;
} strbuf_t;

static void sb_init(strbuf_t *sb) {
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    sb->failed = 0;
}

static int sb_grow(strbuf_t *sb, size_t extra) {
    if (sb->failed || sb->len == SIZE_MAX ||
        extra > SIZE_MAX - sb->len - 1) {
        sb->failed = 1;
        return 0;
    }
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return 1;
    size_t nc = sb->cap < 16 ? 16 : sb->cap;
    while (nc < need) {
        if (nc > SIZE_MAX / 2) {
            nc = need;
            break;
        }
        nc *= 2;
    }
    char *grown = (char *)realloc(sb->buf, nc);
    if (!grown) {
        sb->failed = 1;
        return 0;
    }
    sb->buf = grown;
    sb->cap = nc;
    return 1;
}

static void sb_putc(strbuf_t *sb, char c) {
    if (!sb_grow(sb, 1)) return;
    sb->buf[sb->len++] = c;
}

static void sb_puts(strbuf_t *sb, const char *s) {
    while (*s && !sb->failed) sb_putc(sb, *s++);
}

static void sb_putint(strbuf_t *sb, int v) {
    char tmp[16];
    int i = 0;
    if (v == 0) { sb_putc(sb, '0'); return; }
    while (v > 0) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) sb_putc(sb, tmp[--i]);
}

static int is_meta(int c);

static void sb_puthex(strbuf_t *sb, unsigned v) {
    static const char hex[] = "0123456789abcdef";
    char tmp[8];
    int i = 0;
    if (v == 0) { sb_putc(sb, '0'); return; }
    while (v > 0 && i < 8) { tmp[i++] = hex[v & 15]; v >>= 4; }
    while (i > 0) sb_putc(sb, tmp[--i]);
}

static void sb_putrune(strbuf_t *sb, int r, int in_class) {
    if (r < 0) return;
    if (r >= 32 && r < 127) {
        if (in_class) {
            if (r == ']' || r == '\\' || r == '-' || r == '^')
                sb_putc(sb, '\\');
        } else if (is_meta(r)) {
            sb_putc(sb, '\\');
        }
        sb_putc(sb, (char)r);
        return;
    }
    sb_puts(sb, "\\x{");
    sb_puthex(sb, (unsigned)r);
    sb_putc(sb, '}');
}

static char *sb_finish(strbuf_t *sb) {
    if (!sb_grow(sb, 0)) {
        free(sb->buf);
        return NULL;
    }
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
    case NC_RE_OP_LITERAL: {
        int folded = (n->flags & NC_RE_FLAG_FOLD_CASE) != 0;
        if (folded) sb_puts(sb, "(?i:");
        for (int i = 0; i < n->nrunes; i++)
            sb_putrune(sb, n->runes[i], 0);
        if (folded) sb_putc(sb, ')');
        break;
    }
    case NC_RE_OP_CHAR_CLASS: {
        int folded = (n->flags & NC_RE_FLAG_FOLD_CASE) != 0;
        int negate = (n->flags & NC_RE_FLAG_CLASS_NEGATED) != 0;
        if (folded) sb_puts(sb, "(?i:");
        sb_putc(sb, '[');
        if (negate) sb_putc(sb, '^');
        for (int i = 0; i + 1 < n->nrunes; i += 2) {
            int lo = n->runes[i], hi = n->runes[i + 1];
            if (lo == hi) {
                sb_putrune(sb, lo, 1);
            } else {
                sb_putrune(sb, lo, 1);
                sb_putc(sb, '-');
                sb_putrune(sb, hi, 1);
            }
        }
        sb_putc(sb, ']');
        if (folded) sb_putc(sb, ')');
        break;
    }
    case NC_RE_OP_ANY_CHAR_NOT_NL: sb_putc(sb, '.'); break;
    case NC_RE_OP_ANY_CHAR: sb_puts(sb, "(?s:.)"); break;
    case NC_RE_OP_BEGIN_LINE: sb_putc(sb, '^'); break;
    case NC_RE_OP_END_LINE: sb_putc(sb, '$'); break;
    case NC_RE_OP_BEGIN_TEXT:
        sb_puts(sb, (n->flags & NC_RE_FLAG_WAS_CARET) ? "^" : "\\A");
        break;
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
            int need_paren = (n->subs[0]->op == NC_RE_OP_CONCAT ||
                              n->subs[0]->op == NC_RE_OP_ALTERNATE ||
                              (n->subs[0]->op == NC_RE_OP_LITERAL &&
                               n->subs[0]->nrunes > 1));
            if (need_paren) sb_puts(sb, "(?:");
            node_to_str(n->subs[0], sb);
            if (need_paren) sb_putc(sb, ')');
        }
        sb_putc(sb, '*');
        if (n->flags & NC_RE_FLAG_NON_GREEDY) sb_putc(sb, '?');
        break;
    case NC_RE_OP_PLUS:
        if (n->nsubs > 0) {
            int need_paren = (n->subs[0]->op == NC_RE_OP_CONCAT ||
                              n->subs[0]->op == NC_RE_OP_ALTERNATE ||
                              (n->subs[0]->op == NC_RE_OP_LITERAL &&
                               n->subs[0]->nrunes > 1));
            if (need_paren) sb_puts(sb, "(?:");
            node_to_str(n->subs[0], sb);
            if (need_paren) sb_putc(sb, ')');
        }
        sb_putc(sb, '+');
        if (n->flags & NC_RE_FLAG_NON_GREEDY) sb_putc(sb, '?');
        break;
    case NC_RE_OP_QUEST:
        if (n->nsubs > 0) {
            int need_paren = (n->subs[0]->op == NC_RE_OP_CONCAT ||
                              n->subs[0]->op == NC_RE_OP_ALTERNATE ||
                              (n->subs[0]->op == NC_RE_OP_LITERAL &&
                               n->subs[0]->nrunes > 1));
            if (need_paren) sb_puts(sb, "(?:");
            node_to_str(n->subs[0], sb);
            if (need_paren) sb_putc(sb, ')');
        }
        sb_putc(sb, '?');
        if (n->flags & NC_RE_FLAG_NON_GREEDY) sb_putc(sb, '?');
        break;
    case NC_RE_OP_REPEAT:
        if (n->nsubs > 0) {
            int need_paren = (n->subs[0]->op == NC_RE_OP_CONCAT ||
                              n->subs[0]->op == NC_RE_OP_ALTERNATE ||
                              (n->subs[0]->op == NC_RE_OP_LITERAL &&
                               n->subs[0]->nrunes > 1));
            if (need_paren) sb_puts(sb, "(?:");
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
        for (int i = 0; i < n->nsubs; i++) {
            if (n->subs[i]->op == NC_RE_OP_ALTERNATE) {
                sb_puts(sb, "(?:");
                node_to_str(n->subs[i], sb);
                sb_putc(sb, ')');
            } else {
                node_to_str(n->subs[i], sb);
            }
        }
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
        if (!r) return NULL;
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
        if (a->nrunes != b->nrunes) return 0;
        for (int i = 0; i < a->nrunes; i++)
            if (a->runes[i] != b->runes[i]) return 0;
        if ((a->flags & NC_RE_FLAG_FOLD_CASE) != (b->flags & NC_RE_FLAG_FOLD_CASE)) return 0;
        return 1;
    case NC_RE_OP_CHAR_CLASS:
        if (a->nrunes != b->nrunes) return 0;
        for (int i = 0; i < a->nrunes; i++)
            if (a->runes[i] != b->runes[i]) return 0;
        if ((a->flags & NC_RE_FLAG_FOLD_CASE) !=
            (b->flags & NC_RE_FLAG_FOLD_CASE))
            return 0;
        if ((a->flags & NC_RE_FLAG_CLASS_NEGATED) !=
            (b->flags & NC_RE_FLAG_CLASS_NEGATED))
            return 0;
        return 1;
    case NC_RE_OP_BEGIN_TEXT:
        return (a->flags & NC_RE_FLAG_WAS_CARET) == (b->flags & NC_RE_FLAG_WAS_CARET);
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
