#include "neverc/std/os.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>

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
    const char *mode;
    int rw = flags & 0x03;
    int create = flags & NEVERC_OS_O_CREATE;
    int trunc = flags & NEVERC_OS_O_TRUNC;
    int append = flags & NEVERC_OS_O_APPEND;

    if (rw == NEVERC_OS_O_RDONLY) mode = "rb";
    else if (append) mode = create ? "ab+" : "ab";
    else if (trunc || create) mode = (rw == NEVERC_OS_O_RDWR) ? "wb+" : "wb";
    else mode = (rw == NEVERC_OS_O_RDWR) ? "rb+" : "wb";

    FILE *fp = fopen(name, mode);
    if (!fp) return NULL;

    neverc_os_file_t *f = (neverc_os_file_t*)calloc(1, sizeof(*f));
    if (!f) { fclose(fp); return NULL; }
    f->fp = fp;
    f->fd = fileno(fp);
    (void)perm;
    return f;
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
    if (!name || !data) return -1;
    (void)perm;
    FILE *fp = fopen(name, "wb");
    if (!fp) return -1;
    size_t n = fwrite(data, 1, len, fp);
    fclose(fp);
    return n == len ? 0 : -1;
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

int neverc_os_remove_all(const char *path) {
    if (!path) return -1;
    if (!neverc_os_exists(path)) return 0;
    if (!neverc_os_is_dir(path)) return remove(path);

#if defined(NEVERC_PLATFORM_WINDOWS)
    WIN32_FIND_DATAA fd;
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
        neverc_os_remove_all(child);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return _rmdir(path);
#else
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[4096];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        neverc_os_remove_all(child);
    }
    closedir(d);
    return rmdir(path);
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
    return n > 0 ? 0 : -1;
#else
    const char *tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    strncpy(buf, tmp, cap - 1);
    buf[cap-1] = '\0';
    return 0;
#endif
}

neverc_os_file_t *neverc_os_create_temp(const char *dir, const char *pattern) {
    char path[4096];
    char tmpdir[1024];
    if (!dir || dir[0] == '\0') {
        neverc_os_temp_dir(tmpdir, sizeof(tmpdir));
        dir = tmpdir;
    }
    if (!pattern) pattern = "neverc_tmp_";

    unsigned char rnd[8];
    neverc_platform_random(rnd, sizeof(rnd));
    char hex[17];
    for (int i = 0; i < 8; i++) {
        static const char h[] = "0123456789abcdef";
        hex[i*2] = h[rnd[i]>>4];
        hex[i*2+1] = h[rnd[i]&0xf];
    }
    hex[16] = '\0';

    snprintf(path, sizeof(path), "%s/%s%s", dir, pattern, hex);
    return neverc_os_create(path);
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
            char name[256];
            size_t nlen = (size_t)(eq - p);
            if (nlen < sizeof(name)) {
                memcpy(name, p, nlen);
                name[nlen] = '\0';
                SetEnvironmentVariableA(name, NULL);
            }
        }
        p += strlen(p) + 1;
    }
    FreeEnvironmentStringsA(env);
#else
    extern char **environ;
    if (environ) environ[0] = NULL;
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
    char tmpdir[1024];
    if (!dir || dir[0] == '\0') {
        neverc_os_temp_dir(tmpdir, sizeof(tmpdir));
        dir = tmpdir;
    }
    if (!pattern) pattern = "neverc_tmp_";

    unsigned char rnd[8];
    neverc_platform_random(rnd, sizeof(rnd));
    char hex[17];
    for (int i = 0; i < 8; i++) {
        static const char h[] = "0123456789abcdef";
        hex[i*2] = h[rnd[i]>>4];
        hex[i*2+1] = h[rnd[i]&0xf];
    }
    hex[16] = '\0';

    snprintf(buf, cap, "%s/%s%s", dir, pattern, hex);
    return neverc_os_mkdir(buf, 0700);
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
