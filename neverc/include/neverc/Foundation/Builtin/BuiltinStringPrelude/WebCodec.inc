R"prelude(

#ifndef NEVERC_STRING_HTML_ESCAPE_MAX_PER_BYTE
#define NEVERC_STRING_HTML_ESCAPE_MAX_PER_BYTE ((__SIZE_TYPE__)5)
#endif

#ifndef NEVERC_STRING_JSON_ESCAPE_MAX_PER_BYTE
#define NEVERC_STRING_JSON_ESCAPE_MAX_PER_BYTE ((__SIZE_TYPE__)6)
#endif

#ifndef NEVERC_STRING_HTML_ENTITY_MAX_LEN
#define NEVERC_STRING_HTML_ENTITY_MAX_LEN ((__SIZE_TYPE__)16)
#endif

static __inline__ __SIZE_TYPE__ __neverc_html_replacement_len(char c) {
  switch (c) {
  case '&': return 5;
  case '<': return 4;
  case '>': return 4;
  case '"': return 6;
  case '\'': return 5;
  default: return 0;
  }
}

static string neverc_string_html_escape(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ total = 0;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    __SIZE_TYPE__ rep = __neverc_html_replacement_len(s.data[i]);
    __SIZE_TYPE__ need = rep ? rep : 1u;
    if (total > NEVERC_STRING_MAX_LEN - need)
      return __neverc_string_consume(s);
    total += need;
  }

  if (total == s.len)
    return __neverc_string_own_or_copy(s);

  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    char c = s.data[i];
    switch (c) {
    case '&':
      p[written++] = '&'; p[written++] = 'a'; p[written++] = 'm';
      p[written++] = 'p'; p[written++] = ';';
      break;
    case '<':
      p[written++] = '&'; p[written++] = 'l'; p[written++] = 't';
      p[written++] = ';';
      break;
    case '>':
      p[written++] = '&'; p[written++] = 'g'; p[written++] = 't';
      p[written++] = ';';
      break;
    case '"':
      p[written++] = '&'; p[written++] = 'q'; p[written++] = 'u';
      p[written++] = 'o'; p[written++] = 't'; p[written++] = ';';
      break;
    case '\'':
      p[written++] = '&'; p[written++] = '#'; p[written++] = '3';
      p[written++] = '9'; p[written++] = ';';
      break;
    default:
      p[written++] = c;
      break;
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
}

static __inline__ int __neverc_html_entity_match(const char *a, __SIZE_TYPE__ na,
                                                  const char *b,
                                                  __SIZE_TYPE__ nb) {
  if (na != nb)
    return 0;
  return __builtin_memcmp(a, b, na) == 0;
}

static __inline__ __UINT32_TYPE__
__neverc_html_named_entity_lookup(const char *body, __SIZE_TYPE__ n) {
  if (__neverc_html_entity_match(body, n, "amp", 3))
    return (__UINT32_TYPE__)'&';
  if (__neverc_html_entity_match(body, n, "lt", 2))
    return (__UINT32_TYPE__)'<';
  if (__neverc_html_entity_match(body, n, "gt", 2))
    return (__UINT32_TYPE__)'>';
  if (__neverc_html_entity_match(body, n, "quot", 4))
    return (__UINT32_TYPE__)'"';
  if (__neverc_html_entity_match(body, n, "apos", 4))
    return (__UINT32_TYPE__)'\'';
  if (__neverc_html_entity_match(body, n, "nbsp", 4))
    return 0x00A0u;
  if (__neverc_html_entity_match(body, n, "copy", 4))
    return 0x00A9u;
  if (__neverc_html_entity_match(body, n, "reg", 3))
    return 0x00AEu;

  return 0xFFFFFFFFu;
}

static string neverc_string_html_unescape(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);

  char *p = __neverc_string_alloc_payload(s.len);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ written = 0;
  __SIZE_TYPE__ i = 0;
  while (i < s.len) {
    if (s.data[i] != '&') {
      p[written++] = s.data[i++];
      continue;
    }

    __SIZE_TYPE__ end = i + 1;
    __SIZE_TYPE__ cap_end = i + 1 + NEVERC_STRING_HTML_ENTITY_MAX_LEN;
    if (cap_end > s.len)
      cap_end = s.len;
    while (end < cap_end && s.data[end] != ';')
      end++;
    if (end >= cap_end || end >= s.len || s.data[end] != ';') {

      p[written++] = '&';
      i++;
      continue;
    }

    const char *body = s.data + i + 1;
    __SIZE_TYPE__ body_len = end - (i + 1);
    if (body_len == 0) {

      p[written++] = '&';
      i++;
      continue;
    }

    __UINT32_TYPE__ cp = 0;
    int decoded = 0;
    if (body[0] == '#') {

      __SIZE_TYPE__ digit_start = 1;
      int hex = 0;
      if (body_len >= 2 && (body[1] == 'x' || body[1] == 'X')) {
        hex = 1;
        digit_start = 2;
      }
      if (digit_start < body_len) {
        __UINT32_TYPE__ acc = 0;
        int valid = 1;
        for (__SIZE_TYPE__ k = digit_start; k < body_len; k++) {
          unsigned d;
          if (hex) {
            d = __neverc_string_hex_digit_value(body[k]);
            if (d == 16u) { valid = 0; break; }
            acc = (acc << 4) | (__UINT32_TYPE__)d;
          } else {
            char ch = body[k];
            if (ch < '0' || ch > '9') { valid = 0; break; }
            acc = acc * 10u + (__UINT32_TYPE__)(ch - '0');
          }
          if (acc > 0x10FFFFu) { valid = 0; break; }
        }
        if (valid) {

          if (acc >= 0xD800u && acc <= 0xDFFFu)
            acc = NEVERC_STRING_UTF8_REPLACEMENT;
          cp = acc;
          decoded = 1;
        }
      }
    } else {
      __UINT32_TYPE__ named = __neverc_html_named_entity_lookup(body, body_len);
      if (named != 0xFFFFFFFFu) {
        cp = named;
        decoded = 1;
      }
    }

    if (decoded) {
      __SIZE_TYPE__ enc = __neverc_utf8_encode(p + written, cp);
      if (enc == 0)
        enc = __neverc_utf8_encode(p + written, NEVERC_STRING_UTF8_REPLACEMENT);
      written += enc;
      i = end + 1;
    } else {

      __SIZE_TYPE__ run = end - i + 1;
      __builtin_memcpy(p + written, s.data + i, run);
      written += run;
      i = end + 1;
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
}

static __inline__ __SIZE_TYPE__ __neverc_json_escape_len(unsigned char c) {
  switch (c) {
  case '"':
  case '\\':
  case '\b':
  case '\f':
  case '\n':
  case '\r':
  case '\t':
    return 2;
  default:
    if (c < 0x20u)
      return 6;
    return 0;
  }
}

static string neverc_string_json_escape(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ total = 0;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    __SIZE_TYPE__ esc = __neverc_json_escape_len((unsigned char)s.data[i]);
    __SIZE_TYPE__ need = esc ? esc : 1u;
    if (total > NEVERC_STRING_MAX_LEN - need)
      return __neverc_string_consume(s);
    total += need;
  }
  if (total == s.len)
    return __neverc_string_own_or_copy(s);

  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ written = 0;
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    unsigned char c = (unsigned char)s.data[i];
    switch (c) {
    case '"':  p[written++] = '\\'; p[written++] = '"';  break;
    case '\\': p[written++] = '\\'; p[written++] = '\\'; break;
    case '\b': p[written++] = '\\'; p[written++] = 'b';  break;
    case '\f': p[written++] = '\\'; p[written++] = 'f';  break;
    case '\n': p[written++] = '\\'; p[written++] = 'n';  break;
    case '\r': p[written++] = '\\'; p[written++] = 'r';  break;
    case '\t': p[written++] = '\\'; p[written++] = 't';  break;
    default:
      if (c < 0x20u) {
        p[written++] = '\\';
        p[written++] = 'u';
        p[written++] = '0';
        p[written++] = '0';
        p[written++] = __neverc_string_hex_upper_char(c >> 4);
        p[written++] = __neverc_string_hex_upper_char(c & 0xFu);
      } else {
        p[written++] = (char)c;
      }
      break;
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
}

static __inline__ int __neverc_json_hex4(const char *p, __UINT32_TYPE__ *out) {
  unsigned a = __neverc_string_hex_digit_value(p[0]);
  unsigned b = __neverc_string_hex_digit_value(p[1]);
  unsigned c = __neverc_string_hex_digit_value(p[2]);
  unsigned d = __neverc_string_hex_digit_value(p[3]);
  if (a == 16u || b == 16u || c == 16u || d == 16u)
    return 0;
  *out = (__UINT32_TYPE__)((a << 12) | (b << 8) | (c << 4) | d);
  return 1;
}

static string neverc_string_json_unescape(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);

  char *p = __neverc_string_alloc_payload(s.len);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ written = 0;
  __SIZE_TYPE__ i = 0;
  while (i < s.len) {
    char c = s.data[i];
    if (c != '\\') {
      p[written++] = c;
      i++;
      continue;
    }
    if (i + 1 >= s.len) {

      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }
    char esc = s.data[i + 1];
    switch (esc) {
    case '"':  p[written++] = '"';  i += 2; break;
    case '\\': p[written++] = '\\'; i += 2; break;
    case '/':  p[written++] = '/';  i += 2; break;
    case 'b':  p[written++] = '\b'; i += 2; break;
    case 'f':  p[written++] = '\f'; i += 2; break;
    case 'n':  p[written++] = '\n'; i += 2; break;
    case 'r':  p[written++] = '\r'; i += 2; break;
    case 't':  p[written++] = '\t'; i += 2; break;
    case 'u': {
      if (i + 6 > s.len) {
        NEVERC_STRING_FREE(p);
        return __neverc_string_consume(s);
      }
      __UINT32_TYPE__ cp;
      if (!__neverc_json_hex4(s.data + i + 2, &cp)) {
        NEVERC_STRING_FREE(p);
        return __neverc_string_consume(s);
      }
      i += 6;

      if (cp >= 0xD800u && cp <= 0xDBFFu) {
        if (i + 6 <= s.len && s.data[i] == '\\' && s.data[i + 1] == 'u') {
          __UINT32_TYPE__ lo;
          if (__neverc_json_hex4(s.data + i + 2, &lo) &&
              lo >= 0xDC00u && lo <= 0xDFFFu) {
            cp = 0x10000u + (((cp - 0xD800u) << 10) | (lo - 0xDC00u));
            i += 6;
          } else {
            cp = NEVERC_STRING_UTF8_REPLACEMENT;
          }
        } else {
          cp = NEVERC_STRING_UTF8_REPLACEMENT;
        }
      } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
        cp = NEVERC_STRING_UTF8_REPLACEMENT;
      }
      __SIZE_TYPE__ enc = __neverc_utf8_encode(p + written, cp);
      if (enc == 0)
        enc = __neverc_utf8_encode(p + written, NEVERC_STRING_UTF8_REPLACEMENT);
      written += enc;
      break;
    }
    default:

      NEVERC_STRING_FREE(p);
      return __neverc_string_consume(s);
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
}

static string neverc_string_csv_escape(string s) {
  if (__neverc_string_invalid(s) || s.len == 0)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ extras = 0;
  {
    const char *q = s.data;
    __SIZE_TYPE__ rem = s.len;
    while (rem) {
      const char *hit = (const char *)__builtin_memchr(q, '"', rem);
      if (!hit) break;
      extras++;
      __SIZE_TYPE__ skip = (__SIZE_TYPE__)(hit - q) + 1;
      q += skip; rem -= skip;
    }
  }
  if (s.len > NEVERC_STRING_MAX_LEN - extras)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ inner = s.len + extras;
  if (inner > NEVERC_STRING_MAX_LEN - 2u)
    return __neverc_string_consume(s);
  __SIZE_TYPE__ total = inner + 2u;

  char *p = __neverc_string_alloc_payload(total);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ written = 0;
  p[written++] = '"';
  for (__SIZE_TYPE__ i = 0; i < s.len; i++) {
    char c = s.data[i];
    if (c == '"') {
      p[written++] = '"';
      p[written++] = '"';
    } else {
      p[written++] = c;
    }
  }
  p[written++] = '"';
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
}

static string neverc_string_csv_unescape(string s) {
  if (__neverc_string_invalid(s))
    return __neverc_string_consume(s);

  if (s.len < 2u || s.data[0] != '"' || s.data[s.len - 1u] != '"')
    return __neverc_string_consume(s);

  __SIZE_TYPE__ inner = s.len - 2u;
  if (inner == 0)
    return __neverc_string_consume(s);
  char *p = __neverc_string_alloc_payload(inner);
  if (!p)
    return __neverc_string_consume(s);

  __SIZE_TYPE__ written = 0;
  __SIZE_TYPE__ i = 1;
  __SIZE_TYPE__ end = s.len - 1u;
  while (i < end) {
    char c = s.data[i];
    if (c == '"') {

      if (i + 1u >= end || s.data[i + 1u] != '"') {
        NEVERC_STRING_FREE(p);
        return __neverc_string_consume(s);
      }
      p[written++] = '"';
      i += 2u;
    } else {
      p[written++] = c;
      i++;
    }
  }
  neverc_string_free(s);
  return __neverc_string_finalize_owned(p, written);
})prelude"
