/*
 * Frozen v3389.1.4 consumer: intentionally do not include the current fs.h.
 * This models an already-built client whose enum values are embedded in its
 * code while it loads the current std/io/fs implementation.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef enum {
    V3389_FS_MODE_DIR         = 1 << 0,
    V3389_FS_MODE_APPEND      = 1 << 1,
    V3389_FS_MODE_EXCL        = 1 << 2,
    V3389_FS_MODE_TEMP        = 1 << 3,
    V3389_FS_MODE_LINK        = 1 << 4,
    V3389_FS_MODE_PIPE        = 1 << 5,
    V3389_FS_MODE_SOCKET      = 1 << 6,
    V3389_FS_MODE_DEVICE      = 1 << 7,
    V3389_FS_MODE_CHAR_DEVICE = 1 << 8,
    V3389_FS_MODE_IRREGULAR   = 1 << 9,
    V3389_FS_MODE_SETUID      = 1 << 10,
    V3389_FS_MODE_SETGID      = 1 << 11,
    V3389_FS_MODE_STICKY      = 1 << 12,
    V3389_FS_PERM_MASK        = 0777,
} v3389_fs_mode_t;

typedef struct {
    char name[256];
    int64_t size;
    uint32_t mode;
    time_t mod_time;
    int is_dir;
} v3389_fs_file_info_t;

typedef struct {
    uint64_t before[2];
    v3389_fs_file_info_t value;
    uint64_t after[2];
} guarded_v3389_fs_file_info_t;

int neverc_fs_stat(const char *path, v3389_fs_file_info_t *info);

static void cleanup_paths(const char *file_path, const char *dir_path) {
#if defined(_WIN32)
    DeleteFileA(file_path);
    RemoveDirectoryA(dir_path);
#else
    unlink(file_path);
    rmdir(dir_path);
#endif
}

int main(void) {
    char dir_path[1536];
    char file_path[1792];
    int path_len;

#if defined(_WIN32)
    char temp[1024];
    DWORD temp_len = GetTempPathA((DWORD)sizeof(temp), temp);
    if (temp_len == 0 || temp_len >= sizeof(temp)) return 1;
    path_len = snprintf(dir_path, sizeof(dir_path), "%sneverc_fs_v3389_%lu",
                        temp, (unsigned long)GetCurrentProcessId());
    if (path_len < 0 || (size_t)path_len >= sizeof(dir_path))
        return 1;
    path_len = snprintf(file_path, sizeof(file_path), "%s\\file.txt", dir_path);
    if (path_len < 0 || (size_t)path_len >= sizeof(file_path))
        return 1;
    if (!CreateDirectoryA(dir_path, NULL)) return 1;
#else
    const char *base = getenv("TMPDIR");
    if (!base) base = "/tmp";
    path_len = snprintf(dir_path, sizeof(dir_path), "%s/neverc_fs_v3389_%ld",
                        base, (long)getpid());
    if (path_len < 0 || (size_t)path_len >= sizeof(dir_path))
        return 1;
    path_len = snprintf(file_path, sizeof(file_path), "%s/file.txt", dir_path);
    if (path_len < 0 || (size_t)path_len >= sizeof(file_path))
        return 1;
    if (mkdir(dir_path, 0700) != 0) return 1;
#endif

    FILE *file = fopen(file_path, "wb");
    if (!file) {
        cleanup_paths(file_path, dir_path);
        return 1;
    }
    int write_ok = fputs("x", file) != EOF;
    if (fclose(file) != 0) write_ok = 0;
    if (!write_ok) {
        cleanup_paths(file_path, dir_path);
        return 1;
    }

#if defined(_WIN32)
    if (!SetFileAttributesA(file_path, FILE_ATTRIBUTE_NORMAL)) {
        cleanup_paths(file_path, dir_path);
        return 1;
    }
#else
    if (chmod(dir_path, 0700) != 0 || chmod(file_path, 0600) != 0) {
        cleanup_paths(file_path, dir_path);
        return 1;
    }
#endif

    const uint64_t guard_before = UINT64_C(0x95c0e5b17a9d2468);
    const uint64_t guard_after = UINT64_C(0x6a3f1a4e8562db97);
    guarded_v3389_fs_file_info_t dir_guard;
    guarded_v3389_fs_file_info_t file_guard;
    memset(&dir_guard, 0xa5, sizeof(dir_guard));
    memset(&file_guard, 0x5a, sizeof(file_guard));
    dir_guard.before[0] = dir_guard.before[1] = guard_before;
    dir_guard.after[0] = dir_guard.after[1] = guard_after;
    file_guard.before[0] = file_guard.before[1] = guard_before;
    file_guard.after[0] = file_guard.after[1] = guard_after;
    if (neverc_fs_stat(dir_path, &dir_guard.value) != 0 ||
        neverc_fs_stat(file_path, &file_guard.value) != 0) {
        cleanup_paths(file_path, dir_path);
        return 1;
    }

#if defined(_WIN32)
    const uint32_t expected_dir = 0755U | (uint32_t)V3389_FS_MODE_DIR;
    const uint32_t expected_file = 0644U;
#else
    const uint32_t expected_dir = 0700U | (uint32_t)V3389_FS_MODE_DIR;
    const uint32_t expected_file = 0600U;
#endif

    int ok = dir_guard.before[0] == guard_before &&
             dir_guard.before[1] == guard_before &&
             dir_guard.after[0] == guard_after &&
             dir_guard.after[1] == guard_after &&
             file_guard.before[0] == guard_before &&
             file_guard.before[1] == guard_before &&
             file_guard.after[0] == guard_after &&
             file_guard.after[1] == guard_after &&
             dir_guard.value.is_dir == 1 && file_guard.value.is_dir == 0 &&
             dir_guard.value.mode == expected_dir &&
             file_guard.value.mode == expected_file &&
             (dir_guard.value.mode & V3389_FS_PERM_MASK) ==
                 (expected_dir & V3389_FS_PERM_MASK) &&
             (file_guard.value.mode & V3389_FS_PERM_MASK) == expected_file;

    cleanup_paths(file_path, dir_path);
    if (!ok) return 1;
    puts("passed");
    return 0;
}
