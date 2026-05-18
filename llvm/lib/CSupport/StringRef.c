/*===- StringRef.c - String operations (pure C) -----------------*- C -*-===*/
#include "include/csupport/lstring_lref.h"
#include "include/csupport/stringref.h"
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>

static int ascii_strncasecmp_c(const char *lhs, const char *rhs, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    unsigned char l = (unsigned char)tolower((unsigned char)lhs[i]);
    unsigned char r = (unsigned char)tolower((unsigned char)rhs[i]);
    if (l != r) return l < r ? -1 : 1;
  }
  return 0;
}

int csupport_str_compare_insensitive(const char *a, size_t a_len,
                                     const char *b, size_t b_len) {
  size_t min_len = a_len < b_len ? a_len : b_len;
  int res = ascii_strncasecmp_c(a, b, min_len);
  if (res) return res;
  if (a_len == b_len) return 0;
  return a_len < b_len ? -1 : 1;
}

int csupport_str_compare_numeric(const char *a, size_t a_len,
                                 const char *b, size_t b_len) {
  size_t min_len = a_len < b_len ? a_len : b_len;
  for (size_t i = 0; i < min_len; ++i) {
    if (a[i] >= '0' && a[i] <= '9' && b[i] >= '0' && b[i] <= '9') {
      size_t j;
      for (j = i + 1; j <= min_len; ++j) {
        int ld = (j < a_len && a[j] >= '0' && a[j] <= '9');
        int rd = (j < b_len && b[j] >= '0' && b[j] <= '9');
        if (ld != rd) return rd ? -1 : 1;
        if (!rd) break;
      }
      int cmp = memcmp(a + i, b + i, j - i);
      if (cmp) return cmp < 0 ? -1 : 1;
      i = j - 1;
      continue;
    }
    if (a[i] != b[i])
      return (unsigned char)a[i] < (unsigned char)b[i] ? -1 : 1;
  }
  if (a_len == b_len) return 0;
  return a_len < b_len ? -1 : 1;
}

size_t csupport_str_find_substr(const char *haystack, size_t hay_len,
                                const char *needle, size_t nee_len,
                                size_t from) {
  if (from > hay_len) return (size_t)-1;
  const char *start = haystack + from;
  size_t remaining = hay_len - from;
  if (nee_len == 0) return from;
  if (remaining < nee_len) return (size_t)-1;

  if (nee_len == 1) {
    const char *p = (const char *)memchr(start, needle[0], remaining);
    return p ? (size_t)(p - haystack) : (size_t)-1;
  }

  const char *stop = start + (remaining - nee_len + 1);
  if (remaining < 16 || nee_len > 255) {
    for (; start < stop; ++start)
      if (memcmp(start, needle, nee_len) == 0)
        return (size_t)(start - haystack);
    return (size_t)-1;
  }

  uint8_t skip[256];
  memset(skip, (int)nee_len, 256);
  for (size_t i = 0; i + 1 < nee_len; ++i)
    skip[(uint8_t)needle[i]] = (uint8_t)(nee_len - 1 - i);

  while (start < stop) {
    uint8_t last = (uint8_t)start[nee_len - 1];
    if (last == (uint8_t)needle[nee_len - 1])
      if (memcmp(start, needle, nee_len - 1) == 0)
        return (size_t)(start - haystack);
    start += skip[last];
  }
  return (size_t)-1;
}

size_t csupport_str_rfind(const char *data, size_t len,
                          const char *needle, size_t nee_len) {
  if (nee_len > len) return (size_t)-1;
  for (size_t i = len - nee_len + 1; i > 0; --i) {
    if (memcmp(data + i - 1, needle, nee_len) == 0)
      return i - 1;
  }
  return (size_t)-1;
}

size_t csupport_str_find_first_of(const char *data, size_t len,
                                  const char *chars, size_t chars_len,
                                  size_t from) {
  uint8_t bits[256 / 8];
  memset(bits, 0, sizeof(bits));
  for (size_t i = 0; i < chars_len; i++)
    bits[(uint8_t)chars[i] / 8] |= (uint8_t)(1 << ((uint8_t)chars[i] % 8));
  if (from > len) from = len;
  for (size_t i = from; i < len; i++)
    if (bits[(uint8_t)data[i] / 8] & (1 << ((uint8_t)data[i] % 8)))
      return i;
  return (size_t)-1;
}

size_t csupport_str_find_first_not_of(const char *data, size_t len,
                                      const char *chars, size_t chars_len,
                                      size_t from) {
  uint8_t bits[256 / 8];
  memset(bits, 0, sizeof(bits));
  for (size_t i = 0; i < chars_len; i++)
    bits[(uint8_t)chars[i] / 8] |= (uint8_t)(1 << ((uint8_t)chars[i] % 8));
  if (from > len) from = len;
  for (size_t i = from; i < len; i++)
    if (!(bits[(uint8_t)data[i] / 8] & (1 << ((uint8_t)data[i] % 8))))
      return i;
  return (size_t)-1;
}

size_t csupport_str_count(const char *data, size_t len,
                          const char *needle, size_t nee_len) {
  if (!nee_len) return 0;
  size_t count = 0, pos = 0;
  while ((pos = csupport_str_find_substr(data, len, needle, nee_len, pos)) !=
         (size_t)-1) {
    ++count;
    pos += nee_len;
  }
  return count;
}

static unsigned get_auto_sense_radix(const char **str, size_t *len) {
  if (*len == 0) return 10;
  if (*len >= 2 && ((*str)[0] == '0' && ((*str)[1] == 'x' || (*str)[1] == 'X'))) {
    *str += 2; *len -= 2; return 16;
  }
  if (*len >= 2 && ((*str)[0] == '0' && ((*str)[1] == 'b' || (*str)[1] == 'B'))) {
    *str += 2; *len -= 2; return 2;
  }
  if (*len >= 2 && (*str)[0] == '0' && (*str)[1] == 'o') {
    *str += 2; *len -= 2; return 8;
  }
  if ((*str)[0] == '0' && *len > 1 && (*str)[1] >= '0' && (*str)[1] <= '9') {
    *str += 1; *len -= 1; return 8;
  }
  return 10;
}

int csupport_consume_unsigned(const char **str, size_t *len, unsigned radix,
                              unsigned long long *result) {
  const char *s = *str;
  size_t slen = *len;
  if (radix == 0) radix = get_auto_sense_radix(&s, &slen);
  if (slen == 0) return 1;

  const char *start = s;
  *result = 0;
  while (slen > 0) {
    unsigned char_val;
    if (*s >= '0' && *s <= '9') char_val = (unsigned)(*s - '0');
    else if (*s >= 'a' && *s <= 'z') char_val = (unsigned)(*s - 'a' + 10);
    else if (*s >= 'A' && *s <= 'Z') char_val = (unsigned)(*s - 'A' + 10);
    else break;
    if (char_val >= radix) break;
    unsigned long long prev = *result;
    *result = *result * radix + char_val;
    if (*result / radix < prev) return 1;
    s++; slen--;
  }
  if (s == start) return 1;
  *str = s; *len = slen;
  return 0;
}

size_t csupport_str_find_char_insensitive(const char *data, size_t len,
                                          char c, size_t from) {
  char lo = (char)tolower((unsigned char)c);
  for (size_t i = from; i < len; ++i)
    if (tolower((unsigned char)data[i]) == lo)
      return i;
  return (size_t)-1;
}

size_t csupport_str_find_insensitive(const char *data, size_t len,
                                     const char *needle, size_t nee_len,
                                     size_t from) {
  if (nee_len == 0) return from <= len ? from : (size_t)-1;
  if (from + nee_len > len) return (size_t)-1;
  for (size_t i = from; i + nee_len <= len; ++i)
    if (ascii_strncasecmp_c(data + i, needle, nee_len) == 0)
      return i;
  return (size_t)-1;
}

size_t csupport_str_rfind_char_insensitive(const char *data, size_t len,
                                           char c, size_t from) {
  if (from > len) from = len;
  char lo = (char)tolower((unsigned char)c);
  for (size_t i = from; i > 0; --i)
    if (tolower((unsigned char)data[i - 1]) == lo)
      return i - 1;
  return (size_t)-1;
}

size_t csupport_str_rfind_insensitive(const char *data, size_t len,
                                      const char *needle, size_t nee_len) {
  if (nee_len > len) return (size_t)-1;
  for (size_t i = len - nee_len + 1; i > 0; --i)
    if (ascii_strncasecmp_c(data + i - 1, needle, nee_len) == 0)
      return i - 1;
  return (size_t)-1;
}

int csupport_str_starts_with_insensitive(const char *data, size_t len,
                                         const char *prefix, size_t plen) {
  return len >= plen && ascii_strncasecmp_c(data, prefix, plen) == 0;
}

int csupport_str_ends_with_insensitive(const char *data, size_t len,
                                       const char *suffix, size_t slen) {
  return len >= slen && ascii_strncasecmp_c(data + len - slen, suffix, slen) == 0;
}

size_t csupport_str_find_last_of(const char *data, size_t len,
                                 const char *chars, size_t chars_len,
                                 size_t from) {
  uint8_t bits[256 / 8];
  memset(bits, 0, sizeof(bits));
  for (size_t i = 0; i < chars_len; i++)
    bits[(uint8_t)chars[i] / 8] |= (uint8_t)(1 << ((uint8_t)chars[i] % 8));
  if (from > len) from = len;
  for (size_t i = from; i > 0; --i)
    if (bits[(uint8_t)data[i - 1] / 8] & (1 << ((uint8_t)data[i - 1] % 8)))
      return i - 1;
  return (size_t)-1;
}

size_t csupport_str_find_first_not_of_char(const char *data, size_t len,
                                           char c, size_t from) {
  for (size_t i = from; i < len; ++i)
    if (data[i] != c)
      return i;
  return (size_t)-1;
}

size_t csupport_str_find_last_not_of_char(const char *data, size_t len,
                                          char c, size_t from) {
  if (from > len) from = len;
  for (size_t i = from; i > 0; --i)
    if (data[i - 1] != c)
      return i - 1;
  return (size_t)-1;
}

size_t csupport_str_find_last_not_of(const char *data, size_t len,
                                     const char *chars, size_t chars_len,
                                     size_t from) {
  uint8_t bits[256 / 8];
  memset(bits, 0, sizeof(bits));
  for (size_t i = 0; i < chars_len; i++)
    bits[(uint8_t)chars[i] / 8] |= (uint8_t)(1 << ((uint8_t)chars[i] % 8));
  if (from > len) from = len;
  for (size_t i = from; i > 0; --i)
    if (!(bits[(uint8_t)data[i - 1] / 8] & (1 << ((uint8_t)data[i - 1] % 8))))
      return i - 1;
  return (size_t)-1;
}

int csupport_consume_signed(const char **str, size_t *len, unsigned radix,
                            long long *result) {
  unsigned long long ull;
  if (*len == 0 || **str != '-') {
    if (csupport_consume_unsigned(str, len, radix, &ull) ||
        (long long)ull < 0)
      return 1;
    *result = (long long)ull;
    return 0;
  }
  const char *s2 = *str + 1;
  size_t l2 = *len - 1;
  if (csupport_consume_unsigned(&s2, &l2, radix, &ull) ||
      (long long)(-ull) > 0)
    return 1;
  *str = s2; *len = l2;
  *result = (long long)(-ull);
  return 0;
}

/*--- Auto-sense radix from string prefix (0x=16, 0b=2, 0o=8, 0=8, else 10) ---*/
unsigned csupport_auto_sense_radix(const char **str, size_t *len) {
  if (*len == 0) return 10;
  const char *s = *str;
  size_t n = *len;

  if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    *str = s + 2; *len = n - 2; return 16;
  }
  if (n >= 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
    *str = s + 2; *len = n - 2; return 2;
  }
  if (n >= 2 && s[0] == '0' && s[1] == 'o') {
    *str = s + 2; *len = n - 2; return 8;
  }
  if (n > 1 && s[0] == '0' && s[1] >= '0' && s[1] <= '9') {
    *str = s + 1; *len = n - 1; return 8;
  }
  return 10;
}

/*--- Unicode encoding detection from BOM bytes ---*/
int csupport_detect_unicode_encoding(const unsigned char *data, size_t len,
                                      unsigned *bom_len) {
  if (len == 0) { *bom_len = 0; return 0; } /* UEF_Unknown */

  switch (data[0]) {
  case 0x00:
    if (len >= 4) {
      if (data[1] == 0 && data[2] == 0xFE && data[3] == 0xFF) {
        *bom_len = 4; return 4; /* UEF_UTF32_BE */
      }
      if (data[1] == 0 && data[2] == 0 && data[3] != 0) {
        *bom_len = 0; return 4; /* UEF_UTF32_BE, no BOM */
      }
    }
    if (len >= 2 && data[1] != 0) { *bom_len = 0; return 3; } /* UEF_UTF16_BE */
    *bom_len = 0; return 0;
  case 0xFF:
    if (len >= 4 && data[1] == 0xFE && data[2] == 0 && data[3] == 0) {
      *bom_len = 4; return 5; /* UEF_UTF32_LE */
    }
    if (len >= 2 && data[1] == 0xFE) { *bom_len = 2; return 2; } /* UEF_UTF16_LE */
    *bom_len = 0; return 0;
  case 0xFE:
    if (len >= 2 && data[1] == 0xFF) { *bom_len = 2; return 3; } /* UEF_UTF16_BE */
    *bom_len = 0; return 0;
  case 0xEF:
    if (len >= 3 && data[1] == 0xBB && data[2] == 0xBF) {
      *bom_len = 3; return 1; /* UEF_UTF8 */
    }
    *bom_len = 0; return 0;
  default:
    break;
  }

  if (len >= 4 && data[1] == 0 && data[2] == 0 && data[3] == 0) {
    *bom_len = 0; return 5; /* UEF_UTF32_LE */
  }
  if (len >= 2 && data[1] == 0) {
    *bom_len = 0; return 2; /* UEF_UTF16_LE */
  }
  *bom_len = 0; return 1; /* UEF_UTF8 */
}
