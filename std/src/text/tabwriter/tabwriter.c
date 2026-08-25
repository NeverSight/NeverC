#include "neverc/std/text/tabwriter.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Keep the released writer object layout fixed.  The output allocation owns
 * a private trailer containing state added after v3389.1.4 and the allocation
 * for cells beyond the released 256-cell inline table:
 *
 *     [ output bytes (out_cap, size_t-aligned) ][ tabwriter_private_t ]
 *
 * The trailer moves with the output allocation.  This makes state lifetime
 * exactly match out_buf, needs no process-global registry, and keeps distinct
 * writers independent when used by different threads. */
#define TABWRITER_INLINE_CELLS NEVERC_TABWRITER_MAX_COLS
#define TABWRITER_EXTRA_CELLS \
    (NEVERC_TABWRITER_MAX_CELLS - TABWRITER_INLINE_CELLS)
#define TABWRITER_META_MAGIC UINT64_C(0x5441425752495445)

typedef struct {
    uint64_t magic;
    int failed;
    unsigned char end_char; /* ESCAPE, '>', or ';' while inside a segment */
    int cell_width;
    size_t width_pos;
    int ncells;
    neverc_tabwriter_cell_t *extra_cells;
    size_t extra_capacity;
} tabwriter_private_t;

static tabwriter_private_t *writer_private(neverc_tabwriter_t *w) {
    if (!w || !w->out_buf || w->out_cap == SIZE_MAX)
        return NULL;
    return (tabwriter_private_t *)(void *)(w->out_buf + w->out_cap);
}

static const tabwriter_private_t *writer_private_const(
    const neverc_tabwriter_t *w) {
    if (!w || !w->out_buf || w->out_cap == SIZE_MAX)
        return NULL;
    return (const tabwriter_private_t *)(const void *)(w->out_buf + w->out_cap);
}

static int writer_ensure_private(neverc_tabwriter_t *w) {
    if (!w || w->out_cap == SIZE_MAX)
        return -1;
    tabwriter_private_t *state = writer_private(w);
    if (state)
        return state->magic == TABWRITER_META_MAGIC ? 0 : -1;

    /* Use an aligned initial output capacity so the following trailer remains
     * correctly aligned on every supported data model. */
    const size_t cap = _Alignof(tabwriter_private_t);
    if (sizeof(tabwriter_private_t) > SIZE_MAX - cap) {
        w->out_cap = SIZE_MAX;
        return -1;
    }
    char *block = (char *)realloc(NULL, cap + sizeof(tabwriter_private_t));
    if (!block) {
        w->out_cap = SIZE_MAX; /* allocation-free sticky failure sentinel */
        return -1;
    }
    w->out_buf = block;
    w->out_cap = cap;
    state = writer_private(w);
    memset(state, 0, sizeof(*state));
    state->magic = TABWRITER_META_MAGIC;
    return 0;
}

static int writer_failed(const neverc_tabwriter_t *w) {
    const tabwriter_private_t *state = writer_private_const(w);
    return !w || w->out_cap == SIZE_MAX ||
           (state && (state->magic != TABWRITER_META_MAGIC || state->failed));
}

static void writer_fail(neverc_tabwriter_t *w) {
    if (!w) return;
    tabwriter_private_t *state = writer_private(w);
    if (state && state->magic == TABWRITER_META_MAGIC)
        state->failed = 1;
    else if (!w->out_buf)
        w->out_cap = SIZE_MAX;
}

static int out_reserve(neverc_tabwriter_t *w, size_t extra) {
    if (writer_ensure_private(w) != 0 || writer_failed(w)) return -1;
    if (w->out_len == SIZE_MAX ||
        extra > SIZE_MAX - w->out_len - 1U) {
        writer_fail(w);
        return -1;
    }
    size_t needed = w->out_len + extra + 1U;
    if (needed <= w->out_cap) return 0;

    const size_t maxcap = SIZE_MAX - sizeof(tabwriter_private_t);
    if (needed > maxcap) {
        writer_fail(w);
        return -1;
    }
    size_t newcap = w->out_cap;
    while (newcap < needed) {
        if (newcap > maxcap / 2U) {
            newcap = needed;
            const size_t alignment = _Alignof(tabwriter_private_t);
            size_t rem = newcap % alignment;
            if (rem != 0) {
                size_t add = alignment - rem;
                if (newcap > maxcap - add) {
                    writer_fail(w);
                    return -1;
                }
                newcap += add;
            }
            break;
        }
        newcap *= 2U;
    }

    size_t oldcap = w->out_cap;
    char *nb = (char *)realloc(
        w->out_buf, newcap + sizeof(tabwriter_private_t));
    if (!nb) {
        writer_fail(w);
        return -1;
    }
    memmove(nb + newcap, nb + oldcap, sizeof(tabwriter_private_t));
    w->out_buf = nb;
    w->out_cap = newcap;
    return 0;
}

static int out_append(neverc_tabwriter_t *w, const char *data, size_t len) {
    if (len == 0) return writer_failed(w) ? -1 : 0;
    if (!data || out_reserve(w, len) != 0) {
        writer_fail(w);
        return -1;
    }
    memcpy(w->out_buf + w->out_len, data, len);
    w->out_len += len;
    return 0;
}

static int out_pad(neverc_tabwriter_t *w, size_t count, char ch) {
    if (count == 0) return writer_failed(w) ? -1 : 0;
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

static int total_cells(const neverc_tabwriter_t *w) {
    const tabwriter_private_t *state = writer_private_const(w);
    return state && state->magic == TABWRITER_META_MAGIC
        ? state->ncells : w->ncells;
}

static neverc_tabwriter_cell_t *cell_at(neverc_tabwriter_t *w, int index) {
    if (index < 0 || index >= NEVERC_TABWRITER_MAX_CELLS)
        return NULL;
    if (index < TABWRITER_INLINE_CELLS)
        return &w->cells[index];
    tabwriter_private_t *state = writer_private(w);
    if (!state || state->magic != TABWRITER_META_MAGIC)
        return NULL;
    size_t extra_index = (size_t)(index - TABWRITER_INLINE_CELLS);
    return extra_index < state->extra_capacity
        ? &state->extra_cells[extra_index] : NULL;
}

static neverc_tabwriter_cell_t *cell_for_append(
    neverc_tabwriter_t *w, int index) {
    if (index < TABWRITER_INLINE_CELLS)
        return cell_at(w, index);
    if (index < 0 || index >= NEVERC_TABWRITER_MAX_CELLS)
        return NULL;
    tabwriter_private_t *state = writer_private(w);
    if (!state || state->magic != TABWRITER_META_MAGIC)
        return NULL;
    size_t extra_index = (size_t)(index - TABWRITER_INLINE_CELLS);
    if (extra_index >= state->extra_capacity) {
        size_t new_capacity = state->extra_capacity
            ? state->extra_capacity * 2U : TABWRITER_INLINE_CELLS;
        if (new_capacity > TABWRITER_EXTRA_CELLS)
            new_capacity = TABWRITER_EXTRA_CELLS;
        if (new_capacity <= extra_index ||
            new_capacity > SIZE_MAX / sizeof(*state->extra_cells)) {
            writer_fail(w);
            return NULL;
        }
        neverc_tabwriter_cell_t *cells =
            (neverc_tabwriter_cell_t *)realloc(
                state->extra_cells,
                new_capacity * sizeof(*state->extra_cells));
        if (!cells) {
            writer_fail(w);
            return NULL;
        }
        state->extra_cells = cells;
        state->extra_capacity = new_capacity;
    }
    return &state->extra_cells[extra_index];
}

static void begin_line(neverc_tabwriter_t *w) {
    if (w->nlines < NEVERC_TABWRITER_MAX_LINES) {
        w->lines_start[w->nlines] = total_cells(w);
        w->lines_ncells[w->nlines] = 0;
    }
}

static void update_width(neverc_tabwriter_t *w) {
    tabwriter_private_t *state = writer_private(w);
    if (!state || state->magic != TABWRITER_META_MAGIC) {
        writer_fail(w);
        return;
    }
    if (state->width_pos < w->buf_len) {
        state->cell_width += rune_width(w->buf + state->width_pos,
                                       w->buf_len - state->width_pos);
        state->width_pos = w->buf_len;
    }
}

static void end_escape(neverc_tabwriter_t *w) {
    tabwriter_private_t *state = writer_private(w);
    if (!state || state->magic != TABWRITER_META_MAGIC) {
        writer_fail(w);
        return;
    }
    switch (state->end_char) {
    case (unsigned char)NEVERC_TABWRITER_ESCAPE:
        update_width(w);
        /* Escape bytes are in the buffer unless StripEscape; never count them. */
        if ((w->flags & NEVERC_TABWRITER_STRIP_ESCAPE) == 0) {
            state->cell_width -= 2;
            if (state->cell_width < 0) state->cell_width = 0;
        }
        break;
    case '>':
        state->width_pos = w->buf_len;
        break;
    case ';':
        state->cell_width += 1;
        state->width_pos = w->buf_len;
        break;
    default:
        break;
    }
    state->end_char = 0;
}

static void end_cell(neverc_tabwriter_t *w, int htab) {
    if (writer_failed(w)) return;
    tabwriter_private_t *state = writer_private(w);
    if (!state || state->magic != TABWRITER_META_MAGIC) {
        writer_fail(w);
        return;
    }
    if (state->ncells >= NEVERC_TABWRITER_MAX_CELLS ||
        w->nlines >= NEVERC_TABWRITER_MAX_LINES) {
        writer_fail(w);
        return;
    }
    neverc_tabwriter_cell_t *cell = cell_for_append(w, state->ncells);
    if (!cell) {
        writer_fail(w);
        return;
    }
    /* Cells partition w->buf contiguously, so the start of this cell is the
     * sum of all previously carved cell sizes. Track that running total in
     * buf_carved instead of re-summing every cell on each call. */
    int start = w->buf_carved;
    update_width(w);
    if (writer_failed(w)) return;
    cell->size = (int)w->buf_len - start;
    cell->width = state->cell_width;
    cell->htab = htab;
    w->buf_carved += cell->size;
    state->ncells++;
    w->ncells = state->ncells < TABWRITER_INLINE_CELLS
        ? state->ncells : TABWRITER_INLINE_CELLS;
    w->lines_ncells[w->nlines]++;
    state->cell_width = 0;
    state->width_pos = w->buf_len;
}

static void reset_cells(neverc_tabwriter_t *w) {
    tabwriter_private_t *state = writer_private(w);
    if (!state || state->magic != TABWRITER_META_MAGIC) {
        writer_fail(w);
        return;
    }
    w->buf_len = 0;
    w->buf_carved = 0;
    w->ncells = 0;
    w->nlines = 0;
    w->ncols = 0;
    state->ncells = 0;
    state->cell_width = 0;
    state->width_pos = 0;
    state->end_char = 0;
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
        if (n > INT_MAX - (tw - 1)) return -1;
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
            neverc_tabwriter_cell_t *stored = cell_at(w, start + j);
            if (!stored) return -1;
            /* Output growth relocates the private trailer, so never retain a
             * pointer to an overflow cell across out_append/out_pad. */
            neverc_tabwriter_cell_t cell = *stored;
            const neverc_tabwriter_cell_t *c = &cell;
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
            neverc_tabwriter_cell_t *c = cell_at(
                w, line_start(w, this) + column);
            if (!c) return -1;
            if (pad > 0 && c->width > INT_MAX - pad)
                return -1;
            int ww = c->width + pad;
            if (ww > width) width = ww;
            if (c->width > 0 || c->htab)
                discardable = 0;
        }
        if (discardable && (w->flags & NEVERC_TABWRITER_DISCARD_EMPTY_COLS))
            width = 0;

        if (w->ncols >= NEVERC_TABWRITER_MAX_COLS)
            return -1;
        w->col_widths[w->ncols++] = width;
        if (format_block(w, buf_pos, line0, this, n_lines) != 0)
            return -1;
        w->ncols--;
        line0 = this;
    }
    return write_lines(w, buf_pos, line0, line1, n_lines);
}

static int flush_lines(neverc_tabwriter_t *w) {
    int all_lines_terminated =
        w->nlines == NEVERC_TABWRITER_MAX_LINES;
    int n_lines = (w->nlines < NEVERC_TABWRITER_MAX_LINES)
        ? w->nlines + 1 : NEVERC_TABWRITER_MAX_LINES;
    size_t buf_pos = 0;
    w->ncols = 0;
    if (format_block(w, &buf_pos, 0, n_lines, n_lines) != 0)
        return -1;
    /* At the exact line-table limit there is no slot for the usual empty
     * trailing line, so write_lines cannot infer the final delimiter. */
    if (all_lines_terminated && out_append(w, "\n", 1) != 0)
        return -1;
    return 0;
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

static size_t find_special(const char *p, size_t n, unsigned flags, char *which) {
    const char *best = NULL;
    char ch = 0;
    const char *t = (const char *)memchr(p, '\t', n);
    const char *v = (const char *)memchr(p, '\v', n);
    const char *nl = (const char *)memchr(p, '\n', n);
    const char *f = (const char *)memchr(p, '\f', n);
    const char *esc = (const char *)memchr(p, (char)NEVERC_TABWRITER_ESCAPE, n);
    const char *lt = (flags & NEVERC_TABWRITER_FILTER_HTML)
        ? (const char *)memchr(p, '<', n) : NULL;
    const char *amp = (flags & NEVERC_TABWRITER_FILTER_HTML)
        ? (const char *)memchr(p, '&', n) : NULL;
    if (t && (!best || t < best)) { best = t; ch = '\t'; }
    if (v && (!best || v < best)) { best = v; ch = '\v'; }
    if (nl && (!best || nl < best)) { best = nl; ch = '\n'; }
    if (f && (!best || f < best)) { best = f; ch = '\f'; }
    if (esc && (!best || esc < best)) { best = esc; ch = NEVERC_TABWRITER_ESCAPE; }
    if (lt && (!best || lt < best)) { best = lt; ch = '<'; }
    if (amp && (!best || amp < best)) { best = amp; ch = '&'; }
    *which = ch;
    return best ? (size_t)(best - p) : n;
}

static int append_run(neverc_tabwriter_t *w, const char *data, size_t run) {
    if (!run) return writer_failed(w) ? -1 : 0;
    if (writer_failed(w)) return -1;
    size_t space = (w->buf_len < NEVERC_TABWRITER_MAX_BUF)
                 ? (size_t)NEVERC_TABWRITER_MAX_BUF - w->buf_len : 0;
    size_t take = run < space ? run : space;
    if (take) {
        memcpy(w->buf + w->buf_len, data, take);
        w->buf_len += take;
    }
    if (take < run) {
        writer_fail(w);
        return -1;
    }
    return 0;
}

static void finish_line(neverc_tabwriter_t *w, char delim) {
    end_cell(w, 0);
    if (writer_failed(w)) return;
    w->nlines++;
    if (w->nlines < NEVERC_TABWRITER_MAX_LINES)
        begin_line(w);
    if (delim != '\f') return;
    if (flush_lines(w) != 0) {
        writer_fail(w);
        return;
    }
    if ((w->flags & NEVERC_TABWRITER_DEBUG) &&
        out_append(w, "---\n", 4) != 0) {
        writer_fail(w);
        return;
    }
    if (out_terminate(w) != 0) return;
    reset_cells(w);
}

void neverc_tabwriter_write(neverc_tabwriter_t *w, const char *data, size_t len) {
    if (!w) return;
    if (writer_ensure_private(w) != 0) return;
    if ((!data && len != 0) || writer_failed(w)) {
        writer_fail(w);
        return;
    }
    /* Go: '\t' hard tab, '\v' soft tab, '\n'/'\f' line breaks. '\f' flushes.
     * Escape (\xff) and, with FilterHTML, <tags> / &entities; hide delimiters. */
    size_t i = 0;
    while (i < len) {
        char special = 0;
        size_t rel;
        tabwriter_private_t *state = writer_private(w);
        if (!state || state->magic != TABWRITER_META_MAGIC) {
            writer_fail(w);
            return;
        }
        if (state->end_char) {
            const char *hit = (const char *)memchr(
                data + i, (char)state->end_char, len - i);
            rel = hit ? (size_t)(hit - (data + i)) : (len - i);
            special = hit ? (char)state->end_char : 0;
        } else {
            rel = find_special(data + i, len - i, w->flags, &special);
        }

        if (state->end_char) {
            size_t take = rel;
            if (special) {
                if (!(special == NEVERC_TABWRITER_ESCAPE &&
                      (w->flags & NEVERC_TABWRITER_STRIP_ESCAPE)))
                    take++;
            }
            if (append_run(w, data + i, take) != 0) return;
            i += rel;
            if (!special) break;
            end_escape(w);
            i++;
            continue;
        }

        if (append_run(w, data + i, rel) != 0) return;
        i += rel;
        if (!special) break;

        if (special == '\t' || special == '\v') {
            update_width(w);
            end_cell(w, special == '\t');
            if (writer_failed(w)) return;
        } else if (special == '\n' || special == '\f') {
            update_width(w);
            finish_line(w, special);
            if (writer_failed(w)) return;
        } else if (special == NEVERC_TABWRITER_ESCAPE) {
            update_width(w);
            /* Consume the opener here. Searching from it would treat it as
             * the closer. Keep the byte unless StripEscape. */
            if ((w->flags & NEVERC_TABWRITER_STRIP_ESCAPE) == 0 &&
                append_run(w, data + i, 1) != 0)
                return;
            i++;
            state = writer_private(w);
            if (!state || state->magic != TABWRITER_META_MAGIC) {
                writer_fail(w);
                return;
            }
            state->end_char = (unsigned char)NEVERC_TABWRITER_ESCAPE;
            continue;
        } else {
            update_width(w);
            state = writer_private(w);
            if (!state || state->magic != TABWRITER_META_MAGIC) {
                writer_fail(w);
                return;
            }
            state->end_char = (special == '<') ? (unsigned char)'>'
                                                : (unsigned char)';';
            continue;
        }
        i++;
    }
}

void neverc_tabwriter_flush(neverc_tabwriter_t *w) {
    if (!w || writer_ensure_private(w) != 0 || writer_failed(w)) return;
    tabwriter_private_t *state = writer_private(w);
    if (!state || state->magic != TABWRITER_META_MAGIC) {
        writer_fail(w);
        return;
    }
    if (state->end_char)
        end_escape(w);
    /* Go flush: only terminate the incomplete cell when it has bytes.
     * A trailing htab must remain the last cell of the line (not a column). */
    if (w->buf_len > (size_t)w->buf_carved)
        end_cell(w, 0);
    if (flush_lines(w) != 0) {
        writer_fail(w);
        return;
    }
    if (out_terminate(w) != 0)
        return;
    reset_cells(w);
}

const char *neverc_tabwriter_output(const neverc_tabwriter_t *w, size_t *len) {
    int failed = writer_failed(w);
    if (len) *len = failed ? 0 : w->out_len;
    return failed ? NULL : w->out_buf;
}

void neverc_tabwriter_reset(neverc_tabwriter_t *w) {
    if (!w) return;
    int mw = w->minwidth, tw = w->tabwidth, p = w->padding;
    char pc = w->padchar;
    unsigned f = w->flags;
    if (w->out_buf) {
        tabwriter_private_t *state = writer_private(w);
        if (state && state->magic == TABWRITER_META_MAGIC) {
            free(state->extra_cells);
            state->extra_cells = NULL;
            state->extra_capacity = 0;
            state->magic = 0;
        }
        free(w->out_buf);
    }
    neverc_tabwriter_init(w, mw, tw, p, pc, f);
}
