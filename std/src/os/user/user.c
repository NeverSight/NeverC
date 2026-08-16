#include "neverc/std/os/user.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdio.h>

#if defined(NEVERC_PLATFORM_WINDOWS)
#include <windows.h>
#include <sddl.h>
#include <lmcons.h>
#include <shlobj.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

int neverc_user_current(neverc_user_t *u) {
    if (!u) return -1;
    memset(u, 0, sizeof(*u));

    DWORD size = sizeof(u->username);
    if (!GetUserNameA(u->username, &size)) return -1;

    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return -1;

    DWORD tok_len = 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &tok_len);
    TOKEN_USER *tok_user = (TOKEN_USER *)malloc(tok_len);
    if (!tok_user) { CloseHandle(token); return -1; }

    if (GetTokenInformation(token, TokenUser, tok_user, tok_len, &tok_len)) {
        char *sid_str = NULL;
        if (ConvertSidToStringSidA(tok_user->User.Sid, &sid_str)) {
            snprintf(u->uid, sizeof(u->uid), "%s", sid_str);
            LocalFree(sid_str);
        }
    }
    free(tok_user);

    TOKEN_PRIMARY_GROUP *tok_group = NULL;
    GetTokenInformation(token, TokenPrimaryGroup, NULL, 0, &tok_len);
    tok_group = (TOKEN_PRIMARY_GROUP *)malloc(tok_len);
    if (tok_group && GetTokenInformation(token, TokenPrimaryGroup, tok_group, tok_len, &tok_len)) {
        char *gid_str = NULL;
        if (ConvertSidToStringSidA(tok_group->PrimaryGroup, &gid_str)) {
            snprintf(u->gid, sizeof(u->gid), "%s", gid_str);
            LocalFree(gid_str);
        }
    }
    free(tok_group);
    CloseHandle(token);

    snprintf(u->name, sizeof(u->name), "%s", u->username);

    char home[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, home) == S_OK)
        snprintf(u->home_dir, sizeof(u->home_dir), "%s", home);

    return (u->username[0] && u->uid[0]) ? 0 : -1;
}

int neverc_user_lookup(const char *username, neverc_user_t *u) {
    (void)username; (void)u;
    return -1;  /* Not easily supported on Windows without NetUserGetInfo */
}

int neverc_user_lookup_id(int uid, neverc_user_t *u) {
    (void)uid; (void)u;
    return -1;
}

int neverc_user_lookup_group(const char *name, neverc_group_t *g) {
    (void)name; (void)g;
    return -1;
}

int neverc_user_lookup_group_id(int gid, neverc_group_t *g) {
    (void)gid; (void)g;
    return -1;
}

const char *neverc_user_home_dir(void) {
    static char buf[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, buf) == S_OK)
        return buf;
    const char *h = getenv("USERPROFILE");
    if (h) { snprintf(buf, sizeof(buf), "%s", h); return buf; }
    return "";
}

const char *neverc_user_cache_dir(void) {
    static char buf[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, buf) == S_OK)
        return buf;
    const char *h = getenv("LOCALAPPDATA");
    if (h) { snprintf(buf, sizeof(buf), "%s", h); return buf; }
    return "";
}

const char *neverc_user_config_dir(void) {
    static char buf[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, buf) == S_OK)
        return buf;
    const char *h = getenv("APPDATA");
    if (h) { snprintf(buf, sizeof(buf), "%s", h); return buf; }
    return "";
}

#else /* POSIX: macOS, iOS, Linux, Android, BSD */

#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

static void user_copy_gecos(char *dst, size_t cap, const char *gecos) {
    size_t n = 0;
    if (!gecos) gecos = "";
    while (gecos[n] && gecos[n] != ',' && n + 1 < cap) {
        dst[n] = gecos[n];
        n++;
    }
    dst[n] = '\0';
}

static int user_fill_from_passwd(const struct passwd *pw, neverc_user_t *u) {
    memset(u, 0, sizeof(*u));
    snprintf(u->uid, sizeof(u->uid), "%u", (unsigned)pw->pw_uid);
    snprintf(u->gid, sizeof(u->gid), "%u", (unsigned)pw->pw_gid);
    snprintf(u->username, sizeof(u->username), "%s",
             pw->pw_name ? pw->pw_name : "");
    user_copy_gecos(u->name, sizeof(u->name), pw->pw_gecos);
    snprintf(u->home_dir, sizeof(u->home_dir), "%s",
             pw->pw_dir ? pw->pw_dir : "");
    return 0;
}

static size_t user_pw_bufsize(void) {
#ifdef _SC_GETPW_R_SIZE_MAX
    long n = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (n > 0) return (size_t)n;
#endif
    return 4096;
}

static size_t user_gr_bufsize(void) {
#ifdef _SC_GETGR_R_SIZE_MAX
    long n = sysconf(_SC_GETGR_R_SIZE_MAX);
    if (n > 0) return (size_t)n;
#endif
    return 4096;
}

static int user_lookup_uid(uid_t uid, neverc_user_t *u) {
    struct passwd pwd;
    struct passwd *res = NULL;
    size_t bufsz = user_pw_bufsize();
    char *buf = (char *)malloc(bufsz);
    if (!buf) return -1;
    int rc;
    for (;;) {
        rc = getpwuid_r(uid, &pwd, buf, bufsz, &res);
        if (rc != ERANGE) break;
        if (bufsz > SIZE_MAX / 2) { free(buf); return -1; }
        bufsz *= 2;
        char *grown = (char *)realloc(buf, bufsz);
        if (!grown) { free(buf); return -1; }
        buf = grown;
    }
    if (rc != 0 || !res) { free(buf); return -1; }
    user_fill_from_passwd(res, u);
    free(buf);
    return 0;
}

int neverc_user_current(neverc_user_t *u) {
    if (!u) return -1;
    return user_lookup_uid(getuid(), u);
}

int neverc_user_lookup(const char *username, neverc_user_t *u) {
    if (!username || username[0] == '\0' || !u) return -1;
    struct passwd pwd;
    struct passwd *res = NULL;
    size_t bufsz = user_pw_bufsize();
    char *buf = (char *)malloc(bufsz);
    if (!buf) return -1;
    int rc;
    for (;;) {
        rc = getpwnam_r(username, &pwd, buf, bufsz, &res);
        if (rc != ERANGE) break;
        if (bufsz > SIZE_MAX / 2) { free(buf); return -1; }
        bufsz *= 2;
        char *grown = (char *)realloc(buf, bufsz);
        if (!grown) { free(buf); return -1; }
        buf = grown;
    }
    if (rc != 0 || !res) { free(buf); return -1; }
    user_fill_from_passwd(res, u);
    free(buf);
    return 0;
}

int neverc_user_lookup_id(int uid, neverc_user_t *u) {
    if (!u || uid < 0) return -1;
    return user_lookup_uid((uid_t)uid, u);
}

static int user_fill_group(const struct group *gr, neverc_group_t *g) {
    memset(g, 0, sizeof(*g));
    snprintf(g->gid, sizeof(g->gid), "%u", (unsigned)gr->gr_gid);
    snprintf(g->name, sizeof(g->name), "%s", gr->gr_name ? gr->gr_name : "");
    return 0;
}

int neverc_user_lookup_group(const char *name, neverc_group_t *g) {
    if (!name || name[0] == '\0' || !g) return -1;
    struct group grp;
    struct group *res = NULL;
    size_t bufsz = user_gr_bufsize();
    char *buf = (char *)malloc(bufsz);
    if (!buf) return -1;
    int rc;
    for (;;) {
        rc = getgrnam_r(name, &grp, buf, bufsz, &res);
        if (rc != ERANGE) break;
        if (bufsz > SIZE_MAX / 2) { free(buf); return -1; }
        bufsz *= 2;
        char *grown = (char *)realloc(buf, bufsz);
        if (!grown) { free(buf); return -1; }
        buf = grown;
    }
    if (rc != 0 || !res) { free(buf); return -1; }
    user_fill_group(res, g);
    free(buf);
    return 0;
}

int neverc_user_lookup_group_id(int gid, neverc_group_t *g) {
    if (!g || gid < 0) return -1;
    struct group grp;
    struct group *res = NULL;
    size_t bufsz = user_gr_bufsize();
    char *buf = (char *)malloc(bufsz);
    if (!buf) return -1;
    int rc;
    for (;;) {
        rc = getgrgid_r((gid_t)gid, &grp, buf, bufsz, &res);
        if (rc != ERANGE) break;
        if (bufsz > SIZE_MAX / 2) { free(buf); return -1; }
        bufsz *= 2;
        char *grown = (char *)realloc(buf, bufsz);
        if (!grown) { free(buf); return -1; }
        buf = grown;
    }
    if (rc != 0 || !res) { free(buf); return -1; }
    user_fill_group(res, g);
    free(buf);
    return 0;
}

const char *neverc_user_home_dir(void) {
    static char buf[1024];
    const char *h = getenv("HOME");
    if (h && h[0]) { snprintf(buf, sizeof(buf), "%s", h); return buf; }
    neverc_user_t u;
    if (user_lookup_uid(getuid(), &u) == 0 && u.home_dir[0]) {
        snprintf(buf, sizeof(buf), "%s", u.home_dir);
        return buf;
    }
    return "";
}

const char *neverc_user_cache_dir(void) {
    static char buf[1024];
#if defined(NEVERC_PLATFORM_APPLE)
    const char *home = neverc_user_home_dir();
    if (!home || !home[0]) return "";
    snprintf(buf, sizeof(buf), "%s/Library/Caches", home);
#else
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0]) { snprintf(buf, sizeof(buf), "%s", xdg); return buf; }
    const char *home = neverc_user_home_dir();
    if (!home || !home[0]) return "";
    snprintf(buf, sizeof(buf), "%s/.cache", home);
#endif
    return buf;
}

const char *neverc_user_config_dir(void) {
    static char buf[1024];
#if defined(NEVERC_PLATFORM_APPLE)
    const char *home = neverc_user_home_dir();
    if (!home || !home[0]) return "";
    snprintf(buf, sizeof(buf), "%s/Library/Application Support", home);
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) { snprintf(buf, sizeof(buf), "%s", xdg); return buf; }
    const char *home = neverc_user_home_dir();
    if (!home || !home[0]) return "";
    snprintf(buf, sizeof(buf), "%s/.config", home);
#endif
    return buf;
}

#endif /* POSIX */
