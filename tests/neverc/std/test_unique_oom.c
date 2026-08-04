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

static void *controlled_calloc(size_t count, size_t size) {
    return allocation_fails() ? NULL : calloc(count, size);
}

static void controlled_free(void *ptr) {
    if (ptr) free_count++;
    free(ptr);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define free controlled_free
#include "../../../std/src/unique/unique.c"
#undef malloc
#undef calloc
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
    CHECK(intern_alloc(&byte, SIZE_MAX) == NULL);
    CHECK(allocation_count == 0);

    intern_entry_t sentinel = {0};
    g_table = &sentinel;
    g_cap = SIZE_MAX / 2 + 1;
    CHECK(grow_table() == 0);
    CHECK(allocation_count == 0);
    g_table = NULL;
    g_cap = 0;

    reset_allocator(1);
    neverc_unique_init();
    CHECK(neverc_unique_count() == 0);
    CHECK(g_table == NULL);

    reset_allocator(0);
    neverc_unique_init();
    CHECK(g_table != NULL);
    CHECK(neverc_unique_count() == 0);

    reset_allocator(1);
    neverc_unique_handle_t failed = neverc_unique_make_string("copy fails");
    CHECK(!neverc_unique_handle_valid(failed));
    CHECK(neverc_unique_count() == 0);

    reset_allocator(0);
    for (int64_t value = 0; value < 192; value++) {
        CHECK(neverc_unique_handle_valid(neverc_unique_make_int64(value)));
    }
    CHECK(g_cap == INTERN_INIT_CAP);
    CHECK(neverc_unique_count() == 192);

    fail_at = allocation_count + 1;
    neverc_unique_handle_t fallback = neverc_unique_make_int64(1000);
    CHECK(neverc_unique_handle_valid(fallback));
    CHECK(g_cap == INTERN_INIT_CAP);
    CHECK(neverc_unique_count() == 193);

    fail_at = 0;
    CHECK(neverc_unique_handle_valid(neverc_unique_make_int64(1001)));
    CHECK(g_cap == INTERN_INIT_CAP * 2);
    CHECK(neverc_unique_count() == 194);

    neverc_unique_destroy();
    CHECK(g_table == NULL);
    CHECK(g_cap == 0);
    CHECK(g_count == 0);
    puts("passed");
    return 0;
}
