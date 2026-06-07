#ifndef NEVERC_ARCHIVE_TAR_H
#define NEVERC_ARCHIVE_TAR_H

/*
 * NeverC archive/tar — POSIX tar format (mirrors Go archive/tar).
 *
 * Supports reading and writing POSIX (ustar) tar archives.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_TAR_BLOCK_SIZE 512

typedef struct {
    char     name[256];
    int64_t  size;
    uint32_t mode;
    int64_t  mtime;
    int      typeflag;
    char     linkname[100];
    char     uname[32];
    char     gname[32];
} neverc_tar_header_t;

#define NEVERC_TAR_REG  '0'
#define NEVERC_TAR_LINK '1'
#define NEVERC_TAR_SYM  '2'
#define NEVERC_TAR_DIR  '5'

/* --- Reader --- */
typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
} neverc_tar_reader_t;

void neverc_tar_reader_init(neverc_tar_reader_t *r, const uint8_t *data, size_t len);
int  neverc_tar_reader_next(neverc_tar_reader_t *r, neverc_tar_header_t *hdr);
int  neverc_tar_reader_read(neverc_tar_reader_t *r, const neverc_tar_header_t *hdr,
                            uint8_t *buf, size_t len, size_t *nread);

/* --- Writer --- */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} neverc_tar_writer_t;

void neverc_tar_writer_init(neverc_tar_writer_t *w);
int  neverc_tar_writer_write_header(neverc_tar_writer_t *w,
                                    const neverc_tar_header_t *hdr);
int  neverc_tar_writer_write(neverc_tar_writer_t *w,
                             const uint8_t *data, size_t len);
int  neverc_tar_writer_close(neverc_tar_writer_t *w);
void neverc_tar_writer_free(neverc_tar_writer_t *w);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/archive.h>
#endif


#endif /* NEVERC_ARCHIVE_TAR_H */
