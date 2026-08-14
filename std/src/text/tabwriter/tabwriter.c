#include "neverc/std/text/tabwriter.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static int out_reserve(neverc_tabwriter_t *w, size_t extra) {
    if (w->failed) return -1;
    if (w->out_len == SIZE_MAX ||
        extra > SIZE_MAX - w->out_len - 1U) {
        w->failed = 1;
        return -1;
    }
    size_t needed = w->out_len + extra + 1U;
    if (needed <= w->out_cap) return 0;

    size_t newcap = w->out_cap ? w->out_cap : 1024U;
    while (newcap < needed) {
        if (newcap > SIZE_MAX / 2U) {
            newcap = needed;
            break;
        }
        newcap *= 2U;
    }

    char *nb = (char *)realloc(w->out_buf, newcap);
    if (!nb) {
        w->failed = 1;
        return -1;
    }
    w->out_buf = nb;
    w->out_cap = newcap;
    return 0;
}

static int out_append(neverc_tabwriter_t *w, const char *data, size_t len) {
    if (len == 0) return w->failed ? -1 : 0;
    if (!data || out_reserve(w, len) != 0) {
        w->failed = 1;
        return -1;
    }
    memcpy(w->out_buf + w->out_len, data, len);
    w->out_len += len;
    return 0;
}

static int out_pad(neverc_tabwriter_t *w, size_t count, char ch) {
    if (count == 0) return w->failed ? -1 : 0;
    if (out_reserve(w, count) != 0) return -1;
    memset(w->out_buf + w->out_len, ch, count);
    w->out_len += count;
    return 0;
}

static int rune_width(const char *s, size_t len) {
    int w = 0;
    for (size_t i = 0; i < len; ) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { w++; i++; }
        else if (c < 0xC0) { i++; }
        else if (c < 0xE0) { w++; i += 2; }
        else if (c < 0xF0) { w++; i += 3; }
        else { w++; i += 4; }
    }
    return w;
}

static void begin_line(neverc_tabwriter_t *w) {
    if (w->nlines < 4096) {
        w->lines_start[w->nlines] = w->ncells;
        w->lines_ncells[w->nlines] = 0;
    }
}

static void end_cell(neverc_tabwriter_t *w, int htab) {
    if (w->nlines < 4096 && w->ncells < NEVERC_TABWRITER_MAX_COLS) {
        neverc_tabwriter_cell_t *cell = &w->cells[w->ncells];
        /* Cells partition w->buf contiguously, so the start of this cell is the
         * sum of all previously carved cell sizes. Track that running total in
         * buf_carved instead of re-summing every cell on each call. */
        int start = w->buf_carved;
        cell->size = (int)w->buf_len - start;
        cell->width = rune_width(w->buf + start, (size_t)cell->size);
        cell->htab = htab;
        w->buf_carved += cell->size;
        w->ncells++;
        w->lines_ncells[w->nlines]++;
    }
}

static int flush_lines(neverc_tabwriter_t *w) {
    memset(w->col_widths, 0, sizeof(w->col_widths));
    w->ncols = 0;

    /* lines_start/lines_ncells hold at most 4096 entries; write() lets nlines
     * grow past that (cells/lines beyond the cap are simply dropped), so clamp
     * the render to the tracked range to avoid reading past the arrays. */
    int last_line = (w->nlines < 4096) ? w->nlines : 4095;

    for (int ln = 0; ln <= last_line; ln++) {
        int start = w->lines_start[ln];
        int nc = w->lines_ncells[ln];
        for (int c = 0; c < nc - 1; c++) {
            int col = c;
            int cw = w->cells[start + c].width;
            if (col >= NEVERC_TABWRITER_MAX_COLS) break;
            if (cw > w->col_widths[col])
                w->col_widths[col] = cw;
            if (col + 1 > w->ncols) w->ncols = col + 1;
        }
    }

    size_t buf_pos = 0;
    for (int ln = 0; ln <= last_line; ln++) {
        int start = w->lines_start[ln];
        int nc = w->lines_ncells[ln];
        for (int c = 0; c < nc; c++) {
            neverc_tabwriter_cell_t *cell = &w->cells[start + c];
            if (cell->size < 0 ||
                out_append(w, w->buf + buf_pos,
                           (size_t)cell->size) != 0)
                return -1;

            if (c < nc - 1) {
                int col = c;
                size_t minimum_padding =
                    w->padding > 0 ? (size_t)w->padding : 0U;
                size_t pad_needed = minimum_padding;
                if (col < w->ncols &&
                    w->col_widths[col] > cell->width) {
                    pad_needed +=
                        (size_t)(w->col_widths[col] - cell->width);
                }
                if (w->minwidth > 0) {
                    size_t cell_w = cell->width > 0 ? (size_t)cell->width : 0U;
                    size_t minw = (size_t)w->minwidth;
                    if (cell_w < minw) {
                        size_t want = minw - cell_w;
                        if (want > pad_needed) pad_needed = want;
                    }
                }

                if (cell->htab && w->tabwidth > 0) {
                    size_t width =
                        cell->width > 0 ? (size_t)cell->width : 0U;
                    size_t tabwidth = (size_t)w->tabwidth;
                    if (pad_needed > SIZE_MAX - width) return -1;
                    size_t total = width + pad_needed;
                    size_t remainder = total % tabwidth;
                    if (remainder != 0) {
                        size_t extra = tabwidth - remainder;
                        if (pad_needed > SIZE_MAX - extra) return -1;
                        pad_needed += extra;
                    }
                }

                if (w->flags & NEVERC_TABWRITER_ALIGN_RIGHT) {
                    size_t cell_size = (size_t)cell->size;
                    if (cell_size > w->out_len ||
                        out_reserve(w, pad_needed) != 0)
                        return -1;
                    size_t cell_start = w->out_len - cell_size;
                    memmove(w->out_buf + cell_start + pad_needed,
                            w->out_buf + cell_start, cell_size);
                    memset(w->out_buf + cell_start, w->padchar, pad_needed);
                    w->out_len += pad_needed;
                } else {
                    if (out_pad(w, pad_needed, w->padchar) != 0)
                        return -1;
                }
            }
            buf_pos += (size_t)cell->size;
        }
        if (ln < w->nlines && out_append(w, "\n", 1) != 0)
            return -1;
    }
    return 0;
}

void neverc_tabwriter_init(neverc_tabwriter_t *w, int minwidth, int tabwidth,
                           int padding, char padchar, unsigned flags) {
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->minwidth = minwidth;
    w->tabwidth = tabwidth;
    w->padding = padding;
    w->padchar = padchar ? padchar : ' ';
    w->flags = flags;
    w->nlines = 0;
    begin_line(w);
}

void neverc_tabwriter_write(neverc_tabwriter_t *w, const char *data, size_t len) {
    if (!w) return;
    if ((!data && len != 0) || w->failed) {
        w->failed = 1;
        return;
    }
    /* Scan a line at a time (memchr to the '\n'), then carve tab-separated
     * cells inside it (memchr to each '\t'), bulk-copying every plain run into
     * w->buf. This replaces the byte-at-a-time classify/store loop; the cell
     * boundaries, buffer-cap truncation and line bookkeeping are identical. */
    size_t i = 0;
    while (i < len) {
        const char *nl = (const char *)memchr(data + i, '\n', len - i);
        size_t line_end = nl ? (size_t)(nl - data) : len;

        while (i < line_end) {
            const char *tab = (const char *)memchr(data + i, '\t', line_end - i);
            size_t cell_end = tab ? (size_t)(tab - data) : line_end;
            size_t run = cell_end - i;
            if (run) {
                size_t space = (w->buf_len < NEVERC_TABWRITER_MAX_BUF)
                             ? (size_t)NEVERC_TABWRITER_MAX_BUF - w->buf_len : 0;
                size_t take = run < space ? run : space;
                if (take) {
                    memcpy(w->buf + w->buf_len, data + i, take);
                    w->buf_len += take;
                }
            }
            if (tab) {
                if (w->buf_len < NEVERC_TABWRITER_MAX_BUF)
                    end_cell(w, 1);
                i = cell_end + 1;
            } else {
                i = cell_end;   /* run reaches line end; cell stays open */
            }
        }

        if (nl) {
            end_cell(w, 0);
            w->nlines++;
            if (w->nlines < 4096)
                begin_line(w);
            i = line_end + 1;
        } else {
            break;  /* no terminator: trailing cell is carved later by flush */
        }
    }
}

void neverc_tabwriter_flush(neverc_tabwriter_t *w) {
    if (!w || w->failed) return;
    end_cell(w, 0);
    if (flush_lines(w) != 0) {
        w->failed = 1;
        return;
    }
    if (out_reserve(w, 0) != 0)
        return;
    w->out_buf[w->out_len] = '\0';
}

const char *neverc_tabwriter_output(const neverc_tabwriter_t *w, size_t *len) {
    if (len) *len = (!w || w->failed) ? 0 : w->out_len;
    return (!w || w->failed) ? NULL : w->out_buf;
}

void neverc_tabwriter_reset(neverc_tabwriter_t *w) {
    if (!w) return;
    int mw = w->minwidth, tw = w->tabwidth, p = w->padding;
    char pc = w->padchar;
    unsigned f = w->flags;
    if (w->out_buf) free(w->out_buf);
    neverc_tabwriter_init(w, mw, tw, p, pc, f);
}
