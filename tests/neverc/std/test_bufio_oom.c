#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    return allocation_fails() ? NULL : malloc(size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    return allocation_fails() ? NULL : realloc(ptr, size);
}

#define malloc controlled_malloc
#define realloc controlled_realloc
#include "../../../std/src/bufio/bufio.c"
#undef malloc
#undef realloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",             \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

typedef struct {
    size_t remaining;
} long_reader_t;

static int long_reader_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    long_reader_t *reader = (long_reader_t *)ctx;
    size_t take = reader->remaining < len ? reader->remaining : len;
    memset(buf, 'x', take);
    reader->remaining -= take;
    *n = take;
    return reader->remaining == 0 ? NEVERC_IO_EOF : 0;
}

int main(void) {
    neverc_io_reader_t empty_reader = {0};

    reset_allocator(1);
    neverc_bufio_scanner_t scanner;
    neverc_bufio_scanner_init(&scanner, empty_reader);
    CHECK(neverc_bufio_scanner_scan(&scanner) == 0);
    CHECK(neverc_bufio_scanner_err(&scanner) == NEVERC_IO_ERR_UNEXP);
    neverc_bufio_scanner_free(&scanner);

    reset_allocator(1);
    neverc_bufio_reader_t reader;
    neverc_bufio_reader_init_size(&reader, empty_reader, 8);
    uint8_t byte = 0;
    CHECK(neverc_bufio_reader_read_byte(&reader, &byte) ==
          NEVERC_IO_ERR_UNEXP);
    neverc_bufio_reader_free(&reader);

    reset_allocator(1);
    neverc_bufio_writer_t writer;
    neverc_io_writer_t empty_writer = {0};
    neverc_bufio_writer_init_size(&writer, empty_writer, 8);
    size_t n = 99;
    CHECK(neverc_bufio_writer_write(
              &writer, (const uint8_t *)"x", 1, &n) == NEVERC_IO_ERR_UNEXP);
    CHECK(n == 0);
    neverc_bufio_writer_free(&writer);

    long_reader_t long_reader = { NEVERC_BUFIO_DEFAULT_SIZE + 1000 };
    neverc_io_reader_t long_io = { &long_reader, long_reader_read };
    reset_allocator(0);
    neverc_bufio_scanner_init(&scanner, long_io);
    reset_allocator(1);
    CHECK(neverc_bufio_scanner_scan(&scanner) == 0);
    CHECK(neverc_bufio_scanner_err(&scanner) == NEVERC_IO_ERR_UNEXP);
    CHECK(neverc_bufio_scanner_scan(&scanner) == 0);
    size_t len = 99;
    CHECK(neverc_bufio_scanner_bytes(&scanner, &len) == NULL);
    CHECK(len == 0);
    neverc_bufio_scanner_free(&scanner);

    puts("passed");
    return 0;
}
