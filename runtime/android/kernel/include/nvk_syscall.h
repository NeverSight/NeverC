/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_SYSCALL_H
#define NEVERC_KRT_SYSCALL_H

#include <linux/types.h>
#include <asm/syscall.h>

typedef long (*neverc_krt_syscall_fn_t)(const struct pt_regs *regs);

int neverc_krt_syscall_init(void);

neverc_krt_syscall_fn_t *neverc_krt_syscall_table(void);

int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
			       neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
neverc_krt_syscall_fn_t neverc_krt_syscall_get(int nr);

/*
 * Syscall numbers not covered by asm/syscall.h.
 * The arm64 syscall table is stable across GKI 5.10–6.18.
 */
#ifndef __NR_lsetxattr
#define __NR_lsetxattr        6
#endif
#ifndef __NR_fsetxattr
#define __NR_fsetxattr        7
#endif
#ifndef __NR_lgetxattr
#define __NR_lgetxattr        9
#endif
#ifndef __NR_fgetxattr
#define __NR_fgetxattr       10
#endif
#ifndef __NR_llistxattr
#define __NR_llistxattr      12
#endif
#ifndef __NR_flistxattr
#define __NR_flistxattr      13
#endif
#ifndef __NR_lremovexattr
#define __NR_lremovexattr    15
#endif
#ifndef __NR_fremovexattr
#define __NR_fremovexattr    16
#endif
#ifndef __NR_fallocate
#define __NR_fallocate       47
#endif
#ifndef __NR_accept
#define __NR_accept          202
#endif
#ifndef __NR_getrlimit
#define __NR_getrlimit       163
#endif
#ifndef __NR_setrlimit
#define __NR_setrlimit       164
#endif
#ifndef __NR_getrusage
#define __NR_getrusage       165
#endif
#ifndef __NR_umask
#define __NR_umask           166
#endif
#ifndef __NR_wait4
#define __NR_wait4           260
#endif
#ifndef __NR_prlimit64
#define __NR_prlimit64       261
#endif
#ifndef __NR_getrandom
#define __NR_getrandom       278
#endif
#ifndef __NR_statx
#define __NR_statx           291
#endif
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup  425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter  426
#endif
#ifndef __NR_openat2
#define __NR_openat2         437
#endif

#endif /* NEVERC_KRT_SYSCALL_H */
