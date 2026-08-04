#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static size_t allocation_count;
static size_t free_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    return allocation_fails() ? NULL : malloc(size);
}

static void controlled_free(void *ptr) {
    if (ptr) free_count++;
    free(ptr);
}

#define malloc controlled_malloc
#define free controlled_free
#include "../../../std/src/cstring/cstring.c"
#undef malloc
#undef free

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
    free_count = 0;
    fail_at = failure;
}

int main(void) {
    unsigned char byte = 0x5a;
    reset_allocator(0);
    CHECK(nc_strdup((const char *)&byte, SIZE_MAX) == NULL);
    CHECK(allocation_count == 0);
    size_t result = 0;
    CHECK(!nc_size_add(SIZE_MAX, 1, &result));
    CHECK(!nc_size_mul(SIZE_MAX, 2, &result));
    CHECK(nc_alloc_string(SIZE_MAX) == NULL);
    CHECK(allocation_count == 0);

    for (size_t failure = 1; failure <= 4; failure++) {
        size_t count = 99;
        reset_allocator(failure);
        CHECK(neverc_cstring_split("a,b,c", ",", &count) == NULL);
        CHECK(count == 0);
        CHECK(free_count == failure - 1);

        count = 99;
        reset_allocator(failure);
        CHECK(neverc_cstring_split("abc", "", &count) == NULL);
        CHECK(count == 0);
        CHECK(free_count == failure - 1);

        count = 99;
        reset_allocator(failure);
        CHECK(neverc_cstring_fields("alpha beta gamma", &count) == NULL);
        CHECK(count == 0);
        CHECK(free_count == failure - 1);
    }

    char *before = (char *)(uintptr_t)1;
    char *after = (char *)(uintptr_t)1;
    reset_allocator(2);
    CHECK(neverc_cstring_cut("alpha:beta", ":", &before, &after) == -1);
    CHECK(before == NULL);
    CHECK(after == NULL);
    CHECK(free_count == 1);

    before = (char *)(uintptr_t)1;
    reset_allocator(0);
    CHECK(neverc_cstring_cut("alpha", ":", &before, NULL) == -1);
    CHECK(before == NULL);

    before = (char *)(uintptr_t)1;
    after = (char *)(uintptr_t)1;
    reset_allocator(2);
    CHECK(neverc_cstring_cut("alpha", ":", &before, &after) == -1);
    CHECK(before == NULL);
    CHECK(after == NULL);
    CHECK(free_count == 1);

    after = (char *)(uintptr_t)1;
    reset_allocator(0);
    CHECK(neverc_cstring_cut_prefix(NULL, "alpha", &after) == -1);
    CHECK(after == NULL);

    before = (char *)(uintptr_t)1;
    reset_allocator(0);
    CHECK(neverc_cstring_cut_suffix(NULL, "alpha", &before) == -1);
    CHECK(before == NULL);

    puts("passed");
    return 0;
}
