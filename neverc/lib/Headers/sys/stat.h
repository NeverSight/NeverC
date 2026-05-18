/*===---- sys/stat.h - NeverC shellcode-oriented stat shim ----------------===*\
|*
|* Forward to the system `<sys/stat.h>` in normal builds.  In shellcode
|* mode (`__NEVERC_SHELLCODE__`) expose the syscall-backed primitives the
|* pipeline can lower natively, matching SyscallStubPass coverage.
|*
|* Linux arm64 / Android arm64 do not ship the classic `stat`/`lstat`
|* syscalls; the POSIX-compat layer in SyscallTables redirects them to
|* `fstatat(AT_FDCWD, ...)`, so the declarations here work end-to-end.
|*
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_SYS_STAT_SHIM_H_
#define _NEVERC_SYS_STAT_SHIM_H_

#if defined(__NEVERC_SHELLCODE_KERNEL__)
#error                                                                         \
    "<sys/stat.h> is a user-mode header. In -mshellcode-context=kernel mode call vfs_statx / ZwQueryInformationFile / vnode_getattr directly via the KernelImportPass resolver instead."
#endif

#if !defined(__NEVERC_SHELLCODE__)
#include_next <sys/stat.h>
#else

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int mode_t;
typedef unsigned int dev_t;
typedef unsigned int ino_t;
typedef unsigned int nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long off_t;
typedef long time_t;

/* Mode bits shared by every mainstream UNIX. */
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFREG 0100000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000

#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* Opaque stat buffer — the kernel layout is arch-dependent.  Users who
 * need to reach inside the struct should import their own layout; here
 * we just reserve enough space so `struct stat buf; stat(&buf)` compiles. */
struct stat {
  unsigned char __opaque[144];
};

int stat(const char *path, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int fstatat(int dirfd, const char *path, struct stat *buf, int flags);

int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int fchmodat(int dirfd, const char *path, mode_t mode, int flags);

int mkdir(const char *path, mode_t mode);
int mkdirat(int dirfd, const char *path, mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* __NEVERC_SHELLCODE__ */
#endif /* _NEVERC_SYS_STAT_SHIM_H_ */
