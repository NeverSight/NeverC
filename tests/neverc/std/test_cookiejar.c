#include "neverc/std/net/http/cookiejar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got == NULL && expected == NULL) { tests_passed++; return; }
    if (got == NULL || expected == NULL) {
        tests_failed++;
        printf("  FAIL: %s: got %s, expected %s\n", name,
               got ? got : "NULL", expected ? expected : "NULL");
        return;
    }
    if (strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
}

static void check_not_null(const char *name, const void *ptr) {
    tests_run++;
    if (ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got NULL\n", name); }
}

/* ===== Basic operations ===== */

static void test_basic(void) {
    printf("[basic]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    check_not_null("new jar", jar);
    check_int("empty count", neverc_cookiejar_count(jar), 0);

    neverc_cookiejar_entry_t c = {
        .name = "session",
        .value = "abc123",
        .domain = NULL,
        .path = "/",
        .expires = 0,
        .secure = 0,
        .http_only = 0,
    };
    neverc_cookiejar_set_cookies(jar, "https://example.com/login", &c, 1);
    check_int("count=1", neverc_cookiejar_count(jar), 1);

    neverc_cookiejar_entry_t out[10];
    int n = neverc_cookiejar_cookies(jar, "https://example.com/page", out, 10);
    check_int("get 1 cookie", n, 1);
    if (n > 0) {
        check_str("name", out[0].name, "session");
        check_str("value", out[0].value, "abc123");
    }

    neverc_cookiejar_free(jar);
}

/* ===== Domain matching ===== */

static void test_domain_matching(void) {
    printf("[domain_matching]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();

    neverc_cookiejar_entry_t c1 = {
        .name = "site", .value = "1",
        .domain = ".example.com", .path = "/",
    };
    neverc_cookiejar_entry_t c2 = {
        .name = "other", .value = "2",
        .domain = ".other.com", .path = "/",
    };
    neverc_cookiejar_set_cookies(jar, "https://example.com/", &c1, 1);
    neverc_cookiejar_set_cookies(jar, "https://other.com/", &c2, 1);
    check_int("count=2", neverc_cookiejar_count(jar), 2);

    neverc_cookiejar_entry_t out[10];

    int n = neverc_cookiejar_cookies(jar, "https://example.com/page", out, 10);
    check_int("match example.com", n, 1);
    if (n > 0) check_str("match name", out[0].name, "site");

    n = neverc_cookiejar_cookies(jar, "https://sub.example.com/page", out, 10);
    check_int("match sub.example.com", n, 1);

    n = neverc_cookiejar_cookies(jar, "https://other.com/", out, 10);
    check_int("match other.com", n, 1);
    if (n > 0) check_str("match other", out[0].name, "other");

    n = neverc_cookiejar_cookies(jar, "https://notexample.com/", out, 10);
    check_int("no match notexample.com", n, 0);

    neverc_cookiejar_free(jar);
}

/* ===== Path matching ===== */

static void test_path_matching(void) {
    printf("[path_matching]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();

    neverc_cookiejar_entry_t c1 = {
        .name = "root", .value = "r", .domain = "example.com", .path = "/",
    };
    neverc_cookiejar_entry_t c2 = {
        .name = "api", .value = "a", .domain = "example.com", .path = "/api",
    };
    neverc_cookiejar_set_cookies(jar, "https://example.com/", &c1, 1);
    neverc_cookiejar_set_cookies(jar, "https://example.com/api", &c2, 1);

    neverc_cookiejar_entry_t out[10];

    int n = neverc_cookiejar_cookies(jar, "https://example.com/", out, 10);
    check_int("root path only", n, 1);

    n = neverc_cookiejar_cookies(jar, "https://example.com/api/v1", out, 10);
    check_int("api path matches both", n, 2);

    n = neverc_cookiejar_cookies(jar, "https://example.com/other", out, 10);
    check_int("other path root only", n, 1);

    neverc_cookiejar_free(jar);
}

/* ===== Secure flag ===== */

static void test_secure(void) {
    printf("[secure]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();

    neverc_cookiejar_entry_t c = {
        .name = "token", .value = "secret",
        .domain = "example.com", .path = "/",
        .secure = 1,
    };
    neverc_cookiejar_set_cookies(jar, "https://example.com/", &c, 1);

    neverc_cookiejar_entry_t out[10];

    int n = neverc_cookiejar_cookies(jar, "https://example.com/page", out, 10);
    check_int("secure over https", n, 1);

    n = neverc_cookiejar_cookies(jar, "http://example.com/page", out, 10);
    check_int("no secure over http", n, 0);

    neverc_cookiejar_free(jar);
}

/* ===== Set-Cookie header parsing ===== */

static void test_set_cookie_header(void) {
    printf("[set_cookie_header]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();

    neverc_cookiejar_set_cookie_header(jar, "https://example.com/",
        "session=abc123; Path=/; Domain=.example.com; Secure; HttpOnly");
    check_int("parsed count", neverc_cookiejar_count(jar), 1);

    neverc_cookiejar_entry_t out[10];
    int n = neverc_cookiejar_cookies(jar, "https://example.com/page", out, 10);
    check_int("parsed match", n, 1);
    if (n > 0) {
        check_str("parsed name", out[0].name, "session");
        check_str("parsed value", out[0].value, "abc123");
        check_int("parsed secure", out[0].secure, 1);
        check_int("parsed httponly", out[0].http_only, 1);
    }

    neverc_cookiejar_set_cookie_header(jar, "https://example.com/",
        "lang=en; Path=/");
    check_int("two cookies", neverc_cookiejar_count(jar), 2);

    neverc_cookiejar_free(jar);
}

/* ===== Cookie header generation ===== */

static void test_cookie_header(void) {
    printf("[cookie_header]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();

    neverc_cookiejar_set_cookie_header(jar, "https://example.com/",
        "a=1; Path=/");
    neverc_cookiejar_set_cookie_header(jar, "https://example.com/",
        "b=2; Path=/");

    char *hdr = neverc_cookiejar_cookie_header(jar, "https://example.com/page");
    check_not_null("header string", hdr);
    if (hdr) {
        check_int("has a=1", strstr(hdr, "a=1") != NULL, 1);
        check_int("has b=2", strstr(hdr, "b=2") != NULL, 1);
        check_int("has separator", strstr(hdr, "; ") != NULL, 1);
        free(hdr);
    }

    char *empty = neverc_cookiejar_cookie_header(jar, "https://other.com/");
    check_int("no match null", empty == NULL, 1);

    neverc_cookiejar_free(jar);
}

/* ===== Cookie update ===== */

static void test_update(void) {
    printf("[update]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();

    neverc_cookiejar_entry_t c = {
        .name = "token", .value = "old",
        .domain = "example.com", .path = "/",
    };
    neverc_cookiejar_set_cookies(jar, "https://example.com/", &c, 1);

    c.value = "new";
    neverc_cookiejar_set_cookies(jar, "https://example.com/", &c, 1);

    check_int("still 1 cookie", neverc_cookiejar_count(jar), 1);

    neverc_cookiejar_entry_t out[10];
    int n = neverc_cookiejar_cookies(jar, "https://example.com/", out, 10);
    check_int("get 1", n, 1);
    if (n > 0) check_str("updated value", out[0].value, "new");

    neverc_cookiejar_free(jar);
}

/* ===== Clear operations ===== */

static void test_clear(void) {
    printf("[clear]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();

    neverc_cookiejar_set_cookie_header(jar, "https://a.com/", "x=1; Path=/");
    neverc_cookiejar_set_cookie_header(jar, "https://b.com/", "y=2; Path=/");
    neverc_cookiejar_set_cookie_header(jar, "https://c.com/", "z=3; Path=/");
    check_int("3 cookies", neverc_cookiejar_count(jar), 3);

    neverc_cookiejar_clear_domain(jar, "b.com");
    check_int("after clear b.com", neverc_cookiejar_count(jar), 2);

    neverc_cookiejar_clear_all(jar);
    check_int("after clear all", neverc_cookiejar_count(jar), 0);

    neverc_cookiejar_free(jar);
}

/* ===== Null safety ===== */

static void test_null_safety(void) {
    printf("[null_safety]\n");

    neverc_cookiejar_free(NULL);
    neverc_cookiejar_set_cookies(NULL, "http://x.com/", NULL, 0);
    check_int("cookies null jar", neverc_cookiejar_cookies(NULL, "http://x.com/", NULL, 0), 0);
    check_int("count null jar", neverc_cookiejar_count(NULL), 0);
    neverc_cookiejar_clear_all(NULL);
    neverc_cookiejar_clear_domain(NULL, "x");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookies(jar, NULL, NULL, 0);
    check_int("cookies null url", neverc_cookiejar_cookies(jar, NULL, NULL, 0), 0);
    char *h = neverc_cookiejar_cookie_header(jar, NULL);
    check_int("header null url", h == NULL, 1);
    neverc_cookiejar_free(jar);

    tests_passed++; tests_run++;
}

/* ===== Multiple cookies at once ===== */

static void test_batch(void) {
    printf("[batch]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();

    neverc_cookiejar_entry_t batch[3] = {
        { .name = "a", .value = "1", .domain = "example.com", .path = "/" },
        { .name = "b", .value = "2", .domain = "example.com", .path = "/" },
        { .name = "c", .value = "3", .domain = "example.com", .path = "/api" },
    };
    neverc_cookiejar_set_cookies(jar, "https://example.com/", batch, 3);
    check_int("batch count", neverc_cookiejar_count(jar), 3);

    neverc_cookiejar_entry_t out[10];
    int n = neverc_cookiejar_cookies(jar, "https://example.com/", out, 10);
    check_int("batch root match", n, 2);

    n = neverc_cookiejar_cookies(jar, "https://example.com/api/v1", out, 10);
    check_int("batch api match", n, 3);

    neverc_cookiejar_free(jar);
}

int main(void) {
    printf("=== NeverC cookiejar tests ===\n");

    test_basic();
    test_domain_matching();
    test_path_matching();
    test_secure();
    test_set_cookie_header();
    test_cookie_header();
    test_update();
    test_clear();
    test_null_safety();
    test_batch();

    printf("\n--- cookiejar: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    return tests_failed > 0 ? 1 : 0;
}
