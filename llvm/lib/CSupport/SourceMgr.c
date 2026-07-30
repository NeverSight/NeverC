/*===- SourceMgr.c - Source file management (pure C) ------------*- C -*-===*/
#include "include/csupport/lsource_lmgr.h"
#include "include/csupport/buffer.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

unsigned csupport_find_line_number(const char *buf, size_t buf_len,
                                  size_t offset) {
  unsigned line = 1;
  for (size_t i = 0; i < offset && i < buf_len; i++)
    if (buf[i] == '\n' && line != UINT_MAX)
      line++;
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
    if (buf[i] == '\t') {
      unsigned advance = tab_stop - ((col - 1) % tab_stop);
      if (col > UINT_MAX - advance)
        return UINT_MAX;
      col += advance;
    } else if (col != UINT_MAX) {
      col++;
    }
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
  return lo > UINT_MAX ? UINT_MAX : (unsigned)lo;
}

size_t csupport_smdiag_format_msg(char *buf, size_t buflen,
                                  const char *filename, unsigned line,
                                  unsigned col, const char *kind,
                                  const char *msg) {
  csupport_obuf_t out = csupport_obuf(buf, buflen);
  csupport_obuf_write(&out, filename, strlen(filename));
  csupport_obuf_printf(&out, ":%u", line);
  if (col > 0)
    csupport_obuf_printf(&out, ":%u", col);
  csupport_obuf_write(&out, ": ", 2);
  csupport_obuf_write(&out, kind, strlen(kind));
  csupport_obuf_write(&out, ": ", 2);
  csupport_obuf_write(&out, msg, strlen(msg));
  return csupport_obuf_finish(&out);
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
  if (!filename || filename[0] == '\0') {
    buf[0] = '\0';
    return 0;
  }
  const char *display_name = filename;
  if (filename[0] == '-' && filename[1] == '\0')
    display_name = "<stdin>";
  csupport_obuf_t out = csupport_obuf(buf, buflen);
  csupport_obuf_write(&out, display_name, strlen(display_name));
  if (line_no > 0 && col_no > 0)
    csupport_obuf_printf(&out, ":%d:%d: ", line_no, col_no);
  else if (line_no > 0)
    csupport_obuf_printf(&out, ":%d: ", line_no);
  else
    csupport_obuf_write(&out, ": ", 2);
  size_t needed = csupport_obuf_finish(&out);
  return needed > INT_MAX ? -1 : (int)needed;
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
  if (tab_stop == 0) tab_stop = 8;
  csupport_obuf_t expanded = csupport_obuf(out, cap);
  size_t col = 0;
  for (size_t i = 0; i < len; i++) {
    if (line[i] == '\t') {
      do {
        csupport_obuf_put(&expanded, ' ');
        col++;
      } while ((col % tab_stop) != 0);
    } else {
      csupport_obuf_put(&expanded, line[i]);
      col++;
    }
  }
  return csupport_obuf_finish(&expanded);
}

size_t csupport_json_quote_to_buf(const char *s, size_t len,
                                   char *out, size_t cap) {
  /* An empty string still has to come out as "", so a null pointer with
     nothing behind it is not a reason to write nothing. */
  csupport_obuf_t quoted = csupport_obuf(out, cap);
  csupport_obuf_put(&quoted, '"');
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0x22 || c == 0x5C) csupport_obuf_put(&quoted, '\\');
    if (c >= 0x20) {
      csupport_obuf_put(&quoted, (char)c);
      continue;
    }
    csupport_obuf_put(&quoted, '\\');
    switch (c) {
    case '\t': csupport_obuf_put(&quoted, 't'); break;
    case '\n': csupport_obuf_put(&quoted, 'n'); break;
    case '\r': csupport_obuf_put(&quoted, 'r'); break;
    default: {
      char hex[8];
      int n = snprintf(hex, sizeof(hex), "u%04x", c);
      csupport_obuf_write(&quoted, hex, (size_t)n);
      break;
    }
    }
  }
  csupport_obuf_put(&quoted, '"');
  return csupport_obuf_finish(&quoted);
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
  csupport_obuf_t out = csupport_obuf(buf, cap);
  if (filename && fn_len > 0) {
    csupport_obuf_write(&out, filename, fn_len);
    csupport_obuf_put(&out, ':');
  }
  csupport_obuf_printf(&out, "%u:%u: ", line, col);
  csupport_obuf_write(&out, kind, kind_len);
  csupport_obuf_write(&out, ": ", 2);
  return csupport_obuf_finish(&out);
}

unsigned csupport_count_lines(const char *data, size_t len) {
  if (!data || len == 0) return 0;
  unsigned count = 1;
  for (size_t i = 0; i < len; i++) {
    if (data[i] == '\n' && count != UINT_MAX)
      count++;
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
    if (data[i] == '\n' && line != INT_MAX)
      line++;
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
  if (!line_start)
    return 0;
  if (!data) {
    *line_start = NULL;
    return 0;
  }
  if (start_offset >= data_len) {
    *line_start = data + data_len;
    return 0;
  }
  *line_start = data + start_offset;
  size_t len = 0;
  size_t remaining = data_len - start_offset;
  while (len < remaining && data[start_offset + len] != '\n'
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
  if (elem_size != 1 && elem_size != 2 && elem_size != 4 && elem_size != 8)
    return NULL;
  csupport_offset_cache_t *a =
      (csupport_offset_cache_t *)calloc(1, sizeof(csupport_offset_cache_t));
  if (!a) return NULL;
  a->elem_size = elem_size;
  a->cap = 256;
  a->data = malloc(a->cap * elem_size);
  if (!a->data) {
    free(a);
    return NULL;
  }
  return a;
}

void csupport_offset_cache_destroy(csupport_offset_cache_t *a) {
  if (a) {
    free(a->data);
    free(a);
  }
}

int csupport_offset_cache_push(csupport_offset_cache_t *a, uint64_t val) {
  if (a->count >= a->cap) {
    if (a->cap > SIZE_MAX / 2)
      return 0;
    size_t new_cap = a->cap * 2;
    if (new_cap > SIZE_MAX / a->elem_size)
      return 0;
    void *new_data = realloc(a->data, new_cap * a->elem_size);
    if (!new_data)
      return 0;
    a->data = new_data;
    a->cap = new_cap;
  }
  switch (a->elem_size) {
  case 1: ((uint8_t *)a->data)[a->count] = (uint8_t)val; break;
  case 2: ((uint16_t *)a->data)[a->count] = (uint16_t)val; break;
  case 4: ((uint32_t *)a->data)[a->count] = (uint32_t)val; break;
  case 8: ((uint64_t *)a->data)[a->count] = val; break;
  }
  a->count++;
  return 1;
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
    if (buf[i] == '\n' && !csupport_offset_cache_push(cache, i)) {
      csupport_offset_cache_destroy(cache);
      return NULL;
    }
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
  csupport_obuf_t out = csupport_obuf(buf, cap);
  if (fn_len > 0) {
    if (fn_len == 1 && filename[0] == '-') {
      csupport_obuf_write(&out, "<stdin>", 7);
    } else {
      csupport_obuf_write(&out, filename, fn_len);
    }
    if (line >= 0) {
      csupport_obuf_printf(&out, ":%d", line);
      if (col >= 0) {
        csupport_obuf_printf(&out, ":%lld", (long long)col + 1);
      }
    }
  }
  return csupport_obuf_finish(&out);
}

size_t csupport_format_diag_kind(char *buf, size_t cap, int kind) {
  const char *labels[] = {"error", "warning", "note", "remark"};
  if (kind < 0 || kind > 3) return 0;
  size_t len = strlen(labels[kind]);
  csupport_obuf_t out = csupport_obuf(buf, cap);
  csupport_obuf_write(&out, labels[kind], len);
  return csupport_obuf_finish(&out);
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
