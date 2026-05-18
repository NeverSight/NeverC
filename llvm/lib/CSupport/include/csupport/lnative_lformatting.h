#ifndef CSUPPORT_LNATIVE_LFORMATTING_H
#define CSUPPORT_LNATIVE_LFORMATTING_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_format_hex(char *buf, size_t buflen, uint64_t value,
                        int upper_case);
int csupport_format_decimal(char *buf, size_t buflen, int64_t value);
int csupport_format_udecimal(char *buf, size_t buflen, uint64_t value);
int csupport_format_double(char *buf, size_t buflen, double value,
                           int precision);

int csupport_format_integer_to_buf(char *buf, size_t buflen, uint64_t value,
                                   size_t min_digits, int with_commas,
                                   int is_negative);

int csupport_format_hex_to_buf(char *buf, size_t buflen, uint64_t value,
                               int upper, int prefix, size_t min_width);

size_t csupport_default_float_precision(int style);

/* Format a double with style (0=exp,1=EXP,2=fixed,3=percent).
   Handles NaN/Inf. Returns chars written. */
int csupport_format_double_ex(char *buf, size_t buflen, double value, int style,
                              int precision);

/* Trim leading zero in exponent: "1.23e+012" -> "1.23e+12".
   Returns new length. */
int csupport_trim_exponent_zeros(char *buf, size_t len);

/* Write escaped string to buffer (replaces raw_ostream::write_escaped).
   use_hex_escapes: 1=\xNN, 0=\NNN octal. Returns written length. */
int csupport_write_escaped_to_buf(const char *str, size_t str_len, char *buf,
                                  size_t buf_cap, int use_hex_escapes);

/* Format UUID (16 bytes) to "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX".
   buf must be >= 37 bytes. Returns written length or -1 on error. */
int csupport_format_uuid_to_buf(const unsigned char uuid[16], char *buf,
                                size_t buf_cap);

int csupport_format_binary(char *buf, size_t buflen, uint64_t value,
                           size_t min_width);
int csupport_format_octal(char *buf, size_t buflen, uint64_t value,
                          size_t min_width);
int csupport_format_padded(char *buf, size_t buflen, const char *str,
                           size_t str_len, size_t width, char pad_char,
                           int align_right);
int csupport_format_size_human(char *buf, size_t buflen, uint64_t bytes);

int csupport_format_pointer(char *buf, size_t buflen, uint64_t ptr);
int csupport_format_indent(char *buf, size_t buflen, unsigned depth,
                           unsigned indent_size);
int csupport_is_print_char(char c);
size_t csupport_count_leading_spaces(const char *str, size_t len);
size_t csupport_count_trailing_spaces(const char *str, size_t len);
int csupport_format_hex_bytes(char *buf, size_t buflen,
                              const unsigned char *data, size_t data_len,
                              int upper_case, const char *separator);

void csupport_compute_justify_padding(size_t str_len, size_t width, int justify,
                                      unsigned *left_pad, unsigned *right_pad);
int csupport_format_number_decimal_padded(char *buf, size_t buflen,
                                          int64_t value, unsigned width);
int csupport_format_bytes_line(char *buf, size_t buflen,
                               const unsigned char *data, size_t data_len,
                               size_t num_per_line, size_t byte_group_size,
                               int upper_case, int show_ascii);

size_t csupport_write_padding_char(char *buf, size_t buf_cap, char pad_char,
                                   unsigned count);

int csupport_format_formatted_number(char *buf, size_t buflen,
                                     int64_t dec_value, unsigned width);

int csupport_format_justify(char *buf, size_t buflen, const char *str,
                            size_t str_len, size_t width, int align,
                            char fill_char);

int csupport_format_str_repeat(char *buf, size_t buflen, const char *str,
                               size_t str_len, unsigned count);
int csupport_format_hex_dump(char *buf, size_t buflen,
                             const unsigned char *data, size_t data_len,
                             size_t offset, unsigned bytes_per_line);
int csupport_format_with_commas(char *buf, size_t buflen, int64_t value);

int csupport_format_file_size(char *buf, size_t buflen, uint64_t bytes,
                              int use_si);

int csupport_center_string(char *buf, size_t buflen, const char *str,
                           size_t str_len, unsigned width, char fill);

int csupport_left_justify(char *buf, size_t buflen, const char *str,
                          size_t str_len, unsigned width, char fill);

int csupport_right_justify(char *buf, size_t buflen, const char *str,
                           size_t str_len, unsigned width, char fill);

int csupport_format_integer_width(char *buf, size_t buflen, int64_t val,
                                  unsigned min_width, int pad_zero);

int csupport_format_unsigned_width(char *buf, size_t buflen, uint64_t val,
                                   unsigned min_width, int pad_zero);

int csupport_format_bool(char *buf, size_t buflen, int val);

#ifdef __cplusplus
}
#endif
#endif
