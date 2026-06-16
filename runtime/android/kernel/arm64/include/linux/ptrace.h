/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_PTRACE_H
#define _NEVERC_KRT_LINUX_PTRACE_H

#include <linux/types.h>
#include <asm/ptrace.h>

#define PTRACE_TRACEME   0
#define PTRACE_PEEKTEXT  1
#define PTRACE_PEEKDATA  2
#define PTRACE_POKETEXT  4
#define PTRACE_POKEDATA  5
#define PTRACE_CONT      7
#define PTRACE_KILL      8
#define PTRACE_ATTACH    16
#define PTRACE_DETACH    17

#endif
