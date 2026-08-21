#ifndef NEVERC_IO_H
#define NEVERC_IO_H

/*
 * NeverC io — I/O primitives (mirrors Go io package).
 *
 * Provides Reader/Writer interfaces as function-pointer structs,
 * plus utility functions: ReadAll, Copy, ReadFull, etc.
 * Transient zero-byte successful reads are retried; persistent no-progress
 * is not treated as EOF (Copy/ReadAll stop after a retry limit). ReadFull
 * and ReadAtLeast return NEVERC_IO_EOF only when no bytes were read, matching
 * Go io.ReadFull / ReadAtLeast; a short read that hits EOF is UNEXP.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_IO_EOF       (-1)
#define NEVERC_IO_ERR_SHORT (-2)
#define NEVERC_IO_ERR_UNEXP (-3)

typedef struct {
    void *ctx;
    int (*read)(void *ctx, uint8_t *buf, size_t len, size_t *n);
} neverc_io_reader_t;

typedef struct {
    void *ctx;
    int (*write)(void *ctx, const uint8_t *buf, size_t len, size_t *n);
} neverc_io_writer_t;

typedef struct {
    void *ctx;
    int (*close)(void *ctx);
} neverc_io_closer_t;

uint8_t *neverc_io_read_all(neverc_io_reader_t *r, size_t *outlen);
int      neverc_io_read_full(neverc_io_reader_t *r, uint8_t *buf, size_t len);
int      neverc_io_read_at_least(neverc_io_reader_t *r, uint8_t *buf,
                                  size_t len, size_t min, size_t *n);
int64_t  neverc_io_copy(neverc_io_writer_t *dst, neverc_io_reader_t *src);
int64_t  neverc_io_copy_n(neverc_io_writer_t *dst, neverc_io_reader_t *src,
                          int64_t n);
int64_t  neverc_io_copy_buffer(neverc_io_writer_t *dst, neverc_io_reader_t *src,
                                uint8_t *buf, size_t buflen);
int      neverc_io_write_string(neverc_io_writer_t *w, const char *s,
                                size_t *n);
void     neverc_io_discard_init(neverc_io_writer_t *w);

typedef struct {
    neverc_io_reader_t reader;
    neverc_io_reader_t *inner;
    int64_t remaining;
} neverc_io_limit_reader_t;

void neverc_io_limit_reader_init(neverc_io_limit_reader_t *lr,
                                  neverc_io_reader_t *r, int64_t n);

typedef struct {
    neverc_io_reader_t reader;
    neverc_io_reader_t *inner;
    neverc_io_writer_t *tee;
} neverc_io_tee_reader_t;

void neverc_io_tee_reader_init(neverc_io_tee_reader_t *tr,
                                neverc_io_reader_t *r,
                                neverc_io_writer_t *w);

/* Memory-backed reader/writer */
typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
} neverc_io_mem_reader_t;

void neverc_io_mem_reader_init(neverc_io_mem_reader_t *mr,
                               const uint8_t *data, size_t len);
int  neverc_io_mem_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n);

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} neverc_io_mem_writer_t;

void neverc_io_mem_writer_init(neverc_io_mem_writer_t *mw);
int  neverc_io_mem_writer_write(void *ctx, const uint8_t *buf, size_t len,
                                size_t *n);
void neverc_io_mem_writer_free(neverc_io_mem_writer_t *mw);

/* MultiReader: concatenate multiple readers */
typedef struct {
    neverc_io_reader_t reader;
    neverc_io_reader_t *readers;
    int count;
    int current;
} neverc_io_multi_reader_t;

void neverc_io_multi_reader_init(neverc_io_multi_reader_t *mr,
                                  neverc_io_reader_t *readers, int count);
int  neverc_io_multi_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n);

/* MultiWriter: write to all writers */
typedef struct {
    neverc_io_writer_t writer;
    neverc_io_writer_t *writers;
    int count;
} neverc_io_multi_writer_t;

void neverc_io_multi_writer_init(neverc_io_multi_writer_t *mw,
                                  neverc_io_writer_t *writers, int count);
int  neverc_io_multi_writer_write(void *ctx, const uint8_t *buf, size_t len, size_t *n);

/* Pipe: synchronous in-memory reader/writer pair */
typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
    int      closed;
} neverc_io_pipe_t;

void neverc_io_pipe(neverc_io_pipe_t *pipe,
                     neverc_io_reader_t *r, neverc_io_writer_t *w);
void neverc_io_pipe_close(neverc_io_pipe_t *pipe);
void neverc_io_pipe_free(neverc_io_pipe_t *pipe);

/* NopCloser: wraps a reader with a no-op close */
typedef struct {
    neverc_io_reader_t reader;
    neverc_io_closer_t closer;
    neverc_io_reader_t *inner;
} neverc_io_nop_closer_t;

void neverc_io_nop_closer_init(neverc_io_nop_closer_t *nc,
                                neverc_io_reader_t *r);

#ifdef __cplusplus
}
#endif

#include "io/fs.h"

#ifdef __neverc__
struct __neverc_std_fs_t { char __tag; };

struct __neverc_std_io_t {
    char __tag;
    struct __neverc_std_fs_t fs;
};
extern struct __neverc_std_io_t __neverc_mod_io;
extern struct __neverc_std_io_t io;
#endif

#endif /* NEVERC_IO_H */
