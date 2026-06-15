/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_ASM_SYSCALL_H
#define _NVK_ASM_SYSCALL_H

#include <linux/types.h>
#include <asm/ptrace.h>

/* Common arm64 syscall numbers. */
#define __NR_io_setup           0
#define __NR_io_destroy         1
#define __NR_io_submit          2
#define __NR_io_cancel          3
#define __NR_io_getevents       4
#define __NR_setxattr           5
#define __NR_getxattr           8
#define __NR_listxattr          11
#define __NR_removexattr        14
#define __NR_getcwd             17
#define __NR_eventfd2           19
#define __NR_epoll_create1      20
#define __NR_epoll_ctl          21
#define __NR_epoll_pwait        22
#define __NR_dup                23
#define __NR_dup3               24
#define __NR_fcntl              25
#define __NR_inotify_init1      26
#define __NR_inotify_add_watch  27
#define __NR_inotify_rm_watch   28
#define __NR_ioctl_nr           29
#define __NR_flock              32
#define __NR_mknodat            33
#define __NR_mkdirat            34
#define __NR_unlinkat           35
#define __NR_symlinkat          36
#define __NR_linkat             37
#define __NR_renameat           38
#define __NR_umount2            39
#define __NR_mount              40
#define __NR_statfs             43
#define __NR_fstatfs            44
#define __NR_ftruncate          46
#define __NR_faccessat          48
#define __NR_chdir              49
#define __NR_fchmod             52
#define __NR_fchmodat           53
#define __NR_fchownat           54
#define __NR_fchown             55
#define __NR_openat             56
#define __NR_close              57
#define __NR_pipe2              59
#define __NR_lseek              62
#define __NR_read               63
#define __NR_write              64
#define __NR_readv              65
#define __NR_writev             66
#define __NR_pread64            67
#define __NR_pwrite64           68
#define __NR_sendfile           71
#define __NR_ppoll              73
#define __NR_signalfd4          74
#define __NR_readlinkat         78
#define __NR_newfstatat         79
#define __NR_fstat              80
#define __NR_timerfd_create     85
#define __NR_timerfd_settime    86
#define __NR_timerfd_gettime    87
#define __NR_utimensat          88
#define __NR_exit               93
#define __NR_exit_group         94
#define __NR_set_tid_address    96
#define __NR_futex              98
#define __NR_nanosleep          101
#define __NR_getitimer          102
#define __NR_setitimer          103
#define __NR_clock_gettime      113
#define __NR_clock_getres       114
#define __NR_clock_nanosleep    115
#define __NR_syslog             116
#define __NR_ptrace             117
#define __NR_sched_setparam     118
#define __NR_sched_getparam     121
#define __NR_sched_setaffinity  122
#define __NR_sched_getaffinity  123
#define __NR_sched_yield        124
#define __NR_kill               129
#define __NR_tkill              130
#define __NR_tgkill             131
#define __NR_sigaltstack        132
#define __NR_rt_sigsuspend      133
#define __NR_rt_sigaction       134
#define __NR_rt_sigprocmask     135
#define __NR_rt_sigreturn       139
#define __NR_setpriority        140
#define __NR_getpriority        141
#define __NR_reboot             142
#define __NR_setregid           143
#define __NR_setgid             144
#define __NR_setreuid           145
#define __NR_setuid             146
#define __NR_setresuid          147
#define __NR_getresuid          148
#define __NR_setresgid          149
#define __NR_getresgid          150
#define __NR_setfsuid           151
#define __NR_setfsgid           152
#define __NR_times              153
#define __NR_setpgid            154
#define __NR_getpgid            155
#define __NR_getsid             156
#define __NR_setsid             157
#define __NR_getgroups          158
#define __NR_setgroups          159
#define __NR_uname              160
#define __NR_getpid             172
#define __NR_getppid            173
#define __NR_getuid             174
#define __NR_geteuid            175
#define __NR_getgid             176
#define __NR_getegid            177
#define __NR_gettid             178
#define __NR_sysinfo            179
#define __NR_socket             198
#define __NR_bind               200
#define __NR_listen             201
#define __NR_accept4            242
#define __NR_connect            203
#define __NR_sendto             206
#define __NR_recvfrom           207
#define __NR_setsockopt         208
#define __NR_getsockopt         209
#define __NR_sendmsg            211
#define __NR_recvmsg            212
#define __NR_brk                214
#define __NR_munmap             215
#define __NR_mremap             216
#define __NR_clone              220
#define __NR_execve             221
#define __NR_mmap               222
#define __NR_mprotect           226
#define __NR_msync              227
#define __NR_madvise            233
#define __NR_mlock              228
#define __NR_munlock            229
#define __NR_mlockall           230
#define __NR_munlockall         231
#define __NR_mincore            232
#define __NR_prctl              167
#define __NR_process_vm_readv   270
#define __NR_process_vm_writev  271
#define __NR_memfd_create       279
#define __NR_faccessat2         439

/* Syscall handler type on arm64 (pt_regs-based, 5.10+). */
typedef long (*arm64_syscall_fn_t)(const struct pt_regs *regs);

/* Access a syscall argument from pt_regs. */
#define syscall_get_arg(regs, n) ((regs)->regs[(n)])

#endif /* _NVK_ASM_SYSCALL_H */
