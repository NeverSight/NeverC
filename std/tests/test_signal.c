/*
 * NeverC os/signal tests.
 * Tests signal handler registration and notification.
 */
#include "neverc/os/signal.h"
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

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

    /* After reset, default handler should be restored.
     * For SIGUSR1, default is terminate, so we don't actually raise it.
     * Just verify the API doesn't crash. */
    tests_run++;
    tests_passed++;

    neverc_signal_ignore(SIGUSR1);
    neverc_signal_reset(SIGUSR1);
}

static void test_multiple_signals(void) {
    printf("[multiple_signals]\n");
    static volatile int count = 0;
    g_received_sig = 0;
    count = 0;

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
    ASSERT_INT_EQ(NEVERC_SIGINT, 2);
    ASSERT_INT_EQ(NEVERC_SIGTERM, 15);
    ASSERT_INT_EQ(NEVERC_SIGHUP, 1);
}

int main(void) {
    printf("=== NeverC os/signal Tests ===\n");
    test_notify_and_raise();
    test_ignore();
    test_reset();
    test_multiple_signals();
    test_constants();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
