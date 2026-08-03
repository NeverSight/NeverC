#include "neverc/std/bufio.h"
#include <stdlib.h>
#include <string.h>

/* --- Scanner --- */

void neverc_bufio_scanner_init(neverc_bufio_scanner_t *s,
                               neverc_io_reader_t reader) {
    s->reader = reader;
    s->buf_cap = NEVERC_BUFIO_DEFAULT_SIZE;
    s->buf = (uint8_t *)malloc(s->buf_cap);
    s->buf_len = 0;
    s->start = 0;
    s->token = NULL;
    s->token_len = 0;
    s->done = 0;
    s->err = 0;
    if (!s->buf) {
        s->buf_cap = 0;
        s->done = 1;
        s->err = NEVERC_IO_ERR_UNEXP;
    }
}

int neverc_bufio_scanner_scan(neverc_bufio_scanner_t *s) {
    if (!s) return 0;
    if (s->done && s->start >= s->buf_len) return 0;
    if (!s->buf || s->buf_cap == 0 || !s->reader.read) {
        s->err = NEVERC_IO_ERR_UNEXP;
        s->done = 1;
        return 0;
    }

    /* `scan_pos` marks the first not-yet-examined byte. The previous code
     * rescanned the whole [start, buf_len) window after every refill, turning a
     * long line read in small chunks into O(n^2); tracking the scan frontier
     * keeps each byte examined once (O(n)). memchr does the newline search with
     * a single SIMD scan instead of a byte-at-a-time loop. */
    size_t scan_pos = s->start;

    for (;;) {
        if (scan_pos < s->buf_len) {
            const uint8_t *nl = (const uint8_t *)memchr(s->buf + scan_pos, '\n',
                                                        s->buf_len - scan_pos);
            if (nl) {
                size_t i = (size_t)(nl - s->buf);
                s->token = s->buf + s->start;
                s->token_len = i - s->start;
                if (s->token_len > 0 && s->token[s->token_len - 1] == '\r')
                    s->token_len--;
                s->buf[s->start + s->token_len] = '\0';
                s->start = i + 1;
                return 1;
            }
            scan_pos = s->buf_len;
        }

        if (s->start > 0) {
            size_t remaining = s->buf_len - s->start;
            if (remaining > 0)
                memmove(s->buf, s->buf + s->start, remaining);
            s->buf_len = remaining;
            scan_pos = remaining;   /* already-scanned bytes now live at [0,remaining) */
            s->start = 0;
        }

        if (s->done) {
            if (s->buf_len > s->start) {
                s->token = s->buf + s->start;
                s->token_len = s->buf_len - s->start;
                if (s->token_len > 0 && s->token[s->token_len - 1] == '\r')
                    s->token_len--;
                s->buf[s->start + s->token_len] = '\0';
                s->start = s->buf_len;
                return 1;
            }
            return 0;
        }

        /* Keep one byte free so scanner_text() can always expose a
         * NUL-terminated token, including data returned together with EOF. */
        if (s->buf_len >= s->buf_cap - 1) {
            if (s->buf_cap > SIZE_MAX / 2) {
                s->err = NEVERC_IO_ERR_UNEXP;
                s->done = 1;
                return 0;
            }
            size_t new_cap = s->buf_cap * 2;
            uint8_t *new_buf = (uint8_t *)realloc(s->buf, new_cap);
            if (!new_buf) {
                s->err = NEVERC_IO_ERR_UNEXP;
                s->done = 1;
                return 0;
            }
            s->buf = new_buf;
            s->buf_cap = new_cap;
        }

        size_t available = s->buf_cap - s->buf_len - 1;
        size_t nr = 0;
        int err = s->reader.read(s->reader.ctx,
                                 s->buf + s->buf_len,
                                 available, &nr);
        if (nr > available) {
            s->err = NEVERC_IO_ERR_UNEXP;
            s->done = 1;
            continue;
        }
        s->buf_len += nr;
        if (err == NEVERC_IO_EOF) {
            s->done = 1;
        } else if (err != 0) {
            s->err = err;
            s->done = 1;
        } else if (nr == 0) {
            /* A successful zero-byte read cannot make progress. */
            s->done = 1;
        }
    }
}

const uint8_t *neverc_bufio_scanner_bytes(const neverc_bufio_scanner_t *s,
                                          size_t *len) {
    *len = s->token_len;
    return s->token;
}

const char *neverc_bufio_scanner_text(const neverc_bufio_scanner_t *s) {
    return (const char *)s->token;
}

int neverc_bufio_scanner_err(const neverc_bufio_scanner_t *s) {
    return s->err;
}

void neverc_bufio_scanner_free(neverc_bufio_scanner_t *s) {
    free(s->buf);
    s->buf = NULL;
}

/* --- Buffered Reader --- */

void neverc_bufio_reader_init(neverc_bufio_reader_t *br,
                              neverc_io_reader_t reader) {
    neverc_bufio_reader_init_size(br, reader, NEVERC_BUFIO_DEFAULT_SIZE);
}

void neverc_bufio_reader_init_size(neverc_bufio_reader_t *br,
                                   neverc_io_reader_t reader, size_t size) {
    if (size == 0) size = 1;
    br->reader = reader;
    br->buf_cap = size;
    br->buf = (uint8_t *)malloc(size);
    br->r = br->w = 0;
    br->eof = br->buf ? 0 : 1;
}

static int bufio_fill(neverc_bufio_reader_t *br) {
    if (!br->buf || !br->reader.read) {
        br->eof = 1;
        return 0;
    }
    if (br->r > 0) {
        size_t n = br->w - br->r;
        if (n > 0) memmove(br->buf, br->buf + br->r, n);
        br->w = n;
        br->r = 0;
    }
    if (br->w >= br->buf_cap) return 0;
    size_t nr = 0;
    int err = br->reader.read(br->reader.ctx, br->buf + br->w,
                              br->buf_cap - br->w, &nr);
    br->w += nr;
    if (err != 0 || nr == 0) br->eof = 1;
    return (int)nr;
}

int neverc_bufio_reader_read_byte(neverc_bufio_reader_t *br, uint8_t *b) {
    if (br->r >= br->w) {
        if (br->eof) return NEVERC_IO_EOF;
        bufio_fill(br);
        if (br->r >= br->w) return NEVERC_IO_EOF;
    }
    *b = br->buf[br->r++];
    return 0;
}

int neverc_bufio_reader_read(neverc_bufio_reader_t *br,
                             uint8_t *buf, size_t len, size_t *n) {
    *n = 0;
    while (*n < len) {
        if (br->r >= br->w) {
            if (br->eof) return *n > 0 ? 0 : NEVERC_IO_EOF;
            bufio_fill(br);
            if (br->r >= br->w) return *n > 0 ? 0 : NEVERC_IO_EOF;
        }
        size_t avail = br->w - br->r;
        size_t want = len - *n;
        size_t copy = avail < want ? avail : want;
        memcpy(buf + *n, br->buf + br->r, copy);
        br->r += copy;
        *n += copy;
    }
    return 0;
}

uint8_t *neverc_bufio_reader_read_line(neverc_bufio_reader_t *br, size_t *len) {
    size_t cap = 256, wlen = 0;
    uint8_t *line = (uint8_t *)malloc(cap);
    if (!line) { *len = 0; return NULL; }

    for (;;) {
        uint8_t b;
        int err = neverc_bufio_reader_read_byte(br, &b);
        if (err == NEVERC_IO_EOF) {
            if (wlen == 0) { free(line); *len = 0; return NULL; }
            break;
        }
        if (b == '\n') {
            if (wlen > 0 && line[wlen - 1] == '\r') wlen--;
            break;
        }
        if (wlen >= cap) {
            if (cap > SIZE_MAX / 2) {
                free(line);
                *len = 0;
                return NULL;
            }
            size_t new_cap = cap * 2;
            uint8_t *new_line = (uint8_t *)realloc(line, new_cap);
            if (!new_line) {
                free(line);
                *len = 0;
                return NULL;
            }
            line = new_line;
            cap = new_cap;
        }
        line[wlen++] = b;
    }
    *len = wlen;
    return line;
}

int neverc_bufio_reader_peek(neverc_bufio_reader_t *br,
                             uint8_t *buf, size_t n) {
    while (br->w - br->r < n && !br->eof) bufio_fill(br);
    size_t avail = br->w - br->r;
    size_t copy = avail < n ? avail : n;
    if (copy > 0) memcpy(buf, br->buf + br->r, copy);
    return (int)copy;
}

void neverc_bufio_reader_free(neverc_bufio_reader_t *br) {
    free(br->buf);
    br->buf = NULL;
}

/* --- Buffered Writer --- */

void neverc_bufio_writer_init(neverc_bufio_writer_t *bw,
                              neverc_io_writer_t writer) {
    neverc_bufio_writer_init_size(bw, writer, NEVERC_BUFIO_DEFAULT_SIZE);
}

void neverc_bufio_writer_init_size(neverc_bufio_writer_t *bw,
                                   neverc_io_writer_t writer, size_t size) {
    if (size == 0) size = 1;
    bw->writer = writer;
    bw->buf_cap = size;
    bw->buf = (uint8_t *)malloc(size);
    bw->n = 0;
}

int neverc_bufio_writer_flush(neverc_bufio_writer_t *bw) {
    if (bw->n == 0) return 0;
    if (!bw->buf || !bw->writer.write) return NEVERC_IO_ERR_UNEXP;
    size_t nw = 0;
    int err = bw->writer.write(bw->writer.ctx, bw->buf, bw->n, &nw);
    if (nw > bw->n) return NEVERC_IO_ERR_SHORT;
    if (nw == 0)
        return err < 0 ? err : NEVERC_IO_ERR_SHORT;
    if (nw < bw->n) {
        size_t remaining = bw->n - nw;
        memmove(bw->buf, bw->buf + nw, remaining);
        bw->n = remaining;
    } else {
        bw->n = 0;
    }
    return err;
}

int neverc_bufio_writer_write(neverc_bufio_writer_t *bw,
                              const uint8_t *data, size_t len, size_t *n) {
    *n = 0;
    if (len > 0 && (!bw->buf || bw->buf_cap == 0 || !data))
        return NEVERC_IO_ERR_UNEXP;
    while (*n < len) {
        size_t avail = bw->buf_cap - bw->n;
        size_t copy = (len - *n) < avail ? (len - *n) : avail;
        memcpy(bw->buf + bw->n, data + *n, copy);
        bw->n += copy;
        *n += copy;
        if (bw->n >= bw->buf_cap) {
            int err = neverc_bufio_writer_flush(bw);
            if (err < 0) return err;
        }
    }
    return 0;
}

int neverc_bufio_writer_write_byte(neverc_bufio_writer_t *bw, uint8_t c) {
    if (!bw->buf || bw->buf_cap == 0) return NEVERC_IO_ERR_UNEXP;
    if (bw->n >= bw->buf_cap) {
        int err = neverc_bufio_writer_flush(bw);
        if (err < 0) return err;
    }
    bw->buf[bw->n++] = c;
    return 0;
}

void neverc_bufio_writer_free(neverc_bufio_writer_t *bw) {
    free(bw->buf);
    bw->buf = NULL;
}
