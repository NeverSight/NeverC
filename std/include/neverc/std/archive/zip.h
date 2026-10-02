#ifndef NEVERC_ARCHIVE_ZIP_H
#define NEVERC_ARCHIVE_ZIP_H

/*
 * NeverC archive/zip — ZIP archive format (mirrors Go archive/zip).
 *
 * Supports single-disk ZIP archives. Readers accept the Stored
 * (no-compression) and Deflate methods; writers produce Stored entries.
 * Readers validate the central directory, local headers, bounds, data
 * descriptors, and non-overlapping local records before exposing file data.
 * Stored entries are CRC-32 checked by reader_init; Deflate entries are
 * inflated and checked against their declared size and CRC-32 by
 * reader_file_read. Entry names are limited to 255 bytes.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_ZIP_STORED    0
#define NEVERC_ZIP_DEFLATED  8

typedef struct {
    char     name[256];
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint32_t crc32;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
} neverc_zip_file_header_t;

/* --- Reader --- */
typedef struct {
    const uint8_t *data;
    size_t         len;
    neverc_zip_file_header_t *files;
    int            nfiles;
    const uint8_t **file_data;
} neverc_zip_reader_t;

/* The reader borrows data for its entire lifetime. File-header and file-data
 * pointers are reader-owned views invalidated by reader_free. Call
 * reader_free before reinitializing a reader that completed successfully.
 * Returns 0 on success or -1 for malformed/unsupported archives. A Deflate
 * entry whose declared uncompressed_size exceeds 1032 times its
 * compressed_size (more than any DEFLATE stream can expand) is malformed. */
int  neverc_zip_reader_init(neverc_zip_reader_t *r, const uint8_t *data, size_t len);
int  neverc_zip_reader_count(const neverc_zip_reader_t *r);
const neverc_zip_file_header_t *neverc_zip_reader_file(const neverc_zip_reader_t *r, int idx);
/* Returns a view of a Stored entry's bytes and sets *len to their count.
 * Returns NULL with *len = 0 for an invalid index or an entry using any other
 * method; use reader_file_read for those. */
const uint8_t *neverc_zip_reader_file_data(const neverc_zip_reader_t *r, int idx, size_t *len);
/* Copies entry idx's uncompressed contents into dst, inflating Deflate
 * entries (mirrors reading Go's File.Open to EOF). On input *dst_len is the
 * capacity of dst and must be at least the entry's uncompressed_size; dst
 * may be NULL only when that size is 0. Inflation is bounded by the declared
 * uncompressed_size, and the output must match it and the entry's CRC-32.
 * Returns 0 and sets *dst_len to uncompressed_size on success. Returns -1
 * with *dst_len = 0 on failure; bytes already inflated into dst are cleared
 * so unverified data is never exposed. Safe to call concurrently on one
 * reader. */
int  neverc_zip_reader_file_read(const neverc_zip_reader_t *r, int idx,
                                 uint8_t *dst, size_t *dst_len);
void neverc_zip_reader_free(neverc_zip_reader_t *r);

/* --- Writer --- */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
    uint32_t *offsets;
    neverc_zip_file_header_t *entries;
    int      nentries;
    int      entries_cap;
} neverc_zip_writer_t;

/* The writer owns its output and entry arrays. The completed archive remains
 * available through w->data until writer_free; call writer_free before
 * reinitializing a writer that has been used. Input name/data passed to add are
 * copied and need only remain valid for the duration of that call. */
void neverc_zip_writer_init(neverc_zip_writer_t *w);
int  neverc_zip_writer_add(neverc_zip_writer_t *w, const char *name,
                           const uint8_t *data, size_t len);
int  neverc_zip_writer_close(neverc_zip_writer_t *w);
void neverc_zip_writer_free(neverc_zip_writer_t *w);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/archive.h>
#endif


#endif /* NEVERC_ARCHIVE_ZIP_H */
