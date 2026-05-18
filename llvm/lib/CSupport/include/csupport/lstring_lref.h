#ifndef CSUPPORT_LSTRING_LREF_H
#define CSUPPORT_LSTRING_LREF_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_str_compare_insensitive(const char *a, size_t a_len, const char *b,
                                     size_t b_len);
int csupport_str_compare_numeric(const char *a, size_t a_len, const char *b,
                                 size_t b_len);
size_t csupport_str_find_substr(const char *haystack, size_t hay_len,
                                const char *needle, size_t nee_len,
                                size_t from);
size_t csupport_str_rfind(const char *data, size_t len, const char *needle,
                          size_t nee_len);
size_t csupport_str_find_first_of(const char *data, size_t len,
                                  const char *chars, size_t chars_len,
                                  size_t from);
size_t csupport_str_find_first_not_of(const char *data, size_t len,
                                      const char *chars, size_t chars_len,
                                      size_t from);
size_t csupport_str_count(const char *data, size_t len, const char *needle,
                          size_t nee_len);
int csupport_consume_unsigned(const char **str, size_t *len, unsigned radix,
                              unsigned long long *result);
int csupport_consume_signed(const char **str, size_t *len, unsigned radix,
                            long long *result);

size_t csupport_str_find_char_insensitive(const char *data, size_t len, char c,
                                          size_t from);
size_t csupport_str_find_insensitive(const char *data, size_t len,
                                     const char *needle, size_t nee_len,
                                     size_t from);
size_t csupport_str_rfind_char_insensitive(const char *data, size_t len, char c,
                                           size_t from);
size_t csupport_str_rfind_insensitive(const char *data, size_t len,
                                      const char *needle, size_t nee_len);
int csupport_str_starts_with_insensitive(const char *data, size_t len,
                                         const char *prefix, size_t plen);
int csupport_str_ends_with_insensitive(const char *data, size_t len,
                                       const char *suffix, size_t slen);
size_t csupport_str_find_last_of(const char *data, size_t len,
                                 const char *chars, size_t chars_len,
                                 size_t from);
size_t csupport_str_find_first_not_of_char(const char *data, size_t len, char c,
                                           size_t from);
size_t csupport_str_find_last_not_of_char(const char *data, size_t len, char c,
                                          size_t from);
size_t csupport_str_find_last_not_of(const char *data, size_t len,
                                     const char *chars, size_t chars_len,
                                     size_t from);
unsigned csupport_auto_sense_radix(const char **str, size_t *len);
int csupport_detect_unicode_encoding(const unsigned char *data, size_t len,
                                     unsigned *bom_len);

int csupport_cl_edit_distance(const char *a, size_t a_len, const char *b,
                              size_t b_len, int allow_replacements,
                              unsigned max_dist);
int csupport_cl_edit_distance_insensitive(const char *a, size_t a_len,
                                          const char *b, size_t b_len,
                                          int allow_replacements,
                                          unsigned max_dist);

#ifdef __cplusplus
}
#endif
#endif
