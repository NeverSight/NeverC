#include "neverc/std/io/fs.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(NEVERC_PLATFORM_WINDOWS)
  #include <windows.h>
  #include <io.h>
#else
  #include <sys/stat.h>
  #include <dirent.h>
  #include <unistd.h>
  #include <fnmatch.h>
#endif

static int fs_entries_reserve(neverc_fs_dir_entry_t **entries, size_t *cap,
                              size_t needed) {
    if (needed <= *cap) return 1;
    size_t next = *cap;
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
    size_t next = *cap;
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

static char *fs_join_path(const char *dir, const char *name) {
    size_t dlen = strlen(dir), nlen = strlen(name);
    if (dlen > SIZE_MAX - nlen - 2) return NULL;
    char *full = (char *)malloc(dlen + nlen + 2);
    if (!full) return NULL;
    memcpy(full, dir, dlen);
#if defined(NEVERC_PLATFORM_WINDOWS)
    full[dlen] = '\\';
#else
    full[dlen] = '/';
#endif
    memcpy(full + dlen + 1, name, nlen + 1);
    return full;
}

int neverc_fs_valid_path(const char *name) {
    if (!name || name[0] == '\0') return 0;
    if (name[0] == '/') return 0;
    size_t len = strlen(name);
    if (name[len-1] == '/') return 0;
    if (strcmp(name, ".") == 0) return 1;
    if (strchr(name, '\\') != NULL) return 0;

    const char *p = name;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t elen = slash ? (size_t)(slash - p) : strlen(p);
        if (elen == 0) return 0;
        if (elen == 1 && p[0] == '.') return 0;
        if (elen == 2 && p[0] == '.' && p[1] == '.') return 0;
        p += elen;
        if (*p == '/') p++;
    }
    return 1;
}

int neverc_fs_stat(const char *path, neverc_fs_file_info_t *info) {
    if (!path || !info) return -1;
    memset(info, 0, sizeof(*info));

#if defined(NEVERC_PLATFORM_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attr))
        return -1;
    const char *base = strrchr(path, '\\');
    if (!base) base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(info->name, base, sizeof(info->name) - 1);
    LARGE_INTEGER sz;
    sz.HighPart = attr.nFileSizeHigh; sz.LowPart = attr.nFileSizeLow;
    info->size = sz.QuadPart;
    info->is_dir = (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    ULARGE_INTEGER ull;
    ull.LowPart = attr.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = attr.ftLastWriteTime.dwHighDateTime;
    info->mod_time = (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
    info->mode = info->is_dir ? NEVERC_FS_MODE_DIR | 0755 : 0644;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(info->name, base, sizeof(info->name) - 1);
    info->size = st.st_size;
    info->mod_time = st.st_mtime;
    info->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    info->mode = st.st_mode & 07777;
    if (info->is_dir) info->mode |= NEVERC_FS_MODE_DIR;
    if (S_ISLNK(st.st_mode)) info->mode |= NEVERC_FS_MODE_LINK;
    if (S_ISFIFO(st.st_mode)) info->mode |= NEVERC_FS_MODE_PIPE;
    if (S_ISSOCK(st.st_mode)) info->mode |= NEVERC_FS_MODE_SOCKET;
#endif
    return 0;
}

int neverc_fs_read_file(const char *path, uint8_t **data, size_t *size) {
    if (!path || !data || !size) return -1;
    *data = NULL;
    *size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    *data = (uint8_t *)malloc((size_t)len + 1);
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
    fclose(f);
    return 0;
}

int neverc_fs_read_dir(const char *path, neverc_fs_dir_entry_t **entries,
                       size_t *count) {
    if (!path || !entries || !count) return -1;
    *entries = NULL; *count = 0;

#if defined(NEVERC_PLATFORM_WINDOWS)
    char pattern[MAX_PATH];
    int pattern_len = snprintf(pattern, sizeof(pattern), "%s\\*", path);
    if (pattern_len < 0 || (size_t)pattern_len >= sizeof(pattern)) return -1;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
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
        e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        e->mode = e->is_dir ? NEVERC_FS_MODE_DIR | 0755 : 0644;
        (*count)++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    *entries = result;
#else
    DIR *d = opendir(path);
    if (!d) return -1;
    size_t cap = 16;
    neverc_fs_dir_entry_t *result =
        (neverc_fs_dir_entry_t *)malloc(cap * sizeof(*result));
    if (!result) { closedir(d); return -1; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (!fs_entries_reserve(&result, &cap, *count + 1)) {
            free(result);
            closedir(d);
            *count = 0;
            return -1;
        }
        neverc_fs_dir_entry_t *e = &result[*count];
        memset(e, 0, sizeof(*e));
        strncpy(e->name, de->d_name, sizeof(e->name) - 1);
        e->is_dir = (de->d_type == DT_DIR) ? 1 : 0;
        (*count)++;
    }
    closedir(d);
    *entries = result;
#endif
    return 0;
}

int neverc_fs_glob(const char *dir, const char *pattern,
                   char ***matches, size_t *count) {
    if (!dir || !pattern || !matches || !count) return -1;
    *matches = NULL; *count = 0;

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
#if defined(NEVERC_PLATFORM_WINDOWS)
        /* simple wildcard: only support * */
        int matched = 1;
        const char *p = pattern, *n = entries[i].name;
        while (*p && *n) {
            if (*p == '*') { matched = 1; break; }
            if (*p != *n && *p != '?') { matched = 0; break; }
            p++; n++;
        }
        if (*p == '\0' && *n != '\0') matched = 0;
#else
        int matched = (fnmatch(pattern, entries[i].name, 0) == 0);
#endif
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
        if (rc != 0) { free(full); neverc_fs_free_entries(entries); return rc; }

        if (entries[i].is_dir) {
            rc = walk_recursive(full, fn, userdata);
            if (rc != 0) { free(full); neverc_fs_free_entries(entries); return rc; }
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
    neverc_fs_dir_entry_t root_entry;
    memset(&root_entry, 0, sizeof(root_entry));
    const char *base = strrchr(root, '/');
    base = base ? base + 1 : root;
    strncpy(root_entry.name, base, sizeof(root_entry.name) - 1);
    root_entry.is_dir = 1;
    int rc = fn(root, &root_entry, userdata);
    if (rc != 0) return rc;
    return walk_recursive(root, fn, userdata);
}

void neverc_fs_free_entries(neverc_fs_dir_entry_t *entries) {
    free(entries);
}

void neverc_fs_free_matches(char **matches, size_t count) {
    if (!matches) return;
    for (size_t i = 0; i < count; i++) free(matches[i]);
    free(matches);
}
