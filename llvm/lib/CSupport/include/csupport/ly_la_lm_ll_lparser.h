#ifndef CSUPPORT_LY_LA_LM_LL_LPARSER_H
#define CSUPPORT_LY_LA_LM_LL_LPARSER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_yaml_is_whitespace(char c);
int csupport_yaml_is_flow_indicator(char c);
int csupport_yaml_needs_quoting(const char *str, size_t len);

size_t csupport_yaml_scan_to_next_token(const char *input, size_t len,
                                        size_t start);

int csupport_yaml_decode_hex_escape(const char *start, size_t num_digits,
                                    uint32_t *out_codepoint);

size_t csupport_yaml_escape(const char *input, size_t input_len,
                            int escape_printable, char *out, size_t out_cap);

int csupport_yaml_encode_utf8(uint32_t cp, char *buf, size_t buflen);

int csupport_yaml_is_nb_char(const char *pos, const char *end);
int csupport_yaml_is_b_break(const char *pos, const char *end);
int csupport_yaml_is_blank_or_break(char c);

int csupport_yaml_parse_bool(const char *str, size_t len);

int csupport_yaml_skip_nb_char(const char *pos, const char *end);
int csupport_yaml_skip_ns_char(const char *pos, const char *end);
size_t csupport_yaml_skip_while_ns_char(const char *pos, const char *end);
size_t csupport_yaml_skip_while_s_white(const char *pos, const char *end);
size_t csupport_yaml_skip_while_nb_char(const char *pos, const char *end);
size_t csupport_yaml_skip_while_s_space(const char *pos, const char *end);

int csupport_yaml_was_escaped(const char *first, const char *position);
unsigned csupport_yaml_get_chomped_line_breaks(char chomping_indicator,
                                               unsigned line_breaks,
                                               const char *str, size_t str_len);
int csupport_yaml_is_ns_hex_digit(char c);
int csupport_yaml_is_ns_word_char(char c);
int csupport_yaml_is_ns_uri_char(char c, const char *next, const char *end);
int csupport_yaml_is_plain_safe_non_blank(char c, int flow_level);
int csupport_yaml_is_line_empty(const char *line, size_t len);

/* Decode one UTF-8 codepoint from data[0..len).
   Returns codepoint (0 on error). Sets *out_len to byte count consumed (0 on
   error). */
uint32_t csupport_yaml_decode_utf8(const char *data, size_t len,
                                   unsigned *out_len);

int csupport_yaml_scan_block_style_indicator(char c);
int csupport_yaml_scan_block_chomping_indicator(char c);
unsigned csupport_yaml_scan_block_indent_indicator(char c);
int csupport_yaml_is_document_indicator(const char *pos, const char *end,
                                        int column);
size_t csupport_yaml_skip_whitespace(const char *pos, const char *end);
size_t csupport_yaml_count_indent(const char *pos, const char *end);
int csupport_yaml_is_tag_char(char c);
size_t csupport_yaml_scan_tag_handle(const char *pos, size_t len,
                                     const char **tag_start);

int csupport_yaml_is_break(const char *pos, const char *end);
size_t csupport_yaml_skip_break(const char *pos, const char *end);
size_t csupport_yaml_scan_to_next_token_ex(const char *pos, const char *end,
                                           unsigned *column, unsigned *line,
                                           unsigned flow_level);
size_t csupport_yaml_normalize_line_breaks(const char *src, size_t src_len,
                                           char *out, size_t out_cap);
int csupport_yaml_scan_flow_indicator(char c);
int csupport_yaml_is_anchor_char(char c);
size_t csupport_yaml_scan_anchor(const char *pos, const char *end);
size_t csupport_yaml_scan_alias(const char *pos, const char *end);
int csupport_yaml_count_line_break_len(const char *pos, const char *end);
size_t csupport_yaml_fold_newlines(const char *src, size_t src_len, char *out,
                                   size_t out_cap);

int csupport_yaml_scan_flow_scalar_single(const char *p, size_t len, char *out,
                                          size_t cap, size_t *out_len,
                                          int *lines);
int csupport_yaml_scan_flow_scalar_double(const char *p, size_t len, char *out,
                                          size_t cap, size_t *out_len,
                                          int *lines);
int csupport_yaml_detect_scalar_style(const char *p, size_t len);
size_t csupport_yaml_strip_trailing_newlines(char *buf, size_t len);

int csupport_yaml_classify_token_start(char c, char next_c, int at_col0,
                                       int flow_level, int has_end);
int csupport_yaml_is_document_start(const char *p, const char *end, int column);
int csupport_yaml_is_document_end(const char *p, const char *end, int column);
int csupport_yaml_is_plain_first_char(char c);

int csupport_yaml_count_indent_chars(const char *p, const char *end);
size_t csupport_yaml_unescape_single_quoted(const char *src, size_t src_len,
                                            char *dst, size_t dst_cap);

int csupport_yaml_is_block_scalar_header(char c);

size_t csupport_yaml_unescape_double_quoted(const char *src, size_t src_len,
                                            char *dst, size_t dst_cap);

int csupport_yaml_detect_value_type(const char *p, size_t len);

size_t csupport_yaml_write_key_value(const char *key, size_t key_len,
                                     const char *val, size_t val_len,
                                     int indent, char *out, size_t out_cap);

/* Count lines in YAML text. */
size_t csupport_yaml_count_lines(const char *text, size_t len);

/* Check if a YAML scalar value needs quoting: 0=None, 1=Single, 2=Double */
int csupport_yaml_needs_quoting(const char *str, size_t len);

/* Write a YAML scalar with appropriate quoting to buffer. */
size_t csupport_yaml_write_scalar(const char *str, size_t len, int force_quote,
                                  char *out, size_t out_cap);

/* Trim trailing whitespace from YAML text.
 * Returns new length. */
size_t csupport_yaml_trim_trailing(const char *str, size_t len);

/* Format string as YAML block literal (|). Returns bytes written. */
size_t csupport_yaml_format_block_literal(const char *str, size_t len,
                                          int indent, char *buf, size_t cap);

/* Format items as YAML flow sequence [a, b, c]. Returns bytes written. */
size_t csupport_yaml_format_flow_sequence(const char **items, size_t count,
                                          char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
