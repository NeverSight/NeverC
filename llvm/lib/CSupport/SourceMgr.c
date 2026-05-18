/*===- SourceMgr.c - Source file management (pure C) ------------*- C -*-===*/
#include "include/csupport/lsource_lmgr.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

unsigned csupport_find_line_number(const char *buf, size_t buf_len,
                                  size_t offset) {
  unsigned line = 1;
  for (size_t i = 0; i < offset && i < buf_len; i++)
    if (buf[i] == '\n') line++;
  return line;
}

size_t csupport_find_line_start(const char *buf, size_t offset) {
  while (offset > 0 && buf[offset - 1] != '\n') offset--;
  return offset;
}

size_t csupport_find_line_end(const char *buf, size_t buf_len, size_t offset) {
  while (offset < buf_len && buf[offset] != '\n' && buf[offset] != '\r')
    offset++;
  return offset;
}

unsigned csupport_find_column(const char *buf, size_t offset,
                              unsigned tab_stop) {
  size_t line_start = csupport_find_line_start(buf, offset);
  unsigned col = 1;
  if (tab_stop == 0) tab_stop = 8;
  for (size_t i = line_start; i < offset; i++) {
    if (buf[i] == '\t')
      col = ((col - 1 + tab_stop) / tab_stop) * tab_stop + 1;
    else
      col++;
  }
  return col;
}

void csupport_build_line_offsets(const char *buf, size_t buf_len,
                                 size_t *offsets, size_t *num_lines,
                                 size_t max_lines) {
  size_t count = 0;
  if (count < max_lines) offsets[count] = 0;
  count++;

  for (size_t i = 0; i < buf_len; i++) {
    if (buf[i] == '\n') {
      if (count < max_lines) offsets[count] = i + 1;
      count++;
    }
  }
  *num_lines = count;
}

unsigned csupport_binary_search_line(const size_t *line_offsets,
                                     size_t num_lines, size_t offset) {
  if (num_lines == 0) return 1;
  size_t lo = 0, hi = num_lines;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (line_offsets[mid] <= offset)
      lo = mid + 1;
    else
      hi = mid;
  }
  return (unsigned)lo;
}

size_t csupport_smdiag_format_msg(char *buf, size_t buflen,
                                  const char *filename, unsigned line,
                                  unsigned col, const char *kind,
                                  const char *msg) {
  int n;
  if (col > 0)
    n = snprintf(buf, buflen, "%s:%u:%u: %s: %s",
                 filename, line, col, kind, msg);
  else
    n = snprintf(buf, buflen, "%s:%u: %s: %s",
                 filename, line, kind, msg);
  return n > 0 ? (size_t)n : 0;
}

size_t csupport_expand_tabs_to_buf(const char *src, size_t src_len,
                                   char *buf, size_t buf_cap,
                                   unsigned tab_stop) {
  if (tab_stop == 0) tab_stop = 8;
  size_t out = 0;
  unsigned col = 0;
  for (size_t i = 0; i < src_len; i++) {
    if (src[i] == '\t') {
      do {
        if (out < buf_cap) buf[out] = ' ';
        out++; col++;
      } while ((col % tab_stop) != 0);
    } else {
      if (out < buf_cap) buf[out] = src[i];
      out++; col++;
    }
  }
  if (out < buf_cap) buf[out] = '\0';
  return out;
}

unsigned csupport_count_leading_whitespace_cols(const char *line, size_t len,
                                                unsigned tab_stop) {
  if (tab_stop == 0) tab_stop = 8;
  unsigned col = 0;
  for (size_t i = 0; i < len; i++) {
    if (line[i] == ' ') col++;
    else if (line[i] == '\t') {
      col += tab_stop - (col % tab_stop);
    } else break;
  }
  return col;
}

int csupport_is_non_ascii(char c) { return (unsigned char)c & 0x80; }

void csupport_build_fixit_line(
    char *caret_line, size_t caret_len,
    char *fixit_line, size_t *fixit_len, size_t fixit_cap,
    const char *const *fixit_texts, const size_t *fixit_text_lens,
    const size_t *fixit_start_cols, const size_t *fixit_end_cols,
    size_t num_fixits,
    const char *line_start, const char *line_end) {
  size_t prev_hint_end_col = 0;
  size_t cur_fixit_len = *fixit_len;

  for (size_t i = 0; i < num_fixits; i++) {
    const char *text = fixit_texts[i];
    size_t text_len = fixit_text_lens[i];

    int has_special = 0;
    for (size_t j = 0; j < text_len; j++) {
      if (text[j] == '\n' || text[j] == '\r' || text[j] == '\t') {
        has_special = 1;
        break;
      }
    }
    if (has_special) continue;

    size_t first_col = fixit_start_cols[i];
    size_t hint_col = first_col;
    if (hint_col < prev_hint_end_col)
      hint_col = prev_hint_end_col + 1;

    size_t last_modified = hint_col + text_len;
    if (last_modified > cur_fixit_len) {
      while (cur_fixit_len < last_modified && cur_fixit_len < fixit_cap)
        fixit_line[cur_fixit_len++] = ' ';
    }

    for (size_t j = 0; j < text_len && hint_col + j < fixit_cap; j++)
      fixit_line[hint_col + j] = text[j];

    prev_hint_end_col = last_modified;

    size_t last_col = fixit_end_cols[i];
    if (last_col > first_col && first_col < caret_len) {
      size_t end = last_col < caret_len ? last_col : caret_len;
      memset(caret_line + first_col, '~', end - first_col);
    }
  }
  *fixit_len = cur_fixit_len;
}

size_t csupport_columnwidth_utf8(const char *text, size_t len) {
  size_t width = 0;
  for (size_t i = 0; i < len; ) {
    unsigned char c = (unsigned char)text[i];
    if (c < 0x80) {
      width++; i++;
    } else if (c < 0xC0) {
      i++;
    } else if (c < 0xE0) {
      width++; i += 2;
    } else if (c < 0xF0) {
      width++; i += 3;
    } else {
      width += 2; i += 4;
    }
  }
  return width;
}

size_t csupport_print_source_line_to_buf(const char *line, size_t line_len,
                                          char *buf, size_t buf_cap,
                                          unsigned tab_stop) {
  if (!buf || buf_cap == 0 || !line) return 0;
  if (tab_stop == 0) tab_stop = 8;
  size_t out = 0;
  unsigned col = 0;
  for (size_t i = 0; i < line_len && out + 1 < buf_cap; i++) {
    if (line[i] == '\t') {
      do {
        if (out + 1 >= buf_cap) break;
        buf[out++] = ' ';
        col++;
      } while ((col % tab_stop) != 0);
    } else {
      buf[out++] = line[i];
      col++;
    }
  }
  if (out < buf_cap) buf[out] = '\0';
  else buf[buf_cap - 1] = '\0';
  return out;
}

int csupport_format_diag_location(char *buf, size_t buflen,
                                   const char *filename, int line_no,
                                   int col_no) {
  if (!buf || buflen == 0) return 0;
  int n;
  if (!filename || filename[0] == '\0') {
    buf[0] = '\0';
    return 0;
  }
  const char *display_name = filename;
  if (filename[0] == '-' && filename[1] == '\0')
    display_name = "<stdin>";
  if (line_no > 0 && col_no > 0)
    n = snprintf(buf, buflen, "%s:%d:%d: ", display_name, line_no, col_no);
  else if (line_no > 0)
    n = snprintf(buf, buflen, "%s:%d: ", display_name, line_no);
  else
    n = snprintf(buf, buflen, "%s: ", display_name);
  return (n > 0 && (size_t)n < buflen) ? n : (int)(buflen - 1);
}

size_t csupport_compute_caret_line(const char *source_line, size_t source_len,
                                    unsigned col, unsigned tab_stop,
                                    char *caret_buf, size_t caret_cap) {
  if (!caret_buf || caret_cap == 0) return 0;
  if (tab_stop == 0) tab_stop = 8;
  size_t out = 0;
  unsigned cur_col = 0;
  for (size_t i = 0; i < source_len && i < col && out + 1 < caret_cap; i++) {
    if (source_line[i] == '\t') {
      do {
        if (out + 1 >= caret_cap) goto done;
        caret_buf[out++] = ' ';
        cur_col++;
      } while ((cur_col % tab_stop) != 0);
    } else {
      if (out + 1 >= caret_cap) goto done;
      caret_buf[out++] = ' ';
      cur_col++;
    }
  }
done:
  if (out < caret_cap) {
    caret_buf[out] = '\0';
  } else {
    caret_buf[caret_cap - 1] = '\0';
  }
  return out;
}

size_t csupport_expand_tabs_to_string(const char *line, size_t len,
                                       char *out, size_t cap,
                                       unsigned tab_stop) {
  if (!line || !out || cap == 0) return 0;
  if (tab_stop == 0) tab_stop = 8;
  size_t pos = 0, col = 0;
  for (size_t i = 0; i < len; i++) {
    if (line[i] == '\t') {
      do {
        if (pos < cap - 1) out[pos++] = ' ';
        col++;
      } while ((col % tab_stop) != 0);
    } else {
      if (pos < cap - 1) out[pos++] = line[i];
      col++;
    }
  }
  out[pos] = '\0';
  return pos;
}

size_t csupport_json_quote_to_buf(const char *s, size_t len,
                                   char *out, size_t cap) {
  if (!s || !out || cap == 0) return 0;
  size_t pos = 0;
  if (pos < cap - 1) out[pos++] = '"';
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0x22 || c == 0x5C) {
      if (pos < cap - 1) out[pos++] = '\\';
    }
    if (c >= 0x20) {
      if (pos < cap - 1) out[pos++] = (char)c;
      continue;
    }
    if (pos < cap - 1) out[pos++] = '\\';
    switch (c) {
    case '\t': if (pos < cap - 1) out[pos++] = 't'; break;
    case '\n': if (pos < cap - 1) out[pos++] = 'n'; break;
    case '\r': if (pos < cap - 1) out[pos++] = 'r'; break;
    default: {
      char hex[8];
      int n = snprintf(hex, sizeof(hex), "u%04x", c);
      for (int j = 0; j < n && pos < cap - 1; j++)
        out[pos++] = hex[j];
      break;
    }
    }
  }
  if (pos < cap - 1) out[pos++] = '"';
  out[pos] = '\0';
  return pos;
}

size_t csupport_format_line_marker(char *buf, size_t cap,
                                    unsigned col, unsigned len) {
  if (!buf || cap == 0) return 0;
  size_t pos = 0;
  for (unsigned i = 1; i < col && pos < cap - 1; i++)
    buf[pos++] = ' ';
  if (pos < cap - 1) buf[pos++] = '^';
  for (unsigned i = 1; i < len && pos < cap - 1; i++)
    buf[pos++] = '~';
  buf[pos] = '\0';
  return pos;
}

size_t csupport_format_diag_header(char *buf, size_t cap,
                                    const char *filename, size_t fn_len,
                                    unsigned line, unsigned col,
                                    const char *kind, size_t kind_len) {
  if (!buf || cap == 0) return 0;
  int n = 0;
  if (filename && fn_len > 0)
    n = snprintf(buf, cap, "%.*s:%u:%u: %.*s: ",
                 (int)fn_len, filename, line, col,
                 (int)kind_len, kind);
  else
    n = snprintf(buf, cap, "%u:%u: %.*s: ", line, col,
                 (int)kind_len, kind);
  return n > 0 ? (size_t)n : 0;
}

unsigned csupport_count_lines(const char *data, size_t len) {
  if (!data || len == 0) return 0;
  unsigned count = 1;
  for (size_t i = 0; i < len; i++) {
    if (data[i] == '\n') count++;
  }
  return count;
}

size_t csupport_expand_tabs_with_colmap(const char *line, size_t len,
                                        char *out, size_t cap,
                                        unsigned tab_stop, unsigned *out_col_map,
                                        size_t col_map_cap) {
  if (!out || cap == 0) return 0;
  if (tab_stop == 0) tab_stop = 8;
  size_t pos = 0;
  unsigned col = 0;
  for (size_t i = 0; i < len && pos < cap - 1; i++) {
    if (col_map_cap > 0 && i < col_map_cap)
      out_col_map[i] = (unsigned)pos;
    if (line[i] == '\t') {
      unsigned spaces = tab_stop - (col % tab_stop);
      for (unsigned s = 0; s < spaces && pos < cap - 1; s++) {
        out[pos++] = ' ';
        col++;
      }
    } else {
      out[pos++] = line[i];
      col++;
    }
  }
  out[pos] = '\0';
  return pos;
}

int csupport_find_line_for_offset(const char *data, size_t data_len,
                                   size_t offset) {
  if (!data || offset > data_len) return -1;
  int line = 1;
  for (size_t i = 0; i < offset; i++) {
    if (data[i] == '\n') line++;
  }
  return line;
}

size_t csupport_get_line_start_offset(const char *data, size_t data_len,
                                       unsigned line_num) {
  if (!data || line_num == 0) return 0;
  unsigned cur = 1;
  for (size_t i = 0; i < data_len; i++) {
    if (cur == line_num) return i;
    if (data[i] == '\n') cur++;
  }
  return data_len;
}

size_t csupport_get_line_contents(const char *data, size_t data_len,
                                   size_t start_offset,
                                   const char **line_start) {
  if (!data || start_offset >= data_len) {
    *line_start = data + data_len;
    return 0;
  }
  *line_start = data + start_offset;
  size_t len = 0;
  while (start_offset + len < data_len && data[start_offset + len] != '\n'
         && data[start_offset + len] != '\r') {
    len++;
  }
  return len;
}

/*-- Offset cache for line-number lookups (replaces SmallVector<T,0> template) --*/

struct csupport_offset_cache {
  void *data;
  size_t count;
  size_t cap;
  unsigned elem_size;
};

csupport_offset_cache_t *csupport_offset_cache_create(unsigned elem_size) {
  csupport_offset_cache_t *a =
      (csupport_offset_cache_t *)calloc(1, sizeof(csupport_offset_cache_t));
  if (!a) return NULL;
  a->elem_size = elem_size;
  a->cap = 256;
  a->data = malloc(a->cap * elem_size);
  return a;
}

void csupport_offset_cache_destroy(csupport_offset_cache_t *a) {
  if (a) {
    free(a->data);
    free(a);
  }
}

void csupport_offset_cache_push(csupport_offset_cache_t *a, uint64_t val) {
  if (a->count >= a->cap) {
    a->cap *= 2;
    a->data = realloc(a->data, a->cap * a->elem_size);
  }
  switch (a->elem_size) {
  case 1: ((uint8_t *)a->data)[a->count] = (uint8_t)val; break;
  case 2: ((uint16_t *)a->data)[a->count] = (uint16_t)val; break;
  case 4: ((uint32_t *)a->data)[a->count] = (uint32_t)val; break;
  case 8: ((uint64_t *)a->data)[a->count] = val; break;
  }
  a->count++;
}

uint64_t csupport_offset_cache_get(const csupport_offset_cache_t *a,
                                    size_t i) {
  switch (a->elem_size) {
  case 1: return ((const uint8_t *)a->data)[i];
  case 2: return ((const uint16_t *)a->data)[i];
  case 4: return ((const uint32_t *)a->data)[i];
  case 8: return ((const uint64_t *)a->data)[i];
  default: return 0;
  }
}

size_t csupport_offset_cache_count(const csupport_offset_cache_t *cache) {
  return cache ? cache->count : 0;
}

size_t csupport_offset_cache_lower_bound(const csupport_offset_cache_t *a,
                                          uint64_t val) {
  size_t lo = 0, hi = a->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (csupport_offset_cache_get(a, mid) < val)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

csupport_offset_cache_t *csupport_offset_cache_build(const char *buf,
                                                       size_t buf_len,
                                                       unsigned elem_size) {
  csupport_offset_cache_t *cache = csupport_offset_cache_create(elem_size);
  if (!cache) return NULL;
  for (size_t i = 0; i < buf_len; i++) {
    if (buf[i] == '\n')
      csupport_offset_cache_push(cache, i);
  }
  return cache;
}

unsigned csupport_offset_cache_elem_size(size_t buf_size) {
  if (buf_size <= 0xFF) return 1;
  if (buf_size <= 0xFFFF) return 2;
  if (buf_size <= 0xFFFFFFFFULL) return 4;
  return 8;
}

size_t csupport_format_diag_loc_ex(char *buf, size_t cap,
                                     const char *filename, size_t fn_len,
                                     int line, int col) {
  size_t pos = 0;
  if (fn_len > 0) {
    if (fn_len == 1 && filename[0] == '-') {
      const char *stdin_str = "<stdin>";
      size_t slen = 7;
      if (pos + slen <= cap) { memcpy(buf + pos, stdin_str, slen); pos += slen; }
    } else {
      if (pos + fn_len <= cap) { memcpy(buf + pos, filename, fn_len); pos += fn_len; }
    }
    if (line >= 0) {
      if (pos < cap) buf[pos++] = ':';
      pos += (size_t)snprintf(buf + pos, cap > pos ? cap - pos : 0, "%d", line);
      if (col >= 0) {
        if (pos < cap) buf[pos++] = ':';
        pos += (size_t)snprintf(buf + pos, cap > pos ? cap - pos : 0, "%d", col + 1);
      }
    }
  }
  if (pos < cap) buf[pos] = '\0';
  return pos;
}

size_t csupport_format_diag_kind(char *buf, size_t cap, int kind) {
  const char *labels[] = {"error", "warning", "note", "remark"};
  if (kind < 0 || kind > 3) return 0;
  size_t len = strlen(labels[kind]);
  if (len < cap) { memcpy(buf, labels[kind], len); buf[len] = '\0'; }
  return len;
}

int csupport_count_line_leading_spaces(const char *line, size_t len,
                                         unsigned tab_stop) {
  if (tab_stop == 0) tab_stop = 8;
  unsigned col = 0;
  for (size_t i = 0; i < len; i++) {
    if (line[i] == ' ')
      col++;
    else if (line[i] == '\t')
      col = ((col / tab_stop) + 1) * tab_stop;
    else
      break;
  }
  return (int)col;
}

size_t csupport_build_caret_line(char *buf, size_t cap,
                                   unsigned col, unsigned end_col,
                                   unsigned tab_stop) {
  if (cap == 0) return 0;
  size_t pos = 0;
  for (unsigned i = 0; i < col && pos < cap - 1; i++)
    buf[pos++] = ' ';
  if (pos < cap - 1)
    buf[pos++] = '^';
  for (unsigned i = col + 1; i < end_col && pos < cap - 1; i++)
    buf[pos++] = '~';
  buf[pos] = '\0';
  return pos;
}
