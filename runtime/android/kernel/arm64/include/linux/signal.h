/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SIGNAL_H
#define _NEVERC_KRT_LINUX_SIGNAL_H

#include <linux/types.h>

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20

struct task_struct;

int send_sig(int sig, struct task_struct *t, int priv);
int send_sig_info(int sig, void *info, struct task_struct *t);
int force_sig(int sig);

#endif /* _NEVERC_KRT_LINUX_SIGNAL_H */
