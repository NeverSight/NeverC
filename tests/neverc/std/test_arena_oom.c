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
#include "../../../std/src/arena/arena.c"
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
    reset_allocator(1);
    CHECK(neverc_arena_new() == NULL);
    CHECK(free_count == 0);

    reset_allocator(2);
    CHECK(neverc_arena_new() == NULL);
    CHECK(free_count == 1);

    reset_allocator(0);
    neverc_arena_t *arena = neverc_arena_new();
    CHECK(arena != NULL);
    size_t chunks = neverc_arena_num_chunks(arena);
    size_t bytes = neverc_arena_bytes_allocated(arena);

    reset_allocator(1);
    CHECK(neverc_arena_alloc(arena, 256U * 1024U) == NULL);
    CHECK(neverc_arena_num_chunks(arena) == chunks);
    CHECK(neverc_arena_bytes_allocated(arena) == bytes);
    CHECK(free_count == 0);

    fail_at = 0;
    neverc_arena_free(arena);
    CHECK(free_count == 2);

    reset_allocator(0);
    arena = neverc_arena_new();
    CHECK(arena != NULL);
    CHECK(neverc_arena_alloc(arena, 256U * 1024U) != NULL);
    CHECK(neverc_arena_num_chunks(arena) >= 2);
    reset_allocator(1);
    neverc_arena_reset(arena);
    CHECK(neverc_arena_num_chunks(arena) == 1);
    CHECK(neverc_arena_bytes_allocated(arena) == 0);
    fail_at = 0;
    /* OOM reset must keep the original default-sized chunk, not the newest
     * oversized one: a tiny alloc stays in-place, a large alloc needs another
     * chunk instead of aliasing the 256KiB allocation just reset. */
    CHECK(neverc_arena_alloc(arena, 8) != NULL);
    CHECK(neverc_arena_num_chunks(arena) == 1);
    CHECK(neverc_arena_alloc(arena, 256U * 1024U) != NULL);
    CHECK(neverc_arena_num_chunks(arena) >= 2);
    neverc_arena_free(arena);

    puts("passed");
    return 0;
}
