#ifndef NEVERC_IO_FS_H
#define NEVERC_IO_FS_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Released v3389.1.4 numeric ABI. These values overlap Unix permission bits,
 * so implementations must not infer file type or symlink traversal decisions
 * from mode alone; use is_dir plus lstat/reparse metadata instead. */
typedef enum {
    NEVERC_FS_MODE_DIR         = 1 << 0,
    NEVERC_FS_MODE_APPEND      = 1 << 1,
    NEVERC_FS_MODE_EXCL        = 1 << 2,
    NEVERC_FS_MODE_TEMP        = 1 << 3,
    NEVERC_FS_MODE_LINK        = 1 << 4,
    NEVERC_FS_MODE_PIPE        = 1 << 5,
    NEVERC_FS_MODE_SOCKET      = 1 << 6,
    NEVERC_FS_MODE_DEVICE      = 1 << 7,
    NEVERC_FS_MODE_CHAR_DEVICE = 1 << 8,
    NEVERC_FS_MODE_IRREGULAR   = 1 << 9,
    NEVERC_FS_MODE_SETUID      = 1 << 10,
    NEVERC_FS_MODE_SETGID      = 1 << 11,
    NEVERC_FS_MODE_STICKY      = 1 << 12,
    NEVERC_FS_PERM_MASK        = 0777,
} neverc_fs_mode_t;

typedef struct {
    char     name[256];
    int64_t  size;
    uint32_t mode;
    time_t   mod_time;
    int      is_dir;
} neverc_fs_file_info_t;

typedef struct {
    char name[256];
    int  is_dir;
    uint32_t mode;
} neverc_fs_dir_entry_t;

/* Walk callback sentinels (not errors). neverc_fs_walk_dir returns 0. */
#define NEVERC_FS_SKIP_DIR  (-2)
#define NEVERC_FS_SKIP_ALL  (-3)

int neverc_fs_valid_path(const char *name);

int neverc_fs_stat(const char *path, neverc_fs_file_info_t *info);
int neverc_fs_lstat(const char *path, neverc_fs_file_info_t *info);

int neverc_fs_read_file(const char *path, uint8_t **data, size_t *size);

int neverc_fs_read_dir(const char *path, neverc_fs_dir_entry_t **entries,
                       size_t *count);

/* Match pattern against names in dir only. Patterns containing '/' are
 * rejected so a caller-supplied pattern cannot select nested paths. */
int neverc_fs_glob(const char *dir, const char *pattern,
                   char ***matches, size_t *count);

/* Callback: 0 continue, NEVERC_FS_SKIP_DIR skip directory contents (or
 * remaining siblings if invoked on a non-directory), NEVERC_FS_SKIP_ALL
 * stop successfully, any other non-zero aborts and is returned.
 * entry is NULL when the root cannot be lstat'd; return 0 to surface the
 * original failure, or a skip sentinel to treat it as success. */
int neverc_fs_walk_dir(const char *root,
                       int (*fn)(const char *path,
                                 const neverc_fs_dir_entry_t *entry,
                                 void *userdata),
                       void *userdata);

void neverc_fs_free_entries(neverc_fs_dir_entry_t *entries);
void neverc_fs_free_matches(char **matches, size_t count);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/io.h>
#endif


#endif /* NEVERC_IO_FS_H */
