#ifndef NEVERC_IO_FS_H
#define NEVERC_IO_FS_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEVERC_FS_MODE_DIR    = 1 << 0,
    NEVERC_FS_MODE_APPEND = 1 << 1,
    NEVERC_FS_MODE_EXCL   = 1 << 2,
    NEVERC_FS_MODE_TEMP   = 1 << 3,
    NEVERC_FS_MODE_LINK   = 1 << 4,
    NEVERC_FS_MODE_PIPE   = 1 << 5,
    NEVERC_FS_MODE_SOCKET = 1 << 6,
    NEVERC_FS_MODE_DEVICE = 1 << 7,
    NEVERC_FS_MODE_CHAR_DEVICE = 1 << 8,
    NEVERC_FS_MODE_IRREGULAR   = 1 << 9,
    NEVERC_FS_MODE_SETUID  = 1 << 10,
    NEVERC_FS_MODE_SETGID  = 1 << 11,
    NEVERC_FS_MODE_STICKY  = 1 << 12,
    NEVERC_FS_PERM_MASK    = 0777,
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

int neverc_fs_valid_path(const char *name);

int neverc_fs_stat(const char *path, neverc_fs_file_info_t *info);

int neverc_fs_read_file(const char *path, uint8_t **data, size_t *size);

int neverc_fs_read_dir(const char *path, neverc_fs_dir_entry_t **entries,
                       size_t *count);

int neverc_fs_glob(const char *dir, const char *pattern,
                   char ***matches, size_t *count);

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

#endif /* NEVERC_IO_FS_H */
