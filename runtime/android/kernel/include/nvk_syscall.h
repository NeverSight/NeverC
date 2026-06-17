/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_SYSCALL_H
#define NEVERC_KRT_SYSCALL_H

#include <linux/types.h>
#include <asm/ptrace.h>

typedef long (*neverc_krt_syscall_fn_t)(const struct pt_regs *regs);

int neverc_krt_syscall_init(void);

neverc_krt_syscall_fn_t *neverc_krt_syscall_table(void);

int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
			       neverc_krt_syscall_fn_t *orig);


int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);


neverc_krt_syscall_fn_t neverc_krt_syscall_get(int nr);

#define __NR_io_setup         0
#define __NR_io_destroy       1
#define __NR_io_submit        2
#define __NR_io_cancel        3
#define __NR_io_getevents     4
#define __NR_setxattr         5
#define __NR_lsetxattr        6
#define __NR_fsetxattr        7
#define __NR_getxattr         8
#define __NR_lgetxattr        9
#define __NR_fgetxattr       10
#define __NR_listxattr       11
#define __NR_llistxattr      12
#define __NR_flistxattr      13
#define __NR_removexattr     14
#define __NR_lremovexattr    15
#define __NR_fremovexattr    16
#define __NR_getcwd          17
#define __NR_eventfd2        19
#define __NR_epoll_create1   20
#define __NR_epoll_ctl       21
#define __NR_epoll_pwait     22
#define __NR_dup             23
#define __NR_dup3            24
#define __NR_fcntl           25
#define __NR_inotify_init1   26
#define __NR_inotify_add_watch 27
#define __NR_inotify_rm_watch 28
#define __NR_ioctl_nr        29
#define __NR_flock           32
#define __NR_mknodat         33
#define __NR_mkdirat         34
#define __NR_unlinkat        35
#define __NR_symlinkat       36
#define __NR_linkat          37
#define __NR_renameat        38
#define __NR_umount2         39
#define __NR_mount           40
#define __NR_statfs          43
#define __NR_fstatfs         44
#define __NR_ftruncate       46
#define __NR_fallocate       47
#define __NR_faccessat       48
#define __NR_chdir           49
#define __NR_fchmod          52
#define __NR_fchmodat        53
#define __NR_fchownat        54
#define __NR_fchown          55
#define __NR_openat          56
#define __NR_close           57
#define __NR_pipe2           59
#define __NR_lseek           62
#define __NR_read            63
#define __NR_write           64
#define __NR_readv           65
#define __NR_writev          66
#define __NR_pread64         67
#define __NR_pwrite64        68
#define __NR_sendfile        71
#define __NR_ppoll           73
#define __NR_signalfd4       74
#define __NR_readlinkat      78
#define __NR_newfstatat      79
#define __NR_fstat           80
#define __NR_exit            93
#define __NR_exit_group      94
#define __NR_set_tid_address 96
#define __NR_futex           98
#define __NR_nanosleep      101
#define __NR_getitimer      102
#define __NR_setitimer      103
#define __NR_clock_gettime  113
#define __NR_clock_getres   114
#define __NR_clock_nanosleep 115
#define __NR_kill           129
#define __NR_tkill          130
#define __NR_tgkill         131
#define __NR_rt_sigaction   134
#define __NR_rt_sigprocmask 135
#define __NR_rt_sigreturn   139
#define __NR_setpriority    140
#define __NR_getpriority    141
#define __NR_reboot         142
#define __NR_setregid       143
#define __NR_setgid         144
#define __NR_setreuid       145
#define __NR_setuid         146
#define __NR_setresuid      147
#define __NR_getresuid      148
#define __NR_setresgid      149
#define __NR_getresgid      150
#define __NR_setfsuid       151
#define __NR_setfsgid       152
#define __NR_times          153
#define __NR_setpgid        154
#define __NR_getpgid        155
#define __NR_getsid         156
#define __NR_setsid         157
#define __NR_getgroups      158
#define __NR_setgroups      159
#define __NR_uname          160
#define __NR_getrlimit      163
#define __NR_setrlimit      164
#define __NR_getrusage      165
#define __NR_umask          166
#define __NR_prctl          167
#define __NR_getpid         172
#define __NR_getppid        173
#define __NR_getuid         174
#define __NR_geteuid        175
#define __NR_getgid         176
#define __NR_getegid        177
#define __NR_gettid         178
#define __NR_sysinfo        179
#define __NR_socket         198
#define __NR_bind           200
#define __NR_listen         201
#define __NR_accept         202
#define __NR_connect        203
#define __NR_sendto         206
#define __NR_recvfrom       207
#define __NR_setsockopt     208
#define __NR_getsockopt     209
#define __NR_brk            214
#define __NR_munmap         215
#define __NR_mremap         216
#define __NR_clone          220
#define __NR_execve         221
#define __NR_mmap           222
#define __NR_mprotect       226
#define __NR_madvise        233
#define __NR_wait4          260
#define __NR_prlimit64      261
#define __NR_process_vm_readv  270
#define __NR_process_vm_writev 271
#define __NR_getrandom      278
#define __NR_memfd_create   279
#define __NR_statx          291
#define __NR_io_uring_setup 425
#define __NR_io_uring_enter 426
#define __NR_openat2        437
#define __NR_faccessat2     439

#endif /* NEVERC_KRT_SYSCALL_H */
