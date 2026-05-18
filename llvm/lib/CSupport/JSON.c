/*===- JSON.c - JSON utilities (pure C) -------------------------*- C -*-===*/
#include "include/csupport/lj_ls_lo_ln.h"
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

int csupport_json_is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

size_t csupport_json_skip_whitespace(const char *data, size_t len, size_t pos) {
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

size_t csupport_json_escape_string(const char *src, size_t src_len,
                                   char *dst, size_t dst_cap) {
  size_t out = 0;
  for (size_t i = 0; i < src_len; i++) {
    int esc = csupport_json_escape_char(src[i]);
    if (esc >= 0) {
      if (out + 2 > dst_cap) return 0;
      dst[out++] = '\\';
      dst[out++] = (char)esc;
    } else if ((unsigned char)src[i] < 0x20) {
      if (out + 6 > dst_cap) return 0;
      out += (size_t)snprintf(dst + out, dst_cap - out, "\\u%04x",
                              (unsigned)(unsigned char)src[i]);
    } else {
      if (out + 1 > dst_cap) return 0;
      dst[out++] = src[i];
    }
  }
  if (out < dst_cap) dst[out] = '\0';
  return out;
}

int csupport_json_encode_utf8(uint32_t cp, char *buf, size_t buflen) {
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
  } else if (cp < 0x110000) {
    if (buflen < 4) return 0;
    buf[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }
  return 0;
}

int csupport_json_decode_hex4(const char *hex, uint16_t *out) {
  *out = 0;
  for (int i = 0; i < 4; i++) {
    unsigned char c = (unsigned char)hex[i];
    *out <<= 4;
    if (c >= '0' && c <= '9')
      *out |= (uint16_t)(c - '0');
    else if (c >= 'a' && c <= 'f')
      *out |= (uint16_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      *out |= (uint16_t)(c - 'A' + 10);
    else
      return -1;
  }
  return 0;
}

size_t csupport_json_parse_double(const char *src, size_t len, double *val) {
  if (len == 0) return 0;
  char buf[64];
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, src, n);
  buf[n] = '\0';
  char *end = NULL;
  errno = 0;
  *val = strtod(buf, &end);
  if (end == buf || errno == ERANGE) return 0;
  return (size_t)(end - buf);
}

size_t csupport_json_parse_int64(const char *src, size_t len, int64_t *val) {
  if (len == 0) return 0;
  char buf[32];
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, src, n);
  buf[n] = '\0';
  char *end = NULL;
  errno = 0;
  *val = strtoll(buf, &end, 10);
  if (end == buf || errno == ERANGE) return 0;
  return (size_t)(end - buf);
}

size_t csupport_json_quote_string(const char *src, size_t src_len,
                                  char *dst, size_t dst_cap) {
  if (dst_cap < 3) return 0;
  size_t out = 0;
  dst[out++] = '"';
  for (size_t i = 0; i < src_len; i++) {
    int esc = csupport_json_escape_char(src[i]);
    if (esc >= 0) {
      if (out + 3 > dst_cap) return 0;
      dst[out++] = '\\';
      dst[out++] = (char)esc;
    } else if ((unsigned char)src[i] < 0x20) {
      if (out + 7 > dst_cap) return 0;
      out += (size_t)snprintf(dst + out, dst_cap - out, "\\u%04x",
                              (unsigned)(unsigned char)src[i]);
    } else {
      if (out + 2 > dst_cap) return 0;
      dst[out++] = src[i];
    }
  }
  if (out + 1 >= dst_cap) return 0;
  dst[out++] = '"';
  if (out < dst_cap) dst[out] = '\0';
  return out;
}

int csupport_json_is_number_char(char c) {
  return (c >= '0' && c <= '9') || c == 'e' || c == 'E' ||
         c == '+' || c == '-' || c == '.';
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
  if (len == 0) { *type = 0; return 0; }
  char buf[64];
  size_t i = 0;
  while (i < len && i < sizeof(buf) - 1 && csupport_json_is_number_char(src[i])) {
    buf[i] = src[i];
    i++;
  }
  buf[i] = '\0';
  if (i == 0) { *type = 0; return 0; }

  char *end = NULL;
  errno = 0;
  int64_t iv = strtoll(buf, &end, 10);
  if (end == buf + i && errno != ERANGE) {
    *int_val = iv;
    *type = 1;
    return i;
  }
  if (src[0] != '-') {
    errno = 0;
    uint64_t uv = strtoull(buf, &end, 10);
    if (end == buf + i && errno != ERANGE) {
      *uint_val = uv;
      *type = 2;
      return i;
    }
  }
  *dbl_val = strtod(buf, &end);
  if (end > buf) {
    *type = 3;
    return (size_t)(end - buf);
  }
  *type = 0;
  return 0;
}

int csupport_json_decode_surrogate_pair(uint16_t hi, uint16_t lo, uint32_t *cp) {
  if (hi < 0xD800 || hi >= 0xDC00) return -1;
  if (lo < 0xDC00 || lo >= 0xE000) return -1;
  *cp = 0x10000U + (((uint32_t)(hi - 0xD800)) << 10) + ((uint32_t)(lo - 0xDC00));
  return 0;
}

size_t csupport_json_quote_to_stream(const char *src, size_t src_len,
                                     char *dst, size_t dst_cap) {
  size_t out = 0;
  if (dst_cap < 2) return 0;
  dst[out++] = '"';
  for (size_t i = 0; i < src_len; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c == 0x22 || c == 0x5C) {
      if (out + 2 >= dst_cap) break;
      dst[out++] = '\\';
    }
    if (c >= 0x20) {
      if (out + 1 >= dst_cap) break;
      dst[out++] = (char)c;
      continue;
    }
    if (c == '\t' || c == '\n' || c == '\r') {
      if (out + 2 >= dst_cap) break;
      dst[out++] = '\\';
      dst[out++] = (c == '\t') ? 't' : (c == '\n') ? 'n' : 'r';
    } else {
      if (out + 6 >= dst_cap) break;
      dst[out++] = '\\';
      dst[out++] = 'u';
      out += (size_t)snprintf(dst + out, dst_cap - out, "%04x", (unsigned)c);
    }
  }
  if (out + 1 >= dst_cap) return 0;
  dst[out++] = '"';
  if (out < dst_cap) dst[out] = '\0';
  return out;
}

static size_t write_out(char *dst, size_t dst_cap, size_t *pos, const char *data, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (dst && *pos < dst_cap)
      dst[*pos] = data[i];
    (*pos)++;
  }
  return n;
}

static size_t write_char(char *dst, size_t dst_cap, size_t *pos, char c) {
  return write_out(dst, dst_cap, pos, &c, 1);
}

static int parse_unicode_escape(const char **pos, const char *end,
                                char *dst, size_t dst_cap, size_t *out_pos,
                                const char **error_msg) {
  static const char REPLACEMENT[] = {'\xef', '\xbf', '\xbd'};

  if (*pos + 4 > end) {
    *error_msg = "Invalid \\u escape sequence";
    return -1;
  }
  uint16_t first;
  if (csupport_json_decode_hex4(*pos, &first) != 0) {
    *error_msg = "Invalid \\u escape sequence";
    return -1;
  }
  *pos += 4;

  while (1) {
    if (first < 0xD800 || first >= 0xE000) {
      char buf[4];
      int n = csupport_json_encode_utf8((uint32_t)first, buf, sizeof(buf));
      write_out(dst, dst_cap, out_pos, buf, (size_t)n);
      return 0;
    }
    if (first >= 0xDC00) {
      write_out(dst, dst_cap, out_pos, REPLACEMENT, 3);
      return 0;
    }
    if (*pos + 2 > end || (*pos)[0] != '\\' || (*pos)[1] != 'u') {
      write_out(dst, dst_cap, out_pos, REPLACEMENT, 3);
      return 0;
    }
    *pos += 2;
    uint16_t second;
    if (*pos + 4 > end || csupport_json_decode_hex4(*pos, &second) != 0) {
      *error_msg = "Invalid \\u escape sequence";
      return -1;
    }
    *pos += 4;
    if (second < 0xDC00 || second >= 0xE000) {
      write_out(dst, dst_cap, out_pos, REPLACEMENT, 3);
      first = second;
      continue;
    }
    uint32_t cp = 0x10000u + (((uint32_t)(first - 0xD800)) << 10) +
                  ((uint32_t)(second - 0xDC00));
    char buf[4];
    int n = csupport_json_encode_utf8(cp, buf, sizeof(buf));
    write_out(dst, dst_cap, out_pos, buf, (size_t)n);
    return 0;
  }
}

size_t csupport_json_parse_string_body(const char **pos, const char *end,
                                       char *dst, size_t dst_cap,
                                       const char **error_msg) {
  size_t out = 0;
  const char *p = *pos;
  while (p < end) {
    char c = *p++;
    if (c == '"') {
      *pos = p;
      return out;
    }
    if ((c & 0x1f) == c) {
      *pos = p - 1;
      *error_msg = "Control character in string";
      return (size_t)-1;
    }
    if (c != '\\') {
      write_char(dst, dst_cap, &out, c);
      continue;
    }
    if (p >= end) {
      *pos = p;
      *error_msg = "Unterminated string";
      return (size_t)-1;
    }
    c = *p++;
    switch (c) {
    case '"': case '\\': case '/':
      write_char(dst, dst_cap, &out, c); break;
    case 'b': write_char(dst, dst_cap, &out, '\b'); break;
    case 'f': write_char(dst, dst_cap, &out, '\f'); break;
    case 'n': write_char(dst, dst_cap, &out, '\n'); break;
    case 'r': write_char(dst, dst_cap, &out, '\r'); break;
    case 't': write_char(dst, dst_cap, &out, '\t'); break;
    case 'u': {
      const char *saved = p;
      if (parse_unicode_escape(&p, end, dst, dst_cap, &out, error_msg) < 0) {
        *pos = saved;
        return (size_t)-1;
      }
      break;
    }
    default:
      *pos = p - 1;
      *error_msg = "Invalid escape sequence";
      return (size_t)-1;
    }
  }
  *pos = p;
  *error_msg = "Unterminated string";
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
  if (val == 1.0/0.0 || val == -1.0/0.0) return snprintf(buf, cap, "null");
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
  if (cap >= 5) { memcpy(buf, "null", 4); buf[4] = '\0'; }
  return 4;
}

size_t csupport_json_format_value_bool(char *buf, size_t cap, int val) {
  if (val) {
    if (cap >= 5) { memcpy(buf, "true", 4); buf[4] = '\0'; }
    return 4;
  }
  if (cap >= 6) { memcpy(buf, "false", 5); buf[5] = '\0'; }
  return 5;
}

int csupport_json_validate_utf8(const char *data, size_t len) {
  size_t i = 0;
  while (i < len) {
    unsigned char c = (unsigned char)data[i];
    int n;
    if (c < 0x80) { n = 1; }
    else if ((c & 0xE0) == 0xC0) { n = 2; }
    else if ((c & 0xF0) == 0xE0) { n = 3; }
    else if ((c & 0xF8) == 0xF0) { n = 4; }
    else return 0;
    if (i + n > len) return 0;
    for (int j = 1; j < n; j++)
      if (((unsigned char)data[i+j] & 0xC0) != 0x80) return 0;
    i += n;
  }
  return 1;
}

size_t csupport_json_compact(const char *src, size_t src_len,
                             char *dst, size_t dst_cap) {
  size_t out = 0;
  int in_string = 0;
  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    if (in_string) {
      if (out < dst_cap) dst[out] = c;
      out++;
      if (c == '"') in_string = 0;
      else if (c == '\\' && i + 1 < src_len) {
        i++;
        if (out < dst_cap) dst[out] = src[i];
        out++;
      }
    } else {
      if (c == '"') {
        in_string = 1;
        if (out < dst_cap) dst[out] = c;
        out++;
      } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        if (out < dst_cap) dst[out] = c;
        out++;
      }
    }
  }
  if (out < dst_cap) dst[out] = '\0';
  return out;
}

size_t csupport_json_prettify(const char *src, size_t src_len,
                               char *dst, size_t dst_cap,
                               unsigned indent_width) {
  size_t out = 0;
  unsigned depth = 0;
  int in_string = 0;

#define JP_PUT(ch) do { if (out < dst_cap) dst[out] = (ch); out++; } while(0)
#define JP_NEWLINE() do { \
  JP_PUT('\n'); \
  for (unsigned _i = 0; _i < depth * indent_width; _i++) JP_PUT(' '); \
} while(0)

  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    if (in_string) {
      JP_PUT(c);
      if (c == '"') in_string = 0;
      else if (c == '\\' && i + 1 < src_len) { i++; JP_PUT(src[i]); }
      continue;
    }
    switch (c) {
    case '"': in_string = 1; JP_PUT(c); break;
    case '{': case '[': JP_PUT(c); depth++; JP_NEWLINE(); break;
    case '}': case ']': depth--; JP_NEWLINE(); JP_PUT(c); break;
    case ',': JP_PUT(c); JP_NEWLINE(); break;
    case ':': JP_PUT(c); JP_PUT(' '); break;
    case ' ': case '\t': case '\n': case '\r': break;
    default: JP_PUT(c); break;
    }
  }
  if (out < dst_cap) dst[out] = '\0';
#undef JP_PUT
#undef JP_NEWLINE
  return out;
}

int csupport_json_depth(const char *src, size_t src_len) {
  int depth = 0, max_depth = 0;
  int in_string = 0;
  for (size_t i = 0; i < src_len; i++) {
    if (in_string) {
      if (src[i] == '"') in_string = 0;
      else if (src[i] == '\\' && i + 1 < src_len) i++;
      continue;
    }
    if (src[i] == '"') in_string = 1;
    else if (src[i] == '{' || src[i] == '[') {
      depth++;
      if (depth > max_depth) max_depth = depth;
    }
    else if (src[i] == '}' || src[i] == ']') depth--;
  }
  return max_depth;
}

int csupport_json_is_valid_utf8(const char *src, size_t src_len) {
  size_t i = 0;
  while (i < src_len) {
    unsigned char c = (unsigned char)src[i];
    int expected;
    if (c <= 0x7F) { i++; continue; }
    else if ((c & 0xE0) == 0xC0) expected = 2;
    else if ((c & 0xF0) == 0xE0) expected = 3;
    else if ((c & 0xF8) == 0xF0) expected = 4;
    else return 0;
    if (i + (size_t)expected > src_len) return 0;
    for (int j = 1; j < expected; j++) {
      if (((unsigned char)src[i + j] & 0xC0) != 0x80) return 0;
    }
    i += (size_t)expected;
  }
  return 1;
}

size_t csupport_json_minify(const char *src, size_t src_len,
                             char *dst, size_t dst_cap) {
  size_t out = 0;
  int in_string = 0;
  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    if (in_string) {
      if (out < dst_cap) dst[out] = c;
      out++;
      if (c == '\\' && i + 1 < src_len) {
        i++;
        if (out < dst_cap) dst[out] = src[i];
        out++;
      } else if (c == '"') {
        in_string = 0;
      }
    } else {
      if (c == '"') {
        in_string = 1;
        if (out < dst_cap) dst[out] = c;
        out++;
      } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        if (out < dst_cap) dst[out] = c;
        out++;
      }
    }
  }
  if (out < dst_cap) dst[out] = '\0';
  return out;
}

int csupport_json_validate(const char *src, size_t src_len) {
  int depth = 0;
  int in_string = 0;
  int expect_value = 0;
  int has_content = 0;
  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    if (in_string) {
      if (c == '\\' && i + 1 < src_len) { i++; continue; }
      if (c == '"') in_string = 0;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
    has_content = 1;
    if (c == '"') { in_string = 1; expect_value = 0; }
    else if (c == '{' || c == '[') { depth++; expect_value = 0; }
    else if (c == '}' || c == ']') { depth--; if (depth < 0) return 0; }
    else if (c == ':') { expect_value = 1; }
    else if (c == ',') { expect_value = 1; }
    else { expect_value = 0; }
  }
  (void)expect_value;
  return depth == 0 && has_content && !in_string;
}

size_t csupport_json_pointer_get(const char *json, size_t json_len,
                                  const char *pointer, size_t ptr_len,
                                  const char **value_start) {
  (void)json; (void)json_len; (void)pointer; (void)ptr_len;
  *value_start = 0;
  return 0;
}

size_t csupport_json_count_keys(const char *src, size_t src_len) {
  size_t count = 0;
  int depth = 0;
  int in_string = 0;
  int after_brace = 0;
  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    if (in_string) {
      if (c == '\\' && i + 1 < src_len) { i++; continue; }
      if (c == '"') in_string = 0;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
    if (c == '{') {
      depth++;
      if (depth == 1) after_brace = 1;
    }
    else if (c == '}') { depth--; }
    else if (c == '[') { depth++; }
    else if (c == ']') { depth--; }
    else if (c == '"' && depth == 1) {
      if (after_brace) { count++; after_brace = 0; }
      in_string = 1;
    }
    else if (c == ',' && depth == 1) {
      count++;
    }
    else { after_brace = 0; }
  }
  return count;
}

int csupport_json_match_literal(const char *src, size_t src_len,
                                 size_t pos, const char *literal) {
  size_t llen = strlen(literal);
  if (pos + llen > src_len) return 0;
  return memcmp(src + pos, literal, llen) == 0;
}

size_t csupport_json_format_object_entry(const char *key, size_t key_len,
                                          const char *value, size_t value_len,
                                          char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  if (pos < out_cap) out[pos] = '"';
  pos++;
  pos += csupport_json_escape_string(key, key_len,
                                      pos < out_cap ? out + pos : out,
                                      pos < out_cap ? out_cap - pos : 0);
  const char *sep = "\":";
  for (size_t i = 0; sep[i]; i++) {
    if (pos < out_cap) out[pos] = sep[i];
    pos++;
  }
  for (size_t i = 0; i < value_len; i++) {
    if (pos < out_cap) out[pos] = value[i];
    pos++;
  }
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}
