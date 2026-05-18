/*===- UnicodeNameToCodepoint.c - Unicode name lookup (pure C) -*- C -*-===*/
#include "include/csupport/lunicode_lname_lto_lcodepoint.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int csupport_unicode_is_valid_name_char(char c) {
  return isalnum((unsigned char)c) || c == ' ' || c == '-';
}

int csupport_unicode_normalize_name(const char *name, size_t name_len,
                                    char *out, size_t out_size,
                                    size_t *out_len) {
  size_t pos = 0;
  for (size_t i = 0; i < name_len; i++) {
    char c = name[i];
    if (c == ' ' || c == '_' || c == '-')
      continue;
    if (c >= 'a' && c <= 'z')
      c = (char)(c - 'a' + 'A');
    if (pos >= out_size - 1) return 0;
    out[pos++] = c;
  }
  out[pos] = '\0';
  *out_len = pos;
  return 1;
}

static int is_alnum_c(char c) { return isalnum((unsigned char)c); }
static char to_upper_c(char c) { return (char)toupper((unsigned char)c); }

static void skip_ignorable(const char *s, size_t len, size_t *pos,
                           char *prev, int is_prefix) {
  while (*pos < len) {
    char c = s[*pos];
    size_t next = *pos + 1;
    int ignore = (c == ' ' || c == '_' ||
                  (c == '-' && is_alnum_c(*prev) &&
                   ((next < len && is_alnum_c(s[next])) ||
                    (next == len && is_prefix))));
    *prev = c;
    if (!ignore) break;
    (*pos)++;
  }
}

size_t csupport_unicode_starts_with(const char *name, size_t name_len,
                                     const char *needle, size_t needle_len,
                                     int strict, char *prev_char_in_name,
                                     int is_prefix) {
  if (strict) {
    if (name_len < needle_len) return 0;
    if (memcmp(name, needle, needle_len) != 0) return 0;
    return needle_len;
  }
  if (needle_len == 0) return 0;

  size_t np = 0, ndp = 0;
  char prev_name_orig = *prev_char_in_name;
  char prev_needle = needle[0];

  for (;;) {
    skip_ignorable(name, name_len, &np, prev_char_in_name, 0);
    skip_ignorable(needle, needle_len, &ndp, &prev_needle, is_prefix);
    if (ndp >= needle_len) break;
    if (np >= name_len) break;
    if (to_upper_c(name[np]) != to_upper_c(needle[ndp])) break;
    np++;
    ndp++;
  }
  if (ndp < needle_len) {
    *prev_char_in_name = prev_name_orig;
    return 0;
  }
  return np;
}

csupport_unicode_node_t csupport_unicode_read_node(
    const uint8_t *index, size_t index_size,
    const char *dict, uint32_t offset) {
  csupport_unicode_node_t N = {0, 0xFFFFFFFF, 0, 0, 0, NULL, 0};
  if (offset == 0) {
    N.is_root = 1;
    N.children_offset = 1;
    N.size = 1;
    return N;
  }
  uint32_t origin = offset;
  if (offset >= index_size) return N;
  uint8_t name_info = index[offset++];
  if (offset + 6 >= index_size) return N;

  int long_name = name_info & 0x40;
  int has_value = name_info & 0x80;
  size_t name_size = name_info & ~0xC0u;

  if (long_name) {
    uint32_t dict_offset = ((uint32_t)index[offset++] << 8);
    dict_offset |= index[offset++];
    N.name_data = dict + dict_offset;
    N.name_len = name_size;
  } else {
    N.name_data = dict + name_size;
    N.name_len = 1;
  }

  if (has_value) {
    uint8_t H = index[offset++];
    uint8_t M = index[offset++];
    uint8_t L = index[offset++];
    N.value = ((uint32_t)H << 16 | (uint32_t)M << 8 | (uint32_t)L) >> 3;
    int has_children = L & 0x02;
    N.has_sibling = L & 0x01;
    if (has_children) {
      N.children_offset = (uint32_t)index[offset++] << 16;
      N.children_offset |= (uint32_t)index[offset++] << 8;
      N.children_offset |= index[offset++];
    }
  } else {
    uint8_t H = index[offset++];
    N.has_sibling = H & 0x80;
    int has_children = H & 0x40;
    H &= (uint8_t)~0xC0u;
    if (has_children) {
      N.children_offset = ((uint32_t)H << 16);
      N.children_offset |= ((uint32_t)index[offset++] << 8);
      N.children_offset |= index[offset++];
    }
  }
  N.size = offset - origin;
  return N;
}

/*--- Hangul syllable table + constants ---*/
static const char * const hangul_syllables[][3] = {
  {"G","A",""},{"GG","AE","G"},{"N","YA","GG"},{"D","YAE","GS"},
  {"DD","EO","N"},{"R","E","NJ"},{"M","YEO","NH"},{"B","YE","D"},
  {"BB","O","L"},{"S","WA","LG"},{"SS","WAE","LM"},{NULL,"OE","LB"},
  {"J","YO","LS"},{"JJ","U","LT"},{"C","WEO","LP"},{"K","WE","LH"},
  {"T","WI","M"},{"P","YU","B"},{"H","EU","BS"},{NULL,"YI","S"},
  {NULL,"I","SS"},{NULL,NULL,"NG"},{NULL,NULL,"J"},{NULL,NULL,"C"},
  {NULL,NULL,"K"},{NULL,NULL,"T"},{NULL,NULL,"P"},{NULL,NULL,"H"}
};
#define SBASE  0xAC00u
#define LCOUNT 19u
#define VCOUNT 21u
#define TCOUNT 28u

static size_t find_syllable(const char *name, size_t name_len, int strict,
                            char *prev, int *pos, int column) {
  static const size_t cnt[] = {LCOUNT, VCOUNT, TCOUNT};
  int best_len = -1;
  int best_pos = -1;
  char best_prev = *prev;
  for (size_t i = 0; i < cnt[column]; i++) {
    const char *syl = hangul_syllables[i][column];
    if (!syl) continue;
    size_t syl_len = strlen(syl);
    if ((int)syl_len <= best_len) continue;
    char pv = *prev;
    size_t consumed = csupport_unicode_starts_with(name, name_len,
                                                    syl, syl_len,
                                                    strict, &pv, 0);
    if (consumed == 0) continue;
    best_len = (int)consumed;
    best_pos = (int)i;
    best_prev = pv;
  }
  if (best_len < 0) return 0;
  *prev = best_prev;
  *pos = best_pos;
  return (size_t)best_len;
}

uint32_t csupport_unicode_name_to_hangul(
    const char *name, size_t name_len, int strict,
    char *buf, size_t buf_size, size_t *buf_len) {
  *buf_len = 0;
  char prev = 0;
  size_t consumed = csupport_unicode_starts_with(
      name, name_len, "HANGUL SYLLABLE ", 16, strict, &prev, 0);
  if (consumed == 0) return UINT32_MAX;
  const char *rest = name + consumed;
  size_t rest_len = name_len - consumed;
  int L = -1, V = -1, T = -1;
  size_t c;
  c = find_syllable(rest, rest_len, strict, &prev, &L, 0);
  rest += c; rest_len -= c;
  c = find_syllable(rest, rest_len, strict, &prev, &V, 1);
  rest += c; rest_len -= c;
  c = find_syllable(rest, rest_len, strict, &prev, &T, 2);
  rest += c; rest_len -= c;
  if (L != -1 && V != -1 && T != -1 && rest_len == 0) {
    if (!strict && buf_size > 0) {
      size_t pos = 0;
      const char *pfx = "HANGUL SYLLABLE ";
      size_t plen = 16;
      if (pos + plen < buf_size) { memcpy(buf+pos, pfx, plen); pos += plen; }
      if (hangul_syllables[L][0]) {
        size_t sl = strlen(hangul_syllables[L][0]);
        if (pos+sl < buf_size) { memcpy(buf+pos, hangul_syllables[L][0], sl); pos += sl; }
      }
      if (hangul_syllables[V][1]) {
        size_t sl = strlen(hangul_syllables[V][1]);
        if (pos+sl < buf_size) { memcpy(buf+pos, hangul_syllables[V][1], sl); pos += sl; }
      }
      if (hangul_syllables[T][2]) {
        size_t sl = strlen(hangul_syllables[T][2]);
        if (pos+sl < buf_size) { memcpy(buf+pos, hangul_syllables[T][2], sl); pos += sl; }
      }
      if (pos < buf_size) buf[pos] = '\0';
      *buf_len = pos;
    }
    return SBASE + ((uint32_t)L * VCOUNT + (uint32_t)V) * TCOUNT + (uint32_t)T;
  }
  return UINT32_MAX;
}

/*--- Generated names (CJK UNIFIED IDEOGRAPH-, etc) ---*/
static const struct { const char *prefix; uint32_t start; uint32_t end; }
generated_names[] = {
  {"CJK UNIFIED IDEOGRAPH-",0x3400,0x4DBF},
  {"CJK UNIFIED IDEOGRAPH-",0x4E00,0x9FFF},
  {"CJK UNIFIED IDEOGRAPH-",0x20000,0x2A6DF},
  {"CJK UNIFIED IDEOGRAPH-",0x2A700,0x2B739},
  {"CJK UNIFIED IDEOGRAPH-",0x2B740,0x2B81D},
  {"CJK UNIFIED IDEOGRAPH-",0x2B820,0x2CEA1},
  {"CJK UNIFIED IDEOGRAPH-",0x2CEB0,0x2EBE0},
  {"CJK UNIFIED IDEOGRAPH-",0x30000,0x3134A},
  {"CJK UNIFIED IDEOGRAPH-",0x31350,0x323AF},
  {"TANGUT IDEOGRAPH-",0x17000,0x187F7},
  {"TANGUT IDEOGRAPH-",0x18D00,0x18D08},
  {"KHITAN SMALL SCRIPT CHARACTER-",0x18B00,0x18CD5},
  {"NUSHU CHARACTER-",0x1B170,0x1B2FB},
  {"CJK COMPATIBILITY IDEOGRAPH-",0xF900,0xFA6D},
  {"CJK COMPATIBILITY IDEOGRAPH-",0xFA70,0xFAD9},
  {"CJK COMPATIBILITY IDEOGRAPH-",0x2F800,0x2FA1D},
};
#define NUM_GENERATED (sizeof(generated_names)/sizeof(generated_names[0]))

uint32_t csupport_unicode_name_to_generated(
    const char *name, size_t name_len, int strict,
    char *buf, size_t buf_size, size_t *buf_len) {
  *buf_len = 0;
  for (size_t i = 0; i < NUM_GENERATED; i++) {
    char prev = 0;
    size_t plen = strlen(generated_names[i].prefix);
    size_t consumed = csupport_unicode_starts_with(
        name, name_len, generated_names[i].prefix, plen,
        strict, &prev, 1);
    if (consumed == 0) continue;
    const char *hex_part = name + consumed;
    size_t hex_len = name_len - consumed;
    if (hex_len == 0) continue;
    if (strict) {
      int has_lower = 0;
      for (size_t j = 0; j < hex_len; j++)
        if (hex_part[j] >= 'a' && hex_part[j] <= 'f') { has_lower = 1; break; }
      if (has_lower) return UINT32_MAX;
    }
    char *endp;
    unsigned long long V = strtoull(hex_part, &endp, 16);
    if (endp != hex_part + hex_len) continue;
    if (V < generated_names[i].start || V > generated_names[i].end) continue;
    if (!strict && buf_size > 0) {
      size_t pos = 0;
      if (pos + plen < buf_size) { memcpy(buf+pos, generated_names[i].prefix, plen); pos += plen; }
      char hbuf[16];
      int hlen = snprintf(hbuf, sizeof(hbuf), "%llX", V);
      if (hlen > 0 && pos + (size_t)hlen < buf_size) { memcpy(buf+pos, hbuf, (size_t)hlen); pos += (size_t)hlen; }
      if (pos < buf_size) buf[pos] = '\0';
      *buf_len = pos;
    }
    return (uint32_t)V;
  }
  return UINT32_MAX;
}

uint32_t csupport_unicode_compare_node(
    const uint8_t *index, size_t index_size, const char *dict,
    uint32_t offset,
    const char *name, size_t name_len, int strict,
    char prev_char_in_name,
    char *rev_buf, size_t rev_buf_size, size_t *rev_buf_len) {
  csupport_unicode_node_t N = csupport_unicode_read_node(index, index_size, dict, offset);
  size_t consumed = 0;
  int matches;
  if (N.is_root) {
    matches = 1;
  } else {
    consumed = csupport_unicode_starts_with(
        name, name_len, N.name_data, N.name_len,
        strict, &prev_char_in_name, 0);
    matches = (consumed > 0 || N.name_len == 0);
  }
  if (!matches) {
    *rev_buf_len = 0;
    return UINT32_MAX;
  }
  if (name_len - consumed == 0 && N.value != 0xFFFFFFFF) {
    *rev_buf_len = 0;
    return N.value;
  }
  int has_children = (N.children_offset != 0) || N.is_root;
  if (has_children) {
    uint32_t child_off = N.children_offset;
    for (;;) {
      char child_rev[256];
      size_t child_rev_len = 0;
      uint32_t result = csupport_unicode_compare_node(
          index, index_size, dict, child_off,
          name + consumed, name_len - consumed, strict,
          prev_char_in_name,
          child_rev, sizeof(child_rev), &child_rev_len);
      if (result != UINT32_MAX) {
        csupport_unicode_node_t child = csupport_unicode_read_node(index, index_size, dict, child_off);
        size_t pos = 0;
        if (child_rev_len > 0 && pos + child_rev_len <= rev_buf_size) {
          memcpy(rev_buf + pos, child_rev, child_rev_len);
          pos += child_rev_len;
        }
        for (size_t i = child.name_len; i > 0; i--) {
          if (pos < rev_buf_size) rev_buf[pos++] = child.name_data[i-1];
        }
        *rev_buf_len = pos;
        return result;
      }
      csupport_unicode_node_t cn = csupport_unicode_read_node(index, index_size, dict, child_off);
      child_off += cn.size;
      if (!cn.has_sibling) break;
    }
  }
  *rev_buf_len = 0;
  return UINT32_MAX;
}
