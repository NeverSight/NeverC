#ifndef NEVERC_ARCHIVE_ZIP_H
#define NEVERC_ARCHIVE_ZIP_H

/*
 * NeverC archive/zip — ZIP archive format (mirrors Go archive/zip).
 *
 * Supports reading and writing single-disk ZIP archives in stored
 * (no-compression) mode. Readers validate the central directory, local
 * headers, bounds, CRC32, and non-overlapping local records before
 * exposing file data.
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

/* Returns 0 on success or -1 for malformed/unsupported archives. */
int  neverc_zip_reader_init(neverc_zip_reader_t *r, const uint8_t *data, size_t len);
int  neverc_zip_reader_count(const neverc_zip_reader_t *r);
const neverc_zip_file_header_t *neverc_zip_reader_file(const neverc_zip_reader_t *r, int idx);
const uint8_t *neverc_zip_reader_file_data(const neverc_zip_reader_t *r, int idx, size_t *len);
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
    int      closed;
    int      failed;
} neverc_zip_writer_t;

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
