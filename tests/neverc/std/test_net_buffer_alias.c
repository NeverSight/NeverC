#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *live_allocation;
static size_t live_size;
static void *retired_allocation;
static int force_move;

static void *moving_realloc(void *ptr, size_t size) {
    if (!ptr) {
        void *allocation = malloc(size);
        if (allocation) {
            live_allocation = allocation;
            live_size = size;
        }
        return allocation;
    }
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

#define NC_NET_REALLOC moving_realloc
#include "../../../std/src/net/_net_buffer.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    char initial[200];
    for (size_t i = 0; i < sizeof(initial); i++)
        initial[i] = (char)('A' + (i % 26U));

    nc_buf_t buffer;
    nc_buf_init(&buffer);
    CHECK(nc_buf_append(&buffer, initial, sizeof(initial)) == 0);
    CHECK(buffer.cap == 256U);

    const char *aliased = buffer.data + 50U;
    char expected[100];
    memcpy(expected, aliased, sizeof(expected));

    force_move = 1;
    CHECK(nc_buf_append(&buffer, aliased, sizeof(expected)) == 0);
    force_move = 0;

    CHECK(retired_allocation != NULL);
    CHECK(buffer.data != retired_allocation);
    CHECK(buffer.len == sizeof(initial) + sizeof(expected));
    CHECK(memcmp(buffer.data, initial, sizeof(initial)) == 0);
    CHECK(memcmp(buffer.data + sizeof(initial),
                 expected, sizeof(expected)) == 0);
    CHECK(buffer.data[buffer.len] == '\0');

    nc_buf_free(&buffer);
    free(retired_allocation);
    puts("passed");
    return 0;
}
