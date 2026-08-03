#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

static void *controlled_malloc(size_t size) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at ? NULL : malloc(size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at
               ? NULL : realloc(ptr, size);
}

#define malloc controlled_malloc
#define realloc controlled_realloc
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
    neverc_tar_writer_t w;
    fail_at = 0;
    allocation_count = 0;
    neverc_tar_writer_init(&w);
    CHECK(w.data != NULL);

    fail_at = allocation_count + 1;
    CHECK(neverc_tar_writer_write(&w, (const uint8_t *)"x", 8192) == -1);
    CHECK(w.data != NULL);
    CHECK(w.len == 0);
    neverc_tar_writer_free(&w);
    puts("passed");
    return 0;
}
