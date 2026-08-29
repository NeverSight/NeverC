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

/* Model a realloc that moves its allocation while keeping the retired bytes
 * alive and poisoned. This makes stale reads deterministic without asking the
 * test itself to dereference freed storage. */
static void *moving_realloc(void *ptr, size_t size) {
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
#include "../../../std/src/archive/tar/tar.c"
#undef malloc
#undef realloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    CHECK(writer.data != NULL);
    CHECK(sizeof(neverc_tar_header_v2_t) <= writer.cap);

    size_t initial_cap = writer.cap;
    neverc_tar_header_v2_t *alias =
        (neverc_tar_header_v2_t *)(void *)writer.data;
    memset(alias, 0, sizeof(*alias));
    strcpy(alias->name, "alias.bin");
    alias->size = 1;
    alias->mode = 0600;
    alias->typeflag = NEVERC_TAR_REG;

    /* The header is inside the writer allocation, and this write must grow
     * that allocation. All header values used afterward must be snapshots. */
    writer.len = writer.cap;
    force_move = 1;
    CHECK(neverc_tar_writer_write_header_v2(&writer, alias) == 0);
    force_move = 0;
    CHECK(retired_allocation != NULL);
    CHECK(writer.data != retired_allocation);
    CHECK(writer.cap > initial_cap);
    CHECK(writer.len == initial_cap + NEVERC_TAR_BLOCK_SIZE);
    CHECK(memcmp(writer.data + initial_cap, "alias.bin", 9) == 0);

    uint8_t body = 0x5A;
    CHECK(neverc_tar_writer_write(&writer, &body, 1) == 0);
    CHECK(neverc_tar_writer_close(&writer) == 0);

    neverc_tar_writer_free(&writer);
    free(retired_allocation);
    retired_allocation = NULL;

    /* Entry data can also be a view into the writer allocation. Growing the
     * destination must rebase that view before copying the payload. */
    neverc_tar_writer_init(&writer);
    CHECK(writer.data != NULL);
    neverc_tar_header_v2_t header;
    memset(&header, 0, sizeof(header));
    strcpy(header.name, "body-alias.bin");
    header.size = 1;
    header.mode = 0600;
    header.typeflag = NEVERC_TAR_REG;
    CHECK(neverc_tar_writer_write_header_v2(&writer, &header) == 0);

    initial_cap = writer.cap;
    writer.data[0] = 0x5A;
    const uint8_t *body_alias = writer.data;
    writer.len = writer.cap;
    force_move = 1;
    CHECK(neverc_tar_writer_write(&writer, body_alias, 1) == 0);
    force_move = 0;
    CHECK(retired_allocation != NULL);
    CHECK(writer.data != retired_allocation);
    CHECK(writer.cap > initial_cap);
    CHECK(writer.data[initial_cap] == 0x5A);

    neverc_tar_writer_free(&writer);
    free(retired_allocation);
    retired_allocation = NULL;
    puts("passed");
    return 0;
}
