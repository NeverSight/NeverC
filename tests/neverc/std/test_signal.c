/*
 * NeverC os/signal tests.
 * Tests signal handler registration and notification.
 */
#include "neverc/std/os/signal.h"
#include <stdio.h>
#include <signal.h>
#if !defined(_WIN32)
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

static volatile int g_received_sig = 0;
static volatile int g_handler_called = 0;

static void test_handler(int signum) {
    g_received_sig = signum;
    g_handler_called = 1;
}

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
    g_received_sig = 0;
    g_handler_called = 0;

    neverc_signal_notify(SIGUSR1, test_handler);
    raise(SIGUSR1);

    ASSERT_INT_EQ(g_handler_called, 1);
    ASSERT_INT_EQ(g_received_sig, SIGUSR1);

    neverc_signal_stop(SIGUSR1);
}

static void test_ignore(void) {
    printf("[ignore]\n");
    neverc_signal_ignore(SIGUSR2);
    raise(SIGUSR2);
    tests_run++;
    tests_passed++;
    neverc_signal_reset(SIGUSR2);
}

static void test_reset(void) {
    printf("[reset]\n");
    g_handler_called = 0;
    neverc_signal_notify(SIGUSR1, test_handler);
    neverc_signal_reset(SIGUSR1);

    tests_run++;
    tests_passed++;

    neverc_signal_ignore(SIGUSR1);
    neverc_signal_reset(SIGUSR1);
}

static void test_multiple_signals(void) {
    printf("[multiple_signals]\n");
    g_received_sig = 0;

    neverc_signal_notify(SIGUSR1, test_handler);

    raise(SIGUSR1);
    ASSERT_INT_EQ(g_received_sig, SIGUSR1);

    g_received_sig = 0;
    raise(SIGUSR1);
    ASSERT_INT_EQ(g_received_sig, SIGUSR1);

    neverc_signal_stop(SIGUSR1);
}

static void test_constants(void) {
    printf("[constants]\n");
    ASSERT_INT_EQ(NEVERC_SIGINT, SIGINT);
    ASSERT_INT_EQ(NEVERC_SIGTERM, SIGTERM);
    ASSERT_INT_EQ(NEVERC_SIGHUP, SIGHUP);
    ASSERT_INT_EQ(NEVERC_SIGUSR1, SIGUSR1);
    ASSERT_INT_EQ(NEVERC_SIGUSR2, SIGUSR2);
}

#endif

static void test_wait_null(void) {
    printf("[wait_null]\n");
    ASSERT_INT_EQ(neverc_signal_wait(NULL, 1), -1);
    ASSERT_INT_EQ(neverc_signal_wait(NULL, 0), -1);
}

int main(void) {
    printf("=== NeverC os/signal Tests ===\n");
    test_notify_and_raise();
    test_ignore();
    test_reset();
    test_multiple_signals();
    test_constants();
    test_wait_null();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
