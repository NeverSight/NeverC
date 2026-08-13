#ifndef NEVERC_ENCODING_CSV_H
#define NEVERC_ENCODING_CSV_H

/*
 * NeverC encoding/csv — CSV reading and writing (mirrors Go encoding/csv).
 * RFC 4180 compliant with configurable delimiters.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_CSV_MAX_FIELDS 1024
#define NEVERC_CSV_MAX_FIELD_LEN 65536

typedef struct {
    char   delimiter;
    char   comment;
    int    lazy_quotes;
    int    trim_leading_space;
} neverc_csv_reader_opts_t;

typedef struct {
    char   delimiter;
    int    use_crlf;
} neverc_csv_writer_opts_t;

/*
 * Parse one CSV line from `line` into `fields` array.
 * Each field pointer points into `work_buf` (caller-provided scratch space).
 * Returns the number of fields parsed, or -1 on error.
 */
int neverc_csv_read_line(const char *line, size_t line_len,
                         const char **fields, int max_fields,
                         char *work_buf, size_t work_buf_len,
                         const neverc_csv_reader_opts_t *opts);

/*
 * Parse entire CSV data into a 2D array of strings.
 * Returns number of records, or -1 on error.
 * The caller must initialize each `records[i]` to writable storage for
 * NEVERC_CSV_MAX_FIELDS field pointers. `field_counts[i]` receives the count.
 * All string bytes are stored in `work_buf`; no allocation is performed.
 */
int neverc_csv_read_all(const char *data, size_t data_len,
                        const char ***records, int *field_counts,
                        int max_records,
                        char *work_buf, size_t work_buf_len,
                        const neverc_csv_reader_opts_t *opts);

/*
 * Write one CSV record to `dst`.
 * Returns number of bytes written, or -1 on error.
 */
int neverc_csv_write_record(const char **fields, int nfields,
                            char *dst, size_t dst_len,
                            const neverc_csv_writer_opts_t *opts);

/*
 * Write multiple CSV records to `dst`.
 * Returns total bytes written, or -1 on error.
 */
int neverc_csv_write_all(const char ***records, const int *field_counts,
                         int nrecords,
                         char *dst, size_t dst_len,
                         const neverc_csv_writer_opts_t *opts);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif


#endif
