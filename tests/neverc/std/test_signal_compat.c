#include "neverc/std/os/signal.h"

#include <signal.h>
#include <stdio.h>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "check failed at line %d: %s\n",            \
                    __LINE__, #condition);                                 \
            return 1;                                                      \
        }                                                                  \
    } while (0)

_Static_assert(NEVERC_SIGHUP == 1, "v3389 SIGHUP ABI");
_Static_assert(NEVERC_SIGINT == 2, "v3389 SIGINT ABI");
_Static_assert(NEVERC_SIGUSR1 == 10, "v3389 SIGUSR1 ABI");
_Static_assert(NEVERC_SIGUSR2 == 12, "v3389 SIGUSR2 ABI");
_Static_assert(NEVERC_SIGPIPE == 13, "v3389 SIGPIPE ABI");
_Static_assert(NEVERC_SIGTERM == 15, "v3389 SIGTERM ABI");
_Static_assert(NEVERC_SIGKILL == 9, "v3389 SIGKILL ABI");

#if !defined(_WIN32)
#include <pthread.h>
#include <time.h>

static pthread_mutex_t handler_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t handler_changed = PTHREAD_COND_INITIALIZER;
static int handler_called;
static int handler_signal;

static void record_signal(int signum) {
    pthread_mutex_lock(&handler_lock);
    handler_signal = signum;
    handler_called = 1;
    pthread_cond_broadcast(&handler_changed);
    pthread_mutex_unlock(&handler_lock);
}

static int wait_for_handler(void) {
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return 0;
    deadline.tv_sec += 5;
    pthread_mutex_lock(&handler_lock);
    while (!handler_called) {
        int rc = pthread_cond_timedwait(&handler_changed, &handler_lock,
                                        &deadline);
        if (rc != 0) {
            pthread_mutex_unlock(&handler_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&handler_lock);
    return 1;
}

static int check_representation(int public_signal, int native_signal) {
    pthread_mutex_lock(&handler_lock);
    handler_called = 0;
    handler_signal = 0;
    pthread_mutex_unlock(&handler_lock);
    signal(native_signal, SIG_IGN);
    neverc_signal_notify(public_signal, record_signal);
    raise(native_signal);
    CHECK(neverc_signal_wait(&public_signal, 1) == public_signal);
    CHECK(wait_for_handler());
    pthread_mutex_lock(&handler_lock);
    int received_signal = handler_signal;
    pthread_mutex_unlock(&handler_lock);
    CHECK(received_signal == public_signal);
    neverc_signal_stop(public_signal);
    return 0;
}
#endif

int main(void) {
#if defined(_WIN32)
    int invalid = 99;
    CHECK(neverc_signal_wait(&invalid, 1) == -1);
#else
    CHECK(check_representation(NEVERC_SIGUSR1, SIGUSR1) == 0);
    CHECK(check_representation(NEVERC_SIGUSR2, SIGUSR2) == 0);

    /* Binaries built against the short-lived native-valued header keep their
     * native callback/wait representation as well. */
    CHECK(check_representation(SIGUSR1, SIGUSR1) == 0);
    CHECK(check_representation(SIGUSR2, SIGUSR2) == 0);

    int invalid = 0;
    CHECK(neverc_signal_wait(&invalid, 1) == -1);
    invalid = 999999;
    CHECK(neverc_signal_wait(&invalid, 1) == -1);
    invalid = NEVERC_SIGKILL;
    CHECK(neverc_signal_wait(&invalid, 1) == -1);
    invalid = NEVERC_SIGSTOP;
    CHECK(neverc_signal_wait(&invalid, 1) == -1);
#endif
    puts("passed");
    return 0;
}
