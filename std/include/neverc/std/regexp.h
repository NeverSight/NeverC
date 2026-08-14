#ifndef NEVERC_REGEXP_H
#define NEVERC_REGEXP_H

/*
 * NeverC regexp — Thompson-NFA regular expressions inspired by Go regexp.
 *
 * This is a deliberately small byte-oriented syntax subset, not a complete
 * implementation of Go regexp or RE2. Matching APIs require the whole string;
 * find/replace/split use leftmost-longest non-empty matches.
 * No backtracking is used.
 * Supported syntax: . * + ? | () [] [^] \d \D \w \W \s \S \n \t \r \f \v ^ $ {n} {n,m}
 * Character classes: [a-z] [^abc] []] \d \D \w \W \s \S; \s includes VT/FF.
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

/* QuoteMeta: escape all regex metacharacters in s.  Caller frees result. */
char *neverc_regexp_quote_meta(const char *s);

/* CompilePOSIX: the supported syntax subset with leftmost-longest semantics. */
neverc_regexp_t *neverc_regexp_compile_posix(const char *pattern, const char **errp);

/* MustCompile: like Compile but aborts on error */
neverc_regexp_t *neverc_regexp_must_compile(const char *pattern);
neverc_regexp_t *neverc_regexp_must_compile_posix(const char *pattern);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_regexp_t { char __tag; };
extern struct __neverc_std_regexp_t __neverc_mod_regexp;
extern struct __neverc_std_regexp_t regexp;
#endif

#endif /* NEVERC_REGEXP_H */
