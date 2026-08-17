#ifndef NEVERC_PATH_H
#define NEVERC_PATH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All buffer-writing functions return the number of chars written
   (excluding '\0'), or -1 if the buffer is too small. */

int neverc_path_base(const char *path, char *buf, size_t bufsize);
int neverc_path_dir(const char *path, char *buf, size_t bufsize);
int neverc_path_clean(const char *path, char *buf, size_t bufsize);
int neverc_path_join2(const char *a, const char *b, char *buf, size_t bufsize);
int neverc_path_split(const char *path, char *dir, size_t dirsize,
                      char *file, size_t filesize);

const char *neverc_path_ext(const char *path);
int         neverc_path_isabs(const char *path);
/* Reports whether path stays inside a join base (Go filepath.IsLocal, slash paths).
 * Empty, absolute, and cleaned paths that begin with ".." are not local. */
int         neverc_path_is_local(const char *path);

/* Match: glob-style pattern matching (mirrors Go path.Match).
 * Pattern: * matches any non-/ sequence, ? matches any single non-/ char,
 * [...] matches character class. Returns 1 if matched, 0 if not, -1 on error. */
int neverc_path_match(const char *pattern, const char *name);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
struct __neverc_std_filepath_t { char __tag; };

struct __neverc_std_path_t {
    char __tag;
    struct __neverc_std_filepath_t filepath;
};
extern struct __neverc_std_path_t __neverc_mod_path;
extern struct __neverc_std_path_t path;
#endif

#endif /* NEVERC_PATH_H */
