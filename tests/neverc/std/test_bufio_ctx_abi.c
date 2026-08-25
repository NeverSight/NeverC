#include "neverc/std/bufio.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Freeze the released v3389 context layouts without hard-coding one target's
 * pointer size or alignment rules. */
typedef struct {
    neverc_io_reader_t reader;
    uint8_t *buf;
    size_t buf_len;
    size_t buf_cap;
    size_t start;
    const uint8_t *token;
    size_t token_len;
    int done;
    int err;
} v3389_bufio_scanner_t;

typedef struct {
    neverc_io_reader_t reader;
    uint8_t *buf;
    size_t buf_cap;
    size_t r, w;
    int eof;
    int err;
} v3389_bufio_reader_t;

typedef struct {
    neverc_io_writer_t writer;
    uint8_t *buf;
    size_t buf_cap;
    size_t n;
} v3389_bufio_writer_t;

#define ABI_ASSERT(type, legacy, field)                                      \
    _Static_assert(offsetof(type, field) == offsetof(legacy, field),          \
                   #type "." #field " v3389 offset changed")

_Static_assert(sizeof(neverc_bufio_scanner_t) == sizeof(v3389_bufio_scanner_t),
               "neverc_bufio_scanner_t v3389 size changed");
_Static_assert(_Alignof(neverc_bufio_scanner_t) ==
                   _Alignof(v3389_bufio_scanner_t),
               "neverc_bufio_scanner_t v3389 alignment changed");
ABI_ASSERT(neverc_bufio_scanner_t, v3389_bufio_scanner_t, reader);
ABI_ASSERT(neverc_bufio_scanner_t, v3389_bufio_scanner_t, buf);
ABI_ASSERT(neverc_bufio_scanner_t, v3389_bufio_scanner_t, buf_len);
ABI_ASSERT(neverc_bufio_scanner_t, v3389_bufio_scanner_t, buf_cap);
ABI_ASSERT(neverc_bufio_scanner_t, v3389_bufio_scanner_t, start);
ABI_ASSERT(neverc_bufio_scanner_t, v3389_bufio_scanner_t, token);
ABI_ASSERT(neverc_bufio_scanner_t, v3389_bufio_scanner_t, token_len);
ABI_ASSERT(neverc_bufio_scanner_t, v3389_bufio_scanner_t, done);
ABI_ASSERT(neverc_bufio_scanner_t, v3389_bufio_scanner_t, err);

_Static_assert(sizeof(neverc_bufio_reader_t) == sizeof(v3389_bufio_reader_t),
               "neverc_bufio_reader_t v3389 size changed");
_Static_assert(_Alignof(neverc_bufio_reader_t) ==
                   _Alignof(v3389_bufio_reader_t),
               "neverc_bufio_reader_t v3389 alignment changed");
ABI_ASSERT(neverc_bufio_reader_t, v3389_bufio_reader_t, reader);
ABI_ASSERT(neverc_bufio_reader_t, v3389_bufio_reader_t, buf);
ABI_ASSERT(neverc_bufio_reader_t, v3389_bufio_reader_t, buf_cap);
ABI_ASSERT(neverc_bufio_reader_t, v3389_bufio_reader_t, r);
ABI_ASSERT(neverc_bufio_reader_t, v3389_bufio_reader_t, w);
ABI_ASSERT(neverc_bufio_reader_t, v3389_bufio_reader_t, eof);
ABI_ASSERT(neverc_bufio_reader_t, v3389_bufio_reader_t, err);

_Static_assert(sizeof(neverc_bufio_writer_t) == sizeof(v3389_bufio_writer_t),
               "neverc_bufio_writer_t v3389 size changed");
_Static_assert(_Alignof(neverc_bufio_writer_t) ==
                   _Alignof(v3389_bufio_writer_t),
               "neverc_bufio_writer_t v3389 alignment changed");
ABI_ASSERT(neverc_bufio_writer_t, v3389_bufio_writer_t, writer);
ABI_ASSERT(neverc_bufio_writer_t, v3389_bufio_writer_t, buf);
ABI_ASSERT(neverc_bufio_writer_t, v3389_bufio_writer_t, buf_cap);
ABI_ASSERT(neverc_bufio_writer_t, v3389_bufio_writer_t, n);

#undef ABI_ASSERT

static size_t allocation_count;
static size_t fail_at;

static void *controlled_malloc(size_t size) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at ? NULL : malloc(size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at
               ? NULL
               : realloc(ptr, size);
}

/* Compile the implementation into this executable so the allocation-failure
 * cases exercise the same public entry points without allocator hooks in the
 * production library. Registration must use -fno-builtin-std, like bufio_oom. */
#define malloc controlled_malloc
#define realloc controlled_realloc
#include "../../../std/src/bufio/bufio.c"
#undef malloc
#undef realloc

static int failures;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "check failed at line %d: %s\n", __LINE__,      \
                    #condition);                                              \
            failures++;                                                       \
        }                                                                     \
    } while (0)

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} test_reader_t;

static int test_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    test_reader_t *reader = (test_reader_t *)ctx;
    if (!n) return NEVERC_IO_ERR_UNEXP;
    *n = 0;
    if (reader->pos >= reader->len) return NEVERC_IO_EOF;
    size_t amount = reader->len - reader->pos;
    if (amount > len) amount = len;
    if (amount > 0) memcpy(buf, reader->data + reader->pos, amount);
    reader->pos += amount;
    *n = amount;
    return reader->pos == reader->len ? NEVERC_IO_EOF : 0;
}

static int canary_is_intact(const uint8_t *canary, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (canary[i] != 0xa5) return 0;
    return 1;
}

static void test_scanner_layout_grow_and_oom(void) {
    struct {
        neverc_bufio_scanner_t ctx;
        uint8_t canary[32];
    } guarded;
    uint8_t input[5006];
    memset(input, 'x', 5000);
    /* A space distinguishes the configured ScanWords function from the
     * default ScanLines function after the scanner buffer grows. */
    input[5000] = ' ';
    memcpy(input + 5001, "tail", 4);
    input[5005] = '\n';

    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));
    test_reader_t source = { input, sizeof(input), 0 };
    neverc_io_reader_t reader = { &source, test_reader_read };
    allocation_count = 0;
    fail_at = 0;
    neverc_bufio_scanner_init(&guarded.ctx, reader);
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));
    neverc_bufio_scanner_split(&guarded.ctx, neverc_bufio_scan_words);
    CHECK(neverc_bufio_scanner_scan(&guarded.ctx) == 1);
    size_t token_len = 0;
    const uint8_t *token = neverc_bufio_scanner_bytes(&guarded.ctx, &token_len);
    CHECK(token != NULL && token_len == 5000);
    const char *text = neverc_bufio_scanner_text(&guarded.ctx);
    CHECK(text != NULL && text[token_len] == '\0');
    CHECK(neverc_bufio_scanner_scan(&guarded.ctx) == 1);
    token = neverc_bufio_scanner_bytes(&guarded.ctx, &token_len);
    CHECK(token != NULL && token_len == 4 && memcmp(token, "tail", 4) == 0);
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));
    neverc_bufio_scanner_free(&guarded.ctx);
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));

    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));
    source.pos = 0;
    allocation_count = 0;
    fail_at = 0;
    neverc_bufio_scanner_init(&guarded.ctx, reader);
    CHECK(guarded.ctx.buf != NULL);
    fail_at = allocation_count + 1; /* scanner buffer growth realloc */
    CHECK(neverc_bufio_scanner_scan(&guarded.ctx) == 0);
    CHECK(neverc_bufio_scanner_err(&guarded.ctx) == NEVERC_IO_ERR_UNEXP);
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));
    fail_at = 0;
    neverc_bufio_scanner_free(&guarded.ctx);
}

static void test_reader_layout_and_unread_state(void) {
    struct {
        neverc_bufio_reader_t ctx;
        uint8_t canary[32];
    } guarded;
    static const uint8_t input[] = "abcdef";
    test_reader_t source = { input, sizeof(input) - 1, 0 };
    neverc_io_reader_t reader = { &source, test_reader_read };
    uint8_t out[6] = {0};
    size_t n = 0;

    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));
    allocation_count = 0;
    fail_at = 0;
    neverc_bufio_reader_init_size(&guarded.ctx, reader, 4);
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));
    CHECK(neverc_bufio_reader_read(&guarded.ctx, out, sizeof(out), &n) ==
          NEVERC_IO_EOF);
    CHECK(n == sizeof(out) && memcmp(out, input, sizeof(out)) == 0);
    CHECK(neverc_bufio_reader_unread_byte(&guarded.ctx) == 0);
    CHECK(neverc_bufio_reader_unread_byte(&guarded.ctx) ==
          NEVERC_IO_ERR_UNEXP);
    uint8_t byte = 0;
    CHECK(neverc_bufio_reader_read_byte(&guarded.ctx, &byte) == 0);
    CHECK(byte == 'f');
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));
    neverc_bufio_reader_free(&guarded.ctx);
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));
}

typedef struct {
    uint8_t data[32];
    size_t len;
    int partial;
} test_writer_t;

static int test_writer_write(void *ctx, const uint8_t *buf, size_t len,
                             size_t *n) {
    test_writer_t *writer = (test_writer_t *)ctx;
    if (!n) return NEVERC_IO_ERR_UNEXP;
    size_t amount = len;
    if (writer->partial && amount > 1) amount /= 2;
    if (amount > sizeof(writer->data) - writer->len)
        amount = sizeof(writer->data) - writer->len;
    if (amount > 0) memcpy(writer->data + writer->len, buf, amount);
    writer->len += amount;
    *n = amount;
    return 0;
}

static void test_writer_layout_and_sticky_error(void) {
    struct {
        neverc_bufio_writer_t ctx;
        uint8_t canary[32];
    } guarded;
    test_writer_t sink = { {0}, 0, 1 };
    neverc_io_writer_t writer = { &sink, test_writer_write };
    size_t n = 0;

    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));
    allocation_count = 0;
    fail_at = 0;
    neverc_bufio_writer_init_size(&guarded.ctx, writer, 4);
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));
    CHECK(neverc_bufio_writer_write(
              &guarded.ctx, (const uint8_t *)"abcd", 4, &n) ==
          NEVERC_IO_ERR_SHORT);
    CHECK(n == 4);
    sink.partial = 0;
    CHECK(neverc_bufio_writer_flush(&guarded.ctx) == NEVERC_IO_ERR_SHORT);
    CHECK(neverc_bufio_writer_write_byte(&guarded.ctx, 'x') ==
          NEVERC_IO_ERR_SHORT);
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));
    neverc_bufio_writer_free(&guarded.ctx);
    CHECK(canary_is_intact(guarded.canary, sizeof(guarded.canary)));
}

int main(void) {
    test_scanner_layout_grow_and_oom();
    test_reader_layout_and_unread_state();
    test_writer_layout_and_sticky_error();
    if (failures == 0) puts("passed");
    return failures == 0 ? 0 : 1;
}
