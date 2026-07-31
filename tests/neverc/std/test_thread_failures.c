#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static size_t allocation_call;
static size_t fail_allocation_at;
static size_t live_allocations;
static size_t fail_thread_create_at;

static void *thread_test_calloc(size_t count, size_t size) {
    allocation_call++;
    if (allocation_call == fail_allocation_at)
        return NULL;
    void *allocation = calloc(count, size);
    if (allocation)
        live_allocations++;
    return allocation;
}

static void thread_test_free(void *allocation) {
    if (allocation) {
        live_allocations--;
        free(allocation);
    }
}

static int thread_test_should_fail_create(size_t worker_index) {
    return fail_thread_create_at == worker_index + 1;
}

#define NEVERC_THREAD_CALLOC thread_test_calloc
#define NEVERC_THREAD_FREE thread_test_free
#define NEVERC_THREAD_CREATE_SHOULD_FAIL(worker_index) \
    thread_test_should_fail_create(worker_index)
#include "../../../std/src/thread/thread.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_failures(void) {
    allocation_call = 0;
    fail_allocation_at = 0;
    fail_thread_create_at = 0;
}

int main(void) {
    const size_t executor_allocations =
#if defined(_WIN32)
        4;
#else
        3;
#endif

    for (size_t fail_at = 1; fail_at <= executor_allocations; fail_at++) {
        reset_failures();
        fail_allocation_at = fail_at;
        CHECK(neverc_thread_executor_create(2, 4) == NULL);
        CHECK(live_allocations == 0);
    }

    for (size_t fail_at = 1; fail_at <= 2; fail_at++) {
        reset_failures();
        fail_thread_create_at = fail_at;
        CHECK(neverc_thread_executor_create(2, 4) == NULL);
        CHECK(live_allocations == 0);
    }

    for (size_t fail_at = 1; fail_at <= 2; fail_at++) {
        reset_failures();
        fail_allocation_at = fail_at;
        CHECK(neverc_thread_channel_create(4) == NULL);
        CHECK(live_allocations == 0);
    }

    reset_failures();
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(2, 4);
    CHECK(executor != NULL);
    neverc_thread_executor_free(executor);
    CHECK(live_allocations == 0);

    neverc_thread_channel_t *channel = neverc_thread_channel_create(4);
    CHECK(channel != NULL);
    neverc_thread_channel_free(channel);
    CHECK(live_allocations == 0);

    puts("passed");
    return 0;
}
