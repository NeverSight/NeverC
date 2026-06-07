#ifndef NEVERC_BUFIO_H
#define NEVERC_BUFIO_H

/*
 * NeverC bufio — buffered I/O (mirrors Go bufio package).
 *
 * Scanner: line-by-line or token-by-token reading from a reader.
 * Reader: buffered wrapper around neverc_io_reader_t.
 * Writer: buffered wrapper around neverc_io_writer_t.
 */

#include "neverc/std/io.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_BUFIO_DEFAULT_SIZE 4096

/* --- Scanner --- */
typedef struct {
    neverc_io_reader_t  reader;
    uint8_t            *buf;
    size_t              buf_len;
    size_t              buf_cap;
    size_t              start;
    const uint8_t      *token;
    size_t              token_len;
    int                 done;
    int                 err;
} neverc_bufio_scanner_t;

void neverc_bufio_scanner_init(neverc_bufio_scanner_t *s,
                               neverc_io_reader_t reader);
int  neverc_bufio_scanner_scan(neverc_bufio_scanner_t *s);
const uint8_t *neverc_bufio_scanner_bytes(const neverc_bufio_scanner_t *s,
                                          size_t *len);
const char    *neverc_bufio_scanner_text(const neverc_bufio_scanner_t *s);
int  neverc_bufio_scanner_err(const neverc_bufio_scanner_t *s);
void neverc_bufio_scanner_free(neverc_bufio_scanner_t *s);

/* --- Buffered Reader --- */
typedef struct {
    neverc_io_reader_t  reader;
    uint8_t            *buf;
    size_t              buf_cap;
    size_t              r, w;
    int                 eof;
} neverc_bufio_reader_t;

void    neverc_bufio_reader_init(neverc_bufio_reader_t *br,
                                 neverc_io_reader_t reader);
void    neverc_bufio_reader_init_size(neverc_bufio_reader_t *br,
                                     neverc_io_reader_t reader, size_t size);
int     neverc_bufio_reader_read_byte(neverc_bufio_reader_t *br, uint8_t *b);
int     neverc_bufio_reader_read(neverc_bufio_reader_t *br,
                                 uint8_t *buf, size_t len, size_t *n);
uint8_t *neverc_bufio_reader_read_line(neverc_bufio_reader_t *br, size_t *len);
int     neverc_bufio_reader_peek(neverc_bufio_reader_t *br,
                                 uint8_t *buf, size_t n);
void    neverc_bufio_reader_free(neverc_bufio_reader_t *br);

/* --- Buffered Writer --- */
typedef struct {
    neverc_io_writer_t  writer;
    uint8_t            *buf;
    size_t              buf_cap;
    size_t              n;
} neverc_bufio_writer_t;

void neverc_bufio_writer_init(neverc_bufio_writer_t *bw,
                              neverc_io_writer_t writer);
void neverc_bufio_writer_init_size(neverc_bufio_writer_t *bw,
                                   neverc_io_writer_t writer, size_t size);
int  neverc_bufio_writer_write(neverc_bufio_writer_t *bw,
                               const uint8_t *data, size_t len, size_t *n);
int  neverc_bufio_writer_write_byte(neverc_bufio_writer_t *bw, uint8_t c);
int  neverc_bufio_writer_flush(neverc_bufio_writer_t *bw);
void neverc_bufio_writer_free(neverc_bufio_writer_t *bw);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_bufio_t { char __tag; };
extern struct __neverc_std_bufio_t bufio;
#endif

#endif /* NEVERC_BUFIO_H */
