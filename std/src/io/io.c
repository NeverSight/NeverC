#include "neverc/io.h"
#include <stdlib.h>
#include <string.h>

uint8_t *neverc_io_read_all(neverc_io_reader_t *r, size_t *outlen) {
    size_t cap = 4096, total = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) { *outlen = 0; return NULL; }

    for (;;) {
        if (total >= cap) {
            cap *= 2;
            buf = (uint8_t *)realloc(buf, cap);
            if (!buf) { *outlen = 0; return NULL; }
        }
        size_t n = 0;
        int err = r->read(r->ctx, buf + total, cap - total, &n);
        total += n;
        if (err == NEVERC_IO_EOF || n == 0) break;
        if (err < 0) break;
    }
    *outlen = total;
    return buf;
}

int neverc_io_read_full(neverc_io_reader_t *r, uint8_t *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        size_t n = 0;
        int err = r->read(r->ctx, buf + total, len - total, &n);
        total += n;
        if (err == NEVERC_IO_EOF) {
            return total == len ? 0 : NEVERC_IO_ERR_UNEXP;
        }
        if (err < 0) return err;
    }
    return 0;
}

int64_t neverc_io_copy(neverc_io_writer_t *dst, neverc_io_reader_t *src) {
    uint8_t buf[32768];
    int64_t total = 0;
    for (;;) {
        size_t nr = 0;
        int err = src->read(src->ctx, buf, sizeof(buf), &nr);
        if (nr > 0) {
            size_t nw = 0;
            int werr = dst->write(dst->ctx, buf, nr, &nw);
            total += (int64_t)nw;
            if (werr < 0) return total;
            if (nw != nr) return total;
        }
        if (err == NEVERC_IO_EOF || nr == 0) break;
        if (err < 0) break;
    }
    return total;
}

int64_t neverc_io_copy_n(neverc_io_writer_t *dst, neverc_io_reader_t *src,
                         int64_t n) {
    uint8_t buf[32768];
    int64_t total = 0;
    while (total < n) {
        size_t want = sizeof(buf);
        if ((int64_t)want > n - total) want = (size_t)(n - total);
        size_t nr = 0;
        int err = src->read(src->ctx, buf, want, &nr);
        if (nr > 0) {
            size_t nw = 0;
            dst->write(dst->ctx, buf, nr, &nw);
            total += (int64_t)nw;
        }
        if (err == NEVERC_IO_EOF || nr == 0) break;
        if (err < 0) break;
    }
    return total;
}

int neverc_io_write_string(neverc_io_writer_t *w, const char *s, size_t *n) {
    size_t len = strlen(s);
    return w->write(w->ctx, (const uint8_t *)s, len, n);
}

/* Discard writer */
static int discard_write(void *ctx, const uint8_t *buf, size_t len, size_t *n) {
    (void)ctx; (void)buf;
    *n = len;
    return 0;
}

void neverc_io_discard_init(neverc_io_writer_t *w) {
    w->ctx = NULL;
    w->write = discard_write;
}

/* Memory-backed reader */
void neverc_io_mem_reader_init(neverc_io_mem_reader_t *mr,
                               const uint8_t *data, size_t len) {
    mr->data = data;
    mr->len = len;
    mr->pos = 0;
}

int neverc_io_mem_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    neverc_io_mem_reader_t *mr = (neverc_io_mem_reader_t *)ctx;
    if (mr->pos >= mr->len) { *n = 0; return NEVERC_IO_EOF; }
    size_t avail = mr->len - mr->pos;
    size_t count = len < avail ? len : avail;
    for (size_t i = 0; i < count; i++) buf[i] = mr->data[mr->pos + i];
    mr->pos += count;
    *n = count;
    return 0;
}

/* Memory-backed writer */
void neverc_io_mem_writer_init(neverc_io_mem_writer_t *mw) {
    mw->cap = 256;
    mw->data = (uint8_t *)malloc(mw->cap);
    mw->len = 0;
}

int neverc_io_mem_writer_write(void *ctx, const uint8_t *buf, size_t len,
                               size_t *n) {
    neverc_io_mem_writer_t *mw = (neverc_io_mem_writer_t *)ctx;
    while (mw->len + len > mw->cap) {
        mw->cap *= 2;
        mw->data = (uint8_t *)realloc(mw->data, mw->cap);
    }
    for (size_t i = 0; i < len; i++) mw->data[mw->len + i] = buf[i];
    mw->len += len;
    *n = len;
    return 0;
}

void neverc_io_mem_writer_free(neverc_io_mem_writer_t *mw) {
    free(mw->data);
    mw->data = NULL;
    mw->len = mw->cap = 0;
}

int neverc_io_read_at_least(neverc_io_reader_t *r, uint8_t *buf,
                             size_t len, size_t min, size_t *n) {
    size_t total = 0;
    while (total < min) {
        size_t got = 0;
        int rc = r->read(r->ctx, buf + total, len - total, &got);
        total += got;
        if (rc == NEVERC_IO_EOF) {
            if (n) *n = total;
            return (total >= min) ? NEVERC_IO_EOF : NEVERC_IO_ERR_UNEXP;
        }
        if (rc < 0) { if (n) *n = total; return rc; }
        if (got == 0) break;
    }
    if (n) *n = total;
    return (total < min) ? NEVERC_IO_ERR_SHORT : 0;
}

int64_t neverc_io_copy_buffer(neverc_io_writer_t *dst, neverc_io_reader_t *src,
                               uint8_t *buf, size_t buflen) {
    int64_t written = 0;
    while (1) {
        size_t nr = 0;
        int rc = src->read(src->ctx, buf, buflen, &nr);
        if (nr > 0) {
            size_t nw = 0;
            int wc = dst->write(dst->ctx, buf, nr, &nw);
            written += (int64_t)nw;
            if (wc < 0) return written;
            if (nw != nr) return written;
        }
        if (rc == NEVERC_IO_EOF) break;
        if (rc < 0) return written;
    }
    return written;
}

static int limit_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    neverc_io_limit_reader_t *lr = (neverc_io_limit_reader_t *)ctx;
    if (lr->remaining <= 0) { *n = 0; return NEVERC_IO_EOF; }
    if ((int64_t)len > lr->remaining) len = (size_t)lr->remaining;
    int rc = lr->inner->read(lr->inner->ctx, buf, len, n);
    lr->remaining -= (int64_t)*n;
    return rc;
}

void neverc_io_limit_reader_init(neverc_io_limit_reader_t *lr,
                                  neverc_io_reader_t *r, int64_t n) {
    lr->inner = r;
    lr->remaining = n;
    lr->reader.ctx = lr;
    lr->reader.read = limit_reader_read;
}

static int tee_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    neverc_io_tee_reader_t *tr = (neverc_io_tee_reader_t *)ctx;
    int rc = tr->inner->read(tr->inner->ctx, buf, len, n);
    if (*n > 0 && tr->tee) {
        size_t nw = 0;
        tr->tee->write(tr->tee->ctx, buf, *n, &nw);
    }
    return rc;
}

void neverc_io_tee_reader_init(neverc_io_tee_reader_t *tr,
                                neverc_io_reader_t *r,
                                neverc_io_writer_t *w) {
    tr->inner = r;
    tr->tee = w;
    tr->reader.ctx = tr;
    tr->reader.read = tee_reader_read;
}

/* --- MultiReader --- */

int neverc_io_multi_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    neverc_io_multi_reader_t *mr = (neverc_io_multi_reader_t *)ctx;
    *n = 0;
    while (mr->current < mr->count) {
        neverc_io_reader_t *r = &mr->readers[mr->current];
        size_t got = 0;
        int rc = r->read(r->ctx, buf, len, &got);
        if (got > 0) { *n = got; return 0; }
        if (rc != NEVERC_IO_EOF && rc != 0) return rc;
        mr->current++;
    }
    return NEVERC_IO_EOF;
}

void neverc_io_multi_reader_init(neverc_io_multi_reader_t *mr,
                                  neverc_io_reader_t *readers, int count) {
    mr->readers = readers;
    mr->count = count;
    mr->current = 0;
    mr->reader.ctx = mr;
    mr->reader.read = neverc_io_multi_reader_read;
}

/* --- MultiWriter --- */

int neverc_io_multi_writer_write(void *ctx, const uint8_t *buf, size_t len, size_t *n) {
    neverc_io_multi_writer_t *mw = (neverc_io_multi_writer_t *)ctx;
    *n = 0;
    for (int i = 0; i < mw->count; i++) {
        size_t nw = 0;
        int rc = mw->writers[i].write(mw->writers[i].ctx, buf, len, &nw);
        if (rc != 0) return rc;
        if (nw != len) return NEVERC_IO_ERR_SHORT;
    }
    *n = len;
    return 0;
}

void neverc_io_multi_writer_init(neverc_io_multi_writer_t *mw,
                                  neverc_io_writer_t *writers, int count) {
    mw->writers = writers;
    mw->count = count;
    mw->writer.ctx = mw;
    mw->writer.write = neverc_io_multi_writer_write;
}

/* --- Pipe --- */

static int pipe_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    neverc_io_pipe_t *p = (neverc_io_pipe_t *)ctx;
    *n = 0;
    if (p->len == 0) return p->closed ? NEVERC_IO_EOF : 0;
    size_t to_read = len < p->len ? len : p->len;
    for (size_t i = 0; i < to_read; i++) buf[i] = p->buf[i];
    size_t remaining = p->len - to_read;
    for (size_t i = 0; i < remaining; i++) p->buf[i] = p->buf[to_read + i];
    p->len = remaining;
    *n = to_read;
    return 0;
}

static int pipe_write(void *ctx, const uint8_t *buf, size_t len, size_t *n) {
    neverc_io_pipe_t *p = (neverc_io_pipe_t *)ctx;
    *n = 0;
    if (p->closed) return -1;
    while (p->len + len > p->cap) {
        p->cap = p->cap == 0 ? 256 : p->cap * 2;
        p->buf = (uint8_t *)realloc(p->buf, p->cap);
    }
    for (size_t i = 0; i < len; i++) p->buf[p->len + i] = buf[i];
    p->len += len;
    *n = len;
    return 0;
}

void neverc_io_pipe(neverc_io_pipe_t *pipe_ctx,
                     neverc_io_reader_t *r, neverc_io_writer_t *w) {
    pipe_ctx->buf = NULL;
    pipe_ctx->len = 0;
    pipe_ctx->cap = 0;
    pipe_ctx->closed = 0;
    r->ctx = pipe_ctx;
    r->read = pipe_read;
    w->ctx = pipe_ctx;
    w->write = pipe_write;
}

void neverc_io_pipe_close(neverc_io_pipe_t *p) {
    if (p) p->closed = 1;
}

void neverc_io_pipe_free(neverc_io_pipe_t *p) {
    if (p) { free(p->buf); p->buf = NULL; p->len = 0; p->cap = 0; }
}

/* --- NopCloser --- */

static int nop_close(void *ctx) { (void)ctx; return 0; }

void neverc_io_nop_closer_init(neverc_io_nop_closer_t *nc,
                                neverc_io_reader_t *r) {
    nc->inner = r;
    nc->reader = *r;
    nc->closer.ctx = nc;
    nc->closer.close = nop_close;
}
