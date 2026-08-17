#include "neverc/std/io.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define NCI_IO_MAX_EMPTY_READS 100

static int ensure_capacity(uint8_t **data, size_t *cap, size_t required) {
    if (required == 0 || (*data && *cap >= required)) return 0;

    size_t new_cap = *cap < 256 ? 256 : *cap;
    while (new_cap < required) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = required;
            break;
        }
        new_cap *= 2;
    }

    uint8_t *new_data = (uint8_t *)realloc(*data, new_cap);
    if (!new_data) return NEVERC_IO_ERR_UNEXP;
    *data = new_data;
    *cap = new_cap;
    return 0;
}

static size_t copy_count_room(int64_t total) {
    if (total < 0) total = 0;
    uintmax_t room = (uintmax_t)(INT64_MAX - total);
    return room > (uintmax_t)SIZE_MAX ? SIZE_MAX : (size_t)room;
}

static int add_copy_count(int64_t *total, size_t count) {
    size_t room = copy_count_room(*total);
    if (count > room) {
        *total = INT64_MAX;
        return 1;
    }
    *total += (int64_t)count;
    return *total == INT64_MAX;
}

uint8_t *neverc_io_read_all(neverc_io_reader_t *r, size_t *outlen) {
    if (!outlen) return NULL;
    *outlen = 0;
    if (!r || !r->read) return NULL;

    size_t cap = 4096, total = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return NULL;
    unsigned empty_reads = 0;

    for (;;) {
        if (total >= cap) {
            if (cap > SIZE_MAX / 2) {
                free(buf);
                return NULL;
            }
            size_t new_cap = cap * 2;
            uint8_t *new_buf = (uint8_t *)realloc(buf, new_cap);
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
            cap = new_cap;
        }
        size_t available = cap - total;
        size_t n = 0;
        int err = r->read(r->ctx, buf + total, available, &n);
        if (n > available) {
            free(buf);
            return NULL;
        }
        total += n;
        if (err == NEVERC_IO_EOF) break;
        if (err != 0) {
            free(buf);
            return NULL;
        }
        if (n == 0) {
            if (++empty_reads >= NCI_IO_MAX_EMPTY_READS) break;
            continue;
        }
        empty_reads = 0;
    }
    *outlen = total;
    return buf;
}

int neverc_io_read_full(neverc_io_reader_t *r, uint8_t *buf, size_t len) {
    if (len == 0) return 0;
    if (!r || !r->read || !buf) return NEVERC_IO_ERR_UNEXP;

    size_t total = 0;
    unsigned empty_reads = 0;
    while (total < len) {
        size_t available = len - total;
        size_t n = 0;
        int err = r->read(r->ctx, buf + total, available, &n);
        if (n > available) return NEVERC_IO_ERR_UNEXP;
        total += n;
        if (total == len) return 0;
        if (err == NEVERC_IO_EOF) {
            return NEVERC_IO_ERR_UNEXP;
        }
        if (err != 0) return err;
        if (n == 0) {
            if (++empty_reads >= NCI_IO_MAX_EMPTY_READS)
                return NEVERC_IO_ERR_UNEXP;
            continue;
        }
        empty_reads = 0;
    }
    return 0;
}

int64_t neverc_io_copy(neverc_io_writer_t *dst, neverc_io_reader_t *src) {
    if (!dst || !dst->write || !src || !src->read) return 0;

    uint8_t buf[32768];
    int64_t total = 0;
    unsigned empty_reads = 0;
    for (;;) {
        size_t nr = 0;
        int err = src->read(src->ctx, buf, sizeof(buf), &nr);
        if (nr > sizeof(buf)) return total;
        if (nr > 0) {
            empty_reads = 0;
            size_t to_write = copy_count_room(total);
            if (to_write == 0) return total;
            if (to_write > nr) to_write = nr;
            size_t nw = 0;
            int werr = dst->write(dst->ctx, buf, to_write, &nw);
            if (nw > to_write) return total;
            if (add_copy_count(&total, nw)) return total;
            if (werr != 0) return total;
            if (nw != to_write || to_write < nr) return total;
        }
        if (err == NEVERC_IO_EOF) break;
        if (err != 0) break;
        if (nr == 0) {
            if (++empty_reads >= NCI_IO_MAX_EMPTY_READS) break;
        }
    }
    return total;
}

int64_t neverc_io_copy_n(neverc_io_writer_t *dst, neverc_io_reader_t *src,
                         int64_t n) {
    if (n <= 0 || !dst || !dst->write || !src || !src->read) return 0;

    uint8_t buf[32768];
    int64_t total = 0;
    unsigned empty_reads = 0;
    while (total < n) {
        size_t want = sizeof(buf);
        if ((int64_t)want > n - total) want = (size_t)(n - total);
        size_t nr = 0;
        int err = src->read(src->ctx, buf, want, &nr);
        if (nr > want) return total;
        if (nr > 0) {
            empty_reads = 0;
            size_t to_write = copy_count_room(total);
            if (to_write == 0) return total;
            if (to_write > nr) to_write = nr;
            size_t nw = 0;
            int werr = dst->write(dst->ctx, buf, to_write, &nw);
            if (nw > to_write) return total;
            if (add_copy_count(&total, nw)) return total;
            if (werr != 0 || nw != to_write || to_write < nr) return total;
        }
        if (err == NEVERC_IO_EOF) break;
        if (err != 0) break;
        if (nr == 0) {
            if (++empty_reads >= NCI_IO_MAX_EMPTY_READS) break;
        }
    }
    return total;
}

int neverc_io_write_string(neverc_io_writer_t *w, const char *s, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    if (!w || !w->write || !s) return NEVERC_IO_ERR_UNEXP;
    size_t len = strlen(s);
    int err = w->write(w->ctx, (const uint8_t *)s, len, n);
    if (*n > len) {
        *n = 0;
        return NEVERC_IO_ERR_UNEXP;
    }
    if (err != 0) return err;
    return *n == len ? 0 : NEVERC_IO_ERR_SHORT;
}

/* Discard writer */
static int discard_write(void *ctx, const uint8_t *buf, size_t len, size_t *n) {
    (void)ctx; (void)buf;
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = len;
    return 0;
}

void neverc_io_discard_init(neverc_io_writer_t *w) {
    if (!w) return;
    w->ctx = NULL;
    w->write = discard_write;
}

/* Memory-backed reader */
void neverc_io_mem_reader_init(neverc_io_mem_reader_t *mr,
                               const uint8_t *data, size_t len) {
    if (!mr) return;
    mr->data = data;
    mr->len = len;
    mr->pos = 0;
}

int neverc_io_mem_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    neverc_io_mem_reader_t *mr = (neverc_io_mem_reader_t *)ctx;
    if (!mr || (!buf && len > 0) || (!mr->data && mr->len > 0))
        return NEVERC_IO_ERR_UNEXP;
    if (mr->pos >= mr->len) return NEVERC_IO_EOF;
    size_t avail = mr->len - mr->pos;
    size_t count = len < avail ? len : avail;
    /* Scalar copy: the const source lets the compiler auto-vectorize this, and
     * it beats a memcpy call for the common small-read sizes (measured). */
    for (size_t i = 0; i < count; i++) buf[i] = mr->data[mr->pos + i];
    mr->pos += count;
    *n = count;
    return 0;
}

/* Memory-backed writer */
void neverc_io_mem_writer_init(neverc_io_mem_writer_t *mw) {
    if (!mw) return;
    mw->cap = 256;
    mw->data = (uint8_t *)malloc(mw->cap);
    mw->len = 0;
    if (!mw->data) mw->cap = 0;
}

int neverc_io_mem_writer_write(void *ctx, const uint8_t *buf, size_t len,
                               size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    neverc_io_mem_writer_t *mw = (neverc_io_mem_writer_t *)ctx;
    if (!mw || (!buf && len > 0)) return NEVERC_IO_ERR_UNEXP;
    if (len == 0) return 0;
    if (len > SIZE_MAX - mw->len) return NEVERC_IO_ERR_UNEXP;
    size_t required = mw->len + len;
    int err = ensure_capacity(&mw->data, &mw->cap, required);
    if (err != 0) return err;
    memcpy(mw->data + mw->len, buf, len);
    mw->len = required;
    *n = len;
    return 0;
}

void neverc_io_mem_writer_free(neverc_io_mem_writer_t *mw) {
    if (!mw) return;
    free(mw->data);
    mw->data = NULL;
    mw->len = mw->cap = 0;
}

int neverc_io_read_at_least(neverc_io_reader_t *r, uint8_t *buf,
                             size_t len, size_t min, size_t *n) {
    if (n) *n = 0;
    if (min > len) return NEVERC_IO_ERR_SHORT;
    if (min == 0) return 0;
    if (!r || !r->read || !buf) return NEVERC_IO_ERR_UNEXP;

    size_t total = 0;
    unsigned empty_reads = 0;
    while (total < min) {
        size_t available = len - total;
        size_t got = 0;
        int rc = r->read(r->ctx, buf + total, available, &got);
        if (got > available) {
            if (n) *n = total;
            return NEVERC_IO_ERR_UNEXP;
        }
        total += got;
        if (total >= min) {
            if (n) *n = total;
            return 0;
        }
        if (rc == NEVERC_IO_EOF) {
            if (n) *n = total;
            return NEVERC_IO_ERR_UNEXP;
        }
        if (rc != 0) { if (n) *n = total; return rc; }
        if (got == 0) {
            if (++empty_reads >= NCI_IO_MAX_EMPTY_READS) break;
            continue;
        }
        empty_reads = 0;
    }
    if (n) *n = total;
    return (total < min) ? NEVERC_IO_ERR_SHORT : 0;
}

int64_t neverc_io_copy_buffer(neverc_io_writer_t *dst, neverc_io_reader_t *src,
                               uint8_t *buf, size_t buflen) {
    if (!dst || !dst->write || !src || !src->read || !buf || buflen == 0)
        return 0;
    int64_t written = 0;
    unsigned empty_reads = 0;
    while (1) {
        size_t nr = 0;
        int rc = src->read(src->ctx, buf, buflen, &nr);
        if (nr > buflen) return written;
        if (nr > 0) {
            empty_reads = 0;
            size_t to_write = copy_count_room(written);
            if (to_write == 0) return written;
            if (to_write > nr) to_write = nr;
            size_t nw = 0;
            int wc = dst->write(dst->ctx, buf, to_write, &nw);
            if (nw > to_write) return written;
            if (add_copy_count(&written, nw)) return written;
            if (wc != 0) return written;
            if (nw != to_write || to_write < nr) return written;
        }
        if (rc == NEVERC_IO_EOF) break;
        if (rc != 0) return written;
        if (nr == 0) {
            if (++empty_reads >= NCI_IO_MAX_EMPTY_READS) break;
        }
    }
    return written;
}

static int limit_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    neverc_io_limit_reader_t *lr = (neverc_io_limit_reader_t *)ctx;
    if (!lr || !lr->inner || !lr->inner->read || (!buf && len > 0))
        return NEVERC_IO_ERR_UNEXP;
    if (lr->remaining <= 0) return NEVERC_IO_EOF;
    if ((uint64_t)lr->remaining < (uint64_t)len)
        len = (size_t)lr->remaining;
    int rc = lr->inner->read(lr->inner->ctx, buf, len, n);
    if (*n > len || (uint64_t)*n > (uint64_t)lr->remaining) {
        *n = 0;
        return NEVERC_IO_ERR_UNEXP;
    }
    lr->remaining -= (int64_t)*n;
    if (lr->remaining < 0) lr->remaining = 0;
    return rc;
}

void neverc_io_limit_reader_init(neverc_io_limit_reader_t *lr,
                                  neverc_io_reader_t *r, int64_t n) {
    if (!lr) return;
    lr->inner = r;
    lr->remaining = n > 0 ? n : 0;
    lr->reader.ctx = lr;
    lr->reader.read = limit_reader_read;
}

static int tee_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    neverc_io_tee_reader_t *tr = (neverc_io_tee_reader_t *)ctx;
    if (!tr || !tr->inner || !tr->inner->read || (!buf && len > 0))
        return NEVERC_IO_ERR_UNEXP;
    int rc = tr->inner->read(tr->inner->ctx, buf, len, n);
    if (*n > len) {
        *n = 0;
        return NEVERC_IO_ERR_UNEXP;
    }
    if (*n > 0 && tr->tee) {
        size_t nr = *n;
        size_t nw = 0;
        if (!tr->tee->write) {
            *n = 0;
            return NEVERC_IO_ERR_UNEXP;
        }
        int write_err = tr->tee->write(tr->tee->ctx, buf, nr, &nw);
        if (nw > nr) {
            *n = 0;
            return NEVERC_IO_ERR_UNEXP;
        }
        *n = nw;
        if (write_err != 0) return write_err;
        if (nw != nr) return NEVERC_IO_ERR_SHORT;
    }
    return rc;
}

void neverc_io_tee_reader_init(neverc_io_tee_reader_t *tr,
                                neverc_io_reader_t *r,
                                neverc_io_writer_t *w) {
    if (!tr) return;
    tr->inner = r;
    tr->tee = w;
    tr->reader.ctx = tr;
    tr->reader.read = tee_reader_read;
}

/* --- MultiReader --- */

int neverc_io_multi_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    neverc_io_multi_reader_t *mr = (neverc_io_multi_reader_t *)ctx;
    if (!mr || (!buf && len > 0) || (mr->count > 0 && !mr->readers))
        return NEVERC_IO_ERR_UNEXP;
    if (len == 0) return 0;
    while (mr->current < mr->count) {
        neverc_io_reader_t *r = &mr->readers[mr->current];
        if (!r->read) return NEVERC_IO_ERR_UNEXP;
        size_t got = 0;
        int rc = r->read(r->ctx, buf, len, &got);
        if (got > len) return NEVERC_IO_ERR_UNEXP;
        if (got > 0) {
            *n = got;
            if (rc == NEVERC_IO_EOF) {
                mr->current++;
                return 0;
            }
            return rc;
        }
        if (rc == NEVERC_IO_EOF) {
            mr->current++;
            continue;
        }
        if (rc != 0) return rc;
        return 0;
    }
    return NEVERC_IO_EOF;
}

void neverc_io_multi_reader_init(neverc_io_multi_reader_t *mr,
                                  neverc_io_reader_t *readers, int count) {
    if (!mr) return;
    mr->readers = readers;
    mr->count = count > 0 ? count : 0;
    mr->current = 0;
    mr->reader.ctx = mr;
    mr->reader.read = neverc_io_multi_reader_read;
}

/* --- MultiWriter --- */

int neverc_io_multi_writer_write(void *ctx, const uint8_t *buf, size_t len, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    neverc_io_multi_writer_t *mw = (neverc_io_multi_writer_t *)ctx;
    if (!mw || (!buf && len > 0) || (mw->count > 0 && !mw->writers))
        return NEVERC_IO_ERR_UNEXP;
    for (int i = 0; i < mw->count; i++) {
        if (!mw->writers[i].write) return NEVERC_IO_ERR_UNEXP;
        size_t nw = 0;
        int rc = mw->writers[i].write(mw->writers[i].ctx, buf, len, &nw);
        if (nw > len) return NEVERC_IO_ERR_UNEXP;
        if (rc != 0) return rc;
        if (nw != len) return NEVERC_IO_ERR_SHORT;
    }
    *n = len;
    return 0;
}

void neverc_io_multi_writer_init(neverc_io_multi_writer_t *mw,
                                  neverc_io_writer_t *writers, int count) {
    if (!mw) return;
    mw->writers = writers;
    mw->count = count > 0 ? count : 0;
    mw->writer.ctx = mw;
    mw->writer.write = neverc_io_multi_writer_write;
}

/* --- Pipe --- */

static int pipe_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    neverc_io_pipe_t *p = (neverc_io_pipe_t *)ctx;
    if (!p || (!buf && len > 0) || (!p->buf && p->len > 0))
        return NEVERC_IO_ERR_UNEXP;
    if (p->len == 0) return p->closed ? NEVERC_IO_EOF : 0;
    if (len == 0) return 0;
    size_t to_read = len < p->len ? len : p->len;
    memcpy(buf, p->buf, to_read);
    size_t remaining = p->len - to_read;
    memmove(p->buf, p->buf + to_read, remaining);   /* ranges overlap when to_read < remaining */
    p->len = remaining;
    *n = to_read;
    return 0;
}

static int pipe_write(void *ctx, const uint8_t *buf, size_t len, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    neverc_io_pipe_t *p = (neverc_io_pipe_t *)ctx;
    if (!p || (!buf && len > 0)) return NEVERC_IO_ERR_UNEXP;
    if (p->closed) return NEVERC_IO_ERR_UNEXP;
    if (len == 0) return 0;
    if (len > SIZE_MAX - p->len) return NEVERC_IO_ERR_UNEXP;
    size_t required = p->len + len;
    int err = ensure_capacity(&p->buf, &p->cap, required);
    if (err != 0) return err;
    memcpy(p->buf + p->len, buf, len);
    p->len = required;
    *n = len;
    return 0;
}

void neverc_io_pipe(neverc_io_pipe_t *pipe_ctx,
                     neverc_io_reader_t *r, neverc_io_writer_t *w) {
    if (!pipe_ctx || !r || !w) return;
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
    if (!nc) return;
    nc->inner = r;
    if (r) nc->reader = *r;
    else {
        nc->reader.ctx = NULL;
        nc->reader.read = NULL;
    }
    nc->closer.ctx = nc;
    nc->closer.close = nop_close;
}
