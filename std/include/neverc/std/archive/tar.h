#ifndef NEVERC_ARCHIVE_TAR_H
#define NEVERC_ARCHIVE_TAR_H

/*
 * NeverC archive/tar — POSIX tar format.
 *
 * Supports reading and writing regular files, hard links, symbolic links,
 * and directories in POSIX ustar archives, plus pax linkdata hard-link
 * bodies. Readers accept the POSIX unsigned header checksum and the
 * historical signed checksum, and reject a stored value that matches
 * neither. Character/block devices, FIFOs, PAX x/g extended headers, GNU
 * L/K long-name/link and S sparse metadata, and GNU base-256 numeric fields
 * are not supported.
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

/* Additive full-width ustar API. The released header above safely represents
 * NUL-terminated strings up to 255/99/31/31 bytes. V2 retains all
 * 256/100/32/32 bytes from fixed-width ustar fields without changing that
 * released type's layout. */
typedef struct {
    char     name[257];
    int64_t  size;
    uint32_t mode;
    int64_t  mtime;
    int      typeflag;
    char     linkname[101];
    char     uname[33];
    char     gname[33];
} neverc_tar_header_v2_t;

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

/* The reader borrows data; it must remain unchanged and alive until the last
 * next/read call. Reinitializing a reader is safe and releases no storage. */
void neverc_tar_reader_init(neverc_tar_reader_t *r, const uint8_t *data, size_t len);
/* Returns 1 for an entry, 0 after the required two consecutive zero end
 * blocks, or -1 for malformed or unterminated input. All bytes after the
 * second zero block are ignored. POSIX permits undefined complete 512-byte
 * logical records as physical-record padding; ignoring a final partial
 * record also matches Go archive/tar EOF behavior. */
int  neverc_tar_reader_next(neverc_tar_reader_t *r, neverc_tar_header_t *hdr);
int  neverc_tar_reader_next_v2(neverc_tar_reader_t *r,
                               neverc_tar_header_v2_t *hdr);
/* Reads the current entry incrementally; unread bytes are skipped by next(). */
int  neverc_tar_reader_read(neverc_tar_reader_t *r, const neverc_tar_header_t *hdr,
                            uint8_t *buf, size_t len, size_t *nread);
int  neverc_tar_reader_read_v2(neverc_tar_reader_t *r,
                               const neverc_tar_header_v2_t *hdr,
                               uint8_t *buf, size_t len, size_t *nread);

/* --- Writer --- */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} neverc_tar_writer_t;

/* The writer owns its data buffer. Its bytes remain available through w->data
 * until free; call free before reinitializing a writer that has been used. */
void neverc_tar_writer_init(neverc_tar_writer_t *w);
/* Starts an entry. The preceding entry must have received exactly header.size
 * bytes; a hard link may have a non-zero pax linkdata body. Names over 100
 * bytes are encoded with the ustar prefix when possible. */
int  neverc_tar_writer_write_header(neverc_tar_writer_t *w,
                                    const neverc_tar_header_t *hdr);
int  neverc_tar_writer_write_header_v2(neverc_tar_writer_t *w,
                                       const neverc_tar_header_v2_t *hdr);
/* Writes entry data without implicit truncation; exceeding header.size fails. */
int  neverc_tar_writer_write(neverc_tar_writer_t *w,
                             const uint8_t *data, size_t len);
/* Finishes the archive; fails while an entry is incomplete. */
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
