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
#include "../../../std/src/io/io.c"
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
} fill_reader_t;

static int fill_read(void *ctx, uint8_t *buf, size_t len, size_t *n) {
    fill_reader_t *reader = (fill_reader_t *)ctx;
    size_t take = reader->remaining < len ? reader->remaining : len;
    memset(buf, 'x', take);
    reader->remaining -= take;
    *n = take;
    return reader->remaining == 0 ? NEVERC_IO_EOF : 0;
}

int main(void) {
    fill_reader_t source = {1};
    neverc_io_reader_t reader = { &source, fill_read };
    size_t len = 99;

    reset_allocator(1);
    CHECK(neverc_io_read_all(&reader, &len) == NULL);
    CHECK(len == 0);

    source.remaining = 5000;
    reset_allocator(2);
    CHECK(neverc_io_read_all(&reader, &len) == NULL);
    CHECK(len == 0);

    neverc_io_mem_writer_t memory;
    reset_allocator(1);
    neverc_io_mem_writer_init(&memory);
    CHECK(memory.data == NULL);
    CHECK(memory.len == 0);
    CHECK(memory.cap == 0);

    reset_allocator(0);
    size_t n = 99;
    CHECK(neverc_io_mem_writer_write(
              &memory, (const uint8_t *)"old", 3, &n) == 0);
    CHECK(n == 3);
    uint8_t *old_data = memory.data;
    size_t old_cap = memory.cap;
    uint8_t payload[300] = {0};

    reset_allocator(1);
    n = 99;
    CHECK(neverc_io_mem_writer_write(
              &memory, payload, sizeof(payload), &n) == NEVERC_IO_ERR_UNEXP);
    CHECK(n == 0);
    CHECK(memory.data == old_data);
    CHECK(memory.len == 3);
    CHECK(memory.cap == old_cap);
    CHECK(memcmp(memory.data, "old", 3) == 0);

    reset_allocator(0);
    CHECK(neverc_io_mem_writer_write(
              &memory, payload, sizeof(payload), &n) == 0);
    CHECK(memory.len == 3 + sizeof(payload));
    neverc_io_mem_writer_free(&memory);

    neverc_io_pipe_t pipe;
    neverc_io_reader_t pipe_reader;
    neverc_io_writer_t pipe_writer;
    neverc_io_pipe(&pipe, &pipe_reader, &pipe_writer);
    reset_allocator(1);
    n = 99;
    CHECK(pipe_writer.write(
              pipe_writer.ctx, payload, sizeof(payload), &n) ==
          NEVERC_IO_ERR_UNEXP);
    CHECK(n == 0);
    CHECK(pipe.buf == NULL);
    CHECK(pipe.len == 0);
    CHECK(pipe.cap == 0);

    reset_allocator(0);
    CHECK(pipe_writer.write(pipe_writer.ctx, payload, sizeof(payload), &n) == 0);
    CHECK(n == sizeof(payload));
    CHECK(pipe.len == sizeof(payload));
    neverc_io_pipe_free(&pipe);

    puts("passed");
    return 0;
}
