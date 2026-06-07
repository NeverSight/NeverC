#ifndef NEVERC_REGEXP_H
#define NEVERC_REGEXP_H

/*
 * NeverC regexp — regular expressions (mirrors Go regexp package).
 *
 * NFA-based engine (Thompson construction). No backtracking.
 * Supported syntax: . * + ? | () [] [^] \d \w \s ^ $ {n} {n,m}
 * Character classes: [a-z] [^abc] \d (digit) \w (word) \s (space)
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_regexp neverc_regexp_t;

typedef struct {
    const char *start;
    size_t      len;
} neverc_regexp_match_t;

neverc_regexp_t *neverc_regexp_compile(const char *pattern, const char **errp);
void             neverc_regexp_free(neverc_regexp_t *re);

int  neverc_regexp_match(neverc_regexp_t *re, const char *s);
int  neverc_regexp_match_string(const char *pattern, const char *s);

const char *neverc_regexp_find(neverc_regexp_t *re, const char *s,
                               size_t *match_len);
int  neverc_regexp_find_submatch(neverc_regexp_t *re, const char *s,
                                 neverc_regexp_match_t *matches, int max_matches);

char **neverc_regexp_find_all(neverc_regexp_t *re, const char *s,
                              int n, int *count);

char *neverc_regexp_replace_all(neverc_regexp_t *re, const char *src,
                                const char *repl, size_t *outlen);

char **neverc_regexp_split(neverc_regexp_t *re, const char *s,
                           int n, int *count);

void neverc_regexp_free_strings(char **strs, int count);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_regexp_t { char __tag; };
extern struct __neverc_std_regexp_t regexp;
#endif

#endif /* NEVERC_REGEXP_H */
