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

    return 0;
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

int neverc_user_current(neverc_user_t *u) {
    if (!u) return -1;
    memset(u, 0, sizeof(*u));

    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) return -1;

    snprintf(u->uid, sizeof(u->uid), "%u", (unsigned)uid);
    snprintf(u->gid, sizeof(u->gid), "%u", (unsigned)pw->pw_gid);
    snprintf(u->username, sizeof(u->username), "%s", pw->pw_name ? pw->pw_name : "");
    snprintf(u->name, sizeof(u->name), "%s", pw->pw_gecos ? pw->pw_gecos : "");
    snprintf(u->home_dir, sizeof(u->home_dir), "%s", pw->pw_dir ? pw->pw_dir : "");

    return 0;
}

int neverc_user_lookup(const char *username, neverc_user_t *u) {
    if (!username || !u) return -1;
    memset(u, 0, sizeof(*u));

    struct passwd *pw = getpwnam(username);
    if (!pw) return -1;

    snprintf(u->uid, sizeof(u->uid), "%u", (unsigned)pw->pw_uid);
    snprintf(u->gid, sizeof(u->gid), "%u", (unsigned)pw->pw_gid);
    snprintf(u->username, sizeof(u->username), "%s", pw->pw_name ? pw->pw_name : "");
    snprintf(u->name, sizeof(u->name), "%s", pw->pw_gecos ? pw->pw_gecos : "");
    snprintf(u->home_dir, sizeof(u->home_dir), "%s", pw->pw_dir ? pw->pw_dir : "");

    return 0;
}

int neverc_user_lookup_id(int uid, neverc_user_t *u) {
    if (!u) return -1;
    memset(u, 0, sizeof(*u));

    struct passwd *pw = getpwuid((uid_t)uid);
    if (!pw) return -1;

    snprintf(u->uid, sizeof(u->uid), "%u", (unsigned)pw->pw_uid);
    snprintf(u->gid, sizeof(u->gid), "%u", (unsigned)pw->pw_gid);
    snprintf(u->username, sizeof(u->username), "%s", pw->pw_name ? pw->pw_name : "");
    snprintf(u->name, sizeof(u->name), "%s", pw->pw_gecos ? pw->pw_gecos : "");
    snprintf(u->home_dir, sizeof(u->home_dir), "%s", pw->pw_dir ? pw->pw_dir : "");

    return 0;
}

int neverc_user_lookup_group(const char *name, neverc_group_t *g) {
    if (!name || !g) return -1;
    memset(g, 0, sizeof(*g));

    struct group *gr = getgrnam(name);
    if (!gr) return -1;

    snprintf(g->gid, sizeof(g->gid), "%u", (unsigned)gr->gr_gid);
    snprintf(g->name, sizeof(g->name), "%s", gr->gr_name ? gr->gr_name : "");

    return 0;
}

int neverc_user_lookup_group_id(int gid, neverc_group_t *g) {
    if (!g) return -1;
    memset(g, 0, sizeof(*g));

    struct group *gr = getgrgid((gid_t)gid);
    if (!gr) return -1;

    snprintf(g->gid, sizeof(g->gid), "%u", (unsigned)gr->gr_gid);
    snprintf(g->name, sizeof(g->name), "%s", gr->gr_name ? gr->gr_name : "");

    return 0;
}

const char *neverc_user_home_dir(void) {
    static char buf[1024];
    const char *h = getenv("HOME");
    if (h) { snprintf(buf, sizeof(buf), "%s", h); return buf; }
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) { snprintf(buf, sizeof(buf), "%s", pw->pw_dir); return buf; }
    return "";
}

const char *neverc_user_cache_dir(void) {
    static char buf[1024];
#if defined(NEVERC_PLATFORM_APPLE)
    const char *home = neverc_user_home_dir();
    snprintf(buf, sizeof(buf), "%s/Library/Caches", home);
#else
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0]) { snprintf(buf, sizeof(buf), "%s", xdg); return buf; }
    const char *home = neverc_user_home_dir();
    snprintf(buf, sizeof(buf), "%s/.cache", home);
#endif
    return buf;
}

const char *neverc_user_config_dir(void) {
    static char buf[1024];
#if defined(NEVERC_PLATFORM_APPLE)
    const char *home = neverc_user_home_dir();
    snprintf(buf, sizeof(buf), "%s/Library/Application Support", home);
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) { snprintf(buf, sizeof(buf), "%s", xdg); return buf; }
    const char *home = neverc_user_home_dir();
    snprintf(buf, sizeof(buf), "%s/.config", home);
#endif
    return buf;
}

#endif /* POSIX */
