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
#include "../../../std/src/text/tabwriter/tabwriter.c"
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

int main(void) {
    neverc_tabwriter_t writer;
    neverc_tabwriter_init(
        &writer, 0, 8, 1, ' ', NEVERC_TABWRITER_STRIP_ESCAPE);

    static const char escaped_output[] = {
        'x', NEVERC_TABWRITER_ESCAPE, '\f', NEVERC_TABWRITER_ESCAPE,
        'a', 'b', 'c', 'd', 'e', 'f', 'g',
        'h', 'i', 'j', 'k', 'l', 'm'
    };
    static const char expected[] = {
        'x', '\f', 'a', 'b', 'c', 'd', 'e', 'f',
        'g', 'h', 'i', 'j', 'k', 'l', 'm'
    };
    neverc_tabwriter_write(
        &writer, escaped_output, sizeof(escaped_output));
    neverc_tabwriter_flush(&writer);

    size_t output_length = 0;
    const char *output =
        neverc_tabwriter_output(&writer, &output_length);
    CHECK(output != NULL);
    CHECK(output_length == sizeof(expected));
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
    CHECK(writer.out_cap == 16U);

    /* The embedded form feed flushes one byte and grows out_buf before the
     * remaining aliased input is consumed. Subsequent reads must rebase. */
    force_move = 1;
    neverc_tabwriter_write(&writer, output, output_length);
    force_move = 0;
    CHECK(retired_allocation != NULL);
    CHECK(writer.out_buf != retired_allocation);
    neverc_tabwriter_flush(&writer);

    output = neverc_tabwriter_output(&writer, &output_length);
    CHECK(output != NULL);
    CHECK(output_length >= 13U);
    CHECK(memcmp(output + output_length - 13U,
                 "abcdefghijklm", 13U) == 0);

    free(writer.out_buf);
    free(retired_allocation);
    puts("passed");
    return 0;
}
