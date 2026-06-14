/*
 * NeverC encoding/csv — CSV reader/writer (RFC 4180 compatible).
 * Supports quoted fields, escaped quotes (""), configurable delimiters.
 */

#include "neverc/std/encoding/csv.h"
#include <string.h>

static int needs_quoting(const char *s, char delim, int use_crlf) {
    (void)use_crlf; /* \r and \n always force quoting regardless of line ending */
    for (const char *p = s; *p; p++) {
        if (*p == delim || *p == '"' || *p == '\n' || *p == '\r')
            return 1;
    }
    return 0;
}

/* ---- Reader ---- */

int neverc_csv_read_line(const char *line, size_t line_len,
                         const char **fields, int max_fields,
                         char *work_buf, size_t work_buf_len,
                         const neverc_csv_reader_opts_t *opts) {
    char delim = (opts && opts->delimiter) ? opts->delimiter : ',';
    int trim = (opts && opts->trim_leading_space) ? 1 : 0;
    (void)(opts && opts->lazy_quotes);

    int nfields = 0;
    size_t wpos = 0;
    size_t i = 0;

    /* strip trailing newline */
    while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r'))
        line_len--;

    if (line_len == 0) return 0;

    for (;;) {
        if (nfields >= max_fields) break;

        if (trim) {
            while (i < line_len && (line[i] == ' ' || line[i] == '\t'))
                i++;
        }

        fields[nfields] = work_buf + wpos;

        if (i < line_len && line[i] == '"') {
            i++;
            /* Quoted field: '"' is the only special byte. */
            for (;;) {
                if (i >= line_len) break;                /* unterminated: end of line */
                if (line[i] == '"') {
                    if (i + 1 < line_len && line[i + 1] == '"') {
                        if (wpos >= work_buf_len) return -1;
                        work_buf[wpos++] = '"';
                        i += 2;                          /* escaped doubled quote */
                        continue;
                    }
                    i++;
                    break;                               /* closing quote */
                }
                /* Run of ordinary bytes up to the next quote. Copy a short window
                 * inline (scan and copy fused in one pass, as the old code did),
                 * so escape-dense fields pay no extra cost; only a run still
                 * unbroken after the window escalates to memchr + memcpy, so a
                 * long quoted cell moves at SIMD speed. */
                size_t remain = line_len - i;
                size_t k = 0;
                while (k < 16 && k < remain && line[i + k] != '"') {
                    if (wpos >= work_buf_len) return -1;
                    work_buf[wpos++] = line[i + k];
                    k++;
                }
                i += k;
                if (k == 16) {                           /* long run -> bulk copy */
                    const char *start = line + i;
                    size_t rem2 = line_len - i;
                    const char *q = (const char *)memchr(start, '"', rem2);
                    size_t run = q ? (size_t)(q - start) : rem2;
                    if (run > work_buf_len - wpos) return -1;
                    memcpy(work_buf + wpos, start, run);
                    wpos += run;
                    i += run;
                    if (!q) break;                       /* unterminated: end of line */
                } else if (k == remain) {
                    break;                               /* ran off the end */
                }
            }
        } else {
            /* Unquoted field: jump to the delimiter with memchr (SIMD) and copy
             * the whole run at once instead of one byte at a time. */
            const char *start = line + i;
            size_t remain = line_len - i;
            const char *d = (const char *)memchr(start, delim, remain);
            size_t flen = d ? (size_t)(d - start) : remain;
            if (flen > work_buf_len - wpos) return -1;
            memcpy(work_buf + wpos, start, flen);
            wpos += flen;
            i += flen;
        }

        if (wpos >= work_buf_len) return -1;
        work_buf[wpos++] = '\0';
        nfields++;

        if (i >= line_len) break;
        if (line[i] == delim) {
            i++;
            /* trailing delimiter produces an extra empty field */
            if (i >= line_len) {
                if (nfields < max_fields) {
                    fields[nfields] = work_buf + wpos;
                    if (wpos >= work_buf_len) return -1;
                    work_buf[wpos++] = '\0';
                    nfields++;
                }
                break;
            }
        }
    }

    return nfields;
}

int neverc_csv_read_all(const char *data, size_t data_len,
                        const char ***records, int **field_counts,
                        int max_records,
                        char *work_buf, size_t work_buf_len,
                        const neverc_csv_reader_opts_t *opts) {
    char comment = (opts && opts->comment) ? opts->comment : 0;
    int nrecords = 0;
    size_t pos = 0;

    static const char *field_ptrs[NEVERC_CSV_MAX_FIELDS];

    while (pos < data_len && nrecords < max_records) {
        /* find end of line */
        size_t line_start = pos;
        int in_quote = 0;
        while (pos < data_len) {
            if (data[pos] == '"') in_quote = !in_quote;
            if (!in_quote && (data[pos] == '\n' || data[pos] == '\r')) break;
            pos++;
        }
        size_t line_len = pos - line_start;

        /* skip newline(s) */
        if (pos < data_len && data[pos] == '\r') pos++;
        if (pos < data_len && data[pos] == '\n') pos++;

        /* skip empty lines and comment lines */
        if (line_len == 0) continue;
        if (comment && data[line_start] == comment) continue;

        int nf = neverc_csv_read_line(data + line_start, line_len,
                                       field_ptrs, NEVERC_CSV_MAX_FIELDS,
                                       work_buf, work_buf_len, opts);
        if (nf < 0) return -1;

        records[nrecords] = (const char **)field_ptrs;
        field_counts[nrecords] = (int *)(size_t)nf;
        nrecords++;

        /* advance work_buf */
        size_t used = 0;
        for (int i = 0; i < nf; i++)
            used += strlen(field_ptrs[i]) + 1;
        work_buf += used;
        if (used > work_buf_len) return -1;
        work_buf_len -= used;
    }

    return nrecords;
}

/* ---- Writer ---- */

int neverc_csv_write_record(const char **fields, int nfields,
                            char *dst, size_t dst_len,
                            const neverc_csv_writer_opts_t *opts) {
    char delim = (opts && opts->delimiter) ? opts->delimiter : ',';
    int crlf = (opts && opts->use_crlf) ? 1 : 0;
    size_t pos = 0;

    for (int i = 0; i < nfields; i++) {
        if (i > 0) {
            if (pos >= dst_len) return -1;
            dst[pos++] = delim;
        }

        const char *f = fields[i];
        if (needs_quoting(f, delim, crlf)) {
            /* The common quoted field (forced by a comma or newline) carries no
             * embedded '"', so its body copies verbatim — wrap it in a single
             * memcpy. Only when a '"' is actually present fall back to the
             * byte-at-a-time path that doubles each quote, which keeps that
             * (rarer, quote-dense) case at its original speed. strchr avoids a
             * length scan on the quoted path. */
            if (!strchr(f, '"')) {
                size_t flen = strlen(f);
                if (pos + flen + 2 > dst_len) return -1;
                dst[pos++] = '"';
                memcpy(dst + pos, f, flen);
                pos += flen;
                dst[pos++] = '"';
            } else {
                if (pos >= dst_len) return -1;
                dst[pos++] = '"';
                for (const char *p = f; *p; p++) {
                    if (*p == '"') {
                        if (pos + 1 >= dst_len) return -1;
                        dst[pos++] = '"';
                        dst[pos++] = '"';
                    } else {
                        if (pos >= dst_len) return -1;
                        dst[pos++] = *p;
                    }
                }
                if (pos >= dst_len) return -1;
                dst[pos++] = '"';
            }
        } else {
            size_t flen = strlen(f);
            if (pos + flen > dst_len) return -1;
            memcpy(dst + pos, f, flen);
            pos += flen;
        }
    }

    if (crlf) {
        if (pos + 2 > dst_len) return -1;
        dst[pos++] = '\r';
        dst[pos++] = '\n';
    } else {
        if (pos + 1 > dst_len) return -1;
        dst[pos++] = '\n';
    }

    return (int)pos;
}

int neverc_csv_write_all(const char ***records, const int *field_counts,
                         int nrecords,
                         char *dst, size_t dst_len,
                         const neverc_csv_writer_opts_t *opts) {
    size_t total = 0;
    for (int i = 0; i < nrecords; i++) {
        int n = neverc_csv_write_record(records[i], field_counts[i],
                                        dst + total, dst_len - total, opts);
        if (n < 0) return -1;
        total += (size_t)n;
    }
    return (int)total;
}
