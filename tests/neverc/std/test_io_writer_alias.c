#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *live_allocation;
static size_t live_size;
static void *retired_allocation;
static int force_move;

static void *moving_malloc(size_t size) {
    void *allocation = malloc(size);
    if (allocation) {
        live_allocation = allocation;
        live_size = size;
    }
    return allocation;
}

static void *moving_realloc(void *ptr, size_t size) {
    if (!ptr) return moving_malloc(size);
    if (!force_move) {
        void *allocation = realloc(ptr, size);
        if (allocation) {
            live_allocation = allocation;
            live_size = size;
        }
        return allocation;
    }
    if (ptr != live_allocation || retired_allocation) return NULL;

    void *allocation = malloc(size);
    if (!allocation) return NULL;
    size_t copy_size = live_size < size ? live_size : size;
    memcpy(allocation, ptr, copy_size);
    memset(ptr, 0xA5, live_size);
    retired_allocation = ptr;
    live_allocation = allocation;
    live_size = size;
    return allocation;
}

#define malloc moving_malloc
#define realloc moving_realloc
#include "../../../std/src/io/io.c"
#undef realloc
#undef malloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",                 \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(void) {
    live_allocation = NULL;
    live_size = 0;
    if (retired_allocation) {
        free(retired_allocation);
        retired_allocation = NULL;
    }
    force_move = 0;
}

int main(void) {
    uint8_t payload[250];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(i + 1);

    /* mem writer: append a view of the writer's own buffer across growth. */
    reset_allocator();
    neverc_io_mem_writer_t writer;
    neverc_io_mem_writer_init(&writer);
    size_t written = 0;
    CHECK(neverc_io_mem_writer_write(&writer, payload, sizeof(payload),
                                     &written) == 0);
    CHECK(written == sizeof(payload));
    CHECK(writer.len == 250U);
    CHECK(writer.cap == 256U);

    force_move = 1;
    CHECK(neverc_io_mem_writer_write(&writer, writer.data, 100U,
                                     &written) == 0);
    force_move = 0;
    CHECK(retired_allocation != NULL);
    CHECK(writer.data != retired_allocation);
    CHECK(written == 100U);
    CHECK(writer.len == 350U);
    CHECK(memcmp(writer.data, payload, sizeof(payload)) == 0);
    CHECK(memcmp(writer.data + sizeof(payload), payload, 100U) == 0);
    neverc_io_mem_writer_free(&writer);

    /* pipe writer: same aliasing shape through the public buf field. */
    reset_allocator();
    neverc_io_pipe_t pipe_ctx;
    neverc_io_reader_t reader;
    neverc_io_writer_t pipe_writer;
    neverc_io_pipe(&pipe_ctx, &reader, &pipe_writer);
    CHECK(pipe_writer.write(pipe_writer.ctx, payload, sizeof(payload),
                            &written) == 0);
    CHECK(pipe_ctx.len == 250U);
    CHECK(pipe_ctx.cap == 256U);

    force_move = 1;
    CHECK(pipe_writer.write(pipe_writer.ctx, pipe_ctx.buf, 100U,
                            &written) == 0);
    force_move = 0;
    CHECK(retired_allocation != NULL);
    CHECK(pipe_ctx.buf != retired_allocation);
    CHECK(pipe_ctx.len == 350U);
    CHECK(memcmp(pipe_ctx.buf, payload, sizeof(payload)) == 0);
    CHECK(memcmp(pipe_ctx.buf + sizeof(payload), payload, 100U) == 0);
    neverc_io_pipe_free(&pipe_ctx);

    reset_allocator();
    puts("passed");
    return 0;
}
