#include "neverc/std/bufio.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define NCI_BUFIO_MAX_EMPTY_READS 100
#define NCI_BUFIO_MAX_EMPTY_TOKENS 100

#define NCI_BUFIO_SCANNER_META_MAGIC UINT32_C(0x53434e31)
#define NCI_BUFIO_READER_META_MAGIC  UINT32_C(0x52445231)
#define NCI_BUFIO_WRITER_META_MAGIC  UINT32_C(0x57545231)

typedef struct {
    neverc_bufio_split_func_t split;
    size_t text_saved_at;
    uint32_t magic;
    uint8_t text_saved_byte;
    uint8_t text_saved;
    unsigned consecutive_empty_tokens;
} nci_bufio_scanner_meta_t;

typedef struct {
    uint32_t magic;
    int last_byte;
} nci_bufio_reader_meta_t;

typedef struct {
    uint32_t magic;
    int err;
} nci_bufio_writer_meta_t;

static void *bufio_alloc_with_trailer(size_t capacity, size_t trailer_size) {
    if (capacity > SIZE_MAX - trailer_size) return NULL;
    return malloc(capacity + trailer_size);
}

/* Trailer objects may start at an address with only byte alignment. Always
 * copy them through aligned locals; never cast buf+buf_cap to a struct. */
static int bufio_scanner_meta_load(const neverc_bufio_scanner_t *s,
                                   nci_bufio_scanner_meta_t *meta) {
    if (!s || !meta || !s->buf ||
        s->buf_cap > SIZE_MAX - sizeof(*meta))
        return 0;
    memcpy(meta, s->buf + s->buf_cap, sizeof(*meta));
    return meta->magic == NCI_BUFIO_SCANNER_META_MAGIC && meta->split;
}

static int bufio_scanner_meta_store(neverc_bufio_scanner_t *s,
                                    const nci_bufio_scanner_meta_t *meta) {
    if (!s || !meta || !s->buf ||
        s->buf_cap > SIZE_MAX - sizeof(*meta))
        return 0;
    memcpy(s->buf + s->buf_cap, meta, sizeof(*meta));
    return 1;
}

static int bufio_scanner_grow(neverc_bufio_scanner_t *s, size_t new_cap) {
    nci_bufio_scanner_meta_t meta;
    if (!s || new_cap <= s->buf_cap ||
        new_cap > SIZE_MAX - sizeof(meta) ||
        !bufio_scanner_meta_load(s, &meta))
        return 0;
    size_t old_cap = s->buf_cap;
    uint8_t *new_buf = (uint8_t *)realloc(
        s->buf, new_cap + sizeof(meta));
    if (!new_buf) return 0;
    s->buf = new_buf;
    s->buf_cap = new_cap;
    /* The old trailer is ordinary spare buffer space after a grow. Clear it
     * so private function pointers/state can never become scanner input. */
    size_t clear = sizeof(meta);
    if (clear > new_cap - old_cap) clear = new_cap - old_cap;
    memset(new_buf + old_cap, 0, clear);
    return bufio_scanner_meta_store(s, &meta);
}

static int bufio_reader_meta_load(const neverc_bufio_reader_t *br,
                                  nci_bufio_reader_meta_t *meta) {
    if (!br || !meta || !br->buf ||
        br->buf_cap > SIZE_MAX - sizeof(*meta))
        return 0;
    memcpy(meta, br->buf + br->buf_cap, sizeof(*meta));
    return meta->magic == NCI_BUFIO_READER_META_MAGIC;
}

static int bufio_reader_meta_store(neverc_bufio_reader_t *br,
                                   const nci_bufio_reader_meta_t *meta) {
    if (!br || !meta || !br->buf ||
        br->buf_cap > SIZE_MAX - sizeof(*meta))
        return 0;
    memcpy(br->buf + br->buf_cap, meta, sizeof(*meta));
    return 1;
}

static int bufio_reader_last_byte(const neverc_bufio_reader_t *br) {
    nci_bufio_reader_meta_t meta;
    return bufio_reader_meta_load(br, &meta) ? meta.last_byte : -1;
}

static int bufio_reader_set_last_byte(neverc_bufio_reader_t *br, int value) {
    nci_bufio_reader_meta_t meta;
    if (!bufio_reader_meta_load(br, &meta)) return 0;
    meta.last_byte = value;
    return bufio_reader_meta_store(br, &meta);
}

static int bufio_writer_meta_load(const neverc_bufio_writer_t *bw,
                                  nci_bufio_writer_meta_t *meta) {
    if (!bw || !meta || !bw->buf ||
        bw->buf_cap > SIZE_MAX - sizeof(*meta))
        return 0;
    memcpy(meta, bw->buf + bw->buf_cap, sizeof(*meta));
    return meta->magic == NCI_BUFIO_WRITER_META_MAGIC;
}

static int bufio_writer_meta_store(neverc_bufio_writer_t *bw,
                                   const nci_bufio_writer_meta_t *meta) {
    if (!bw || !meta || !bw->buf ||
        bw->buf_cap > SIZE_MAX - sizeof(*meta))
        return 0;
    memcpy(bw->buf + bw->buf_cap, meta, sizeof(*meta));
    return 1;
}

static int bufio_writer_error(const neverc_bufio_writer_t *bw) {
    nci_bufio_writer_meta_t meta;
    return bufio_writer_meta_load(bw, &meta) ? meta.err : 0;
}

static int bufio_writer_set_error(neverc_bufio_writer_t *bw, int err) {
    nci_bufio_writer_meta_t meta;
    if (!bufio_writer_meta_load(bw, &meta)) return 0;
    meta.err = err;
    return bufio_writer_meta_store(bw, &meta);
}

/* --- Scanner --- */

static void bufio_scanner_restore_text(neverc_bufio_scanner_t *s) {
    nci_bufio_scanner_meta_t meta;
    if (!bufio_scanner_meta_load(s, &meta) || !meta.text_saved) return;
    if (meta.text_saved_at < s->buf_cap)
        s->buf[meta.text_saved_at] = meta.text_saved_byte;
    meta.text_saved = 0;
    (void)bufio_scanner_meta_store(s, &meta);
}

static void bufio_scanner_fail(neverc_bufio_scanner_t *s, int err) {
    bufio_scanner_restore_text(s);
    s->err = err;
    s->done = 1;
    s->start = s->buf_len;
    s->token = NULL;
    s->token_len = 0;
}

static int bufio_utf8_decode(const uint8_t *s, size_t n, uint32_t *rune) {
    if (n < 1) return 0;
    unsigned c = s[0];
    if (c < 0x80) {
        *rune = c;
        return 1;
    }
    if (c < 0xC2 || c >= 0xF5) {
        *rune = 0xFFFD;
        return 1;
    }
    if (c < 0xE0) {
        if (n < 2 || (s[1] & 0xC0) != 0x80) {
            *rune = 0xFFFD;
            return 1;
        }
        *rune = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if (c < 0xF0) {
        if (n < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
            *rune = 0xFFFD;
            return 1;
        }
        uint32_t r = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        if (r < 0x800 || (r >= 0xD800 && r <= 0xDFFF)) {
            *rune = 0xFFFD;
            return 1;
        }
        *rune = r;
        return 3;
    }
    if (n < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 ||
        (s[3] & 0xC0) != 0x80) {
        *rune = 0xFFFD;
        return 1;
    }
    uint32_t r = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                 ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    if (r < 0x10000 || r > 0x10FFFF) {
        *rune = 0xFFFD;
        return 1;
    }
    *rune = r;
    return 4;
}

/*
 * Go unicode/utf8.FullRune inverted: wait only when more bytes could
 * still complete a valid rune. A bad continuation is a finished width-1
 * error rune (ScanWords then splits on the following space).
 */
static int bufio_utf8_incomplete(const uint8_t *s, size_t n) {
    size_t need;
    size_t i;
    unsigned c;
    if (n < 1) return 0;
    c = s[0];
    if (c < 0xC2 || c >= 0xF5) return 0;
    if (c < 0xE0) need = 2;
    else if (c < 0xF0) need = 3;
    else need = 4;
    if (n >= need) return 0;
    for (i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80)
            return 0;
    }
    return 1;
}

/* Go unicode.IsSpace, used by bufio.ScanWords. */
static int bufio_rune_is_space(uint32_t r) {
    switch (r) {
    case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
    case 0x85: case 0xA0: case 0x1680:
    case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
        return 1;
    default:
        return r >= 0x2000 && r <= 0x200A;
    }
}

static void bufio_split_clear(size_t *advance, const uint8_t **token,
                              size_t *token_len, int *err) {
    if (advance) *advance = 0;
    if (token) *token = NULL;
    if (token_len) *token_len = 0;
    if (err) *err = 0;
}

int neverc_bufio_scan_lines(const uint8_t *data, size_t data_len, int at_eof,
                            size_t *advance, const uint8_t **token,
                            size_t *token_len, int *err) {
    bufio_split_clear(advance, token, token_len, err);
    if (!advance || !token || !token_len) return -1;
    if (data_len > 0 && !data) {
        if (err) *err = NEVERC_IO_ERR_UNEXP;
        return -1;
    }
    if (at_eof && data_len == 0) return 0;

    const uint8_t *nl = data_len ? (const uint8_t *)memchr(data, '\n', data_len)
                                 : NULL;
    if (nl) {
        size_t i = (size_t)(nl - data);
        size_t tlen = i;
        if (tlen > 0 && data[tlen - 1] == '\r') tlen--;
        *advance = i + 1;
        *token = data;
        *token_len = tlen;
        return 1;
    }
    if (at_eof) {
        size_t tlen = data_len;
        if (tlen > 0 && data[tlen - 1] == '\r') tlen--;
        *advance = data_len;
        *token = data;
        *token_len = tlen;
        return 1;
    }
    return 0;
}

int neverc_bufio_scan_words(const uint8_t *data, size_t data_len, int at_eof,
                            size_t *advance, const uint8_t **token,
                            size_t *token_len, int *err) {
    bufio_split_clear(advance, token, token_len, err);
    if (!advance || !token || !token_len) return -1;
    if (data_len > 0 && !data) {
        if (err) *err = NEVERC_IO_ERR_UNEXP;
        return -1;
    }

    size_t start = 0;
    while (start < data_len) {
        if (!at_eof && bufio_utf8_incomplete(data + start, data_len - start)) {
            *advance = start;
            return 0;
        }
        uint32_t r;
        int w = bufio_utf8_decode(data + start, data_len - start, &r);
        if (w < 1) break;
        if (!bufio_rune_is_space(r)) break;
        start += (size_t)w;
    }
    size_t i = start;
    while (i < data_len) {
        if (!at_eof && bufio_utf8_incomplete(data + i, data_len - i)) {
            *advance = start;
            return 0;
        }
        uint32_t r;
        int w = bufio_utf8_decode(data + i, data_len - i, &r);
        if (w < 1) break;
        if (bufio_rune_is_space(r)) {
            *advance = i + (size_t)w;
            *token = data + start;
            *token_len = i - start;
            return 1;
        }
        i += (size_t)w;
    }
    if (at_eof && start < data_len) {
        *advance = data_len;
        *token = data + start;
        *token_len = data_len - start;
        return 1;
    }
    *advance = start;
    return 0;
}

int neverc_bufio_scan_bytes(const uint8_t *data, size_t data_len, int at_eof,
                            size_t *advance, const uint8_t **token,
                            size_t *token_len, int *err) {
    bufio_split_clear(advance, token, token_len, err);
    if (!advance || !token || !token_len) return -1;
    (void)at_eof;
    if (data_len == 0) return 0;
    if (!data) {
        if (err) *err = NEVERC_IO_ERR_UNEXP;
        return -1;
    }
    *advance = 1;
    *token = data;
    *token_len = 1;
    return 1;
}

void neverc_bufio_scanner_init(neverc_bufio_scanner_t *s,
                               neverc_io_reader_t reader) {
    if (!s) return;
    s->reader = reader;
    s->buf_cap = NEVERC_BUFIO_DEFAULT_SIZE;
    s->buf = (uint8_t *)bufio_alloc_with_trailer(
        s->buf_cap, sizeof(nci_bufio_scanner_meta_t));
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
        return;
    }
    nci_bufio_scanner_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.split = neverc_bufio_scan_lines;
    meta.magic = NCI_BUFIO_SCANNER_META_MAGIC;
    (void)bufio_scanner_meta_store(s, &meta);
}

void neverc_bufio_scanner_split(neverc_bufio_scanner_t *s,
                                neverc_bufio_split_func_t split) {
    if (!s) return;
    nci_bufio_scanner_meta_t meta;
    if (!bufio_scanner_meta_load(s, &meta)) return;
    meta.split = split ? split : neverc_bufio_scan_lines;
    (void)bufio_scanner_meta_store(s, &meta);
}

int neverc_bufio_scanner_scan(neverc_bufio_scanner_t *s) {
    if (!s) return 0;
    bufio_scanner_restore_text(s);
    /* Go assigns s.token on every split call, so a scan that reports false
     * never leaves the previous token visible. The NUL written for it was
     * just restored, so the stale pointer is no longer terminated either. */
    s->token = NULL;
    s->token_len = 0;
    if (s->done && s->start >= s->buf_len) return 0;
    if (!s->buf || s->buf_cap == 0 || !s->reader.read ||
        s->start > s->buf_len || s->buf_len >= s->buf_cap) {
        bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
        return 0;
    }

    nci_bufio_scanner_meta_t meta;
    if (!bufio_scanner_meta_load(s, &meta)) {
        bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
        return 0;
    }
    neverc_bufio_split_func_t split = meta.split;
    unsigned empty_reads = 0;

    for (;;) {
        size_t data_len = s->buf_len - s->start;
        /* Go bufio.SplitFunc: never called with empty data unless atEOF.
         * A ScanBytes port that indexes data[0] after only checking
         * (atEOF && len==0) would otherwise fail on the first Scan. */
        if (data_len > 0 || s->done) {
            size_t advance = 0;
            const uint8_t *token = NULL;
            size_t token_len = 0;
            int split_err = 0;
            int split_rc = split(s->buf + s->start, data_len, s->done,
                                 &advance, &token, &token_len, &split_err);
            if (split_rc < 0) {
                bufio_scanner_fail(s, split_err != 0 ? split_err
                                                     : NEVERC_IO_ERR_UNEXP);
                return 0;
            }
            if (advance > data_len) {
                bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
                return 0;
            }
            if (split_rc > 0) {
                const uint8_t *window = s->buf + s->start;
                if (!token || token < window || token > window + data_len ||
                    token_len > (size_t)(window + data_len - token) ||
                    token_len > (size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE) {
                    bufio_scanner_fail(s, token_len >
                                              (size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE
                                          ? NEVERC_BUFIO_ERR_TOO_LONG
                                          : NEVERC_IO_ERR_UNEXP);
                    return 0;
                }
                /* Go permits zero-advance tokens at EOF, but stops a split
                 * function that returns too many without making progress. */
                if (s->done && advance == 0) {
                    if (meta.consecutive_empty_tokens >=
                        NCI_BUFIO_MAX_EMPTY_TOKENS) {
                        bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
                        return 0;
                    }
                    meta.consecutive_empty_tokens++;
                } else {
                    meta.consecutive_empty_tokens = 0;
                }
                s->token = token;
                s->token_len = token_len;
                size_t token_end = (size_t)(token - s->buf) + token_len;
                if (token_end < s->buf_cap) {
                    meta.text_saved_byte = s->buf[token_end];
                    meta.text_saved_at = token_end;
                    meta.text_saved = 1;
                    s->buf[token_end] = '\0';
                }
                if (!bufio_scanner_meta_store(s, &meta)) {
                    bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
                    return 0;
                }
                s->start += advance;
                return 1;
            }

            /* Need more data. Honor skip-advance (ScanWords leading space). */
            s->start += advance;

            if (s->done) return 0;
        }

        if (s->start > 0) {
            if (s->start > s->buf_len) {
                bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
                return 0;
            }
            size_t remaining = s->buf_len - s->start;
            if (remaining > 0)
                memmove(s->buf, s->buf + s->start, remaining);
            s->buf_len = remaining;
            s->start = 0;
        }

        /* Keep one byte free so scanner_text() can always expose a
         * NUL-terminated token, including data returned together with EOF.
         * The data limit is MaxScanTokenSize itself (Go): a 64KiB line plus
         * its newline must not fit. A trailing probe byte used to accept that
         * pair and return a max-sized token instead of ErrTooLong. */
        if (s->buf_len >= s->buf_cap - 1) {
            size_t max_cap = (size_t)NEVERC_BUFIO_MAX_SCAN_TOKEN_SIZE + 1;
            if (s->buf_cap >= max_cap) {
                /* Go: a max-sized token at EOF is allowed. The extra
                 * slot is reserved for scanner_text's NUL terminator, so
                 * probe into a local byte. A zero-length Read cannot
                 * distinguish EOF from temporary lack of progress. */
                if (!s->done) {
                    uint8_t probe_byte = 0;
                    size_t probe = 0;
                    int perr = s->reader.read(s->reader.ctx,
                                              &probe_byte, 1, &probe);
                    if (probe > 1) {
                        bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
                        return 0;
                    }
                    if (probe > 0) {
                        bufio_scanner_fail(s, NEVERC_BUFIO_ERR_TOO_LONG);
                        return 0;
                    }
                    if (perr == NEVERC_IO_EOF) {
                        s->done = 1;
                        continue;
                    }
                    if (perr != 0) {
                        s->err = perr;
                        s->done = 1;
                        /* Go records the error and lets the split function
                         * recover the buffered final token on this same
                         * Scan. Returning here drops it and leaves the
                         * scanner resumable, so the next Scan reports true
                         * after this one reported false. */
                        continue;
                    }
                    if (++empty_reads >= NCI_BUFIO_MAX_EMPTY_READS) {
                        bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
                        return 0;
                    }
                    continue;
                }
                bufio_scanner_fail(s, NEVERC_BUFIO_ERR_TOO_LONG);
                return 0;
            }
            if (s->buf_cap > SIZE_MAX / 2) {
                bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
                return 0;
            }
            size_t new_cap = s->buf_cap * 2;
            if (new_cap > max_cap) new_cap = max_cap;
            if (new_cap <= s->buf_cap) {
                bufio_scanner_fail(s, NEVERC_BUFIO_ERR_TOO_LONG);
                return 0;
            }
            if (!bufio_scanner_grow(s, new_cap)) {
                bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
                return 0;
            }
        }

        size_t available = s->buf_cap - s->buf_len - 1;
        size_t nr = 0;
        int err = s->reader.read(s->reader.ctx,
                                 s->buf + s->buf_len,
                                 available, &nr);
        if (nr > available) {
            bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
            return 0;
        }
        s->buf_len += nr;
        if (err == NEVERC_IO_EOF) {
            s->done = 1;
        } else if (err != 0) {
            s->err = err;
            s->done = 1;
        } else if (nr == 0) {
            if (++empty_reads >= NCI_BUFIO_MAX_EMPTY_READS) {
                bufio_scanner_fail(s, NEVERC_IO_ERR_UNEXP);
                return 0;
            }
        } else {
            empty_reads = 0;
        }
    }
}

const uint8_t *neverc_bufio_scanner_bytes(const neverc_bufio_scanner_t *s,
                                          size_t *len) {
    if (!len) return NULL;
    *len = 0;
    if (!s) return NULL;
    *len = s->token_len;
    return s->token;
}

const char *neverc_bufio_scanner_text(const neverc_bufio_scanner_t *s) {
    return s ? (const char *)s->token : NULL;
}

int neverc_bufio_scanner_err(const neverc_bufio_scanner_t *s) {
    return s ? s->err : NEVERC_IO_ERR_UNEXP;
}

void neverc_bufio_scanner_free(neverc_bufio_scanner_t *s) {
    if (!s) return;
    bufio_scanner_restore_text(s);
    free(s->buf);
    s->buf = NULL;
    s->buf_len = s->buf_cap = s->start = 0;
    s->token = NULL;
    s->token_len = 0;
    s->done = 1;
}

/* --- Buffered Reader --- */

void neverc_bufio_reader_init(neverc_bufio_reader_t *br,
                              neverc_io_reader_t reader) {
    neverc_bufio_reader_init_size(br, reader, NEVERC_BUFIO_DEFAULT_SIZE);
}

void neverc_bufio_reader_init_size(neverc_bufio_reader_t *br,
                                   neverc_io_reader_t reader, size_t size) {
    if (!br) return;
    if (size == 0) size = 1;
    br->reader = reader;
    br->buf_cap = size;
    br->buf = (uint8_t *)bufio_alloc_with_trailer(
        size, sizeof(nci_bufio_reader_meta_t));
    br->r = br->w = 0;
    br->eof = br->buf ? 0 : 1;
    br->err = br->buf ? 0 : NEVERC_IO_ERR_UNEXP;
    if (br->buf) {
        nci_bufio_reader_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        meta.magic = NCI_BUFIO_READER_META_MAGIC;
        meta.last_byte = -1;
        (void)bufio_reader_meta_store(br, &meta);
    }
}

static size_t bufio_fill(neverc_bufio_reader_t *br) {
    if (!br->buf || !br->reader.read) {
        br->eof = 1;
        br->err = NEVERC_IO_ERR_UNEXP;
        return 0;
    }
    if (br->r > 0) {
        size_t n = br->w - br->r;
        if (n > 0) memmove(br->buf, br->buf + br->r, n);
        br->w = n;
        br->r = 0;
    }
    if (br->w >= br->buf_cap) return 0;
    size_t available = br->buf_cap - br->w;
    for (unsigned empty_reads = 0;
         empty_reads < NCI_BUFIO_MAX_EMPTY_READS;
         empty_reads++) {
        size_t nr = 0;
        int err = br->reader.read(br->reader.ctx, br->buf + br->w,
                                  available, &nr);
        if (nr > available) {
            br->eof = 1;
            br->err = NEVERC_IO_ERR_UNEXP;
            return 0;
        }
        br->w += nr;
        if (err != 0) {
            br->eof = 1;
            br->err = err;
            return nr;
        }
        if (nr != 0)
            return nr;
    }
    br->eof = 1;
    br->err = NEVERC_IO_ERR_UNEXP;
    return 0;
}

static int bufio_reader_terminal_error(const neverc_bufio_reader_t *br) {
    return br->err != 0 ? br->err : NEVERC_IO_EOF;
}

int neverc_bufio_reader_read_byte(neverc_bufio_reader_t *br, uint8_t *b) {
    if (!br || !b) return NEVERC_IO_ERR_UNEXP;
    if (br->r >= br->w) {
        if (br->eof) return bufio_reader_terminal_error(br);
        bufio_fill(br);
        if (br->r >= br->w) return bufio_reader_terminal_error(br);
    }
    *b = br->buf[br->r++];
    (void)bufio_reader_set_last_byte(br, *b);
    return 0;
}

int neverc_bufio_reader_unread_byte(neverc_bufio_reader_t *br) {
    int last_byte = bufio_reader_last_byte(br);
    if (!br || !br->buf || last_byte < 0 || br->r > br->w ||
        br->w > br->buf_cap)
        return NEVERC_IO_ERR_UNEXP;
    if (br->r == 0 && br->w > 0) return NEVERC_IO_ERR_UNEXP;
    if (br->r > 0) {
        br->r--;
        br->buf[br->r] = (uint8_t)last_byte;
    } else {
        if (br->buf_cap == 0) return NEVERC_IO_ERR_UNEXP;
        br->buf[0] = (uint8_t)last_byte;
        br->w = 1;
        br->r = 0;
    }
    (void)bufio_reader_set_last_byte(br, -1);
    return 0;
}

int neverc_bufio_reader_read(neverc_bufio_reader_t *br,
                             uint8_t *buf, size_t len, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    if (len == 0) {
        if (!br || br->r > br->w || br->w > br->buf_cap)
            return NEVERC_IO_ERR_UNEXP;
        if (br->r < br->w)
            return 0;
        if (br->eof)
            return bufio_reader_terminal_error(br);
        return 0;
    }
    if (!br || !buf || br->r > br->w || br->w > br->buf_cap)
        return NEVERC_IO_ERR_UNEXP;

    /* Go bufio.Reader.Read copies at most once. Looping until `len` is
     * ReadFull semantics and blocks a short underlying read (a socket
     * that delivered 10 bytes while the caller asked for 100). */
    if (br->r == br->w) {
        if (br->eof)
            return bufio_reader_terminal_error(br);
        if (len >= br->buf_cap) {
            if (!br->reader.read) {
                br->eof = 1;
                br->err = NEVERC_IO_ERR_UNEXP;
                return NEVERC_IO_ERR_UNEXP;
            }
            for (unsigned empty_reads = 0;
                 empty_reads < NCI_BUFIO_MAX_EMPTY_READS;
                 empty_reads++) {
                size_t nr = 0;
                int err = br->reader.read(br->reader.ctx, buf, len, &nr);
                if (nr > len) {
                    br->eof = 1;
                    br->err = NEVERC_IO_ERR_UNEXP;
                    return NEVERC_IO_ERR_UNEXP;
                }
                *n = nr;
                if (nr > 0)
                    (void)bufio_reader_set_last_byte(br, buf[nr - 1]);
                if (err != 0) {
                    br->eof = 1;
                    br->err = err;
                    if (nr == 0)
                        return bufio_reader_terminal_error(br);
                    return err;
                }
                if (nr != 0)
                    return 0;
            }
            br->eof = 1;
            br->err = NEVERC_IO_ERR_UNEXP;
            return NEVERC_IO_ERR_UNEXP;
        }
        bufio_fill(br);
        if (br->r >= br->w)
            return bufio_reader_terminal_error(br);
    }

    size_t avail = br->w - br->r;
    size_t copy = avail < len ? avail : len;
    memcpy(buf, br->buf + br->r, copy);
    br->r += copy;
    *n = copy;
    if (copy > 0)
        (void)bufio_reader_set_last_byte(br, buf[copy - 1]);
    return 0;
}

uint8_t *neverc_bufio_reader_read_line(neverc_bufio_reader_t *br, size_t *len) {
    if (!len) return NULL;
    *len = 0;
    if (!br) return NULL;
    size_t cap = 256, wlen = 0;
    uint8_t *line = (uint8_t *)malloc(cap);
    if (!line) { *len = 0; return NULL; }

    for (;;) {
        uint8_t b;
        int err = neverc_bufio_reader_read_byte(br, &b);
        if (err != 0) {
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
    if (wlen >= cap) {
        if (cap > SIZE_MAX / 2) {
            free(line);
            *len = 0;
            return NULL;
        }
        uint8_t *new_line = (uint8_t *)realloc(line, cap * 2);
        if (!new_line) {
            free(line);
            *len = 0;
            return NULL;
        }
        line = new_line;
    }
    line[wlen] = '\0';
    *len = wlen;
    return line;
}

int neverc_bufio_reader_peek(neverc_bufio_reader_t *br,
                             uint8_t *buf, size_t n) {
    if (!br || (!buf && n > 0) || br->r > br->w || br->w > br->buf_cap)
        return NEVERC_IO_ERR_UNEXP;
    (void)bufio_reader_set_last_byte(br, -1);
    if (n == 0) return 0;
    while (br->w - br->r < n && !br->eof)
        if (bufio_fill(br) == 0) break;
    size_t avail = br->w - br->r;
    size_t copy = avail < n ? avail : n;
    if (copy > INT_MAX) copy = INT_MAX;
    if (copy > 0) memcpy(buf, br->buf + br->r, copy);
    return (int)copy;
}

void neverc_bufio_reader_free(neverc_bufio_reader_t *br) {
    if (!br) return;
    free(br->buf);
    br->buf = NULL;
    br->buf_cap = br->r = br->w = 0;
    br->eof = 1;
    br->err = NEVERC_IO_EOF;
}

/* --- Buffered Writer --- */

void neverc_bufio_writer_init(neverc_bufio_writer_t *bw,
                              neverc_io_writer_t writer) {
    neverc_bufio_writer_init_size(bw, writer, NEVERC_BUFIO_DEFAULT_SIZE);
}

void neverc_bufio_writer_init_size(neverc_bufio_writer_t *bw,
                                   neverc_io_writer_t writer, size_t size) {
    if (!bw) return;
    if (size == 0) size = 1;
    bw->writer = writer;
    bw->buf_cap = size;
    bw->buf = (uint8_t *)bufio_alloc_with_trailer(
        size, sizeof(nci_bufio_writer_meta_t));
    bw->n = 0;
    if (bw->buf) {
        nci_bufio_writer_meta_t meta;
        memset(&meta, 0, sizeof(meta));
        meta.magic = NCI_BUFIO_WRITER_META_MAGIC;
        (void)bufio_writer_meta_store(bw, &meta);
    }
}

int neverc_bufio_writer_flush(neverc_bufio_writer_t *bw) {
    if (!bw || bw->n > bw->buf_cap) return NEVERC_IO_ERR_UNEXP;
    int sticky_error = bufio_writer_error(bw);
    if (sticky_error != 0) return sticky_error;
    if (bw->n == 0) return 0;
    if (!bw->buf || !bw->writer.write) return NEVERC_IO_ERR_UNEXP;
    size_t nw = 0;
    int err = bw->writer.write(bw->writer.ctx, bw->buf, bw->n, &nw);
    if (nw > bw->n) {
        (void)bufio_writer_set_error(bw, NEVERC_IO_ERR_SHORT);
        return NEVERC_IO_ERR_SHORT;
    }
    if (nw == 0) {
        sticky_error = err < 0 ? err : NEVERC_IO_ERR_SHORT;
        (void)bufio_writer_set_error(bw, sticky_error);
        return sticky_error;
    }
    if (nw < bw->n) {
        size_t remaining = bw->n - nw;
        memmove(bw->buf, bw->buf + nw, remaining);
        bw->n = remaining;
        sticky_error = err != 0 ? err : NEVERC_IO_ERR_SHORT;
        (void)bufio_writer_set_error(bw, sticky_error);
        return sticky_error;
    }
    bw->n = 0;
    if (err != 0) {
        (void)bufio_writer_set_error(bw, err);
        return err;
    }
    return 0;
}

int neverc_bufio_writer_write(neverc_bufio_writer_t *bw,
                              const uint8_t *data, size_t len, size_t *n) {
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    if (len == 0) return 0;
    if (!bw || !bw->buf || bw->buf_cap == 0 || !data ||
        bw->n > bw->buf_cap)
        return NEVERC_IO_ERR_UNEXP;
    int sticky_error = bufio_writer_error(bw);
    if (sticky_error != 0) return sticky_error;
    while (*n < len) {
        size_t avail = bw->buf_cap - bw->n;
        size_t copy = (len - *n) < avail ? (len - *n) : avail;
        memcpy(bw->buf + bw->n, data + *n, copy);
        bw->n += copy;
        *n += copy;
        if (bw->n >= bw->buf_cap) {
            int err = neverc_bufio_writer_flush(bw);
            if (err != 0) return err;
        }
    }
    return 0;
}

int neverc_bufio_writer_write_byte(neverc_bufio_writer_t *bw, uint8_t c) {
    if (!bw || !bw->buf || bw->buf_cap == 0 || bw->n > bw->buf_cap)
        return NEVERC_IO_ERR_UNEXP;
    int sticky_error = bufio_writer_error(bw);
    if (sticky_error != 0) return sticky_error;
    if (bw->n >= bw->buf_cap) {
        int err = neverc_bufio_writer_flush(bw);
        if (err != 0) return err;
    }
    bw->buf[bw->n++] = c;
    return 0;
}

void neverc_bufio_writer_free(neverc_bufio_writer_t *bw) {
    if (!bw) return;
    free(bw->buf);
    bw->buf = NULL;
    bw->buf_cap = bw->n = 0;
}
