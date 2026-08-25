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

    neverc_tar_header_t header = {0};
    strcpy(header.name, "large");
    header.size = 8192;
    header.mode = 0600;
    header.typeflag = NEVERC_TAR_REG;
    CHECK(neverc_tar_writer_write_header(&w, &header) == 0);
    CHECK(w.len == NEVERC_TAR_BLOCK_SIZE);

    uint8_t data[8192] = {0};
    fail_at = allocation_count + 1;
    CHECK(neverc_tar_writer_write(&w, data, sizeof(data)) == -1);
    CHECK(allocation_count == fail_at);
    CHECK(w.data != NULL);
    CHECK(w.len == NEVERC_TAR_BLOCK_SIZE);
    CHECK(neverc_tar_writer_close(&w) == -1);
    neverc_tar_writer_free(&w);
    puts("passed");
    return 0;
}
