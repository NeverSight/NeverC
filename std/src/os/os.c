#include "neverc/std/os.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>

#ifndef NCI_OS_RANDOM
#define NCI_OS_RANDOM neverc_platform_random
#endif

#if defined(NEVERC_PLATFORM_APPLE)
#include <mach-o/dyld.h>
#endif

#if defined(NEVERC_PLATFORM_WINDOWS)
#include <windows.h>
#include <tlhelp32.h>
#include <winioctl.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/types.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#endif

struct neverc_os_file {
    FILE *fp;
    int   fd;
    int   is_std;
};

static neverc_os_file_t *file_from_descriptor(int fd, const char *mode);

#if defined(NEVERC_PLATFORM_WINDOWS)
static void os_win_set_errno(void) {
    DWORD e = GetLastError();
    switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
        errno = ENOENT;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        errno = EACCES;
        break;
    case ERROR_PRIVILEGE_NOT_HELD:
        errno = EPERM;
        break;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        errno = EEXIST;
        break;
    case ERROR_NOT_SAME_DEVICE:
        errno = EXDEV;
        break;
    case ERROR_DIR_NOT_EMPTY:
        errno = ENOTEMPTY;
        break;
    case ERROR_FILENAME_EXCED_RANGE:
        errno = ENAMETOOLONG;
        break;
    case ERROR_DISK_FULL:
        errno = ENOSPC;
        break;
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
        errno = EINVAL;
        break;
    default:
        errno = EINVAL;
        break;
    }
}

static int os_win_fail(void) {
    os_win_set_errno();
    return -1;
}

/* \\?\, \\.\, and \??\ are Win32/NT prefixes, not FindFirstFile wildcards.
 * fs.ReadDir already skips them; os.ReadDir/RemoveAll must too. */
static int os_win_skip_extended_prefix(const char *path) {
    if ((path[0] == '\\' || path[0] == '/') &&
        path[1] == '?' && path[2] == '?' &&
        (path[3] == '\\' || path[3] == '/'))
        return 4;
    if ((path[0] == '\\' || path[0] == '/') &&
        (path[1] == '\\' || path[1] == '/') &&
        (path[2] == '?' || path[2] == '.') &&
        (path[3] == '\\' || path[3] == '/'))
        return 4;
    return 0;
}

static int os_win_has_wildcards(const char *path) {
    path += os_win_skip_extended_prefix(path);
    for (; *path; path++) {
        char c = *path;
        if (c == '*' || c == '?' || c == '<' || c == '>' || c == '"')
            return 1;
    }
    return 0;
}

static int os_win_is_unc_remainder(const char *p) {
    return (p[0] == 'U' || p[0] == 'u') &&
           (p[1] == 'N' || p[1] == 'n') &&
           (p[2] == 'C' || p[2] == 'c') &&
           (p[3] == '\\' || p[3] == '/');
}

/* Go os.RemoveAll keeps \\?\ so Win32 does not re-parse '..'. After dropping
 * the prefix for ANSI APIs, a leftover '..' component would walk to the
 * parent. Reject that instead of deleting the wrong tree. Win32 also
 * strips trailing spaces/dots per component, so ".. " is the parent. */
static int os_win_path_has_dotdot_component(const char *path) {
    const char *p = path;
    while (*p) {
        const char *start = p;
        while (*p && *p != '\\' && *p != '/')
            p++;
        size_t n = (size_t)(p - start);
        if (n == 2 && start[0] == '.' && start[1] == '.')
            return 1;
        size_t stripped = n;
        while (stripped > 0 &&
               (start[stripped - 1] == ' ' || start[stripped - 1] == '.'))
            stripped--;
        if (stripped != n && stripped == 2 &&
            start[0] == '.' && start[1] == '.')
            return 1;
        if (*p)
            p++;
    }
    return 0;
}

/* Map NT/Win32 prefixes to a path ANSI APIs can use without turning a
 * volume/device path into a cwd-relative name (Go os.normaliseLinkPath /
 * RemoveAll). Drive-absolute \\?\C:\foo may drop the prefix; \\?\C: must
 * not become C: (the drive's cwd). \??\ is converted to \\?\ otherwise so
 * FindFirstFileA does not treat '?' as a wildcard. */
static int os_win_prepare_path(const char *path, char *dst, size_t dst_cap,
                               const char **out) {
    int skip = os_win_skip_extended_prefix(path);
    const char *rest = path + skip;
    int nt_prefix;
    int n;

    if (skip == 0) {
        *out = path;
        return 0;
    }
    nt_prefix = path[1] == '?' && path[2] == '?';

    if (os_win_is_unc_remainder(rest)) {
        n = snprintf(dst, dst_cap, "\\\\%s", rest + 4);
        if (n < 0 || (size_t)n >= dst_cap)
            return -1;
        *out = dst;
        return 0;
    }

    if (rest[0] != '\0' && rest[1] == ':' &&
        (rest[2] == '\\' || rest[2] == '/')) {
        *out = rest;
        return 0;
    }

    if (nt_prefix) {
        n = snprintf(dst, dst_cap, "\\\\?\\%s", rest);
        if (n < 0 || (size_t)n >= dst_cap)
            return -1;
        *out = dst;
        return 0;
    }
    *out = path;
    return 0;
}

/* Go path/filepath: "C:" is the drive's cwd, not C:\. Join("C:", "*")
 * is "C:*". "C:\*" would list/delete the volume root. */
static int os_win_is_drive_cwd(const char *dir, size_t n) {
    return n == 2 &&
           ((dir[0] >= 'A' && dir[0] <= 'Z') ||
            (dir[0] >= 'a' && dir[0] <= 'z')) &&
           dir[1] == ':';
}

/* Go: \\?\C: is not a valid Win32 directory name (needs \\?\C:\). */
static int os_win_is_prefixed_bare_drive(const char *dir, size_t n) {
    if (n != 6) return 0;
    if (dir[5] != ':') return 0;
    if (!((dir[4] >= 'A' && dir[4] <= 'Z') ||
          (dir[4] >= 'a' && dir[4] <= 'z')))
        return 0;
    if (!(dir[0] == '\\' || dir[0] == '/')) return 0;
    if (!(dir[3] == '\\' || dir[3] == '/')) return 0;
    if (dir[1] == '?' && dir[2] == '?')
        return 1;
    return (dir[1] == '\\' || dir[1] == '/') && dir[2] == '?';
}

/* Go #17500: \\?\C:\\* (doubled slash) fails on some Windows versions. */
static int os_win_dir_star(char *pattern, size_t cap, const char *dir) {
    size_t n;
    int w;
    if (!dir || dir[0] == '\0')
        return -1;
    n = strlen(dir);
    if (os_win_is_prefixed_bare_drive(dir, n)) {
        errno = EINVAL;
        return -1;
    }
    if (dir[n - 1] == '\\' || dir[n - 1] == '/' || os_win_is_drive_cwd(dir, n))
        w = snprintf(pattern, cap, "%s*", dir);
    else
        w = snprintf(pattern, cap, "%s\\*", dir);
    if (w < 0 || (size_t)w >= cap)
        return -1;
    return 0;
}

static int os_win_join_child(char *dst, size_t cap, const char *dir,
                             const char *name) {
    size_t n;
    if (!dir || !name || cap == 0) return -1;
    n = strlen(dir);
    if (n == 0) return -1;
    if (dir[n - 1] == '\\' || dir[n - 1] == '/' || os_win_is_drive_cwd(dir, n))
        return snprintf(dst, cap, "%s%s", dir, name);
    return snprintf(dst, cap, "%s\\%s", dir, name);
}

/* Win32 strips trailing spaces/dots, so ".. " is the parent directory. */
static int os_win_entry_name_ok(const char *name) {
    size_t n;
    if (!name || name[0] == '\0') return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    if (strchr(name, '/') || strchr(name, '\\')) return 0;
    n = strlen(name);
    while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '.'))
        n--;
    if (n == 0) return 0;
    if (n == 1 && name[0] == '.') return 0;
    if (n == 2 && name[0] == '.' && name[1] == '.') return 0;
    return 1;
}

static int64_t os_filetime_unix(FILETIME ft) {
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    if (ull.QuadPart < 116444736000000000ULL)
        return 0;
    return (int64_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}
#endif

static neverc_os_file_t g_stdin  = { NULL, 0, 1 };
static neverc_os_file_t g_stdout = { NULL, 1, 1 };
static neverc_os_file_t g_stderr = { NULL, 2, 1 };
static int g_std_init = 0;

static void init_std(void) {
    if (!g_std_init) {
        g_stdin.fp = stdin;
        g_stdout.fp = stdout;
        g_stderr.fp = stderr;
        g_std_init = 1;
    }
}

neverc_os_file_t *neverc_os_stdin(void)  { init_std(); return &g_stdin; }
neverc_os_file_t *neverc_os_stdout(void) { init_std(); return &g_stdout; }
neverc_os_file_t *neverc_os_stderr(void) { init_std(); return &g_stderr; }

/* ---- Environment ---- */

static int os_env_key_ok(const char *key) {
    if (!key || key[0] == '\0') return 0;
    for (const char *p = key; *p; p++) {
        if (*p == '=') return 0;
    }
    return 1;
}

#if defined(NEVERC_PLATFORM_WINDOWS)
/* getenv() reads a CRT snapshot; SetEnvironmentVariableA updates the process
   environment block directly.  Route all NeverC os env reads through Win32 so
   neverc_os_setenv/unsetenv are immediately visible to neverc_os_getenv. */
static char neverc_os_getenv_buf[32768];
#endif

const char *neverc_os_getenv(const char *key) {
    if (!os_env_key_ok(key)) return NULL;
#if defined(NEVERC_PLATFORM_WINDOWS)
    SetLastError(ERROR_SUCCESS);
    DWORD n = GetEnvironmentVariableA(key, neverc_os_getenv_buf,
                                    (DWORD)sizeof(neverc_os_getenv_buf));
    if (n >= sizeof(neverc_os_getenv_buf)) return NULL;
    if (n == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return NULL;
        neverc_os_getenv_buf[0] = '\0';
    }
    return neverc_os_getenv_buf;
#else
    return getenv(key);
#endif
}

int neverc_os_setenv(const char *key, const char *value) {
    if (!os_env_key_ok(key) || !value) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (!SetEnvironmentVariableA(key, value))
        return os_win_fail();
    return 0;
#else
    return setenv(key, value, 1) == 0 ? 0 : -1;
#endif
}

int neverc_os_unsetenv(const char *key) {
    if (!os_env_key_ok(key)) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (!SetEnvironmentVariableA(key, NULL))
        return os_win_fail();
    return 0;
#else
    return unsetenv(key) == 0 ? 0 : -1;
#endif
}

int neverc_os_hostname(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (cap > 0xFFFFFFFFUL) return -1;
    DWORD sz = (DWORD)cap;
    if (!GetComputerNameA(buf, &sz))
        return os_win_fail();
    return 0;
#else
    char tmp[1024];
    if (gethostname(tmp, sizeof(tmp)) != 0) return -1;
    tmp[sizeof(tmp) - 1] = '\0';
    size_t n = strlen(tmp);
    if (n >= sizeof(tmp) - 1) return -1;
    if (n >= cap) return -1;
    memcpy(buf, tmp, n + 1);
    return 0;
#endif
}

/* ---- Working Directory ---- */

int neverc_os_getwd(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (cap > (size_t)INT_MAX) return -1;
    return _getcwd(buf, (int)cap) ? 0 : -1;
#else
    return getcwd(buf, cap) ? 0 : -1;
#endif
}

int neverc_os_chdir(const char *dir) {
    if (!dir || dir[0] == '\0') return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return _chdir(dir) == 0 ? 0 : -1;
#else
    return chdir(dir) == 0 ? 0 : -1;
#endif
}

/* ---- File Operations ---- */

neverc_os_file_t *neverc_os_open(const char *name, int flags, uint32_t perm) {
    if (!name || name[0] == '\0') return NULL;
    int rw = flags & 0x03;
    if (rw != NEVERC_OS_O_RDONLY && rw != NEVERC_OS_O_WRONLY &&
        rw != NEVERC_OS_O_RDWR)
        return NULL;
    /* O_TRUNC requires write access. Some CRTs still truncate a read-only
     * open; Go/Windows CreateFile refuses TRUNCATE_EXISTING without
     * GENERIC_WRITE. Fail closed instead of wiping the file. */
    if ((flags & NEVERC_OS_O_TRUNC) && rw == NEVERC_OS_O_RDONLY)
        return NULL;

    int native_flags;
#if defined(NEVERC_PLATFORM_WINDOWS)
    native_flags = _O_BINARY | _O_NOINHERIT;
    native_flags |= rw == NEVERC_OS_O_RDONLY ? _O_RDONLY
                  : rw == NEVERC_OS_O_WRONLY ? _O_WRONLY : _O_RDWR;
    if (flags & NEVERC_OS_O_CREATE) native_flags |= _O_CREAT;
    if (flags & NEVERC_OS_O_EXCL) native_flags |= _O_EXCL;
    if (flags & NEVERC_OS_O_TRUNC) native_flags |= _O_TRUNC;
    if (flags & NEVERC_OS_O_APPEND) native_flags |= _O_APPEND;
    int native_perm = 0;
    if (perm & 0444) native_perm |= _S_IREAD;
    if (perm & 0222) native_perm |= _S_IWRITE;
    int fd = _open(name, native_flags, native_perm);
#else
    native_flags = rw == NEVERC_OS_O_RDONLY ? O_RDONLY
                 : rw == NEVERC_OS_O_WRONLY ? O_WRONLY : O_RDWR;
    if (flags & NEVERC_OS_O_CREATE) native_flags |= O_CREAT;
    if (flags & NEVERC_OS_O_EXCL) native_flags |= O_EXCL;
    if (flags & NEVERC_OS_O_TRUNC) native_flags |= O_TRUNC;
    if (flags & NEVERC_OS_O_APPEND) native_flags |= O_APPEND;
#ifdef O_CLOEXEC
    native_flags |= O_CLOEXEC;
#endif
    int fd = open(name, native_flags, (mode_t)(perm & NEVERC_OS_MODE_PERM));
#endif
    if (fd < 0) return NULL;

    const char *mode = rw == NEVERC_OS_O_RDONLY ? "rb"
                     : rw == NEVERC_OS_O_RDWR
                           ? ((flags & NEVERC_OS_O_APPEND) ? "ab+" : "rb+")
                           : ((flags & NEVERC_OS_O_APPEND) ? "ab" : "wb");
    return file_from_descriptor(fd, mode);
}

neverc_os_file_t *neverc_os_create(const char *name) {
    return neverc_os_open(name, NEVERC_OS_O_RDWR | NEVERC_OS_O_CREATE | NEVERC_OS_O_TRUNC, 0666);
}

void neverc_os_close(neverc_os_file_t *f) {
    if (!f || f->is_std) return;
    if (f->fp) fclose(f->fp);
    free(f);
}

int neverc_os_read(neverc_os_file_t *f, void *buf, size_t count) {
    if (!f || !f->fp || (!buf && count != 0) || count > INT_MAX) return -1;
    size_t n = fread(buf, 1, count, f->fp);
    if (ferror(f->fp)) return -1;
    return (int)n;
}

int neverc_os_write(neverc_os_file_t *f, const void *buf, size_t count) {
    if (!f || !f->fp || (!buf && count != 0) || count > INT_MAX) return -1;
    size_t n = fwrite(buf, 1, count, f->fp);
    if (ferror(f->fp)) return -1;
    return (int)n;
}

int64_t neverc_os_seek(neverc_os_file_t *f, int64_t offset, int whence) {
    if (!f || !f->fp) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (_fseeki64(f->fp, offset, whence) != 0) return -1;
    return (int64_t)_ftelli64(f->fp);
#else
    if ((off_t)offset != offset) return -1;
    if (fseeko(f->fp, (off_t)offset, whence) != 0) return -1;
    off_t pos = ftello(f->fp);
    if (pos < 0) return -1;
    return (int64_t)pos;
#endif
}

int neverc_os_sync(neverc_os_file_t *f) {
    if (!f || !f->fp) return -1;
    if (fflush(f->fp) != 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    HANDLE h = (HANDLE)_get_osfhandle(f->fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (FlushFileBuffers(h)) return 0;
    DWORD err = GetLastError();
    return (err == ERROR_INVALID_HANDLE || err == ERROR_INVALID_FUNCTION) ? 0 : -1;
#else
    return fsync(f->fd) == 0 ? 0 : -1;
#endif
}

/* ---- Convenience File Operations ---- */

static int os_file_size64(FILE *fp, uint64_t *size) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (_fseeki64(fp, 0, SEEK_END) != 0) return -1;
    __int64 sz = _ftelli64(fp);
    if (sz < 0) return -1;
    if (_fseeki64(fp, 0, SEEK_SET) != 0) return -1;
    *size = (uint64_t)sz;
#else
    if (fseeko(fp, 0, SEEK_END) != 0) return -1;
    off_t sz = ftello(fp);
    if (sz < 0) return -1;
    if (fseeko(fp, 0, SEEK_SET) != 0) return -1;
    *size = (uint64_t)sz;
#endif
    return 0;
}

/* Go os.ReadFile: st_size is a hint. Size-0 files (/proc, sysfs) still have
 * contents; keep reading until EOF. */
static int os_read_fp_all(FILE *fp, uint64_t hint, unsigned char **out,
                          size_t *out_len) {
    size_t cap;
    if (hint > (uint64_t)SIZE_MAX - 1U) return -1;
    cap = (hint == 0) ? 512U : (size_t)hint;
    unsigned char *buf = (unsigned char *)malloc(cap + 1U);
    if (!buf) return -1;
    size_t n = 0;
    for (;;) {
        if (n >= cap) {
            if (cap > (SIZE_MAX - 1U) / 2U) {
                free(buf);
                return -1;
            }
            size_t ncap = cap * 2U;
            unsigned char *nb = (unsigned char *)realloc(buf, ncap + 1U);
            if (!nb) {
                free(buf);
                return -1;
            }
            buf = nb;
            cap = ncap;
        }
        size_t got = fread(buf + n, 1, cap - n, fp);
        n += got;
        if (ferror(fp)) {
            free(buf);
            return -1;
        }
        if (got == 0)
            break;
    }
    buf[n] = '\0';
    *out = buf;
    *out_len = n;
    return 0;
}

int neverc_os_read_file(const char *name, unsigned char **out, size_t *out_len) {
    if (!name || !out || !out_len) return -1;
    *out = NULL;
    *out_len = 0;
    FILE *fp = fopen(name, "rb");
    if (!fp) return -1;

    uint64_t sz = 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    struct _stat64 st;
    if (_fstat64(_fileno(fp), &st) != 0) {
        fclose(fp);
        return -1;
    }
    if (st.st_mode & _S_IFDIR) {
        fclose(fp);
        return -1;
    }
    if ((st.st_mode & _S_IFREG) && st.st_size >= 0) {
        sz = (uint64_t)st.st_size;
    } else if (os_file_size64(fp, &sz) != 0) {
        fclose(fp);
        return -1;
    }
#else
    struct stat st;
    if (fstat(fileno(fp), &st) != 0) {
        fclose(fp);
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        fclose(fp);
        return -1;
    }
    if (S_ISREG(st.st_mode) && st.st_size >= 0) {
        sz = (uint64_t)st.st_size;
    } else if (os_file_size64(fp, &sz) != 0) {
        fclose(fp);
        return -1;
    }
#endif
    if (os_read_fp_all(fp, sz, out, out_len) != 0) {
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) {
        free(*out);
        *out = NULL;
        *out_len = 0;
        return -1;
    }
    return 0;
}

int neverc_os_write_file(const char *name, const unsigned char *data, size_t len, uint32_t perm) {
    if (!name || (!data && len != 0)) return -1;
    neverc_os_file_t *file = neverc_os_open(
        name, NEVERC_OS_O_WRONLY | NEVERC_OS_O_CREATE | NEVERC_OS_O_TRUNC,
        perm);
    if (!file) return -1;

    size_t offset = 0;
    int result = 0;
    while (offset < len) {
        size_t remaining = len - offset;
        size_t chunk = remaining > (size_t)INT_MAX
                           ? (size_t)INT_MAX
                           : remaining;
        int written = neverc_os_write(file, data + offset, chunk);
        if (written <= 0) {
            result = -1;
            break;
        }
        offset += (size_t)written;
    }
    if (file->fp && fclose(file->fp) != 0) result = -1;
    file->fp = NULL;
    free(file);
    return result;
}

/* ---- Directory Operations ---- */

int neverc_os_mkdir(const char *name, uint32_t perm) {
    if (!name || name[0] == '\0') return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    (void)perm;
    return _mkdir(name) == 0 ? 0 : -1;
#else
    return mkdir(name, (mode_t)perm) == 0 ? 0 : -1;
#endif
}

int neverc_os_mkdir_all(const char *path, uint32_t perm) {
    if (!path || path[0] == '\0') return -1;
    char buf[4096];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return -1;
    memcpy(buf, path, len + 1);

    neverc_os_fileinfo_t info;
    if (neverc_os_stat(path, &info) == 0) {
        if (info.is_dir) return 0;
        errno = ENOTDIR;
        return -1;
    }

    int mkdir_err = 0;
    for (size_t i = 1; i <= len; i++) {
#if defined(NEVERC_PLATFORM_WINDOWS)
        int sep = (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0');
#else
        int sep = (buf[i] == '/' || buf[i] == '\0');
#endif
        if (sep) {
            char saved = buf[i];
            buf[i] = '\0';
            if (neverc_os_mkdir(buf, perm) != 0) {
                mkdir_err = errno;
                if (neverc_os_stat(buf, &info) == 0 && !info.is_dir) {
                    errno = ENOTDIR;
                    return -1;
                }
            } else {
                mkdir_err = 0;
            }
            buf[i] = saved;
        }
    }
    if (neverc_os_is_dir(path)) return 0;
    if (neverc_os_stat(path, &info) == 0) {
        errno = ENOTDIR;
        return -1;
    }
    if (mkdir_err != 0) errno = mkdir_err;
    return -1;
}

#if defined(NEVERC_PLATFORM_WINDOWS)
/* Go os.fileStat.Mode: READONLY → 0444, else 0666; directories add 0111. */
static uint32_t os_win_mode_from_attrs(DWORD attrs, int is_dir) {
    uint32_t mode = (attrs & FILE_ATTRIBUTE_READONLY) ? 0444U : 0666U;
    if (is_dir)
        mode |= NEVERC_OS_MODE_DIR | 0111U;
    return mode;
}

/* Drive `C:` / UNC `\\server\share` after FromSlash. */
static size_t os_win_volume_len(const char *path) {
    if (!path || !path[0])
        return 0;
    if (((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':')
        return 2;
    if (path[0] == '\\' && path[1] == '\\') {
        const char *p = path + 2;
        while (*p && *p != '\\')
            p++;
        if (*p == '\\') {
            p++;
            while (*p && *p != '\\')
                p++;
        }
        return (size_t)(p - path);
    }
    return 0;
}

/* Go os.dirname: volume + parent, or "." when the name has no separator. */
static int os_win_dirname(const char *path, char *out, size_t cap) {
    size_t vol;
    size_t n;
    if (!path || !out || cap == 0)
        return -1;
    vol = os_win_volume_len(path);
    n = strlen(path);
    while (n > vol && (path[n - 1] == '\\' || path[n - 1] == '/'))
        n--;
    while (n > vol && path[n - 1] != '\\' && path[n - 1] != '/')
        n--;
    while (n > vol && (path[n - 1] == '\\' || path[n - 1] == '/'))
        n--;
    if (n <= vol) {
        if (vol > 0) {
            if (vol >= cap)
                return -1;
            memcpy(out, path, vol);
            out[vol] = '\0';
            return 0;
        }
        if (cap < 2)
            return -1;
        out[0] = '.';
        out[1] = '\0';
        return 0;
    }
    if (n >= cap)
        return -1;
    memcpy(out, path, n);
    out[n] = '\0';
    return 0;
}

/* Go os.Symlink destpath: resolve a relative oldname against newname. */
static int os_win_symlink_destpath(const char *oldname, const char *newname,
                                   char *out, size_t cap) {
    size_t old_vol;
    if (!oldname || !newname || !out || cap == 0)
        return -1;
    old_vol = os_win_volume_len(oldname);
    if (old_vol != 0) {
        size_t n = strlen(oldname);
        if (n >= cap)
            return -1;
        memcpy(out, oldname, n + 1);
        return 0;
    }
    if (oldname[0] == '\\') {
        size_t new_vol = os_win_volume_len(newname);
        size_t n = strlen(oldname);
        if (new_vol + n >= cap)
            return -1;
        if (new_vol > 0)
            memcpy(out, newname, new_vol);
        memcpy(out + new_vol, oldname, n + 1);
        return 0;
    }
    {
        char dir[4096];
        int n;
        if (os_win_dirname(newname, dir, sizeof(dir)) != 0)
            return -1;
        n = snprintf(out, cap, "%s\\%s", dir, oldname);
        if (n < 0 || (size_t)n >= cap)
            return -1;
    }
    return 0;
}

/* Go os.Remove / RemoveAll: ACCESS_DENIED on a readonly file or directory
 * is retried after clearing FILE_ATTRIBUTE_READONLY. Save each last-error:
 * DeleteFileA on a readonly file returns ACCESS_DENIED, then
 * RemoveDirectoryA on that same file overwrites it with ERROR_DIRECTORY. */
static int os_win_delete_path(const char *name) {
    DWORD file_err = 0, dir_err = 0;
    if (DeleteFileA(name)) return 0;
    file_err = GetLastError();
    if (RemoveDirectoryA(name)) return 0;
    dir_err = GetLastError();
    if (file_err == ERROR_ACCESS_DENIED || dir_err == ERROR_ACCESS_DENIED) {
        DWORD attr = GetFileAttributesA(name);
        if (attr != INVALID_FILE_ATTRIBUTES &&
            (attr & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesA(name, attr & ~(DWORD)FILE_ATTRIBUTE_READONLY);
            if (DeleteFileA(name) || RemoveDirectoryA(name)) return 0;
        }
    }
    return -1;
}
#endif

int neverc_os_remove(const char *name) {
    if (!name || name[0] == '\0') return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return os_win_delete_path(name) == 0 ? 0 : os_win_fail();
#else
    return remove(name) == 0 ? 0 : -1;
#endif
}

#if !defined(NEVERC_PLATFORM_WINDOWS)
static int os_same_file(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static int os_remove_dir_contents(int dir_fd) {
    int scan_fd = dup(dir_fd);
    if (scan_fd < 0) return -1;
    DIR *dir = fdopendir(scan_fd);
    if (!dir) {
        close(scan_fd);
        return -1;
    }

    int result = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno != 0) result = -1;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        struct stat before;
        if (fstatat(dir_fd, entry->d_name, &before,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno != ENOENT) result = -1;
            continue;
        }
        if (!S_ISDIR(before.st_mode)) {
            if (unlinkat(dir_fd, entry->d_name, 0) != 0 &&
                errno != ENOENT)
                result = -1;
            continue;
        }

        int open_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW;
#ifdef O_CLOEXEC
        open_flags |= O_CLOEXEC;
#endif
        int child_fd = openat(dir_fd, entry->d_name, open_flags);
        if (child_fd < 0) {
            if (errno != ENOENT) result = -1;
            continue;
        }

        struct stat opened;
        int opened_ok =
            fstat(child_fd, &opened) == 0 &&
            os_same_file(&before, &opened);
        if (!opened_ok ||
            os_remove_dir_contents(child_fd) != 0)
            result = -1;
        if (close(child_fd) != 0)
            result = -1;
        if (!opened_ok)
            continue;

        struct stat current;
        if (fstatat(dir_fd, entry->d_name, &current,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno != ENOENT) result = -1;
            continue;
        }
        if (!os_same_file(&opened, &current)) {
            result = -1;
            continue;
        }
        if (unlinkat(dir_fd, entry->d_name, AT_REMOVEDIR) != 0 &&
            errno != ENOENT)
            result = -1;
    }

    if (closedir(dir) != 0)
        result = -1;
    return result;
}
#endif

static int os_is_sep(char c) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/* Go os.endsWithDot: rmdir(".") is EINVAL, so RemoveAll(".") must not
 * empty the current directory first. Trailing separators are ignored so
 * "./" and ".\\" are the same as ".". Checked after \\?\ stripping so
 * "\\\\?\\." cannot jail-break back to cwd. */
static int os_remove_all_ends_with_dot(const char *path) {
    size_t n;
    if (!path || path[0] == '\0')
        return 0;
    n = strlen(path);
    while (n > 1 && os_is_sep(path[n - 1]))
        n--;
    if (n == 1 && path[0] == '.')
        return 1;
    if (n >= 2 && path[n - 1] == '.' && os_is_sep(path[n - 2]))
        return 1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    /* After \\?\ is dropped, Win32 strips trailing spaces/dots: ". " is
     * rmdir("."). Exact "foo." stays a filename (stripped != "."). */
    {
        size_t end = n;
        size_t start;
        while (n > 0 && !os_is_sep(path[n - 1]))
            n--;
        start = n;
        n = end - start;
        while (n > 0 && (path[start + n - 1] == ' ' ||
                         path[start + n - 1] == '.'))
            n--;
        if ((end - start) != n && n <= 1 &&
            (n == 0 || path[start] == '.'))
            return 1;
    }
#endif
    return 0;
}

int neverc_os_remove_all(const char *path) {
    if (!path || path[0] == '\0') return -1;

#if defined(NEVERC_PLATFORM_WINDOWS)
    char prepared[4096];
    const char *use;
    if (os_win_has_wildcards(path)) {
        errno = EINVAL;
        return -1;
    }
    if (os_win_prepare_path(path, prepared, sizeof(prepared), &use) != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    path = use;
    if (os_win_path_has_dotdot_component(path)) {
        errno = EINVAL;
        return -1;
    }
#endif
    if (os_remove_all_ends_with_dot(path)) {
        errno = EINVAL;
        return -1;
    }
#if defined(NEVERC_PLATFORM_WINDOWS)
    /* \\?\. and \??\. keep an extended prefix after prepare; the final
     * component is still "." (Go os.endsWithDot / rmdir(".")). */
    {
        int skip = os_win_skip_extended_prefix(path);
        if (skip && os_remove_all_ends_with_dot(path + skip)) {
            errno = EINVAL;
            return -1;
        }
    }
#endif

#if defined(NEVERC_PLATFORM_WINDOWS)
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return (error == ERROR_FILE_NOT_FOUND ||
                error == ERROR_PATH_NOT_FOUND) ? 0 : -1;
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
        return os_win_delete_path(path) == 0 ? 0 : -1;
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return os_win_delete_path(path) == 0 ? 0 : -1;

    WIN32_FIND_DATAA fd;
    char pattern[4096];
    if (os_win_dir_star(pattern, sizeof(pattern), path) != 0)
        return -1;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int result = 0;
    do {
        if (!os_win_entry_name_ok(fd.cFileName)) continue;
        char child[4096];
        int child_len = os_win_join_child(
            child, sizeof(child), path, fd.cFileName);
        if (child_len < 0 || (size_t)child_len >= sizeof(child) ||
            neverc_os_remove_all(child) != 0)
            result = -1;
    } while (FindNextFileA(h, &fd));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        result = -1;
    FindClose(h);
    if (result != 0)
        return -1;
    return os_win_delete_path(path) == 0 ? 0 : -1;
#else
    size_t path_len = strlen(path);
    while (path_len > 1 && path[path_len - 1] == '/')
        path_len--;
    if (path_len == SIZE_MAX) return -1;
    char *clean_path = (char *)malloc(path_len + 1);
    if (!clean_path) return -1;
    memcpy(clean_path, path, path_len);
    clean_path[path_len] = '\0';

    struct stat st;
    if (lstat(clean_path, &st) != 0) {
        int result = errno == ENOENT ? 0 : -1;
        free(clean_path);
        return result;
    }
    if (!S_ISDIR(st.st_mode)) {
        int result = unlink(clean_path);
        free(clean_path);
        return result;
    }

    int open_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW;
#ifdef O_CLOEXEC
    open_flags |= O_CLOEXEC;
#endif
    int root_fd = open(clean_path, open_flags);
    if (root_fd < 0) {
        free(clean_path);
        return -1;
    }
    struct stat opened;
    int result =
        fstat(root_fd, &opened) == 0 && os_same_file(&st, &opened)
            ? os_remove_dir_contents(root_fd) : -1;
    if (close(root_fd) != 0)
        result = -1;

    struct stat current;
    if (result == 0 && lstat(clean_path, &current) != 0) {
        if (errno != ENOENT)
            result = -1;
    } else if (result == 0 && !os_same_file(&opened, &current)) {
        result = -1;
    }
    if (result == 0 && rmdir(clean_path) != 0 && errno != ENOENT)
        result = -1;
    free(clean_path);
    return result;
#endif
}

int neverc_os_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath || oldpath[0] == '\0' || newpath[0] == '\0')
        return -1;
    return rename(oldpath, newpath) == 0 ? 0 : -1;
}

/* ---- File Info ---- */

static int os_dir_entry_cmp(const void *a, const void *b) {
    return strcmp(((const neverc_os_dir_entry_t *)a)->name,
                  ((const neverc_os_dir_entry_t *)b)->name);
}

static void os_fill_base_name(char *dst, size_t cap, const char *name) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!name) return;
    size_t len = strlen(name);
    while (len > 1 && os_is_sep(name[len - 1]))
        len--;
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (os_is_sep(name[i]))
            start = i + 1;
    }
    size_t n = len - start;
    if (n == 0) {
        if (cap > 1 && len > 0 && os_is_sep(name[0])) {
            dst[0] = name[0];
            dst[1] = '\0';
        }
        return;
    }
    if (n >= cap) n = cap - 1;
    memcpy(dst, name + start, n);
    dst[n] = '\0';
}

#if !defined(NEVERC_PLATFORM_WINDOWS)
static void os_fill_from_st(neverc_os_fileinfo_t *info, const char *name,
                            const struct stat *st) {
    info->size = (int64_t)st->st_size;
    info->mode = (uint32_t)st->st_mode & NEVERC_OS_MODE_PERM;
    info->mod_time = (int64_t)st->st_mtime;
    info->is_dir = S_ISDIR(st->st_mode);
    if (info->is_dir) info->mode |= NEVERC_OS_MODE_DIR;
    if (S_ISLNK(st->st_mode)) info->mode |= NEVERC_OS_MODE_SYMLINK;
    if (S_ISFIFO(st->st_mode)) info->mode |= NEVERC_OS_MODE_NAMEDPIPE;
    if (S_ISSOCK(st->st_mode)) info->mode |= NEVERC_OS_MODE_SOCKET;
    if (S_ISCHR(st->st_mode))
        info->mode |= NEVERC_OS_MODE_DEVICE | NEVERC_OS_MODE_CHARDEVICE;
    else if (S_ISBLK(st->st_mode))
        info->mode |= NEVERC_OS_MODE_DEVICE;
    if (st->st_mode & S_ISUID) info->mode |= NEVERC_OS_MODE_SETUID;
    if (st->st_mode & S_ISGID) info->mode |= NEVERC_OS_MODE_SETGID;
    if (st->st_mode & S_ISVTX) info->mode |= NEVERC_OS_MODE_STICKY;
    if (!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode) && !S_ISLNK(st->st_mode) &&
        !S_ISFIFO(st->st_mode) && !S_ISSOCK(st->st_mode) &&
        !S_ISCHR(st->st_mode) && !S_ISBLK(st->st_mode))
        info->mode |= NEVERC_OS_MODE_IRREGULAR;
    os_fill_base_name(info->name, sizeof(info->name), name);
}
#endif

int neverc_os_stat(const char *name, neverc_os_fileinfo_t *info) {
    if (!name || !info) return -1;
    memset(info, 0, sizeof(*info));

#if defined(NEVERC_PLATFORM_WINDOWS)
    /* Follow reparse points like Go os.Stat / CreateFile without
     * FILE_FLAG_OPEN_REPARSE_POINT, then apply Win32 attributes (not CRT
     * _stat64 0777). */
    HANDLE h = CreateFileA(name, FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    BY_HANDLE_FILE_INFORMATION bh;
    DWORD ftype;
    LARGE_INTEGER sz;
    if (h == INVALID_HANDLE_VALUE)
        return os_win_fail();
    if (!GetFileInformationByHandle(h, &bh)) {
        os_win_set_errno();
        CloseHandle(h);
        return -1;
    }
    ftype = GetFileType(h);
    CloseHandle(h);
    sz.HighPart = (LONG)bh.nFileSizeHigh;
    sz.LowPart = bh.nFileSizeLow;
    info->size = sz.QuadPart;
    info->mod_time = os_filetime_unix(bh.ftLastWriteTime);
    info->is_dir = (bh.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info->mode = os_win_mode_from_attrs(bh.dwFileAttributes, info->is_dir);
    if (ftype == FILE_TYPE_PIPE)
        info->mode |= NEVERC_OS_MODE_NAMEDPIPE;
    else if (ftype == FILE_TYPE_CHAR)
        info->mode |= NEVERC_OS_MODE_DEVICE | NEVERC_OS_MODE_CHARDEVICE;
    os_fill_base_name(info->name, sizeof(info->name), name);
    return 0;
#else
    struct stat st;
    if (stat(name, &st) != 0) return -1;
    os_fill_from_st(info, name, &st);
    return 0;
#endif
}

int neverc_os_lstat(const char *name, neverc_os_fileinfo_t *info) {
    if (!name || !info) return -1;
    memset(info, 0, sizeof(*info));
#if defined(NEVERC_PLATFORM_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExA(name, GetFileExInfoStandard, &attr))
        return os_win_fail();
    LARGE_INTEGER sz;
    sz.HighPart = (LONG)attr.nFileSizeHigh;
    sz.LowPart = attr.nFileSizeLow;
    info->size = sz.QuadPart;
    info->mod_time = os_filetime_unix(attr.ftLastWriteTime);
    int reparse = (attr.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    info->is_dir = !reparse &&
                   (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info->mode = os_win_mode_from_attrs(attr.dwFileAttributes, info->is_dir);
    if (reparse) info->mode |= NEVERC_OS_MODE_SYMLINK;
    os_fill_base_name(info->name, sizeof(info->name), name);
    return 0;
#else
    struct stat st;
    if (lstat(name, &st) != 0) return -1;
    os_fill_from_st(info, name, &st);
    return 0;
#endif
}

int neverc_os_exists(const char *name) {
    if (!name) return 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return lstat(name, &st) == 0;
#endif
}

int neverc_os_is_dir(const char *name) {
    neverc_os_fileinfo_t info;
    if (neverc_os_stat(name, &info) != 0) return 0;
    return info.is_dir;
}

/* ---- Process ---- */

int neverc_os_getpid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}

int neverc_os_getppid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    DWORD pid;
    DWORD ppid = 0;
    int found = 0;
    if (snap == INVALID_HANDLE_VALUE)
        return -1;
    pe.dwSize = sizeof(pe);
    pid = GetCurrentProcessId();
    if (Process32First(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                ppid = pe.th32ParentProcessID;
                found = 1;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found ? (int)ppid : -1;
#else
    return (int)getppid();
#endif
}

int neverc_os_getuid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return -1;
#else
    return (int)getuid();
#endif
}

int neverc_os_getgid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return -1;
#else
    return (int)getgid();
#endif
}

void neverc_os_exit(int code) {
    exit(code);
}

/* ---- Temp ---- */

int neverc_os_temp_dir(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    DWORD n = GetTempPathA((DWORD)cap, buf);
    return n > 0 && (size_t)n < cap ? 0 : -1;
#else
    const char *tmp = getenv("TMPDIR");
    if (!tmp || tmp[0] == '\0') tmp = "/tmp";
    size_t len = strlen(tmp);
    if (len >= cap) return -1;
    memcpy(buf, tmp, len + 1);
    return 0;
#endif
}

static int os_temp_pattern_ok(const char *pattern) {
    if (!pattern) return 1;
    for (const char *p = pattern; *p; p++) {
        if (*p == '/' || *p == '\\') return 0;
    }
    return 1;
}

/* Go prefixAndSuffix: the last '*' is replaced by the random string. */
static void os_temp_prefix_suffix(const char *pattern,
                                  const char **prefix, size_t *plen,
                                  const char **suffix, size_t *slen) {
    size_t n = strlen(pattern);
    size_t star = (size_t)-1;
    for (size_t i = 0; i < n; i++) {
        if (pattern[i] == '*') star = i;
    }
    if (star == (size_t)-1) {
        *prefix = pattern;
        *plen = n;
        *suffix = "";
        *slen = 0;
    } else {
        *prefix = pattern;
        *plen = star;
        *suffix = pattern + star + 1;
        *slen = n - star - 1;
    }
}

neverc_os_file_t *neverc_os_create_temp(const char *dir, const char *pattern) {
    char path[4096];
    char tmpdir[1024];
    if (!dir || dir[0] == '\0') {
        if (neverc_os_temp_dir(tmpdir, sizeof(tmpdir)) != 0)
            return NULL;
        dir = tmpdir;
    }
    if (!pattern) pattern = "neverc_tmp_";
    if (!os_temp_pattern_ok(pattern)) return NULL;

    for (int attempt = 0; attempt < 100; ++attempt) {
        unsigned char rnd[8];
        if (NCI_OS_RANDOM(rnd, sizeof(rnd)) != 0)
            return NULL;
        char hex[17];
        for (int i = 0; i < 8; i++) {
            static const char h[] = "0123456789abcdef";
            hex[i*2] = h[rnd[i]>>4];
            hex[i*2+1] = h[rnd[i]&0xf];
        }
        hex[16] = '\0';

        const char *prefix, *suffix;
        size_t plen, slen;
        os_temp_prefix_suffix(pattern, &prefix, &plen, &suffix, &slen);
#if defined(NEVERC_PLATFORM_WINDOWS)
        {
            char leaf[512];
            int leaf_n = snprintf(leaf, sizeof(leaf), "%.*s%s%.*s",
                                  (int)plen, prefix, hex, (int)slen, suffix);
            int n;
            if (leaf_n < 0 || (size_t)leaf_n >= sizeof(leaf))
                return NULL;
            n = os_win_join_child(path, sizeof(path), dir, leaf);
            if (n < 0 || (size_t)n >= sizeof(path))
                return NULL;
        }
#else
        int n = snprintf(path, sizeof(path), "%s/%.*s%s%.*s",
                         dir, (int)plen, prefix, hex, (int)slen, suffix);
        if (n < 0 || (size_t)n >= sizeof(path))
            return NULL;
#endif
        neverc_os_file_t *file = neverc_os_open(
            path, NEVERC_OS_O_RDWR | NEVERC_OS_O_CREATE | NEVERC_OS_O_EXCL,
            0600);
        if (file) return file;
        if (errno != EEXIST)
            return NULL;
    }
    return NULL;
}

/* ---- Extended environment ---- */

int neverc_os_lookup_env(const char *key, const char **value) {
    const char *v = neverc_os_getenv(key);
    if (value) *value = v;
    return v != NULL;
}

char **neverc_os_environ(int *count) {
    if (!count) return NULL;
    *count = 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    char *env = GetEnvironmentStringsA();
    if (!env) return NULL;
    size_t n = 0;
    char *p = env;
    while (*p) {
        if (n == (size_t)INT_MAX) { FreeEnvironmentStringsA(env); return NULL; }
        n++;
        p += strlen(p) + 1;
    }
    if (n > SIZE_MAX / sizeof(char *) - 1) {
        FreeEnvironmentStringsA(env);
        return NULL;
    }
    char **result = (char **)malloc((n + 1) * sizeof(char *));
    if (!result) { FreeEnvironmentStringsA(env); return NULL; }
    p = env;
    size_t i = 0;
    for (; i < n; i++) {
        result[i] = strdup(p);
        if (!result[i]) {
            while (i > 0) free(result[--i]);
            free(result);
            FreeEnvironmentStringsA(env);
            return NULL;
        }
        p += strlen(p) + 1;
    }
    result[n] = NULL;
    *count = (int)n;
    FreeEnvironmentStringsA(env);
    return result;
#else
    extern char **environ;
    if (!environ) return NULL;
    size_t n = 0;
    while (environ[n]) {
        if (n == (size_t)INT_MAX) return NULL;
        n++;
    }
    if (n > SIZE_MAX / sizeof(char *) - 1) return NULL;
    char **result = (char **)malloc((n + 1) * sizeof(char *));
    if (!result) return NULL;
    size_t i = 0;
    for (; i < n; i++) {
        result[i] = strdup(environ[i]);
        if (!result[i]) {
            while (i > 0) free(result[--i]);
            free(result);
            return NULL;
        }
    }
    result[n] = NULL;
    *count = (int)n;
    return result;
#endif
}

void neverc_os_clearenv(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    char *env = GetEnvironmentStringsA();
    if (!env) return;
    char *p = env;
    while (*p) {
        char *eq = strchr(p, '=');
        if (eq && eq != p) {
            size_t nlen = (size_t)(eq - p);
            if (nlen == SIZE_MAX) break;
            char stack_name[256];
            char *name = nlen < sizeof(stack_name)
                             ? stack_name
                             : (char *)malloc(nlen + 1);
            if (name) {
                memcpy(name, p, nlen);
                name[nlen] = '\0';
                SetEnvironmentVariableA(name, NULL);
                if (name != stack_name) free(name);
            }
        }
        p += strlen(p) + 1;
    }
    FreeEnvironmentStringsA(env);
#else
    extern char **environ;
    while (environ && environ[0]) {
        char *eq = strchr(environ[0], '=');
        if (!eq || eq == environ[0]) break;
        size_t nlen = (size_t)(eq - environ[0]);
        char stack_name[256];
        char *name = nlen < sizeof(stack_name)
                         ? stack_name
                         : (char *)malloc(nlen + 1);
        if (!name) break;
        memcpy(name, environ[0], nlen);
        name[nlen] = '\0';
        int rc = unsetenv(name);
        if (name != stack_name) free(name);
        if (rc != 0) break;
    }
#endif
}

static int os_buffer_append(char **buffer, size_t *length, size_t *capacity,
                            const char *data, size_t data_length) {
    if (*length == SIZE_MAX || data_length > SIZE_MAX - *length - 1) return -1;
    size_t needed = *length + data_length + 1;
    if (needed > *capacity) {
        size_t new_capacity = *capacity;
        while (new_capacity < needed) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = needed;
                break;
            }
            new_capacity *= 2;
        }
        char *grown = (char *)realloc(*buffer, new_capacity);
        if (!grown) return -1;
        *buffer = grown;
        *capacity = new_capacity;
    }
    if (data_length != 0) memcpy(*buffer + *length, data, data_length);
    *length += data_length;
    return 0;
}

char *neverc_os_expand_env(const char *s) {
    if (!s) return NULL;
    size_t cap = 256, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    const char *p = s;
    while (*p) {
        if (*p == '$') {
            p++;
            const char *name = p;
            size_t name_length = 0;
            if (*p == '{') {
                p++;
                name = p;
                while (*p && *p != '}') p++;
                if (*p != '}') {
                    if (os_buffer_append(&out, &len, &cap, "${", 2) != 0 ||
                        os_buffer_append(&out, &len, &cap, name,
                                         (size_t)(p - name)) != 0) {
                        free(out);
                        return NULL;
                    }
                    break;
                }
                name_length = (size_t)(p - name);
                p++;
            } else {
                while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                       (*p >= '0' && *p <= '9') || *p == '_')
                    p++;
                name_length = (size_t)(p - name);
                if (name_length == 0) {
                    if (os_buffer_append(&out, &len, &cap, "$", 1) != 0) {
                        free(out);
                        return NULL;
                    }
                    continue;
                }
            }
            if (name_length == SIZE_MAX) { free(out); return NULL; }
            char *varname = (char *)malloc(name_length + 1);
            if (!varname) { free(out); return NULL; }
            memcpy(varname, name, name_length);
            varname[name_length] = '\0';
            const char *val = neverc_os_getenv(varname);
            free(varname);
            if (val) {
                size_t vallen = strlen(val);
                if (os_buffer_append(&out, &len, &cap, val, vallen) != 0) {
                    free(out);
                    return NULL;
                }
            }
        } else {
            if (os_buffer_append(&out, &len, &cap, p, 1) != 0) {
                free(out);
                return NULL;
            }
            p++;
        }
    }
    out[len] = '\0';
    return out;
}

/* ---- Extended file operations ---- */

int neverc_os_chmod(const char *name, uint32_t mode) {
    if (!name) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    int p = 0;
    if (mode & 0444) p |= _S_IREAD;
    if (mode & 0222) p |= _S_IWRITE;
    return _chmod(name, p) == 0 ? 0 : -1;
#else
    return chmod(name, (mode_t)mode) == 0 ? 0 : -1;
#endif
}

int neverc_os_truncate(const char *name, int64_t size) {
    if (!name || size < 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return os_win_fail();
    LARGE_INTEGER li; li.QuadPart = size;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
        os_win_set_errno();
        CloseHandle(h);
        return -1;
    }
    CloseHandle(h);
    return 0;
#else
    if ((off_t)size != size) return -1;
    return truncate(name, (off_t)size) == 0 ? 0 : -1;
#endif
}

int neverc_os_link(const char *oldname, const char *newname) {
    if (!oldname || !newname) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return CreateHardLinkA(newname, oldname, NULL) ? 0 : os_win_fail();
#else
    return link(oldname, newname) == 0 ? 0 : -1;
#endif
}

int neverc_os_symlink(const char *oldname, const char *newname) {
    if (!oldname || !newname) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    {
        char slashed[4096];
        char destpath[4096];
        size_t n = strlen(oldname);
        size_t i;
        DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        DWORD attr;
        if (n >= sizeof(slashed)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(slashed, oldname, n + 1);
        for (i = 0; i < n; i++) {
            if (slashed[i] == '/')
                slashed[i] = '\\';
        }
        if (os_win_symlink_destpath(slashed, newname, destpath,
                                    sizeof(destpath)) != 0) {
            errno = ENAMETOOLONG;
            return -1;
        }
        attr = GetFileAttributesA(destpath);
        if (attr != INVALID_FILE_ATTRIBUTES &&
            (attr & FILE_ATTRIBUTE_DIRECTORY))
            flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
        if (CreateSymbolicLinkA(newname, slashed, flags))
            return 0;
        flags &= ~(DWORD)SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
        return CreateSymbolicLinkA(newname, slashed, flags) ? 0 : os_win_fail();
    }
#else
    return symlink(oldname, newname) == 0 ? 0 : -1;
#endif
}

int neverc_os_readlink(const char *name, char *buf, size_t cap) {
    if (!name || !buf || cap == 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK (0xA000000CL)
#endif
#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT (0xA0000003L)
#endif
    HANDLE h = CreateFileA(name, FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) return os_win_fail();
    BYTE data[16384];
    DWORD returned = 0;
    if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, data, sizeof(data),
                         &returned, NULL)) {
        CloseHandle(h);
        return os_win_fail();
    }
    CloseHandle(h);

    typedef struct {
        ULONG ReparseTag;
        USHORT ReparseDataLength;
        USHORT Reserved;
        union {
            struct {
                USHORT SubstituteNameOffset;
                USHORT SubstituteNameLength;
                USHORT PrintNameOffset;
                USHORT PrintNameLength;
                ULONG Flags;
                WCHAR PathBuffer[1];
            } SymbolicLinkReparseBuffer;
            struct {
                USHORT SubstituteNameOffset;
                USHORT SubstituteNameLength;
                USHORT PrintNameOffset;
                USHORT PrintNameLength;
                WCHAR PathBuffer[1];
            } MountPointReparseBuffer;
        };
    } os_reparse_data_t;

    os_reparse_data_t *rp = (os_reparse_data_t *)data;
    const WCHAR *wsrc = NULL;
    size_t wlen = 0;
    const BYTE *base = data;
    const BYTE *pathbuf = NULL;
    USHORT off = 0, nlen = 0;
    if (returned < 8) {
        errno = EINVAL;
        return -1;
    }
    if (rp->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        off = rp->SymbolicLinkReparseBuffer.PrintNameOffset;
        nlen = rp->SymbolicLinkReparseBuffer.PrintNameLength;
        if (nlen == 0) {
            off = rp->SymbolicLinkReparseBuffer.SubstituteNameOffset;
            nlen = rp->SymbolicLinkReparseBuffer.SubstituteNameLength;
        }
        pathbuf = (const BYTE *)rp->SymbolicLinkReparseBuffer.PathBuffer;
    } else if (rp->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        off = rp->MountPointReparseBuffer.PrintNameOffset;
        nlen = rp->MountPointReparseBuffer.PrintNameLength;
        if (nlen == 0) {
            off = rp->MountPointReparseBuffer.SubstituteNameOffset;
            nlen = rp->MountPointReparseBuffer.SubstituteNameLength;
        }
        pathbuf = (const BYTE *)rp->MountPointReparseBuffer.PathBuffer;
    } else {
        errno = EINVAL;
        return -1;
    }
    if (!pathbuf || pathbuf < base || pathbuf > base + returned) {
        errno = EINVAL;
        return -1;
    }
    {
        size_t room = (size_t)(base + returned - pathbuf);
        if ((size_t)off > room || (size_t)nlen > room - (size_t)off) {
            errno = EINVAL;
            return -1;
        }
    }
    if (nlen == 0 || (nlen % sizeof(WCHAR)) != 0) {
        errno = EINVAL;
        return -1;
    }
    wsrc = (const WCHAR *)(pathbuf + off);
    wlen = (size_t)nlen / sizeof(WCHAR);
    if (!wsrc || wlen == 0 || wlen > INT_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (cap > INT_MAX) cap = INT_MAX;
    int n = WideCharToMultiByte(CP_ACP, 0, wsrc, (int)wlen, buf, (int)cap - 1,
                                NULL, NULL);
    if (n <= 0 || (size_t)n >= cap) return os_win_fail();
    buf[n] = '\0';
    /* Go os.normaliseLinkPath: \??\C:\foo -> C:\foo, \??\UNC\x -> \\x,
     * \??\Volume{guid}\ -> \\?\Volume{guid}\ (not a cwd-relative name). */
    if (strncmp(buf, "\\??\\UNC\\", 8) == 0 || strncmp(buf, "\\\\?\\UNC\\", 8) == 0) {
        buf[0] = '\\';
        buf[1] = '\\';
        memmove(buf + 2, buf + 8, strlen(buf + 8) + 1);
    } else if ((strncmp(buf, "\\??\\", 4) == 0 || strncmp(buf, "\\\\?\\", 4) == 0) &&
               buf[4] != '\0' && buf[5] == ':') {
        memmove(buf, buf + 4, strlen(buf + 4) + 1);
    } else if (strncmp(buf, "\\??\\", 4) == 0) {
        buf[1] = '\\';
    }
    return 0;
#else
    ssize_t n = readlink(name, buf, cap);
    if (n < 0 || (size_t)n >= cap) return -1;
    buf[n] = '\0';
    return 0;
#endif
}

int neverc_os_read_dir(const char *dirname, neverc_os_dir_entry_t **entries,
                       size_t *count) {
    if (!dirname || dirname[0] == '\0' || !entries || !count) return -1;
    *entries = NULL;
    *count = 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (os_win_has_wildcards(dirname)) {
        errno = EINVAL;
        return -1;
    }
#endif
    size_t cap = 16;
    neverc_os_dir_entry_t *arr = (neverc_os_dir_entry_t *)malloc(cap * sizeof(*arr));
    if (!arr) return -1;
    size_t length = 0;

#if defined(NEVERC_PLATFORM_WINDOWS)
    char prepared[4096];
    char pattern[4096];
    const char *diruse;
    if (os_win_prepare_path(dirname, prepared, sizeof(prepared), &diruse) != 0) {
        free(arr);
        errno = ENAMETOOLONG;
        return -1;
    }
    if (os_win_path_has_dotdot_component(diruse)) {
        free(arr);
        errno = EINVAL;
        return -1;
    }
    if (os_win_dir_star(pattern, sizeof(pattern), diruse) != 0) {
        free(arr);
        return -1;
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { free(arr); return os_win_fail(); }
    do {
        if (!os_win_entry_name_ok(fd.cFileName)) continue;
        if (length >= cap) {
            if (cap > SIZE_MAX / 2 || cap * 2 > SIZE_MAX / sizeof(*arr)) {
                FindClose(h); free(arr); return -1;
            }
            size_t new_cap = cap * 2;
            neverc_os_dir_entry_t *grown = (neverc_os_dir_entry_t *)realloc(
                arr, new_cap * sizeof(*arr));
            if (!grown) { FindClose(h); free(arr); return -1; }
            arr = grown;
            cap = new_cap;
        }
        memset(&arr[length], 0, sizeof(arr[length]));
        strncpy(arr[length].name, fd.cFileName, sizeof(arr[length].name) - 1);
        {
            int reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            arr[length].is_dir = !reparse &&
                (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }
        length++;
    } while (FindNextFileA(h, &fd));
    DWORD err = GetLastError();
    FindClose(h);
    if (err != ERROR_NO_MORE_FILES) {
        free(arr);
        SetLastError(err);
        return os_win_fail();
    }
#else
    DIR *d = opendir(dirname);
    if (!d) { free(arr); return -1; }
    for (;;) {
        errno = 0;
        struct dirent *ent = readdir(d);
        if (!ent) {
            if (errno != 0) { closedir(d); free(arr); return -1; }
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (length >= cap) {
            if (cap > SIZE_MAX / 2 || cap * 2 > SIZE_MAX / sizeof(*arr)) {
                closedir(d); free(arr); return -1;
            }
            size_t new_cap = cap * 2;
            neverc_os_dir_entry_t *grown = (neverc_os_dir_entry_t *)realloc(
                arr, new_cap * sizeof(*arr));
            if (!grown) { closedir(d); free(arr); return -1; }
            arr = grown;
            cap = new_cap;
        }
        memset(&arr[length], 0, sizeof(arr[length]));
        strncpy(arr[length].name, ent->d_name, sizeof(arr[length].name) - 1);
        if (ent->d_type == DT_DIR) {
            arr[length].is_dir = 1;
        } else if (ent->d_type == DT_UNKNOWN) {
            char full[4096];
            int n = snprintf(full, sizeof(full), "%s/%s", dirname, ent->d_name);
            if (n > 0 && (size_t)n < sizeof(full)) {
                struct stat st;
                if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode))
                    arr[length].is_dir = 1;
            }
        }
        length++;
    }
    if (closedir(d) != 0) { free(arr); return -1; }
#endif
    if (length > 1)
        qsort(arr, length, sizeof(*arr), os_dir_entry_cmp);
    *entries = arr;
    *count = length;
    return 0;
}

int neverc_os_mkdir_temp(const char *dir, const char *pattern,
                         char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
    char tmpdir[1024];
    if (!dir || dir[0] == '\0') {
        if (neverc_os_temp_dir(tmpdir, sizeof(tmpdir)) != 0)
            return -1;
        dir = tmpdir;
    }
    if (!pattern) pattern = "neverc_tmp_";
    if (!os_temp_pattern_ok(pattern)) return -1;

    for (int attempt = 0; attempt < 100; ++attempt) {
        unsigned char rnd[8];
        if (NCI_OS_RANDOM(rnd, sizeof(rnd)) != 0)
            return -1;
        char hex[17];
        for (int i = 0; i < 8; i++) {
            static const char h[] = "0123456789abcdef";
            hex[i*2] = h[rnd[i]>>4];
            hex[i*2+1] = h[rnd[i]&0xf];
        }
        hex[16] = '\0';

        const char *prefix, *suffix;
        size_t plen, slen;
        os_temp_prefix_suffix(pattern, &prefix, &plen, &suffix, &slen);
#if defined(NEVERC_PLATFORM_WINDOWS)
        {
            char leaf[512];
            int leaf_n = snprintf(leaf, sizeof(leaf), "%.*s%s%.*s",
                                  (int)plen, prefix, hex, (int)slen, suffix);
            int n;
            if (leaf_n < 0 || (size_t)leaf_n >= sizeof(leaf))
                return -1;
            n = os_win_join_child(buf, cap, dir, leaf);
            if (n < 0 || (size_t)n >= cap)
                return -1;
        }
#else
        int n = snprintf(buf, cap, "%s/%.*s%s%.*s",
                         dir, (int)plen, prefix, hex, (int)slen, suffix);
        if (n < 0 || (size_t)n >= cap)
            return -1;
#endif
        if (neverc_os_mkdir(buf, 0700) == 0)
            return 0;
        if (errno != EEXIST)
            return -1;
    }
    return -1;
}

int neverc_os_getegid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return -1;
#else
    return (int)getegid();
#endif
}

int neverc_os_geteuid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return -1;
#else
    return (int)geteuid();
#endif
}

static int os_copy_cstr(char *buf, size_t cap, const char *src) {
    if (!buf || cap == 0 || !src || src[0] == '\0') return -1;
    size_t n = strlen(src);
    if (n >= cap) return -1;
    memcpy(buf, src, n + 1);
    return 0;
}

static int os_format_under(char *buf, size_t cap, const char *fmt, const char *a) {
    if (!buf || cap == 0) return -1;
    int n = snprintf(buf, cap, fmt, a);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

/* Go os.UserCacheDir / UserConfigDir: the constructed path must be absolute. */
static int os_path_isabs(const char *p) {
    if (!p || !p[0]) return 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if ((p[0] == '\\' && p[1] == '\\') || (p[0] == '/' && p[1] == '/'))
        return 1;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
        p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
        return 1;
    return 0;
#else
    return p[0] == '/';
#endif
}

static int os_require_abs(char *buf) {
    if (!os_path_isabs(buf)) {
        if (buf) buf[0] = '\0';
        return -1;
    }
    return 0;
}

int neverc_os_executable(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (cap > 0xFFFFFFFFUL) return -1;
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)cap);
    return n > 0 && n < (DWORD)cap ? 0 : -1;
#elif defined(NEVERC_PLATFORM_APPLE)
    uint32_t size = (uint32_t)cap;
    return _NSGetExecutablePath(buf, &size) == 0 ? 0 : -1;
#elif defined(NEVERC_PLATFORM_LINUX)
    ssize_t n = readlink("/proc/self/exe", buf, cap);
    if (n < 0 || (size_t)n >= cap) return -1;
    buf[n] = '\0';
    return 0;
#else
    return -1;
#endif
}

int neverc_os_user_home_dir(char *buf, size_t cap) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return os_copy_cstr(buf, cap, neverc_os_getenv("USERPROFILE"));
#else
    return os_copy_cstr(buf, cap, getenv("HOME"));
#endif
}

int neverc_os_user_cache_dir(char *buf, size_t cap) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (os_copy_cstr(buf, cap, neverc_os_getenv("LOCALAPPDATA")) != 0)
        return -1;
    return os_require_abs(buf);
#elif defined(NEVERC_PLATFORM_APPLE)
    char home[1024];
    if (neverc_os_user_home_dir(home, sizeof(home)) < 0) return -1;
    if (os_format_under(buf, cap, "%s/Library/Caches", home) != 0)
        return -1;
    return os_require_abs(buf);
#else
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0]) {
        if (xdg[0] != '/') return -1;
        if (os_copy_cstr(buf, cap, xdg) != 0) return -1;
        return os_require_abs(buf);
    }
    char home[1024];
    if (neverc_os_user_home_dir(home, sizeof(home)) < 0) return -1;
    if (os_format_under(buf, cap, "%s/.cache", home) != 0) return -1;
    return os_require_abs(buf);
#endif
}

int neverc_os_user_config_dir(char *buf, size_t cap) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (os_copy_cstr(buf, cap, neverc_os_getenv("APPDATA")) != 0)
        return -1;
    return os_require_abs(buf);
#elif defined(NEVERC_PLATFORM_APPLE)
    char home[1024];
    if (neverc_os_user_home_dir(home, sizeof(home)) < 0) return -1;
    if (os_format_under(buf, cap, "%s/Library/Application Support", home) != 0)
        return -1;
    return os_require_abs(buf);
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        if (xdg[0] != '/') return -1;
        if (os_copy_cstr(buf, cap, xdg) != 0) return -1;
        return os_require_abs(buf);
    }
    char home[1024];
    if (neverc_os_user_home_dir(home, sizeof(home)) < 0) return -1;
    if (os_format_under(buf, cap, "%s/.config", home) != 0) return -1;
    return os_require_abs(buf);
#endif
}

static void close_file_descriptor(int fd) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    _close(fd);
#else
    close(fd);
#endif
}

static neverc_os_file_t *file_from_descriptor(int fd, const char *mode) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    FILE *fp = _fdopen(fd, mode);
#else
    FILE *fp = fdopen(fd, mode);
#endif
    if (!fp) {
        close_file_descriptor(fd);
        return NULL;
    }

    neverc_os_file_t *file = (neverc_os_file_t *)calloc(1, sizeof(*file));
    if (!file) {
        fclose(fp);
        return NULL;
    }
    file->fp = fp;
    file->fd = fd;
    if (setvbuf(fp, NULL, _IONBF, 0) != 0) {
        neverc_os_close(file);
        return NULL;
    }
    return file;
}

int neverc_os_pipe(neverc_os_file_t **r, neverc_os_file_t **w) {
    if (!r || !w || r == w) return -1;
    *r = NULL;
    *w = NULL;

    int rfd, wfd;
#if defined(NEVERC_PLATFORM_WINDOWS)
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, NULL, 0)) return -1;
    rfd = _open_osfhandle((intptr_t)hRead, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
    if (rfd < 0) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }
    wfd = _open_osfhandle((intptr_t)hWrite, _O_WRONLY | _O_BINARY | _O_NOINHERIT);
    if (wfd < 0) {
        close_file_descriptor(rfd);
        CloseHandle(hWrite);
        return -1;
    }
#else
    int fds[2];
    if (pipe(fds) < 0) return -1;
    rfd = fds[0];
    wfd = fds[1];
#ifdef FD_CLOEXEC
    if (fcntl(rfd, F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(wfd, F_SETFD, FD_CLOEXEC) != 0) {
        close_file_descriptor(rfd);
        close_file_descriptor(wfd);
        return -1;
    }
#endif
#endif

    neverc_os_file_t *reader = file_from_descriptor(rfd, "rb");
    if (!reader) {
        close_file_descriptor(wfd);
        return -1;
    }
    neverc_os_file_t *writer = file_from_descriptor(wfd, "wb");
    if (!writer) {
        neverc_os_close(reader);
        return -1;
    }

    *r = reader;
    *w = writer;
    return 0;
}

int neverc_os_chown(const char *name, int uid, int gid) {
    if (!name) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    (void)uid; (void)gid;
    return -1;
#else
    return chown(name, (uid_t)uid, (gid_t)gid) == 0 ? 0 : -1;
#endif
}

int neverc_os_lchown(const char *name, int uid, int gid) {
    if (!name) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    (void)uid; (void)gid;
    return -1;
#else
    return lchown(name, (uid_t)uid, (gid_t)gid) == 0 ? 0 : -1;
#endif
}

int neverc_os_is_exist(int err) { return err == EEXIST; }
int neverc_os_is_not_exist(int err) { return err == ENOENT; }
int neverc_os_is_permission(int err) { return err == EACCES || err == EPERM; }
