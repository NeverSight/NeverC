#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static size_t allocation_count;
static size_t free_count;
static size_t fail_at;
static int custom_free_count;

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
#include "../../../std/src/weak/weak.c"
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

static void custom_free(void *ptr) {
    (void)ptr;
    custom_free_count++;
}

int main(void) {
    int value = 42;
    CHECK(neverc_weak_new(NULL, sizeof(value))._ctrl == NULL);
    CHECK(neverc_weak_new(&value, 0)._ctrl == NULL);

    reset_allocator(1);
    CHECK(neverc_weak_new(&value, sizeof(value))._ctrl == NULL);
    CHECK(free_count == 0);

    reset_allocator(2);
    CHECK(neverc_weak_new(&value, sizeof(value))._ctrl == NULL);
    CHECK(free_count == 1);

    custom_free_count = 0;
    reset_allocator(1);
    CHECK(neverc_weak_new_with_free(&value, custom_free)._ctrl == NULL);
    CHECK(custom_free_count == 0);

    reset_allocator(0);
    neverc_weak_strong_t strong =
        neverc_weak_new(&value, sizeof(value));
    CHECK(strong._ctrl != NULL);
    LOCK();
    ctrl_block_t *cb = ctrl_for_strong_locked(strong);
    UNLOCK();
    CHECK(cb != NULL);

    reset_allocator(1);
    CHECK(neverc_weak_make(strong) == NULL);
    CHECK(cb->weak == 1);
    CHECK(free_count == 0);

    reset_allocator(0);
    neverc_weak_ref_t *weak = neverc_weak_make(strong);
    CHECK(weak != NULL);
    CHECK(cb->weak == 2);

    cb->strong = INT32_MAX;
    CHECK(neverc_weak_strong_retain(strong)._ctrl == NULL);
    CHECK(neverc_weak_upgrade(weak)._ctrl == NULL);
    CHECK(cb->strong == INT32_MAX);
    cb->strong = 1;

    weak->refs = INT32_MAX;
    CHECK(neverc_weak_ref_retain(weak) == NULL);
    CHECK(weak->refs == INT32_MAX);
    CHECK(cb->weak == 2);
    weak->refs = 1;

    cb->weak = INT32_MAX;
    CHECK(neverc_weak_ref_retain(weak) == NULL);
    CHECK(weak->refs == 1);
    CHECK(cb->weak == INT32_MAX);
    CHECK(neverc_weak_make(strong) == NULL);
    CHECK(cb->weak == INT32_MAX);
    cb->weak = 2;

    neverc_weak_ref_release(weak);
    neverc_weak_strong_release(&strong);
    puts("passed");
    return 0;
}
