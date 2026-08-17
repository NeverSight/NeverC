#include "neverc/std/io/fs.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#if defined(NEVERC_PLATFORM_WINDOWS)
  #include <windows.h>
  #include <io.h>
  #include <sys/stat.h>
  #include <sys/types.h>
#else
  #include <sys/stat.h>
  #include <dirent.h>
  #include <unistd.h>
#endif

/* Go path.Match on a single name (io/fs.Glob). Returns 1, 0, or -1. */
static const char *fs_scan_chunk(const char *pattern, int *star,
                                 const char **chunk, size_t *clen) {
    *star = 0;
    while (*pattern == '*') {
        pattern++;
        *star = 1;
    }
    const char *start = pattern;
    int inrange = 0;
    const char *p = pattern;
    for (; *p; p++) {
        if (*p == '\\') {
            if (p[1]) p++;
            continue;
        }
        if (*p == '[') inrange = 1;
        else if (*p == ']') inrange = 0;
        else if (*p == '*' && !inrange) break;
    }
    *chunk = start;
    *clen = (size_t)(p - start);
    return p;
}

static int fs_get_esc(const char *chunk, size_t clen, size_t *i,
                      unsigned char *out) {
    if (*i >= clen || chunk[*i] == '-' || chunk[*i] == ']')
        return -1;
    if (chunk[*i] == '\\') {
        (*i)++;
        if (*i >= clen) return -1;
    }
    *out = (unsigned char)chunk[*i];
    (*i)++;
    if (*i >= clen) return -1;
    return 0;
}

static int fs_match_chunk(const char *chunk, size_t clen, const char *s,
                          const char **rest) {
    int failed = 0;
    size_t i = 0;
    while (i < clen) {
        if (!failed && *s == '\0')
            failed = 1;
        if (chunk[i] == '[') {
            unsigned char c = 0;
            if (!failed) {
                c = (unsigned char)*s;
                if (*s) s++;
            }
            i++;
            int negated = 0;
            if (i < clen && chunk[i] == '^') {
                negated = 1;
                i++;
            }
            int matched = 0;
            int nrange = 0;
            for (;;) {
                if (i < clen && chunk[i] == ']' && nrange > 0) {
                    i++;
                    break;
                }
                unsigned char lo, hi;
                if (fs_get_esc(chunk, clen, &i, &lo) != 0)
                    return -1;
                hi = lo;
                if (chunk[i] == '-') {
                    i++;
                    if (fs_get_esc(chunk, clen, &i, &hi) != 0)
                        return -1;
                }
                if (lo <= c && c <= hi)
                    matched = 1;
                nrange++;
            }
            if (matched == negated)
                failed = 1;
        } else if (chunk[i] == '?') {
            if (!failed) {
                if (*s == '/')
                    failed = 1;
                if (*s)
                    s++;
            }
            i++;
        } else {
            if (chunk[i] == '\\') {
                i++;
                if (i >= clen)
                    return -1;
            }
            if (!failed) {
                if ((unsigned char)chunk[i] != (unsigned char)*s)
                    failed = 1;
                if (*s)
                    s++;
            }
            i++;
        }
    }
    if (failed)
        return 0;
    *rest = s;
    return 1;
}

static int fs_glob_match(const char *pattern, const char *name) {
    if (!pattern || !name)
        return -1;

    while (*pattern) {
        int star = 0;
        const char *chunk = NULL;
        size_t clen = 0;
        const char *rest_pat = fs_scan_chunk(pattern, &star, &chunk, &clen);
        if (star && clen == 0) {
            while (*name) {
                if (*name == '/')
                    return 0;
                name++;
            }
            return 1;
        }

        const char *t = NULL;
        int ok = fs_match_chunk(chunk, clen, name, &t);
        if (ok < 0)
            return -1;
        if (ok && (*t == '\0' || *rest_pat != '\0')) {
            name = t;
            pattern = rest_pat;
            continue;
        }

        if (star) {
            int advanced = 0;
            const char *n;
            for (n = name; *n && *n != '/'; n++) {
                ok = fs_match_chunk(chunk, clen, n + 1, &t);
                if (ok < 0)
                    return -1;
                if (ok) {
                    if (*rest_pat == '\0' && *t != '\0')
                        continue;
                    name = t;
                    pattern = rest_pat;
                    advanced = 1;
                    break;
                }
            }
            if (advanced)
                continue;
        }

        pattern = rest_pat;
        while (*pattern) {
            rest_pat = fs_scan_chunk(pattern, &star, &chunk, &clen);
            const char *dummy = NULL;
            if (fs_match_chunk(chunk, clen, "", &dummy) < 0)
                return -1;
            pattern = rest_pat;
        }
        return 0;
    }
    return *name == '\0' ? 1 : 0;
}

static int fs_entries_reserve(neverc_fs_dir_entry_t **entries, size_t *cap,
                              size_t needed) {
    if (needed <= *cap) return 1;
    size_t next = *cap == 0 ? 8 : *cap;
    while (next < needed) {
        if (next > SIZE_MAX / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    if (next > SIZE_MAX / sizeof(**entries)) return 0;
    neverc_fs_dir_entry_t *grown = (neverc_fs_dir_entry_t *)realloc(
        *entries, next * sizeof(**entries));
    if (!grown) return 0;
    *entries = grown;
    *cap = next;
    return 1;
}

static int fs_matches_reserve(char ***matches, size_t *cap, size_t needed) {
    if (needed <= *cap) return 1;
    size_t next = *cap == 0 ? 8 : *cap;
    while (next < needed) {
        if (next > SIZE_MAX / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    if (next > SIZE_MAX / sizeof(**matches)) return 0;
    char **grown = (char **)realloc(*matches, next * sizeof(**matches));
    if (!grown) return 0;
    *matches = grown;
    *cap = next;
    return 1;
}

static int fs_is_sep(char c) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

static void fs_fill_base_name(char *dst, size_t cap, const char *name) {
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!name) return;
    size_t len = strlen(name);
    while (len > 1 && fs_is_sep(name[len - 1]))
        len--;
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (fs_is_sep(name[i]))
            start = i + 1;
    }
    size_t n = len - start;
    if (n == 0) {
        if (cap > 1 && len > 0 && fs_is_sep(name[0])) {
            dst[0] = name[0];
            dst[1] = '\0';
        }
        return;
    }
    if (n >= cap) n = cap - 1;
    memcpy(dst, name + start, n);
    dst[n] = '\0';
}

static char *fs_join_path(const char *dir, const char *name) {
    size_t dlen = strlen(dir), nlen = strlen(name);
    while (dlen > 1 && fs_is_sep(dir[dlen - 1]))
        dlen--;
    int need_sep = dlen > 0 && !fs_is_sep(dir[dlen - 1]);
    size_t extra = need_sep ? 1 : 0;
    if (dlen > SIZE_MAX - nlen - extra - 1) return NULL;
    char *full = (char *)malloc(dlen + extra + nlen + 1);
    if (!full) return NULL;
    memcpy(full, dir, dlen);
    if (need_sep) {
#if defined(NEVERC_PLATFORM_WINDOWS)
        full[dlen] = '\\';
#else
        full[dlen] = '/';
#endif
    }
    memcpy(full + dlen + extra, name, nlen + 1);
    return full;
}

static int fs_opened_size(FILE *f, uint64_t *size, int *is_dir) {
    *is_dir = 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    struct _stat64 st;
    if (_fstat64(_fileno(f), &st) != 0) return -1;
    if (st.st_mode & _S_IFDIR) {
        *is_dir = 1;
        return 0;
    }
    if (st.st_size < 0) return -1;
    *size = (uint64_t)st.st_size;
#else
    struct stat st;
    if (fstat(fileno(f), &st) != 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        *is_dir = 1;
        return 0;
    }
    if (st.st_size < 0) return -1;
    *size = (uint64_t)st.st_size;
#endif
    return 0;
}

#if defined(NEVERC_PLATFORM_WINDOWS)
static int fs_win_fail(void) {
    DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ||
        e == ERROR_INVALID_DRIVE)
        errno = ENOENT;
    else if (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION)
        errno = EACCES;
    else if (e == ERROR_FILENAME_EXCED_RANGE)
        errno = ENAMETOOLONG;
    else
        errno = EINVAL;
    return -1;
}

static time_t fs_filetime_to_time(FILETIME ft) {
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    if (ull.QuadPart < 116444736000000000ULL)
        return (time_t)0;
    return (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

static int fs_stat_win(const char *path, neverc_fs_file_info_t *info, int follow) {
    memset(info, 0, sizeof(*info));
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (!follow)
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    HANDLE h = CreateFileA(path, FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, flags, NULL);
    BY_HANDLE_FILE_INFORMATION bh;
    int have_handle = 0;
    if (h != INVALID_HANDLE_VALUE) {
        have_handle = GetFileInformationByHandle(h, &bh) ? 1 : 0;
        CloseHandle(h);
        if (!have_handle)
            return fs_win_fail();
    } else if (follow) {
        return fs_win_fail();
    } else {
        WIN32_FILE_ATTRIBUTE_DATA attr;
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attr))
            return fs_win_fail();
        bh.dwFileAttributes = attr.dwFileAttributes;
        bh.ftLastWriteTime = attr.ftLastWriteTime;
        bh.nFileSizeHigh = attr.nFileSizeHigh;
        bh.nFileSizeLow = attr.nFileSizeLow;
    }

    fs_fill_base_name(info->name, sizeof(info->name), path);
    LARGE_INTEGER sz;
    sz.HighPart = (LONG)bh.nFileSizeHigh;
    sz.LowPart = bh.nFileSizeLow;
    info->size = sz.QuadPart;
    info->mod_time = fs_filetime_to_time(bh.ftLastWriteTime);
    int reparse = (bh.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    info->is_dir = (bh.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                   (follow || !reparse);
    info->mode = info->is_dir ? (uint32_t)(NEVERC_FS_MODE_DIR | 0755) : 0644U;
    if (!follow && reparse)
        info->mode |= NEVERC_FS_MODE_LINK;
    return 0;
}
#else
static void fs_fill_from_st(neverc_fs_file_info_t *info, const char *path,
                            const struct stat *st) {
    memset(info, 0, sizeof(*info));
    fs_fill_base_name(info->name, sizeof(info->name), path);
    info->size = st->st_size;
    info->mod_time = st->st_mtime;
    info->is_dir = S_ISDIR(st->st_mode) ? 1 : 0;
    info->mode = (uint32_t)(st->st_mode & NEVERC_FS_PERM_MASK);
    if (info->is_dir) info->mode |= NEVERC_FS_MODE_DIR;
    if (S_ISLNK(st->st_mode)) info->mode |= NEVERC_FS_MODE_LINK;
    if (S_ISFIFO(st->st_mode)) info->mode |= NEVERC_FS_MODE_PIPE;
    if (S_ISSOCK(st->st_mode)) info->mode |= NEVERC_FS_MODE_SOCKET;
    if (S_ISCHR(st->st_mode))
        info->mode |= NEVERC_FS_MODE_DEVICE | NEVERC_FS_MODE_CHAR_DEVICE;
    else if (S_ISBLK(st->st_mode))
        info->mode |= NEVERC_FS_MODE_DEVICE;
    if (st->st_mode & S_ISUID) info->mode |= NEVERC_FS_MODE_SETUID;
    if (st->st_mode & S_ISGID) info->mode |= NEVERC_FS_MODE_SETGID;
    if (st->st_mode & S_ISVTX) info->mode |= NEVERC_FS_MODE_STICKY;
    if (!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode) && !S_ISLNK(st->st_mode) &&
        !S_ISFIFO(st->st_mode) && !S_ISSOCK(st->st_mode) &&
        !S_ISCHR(st->st_mode) && !S_ISBLK(st->st_mode))
        info->mode |= NEVERC_FS_MODE_IRREGULAR;
}
#endif

static int fs_ci_eq(const char *a, size_t alen, const char *b) {
    size_t i;
    for (i = 0; b[i] != '\0'; i++) {
        unsigned char ca, cb;
        if (i >= alen) return 0;
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return i == alen;
}

static int fs_win_reserved_component(const char *p, size_t elen) {
    static const char *const reserved[] = {
        "con", "prn", "aux", "nul",
        "com0", "com1", "com2", "com3", "com4",
        "com5", "com6", "com7", "com8", "com9",
        "lpt0", "lpt1", "lpt2", "lpt3", "lpt4",
        "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
    };
    size_t stem = 0, i;
    while (stem < elen && p[stem] != '.') stem++;
    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (fs_ci_eq(p, stem, reserved[i])) return 1;
    }
    return 0;
}

int neverc_fs_valid_path(const char *name) {
    if (!name || name[0] == '\0') return 0;
    if (name[0] == '/') return 0;
    size_t len = strlen(name);
    if (name[len-1] == '/') return 0;
    if (strcmp(name, ".") == 0) return 1;
    if (strchr(name, '\\') != NULL) return 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if (((name[0] >= 'A' && name[0] <= 'Z') ||
         (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':')
        return 0;
    if (strchr(name, ':') != NULL) return 0;
#endif

    const char *p = name;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t elen = slash ? (size_t)(slash - p) : strlen(p);
        if (elen == 0) return 0;
        if (elen == 1 && p[0] == '.') return 0;
        if (elen == 2 && p[0] == '.' && p[1] == '.') return 0;
        if (p[elen - 1] == '.' || p[elen - 1] == ' ') return 0;
        if (fs_win_reserved_component(p, elen)) return 0;
        p += elen;
        if (*p == '/') p++;
    }
    return 1;
}

int neverc_fs_stat(const char *path, neverc_fs_file_info_t *info) {
    if (!path || !info) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return fs_stat_win(path, info, 1);
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    fs_fill_from_st(info, path, &st);
    return 0;
#endif
}

int neverc_fs_lstat(const char *path, neverc_fs_file_info_t *info) {
    if (!path || !info) return -1;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return fs_stat_win(path, info, 0);
#else
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    fs_fill_from_st(info, path, &st);
    return 0;
#endif
}

int neverc_fs_read_file(const char *path, uint8_t **data, size_t *size) {
    if (!path || !data || !size) return -1;
    *data = NULL;
    *size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint64_t len = 0;
    int is_dir = 0;
    if (fs_opened_size(f, &len, &is_dir) != 0 || is_dir ||
        len > (uint64_t)SIZE_MAX - 1U) {
        fclose(f);
        return -1;
    }
    *data = (uint8_t *)malloc((size_t)len + 1U);
    if (!*data) { fclose(f); return -1; }
    *size = fread(*data, 1, (size_t)len, f);
    if (*size < (size_t)len && ferror(f)) {
        free(*data);
        *data = NULL;
        *size = 0;
        fclose(f);
        return -1;
    }
    (*data)[*size] = 0;
    if (fclose(f) != 0) {
        free(*data);
        *data = NULL;
        *size = 0;
        return -1;
    }
    return 0;
}

int neverc_fs_read_dir(const char *path, neverc_fs_dir_entry_t **entries,
                       size_t *count) {
    if (!path || !entries || !count) return -1;
    *entries = NULL; *count = 0;

#if defined(NEVERC_PLATFORM_WINDOWS)
    char pattern[4096];
    int pattern_len = snprintf(pattern, sizeof(pattern), "%s\\*", path);
    if (pattern_len < 0 || (size_t)pattern_len >= sizeof(pattern)) return -1;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return fs_win_fail();
    size_t cap = 16;
    neverc_fs_dir_entry_t *result =
        (neverc_fs_dir_entry_t *)malloc(cap * sizeof(*result));
    if (!result) { FindClose(h); return -1; }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (!fs_entries_reserve(&result, &cap, *count + 1)) {
            free(result);
            FindClose(h);
            *count = 0;
            return -1;
        }
        neverc_fs_dir_entry_t *e = &result[*count];
        memset(e, 0, sizeof(*e));
        strncpy(e->name, fd.cFileName, sizeof(e->name) - 1);
        {
            int reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            e->is_dir = !reparse &&
                        (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e->mode = e->is_dir ? NEVERC_FS_MODE_DIR | 0755 : 0644;
            if (reparse) e->mode |= NEVERC_FS_MODE_LINK;
        }
        (*count)++;
    } while (FindNextFileA(h, &fd));
    {
        DWORD err = GetLastError();
        FindClose(h);
        if (err != ERROR_NO_MORE_FILES) {
            free(result);
            *entries = NULL;
            *count = 0;
            SetLastError(err);
            return fs_win_fail();
        }
    }
    *entries = result;
#else
    DIR *d = opendir(path);
    if (!d) return -1;
    size_t cap = 16;
    neverc_fs_dir_entry_t *result =
        (neverc_fs_dir_entry_t *)malloc(cap * sizeof(*result));
    if (!result) { closedir(d); return -1; }
    for (;;) {
        errno = 0;
        struct dirent *de = readdir(d);
        if (!de) {
            if (errno != 0) {
                free(result);
                closedir(d);
                *entries = NULL;
                *count = 0;
                return -1;
            }
            break;
        }
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (!fs_entries_reserve(&result, &cap, *count + 1)) {
            free(result);
            closedir(d);
            *entries = NULL;
            *count = 0;
            return -1;
        }
        neverc_fs_dir_entry_t *e = &result[*count];
        memset(e, 0, sizeof(*e));
        strncpy(e->name, de->d_name, sizeof(e->name) - 1);
        if (de->d_type == DT_DIR) {
            e->is_dir = 1;
            e->mode = NEVERC_FS_MODE_DIR;
        } else if (de->d_type == DT_LNK) {
            e->mode = NEVERC_FS_MODE_LINK;
        } else if (de->d_type == DT_FIFO) {
            e->mode = NEVERC_FS_MODE_PIPE;
        } else if (de->d_type == DT_SOCK) {
            e->mode = NEVERC_FS_MODE_SOCKET;
        } else if (de->d_type == DT_CHR) {
            e->mode = NEVERC_FS_MODE_DEVICE | NEVERC_FS_MODE_CHAR_DEVICE;
        } else if (de->d_type == DT_BLK) {
            e->mode = NEVERC_FS_MODE_DEVICE;
        } else if (de->d_type == DT_UNKNOWN) {
            char *full = fs_join_path(path, de->d_name);
            if (full) {
                neverc_fs_file_info_t stinfo;
                if (neverc_fs_lstat(full, &stinfo) == 0) {
                    e->is_dir = stinfo.is_dir;
                    e->mode = stinfo.mode;
                }
                free(full);
            }
        }
        (*count)++;
    }
    if (closedir(d) != 0) {
        free(result);
        *entries = NULL;
        *count = 0;
        return -1;
    }
    *entries = result;
#endif
    return 0;
}

int neverc_fs_glob(const char *dir, const char *pattern,
                   char ***matches, size_t *count) {
    if (!dir || !pattern || !matches || !count) return -1;
    *matches = NULL; *count = 0;
    if (fs_glob_match(pattern, "") < 0) return -1;

    neverc_fs_dir_entry_t *entries = NULL;
    size_t nentries = 0;
    if (neverc_fs_read_dir(dir, &entries, &nentries) != 0) return -1;

    size_t cap = 8;
    char **result = (char **)malloc(cap * sizeof(*result));
    if (!result) {
        neverc_fs_free_entries(entries);
        return -1;
    }

    for (size_t i = 0; i < nentries; i++) {
        int matched = fs_glob_match(pattern, entries[i].name);
        if (matched < 0) goto glob_fail;
        if (matched) {
            if (!fs_matches_reserve(&result, &cap, *count + 1))
                goto glob_fail;
            char *full = fs_join_path(dir, entries[i].name);
            if (!full) goto glob_fail;
            result[*count] = full;
            (*count)++;
        }
    }
    neverc_fs_free_entries(entries);
    *matches = result;
    return 0;

glob_fail:
    neverc_fs_free_entries(entries);
    neverc_fs_free_matches(result, *count);
    *matches = NULL;
    *count = 0;
    return -1;
}

static int fs_is_real_dir(const char *path, const neverc_fs_dir_entry_t *entry) {
    if (!entry || !entry->is_dir || (entry->mode & NEVERC_FS_MODE_LINK))
        return 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static int walk_recursive(const char *path,
                          int (*fn)(const char *, const neverc_fs_dir_entry_t *, void *),
                          void *userdata) {
    neverc_fs_dir_entry_t *entries = NULL;
    size_t count = 0;
    if (neverc_fs_read_dir(path, &entries, &count) != 0) return -1;

    for (size_t i = 0; i < count; i++) {
        char *full = fs_join_path(path, entries[i].name);
        if (!full) {
            neverc_fs_free_entries(entries);
            return -1;
        }

        int rc = fn(full, &entries[i], userdata);
        if (rc == NEVERC_FS_SKIP_ALL) {
            free(full);
            neverc_fs_free_entries(entries);
            return NEVERC_FS_SKIP_ALL;
        }
        if (rc == NEVERC_FS_SKIP_DIR) {
            int skip_rest = !entries[i].is_dir;
            free(full);
            if (skip_rest) break;
            continue;
        }
        if (rc != 0) {
            free(full);
            neverc_fs_free_entries(entries);
            return rc;
        }

        if (fs_is_real_dir(full, &entries[i])) {
            rc = walk_recursive(full, fn, userdata);
            if (rc != 0) {
                free(full);
                neverc_fs_free_entries(entries);
                return rc;
            }
        }
        free(full);
    }
    neverc_fs_free_entries(entries);
    return 0;
}

int neverc_fs_walk_dir(const char *root,
                       int (*fn)(const char *path,
                                 const neverc_fs_dir_entry_t *entry,
                                 void *userdata),
                       void *userdata) {
    if (!root || !fn) return -1;
    neverc_fs_file_info_t info;
    if (neverc_fs_lstat(root, &info) != 0) return -1;
    neverc_fs_dir_entry_t root_entry;
    memset(&root_entry, 0, sizeof(root_entry));
    memcpy(root_entry.name, info.name, sizeof(root_entry.name));
    root_entry.is_dir = info.is_dir;
    root_entry.mode = info.mode;
    int rc = fn(root, &root_entry, userdata);
    if (rc == NEVERC_FS_SKIP_DIR || rc == NEVERC_FS_SKIP_ALL) return 0;
    if (rc != 0) return rc;
    if (!info.is_dir || (info.mode & NEVERC_FS_MODE_LINK)) return 0;
    rc = walk_recursive(root, fn, userdata);
    if (rc == NEVERC_FS_SKIP_ALL) return 0;
    return rc;
}

void neverc_fs_free_entries(neverc_fs_dir_entry_t *entries) {
    free(entries);
}

void neverc_fs_free_matches(char **matches, size_t count) {
    if (!matches) return;
    for (size_t i = 0; i < count; i++) free(matches[i]);
    free(matches);
}
