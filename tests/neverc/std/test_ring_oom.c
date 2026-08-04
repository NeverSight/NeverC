#include <stdio.h>
#include <stdlib.h>

static size_t allocation_count;
static size_t free_count;
static size_t fail_at;

static void *controlled_malloc(size_t size) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at ? NULL : malloc(size);
}

static void controlled_free(void *ptr) {
    if (ptr) free_count++;
    free(ptr);
}

#define malloc controlled_malloc
#define free controlled_free
#include "../../../std/src/container/ring/ring.c"
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
    for (size_t i = 1; i <= 5; i++) {
        reset_allocator(i);
        CHECK(neverc_ring_new(5) == NULL);
        CHECK(allocation_count == i);
        CHECK(free_count == i - 1);
    }

    reset_allocator(0);
    neverc_ring_t *ring = neverc_ring_new(5);
    CHECK(ring != NULL);
    CHECK(neverc_ring_len(ring) == 5);
    neverc_ring_free(ring);
    CHECK(free_count == 5);
    puts("passed");
    return 0;
}
