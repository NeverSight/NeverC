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

static BOOL WINAPI win_ctrl_handler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
            if (g_win_handlers[NEVERC_SIGINT]) { g_win_handlers[NEVERC_SIGINT](NEVERC_SIGINT); return TRUE; }
            break;
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            if (g_win_handlers[NEVERC_SIGTERM]) { g_win_handlers[NEVERC_SIGTERM](NEVERC_SIGTERM); return TRUE; }
            break;
    }
    return FALSE;
}

void neverc_signal_notify(int signum, neverc_signal_handler_t handler) {
    if (signum < 0 || signum >= 32) return;
    g_win_handlers[signum] = handler;
    if (signum == NEVERC_SIGINT || signum == NEVERC_SIGTERM)
        SetConsoleCtrlHandler(win_ctrl_handler, TRUE);
    signal(signum, handler);
}

void neverc_signal_stop(int signum) {
    if (signum < 0 || signum >= 32) return;
    g_win_handlers[signum] = NULL;
    signal(signum, SIG_DFL);
}

void neverc_signal_reset(int signum) {
    neverc_signal_stop(signum);
}

void neverc_signal_ignore(int signum) {
    if (signum < 0 || signum >= 32) return;
    g_win_handlers[signum] = NULL;
    signal(signum, SIG_IGN);
}

int neverc_signal_wait(const int *sigs, int nsigs) {
    if (!sigs || nsigs <= 0) return -1;
    (void)sigs; (void)nsigs;
    SleepEx(INFINITE, TRUE);
    return 0;
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
    g_handlers[signum] = handler;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = posix_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(signum, &sa, NULL);
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
    if (!sigs || nsigs <= 0) return -1;
    sigset_t set;
    sigemptyset(&set);
    for (int i = 0; i < nsigs; i++)
        sigaddset(&set, sigs[i]);

    sigset_t old;
    sigprocmask(SIG_BLOCK, &set, &old);

    int sig = 0;
    sigwait(&set, &sig);

    sigprocmask(SIG_SETMASK, &old, NULL);
    return sig;
}

#endif /* POSIX */
