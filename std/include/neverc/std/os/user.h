#ifndef NEVERC_OS_USER_H
#define NEVERC_OS_USER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char uid[64];
    char gid[64];
    char username[256];
    char name[256];
    char home_dir[1024];
} neverc_user_t;

typedef struct {
    char gid[64];
    char name[256];
} neverc_group_t;

/* Current returns the current user. Returns 0 on success, -1 on failure. */
int neverc_user_current(neverc_user_t *u);

/* Lookup looks up a user by username. Returns 0 on success, -1 if not found. */
int neverc_user_lookup(const char *username, neverc_user_t *u);

/* LookupId looks up a user by numeric UID. Returns 0 on success. */
int neverc_user_lookup_id(int uid, neverc_user_t *u);

/* LookupGroup looks up a group by name. Returns 0 on success. */
int neverc_user_lookup_group(const char *name, neverc_group_t *g);

/* LookupGroupId looks up a group by numeric GID. Returns 0 on success. */
int neverc_user_lookup_group_id(int gid, neverc_group_t *g);

/* HomeDir returns the home directory of the current user. */
const char *neverc_user_home_dir(void);

/* CacheDir returns the default cache directory for the current user. */
const char *neverc_user_cache_dir(void);

/* ConfigDir returns the default config directory for the current user. */
const char *neverc_user_config_dir(void);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/os.h>
#endif


#endif /* NEVERC_OS_USER_H */
