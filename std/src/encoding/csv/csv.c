/*
 * NeverC encoding/csv — CSV reader/writer (RFC 4180 compatible).
 * Supports quoted fields, escaped quotes (""), configurable delimiters.
 */

#include "neverc/std/encoding/csv.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

static int csv_decode_rune(const unsigned char *b, size_t n,
                           uint32_t *cp, size_t *adv) {
    size_t need, i;
    uint32_t value;
    if (n == 0)
        return -1;
    if (b[0] < 0x80) {
        *cp = b[0];
        *adv = 1;
        return 0;
    }
    if (b[0] >= 0xc2 && b[0] <= 0xdf) {
        value = b[0] & 0x1fU;
        need = 2;
    } else if (b[0] >= 0xe0 && b[0] <= 0xef) {
        value = b[0] & 0x0fU;
        need = 3;
    } else if (b[0] >= 0xf0 && b[0] <= 0xf4) {
        value = b[0] & 0x07U;
        need = 4;
    } else {
        return -1;
    }
    if (need > n)
        return -1;
    for (i = 1; i < need; i++) {
        if ((b[i] & 0xc0U) != 0x80U)
            return -1;
        value = (value << 6U) | (b[i] & 0x3fU);
    }
    if ((need == 3 && value < 0x800) || (need == 4 && value < 0x10000) ||
        value > 0x10ffffU)
        return -1;
    *cp = value;
    *adv = need;
    return 0;
}

static int csv_codepoint_is_space(uint32_t cp) {
    switch (cp) {
    case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
    case 0x85: case 0xa0: case 0x1680:
    case 0x2028: case 0x2029: case 0x202f: case 0x205f: case 0x3000:
        return 1;
    default:
        return cp >= 0x2000 && cp <= 0x200a;
    }
}

static int csv_first_rune(const char *s, uint32_t *cp) {
    size_t n = 0, adv;
    while (n < 4 && s[n])
        n++;
    return csv_decode_rune((const unsigned char *)s, n, cp, &adv);
}

static size_t csv_skip_leading_space(const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        size_t adv;
        if (csv_decode_rune((const unsigned char *)s + i, n - i, &cp, &adv) != 0)
            break;
        if (!csv_codepoint_is_space(cp) || cp == '\n')
            break;
        i += adv;
    }
    return i;
}

static int needs_quoting(const char *s, char delim, int use_crlf) {
    uint32_t first;
    (void)use_crlf; /* \r and \n always force quoting regardless of line ending */
    if (!s || !s[0]) return 0;
    /* Spreadsheet formula prefixes (OWASP CSV Injection). Quoted so Excel /
     * LibreOffice cannot treat a field as `=CMD()` / `-=CMD()` when opened.
     * Tab and CR are already quoted: tab via unicode.IsSpace, CR in the loop. */
    if (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@')
        return 1;
    /* Postgres COPY terminator; quoted so it is not taken as end-of-data. */
    if (s[0] == '\\' && s[1] == '.' && s[2] == '\0')
        return 1;
    for (const char *p = s; *p; p++) {
        if (*p == delim || *p == '"' || *p == '\n' || *p == '\r')
            return 1;
    }
    /* Go encoding/csv quotes a field whose first rune is unicode.IsSpace
     * so readers that trim unquoted leading space (including this package's
     * trim_leading_space) cannot drop it. */
    return csv_first_rune(s, &first) == 0 && csv_codepoint_is_space(first);
}

/* ---- Reader ---- */

int neverc_csv_read_line(const char *line, size_t line_len,
                         const char **fields, int max_fields,
                         char *work_buf, size_t work_buf_len,
                         const neverc_csv_reader_opts_t *opts) {
    char delim = (opts && opts->delimiter) ? opts->delimiter : ',';
    int trim = (opts && opts->trim_leading_space) ? 1 : 0;
    int lazy = (opts && opts->lazy_quotes) ? 1 : 0;
    if ((!line && line_len > 0) || !fields || max_fields <= 0 ||
        (!work_buf && line_len > 0) || delim == '"' ||
        delim == '\r' || delim == '\n')
        return -1;
    /* Fields are exposed as NUL-terminated C strings with no parallel length
     * array, so embedded NUL bytes cannot be represented without truncation. */
    if (line_len > 0 && memchr(line, '\0', line_len) != NULL)
        return -1;

    int nfields = 0;
    size_t wpos = 0;
    size_t i = 0;
    int stripped_nl = 0;

    /* Go encoding/csv readLine: drop one trailing LF, and the CR of a
     * CRLF pair. A last line with no LF still drops one trailing CR.
     * Extra CRs before that are field bytes, not extra terminators. */
    if (line_len > 0 && line[line_len - 1] == '\n') {
        line_len--;
        stripped_nl = 1;
        if (line_len > 0 && line[line_len - 1] == '\r')
            line_len--;
    } else if (line_len > 0 && line[line_len - 1] == '\r') {
        line_len--;
    }

    if (line_len == 0) return 0;

    for (;;) {
        if (nfields >= max_fields) return -1;

        if (trim)
            i += csv_skip_leading_space(line + i, line_len - i);

        fields[nfields] = work_buf + wpos;

        int field_unclosed = 0;
        if (i < line_len && line[i] == '"') {
            i++;
            int closed = 0;
            /* Quoted field: '"' is the only special byte. Go encoding/csv
             * readLine converts each physical CRLF to LF, including inside
             * multiline quoted values (Issue 21201). */
            for (;;) {
                if (i >= line_len) break;
                if (line[i] == '"') {
                    if (i + 1 < line_len && line[i + 1] == '"') {
                        if (wpos >= work_buf_len) return -1;
                        work_buf[wpos++] = '"';
                        i += 2;                          /* escaped doubled quote */
                        continue;
                    }
                    if (lazy && i + 1 < line_len &&
                        line[i + 1] != delim) {
                        if (wpos >= work_buf_len) return -1;
                        work_buf[wpos++] = '"';
                        i++;
                        continue;
                    }
                    i++;
                    closed = 1;
                    break;                               /* closing quote */
                }
                if (line[i] == '\r' && i + 1 < line_len &&
                    line[i + 1] == '\n') {
                    if (wpos >= work_buf_len) return -1;
                    work_buf[wpos++] = '\n';
                    i += 2;
                    continue;
                }
                /* Run of ordinary bytes up to the next quote or CRLF. */
                size_t remain = line_len - i;
                size_t k = 0;
                while (k < 16 && k < remain && line[i + k] != '"') {
                    if (line[i + k] == '\r' && i + k + 1 < line_len &&
                        line[i + k + 1] == '\n')
                        break;
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
                    const char *cr = (const char *)memchr(start, '\r', run);
                    while (cr) {
                        size_t off = (size_t)(cr - start);
                        if (off + 1 < rem2 && cr[1] == '\n') {
                            run = off;
                            break;
                        }
                        if (off + 1 >= run)
                            break;
                        cr = (const char *)memchr(cr + 1, '\r', run - off - 1);
                    }
                    if (run > work_buf_len - wpos) return -1;
                    memcpy(work_buf + wpos, start, run);
                    wpos += run;
                    i += run;
                    if (!q && run == rem2) break;        /* unterminated: end of line */
                } else if (k == remain) {
                    break;                               /* ran off the end */
                }
            }
            if (!closed && !lazy) return -1;
            if (!closed)
                field_unclosed = 1;
            if (closed && i < line_len && line[i] != delim) return -1;
        } else {
            /* Unquoted field: jump to the delimiter with memchr (SIMD) and copy
             * the whole run at once instead of one byte at a time. */
            const char *start = line + i;
            size_t remain = line_len - i;
            const char *d = (const char *)memchr(start, delim, remain);
            size_t flen = d ? (size_t)(d - start) : remain;
            if (!lazy && memchr(start, '"', flen)) return -1;
            if (flen > work_buf_len - wpos) return -1;
            memcpy(work_buf + wpos, start, flen);
            wpos += flen;
            i += flen;
        }

        /* LazyQuotes unterminated field: Go copies the line's trailing \n
         * into the field because the quote is still open. */
        if (field_unclosed && stripped_nl) {
            if (wpos >= work_buf_len) return -1;
            work_buf[wpos++] = '\n';
        }
        if (wpos >= work_buf_len) return -1;
        if ((size_t)((work_buf + wpos) - fields[nfields]) >
            NEVERC_CSV_MAX_FIELD_LEN)
            return -1;
        work_buf[wpos++] = '\0';
        nfields++;

        if (i >= line_len) break;
        if (line[i] == delim) {
            i++;
            /* trailing delimiter produces an extra empty field */
            if (i >= line_len) {
                if (nfields >= max_fields) return -1;
                fields[nfields] = work_buf + wpos;
                if (wpos >= work_buf_len) return -1;
                work_buf[wpos++] = '\0';
                nfields++;
                break;
            }
        }
    }

    return nfields;
}

int neverc_csv_read_all(const char *data, size_t data_len,
                        const char ***records, int *field_counts,
                        int max_records,
                        char *work_buf, size_t work_buf_len,
                        const neverc_csv_reader_opts_t *opts) {
    if ((!data && data_len > 0) || !records || !field_counts ||
        max_records < 0 || (!work_buf && data_len > 0))
        return -1;
    char comment = (opts && opts->comment) ? opts->comment : 0;
    char delim = (opts && opts->delimiter) ? opts->delimiter : ',';
    int trim = (opts && opts->trim_leading_space) ? 1 : 0;
    int lazy = (opts && opts->lazy_quotes) ? 1 : 0;
    int nrecords = 0;
    size_t pos = 0;

    while (pos < data_len) {
        /* Comment and empty lines are physical lines; quotes inside comments
         * have no CSV meaning and must not consume following records.
         * Record separators match Go encoding/csv: LF or CRLF. A lone CR is
         * a field byte, not a line break (RFC 4180 only names CRLF; Go splits
         * on '\n' and strips CR only when it precedes LF or EOF). */
        if (comment && data[pos] == comment) {
            while (pos < data_len && data[pos] != '\n')
                pos++;
            if (pos < data_len && data[pos] == '\n') pos++;
            continue;
        }
        if (data[pos] == '\n') {
            pos++;
            continue;
        }
        if (data[pos] == '\r' && pos + 1 < data_len &&
            data[pos + 1] == '\n') {
            pos += 2;
            continue;
        }

        /* find end of line */
        size_t line_start = pos;
        int in_quote = 0;
        int field_start = 1;
        while (pos < data_len) {
            char byte = data[pos];
            if (in_quote) {
                if (byte == '"') {
                    if (pos + 1 < data_len && data[pos + 1] == '"') {
                        pos += 2;
                        continue;
                    }
                    if (lazy && pos + 1 < data_len &&
                        data[pos + 1] != delim &&
                        data[pos + 1] != '\n' &&
                        !(data[pos + 1] == '\r' && pos + 2 < data_len &&
                          data[pos + 2] == '\n')) {
                        pos++;
                        continue;
                    }
                    in_quote = 0;
                }
            } else {
                if (byte == '\n') break;
                if (byte == '\r' && pos + 1 < data_len &&
                    data[pos + 1] == '\n')
                    break;
                if (field_start && trim) {
                    size_t skip = csv_skip_leading_space(
                        data + pos, data_len - pos);
                    if (skip > 0) {
                        pos += skip;
                        continue;
                    }
                }
                if (field_start && byte == '"') {
                    in_quote = 1;
                    field_start = 0;
                } else {
                    field_start = byte == delim;
                }
            }
            pos++;
        }
        if (in_quote && !lazy) return -1;

        /* Include the record terminator so read_line can apply Go's
         * single-CRLF / EOF-CR strip. Interior CRs stay in the field. */
        if (pos < data_len && data[pos] == '\r') pos++;
        if (pos < data_len && data[pos] == '\n') pos++;
        size_t line_len = pos - line_start;

        if (nrecords >= max_records || !records[nrecords]) return -1;

        int nf = neverc_csv_read_line(data + line_start, line_len,
                                       records[nrecords],
                                       NEVERC_CSV_MAX_FIELDS,
                                       work_buf, work_buf_len, opts);
        if (nf < 0) return -1;
        /* A lone CR at EOF is stripped by read_line, leaving an empty
         * physical line. Go encoding/csv skips those blank lines. */
        if (nf == 0) continue;

        field_counts[nrecords] = nf;
        nrecords++;

        /* advance work_buf */
        size_t used = 0;
        for (int i = 0; i < nf; i++) {
            size_t field_len = strlen(records[nrecords - 1][i]);
            if (field_len >= work_buf_len - used) return -1;
            used += field_len + 1;
        }
        work_buf += used;
        work_buf_len -= used;
    }

    return nrecords;
}

/* ---- Writer ---- */

int neverc_csv_write_record(const char **fields, int nfields,
                            char *dst, size_t dst_len,
                            const neverc_csv_writer_opts_t *opts) {
    if (!fields || nfields < 0 || !dst) return -1;
    char delim = (opts && opts->delimiter) ? opts->delimiter : ',';
    if (delim == '"' || delim == '\r' || delim == '\n') return -1;
    int crlf = (opts && opts->use_crlf) ? 1 : 0;
    size_t pos = 0;

    for (int i = 0; i < nfields; i++) {
        if (i > 0) {
            if (pos >= dst_len) return -1;
            dst[pos++] = delim;
        }

        const char *f = fields[i];
        if (!f) return -1;
        if (needs_quoting(f, delim, crlf)) {
            /* The common quoted field (forced by a comma or newline) carries no
             * embedded '"', so its body copies verbatim — wrap it in a single
             * memcpy. Only when a '"' is actually present fall back to the
             * byte-at-a-time path that doubles each quote, which keeps that
             * (rarer, quote-dense) case at its original speed. strchr avoids a
             * length scan on the quoted path. */
            if (!strchr(f, '"')) {
                size_t flen = strlen(f);
                if (flen > dst_len - pos ||
                    dst_len - pos - flen < 2)
                    return -1;
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
            if (flen > dst_len - pos) return -1;
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

    return pos <= (size_t)INT_MAX ? (int)pos : -1;
}

int neverc_csv_write_all(const char ***records, const int *field_counts,
                         int nrecords,
                         char *dst, size_t dst_len,
                         const neverc_csv_writer_opts_t *opts) {
    if (!records || !field_counts || nrecords < 0 ||
        (!dst && nrecords > 0))
        return -1;
    size_t total = 0;
    for (int i = 0; i < nrecords; i++) {
        if (!records[i] || total > dst_len) return -1;
        int n = neverc_csv_write_record(records[i], field_counts[i],
                                        dst + total, dst_len - total, opts);
        if (n < 0) return -1;
        total += (size_t)n;
    }
    return total <= (size_t)INT_MAX ? (int)total : -1;
}
