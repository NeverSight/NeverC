#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#endif

static size_t allocation_call;
static size_t fail_allocation_at;
static size_t live_allocations;
static size_t fail_thread_create_at;
static int fail_native_join;
static size_t failed_native_joins;
static size_t successful_native_joins;

#if defined(_WIN32)
static DWORD WINAPI thread_test_wait_for_single_object(
    HANDLE handle, DWORD milliseconds) {
    if (fail_native_join) {
        failed_native_joins++;
        SetLastError(ERROR_INVALID_HANDLE);
        return WAIT_FAILED;
    }
    DWORD result = WaitForSingleObject(handle, milliseconds);
    if (result == WAIT_OBJECT_0)
        successful_native_joins++;
    return result;
}
#define WaitForSingleObject thread_test_wait_for_single_object
#else
static int thread_test_pthread_join(pthread_t thread, void **result) {
    if (fail_native_join) {
        failed_native_joins++;
        return EDEADLK;
    }
    int join_result = pthread_join(thread, result);
    if (join_result == 0)
        successful_native_joins++;
    return join_result;
}
#define pthread_join thread_test_pthread_join
#endif

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
#if defined(_WIN32)
#undef WaitForSingleObject
#else
#undef pthread_join
#endif

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
    fail_native_join = 0;
    failed_native_joins = 0;
    successful_native_joins = 0;
}

static atomic_int parked_worker_started;
static atomic_int parked_worker_release;

static void parked_worker_task(void *unused) {
    (void)unused;
    atomic_store_explicit(&parked_worker_started, 1, memory_order_release);
    while (!atomic_load_explicit(&parked_worker_release,
                                 memory_order_acquire)) {
    }
}

static int test_executor_join_failure_retry(void) {
    reset_failures();
    atomic_store_explicit(&parked_worker_started, 0, memory_order_relaxed);
    atomic_store_explicit(&parked_worker_release, 0, memory_order_relaxed);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1, 1);
    CHECK(executor != NULL);
    CHECK(neverc_thread_executor_submit(
              executor, parked_worker_task, NULL) == NEVERC_THREAD_OK);
    while (!atomic_load_explicit(&parked_worker_started,
                                 memory_order_acquire)) {
    }
    CHECK(neverc_thread_executor_active(executor) == 1);

    size_t executor_allocations = live_allocations;
    CHECK(executor_allocations > 0);
    fail_native_join = 1;
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_SYSTEM);
    CHECK(failed_native_joins == 1);
    CHECK(successful_native_joins == 0);
    CHECK(live_allocations == executor_allocations);

    fail_native_join = 0;
    atomic_store_explicit(&parked_worker_release, 1, memory_order_release);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(successful_native_joins == 1);
    neverc_thread_executor_free(executor);
    CHECK(live_allocations == 0);
    return 0;
}

int main(void) {
    const size_t executor_allocations = 4;

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

    CHECK(test_executor_join_failure_retry() == 0);

    puts("passed");
    return 0;
}
