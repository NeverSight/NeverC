#ifndef CSUPPORT_LREGEX_H
#define CSUPPORT_LREGEX_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_regex_is_literal(const char *pattern, size_t len);

int csupport_regex_escape(const char *src, size_t src_len, char *dst,
                          size_t dst_size, size_t *out_len);

int csupport_is_regex_metachar(char c);

int csupport_wildcard_to_regex(const char *glob, size_t glob_len, char *regex,
                               size_t regex_size, size_t *out_len);

/* Count capturing groups in a regex pattern */
int csupport_regex_count_groups(const char *pattern, size_t len);

/* Regex substitution: apply replacement template to matched string.
   match_starts/match_ends are parallel arrays of num_matches match positions.
   Returns the number of bytes written (may exceed out_size if buffer too
   small). err receives error messages (if non-NULL). */
size_t csupport_regex_sub(const char *repl, size_t repl_len, const char *orig,
                          size_t orig_len, const size_t *match_starts,
                          const size_t *match_ends, size_t num_matches,
                          char *out, size_t out_size, char *err,
                          size_t err_size);

/* Simple glob matching (supports * and ? wildcards).
   Returns 1 if matches, 0 otherwise. */
int csupport_simple_glob_match(const char *pattern, size_t plen,
                               const char *str, size_t slen);

size_t csupport_regex_find_literal_prefix(const char *pattern, size_t plen,
                                          char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
#endif
