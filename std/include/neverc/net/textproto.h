#ifndef NEVERC_NET_TEXTPROTO_H
#define NEVERC_NET_TEXTPROTO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char **keys;
    char **values;
    size_t count;
    size_t capacity;
} neverc_mime_header_t;

void neverc_mime_header_init(neverc_mime_header_t *h);
void neverc_mime_header_free(neverc_mime_header_t *h);
void neverc_mime_header_add(neverc_mime_header_t *h, const char *key, const char *value);
void neverc_mime_header_set(neverc_mime_header_t *h, const char *key, const char *value);
const char *neverc_mime_header_get(const neverc_mime_header_t *h, const char *key);
void neverc_mime_header_del(neverc_mime_header_t *h, const char *key);
size_t neverc_mime_header_len(const neverc_mime_header_t *h);

char *neverc_textproto_canonical_mime_header_key(const char *key);

int neverc_textproto_read_mime_header(const char *data, size_t len,
                                      neverc_mime_header_t *h, size_t *consumed);

int neverc_textproto_read_line(const char *data, size_t len,
                                char *line, size_t line_cap, size_t *consumed);

int neverc_textproto_read_dot_lines(const char *data, size_t len,
                                     char **lines, size_t max_lines,
                                     size_t *nlines, size_t *consumed);

int neverc_textproto_read_code_line(const char *line, int *code,
                                     const char **msg);

int neverc_textproto_trim_string(const char *s, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_NET_TEXTPROTO_H */
