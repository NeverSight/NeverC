#include "neverc/std/os/signal.h"

#include <stdio.h>

#if !defined(_WIN32)
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SIGNAL_ITERATIONS 2000

static atomic_int callback_count;
static atomic_int callback_mismatch;

static void stable_handler(int signum) {
    if (signum != NEVERC_SIGUSR1)
        atomic_fetch_add_explicit(&callback_mismatch, 1,
                                  memory_order_relaxed);
    atomic_fetch_add_explicit(&callback_count, 1, memory_order_relaxed);
}

static void alternate_handler(int signum) {
    if (signum != SIGUSR1)
        atomic_fetch_add_explicit(&callback_mismatch, 1,
                                  memory_order_relaxed);
    atomic_fetch_add_explicit(&callback_count, 1, memory_order_relaxed);
}

static void *replace_handlers(void *unused) {
    (void)unused;
    for (int i = 0; i < SIGNAL_ITERATIONS; ++i) {
        if (i & 1)
            neverc_signal_notify(NEVERC_SIGUSR1, stable_handler);
        else
            neverc_signal_notify(SIGUSR1, alternate_handler);
    }
    return NULL;
}

static void *raise_signals(void *unused) {
    (void)unused;
    for (int i = 0; i < SIGNAL_ITERATIONS; ++i)
        raise(SIGUSR1);
    return NULL;
}

static int wait_for_callback_delivery(void) {
    struct timespec deadline;
    struct timespec now;
    const struct timespec retry_delay = {0, 1000000L};
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
        return 0;
    deadline.tv_sec += 5;
    while (atomic_load_explicit(&callback_count,
                                memory_order_relaxed) == 0) {
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
            now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec &&
             now.tv_nsec >= deadline.tv_nsec))
            return 0;
        (void)nanosleep(&retry_delay, NULL);
    }
    return 1;
}

static int test_concurrent_notify_and_delivery(void) {
    pthread_t replacer;
    pthread_t raiser;
    int replacer_started = 0;
    int raiser_started = 0;

    atomic_store_explicit(&callback_count, 0, memory_order_relaxed);
    atomic_store_explicit(&callback_mismatch, 0, memory_order_relaxed);
    neverc_signal_notify(NEVERC_SIGUSR1, stable_handler);

    replacer_started = pthread_create(&replacer, NULL, replace_handlers,
                                      NULL) == 0;
    if (replacer_started)
        raiser_started = pthread_create(&raiser, NULL, raise_signals,
                                        NULL) == 0;
    if (!replacer_started || !raiser_started) {
        fputs("pthread_create failed\n", stderr);
        if (replacer_started)
            pthread_join(replacer, NULL);
        neverc_signal_stop(NEVERC_SIGUSR1);
        return 1;
    }
    pthread_join(replacer, NULL);
    pthread_join(raiser, NULL);

    /* Make completion deterministic: wait consumes the final event, then an
     * explicit bounded wait proves that a callback worker actually ran before
     * stop is allowed to invalidate queued work. */
    neverc_signal_notify(NEVERC_SIGUSR1, stable_handler);
    raise(SIGUSR1);
    int usr1 = NEVERC_SIGUSR1;
    if (neverc_signal_wait(&usr1, 1) != NEVERC_SIGUSR1) {
        fputs("final signal was not waitable\n", stderr);
        neverc_signal_stop(NEVERC_SIGUSR1);
        return 1;
    }
    int callback_observed = wait_for_callback_delivery();
    neverc_signal_stop(NEVERC_SIGUSR1);
    if (atomic_load_explicit(&callback_mismatch, memory_order_relaxed) != 0) {
        fputs("handler/signum registration was observed inconsistently\n",
              stderr);
        return 1;
    }
    if (!callback_observed) {
        fputs("no callback was dispatched\n", stderr);
        return 1;
    }
    return 0;
}

static pthread_mutex_t wait_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wait_changed = PTHREAD_COND_INITIALIZER;
static int waiters_ready;
static int waiters_done;

typedef struct wait_context {
    int signum;
    int result;
    int mask_unchanged;
} wait_context_t;

static int wait_for_counter(int *counter, int expected) {
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return 0;
    deadline.tv_sec += 5;
    pthread_mutex_lock(&wait_lock);
    while (*counter < expected) {
        int rc = pthread_cond_timedwait(&wait_changed, &wait_lock, &deadline);
        if (rc != 0) {
            pthread_mutex_unlock(&wait_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&wait_lock);
    return 1;
}

static void *wait_for_one_signal(void *opaque) {
    wait_context_t *context = (wait_context_t *)opaque;
    sigset_t before;
    sigset_t after;
    context->mask_unchanged = 0;
    (void)pthread_sigmask(SIG_SETMASK, NULL, &before);

    pthread_mutex_lock(&wait_lock);
    ++waiters_ready;
    pthread_cond_broadcast(&wait_changed);
    pthread_mutex_unlock(&wait_lock);

    context->result = neverc_signal_wait(&context->signum, 1);
    (void)pthread_sigmask(SIG_SETMASK, NULL, &after);
    context->mask_unchanged = 1;
    for (int signum = 1; signum < NSIG; ++signum) {
        if (sigismember(&before, signum) != sigismember(&after, signum)) {
            context->mask_unchanged = 0;
            break;
        }
    }

    pthread_mutex_lock(&wait_lock);
    ++waiters_done;
    pthread_cond_broadcast(&wait_changed);
    pthread_mutex_unlock(&wait_lock);
    return NULL;
}

static void noop_handler(int signum) {
    (void)signum;
}

static int test_independent_concurrent_waiters(void) {
    wait_context_t contexts[2] = {
        {NEVERC_SIGUSR1, -1, 0},
        {NEVERC_SIGUSR2, -1, 0}
    };
    pthread_t threads[2];
    int first_started = 0;
    int second_started = 0;

    neverc_signal_notify(NEVERC_SIGUSR1, noop_handler);
    neverc_signal_notify(NEVERC_SIGUSR2, noop_handler);
    pthread_mutex_lock(&wait_lock);
    waiters_ready = 0;
    waiters_done = 0;
    pthread_mutex_unlock(&wait_lock);

    first_started = pthread_create(&threads[0], NULL, wait_for_one_signal,
                                   &contexts[0]) == 0;
    if (first_started)
        second_started = pthread_create(&threads[1], NULL,
                                        wait_for_one_signal,
                                        &contexts[1]) == 0;
    if (!first_started || !second_started) {
        fputs("waiter pthread_create failed\n", stderr);
        if (first_started) {
            (void)kill(getpid(), SIGUSR1);
            pthread_join(threads[0], NULL);
        }
        neverc_signal_stop(NEVERC_SIGUSR1);
        neverc_signal_stop(NEVERC_SIGUSR2);
        return 1;
    }
    if (!wait_for_counter(&waiters_ready, 2)) {
        fputs("waiters did not become ready\n", stderr);
        return 1;
    }

    (void)kill(getpid(), SIGUSR1);
    (void)kill(getpid(), SIGUSR2);
    if (!wait_for_counter(&waiters_done, 2)) {
        fputs("independent waiters blocked indefinitely\n", stderr);
        return 1;
    }
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    neverc_signal_stop(NEVERC_SIGUSR1);
    neverc_signal_stop(NEVERC_SIGUSR2);

    if (contexts[0].result != NEVERC_SIGUSR1 ||
        contexts[1].result != NEVERC_SIGUSR2 ||
        !contexts[0].mask_unchanged || !contexts[1].mask_unchanged) {
        fputs("waiters consumed the wrong signal or changed a signal mask\n",
              stderr);
        return 1;
    }
    return 0;
}

static int test_same_signal_concurrent_waiters(void) {
    wait_context_t contexts[2] = {
        {NEVERC_SIGUSR1, -1, 0},
        {NEVERC_SIGUSR1, -1, 0}
    };
    pthread_t threads[2];
    int first_started = 0;
    int second_started = 0;

    neverc_signal_notify(NEVERC_SIGUSR1, noop_handler);
    pthread_mutex_lock(&wait_lock);
    waiters_ready = 0;
    waiters_done = 0;
    pthread_mutex_unlock(&wait_lock);

    first_started = pthread_create(&threads[0], NULL, wait_for_one_signal,
                                   &contexts[0]) == 0;
    if (first_started)
        second_started = pthread_create(&threads[1], NULL,
                                        wait_for_one_signal,
                                        &contexts[1]) == 0;
    if (!first_started || !second_started) {
        fputs("same-signal waiter pthread_create failed\n", stderr);
        if (first_started) {
            raise(SIGUSR1);
            pthread_join(threads[0], NULL);
        }
        neverc_signal_stop(NEVERC_SIGUSR1);
        return 1;
    }
    if (!wait_for_counter(&waiters_ready, 2)) {
        fputs("same-signal waiters did not become ready\n", stderr);
        return 1;
    }

    /* raise() does not return until the process signal handler has run, so
     * these are two distinct transport records even if neither waiter has
     * acquired the library lock yet. */
    raise(SIGUSR1);
    raise(SIGUSR1);
    if (!wait_for_counter(&waiters_done, 2)) {
        fputs("two deliveries did not release two same-signal waiters\n",
              stderr);
        return 1;
    }
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    neverc_signal_stop(NEVERC_SIGUSR1);

    if (contexts[0].result != NEVERC_SIGUSR1 ||
        contexts[1].result != NEVERC_SIGUSR1 ||
        !contexts[0].mask_unchanged || !contexts[1].mask_unchanged) {
        fputs("same-signal waiters consumed an invalid delivery\n", stderr);
        return 1;
    }
    return 0;
}

static volatile sig_atomic_t external_handler_called;

static void external_handler(int signum) {
    (void)signum;
    external_handler_called = 1;
}

static int test_wait_restores_external_sigaction(void) {
    struct sigaction original;
    struct sigaction custom;
    struct sigaction current;
    wait_context_t context = {NEVERC_SIGUSR2, -1, 0};
    pthread_t waiter;

    if (sigaction(SIGUSR2, NULL, &original) != 0)
        return 1;
    memset(&custom, 0, sizeof(custom));
    sigemptyset(&custom.sa_mask);
    custom.sa_handler = external_handler;
    if (sigaction(SIGUSR2, &custom, NULL) != 0)
        return 1;

    pthread_mutex_lock(&wait_lock);
    waiters_ready = 0;
    waiters_done = 0;
    pthread_mutex_unlock(&wait_lock);
    if (pthread_create(&waiter, NULL, wait_for_one_signal, &context) != 0) {
        (void)sigaction(SIGUSR2, &original, NULL);
        return 1;
    }
    if (!wait_for_counter(&waiters_ready, 1))
        return 1;

    int transport_installed = 0;
    for (int i = 0; i < 100000; ++i) {
        if (sigaction(SIGUSR2, NULL, &current) == 0 &&
            current.sa_handler != external_handler) {
            transport_installed = 1;
            break;
        }
        sched_yield();
    }
    if (!transport_installed) {
        fputs("wait did not install its transport action\n", stderr);
        return 1;
    }
    (void)kill(getpid(), SIGUSR2);
    if (!wait_for_counter(&waiters_done, 1)) {
        fputs("transient waiter blocked indefinitely\n", stderr);
        return 1;
    }
    pthread_join(waiter, NULL);
    if (sigaction(SIGUSR2, NULL, &current) != 0 ||
        current.sa_handler != external_handler || external_handler_called) {
        fputs("wait did not restore the caller's sigaction\n", stderr);
        (void)sigaction(SIGUSR2, &original, NULL);
        return 1;
    }
    (void)sigaction(SIGUSR2, &original, NULL);
    return context.result == NEVERC_SIGUSR2 ? 0 : 1;
}

static pthread_mutex_t reentrant_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t reentrant_changed = PTHREAD_COND_INITIALIZER;
static int reentrant_done;
static int reentrant_wait_result;

static void reentrant_handler(int signum) {
    int usr2 = NEVERC_SIGUSR2;
    (void)signum;
    neverc_signal_notify(NEVERC_SIGUSR2, noop_handler);
    neverc_signal_ignore(NEVERC_SIGUSR2);
    neverc_signal_reset(NEVERC_SIGUSR2);
    neverc_signal_notify(NEVERC_SIGUSR2, noop_handler);
    neverc_signal_stop(NEVERC_SIGUSR2);
    reentrant_wait_result = neverc_signal_wait(&usr2, 1);
    neverc_signal_stop(NEVERC_SIGUSR1);
    pthread_mutex_lock(&reentrant_lock);
    reentrant_done = 1;
    pthread_cond_broadcast(&reentrant_changed);
    pthread_mutex_unlock(&reentrant_lock);
}

static int test_callback_api_reentrancy(void) {
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return 1;
    deadline.tv_sec += 5;
    pthread_mutex_lock(&reentrant_lock);
    reentrant_done = 0;
    pthread_mutex_unlock(&reentrant_lock);
    reentrant_wait_result = 0;

    neverc_signal_notify(NEVERC_SIGUSR1, reentrant_handler);
    raise(SIGUSR1);
    pthread_mutex_lock(&reentrant_lock);
    while (!reentrant_done) {
        if (pthread_cond_timedwait(&reentrant_changed, &reentrant_lock,
                                   &deadline) != 0) {
            pthread_mutex_unlock(&reentrant_lock);
            fputs("callback API reentrancy deadlocked\n", stderr);
            return 1;
        }
    }
    pthread_mutex_unlock(&reentrant_lock);
    neverc_signal_stop(NEVERC_SIGUSR1);
    return reentrant_wait_result == -1 ? 0 : 1;
}

static pthread_mutex_t cross_stop_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cross_stop_changed = PTHREAD_COND_INITIALIZER;
static int cross_stop_entered;
static int cross_stop_done;

static void cross_stop_handler(int signum) {
    pthread_mutex_lock(&cross_stop_lock);
    ++cross_stop_entered;
    pthread_cond_broadcast(&cross_stop_changed);
    while (cross_stop_entered < 2)
        pthread_cond_wait(&cross_stop_changed, &cross_stop_lock);
    pthread_mutex_unlock(&cross_stop_lock);

    if (signum == NEVERC_SIGUSR1)
        neverc_signal_stop(NEVERC_SIGUSR2);
    else
        neverc_signal_stop(NEVERC_SIGUSR1);

    pthread_mutex_lock(&cross_stop_lock);
    ++cross_stop_done;
    pthread_cond_broadcast(&cross_stop_changed);
    pthread_mutex_unlock(&cross_stop_lock);
}

static int test_callbacks_can_cross_stop(void) {
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return 1;
    deadline.tv_sec += 5;
    pthread_mutex_lock(&cross_stop_lock);
    cross_stop_entered = 0;
    cross_stop_done = 0;
    pthread_mutex_unlock(&cross_stop_lock);

    neverc_signal_notify(NEVERC_SIGUSR1, cross_stop_handler);
    neverc_signal_notify(NEVERC_SIGUSR2, cross_stop_handler);
    raise(SIGUSR1);
    raise(SIGUSR2);

    pthread_mutex_lock(&cross_stop_lock);
    while (cross_stop_done < 2) {
        int rc = pthread_cond_timedwait(&cross_stop_changed, &cross_stop_lock,
                                        &deadline);
        if (rc != 0) {
            pthread_mutex_unlock(&cross_stop_lock);
            fputs("callbacks deadlocked while cross-stopping signals\n",
                  stderr);
            return 1;
        }
    }
    pthread_mutex_unlock(&cross_stop_lock);
    neverc_signal_stop(NEVERC_SIGUSR1);
    neverc_signal_stop(NEVERC_SIGUSR2);
    return 0;
}

static pthread_mutex_t blocking_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t blocking_changed = PTHREAD_COND_INITIALIZER;
static int blocking_entered;
static int blocking_release;
static int stop_started;
static int stop_done;

static void blocking_handler(int signum) {
    (void)signum;
    pthread_mutex_lock(&blocking_lock);
    blocking_entered = 1;
    pthread_cond_broadcast(&blocking_changed);
    while (!blocking_release)
        pthread_cond_wait(&blocking_changed, &blocking_lock);
    pthread_mutex_unlock(&blocking_lock);
}

static void *stop_signal(void *unused) {
    (void)unused;
    pthread_mutex_lock(&blocking_lock);
    stop_started = 1;
    pthread_cond_broadcast(&blocking_changed);
    pthread_mutex_unlock(&blocking_lock);
    neverc_signal_stop(NEVERC_SIGUSR1);
    pthread_mutex_lock(&blocking_lock);
    stop_done = 1;
    pthread_cond_broadcast(&blocking_changed);
    pthread_mutex_unlock(&blocking_lock);
    return NULL;
}

static int test_stop_waits_for_inflight_callback(void) {
    struct timespec deadline;
    pthread_t stopper;
    pthread_mutex_lock(&blocking_lock);
    blocking_entered = 0;
    blocking_release = 0;
    stop_started = 0;
    stop_done = 0;
    pthread_mutex_unlock(&blocking_lock);

    neverc_signal_notify(NEVERC_SIGUSR1, blocking_handler);
    raise(SIGUSR1);
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return 1;
    deadline.tv_sec += 5;
    pthread_mutex_lock(&blocking_lock);
    while (!blocking_entered) {
        if (pthread_cond_timedwait(&blocking_changed, &blocking_lock,
                                   &deadline) != 0) {
            pthread_mutex_unlock(&blocking_lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&blocking_lock);

    if (pthread_create(&stopper, NULL, stop_signal, NULL) != 0)
        return 1;
    pthread_mutex_lock(&blocking_lock);
    while (!stop_started) {
        if (pthread_cond_wait(&blocking_changed, &blocking_lock) != 0) {
            pthread_mutex_unlock(&blocking_lock);
            return 1;
        }
    }
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        pthread_mutex_unlock(&blocking_lock);
        return 1;
    }
    deadline.tv_nsec += 200000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    while (!stop_done) {
        int rc = pthread_cond_timedwait(&blocking_changed, &blocking_lock,
                                        &deadline);
        if (rc == ETIMEDOUT)
            break;
        if (rc != 0) {
            pthread_mutex_unlock(&blocking_lock);
            return 1;
        }
    }
    if (stop_done) {
        pthread_mutex_unlock(&blocking_lock);
        fputs("stop returned before the callback finished\n", stderr);
        return 1;
    }
    blocking_release = 1;
    pthread_cond_broadcast(&blocking_changed);
    pthread_mutex_unlock(&blocking_lock);
    pthread_join(stopper, NULL);

    pthread_mutex_lock(&blocking_lock);
    int completed = stop_done;
    pthread_mutex_unlock(&blocking_lock);
    return completed ? 0 : 1;
}

static int test_blocked_callback_does_not_stall_other_signal(void) {
    wait_context_t context = {NEVERC_SIGUSR2, -1, 0};
    struct timespec deadline;
    pthread_t waiter;

    pthread_mutex_lock(&blocking_lock);
    blocking_entered = 0;
    blocking_release = 0;
    pthread_mutex_unlock(&blocking_lock);
    pthread_mutex_lock(&wait_lock);
    waiters_ready = 0;
    waiters_done = 0;
    pthread_mutex_unlock(&wait_lock);

    neverc_signal_notify(NEVERC_SIGUSR1, blocking_handler);
    neverc_signal_notify(NEVERC_SIGUSR2, noop_handler);
    raise(SIGUSR1);
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return 1;
    deadline.tv_sec += 5;
    pthread_mutex_lock(&blocking_lock);
    while (!blocking_entered) {
        if (pthread_cond_timedwait(&blocking_changed, &blocking_lock,
                                   &deadline) != 0) {
            pthread_mutex_unlock(&blocking_lock);
            return 1;
        }
    }
    pthread_mutex_unlock(&blocking_lock);

    if (pthread_create(&waiter, NULL, wait_for_one_signal, &context) != 0)
        return 1;
    if (!wait_for_counter(&waiters_ready, 1))
        return 1;
    raise(SIGUSR2);
    if (!wait_for_counter(&waiters_done, 1)) {
        fputs("blocked USR1 callback stalled USR2 delivery\n", stderr);
        pthread_mutex_lock(&blocking_lock);
        blocking_release = 1;
        pthread_cond_broadcast(&blocking_changed);
        pthread_mutex_unlock(&blocking_lock);
        pthread_join(waiter, NULL);
        return 1;
    }
    pthread_join(waiter, NULL);

    pthread_mutex_lock(&blocking_lock);
    blocking_release = 1;
    pthread_cond_broadcast(&blocking_changed);
    pthread_mutex_unlock(&blocking_lock);
    neverc_signal_stop(NEVERC_SIGUSR1);
    neverc_signal_stop(NEVERC_SIGUSR2);
    return context.result == NEVERC_SIGUSR2 ? 0 : 1;
}

#if !defined(NEVERC_SIGNAL_SKIP_FORK)
static int test_fork_restarts_dispatcher(void) {
    neverc_signal_notify(NEVERC_SIGUSR1, noop_handler);
    pid_t child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        struct sigaction action;
        alarm(5);
        if (sigaction(SIGUSR1, NULL, &action) != 0 ||
            action.sa_handler != SIG_DFL)
            _exit(2);
        neverc_signal_notify(NEVERC_SIGUSR1, noop_handler);
        raise(SIGUSR1);
        int usr1 = NEVERC_SIGUSR1;
        int result = neverc_signal_wait(&usr1, 1);
        neverc_signal_stop(NEVERC_SIGUSR1);
        _exit(result == NEVERC_SIGUSR1 ? 0 : 3);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child)
        return 1;
    neverc_signal_stop(NEVERC_SIGUSR1);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fputs("child signal dispatcher did not restart cleanly\n", stderr);
        return 1;
    }
    return 0;
}
#endif
#else
#include <signal.h>
#include <windows.h>

#define WINDOWS_SIGNAL_ITERATIONS 1000

static volatile LONG windows_callback_count;
static volatile LONG windows_callback_mismatch;

static void windows_handler_a(int signum) {
    if (signum != SIGINT)
        InterlockedIncrement(&windows_callback_mismatch);
    InterlockedIncrement(&windows_callback_count);
}

static void windows_handler_b(int signum) {
    if (signum != SIGINT)
        InterlockedIncrement(&windows_callback_mismatch);
    InterlockedIncrement(&windows_callback_count);
}

static DWORD WINAPI replace_windows_handlers(void *unused) {
    (void)unused;
    for (int i = 0; i < WINDOWS_SIGNAL_ITERATIONS; ++i)
        neverc_signal_notify(SIGINT,
                             (i & 1) ? windows_handler_a : windows_handler_b);
    return 0;
}

static DWORD WINAPI raise_windows_signals(void *unused) {
    (void)unused;
    for (int i = 0; i < WINDOWS_SIGNAL_ITERATIONS; ++i)
        raise(SIGINT);
    return 0;
}

static int test_windows_concurrent_notify_and_delivery(void) {
    InterlockedExchange(&windows_callback_count, 0);
    InterlockedExchange(&windows_callback_mismatch, 0);
    neverc_signal_notify(SIGINT, windows_handler_a);
    HANDLE replacer = CreateThread(NULL, 0, replace_windows_handlers,
                                   NULL, 0, NULL);
    HANDLE raiser = CreateThread(NULL, 0, raise_windows_signals,
                                 NULL, 0, NULL);
    if (!replacer || !raiser) {
        if (replacer) CloseHandle(replacer);
        if (raiser) CloseHandle(raiser);
        neverc_signal_stop(SIGINT);
        return 1;
    }
    HANDLE threads[2] = {replacer, raiser};
    DWORD wait_result = WaitForMultipleObjects(2, threads, TRUE, 30000);
    CloseHandle(replacer);
    CloseHandle(raiser);
    neverc_signal_stop(SIGINT);
    if (wait_result != WAIT_OBJECT_0)
        return 1;
    if (InterlockedCompareExchange(&windows_callback_count, 0, 0) == 0 ||
        InterlockedCompareExchange(&windows_callback_mismatch, 0, 0) != 0)
        return 1;
    return 0;
}

typedef struct windows_wait_context {
    int signum;
    int result;
} windows_wait_context_t;

static DWORD WINAPI wait_windows_signal(void *opaque) {
    windows_wait_context_t *context = (windows_wait_context_t *)opaque;
    context->result = neverc_signal_wait(&context->signum, 1);
    return 0;
}

static void windows_noop_handler(int signum) {
    (void)signum;
}

static int test_windows_independent_waiters(void) {
    windows_wait_context_t contexts[2] = {
        {SIGINT, -1},
        {SIGTERM, -1}
    };
    neverc_signal_notify(SIGINT, windows_noop_handler);
    neverc_signal_notify(SIGTERM, windows_noop_handler);
    HANDLE threads[2] = {
        CreateThread(NULL, 0, wait_windows_signal, &contexts[0], 0, NULL),
        CreateThread(NULL, 0, wait_windows_signal, &contexts[1], 0, NULL)
    };
    if (!threads[0] || !threads[1])
        return 1;
    raise(SIGINT);
    raise(SIGTERM);
    DWORD waited = WaitForMultipleObjects(2, threads, TRUE, 30000);
    CloseHandle(threads[0]);
    CloseHandle(threads[1]);
    neverc_signal_stop(SIGINT);
    neverc_signal_stop(SIGTERM);
    if (waited != WAIT_OBJECT_0 || contexts[0].result != SIGINT ||
        contexts[1].result != SIGTERM)
        return 1;
    return 0;
}

static int test_windows_same_signal_waiters(void) {
    windows_wait_context_t contexts[2] = {
        {SIGINT, -1},
        {SIGINT, -1}
    };
    neverc_signal_notify(SIGINT, windows_noop_handler);
    HANDLE threads[2] = {
        CreateThread(NULL, 0, wait_windows_signal, &contexts[0], 0, NULL),
        CreateThread(NULL, 0, wait_windows_signal, &contexts[1], 0, NULL)
    };
    if (!threads[0] || !threads[1])
        return 1;
    raise(SIGINT);
    raise(SIGINT);
    DWORD waited = WaitForMultipleObjects(2, threads, TRUE, 30000);
    CloseHandle(threads[0]);
    CloseHandle(threads[1]);
    neverc_signal_stop(SIGINT);
    if (waited != WAIT_OBJECT_0 || contexts[0].result != SIGINT ||
        contexts[1].result != SIGINT)
        return 1;
    return 0;
}

static int test_windows_unsupported_wait_fails_fast(void) {
    int signum = 0;
    if (neverc_signal_wait(&signum, 1) != -1)
        return 1;
    signum = NEVERC_SIGUSR1;
    if (neverc_signal_wait(&signum, 1) != -1)
        return 1;
    signum = NEVERC_SIGUSR2;
    return neverc_signal_wait(&signum, 1) == -1 ? 0 : 1;
}
#endif

int main(void) {
#if !defined(_WIN32)
    alarm(60);
    if (test_concurrent_notify_and_delivery() != 0)
        return 1;
    if (test_independent_concurrent_waiters() != 0)
        return 1;
    if (test_same_signal_concurrent_waiters() != 0)
        return 1;
    if (test_wait_restores_external_sigaction() != 0)
        return 1;
    if (test_callback_api_reentrancy() != 0)
        return 1;
    if (test_callbacks_can_cross_stop() != 0)
        return 1;
    if (test_stop_waits_for_inflight_callback() != 0)
        return 1;
    if (test_blocked_callback_does_not_stall_other_signal() != 0)
        return 1;
#if !defined(NEVERC_SIGNAL_SKIP_FORK)
    if (test_fork_restarts_dispatcher() != 0)
        return 1;
#endif
    alarm(0);
#else
    if (test_windows_concurrent_notify_and_delivery() != 0)
        return 1;
    if (test_windows_independent_waiters() != 0)
        return 1;
    if (test_windows_same_signal_waiters() != 0)
        return 1;
    if (test_windows_unsupported_wait_fails_fast() != 0)
        return 1;
#endif
    puts("passed");
    return 0;
}
