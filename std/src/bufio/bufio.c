#include "neverc/bufio.h"
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
}

int neverc_bufio_scanner_scan(neverc_bufio_scanner_t *s) {
    if (s->done) return 0;

    for (;;) {
        for (size_t i = s->start; i < s->buf_len; i++) {
            if (s->buf[i] == '\n') {
                s->token = s->buf + s->start;
                s->token_len = i - s->start;
                if (s->token_len > 0 && s->token[s->token_len - 1] == '\r')
                    s->token_len--;
                s->start = i + 1;
                return 1;
            }
        }

        if (s->start > 0 && s->buf_len > s->start) {
            size_t remaining = s->buf_len - s->start;
            for (size_t i = 0; i < remaining; i++)
                s->buf[i] = s->buf[s->start + i];
            s->buf_len = remaining;
            s->start = 0;
        } else if (s->start > 0) {
            s->buf_len = 0;
            s->start = 0;
        }

        if (s->buf_len >= s->buf_cap) {
            s->buf_cap *= 2;
            s->buf = (uint8_t *)realloc(s->buf, s->buf_cap);
        }

        size_t nr = 0;
        int err = s->reader.read(s->reader.ctx,
                                 s->buf + s->buf_len,
                                 s->buf_cap - s->buf_len, &nr);
        s->buf_len += nr;
        if (err == NEVERC_IO_EOF || nr == 0) {
            s->done = 1;
            if (s->buf_len > s->start) {
                s->token = s->buf + s->start;
                s->token_len = s->buf_len - s->start;
                if (s->token_len > 0 && s->token[s->token_len - 1] == '\r')
                    s->token_len--;
                s->start = s->buf_len;
                return 1;
            }
            return 0;
        }
        if (err < 0) {
            s->err = err;
            s->done = 1;
            return 0;
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
    br->reader = reader;
    br->buf_cap = size;
    br->buf = (uint8_t *)malloc(size);
    br->r = br->w = 0;
    br->eof = 0;
}

static int bufio_fill(neverc_bufio_reader_t *br) {
    if (br->r > 0) {
        size_t n = br->w - br->r;
        for (size_t i = 0; i < n; i++) br->buf[i] = br->buf[br->r + i];
        br->w = n;
        br->r = 0;
    }
    if (br->w >= br->buf_cap) return 0;
    size_t nr = 0;
    int err = br->reader.read(br->reader.ctx, br->buf + br->w,
                              br->buf_cap - br->w, &nr);
    br->w += nr;
    if (err == NEVERC_IO_EOF) br->eof = 1;
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
        for (size_t i = 0; i < copy; i++) buf[*n + i] = br->buf[br->r + i];
        br->r += copy;
        *n += copy;
    }
    return 0;
}

uint8_t *neverc_bufio_reader_read_line(neverc_bufio_reader_t *br, size_t *len) {
    size_t cap = 256, wlen = 0;
    uint8_t *line = (uint8_t *)malloc(cap);

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
            cap *= 2;
            line = (uint8_t *)realloc(line, cap);
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
    for (size_t i = 0; i < copy; i++) buf[i] = br->buf[br->r + i];
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
    bw->writer = writer;
    bw->buf_cap = size;
    bw->buf = (uint8_t *)malloc(size);
    bw->n = 0;
}

int neverc_bufio_writer_flush(neverc_bufio_writer_t *bw) {
    if (bw->n == 0) return 0;
    size_t nw = 0;
    int err = bw->writer.write(bw->writer.ctx, bw->buf, bw->n, &nw);
    if (nw < bw->n) {
        size_t remaining = bw->n - nw;
        for (size_t i = 0; i < remaining; i++) bw->buf[i] = bw->buf[nw + i];
        bw->n = remaining;
    } else {
        bw->n = 0;
    }
    return err;
}

int neverc_bufio_writer_write(neverc_bufio_writer_t *bw,
                              const uint8_t *data, size_t len, size_t *n) {
    *n = 0;
    while (*n < len) {
        size_t avail = bw->buf_cap - bw->n;
        size_t copy = (len - *n) < avail ? (len - *n) : avail;
        for (size_t i = 0; i < copy; i++) bw->buf[bw->n + i] = data[*n + i];
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
