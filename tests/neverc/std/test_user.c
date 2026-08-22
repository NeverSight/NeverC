#include "neverc/std/os/user.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(expr) do { \
    tests_run++; \
    if (expr) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s\n", __LINE__, #expr); } \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a), _b = (b); tests_run++; \
    if (_a == _b) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s = %d, expected %d\n", __LINE__, #a, _a, _b); } \
} while(0)

static void test_current(void) {
    printf("[current]\n");
    neverc_user_t u;
    int rc = neverc_user_current(&u);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_TRUE(strlen(u.uid) > 0);
    ASSERT_TRUE(strlen(u.username) > 0);
    ASSERT_TRUE(strlen(u.home_dir) > 0);
    ASSERT_TRUE(strchr(u.name, ',') == NULL);
    printf("  uid=%s gid=%s user=%s home=%s\n", u.uid, u.gid, u.username, u.home_dir);
}

static void test_lookup(void) {
    printf("[lookup]\n");
    neverc_user_t current;
    if (neverc_user_current(&current) != 0) return;

    neverc_user_t looked_up;
    int rc = neverc_user_lookup(current.username, &looked_up);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_TRUE(strcmp(looked_up.username, current.username) == 0);
    ASSERT_TRUE(strcmp(looked_up.uid, current.uid) == 0);
    ASSERT_TRUE(strcmp(looked_up.home_dir, current.home_dir) == 0);
}

static void test_lookup_id(void) {
    printf("[lookup_id]\n");
    neverc_user_t current;
    if (neverc_user_current(&current) != 0) return;

    int uid = atoi(current.uid);
    neverc_user_t looked_up;
    int rc = neverc_user_lookup_id(uid, &looked_up);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_TRUE(strcmp(looked_up.uid, current.uid) == 0);
}

static void test_lookup_nonexistent(void) {
    printf("[lookup_nonexistent]\n");
    neverc_user_t u;
    memset(&u, 0xAA, sizeof(u));
    int rc = neverc_user_lookup("__neverc_nonexistent_user_12345__", &u);
    ASSERT_EQ_INT(rc, -1);
    ASSERT_TRUE(u.uid[0] == '\0');
    ASSERT_TRUE(u.username[0] == '\0');
    ASSERT_TRUE(u.home_dir[0] == '\0');

    memset(&u, 0xAA, sizeof(u));
    ASSERT_EQ_INT(neverc_user_lookup("", &u), -1);
    ASSERT_TRUE(u.username[0] == '\0');

    neverc_group_t g;
    memset(&g, 0xAA, sizeof(g));
    ASSERT_EQ_INT(neverc_user_lookup_group("__neverc_nonexistent_group_12345__",
                                          &g), -1);
    ASSERT_TRUE(g.name[0] == '\0');
    ASSERT_TRUE(g.gid[0] == '\0');
}

static void test_lookup_group(void) {
    printf("[lookup_group]\n");
    neverc_group_t g;
    int rc = -1;
#if defined(_WIN32)
    rc = neverc_user_lookup_group("Administrators", &g);
    if (rc != 0)
        rc = neverc_user_lookup_group("Users", &g);
#elif defined(__APPLE__) || defined(__linux__)
    rc = neverc_user_lookup_group("staff", &g);
    if (rc != 0)
        rc = neverc_user_lookup_group("users", &g);
    if (rc != 0)
        rc = neverc_user_lookup_group("root", &g);
#endif
    if (rc == 0) {
        ASSERT_TRUE(strlen(g.name) > 0);
        ASSERT_TRUE(strlen(g.gid) > 0);
    } else {
        (void)g;
        tests_run++;
        tests_passed++;
    }
#if !defined(_WIN32)
    {
        neverc_group_t zero;
        int zrc = neverc_user_lookup_group_id(0, &zero);
        ASSERT_EQ_INT(zrc, 0);
        ASSERT_TRUE(strlen(zero.name) > 0);
        ASSERT_TRUE(strlen(zero.gid) > 0);
    }
#endif
}

static void test_home_dir(void) {
    printf("[home_dir]\n");
    const char *home = neverc_user_home_dir();
    ASSERT_TRUE(home != NULL);
    ASSERT_TRUE(strlen(home) > 0);
    printf("  home=%s\n", home);

#if !defined(_WIN32)
    const char *old = getenv("HOME");
    char *saved = old ? strdup(old) : NULL;
    setenv("HOME", "", 1);
    home = neverc_user_home_dir();
    ASSERT_TRUE(home != NULL && home[0] != '\0');
    const char *cache = neverc_user_cache_dir();
    ASSERT_TRUE(cache != NULL && strcmp(cache, "/Library/Caches") != 0);
    ASSERT_TRUE(strcmp(cache, "/.cache") != 0);
    const char *config = neverc_user_config_dir();
    ASSERT_TRUE(config != NULL && strcmp(config, "/Library/Application Support") != 0);
    ASSERT_TRUE(strcmp(config, "/.config") != 0);
    if (saved) {
        setenv("HOME", saved, 1);
        free(saved);
    } else {
        unsetenv("HOME");
    }

    {
        const char *cur = getenv("HOME");
        char *restore = cur ? strdup(cur) : NULL;
        char longhome[2048];
        memset(longhome, 'x', 2000);
        longhome[0] = '/';
        longhome[2000] = '\0';
        setenv("HOME", longhome, 1);
        home = neverc_user_home_dir();
        ASSERT_TRUE(home != NULL);
        /* Must not silently truncate into the 1024-byte static buffer. */
        ASSERT_TRUE(!(home[0] == '/' && home[1] == 'x' && strlen(home) == 1023));
        cache = neverc_user_cache_dir();
        ASSERT_TRUE(cache != NULL);
        ASSERT_TRUE(!(cache[0] == '/' && cache[1] == 'x'));
        config = neverc_user_config_dir();
        ASSERT_TRUE(config != NULL);
        ASSERT_TRUE(!(config[0] == '/' && config[1] == 'x'));
        if (restore) {
            setenv("HOME", restore, 1);
            free(restore);
        } else {
            unsetenv("HOME");
        }
    }
#endif
}

static void test_cache_dir(void) {
    printf("[cache_dir]\n");
    const char *cache = neverc_user_cache_dir();
    ASSERT_TRUE(cache != NULL);
    ASSERT_TRUE(strlen(cache) > 0);
    printf("  cache=%s\n", cache);

#if !defined(_WIN32) && !defined(__APPLE__)
    {
        const char *old = getenv("XDG_CACHE_HOME");
        char *saved = old ? strdup(old) : NULL;
        setenv("XDG_CACHE_HOME", "rel/cache", 1);
        cache = neverc_user_cache_dir();
        ASSERT_TRUE(cache != NULL && cache[0] == '\0');
        setenv("XDG_CACHE_HOME", ".", 1);
        cache = neverc_user_cache_dir();
        ASSERT_TRUE(cache != NULL && cache[0] == '\0');
        if (saved) {
            setenv("XDG_CACHE_HOME", saved, 1);
            free(saved);
        } else {
            unsetenv("XDG_CACHE_HOME");
        }
    }
#endif

#if !defined(_WIN32)
    {
        const char *old_cache = getenv("XDG_CACHE_HOME");
        const char *old_cfg = getenv("XDG_CONFIG_HOME");
        const char *old_home = getenv("HOME");
        char *saved_cache = old_cache ? strdup(old_cache) : NULL;
        char *saved_cfg = old_cfg ? strdup(old_cfg) : NULL;
        char *saved_home = old_home ? strdup(old_home) : NULL;
        unsetenv("XDG_CACHE_HOME");
        unsetenv("XDG_CONFIG_HOME");
        setenv("HOME", "relhome", 1);
        cache = neverc_user_cache_dir();
        ASSERT_TRUE(cache != NULL && cache[0] == '\0');
        const char *config = neverc_user_config_dir();
        ASSERT_TRUE(config != NULL && config[0] == '\0');
        if (saved_cache)
            setenv("XDG_CACHE_HOME", saved_cache, 1);
        else
            unsetenv("XDG_CACHE_HOME");
        if (saved_cfg)
            setenv("XDG_CONFIG_HOME", saved_cfg, 1);
        else
            unsetenv("XDG_CONFIG_HOME");
        if (saved_home)
            setenv("HOME", saved_home, 1);
        else
            unsetenv("HOME");
        free(saved_cache);
        free(saved_cfg);
        free(saved_home);
    }
#endif
}

static void test_config_dir(void) {
    printf("[config_dir]\n");
    const char *config = neverc_user_config_dir();
    ASSERT_TRUE(config != NULL);
    ASSERT_TRUE(strlen(config) > 0);
    printf("  config=%s\n", config);

#if !defined(_WIN32) && !defined(__APPLE__)
    {
        const char *old = getenv("XDG_CONFIG_HOME");
        char *saved = old ? strdup(old) : NULL;
        setenv("XDG_CONFIG_HOME", "rel/config", 1);
        config = neverc_user_config_dir();
        ASSERT_TRUE(config != NULL && config[0] == '\0');
        if (saved) {
            setenv("XDG_CONFIG_HOME", saved, 1);
            free(saved);
        } else {
            unsetenv("XDG_CONFIG_HOME");
        }
    }
#endif
}

static void test_null_args(void) {
    printf("[null_args]\n");
    ASSERT_EQ_INT(neverc_user_current(NULL), -1);
    ASSERT_EQ_INT(neverc_user_lookup(NULL, NULL), -1);
    ASSERT_EQ_INT(neverc_user_lookup_id(0, NULL), -1);
    ASSERT_EQ_INT(neverc_user_lookup_group(NULL, NULL), -1);
    ASSERT_EQ_INT(neverc_user_lookup_group_id(0, NULL), -1);

    neverc_user_t u;
    neverc_group_t g;
    memset(&u, 0xAA, sizeof(u));
    memset(&g, 0xAA, sizeof(g));
    ASSERT_EQ_INT(neverc_user_lookup("", &u), -1);
    ASSERT_TRUE(u.username[0] == '\0');
    ASSERT_EQ_INT(neverc_user_lookup_id(-1, &u), -1);
    ASSERT_TRUE(u.uid[0] == '\0');
    ASSERT_EQ_INT(neverc_user_lookup_group("", &g), -1);
    ASSERT_TRUE(g.name[0] == '\0');
    ASSERT_EQ_INT(neverc_user_lookup_group_id(-1, &g), -1);
    ASSERT_TRUE(g.gid[0] == '\0');
}

int main(void) {
    printf("NeverC os/user tests\n");
    test_current();
    test_lookup();
#if !defined(_WIN32)
    test_lookup_id();
#else
    printf("[lookup_id] SKIP on Windows (SID is not an int)\n");
    tests_run++; tests_passed++;
#endif
    test_lookup_nonexistent();
    test_lookup_group();
    test_home_dir();
    test_cache_dir();
    test_config_dir();
    test_null_args();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
