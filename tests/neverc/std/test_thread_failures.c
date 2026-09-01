#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

static size_t allocation_call;
static size_t fail_allocation_at;
static size_t live_allocations;
static size_t fail_thread_create_at;
static atomic_int fail_native_join;
static _Atomic size_t failed_native_joins;
static _Atomic size_t successful_native_joins;
static atomic_int observe_condition_wait;
static atomic_int condition_wait_entered;
static atomic_int observe_created_worker;
static atomic_int created_worker_entered;
static atomic_int created_worker_release;
static atomic_int capture_executor_allocation;
struct neverc_thread_executor;
static _Atomic(struct neverc_thread_executor *) observed_executor;

static int thread_test_wait_for_flag(atomic_int *flag) {
#if defined(_WIN32)
    ULONGLONG deadline = GetTickCount64() + 10000;
#else
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
        return 0;
    deadline.tv_sec += 10;
#endif
    while (!atomic_load_explicit(flag, memory_order_acquire)) {
#if defined(_WIN32)
        if (GetTickCount64() >= deadline)
            return 0;
        Sleep(10);
#else
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
            now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec &&
             now.tv_nsec >= deadline.tv_nsec)) {
            return 0;
        }
        struct timespec delay = {0, 10000000L};
        (void)nanosleep(&delay, NULL);
#endif
    }
    return 1;
}

#if defined(_WIN32)
static int thread_test_is_stopped_condition(
    PCONDITION_VARIABLE condition);

static BOOL thread_test_sleep_condition_variable_cs(
    PCONDITION_VARIABLE condition, PCRITICAL_SECTION mutex,
    DWORD milliseconds) {
    if (atomic_load_explicit(&observe_condition_wait,
                             memory_order_acquire) &&
        thread_test_is_stopped_condition(condition)) {
        atomic_store_explicit(&condition_wait_entered, 1,
                              memory_order_release);
    }
    return SleepConditionVariableCS(condition, mutex, milliseconds);
}

typedef struct {
    LPTHREAD_START_ROUTINE function;
    void *arg;
} thread_test_start_context_t;

static thread_test_start_context_t thread_test_start_context;

static DWORD WINAPI thread_test_created_worker_start(void *opaque) {
    thread_test_start_context_t *context =
        (thread_test_start_context_t *)opaque;
    atomic_store_explicit(&created_worker_entered, 1,
                          memory_order_release);
    while (!atomic_load_explicit(&created_worker_release,
                                 memory_order_acquire)) {
    }
    return context->function(context->arg);
}

static HANDLE thread_test_create_thread(
    LPSECURITY_ATTRIBUTES attributes, SIZE_T stack_size,
    LPTHREAD_START_ROUTINE function, LPVOID arg,
    DWORD creation_flags, LPDWORD thread_id) {
    if (!atomic_load_explicit(&observe_created_worker,
                              memory_order_acquire)) {
        return CreateThread(attributes, stack_size, function, arg,
                            creation_flags, thread_id);
    }
    thread_test_start_context.function = function;
    thread_test_start_context.arg = arg;
    return CreateThread(attributes, stack_size,
                        thread_test_created_worker_start,
                        &thread_test_start_context, creation_flags,
                        thread_id);
}

static DWORD WINAPI thread_test_wait_for_single_object(
    HANDLE handle, DWORD milliseconds) {
    if (atomic_load_explicit(&fail_native_join,
                             memory_order_acquire)) {
        atomic_fetch_add_explicit(&failed_native_joins, 1,
                                  memory_order_relaxed);
        SetLastError(ERROR_INVALID_HANDLE);
        return WAIT_FAILED;
    }
    DWORD result = WaitForSingleObject(handle, milliseconds);
    if (result == WAIT_OBJECT_0) {
        atomic_fetch_add_explicit(&successful_native_joins, 1,
                                  memory_order_relaxed);
    }
    return result;
}
#define SleepConditionVariableCS thread_test_sleep_condition_variable_cs
#define CreateThread thread_test_create_thread
#define WaitForSingleObject thread_test_wait_for_single_object
#else
static int thread_test_is_stopped_condition(
    pthread_cond_t *condition);

static int thread_test_pthread_cond_wait(
    pthread_cond_t *condition, pthread_mutex_t *mutex) {
    if (atomic_load_explicit(&observe_condition_wait,
                             memory_order_acquire) &&
        thread_test_is_stopped_condition(condition)) {
        atomic_store_explicit(&condition_wait_entered, 1,
                              memory_order_release);
    }
    return pthread_cond_wait(condition, mutex);
}

typedef struct {
    void *(*function)(void *);
    void *arg;
} thread_test_start_context_t;

static thread_test_start_context_t thread_test_start_context;

static void *thread_test_created_worker_start(void *opaque) {
    thread_test_start_context_t *context =
        (thread_test_start_context_t *)opaque;
    atomic_store_explicit(&created_worker_entered, 1,
                          memory_order_release);
    while (!atomic_load_explicit(&created_worker_release,
                                 memory_order_acquire)) {
    }
    return context->function(context->arg);
}

static int thread_test_pthread_create(
    pthread_t *thread, const pthread_attr_t *attributes,
    void *(*function)(void *), void *arg) {
    if (!atomic_load_explicit(&observe_created_worker,
                              memory_order_acquire)) {
        return pthread_create(thread, attributes, function, arg);
    }
    thread_test_start_context.function = function;
    thread_test_start_context.arg = arg;
    return pthread_create(thread, attributes,
                          thread_test_created_worker_start,
                          &thread_test_start_context);
}

static int thread_test_pthread_join(pthread_t thread, void **result) {
    if (atomic_load_explicit(&fail_native_join,
                             memory_order_acquire)) {
        atomic_fetch_add_explicit(&failed_native_joins, 1,
                                  memory_order_relaxed);
        return EDEADLK;
    }
    int join_result = pthread_join(thread, result);
    if (join_result == 0) {
        atomic_fetch_add_explicit(&successful_native_joins, 1,
                                  memory_order_relaxed);
    }
    return join_result;
}
#define pthread_cond_wait thread_test_pthread_cond_wait
#define pthread_create thread_test_pthread_create
#define pthread_join thread_test_pthread_join
#endif

static void *thread_test_calloc(size_t count, size_t size) {
    allocation_call++;
    if (allocation_call == fail_allocation_at)
        return NULL;
    void *allocation = calloc(count, size);
    if (allocation) {
        live_allocations++;
        if (allocation_call == 1 &&
            atomic_load_explicit(&capture_executor_allocation,
                                 memory_order_acquire)) {
            atomic_store_explicit(&observed_executor,
                                  (struct neverc_thread_executor *)allocation,
                                  memory_order_release);
        }
    }
    return allocation;
}

static void thread_test_free(void *allocation) {
    if (allocation) {
        live_allocations--;
        free(allocation);
    }
}

static int thread_test_should_fail_create(size_t worker_index) {
    if (atomic_load_explicit(&observe_created_worker,
                             memory_order_acquire) &&
        fail_thread_create_at == worker_index + 1) {
        if (!thread_test_wait_for_flag(&created_worker_entered)) {
            fprintf(stderr,
                    "created worker did not reach its entry gate\n");
            exit(1);
        }
    }
    return fail_thread_create_at == worker_index + 1;
}

#define NEVERC_THREAD_CALLOC thread_test_calloc
#define NEVERC_THREAD_FREE thread_test_free
#define NEVERC_THREAD_CREATE_SHOULD_FAIL(worker_index) \
    thread_test_should_fail_create(worker_index)
#include "../../../std/src/thread/thread.c"
#if defined(_WIN32)
#undef WaitForSingleObject
#undef CreateThread
#undef SleepConditionVariableCS
#else
#undef pthread_join
#undef pthread_create
#undef pthread_cond_wait
#endif

#if defined(_WIN32)
static int thread_test_is_stopped_condition(
    PCONDITION_VARIABLE condition) {
    neverc_thread_executor_t *executor = atomic_load_explicit(
        &observed_executor, memory_order_acquire);
    return executor && condition == &executor->stopped;
}
#else
static int thread_test_is_stopped_condition(
    pthread_cond_t *condition) {
    neverc_thread_executor_t *executor = atomic_load_explicit(
        &observed_executor, memory_order_acquire);
    return executor && condition == &executor->stopped.cv;
}
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
    atomic_store_explicit(&fail_native_join, 0, memory_order_relaxed);
    atomic_store_explicit(&failed_native_joins, 0, memory_order_relaxed);
    atomic_store_explicit(&successful_native_joins, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&observe_condition_wait, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&condition_wait_entered, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&observe_created_worker, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&created_worker_entered, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&created_worker_release, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&capture_executor_allocation, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&observed_executor, NULL,
                          memory_order_relaxed);
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

#if defined(_WIN32)
typedef HANDLE thread_test_native_handle_t;
typedef DWORD(WINAPI *thread_test_native_function_t)(void *);

static int thread_test_native_start(
    thread_test_native_handle_t *handle,
    thread_test_native_function_t function, void *arg) {
    DWORD thread_id = 0;
    *handle = CreateThread(NULL, 0, function, arg, 0, &thread_id);
    return *handle ? 0 : -1;
}

static int thread_test_native_join(thread_test_native_handle_t handle) {
    DWORD result = WaitForSingleObject(handle, INFINITE);
    BOOL closed = CloseHandle(handle);
    return result == WAIT_OBJECT_0 && closed ? 0 : -1;
}
#else
typedef pthread_t thread_test_native_handle_t;
typedef void *(*thread_test_native_function_t)(void *);

static int thread_test_native_start(
    thread_test_native_handle_t *handle,
    thread_test_native_function_t function, void *arg) {
    return pthread_create(handle, NULL, function, arg);
}

static int thread_test_native_join(thread_test_native_handle_t handle) {
    return pthread_join(handle, NULL);
}
#endif

static int wait_for_checkpoint_or_finish(
    atomic_int *checkpoint, atomic_int *finished) {
#if defined(_WIN32)
    ULONGLONG deadline = GetTickCount64() + 10000;
#else
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
        return 0;
    deadline.tv_sec += 10;
#endif
    for (;;) {
        if (atomic_load_explicit(checkpoint, memory_order_acquire) ||
            atomic_load_explicit(finished, memory_order_acquire)) {
            return 1;
        }
#if defined(_WIN32)
        if (GetTickCount64() >= deadline)
            return 0;
        Sleep(10);
#else
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
            now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec &&
             now.tv_nsec >= deadline.tv_nsec)) {
            return 0;
        }
        struct timespec delay = {0, 10000000L};
        (void)nanosleep(&delay, NULL);
#endif
    }
}

typedef struct {
    neverc_thread_executor_t *executor;
    atomic_int finished;
} executor_free_call_t;

#if defined(_WIN32)
static DWORD WINAPI executor_free_call(void *opaque) {
#else
static void *executor_free_call(void *opaque) {
#endif
    executor_free_call_t *call = (executor_free_call_t *)opaque;
    neverc_thread_executor_free(call->executor);
    atomic_store_explicit(&call->finished, 1, memory_order_release);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

typedef struct {
    neverc_thread_executor_t *result;
    atomic_int finished;
} executor_create_call_t;

#if defined(_WIN32)
static DWORD WINAPI executor_create_call(void *opaque) {
#else
static void *executor_create_call(void *opaque) {
#endif
    executor_create_call_t *call = (executor_create_call_t *)opaque;
    call->result = neverc_thread_executor_create(2, 4);
    atomic_store_explicit(&call->finished, 1, memory_order_release);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
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
    CHECK(thread_test_wait_for_flag(&parked_worker_started));
    CHECK(neverc_thread_executor_active(executor) == 1);

    size_t executor_allocations = live_allocations;
    CHECK(executor_allocations > 0);
    atomic_store_explicit(&fail_native_join, 1, memory_order_release);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_SYSTEM);
    CHECK(atomic_load_explicit(&failed_native_joins,
                               memory_order_acquire) == 1);
    CHECK(atomic_load_explicit(&successful_native_joins,
                               memory_order_acquire) == 0);
    CHECK(live_allocations == executor_allocations);

    atomic_store_explicit(&fail_native_join, 0, memory_order_release);
    atomic_store_explicit(&parked_worker_release, 1, memory_order_release);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(atomic_load_explicit(&successful_native_joins,
                               memory_order_acquire) == 1);
    neverc_thread_executor_free(executor);
    CHECK(live_allocations == 0);
    return 0;
}

/* Returns 1 for the expected RED while still cleaning up the live executor,
 * so the partial-create proof can run in the same process. */
static int test_executor_free_waits_for_worker_exit(void) {
    reset_failures();
    atomic_store_explicit(&parked_worker_started, 0, memory_order_relaxed);
    atomic_store_explicit(&parked_worker_release, 0, memory_order_relaxed);

    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1, 1);
    CHECK(executor != NULL);
    CHECK(neverc_thread_executor_submit(
              executor, parked_worker_task, NULL) == NEVERC_THREAD_OK);
    if (!thread_test_wait_for_flag(&parked_worker_started)) {
        fprintf(stderr, "parked worker did not start\n");
        return 2;
    }
    CHECK(neverc_thread_executor_active(executor) == 1);

    size_t executor_allocations = live_allocations;
    CHECK(executor_allocations > 0);
    atomic_store_explicit(&observed_executor, executor,
                          memory_order_release);
    atomic_store_explicit(&fail_native_join, 1,
                          memory_order_release);
    atomic_store_explicit(&observe_condition_wait, 1,
                          memory_order_release);
    executor_free_call_t call;
    call.executor = executor;
    atomic_init(&call.finished, 0);
    thread_test_native_handle_t thread;
    CHECK(thread_test_native_start(&thread, executor_free_call, &call) == 0);

    if (!wait_for_checkpoint_or_finish(&condition_wait_entered,
                                       &call.finished)) {
        fprintf(stderr, "free did not reach a terminal checkpoint\n");
        return 2;
    }
    int waited = atomic_load_explicit(&condition_wait_entered,
                                      memory_order_acquire);
    int finished_before_release =
        atomic_load_explicit(&call.finished, memory_order_acquire);

    if (waited && finished_before_release) {
        fprintf(stderr,
                "free returned after publishing a stale wait checkpoint\n");
        return 2;
    }
    if (!waited && !finished_before_release) {
        fprintf(stderr, "free checkpoint state is inconsistent\n");
        return 2;
    }

    CHECK(atomic_load_explicit(&failed_native_joins,
                               memory_order_acquire) == 1);
    CHECK(atomic_load_explicit(&successful_native_joins,
                               memory_order_acquire) == 0);
    CHECK(live_allocations == executor_allocations);

    if (finished_before_release) {
        CHECK(thread_test_native_join(thread) == 0);
        atomic_store_explicit(&fail_native_join, 0,
                              memory_order_release);
        atomic_store_explicit(&parked_worker_release, 1,
                              memory_order_release);
        CHECK(neverc_thread_executor_shutdown(executor) ==
              NEVERC_THREAD_OK);
        neverc_thread_executor_free(executor);
    } else {
        atomic_store_explicit(&fail_native_join, 0,
                              memory_order_release);
        atomic_store_explicit(&parked_worker_release, 1,
                              memory_order_release);
        CHECK(thread_test_native_join(thread) == 0);
    }
    atomic_store_explicit(&observe_condition_wait, 0,
                          memory_order_release);

    CHECK(atomic_load_explicit(&successful_native_joins,
                               memory_order_acquire) == 1);
    CHECK(live_allocations == 0);
    if (finished_before_release) {
        fprintf(stderr,
                "free returned before the worker-exit checkpoint\n");
        return 1;
    }
    return 0;
}

static int test_partial_create_waits_for_worker_exit(void) {
    reset_failures();
    fail_thread_create_at = 2;
    atomic_store_explicit(&fail_native_join, 1,
                          memory_order_release);
    atomic_store_explicit(&capture_executor_allocation, 1,
                          memory_order_release);
    atomic_store_explicit(&observe_created_worker, 1,
                          memory_order_release);
    atomic_store_explicit(&observe_condition_wait, 1,
                          memory_order_release);

    executor_create_call_t call;
    call.result = NULL;
    atomic_init(&call.finished, 0);
    thread_test_native_handle_t thread;
    CHECK(thread_test_native_start(&thread, executor_create_call, &call) ==
          0);
    if (!wait_for_checkpoint_or_finish(&created_worker_entered,
                                       &call.finished)) {
        fprintf(stderr, "created worker did not reach its entry gate\n");
        return 2;
    }
    if (!atomic_load_explicit(&created_worker_entered,
                              memory_order_acquire)) {
        fprintf(stderr, "create returned before the worker entry gate\n");
        return 2;
    }
    if (!wait_for_checkpoint_or_finish(&condition_wait_entered,
                                       &call.finished)) {
        fprintf(stderr, "partial-create cleanup did not reach a checkpoint\n");
        return 2;
    }

    int waited = atomic_load_explicit(&condition_wait_entered,
                                      memory_order_acquire);
    int finished_before_release =
        atomic_load_explicit(&call.finished, memory_order_acquire);
    if (waited && finished_before_release) {
        fprintf(stderr,
                "partial create returned after a stale wait checkpoint\n");
        return 2;
    }
    if (!waited && !finished_before_release) {
        fprintf(stderr, "partial-create checkpoint state is inconsistent\n");
        return 2;
    }
    CHECK(atomic_load_explicit(&observed_executor,
                               memory_order_acquire) != NULL);

    if (finished_before_release) {
        CHECK(thread_test_native_join(thread) == 0);
        fprintf(stderr,
                "partial create returned before the worker-exit checkpoint\n");
        return 1;
    }

    CHECK(atomic_load_explicit(&failed_native_joins,
                               memory_order_acquire) == 1);
    CHECK(atomic_load_explicit(&successful_native_joins,
                               memory_order_acquire) == 0);
    CHECK(live_allocations > 0);
    atomic_store_explicit(&fail_native_join, 0,
                          memory_order_release);
    atomic_store_explicit(&created_worker_release, 1,
                          memory_order_release);
    CHECK(thread_test_native_join(thread) == 0);
    atomic_store_explicit(&observe_condition_wait, 0,
                          memory_order_release);
    atomic_store_explicit(&observe_created_worker, 0,
                          memory_order_release);
    atomic_store_explicit(&capture_executor_allocation, 0,
                          memory_order_release);

    CHECK(call.result == NULL);
    CHECK(atomic_load_explicit(&call.finished,
                               memory_order_acquire) == 1);
    CHECK(atomic_load_explicit(&failed_native_joins,
                               memory_order_acquire) == 1);
    CHECK(atomic_load_explicit(&successful_native_joins,
                               memory_order_acquire) == 1);
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
    int free_wait_result = test_executor_free_waits_for_worker_exit();
    CHECK(free_wait_result != 2);
    CHECK(test_partial_create_waits_for_worker_exit() == 0);
    CHECK(free_wait_result == 0);

    puts("passed");
    return 0;
}
