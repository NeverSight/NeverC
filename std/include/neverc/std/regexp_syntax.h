#ifndef NEVERC_REGEXP_SYNTAX_H
#define NEVERC_REGEXP_SYNTAX_H

/*
 * NeverC regexp/syntax — regex parse tree (mirrors Go regexp/syntax).
 *
 * Parses regular expression patterns into abstract syntax trees.
 * Supports: literal, char class, ., *, +, ?, {n,m}, |, (), (?:),
 *           anchors (^ $ \b \B \A \z), escapes (\d \w \s \D \W \S).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NC_RE_OP_NO_MATCH = 1,
    NC_RE_OP_EMPTY_MATCH,
    NC_RE_OP_LITERAL,
    NC_RE_OP_CHAR_CLASS,
    NC_RE_OP_ANY_CHAR_NOT_NL,
    NC_RE_OP_ANY_CHAR,
    NC_RE_OP_BEGIN_LINE,
    NC_RE_OP_END_LINE,
    NC_RE_OP_BEGIN_TEXT,
    NC_RE_OP_END_TEXT,
    NC_RE_OP_WORD_BOUNDARY,
    NC_RE_OP_NO_WORD_BOUNDARY,
    NC_RE_OP_CAPTURE,
    NC_RE_OP_STAR,
    NC_RE_OP_PLUS,
    NC_RE_OP_QUEST,
    NC_RE_OP_REPEAT,
    NC_RE_OP_CONCAT,
    NC_RE_OP_ALTERNATE
} neverc_regexp_op_t;

typedef enum {
    NC_RE_FLAG_FOLD_CASE   = 1 << 0,
    NC_RE_FLAG_NON_GREEDY  = 1 << 1,
    NC_RE_FLAG_DOT_NL      = 1 << 2,
    NC_RE_FLAG_MULTI_LINE  = 1 << 3,
    NC_RE_FLAG_PERL        = 1 << 4,
    NC_RE_FLAG_POSIX       = 1 << 5,
    NC_RE_FLAG_WAS_DOLLAR  = 1 << 6
} neverc_regexp_flags_t;

typedef struct neverc_regexp_syntax_node {
    neverc_regexp_op_t    op;
    int                   flags;

    struct neverc_regexp_syntax_node **subs;
    int                   nsubs;

    /* For OpLiteral: the literal characters; for OpCharClass: range pairs */
    int                  *runes;
    int                   nrunes;

    int                   min, max;   /* for OpRepeat */
    int                   cap;        /* for OpCapture: capture index */
    char                 *name;       /* for OpCapture: capture name */
} neverc_regexp_syntax_node_t;

/* Parse a regex pattern into an AST. Returns NULL on error; *errp set. */
neverc_regexp_syntax_node_t *neverc_regexp_syntax_parse(
    const char *pattern, int flags, const char **errp);

/* Free an AST tree (recursive). */
void neverc_regexp_syntax_free(neverc_regexp_syntax_node_t *node);

/* Convert AST back to regex string. Caller frees result. */
char *neverc_regexp_syntax_string(const neverc_regexp_syntax_node_t *node);

/* Check structural equality of two ASTs. */
int neverc_regexp_syntax_equal(const neverc_regexp_syntax_node_t *a,
                                const neverc_regexp_syntax_node_t *b);

/* Return human-readable name for an Op. */
const char *neverc_regexp_syntax_op_string(neverc_regexp_op_t op);

/* Count total nodes in the tree. */
int neverc_regexp_syntax_node_count(const neverc_regexp_syntax_node_t *node);

#ifdef __cplusplus
}
#endif

/* ===== Std Module Dot-Syntax Support ===== */
#ifdef __neverc__
struct __neverc_std_regexp_syntax_t { char __tag; };
extern struct __neverc_std_regexp_syntax_t __neverc_mod_regexp_syntax;
#endif

#endif /* NEVERC_REGEXP_SYNTAX_H */
