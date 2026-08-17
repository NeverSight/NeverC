#ifndef NEVERC_PATH_FILEPATH_H
#define NEVERC_PATH_FILEPATH_H

/*
 * NeverC path/filepath — OS-specific path manipulation.
 * Mirrors Go path/filepath: uses '/' on Unix, '\\' on Windows.
 * All functions operate on the compile-target's path separator.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define NEVERC_FILEPATH_SEP     '\\'
#define NEVERC_FILEPATH_LISTSEP ';'
#else
#define NEVERC_FILEPATH_SEP     '/'
#define NEVERC_FILEPATH_LISTSEP ':'
#endif

const char *neverc_filepath_base(const char *path, char *buf, size_t buf_len);
const char *neverc_filepath_dir(const char *path, char *buf, size_t buf_len);
const char *neverc_filepath_ext(const char *path);
int         neverc_filepath_isabs(const char *path);
/* Reports whether path stays inside a join base (Go filepath.IsLocal).
 * Empty, absolute, reserved Windows device names, and cleaned paths that
 * begin with ".." are not local. */
int         neverc_filepath_is_local(const char *path);
const char *neverc_filepath_volume_name(const char *path, char *buf, size_t buf_len);
const char *neverc_filepath_clean(const char *path, char *buf, size_t buf_len);
const char *neverc_filepath_join(const char *a, const char *b, char *buf, size_t buf_len);
/* Lexical relative path from base to targ (Go filepath.Rel). Returns NULL if
 * targ cannot be made relative to base, if base still contains ".." after
 * Clean, or on OOM. A leading ".." in a successful result means targ is
 * outside base; callers that need a jail must also check IsLocal. */
const char *neverc_filepath_rel(const char *basepath, const char *targpath,
                                char *buf, size_t buf_len);
void        neverc_filepath_split(const char *path, const char **dir, size_t *dir_len,
                                   const char **file);
int         neverc_filepath_match(const char *pattern, const char *name);
const char *neverc_filepath_to_slash(const char *path, char *buf, size_t buf_len);
const char *neverc_filepath_from_slash(const char *path, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/path.h>
#endif


#endif
