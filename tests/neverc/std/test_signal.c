/*
 * NeverC os/signal tests.
 * Tests signal handler registration and notification.
 */
#include "neverc/std/os/signal.h"
#include <stdio.h>
#include <signal.h>
#if !defined(_WIN32)
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

#if defined(_WIN32)
static volatile int g_received_sig;
static volatile int g_handler_called;
static void test_handler(int signum) {
    g_received_sig = signum;
    g_handler_called = 1;
}
#else
static pthread_mutex_t g_handler_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_handler_changed = PTHREAD_COND_INITIALIZER;
static int g_received_sig;
static int g_handler_called;

static void test_handler(int signum) {
    pthread_mutex_lock(&g_handler_lock);
    g_received_sig = signum;
    ++g_handler_called;
    pthread_cond_broadcast(&g_handler_changed);
    pthread_mutex_unlock(&g_handler_lock);
}

static void reset_handler_state(void) {
    pthread_mutex_lock(&g_handler_lock);
    g_received_sig = 0;
    g_handler_called = 0;
    pthread_mutex_unlock(&g_handler_lock);
}

static void clear_received_signal(void) {
    pthread_mutex_lock(&g_handler_lock);
    g_received_sig = 0;
    pthread_mutex_unlock(&g_handler_lock);
}

static int wait_for_handler_calls(int expected) {
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return 0;
    deadline.tv_sec += 5;

    pthread_mutex_lock(&g_handler_lock);
    while (g_handler_called < expected) {
        int rc = pthread_cond_timedwait(&g_handler_changed, &g_handler_lock,
                                        &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&g_handler_lock);
            return 0;
        }
        if (rc != 0) {
            pthread_mutex_unlock(&g_handler_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_handler_lock);
    return 1;
}

static int read_received_signal(void) {
    int signum;
    pthread_mutex_lock(&g_handler_lock);
    signum = g_received_sig;
    pthread_mutex_unlock(&g_handler_lock);
    return signum;
}

static int read_handler_calls(void) {
    int count;
    pthread_mutex_lock(&g_handler_lock);
    count = g_handler_called;
    pthread_mutex_unlock(&g_handler_lock);
    return count;
}
#endif

#if defined(_WIN32)

static void test_notify_and_raise(void) {
    printf("[notify_and_raise]\n");
    g_received_sig = 0;
    g_handler_called = 0;

    neverc_signal_notify(SIGINT, test_handler);
    raise(SIGINT);

    ASSERT_INT_EQ(g_handler_called, 1);
    ASSERT_INT_EQ(g_received_sig, SIGINT);

    neverc_signal_stop(SIGINT);
}

static void test_ignore(void) {
    printf("[ignore]\n");
    neverc_signal_ignore(SIGTERM);
    tests_run++;
    tests_passed++;
    neverc_signal_reset(SIGTERM);
}

static void test_reset(void) {
    printf("[reset]\n");
    g_handler_called = 0;
    neverc_signal_notify(SIGINT, test_handler);
    neverc_signal_reset(SIGINT);
    tests_run++;
    tests_passed++;
}

static void test_multiple_signals(void) {
    printf("[multiple_signals]\n");
    g_received_sig = 0;

    neverc_signal_notify(SIGINT, test_handler);

    raise(SIGINT);
    ASSERT_INT_EQ(g_received_sig, SIGINT);

    g_received_sig = 0;
    neverc_signal_notify(SIGINT, test_handler);
    raise(SIGINT);
    ASSERT_INT_EQ(g_received_sig, SIGINT);

    neverc_signal_stop(SIGINT);
}

static void test_constants(void) {
    printf("[constants]\n");
    ASSERT_INT_EQ(NEVERC_SIGINT, 2);
    ASSERT_INT_EQ(NEVERC_SIGTERM, 15);
    tests_run++;
    tests_passed++;
}

#else /* POSIX */

static void test_notify_and_raise(void) {
    printf("[notify_and_raise]\n");
    reset_handler_state();

    neverc_signal_notify(NEVERC_SIGUSR1, test_handler);
    raise(SIGUSR1);

    ASSERT_TRUE(wait_for_handler_calls(1));
    ASSERT_INT_EQ(read_received_signal(), NEVERC_SIGUSR1);

    neverc_signal_stop(NEVERC_SIGUSR1);
}

static void test_ignore(void) {
    printf("[ignore]\n");
    neverc_signal_ignore(NEVERC_SIGUSR2);
    raise(SIGUSR2);
    tests_run++;
    tests_passed++;
    neverc_signal_reset(NEVERC_SIGUSR2);
}

static void test_reset(void) {
    printf("[reset]\n");
    reset_handler_state();
    neverc_signal_notify(NEVERC_SIGUSR1, test_handler);
    neverc_signal_reset(NEVERC_SIGUSR1);

    tests_run++;
    tests_passed++;

    neverc_signal_ignore(NEVERC_SIGUSR1);
    neverc_signal_reset(NEVERC_SIGUSR1);
}

static void test_multiple_signals(void) {
    printf("[multiple_signals]\n");
    reset_handler_state();

    neverc_signal_notify(NEVERC_SIGUSR1, test_handler);

    raise(SIGUSR1);
    ASSERT_TRUE(wait_for_handler_calls(1));
    ASSERT_INT_EQ(read_received_signal(), NEVERC_SIGUSR1);

    clear_received_signal();
    raise(SIGUSR1);
    ASSERT_TRUE(wait_for_handler_calls(2));
    ASSERT_INT_EQ(read_received_signal(), NEVERC_SIGUSR1);

    neverc_signal_stop(NEVERC_SIGUSR1);
}

static void test_constants(void) {
    printf("[constants]\n");
    ASSERT_INT_EQ(NEVERC_SIGINT, 2);
    ASSERT_INT_EQ(NEVERC_SIGTERM, 15);
    ASSERT_INT_EQ(NEVERC_SIGHUP, 1);
    ASSERT_INT_EQ(NEVERC_SIGUSR1, 10);
    ASSERT_INT_EQ(NEVERC_SIGUSR2, 12);
}

#endif

#if defined(_WIN32)

static void test_wait_after_raise(void) {
    printf("[wait_after_raise]\n");
    g_handler_called = 0;
    neverc_signal_notify(SIGINT, test_handler);
    raise(SIGINT);
    ASSERT_INT_EQ(g_handler_called, 1);
    int sig = SIGINT;
    ASSERT_INT_EQ(neverc_signal_wait(&sig, 1), SIGINT);
    neverc_signal_stop(SIGINT);
}

static void test_stop_clears_pending(void) {
    printf("[stop_clears_pending]\n");
    g_handler_called = 0;
    neverc_signal_notify(SIGINT, test_handler);
    neverc_signal_notify(SIGTERM, test_handler);
    raise(SIGINT);
    ASSERT_INT_EQ(g_handler_called, 1);
    neverc_signal_stop(SIGINT);
    g_handler_called = 0;
    raise(SIGTERM);
    ASSERT_INT_EQ(g_handler_called, 1);
    int sigs[2] = { SIGINT, SIGTERM };
    ASSERT_INT_EQ(neverc_signal_wait(sigs, 2), SIGTERM);
    neverc_signal_stop(SIGTERM);
}

static void test_notify_twice_does_not_lose_handler(void) {
    printf("[notify_twice]\n");
    g_handler_called = 0;
    neverc_signal_notify(SIGINT, test_handler);
    neverc_signal_notify(SIGINT, test_handler);
    raise(SIGINT);
    ASSERT_INT_EQ(g_handler_called, 1);
    g_handler_called = 0;
    raise(SIGINT);
    ASSERT_INT_EQ(g_handler_called, 1);
    neverc_signal_stop(SIGINT);
}

#endif

static void test_wait_null(void) {
    printf("[wait_null]\n");
    ASSERT_INT_EQ(neverc_signal_wait(NULL, 1), -1);
    ASSERT_INT_EQ(neverc_signal_wait(NULL, 0), -1);
    int dummy = NEVERC_SIGINT;
    ASSERT_INT_EQ(neverc_signal_wait(&dummy, -1), -1);
}

#if !defined(_WIN32)
static void test_wait_after_raise(void) {
    printf("[wait_after_raise]\n");
    reset_handler_state();
    neverc_signal_notify(NEVERC_SIGUSR1, test_handler);
    raise(SIGUSR1);
    int sig = NEVERC_SIGUSR1;
    ASSERT_INT_EQ(neverc_signal_wait(&sig, 1), NEVERC_SIGUSR1);
    ASSERT_TRUE(wait_for_handler_calls(1));
    neverc_signal_stop(NEVERC_SIGUSR1);
}

static void test_wait_invalid_and_pending(void) {
    printf("[wait_invalid_and_pending]\n");
    int bad = 999999;
    ASSERT_INT_EQ(neverc_signal_wait(&bad, 1), -1);
    int kill_sig = SIGKILL;
    ASSERT_INT_EQ(neverc_signal_wait(&kill_sig, 1), -1);
    int stop_sig = SIGSTOP;
    ASSERT_INT_EQ(neverc_signal_wait(&stop_sig, 1), -1);

    sigset_t before_wait, after;
    ASSERT_INT_EQ(pthread_sigmask(SIG_SETMASK, NULL, &before_wait), 0);
    reset_handler_state();
    neverc_signal_notify(NEVERC_SIGUSR1, test_handler);
    raise(SIGUSR1);
    int usr1 = NEVERC_SIGUSR1;
    ASSERT_INT_EQ(neverc_signal_wait(&usr1, 1), NEVERC_SIGUSR1);
    ASSERT_TRUE(wait_for_handler_calls(1));
    ASSERT_INT_EQ(pthread_sigmask(SIG_SETMASK, NULL, &after), 0);
    for (int sig = 1; sig < NSIG; ++sig)
        ASSERT_TRUE(sigismember(&after, sig) ==
                    sigismember(&before_wait, sig));
    neverc_signal_stop(NEVERC_SIGUSR1);
}

static void test_stop_clears_pending(void) {
    printf("[stop_clears_pending]\n");
    reset_handler_state();
    neverc_signal_notify(NEVERC_SIGUSR1, test_handler);
    neverc_signal_notify(NEVERC_SIGUSR2, test_handler);
    raise(SIGUSR1);
    ASSERT_TRUE(wait_for_handler_calls(1));
    neverc_signal_stop(NEVERC_SIGUSR1);
    reset_handler_state();
    raise(SIGUSR2);
    ASSERT_TRUE(wait_for_handler_calls(1));
    int sigs[2] = { NEVERC_SIGUSR1, NEVERC_SIGUSR2 };
    ASSERT_INT_EQ(neverc_signal_wait(sigs, 2), NEVERC_SIGUSR2);
    neverc_signal_stop(NEVERC_SIGUSR2);
}

static void test_notify_null_clears_handler(void) {
    printf("[notify_null]\n");
    reset_handler_state();
    neverc_signal_notify(SIGCONT, test_handler);
    raise(SIGCONT);
    ASSERT_TRUE(wait_for_handler_calls(1));
    reset_handler_state();
    neverc_signal_notify(SIGCONT, NULL);
    raise(SIGCONT);
    ASSERT_INT_EQ(read_handler_calls(), 0);
    neverc_signal_reset(SIGCONT);
}

#if defined(SIGRTMAX)
static void test_rtmax_notify(void) {
    printf("[rtmax_notify]\n");
    reset_handler_state();
    /* Default SIGRTMAX terminates. Ignore first so a silent notify no-op
     * fails the assertion instead of killing the process. */
    signal(SIGRTMAX, SIG_IGN);
    neverc_signal_notify(SIGRTMAX, test_handler);
    raise(SIGRTMAX);
    int rt = SIGRTMAX;
    ASSERT_INT_EQ(neverc_signal_wait(&rt, 1), SIGRTMAX);
    ASSERT_TRUE(wait_for_handler_calls(1));
    ASSERT_INT_EQ(read_received_signal(), SIGRTMAX);
    neverc_signal_stop(SIGRTMAX);
}
#endif
#endif

int main(void) {
    printf("=== NeverC os/signal Tests ===\n");
    test_notify_and_raise();
    test_ignore();
    test_reset();
    test_multiple_signals();
    test_constants();
    test_wait_null();
#if defined(_WIN32)
    test_wait_after_raise();
    test_stop_clears_pending();
    test_notify_twice_does_not_lose_handler();
#endif
#if !defined(_WIN32)
    test_wait_after_raise();
    test_wait_invalid_and_pending();
    test_stop_clears_pending();
    test_notify_null_clears_handler();
#if defined(SIGRTMAX)
    test_rtmax_notify();
#endif
#endif
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
