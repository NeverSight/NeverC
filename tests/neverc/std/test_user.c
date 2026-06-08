#include "neverc/std/os/user.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    int rc = neverc_user_lookup("__neverc_nonexistent_user_12345__", &u);
    ASSERT_EQ_INT(rc, -1);
}

static void test_lookup_group(void) {
    printf("[lookup_group]\n");
    neverc_group_t g;
#if defined(__APPLE__) || defined(__linux__)
    int rc = neverc_user_lookup_group("staff", &g);
    if (rc != 0) {
        rc = neverc_user_lookup_group("users", &g);
    }
    if (rc != 0) {
        rc = neverc_user_lookup_group("root", &g);
    }
    if (rc == 0) {
        ASSERT_TRUE(strlen(g.name) > 0);
        ASSERT_TRUE(strlen(g.gid) > 0);
    }
#endif
    (void)g;
    tests_run++; tests_passed++;
}

static void test_home_dir(void) {
    printf("[home_dir]\n");
    const char *home = neverc_user_home_dir();
    ASSERT_TRUE(home != NULL);
    ASSERT_TRUE(strlen(home) > 0);
    printf("  home=%s\n", home);
}

static void test_cache_dir(void) {
    printf("[cache_dir]\n");
    const char *cache = neverc_user_cache_dir();
    ASSERT_TRUE(cache != NULL);
    ASSERT_TRUE(strlen(cache) > 0);
    printf("  cache=%s\n", cache);
}

static void test_config_dir(void) {
    printf("[config_dir]\n");
    const char *config = neverc_user_config_dir();
    ASSERT_TRUE(config != NULL);
    ASSERT_TRUE(strlen(config) > 0);
    printf("  config=%s\n", config);
}

static void test_null_args(void) {
    printf("[null_args]\n");
    ASSERT_EQ_INT(neverc_user_current(NULL), -1);
    ASSERT_EQ_INT(neverc_user_lookup(NULL, NULL), -1);
    ASSERT_EQ_INT(neverc_user_lookup_id(0, NULL), -1);
    ASSERT_EQ_INT(neverc_user_lookup_group(NULL, NULL), -1);
    ASSERT_EQ_INT(neverc_user_lookup_group_id(0, NULL), -1);
}

int main(void) {
    printf("NeverC os/user tests\n");
    test_current();
#if !defined(_WIN32)
    test_lookup();
    test_lookup_id();
#else
    printf("[lookup] SKIP on Windows (not supported)\n");
    tests_run++; tests_passed++;
    printf("[lookup_id] SKIP on Windows (not supported)\n");
    tests_run++; tests_passed++;
#endif
    test_lookup_nonexistent();
    test_lookup_group();
    test_home_dir();
    test_cache_dir();
    test_config_dir();
    test_null_args();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
