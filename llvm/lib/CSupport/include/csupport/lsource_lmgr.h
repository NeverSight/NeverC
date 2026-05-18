#ifndef CSUPPORT_LSOURCE_LMGR_H
#define CSUPPORT_LSOURCE_LMGR_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

unsigned csupport_find_line_number(const char *buf, size_t buf_len,
                                   size_t offset);
size_t csupport_find_line_start(const char *buf, size_t offset);
size_t csupport_find_line_end(const char *buf, size_t buf_len, size_t offset);
unsigned csupport_find_column(const char *buf, size_t offset,
                              unsigned tab_stop);

void csupport_build_line_offsets(const char *buf, size_t buf_len,
                                 size_t *offsets, size_t *num_lines,
                                 size_t max_lines);

unsigned csupport_binary_search_line(const size_t *line_offsets,
                                     size_t num_lines, size_t offset);

size_t csupport_smdiag_format_msg(char *buf, size_t buflen,
                                  const char *filename, unsigned line,
                                  unsigned col, const char *kind,
                                  const char *msg);

size_t csupport_expand_tabs_to_buf(const char *src, size_t src_len, char *buf,
                                   size_t buf_cap, unsigned tab_stop);
unsigned csupport_count_leading_whitespace_cols(const char *line, size_t len,
                                                unsigned tab_stop);
int csupport_is_non_ascii(char c);
size_t csupport_columnwidth_utf8(const char *text, size_t len);

void csupport_build_fixit_line(char *caret_line, size_t caret_len,
                               char *fixit_line, size_t *fixit_len,
                               size_t fixit_cap, const char *const *fixit_texts,
                               const size_t *fixit_text_lens,
                               const size_t *fixit_start_cols,
                               const size_t *fixit_end_cols, size_t num_fixits,
                               const char *line_start, const char *line_end);

size_t csupport_print_source_line_to_buf(const char *line, size_t line_len,
                                         char *buf, size_t buf_cap,
                                         unsigned tab_stop);

int csupport_format_diag_location(char *buf, size_t buflen,
                                  const char *filename, int line_no,
                                  int col_no);

size_t csupport_compute_caret_line(const char *source_line, size_t source_len,
                                   unsigned col, unsigned tab_stop,
                                   char *caret_buf, size_t caret_cap);

size_t csupport_expand_tabs_to_string(const char *line, size_t len, char *out,
                                      size_t cap, unsigned tab_stop);

size_t csupport_json_quote_to_buf(const char *s, size_t len, char *out,
                                  size_t cap);

size_t csupport_format_line_marker(char *buf, size_t cap, unsigned col,
                                   unsigned len);
size_t csupport_format_diag_header(char *buf, size_t cap, const char *filename,
                                   size_t fn_len, unsigned line, unsigned col,
                                   const char *kind, size_t kind_len);
unsigned csupport_count_lines(const char *data, size_t len);

size_t csupport_expand_tabs_with_colmap(const char *line, size_t len, char *out,
                                        size_t cap, unsigned tab_stop,
                                        unsigned *out_col_map,
                                        size_t col_map_cap);

int csupport_find_line_for_offset(const char *data, size_t data_len,
                                  size_t offset);

size_t csupport_get_line_start_offset(const char *data, size_t data_len,
                                      unsigned line_num);

size_t csupport_get_line_contents(const char *data, size_t data_len,
                                  size_t start_offset, const char **line_start);

typedef struct csupport_offset_cache csupport_offset_cache_t;

csupport_offset_cache_t *csupport_offset_cache_create(unsigned elem_size);
void csupport_offset_cache_destroy(csupport_offset_cache_t *cache);
void csupport_offset_cache_push(csupport_offset_cache_t *cache, uint64_t val);
uint64_t csupport_offset_cache_get(const csupport_offset_cache_t *cache,
                                   size_t idx);
size_t csupport_offset_cache_count(const csupport_offset_cache_t *cache);
size_t csupport_offset_cache_lower_bound(const csupport_offset_cache_t *cache,
                                         uint64_t val);

csupport_offset_cache_t *csupport_offset_cache_build(const char *buf,
                                                     size_t buf_len,
                                                     unsigned elem_size);

unsigned csupport_offset_cache_elem_size(size_t buf_size);

size_t csupport_expand_tabs_to_string(const char *line, size_t len, char *out,
                                      size_t cap, unsigned tab_stop);

size_t csupport_format_diag_kind(char *buf, size_t cap, int kind);
int csupport_count_line_leading_spaces(const char *line, size_t len,
                                       unsigned tab_stop);
size_t csupport_build_caret_line(char *buf, size_t cap, unsigned col,
                                 unsigned end_col, unsigned tab_stop);
size_t csupport_format_diag_loc_ex(char *buf, size_t cap, const char *filename,
                                   size_t fn_len, int line, int col);

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_LSOURCE_LMGR_H */
