#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

static void *controlled_realloc(void *ptr, size_t size) {
    allocation_count++;
    if (fail_at != 0 && allocation_count == fail_at)
        return NULL;
    return realloc(ptr, size);
}

#define realloc controlled_realloc
#include "../../../std/src/text/tabwriter/tabwriter.c"
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
    neverc_tabwriter_t writer;
    neverc_tabwriter_init(&writer, 1, 8, 1, ' ', 0);
    neverc_tabwriter_write(&writer, "left\tright", 10);

    allocation_count = 0;
    fail_at = 1;
    neverc_tabwriter_flush(&writer);
    size_t length = 123;
    CHECK(neverc_tabwriter_output(&writer, &length) == NULL);
    CHECK(length == 0);

    /* Failure is sticky and repeated flush/reset operations remain safe. */
    neverc_tabwriter_flush(&writer);
    fail_at = 0;
    neverc_tabwriter_reset(&writer);
    neverc_tabwriter_write(&writer, "ok", 2);
    neverc_tabwriter_flush(&writer);
    const char *output = neverc_tabwriter_output(&writer, &length);
    CHECK(output != NULL);
    CHECK(length == 2);
    CHECK(memcmp(output, "ok", 2) == 0);
    neverc_tabwriter_reset(&writer);

    puts("passed");
    return 0;
}
