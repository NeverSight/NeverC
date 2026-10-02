#ifndef NEVERC_ARCHIVE_TAR_H
#define NEVERC_ARCHIVE_TAR_H

/*
 * NeverC archive/tar — POSIX tar format.
 *
 * Supports reading and writing regular files, hard links, symbolic links,
 * and directories, plus pax linkdata hard-link bodies. The writer produces
 * POSIX ustar headers.
 *
 * Readers accept ustar, pax, GNU, star, and v7 headers as Go archive/tar
 * does: pax extended ('x') records for path, linkpath, size, uid, gid,
 * uname, gname, and mtime/atime/ctime with sub-second precision, GNU long
 * name and link ('L'/'K') blocks, and base-256 numeric fields. Only the last
 * 'x' block before a file applies, an empty pax value keeps the header
 * field, and a GNU long name or link overrides the pax value. Global pax
 * ('g') blocks are validated and skipped: as in Go their records do not carry
 * into later entries, and unlike Go they are not returned as entries (a 'g'
 * block also drops metadata pending from earlier 'x'/'L'/'K' blocks, as it
 * does in Go). Malformed pax records or numbers fail the entry like Go's
 * ErrHeader. Each pax or GNU metadata payload is limited to
 * NEVERC_TAR_SPECIAL_MAX bytes, as in Go; the reader never allocates.
 * Readers accept the POSIX unsigned header checksum and the historical
 * signed checksum, and reject a stored value that matches neither.
 * Character/block devices, FIFOs, and sparse files (GNU 'S' and the pax
 * GNU.sparse formats) are rejected.
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

/* Additive long-name API. V3 holds names, link targets, and owner names that
 * pax or GNU records extend past the ustar fields, up to the capacities
 * below (NUL included), and adds owner ids and access/change times. Times
 * are seconds since the Unix epoch plus nanoseconds in [0, 999999999]
 * (floor seconds for negative times; v1/v2 report those seconds). An atime
 * or ctime of 0 with 0 nanoseconds means the archive recorded none. */
#define NEVERC_TAR_V3_NAME_SIZE  4096
#define NEVERC_TAR_V3_OWNER_SIZE 256

typedef struct {
    char     name[NEVERC_TAR_V3_NAME_SIZE];
    int64_t  size;
    uint32_t mode;
    int64_t  mtime;
    int      typeflag;
    char     linkname[NEVERC_TAR_V3_NAME_SIZE];
    char     uname[NEVERC_TAR_V3_OWNER_SIZE];
    char     gname[NEVERC_TAR_V3_OWNER_SIZE];
    int64_t  uid;
    int64_t  gid;
    int32_t  mtime_nsec;
    int32_t  atime_nsec;
    int32_t  ctime_nsec;
    int64_t  atime;
    int64_t  ctime;
} neverc_tar_header_v3_t;

/* Largest pax 'x'/'g' or GNU 'L'/'K' payload the reader accepts. */
#define NEVERC_TAR_SPECIAL_MAX (1024 * 1024)

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
 * next/read call. Reinitializing a reader is safe and releases no storage.
 * After init, data/len/pos are the reader's cursor rather than the init
 * arguments: next/read advance data and shrink len past consumed bytes, and
 * pos counts the unread payload bytes of the current entry. Keep a separate
 * pointer to the buffer passed to init. A copy of the struct is an
 * independent cursor. Each call does work proportional to the bytes it
 * consumes, so iterating an archive is linear in its size. */
void neverc_tar_reader_init(neverc_tar_reader_t *r, const uint8_t *data, size_t len);
/* Returns 1 for an entry, 0 after the required two consecutive zero end
 * blocks, or -1 for malformed or unterminated input. All bytes after the
 * second zero block are ignored. POSIX permits undefined complete 512-byte
 * logical records as physical-record padding; ignoring a final partial
 * record also matches Go archive/tar EOF behavior. Metadata blocks followed
 * directly by the end blocks also end the archive, as in Go. An entry whose
 * name, link target, or owner names exceed the fields of the header version
 * used fails (-1) without advancing; next_v3 can then read it. On failure
 * hdr is zeroed. */
int  neverc_tar_reader_next(neverc_tar_reader_t *r, neverc_tar_header_t *hdr);
int  neverc_tar_reader_next_v2(neverc_tar_reader_t *r,
                               neverc_tar_header_v2_t *hdr);
int  neverc_tar_reader_next_v3(neverc_tar_reader_t *r,
                               neverc_tar_header_v3_t *hdr);
/* Reads the current entry incrementally; unread bytes are skipped by next().
 * hdr must be the header next() returned for the current entry; a header
 * whose size is below the entry's unread byte count fails. Once the entry is
 * exhausted (or before the first next()), reads return 0 with *nread == 0.
 * A failed next() or read() leaves the reader unchanged. */
int  neverc_tar_reader_read(neverc_tar_reader_t *r, const neverc_tar_header_t *hdr,
                            uint8_t *buf, size_t len, size_t *nread);
int  neverc_tar_reader_read_v2(neverc_tar_reader_t *r,
                               const neverc_tar_header_v2_t *hdr,
                               uint8_t *buf, size_t len, size_t *nread);
int  neverc_tar_reader_read_v3(neverc_tar_reader_t *r,
                               const neverc_tar_header_v3_t *hdr,
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
