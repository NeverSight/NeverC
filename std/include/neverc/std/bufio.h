#ifndef NEVERC_BUFIO_H
#define NEVERC_BUFIO_H

/*
 * NeverC bufio — buffered I/O (mirrors Go bufio package).
 *
 * Scanner: line-by-line or token-by-token reading from a reader.
 * Reader: buffered wrapper around neverc_io_reader_t. Read copies at most
 * once (Go bufio.Reader.Read); it does not loop like ReadFull.
 * Writer: buffered wrapper around neverc_io_writer_t.
 * Transient zero-byte successful reads are retried; persistent no-progress
 * readers terminate with NEVERC_IO_ERR_UNEXP rather than being mistaken for EOF.
 */

#include "neverc/std/io.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_BUFIO_DEFAULT_SIZE 4096
#define NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE (64 * 1024)
#define NEVERC_BUFIO_ERR_TOO_LONG (-4)

/* SplitFunc: 1 = token ready, 0 = need more data, -1 = error (*err set).
 * On 1, *token / *token_len name a subslice of data and *advance is consumed.
 * On 0, *advance may skip a prefix (ScanWords leading space).
 * Never called with data_len==0 unless at_eof (Go bufio.SplitFunc). */
typedef int (*neverc_bufio_split_func_t)(const uint8_t *data, size_t data_len,
                                         int at_eof,
                                         size_t *advance,
                                         const uint8_t **token,
                                         size_t *token_len,
                                         int *err);

int neverc_bufio_scan_lines(const uint8_t *data, size_t data_len, int at_eof,
                            size_t *advance, const uint8_t **token,
                            size_t *token_len, int *err);
int neverc_bufio_scan_words(const uint8_t *data, size_t data_len, int at_eof,
                            size_t *advance, const uint8_t **token,
                            size_t *token_len, int *err);
int neverc_bufio_scan_bytes(const uint8_t *data, size_t data_len, int at_eof,
                            size_t *advance, const uint8_t **token,
                            size_t *token_len, int *err);

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

/* Scanner owns its buffer. Do not copy an initialized scanner by value; call
 * scanner_free before reinitializing it. */
void neverc_bufio_scanner_init(neverc_bufio_scanner_t *s,
                               neverc_io_reader_t reader);
void neverc_bufio_scanner_split(neverc_bufio_scanner_t *s,
                                neverc_bufio_split_func_t split);
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
    int                 err;
} neverc_bufio_reader_t;

/* Reader owns its buffer. Do not copy an initialized reader by value; call
 * reader_free before reinitializing it. */
void    neverc_bufio_reader_init(neverc_bufio_reader_t *br,
                                 neverc_io_reader_t reader);
void    neverc_bufio_reader_init_size(neverc_bufio_reader_t *br,
                                     neverc_io_reader_t reader, size_t size);
int     neverc_bufio_reader_read_byte(neverc_bufio_reader_t *br, uint8_t *b);
int     neverc_bufio_reader_unread_byte(neverc_bufio_reader_t *br);
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

/* Writer owns its buffer. Do not copy an initialized writer by value; call
 * writer_free before reinitializing it. */
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
extern struct __neverc_std_bufio_t __neverc_mod_bufio;
extern struct __neverc_std_bufio_t bufio;
#endif

#endif /* NEVERC_BUFIO_H */
