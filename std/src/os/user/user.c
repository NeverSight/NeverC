#include "neverc/std/os/user.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int user_copy_env_path(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0 || !src || src[0] == '\0') return -1;
    size_t n = strlen(src);
    if (n >= cap) return -1;
    memcpy(dst, src, n + 1);
    return 0;
}

static int user_path_isabs(const char *p) {
    if (!p || !p[0]) return 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    if ((p[0] == '\\' && p[1] == '\\') || (p[0] == '/' && p[1] == '/'))
        return 1;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
        p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
        return 1;
    return 0;
#else
    return p[0] == '/';
#endif
}

/* Empty src is allowed (unlike env paths). Truncation is failure so a
 * shortened home/SID/username cannot be treated as the real value. */
static int user_copy_field(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return -1;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) return -1;
    memcpy(dst, src, n + 1);
    return 0;
}

#if defined(NEVERC_PLATFORM_WINDOWS)
#include <windows.h>
#include <sddl.h>
#include <lmcons.h>
#include <shlobj.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

int neverc_user_current(neverc_user_t *u) {
    int invalid = 0;
    if (!u) return -1;
    memset(u, 0, sizeof(*u));
    neverc_user_t tmp;
    memset(&tmp, 0, sizeof(tmp));

    DWORD size = sizeof(tmp.username);
    if (!GetUserNameA(tmp.username, &size)) return -1;

    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return -1;

    DWORD tok_len = 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &tok_len);
    TOKEN_USER *tok_user = (TOKEN_USER *)malloc(tok_len);
    if (!tok_user) { CloseHandle(token); return -1; }

    if (GetTokenInformation(token, TokenUser, tok_user, tok_len, &tok_len)) {
        char *sid_str = NULL;
        if (ConvertSidToStringSidA(tok_user->User.Sid, &sid_str)) {
            if (user_copy_field(tmp.uid, sizeof(tmp.uid), sid_str) != 0)
                invalid = 1;
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
            if (user_copy_field(tmp.gid, sizeof(tmp.gid), gid_str) != 0)
                invalid = 1;
            LocalFree(gid_str);
        }
    }
    free(tok_group);
    CloseHandle(token);

    if (user_copy_field(tmp.name, sizeof(tmp.name), tmp.username) != 0)
        invalid = 1;

    char home[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, home) == S_OK) {
        if (user_copy_field(tmp.home_dir, sizeof(tmp.home_dir), home) != 0)
            invalid = 1;
    }

    if (invalid || !tmp.username[0] || !tmp.uid[0])
        return -1;
    *u = tmp;
    return 0;
}

static int user_windows_lookup_account(const char *account, char *sid_buf,
                                       size_t sid_cap, char *name_buf,
                                       size_t name_cap, SID_NAME_USE *use_out) {
    DWORD sid_sz = 0, domain_sz = 0;
    SID_NAME_USE use = SidTypeUnknown;
    PSID sid = NULL;
    char *domain = NULL;
    char *sid_str = NULL;
    int rc = -1;
    if (!account || !account[0] || !sid_buf || sid_cap == 0) return -1;
    LookupAccountNameA(NULL, account, NULL, &sid_sz, NULL, &domain_sz, &use);
    if (sid_sz == 0) return -1;
    sid = (PSID)malloc(sid_sz);
    domain = (char *)malloc(domain_sz ? domain_sz : 1);
    if (!sid || !domain) goto done;
    if (!LookupAccountNameA(NULL, account, sid, &sid_sz, domain, &domain_sz, &use))
        goto done;
    if (!ConvertSidToStringSidA(sid, &sid_str)) goto done;
    rc = user_copy_field(sid_buf, sid_cap, sid_str);
    LocalFree(sid_str);
    if (rc != 0) goto done;
    if (name_buf && name_cap &&
        user_copy_field(name_buf, name_cap, account) != 0) {
        sid_buf[0] = '\0';
        rc = -1;
        goto done;
    }
    if (use_out) *use_out = use;
    rc = sid_buf[0] ? 0 : -1;
done:
    free(sid);
    free(domain);
    return rc;
}

int neverc_user_lookup(const char *username, neverc_user_t *u) {
    neverc_user_t current;
    neverc_user_t tmp;
    SID_NAME_USE use = SidTypeUnknown;
    int have_current;
    if (!u) return -1;
    memset(u, 0, sizeof(*u));
    if (!username || username[0] == '\0') return -1;
    memset(&tmp, 0, sizeof(tmp));
    have_current = neverc_user_current(&current) == 0;
    if (have_current && _stricmp(current.username, username) == 0) {
        *u = current;
        return 0;
    }
    if (user_windows_lookup_account(username, tmp.uid, sizeof(tmp.uid),
                                    tmp.username, sizeof(tmp.username),
                                    &use) != 0)
        return -1;
    if (use != SidTypeUser && use != SidTypeDeletedAccount)
        return -1;
    if (have_current && _stricmp(current.uid, tmp.uid) == 0) {
        *u = current;
        return 0;
    }
    if (user_copy_field(tmp.name, sizeof(tmp.name), tmp.username) != 0)
        return -1;
    *u = tmp;
    return 0;
}

int neverc_user_lookup_id(int uid, neverc_user_t *u) {
    (void)uid;
    if (!u) return -1;
    memset(u, 0, sizeof(*u));
    return -1;
}

int neverc_user_lookup_group(const char *name, neverc_group_t *g) {
    SID_NAME_USE use = SidTypeUnknown;
    if (!g) return -1;
    memset(g, 0, sizeof(*g));
    if (!name || name[0] == '\0') return -1;
    if (user_windows_lookup_account(name, g->gid, sizeof(g->gid),
                                    g->name, sizeof(g->name), &use) != 0)
        return -1;
    if (use != SidTypeGroup && use != SidTypeWellKnownGroup &&
        use != SidTypeAlias && use != SidTypeLabel)
        return -1;
    return 0;
}

int neverc_user_lookup_group_id(int gid, neverc_group_t *g) {
    (void)gid;
    if (!g) return -1;
    memset(g, 0, sizeof(*g));
    return -1;
}

const char *neverc_user_home_dir(void) {
    static char buf[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, buf) == S_OK)
        return buf;
    if (user_copy_env_path(buf, sizeof(buf), getenv("USERPROFILE")) == 0)
        return buf;
    return "";
}

const char *neverc_user_cache_dir(void) {
    static char buf[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, buf) == S_OK &&
        user_path_isabs(buf))
        return buf;
    if (user_copy_env_path(buf, sizeof(buf), getenv("LOCALAPPDATA")) == 0 &&
        user_path_isabs(buf))
        return buf;
    buf[0] = '\0';
    return "";
}

const char *neverc_user_config_dir(void) {
    static char buf[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, buf) == S_OK &&
        user_path_isabs(buf))
        return buf;
    if (user_copy_env_path(buf, sizeof(buf), getenv("APPDATA")) == 0 &&
        user_path_isabs(buf))
        return buf;
    buf[0] = '\0';
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

static int user_copy_gecos(char *dst, size_t cap, const char *gecos) {
    size_t n = 0;
    if (!dst || cap == 0) return -1;
    if (!gecos) gecos = "";
    while (gecos[n] && gecos[n] != ',') {
        if (n + 1 >= cap) {
            dst[0] = '\0';
            return -1;
        }
        dst[n] = gecos[n];
        n++;
    }
    dst[n] = '\0';
    return 0;
}

static int user_format_uid(char *dst, size_t cap, unsigned v) {
    int n = snprintf(dst, cap, "%u", v);
    if (n < 0 || (size_t)n >= cap) {
        if (dst && cap) dst[0] = '\0';
        return -1;
    }
    return 0;
}

static int user_fill_from_passwd(const struct passwd *pw, neverc_user_t *u) {
    neverc_user_t tmp;
    if (!pw || !u) return -1;
    memset(&tmp, 0, sizeof(tmp));
    if (user_format_uid(tmp.uid, sizeof(tmp.uid), (unsigned)pw->pw_uid) != 0)
        return -1;
    if (user_format_uid(tmp.gid, sizeof(tmp.gid), (unsigned)pw->pw_gid) != 0)
        return -1;
    if (user_copy_field(tmp.username, sizeof(tmp.username),
                        pw->pw_name ? pw->pw_name : "") != 0)
        return -1;
    if (tmp.username[0] == '\0') return -1;
    if (user_copy_gecos(tmp.name, sizeof(tmp.name), pw->pw_gecos) != 0)
        return -1;
    if (user_copy_field(tmp.home_dir, sizeof(tmp.home_dir),
                        pw->pw_dir ? pw->pw_dir : "") != 0)
        return -1;
    *u = tmp;
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
    int fill = user_fill_from_passwd(res, u);
    free(buf);
    return fill;
}

int neverc_user_current(neverc_user_t *u) {
    if (!u) return -1;
    memset(u, 0, sizeof(*u));
    return user_lookup_uid(getuid(), u);
}

int neverc_user_lookup(const char *username, neverc_user_t *u) {
    if (!u) return -1;
    memset(u, 0, sizeof(*u));
    if (!username || username[0] == '\0') return -1;
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
    int fill = user_fill_from_passwd(res, u);
    free(buf);
    return fill;
}

int neverc_user_lookup_id(int uid, neverc_user_t *u) {
    if (!u) return -1;
    memset(u, 0, sizeof(*u));
    if (uid < 0) return -1;
    return user_lookup_uid((uid_t)uid, u);
}

static int user_fill_group(const struct group *gr, neverc_group_t *g) {
    neverc_group_t tmp;
    if (!gr || !g) return -1;
    memset(&tmp, 0, sizeof(tmp));
    if (user_format_uid(tmp.gid, sizeof(tmp.gid), (unsigned)gr->gr_gid) != 0)
        return -1;
    if (user_copy_field(tmp.name, sizeof(tmp.name),
                        gr->gr_name ? gr->gr_name : "") != 0)
        return -1;
    if (tmp.name[0] == '\0') return -1;
    *g = tmp;
    return 0;
}

int neverc_user_lookup_group(const char *name, neverc_group_t *g) {
    if (!g) return -1;
    memset(g, 0, sizeof(*g));
    if (!name || name[0] == '\0') return -1;
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
    int fill = user_fill_group(res, g);
    free(buf);
    return fill;
}

int neverc_user_lookup_group_id(int gid, neverc_group_t *g) {
    if (!g) return -1;
    memset(g, 0, sizeof(*g));
    if (gid < 0) return -1;
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
    int fill = user_fill_group(res, g);
    free(buf);
    return fill;
}

static int user_format_under(char *dst, size_t cap, const char *fmt, const char *a) {
    if (!dst || cap == 0 || !a) return -1;
    int n = snprintf(dst, cap, fmt, a);
    if (n < 0 || (size_t)n >= cap) {
        dst[0] = '\0';
        return -1;
    }
    return 0;
}

const char *neverc_user_home_dir(void) {
    static char buf[1024];
    const char *h = getenv("HOME");
    if (user_copy_env_path(buf, sizeof(buf), h) == 0)
        return buf;
    neverc_user_t u;
    if (user_lookup_uid(getuid(), &u) == 0 &&
        user_copy_env_path(buf, sizeof(buf), u.home_dir) == 0)
        return buf;
    return "";
}

const char *neverc_user_cache_dir(void) {
    static char buf[1024];
#if defined(NEVERC_PLATFORM_APPLE)
    const char *home = neverc_user_home_dir();
    if (!home || !home[0]) return "";
    if (user_format_under(buf, sizeof(buf), "%s/Library/Caches", home) != 0)
        return "";
#else
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0]) {
        /* Go UserCacheDir: relative XDG_* is an error, not a leftover cwd. */
        if (xdg[0] != '/') return "";
        if (user_copy_env_path(buf, sizeof(buf), xdg) != 0)
            return "";
    } else {
        const char *home = neverc_user_home_dir();
        if (!home || !home[0]) return "";
        if (user_format_under(buf, sizeof(buf), "%s/.cache", home) != 0)
            return "";
    }
#endif
    if (!user_path_isabs(buf)) {
        buf[0] = '\0';
        return "";
    }
    return buf;
}

const char *neverc_user_config_dir(void) {
    static char buf[1024];
#if defined(NEVERC_PLATFORM_APPLE)
    const char *home = neverc_user_home_dir();
    if (!home || !home[0]) return "";
    if (user_format_under(buf, sizeof(buf), "%s/Library/Application Support",
                          home) != 0)
        return "";
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        if (xdg[0] != '/') return "";
        if (user_copy_env_path(buf, sizeof(buf), xdg) != 0)
            return "";
    } else {
        const char *home = neverc_user_home_dir();
        if (!home || !home[0]) return "";
        if (user_format_under(buf, sizeof(buf), "%s/.config", home) != 0)
            return "";
    }
#endif
    if (!user_path_isabs(buf)) {
        buf[0] = '\0';
        return "";
    }
    return buf;
}

#endif /* POSIX */
