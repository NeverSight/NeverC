/*
 * A/B benchmark + correctness check: text/tabwriter elastic-tabstop renderer.
 *
 *  - old_* — the previous library code, reproduced verbatim:
 *      neverc_tabwriter_write classified and stored input one byte at a time;
 *      out_pad emitted padding one byte at a time; end_cell re-summed every
 *      previously carved cell's size to find the current cell's start.
 *
 *  - neverc_tabwriter_* (library) — now scans a line at a time with memchr and
 *      bulk-copies each tab-separated run with memcpy, fills padding with a
 *      single memset, and tracks the carved-bytes running total in O(1).
 *
 * Every change is behavior-preserving, so each input is rendered with BOTH the
 * reproduced old code and the live library across a matrix of configs and the
 * emitted bytes must be identical before any timing is reported.
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -I std/include -o /tmp/tabwriter_bench \
 *      tests/neverc/std/tabwriter_bench.c std/src/text/tabwriter/tabwriter.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/text/tabwriter.h"

/* ============================================================
 * OLD implementation — verbatim reproduction (pre-optimization)
 * ============================================================ */
#define OLD_MAX_COLS NEVERC_TABWRITER_MAX_COLS
#define OLD_MAX_BUF  NEVERC_TABWRITER_MAX_BUF

typedef struct { int size; int width; int htab; } old_cell_t;

typedef struct {
    char   *out_buf;
    size_t  out_len;
    size_t  out_cap;
    int     minwidth;
    int     tabwidth;
    int     padding;
    char    padchar;
    unsigned flags;
    char    buf[OLD_MAX_BUF];
    size_t  buf_len;
    old_cell_t cells[OLD_MAX_COLS];
    int     ncells;
    int     col_widths[OLD_MAX_COLS];
    int     ncols;
    int     lines_start[4096];
    int     lines_ncells[4096];
    int     nlines;
} old_tw_t;

static void old_out_append(old_tw_t *w, const char *data, size_t len) {
    while (w->out_len + len >= w->out_cap) {
        size_t newcap = w->out_cap ? w->out_cap * 2 : 1024;
        char *nb = (char *)malloc(newcap);
        if (w->out_buf) { memcpy(nb, w->out_buf, w->out_len); free(w->out_buf); }
        w->out_buf = nb;
        w->out_cap = newcap;
    }
    memcpy(w->out_buf + w->out_len, data, len);
    w->out_len += len;
}

static void old_out_pad(old_tw_t *w, int count, char ch) {
    for (int i = 0; i < count; i++)
        old_out_append(w, &ch, 1);
}

static int old_rune_width(const char *s, size_t len) {
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

static void old_begin_line(old_tw_t *w) {
    if (w->nlines < 4096) {
        w->lines_start[w->nlines] = w->ncells;
        w->lines_ncells[w->nlines] = 0;
    }
}

static void old_end_cell(old_tw_t *w, int htab) {
    if (w->nlines < 4096 && w->ncells < OLD_MAX_COLS) {
        old_cell_t *cell = &w->cells[w->ncells];
        int start = 0;
        if (w->ncells > 0) {
            int prev_end = 0;
            for (int i = 0; i < w->ncells; i++)
                prev_end += w->cells[i].size;
            start = prev_end;
        } else {
            start = 0;
        }
        cell->size = (int)w->buf_len - start;
        cell->width = old_rune_width(w->buf + start, (size_t)cell->size);
        cell->htab = htab;
        w->ncells++;
        w->lines_ncells[w->nlines]++;
    }
}

static void old_flush_lines(old_tw_t *w) {
    memset(w->col_widths, 0, sizeof(w->col_widths));
    w->ncols = 0;

    for (int ln = 0; ln <= w->nlines; ln++) {
        int start = w->lines_start[ln];
        int nc = w->lines_ncells[ln];
        for (int c = 0; c < nc - 1; c++) {
            int col = c;
            int cw = w->cells[start + c].width;
            if (col >= OLD_MAX_COLS) break;
            if (cw > w->col_widths[col]) w->col_widths[col] = cw;
            if (col + 1 > w->ncols) w->ncols = col + 1;
        }
    }

    int buf_pos = 0;
    for (int ln = 0; ln <= w->nlines; ln++) {
        int start = w->lines_start[ln];
        int nc = w->lines_ncells[ln];
        for (int c = 0; c < nc; c++) {
            old_cell_t *cell = &w->cells[start + c];
            old_out_append(w, w->buf + buf_pos, (size_t)cell->size);

            if (c < nc - 1) {
                int col = c;
                int pad_needed = 0;
                if (col < w->ncols) pad_needed = w->col_widths[col] - cell->width + w->padding;
                else pad_needed = w->padding;
                if (pad_needed < w->padding) pad_needed = w->padding;

                if (cell->htab && w->tabwidth > 0) {
                    int tw = w->tabwidth;
                    int total = cell->width + pad_needed;
                    int aligned = ((total + tw - 1) / tw) * tw;
                    pad_needed = aligned - cell->width;
                    if (pad_needed < w->padding) pad_needed = w->padding;
                }

                if (w->flags & NEVERC_TABWRITER_ALIGN_RIGHT) {
                    char tmp[4096];
                    int cell_start_in_out = (int)w->out_len - cell->size;
                    if (cell_start_in_out >= 0 && cell->size <= (int)sizeof(tmp)) {
                        memcpy(tmp, w->out_buf + cell_start_in_out, (size_t)cell->size);
                        old_out_pad(w, pad_needed, w->padchar);
                        w->out_len = (size_t)cell_start_in_out;
                        old_out_pad(w, pad_needed, w->padchar);
                        old_out_append(w, tmp, (size_t)cell->size);
                    }
                } else {
                    old_out_pad(w, pad_needed, w->padchar);
                }
            }
            buf_pos += cell->size;
        }
        if (ln < w->nlines) old_out_append(w, "\n", 1);
    }
}

static void old_init(old_tw_t *w, int minwidth, int tabwidth, int padding,
                     char padchar, unsigned flags) {
    memset(w, 0, sizeof(*w));
    w->minwidth = minwidth;
    w->tabwidth = tabwidth;
    w->padding = padding;
    w->padchar = padchar ? padchar : ' ';
    w->flags = flags;
    w->nlines = 0;
    old_begin_line(w);
}

static void old_write(old_tw_t *w, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char ch = data[i];
        if (ch == '\t') {
            if (w->buf_len < OLD_MAX_BUF) old_end_cell(w, 1);
        } else if (ch == '\n') {
            old_end_cell(w, 0);
            w->nlines++;
            if (w->nlines < 4096) old_begin_line(w);
        } else {
            if (w->buf_len < OLD_MAX_BUF) w->buf[w->buf_len++] = ch;
        }
    }
}

static void old_flush(old_tw_t *w) {
    old_end_cell(w, 0);
    old_flush_lines(w);
    if (w->out_len + 1 >= w->out_cap) {
        size_t newcap = w->out_cap ? w->out_cap * 2 : 1024;
        char *nb = (char *)realloc(w->out_buf, newcap);
        if (nb) { w->out_buf = nb; w->out_cap = newcap; }
    }
    if (w->out_buf) w->out_buf[w->out_len] = '\0';
}

/* ============================================================
 * Config matrix + helpers
 * ============================================================ */
typedef struct { int mw, tw, pad; char pc; unsigned flags; const char *name; } cfg_t;

static const cfg_t CFGS[] = {
    {1, 8, 1, ' ', 0,                            "def"},
    {1, 8, 2, ' ', 0,                            "pad2"},
    {1, 8, 1, '.', 0,                            "dot"},
    {0, 4, 1, ' ', 0,                            "tw4"},
    {1, 0, 1, ' ', 0,                            "tw0"},
    {1, 8, 0, ' ', 0,                            "pad0"},
    {1, 8, 3, ' ', NEVERC_TABWRITER_ALIGN_RIGHT, "right"},
};
#define NCFG ((int)(sizeof(CFGS)/sizeof(CFGS[0])))

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile size_t sink;

/* Render with the reproduced OLD code; returns malloc'd bytes (caller frees). */
static char *render_old(old_tw_t *w, const cfg_t *c,
                        const char *in, size_t len, size_t *out_len) {
    if (w->out_buf) { free(w->out_buf); }
    old_init(w, c->mw, c->tw, c->pad, c->pc, c->flags);
    old_write(w, in, len);
    old_flush(w);
    size_t L = w->out_len;
    char *r = (char *)malloc(L + 1);
    memcpy(r, w->out_buf ? w->out_buf : "", L);
    r[L] = '\0';
    *out_len = L;
    return r;
}

/* Render with the live LIBRARY; returns malloc'd bytes (caller frees). */
static char *render_new(neverc_tabwriter_t *w, const cfg_t *c,
                        const char *in, size_t len, size_t *out_len) {
    if (w->out_buf) { free(w->out_buf); w->out_buf = NULL; w->out_cap = 0; w->out_len = 0; }
    neverc_tabwriter_init(w, c->mw, c->tw, c->pad, c->pc, c->flags);
    neverc_tabwriter_write(w, in, len);
    neverc_tabwriter_flush(w);
    size_t L;
    const char *o = neverc_tabwriter_output(w, &L);
    char *r = (char *)malloc(L + 1);
    memcpy(r, o ? o : "", L);
    r[L] = '\0';
    *out_len = L;
    return r;
}

static old_tw_t      *OW;   /* reused scratch writers (≈100 KB each) */
static neverc_tabwriter_t *NW;

static int identical_all_cfgs(const char *in, size_t len) {
    for (int ci = 0; ci < NCFG; ci++) {
        size_t lo = 0, ln = 0;
        char *o = render_old(OW, &CFGS[ci], in, len, &lo);
        char *n = render_new(NW, &CFGS[ci], in, len, &ln);
        int ok = (lo == ln) && (memcmp(o, n, lo) == 0);
        free(o); free(n);
        if (!ok) return 0;
    }
    return 1;
}

/* ============================================================
 * Deterministic input generators
 * ============================================================ */
static unsigned rng = 0x12345678u;
static unsigned nextr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* mix of letters, tabs ('\t' ~ pt%), newlines ('\n' ~ pn%) and a few UTF-8
 * multibyte runes (~ pu%) to exercise rune_width parity. */
static char *gen_mixed(size_t approx, int pt, int pn, int pu, size_t *out_len) {
    char *b = (char *)malloc(approx + 8);
    size_t k = 0;
    while (k < approx) {
        unsigned r = nextr() % 100u;
        if ((int)r < pt) b[k++] = '\t';
        else if ((int)r < pt + pn) b[k++] = '\n';
        else if ((int)r < pt + pn + pu && k + 3 < approx) {
            /* 2- or 3-byte UTF-8 */
            if (nextr() & 1) { b[k++] = (char)0xC3; b[k++] = (char)(0x80 + (nextr() % 0x3F)); }
            else { b[k++] = (char)0xE2; b[k++] = (char)0x82; b[k++] = (char)0xAC; }
        } else {
            b[k++] = (char)('a' + (nextr() % 26));
        }
    }
    *out_len = k;
    return b;
}

/* N lines of NCOL tab-separated cells, each cell `cellw` letters wide. */
static char *gen_table(int nrow, int ncol, int cellw, size_t *out_len) {
    /* each cell emits up to cellw+2 letters plus one tab; plus one '\n' per row */
    size_t cap = (size_t)nrow * (size_t)ncol * (size_t)(cellw + 3) + (size_t)nrow + 16;
    char *b = (char *)malloc(cap);
    size_t k = 0;
    for (int r = 0; r < nrow; r++) {
        for (int c = 0; c < ncol; c++) {
            int w = cellw + (int)(nextr() % 3);
            for (int j = 0; j < w; j++) b[k++] = (char)('a' + (nextr() % 26));
            if (c < ncol - 1) b[k++] = '\t';
        }
        if (r < nrow - 1) b[k++] = '\n';
    }
    *out_len = k;
    return b;
}

/* Single string of `n` plain letters (one giant cell, no delimiters). */
static char *gen_plain(size_t n, char fill, size_t *out_len) {
    char *b = (char *)malloc(n + 1);
    memset(b, fill, n);
    *out_len = n;
    return b;
}

/* ============================================================
 * Correctness
 * ============================================================ */
static int correctness(void) {
    static const char *lits[] = {
        "", "\t", "\n", "\t\n", "\n\t", "a", "a\t", "a\n", "a\tb",
        "a\tb\tc\naa\tbb\tcc\naaa\tbbb\tccc",
        "name\tage\tcity\nalice\t30\tNY\nbob\t25\tLA",
        "hello\nworld", "just plain text", "a\t\tb", "\t\t\t", "\n\n\n",
        "trailing\t", "trailing\n", "\rcarriage\ttab\r\n",
        "héllo\twörld\nüber\tcafé",         /* UTF-8 */
        "x\ty\nlonger-first-cell\tz",
    };
    int ok = 0, total = 0;
    for (size_t i = 0; i < sizeof(lits)/sizeof(lits[0]); i++) {
        total++;
        if (identical_all_cfgs(lits[i], strlen(lits[i]))) ok++;
        else printf("  LIT FAIL [%zu] = [%s]\n", i, lits[i]);
    }

    /* randomized mixes across densities and sizes */
    for (int trial = 0; trial < 400; trial++) {
        size_t sz = (size_t)(8 + (nextr() % 4000));
        int pt = 2 + (int)(nextr() % 25);
        int pn = 1 + (int)(nextr() % 12);
        int pu = (int)(nextr() % 6);
        size_t L; char *s = gen_mixed(sz, pt, pn, pu, &L);
        total++;
        if (identical_all_cfgs(s, L)) ok++;
        else printf("  MIX FAIL trial=%d sz=%zu pt=%d pn=%d pu=%d\n", trial, sz, pt, pn, pu);
        free(s);
    }

    /* structured tables (many cells, possibly hitting the 256-cell cap).
     * Kept within tabwriter's supported domain: <4096 lines and <=256 cells. */
    struct { int r, c, w; } tabs[] = {
        {3,3,2},{10,5,4},{1,250,3},{60,4,8},{300,2,5},{40,6,6},{4000,1,3}
    };
    for (size_t i = 0; i < sizeof(tabs)/sizeof(tabs[0]); i++) {
        size_t L; char *s = gen_table(tabs[i].r, tabs[i].c, tabs[i].w, &L);
        total++;
        if (identical_all_cfgs(s, L)) ok++;
        else printf("  TABLE FAIL r=%d c=%d w=%d\n", tabs[i].r, tabs[i].c, tabs[i].w);
        free(s);
    }

    /* buffer-cap stress: inputs larger than MAX_BUF (truncation parity).
     * Newline rate kept low so line count stays under the 4096 cap. */
    {
        size_t L; char *s = gen_mixed(150000, 10, 1, 2, &L);
        total++;
        if (identical_all_cfgs(s, L)) ok++; else printf("  BIGMIX FAIL\n");
        free(s);
    }
    {
        size_t L; char *s = gen_plain(200000, 'q', &L);
        total++;
        if (identical_all_cfgs(s, L)) ok++; else printf("  BIGPLAIN FAIL\n");
        free(s);
    }

    /* OOB-hardening smoke: >4096 lines exceed lines_start[]'s capacity. The old
     * code read past the array here (UB), so this is checked on the live library
     * only — it must render without a crash (verified clean under ASan). */
    {
        size_t L; char *s = gen_table(6000, 1, 4, &L);
        if (NW->out_buf) { free(NW->out_buf); NW->out_buf = NULL; NW->out_cap = 0; NW->out_len = 0; }
        neverc_tabwriter_init(NW, 1, 8, 1, ' ', 0);
        neverc_tabwriter_write(NW, s, L);
        neverc_tabwriter_flush(NW);
        size_t ol; const char *o = neverc_tabwriter_output(NW, &ol);
        total++;
        if (o && ol > 0) ok++; else printf("  OOB-SMOKE FAIL\n");
        free(s);
    }

    printf("edge cases: %d/%d identical\n", ok, total);
    return ok == total;
}

/* ============================================================
 * Timing
 * ============================================================ */
static void bench(const char *label, const cfg_t *c, const char *in, size_t len,
                  const char *note) {
    int iters = (int)(80000000ULL / (len + 1));
    if (iters < 30) iters = 30;
    if (iters > 8000) iters = 8000;

    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            if (OW->out_buf) { free(OW->out_buf); }
            old_init(OW, c->mw, c->tw, c->pad, c->pc, c->flags);
            old_write(OW, in, len);
            old_flush(OW);
            sink = OW->out_len;
        }
        double e = now_sec() - t0; if (e < t_old) t_old = e;

        t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            if (NW->out_buf) { free(NW->out_buf); NW->out_buf = NULL; NW->out_cap = 0; NW->out_len = 0; }
            neverc_tabwriter_init(NW, c->mw, c->tw, c->pad, c->pc, c->flags);
            neverc_tabwriter_write(NW, in, len);
            neverc_tabwriter_flush(NW);
            sink = NW->out_len;
        }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    if (OW->out_buf) { free(OW->out_buf); OW->out_buf = NULL; OW->out_cap = 0; }
    if (NW->out_buf) { free(NW->out_buf); NW->out_buf = NULL; NW->out_cap = 0; }

    printf("%-16s  %9.1f ms  %9.1f ms  %6.2fx   %s\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, note);
}

int main(void) {
    OW = (old_tw_t *)calloc(1, sizeof(*OW));
    NW = (neverc_tabwriter_t *)calloc(1, sizeof(*NW));

    printf("=== text/tabwriter: memchr bulk-copy write + memset pad (new) vs byte loop (old) ===\n");

    int all_ok = correctness();

#ifndef TW_NO_TIMING
    printf("\n%-16s  %9s  %9s  %8s\n", "case", "old", "new", "speedup");
    size_t L;
    char *big   = gen_plain(1u << 20, 'x', &L);                 /* 1 MB, one cell */
    bench("big_text_1M",   &CFGS[0], big, L, "(1 cell, no delims; scan-bound)");
    free(big);

    char *lines = gen_table(64, 1, 1024, &L);                   /* 64 lines, no tabs */
    bench("long_lines",    &CFGS[0], lines, L, "(64x~1KB cells, \\n only)");
    free(lines);

    char *cells = gen_table(32, 4, 400, &L);                    /* long cells, 128 cells */
    bench("long_cells",    &CFGS[0], cells, L, "(32x4 cols, ~400B cells)");
    free(cells);

    char *wide  = gen_table(1, 250, 3, &L);                     /* tab-dense single line */
    bench("tab_dense",     &CFGS[0], wide, L, "(1 line x 250 cols)");
    free(wide);

    char *typ   = gen_table(40, 6, 6, &L);                      /* realistic-ish table */
    bench("typical",       &CFGS[1], typ, L, "(40 rows x 6 cols)");
    bench("typical_right", &CFGS[6], typ, L, "(align-right)");
    free(typ);
#endif

    free(OW->out_buf); free(NW->out_buf);
    free(OW); free(NW);
    printf("\n=== Done%s ===\n", all_ok ? "" : " (MISMATCH!)");
    return all_ok ? 0 : 1;
}
