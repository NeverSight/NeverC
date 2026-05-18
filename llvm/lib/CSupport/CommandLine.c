/*===- CommandLine.c - Command line parsing (pure C) -------------*- C -*-===*/
#include "include/csupport/lcommand_lline.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int csupport_cl_is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int csupport_cl_is_whitespace_or_null(char c) {
  return csupport_cl_is_whitespace(c) || c == '\0';
}

int csupport_cl_is_quote(char c) {
  return c == '"' || c == '\'';
}

int csupport_cl_has_utf8_bom(const char *data, size_t len) {
  return len >= 3 &&
         (unsigned char)data[0] == 0xef &&
         (unsigned char)data[1] == 0xbb &&
         (unsigned char)data[2] == 0xbf;
}

int csupport_cl_is_windows_special_char(char c) {
  return csupport_cl_is_whitespace_or_null(c) || c == '\\' || c == '"';
}

int csupport_cl_is_windows_special_char_in_cmd_name(char c) {
  return csupport_cl_is_whitespace_or_null(c) || c == '"';
}

size_t csupport_cl_parse_backslash(const char *src, size_t src_len, size_t pos,
                                   char *out, size_t out_cap, size_t *out_pos) {
  size_t I = pos;
  int backslash_count = 0;
  do {
    ++I;
    ++backslash_count;
  } while (I < src_len && src[I] == '\\');

  int followed_by_dquote = (I < src_len && src[I] == '"');
  if (followed_by_dquote) {
    int half = backslash_count / 2;
    for (int i = 0; i < half && *out_pos < out_cap; i++)
      out[(*out_pos)++] = '\\';
    if (backslash_count % 2 == 0)
      return I - 1;
    if (*out_pos < out_cap)
      out[(*out_pos)++] = '"';
    return I;
  }
  for (int i = 0; i < backslash_count && *out_pos < out_cap; i++)
    out[(*out_pos)++] = '\\';
  return I - 1;
}

int csupport_cl_tokenize_command_line(const char *source, size_t source_len,
                                      const char **argv, int max_args) {
  int argc = 0;
  const char *p = source;
  const char *end = source + source_len;

  while (p < end && argc < max_args) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
    if (p >= end) break;

    const char *start = p;
    int in_quote = 0;
    while (p < end) {
      if (*p == '"') { in_quote = !in_quote; p++; continue; }
      if (!in_quote && (*p == ' ' || *p == '\t' || *p == '\n')) break;
      p++;
    }
    if (argv) {
      argv[argc] = start;
    }
    argc++;
  }
  return argc;
}

size_t csupport_cl_tokenize_gnu(const char *src, size_t src_len,
                                char *token_buf, size_t token_buf_cap,
                                const char **tokens, size_t max_tokens,
                                size_t *token_count, int mark_eols) {
  size_t ntokens = 0;
  size_t tpos = 0;
  size_t I = 0;

  while (I < src_len && ntokens < max_tokens) {
    size_t tok_start = tpos;

    while (I < src_len && csupport_cl_is_whitespace(src[I])) {
      if (mark_eols && src[I] == '\n' && ntokens < max_tokens)
        tokens[ntokens++] = NULL;
      ++I;
    }
    if (I >= src_len) break;

    tok_start = tpos;

    while (I < src_len) {
      char C = src[I];

      if (I + 1 < src_len && C == '\\') {
        ++I;
        if (tpos < token_buf_cap)
          token_buf[tpos++] = src[I];
        ++I;
        continue;
      }

      if (csupport_cl_is_quote(C)) {
        ++I;
        while (I < src_len && src[I] != C) {
          if (src[I] == '\\' && I + 1 < src_len)
            ++I;
          if (tpos < token_buf_cap)
            token_buf[tpos++] = src[I];
          ++I;
        }
        if (I < src_len) ++I;
        continue;
      }

      if (csupport_cl_is_whitespace(C)) {
        if (tpos > tok_start) {
          if (tpos < token_buf_cap)
            token_buf[tpos++] = '\0';
          if (ntokens < max_tokens)
            tokens[ntokens++] = token_buf + tok_start;
        }
        if (mark_eols && C == '\n' && ntokens < max_tokens)
          tokens[ntokens++] = NULL;
        ++I;
        break;
      }

      if (tpos < token_buf_cap)
        token_buf[tpos++] = C;
      ++I;
    }

    if (I >= src_len && tpos > tok_start) {
      if (tpos < token_buf_cap)
        token_buf[tpos++] = '\0';
      if (ntokens < max_tokens)
        tokens[ntokens++] = token_buf + tok_start;
    }
  }

  if (token_count) *token_count = ntokens;
  return tpos;
}

size_t csupport_cl_tokenize_windows(const char *src, size_t src_len,
                                    char *token_buf, size_t token_buf_cap,
                                    const char **tokens, size_t max_tokens,
                                    size_t *token_count,
                                    int initial_cmd_name) {
  size_t ntokens = 0;
  size_t tpos = 0;
  size_t I = 0;
  int cmd_name = initial_cmd_name;
  enum { INIT, UNQUOTED, QUOTED } state = INIT;

  while (I < src_len && ntokens < max_tokens) {
    switch (state) {
    case INIT: {
      size_t tok_start = tpos;
      while (I < src_len && csupport_cl_is_whitespace_or_null(src[I]))
        ++I;
      if (I >= src_len) goto done;

      tok_start = tpos;
      size_t normal_start = I;
      if (cmd_name) {
        while (I < src_len && !csupport_cl_is_windows_special_char_in_cmd_name(src[I]))
          ++I;
      } else {
        while (I < src_len && !csupport_cl_is_windows_special_char(src[I]))
          ++I;
      }
      size_t nlen = I - normal_start;
      if (nlen > 0 && tpos + nlen <= token_buf_cap) {
        memcpy(token_buf + tpos, src + normal_start, nlen);
        tpos += nlen;
      }

      if (I >= src_len || csupport_cl_is_whitespace_or_null(src[I])) {
        if (tpos < token_buf_cap) token_buf[tpos++] = '\0';
        if (ntokens < max_tokens) tokens[ntokens++] = token_buf + tok_start;
        cmd_name = (I < src_len && src[I] == '\n') ? initial_cmd_name : 0;
        if (I < src_len) ++I;
      } else if (src[I] == '"') {
        state = QUOTED;
        ++I;
      } else if (src[I] == '\\') {
        size_t out_pos = tpos;
        I = csupport_cl_parse_backslash(src, src_len, I,
                                        token_buf, token_buf_cap, &out_pos);
        tpos = out_pos;
        ++I;
        state = UNQUOTED;
      }
      break;
    }
    case UNQUOTED:
      if (csupport_cl_is_whitespace_or_null(src[I])) {
        if (tpos < token_buf_cap) token_buf[tpos++] = '\0';
        if (ntokens < max_tokens) {
          size_t tok_start_u = 0;
          for (size_t k = tpos - 1; k > 0; k--) {
            if (token_buf[k - 1] == '\0') { tok_start_u = k; break; }
          }
          tokens[ntokens++] = token_buf + tok_start_u;
        }
        cmd_name = (src[I] == '\n') ? initial_cmd_name : 0;
        state = INIT;
        ++I;
      } else if (src[I] == '"') {
        state = QUOTED;
        ++I;
      } else if (src[I] == '\\' && !cmd_name) {
        size_t out_pos = tpos;
        I = csupport_cl_parse_backslash(src, src_len, I,
                                        token_buf, token_buf_cap, &out_pos);
        tpos = out_pos;
        ++I;
      } else {
        if (tpos < token_buf_cap) token_buf[tpos++] = src[I];
        ++I;
      }
      break;
    case QUOTED:
      if (src[I] == '"') {
        if (I + 1 < src_len && src[I + 1] == '"') {
          if (tpos < token_buf_cap) token_buf[tpos++] = '"';
          I += 2;
        } else {
          state = UNQUOTED;
          ++I;
        }
      } else if (src[I] == '\\' && !cmd_name) {
        size_t out_pos = tpos;
        I = csupport_cl_parse_backslash(src, src_len, I,
                                        token_buf, token_buf_cap, &out_pos);
        tpos = out_pos;
        ++I;
      } else {
        if (tpos < token_buf_cap) token_buf[tpos++] = src[I];
        ++I;
      }
      break;
    }
  }

done:
  if (state != INIT && tpos > 0) {
    if (tpos < token_buf_cap) token_buf[tpos++] = '\0';
    if (ntokens < max_tokens) {
      size_t tok_start_f = 0;
      for (size_t k = tpos - 1; k > 0; k--) {
        if (token_buf[k - 1] == '\0') { tok_start_f = k; break; }
      }
      tokens[ntokens++] = token_buf + tok_start_f;
    }
  }
  if (token_count) *token_count = ntokens;
  return tpos;
}

int csupport_cl_tokenize_config_line(const char *src, size_t src_len,
                                     char *line_buf, size_t line_buf_cap,
                                     size_t *line_len,
                                     size_t *consumed) {
  const char *p = src;
  const char *end = src + src_len;
  size_t lpos = 0;

  while (p < end && csupport_cl_is_whitespace(*p)) p++;
  if (p >= end) {
    *consumed = src_len;
    *line_len = 0;
    return 0;
  }

  if (*p == '#') {
    while (p < end && *p != '\n') p++;
    *consumed = (size_t)(p - src);
    *line_len = 0;
    return 0;
  }

  const char *start = p;
  for (; p < end; p++) {
    if (*p == '\\') {
      if (p + 1 < end) {
        p++;
        if (*p == '\n' || (*p == '\r' && p + 1 < end && p[1] == '\n')) {
          size_t seg = (size_t)((p - 1) - start);
          if (lpos + seg <= line_buf_cap)
            memcpy(line_buf + lpos, start, seg);
          lpos += seg;
          if (*p == '\r') p++;
          start = p + 1;
        }
      }
    } else if (*p == '\n') {
      break;
    }
  }

  size_t seg = (size_t)(p - start);
  if (lpos + seg <= line_buf_cap)
    memcpy(line_buf + lpos, start, seg);
  lpos += seg;

  if (lpos < line_buf_cap) line_buf[lpos] = '\0';
  *line_len = lpos;
  *consumed = (size_t)(p - src);
  return 1;
}

int csupport_cl_has_utf16_bom(const char *data, size_t len) {
  if (len < 2) return 0;
  unsigned char b0 = (unsigned char)data[0];
  unsigned char b1 = (unsigned char)data[1];
  return (b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF);
}

int csupport_cl_expand_response_file_line(const char *line, size_t line_len,
                                          char *out, size_t out_cap,
                                          size_t *out_len) {
  size_t pos = 0;
  size_t i = 0;
  while (i < line_len && csupport_cl_is_whitespace(line[i])) i++;
  if (i >= line_len || line[i] == '#') {
    *out_len = 0;
    return 0;
  }
  for (; i < line_len; i++) {
    if (line[i] == '\\' && i + 1 < line_len) {
      char next = line[i + 1];
      if (next == '\n' || next == '\r') {
        i++;
        if (next == '\r' && i + 1 < line_len && line[i + 1] == '\n') i++;
        continue;
      }
    }
    if (pos < out_cap) out[pos] = line[i];
    pos++;
  }
  size_t trim = pos < out_cap ? pos : out_cap;
  while (trim > 0 && csupport_cl_is_whitespace(out[trim - 1])) trim--;
  pos = trim;
  if (pos < out_cap) out[pos] = '\0';
  *out_len = pos;
  return pos > 0 ? 1 : 0;
}

size_t csupport_cl_find_option_prefix(const char *arg, size_t arg_len) {
  if (arg_len >= 2 && arg[0] == '-' && arg[1] == '-') return 2;
  if (arg_len >= 1 && arg[0] == '-') return 1;
  if (arg_len >= 1 && arg[0] == '/') return 1;
  return 0;
}

int csupport_cl_split_option_value(const char *arg, size_t arg_len,
                                   const char **key, size_t *key_len,
                                   const char **val, size_t *val_len) {
  size_t prefix = csupport_cl_find_option_prefix(arg, arg_len);
  const char *name = arg + prefix;
  size_t name_len = arg_len - prefix;
  for (size_t i = 0; i < name_len; i++) {
    if (name[i] == '=') {
      *key = name;
      *key_len = i;
      *val = name + i + 1;
      *val_len = name_len - i - 1;
      return 1;
    }
  }
  *key = name;
  *key_len = name_len;
  *val = NULL;
  *val_len = 0;
  return 0;
}

int csupport_cl_edit_distance(const char *a, size_t a_len,
                              const char *b, size_t b_len,
                              int allow_replacements,
                              unsigned max_dist) {
  size_t m = a_len, n = b_len;
  if (m == 0) return (int)n;
  if (n == 0) return (int)m;

  unsigned *prev = (unsigned *)malloc((n + 1) * sizeof(unsigned));
  unsigned *curr = (unsigned *)malloc((n + 1) * sizeof(unsigned));
  if (!prev || !curr) { free(prev); free(curr); return -1; }

  for (size_t j = 0; j <= n; j++) prev[j] = (unsigned)j;

  for (size_t i = 1; i <= m; i++) {
    curr[0] = (unsigned)i;
    unsigned best = curr[0];
    for (size_t j = 1; j <= n; j++) {
      if (a[i - 1] == b[j - 1]) {
        curr[j] = prev[j - 1];
      } else if (allow_replacements) {
        unsigned r = prev[j - 1] + 1;
        unsigned d = prev[j] + 1;
        unsigned ins = curr[j - 1] + 1;
        curr[j] = r < d ? (r < ins ? r : ins) : (d < ins ? d : ins);
      } else {
        unsigned d = prev[j] + 1;
        unsigned ins = curr[j - 1] + 1;
        curr[j] = d < ins ? d : ins;
      }
      if (curr[j] < best) best = curr[j];
    }
    if (max_dist > 0 && best > max_dist) {
      free(prev); free(curr);
      return (int)(max_dist + 1);
    }
    unsigned *tmp = prev; prev = curr; curr = tmp;
  }
  unsigned result = prev[n];
  free(prev); free(curr);
  return (int)result;
}

int csupport_cl_edit_distance_insensitive(const char *a, size_t a_len,
                                          const char *b, size_t b_len,
                                          int allow_replacements,
                                          unsigned max_dist) {
  size_t m = a_len, n = b_len;
  if (m == 0) return (int)n;
  if (n == 0) return (int)m;

  unsigned *prev = (unsigned *)malloc((n + 1) * sizeof(unsigned));
  unsigned *curr = (unsigned *)malloc((n + 1) * sizeof(unsigned));
  if (!prev || !curr) { free(prev); free(curr); return -1; }

  for (size_t j = 0; j <= n; j++) prev[j] = (unsigned)j;

  for (size_t i = 1; i <= m; i++) {
    curr[0] = (unsigned)i;
    unsigned best = curr[0];
    char ac = a[i - 1];
    if (ac >= 'A' && ac <= 'Z') ac = (char)(ac + ('a' - 'A'));
    for (size_t j = 1; j <= n; j++) {
      char bc = b[j - 1];
      if (bc >= 'A' && bc <= 'Z') bc = (char)(bc + ('a' - 'A'));
      if (ac == bc) {
        curr[j] = prev[j - 1];
      } else if (allow_replacements) {
        unsigned r = prev[j - 1] + 1;
        unsigned d = prev[j] + 1;
        unsigned ins = curr[j - 1] + 1;
        curr[j] = r < d ? (r < ins ? r : ins) : (d < ins ? d : ins);
      } else {
        unsigned d = prev[j] + 1;
        unsigned ins = curr[j - 1] + 1;
        curr[j] = d < ins ? d : ins;
      }
      if (curr[j] < best) best = curr[j];
    }
    if (max_dist > 0 && best > max_dist) {
      free(prev); free(curr);
      return (int)(max_dist + 1);
    }
    unsigned *tmp = prev; prev = curr; curr = tmp;
  }
  unsigned result = prev[n];
  free(prev); free(curr);
  return (int)result;
}

int csupport_cl_tokenize_gnu_to_buf(const char *src, size_t src_len,
                                     char *tokens_buf, size_t tokens_cap,
                                     size_t *token_offsets, size_t *token_lens,
                                     size_t max_tokens, size_t *num_tokens) {
  size_t ntok = 0, buf_pos = 0, tok_start = 0;
  int in_token = 0;
  for (size_t i = 0; i < src_len; i++) {
    char c = src[i];
    if (!in_token && csupport_cl_is_whitespace(c)) continue;
    if (!in_token) { in_token = 1; tok_start = buf_pos; }
    if (c == '\\' && i + 1 < src_len) {
      i++;
      if (buf_pos < tokens_cap) tokens_buf[buf_pos] = src[i];
      buf_pos++; continue;
    }
    if (csupport_cl_is_quote(c)) {
      char quote = c; i++;
      while (i < src_len && src[i] != quote) {
        if (src[i] == '\\' && i + 1 < src_len) i++;
        if (buf_pos < tokens_cap) tokens_buf[buf_pos] = src[i];
        buf_pos++; i++;
      }
      continue;
    }
    if (csupport_cl_is_whitespace(c)) {
      if (in_token && ntok < max_tokens) {
        token_offsets[ntok] = tok_start;
        token_lens[ntok] = buf_pos - tok_start;
        ntok++;
      }
      if (buf_pos < tokens_cap) tokens_buf[buf_pos] = '\0';
      buf_pos++; in_token = 0; continue;
    }
    if (buf_pos < tokens_cap) tokens_buf[buf_pos] = c;
    buf_pos++;
  }
  if (in_token && ntok < max_tokens) {
    token_offsets[ntok] = tok_start;
    token_lens[ntok] = buf_pos - tok_start;
    ntok++;
  }
  if (buf_pos < tokens_cap) tokens_buf[buf_pos] = '\0';
  *num_tokens = ntok;
  return 0;
}

size_t csupport_cl_format_option_help(char *buf, size_t buflen,
                                       const char *prefix, size_t prefix_len,
                                       const char *name, size_t name_len,
                                       const char *help, size_t help_len,
                                       unsigned max_width) {
  if (!buf || buflen == 0) return 0;
  size_t col = 0;
  for (size_t i = 0; i < prefix_len && col < buflen - 1; i++)
    buf[col++] = prefix[i];
  for (size_t i = 0; i < name_len && col < buflen - 1; i++)
    buf[col++] = name[i];
  if (col + 3 < max_width && help_len > 0) {
    while (col < max_width && col < buflen - 1) buf[col++] = ' ';
    if (col + 1 < buflen) buf[col++] = '-';
    if (col + 1 < buflen) buf[col++] = ' ';
    for (size_t i = 0; i < help_len && col < buflen - 1; i++)
      buf[col++] = help[i];
  }
  buf[col] = '\0';
  return col;
}

size_t csupport_cl_arg_plus_prefixes_size(const char *arg_name, size_t arg_len,
                                           size_t pad) {
  size_t prefix_len = (arg_len == 1) ? 1 : 2; /* "-" or "--" */
  size_t help_sep_len = 3; /* " - " */
  return arg_len + pad + prefix_len + help_sep_len;
}

size_t csupport_cl_format_arg_prefix(char *buf, size_t buflen,
                                      const char *arg_name, size_t arg_len,
                                      size_t pad) {
  if (!buf || buflen == 0) return 0;
  size_t out = 0;
  for (size_t i = 0; i < pad && out + 1 < buflen; i++)
    buf[out++] = ' ';
  if (arg_len > 1) {
    if (out + 2 < buflen) { buf[out++] = '-'; buf[out++] = '-'; }
  } else {
    if (out + 1 < buflen) buf[out++] = '-';
  }
  buf[out] = '\0';
  return out;
}

int csupport_cl_string_distance(const char *a, size_t a_len,
                                 const char *b, size_t b_len,
                                 int allow_replacements, unsigned max_dist) {
  size_t m = a_len, n = b_len;
  if (m > n) {
    const char *tmp_s = a; a = b; b = tmp_s;
    size_t tmp_l = m; m = n; n = tmp_l;
  }
  if (max_dist > 0 && n - m > max_dist) return (int)(n - m);
  unsigned row[256];
  if (n + 1 > 256) return -1;
  for (unsigned i = 0; i <= (unsigned)n; i++) row[i] = i;
  for (size_t i = 1; i <= m; i++) {
    row[0] = (unsigned)i;
    unsigned prev = (unsigned)(i - 1);
    for (size_t j = 1; j <= n; j++) {
      unsigned old = row[j];
      unsigned sub_cost = (a[i-1] == b[j-1]) ? 0 : 1;
      unsigned ins = row[j] + 1;
      unsigned del = row[j-1] + 1;
      unsigned sub = allow_replacements ? (prev + sub_cost) : (ins < del ? ins : del);
      unsigned best = ins < del ? ins : del;
      if (sub < best) best = sub;
      row[j] = best;
      prev = old;
    }
  }
  return (int)row[n];
}

int csupport_cl_looks_like_option(const char *arg, size_t len) {
  if (len == 0) return 0;
  if (arg[0] != '-') return 0;
  if (len == 1) return 0;
  if (arg[1] == '-') return (len > 2) ? 2 : 0;
  return 1;
}

int csupport_cl_skip_comment_line(const char *line, size_t len) {
  size_t i = 0;
  while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
  if (i >= len) return 1;
  return (line[i] == '#') ? 1 : 0;
}

size_t csupport_cl_trim_trailing_whitespace(const char *str, size_t len) {
  while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' ||
                     str[len-1] == '\r' || str[len-1] == '\n'))
    len--;
  return len;
}

int csupport_cl_parse_numeric_option(const char *str, size_t len,
                                      long long *out_val) {
  if (!str || len == 0 || !out_val) return 0;
  char buf[64];
  size_t n = len < sizeof(buf)-1 ? len : sizeof(buf)-1;
  memcpy(buf, str, n);
  buf[n] = '\0';
  char *end = 0;
  long long val = strtoll(buf, &end, 10);
  if (end == buf) return 0;
  *out_val = val;
  return 1;
}

int csupport_cl_match_prefix(const char *arg, size_t arg_len,
                              const char *prefix, size_t prefix_len) {
  if (arg_len < prefix_len) return 0;
  return memcmp(arg, prefix, prefix_len) == 0;
}

size_t csupport_cl_strip_leading_dashes(const char *arg, size_t len) {
  if (len >= 2 && arg[0] == '-' && arg[1] == '-') return 2;
  if (len >= 1 && arg[0] == '-') return 1;
  if (len >= 1 && arg[0] == '/') return 1;
  return 0;
}

int csupport_cl_split_key_value(const char *arg, size_t len,
                                 const char **key, size_t *key_len,
                                 const char **val, size_t *val_len) {
  for (size_t i = 0; i < len; i++) {
    if (arg[i] == '=') {
      *key = arg;
      *key_len = i;
      *val = arg + i + 1;
      *val_len = len - i - 1;
      return 1;
    }
  }
  *key = arg;
  *key_len = len;
  *val = NULL;
  *val_len = 0;
  return 0;
}

int csupport_cl_is_positional_arg(const char *arg, size_t len) {
  if (len == 0) return 1;
  if (arg[0] == '-') {
    if (len == 1) return 0;
    if (len == 2 && arg[1] == '-') return 1;
    return 0;
  }
  return 1;
}

size_t csupport_cl_format_option_usage(char *buf, size_t buflen,
                                        const char *name, size_t name_len,
                                        const char *meta, size_t meta_len,
                                        int is_required) {
  size_t pos = 0;
  if (!is_required && pos < buflen) buf[pos++] = '[';
  if (pos < buflen) buf[pos++] = '-';
  if (name_len > 1 && pos < buflen) buf[pos++] = '-';
  size_t n = name_len < (buflen - pos) ? name_len : (buflen - pos);
  if (n > 0) { memcpy(buf + pos, name, n); pos += n; }
  if (meta_len > 0) {
    if (pos < buflen) buf[pos++] = '=';
    n = meta_len < (buflen - pos) ? meta_len : (buflen - pos);
    if (n > 0) { memcpy(buf + pos, meta, n); pos += n; }
  }
  if (!is_required && pos < buflen) buf[pos++] = ']';
  if (pos < buflen) buf[pos] = '\0';
  return pos;
}

int csupport_cl_parse_bool(const char *str, size_t len, int *out) {
  if (!str || len == 0 || !out) return 0;
  if ((len == 4 && memcmp(str, "true", 4) == 0) ||
      (len == 1 && str[0] == '1') ||
      (len == 3 && (memcmp(str, "yes", 3) == 0 || memcmp(str, "Yes", 3) == 0)) ||
      (len == 2 && (memcmp(str, "on", 2) == 0 || memcmp(str, "On", 2) == 0)) ||
      (len == 4 && memcmp(str, "TRUE", 4) == 0)) {
    *out = 1;
    return 1;
  }
  if ((len == 5 && memcmp(str, "false", 5) == 0) ||
      (len == 1 && str[0] == '0') ||
      (len == 2 && (memcmp(str, "no", 2) == 0 || memcmp(str, "No", 2) == 0)) ||
      (len == 3 && (memcmp(str, "off", 3) == 0 || memcmp(str, "Off", 3) == 0)) ||
      (len == 5 && memcmp(str, "FALSE", 5) == 0)) {
    *out = 0;
    return 1;
  }
  return 0;
}

size_t csupport_cl_word_wrap(const char *text, size_t text_len,
                              unsigned start_col, unsigned max_col,
                              char *buf, size_t buflen) {
  if (!text || text_len == 0 || !buf || buflen == 0) return 0;
  size_t out = 0;
  unsigned col = start_col;
  size_t word_start = 0;
  for (size_t i = 0; i <= text_len; i++) {
    int at_end = (i == text_len);
    int is_sp = !at_end && (text[i] == ' ' || text[i] == '\t');
    if (is_sp || at_end) {
      size_t wlen = i - word_start;
      if (wlen > 0) {
        if (col + wlen > max_col && col > start_col) {
          if (out < buflen) buf[out++] = '\n';
          for (unsigned s = 0; s < start_col && out < buflen; s++)
            buf[out++] = ' ';
          col = start_col;
        } else if (col > start_col && out > 0) {
          if (out < buflen) buf[out++] = ' ';
          col++;
        }
        size_t n = wlen < (buflen - out) ? wlen : (buflen - out);
        memcpy(buf + out, text + word_start, n);
        out += n;
        col += (unsigned)wlen;
      }
      word_start = i + 1;
    }
  }
  if (out < buflen) buf[out] = '\0';
  return out;
}

size_t csupport_cl_format_option_help_line(char *buf, size_t buflen,
                                            const char *prefix, size_t prefix_len,
                                            const char *name, size_t name_len,
                                            const char *desc, size_t desc_len,
                                            unsigned help_col) {
  if (!buf || buflen == 0) return 0;
  size_t pos = 0;
  if (prefix && prefix_len > 0) {
    size_t n = prefix_len < buflen ? prefix_len : buflen - 1;
    memcpy(buf, prefix, n);
    pos = n;
  }
  if (name && name_len > 0 && pos < buflen - 1) {
    size_t n = name_len < (buflen - pos - 1) ? name_len : (buflen - pos - 1);
    memcpy(buf + pos, name, n);
    pos += n;
  }
  while (pos < help_col && pos < buflen - 1)
    buf[pos++] = ' ';
  if (pos < buflen - 1)
    buf[pos++] = '-';
  if (pos < buflen - 1)
    buf[pos++] = ' ';
  if (desc && desc_len > 0 && pos < buflen - 1) {
    size_t n = desc_len < (buflen - pos - 1) ? desc_len : (buflen - pos - 1);
    memcpy(buf + pos, desc, n);
    pos += n;
  }
  if (pos < buflen) buf[pos] = '\0';
  return pos;
}

int csupport_cl_comma_separate(const char *val, size_t val_len,
                                const char **tokens, size_t *token_lens,
                                size_t max_tokens) {
  if (!val || val_len == 0 || !tokens || !token_lens || max_tokens == 0) return 0;
  int count = 0;
  size_t start = 0;
  for (size_t i = 0; i <= val_len; i++) {
    if (i == val_len || val[i] == ',') {
      if ((size_t)count >= max_tokens) break;
      tokens[count] = val + start;
      token_lens[count] = i - start;
      count++;
      start = i + 1;
    }
  }
  return count;
}

int csupport_cl_parse_int(const char *str, size_t len, int *out) {
  if (!str || len == 0 || !out) return 0;
  char buf[32];
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, str, n);
  buf[n] = '\0';
  char *end = 0;
  long val = strtol(buf, &end, 0);
  if (end == buf || *end != '\0') return 0;
  *out = (int)val;
  return 1;
}

int csupport_cl_parse_unsigned(const char *str, size_t len, unsigned *out) {
  if (!str || len == 0 || !out) return 0;
  char buf[32];
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, str, n);
  buf[n] = '\0';
  char *end = 0;
  unsigned long val = strtoul(buf, &end, 0);
  if (end == buf || *end != '\0') return 0;
  *out = (unsigned)val;
  return 1;
}

int csupport_cl_parse_uint64(const char *str, size_t len, uint64_t *out) {
  if (!str || len == 0 || !out) return 0;
  char buf[32];
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, str, n);
  buf[n] = '\0';
  char *end = 0;
  unsigned long long val = strtoull(buf, &end, 0);
  if (end == buf || *end != '\0') return 0;
  *out = (uint64_t)val;
  return 1;
}

size_t csupport_cl_format_usage_line(char *buf, size_t buflen,
                                     const char *prog, size_t prog_len,
                                     const char *args, size_t args_len) {
  size_t pos = 0;
  const char *prefix = "USAGE: ";
  size_t prefix_len = 7;
  if (pos + prefix_len < buflen) { memcpy(buf + pos, prefix, prefix_len); }
  pos += prefix_len;
  if (prog && prog_len > 0) {
    size_t n = prog_len < buflen - pos ? prog_len : (pos < buflen ? buflen - pos - 1 : 0);
    if (n > 0 && pos < buflen) memcpy(buf + pos, prog, n);
    pos += prog_len;
  }
  if (pos < buflen) buf[pos] = ' ';
  pos++;
  if (args && args_len > 0) {
    size_t n = args_len < buflen - pos ? args_len : (pos < buflen ? buflen - pos - 1 : 0);
    if (n > 0 && pos < buflen) memcpy(buf + pos, args, n);
    pos += args_len;
  }
  if (pos < buflen) buf[pos] = '\0';
  return pos;
}

int csupport_cl_is_arg_equal(const char *arg, size_t arg_len,
                              const char *name, size_t name_len) {
  if (arg_len != name_len) return 0;
  return memcmp(arg, name, arg_len) == 0;
}

int csupport_cl_starts_with_dash(const char *arg, size_t len) {
  if (!arg || len == 0) return 0;
  return arg[0] == '-';
}

int csupport_cl_is_double_dash(const char *arg, size_t len) {
  return len == 2 && arg[0] == '-' && arg[1] == '-';
}

int csupport_cl_is_response_file(const char *arg, size_t len) {
  return len > 0 && arg[0] == '@';
}

size_t csupport_cl_extract_option_name(const char *arg, size_t arg_len,
                                        char *name_buf, size_t name_buflen) {
  if (!name_buf || name_buflen == 0) return 0;
  size_t start = 0;
  while (start < arg_len && arg[start] == '-') start++;
  size_t end = start;
  while (end < arg_len && arg[end] != '=') end++;
  size_t name_len = end - start;
  if (name_len >= name_buflen) name_len = name_buflen - 1;
  if (name_len > 0) memcpy(name_buf, arg + start, name_len);
  name_buf[name_len] = '\0';
  return name_len;
}

size_t csupport_cl_extract_option_value(const char *arg, size_t arg_len,
                                         char *val_buf, size_t val_buflen) {
  if (!val_buf || val_buflen == 0) return 0;
  const char *eq = (const char *)memchr(arg, '=', arg_len);
  if (!eq) { val_buf[0] = '\0'; return 0; }
  size_t val_start = (size_t)(eq - arg) + 1;
  size_t val_len = arg_len - val_start;
  if (val_len >= val_buflen) val_len = val_buflen - 1;
  memcpy(val_buf, arg + val_start, val_len);
  val_buf[val_len] = '\0';
  return val_len;
}

int csupport_cl_option_has_value(const char *arg, size_t arg_len) {
  return memchr(arg, '=', arg_len) != NULL;
}

int csupport_cl_compare_options(const char *a, size_t a_len,
                                 const char *b, size_t b_len) {
  size_t min_len = a_len < b_len ? a_len : b_len;
  int r = memcmp(a, b, min_len);
  if (r != 0) return r;
  if (a_len < b_len) return -1;
  if (a_len > b_len) return 1;
  return 0;
}

int csupport_cl_comma_split(const char *value, size_t value_len,
                            char *out_token, size_t out_cap,
                            size_t *consumed) {
  if (consumed) *consumed = 0;
  if (!out_token || out_cap == 0 || !consumed) return 0;
  size_t i = 0;
  while (i < value_len && value[i] != ',') i++;
  size_t tok_len = i < out_cap ? i : out_cap - 1;
  memcpy(out_token, value, tok_len);
  out_token[tok_len] = '\0';
  *consumed = (i < value_len) ? i + 1 : i;
  return (i < value_len) ? 1 : 0;
}

size_t csupport_cl_format_help_text(const char *desc, size_t desc_len,
                                     unsigned start_col, unsigned max_col,
                                     char *out, size_t out_cap) {
  if (!desc || desc_len == 0 || out_cap == 0) return 0;
  size_t pos = 0;
  size_t col = start_col;
  size_t i = 0;
  while (i < desc_len) {
    if (col >= max_col && desc[i] == ' ') {
      if (pos < out_cap - 1) out[pos++] = '\n';
      unsigned j = 0;
      while (j < start_col && pos < out_cap - 1) {
        out[pos++] = ' ';
        j++;
      }
      col = start_col;
      i++;
    } else {
      if (pos < out_cap - 1) out[pos++] = desc[i];
      col++;
      i++;
    }
  }
  out[pos] = '\0';
  return pos;
}

size_t csupport_cl_format_option_name(const char *name, size_t name_len,
                                       int is_long, char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  if (is_long) {
    if (pos + 2 < out_cap) { out[pos++] = '-'; out[pos++] = '-'; }
  } else {
    if (pos + 1 < out_cap) out[pos++] = '-';
  }
  size_t copy_len = name_len;
  if (pos + copy_len >= out_cap) copy_len = pos < out_cap - 1 ? out_cap - pos - 1 : 0;
  memcpy(out + pos, name, copy_len);
  pos += copy_len;
  out[pos] = '\0';
  return pos;
}

int csupport_cl_validate_option_name(const char *name, size_t name_len) {
  if (!name || name_len == 0) return 0;
  for (size_t i = 0; i < name_len; i++) {
    char c = name[i];
    if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
        !(c >= '0' && c <= '9') && c != '-' && c != '_')
      return 0;
  }
  return 1;
}

size_t csupport_cl_format_env_var(const char *prefix, size_t prefix_len,
                                   const char *name, size_t name_len,
                                   char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  if (prefix && prefix_len > 0) {
    size_t n = prefix_len < out_cap - 1 ? prefix_len : out_cap - 1;
    memcpy(out, prefix, n);
    pos = n;
    if (pos < out_cap - 1) out[pos++] = '_';
  }
  for (size_t i = 0; i < name_len && pos < out_cap - 1; i++) {
    char c = name[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c == '-') c = '_';
    out[pos++] = c;
  }
  out[pos] = '\0';
  return pos;
}

int csupport_cl_is_numeric(const char *str, size_t len) {
  if (!str || len == 0) return 0;
  size_t i = 0;
  if (str[0] == '-' || str[0] == '+') i++;
  if (i >= len) return 0;
  int has_digit = 0;
  int has_dot = 0;
  for (; i < len; i++) {
    if (str[i] >= '0' && str[i] <= '9') has_digit = 1;
    else if (str[i] == '.' && !has_dot) has_dot = 1;
    else if (str[i] == 'e' || str[i] == 'E') {
      i++;
      if (i < len && (str[i] == '+' || str[i] == '-')) i++;
      for (; i < len; i++)
        if (str[i] < '0' || str[i] > '9') return 0;
      break;
    }
    else return 0;
  }
  return has_digit;
}

size_t csupport_cl_tokenize_simple(const char *src, size_t src_len,
                                    const char **tokens, size_t max_tokens) {
  size_t count = 0;
  size_t i = 0;
  while (i < src_len && count < max_tokens) {
    while (i < src_len && (src[i] == ' ' || src[i] == '\t')) i++;
    if (i >= src_len) break;
    if (src[i] == '"') {
      i++;
      tokens[count++] = src + i;
      while (i < src_len && src[i] != '"') {
        if (src[i] == '\\' && i + 1 < src_len) i++;
        i++;
      }
      if (i < src_len) i++;
    } else if (src[i] == '\'') {
      i++;
      tokens[count++] = src + i;
      while (i < src_len && src[i] != '\'') i++;
      if (i < src_len) i++;
    } else {
      tokens[count++] = src + i;
      while (i < src_len && src[i] != ' ' && src[i] != '\t') i++;
    }
  }
  return count;
}

int csupport_cl_parse_key_value(const char *arg, size_t arg_len,
                                 const char **key_start, size_t *key_len,
                                 const char **val_start, size_t *val_len) {
  for (size_t i = 0; i < arg_len; i++) {
    if (arg[i] == '=') {
      *key_start = arg;
      *key_len = i;
      *val_start = arg + i + 1;
      *val_len = arg_len - i - 1;
      return 1;
    }
  }
  *key_start = arg;
  *key_len = arg_len;
  *val_start = 0;
  *val_len = 0;
  return 0;
}

size_t csupport_cl_format_value_list(const char *const *values,
                                      size_t num_values,
                                      const char *separator,
                                      char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  size_t sep_len = separator ? strlen(separator) : 0;
  for (size_t i = 0; i < num_values; i++) {
    if (i > 0 && sep_len > 0) {
      size_t n = (pos + sep_len < out_cap) ? sep_len : (out_cap > pos ? out_cap - pos : 0);
      if (n > 0) memcpy(out + pos, separator, n);
      pos += sep_len;
    }
    if (values[i]) {
      size_t vlen = strlen(values[i]);
      size_t n = (pos + vlen < out_cap) ? vlen : (out_cap > pos ? out_cap - pos : 0);
      if (n > 0) memcpy(out + pos, values[i], n);
      pos += vlen;
    }
  }
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}

int csupport_cl_expand_tilde(const char *path, size_t path_len,
                              char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  if (path_len == 0 || path[0] != '~') {
    size_t n = (path_len < out_cap) ? path_len : out_cap - 1;
    memcpy(out, path, n);
    out[n] = '\0';
    return (int)n;
  }
  const char *home = getenv("HOME");
  if (!home) home = "";
  size_t home_len = strlen(home);
  size_t rest_len = path_len - 1;
  size_t total = home_len + rest_len;
  size_t n = (total < out_cap) ? total : out_cap - 1;
  if (home_len <= n) {
    memcpy(out, home, home_len);
    size_t rest_n = n - home_len;
    if (rest_n > 0) memcpy(out + home_len, path + 1, rest_n);
  } else {
    memcpy(out, home, n);
  }
  out[n] = '\0';
  return (int)n;
}

size_t csupport_cl_count_tokens(const char *str, size_t str_len) {
  size_t count = 0;
  int in_token = 0;
  for (size_t i = 0; i < str_len; i++) {
    char c = str[i];
    int is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    if (!is_ws && !in_token) { count++; in_token = 1; }
    else if (is_ws) in_token = 0;
  }
  return count;
}

int csupport_cl_compare_options_by_name(const char *a, size_t a_len,
                                         const char *b, size_t b_len) {
  while (a_len > 0 && *a == '-') { a++; a_len--; }
  while (b_len > 0 && *b == '-') { b++; b_len--; }
  size_t min_len = a_len < b_len ? a_len : b_len;
  for (size_t i = 0; i < min_len; i++) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
    if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
    if (ca != cb) return (ca < cb) ? -1 : 1;
  }
  if (a_len != b_len) return (a_len < b_len) ? -1 : 1;
  return 0;
}

typedef void (*csupport_cl_token_cb)(const char *tok, size_t tok_len, void *ctx);
typedef void (*csupport_cl_eol_cb)(void *ctx);

void csupport_cl_tokenize_windows_impl(
    const char *src, size_t src_len,
    csupport_cl_token_cb add_token, csupport_cl_eol_cb mark_eol,
    void *ctx, int initial_command_name) {
  char token[4096];
  size_t tok_len = 0;
  int command_name = initial_command_name;
  enum { INIT, UNQUOTED, QUOTED } state = INIT;

  for (size_t i = 0; i < src_len; ++i) {
    switch (state) {
    case INIT: {
      tok_len = 0;
      while (i < src_len && csupport_cl_is_whitespace_or_null(src[i])) {
        if (src[i] == '\n' && mark_eol) mark_eol(ctx);
        ++i;
      }
      if (i >= src_len) break;
      size_t start = i;
      if (command_name) {
        while (i < src_len && !csupport_cl_is_windows_special_char_in_cmd_name(src[i]))
          ++i;
      } else {
        while (i < src_len && !csupport_cl_is_windows_special_char(src[i]))
          ++i;
      }
      size_t normal_len = i - start;
      if (i >= src_len || csupport_cl_is_whitespace_or_null(src[i])) {
        add_token(src + start, normal_len, ctx);
        if (i < src_len && src[i] == '\n') {
          if (mark_eol) mark_eol(ctx);
          command_name = initial_command_name;
        } else {
          command_name = 0;
        }
      } else if (src[i] == '"') {
        if (normal_len > 0 && tok_len + normal_len < sizeof(token)) {
          memcpy(token + tok_len, src + start, normal_len);
          tok_len += normal_len;
        }
        state = QUOTED;
      } else if (src[i] == '\\') {
        if (normal_len > 0 && tok_len + normal_len < sizeof(token)) {
          memcpy(token + tok_len, src + start, normal_len);
          tok_len += normal_len;
        }
        size_t out_len = 0;
        char bs_buf[256];
        size_t new_i = csupport_cl_parse_backslash(src, src_len, i,
                                                    bs_buf, sizeof(bs_buf), &out_len);
        if (out_len > 0 && tok_len + out_len < sizeof(token)) {
          memcpy(token + tok_len, bs_buf, out_len);
          tok_len += out_len;
        }
        i = new_i;
        state = UNQUOTED;
      }
      break;
    }
    case UNQUOTED:
      if (csupport_cl_is_whitespace_or_null(src[i])) {
        add_token(token, tok_len, ctx);
        tok_len = 0;
        if (src[i] == '\n') {
          command_name = initial_command_name;
          if (mark_eol) mark_eol(ctx);
        } else {
          command_name = 0;
        }
        state = INIT;
      } else if (src[i] == '"') {
        state = QUOTED;
      } else if (src[i] == '\\' && !command_name) {
        size_t out_len = 0;
        char bs_buf[256];
        size_t new_i = csupport_cl_parse_backslash(src, src_len, i,
                                                    bs_buf, sizeof(bs_buf), &out_len);
        if (out_len > 0 && tok_len + out_len < sizeof(token)) {
          memcpy(token + tok_len, bs_buf, out_len);
          tok_len += out_len;
        }
        i = new_i;
      } else {
        if (tok_len < sizeof(token) - 1)
          token[tok_len++] = src[i];
      }
      break;
    case QUOTED:
      if (src[i] == '"') {
        if (i < (src_len - 1) && src[i + 1] == '"') {
          if (tok_len < sizeof(token) - 1)
            token[tok_len++] = '"';
          ++i;
        } else {
          state = UNQUOTED;
        }
      } else if (src[i] == '\\' && !command_name) {
        size_t out_len = 0;
        char bs_buf[256];
        size_t new_i = csupport_cl_parse_backslash(src, src_len, i,
                                                    bs_buf, sizeof(bs_buf), &out_len);
        if (out_len > 0 && tok_len + out_len < sizeof(token)) {
          memcpy(token + tok_len, bs_buf, out_len);
          tok_len += out_len;
        }
        i = new_i;
      } else {
        if (tok_len < sizeof(token) - 1)
          token[tok_len++] = src[i];
      }
      break;
    }
  }
  if (state != INIT && tok_len > 0)
    add_token(token, tok_len, ctx);
}

void csupport_cl_tokenize_gnu_impl(
    const char *src, size_t src_len,
    csupport_cl_token_cb add_token, csupport_cl_eol_cb mark_eol,
    void *ctx) {
  char token[4096];
  size_t tok_len = 0;
  int in_token = 0;
  char quote_char = 0;

  for (size_t i = 0; i < src_len; ++i) {
    char c = src[i];

    if (quote_char) {
      if (c == quote_char) {
        quote_char = 0;
      } else if (c == '\\' && quote_char == '"' && i + 1 < src_len) {
        ++i;
        char next = src[i];
        if (next == '"' || next == '\\' || next == '$' || next == '`' || next == '\n') {
          if (tok_len < sizeof(token) - 1) token[tok_len++] = next;
        } else {
          if (tok_len < sizeof(token) - 1) token[tok_len++] = '\\';
          if (tok_len < sizeof(token) - 1) token[tok_len++] = next;
        }
      } else {
        if (tok_len < sizeof(token) - 1) token[tok_len++] = c;
      }
      in_token = 1;
      continue;
    }

    if (c == '\\' && i + 1 < src_len) {
      ++i;
      if (src[i] == '\n') {
        continue;
      }
      if (tok_len < sizeof(token) - 1) token[tok_len++] = src[i];
      in_token = 1;
      continue;
    }

    if (c == '\'' || c == '"') {
      quote_char = c;
      in_token = 1;
      continue;
    }

    if (c == '#' && !in_token) {
      while (i < src_len && src[i] != '\n') ++i;
      if (i < src_len && src[i] == '\n') {
        if (mark_eol) mark_eol(ctx);
      }
      continue;
    }

    if (csupport_cl_is_whitespace(c) || c == '\0') {
      if (in_token) {
        add_token(token, tok_len, ctx);
        tok_len = 0;
        in_token = 0;
      }
      if (c == '\n' && mark_eol) mark_eol(ctx);
      continue;
    }

    if (tok_len < sizeof(token) - 1) token[tok_len++] = c;
    in_token = 1;
  }

  if (in_token && tok_len > 0)
    add_token(token, tok_len, ctx);
}

size_t csupport_cl_format_wrapped_text(const char *text, size_t text_len,
                                        unsigned start_col, unsigned max_col,
                                        char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  unsigned col = start_col;
  size_t i = 0;
  while (i < text_len) {
    size_t word_start = i;
    while (i < text_len && text[i] != ' ' && text[i] != '\n')
      i++;
    size_t word_len = i - word_start;
    if (word_len == 0) { i++; continue; }
    if (col + word_len > max_col && col > start_col) {
      if (pos < out_cap) out[pos] = '\n';
      pos++;
      for (unsigned j = 0; j < start_col; j++) {
        if (pos < out_cap) out[pos] = ' ';
        pos++;
      }
      col = start_col;
    }
    if (col > start_col && pos > 0) {
      if (pos < out_cap) out[pos] = ' ';
      pos++;
      col++;
    }
    for (size_t j = 0; j < word_len; j++) {
      if (pos < out_cap) out[pos] = text[word_start + j];
      pos++;
    }
    col += (unsigned)word_len;
    while (i < text_len && text[i] == ' ') i++;
    if (i < text_len && text[i] == '\n') { i++; col = max_col + 1; }
  }
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}

int csupport_cl_parse_double(const char *str, size_t len, double *out_val) {
  if (!str || len == 0) return 0;
  char buf[128];
  size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, str, copy_len);
  buf[copy_len] = '\0';
  char *end = 0;
  double val = strtod(buf, &end);
  if (end == buf || end != buf + copy_len) return 0;
  if (out_val) *out_val = val;
  return 1;
}

int csupport_cl_should_print_option(const char *name, size_t name_len,
                                     const char *desc, size_t desc_len,
                                     int value_expected_optional) {
  return !value_expected_optional || name_len > 0 || desc_len > 0;
}

size_t csupport_cl_get_option_prefixes_size(void) {
  return 5 + 3;
}

int csupport_cl_comma_separate_values(const char *value, size_t value_len,
                                       void (*callback)(const char *, size_t, void *),
                                       void *ctx) {
  if (!value || value_len == 0) {
    if (callback) callback(value, value_len, ctx);
    return 1;
  }
  const char *p = value;
  const char *end = value + value_len;
  int count = 0;
  while (p < end) {
    const char *comma = p;
    while (comma < end && *comma != ',') comma++;
    if (callback) callback(p, (size_t)(comma - p), ctx);
    count++;
    p = (comma < end) ? comma + 1 : end;
  }
  return count;
}

size_t csupport_cl_extract_config_line(const char *src, size_t src_len,
                                        size_t *offset,
                                        char *out, size_t out_cap) {
  if (!src || !offset) return 0;
  if (!out || out_cap == 0) { *offset = src_len; return 0; }
  const char *cur = src + *offset;
  const char *end = src + src_len;

  while (cur < end && csupport_cl_is_whitespace(*cur))
    cur++;
  if (cur >= end) { *offset = src_len; return 0; }

  if (*cur == '#') {
    while (cur < end && *cur != '\n') cur++;
    *offset = (size_t)(cur - src);
    return 0;
  }

  size_t line_len = 0;
  const char *start = cur;
  for (; cur < end; cur++) {
    if (*cur == '\\' && cur + 1 < end) {
      const char *next = cur + 1;
      if (*next == '\n' || (*next == '\r' && next + 1 < end && next[1] == '\n')) {
        size_t seg = (size_t)(cur - start);
        if (line_len + seg <= out_cap)
          memcpy(out + line_len, start, seg);
        line_len += seg;
        cur = next;
        if (*cur == '\r') cur++;
        start = cur + 1;
      } else {
        cur = next;
      }
    } else if (*cur == '\n') {
      break;
    }
  }

  size_t seg = (size_t)(cur - start);
  if (line_len + seg <= out_cap)
    memcpy(out + line_len, start, seg);
  line_len += seg;
  if (line_len < out_cap) out[line_len] = '\0';

  *offset = (size_t)(cur - src);
  return line_len;
}

int csupport_cl_is_valid_option_name(const char *name, size_t len) {
  if (!name || len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
      return 0;
  }
  return 1;
}

size_t csupport_cl_format_env_var_name(const char *prefix, size_t prefix_len,
                                        const char *name, size_t name_len,
                                        char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  for (size_t i = 0; i < prefix_len && pos < out_cap; i++)
    out[pos++] = (prefix[i] >= 'a' && prefix[i] <= 'z')
                     ? (char)(prefix[i] - 32) : prefix[i];
  if (pos < out_cap) out[pos++] = '_';
  for (size_t i = 0; i < name_len && pos < out_cap; i++) {
    char c = name[i];
    if (c == '-') c = '_';
    else if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    out[pos++] = c;
  }
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}

int csupport_cl_is_flag_option(const char *name, size_t len) {
  if (!name || len == 0) return 0;
  if (name[0] != '-') return 0;
  if (len >= 2 && name[1] == '-') return 1;
  return 1;
}

int csupport_cl_strip_negation(const char *name, size_t name_len,
                                 char *out, size_t out_cap) {
  if (!name || name_len < 3 || !out || out_cap == 0) return 0;
  const char *prefix = "no-";
  size_t plen = 3;
  size_t start = 0;
  if (name[0] == '-') start++;
  if (start < name_len && name[start] == '-') start++;
  if (start + plen > name_len) return 0;
  if (memcmp(name + start, prefix, plen) != 0) return 0;
  size_t rest = name_len - start - plen;
  size_t copy = rest < out_cap - 1 ? rest : out_cap - 1;
  if (start > 0) {
    size_t dp = start < out_cap ? start : out_cap;
    memcpy(out, name, dp);
    if (dp + copy < out_cap) {
      memcpy(out + dp, name + start + plen, copy);
      out[dp + copy] = '\0';
      return 1;
    }
  }
  memcpy(out, name + start + plen, copy);
  out[copy] = '\0';
  return 1;
}

size_t csupport_cl_format_version(int major, int minor, int patch,
                                    char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  int n;
  if (patch >= 0)
    n = snprintf(out, out_cap, "%d.%d.%d", major, minor, patch);
  else
    n = snprintf(out, out_cap, "%d.%d", major, minor);
  return n > 0 ? (size_t)n : 0;
}

size_t csupport_cl_format_opt_width(const char *name, size_t name_len,
                                     const char *value_str, size_t value_len,
                                     char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  if (pos + 2 < out_cap) { out[pos++] = ' '; out[pos++] = ' '; }
  const char *prefix = (name_len > 1) ? "--" : "-";
  size_t plen = (name_len > 1) ? 2 : 1;
  for (size_t i = 0; i < plen && pos < out_cap - 1; i++)
    out[pos++] = prefix[i];
  for (size_t i = 0; i < name_len && pos < out_cap - 1; i++)
    out[pos++] = name[i];
  if (value_len > 0) {
    if (pos < out_cap - 1) out[pos++] = '=';
    if (pos < out_cap - 1) out[pos++] = '<';
    for (size_t i = 0; i < value_len && pos < out_cap - 1; i++)
      out[pos++] = value_str[i];
    if (pos < out_cap - 1) out[pos++] = '>';
  }
  out[pos] = '\0';
  return pos;
}

/* csupport_cl_parse_uint64 already defined above */
/* csupport_cl_parse_int64 already defined above */

size_t csupport_cl_format_size_suffix(uint64_t bytes, char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  const char *suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double val = (double)bytes;
  int idx = 0;
  while (val >= 1024.0 && idx < 4) { val /= 1024.0; idx++; }
  int n = snprintf(out, out_cap, "%.1f %s", val, suffixes[idx]);
  return n > 0 ? (size_t)n : 0;
}

size_t csupport_cl_expand_cfg_dir(const char *arg, size_t arg_len,
                                  const char *base_path, size_t base_len,
                                  char *out, size_t out_cap) {
  if (!arg || !out || out_cap == 0) return 0;
  const char token[] = "<CFGDIR>";
  const size_t token_len = 8;
  size_t pos = 0;
  size_t start = 0;
  while (start < arg_len) {
    const char *found = 0;
    for (size_t i = start; i + token_len <= arg_len; i++) {
      int match = 1;
      for (size_t j = 0; j < token_len; j++) {
        if (arg[i + j] != token[j]) { match = 0; break; }
      }
      if (match) { found = arg + i; break; }
    }
    if (!found) {
      size_t remain = arg_len - start;
      for (size_t i = 0; i < remain && pos < out_cap - 1; i++)
        out[pos++] = arg[start + i];
      break;
    }
    size_t prefix = (size_t)(found - arg) - start;
    for (size_t i = 0; i < prefix && pos < out_cap - 1; i++)
      out[pos++] = arg[start + i];
    for (size_t i = 0; i < base_len && pos < out_cap - 1; i++)
      out[pos++] = base_path[i];
    start = (size_t)(found - arg) + token_len;
  }
  out[pos] = '\0';
  return pos;
}

int csupport_cl_edit_distance_impl(const char *a, size_t a_len,
                                   const char *b, size_t b_len,
                                   int allow_replacements,
                                   unsigned max_distance) {
  if (!a || !b) return (int)(a_len > b_len ? a_len : b_len);
  size_t m = a_len, n = b_len;
  if (n > m) {
    const char *tmp_s = a; a = b; b = tmp_s;
    size_t tmp_n = m; m = n; n = tmp_n;
  }
  if (n == 0) return (int)m;
  unsigned *row = (unsigned *)malloc((n + 1) * sizeof(unsigned));
  if (!row) return -1;
  for (size_t i = 0; i <= n; i++) row[i] = (unsigned)i;
  for (size_t i = 1; i <= m; i++) {
    unsigned prev = (unsigned)(i - 1);
    row[0] = (unsigned)i;
    unsigned best = row[0];
    for (size_t j = 1; j <= n; j++) {
      unsigned old_row_j = row[j];
      if (a[i - 1] == b[j - 1]) {
        row[j] = prev;
      } else if (allow_replacements) {
        unsigned del = row[j] + 1;
        unsigned ins = row[j - 1] + 1;
        unsigned rep = prev + 1;
        row[j] = del < ins ? del : ins;
        if (rep < row[j]) row[j] = rep;
      } else {
        unsigned del = row[j] + 1;
        unsigned ins = row[j - 1] + 1;
        row[j] = del < ins ? del : ins;
      }
      if (row[j] < best) best = row[j];
      prev = old_row_j;
    }
    if (max_distance && best > max_distance) { free(row); return (int)max_distance + 1; }
  }
  unsigned result = row[n];
  free(row);
  return (int)result;
}

size_t csupport_cl_format_option_pair(const char *name, size_t name_len,
                                      const char *desc, size_t desc_len,
                                      unsigned indent, unsigned max_width,
                                      char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  for (unsigned i = 0; i < indent && pos < out_cap - 1; i++)
    out[pos++] = ' ';
  if (name_len > 1 && pos + 2 < out_cap) { out[pos++] = '-'; out[pos++] = '-'; }
  else if (name_len == 1 && pos + 1 < out_cap) { out[pos++] = '-'; }
  for (size_t i = 0; i < name_len && pos < out_cap - 1; i++)
    out[pos++] = name[i];
  if (desc_len > 0) {
    unsigned pad_to = indent + 24;
    while (pos < pad_to && pos < out_cap - 1) out[pos++] = ' ';
    if (pos < out_cap - 1) out[pos++] = ' ';
    if (pos < out_cap - 1) out[pos++] = '-';
    if (pos < out_cap - 1) out[pos++] = ' ';
    size_t col = pos;
    for (size_t i = 0; i < desc_len && pos < out_cap - 1; i++) {
      if (desc[i] == '\n' || (max_width > 0 && col >= max_width && desc[i] == ' ')) {
        out[pos++] = '\n';
        for (unsigned j = 0; j < pad_to + 3 && pos < out_cap - 1; j++)
          out[pos++] = ' ';
        col = pad_to + 3;
        if (desc[i] == ' ') continue;
      }
      out[pos++] = desc[i];
      col++;
    }
  }
  out[pos] = '\0';
  return pos;
}

size_t csupport_cl_expand_cfgdir(const char *arg, size_t arg_len,
                                  const char *base_path, size_t base_len,
                                  char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  static const char token[] = "<CFGDIR>";
  static const size_t token_len = 8;
  size_t pos = 0;
  size_t start = 0;

  for (;;) {
    const char *found = 0;
    for (size_t i = start; i + token_len <= arg_len; i++) {
      if (memcmp(arg + i, token, token_len) == 0) {
        found = arg + i;
        break;
      }
    }
    if (!found) break;

    size_t prefix_len = (size_t)(found - (arg + start));
    if (prefix_len > 0 && pos + prefix_len < out_cap) {
      memcpy(out + pos, arg + start, prefix_len);
      pos += prefix_len;
    }
    if (pos + base_len < out_cap) {
      memcpy(out + pos, base_path, base_len);
      pos += base_len;
    }
    start = (size_t)(found - arg) + token_len;
  }

  if (start == 0) {
    size_t n = arg_len < out_cap - 1 ? arg_len : out_cap - 1;
    memcpy(out, arg, n);
    out[n] = '\0';
    return n;
  }

  size_t remaining = arg_len - start;
  if (remaining > 0 && pos + remaining < out_cap) {
    memcpy(out + pos, arg + start, remaining);
    pos += remaining;
  }
  if (pos < out_cap) out[pos] = '\0';
  else if (out_cap > 0) out[out_cap - 1] = '\0';
  return pos;
}

int csupport_cl_opt_name_compare(const char *a, const char *b) {
  return strcmp(a, b);
}

size_t csupport_cl_skip_dash_prefix(const char *arg, size_t len) {
  if (len >= 2 && arg[0] == '-' && arg[1] == '-') return 2;
  if (len >= 1 && arg[0] == '-') return 1;
  return 0;
}

size_t csupport_cl_format_arg_with_prefix(const char *name, size_t name_len,
                                           size_t pad,
                                           char *out, size_t out_cap) {
  if (!out || out_cap == 0) return 0;
  size_t pos = 0;
  for (size_t i = 0; i < pad && pos < out_cap - 1; i++)
    out[pos++] = ' ';
  if (name_len > 1) {
    if (pos < out_cap - 1) out[pos++] = '-';
    if (pos < out_cap - 1) out[pos++] = '-';
  } else if (name_len == 1) {
    if (pos < out_cap - 1) out[pos++] = '-';
  }
  for (size_t i = 0; i < name_len && pos < out_cap - 1; i++)
    out[pos++] = name[i];
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}

int csupport_cl_find_nearest_option(const char *arg, size_t arg_len,
                                     const char *const *names, size_t num_names,
                                     size_t *best_idx, size_t max_distance) {
  size_t best_dist = max_distance + 1;
  int found = 0;
  for (size_t i = 0; i < num_names; i++) {
    size_t name_len = strlen(names[i]);
    size_t dist = csupport_cl_edit_distance(arg, arg_len, names[i], name_len,
                                             1, max_distance);
    if (dist < best_dist) {
      best_dist = dist;
      *best_idx = i;
      found = 1;
    }
  }
  return found ? (int)best_dist : -1;
}

size_t csupport_cl_format_value_desc(const char *desc, size_t desc_len,
                                      char *out, size_t out_cap) {
  if (desc_len == 0 || out_cap == 0) return 0;
  size_t pos = 0;
  if (pos < out_cap - 1) out[pos++] = '=';
  if (pos < out_cap - 1) out[pos++] = '<';
  for (size_t i = 0; i < desc_len && pos < out_cap - 1; i++)
    out[pos++] = desc[i];
  if (pos < out_cap - 1) out[pos++] = '>';
  if (pos < out_cap) out[pos] = '\0';
  return pos;
}

int csupport_cl_parse_int64(const char *str, size_t len, int64_t *out) {
  if (len == 0) return 0;
  char buf[32];
  if (len >= sizeof(buf)) return 0;
  memcpy(buf, str, len);
  buf[len] = '\0';
  char *end;
  long long val = strtoll(buf, &end, 0);
  if (end == buf || *end != '\0') return 0;
  *out = (int64_t)val;
  return 1;
}

int csupport_cl_parse_double_ex(const char *str, size_t len, double *out) {
  if (len == 0) return 0;
  char buf[64];
  if (len >= sizeof(buf)) return 0;
  memcpy(buf, str, len);
  buf[len] = '\0';
  char *end;
  double val = strtod(buf, &end);
  if (end == buf || *end != '\0') return 0;
  *out = val;
  return 1;
}

/*--- Help text formatting (pure C) ---*/

size_t csupport_cl_format_help_str(const char *help, size_t help_len,
                                    size_t global_width, size_t option_width,
                                    char *buf, size_t cap) {
  if (!buf || cap == 0) return 0;
  size_t pos = 0;
  if (option_width >= global_width) global_width = option_width + 1;
  size_t pad = global_width - option_width;
  if (pad > 0 && pos + pad < cap) {
    memset(buf + pos, ' ', pad);
    pos += pad;
  }
  if (pos + 2 < cap) {
    buf[pos++] = '-';
    buf[pos++] = ' ';
  }
  size_t to_copy = help_len;
  if (pos + to_copy >= cap) to_copy = cap - pos - 1;
  memcpy(buf + pos, help, to_copy);
  pos += to_copy;
  if (pos < cap) buf[pos++] = '\n';
  if (pos < cap) buf[pos] = '\0';
  return pos;
}

int csupport_cl_parse_bool_value(const char *str, size_t len) {
  if (len == 0) return 1;
  if (len == 1) {
    if (str[0] == '1') return 1;
    if (str[0] == '0') return 0;
  }
  if (len == 4 && (memcmp(str, "true", 4) == 0 ||
                    memcmp(str, "TRUE", 4) == 0 ||
                    memcmp(str, "True", 4) == 0)) return 1;
  if (len == 5 && (memcmp(str, "false", 5) == 0 ||
                    memcmp(str, "FALSE", 5) == 0 ||
                    memcmp(str, "False", 5) == 0)) return 0;
  if (len == 3 && (memcmp(str, "yes", 3) == 0 ||
                    memcmp(str, "YES", 3) == 0)) return 1;
  if (len == 2 && (memcmp(str, "no", 2) == 0 ||
                    memcmp(str, "NO", 2) == 0)) return 0;
  if (len == 2 && (memcmp(str, "on", 2) == 0 ||
                    memcmp(str, "ON", 2) == 0)) return 1;
  if (len == 3 && (memcmp(str, "off", 3) == 0 ||
                    memcmp(str, "OFF", 3) == 0)) return 0;
  return -1;
}

size_t csupport_cl_format_option_info(const char *prefix, const char *name,
                                       const char *value_desc,
                                       int has_value, int value_optional,
                                       int positional_eats_args,
                                       char *buf, size_t cap) {
  size_t pos = 0;
  size_t plen = strlen(prefix);
  size_t nlen = strlen(name);

  if (pos + plen + nlen < cap) {
    memcpy(buf + pos, prefix, plen);
    pos += plen;
    memcpy(buf + pos, name, nlen);
    pos += nlen;
  }

  if (has_value && value_desc && value_desc[0]) {
    size_t vlen = strlen(value_desc);
    if (positional_eats_args) {
      if (pos + vlen + 5 < cap) {
        buf[pos++] = ' '; buf[pos++] = '<';
        memcpy(buf + pos, value_desc, vlen); pos += vlen;
        buf[pos++] = '>'; buf[pos++] = '.'; buf[pos++] = '.'; buf[pos++] = '.';
      }
    } else if (value_optional) {
      if (pos + vlen + 4 < cap) {
        buf[pos++] = '['; buf[pos++] = '='; buf[pos++] = '<';
        memcpy(buf + pos, value_desc, vlen); pos += vlen;
        buf[pos++] = '>'; buf[pos++] = ']';
      }
    } else {
      char sep = (nlen == 1) ? ' ' : '=';
      if (pos + vlen + 3 < cap) {
        buf[pos++] = sep; buf[pos++] = '<';
        memcpy(buf + pos, value_desc, vlen); pos += vlen;
        buf[pos++] = '>';
      }
    }
  }

  if (pos < cap) buf[pos] = '\0';
  return pos;
}

int csupport_cl_format_opt_with_prefix2(const char *name, size_t name_len,
                                         size_t pad, char *buf, size_t cap) {
  size_t pos = csupport_cl_format_arg_prefix(buf, cap, name, name_len, pad);
  if (pos + name_len <= cap) {
    memcpy(buf + pos, name, name_len);
    pos += name_len;
  }
  if (pos < cap) buf[pos] = '\0';
  return (int)pos;
}

int csupport_cl_sort_cmp(const void *a, const void *b) {
  const char *const *sa = (const char *const *)a;
  const char *const *sb = (const char *const *)b;
  return strcmp(*sa, *sb);
}

size_t csupport_cl_format_version_printer(const char *program_name,
                                           const char *version,
                                           char *buf, size_t cap) {
  size_t pos = 0;
  if (program_name) {
    size_t plen = strlen(program_name);
    if (pos + plen < cap) { memcpy(buf + pos, program_name, plen); pos += plen; }
  }
  const char *sep = " version ";
  size_t slen = 9;
  if (pos + slen < cap) { memcpy(buf + pos, sep, slen); pos += slen; }
  if (version) {
    size_t vlen = strlen(version);
    if (pos + vlen < cap) { memcpy(buf + pos, version, vlen); pos += vlen; }
  }
  if (pos < cap) { buf[pos++] = '\n'; }
  if (pos < cap) buf[pos] = '\0';
  return pos;
}
