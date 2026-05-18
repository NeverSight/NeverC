/*===- Regex.c - Regex utilities (pure C) ----------------------*- C -*-===*/
#include "include/csupport/lregex.h"
#include <string.h>

int csupport_is_regex_metachar(char c) {
  return c == '.' || c == '^' || c == '$' || c == '|' ||
         c == '(' || c == ')' || c == '[' || c == ']' ||
         c == '{' || c == '}' || c == '*' || c == '+' ||
         c == '?' || c == '\\';
}

int csupport_regex_is_literal(const char *pattern, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (csupport_is_regex_metachar(pattern[i])) return 0;
  }
  return 1;
}

int csupport_regex_escape(const char *src, size_t src_len,
                          char *dst, size_t dst_size, size_t *out_len) {
  size_t pos = 0;
  for (size_t i = 0; i < src_len; i++) {
    if (csupport_is_regex_metachar(src[i])) {
      if (pos + 2 > dst_size) return 0;
      dst[pos++] = '\\';
      dst[pos++] = src[i];
    } else {
      if (pos + 1 > dst_size) return 0;
      dst[pos++] = src[i];
    }
  }
  if (pos < dst_size) dst[pos] = '\0';
  *out_len = pos;
  return 1;
}

int csupport_wildcard_to_regex(const char *glob, size_t glob_len,
                               char *regex, size_t regex_size,
                               size_t *out_len) {
  size_t pos = 0;

#define EMIT(c) do { if (pos >= regex_size) return 0; regex[pos++] = (c); } while(0)

  EMIT('^');
  for (size_t i = 0; i < glob_len; i++) {
    switch (glob[i]) {
    case '*':
      EMIT('.');
      EMIT('*');
      break;
    case '?':
      EMIT('.');
      break;
    case '.': case '^': case '$': case '|':
    case '(': case ')': case '{': case '}':
    case '+': case '\\':
      EMIT('\\');
      EMIT(glob[i]);
      break;
    case '[':
      EMIT('[');
      i++;
      if (i < glob_len && glob[i] == '!') {
        EMIT('^');
        i++;
      }
      while (i < glob_len && glob[i] != ']') {
        EMIT(glob[i]);
        i++;
      }
      if (i < glob_len) EMIT(']');
      break;
    default:
      EMIT(glob[i]);
      break;
    }
  }
  EMIT('$');
  if (pos < regex_size) regex[pos] = '\0';
  *out_len = pos;
  return 1;
#undef EMIT
}

int csupport_regex_count_groups(const char *pattern, size_t len) {
  int count = 0;
  int in_escape = 0;
  for (size_t i = 0; i < len; i++) {
    if (in_escape) { in_escape = 0; continue; }
    if (pattern[i] == '\\') { in_escape = 1; continue; }
    if (pattern[i] == '(') count++;
  }
  return count;
}

size_t csupport_regex_sub(const char *repl, size_t repl_len,
                          const char *orig, size_t orig_len,
                          const size_t *match_starts, const size_t *match_ends,
                          size_t num_matches,
                          char *out, size_t out_size,
                          char *err, size_t err_size) {
  size_t pos = 0;
  size_t epos = 0;

#define SUB_EMIT(c) do { if (pos < out_size) out[pos] = (c); pos++; } while(0)
#define SUB_APPEND(p, n) do { for (size_t _k = 0; _k < (n); _k++) SUB_EMIT((p)[_k]); } while(0)
#define SUB_ERR(msg) do { if (err && err_size > 0 && epos == 0) { size_t _ml = strlen(msg); if (_ml >= err_size) _ml = err_size - 1; memcpy(err, msg, _ml); err[_ml] = '\0'; epos = _ml; } } while(0)

  if (num_matches == 0 || match_starts[0] > orig_len) {
    SUB_APPEND(orig, orig_len);
    if (pos < out_size) out[pos] = '\0';
    return pos;
  }

  SUB_APPEND(orig, match_starts[0]);

  size_t ri = 0;
  while (ri < repl_len) {
    size_t seg_start = ri;
    while (ri < repl_len && repl[ri] != '\\') ri++;
    SUB_APPEND(repl + seg_start, ri - seg_start);

    if (ri >= repl_len) break;
    ri++; /* skip backslash */
    if (ri >= repl_len) {
      SUB_ERR("replacement string contained trailing backslash");
      break;
    }

    char esc = repl[ri];
    if (esc == 't') { SUB_EMIT('\t'); ri++; }
    else if (esc == 'n') { SUB_EMIT('\n'); ri++; }
    else if (esc == 'g' && ri + 2 < repl_len && repl[ri + 1] == '<') {
      ri += 2; /* skip g< */
      size_t end_bracket = ri;
      while (end_bracket < repl_len && repl[end_bracket] != '>') end_bracket++;
      if (end_bracket < repl_len) {
        unsigned ref = 0;
        int valid = 1;
        for (size_t j = ri; j < end_bracket; j++) {
          if (repl[j] < '0' || repl[j] > '9') { valid = 0; break; }
          ref = ref * 10 + (unsigned)(repl[j] - '0');
        }
        if (valid && ref < num_matches) {
          SUB_APPEND(orig + match_starts[ref], match_ends[ref] - match_starts[ref]);
        } else {
          SUB_ERR("invalid backreference string");
        }
        ri = end_bracket + 1;
      } else {
        SUB_EMIT(esc);
      }
    }
    else if (esc >= '0' && esc <= '9') {
      unsigned ref = 0;
      while (ri < repl_len && repl[ri] >= '0' && repl[ri] <= '9') {
        ref = ref * 10 + (unsigned)(repl[ri] - '0');
        ri++;
      }
      if (ref < num_matches) {
        SUB_APPEND(orig + match_starts[ref], match_ends[ref] - match_starts[ref]);
      } else {
        SUB_ERR("invalid backreference string");
      }
    }
    else {
      SUB_EMIT(esc);
      ri++;
    }
  }

  if (match_ends[0] <= orig_len)
    SUB_APPEND(orig + match_ends[0], orig_len - match_ends[0]);

  if (pos < out_size) out[pos] = '\0';
  return pos;
#undef SUB_EMIT
#undef SUB_APPEND
#undef SUB_ERR
}

int csupport_simple_glob_match(const char *pattern, size_t plen,
                               const char *str, size_t slen) {
  size_t pi = 0, si = 0;
  size_t star_pi = (size_t)-1, star_si = 0;
  while (si < slen) {
    if (pi < plen && (pattern[pi] == '?' || pattern[pi] == str[si])) {
      pi++; si++;
    } else if (pi < plen && pattern[pi] == '*') {
      star_pi = pi++;
      star_si = si;
    } else if (star_pi != (size_t)-1) {
      pi = star_pi + 1;
      si = ++star_si;
    } else {
      return 0;
    }
  }
  while (pi < plen && pattern[pi] == '*') pi++;
  return pi == plen;
}

size_t csupport_regex_find_literal_prefix(const char *pattern, size_t plen,
                                           char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  for (size_t i = 0; i < plen && pos + 1 < out_cap; i++) {
    char c = pattern[i];
    if (c == '.' || c == '^' || c == '$' || c == '|' || c == '*' ||
        c == '+' || c == '?' || c == '(' || c == ')' || c == '[' ||
        c == ']' || c == '{' || c == '}')
      break;
    if (c == '\\') {
      if (i + 1 < plen) {
        char next = pattern[i + 1];
        if (next == 'n') { out[pos++] = '\n'; i++; }
        else if (next == 't') { out[pos++] = '\t'; i++; }
        else if (next == 'r') { out[pos++] = '\r'; i++; }
        else { out[pos++] = next; i++; }
      }
    } else {
      out[pos++] = c;
    }
  }
  out[pos] = '\0';
  return pos;
}
