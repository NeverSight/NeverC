#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t successful_allocations;
static size_t free_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    if (allocation_fails()) return NULL;
    void *result = malloc(size);
    if (result) successful_allocations++;
    return result;
}

static void *controlled_calloc(size_t count, size_t size) {
    if (allocation_fails()) return NULL;
    void *result = calloc(count, size);
    if (result) successful_allocations++;
    return result;
}

static char *controlled_strdup(const char *s) {
    if (allocation_fails()) return NULL;
    size_t length = strlen(s);
    char *copy = (char *)malloc(length + 1);
    if (copy) {
        memcpy(copy, s, length + 1);
        successful_allocations++;
    }
    return copy;
}

static void controlled_free(void *ptr) {
    if (ptr) free_count++;
    free(ptr);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define strdup controlled_strdup
#define free controlled_free
#include "../../../std/src/net/http/http2/http2.c"
#undef malloc
#undef calloc
#undef strdup
#undef free

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
    successful_allocations = 0;
    free_count = 0;
    fail_at = failure;
}

int main(void) {
    hpack_dyn_table_t table;
    dyn_table_init(&table, 4096);

    reset_allocator(0);
    dyn_table_add(&table, "name", "value");
    size_t add_allocations = allocation_count;
    CHECK(add_allocations > 0);
    dyn_table_free(&table);
    CHECK(free_count == successful_allocations);

    for (size_t failure = 1; failure <= add_allocations; failure++) {
        dyn_table_init(&table, 4096);
        reset_allocator(failure);
        dyn_table_add(&table, "name", "value");
        CHECK(table.count == 0);
        CHECK(table.size == 0);
        CHECK(free_count == successful_allocations);
        dyn_table_free(&table);
    }

    neverc_hpack_decoder_t decoder;
    dyn_table_init(&decoder.dyn, 4096);
    decoder.max_table_size = 4096;
    uint8_t indexed_name[] = {0x44, 0x01, 'x'};
    neverc_hpack_header_t headers[1];
    int nheaders = 0;
    reset_allocator(1);
    CHECK(neverc_hpack_decode(&decoder, indexed_name, sizeof(indexed_name),
                              headers, 1, &nheaders) == -1);
    CHECK(nheaders == 0);
    CHECK(free_count == successful_allocations);
    dyn_table_free(&decoder.dyn);

    puts("passed");
    return 0;
}
