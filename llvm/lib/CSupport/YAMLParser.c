/*===- YAMLParser.c - YAML parsing utilities (pure C) ---------*- C -*-===*/
#include "include/csupport/ly_la_lm_ll_lparser.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

int csupport_yaml_is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int csupport_yaml_is_flow_indicator(char c) {
  return c == ',' || c == '[' || c == ']' || c == '{' || c == '}';
}

static int yaml_is_numeric(const char *str, size_t len) {
  if (len == 0) return 0;
  if (len == 1 && (str[0] == '+' || str[0] == '-')) return 0;

  if (len == 4 && str[0] == '.' &&
      (memcmp(str, ".nan", 4) == 0 || memcmp(str, ".NaN", 4) == 0 ||
       memcmp(str, ".NAN", 4) == 0))
    return 1;

  size_t start = 0;
  if (str[0] == '+' || str[0] == '-') start = 1;

  if (len - start == 4 &&
      (memcmp(str + start, ".inf", 4) == 0 ||
       memcmp(str + start, ".Inf", 4) == 0 ||
       memcmp(str + start, ".INF", 4) == 0))
    return 1;

  if (start == 0 && len > 2 && str[0] == '0') {
    if (str[1] == 'o') {
      for (size_t i = 2; i < len; i++)
        if (str[i] < '0' || str[i] > '7') return 0;
      return 1;
    }
    if (str[1] == 'x') {
      for (size_t i = 2; i < len; i++) {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
          return 0;
      }
      return 1;
    }
  }

  const char *p = str + start;
  size_t rem = len - start;
  if (rem == 0) return 0;
  if (p[0] == 'e' || p[0] == 'E') return 0;
  if (p[0] == '.' && (rem == 1 || p[1] < '0' || p[1] > '9')) return 0;

  size_t j = 0;
  while (j < rem && p[j] >= '0' && p[j] <= '9') j++;
  if (j == rem) return 1;

  if (p[j] == '.') {
    j++;
    while (j < rem && p[j] >= '0' && p[j] <= '9') j++;
    if (j == rem) return 1;
  }

  if (p[j] == 'e' || p[j] == 'E') {
    j++;
    if (j < rem && (p[j] == '+' || p[j] == '-')) j++;
    if (j >= rem) return 0;
    size_t ej = j;
    while (j < rem && p[j] >= '0' && p[j] <= '9') j++;
    if (j == ej) return 0;
    if (j == rem) return 1;
  }

  return 0;
}

int csupport_yaml_needs_quoting(const char *str, size_t len) {
  if (len == 0) return 1;

  int max_quoting = 0;
  if (str[0] == ' ' || str[0] == '\t') max_quoting = 1;
  if (str[len - 1] == ' ' || str[len - 1] == '\t') max_quoting = 1;

  if (len == 4 && (memcmp(str, "null", 4) == 0 ||
                   memcmp(str, "Null", 4) == 0 ||
                   memcmp(str, "NULL", 4) == 0))
    max_quoting = 1;
  if (csupport_yaml_parse_bool(str, len) >= 0)
    max_quoting = 1;
  if (len == 1 && (str[0] == '~' ||
                   str[0] == 'y' || str[0] == 'Y' ||
                   str[0] == 'n' || str[0] == 'N'))
    max_quoting = 1;
  if (yaml_is_numeric(str, len))
    max_quoting = 1;

  if (str[0] == '-' || str[0] == '?' || str[0] == ':' ||
      str[0] == '\\' || str[0] == ',' || str[0] == '[' ||
      str[0] == ']' || str[0] == '{' || str[0] == '}' ||
      str[0] == '#' || str[0] == '&' || str[0] == '*' ||
      str[0] == '!' || str[0] == '|' || str[0] == '>' ||
      str[0] == '\'' || str[0] == '"' || str[0] == '%' ||
      str[0] == '@' || str[0] == '`')
    max_quoting = 1;

  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9'))
      continue;
    switch (c) {
    case '_': case '-': case '^': case '.': case ',':
    case ' ': case '\t':
      continue;
    case '\n': case '\r': case 0x7F:
      return 2;
    case '/':
    default:
      if (c <= 0x1F) return 2;
      if (c & 0x80) return 2;
      max_quoting = 1;
    }
  }

  return max_quoting;
}

size_t csupport_yaml_scan_to_next_token(const char *input, size_t len,
                                        size_t start) {
  size_t pos = start;
  while (pos < len) {
    char c = input[pos];
    if (c == ' ' || c == '\t') { pos++; continue; }
    if (c == '#') {
      while (pos < len && input[pos] != '\n') pos++;
      continue;
    }
    break;
  }
  return pos;
}

int csupport_yaml_decode_hex_escape(const char *start, size_t num_digits,
                                    uint32_t *out_codepoint) {
  uint32_t val = 0;
  for (size_t i = 0; i < num_digits; i++) {
    char c = start[i];
    uint32_t nibble;
    if (c >= '0' && c <= '9') nibble = (uint32_t)(c - '0');
    else if (c >= 'a' && c <= 'f') nibble = (uint32_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') nibble = (uint32_t)(c - 'A' + 10);
    else return 0;
    val = (val << 4) | nibble;
  }
  *out_codepoint = val;
  return 1;
}

static int yaml_encode_utf8(uint32_t cp, char *buf, size_t buflen) {
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

static int yaml_utf8_char_len(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 0;
}

static uint32_t yaml_decode_utf8(const char *s, size_t avail, int *len) {
  unsigned char c = (unsigned char)s[0];
  int n = yaml_utf8_char_len(c);
  if (n == 0 || (size_t)n > avail) { *len = 1; return 0xFFFD; }
  uint32_t cp;
  switch (n) {
  case 1: cp = c; break;
  case 2: cp = c & 0x1F; break;
  case 3: cp = c & 0x0F; break;
  case 4: cp = c & 0x07; break;
  default: *len = 1; return 0xFFFD;
  }
  for (int i = 1; i < n; i++) {
    unsigned char b = (unsigned char)s[i];
    if ((b & 0xC0) != 0x80) { *len = 1; return 0xFFFD; }
    cp = (cp << 6) | (b & 0x3F);
  }
  *len = n;
  return cp;
}

static int yaml_is_printable_cp(uint32_t cp) {
  if (cp < 0x20) return cp == 0x09 || cp == 0x0A || cp == 0x0D;
  if (cp <= 0x7E) return 1;
  if (cp == 0x85 || cp == 0xA0 || cp == 0x2028 || cp == 0x2029) return 0;
  if (cp >= 0xA0 && cp <= 0xD7FF) return 1;
  if (cp >= 0xE000 && cp <= 0xFFFD) return 1;
  if (cp >= 0x10000 && cp <= 0x10FFFF) return 1;
  return 0;
}

#define YAML_EMIT(c) do { if (pos < out_cap - 1) out[pos++] = (c); } while(0)
#define YAML_EMITS(s, n) do { for (size_t _k = 0; _k < (n) && pos < out_cap - 1; _k++) out[pos++] = (s)[_k]; } while(0)

size_t csupport_yaml_escape(const char *input, size_t input_len,
                            int escape_printable,
                            char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  for (size_t i = 0; i < input_len; ) {
    unsigned char c = (unsigned char)input[i];
    if (c == '\\') { YAML_EMITS("\\\\", 2); i++; }
    else if (c == '"') { YAML_EMITS("\\\"", 2); i++; }
    else if (c == 0) { YAML_EMITS("\\0", 2); i++; }
    else if (c == 0x07) { YAML_EMITS("\\a", 2); i++; }
    else if (c == 0x08) { YAML_EMITS("\\b", 2); i++; }
    else if (c == 0x09) { YAML_EMITS("\\t", 2); i++; }
    else if (c == 0x0A) { YAML_EMITS("\\n", 2); i++; }
    else if (c == 0x0B) { YAML_EMITS("\\v", 2); i++; }
    else if (c == 0x0C) { YAML_EMITS("\\f", 2); i++; }
    else if (c == 0x0D) { YAML_EMITS("\\r", 2); i++; }
    else if (c == 0x1B) { YAML_EMITS("\\e", 2); i++; }
    else if (c < 0x20) {
      char hex[8];
      int n = snprintf(hex, sizeof(hex), "\\x%02X", (unsigned)c);
      YAML_EMITS(hex, (size_t)n);
      i++;
    } else if (c & 0x80) {
      int len = 0;
      uint32_t cp = yaml_decode_utf8(input + i, input_len - i, &len);
      if (len <= 0 || (cp == 0xFFFD && len == 1)) {
        char repl[4];
        int rn = yaml_encode_utf8(0xFFFD, repl, sizeof(repl));
        YAML_EMITS(repl, (size_t)rn);
        i += 1;
        continue;
      }
      if (cp == 0x85) { YAML_EMITS("\\N", 2); }
      else if (cp == 0xA0) { YAML_EMITS("\\_", 2); }
      else if (cp == 0x2028) { YAML_EMITS("\\L", 2); }
      else if (cp == 0x2029) { YAML_EMITS("\\P", 2); }
      else if (!escape_printable && yaml_is_printable_cp(cp)) {
        YAML_EMITS(input + i, (size_t)len);
      } else {
        char hex[16];
        int n;
        if (cp <= 0xFF)
          n = snprintf(hex, sizeof(hex), "\\x%02X", cp);
        else if (cp <= 0xFFFF)
          n = snprintf(hex, sizeof(hex), "\\u%04X", cp);
        else
          n = snprintf(hex, sizeof(hex), "\\U%08X", cp);
        YAML_EMITS(hex, (size_t)n);
      }
      i += (size_t)len;
    } else {
      YAML_EMIT((char)c);
      i++;
    }
  }
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}

#undef YAML_EMIT
#undef YAML_EMITS

int csupport_yaml_encode_utf8(uint32_t cp, char *buf, size_t buflen) {
  return yaml_encode_utf8(cp, buf, buflen);
}

int csupport_yaml_is_nb_char(const char *pos, const char *end) {
  if (pos >= end) return 0;
  unsigned char c = (unsigned char)*pos;
  if (c == 0x09 || (c >= 0x20 && c <= 0x7E))
    return 1;
  if (c & 0x80) {
    int len = 0;
    uint32_t cp = yaml_decode_utf8(pos, (size_t)(end - pos), &len);
    if (len > 0 && cp != 0xFFFD && cp != 0xFEFF &&
        (cp == 0x85 || (cp >= 0xA0 && cp <= 0xD7FF) ||
         (cp >= 0xE000 && cp <= 0xFFFD) ||
         (cp >= 0x10000 && cp <= 0x10FFFF)))
      return len;
  }
  return 0;
}

int csupport_yaml_is_b_break(const char *pos, const char *end) {
  if (pos >= end) return 0;
  if (*pos == '\r') {
    if (pos + 1 < end && *(pos + 1) == '\n') return 2;
    return 1;
  }
  if (*pos == '\n') return 1;
  return 0;
}

int csupport_yaml_is_blank_or_break(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int csupport_yaml_parse_bool(const char *str, size_t len) {
  if (len == 1) {
    if (str[0] == 'y' || str[0] == 'Y') return 1;
    if (str[0] == 'n' || str[0] == 'N') return 0;
    return -1;
  }
  if (len == 2) {
    if ((str[0] == 'O' || str[0] == 'o') && str[1] == 'n') return 1;
    if (str[0] == 'O' && str[1] == 'N') return 1;
    if ((str[0] == 'N' || str[0] == 'n') && str[1] == 'o') return 0;
    if (str[0] == 'N' && str[1] == 'O') return 0;
    return -1;
  }
  if (len == 3) {
    if ((str[0] == 'O' || str[0] == 'o') && str[1] == 'f' && str[2] == 'f')
      return 0;
    if (str[0] == 'O' && str[1] == 'F' && str[2] == 'F') return 0;
    if ((str[0] == 'Y' || str[0] == 'y') && str[1] == 'e' && str[2] == 's')
      return 1;
    if (str[0] == 'Y' && str[1] == 'E' && str[2] == 'S') return 1;
    return -1;
  }
  if (len == 4) {
    if ((str[0] == 'T' || str[0] == 't') && str[1] == 'r' && str[2] == 'u' && str[3] == 'e')
      return 1;
    if (str[0] == 'T' && str[1] == 'R' && str[2] == 'U' && str[3] == 'E') return 1;
    return -1;
  }
  if (len == 5) {
    if ((str[0] == 'F' || str[0] == 'f') && str[1] == 'a' && str[2] == 'l' && str[3] == 's' && str[4] == 'e')
      return 0;
    if (str[0] == 'F' && str[1] == 'A' && str[2] == 'L' && str[3] == 'S' && str[4] == 'E') return 0;
    return -1;
  }
  return -1;
}

int csupport_yaml_skip_nb_char(const char *pos, const char *end) {
  int n = csupport_yaml_is_nb_char(pos, end);
  return n > 0 ? n : 0;
}

int csupport_yaml_skip_ns_char(const char *pos, const char *end) {
  if (pos >= end) return 0;
  if (*pos == ' ' || *pos == '\t') return 0;
  return csupport_yaml_skip_nb_char(pos, end);
}

size_t csupport_yaml_skip_while_ns_char(const char *pos, const char *end) {
  size_t total = 0;
  while (pos < end) {
    int n = csupport_yaml_skip_ns_char(pos, end);
    if (n == 0) break;
    total += (size_t)n;
    pos += n;
  }
  return total;
}

size_t csupport_yaml_skip_while_s_white(const char *pos, const char *end) {
  size_t total = 0;
  while (pos < end && (*pos == ' ' || *pos == '\t')) {
    ++pos; ++total;
  }
  return total;
}

size_t csupport_yaml_skip_while_nb_char(const char *pos, const char *end) {
  size_t total = 0;
  while (pos < end) {
    int n = csupport_yaml_is_nb_char(pos, end);
    if (n <= 0) break;
    total += (size_t)n;
    pos += n;
  }
  return total;
}

size_t csupport_yaml_skip_while_s_space(const char *pos, const char *end) {
  size_t total = 0;
  while (pos < end && *pos == ' ') {
    ++pos; ++total;
  }
  return total;
}

int csupport_yaml_was_escaped(const char *first, const char *position) {
  if (position <= first) return 0;
  const char *i = position - 1;
  while (i >= first && *i == '\\')
    --i;
  return ((position - 1 - i) % 2 == 1) ? 1 : 0;
}

unsigned csupport_yaml_get_chomped_line_breaks(char chomping_indicator,
                                               unsigned line_breaks,
                                               const char *str, size_t str_len) {
  if (chomping_indicator == '-') return 0;
  if (chomping_indicator == '+') return line_breaks;
  return (str_len == 0) ? 0 : 1;
}

int csupport_yaml_is_ns_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

int csupport_yaml_is_ns_word_char(char c) {
  return c == '-' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int csupport_yaml_is_ns_uri_char(char c, const char *next, const char *end) {
  if (c == '%' && next + 1 < end &&
      csupport_yaml_is_ns_hex_digit(*next) &&
      csupport_yaml_is_ns_hex_digit(*(next + 1)))
    return 1;
  if (csupport_yaml_is_ns_word_char(c)) return 1;
  const char *special = "#;/?:@&=+$,_.!~*'()[]";
  for (const char *s = special; *s; ++s)
    if (c == *s) return 1;
  return 0;
}

int csupport_yaml_is_plain_safe_non_blank(char c, int flow_level) {
  if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return 0;
  if (flow_level && (c == ',' || c == '[' || c == ']' ||
                     c == '{' || c == '}'))
    return 0;
  return 1;
}

int csupport_yaml_is_line_empty(const char *line, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    char c = line[i];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
      return 0;
  }
  return 1;
}

uint32_t csupport_yaml_decode_utf8(const char *data, size_t len,
                                   unsigned *out_len) {
  const unsigned char *p = (const unsigned char *)data;
  *out_len = 0;
  if (len == 0) return 0;
  /* 1 byte: [0x00, 0x7f] */
  if ((p[0] & 0x80) == 0) {
    *out_len = 1;
    return p[0];
  }
  /* 2 bytes: [0x80, 0x7ff] */
  if (len >= 2 && (p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
    uint32_t cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    if (cp >= 0x80) { *out_len = 2; return cp; }
  }
  /* 3 bytes: [0x800, 0xffff] */
  if (len >= 3 && (p[0] & 0xF0) == 0xE0 &&
      (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
    uint32_t cp = ((uint32_t)(p[0] & 0x0F) << 12) |
                  ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    if (cp >= 0x800 && (cp < 0xD800 || cp > 0xDFFF)) {
      *out_len = 3;
      return cp;
    }
  }
  /* 4 bytes: [0x10000, 0x10FFFF] */
  if (len >= 4 && (p[0] & 0xF8) == 0xF0 &&
      (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
      (p[3] & 0xC0) == 0x80) {
    uint32_t cp = ((uint32_t)(p[0] & 0x07) << 18) |
                  ((uint32_t)(p[1] & 0x3F) << 12) |
                  ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    if (cp >= 0x10000 && cp <= 0x10FFFF) {
      *out_len = 4;
      return cp;
    }
  }
  return 0;
}

int csupport_yaml_scan_block_style_indicator(char c) {
  return (c == '>' || c == '|') ? c : 0;
}

int csupport_yaml_scan_block_chomping_indicator(char c) {
  return (c == '+' || c == '-') ? c : 0;
}

unsigned csupport_yaml_scan_block_indent_indicator(char c) {
  if (c >= '1' && c <= '9') return (unsigned)(c - '0');
  return 0;
}

int csupport_yaml_is_document_indicator(const char *pos, const char *end,
                                         int column) {
  if (column != 0) return 0;
  if (end - pos < 3) return 0;
  if (pos[0] == '-' && pos[1] == '-' && pos[2] == '-') return 1;
  if (pos[0] == '.' && pos[1] == '.' && pos[2] == '.') return 2;
  return 0;
}

size_t csupport_yaml_skip_whitespace(const char *pos, const char *end) {
  const char *p = pos;
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  return (size_t)(p - pos);
}

size_t csupport_yaml_count_indent(const char *pos, const char *end) {
  const char *p = pos;
  while (p < end && *p == ' ') p++;
  return (size_t)(p - pos);
}

int csupport_yaml_is_tag_char(char c) {
  if (c == '!' || c == '#' || c == ';' || c == '/' || c == '?' ||
      c == ':' || c == '@' || c == '&' || c == '=' || c == '+' ||
      c == '$' || c == ',' || c == '_' || c == '.' || c == '~' ||
      c == '*' || c == '\'' || c == '(' || c == ')') return 1;
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) return 1;
  return 0;
}

size_t csupport_yaml_scan_tag_handle(const char *pos, size_t len,
                                      const char **tag_start) {
  if (len == 0 || pos[0] != '!') { *tag_start = pos; return 0; }
  size_t i = 1;
  if (i < len && pos[i] == '!') {
    *tag_start = pos;
    return 2;
  }
  while (i < len && csupport_yaml_is_tag_char(pos[i])) i++;
  if (i < len && pos[i] == '!') {
    *tag_start = pos;
    return i + 1;
  }
  *tag_start = pos;
  return 1;
}

int csupport_yaml_is_break(const char *pos, const char *end) {
  if (pos >= end) return 0;
  if (*pos == '\n') return 1;
  if (*pos == '\r') return (pos + 1 < end && pos[1] == '\n') ? 2 : 1;
  return 0;
}

size_t csupport_yaml_skip_break(const char *pos, const char *end) {
  if (pos >= end) return 0;
  if (*pos == '\n') return 1;
  if (*pos == '\r') {
    if (pos + 1 < end && pos[1] == '\n') return 2;
    return 1;
  }
  return 0;
}

size_t csupport_yaml_scan_to_next_token_ex(const char *pos, const char *end,
                                            unsigned *column, unsigned *line,
                                            unsigned flow_level) {
  const char *start = pos;
  for (;;) {
    while (pos < end && (*pos == ' ' || (flow_level && *pos == '\t'))) {
      pos++;
      (*column)++;
    }
    if (pos >= end) break;
    if (*pos == '#') {
      while (pos < end && *pos != '\n' && *pos != '\r') {
        pos++;
        (*column)++;
      }
      if (pos >= end) break;
    }
    size_t brk = csupport_yaml_skip_break(pos, end);
    if (brk == 0) break;
    pos += brk;
    (*line)++;
    *column = 0;
  }
  return (size_t)(pos - start);
}

size_t csupport_yaml_normalize_line_breaks(const char *src, size_t src_len,
                                            char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  size_t i = 0;
  while (i < src_len && pos + 1 < out_cap) {
    if (src[i] == '\r') {
      out[pos++] = '\n';
      if (i + 1 < src_len && src[i+1] == '\n') i++;
      i++;
    } else {
      out[pos++] = src[i++];
    }
  }
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}

int csupport_yaml_scan_flow_indicator(char c) {
  switch (c) {
  case '[': return 1;
  case ']': return 2;
  case '{': return 3;
  case '}': return 4;
  case ',': return 5;
  default:  return 0;
  }
}

int csupport_yaml_is_anchor_char(char c) {
  if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',') return 0;
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 0;
  if (c == '\0') return 0;
  return 1;
}

size_t csupport_yaml_scan_anchor(const char *pos, const char *end) {
  const char *p = pos;
  while (p < end && csupport_yaml_is_anchor_char(*p)) p++;
  return (size_t)(p - pos);
}

size_t csupport_yaml_scan_alias(const char *pos, const char *end) {
  return csupport_yaml_scan_anchor(pos, end);
}

int csupport_yaml_count_line_break_len(const char *pos, const char *end) {
  if (pos >= end) return 0;
  if (*pos == '\n') return 1;
  if (*pos == '\r') return (pos + 1 < end && pos[1] == '\n') ? 2 : 1;
  if ((unsigned char)*pos == 0xC2 && pos + 1 < end &&
      (unsigned char)pos[1] == 0x85) return 2;
  if ((unsigned char)*pos == 0xE2 && pos + 2 < end &&
      (unsigned char)pos[1] == 0x80 &&
      ((unsigned char)pos[2] == 0xA8 || (unsigned char)pos[2] == 0xA9))
    return 3;
  return 0;
}

size_t csupport_yaml_fold_newlines(const char *src, size_t src_len,
                                    char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  size_t i = 0;
  int consecutive_breaks = 0;
  while (i < src_len && pos + 1 < out_cap) {
    int blen = csupport_yaml_count_line_break_len(src + i, src + src_len);
    if (blen > 0) {
      consecutive_breaks++;
      if (consecutive_breaks > 1) {
        out[pos++] = '\n';
      }
      i += (size_t)blen;
    } else {
      if (consecutive_breaks == 1) {
        out[pos++] = ' ';
      }
      consecutive_breaks = 0;
      out[pos++] = src[i++];
    }
  }
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}

int csupport_yaml_scan_flow_scalar_single(const char *p, size_t len,
                                          char *out, size_t cap,
                                          size_t *out_len, int *lines) {
  if (!p || len == 0 || !out || cap == 0) return 0;
  size_t i = 0, pos = 0;
  int lc = 0;
  if (p[0] != '\'') return 0;
  i = 1;
  while (i < len) {
    if (p[i] == '\'') {
      if (i + 1 < len && p[i+1] == '\'') {
        if (pos < cap) out[pos++] = '\'';
        i += 2;
      } else {
        i++;
        break;
      }
    } else if (p[i] == '\n' || p[i] == '\r') {
      lc++;
      if (pos < cap) out[pos++] = '\n';
      if (p[i] == '\r' && i + 1 < len && p[i+1] == '\n') i++;
      i++;
    } else {
      if (pos < cap) out[pos++] = p[i];
      i++;
    }
  }
  if (pos < cap) out[pos] = '\0';
  if (out_len) *out_len = pos;
  if (lines) *lines = lc;
  return (int)i;
}

int csupport_yaml_scan_flow_scalar_double(const char *p, size_t len,
                                           char *out, size_t cap,
                                           size_t *out_len, int *lines) {
  if (!p || len == 0 || !out || cap == 0) return 0;
  size_t i = 0, pos = 0;
  int lc = 0;
  if (p[0] != '"') return 0;
  i = 1;
  while (i < len && p[i] != '"') {
    if (p[i] == '\\' && i + 1 < len) {
      i++;
      switch (p[i]) {
      case '0': if (pos < cap) out[pos++] = '\0'; break;
      case 'a': if (pos < cap) out[pos++] = '\a'; break;
      case 'b': if (pos < cap) out[pos++] = '\b'; break;
      case 't': case '\t': if (pos < cap) out[pos++] = '\t'; break;
      case 'n': if (pos < cap) out[pos++] = '\n'; break;
      case 'v': if (pos < cap) out[pos++] = '\v'; break;
      case 'f': if (pos < cap) out[pos++] = '\f'; break;
      case 'r': if (pos < cap) out[pos++] = '\r'; break;
      case 'e': if (pos < cap) out[pos++] = 0x1B; break;
      case ' ': if (pos < cap) out[pos++] = ' '; break;
      case '"': if (pos < cap) out[pos++] = '"'; break;
      case '/': if (pos < cap) out[pos++] = '/'; break;
      case '\\': if (pos < cap) out[pos++] = '\\'; break;
      case 'N': if (pos + 2 <= cap) { out[pos++]=(char)0xC2; out[pos++]=(char)0x85; } break;
      case '_': if (pos + 2 <= cap) { out[pos++]=(char)0xC2; out[pos++]=(char)0xA0; } break;
      case 'L': if (pos + 3 <= cap) { out[pos++]=(char)0xE2; out[pos++]=(char)0x80; out[pos++]=(char)0xA8; } break;
      case 'P': if (pos + 3 <= cap) { out[pos++]=(char)0xE2; out[pos++]=(char)0x80; out[pos++]=(char)0xA9; } break;
      case 'x':
        if (i + 2 < len) {
          uint32_t cp_val;
          if (!csupport_yaml_decode_hex_escape(p + i + 1, 2, &cp_val))
            cp_val = 0xFFFD;
          char utf8[4];
          int ulen = yaml_encode_utf8(cp_val, utf8, sizeof(utf8));
          for (int u = 0; u < ulen && pos < cap; u++) out[pos++] = utf8[u];
          i += 2;
        }
        break;
      case 'u':
        if (i + 4 < len) {
          uint32_t cp_val;
          if (!csupport_yaml_decode_hex_escape(p + i + 1, 4, &cp_val))
            cp_val = 0xFFFD;
          char utf8[4];
          int ulen = yaml_encode_utf8(cp_val, utf8, sizeof(utf8));
          for (int u = 0; u < ulen && pos < cap; u++) out[pos++] = utf8[u];
          i += 4;
        }
        break;
      case 'U':
        if (i + 8 < len) {
          uint32_t cp_val;
          if (!csupport_yaml_decode_hex_escape(p + i + 1, 8, &cp_val))
            cp_val = 0xFFFD;
          char utf8[4];
          int ulen = yaml_encode_utf8(cp_val, utf8, sizeof(utf8));
          for (int u = 0; u < ulen && pos < cap; u++) out[pos++] = utf8[u];
          i += 8;
        }
        break;
      case '\r':
        if (i + 1 < len && p[i+1] == '\n') i++;
        __attribute__((fallthrough));
      case '\n':
        lc++;
        while (i + 1 < len && (p[i+1] == ' ' || p[i+1] == '\t')) i++;
        break;
      default: if (pos < cap) out[pos++] = p[i]; break;
      }
      i++;
    } else if (p[i] == '\n' || p[i] == '\r') {
      lc++;
      if (pos < cap) out[pos++] = '\n';
      if (p[i] == '\r' && i + 1 < len && p[i+1] == '\n') i++;
      i++;
    } else {
      if (pos < cap) out[pos++] = p[i++];
    }
  }
  if (i < len && p[i] == '"') i++;
  if (pos < cap) out[pos] = '\0';
  if (out_len) *out_len = pos;
  if (lines) *lines = lc;
  return (int)i;
}

int csupport_yaml_detect_scalar_style(const char *p, size_t len) {
  if (!p || len == 0) return 0;
  if (p[0] == '\'') return 1;
  if (p[0] == '"') return 2;
  if (p[0] == '|') return 3;
  if (p[0] == '>') return 4;
  return 0;
}

size_t csupport_yaml_strip_trailing_newlines(char *buf, size_t len) {
  while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
    len--;
  buf[len] = '\0';
  return len;
}

int csupport_yaml_classify_token_start(char c, char next_c,
                                        int at_col0, int flow_level,
                                        int has_end) {
  if (at_col0 && c == '%') return 1;
  if (c == '[') return 2;
  if (c == '{') return 3;
  if (c == ']') return 4;
  if (c == '}') return 5;
  if (c == ',') return 6;
  if (c == '-' && (has_end || next_c == ' ' || next_c == '\t' ||
                   next_c == '\n' || next_c == '\r'))
    return 7;
  if (c == '?' && (has_end || next_c == ' ' || next_c == '\t' ||
                   next_c == '\n' || next_c == '\r'))
    return 8;
  if (c == '*') return 10;
  if (c == '&') return 11;
  if (c == '!') return 12;
  if (c == '|' && !flow_level) return 13;
  if (c == '>' && !flow_level) return 14;
  if (c == '\'') return 15;
  if (c == '"') return 16;
  return 0;
}

int csupport_yaml_is_document_start(const char *p, const char *end,
                                     int column) {
  if (column != 0) return 0;
  if (p + 3 > end) return 0;
  if (p[0] == '-' && p[1] == '-' && p[2] == '-') {
    if (p + 3 == end) return 1;
    char c = p[3];
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r') ? 1 : 0;
  }
  return 0;
}

int csupport_yaml_is_document_end(const char *p, const char *end,
                                   int column) {
  if (column != 0) return 0;
  if (p + 3 > end) return 0;
  if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
    if (p + 3 == end) return 1;
    char c = p[3];
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r') ? 1 : 0;
  }
  return 0;
}

int csupport_yaml_is_plain_first_char(char c) {
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 0;
  if (c == '-' || c == '?' || c == ':' || c == ',' ||
      c == '[' || c == ']' || c == '{' || c == '}' ||
      c == '#' || c == '&' || c == '*' || c == '!' ||
      c == '|' || c == '>' || c == '\'' || c == '"' ||
      c == '%' || c == '@' || c == '`')
    return 0;
  return 1;
}

int csupport_yaml_count_indent_chars(const char *p, const char *end) {
  int count = 0;
  while (p < end && *p == ' ') { count++; p++; }
  return count;
}

size_t csupport_yaml_unescape_single_quoted(const char *src, size_t src_len,
                                              char *dst, size_t dst_cap) {
  size_t out = 0;
  for (size_t i = 0; i < src_len; i++) {
    if (src[i] == '\'' && i + 1 < src_len && src[i + 1] == '\'') {
      if (out < dst_cap) dst[out] = '\'';
      out++;
      i++;
    } else {
      if (out < dst_cap) dst[out] = src[i];
      out++;
    }
  }
  if (out < dst_cap) dst[out] = '\0';
  return out;
}

int csupport_yaml_is_block_scalar_header(char c) {
  return c == '|' || c == '>';
}

size_t csupport_yaml_unescape_double_quoted(const char *src, size_t src_len,
                                             char *dst, size_t dst_cap) {
  size_t out = 0;
  for (size_t i = 0; i < src_len; i++) {
    if (src[i] == '\\' && i + 1 < src_len) {
      i++;
      char c;
      switch (src[i]) {
      case '0':  c = '\0'; break;
      case 'a':  c = '\a'; break;
      case 'b':  c = '\b'; break;
      case 't': case '\t': c = '\t'; break;
      case 'n':  c = '\n'; break;
      case 'v':  c = '\v'; break;
      case 'f':  c = '\f'; break;
      case 'r':  c = '\r'; break;
      case 'e':  c = '\x1b'; break;
      case ' ':  c = ' '; break;
      case '"':  c = '"'; break;
      case '/':  c = '/'; break;
      case '\\': c = '\\'; break;
      case '\r':
        if (i + 1 < src_len && src[i+1] == '\n') i++;
        __attribute__((fallthrough));
      case '\n':
        while (i + 1 < src_len && (src[i+1] == ' ' || src[i+1] == '\t')) i++;
        continue;
      case 'N':  /* NEL U+0085 */
        if (out + 2 <= dst_cap) { dst[out] = (char)0xC2; dst[out+1] = (char)0x85; }
        out += 2; continue;
      case '_':  /* NBSP U+00A0 */
        if (out + 2 <= dst_cap) { dst[out] = (char)0xC2; dst[out+1] = (char)0xA0; }
        out += 2; continue;
      case 'L':  /* LS U+2028 */
        if (out + 3 <= dst_cap) { dst[out] = (char)0xE2; dst[out+1] = (char)0x80; dst[out+2] = (char)0xA8; }
        out += 3; continue;
      case 'P':  /* PS U+2029 */
        if (out + 3 <= dst_cap) { dst[out] = (char)0xE2; dst[out+1] = (char)0x80; dst[out+2] = (char)0xA9; }
        out += 3; continue;
      case 'x':
        if (i + 2 < src_len) {
          uint32_t cp_val;
          if (!csupport_yaml_decode_hex_escape(src + i + 1, 2, &cp_val))
            cp_val = 0xFFFD;
          char utf8[4];
          int ulen = yaml_encode_utf8(cp_val, utf8, sizeof(utf8));
          for (int u = 0; u < ulen; u++) { if (out < dst_cap) dst[out] = utf8[u]; out++; }
          i += 2;
        }
        continue;
      case 'u':
        if (i + 4 < src_len) {
          uint32_t cp_val;
          if (!csupport_yaml_decode_hex_escape(src + i + 1, 4, &cp_val))
            cp_val = 0xFFFD;
          char utf8[4];
          int ulen = yaml_encode_utf8(cp_val, utf8, sizeof(utf8));
          for (int u = 0; u < ulen; u++) { if (out < dst_cap) dst[out] = utf8[u]; out++; }
          i += 4;
        }
        continue;
      case 'U':
        if (i + 8 < src_len) {
          uint32_t cp_val;
          if (!csupport_yaml_decode_hex_escape(src + i + 1, 8, &cp_val))
            cp_val = 0xFFFD;
          char utf8[4];
          int ulen = yaml_encode_utf8(cp_val, utf8, sizeof(utf8));
          for (int u = 0; u < ulen; u++) { if (out < dst_cap) dst[out] = utf8[u]; out++; }
          i += 8;
        }
        continue;
      default: c = src[i]; break;
      }
      if (out < dst_cap) dst[out] = c;
      out++;
    } else {
      if (out < dst_cap) dst[out] = src[i];
      out++;
    }
  }
  if (out < dst_cap) dst[out] = '\0';
  return out;
}

/* csupport_yaml_is_flow_indicator already defined above */

int csupport_yaml_detect_value_type(const char *p, size_t len) {
  if (len == 0) return 0;
  if (len == 4 && (memcmp(p, "true", 4) == 0 || memcmp(p, "True", 4) == 0 ||
                   memcmp(p, "TRUE", 4) == 0)) return 1; /* bool true */
  if (len == 5 && (memcmp(p, "false", 5) == 0 || memcmp(p, "False", 5) == 0 ||
                   memcmp(p, "FALSE", 5) == 0)) return 2; /* bool false */
  if (len == 4 && (memcmp(p, "null", 4) == 0 || memcmp(p, "Null", 4) == 0 ||
                   memcmp(p, "NULL", 4) == 0)) return 3; /* null */
  if (len == 1 && p[0] == '~') return 3; /* null */
  /* check integer */
  size_t start = 0;
  if (p[0] == '-' || p[0] == '+') start = 1;
  if (start < len) {
    int all_digits = 1;
    for (size_t i = start; i < len; i++) {
      if (p[i] < '0' || p[i] > '9') { all_digits = 0; break; }
    }
    if (all_digits && len - start > 0) return 4; /* integer */
  }
  return 0; /* unknown / string */
}

size_t csupport_yaml_write_key_value(const char *key, size_t key_len,
                                      const char *val, size_t val_len,
                                      int indent,
                                      char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  for (int i = 0; i < indent && pos + 1 < out_cap; i++) out[pos++] = ' ';
  for (size_t i = 0; i < key_len && pos < out_cap - 1; i++) out[pos++] = key[i];
  if (pos < out_cap - 1) out[pos++] = ':';
  if (pos < out_cap - 1) out[pos++] = ' ';
  for (size_t i = 0; i < val_len && pos < out_cap - 1; i++) out[pos++] = val[i];
  if (pos < out_cap - 1) out[pos++] = '\n';
  out[pos] = '\0';
  return pos;
}

size_t csupport_yaml_render_block_scalar(const char *text, size_t text_len,
                                         int indent, int style,
                                         char *out, size_t out_cap) {
  if (!text || !out || out_cap == 0) return 0;
  size_t pos = 0;
  if (pos < out_cap - 1) out[pos++] = (style == 1) ? '|' : '>';
  if (pos < out_cap - 1) out[pos++] = '\n';
  size_t line_start = 0;
  for (size_t i = 0; i <= text_len; i++) {
    if (i == text_len || text[i] == '\n') {
      for (int j = 0; j < indent && pos < out_cap - 1; j++)
        out[pos++] = ' ';
      for (size_t k = line_start; k < i && pos < out_cap - 1; k++)
        out[pos++] = text[k];
      if (pos < out_cap - 1) out[pos++] = '\n';
      line_start = i + 1;
    }
  }
  out[pos] = '\0';
  return pos;
}

int csupport_yaml_scan_plain_scalar(const char *buf, size_t len,
                                    int flow_level,
                                    size_t *out_end) {
  if (!buf || len == 0) { if (out_end) *out_end = 0; return 0; }
  size_t i = 0;
  if (buf[i] == '-' || buf[i] == '?' || buf[i] == ':') {
    if (i + 1 >= len || buf[i + 1] == ' ' || buf[i + 1] == '\n' ||
        buf[i + 1] == '\r' || buf[i + 1] == '\t')
      { if (out_end) *out_end = 0; return 0; }
  }
  while (i < len) {
    char c = buf[i];
    if (c == '\n' || c == '\r') break;
    if (c == '#' && i > 0 && (buf[i-1] == ' ' || buf[i-1] == '\t')) break;
    if (c == ':' && (i + 1 >= len || buf[i+1] == ' ' || buf[i+1] == '\n' ||
                     buf[i+1] == '\r' || buf[i+1] == '\t')) break;
    if (flow_level > 0 && (c == ',' || c == '[' || c == ']' || c == '{' || c == '}')) break;
    i++;
  }
  while (i > 0 && (buf[i-1] == ' ' || buf[i-1] == '\t')) i--;
  if (out_end) *out_end = i;
  return i > 0 ? 1 : 0;
}

size_t csupport_yaml_emit_mapping(const char * const *keys, const char * const *values,
                                  size_t count, int indent,
                                  char *out, size_t out_cap) {
  if (!keys || !values || !out || out_cap == 0) return 0;
  size_t pos = 0;
  for (size_t idx = 0; idx < count; idx++) {
    for (int j = 0; j < indent && pos < out_cap - 1; j++)
      out[pos++] = ' ';
    const char *k = keys[idx];
    if (k) { while (*k && pos < out_cap - 1) out[pos++] = *k++; }
    if (pos < out_cap - 1) out[pos++] = ':';
    if (pos < out_cap - 1) out[pos++] = ' ';
    const char *v = values[idx];
    if (v) { while (*v && pos < out_cap - 1) out[pos++] = *v++; }
    if (pos < out_cap - 1) out[pos++] = '\n';
  }
  out[pos] = '\0';
  return pos;
}

size_t csupport_yaml_count_lines(const char *text, size_t len) {
  if (!text || len == 0) return 0;
  size_t count = 1;
  for (size_t i = 0; i < len; i++) {
    if (text[i] == '\n') count++;
    else if (text[i] == '\r') {
      count++;
      if (i + 1 < len && text[i + 1] == '\n') i++;
    }
  }
  return count;
}

size_t csupport_yaml_write_scalar(const char *str, size_t len,
                                   int force_quote,
                                   char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  int quoting = csupport_yaml_needs_quoting(str, len);
  if (force_quote && quoting < 1) quoting = 1;
  size_t pos = 0;

  if (quoting == 0) {
    size_t n = len < out_cap - 1 ? len : out_cap - 1;
    memcpy(out, str, n);
    out[n] = '\0';
    return n;
  }

  if (quoting >= 2) {
    if (pos < out_cap - 1) out[pos++] = '"';
    size_t escaped_len = csupport_yaml_escape(str, len, 0,
                                               out + pos, out_cap - pos);
    pos += escaped_len;
    if (pos < out_cap - 1) out[pos++] = '"';
    out[pos] = '\0';
    return pos;
  }

  if (pos < out_cap - 1) out[pos++] = '\'';
  for (size_t i = 0; i < len && pos < out_cap - 1; i++) {
    if (str[i] == '\'') {
      if (pos < out_cap - 1) out[pos++] = '\'';
      if (pos < out_cap - 1) out[pos++] = '\'';
    } else {
      out[pos++] = str[i];
    }
  }
  if (pos < out_cap - 1) out[pos++] = '\'';
  out[pos] = '\0';
  return pos;
}

size_t csupport_yaml_trim_trailing(const char *str, size_t len) {
  while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
                     str[len - 1] == '\r' || str[len - 1] == '\n'))
    len--;
  return len;
}

size_t csupport_yaml_format_block_literal(const char *str, size_t len,
                                            int indent, char *buf, size_t cap) {
  size_t pos = 0;
  if (pos < cap) buf[pos++] = '|';
  if (pos < cap) buf[pos++] = '\n';
  size_t line_start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i == len || str[i] == '\n') {
      for (int j = 0; j < indent && pos < cap; j++) buf[pos++] = ' ';
      size_t line_len = i - line_start;
      if (pos + line_len < cap) {
        memcpy(buf + pos, str + line_start, line_len);
        pos += line_len;
      }
      if (pos < cap) buf[pos++] = '\n';
      line_start = i + 1;
    }
  }
  if (pos < cap) buf[pos] = '\0';
  return pos;
}

size_t csupport_yaml_format_flow_sequence(const char **items, size_t count,
                                            char *buf, size_t cap) {
  size_t pos = 0;
  if (pos < cap) buf[pos++] = '[';
  for (size_t i = 0; i < count; i++) {
    if (i > 0 && pos + 2 < cap) { buf[pos++] = ','; buf[pos++] = ' '; }
    if (items[i]) {
      size_t slen = strlen(items[i]);
      if (pos + slen < cap) { memcpy(buf + pos, items[i], slen); pos += slen; }
    }
  }
  if (pos < cap) buf[pos++] = ']';
  if (pos < cap) buf[pos] = '\0';
  return pos;
}
