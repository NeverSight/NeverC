/*===- Regex.c - Regex utilities (pure C) ----------------------*- C -*-===*/
#include "include/csupport/lregex.h"
#include "include/csupport/buffer.h"
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

size_t csupport_regex_escape(const char *src, size_t src_len,
                             char *dst, size_t dst_size) {
  csupport_obuf_t out = csupport_obuf(dst, dst_size);
  for (size_t i = 0; i < src_len; i++) {
    if (csupport_is_regex_metachar(src[i]))
      csupport_obuf_put(&out, '\\');
    csupport_obuf_put(&out, src[i]);
  }
  return csupport_obuf_finish(&out);
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

/* Only the first complaint is kept: the ones after it describe the same
   broken replacement string, and the caller has room for one. */
static void regex_sub_complain(csupport_obuf_t *problem, const char *msg) {
  if (problem->needed)
    return;
  csupport_obuf_write(problem, msg, strlen(msg));
  csupport_obuf_finish(problem);
}

size_t csupport_regex_sub(const char *repl, size_t repl_len,
                          const char *orig, size_t orig_len,
                          const size_t *match_starts, const size_t *match_ends,
                          size_t num_matches,
                          char *out, size_t out_size,
                          char *err, size_t err_size) {
  csupport_obuf_t buf = csupport_obuf(out, out_size);
  csupport_obuf_t problem = csupport_obuf(err, err_size);

  if (num_matches == 0 || match_starts[0] > orig_len) {
    csupport_obuf_write(&buf, orig, orig_len);
    return csupport_obuf_finish(&buf);
  }

  csupport_obuf_write(&buf, orig, match_starts[0]);

  size_t ri = 0;
  while (ri < repl_len) {
    size_t seg_start = ri;
    while (ri < repl_len && repl[ri] != '\\') ri++;
    csupport_obuf_write(&buf, repl + seg_start, ri - seg_start);

    if (ri >= repl_len) break;
    ri++; /* skip backslash */
    if (ri >= repl_len) {
      regex_sub_complain(&problem,
                         "replacement string contained trailing backslash");
      break;
    }

    char esc = repl[ri];
    if (esc == 't') { csupport_obuf_put(&buf, '\t'); ri++; }
    else if (esc == 'n') { csupport_obuf_put(&buf, '\n'); ri++; }
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
          csupport_obuf_write(&buf, orig + match_starts[ref],
                              match_ends[ref] - match_starts[ref]);
        } else {
          regex_sub_complain(&problem, "invalid backreference string");
        }
        ri = end_bracket + 1;
      } else {
        csupport_obuf_put(&buf, esc);
      }
    }
    else if (esc >= '0' && esc <= '9') {
      unsigned ref = 0;
      while (ri < repl_len && repl[ri] >= '0' && repl[ri] <= '9') {
        ref = ref * 10 + (unsigned)(repl[ri] - '0');
        ri++;
      }
      if (ref < num_matches) {
        csupport_obuf_write(&buf, orig + match_starts[ref],
                            match_ends[ref] - match_starts[ref]);
      } else {
        regex_sub_complain(&problem, "invalid backreference string");
      }
    }
    else {
      csupport_obuf_put(&buf, esc);
      ri++;
    }
  }

  if (match_ends[0] <= orig_len)
    csupport_obuf_write(&buf, orig + match_ends[0], orig_len - match_ends[0]);

  return csupport_obuf_finish(&buf);
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
