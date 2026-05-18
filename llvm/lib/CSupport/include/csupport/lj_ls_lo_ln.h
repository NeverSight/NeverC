#ifndef CSUPPORT_LJ_LS_LO_LN_H
#define CSUPPORT_LJ_LS_LO_LN_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_json_is_whitespace(char c);
size_t csupport_json_skip_whitespace(const char *data, size_t len, size_t pos);
int csupport_json_escape_char(char c);
size_t csupport_json_escape_string(const char *src, size_t src_len, char *dst,
                                   size_t dst_cap);

/* UTF-8 encoding: encode a Unicode codepoint into buf, return bytes written */
int csupport_json_encode_utf8(uint32_t codepoint, char *buf, size_t buflen);

/* Decode 4 hex characters to a uint16_t, return 0 on success */
int csupport_json_decode_hex4(const char *hex, uint16_t *out);

/* Parse a JSON number from src[0..len), store result in *val.
   Return number of chars consumed, 0 on error. */
size_t csupport_json_parse_double(const char *src, size_t len, double *val);
size_t csupport_json_parse_int64(const char *src, size_t len, int64_t *val);

/* Quote a string for JSON output (adds surrounding quotes).
   Returns bytes written (not including NUL), 0 if buffer too small. */
size_t csupport_json_quote_string(const char *src, size_t src_len, char *dst,
                                  size_t dst_cap);

/* Check if a character can be part of a JSON number */
int csupport_json_is_number_char(char c);

/* Calculate line and column from a position in a JSON string */
void csupport_json_calc_line_col(const char *start, const char *pos, int *line,
                                 int *col);

/* Unescape a JSON escape character (reverse of escape_char).
   e.g. 'n' -> '\n'. Returns -1 if not a valid escape. */
int csupport_json_unescape_char(char c);

/* Parse a JSON number. Tries int64 first, then uint64, then double.
   *type: 0=error, 1=int64, 2=uint64, 3=double.
   Returns chars consumed. */
size_t csupport_json_parse_number_ex(const char *src, size_t len,
                                     int64_t *int_val, uint64_t *uint_val,
                                     double *dbl_val, int *type);

/* Decode a UTF-16 surrogate pair into a Unicode codepoint.
   Returns 0 on success, -1 if not a valid surrogate pair. */
int csupport_json_decode_surrogate_pair(uint16_t hi, uint16_t lo, uint32_t *cp);

/* Quote a string for raw_ostream-style JSON output (matching LLVM's quote()).
   Returns bytes written. */
size_t csupport_json_quote_to_stream(const char *src, size_t src_len, char *dst,
                                     size_t dst_cap);

/* Parse a JSON string body (leading '"' already consumed).
   Reads from *pos to end, handling escape sequences and \uXXXX.
   Writes decoded bytes to dst, up to dst_cap.
   Returns output length on success (may exceed dst_cap if truncated).
   Returns (size_t)-1 on error, sets *error_msg.
   *pos is always advanced (to after '"' on success, or error location). */
size_t csupport_json_parse_string_body(const char **pos, const char *end,
                                       char *dst, size_t dst_cap,
                                       const char **error_msg);

int csupport_json_format_int64(char *buf, size_t cap, int64_t val);
int csupport_json_format_uint64(char *buf, size_t cap, uint64_t val);
int csupport_json_format_double(char *buf, size_t cap, double val);
size_t csupport_json_format_value_null(char *buf, size_t cap);
size_t csupport_json_format_value_bool(char *buf, size_t cap, int val);
int csupport_json_validate_utf8(const char *data, size_t len);
size_t csupport_json_compact(const char *src, size_t src_len, char *dst,
                             size_t dst_cap);
size_t csupport_json_prettify(const char *src, size_t src_len, char *dst,
                              size_t dst_cap, unsigned indent_width);
int csupport_json_depth(const char *src, size_t src_len);
int csupport_json_is_valid_utf8(const char *src, size_t src_len);

size_t csupport_json_minify(const char *src, size_t src_len, char *dst,
                            size_t dst_cap);
int csupport_json_validate(const char *src, size_t src_len);
size_t csupport_json_pointer_get(const char *json, size_t json_len,
                                 const char *pointer, size_t ptr_len,
                                 const char **value_start);
size_t csupport_json_count_keys(const char *src, size_t src_len);
int csupport_json_match_literal(const char *src, size_t src_len, size_t pos,
                                const char *literal);
size_t csupport_json_format_object_entry(const char *key, size_t key_len,
                                         const char *value, size_t value_len,
                                         char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
#endif
