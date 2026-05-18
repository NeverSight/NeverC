/*===- GlobPattern.c - Glob matching (pure C) -------------------*- C -*-===*/
#include "include/csupport/lglob_lpattern.h"
#include <string.h>

static int match_char_class(const char *pat, size_t pat_len, char c) {
  int negate = 0;
  size_t i = 0;
  if (i < pat_len && (pat[i] == '!' || pat[i] == '^')) {
    negate = 1;
    i++;
  }
  int matched = 0;
  while (i < pat_len) {
    if (i + 2 < pat_len && pat[i + 1] == '-') {
      if ((unsigned char)c >= (unsigned char)pat[i] &&
          (unsigned char)c <= (unsigned char)pat[i + 2])
        matched = 1;
      i += 3;
    } else {
      if (c == pat[i])
        matched = 1;
      i++;
    }
  }
  return negate ? !matched : matched;
}

int csupport_glob_match(const char *pattern, size_t pat_len, const char *str,
                        size_t str_len) {
  size_t pi = 0, si = 0;
  size_t star_pi = (size_t)-1, star_si = 0;

  while (si < str_len) {
    if (pi < pat_len && pattern[pi] == '*') {
      star_pi = pi++;
      star_si = si;
    } else if (pi < pat_len && pattern[pi] == '?') {
      pi++;
      si++;
    } else if (pi < pat_len && pattern[pi] == '[') {
      size_t end = pi + 2;
      while (end < pat_len && pattern[end] != ']')
        end++;
      if (end >= pat_len) return 0;
      if (!match_char_class(pattern + pi + 1, end - pi - 1, str[si])) {
        if (star_pi == (size_t)-1) return 0;
        pi = star_pi + 1;
        si = ++star_si;
      } else {
        pi = end + 1;
        si++;
      }
    } else if (pi < pat_len && pattern[pi] == '\\') {
      pi++;
      if (pi >= pat_len || pattern[pi] != str[si]) {
        if (star_pi == (size_t)-1) return 0;
        pi = star_pi + 1;
        si = ++star_si;
      } else {
        pi++;
        si++;
      }
    } else if (pi < pat_len && pattern[pi] == str[si]) {
      pi++;
      si++;
    } else if (star_pi != (size_t)-1) {
      pi = star_pi + 1;
      si = ++star_si;
    } else {
      return 0;
    }
  }

  while (pi < pat_len && pattern[pi] == '*')
    pi++;
  return pi == pat_len;
}

int csupport_glob_expand_charset(const char *chars, size_t chars_len,
                                 uint8_t bitmap[256]) {
  memset(bitmap, 0, 256);
  size_t i = 0;
  int negate = 0;

  if (i < chars_len && (chars[i] == '!' || chars[i] == '^')) {
    negate = 1;
    i++;
  }

  while (i < chars_len) {
    if (i + 2 < chars_len && chars[i + 1] == '-') {
      unsigned char start = (unsigned char)chars[i];
      unsigned char end = (unsigned char)chars[i + 2];
      if (start > end) return -1;
      for (unsigned c = start; c <= end; c++)
        bitmap[c] = 1;
      i += 3;
    } else {
      bitmap[(unsigned char)chars[i]] = 1;
      i++;
    }
  }

  if (negate) {
    for (unsigned c = 0; c < 256; c++)
      bitmap[c] = !bitmap[c];
  }
  return 0;
}

int csupport_glob_match_bracket(const uint8_t bitmap[256], char c) {
  return bitmap[(unsigned char)c] != 0;
}

int csupport_glob_match_advanced(const char *pat, size_t pat_len,
                                 const uint8_t *bracket_bitmaps,
                                 const size_t *bracket_offsets,
                                 size_t bracket_count,
                                 const char *str, size_t str_len) {
  const char *P = pat, *SegmentBegin = NULL, *S = str, *SavedS = S;
  const char *const PEnd = P + pat_len, *const End = S + str_len;
  size_t B = 0, SavedB = 0;

  while (S != End) {
    if (P == PEnd)
      ;
    else if (*P == '*') {
      SegmentBegin = ++P;
      SavedS = S;
      SavedB = B;
      continue;
    } else if (*P == '[') {
      if (B < bracket_count &&
          bracket_bitmaps[B * 256 + (unsigned char)*S]) {
        P = pat + bracket_offsets[B++];
        ++S;
        continue;
      }
    } else if (*P == '\\') {
      ++P;
      if (P < PEnd && *P == *S) {
        ++P;
        ++S;
        continue;
      }
    } else if (*P == *S || *P == '?') {
      ++P;
      ++S;
      continue;
    }
    if (!SegmentBegin)
      return 0;
    P = SegmentBegin;
    S = ++SavedS;
    B = SavedB;
  }

  while (P < PEnd && *P == '*')
    ++P;
  return P == PEnd;
}

int csupport_glob_parse_brace_expansions(const char *pat, size_t pat_len,
                                          csupport_brace_expansion_t *out,
                                          size_t max_expansions,
                                          const char **error_msg) {
  size_t num = 0;
  csupport_brace_expansion_t *cur = 0;
  size_t term_begin = 0;

  for (size_t i = 0; i < pat_len; ++i) {
    if (pat[i] == '[') {
      size_t j = i + 2;
      while (j < pat_len && pat[j] != ']') ++j;
      if (j >= pat_len) {
        if (error_msg) *error_msg = "invalid glob pattern, unmatched '['";
        return -1;
      }
      i = j;
    } else if (pat[i] == '{') {
      if (cur) {
        if (error_msg) *error_msg = "nested brace expansions are not supported";
        return -1;
      }
      if (num >= max_expansions) {
        if (error_msg) *error_msg = "too many brace expansions";
        return -1;
      }
      cur = &out[num];
      cur->start = i;
      cur->num_terms = 0;
      term_begin = i + 1;
    } else if (pat[i] == ',') {
      if (!cur) continue;
      if (cur->num_terms < 64) {
        cur->term_offsets[cur->num_terms] = term_begin;
        cur->term_lengths[cur->num_terms] = i - term_begin;
        cur->num_terms++;
      }
      term_begin = i + 1;
    } else if (pat[i] == '}') {
      if (!cur) continue;
      if (cur->num_terms == 0) {
        if (error_msg)
          *error_msg = "empty or singleton brace expansions are not supported";
        return -1;
      }
      if (cur->num_terms < 64) {
        cur->term_offsets[cur->num_terms] = term_begin;
        cur->term_lengths[cur->num_terms] = i - term_begin;
        cur->num_terms++;
      }
      cur->length = i - cur->start + 1;
      cur = 0;
      num++;
    } else if (pat[i] == '\\') {
      if (++i >= pat_len) {
        if (error_msg) *error_msg = "invalid glob pattern, stray '\\'";
        return -1;
      }
    }
  }
  if (cur) {
    if (error_msg) *error_msg = "incomplete brace expansion";
    return -1;
  }
  return (int)num;
}

size_t csupport_glob_count_sub_patterns(const csupport_brace_expansion_t *exps,
                                         size_t num_expansions) {
  size_t count = 1;
  for (size_t i = 0; i < num_expansions; ++i) {
    size_t terms = exps[i].num_terms;
    if (terms == 0) terms = 1;
    if (count > ((size_t)-1) / terms)
      return (size_t)-1;
    count *= terms;
  }
  return count;
}
