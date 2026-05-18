/*===---- unistd.h - NeverC shellcode-oriented POSIX shim -----------------===*\
|*
|* In normal builds this header forwards to the system unistd.h.
|* In shellcode mode (`__NEVERC_SHELLCODE__`) we expose a compact subset
|* that matches SyscallStubPass coverage so users can keep writing
|* `#include <unistd.h>` style code without hand-written externs.
|*
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_UNISTD_SHIM_H_
#define _NEVERC_UNISTD_SHIM_H_

#if defined(__NEVERC_SHELLCODE_KERNEL__)
#error                                                                         \
    "<unistd.h> is a user-mode header and cannot be used in -mshellcode-context=kernel builds. Ring-0 payloads have no POSIX syscall stub surface; include <neverc/kernel.h> and request the helper through the KernelImportPass resolver shim instead."
#endif

#if !defined(__NEVERC_SHELLCODE__)
#include_next <unistd.h>
#else

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int pid_t;
typedef long ssize_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long off_t;
typedef int mode_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Standard file descriptors. */
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* lseek whence. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* access(2) mode bits. */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

/* I/O primitives backed by SyscallStubPass / WinPEBImportPass. */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);

/* File-system operations. */
int unlink(const char *pathname);
int access(const char *pathname, int mode);
int chdir(const char *path);
int fchdir(int fd);
int rmdir(const char *path);
int mkdir(const char *path, mode_t mode);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
int rename(const char *oldpath, const char *newpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
int chown(const char *path, uid_t owner, gid_t group);
char *getcwd(char *buf, size_t size);

/* Process / identity. */
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t uid);
int setegid(gid_t gid);
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);

void _exit(int status);
unsigned int sleep(unsigned int seconds);

#ifdef __cplusplus
}
#endif

#endif /* __NEVERC_SHELLCODE__ */
#endif /* _NEVERC_UNISTD_SHIM_H_ */
