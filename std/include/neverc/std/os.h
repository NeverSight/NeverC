#ifndef NEVERC_OS_H
#define NEVERC_OS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* File mode bits (mirrors Go os.FileMode) */
#define NEVERC_OS_MODE_DIR     (1 << 31)
#define NEVERC_OS_MODE_SYMLINK (1 << 27)
#define NEVERC_OS_MODE_PERM    0777

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

/* Environment */
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

/* Stdin/Stdout/Stderr */
neverc_os_file_t *neverc_os_stdin(void);
neverc_os_file_t *neverc_os_stdout(void);
neverc_os_file_t *neverc_os_stderr(void);

/* Extended environment */
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

/* Error classification */
int         neverc_os_is_exist(int err);
int         neverc_os_is_not_exist(int err);
int         neverc_os_is_permission(int err);

#ifdef __cplusplus
}
#endif

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
