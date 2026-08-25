/*
 * NeverC os/signal — signal handling.
 * Mirrors Go os/signal package.
 *
 * POSIX: sigaction() based handler registration.
 * Windows: SetConsoleCtrlHandler() for SIGINT/SIGTERM equivalent.
 */

#include "neverc/std/os/signal.h"
#include "neverc/std/_platform.h"

#if defined(NEVERC_PLATFORM_WINDOWS)

#include <limits.h>
#include <windows.h>
#include <signal.h>

static PVOID volatile g_win_handlers[32] = {0};
static volatile LONG g_win_pending[32] = {0};
static volatile LONG g_win_ignored[32] = {0};
static volatile LONG g_win_waiters[32] = {0};
/* 0 = default/stopped, 1 = notify wrapper, 2 = ignored. */
static volatile LONG g_win_crt_mode[32] = {0};
static HANDLE g_win_events[32] = {0};
static int g_win_ctrl_registered = 0;
static SRWLOCK g_win_config_lock = SRWLOCK_INIT;
static BOOL WINAPI win_ctrl_handler(DWORD type);

static neverc_signal_handler_t win_load_handler(int signum) {
    if (signum < 0 || signum >= 32) return NULL;
    return (neverc_signal_handler_t)InterlockedCompareExchangePointer(
        &g_win_handlers[signum], NULL, NULL);
}

static void win_store_handler(int signum,
                              neverc_signal_handler_t handler) {
    if (signum < 0 || signum >= 32) return;
    (void)InterlockedExchangePointer(&g_win_handlers[signum],
                                     (PVOID)handler);
}

/* g_win_config_lock is held. The event is created before an OS handler is
 * installed and is never closed or replaced, so handlers only read it. */
static HANDLE win_ensure_event_locked(int signum) {
    if (signum < 0 || signum >= 32) return NULL;
    if (!g_win_events[signum])
        g_win_events[signum] = CreateEventA(NULL, TRUE, FALSE, NULL);
    return g_win_events[signum];
}

static void win_mark_pending(int signum) {
    if (signum < 0 || signum >= 32) return;
    LONG observed = InterlockedCompareExchange(&g_win_pending[signum], 0, 0);
    while (observed < LONG_MAX) {
        LONG replaced = InterlockedCompareExchange(&g_win_pending[signum],
                                                   observed + 1, observed);
        if (replaced == observed)
            break;
        observed = replaced;
    }
    if (g_win_events[signum]) SetEvent(g_win_events[signum]);
}

static int win_take_pending(int signum, LONG *remaining) {
    LONG observed;
    if (signum < 0 || signum >= 32)
        return 0;
    observed = InterlockedCompareExchange(&g_win_pending[signum], 0, 0);
    while (observed > 0) {
        LONG replaced = InterlockedCompareExchange(&g_win_pending[signum],
                                                   observed - 1, observed);
        if (replaced == observed) {
            if (remaining)
                *remaining = observed - 1;
            return 1;
        }
        observed = replaced;
    }
    return 0;
}

static int win_is_ignored(int signum) {
    return signum >= 0 && signum < 32 &&
           InterlockedCompareExchange(&g_win_ignored[signum], 0, 0) != 0;
}

static int win_has_waiter(int signum) {
    return signum >= 0 && signum < 32 &&
           InterlockedCompareExchange(&g_win_waiters[signum], 0, 0) != 0;
}

static int win_ensure_ctrl_handler_locked(void) {
    if (!g_win_ctrl_registered &&
        SetConsoleCtrlHandler(win_ctrl_handler, TRUE))
        g_win_ctrl_registered = 1;
    return g_win_ctrl_registered ? 0 : -1;
}

static BOOL WINAPI win_ctrl_handler(DWORD type) {
    switch (type) {
        /* Go runtime/os_windows.go ctrlHandler: CTRL_C and CTRL_BREAK
         * are SIGINT; close / logoff / shutdown are SIGTERM. */
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT: {
            int ignored = win_is_ignored(NEVERC_SIGINT);
            int waiting = win_has_waiter(NEVERC_SIGINT);
            if (ignored && !waiting) return TRUE;
            neverc_signal_handler_t handler =
                ignored ? NULL : win_load_handler(NEVERC_SIGINT);
            win_mark_pending(NEVERC_SIGINT);
            if (handler) {
                handler(NEVERC_SIGINT);
                return TRUE;
            }
            if (waiting) return TRUE;
            break;
        }
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT: {
            int ignored = win_is_ignored(NEVERC_SIGTERM);
            int waiting = win_has_waiter(NEVERC_SIGTERM);
            if (ignored && !waiting) return TRUE;
            neverc_signal_handler_t handler =
                ignored ? NULL : win_load_handler(NEVERC_SIGTERM);
            win_mark_pending(NEVERC_SIGTERM);
            if (handler) {
                handler(NEVERC_SIGTERM);
                return TRUE;
            }
            if (waiting) return TRUE;
            break;
        }
    }
    return FALSE;
}

static void win_crt_handler(int signum) {
    neverc_signal_handler_t handler = NULL;
    if (signum >= 0 && signum < 32)
        handler = win_load_handler(signum);
    win_mark_pending(signum);
    /* MSVCRT resets to SIG_DFL after delivery; re-arm the wrapper. */
    if (signum >= 0 && signum < 32 &&
        InterlockedCompareExchange(&g_win_crt_mode[signum], 0, 0) == 1) {
        signal(signum, win_crt_handler);
        /* stop/ignore may have raced the re-arm after our first load. Their
         * disposition wins when the mode changed meanwhile. */
        LONG mode = InterlockedCompareExchange(&g_win_crt_mode[signum], 0, 0);
        if (mode == 0)
            signal(signum, SIG_DFL);
        else if (mode == 2)
            signal(signum, SIG_IGN);
    }
    if (handler) handler(signum);
}

static int win_crt_supported(int signum) {
    return signum == NEVERC_SIGINT || signum == NEVERC_SIGTERM ||
           signum == SIGINT || signum == SIGTERM ||
#ifdef SIGBREAK
           signum == SIGBREAK ||
#endif
#ifdef SIGABRT
           signum == SIGABRT ||
#endif
#ifdef SIGFPE
           signum == SIGFPE ||
#endif
#ifdef SIGILL
           signum == SIGILL ||
#endif
#ifdef SIGSEGV
           signum == SIGSEGV ||
#endif
           0;
}

/* g_win_config_lock is held. Waiters temporarily take precedence over ignore
 * so wait can observe a signal; the ignore disposition is restored when the
 * final waiter leaves. */
static int win_apply_crt_disposition_locked(int signum) {
    if (!win_crt_supported(signum))
        return 0;
    if (win_load_handler(signum) || win_has_waiter(signum)) {
        InterlockedExchange(&g_win_crt_mode[signum], 1);
        return signal(signum, win_crt_handler) == SIG_ERR ? -1 : 0;
    }
    if (win_is_ignored(signum)) {
        InterlockedExchange(&g_win_crt_mode[signum], 2);
        return signal(signum, SIG_IGN) == SIG_ERR ? -1 : 0;
    }
    InterlockedExchange(&g_win_crt_mode[signum], 0);
    return signal(signum, SIG_DFL) == SIG_ERR ? -1 : 0;
}

static int win_console_state_needed_locked(void) {
    return win_load_handler(NEVERC_SIGINT) ||
           win_load_handler(NEVERC_SIGTERM) ||
           win_is_ignored(NEVERC_SIGINT) ||
           win_is_ignored(NEVERC_SIGTERM) ||
           win_has_waiter(NEVERC_SIGINT) ||
           win_has_waiter(NEVERC_SIGTERM);
}

static int win_update_ctrl_registration_locked(void) {
    if (win_console_state_needed_locked()) {
        return win_ensure_ctrl_handler_locked();
    } else if (g_win_ctrl_registered) {
        if (SetConsoleCtrlHandler(win_ctrl_handler, FALSE))
            g_win_ctrl_registered = 0;
    }
    return 0;
}

void neverc_signal_notify(int signum, neverc_signal_handler_t handler) {
    if (signum < 0 || signum >= 32) return;
    if (!handler) {
        neverc_signal_stop(signum);
        return;
    }
    AcquireSRWLockExclusive(&g_win_config_lock);
    win_ensure_event_locked(signum);
    win_store_handler(signum, handler);
    InterlockedExchange(&g_win_ignored[signum], 0);
    if (signum == NEVERC_SIGINT || signum == NEVERC_SIGTERM)
        (void)win_update_ctrl_registration_locked();
    (void)win_apply_crt_disposition_locked(signum);
    ReleaseSRWLockExclusive(&g_win_config_lock);
}

void neverc_signal_stop(int signum) {
    if (signum < 0 || signum >= 32) return;
    AcquireSRWLockExclusive(&g_win_config_lock);
    win_store_handler(signum, NULL);
    InterlockedExchange(&g_win_ignored[signum], 0);
    InterlockedExchange(&g_win_pending[signum], 0);
    if (g_win_events[signum]) ResetEvent(g_win_events[signum]);
    (void)win_apply_crt_disposition_locked(signum);
    if (signum == NEVERC_SIGINT || signum == NEVERC_SIGTERM)
        (void)win_update_ctrl_registration_locked();
    ReleaseSRWLockExclusive(&g_win_config_lock);
}

void neverc_signal_reset(int signum) {
    neverc_signal_stop(signum);
}

void neverc_signal_ignore(int signum) {
    if (signum < 0 || signum >= 32) return;
    AcquireSRWLockExclusive(&g_win_config_lock);
    win_store_handler(signum, NULL);
    InterlockedExchange(&g_win_ignored[signum], 1);
    InterlockedExchange(&g_win_pending[signum], 0);
    if (g_win_events[signum]) ResetEvent(g_win_events[signum]);
    if (signum == NEVERC_SIGINT || signum == NEVERC_SIGTERM)
        (void)win_update_ctrl_registration_locked();
    (void)win_apply_crt_disposition_locked(signum);
    ReleaseSRWLockExclusive(&g_win_config_lock);
}

int neverc_signal_wait(const int *sigs, int nsigs) {
    int i;
    int unique_sigs[32];
    HANDLE events[32];
    DWORD unique_count = 0;
    DWORD installed = 0;
    int result = -1;
    if (!sigs || nsigs <= 0 || nsigs > 32) return -1;
    for (i = 0; i < nsigs; i++) {
        if (sigs[i] <= 0 || sigs[i] >= 32 ||
            !win_crt_supported(sigs[i])) return -1;
        if (sigs[i] == NEVERC_SIGKILL || sigs[i] == NEVERC_SIGSTOP) return -1;
        int duplicate = 0;
        for (DWORD j = 0; j < unique_count; ++j) {
            if (unique_sigs[j] == sigs[i]) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate)
            unique_sigs[unique_count++] = sigs[i];
    }
    AcquireSRWLockExclusive(&g_win_config_lock);
    for (; installed < unique_count; ++installed) {
        int signum = unique_sigs[installed];
        events[installed] = win_ensure_event_locked(signum);
        if (!events[installed])
            break;
        InterlockedIncrement(&g_win_waiters[signum]);
        if ((signum == NEVERC_SIGINT || signum == NEVERC_SIGTERM) &&
            win_update_ctrl_registration_locked() != 0) {
            InterlockedDecrement(&g_win_waiters[signum]);
            break;
        }
        if (win_apply_crt_disposition_locked(signum) != 0) {
            InterlockedDecrement(&g_win_waiters[signum]);
            (void)win_apply_crt_disposition_locked(signum);
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_win_config_lock);
    if (installed != unique_count)
        goto cleanup;
    for (;;) {
        for (i = 0; i < nsigs; i++) {
            LONG remaining = 0;
            if (win_take_pending(sigs[i], &remaining)) {
                HANDLE event = g_win_events[sigs[i]];
                if (remaining == 0)
                    ResetEvent(event);
                /* Do not erase a delivery that raced ResetEvent. */
                if (InterlockedCompareExchange(&g_win_pending[sigs[i]], 0, 0)
                    != 0)
                    SetEvent(event);
                result = sigs[i];
                goto cleanup;
            }
        }
        DWORD waited = WaitForMultipleObjects(unique_count, events, FALSE,
                                               INFINITE);
        if (waited >= WAIT_OBJECT_0 + unique_count)
            goto cleanup;
    }

cleanup:
    AcquireSRWLockExclusive(&g_win_config_lock);
    for (DWORD j = 0; j < installed; ++j) {
        int signum = unique_sigs[j];
        InterlockedDecrement(&g_win_waiters[signum]);
        (void)win_apply_crt_disposition_locked(signum);
    }
    (void)win_update_ctrl_registration_locked();
    ReleaseSRWLockExclusive(&g_win_config_lock);
    return result;
}

#else /* POSIX */

#include <stdatomic.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Linux NSIG is 65 (signals 1..64, including SIGRTMAX). A table of 64
 * silently dropped notify/stop/ignore for signal 64 while sigwait still
 * accepted it — raise(SIGRTMAX) then used the default terminate action. */
#if defined(NSIG) && NSIG > 0
#define NEVERC_SIGNAL_NSIG NSIG
#else
#define NEVERC_SIGNAL_NSIG 65
#endif

enum posix_signal_disposition {
    POSIX_SIGNAL_DEFAULT = 0,
    POSIX_SIGNAL_NOTIFY,
    POSIX_SIGNAL_IGNORE
};

typedef unsigned int posix_signal_generation_t;

typedef struct posix_signal_event {
    posix_signal_generation_t generation;
} posix_signal_event_t;

typedef struct posix_callback_job {
    neverc_signal_handler_t handler;
    int callback_signum;
    posix_signal_generation_t generation;
    struct posix_callback_job *next;
} posix_callback_job_t;

typedef struct posix_signal_state {
    int read_fd;
    neverc_signal_handler_t handler;
    int callback_signum;
    unsigned int waiter_count;
    unsigned int callbacks_inflight;
    unsigned int pending_count;
    posix_signal_generation_t generation;
    posix_signal_generation_t inflight_generation;
    enum posix_signal_disposition disposition;
    struct sigaction saved_action;
    int saved_action_valid;
    pthread_t worker;
    int worker_started;
    unsigned int callback_queue_count;
    posix_callback_job_t *callback_head;
    posix_callback_job_t *callback_tail;
} posix_signal_state_t;

typedef struct posix_thread_context {
    int native_signum;
    unsigned long long runtime_generation;
} posix_thread_context_t;

#define NEVERC_SIGNAL_DISPATCH_BATCH 256U
#define NEVERC_SIGNAL_CALLBACK_QUEUE_MAX 1024U

/* The handler uses C11 atomics in async-signal context. Require types for
 * which the implementation guarantees lock-free operations rather than
 * risking a hidden libc lock from an atomic helper. */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "signal transport requires always-lock-free int atomics");

/* The async handler only performs lock-free atomic operations and write(2).
 * A generation travels with every pipe record so a late handler from a prior
 * registration cannot be interpreted using a replacement callback. */
static posix_signal_state_t g_signal_states[NEVERC_SIGNAL_NSIG];
static _Atomic int g_signal_write_fds[NEVERC_SIGNAL_NSIG];
static _Atomic unsigned int
    g_signal_active_generations[NEVERC_SIGNAL_NSIG];
static _Atomic unsigned int
    g_signal_overflow_generations[NEVERC_SIGNAL_NSIG];
static pthread_mutex_t g_signal_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t *g_signal_changed = NULL;
static pthread_t g_signal_dispatcher;
static int g_signal_control_read = -1;
static int g_signal_control_write = -1;
static int g_signal_runtime_ready = 0;
static int g_signal_state_initialized = 0;
static int g_signal_atfork_registered = 0;
static unsigned long long g_signal_runtime_generation = 1;
static struct sigaction g_signal_transport_action;
static struct sigaction g_signal_default_action;
static struct sigaction g_signal_ignore_action;
static _Thread_local int g_signal_callback_native;

static int posix_signal_to_native(int signum) {
    switch (signum) {
        case NEVERC_SIGHUP:   return SIGHUP;
        case NEVERC_SIGINT:   return SIGINT;
        case NEVERC_SIGKILL:  return SIGKILL;
        case NEVERC_SIGUSR1:  return SIGUSR1;
        case NEVERC_SIGUSR2:  return SIGUSR2;
        case NEVERC_SIGPIPE:  return SIGPIPE;
        case NEVERC_SIGTERM:  return SIGTERM;
        case NEVERC_SIGSTOP:  return SIGSTOP;
        default:               return signum;
    }
}

static int posix_signal_in_range(int signum) {
    return signum > 0 && signum < NEVERC_SIGNAL_NSIG;
}

static int posix_set_fd_flags(int fd) {
    int status_flags = fcntl(fd, F_GETFL, 0);
    int descriptor_flags = fcntl(fd, F_GETFD, 0);
    if (status_flags < 0 || descriptor_flags < 0)
        return -1;
    if (fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) != 0)
        return -1;
    if (fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0)
        return -1;
    return 0;
}

static int posix_make_pipe(int fds[2]) {
    if (pipe(fds) != 0)
        return -1;
    if (posix_set_fd_flags(fds[0]) != 0 ||
        posix_set_fd_flags(fds[1]) != 0) {
        close(fds[0]);
        close(fds[1]);
        fds[0] = -1;
        fds[1] = -1;
        return -1;
    }
    return 0;
}

static void posix_drain_fd(int fd) {
    unsigned char buffer[128];
    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0)
            continue;
        if (count < 0 && errno == EINTR)
            continue;
        return;
    }
}

static void posix_record_overflow(int native_signum,
                                  posix_signal_generation_t generation) {
    unsigned int observed = atomic_load_explicit(
        &g_signal_overflow_generations[native_signum],
        memory_order_relaxed);
    while (observed < generation &&
           !atomic_compare_exchange_weak_explicit(
               &g_signal_overflow_generations[native_signum], &observed,
               generation, memory_order_relaxed, memory_order_relaxed)) {
    }
}

static void posix_signal_handler(int signum) {
    int saved_errno = errno;
    if (posix_signal_in_range(signum)) {
        posix_signal_generation_t generation = atomic_load_explicit(
            &g_signal_active_generations[signum], memory_order_acquire);
        int fd = atomic_load_explicit(&g_signal_write_fds[signum],
                                      memory_order_relaxed);
        if (generation != 0 && fd >= 0) {
            posix_signal_event_t event = {generation};
            ssize_t written;
            do {
                written = write(fd, &event, sizeof(event));
            } while (written < 0 && errno == EINTR);
            if (written != (ssize_t)sizeof(event) &&
                (errno == EAGAIN || errno == EWOULDBLOCK))
                posix_record_overflow(signum, generation);
        }
    }
    errno = saved_errno;
}

static int posix_transport_needed(const posix_signal_state_t *state) {
    return state->waiter_count > 0 ||
           state->disposition == POSIX_SIGNAL_NOTIFY;
}

static int posix_apply_disposition_locked(int native_signum) {
    posix_signal_state_t *state = &g_signal_states[native_signum];
    if (state->waiter_count > 0 ||
        state->disposition == POSIX_SIGNAL_NOTIFY)
        return sigaction(native_signum, &g_signal_transport_action, NULL);
    if (state->disposition == POSIX_SIGNAL_IGNORE)
        return sigaction(native_signum, &g_signal_ignore_action, NULL);
    if (state->saved_action_valid) {
        if (sigaction(native_signum, &state->saved_action, NULL) != 0)
            return -1;
        state->saved_action_valid = 0;
        return 0;
    }
    return sigaction(native_signum, &g_signal_default_action, NULL);
}

static void posix_wake_dispatcher_locked(void) {
    unsigned char event = 1;
    ssize_t written;
    do {
        written = write(g_signal_control_write, &event, sizeof(event));
    } while (written < 0 && errno == EINTR);
    (void)written;
}

static int posix_ensure_signal_pipe_locked(int native_signum) {
    posix_signal_state_t *state = &g_signal_states[native_signum];
    int fds[2] = {-1, -1};
    if (state->read_fd >= 0)
        return 0;
    if (posix_make_pipe(fds) != 0)
        return -1;
    state->read_fd = fds[0];
    /* Publish the immutable write descriptor last, before sigaction can make
     * posix_signal_handler reachable for this signal. */
    atomic_store_explicit(&g_signal_write_fds[native_signum], fds[1],
                          memory_order_release);
    posix_wake_dispatcher_locked();
    return 0;
}

static void posix_discard_callback_jobs_locked(
    posix_signal_state_t *state) {
    posix_callback_job_t *job = state->callback_head;
    while (job) {
        posix_callback_job_t *next = job->next;
        free(job);
        job = next;
    }
    state->callback_head = NULL;
    state->callback_tail = NULL;
    state->callback_queue_count = 0;
}

static int posix_rotate_generation_locked(int native_signum) {
    posix_signal_state_t *state = &g_signal_states[native_signum];
    atomic_store_explicit(&g_signal_active_generations[native_signum], 0,
                          memory_order_release);
    (void)atomic_exchange_explicit(
        &g_signal_overflow_generations[native_signum], 0,
        memory_order_acq_rel);
    state->pending_count = 0;
    posix_discard_callback_jobs_locked(state);
    if (state->read_fd >= 0)
        posix_drain_fd(state->read_fd);
    /* A late old-generation handler may publish overflow after the first
     * exchange. Clear anything already visible before the new generation is
     * published; later old values are lower and are ignored by the dispatcher. */
    (void)atomic_exchange_explicit(
        &g_signal_overflow_generations[native_signum], 0,
        memory_order_acq_rel);
    if (state->generation == UINT_MAX)
        return -1;
    ++state->generation;
    return 0;
}

static int posix_reconfigure_signal_locked(int native_signum) {
    int rotate_result = posix_rotate_generation_locked(native_signum);
    int action_result = posix_apply_disposition_locked(native_signum);
    if (rotate_result != 0 || action_result != 0) {
        atomic_store_explicit(&g_signal_active_generations[native_signum], 0,
                              memory_order_release);
        return -1;
    }
    /* Install the transport action before publishing its generation. A signal
     * in this short window is safely dropped by the handler; publishing first
     * would leave first-time notify exposed to the previous default action. */
    if (posix_transport_needed(&g_signal_states[native_signum]))
        atomic_store_explicit(&g_signal_active_generations[native_signum],
                              g_signal_states[native_signum].generation,
                              memory_order_release);
    return 0;
}

static void posix_finish_callback(
    int native_signum, unsigned long long runtime_generation,
    posix_signal_generation_t callback_generation) {
    pthread_mutex_lock(&g_signal_lock);
    posix_signal_state_t *state = &g_signal_states[native_signum];
    if (runtime_generation == g_signal_runtime_generation &&
        state->callbacks_inflight > 0 &&
        state->inflight_generation == callback_generation) {
        --state->callbacks_inflight;
        if (state->callbacks_inflight == 0)
            state->inflight_generation = 0;
    }
    if (g_signal_changed)
        pthread_cond_broadcast(g_signal_changed);
    pthread_mutex_unlock(&g_signal_lock);
}

static void *posix_signal_worker_main(void *opaque) {
    posix_thread_context_t context = *(posix_thread_context_t *)opaque;
    free(opaque);
    for (;;) {
        pthread_mutex_lock(&g_signal_lock);
        posix_signal_state_t *state =
            &g_signal_states[context.native_signum];
        while (context.runtime_generation == g_signal_runtime_generation &&
               !state->callback_head) {
            if (pthread_cond_wait(g_signal_changed, &g_signal_lock) != 0)
                break;
        }
        if (context.runtime_generation != g_signal_runtime_generation) {
            pthread_mutex_unlock(&g_signal_lock);
            return NULL;
        }
        posix_callback_job_t *job = state->callback_head;
        if (!job) {
            pthread_mutex_unlock(&g_signal_lock);
            continue;
        }
        state->callback_head = job->next;
        if (!state->callback_head)
            state->callback_tail = NULL;
        if (state->callback_queue_count > 0)
            --state->callback_queue_count;
        if (job->generation != state->generation ||
            state->disposition != POSIX_SIGNAL_NOTIFY ||
            job->handler != state->handler ||
            job->callback_signum != state->callback_signum) {
            pthread_mutex_unlock(&g_signal_lock);
            free(job);
            continue;
        }
        neverc_signal_handler_t handler = job->handler;
        int callback_signum = job->callback_signum;
        posix_signal_generation_t callback_generation = job->generation;
        state->callbacks_inflight = 1;
        state->inflight_generation = callback_generation;
        pthread_mutex_unlock(&g_signal_lock);
        free(job);

        int previous_callback_native = g_signal_callback_native;
        g_signal_callback_native = context.native_signum;
        handler(callback_signum);
        g_signal_callback_native = previous_callback_native;
        posix_finish_callback(context.native_signum,
                              context.runtime_generation,
                              callback_generation);
    }
}

static int posix_start_signal_worker_locked(int native_signum) {
    posix_signal_state_t *state = &g_signal_states[native_signum];
    if (state->worker_started)
        return 0;
    posix_thread_context_t *context =
        (posix_thread_context_t *)malloc(sizeof(*context));
    if (!context)
        return -1;
    context->native_signum = native_signum;
    context->runtime_generation = g_signal_runtime_generation;
    if (pthread_create(&state->worker, NULL, posix_signal_worker_main,
                       context) != 0) {
        free(context);
        return -1;
    }
    state->worker_started = 1;
    (void)pthread_detach(state->worker);
    return 0;
}

static void posix_enqueue_callback_locked(
    int native_signum, posix_signal_generation_t generation) {
    posix_signal_state_t *state = &g_signal_states[native_signum];
    if (!state->worker_started || !state->handler ||
        state->callback_queue_count >= NEVERC_SIGNAL_CALLBACK_QUEUE_MAX)
        return;
    posix_callback_job_t *job =
        (posix_callback_job_t *)malloc(sizeof(*job));
    if (!job)
        return;
    job->handler = state->handler;
    job->callback_signum = state->callback_signum;
    job->generation = generation;
    job->next = NULL;
    if (state->callback_tail)
        state->callback_tail->next = job;
    else
        state->callback_head = job;
    state->callback_tail = job;
    ++state->callback_queue_count;
}

static void posix_accept_signal_event_locked(
    int native_signum, posix_signal_generation_t generation) {
    posix_signal_state_t *state = &g_signal_states[native_signum];
    if (generation == 0 || generation != state->generation ||
        generation != atomic_load_explicit(
                          &g_signal_active_generations[native_signum],
                          memory_order_acquire) ||
        !posix_transport_needed(state))
        return;
    if (state->pending_count < UINT_MAX)
        ++state->pending_count;
    if (state->disposition == POSIX_SIGNAL_NOTIFY)
        posix_enqueue_callback_locked(native_signum, generation);
    if (g_signal_changed)
        pthread_cond_broadcast(g_signal_changed);
}

static void posix_dispatch_signal_fd(int native_signum, int fd) {
    pthread_mutex_lock(&g_signal_lock);
    posix_signal_generation_t overflow_before = atomic_exchange_explicit(
        &g_signal_overflow_generations[native_signum], 0,
        memory_order_acq_rel);
    if (overflow_before != 0)
        posix_accept_signal_event_locked(native_signum, overflow_before);
    for (unsigned int processed = 0;
         processed < NEVERC_SIGNAL_DISPATCH_BATCH; ++processed) {
        posix_signal_event_t event;
        ssize_t count = read(fd, &event, sizeof(event));
        if (count == (ssize_t)sizeof(event)) {
            posix_accept_signal_event_locked(native_signum,
                                             event.generation);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        break;
    }
    posix_signal_generation_t overflow_after = atomic_exchange_explicit(
        &g_signal_overflow_generations[native_signum], 0,
        memory_order_acq_rel);
    if (overflow_after != 0)
        posix_accept_signal_event_locked(native_signum, overflow_after);
    if (g_signal_changed)
        pthread_cond_broadcast(g_signal_changed);
    pthread_mutex_unlock(&g_signal_lock);
}

static void *posix_signal_dispatch_main(void *unused) {
    unsigned long long runtime_generation =
        ((posix_thread_context_t *)unused)->runtime_generation;
    free(unused);
    for (;;) {
        struct pollfd poll_fds[NEVERC_SIGNAL_NSIG + 1];
        int native_signums[NEVERC_SIGNAL_NSIG + 1];
        nfds_t count = 1;

        poll_fds[0].fd = g_signal_control_read;
        poll_fds[0].events = POLLIN;
        poll_fds[0].revents = 0;
        native_signums[0] = 0;

        pthread_mutex_lock(&g_signal_lock);
        if (runtime_generation != g_signal_runtime_generation) {
            pthread_mutex_unlock(&g_signal_lock);
            return NULL;
        }
        for (int signum = 1; signum < NEVERC_SIGNAL_NSIG; ++signum) {
            int fd = g_signal_states[signum].read_fd;
            if (fd < 0)
                continue;
            poll_fds[count].fd = fd;
            poll_fds[count].events = POLLIN;
            poll_fds[count].revents = 0;
            native_signums[count] = signum;
            ++count;
        }
        pthread_mutex_unlock(&g_signal_lock);

        int rc;
        do {
            rc = poll(poll_fds, count, -1);
        } while (rc < 0 && errno == EINTR);
        if (rc < 0)
            continue;

        if (poll_fds[0].revents & POLLIN)
            posix_drain_fd(g_signal_control_read);
        for (nfds_t i = 1; i < count; ++i) {
            if (poll_fds[i].revents & POLLIN)
                posix_dispatch_signal_fd(native_signums[i], poll_fds[i].fd);
        }
    }
    return NULL;
}

static void posix_signal_atfork_prepare(void) {
    pthread_mutex_lock(&g_signal_lock);
}

static void posix_signal_atfork_parent(void) {
    pthread_mutex_unlock(&g_signal_lock);
}

static void posix_signal_atfork_child(void) {
    /* The dispatcher and all persistent workers vanish unless one of them was
     * the fork caller. Runtime generation invalidation makes a surviving
     * callback worker exit after its current callback, so the child always
     * rebuilds one coherent dispatcher/worker generation. */
    ++g_signal_runtime_generation;
    if (g_signal_runtime_generation == 0)
        ++g_signal_runtime_generation;
    for (int i = 0; i < NEVERC_SIGNAL_NSIG; ++i) {
        posix_signal_state_t *state = &g_signal_states[i];
        if (i > 0 && state->disposition == POSIX_SIGNAL_NOTIFY) {
            (void)sigaction(i, &g_signal_default_action, NULL);
        } else if (i > 0 && state->waiter_count > 0) {
            if (state->disposition == POSIX_SIGNAL_IGNORE)
                (void)sigaction(i, &g_signal_ignore_action, NULL);
            else if (state->saved_action_valid)
                (void)sigaction(i, &state->saved_action, NULL);
            else
                (void)sigaction(i, &g_signal_default_action, NULL);
        }
        if (state->read_fd >= 0)
            close(state->read_fd);
        int write_fd = atomic_load_explicit(&g_signal_write_fds[i],
                                            memory_order_relaxed);
        if (write_fd >= 0)
            close(write_fd);
        atomic_store_explicit(&g_signal_write_fds[i], -1,
                              memory_order_relaxed);
        atomic_store_explicit(&g_signal_active_generations[i], 0,
                              memory_order_relaxed);
        atomic_store_explicit(&g_signal_overflow_generations[i], 0,
                              memory_order_relaxed);
        state->read_fd = -1;
        state->handler = NULL;
        state->callback_signum = 0;
        state->waiter_count = 0;
        state->callbacks_inflight = 0;
        state->pending_count = 0;
        state->generation = 0;
        state->inflight_generation = 0;
        state->saved_action_valid = 0;
        state->worker_started = 0;
        state->callback_queue_count = 0;
        /* Queued allocations are deliberately abandoned in the child: free()
         * is not async-signal-safe after a multithreaded fork. */
        state->callback_head = NULL;
        state->callback_tail = NULL;
        if (state->disposition == POSIX_SIGNAL_NOTIFY)
            state->disposition = POSIX_SIGNAL_DEFAULT;
    }
    if (g_signal_control_read >= 0)
        close(g_signal_control_read);
    if (g_signal_control_write >= 0)
        close(g_signal_control_write);
    g_signal_control_read = -1;
    g_signal_control_write = -1;
    g_signal_runtime_ready = 0;
    /* The inherited condition variable may retain wait queues belonging to
     * vanished threads. Leave that child-private allocation unreachable and
     * build a fresh generation on the next API call. */
    g_signal_changed = NULL;
    pthread_mutex_unlock(&g_signal_lock);
}

static int posix_signal_runtime_start_locked(void) {
    int control[2] = {-1, -1};
    if (!g_signal_state_initialized) {
        for (int i = 0; i < NEVERC_SIGNAL_NSIG; ++i) {
            g_signal_states[i].read_fd = -1;
            atomic_init(&g_signal_write_fds[i], -1);
            atomic_init(&g_signal_active_generations[i], 0);
            atomic_init(&g_signal_overflow_generations[i], 0);
        }
        memset(&g_signal_transport_action, 0,
               sizeof(g_signal_transport_action));
        sigemptyset(&g_signal_transport_action.sa_mask);
        g_signal_transport_action.sa_handler = posix_signal_handler;
        g_signal_transport_action.sa_flags = SA_RESTART;
        memset(&g_signal_default_action, 0, sizeof(g_signal_default_action));
        sigemptyset(&g_signal_default_action.sa_mask);
        g_signal_default_action.sa_handler = SIG_DFL;
        memset(&g_signal_ignore_action, 0, sizeof(g_signal_ignore_action));
        sigemptyset(&g_signal_ignore_action.sa_mask);
        g_signal_ignore_action.sa_handler = SIG_IGN;
        g_signal_state_initialized = 1;
    }
    if (!g_signal_atfork_registered) {
        if (pthread_atfork(posix_signal_atfork_prepare,
                           posix_signal_atfork_parent,
                           posix_signal_atfork_child) != 0)
            return -1;
        g_signal_atfork_registered = 1;
    }
    if (!g_signal_changed) {
        pthread_cond_t *changed =
            (pthread_cond_t *)malloc(sizeof(*changed));
        if (!changed)
            return -1;
        if (pthread_cond_init(changed, NULL) != 0) {
            free(changed);
            return -1;
        }
        g_signal_changed = changed;
    }
    if (g_signal_control_read < 0) {
        if (posix_make_pipe(control) != 0)
            return -1;
        g_signal_control_read = control[0];
        g_signal_control_write = control[1];
    }
    if (g_signal_runtime_ready)
        return 0;
    posix_thread_context_t *context =
        (posix_thread_context_t *)malloc(sizeof(*context));
    if (!context)
        return -1;
    context->native_signum = 0;
    context->runtime_generation = g_signal_runtime_generation;
    if (pthread_create(&g_signal_dispatcher, NULL,
                       posix_signal_dispatch_main, context) != 0) {
        free(context);
        return -1;
    }
    (void)pthread_detach(g_signal_dispatcher);
    g_signal_runtime_ready = 1;
    return 0;
}

static int posix_signal_runtime_ensure(void) {
    int result;
    pthread_mutex_lock(&g_signal_lock);
    result = posix_signal_runtime_start_locked();
    pthread_mutex_unlock(&g_signal_lock);
    return result;
}

static int posix_signal_is_catchable(int native_signum) {
    sigset_t valid;
    if (!posix_signal_in_range(native_signum) ||
        native_signum == SIGKILL || native_signum == SIGSTOP)
        return 0;
    sigemptyset(&valid);
    return sigaddset(&valid, native_signum) == 0;
}

static void posix_wait_for_callbacks_locked(
    int native_signum,
    posix_signal_generation_t callback_generation) {
    /* Never wait from a callback worker: callbacks on two signals are allowed
     * to stop each other, and cross-waiting would deadlock both workers. */
    if (g_signal_callback_native != 0)
        return;
    while (g_signal_states[native_signum].callbacks_inflight > 0 &&
           g_signal_states[native_signum].inflight_generation ==
               callback_generation)
        pthread_cond_wait(g_signal_changed, &g_signal_lock);
}

void neverc_signal_notify(int signum, neverc_signal_handler_t handler) {
    int native_signum = posix_signal_to_native(signum);
    if (!handler) {
        neverc_signal_stop(signum);
        return;
    }
    if (!posix_signal_is_catchable(native_signum) ||
        posix_signal_runtime_ensure() != 0)
        return;

    pthread_mutex_lock(&g_signal_lock);
    posix_signal_state_t *state = &g_signal_states[native_signum];
    if (posix_ensure_signal_pipe_locked(native_signum) != 0 ||
        posix_start_signal_worker_locked(native_signum) != 0) {
        pthread_mutex_unlock(&g_signal_lock);
        return;
    }
    neverc_signal_handler_t previous_handler = state->handler;
    int previous_callback_signum = state->callback_signum;
    enum posix_signal_disposition previous_disposition = state->disposition;
    int previous_saved_action_valid = state->saved_action_valid;
    struct sigaction previous_saved_action = state->saved_action;
    state->handler = handler;
    state->callback_signum = signum;
    state->disposition = POSIX_SIGNAL_NOTIFY;
    state->saved_action_valid = 0;
    if (posix_reconfigure_signal_locked(native_signum) != 0) {
        state->handler = previous_handler;
        state->callback_signum = previous_callback_signum;
        state->disposition = previous_disposition;
        state->saved_action_valid = previous_saved_action_valid;
        state->saved_action = previous_saved_action;
        (void)posix_reconfigure_signal_locked(native_signum);
    }
    pthread_mutex_unlock(&g_signal_lock);
}

void neverc_signal_stop(int signum) {
    int native_signum = posix_signal_to_native(signum);
    if (!posix_signal_is_catchable(native_signum) ||
        posix_signal_runtime_ensure() != 0)
        return;

    pthread_mutex_lock(&g_signal_lock);
    posix_signal_state_t *state = &g_signal_states[native_signum];
    posix_signal_generation_t callback_generation =
        state->inflight_generation;
    state->handler = NULL;
    state->callback_signum = 0;
    state->disposition = POSIX_SIGNAL_DEFAULT;
    state->saved_action_valid = 0;
    (void)posix_reconfigure_signal_locked(native_signum);
    posix_wait_for_callbacks_locked(native_signum, callback_generation);
    pthread_mutex_unlock(&g_signal_lock);
}

void neverc_signal_reset(int signum) {
    neverc_signal_stop(signum);
}

void neverc_signal_ignore(int signum) {
    int native_signum = posix_signal_to_native(signum);
    if (!posix_signal_is_catchable(native_signum) ||
        posix_signal_runtime_ensure() != 0)
        return;

    pthread_mutex_lock(&g_signal_lock);
    posix_signal_state_t *state = &g_signal_states[native_signum];
    posix_signal_generation_t callback_generation =
        state->inflight_generation;
    state->handler = NULL;
    state->callback_signum = 0;
    state->disposition = POSIX_SIGNAL_IGNORE;
    state->saved_action_valid = 0;
    (void)posix_reconfigure_signal_locked(native_signum);
    posix_wait_for_callbacks_locked(native_signum, callback_generation);
    pthread_mutex_unlock(&g_signal_lock);
}

int neverc_signal_wait(const int *sigs, int nsigs) {
    int native_signums[NEVERC_SIGNAL_NSIG];
    int unique_signums[NEVERC_SIGNAL_NSIG];
    int unique_count = 0;
    int result = -1;

    if (!sigs || nsigs <= 0 || nsigs > NEVERC_SIGNAL_NSIG)
        return -1;
    if (g_signal_callback_native != 0)
        return -1;
    for (int i = 0; i < nsigs; ++i) {
        int native_signum = posix_signal_to_native(sigs[i]);
        if (!posix_signal_is_catchable(native_signum))
            return -1;
        native_signums[i] = native_signum;
        int duplicate = 0;
        for (int j = 0; j < unique_count; ++j) {
            if (unique_signums[j] == native_signum) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate)
            unique_signums[unique_count++] = native_signum;
    }
    if (posix_signal_runtime_ensure() != 0)
        return -1;

    pthread_mutex_lock(&g_signal_lock);
    int installed = 0;
    for (; installed < unique_count; ++installed) {
        int native_signum = unique_signums[installed];
        posix_signal_state_t *state = &g_signal_states[native_signum];
        int transport_was_needed = posix_transport_needed(state);
        if (posix_ensure_signal_pipe_locked(native_signum) != 0) {
            break;
        }
        if (state->waiter_count == UINT_MAX)
            break;
        if (state->waiter_count == 0 &&
            state->disposition == POSIX_SIGNAL_DEFAULT &&
            !state->saved_action_valid) {
            if (sigaction(native_signum, NULL, &state->saved_action) != 0)
                break;
            state->saved_action_valid = 1;
        }
        ++state->waiter_count;
        if (!transport_was_needed &&
            posix_reconfigure_signal_locked(native_signum) != 0) {
            --state->waiter_count;
            (void)posix_reconfigure_signal_locked(native_signum);
            break;
        }
    }
    if (installed != unique_count)
        goto cleanup;

    for (;;) {
        for (int i = 0; i < nsigs; ++i) {
            posix_signal_state_t *state =
                &g_signal_states[native_signums[i]];
            if (state->pending_count > 0) {
                --state->pending_count;
                result = sigs[i];
                goto cleanup;
            }
        }
        if (pthread_cond_wait(g_signal_changed, &g_signal_lock) != 0)
            goto cleanup;
    }

cleanup:
    for (int i = 0; i < installed; ++i) {
        posix_signal_state_t *state = &g_signal_states[unique_signums[i]];
        if (state->waiter_count > 0)
            --state->waiter_count;
        if (!posix_transport_needed(state))
            (void)posix_reconfigure_signal_locked(unique_signums[i]);
    }
    pthread_mutex_unlock(&g_signal_lock);
    return result;
}

#endif /* POSIX */
