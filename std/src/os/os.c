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
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/types.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <sys/stat.h>
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

#if defined(NEVERC_PLATFORM_WINDOWS)
/* getenv() reads a CRT snapshot; SetEnvironmentVariableA updates the process
   environment block directly.  Route all NeverC os env reads through Win32 so
   neverc_os_setenv/unsetenv are immediately visible to neverc_os_getenv. */
static char neverc_os_getenv_buf[32768];
#endif

const char *neverc_os_getenv(const char *key) {
    if (!key) return NULL;
#if defined(NEVERC_PLATFORM_WINDOWS)
    DWORD n = GetEnvironmentVariableA(key, neverc_os_getenv_buf,
                                    (DWORD)sizeof(neverc_os_getenv_buf));
    if (n == 0 || n >= sizeof(neverc_os_getenv_buf)) return NULL;
    return neverc_os_getenv_buf;
#else
    return getenv(key);
#endif
}

int neverc_os_setenv(const char *key, const char *value) {
    if (!key || !value) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return SetEnvironmentVariableA(key, value) ? 0 : -1;
#else
    return setenv(key, value, 1);
#endif
}

int neverc_os_unsetenv(const char *key) {
    if (!key) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return SetEnvironmentVariableA(key, NULL) ? 0 : -1;
#else
    return unsetenv(key);
#endif
}

int neverc_os_hostname(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    DWORD sz = (DWORD)cap;
    return GetComputerNameA(buf, &sz) ? 0 : -1;
#else
    return gethostname(buf, cap);
#endif
}

/* ---- Working Directory ---- */

int neverc_os_getwd(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return _getcwd(buf, (int)cap) ? 0 : -1;
#else
    return getcwd(buf, cap) ? 0 : -1;
#endif
}

int neverc_os_chdir(const char *dir) {
    if (!dir) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return _chdir(dir);
#else
    return chdir(dir);
#endif
}

/* ---- File Operations ---- */

neverc_os_file_t *neverc_os_open(const char *name, int flags, uint32_t perm) {
    if (!name) return NULL;
    int rw = flags & 0x03;
    if (rw != NEVERC_OS_O_RDONLY && rw != NEVERC_OS_O_WRONLY &&
        rw != NEVERC_OS_O_RDWR)
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
    if (n == 0 && ferror(f->fp)) return -1;
    return (int)n;
}

int neverc_os_write(neverc_os_file_t *f, const void *buf, size_t count) {
    if (!f || !f->fp || (!buf && count != 0) || count > INT_MAX) return -1;
    size_t n = fwrite(buf, 1, count, f->fp);
    if (n == 0 && ferror(f->fp)) return -1;
    return (int)n;
}

int64_t neverc_os_seek(neverc_os_file_t *f, int64_t offset, int whence) {
    if (!f || !f->fp) return -1;
    if (fseek(f->fp, (long)offset, whence) != 0) return -1;
    return (int64_t)ftell(f->fp);
}

int neverc_os_sync(neverc_os_file_t *f) {
    if (!f || !f->fp) return -1;
    if (fflush(f->fp) != 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return 0;
#else
    return fsync(f->fd);
#endif
}

/* ---- Convenience File Operations ---- */

int neverc_os_read_file(const char *name, unsigned char **out, size_t *out_len) {
    if (!name || !out || !out_len) return -1;
    *out = NULL;
    *out_len = 0;
    FILE *fp = fopen(name, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return -1; }
    if ((uint64_t)sz > (uint64_t)(SIZE_MAX - 1)) { fclose(fp); return -1; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return -1; }
    unsigned char *buf = (unsigned char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return -1; }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    if (n != (size_t)sz && ferror(fp)) {
        free(buf);
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) { free(buf); return -1; }
    buf[n] = '\0';
    *out = buf;
    *out_len = n;
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
    if (!name) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    (void)perm;
    return _mkdir(name);
#else
    return mkdir(name, (mode_t)perm);
#endif
}

int neverc_os_mkdir_all(const char *path, uint32_t perm) {
    if (!path) return -1;
    char buf[4096];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return -1;
    memcpy(buf, path, len + 1);

    for (size_t i = 1; i <= len; i++) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            char saved = buf[i];
            buf[i] = '\0';
            neverc_os_mkdir(buf, perm);
            buf[i] = saved;
        }
    }
    return neverc_os_is_dir(path) ? 0 : -1;
}

int neverc_os_remove(const char *name) {
    if (!name) return -1;
    return remove(name);
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

int neverc_os_remove_all(const char *path) {
    if (!path) return -1;

#if defined(NEVERC_PLATFORM_WINDOWS)
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return (error == ERROR_FILE_NOT_FOUND ||
                error == ERROR_PATH_NOT_FOUND) ? 0 : -1;
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        BOOL removed = (attrs & FILE_ATTRIBUTE_DIRECTORY)
            ? RemoveDirectoryA(path) : DeleteFileA(path);
        return removed ? 0 : -1;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return DeleteFileA(path) ? 0 : -1;

    WIN32_FIND_DATAA fd;
    char pattern[4096];
    int pattern_len = snprintf(pattern, sizeof(pattern), "%s\\*", path);
    if (pattern_len < 0 || (size_t)pattern_len >= sizeof(pattern))
        return -1;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int result = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char child[4096];
        int child_len = snprintf(
            child, sizeof(child), "%s\\%s", path, fd.cFileName);
        if (child_len < 0 || (size_t)child_len >= sizeof(child) ||
            neverc_os_remove_all(child) != 0)
            result = -1;
    } while (FindNextFileA(h, &fd));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        result = -1;
    FindClose(h);
    if (result != 0)
        return -1;
    return RemoveDirectoryA(path) ? 0 : -1;
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
    if (!oldpath || !newpath) return -1;
    return rename(oldpath, newpath);
}

/* ---- File Info ---- */

int neverc_os_stat(const char *name, neverc_os_fileinfo_t *info) {
    if (!name || !info) return -1;
    memset(info, 0, sizeof(*info));

#if defined(NEVERC_PLATFORM_WINDOWS)
    struct _stat64 st;
    if (_stat64(name, &st) != 0) return -1;
    info->size = st.st_size;
    info->mode = (uint32_t)st.st_mode;
    info->mod_time = (int64_t)st.st_mtime;
    info->is_dir = (st.st_mode & _S_IFDIR) != 0;
#else
    struct stat st;
    if (stat(name, &st) != 0) return -1;
    info->size = (int64_t)st.st_size;
    info->mode = (uint32_t)st.st_mode & 0777;
    if (S_ISDIR(st.st_mode)) info->mode |= NEVERC_OS_MODE_DIR;
    info->mod_time = (int64_t)st.st_mtime;
    info->is_dir = S_ISDIR(st.st_mode);
#endif

    const char *base = strrchr(name, '/');
#if defined(NEVERC_PLATFORM_WINDOWS)
    const char *base2 = strrchr(name, '\\');
    if (base2 && (!base || base2 > base)) base = base2;
#endif
    base = base ? base + 1 : name;
    strncpy(info->name, base, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    return 0;
}

int neverc_os_lstat(const char *name, neverc_os_fileinfo_t *info) {
    if (!name || !info) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return neverc_os_stat(name, info);
#else
    memset(info, 0, sizeof(*info));
    struct stat st;
    if (lstat(name, &st) != 0) return -1;
    info->size = (int64_t)st.st_size;
    info->mode = (uint32_t)st.st_mode & 0777;
    if (S_ISDIR(st.st_mode)) info->mode |= NEVERC_OS_MODE_DIR;
    if (S_ISLNK(st.st_mode)) info->mode |= NEVERC_OS_MODE_SYMLINK;
    info->mod_time = (int64_t)st.st_mtime;
    info->is_dir = S_ISDIR(st.st_mode);

    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    strncpy(info->name, base, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    return 0;
#endif
}

int neverc_os_exists(const char *name) {
    if (!name) return 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(name, &st) == 0;
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
    return 0;
#else
    return (int)getppid();
#endif
}

int neverc_os_getuid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return 0;
#else
    return (int)getuid();
#endif
}

int neverc_os_getgid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return 0;
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

neverc_os_file_t *neverc_os_create_temp(const char *dir, const char *pattern) {
    char path[4096];
    char tmpdir[1024];
    if (!dir || dir[0] == '\0') {
        if (neverc_os_temp_dir(tmpdir, sizeof(tmpdir)) != 0)
            return NULL;
        dir = tmpdir;
    }
    if (!pattern) pattern = "neverc_tmp_";

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

        int n = snprintf(path, sizeof(path), "%s/%s%s", dir, pattern, hex);
        if (n < 0 || (size_t)n >= sizeof(path))
            return NULL;
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
        if (!eq) break;
        size_t nlen = (size_t)(eq - environ[0]);
        char stack_name[256];
        char *name = nlen < sizeof(stack_name)
                         ? stack_name
                         : (char *)malloc(nlen + 1);
        if (!name) break;
        memcpy(name, environ[0], nlen);
        name[nlen] = '\0';
        unsetenv(name);
        if (name != stack_name) free(name);
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
    (void)name; (void)mode;
    return 0;
#else
    return chmod(name, (mode_t)mode) == 0 ? 0 : -1;
#endif
}

int neverc_os_truncate(const char *name, int64_t size) {
    if (!name || size < 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    LARGE_INTEGER li; li.QuadPart = size;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
        CloseHandle(h);
        return -1;
    }
    CloseHandle(h);
    return 0;
#else
    return truncate(name, (off_t)size) == 0 ? 0 : -1;
#endif
}

int neverc_os_link(const char *oldname, const char *newname) {
    if (!oldname || !newname) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return CreateHardLinkA(newname, oldname, NULL) ? 0 : -1;
#else
    return link(oldname, newname) == 0 ? 0 : -1;
#endif
}

int neverc_os_symlink(const char *oldname, const char *newname) {
    if (!oldname || !newname) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    DWORD attr = GetFileAttributesA(oldname);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    return CreateSymbolicLinkA(newname, oldname, flags) ? 0 : -1;
#else
    return symlink(oldname, newname) == 0 ? 0 : -1;
#endif
}

int neverc_os_readlink(const char *name, char *buf, size_t cap) {
    if (!name || !buf || cap == 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    (void)name; (void)buf; (void)cap;
    return -1;
#else
    ssize_t n = readlink(name, buf, cap - 1);
    if (n < 0) return -1;
    buf[n] = '\0';
    return 0;
#endif
}

int neverc_os_read_dir(const char *dirname, neverc_os_dir_entry_t **entries,
                       size_t *count) {
    if (!dirname || !entries || !count) return -1;
    *entries = NULL;
    *count = 0;
    size_t cap = 16;
    neverc_os_dir_entry_t *arr = (neverc_os_dir_entry_t *)malloc(cap * sizeof(*arr));
    if (!arr) return -1;
    size_t length = 0;

#if defined(NEVERC_PLATFORM_WINDOWS)
    char pattern[4096];
    int pattern_length = snprintf(pattern, sizeof(pattern), "%s\\*", dirname);
    if (pattern_length < 0 || (size_t)pattern_length >= sizeof(pattern)) {
        free(arr);
        return -1;
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { free(arr); return -1; }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
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
        arr[length].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        length++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dirname);
    if (!d) { free(arr); return -1; }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
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
        arr[length].is_dir = (ent->d_type == DT_DIR) ? 1 : 0;
        length++;
    }
    closedir(d);
#endif
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

        int n = snprintf(buf, cap, "%s/%s%s", dir, pattern, hex);
        if (n < 0 || (size_t)n >= cap)
            return -1;
        if (neverc_os_mkdir(buf, 0700) == 0)
            return 0;
        if (errno != EEXIST)
            return -1;
    }
    return -1;
}

int neverc_os_getegid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return 0;
#else
    return (int)getegid();
#endif
}

int neverc_os_geteuid(void) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return 0;
#else
    return (int)geteuid();
#endif
}

int neverc_os_executable(char *buf, size_t cap) {
    if (!buf || cap == 0) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)cap);
    return n > 0 ? 0 : -1;
#elif defined(NEVERC_PLATFORM_APPLE)
    uint32_t size = (uint32_t)cap;
    return _NSGetExecutablePath(buf, &size) == 0 ? 0 : -1;
#elif defined(NEVERC_PLATFORM_LINUX)
    ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
    if (n < 0) return -1;
    buf[n] = '\0';
    return 0;
#else
    return -1;
#endif
}

int neverc_os_user_home_dir(char *buf, size_t cap) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    const char *h = neverc_os_getenv("USERPROFILE");
    if (!h) return -1;
    strncpy(buf, h, cap - 1); buf[cap-1] = '\0';
    return 0;
#else
    const char *h = getenv("HOME");
    if (!h) return -1;
    strncpy(buf, h, cap - 1); buf[cap-1] = '\0';
    return 0;
#endif
}

int neverc_os_user_cache_dir(char *buf, size_t cap) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    const char *d = neverc_os_getenv("LOCALAPPDATA");
    if (!d) return -1;
    strncpy(buf, d, cap - 1); buf[cap-1] = '\0';
    return 0;
#elif defined(NEVERC_PLATFORM_APPLE)
    char home[1024];
    if (neverc_os_user_home_dir(home, sizeof(home)) < 0) return -1;
    snprintf(buf, cap, "%s/Library/Caches", home);
    return 0;
#else
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg) { strncpy(buf, xdg, cap - 1); buf[cap-1] = '\0'; return 0; }
    char home[1024];
    if (neverc_os_user_home_dir(home, sizeof(home)) < 0) return -1;
    snprintf(buf, cap, "%s/.cache", home);
    return 0;
#endif
}

int neverc_os_user_config_dir(char *buf, size_t cap) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    const char *d = neverc_os_getenv("APPDATA");
    if (!d) return -1;
    strncpy(buf, d, cap - 1); buf[cap-1] = '\0';
    return 0;
#elif defined(NEVERC_PLATFORM_APPLE)
    char home[1024];
    if (neverc_os_user_home_dir(home, sizeof(home)) < 0) return -1;
    snprintf(buf, cap, "%s/Library/Application Support", home);
    return 0;
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) { strncpy(buf, xdg, cap - 1); buf[cap-1] = '\0'; return 0; }
    char home[1024];
    if (neverc_os_user_home_dir(home, sizeof(home)) < 0) return -1;
    snprintf(buf, cap, "%s/.config", home);
    return 0;
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
    rfd = _open_osfhandle((intptr_t)hRead, _O_RDONLY | _O_BINARY);
    if (rfd < 0) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }
    wfd = _open_osfhandle((intptr_t)hWrite, _O_WRONLY | _O_BINARY);
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
#if defined(NEVERC_PLATFORM_WINDOWS)
    (void)name; (void)uid; (void)gid;
    return 0;
#else
    return chown(name, (uid_t)uid, (gid_t)gid) == 0 ? 0 : -1;
#endif
}

int neverc_os_lchown(const char *name, int uid, int gid) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    (void)name; (void)uid; (void)gid;
    return 0;
#else
    return lchown(name, (uid_t)uid, (gid_t)gid) == 0 ? 0 : -1;
#endif
}

int neverc_os_is_exist(int err) { return err == 17; /* EEXIST */ }
int neverc_os_is_not_exist(int err) { return err == 2; /* ENOENT */ }
int neverc_os_is_permission(int err) { return err == 13; /* EACCES */ }
