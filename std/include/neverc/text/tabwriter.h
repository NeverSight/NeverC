#ifndef NEVERC_TEXT_TABWRITER_H
#define NEVERC_TEXT_TABWRITER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_TABWRITER_FILTER_HTML        (1 << 0)
#define NEVERC_TABWRITER_STRIP_ESCAPE       (1 << 1)
#define NEVERC_TABWRITER_ALIGN_RIGHT        (1 << 2)
#define NEVERC_TABWRITER_DISCARD_EMPTY_COLS (1 << 3)
#define NEVERC_TABWRITER_TAB_INDENT         (1 << 4)
#define NEVERC_TABWRITER_DEBUG              (1 << 5)

#define NEVERC_TABWRITER_MAX_COLS 256
#define NEVERC_TABWRITER_MAX_BUF  (64 * 1024)

typedef struct {
    int size;
    int width;
    int htab;
} neverc_tabwriter_cell_t;

typedef struct {
    char   *out_buf;
    size_t  out_len;
    size_t  out_cap;

    int     minwidth;
    int     tabwidth;
    int     padding;
    char    padchar;
    unsigned flags;

    char    buf[NEVERC_TABWRITER_MAX_BUF];
    size_t  buf_len;

    neverc_tabwriter_cell_t cells[NEVERC_TABWRITER_MAX_COLS];
    int     ncells;

    int     col_widths[NEVERC_TABWRITER_MAX_COLS];
    int     ncols;

    int     lines_start[4096];
    int     lines_ncells[4096];
    int     nlines;
} neverc_tabwriter_t;

void neverc_tabwriter_init(neverc_tabwriter_t *w, int minwidth, int tabwidth,
                           int padding, char padchar, unsigned flags);
void neverc_tabwriter_write(neverc_tabwriter_t *w, const char *data, size_t len);
void neverc_tabwriter_flush(neverc_tabwriter_t *w);
const char *neverc_tabwriter_output(const neverc_tabwriter_t *w, size_t *len);
void neverc_tabwriter_reset(neverc_tabwriter_t *w);

#ifdef __cplusplus
}
#endif

#endif
