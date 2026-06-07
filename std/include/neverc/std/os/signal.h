#ifndef NEVERC_OS_SIGNAL_H
#define NEVERC_OS_SIGNAL_H

/*
 * NeverC os/signal — signal handling.
 * Mirrors Go os/signal package.
 * Cross-platform: POSIX signals + Windows limited signal support.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SIGINT   2
#define NEVERC_SIGTERM  15
#define NEVERC_SIGHUP   1
#define NEVERC_SIGUSR1  10
#define NEVERC_SIGUSR2  12
#define NEVERC_SIGPIPE  13

typedef void (*neverc_signal_handler_t)(int signum);

void neverc_signal_notify(int signum, neverc_signal_handler_t handler);

void neverc_signal_stop(int signum);

void neverc_signal_reset(int signum);

void neverc_signal_ignore(int signum);

int  neverc_signal_wait(const int *sigs, int nsigs);

#ifdef __cplusplus
}
#endif

#endif
