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

/* Go unicode/utf8.RuneCount: each DecodeRune is width 1. Invalid sequences
 * (including a lone continuation such as 0x96) consume one byte and count 1. */
static size_t utf8_rune_len(const unsigned char *s, size_t n) {
    unsigned char c;
    if (n == 0) return 0;
    c = s[0];
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c < 0xE0) {
        if (n >= 2 && (s[1] & 0xC0) == 0x80) return 2;
    } else if (c >= 0xE0 && c < 0xF0 && n >= 3 &&
               (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        if ((c > 0xE0 || s[1] >= 0xA0) && (c != 0xED || s[1] < 0xA0))
            return 3;
    } else if (c >= 0xF0 && c < 0xF5 && n >= 4 &&
               (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
               (s[3] & 0xC0) == 0x80) {
        if ((c > 0xF0 || s[1] >= 0x90) && (c < 0xF4 || s[1] < 0x90))
            return 4;
    }
    return 1;
}

static int rune_width(const char *s, size_t len) {
    int w = 0;
    size_t i = 0;
    while (i < len) {
        i += utf8_rune_len((const unsigned char *)s + i, len - i);
        w++;
    }
    return w;
}

static void begin_line(neverc_tabwriter_t *w) {
    if (w->nlines < NEVERC_TABWRITER_MAX_LINES) {
        w->lines_start[w->nlines] = w->ncells;
        w->lines_ncells[w->nlines] = 0;
    }
}

static void end_cell(neverc_tabwriter_t *w, int htab) {
    if (w->failed) return;
    if (w->ncells >= NEVERC_TABWRITER_MAX_CELLS) {
        w->failed = 1;
        return;
    }
    if (w->nlines < NEVERC_TABWRITER_MAX_LINES) {
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

static void reset_cells(neverc_tabwriter_t *w) {
    w->buf_len = 0;
    w->buf_carved = 0;
    w->ncells = 0;
    w->nlines = 0;
    w->ncols = 0;
    begin_line(w);
}

static int line_ncells(const neverc_tabwriter_t *w, int ln) {
    return (ln >= 0 && ln < NEVERC_TABWRITER_MAX_LINES) ? w->lines_ncells[ln] : 0;
}

static int line_start(const neverc_tabwriter_t *w, int ln) {
    return (ln >= 0 && ln < NEVERC_TABWRITER_MAX_LINES) ? w->lines_start[ln] : 0;
}

/* Go writePadding: tab padchar (or TabIndent useTabs) snaps to tab stops
 * and emits '\t'; otherwise emit (cellw-textw) padchars. */
static int write_padding(neverc_tabwriter_t *w, int textw, int cellw, int use_tabs) {
    if (textw < 0) textw = 0;
    if (cellw < 0) cellw = 0;
    if (w->padchar == '\t' || use_tabs) {
        int tw = w->tabwidth;
        if (tw <= 0) return 0;
        if (cellw > 0) {
            int rem = cellw % tw;
            if (rem != 0) {
                if (cellw > INT32_MAX - (tw - rem)) return -1;
                cellw += tw - rem;
            }
        }
        int n = cellw - textw;
        if (n < 0) n = 0;
        int ntabs = (n + tw - 1) / tw;
        return out_pad(w, (size_t)ntabs, '\t');
    }
    if (cellw < textw) return 0;
    return out_pad(w, (size_t)(cellw - textw), w->padchar);
}

static int write_lines(neverc_tabwriter_t *w, size_t *buf_pos,
                       int line0, int line1, int n_lines) {
    for (int i = line0; i < line1; i++) {
        int start = line_start(w, i);
        int nc = line_ncells(w, i);
        int use_tabs = (w->flags & NEVERC_TABWRITER_TAB_INDENT) != 0;
        for (int j = 0; j < nc; j++) {
            neverc_tabwriter_cell_t *c = &w->cells[start + j];
            if (j > 0 && (w->flags & NEVERC_TABWRITER_DEBUG)) {
                if (out_append(w, "|", 1) != 0) return -1;
            }
            if (c->size < 0) return -1;
            if (c->size == 0) {
                if (j < w->ncols &&
                    write_padding(w, c->width, w->col_widths[j], use_tabs) != 0)
                    return -1;
            } else {
                use_tabs = 0;
                if (w->flags & NEVERC_TABWRITER_ALIGN_RIGHT) {
                    if (j < w->ncols &&
                        write_padding(w, c->width, w->col_widths[j], 0) != 0)
                        return -1;
                    if (out_append(w, w->buf + *buf_pos, (size_t)c->size) != 0)
                        return -1;
                    *buf_pos += (size_t)c->size;
                } else {
                    if (out_append(w, w->buf + *buf_pos, (size_t)c->size) != 0)
                        return -1;
                    *buf_pos += (size_t)c->size;
                    if (j < w->ncols &&
                        write_padding(w, c->width, w->col_widths[j], 0) != 0)
                        return -1;
                }
            }
        }
        if (i + 1 != n_lines && out_append(w, "\n", 1) != 0)
            return -1;
    }
    return 0;
}

/* Go Writer.format: elastic tabstops — a short line ends the current
 * column block so later rows do not stretch earlier ones. */
static int format_block(neverc_tabwriter_t *w, size_t *buf_pos,
                        int line0, int line1, int n_lines) {
    int column = w->ncols;
    int this;
    for (this = line0; this < line1; this++) {
        if (column >= line_ncells(w, this) - 1)
            continue;

        if (write_lines(w, buf_pos, line0, this, n_lines) != 0)
            return -1;
        line0 = this;

        int width = w->minwidth > 0 ? w->minwidth : 0;
        int pad = w->padding > 0 ? w->padding : 0;
        int discardable = 1;
        for (; this < line1; this++) {
            int nc = line_ncells(w, this);
            if (column >= nc - 1)
                break;
            neverc_tabwriter_cell_t *c =
                &w->cells[line_start(w, this) + column];
            int ww = c->width + pad;
            if (ww > width) width = ww;
            if (c->width > 0 || c->htab)
                discardable = 0;
        }
        if (discardable && (w->flags & NEVERC_TABWRITER_DISCARD_EMPTY_COLS))
            width = 0;

        if (w->ncols >= NEVERC_TABWRITER_MAX_COLS)
            return write_lines(w, buf_pos, line0, line1, n_lines);
        w->col_widths[w->ncols++] = width;
        if (format_block(w, buf_pos, line0, this, n_lines) != 0)
            return -1;
        w->ncols--;
        line0 = this;
    }
    return write_lines(w, buf_pos, line0, line1, n_lines);
}

static int flush_lines(neverc_tabwriter_t *w) {
    int n_lines = (w->nlines < NEVERC_TABWRITER_MAX_LINES)
        ? w->nlines + 1 : NEVERC_TABWRITER_MAX_LINES;
    size_t buf_pos = 0;
    w->ncols = 0;
    return format_block(w, &buf_pos, 0, n_lines, n_lines);
}

static int out_terminate(neverc_tabwriter_t *w) {
    if (out_reserve(w, 0) != 0) return -1;
    w->out_buf[w->out_len] = '\0';
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
    /* Go Init: tab padding forces left alignment. */
    if (w->padchar == '\t')
        flags &= ~NEVERC_TABWRITER_ALIGN_RIGHT;
    w->flags = flags;
    w->nlines = 0;
    begin_line(w);
}

static size_t find_delim(const char *p, size_t n, char *which) {
    const char *t = (const char *)memchr(p, '\t', n);
    const char *v = (const char *)memchr(p, '\v', n);
    const char *nl = (const char *)memchr(p, '\n', n);
    const char *f = (const char *)memchr(p, '\f', n);
    const char *best = NULL;
    char ch = 0;
    if (t && (!best || t < best)) { best = t; ch = '\t'; }
    if (v && (!best || v < best)) { best = v; ch = '\v'; }
    if (nl && (!best || nl < best)) { best = nl; ch = '\n'; }
    if (f && (!best || f < best)) { best = f; ch = '\f'; }
    *which = ch;
    return best ? (size_t)(best - p) : n;
}

static int append_run(neverc_tabwriter_t *w, const char *data, size_t run) {
    if (!run) return 0;
    size_t space = (w->buf_len < NEVERC_TABWRITER_MAX_BUF)
                 ? (size_t)NEVERC_TABWRITER_MAX_BUF - w->buf_len : 0;
    size_t take = run < space ? run : space;
    if (take) {
        memcpy(w->buf + w->buf_len, data, take);
        w->buf_len += take;
    }
    return 0;
}

void neverc_tabwriter_write(neverc_tabwriter_t *w, const char *data, size_t len) {
    if (!w) return;
    if ((!data && len != 0) || w->failed) {
        w->failed = 1;
        return;
    }
    /* Go treats '\t' as a hard tab, '\v' as a soft tab, and both '\n' and
     * '\f' as line breaks. '\f' also flushes the current column block. */
    size_t i = 0;
    while (i < len) {
        char delim = 0;
        size_t rel = find_delim(data + i, len - i, &delim);
        if (append_run(w, data + i, rel) != 0) return;
        i += rel;
        if (!delim) break;

        if (delim == '\t' || delim == '\v') {
            if (w->buf_len < NEVERC_TABWRITER_MAX_BUF)
                end_cell(w, delim == '\t');
            if (w->failed) return;
        } else {
            end_cell(w, 0);
            if (w->failed) return;
            w->nlines++;
            if (w->nlines < NEVERC_TABWRITER_MAX_LINES)
                begin_line(w);
            if (delim == '\f') {
                if (flush_lines(w) != 0) {
                    w->failed = 1;
                    return;
                }
                if ((w->flags & NEVERC_TABWRITER_DEBUG) &&
                    out_append(w, "---\n", 4) != 0) {
                    w->failed = 1;
                    return;
                }
                if (out_terminate(w) != 0) return;
                reset_cells(w);
            }
        }
        i++;
    }
}

void neverc_tabwriter_flush(neverc_tabwriter_t *w) {
    if (!w || w->failed) return;
    /* Go flush: only terminate the incomplete cell when it has bytes.
     * A trailing htab must remain the last cell of the line (not a column). */
    if (w->buf_len > (size_t)w->buf_carved)
        end_cell(w, 0);
    if (flush_lines(w) != 0) {
        w->failed = 1;
        return;
    }
    if (out_terminate(w) != 0)
        return;
    reset_cells(w);
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
