#ifndef CSUPPORT_LGLOB_LPATTERN_H
#define CSUPPORT_LGLOB_LPATTERN_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_glob_match(const char *pattern, size_t pat_len, const char *str,
                        size_t str_len);

int csupport_glob_expand_charset(const char *chars, size_t chars_len,
                                 uint8_t bitmap[256]);

int csupport_glob_match_bracket(const uint8_t bitmap[256], char c);

int csupport_glob_match_advanced(const char *pat, size_t pat_len,
                                 const uint8_t *bracket_bitmaps,
                                 const size_t *bracket_offsets,
                                 size_t bracket_count, const char *str,
                                 size_t str_len);

typedef struct {
  size_t start;
  size_t length;
  size_t term_offsets[64];
  size_t term_lengths[64];
  size_t num_terms;
} csupport_brace_expansion_t;

/*
 * Parse brace expansion syntax from a glob pattern.
 * Returns number of brace expansions found, or -1 on error.
 * error_msg (if non-null) is set to a static error message on failure.
 */
int csupport_glob_parse_brace_expansions(const char *pat, size_t pat_len,
                                         csupport_brace_expansion_t *out,
                                         size_t max_expansions,
                                         const char **error_msg);

/*
 * Count the total sub-patterns that would result from brace expansion.
 * Returns the product of all term counts, or SIZE_MAX on overflow.
 */
size_t csupport_glob_count_sub_patterns(const csupport_brace_expansion_t *exps,
                                        size_t num_expansions);

#ifdef __cplusplus
}
#endif
#endif
