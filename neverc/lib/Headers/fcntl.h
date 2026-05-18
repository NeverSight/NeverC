/*===---- fcntl.h - NeverC shellcode-oriented fcntl shim ------------------===*\
|*
|* Plain `fcntl.h` shim.  In normal compilation we forward to the system
|* fcntl.h.  In shellcode mode (`__NEVERC_SHELLCODE__`) we expose the
|* minimum surface SyscallStubPass can lower natively so users can keep
|* writing ordinary `#include <fcntl.h>` code.
|*
|* On aarch64-linux the syscall table ships no `open`; the pipeline's
|* POSIX-compat layer transparently redirects to `openat(AT_FDCWD, ...)`,
|* so the externs declared here still work end-to-end.
|*
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_FCNTL_SHIM_H_
#define _NEVERC_FCNTL_SHIM_H_

#if defined(__NEVERC_SHELLCODE_KERNEL__)
#error                                                                         \
    "<fcntl.h> is a user-mode header. In -mshellcode-context=kernel mode include <neverc/kernel.h> and route file-open semantics through a kernel-side helper (filp_open on Linux, ZwCreateFile on Windows, vnode_open on XNU)."
#endif

#if !defined(__NEVERC_SHELLCODE__)
#include_next <fcntl.h>
#else

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open flags.  Values are kernel-ABI-specific; Darwin and Linux use
 * different bit assignments, and the shellcode pipeline passes them
 * straight through to the kernel via svc/syscall. */
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_ACCMODE 0x0003
#if defined(__APPLE__)
#define O_NONBLOCK 0x0004
#define O_APPEND 0x0008
#define O_CREAT 0x0200
#define O_TRUNC 0x0400
#define O_EXCL 0x0800
#define O_NOCTTY 0x20000
#define O_CLOEXEC 0x1000000
#else /* Linux / Android */
#define O_CREAT 0x0040
#define O_EXCL 0x0080
#define O_NOCTTY 0x0100
#define O_TRUNC 0x0200
#define O_APPEND 0x0400
#define O_NONBLOCK 0x0800
#define O_CLOEXEC 0x80000
#endif

/* fcntl(2) commands used by the whitelisted syscall.  Consumers that
 * need other commands can add them without touching the compiler. */
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1

/* AT_* directory-fd constants for `*at` syscalls.  SyscallStubPass's
 * POSIX compat layer injects AT_FDCWD automatically for classic callers,
 * but users that invoke `openat`/`fstatat`/... directly still want the
 * canonical constants.  Values differ between Darwin and Linux. */
#if defined(__APPLE__)
#define AT_FDCWD (-2)
#define AT_EACCESS 0x10
#define AT_SYMLINK_NOFOLLOW 0x20
#define AT_REMOVEDIR 0x80
#define AT_SYMLINK_FOLLOW 0x40
#else /* Linux / Android */
#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_SYMLINK_FOLLOW 0x400
#define AT_EACCESS 0x200
#endif

typedef int mode_t;

int open(const char *pathname, int flags, ...);
int openat(int dirfd, const char *pathname, int flags, ...);
int creat(const char *pathname, mode_t mode);
int fcntl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif /* __NEVERC_SHELLCODE__ */
#endif /* _NEVERC_FCNTL_SHIM_H_ */
