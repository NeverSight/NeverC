#ifndef CSUPPORT_LUNICODE_LNAME_LTO_LCODEPOINT_H
#define CSUPPORT_LUNICODE_LNAME_LTO_LCODEPOINT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_unicode_normalize_name(const char *name, size_t name_len,
                                    char *out, size_t out_size,
                                    size_t *out_len);

int csupport_unicode_is_valid_name_char(char c);

/* UAX44-LM2 loose matching: returns consumed chars from name, 0 on failure */
size_t csupport_unicode_starts_with(const char *name, size_t name_len,
                                    const char *needle, size_t needle_len,
                                    int strict, char *prev_char_in_name,
                                    int is_prefix);

typedef struct {
  int is_root;
  uint32_t value;
  uint32_t children_offset;
  int has_sibling;
  uint32_t size;
  const char *name_data;
  size_t name_len;
} csupport_unicode_node_t;

csupport_unicode_node_t csupport_unicode_read_node(const uint8_t *index,
                                                   size_t index_size,
                                                   const char *dict,
                                                   uint32_t offset);

/* Hangul syllable lookup: returns codepoint or UINT32_MAX */
uint32_t csupport_unicode_name_to_hangul(const char *name, size_t name_len,
                                         int strict, char *matched_buf,
                                         size_t buf_size, size_t *matched_len);

/* Generated name lookup (CJK UNIFIED IDEOGRAPH-, etc) */
uint32_t csupport_unicode_name_to_generated(const char *name, size_t name_len,
                                            int strict, char *matched_buf,
                                            size_t buf_size,
                                            size_t *matched_len);

/* Tree-based name matching: returns codepoint or UINT32_MAX */
uint32_t csupport_unicode_compare_node(const uint8_t *index, size_t index_size,
                                       const char *dict, uint32_t offset,
                                       const char *name, size_t name_len,
                                       int strict, char prev_char_in_name,
                                       char *rev_buf, size_t rev_buf_size,
                                       size_t *rev_buf_len);

#ifdef __cplusplus
}
#endif
#endif
