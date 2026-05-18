/*===- NativeFormatting.c - Number formatting (pure C) ----------*- C -*-===*/
#include "include/csupport/lnative_lformatting.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static const char upper_hex_digits[] = "0123456789ABCDEF";
static const char lower_hex_digits[] = "0123456789abcdef";

int csupport_format_hex(char *buf, size_t buflen, uint64_t value,
                        int upper_case) {
  const char *digits = upper_case ? upper_hex_digits : lower_hex_digits;
  char tmp[17];
  int pos = 0;

  if (value == 0) {
    tmp[pos++] = '0';
  } else {
    while (value) {
      tmp[pos++] = digits[value & 0xf];
      value >>= 4;
    }
  }

  if ((size_t)pos >= buflen) return -1;
  for (int i = 0; i < pos; i++)
    buf[i] = tmp[pos - 1 - i];
  buf[pos] = '\0';
  return pos;
}

int csupport_format_decimal(char *buf, size_t buflen, int64_t value) {
  return snprintf(buf, buflen, "%lld", (long long)value);
}

int csupport_format_udecimal(char *buf, size_t buflen, uint64_t value) {
  return snprintf(buf, buflen, "%llu", (unsigned long long)value);
}

int csupport_format_double(char *buf, size_t buflen, double value,
                           int precision) {
  if (precision < 0)
    return snprintf(buf, buflen, "%g", value);
  return snprintf(buf, buflen, "%.*f", precision, value);
}

static int format_to_end(char *buf, size_t buflen, uint64_t value) {
  char *end = buf + buflen;
  char *cur = end;
  do {
    *--cur = '0' + (char)(value % 10);
    value /= 10;
  } while (value && cur > buf);
  int len = (int)(end - cur);
  if (cur != buf) memmove(buf, cur, (size_t)len);
  return len;
}

int csupport_format_integer_to_buf(char *buf, size_t buflen, uint64_t value,
                                   size_t min_digits, int with_commas,
                                   int is_negative) {
  char numbuf[128];
  int len = format_to_end(numbuf, sizeof(numbuf), value);
  char *out = buf;
  char *end = buf + buflen;

  if (is_negative && out < end) *out++ = '-';

  if (!with_commas) {
    for (size_t i = (size_t)len; i < min_digits && out < end; i++)
      *out++ = '0';
    size_t copy = ((size_t)len < (size_t)(end - out))
                      ? (size_t)len
                      : (size_t)(end - out);
    memcpy(out, numbuf, copy);
    out += copy;
  } else {
    int initial = ((len - 1) % 3) + 1;
    size_t copied = 0;
    for (int i = 0; i < initial && out < end; i++)
      *out++ = numbuf[copied++];
    while (copied < (size_t)len) {
      if (out < end) *out++ = ',';
      for (int i = 0; i < 3 && copied < (size_t)len && out < end; i++)
        *out++ = numbuf[copied++];
    }
  }

  if (out < end) *out = '\0';
  return (int)(out - buf);
}

static int bit_width_64(uint64_t v) {
  if (v == 0) return 0;
  return 64 - __builtin_clzll(v);
}

int csupport_format_hex_to_buf(char *buf, size_t buflen, uint64_t value,
                               int upper, int prefix,
                               size_t min_width) {
  if (!buf || buflen == 0) return 0;
  const char *digits = upper ? upper_hex_digits : lower_hex_digits;
  int nibbles = (bit_width_64(value) + 3) / 4;
  if (nibbles < 1) nibbles = 1;

  size_t prefix_chars = prefix ? 2 : 0;
  size_t num_chars = (size_t)nibbles + prefix_chars;
  if (min_width > num_chars) num_chars = min_width;
  if (num_chars >= buflen) num_chars = buflen - 1;

  memset(buf, '0', num_chars);
  if (prefix) buf[1] = 'x';

  char *endp = buf + num_chars;
  char *cur = endp;
  uint64_t n = value;
  while (n && cur > buf + prefix_chars) {
    *--cur = digits[n % 16];
    n /= 16;
  }

  buf[num_chars] = '\0';
  return (int)num_chars;
}

size_t csupport_default_float_precision(int style) {
  switch (style) {
  case 0: /* Exponent */
  case 1: /* ExponentUpper */
    return 6;
  case 2: /* Fixed */
  case 3: /* Percent */
    return 2;
  default:
    return 6;
  }
}

static int is_negative_zero(double v) {
  if (v != 0.0) return 0;
  union { double d; uint64_t u; } u;
  u.d = v;
  return (u.u >> 63) != 0;
}

int csupport_format_double_ex(char *buf, size_t buflen, double value,
                              int style, int precision) {
  if (buflen == 0) return 0;

  if (value != value) { /* NaN */
    int n = snprintf(buf, buflen, "nan");
    return n > 0 ? n : 0;
  }
  if (value == 1.0/0.0 || value == -1.0/0.0) { /* Inf */
    int n = snprintf(buf, buflen, "%sINF", value < 0 ? "-" : "");
    return n > 0 ? n : 0;
  }

  int is_exp = (style == 0 || style == 1);

  if (is_exp && is_negative_zero(value)) {
    char letter = (style == 1) ? 'E' : 'e';
    int n = snprintf(buf, buflen, "-0.%0*d%c+00", precision, 0, letter);
    return (n > 0 && (size_t)n < buflen) ? n : 0;
  }

  double val = (style == 3) ? value * 100.0 : value;
  char fmt[16];
  switch (style) {
  case 0: snprintf(fmt, sizeof(fmt), "%%.%de", precision); break;
  case 1: snprintf(fmt, sizeof(fmt), "%%.%dE", precision); break;
  default: snprintf(fmt, sizeof(fmt), "%%.%df", precision); break;
  }
  int n = snprintf(buf, buflen, fmt, val);
  if (n < 0) n = 0;

  if (is_exp && n > 0)
    n = csupport_trim_exponent_zeros(buf, (size_t)n);

  if (style == 3 && (size_t)n < buflen - 1) {
    buf[n++] = '%';
    buf[n] = '\0';
  }
  return n;
}

int csupport_trim_exponent_zeros(char *buf, size_t len) {
  if (len < 5) return (int)len;
  char e = buf[len - 5];
  if (e != 'e' && e != 'E') return (int)len;
  int cs = buf[len - 4];
  if (cs != '+' && cs != '-') return (int)len;
  if (buf[len - 3] != '0') return (int)len;
  int c1 = buf[len - 2];
  int c0 = buf[len - 1];
  if (c1 < '0' || c1 > '9' || c0 < '0' || c0 > '9') return (int)len;
  buf[len - 3] = (char)c1;
  buf[len - 2] = (char)c0;
  buf[len - 1] = '\0';
  return (int)(len - 1);
}

int csupport_write_escaped_to_buf(const char *str, size_t str_len,
                                  char *buf, size_t buf_cap,
                                  int use_hex_escapes) {
  size_t pos = 0;
  for (size_t i = 0; i < str_len; i++) {
    unsigned char c = (unsigned char)str[i];
    const char *esc = NULL;
    size_t esc_len = 0;
    char hex_buf[5];

    switch (c) {
    case '\\': esc = "\\\\"; esc_len = 2; break;
    case '\t': esc = "\\t";  esc_len = 2; break;
    case '\n': esc = "\\n";  esc_len = 2; break;
    case '"':  esc = "\\\""; esc_len = 2; break;
    default:
      if (c >= 0x20 && c <= 0x7E) {
        if (pos < buf_cap) buf[pos] = (char)c;
        pos++;
        continue;
      }
      if (use_hex_escapes) {
        static const char hex[] = "0123456789abcdef";
        hex_buf[0] = '\\';
        hex_buf[1] = 'x';
        hex_buf[2] = hex[(c >> 4) & 0xF];
        hex_buf[3] = hex[c & 0xF];
        esc = hex_buf;
        esc_len = 4;
      } else {
        hex_buf[0] = '\\';
        hex_buf[1] = (char)('0' + ((c >> 6) & 7));
        hex_buf[2] = (char)('0' + ((c >> 3) & 7));
        hex_buf[3] = (char)('0' + (c & 7));
        esc = hex_buf;
        esc_len = 4;
      }
      break;
    }
    if (esc) {
      for (size_t j = 0; j < esc_len; j++) {
        if (pos < buf_cap) buf[pos] = esc[j];
        pos++;
      }
    }
  }
  if (pos < buf_cap) buf[pos] = '\0';
  return (int)pos;
}

int csupport_format_uuid_to_buf(const unsigned char uuid[16],
                                char *buf, size_t buf_cap) {
  if (buf_cap < 37) return -1;
  static const char hex[] = "0123456789ABCDEF";
  size_t pos = 0;
  for (int i = 0; i < 16; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      buf[pos++] = '-';
    }
    buf[pos++] = hex[(uuid[i] >> 4) & 0xF];
    buf[pos++] = hex[uuid[i] & 0xF];
  }
  buf[pos] = '\0';
  return (int)pos;
}

int csupport_format_binary(char *buf, size_t buflen, uint64_t value,
                           size_t min_width) {
  char tmp[65];
  int pos = 0;
  if (value == 0) {
    tmp[pos++] = '0';
  } else {
    while (value) {
      tmp[pos++] = '0' + (value & 1);
      value >>= 1;
    }
  }
  int padding = 0;
  if ((size_t)pos < min_width)
    padding = (int)(min_width - (size_t)pos);
  if ((size_t)(pos + padding) >= buflen) return -1;
  int out = 0;
  for (int i = 0; i < padding; i++)
    buf[out++] = '0';
  for (int i = pos - 1; i >= 0; i--)
    buf[out++] = tmp[i];
  buf[out] = '\0';
  return out;
}

int csupport_format_octal(char *buf, size_t buflen, uint64_t value,
                          size_t min_width) {
  char tmp[23];
  int pos = 0;
  if (value == 0) {
    tmp[pos++] = '0';
  } else {
    while (value) {
      tmp[pos++] = '0' + (char)(value & 7);
      value >>= 3;
    }
  }
  int padding = 0;
  if ((size_t)pos < min_width)
    padding = (int)(min_width - (size_t)pos);
  if ((size_t)(pos + padding) >= buflen) return -1;
  int out = 0;
  for (int i = 0; i < padding; i++)
    buf[out++] = '0';
  for (int i = pos - 1; i >= 0; i--)
    buf[out++] = tmp[i];
  buf[out] = '\0';
  return out;
}

int csupport_format_padded(char *buf, size_t buflen, const char *str,
                           size_t str_len, size_t width, char pad_char,
                           int align_right) {
  if (str_len >= width) {
    if (str_len >= buflen) return -1;
    memcpy(buf, str, str_len);
    buf[str_len] = '\0';
    return (int)str_len;
  }
  size_t padding = width - str_len;
  if (width >= buflen) return -1;
  int out = 0;
  if (align_right) {
    for (size_t i = 0; i < padding; i++)
      buf[out++] = pad_char;
    memcpy(buf + out, str, str_len);
    out += (int)str_len;
  } else {
    memcpy(buf + out, str, str_len);
    out += (int)str_len;
    for (size_t i = 0; i < padding; i++)
      buf[out++] = pad_char;
  }
  buf[out] = '\0';
  return out;
}

int csupport_format_size_human(char *buf, size_t buflen, uint64_t bytes) {
  static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  int unit = 0;
  double val = (double)bytes;
  while (val >= 1024.0 && unit < 5) {
    val /= 1024.0;
    unit++;
  }
  if (unit == 0)
    return snprintf(buf, buflen, "%llu B", (unsigned long long)bytes);
  return snprintf(buf, buflen, "%.1f %s", val, units[unit]);
}

int csupport_format_pointer(char *buf, size_t buflen, uint64_t ptr) {
  return snprintf(buf, buflen, "0x%016llx", (unsigned long long)ptr);
}

int csupport_format_indent(char *buf, size_t buflen, unsigned depth,
                           unsigned indent_size) {
  if (!buf || buflen == 0) return 0;
  unsigned total = depth * indent_size;
  if (total >= buflen) total = (unsigned)(buflen - 1);
  for (unsigned i = 0; i < total; i++)
    buf[i] = ' ';
  buf[total] = '\0';
  return (int)total;
}

int csupport_is_print_char(char c) {
  return (unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E;
}

size_t csupport_count_leading_spaces(const char *str, size_t len) {
  size_t count = 0;
  while (count < len && str[count] == ' ') count++;
  return count;
}

size_t csupport_count_trailing_spaces(const char *str, size_t len) {
  size_t count = 0;
  while (count < len && str[len - 1 - count] == ' ') count++;
  return count;
}

int csupport_format_hex_bytes(char *buf, size_t buflen,
                              const unsigned char *data, size_t data_len,
                              int upper_case, const char *separator) {
  const char *digits = upper_case ? "0123456789ABCDEF" : "0123456789abcdef";
  size_t sep_len = separator ? strlen(separator) : 0;
  size_t pos = 0;
  for (size_t i = 0; i < data_len; i++) {
    if (i > 0 && sep_len > 0) {
      if (pos + sep_len < buflen) {
        memcpy(buf + pos, separator, sep_len);
      }
      pos += sep_len;
    }
    if (pos + 2 < buflen) {
      buf[pos] = digits[(data[i] >> 4) & 0xF];
      buf[pos + 1] = digits[data[i] & 0xF];
    }
    pos += 2;
  }
  if (pos < buflen) buf[pos] = '\0';
  else if (buflen > 0) buf[buflen - 1] = '\0';
  return (int)pos;
}

void csupport_compute_justify_padding(size_t str_len, size_t width,
                                       int justify,
                                       unsigned *left_pad,
                                       unsigned *right_pad) {
  *left_pad = 0;
  *right_pad = 0;
  if (str_len >= width) return;
  size_t diff = width - str_len;
  switch (justify) {
  case 0: break;
  case 1: *right_pad = (unsigned)diff; break;
  case 2: *left_pad = (unsigned)diff; break;
  case 3:
    *left_pad = (unsigned)(diff / 2);
    *right_pad = (unsigned)(diff - *left_pad);
    break;
  }
}

int csupport_format_number_decimal_padded(char *buf, size_t buflen,
                                           int64_t value, unsigned width) {
  if (!buf || buflen == 0) return 0;
  char tmp[32];
  int len = snprintf(tmp, sizeof(tmp), "%lld", (long long)value);
  if (len < 0) return -1;
  int pad = 0;
  if ((unsigned)len < width) pad = (int)(width - (unsigned)len);
  int pos = 0;
  for (int i = 0; i < pad && pos < (int)buflen - 1; i++)
    buf[pos++] = ' ';
  for (int i = 0; i < len && pos < (int)buflen - 1; i++)
    buf[pos++] = tmp[i];
  buf[pos] = '\0';
  return pos;
}

int csupport_format_bytes_line(char *buf, size_t buflen,
                                const unsigned char *data, size_t data_len,
                                size_t num_per_line, size_t byte_group_size,
                                int upper_case, int show_ascii) {
  const char *digits = upper_case ? "0123456789ABCDEF" : "0123456789abcdef";
  size_t pos = 0;
  size_t line_bytes = data_len < num_per_line ? data_len : num_per_line;

  for (size_t i = 0; i < line_bytes; i++) {
    if (i > 0 && byte_group_size > 0 && (i % byte_group_size) == 0) {
      if (pos < buflen) buf[pos] = ' ';
      pos++;
    }
    if (pos + 1 < buflen) {
      buf[pos] = digits[(data[i] >> 4) & 0xF];
      buf[pos + 1] = digits[data[i] & 0xF];
    }
    pos += 2;
  }

  if (show_ascii) {
    size_t num_groups = byte_group_size > 0
        ? (num_per_line + byte_group_size - 1) / byte_group_size : 0;
    size_t block_width = num_per_line * 2 + (num_groups > 0 ? num_groups - 1 : 0);
    while (pos < block_width + 2 && pos < buflen) {
      buf[pos++] = ' ';
    }
    if (pos < buflen) buf[pos++] = '|';
    for (size_t i = 0; i < line_bytes; i++) {
      char c = (data[i] >= 32 && data[i] < 127) ? (char)data[i] : '.';
      if (pos < buflen) buf[pos++] = c;
    }
    if (pos < buflen) buf[pos++] = '|';
  }

  if (pos < buflen) buf[pos] = '\0';
  else if (buflen > 0) buf[buflen - 1] = '\0';
  return (int)pos;
}

size_t csupport_write_padding_char(char *buf, size_t buf_cap,
                                    char pad_char, unsigned count) {
  size_t n = count < buf_cap ? count : buf_cap;
  memset(buf, pad_char, n);
  if (n < buf_cap) buf[n] = '\0';
  return n;
}

int csupport_format_formatted_number(char *buf, size_t buflen,
                                      int64_t dec_value, unsigned width) {
  if (!buf || buflen == 0) return 0;
  char tmp[32];
  int len = snprintf(tmp, sizeof(tmp), "%lld", (long long)dec_value);
  if (len < 0) return 0;
  int padding = 0;
  if ((unsigned)len < width) padding = (int)(width - (unsigned)len);
  int pos = 0;
  for (int i = 0; i < padding && pos < (int)buflen - 1; i++)
    buf[pos++] = ' ';
  for (int i = 0; i < len && pos < (int)buflen - 1; i++)
    buf[pos++] = tmp[i];
  buf[pos] = '\0';
  return pos;
}

int csupport_format_justify(char *buf, size_t buflen,
                             const char *str, size_t str_len,
                             size_t width, int align,
                             char fill_char) {
  if (!buf || buflen == 0) return 0;
  if (str_len >= width) {
    size_t n = str_len < buflen - 1 ? str_len : buflen - 1;
    memcpy(buf, str, n);
    buf[n] = '\0';
    return (int)n;
  }
  size_t pad = width - str_len;
  size_t total = width < buflen - 1 ? width : buflen - 1;
  int pos = 0;
  if (align == 0) { /* left */
    size_t n = str_len < total ? str_len : total;
    memcpy(buf, str, n);
    pos = (int)n;
    while ((size_t)pos < total) buf[pos++] = fill_char;
  } else if (align == 1) { /* right */
    size_t pad_n = pad < total ? pad : total;
    for (size_t i = 0; i < pad_n; i++) buf[pos++] = fill_char;
    size_t n = str_len < (total - pad_n) ? str_len : (total - pad_n);
    memcpy(buf + pos, str, n);
    pos += (int)n;
  } else { /* center */
    size_t left_pad = pad / 2;
    size_t right_pad = pad - left_pad;
    for (size_t i = 0; i < left_pad && (size_t)pos < total; i++)
      buf[pos++] = fill_char;
    size_t n = str_len < (total - (size_t)pos) ? str_len : (total - (size_t)pos);
    memcpy(buf + pos, str, n);
    pos += (int)n;
    for (size_t i = 0; i < right_pad && (size_t)pos < total; i++)
      buf[pos++] = fill_char;
  }
  buf[pos] = '\0';
  return pos;
}

int csupport_format_str_repeat(char *buf, size_t buflen,
                                const char *str, size_t str_len,
                                unsigned count) {
  if (!buf || buflen == 0) return 0;
  int pos = 0;
  for (unsigned i = 0; i < count; i++) {
    for (size_t j = 0; j < str_len && (size_t)pos + 1 < buflen; j++)
      buf[pos++] = str[j];
  }
  buf[pos] = '\0';
  return pos;
}

int csupport_format_hex_dump(char *buf, size_t buflen,
                              const unsigned char *data, size_t data_len,
                              size_t offset, unsigned bytes_per_line) {
  if (!buf || buflen == 0) return 0;
  if (bytes_per_line == 0) bytes_per_line = 16;
  int pos = 0;
  size_t i = 0;
  while (i < data_len && (size_t)pos + 10 < buflen) {
    int n = snprintf(buf + pos, buflen - (size_t)pos, "%08zx: ", offset + i);
    if (n > 0) pos += n;
    for (unsigned j = 0; j < bytes_per_line && (size_t)pos + 3 < buflen; j++) {
      if (i + j < data_len)
        n = snprintf(buf + pos, buflen - (size_t)pos, "%02x ", data[i + j]);
      else
        n = snprintf(buf + pos, buflen - (size_t)pos, "   ");
      if (n > 0) pos += n;
    }
    if ((size_t)pos + 1 < buflen) buf[pos++] = '|';
    for (unsigned j = 0; j < bytes_per_line && (size_t)pos + 1 < buflen; j++) {
      if (i + j < data_len) {
        unsigned char c = data[i + j];
        buf[pos++] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
      }
    }
    if ((size_t)pos + 2 < buflen) { buf[pos++] = '|'; buf[pos++] = '\n'; }
    i += bytes_per_line;
  }
  buf[pos] = '\0';
  return pos;
}

int csupport_format_with_commas(char *buf, size_t buflen, int64_t value) {
  if (!buf || buflen == 0) return 0;
  char tmp[32];
  int neg = value < 0;
  uint64_t abs_val = neg ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
  int tlen = 0;
  if (abs_val == 0) {
    tmp[tlen++] = '0';
  } else {
    while (abs_val > 0) {
      tmp[tlen++] = '0' + (int)(abs_val % 10);
      abs_val /= 10;
    }
  }
  int pos = 0;
  if (neg && (size_t)pos < buflen) buf[pos++] = '-';
  int initial = ((tlen - 1) % 3) + 1;
  for (int i = tlen - 1; i >= tlen - initial && i >= 0 && (size_t)pos + 1 < buflen; i--)
    buf[pos++] = tmp[i];
  for (int i = tlen - initial - 1; i >= 0;) {
    if ((size_t)pos + 1 < buflen) buf[pos++] = ',';
    for (int j = 0; j < 3 && i >= 0 && (size_t)pos + 1 < buflen; j++, i--)
      buf[pos++] = tmp[i];
  }
  buf[pos] = '\0';
  return pos;
}

int csupport_format_file_size(char *buf, size_t buflen, uint64_t bytes,
                               int use_si) {
  if (!buf || buflen == 0) return 0;
  const char *units_iec[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
  const char *units_si[] = {"B", "kB", "MB", "GB", "TB", "PB"};
  const char **units = use_si ? units_si : units_iec;
  unsigned divisor = use_si ? 1000 : 1024;
  int idx = 0;
  double val = (double)bytes;
  while (val >= (double)divisor && idx < 5) {
    val /= (double)divisor;
    idx++;
  }
  if (idx == 0)
    return snprintf(buf, buflen, "%llu %s", (unsigned long long)bytes, units[0]);
  return snprintf(buf, buflen, "%.1f %s", val, units[idx]);
}

int csupport_center_string(char *buf, size_t buflen,
                            const char *str, size_t str_len,
                            unsigned width, char fill) {
  if (!buf || buflen == 0) return 0;
  if (str_len >= width) {
    size_t n = str_len < buflen - 1 ? str_len : buflen - 1;
    memcpy(buf, str, n);
    buf[n] = '\0';
    return (int)n;
  }
  unsigned total_pad = width - (unsigned)str_len;
  unsigned left_pad = total_pad / 2;
  unsigned right_pad = total_pad - left_pad;
  int pos = 0;
  for (unsigned i = 0; i < left_pad && (size_t)pos < buflen - 1; i++)
    buf[pos++] = fill;
  for (size_t i = 0; i < str_len && (size_t)pos < buflen - 1; i++)
    buf[pos++] = str[i];
  for (unsigned i = 0; i < right_pad && (size_t)pos < buflen - 1; i++)
    buf[pos++] = fill;
  buf[pos] = '\0';
  return pos;
}

int csupport_left_justify(char *buf, size_t buflen,
                           const char *str, size_t str_len,
                           unsigned width, char fill) {
  if (!buf || buflen == 0) return 0;
  int pos = 0;
  for (size_t i = 0; i < str_len && (size_t)pos < buflen - 1; i++)
    buf[pos++] = str[i];
  while ((unsigned)pos < width && (size_t)pos < buflen - 1)
    buf[pos++] = fill;
  buf[pos] = '\0';
  return pos;
}

int csupport_right_justify(char *buf, size_t buflen,
                            const char *str, size_t str_len,
                            unsigned width, char fill) {
  if (!buf || buflen == 0) return 0;
  int pos = 0;
  unsigned pad = (str_len < width) ? width - (unsigned)str_len : 0;
  for (unsigned i = 0; i < pad && (size_t)pos < buflen - 1; i++)
    buf[pos++] = fill;
  for (size_t i = 0; i < str_len && (size_t)pos < buflen - 1; i++)
    buf[pos++] = str[i];
  buf[pos] = '\0';
  return pos;
}

int csupport_format_integer_width(char *buf, size_t buflen,
                                   int64_t val, unsigned min_width,
                                   int pad_zero) {
  if (!buf || buflen == 0) return 0;
  char tmp[64];
  int n;
  if (val < 0)
    n = snprintf(tmp, sizeof(tmp), "%lld", (long long)val);
  else
    n = snprintf(tmp, sizeof(tmp), "%lld", (long long)val);
  if (n <= 0) return 0;
  if ((unsigned)n >= min_width || !pad_zero)
    return snprintf(buf, buflen, "%s", tmp);
  int pos = 0;
  int start = (val < 0) ? 1 : 0;
  if (start && (size_t)pos < buflen - 1) buf[pos++] = '-';
  unsigned digits = (unsigned)n - start;
  unsigned pad = min_width - digits - start;
  for (unsigned i = 0; i < pad && (size_t)pos < buflen - 1; i++)
    buf[pos++] = '0';
  for (int i = start; i < n && (size_t)pos < buflen - 1; i++)
    buf[pos++] = tmp[i];
  buf[pos] = '\0';
  return pos;
}

int csupport_format_unsigned_width(char *buf, size_t buflen,
                                    uint64_t val, unsigned min_width,
                                    int pad_zero) {
  if (!buf || buflen == 0) return 0;
  char tmp[64];
  int n = snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)val);
  if (n <= 0) return 0;
  if ((unsigned)n >= min_width || !pad_zero)
    return snprintf(buf, buflen, "%s", tmp);
  int pos = 0;
  unsigned pad = min_width - (unsigned)n;
  for (unsigned i = 0; i < pad && (size_t)pos < buflen - 1; i++)
    buf[pos++] = '0';
  for (int i = 0; i < n && (size_t)pos < buflen - 1; i++)
    buf[pos++] = tmp[i];
  buf[pos] = '\0';
  return pos;
}

int csupport_format_bool(char *buf, size_t buflen, int val) {
  return snprintf(buf, buflen, "%s", val ? "true" : "false");
}

/* csupport_format_pointer already declared in header with uint64_t signature */
