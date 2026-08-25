#ifndef NEVERC_OS_H
#define NEVERC_OS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* File mode bits (mirrors Go os.FileMode) */
#define NEVERC_OS_MODE_DIR        (1U << 31)
#define NEVERC_OS_MODE_APPEND     (1U << 30)
#define NEVERC_OS_MODE_EXCL       (1U << 29)
#define NEVERC_OS_MODE_TEMPORARY  (1U << 28)
#define NEVERC_OS_MODE_SYMLINK    (1U << 27)
#define NEVERC_OS_MODE_DEVICE     (1U << 26)
#define NEVERC_OS_MODE_NAMEDPIPE  (1U << 25)
#define NEVERC_OS_MODE_SOCKET     (1U << 24)
#define NEVERC_OS_MODE_SETUID     (1U << 23)
#define NEVERC_OS_MODE_SETGID     (1U << 22)
#define NEVERC_OS_MODE_CHARDEVICE (1U << 21)
#define NEVERC_OS_MODE_STICKY     (1U << 20)
#define NEVERC_OS_MODE_IRREGULAR  (1U << 19)
#define NEVERC_OS_MODE_PERM       0777

/* Open flags */
#define NEVERC_OS_O_RDONLY  0x0000
#define NEVERC_OS_O_WRONLY  0x0001
#define NEVERC_OS_O_RDWR    0x0002
#define NEVERC_OS_O_CREATE  0x0040
#define NEVERC_OS_O_TRUNC   0x0200
#define NEVERC_OS_O_APPEND  0x0400
#define NEVERC_OS_O_EXCL    0x0080

/* Seek whence */
#define NEVERC_OS_SEEK_SET  0
#define NEVERC_OS_SEEK_CUR  1
#define NEVERC_OS_SEEK_END  2

typedef struct {
    char     name[1024];
    int64_t  size;
    uint32_t mode;
    int64_t  mod_time;
    int      is_dir;
} neverc_os_fileinfo_t;

typedef struct neverc_os_file neverc_os_file_t;

/* Environment. getenv returns a borrowed, read-only pointer: do not free or
 * modify it. A later environment lookup, any environment mutation, or thread
 * exit may invalidate it. Concurrent environment mutation requires external
 * synchronization. lookup_env returns the same kind of borrowed pointer. */
const char *neverc_os_getenv(const char *key);
int         neverc_os_setenv(const char *key, const char *value);
int         neverc_os_unsetenv(const char *key);
int         neverc_os_hostname(char *buf, size_t cap);

/* Working directory */
int  neverc_os_getwd(char *buf, size_t cap);
int  neverc_os_chdir(const char *dir);

/* File operations */
neverc_os_file_t *neverc_os_open(const char *name, int flags, uint32_t perm);
neverc_os_file_t *neverc_os_create(const char *name);
void              neverc_os_close(neverc_os_file_t *f);
int               neverc_os_read(neverc_os_file_t *f, void *buf, size_t count);
int               neverc_os_write(neverc_os_file_t *f, const void *buf, size_t count);
int64_t           neverc_os_seek(neverc_os_file_t *f, int64_t offset, int whence);
int               neverc_os_sync(neverc_os_file_t *f);

/* Convenience: read/write entire file */
int  neverc_os_read_file(const char *name, unsigned char **out, size_t *out_len);
int  neverc_os_write_file(const char *name, const unsigned char *data, size_t len, uint32_t perm);

/* Directory operations */
int  neverc_os_mkdir(const char *name, uint32_t perm);
int  neverc_os_mkdir_all(const char *path, uint32_t perm);
int  neverc_os_remove(const char *name);
/* Removes a tree without following links encountered at or below path.
 * Ancestor components must be trusted, and callers must prevent concurrent
 * namespace replacement while removal is in progress. Paths whose final
 * component is "." are rejected (Go os.RemoveAll / rmdir(".")). */
int  neverc_os_remove_all(const char *path);
int  neverc_os_rename(const char *oldpath, const char *newpath);

/* File info */
int  neverc_os_stat(const char *name, neverc_os_fileinfo_t *info);
int  neverc_os_lstat(const char *name, neverc_os_fileinfo_t *info);
int  neverc_os_exists(const char *name);
int  neverc_os_is_dir(const char *name);

/* Process */
int  neverc_os_getpid(void);
int  neverc_os_getppid(void);
int  neverc_os_getuid(void);
int  neverc_os_getgid(void);
void neverc_os_exit(int code);

/* Temp files */
int  neverc_os_temp_dir(char *buf, size_t cap);
neverc_os_file_t *neverc_os_create_temp(const char *dir, const char *pattern);

/* Stdin/Stdout/Stderr are borrowed process-lifetime wrappers. Initialization
 * is thread-safe, and passing these wrappers to neverc_os_close is a no-op. */
neverc_os_file_t *neverc_os_stdin(void);
neverc_os_file_t *neverc_os_stdout(void);
neverc_os_file_t *neverc_os_stderr(void);

/* Extended environment; lookup_env's value follows getenv's borrowed-pointer
 * lifetime contract above. */
int         neverc_os_lookup_env(const char *key, const char **value);
char      **neverc_os_environ(int *count);
void        neverc_os_clearenv(void);
char       *neverc_os_expand_env(const char *s);

/* Extended file operations */
int         neverc_os_chmod(const char *name, uint32_t mode);
int         neverc_os_truncate(const char *name, int64_t size);
int         neverc_os_link(const char *oldname, const char *newname);
int         neverc_os_symlink(const char *oldname, const char *newname);
int         neverc_os_readlink(const char *name, char *buf, size_t cap);

/* Extended directory operations */
typedef struct {
    char     name[1024];
    int      is_dir;
} neverc_os_dir_entry_t;

int         neverc_os_read_dir(const char *dirname, neverc_os_dir_entry_t **entries,
                               size_t *count);
int         neverc_os_mkdir_temp(const char *dir, const char *pattern,
                                char *buf, size_t cap);

/* Extended process info */
int         neverc_os_getegid(void);
int         neverc_os_geteuid(void);
int         neverc_os_executable(char *buf, size_t cap);

/* User directories */
int         neverc_os_user_home_dir(char *buf, size_t cap);
int         neverc_os_user_cache_dir(char *buf, size_t cap);
int         neverc_os_user_config_dir(char *buf, size_t cap);

/* Pipe: creates connected read/write file descriptors */
int         neverc_os_pipe(neverc_os_file_t **r, neverc_os_file_t **w);

/* Ownership (POSIX; no-op on Windows) */
int         neverc_os_chown(const char *name, int uid, int gid);
int         neverc_os_lchown(const char *name, int uid, int gid);

/* Error classification */
int         neverc_os_is_exist(int err);
int         neverc_os_is_not_exist(int err);
int         neverc_os_is_permission(int err);

#ifdef __cplusplus
}
#endif

#include "os/exec.h"
#include "os/signal.h"
#include "os/user.h"

#ifdef __neverc__
struct __neverc_std_exec_t { char __tag; };
struct __neverc_std_signal_t { char __tag; };
struct __neverc_std_user_t { char __tag; };

struct __neverc_std_os_t {
    char __tag;
    struct __neverc_std_exec_t exec;
    struct __neverc_std_signal_t signal;
    struct __neverc_std_user_t user;
};
extern struct __neverc_std_os_t __neverc_mod_os;
extern struct __neverc_std_os_t os;
#endif

#endif
