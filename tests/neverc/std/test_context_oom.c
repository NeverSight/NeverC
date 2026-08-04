#include <stdio.h>
#include <stdlib.h>

static size_t allocation_count;
static size_t fail_at;

static void *controlled_calloc(size_t count, size_t size) {
    allocation_count++;
    if (fail_at != 0 && allocation_count == fail_at)
        return NULL;
    return calloc(count, size);
}

#define calloc controlled_calloc
#include "../../../std/src/context/context.c"
#undef calloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",             \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

static void unused_callback(void) {}

int main(void) {
    reset_allocator(1);
    CHECK(neverc_context_background() == NULL);

    reset_allocator(0);
    neverc_context_t *parent = neverc_context_background();
    CHECK(parent != NULL);

    neverc_cancel_func_t cancel = (neverc_cancel_func_t)unused_callback;
    reset_allocator(1);
    CHECK(neverc_context_with_cancel(parent, &cancel) == NULL);
    CHECK(cancel == NULL);
    CHECK(neverc_context_done(parent) == 0);

    neverc_context_cancel_handle_t *handle =
        (neverc_context_cancel_handle_t *)(uintptr_t)1;
    reset_allocator(1);
    CHECK(neverc_context_with_cancel_handle(parent, &handle) == NULL);
    CHECK(handle == NULL);
    CHECK(neverc_context_done(parent) == 0);

    handle = (neverc_context_cancel_handle_t *)(uintptr_t)1;
    reset_allocator(2);
    CHECK(neverc_context_with_timeout_handle(parent, 1000, &handle) == NULL);
    CHECK(handle == NULL);
    CHECK(neverc_context_done(parent) == 0);

    reset_allocator(1);
    CHECK(neverc_context_with_value(parent, "key", "value") == NULL);
    CHECK(neverc_context_value(parent, "key") == NULL);

    reset_allocator(0);
    neverc_context_t *ctx =
        neverc_context_with_timeout(parent, 1000, NULL);
    CHECK(ctx != NULL);
    reset_allocator(1);
    CHECK(neverc_context_after_func(ctx, unused_callback) == NULL);

    reset_allocator(0);
    neverc_context_free(ctx);
    neverc_context_free(parent);
    puts("passed");
    return 0;
}
