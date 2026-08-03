#include <stdio.h>
#include <stdlib.h>

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
#include "../../../std/src/io/fs/fs.c"
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
    reset_allocator(0);
    neverc_fs_dir_entry_t *entries = NULL;
    size_t count = 0;
    CHECK(neverc_fs_read_dir(".", &entries, &count) == 0);
    size_t dir_allocations = allocation_count;
    neverc_fs_free_entries(entries);

    for (size_t failure = 1; failure <= dir_allocations; failure++) {
        reset_allocator(failure);
        entries = (neverc_fs_dir_entry_t *)1;
        count = 99;
        CHECK(neverc_fs_read_dir(".", &entries, &count) == -1);
        CHECK(entries == NULL);
        CHECK(count == 0);
    }

    reset_allocator(0);
    char **matches = NULL;
    count = 0;
    CHECK(neverc_fs_glob(".", "*", &matches, &count) == 0);
    size_t glob_allocations = allocation_count;
    neverc_fs_free_matches(matches, count);

    for (size_t failure = 1; failure <= glob_allocations; failure++) {
        reset_allocator(failure);
        matches = (char **)1;
        count = 99;
        CHECK(neverc_fs_glob(".", "*", &matches, &count) == -1);
        CHECK(matches == NULL);
        CHECK(count == 0);
    }
    puts("passed");
    return 0;
}
