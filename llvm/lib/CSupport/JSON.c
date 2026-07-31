/*===- JSON.c - JSON utilities (pure C) -------------------------*- C -*-===*/
#include "include/csupport/lj_ls_lo_ln.h"
#include "include/csupport/buffer.h"
#include "include/csupport/number_parse.h"
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

int csupport_json_is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

size_t csupport_json_skip_whitespace(const char *data, size_t len, size_t pos) {
  if ((!data && len != 0) || pos > len)
    return len;
  while (pos < len && csupport_json_is_whitespace(data[pos]))
    pos++;
  return pos;
}

int csupport_json_escape_char(char c) {
  switch (c) {
  case '"': return '"';
  case '\\': return '\\';
  case '\b': return 'b';
  case '\f': return 'f';
  case '\n': return 'n';
  case '\r': return 'r';
  case '\t': return 't';
  default: return -1;
  }
}

static void json_escape_to_obuf(const char *src, size_t src_len,
                                csupport_obuf_t *out) {
  for (size_t i = 0; i < src_len; ++i) {
    const unsigned char c = (unsigned char)src[i];
    const int escape = csupport_json_escape_char((char)c);
    if (escape >= 0) {
      csupport_obuf_put(out, '\\');
      csupport_obuf_put(out, (char)escape);
    } else if (c < 0x20) {
      csupport_obuf_printf(out, "\\u%04x", (unsigned)c);
    } else {
      csupport_obuf_put(out, (char)c);
    }
  }
}

size_t csupport_json_escape_string(const char *src, size_t src_len,
                                   char *dst, size_t dst_cap) {
  if (!src && src_len != 0)
    return SIZE_MAX;
  csupport_obuf_t out = csupport_obuf(dst, dst_cap);
  json_escape_to_obuf(src, src_len, &out);
  return csupport_obuf_finish(&out);
}

int csupport_json_encode_utf8(uint32_t cp, char *buf, size_t buflen) {
  if (!buf || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
    return 0;
  if (cp < 0x80) {
    if (buflen < 1) return 0;
    buf[0] = (char)(cp & 0x7F);
    return 1;
  } else if (cp < 0x800) {
    if (buflen < 2) return 0;
    buf[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
    buf[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp < 0x10000) {
    if (buflen < 3) return 0;
    buf[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  } else {
    if (buflen < 4) return 0;
    buf[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }
}

int csupport_json_decode_hex4(const char *hex, uint16_t *out) {
  if (!hex || !out)
    return -1;
  uint16_t value = 0;
  for (int i = 0; i < 4; i++) {
    unsigned char c = (unsigned char)hex[i];
    value <<= 4;
    if (c >= '0' && c <= '9')
      value |= (uint16_t)(c - '0');
    else if (c >= 'a' && c <= 'f')
      value |= (uint16_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      value |= (uint16_t)(c - 'A' + 10);
    else
      return -1;
  }
  *out = value;
  return 0;
}

int csupport_json_is_number_char(char c) {
  return (c >= '0' && c <= '9') || c == 'e' || c == 'E' ||
         c == '+' || c == '-' || c == '.';
}

static size_t json_number_prefix_length(const char *src, size_t len) {
  size_t length = 0;
  while (length < len && csupport_json_is_number_char(src[length]))
    ++length;
  return length;
}

static size_t json_valid_number_prefix_length(const char *src, size_t len) {
  size_t pos = 0;
  if (pos < len && src[pos] == '-')
    ++pos;
  if (pos == len)
    return 0;

  if (src[pos] == '0') {
    ++pos;
  } else {
    if (src[pos] < '1' || src[pos] > '9')
      return 0;
    do {
      ++pos;
    } while (pos < len && src[pos] >= '0' && src[pos] <= '9');
  }

  if (pos < len && src[pos] == '.') {
    const size_t fraction = ++pos;
    while (pos < len && src[pos] >= '0' && src[pos] <= '9')
      ++pos;
    if (pos == fraction)
      return 0;
  }

  if (pos < len && (src[pos] == 'e' || src[pos] == 'E')) {
    ++pos;
    if (pos < len && (src[pos] == '+' || src[pos] == '-'))
      ++pos;
    const size_t exponent = pos;
    while (pos < len && src[pos] >= '0' && src[pos] <= '9')
      ++pos;
    if (pos == exponent)
      return 0;
  }
  return pos;
}

size_t csupport_json_parse_double(const char *src, size_t len, double *val) {
  if (!src || !val)
    return 0;
  const size_t token_len = json_number_prefix_length(src, len);
  if (token_len == 0) return 0;
  char local[64];
  char *buf =
      csupport_copy_number_text(src, token_len, local, sizeof(local));
  if (!buf) return 0;
  char *end = NULL;
  errno = 0;
  double parsed = strtod(buf, &end);
  size_t consumed = end == buf || errno == ERANGE ? 0 : (size_t)(end - buf);
  if (consumed != 0)
    *val = parsed;
  csupport_free_number_text(buf, local);
  return consumed;
}

size_t csupport_json_parse_int64(const char *src, size_t len, int64_t *val) {
  if (!src || !val)
    return 0;
  const size_t token_len = json_number_prefix_length(src, len);
  if (token_len == 0) return 0;
  char local[32];
  char *buf =
      csupport_copy_number_text(src, token_len, local, sizeof(local));
  if (!buf) return 0;
  char *end = NULL;
  errno = 0;
  int64_t parsed = strtoll(buf, &end, 10);
  size_t consumed = end == buf || errno == ERANGE ? 0 : (size_t)(end - buf);
  if (consumed != 0)
    *val = parsed;
  csupport_free_number_text(buf, local);
  return consumed;
}

size_t csupport_json_quote_string(const char *src, size_t src_len,
                                  char *dst, size_t dst_cap) {
  if (!src && src_len != 0)
    return SIZE_MAX;
  csupport_obuf_t out = csupport_obuf(dst, dst_cap);
  csupport_obuf_put(&out, '"');
  json_escape_to_obuf(src, src_len, &out);
  csupport_obuf_put(&out, '"');
  return csupport_obuf_finish(&out);
}

void csupport_json_calc_line_col(const char *start, const char *pos,
                                 int *line, int *col) {
  int l = 1;
  const char *line_start = start;
  for (const char *p = start; p < pos; p++) {
    if (*p == '\n') {
      l++;
      line_start = p + 1;
    }
  }
  *line = l;
  *col = (int)(pos - line_start);
}

int csupport_json_unescape_char(char c) {
  switch (c) {
  case '"': return '"';
  case '\\': return '\\';
  case '/': return '/';
  case 'b': return '\b';
  case 'f': return '\f';
  case 'n': return '\n';
  case 'r': return '\r';
  case 't': return '\t';
  default: return -1;
  }
}

size_t csupport_json_parse_number_ex(const char *src, size_t len,
                                     int64_t *int_val, uint64_t *uint_val,
                                     double *dbl_val, int *type) {
  if (!src || !int_val || !uint_val || !dbl_val || !type)
    return 0;
  *type = 0;
  const size_t token_len = json_number_prefix_length(src, len);
  if (token_len == 0) return 0;
  char local[64];
  char *buf =
      csupport_copy_number_text(src, token_len, local, sizeof(local));
  if (!buf) return 0;

  char *end = NULL;
  size_t consumed = 0;
  errno = 0;
  int64_t iv = strtoll(buf, &end, 10);
  if (end == buf + token_len && errno != ERANGE) {
    *int_val = iv;
    *type = 1;
    consumed = token_len;
    goto done;
  }
  if (src[0] != '-') {
    errno = 0;
    uint64_t uv = strtoull(buf, &end, 10);
    if (end == buf + token_len && errno != ERANGE) {
      *uint_val = uv;
      *type = 2;
      consumed = token_len;
      goto done;
    }
  }
  errno = 0;
  double dv = strtod(buf, &end);
  if (end > buf && errno != ERANGE) {
    *dbl_val = dv;
    *type = 3;
    consumed = (size_t)(end - buf);
  }

done:
  csupport_free_number_text(buf, local);
  return consumed;
}

int csupport_json_decode_surrogate_pair(uint16_t hi, uint16_t lo, uint32_t *cp) {
  if (hi < 0xD800 || hi >= 0xDC00) return -1;
  if (lo < 0xDC00 || lo >= 0xE000) return -1;
  *cp = 0x10000U + (((uint32_t)(hi - 0xD800)) << 10) + ((uint32_t)(lo - 0xDC00));
  return 0;
}

size_t csupport_json_quote_to_stream(const char *src, size_t src_len,
                                     char *dst, size_t dst_cap) {
  return csupport_json_quote_string(src, src_len, dst, dst_cap);
}

static void json_set_error(const char **error_msg, const char *message) {
  if (error_msg)
    *error_msg = message;
}

static int parse_unicode_escape(const char **pos, const char *end,
                                csupport_obuf_t *out,
                                const char **error_msg) {
  static const char REPLACEMENT[] = {'\xef', '\xbf', '\xbd'};

  if (*pos > end || (size_t)(end - *pos) < 4) {
    json_set_error(error_msg, "Invalid \\u escape sequence");
    return -1;
  }
  uint16_t first;
  if (csupport_json_decode_hex4(*pos, &first) != 0) {
    json_set_error(error_msg, "Invalid \\u escape sequence");
    return -1;
  }
  *pos += 4;

  while (1) {
    if (first < 0xD800 || first >= 0xE000) {
      char buf[4];
      int n = csupport_json_encode_utf8((uint32_t)first, buf, sizeof(buf));
      csupport_obuf_write(out, buf, (size_t)n);
      return 0;
    }
    if (first >= 0xDC00) {
      csupport_obuf_write(out, REPLACEMENT, 3);
      return 0;
    }
    if (*pos > end || (size_t)(end - *pos) < 2 ||
        (*pos)[0] != '\\' || (*pos)[1] != 'u') {
      csupport_obuf_write(out, REPLACEMENT, 3);
      return 0;
    }
    *pos += 2;
    uint16_t second;
    if (*pos > end || (size_t)(end - *pos) < 4 ||
        csupport_json_decode_hex4(*pos, &second) != 0) {
      json_set_error(error_msg, "Invalid \\u escape sequence");
      return -1;
    }
    *pos += 4;
    if (second < 0xDC00 || second >= 0xE000) {
      csupport_obuf_write(out, REPLACEMENT, 3);
      first = second;
      continue;
    }
    uint32_t cp = 0x10000u + (((uint32_t)(first - 0xD800)) << 10) +
                  ((uint32_t)(second - 0xDC00));
    char buf[4];
    int n = csupport_json_encode_utf8(cp, buf, sizeof(buf));
    csupport_obuf_write(out, buf, (size_t)n);
    return 0;
  }
}

size_t csupport_json_parse_string_body(const char **pos, const char *end,
                                       char *dst, size_t dst_cap,
                                       const char **error_msg) {
  if (!pos || !*pos || !end || *pos > end) {
    json_set_error(error_msg, "Invalid string bounds");
    return SIZE_MAX;
  }
  csupport_obuf_t out = csupport_obuf(dst, dst_cap);
  const char *p = *pos;
  while (p < end) {
    char c = *p++;
    if (c == '"') {
      *pos = p;
      return csupport_obuf_finish(&out);
    }
    if ((c & 0x1f) == c) {
      *pos = p - 1;
      json_set_error(error_msg, "Control character in string");
      return (size_t)-1;
    }
    if (c != '\\') {
      csupport_obuf_put(&out, c);
      continue;
    }
    if (p >= end) {
      *pos = p;
      json_set_error(error_msg, "Unterminated string");
      return (size_t)-1;
    }
    c = *p++;
    switch (c) {
    case '"': case '\\': case '/':
      csupport_obuf_put(&out, c); break;
    case 'b': csupport_obuf_put(&out, '\b'); break;
    case 'f': csupport_obuf_put(&out, '\f'); break;
    case 'n': csupport_obuf_put(&out, '\n'); break;
    case 'r': csupport_obuf_put(&out, '\r'); break;
    case 't': csupport_obuf_put(&out, '\t'); break;
    case 'u': {
      const char *saved = p;
      if (parse_unicode_escape(&p, end, &out, error_msg) < 0) {
        *pos = saved;
        return (size_t)-1;
      }
      break;
    }
    default:
      *pos = p - 1;
      json_set_error(error_msg, "Invalid escape sequence");
      return (size_t)-1;
    }
  }
  *pos = p;
  json_set_error(error_msg, "Unterminated string");
  return (size_t)-1;
}

int csupport_json_format_int64(char *buf, size_t cap, int64_t val) {
  return snprintf(buf, cap, "%lld", (long long)val);
}

int csupport_json_format_uint64(char *buf, size_t cap, uint64_t val) {
  return snprintf(buf, cap, "%llu", (unsigned long long)val);
}

int csupport_json_format_double(char *buf, size_t cap, double val) {
  if (val != val) return snprintf(buf, cap, "null");
  if (val == HUGE_VAL || val == -HUGE_VAL) return snprintf(buf, cap, "null");
  int len = snprintf(buf, cap, "%.17g", val);
  if (len > 0 && (size_t)len < cap) {
    int has_dot = 0, has_e = 0;
    for (int i = 0; i < len; i++) {
      if (buf[i] == '.') has_dot = 1;
      if (buf[i] == 'e' || buf[i] == 'E') has_e = 1;
    }
    if (!has_dot && !has_e && (size_t)(len + 2) < cap) {
      buf[len++] = '.';
      buf[len++] = '0';
      buf[len] = '\0';
    }
  }
  return len;
}

size_t csupport_json_format_value_null(char *buf, size_t cap) {
  csupport_obuf_t out = csupport_obuf(buf, cap);
  csupport_obuf_write(&out, "null", 4);
  return csupport_obuf_finish(&out);
}

size_t csupport_json_format_value_bool(char *buf, size_t cap, int val) {
  csupport_obuf_t out = csupport_obuf(buf, cap);
  const char *text = val ? "true" : "false";
  const size_t length = val ? 4 : 5;
  csupport_obuf_write(&out, text, length);
  return csupport_obuf_finish(&out);
}

static int json_validate_utf8(const char *data, size_t len) {
  if (!data && len != 0)
    return 0;
  size_t i = 0;
  while (i < len) {
    const unsigned char first = (unsigned char)data[i++];
    if (first <= 0x7f)
      continue;

    unsigned count;
    unsigned char second_min = 0x80;
    unsigned char second_max = 0xbf;
    if (first >= 0xc2 && first <= 0xdf) {
      count = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      count = 2;
      if (first == 0xe0)
        second_min = 0xa0; // reject overlong encodings
      else if (first == 0xed)
        second_max = 0x9f; // reject UTF-16 surrogates
    } else if (first >= 0xf0 && first <= 0xf4) {
      count = 3;
      if (first == 0xf0)
        second_min = 0x90; // reject overlong encodings
      else if (first == 0xf4)
        second_max = 0x8f; // stop at U+10FFFF
    } else {
      return 0;
    }

    if (count > len - i)
      return 0;
    const unsigned char second = (unsigned char)data[i];
    if (second < second_min || second > second_max)
      return 0;
    for (unsigned j = 1; j < count; ++j) {
      const unsigned char continuation = (unsigned char)data[i + j];
      if (continuation < 0x80 || continuation > 0xbf)
        return 0;
    }
    i += count;
  }
  return 1;
}

int csupport_json_validate_utf8(const char *data, size_t len) {
  return json_validate_utf8(data, len);
}

static size_t json_minify(const char *src, size_t src_len,
                          char *dst, size_t dst_cap) {
  if (!src && src_len != 0)
    return SIZE_MAX;
  csupport_obuf_t out = csupport_obuf(dst, dst_cap);
  int in_string = 0;
  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    if (in_string) {
      csupport_obuf_put(&out, c);
      if (c == '"') in_string = 0;
      else if (c == '\\' && i + 1 < src_len) {
        csupport_obuf_put(&out, src[++i]);
      }
    } else {
      if (c == '"') {
        in_string = 1;
        csupport_obuf_put(&out, c);
      } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        csupport_obuf_put(&out, c);
      }
    }
  }
  return csupport_obuf_finish(&out);
}

size_t csupport_json_compact(const char *src, size_t src_len,
                             char *dst, size_t dst_cap) {
  return json_minify(src, src_len, dst, dst_cap);
}

static void json_obuf_repeat(csupport_obuf_t *out, char value, size_t count) {
  if (out->needed == SIZE_MAX)
    return;
  size_t writable = 0;
  if (out->cap != 0 && out->needed < out->cap - 1)
    writable = out->cap - 1 - out->needed;
  if (writable > count)
    writable = count;
  if (writable != 0)
    memset(out->data + out->needed, value, writable);
  csupport_obuf_grew(out, count);
}

static int json_pretty_newline(csupport_obuf_t *out, size_t depth,
                               unsigned indent_width) {
  csupport_obuf_put(out, '\n');
  if (depth != 0 && indent_width > SIZE_MAX / depth) {
    out->needed = SIZE_MAX;
    return 0;
  }
  json_obuf_repeat(out, ' ', depth * indent_width);
  return out->needed != SIZE_MAX;
}

size_t csupport_json_prettify(const char *src, size_t src_len,
                               char *dst, size_t dst_cap,
                               unsigned indent_width) {
  if (!csupport_json_validate(src, src_len))
    return SIZE_MAX;
  csupport_obuf_t out = csupport_obuf(dst, dst_cap);
  size_t depth = 0;
  int in_string = 0;

  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    if (in_string) {
      csupport_obuf_put(&out, c);
      if (c == '"') in_string = 0;
      else if (c == '\\' && i + 1 < src_len)
        csupport_obuf_put(&out, src[++i]);
      continue;
    }
    switch (c) {
    case '"':
      in_string = 1;
      csupport_obuf_put(&out, c);
      break;
    case '{':
    case '[':
      csupport_obuf_put(&out, c);
      if (depth == SIZE_MAX)
        return SIZE_MAX;
      ++depth;
      if (!json_pretty_newline(&out, depth, indent_width))
        return csupport_obuf_finish(&out);
      break;
    case '}':
    case ']':
      if (depth == 0) {
        out.needed = SIZE_MAX;
        return csupport_obuf_finish(&out);
      }
      --depth;
      if (!json_pretty_newline(&out, depth, indent_width))
        return csupport_obuf_finish(&out);
      csupport_obuf_put(&out, c);
      break;
    case ',':
      csupport_obuf_put(&out, c);
      if (!json_pretty_newline(&out, depth, indent_width))
        return csupport_obuf_finish(&out);
      break;
    case ':':
      csupport_obuf_put(&out, c);
      csupport_obuf_put(&out, ' ');
      break;
    case ' ': case '\t': case '\n': case '\r': break;
    default:
      csupport_obuf_put(&out, c);
      break;
    }
  }
  return csupport_obuf_finish(&out);
}

int csupport_json_depth(const char *src, size_t src_len) {
  int depth = 0, max_depth = 0;
  int in_string = 0;
  if (!csupport_json_validate(src, src_len))
    return -1;
  for (size_t i = 0; i < src_len; i++) {
    if (in_string) {
      if (src[i] == '"') in_string = 0;
      else if (src[i] == '\\' && i + 1 < src_len) i++;
      continue;
    }
    if (src[i] == '"') in_string = 1;
    else if (src[i] == '{' || src[i] == '[') {
      if (depth == INT_MAX)
        return -1;
      depth++;
      if (depth > max_depth) max_depth = depth;
    }
    else if (src[i] == '}' || src[i] == ']') {
      if (depth == 0)
        return -1;
      depth--;
    }
  }
  return depth == 0 && !in_string ? max_depth : -1;
}

int csupport_json_is_valid_utf8(const char *src, size_t src_len) {
  return json_validate_utf8(src, src_len);
}

size_t csupport_json_minify(const char *src, size_t src_len,
                             char *dst, size_t dst_cap) {
  return json_minify(src, src_len, dst, dst_cap);
}

enum { JSON_MAX_PARSE_DEPTH = 512 };

typedef struct {
  const char *data;
  size_t length;
  size_t position;
  unsigned depth;
  size_t *top_level_key_count;
} json_parser_t;

static void json_parser_skip_whitespace(json_parser_t *parser) {
  parser->position = csupport_json_skip_whitespace(
      parser->data, parser->length, parser->position);
}

static int json_parser_parse_string(json_parser_t *parser) {
  if (parser->position >= parser->length ||
      parser->data[parser->position] != '"')
    return 0;
  ++parser->position;
  while (parser->position < parser->length) {
    const unsigned char c =
        (unsigned char)parser->data[parser->position++];
    if (c == '"')
      return 1;
    if (c < 0x20)
      return 0;
    if (c != '\\')
      continue;
    if (parser->position >= parser->length)
      return 0;
    const char escape = parser->data[parser->position++];
    if (csupport_json_unescape_char(escape) >= 0)
      continue;
    if (escape != 'u' || parser->length - parser->position < 4)
      return 0;
    uint16_t code_unit;
    if (csupport_json_decode_hex4(parser->data + parser->position,
                                  &code_unit) != 0)
      return 0;
    (void)code_unit;
    parser->position += 4;
  }
  return 0;
}

static int json_parser_parse_value(json_parser_t *parser);

static int json_parser_enter_container(json_parser_t *parser) {
  if (parser->depth == JSON_MAX_PARSE_DEPTH)
    return 0;
  ++parser->depth;
  return 1;
}

static int json_parser_parse_array(json_parser_t *parser) {
  if (!json_parser_enter_container(parser))
    return 0;
  ++parser->position; // '['
  json_parser_skip_whitespace(parser);
  if (parser->position < parser->length &&
      parser->data[parser->position] == ']') {
    ++parser->position;
    --parser->depth;
    return 1;
  }

  for (;;) {
    if (!json_parser_parse_value(parser)) {
      --parser->depth;
      return 0;
    }
    json_parser_skip_whitespace(parser);
    if (parser->position >= parser->length) {
      --parser->depth;
      return 0;
    }
    const char delimiter = parser->data[parser->position++];
    if (delimiter == ']') {
      --parser->depth;
      return 1;
    }
    if (delimiter != ',') {
      --parser->depth;
      return 0;
    }
    json_parser_skip_whitespace(parser);
  }
}

static int json_parser_parse_object(json_parser_t *parser) {
  if (!json_parser_enter_container(parser))
    return 0;
  ++parser->position; // '{'
  json_parser_skip_whitespace(parser);
  if (parser->position < parser->length &&
      parser->data[parser->position] == '}') {
    ++parser->position;
    --parser->depth;
    return 1;
  }

  for (;;) {
    if (!json_parser_parse_string(parser)) {
      --parser->depth;
      return 0;
    }
    if (parser->depth == 1 && parser->top_level_key_count) {
      if (*parser->top_level_key_count == SIZE_MAX) {
        --parser->depth;
        return 0;
      }
      ++*parser->top_level_key_count;
    }
    json_parser_skip_whitespace(parser);
    if (parser->position >= parser->length ||
        parser->data[parser->position++] != ':') {
      --parser->depth;
      return 0;
    }
    if (!json_parser_parse_value(parser)) {
      --parser->depth;
      return 0;
    }
    json_parser_skip_whitespace(parser);
    if (parser->position >= parser->length) {
      --parser->depth;
      return 0;
    }
    const char delimiter = parser->data[parser->position++];
    if (delimiter == '}') {
      --parser->depth;
      return 1;
    }
    if (delimiter != ',') {
      --parser->depth;
      return 0;
    }
    json_parser_skip_whitespace(parser);
  }
}

static int json_parser_consume_literal(json_parser_t *parser,
                                       const char *literal, size_t length) {
  if (length > parser->length - parser->position ||
      memcmp(parser->data + parser->position, literal, length) != 0)
    return 0;
  parser->position += length;
  return 1;
}

static int json_parser_parse_value(json_parser_t *parser) {
  json_parser_skip_whitespace(parser);
  if (parser->position >= parser->length)
    return 0;
  switch (parser->data[parser->position]) {
  case '"':
    return json_parser_parse_string(parser);
  case '{':
    return json_parser_parse_object(parser);
  case '[':
    return json_parser_parse_array(parser);
  case 't':
    return json_parser_consume_literal(parser, "true", 4);
  case 'f':
    return json_parser_consume_literal(parser, "false", 5);
  case 'n':
    return json_parser_consume_literal(parser, "null", 4);
  default: {
    const size_t number_length = json_valid_number_prefix_length(
        parser->data + parser->position, parser->length - parser->position);
    if (number_length == 0)
      return 0;
    parser->position += number_length;
    return 1;
  }
  }
}

static int json_parse_document(const char *src, size_t src_len,
                               size_t *top_level_key_count,
                               size_t *value_start, size_t *value_end) {
  if ((!src && src_len != 0) || !json_validate_utf8(src, src_len))
    return 0;
  json_parser_t parser = {src, src_len, 0, 0, top_level_key_count};
  json_parser_skip_whitespace(&parser);
  const size_t start = parser.position;
  if (!json_parser_parse_value(&parser))
    return 0;
  const size_t end = parser.position;
  json_parser_skip_whitespace(&parser);
  if (parser.position != src_len)
    return 0;
  if (value_start)
    *value_start = start;
  if (value_end)
    *value_end = end;
  return 1;
}

int csupport_json_validate(const char *src, size_t src_len) {
  return json_parse_document(src, src_len, NULL, NULL, NULL);
}

static int json_decode_pointer_segment(const char *segment, size_t length,
                                       char *out, size_t *out_length) {
  size_t written = 0;
  for (size_t i = 0; i < length; ++i) {
    char c = segment[i];
    if (c == '~') {
      if (++i == length || (segment[i] != '0' && segment[i] != '1'))
        return 0;
      c = segment[i] == '0' ? '~' : '/';
    }
    out[written++] = c;
  }
  *out_length = written;
  return 1;
}

static int json_string_equals(const char *data, size_t string_start,
                              size_t limit, const char *expected,
                              size_t expected_length, size_t *after_string) {
  const char *position = data + string_start + 1;
  const char *end = data + limit;
  const char *error = NULL;
  const size_t needed =
      csupport_json_parse_string_body(&position, end, NULL, 0, &error);
  if (needed == SIZE_MAX)
    return -1;

  char local[256];
  char *decoded =
      needed < sizeof(local) ? local : (char *)malloc(needed + 1);
  if (!decoded)
    return -1;
  position = data + string_start + 1;
  const size_t decoded_length = csupport_json_parse_string_body(
      &position, end, decoded, needed + 1, &error);
  const int equal =
      decoded_length == expected_length &&
      (expected_length == 0 ||
       memcmp(decoded, expected, expected_length) == 0);
  if (decoded != local)
    free(decoded);
  if (decoded_length == SIZE_MAX)
    return -1;
  *after_string = (size_t)(position - data);
  return equal;
}

static int json_find_object_member(const char *data, size_t object_start,
                                   size_t object_end, const char *key,
                                   size_t key_length, size_t *value_start,
                                   size_t *value_end) {
  json_parser_t parser = {data, object_end, object_start + 1, 0, NULL};
  json_parser_skip_whitespace(&parser);
  if (parser.position < object_end && data[parser.position] == '}')
    return 0;

  for (;;) {
    const size_t key_start = parser.position;
    size_t after_key = 0;
    const int matches = json_string_equals(data, key_start, object_end, key,
                                           key_length, &after_key);
    if (matches < 0)
      return -1;
    parser.position = after_key;
    json_parser_skip_whitespace(&parser);
    if (parser.position >= object_end || data[parser.position++] != ':')
      return -1;
    json_parser_skip_whitespace(&parser);
    const size_t child_start = parser.position;
    if (!json_parser_parse_value(&parser))
      return -1;
    const size_t child_end = parser.position;
    if (matches) {
      *value_start = child_start;
      *value_end = child_end;
      return 1;
    }
    json_parser_skip_whitespace(&parser);
    if (parser.position >= object_end)
      return -1;
    const char delimiter = data[parser.position++];
    if (delimiter == '}')
      return 0;
    if (delimiter != ',')
      return -1;
    json_parser_skip_whitespace(&parser);
  }
}

static int json_parse_array_index(const char *text, size_t length,
                                  size_t *index) {
  if (length == 0 || (length > 1 && text[0] == '0'))
    return 0;
  size_t value = 0;
  for (size_t i = 0; i < length; ++i) {
    const unsigned digit = (unsigned)(unsigned char)text[i] - '0';
    if (digit >= 10 || value > (SIZE_MAX - digit) / 10)
      return 0;
    value = value * 10 + digit;
  }
  *index = value;
  return 1;
}

static int json_find_array_element(const char *data, size_t array_start,
                                   size_t array_end, size_t wanted_index,
                                   size_t *value_start, size_t *value_end) {
  json_parser_t parser = {data, array_end, array_start + 1, 0, NULL};
  json_parser_skip_whitespace(&parser);
  if (parser.position < array_end && data[parser.position] == ']')
    return 0;

  size_t index = 0;
  for (;;) {
    const size_t child_start = parser.position;
    if (!json_parser_parse_value(&parser))
      return -1;
    const size_t child_end = parser.position;
    if (index == wanted_index) {
      *value_start = child_start;
      *value_end = child_end;
      return 1;
    }
    if (index == SIZE_MAX)
      return -1;
    ++index;
    json_parser_skip_whitespace(&parser);
    if (parser.position >= array_end)
      return -1;
    const char delimiter = data[parser.position++];
    if (delimiter == ']')
      return 0;
    if (delimiter != ',')
      return -1;
    json_parser_skip_whitespace(&parser);
  }
}

size_t csupport_json_pointer_get(const char *json, size_t json_len,
                                  const char *pointer, size_t ptr_len,
                                  const char **value_start) {
  if (!value_start)
    return 0;
  *value_start = NULL;
  if ((!pointer && ptr_len != 0) ||
      !json || json_len == 0 ||
      !json_validate_utf8(pointer, ptr_len))
    return 0;

  size_t current_start;
  size_t current_end;
  if (!json_parse_document(json, json_len, NULL, &current_start, &current_end))
    return 0;

  size_t pointer_position = 0;
  while (pointer_position < ptr_len) {
    if (pointer[pointer_position++] != '/')
      return 0;
    const size_t segment_start = pointer_position;
    while (pointer_position < ptr_len && pointer[pointer_position] != '/')
      ++pointer_position;
    const size_t encoded_length = pointer_position - segment_start;

    char local[256];
    char *segment = encoded_length < sizeof(local)
                        ? local
                        : (char *)malloc(encoded_length + 1);
    if (!segment)
      return 0;
    size_t segment_length;
    const int decoded = json_decode_pointer_segment(
        pointer + segment_start, encoded_length, segment, &segment_length);
    if (!decoded) {
      if (segment != local)
        free(segment);
      return 0;
    }

    int found;
    size_t child_start = 0;
    size_t child_end = 0;
    if (json[current_start] == '{') {
      found = json_find_object_member(json, current_start, current_end, segment,
                                      segment_length, &child_start, &child_end);
    } else if (json[current_start] == '[') {
      size_t index;
      found = json_parse_array_index(segment, segment_length, &index)
                  ? json_find_array_element(json, current_start, current_end,
                                            index, &child_start, &child_end)
                  : 0;
    } else {
      found = 0;
    }
    if (segment != local)
      free(segment);
    if (found != 1)
      return 0;
    current_start = child_start;
    current_end = child_end;
  }

  *value_start = json + current_start;
  return current_end - current_start;
}

size_t csupport_json_count_keys(const char *src, size_t src_len) {
  size_t count = 0;
  return json_parse_document(src, src_len, &count, NULL, NULL) ? count : 0;
}

int csupport_json_match_literal(const char *src, size_t src_len,
                                 size_t pos, const char *literal) {
  if ((!src && src_len != 0) || !literal || pos > src_len)
    return 0;
  size_t llen = strlen(literal);
  if (llen > src_len - pos) return 0;
  return memcmp(src + pos, literal, llen) == 0;
}

size_t csupport_json_format_object_entry(const char *key, size_t key_len,
                                          const char *value, size_t value_len,
                                          char *out, size_t out_cap) {
  if ((!key && key_len != 0) || (!value && value_len != 0))
    return SIZE_MAX;
  csupport_obuf_t buffer = csupport_obuf(out, out_cap);
  csupport_obuf_put(&buffer, '"');
  json_escape_to_obuf(key, key_len, &buffer);
  csupport_obuf_write(&buffer, "\":", 2);
  csupport_obuf_write(&buffer, value, value_len);
  return csupport_obuf_finish(&buffer);
}
