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

#ifdef _WIN32
#define NEVERC_SIGINT   2
#define NEVERC_SIGTERM  15
#define NEVERC_SIGHUP   1
#define NEVERC_SIGUSR1  10
#define NEVERC_SIGUSR2  12
#define NEVERC_SIGPIPE  13
#define NEVERC_SIGKILL  9
#define NEVERC_SIGSTOP  19
#else
#include <signal.h>
#define NEVERC_SIGINT   SIGINT
#define NEVERC_SIGTERM  SIGTERM
#define NEVERC_SIGHUP   SIGHUP
#define NEVERC_SIGUSR1  SIGUSR1
#define NEVERC_SIGUSR2  SIGUSR2
#define NEVERC_SIGPIPE  SIGPIPE
#define NEVERC_SIGKILL  SIGKILL
#define NEVERC_SIGSTOP  SIGSTOP
#endif

typedef void (*neverc_signal_handler_t)(int signum);

/* NULL handler is equivalent to neverc_signal_stop. */
void neverc_signal_notify(int signum, neverc_signal_handler_t handler);

void neverc_signal_stop(int signum);

void neverc_signal_reset(int signum);

void neverc_signal_ignore(int signum);

/* Blocks until one of sigs is pending/received. Returns the signal number,
 * or -1 on invalid input. SIGKILL/SIGSTOP cannot be waited for. */
int  neverc_signal_wait(const int *sigs, int nsigs);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/os.h>
#endif


#endif
