#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct allocation_header {
    size_t size;
    struct allocation_header *next;
    max_align_t alignment;
} allocation_header_t;

static allocation_header_t *retired_allocations;
static int retire_moved_allocations;
static int retired_count;

static allocation_header_t *allocation_header(void *ptr) {
    return ptr ? ((allocation_header_t *)ptr) - 1 : NULL;
}

static void retire_allocation(allocation_header_t *header) {
    memset(header + 1, 0xA5, header->size);
    header->next = retired_allocations;
    retired_allocations = header;
    retired_count++;
}

static void *moving_malloc(size_t size) {
    if (size > SIZE_MAX - sizeof(allocation_header_t)) return NULL;
    allocation_header_t *header =
        (allocation_header_t *)malloc(sizeof(*header) + size);
    if (!header) return NULL;
    header->size = size;
    header->next = NULL;
    return header + 1;
}

static void *moving_realloc(void *ptr, size_t size) {
    if (!ptr) return moving_malloc(size);
    if (size > SIZE_MAX - sizeof(allocation_header_t)) return NULL;
    allocation_header_t *header = allocation_header(ptr);
    if (!retire_moved_allocations) {
        header = (allocation_header_t *)realloc(
            header, sizeof(*header) + size);
        if (!header) return NULL;
        header->size = size;
        header->next = NULL;
        return header + 1;
    }

    void *allocation = moving_malloc(size);
    if (!allocation) return NULL;
    size_t copy_size = header->size < size ? header->size : size;
    memcpy(allocation, ptr, copy_size);
    retire_allocation(header);
    return allocation;
}

static void moving_free(void *ptr) {
    if (!ptr) return;
    allocation_header_t *header = allocation_header(ptr);
    if (retire_moved_allocations) {
        retire_allocation(header);
        return;
    }
    free(header);
}

#define malloc moving_malloc
#define realloc moving_realloc
#define free moving_free
#include "../../../std/src/archive/zip/zip.c"
#undef free
#undef realloc
#undef malloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void free_retired_allocations(void) {
    while (retired_allocations) {
        allocation_header_t *next = retired_allocations->next;
        free(retired_allocations);
        retired_allocations = next;
    }
}

int main(void) {
    neverc_zip_writer_t writer;
    neverc_zip_writer_init(&writer);
    CHECK(writer.data != NULL);
    CHECK(writer.entries != NULL);
    CHECK(writer.offsets != NULL);

    uint8_t payload[210];
    memset(payload, 0x3C, sizeof(payload));
    for (int i = 0; i < 16; i++) {
        char name[32];
        (void)snprintf(name, sizeof(name), "entry-%02d.bin", i);
        payload[0] = (uint8_t)i;
        CHECK(neverc_zip_writer_add(
                  &writer, name, payload, sizeof(payload)) == 0);
    }
    CHECK(writer.nentries == 16);
    CHECK(writer.len == 4032U);
    CHECK(writer.cap == 4096U);

    const char *aliased_name = writer.entries[0].name;
    const uint8_t *aliased_data = writer.data;
    uint8_t expected[64];
    memcpy(expected, aliased_data, sizeof(expected));
    size_t old_length = writer.len;

    retire_moved_allocations = 1;
    CHECK(neverc_zip_writer_add(
              &writer, aliased_name, aliased_data, sizeof(expected)) == 0);
    retire_moved_allocations = 0;

    CHECK(retired_count == 3);
    CHECK(writer.nentries == 17);
    CHECK(writer.len == old_length + 30U + 12U + sizeof(expected));
    CHECK(writer.offsets[16] == old_length);
    CHECK(strcmp(writer.entries[16].name, "entry-00.bin") == 0);
    CHECK(memcmp(writer.data + old_length + 30U,
                 "entry-00.bin", 12U) == 0);
    CHECK(memcmp(writer.data + old_length + 42U,
                 expected, sizeof(expected)) == 0);
    CHECK(neverc_zip_writer_close(&writer) == 0);

    neverc_zip_reader_t reader;
    CHECK(neverc_zip_reader_init(
              &reader, writer.data, writer.len) == 0);
    CHECK(neverc_zip_reader_count(&reader) == 17);
    const neverc_zip_file_header_t *entry =
        neverc_zip_reader_file(&reader, 16);
    CHECK(entry != NULL);
    CHECK(strcmp(entry->name, "entry-00.bin") == 0);
    size_t data_length = 0;
    const uint8_t *entry_data =
        neverc_zip_reader_file_data(&reader, 16, &data_length);
    CHECK(entry_data != NULL);
    CHECK(data_length == sizeof(expected));
    CHECK(memcmp(entry_data, expected, sizeof(expected)) == 0);

    neverc_zip_reader_free(&reader);
    neverc_zip_writer_free(&writer);
    free_retired_allocations();
    puts("passed");
    return 0;
}
