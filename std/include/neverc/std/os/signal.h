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

/* Stable NeverC signal IDs.  These values were published by v3389 and are
 * intentionally independent of the host's native signal numbers.  The POSIX
 * implementation translates at its boundary.  In particular, Darwin uses
 * native 30/31 for SIGUSR1/SIGUSR2 while legacy NeverC callers pass 10/12.
 *
 * Native signal numbers remain accepted when they do not collide with these
 * IDs.  A collision is necessarily interpreted as the stable NeverC ID (for
 * example Darwin native SIGBUS=10 and SIGSYS=12 cannot be selected by number
 * through this compatibility API). */
#define NEVERC_SIGINT   2
#define NEVERC_SIGTERM  15
#define NEVERC_SIGHUP   1
#define NEVERC_SIGUSR1  10
#define NEVERC_SIGUSR2  12
#define NEVERC_SIGPIPE  13
#define NEVERC_SIGKILL  9
#ifdef _WIN32
#define NEVERC_SIGSTOP  19
#else
#include <signal.h>
/* SIGSTOP was added after v3389 and remains native-valued so existing native
 * SIGCONT callers are not reinterpreted as STOP on Darwin (19 vs 17). */
#define NEVERC_SIGSTOP  SIGSTOP
#endif

typedef void (*neverc_signal_handler_t)(int signum);

/* NULL handler is equivalent to neverc_signal_stop. The handler receives the
 * same NeverC/native numeric representation passed to notify.
 *
 * On POSIX the callback runs asynchronously on a per-signal library worker,
 * never in the restricted async-signal context. Workers isolate different
 * signals, while callbacks for one signal remain ordered. notify/stop/reset/
 * ignore may be called by a callback; wait returns -1 from any such callback
 * to avoid blocking its worker. On POSIX, stop/reset/ignore do not return while
 * a callback already started by another thread is still running. Calls made
 * from a callback invalidate queued work immediately but do not wait for any
 * callback worker, which prevents two callbacks from cross-stopping each other
 * into a deadlock. */
void neverc_signal_notify(int signum, neverc_signal_handler_t handler);

void neverc_signal_stop(int signum);

void neverc_signal_reset(int signum);

void neverc_signal_ignore(int signum);

/* Blocks until one of sigs is pending/received. Returns the matching element
 * using the caller's numeric representation, or -1 on invalid input.
 * SIGKILL/SIGSTOP cannot be waited for.  POSIX wait does not alter the calling
 * thread's signal mask and restores a pre-existing sigaction after the final
 * transient waiter exits. A signal blocked in every process thread cannot be
 * observed until some thread unblocks it.
 *
 * After fork(), POSIX notify registrations and inherited waits are cleared in
 * the child because their dispatcher/worker threads no longer exist; explicit
 * ignore dispositions remain inherited. Re-register child callbacks as needed. */
int  neverc_signal_wait(const int *sigs, int nsigs);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/os.h>
#endif


#endif
