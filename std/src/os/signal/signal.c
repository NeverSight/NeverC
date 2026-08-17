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

#include <windows.h>
#include <signal.h>

static neverc_signal_handler_t g_win_handlers[32] = {0};
static volatile LONG g_win_pending[32] = {0};
static HANDLE g_win_event = NULL;
static int g_win_ctrl_registered = 0;

static void win_ensure_event(void) {
    if (!g_win_event)
        g_win_event = CreateEventA(NULL, TRUE, FALSE, NULL);
}

static void win_mark_pending(int signum) {
    if (signum < 0 || signum >= 32) return;
    InterlockedExchange(&g_win_pending[signum], 1);
    win_ensure_event();
    if (g_win_event) SetEvent(g_win_event);
}

static BOOL WINAPI win_ctrl_handler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
            win_mark_pending(NEVERC_SIGINT);
            if (g_win_handlers[NEVERC_SIGINT]) {
                g_win_handlers[NEVERC_SIGINT](NEVERC_SIGINT);
                return TRUE;
            }
            break;
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            win_mark_pending(NEVERC_SIGTERM);
            if (g_win_handlers[NEVERC_SIGTERM]) {
                g_win_handlers[NEVERC_SIGTERM](NEVERC_SIGTERM);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

static void win_crt_handler(int signum) {
    neverc_signal_handler_t handler = NULL;
    if (signum >= 0 && signum < 32)
        handler = g_win_handlers[signum];
    win_mark_pending(signum);
    /* MSVCRT resets to SIG_DFL after delivery; re-arm the wrapper. */
    signal(signum, win_crt_handler);
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
           0;
}

void neverc_signal_notify(int signum, neverc_signal_handler_t handler) {
    if (signum < 0 || signum >= 32) return;
    if (!handler) {
        neverc_signal_stop(signum);
        return;
    }
    g_win_handlers[signum] = handler;
    win_ensure_event();
    if ((signum == NEVERC_SIGINT || signum == NEVERC_SIGTERM) &&
        !g_win_ctrl_registered) {
        if (SetConsoleCtrlHandler(win_ctrl_handler, TRUE))
            g_win_ctrl_registered = 1;
    }
    if (win_crt_supported(signum))
        signal(signum, win_crt_handler);
}

void neverc_signal_stop(int signum) {
    if (signum < 0 || signum >= 32) return;
    g_win_handlers[signum] = NULL;
    if (win_crt_supported(signum))
        signal(signum, SIG_DFL);
    if (!g_win_handlers[NEVERC_SIGINT] && !g_win_handlers[NEVERC_SIGTERM] &&
        g_win_ctrl_registered) {
        SetConsoleCtrlHandler(win_ctrl_handler, FALSE);
        g_win_ctrl_registered = 0;
    }
}

void neverc_signal_reset(int signum) {
    neverc_signal_stop(signum);
}

void neverc_signal_ignore(int signum) {
    if (signum < 0 || signum >= 32) return;
    g_win_handlers[signum] = NULL;
    if (win_crt_supported(signum))
        signal(signum, SIG_IGN);
}

int neverc_signal_wait(const int *sigs, int nsigs) {
    int i;
    if (!sigs || nsigs <= 0 || nsigs > 32) return -1;
    for (i = 0; i < nsigs; i++) {
        if (sigs[i] < 0 || sigs[i] >= 32) return -1;
        if (sigs[i] == NEVERC_SIGKILL || sigs[i] == NEVERC_SIGSTOP) return -1;
    }
    win_ensure_event();
    if (!g_win_event) return -1;
    for (;;) {
        for (i = 0; i < nsigs; i++) {
            if (InterlockedCompareExchange(&g_win_pending[sigs[i]], 0, 1) == 1)
                return sigs[i];
        }
        if (WaitForSingleObject(g_win_event, INFINITE) != WAIT_OBJECT_0)
            return -1;
        ResetEvent(g_win_event);
    }
}

#else /* POSIX */

#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static neverc_signal_handler_t g_handlers[64] = {0};

static void posix_signal_handler(int signum) {
    if (signum >= 0 && signum < 64 && g_handlers[signum])
        g_handlers[signum](signum);
}

void neverc_signal_notify(int signum, neverc_signal_handler_t handler) {
    if (signum < 0 || signum >= 64) return;
    if (!handler) {
        neverc_signal_stop(signum);
        return;
    }
    g_handlers[signum] = handler;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = posix_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(signum, &sa, NULL) != 0)
        g_handlers[signum] = NULL;
}

void neverc_signal_stop(int signum) {
    if (signum < 0 || signum >= 64) return;
    g_handlers[signum] = NULL;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(signum, &sa, NULL);
}

void neverc_signal_reset(int signum) {
    neverc_signal_stop(signum);
}

void neverc_signal_ignore(int signum) {
    if (signum < 0 || signum >= 64) return;
    g_handlers[signum] = NULL;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(signum, &sa, NULL);
}

int neverc_signal_wait(const int *sigs, int nsigs) {
    if (!sigs || nsigs <= 0 || nsigs > 64) return -1;
    sigset_t set;
    sigemptyset(&set);
    for (int i = 0; i < nsigs; i++) {
        if (sigs[i] == SIGKILL || sigs[i] == SIGSTOP) return -1;
        if (sigaddset(&set, sigs[i]) != 0) return -1;
    }

    sigset_t old;
    if (sigprocmask(SIG_BLOCK, &set, &old) != 0) return -1;

    int sig = 0;
    int rc = sigwait(&set, &sig);

    sigprocmask(SIG_SETMASK, &old, NULL);
    return rc == 0 ? sig : -1;
}

#endif /* POSIX */
