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
#include "../../../std/src/archive/zip/zip.c"
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

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

int main(void) {
    for (size_t failure = 1; failure <= 3; failure++) {
        reset_allocator(failure);
        neverc_zip_writer_t w;
        neverc_zip_writer_init(&w);
        int result = neverc_zip_writer_add(
            &w, "x", (const uint8_t *)"x", 1);
        CHECK(result == -1 || (result == 0 && w.nentries == 1));
        neverc_zip_writer_free(&w);
    }

    reset_allocator(0);
    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);
    uint8_t *large = (uint8_t *)malloc(8192);
    CHECK(large != NULL);
    memset(large, 'x', 8192);
    fail_at = allocation_count + 1;
    CHECK(neverc_zip_writer_add(&w, "large", large, 8192) == -1);
    CHECK(w.nentries == 0);
    fail_at = 0;
    free(large);
    neverc_zip_writer_free(&w);

    for (size_t relative_failure = 1; relative_failure <= 2;
         relative_failure++) {
        reset_allocator(0);
        neverc_zip_writer_init(&w);
        for (int i = 0; i < 16; i++)
            CHECK(neverc_zip_writer_add(&w, "x", (const uint8_t *)"x", 1) == 0);
        fail_at = allocation_count + relative_failure;
        CHECK(neverc_zip_writer_add(&w, "overflow",
                                    (const uint8_t *)"x", 1) == -1);
        CHECK(w.nentries == 16);
        fail_at = 0;
        neverc_zip_writer_free(&w);
    }

    reset_allocator(0);
    neverc_zip_writer_init(&w);
    CHECK(neverc_zip_writer_add(
              &w, "reader", (const uint8_t *)"data", 4) == 0);
    CHECK(neverc_zip_writer_close(&w) == 0);
    for (size_t failure = 1; failure <= 2; failure++) {
        reset_allocator(failure);
        neverc_zip_reader_t r;
        CHECK(neverc_zip_reader_init(&r, w.data, w.len) == -1);
        CHECK(allocation_count >= failure);
        neverc_zip_reader_free(&r);
    }
    fail_at = 0;
    neverc_zip_writer_free(&w);

    reset_allocator(0);
    neverc_zip_writer_init(&w);
    CHECK(neverc_zip_writer_add(&w, "a", (const uint8_t *)"x", 1) == 0);
    CHECK(neverc_zip_writer_add(&w, "b", (const uint8_t *)"y", 1) == 0);
    CHECK(neverc_zip_writer_close(&w) == 0);
    for (size_t failure = 1; failure <= 3; failure++) {
        reset_allocator(failure);
        neverc_zip_reader_t r;
        CHECK(neverc_zip_reader_init(&r, w.data, w.len) == -1);
        CHECK(allocation_count >= failure);
        neverc_zip_reader_free(&r);
    }
    fail_at = 0;
    neverc_zip_writer_free(&w);

    puts("passed");
    return 0;
}
