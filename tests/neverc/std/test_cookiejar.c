#include "neverc/std/net/http/cookiejar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void test_public_suffix_domain(void) {
    printf("[public_suffix_domain]\n");

    neverc_cookiejar_entry_t cookie = {
        .name = "sid", .value = "x", .domain = "co.uk", .path = "/",
    };
    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookies(
        jar, "https://evil.co.uk/", &cookie, 1);
    check_int("reject Domain=co.uk from evil.co.uk",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://evil.co.uk/", "sid=x; Domain=co.uk; Path=/");
    check_int("reject Set-Cookie Domain=co.uk",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "example.co.uk";
    neverc_cookiejar_set_cookies(
        jar, "https://www.example.co.uk/", &cookie, 1);
    neverc_cookiejar_entry_t out[1];
    int n = neverc_cookiejar_cookies(
        jar, "https://www.example.co.uk/", out, 1);
    check_int("Domain=example.co.uk is not a public suffix", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://other.co.uk/", out, 1);
    check_int("example.co.uk cookie is not sent to other.co.uk", n, 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "com";
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &cookie, 1);
    check_int("reject Domain=com from example.com",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "github.io";
    neverc_cookiejar_set_cookies(
        jar, "https://evil.github.io/", &cookie, 1);
    check_int("reject Domain=github.io from evil.github.io",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "evil.github.io";
    neverc_cookiejar_set_cookies(
        jar, "https://www.evil.github.io/", &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://www.evil.github.io/", out, 1);
    check_int("Domain=evil.github.io is registrable", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://other.github.io/", out, 1);
    check_int("github.io pages do not share that cookie", n, 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "blogspot.com";
    neverc_cookiejar_set_cookies(
        jar, "https://evil.blogspot.com/", &cookie, 1);
    check_int("reject Domain=blogspot.com from evil.blogspot.com",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://evil.github.io/",
        "sid=x; Domain=github.io; Path=/");
    check_int("reject Set-Cookie Domain=github.io",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "blogspot.co.uk";
    neverc_cookiejar_set_cookies(
        jar, "https://evil.blogspot.co.uk/", &cookie, 1);
    check_int("reject Domain=blogspot.co.uk from evil.blogspot.co.uk",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "evil.blogspot.co.uk";
    neverc_cookiejar_set_cookies(
        jar, "https://www.evil.blogspot.co.uk/", &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://www.evil.blogspot.co.uk/", out, 1);
    check_int("Domain=evil.blogspot.co.uk is registrable", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://victim.blogspot.co.uk/", out, 1);
    check_int("blogspot.co.uk blogs do not share that cookie", n, 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "blogspot.de";
    neverc_cookiejar_set_cookies(
        jar, "https://evil.blogspot.de/", &cookie, 1);
    check_int("reject Domain=blogspot.de from evil.blogspot.de",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "k12.oh.us";
    neverc_cookiejar_set_cookies(
        jar, "https://evil.k12.oh.us/", &cookie, 1);
    check_int("reject Domain=k12.oh.us from evil.k12.oh.us",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "s3.eu-west-1.amazonaws.com";
    neverc_cookiejar_set_cookies(
        jar, "https://evil.s3.eu-west-1.amazonaws.com/", &cookie, 1);
    check_int("reject Domain=s3.eu-west-1.amazonaws.com",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "s3.ap-northeast-1.amazonaws.com";
    neverc_cookiejar_set_cookies(
        jar, "https://evil.s3.ap-northeast-1.amazonaws.com/", &cookie, 1);
    check_int("reject Domain=s3.ap-northeast-1.amazonaws.com",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "bucket.s3.eu-west-1.amazonaws.com";
    neverc_cookiejar_set_cookies(
        jar, "https://evil.bucket.s3.eu-west-1.amazonaws.com/", &cookie, 1);
    check_int("reject Domain=bucket.s3.eu-west-1.amazonaws.com",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "evil.bucket.s3.eu-west-1.amazonaws.com";
    neverc_cookiejar_set_cookies(
        jar, "https://www.evil.bucket.s3.eu-west-1.amazonaws.com/",
        &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://www.evil.bucket.s3.eu-west-1.amazonaws.com/", out, 1);
    check_int("regional S3 bucket host stays registrable", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://victim.bucket.s3.eu-west-1.amazonaws.com/", out, 1);
    check_int("regional S3 bucket cookie is not sent to sibling", n, 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "uk.com";
    neverc_cookiejar_set_cookies(
        jar, "https://evil.uk.com/", &cookie, 1);
    check_int("reject Domain=uk.com from evil.uk.com",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "pvt.k12.ma.us";
    neverc_cookiejar_set_cookies(
        jar, "https://school.pvt.k12.ma.us/", &cookie, 1);
    check_int("reject Domain=pvt.k12.ma.us",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "example.co.uk";
    neverc_cookiejar_set_cookies(
        jar, "https://www.example.co.uk/", &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://www.example.co.uk/", out, 1);
    check_int("Domain=example.co.uk stays registrable", n, 1);
    neverc_cookiejar_free(jar);
}

static void test_domain_security(void) {
    printf("[domain_security]\n");

    neverc_cookiejar_entry_t cookie = {
        .name = "session", .value = "secret", .domain = ".evil.com",
        .path = "/",
    };
    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &cookie, 1);
    check_int("reject cross-domain cookie", neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "example.com";
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &cookie, 1);
    neverc_cookiejar_entry_t out[1];
    int n = neverc_cookiejar_cookies(
        jar, "https://sub.example.com/", out, 1);
    check_int("domain cookie matches subdomain", n, 1);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = NULL;
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://sub.example.com/", out, 1);
    check_int("host-only cookie excludes subdomain", n, 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/", "session=x; Domain=evil.com; Path=/");
    check_int("reject cross-domain Set-Cookie",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    /* Go domainAndType: only one leading dot is stripped; "..example.com"
     * is malformed and must not become a domain cookie for example.com. */
    jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://www.example.com/",
        "sid=x; Domain=..example.com; Path=/");
    check_int("reject Domain=..example.com Set-Cookie",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "..example.com";
    neverc_cookiejar_set_cookies(
        jar, "https://www.example.com/", &cookie, 1);
    check_int("reject Domain=..example.com set_cookies",
              neverc_cookiejar_count(jar), 0);
    n = neverc_cookiejar_cookies(
        jar, "https://other.example.com/", out, 1);
    check_int("double-dot Domain does not leak to sibling", n, 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://www.example.com/", "sid=x; Domain=.; Path=/");
    check_int("reject Domain=.", neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_set_cookie_header(
        jar, "https://www.example.com/", "sid=x; Domain=..; Path=/");
    check_int("reject Domain=..", neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_set_cookie_header(
        jar, "https://www.example.com/",
        "sid=x; Domain=.example.com; Path=/");
    check_int("single leading dot still accepted",
              neverc_cookiejar_count(jar), 1);
    n = neverc_cookiejar_cookies(
        jar, "https://other.example.com/", out, 1);
    check_int("single leading dot is a domain cookie", n, 1);
    neverc_cookiejar_free(jar);

    /* Go isIP: a request-host containing '%' is not a DNS name, so it
     * cannot set Domain=www.example.com cookies via suffix matching. */
    jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://x%25.www.example.com/",
        "sid=x; Domain=www.example.com; Path=/");
    check_int("percent-host cannot set Domain=www.example.com",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_set_cookie_header(
        jar, "https://x%25.www.example.com/",
        "sid=x; Domain=example.com; Path=/");
    check_int("percent-host cannot set Domain=example.com",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);
}

static void test_http_schemes_only(void) {
    printf("[http_schemes_only]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_entry_t cookie = {
        .name = "sid", .value = "x", .path = "/",
    };
    neverc_cookiejar_set_cookies(jar, "ftp://example.com/", &cookie, 1);
    check_int("reject ftp cookie URL", neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_set_cookies(jar, "file://example.com/", &cookie, 1);
    check_int("reject file cookie URL", neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_set_cookies(jar, "ws://example.com/", &cookie, 1);
    check_int("reject ws cookie URL", neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_set_cookie_header(
        jar, "javascript://example.com/", "sid=x; Path=/");
    check_int("reject javascript cookie URL", neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_set_cookies(jar, "https://example.com/", &cookie, 1);
    check_int("accept https cookie URL", neverc_cookiejar_count(jar), 1);
    neverc_cookiejar_free(jar);
}

static void test_cookie_prefixes(void) {
    printf("[cookie_prefixes]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_entry_t cookie = {
        .name = "__Host-session", .value = "secret", .path = "/",
        .secure = 1,
    };
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &cookie, 1);
    neverc_cookiejar_entry_t out[2];
    int n = neverc_cookiejar_cookies(
        jar, "https://example.com/", out, 2);
    check_int("accept __Host- with Secure Path=/ host-only", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://sub.example.com/", out, 2);
    check_int("__Host- cookie is host-only", n, 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "example.com";
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &cookie, 1);
    check_int("reject __Host- with Domain attribute",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = NULL;
    cookie.path = "/admin";
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/admin", &cookie, 1);
    check_int("reject __Host- without Path=/",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.path = "/";
    cookie.secure = 0;
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &cookie, 1);
    check_int("reject __Host- without Secure",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.name = "__Secure-token";
    cookie.secure = 0;
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &cookie, 1);
    check_int("reject __Secure- without Secure",
              neverc_cookiejar_count(jar), 0);
    cookie.secure = 1;
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://example.com/", out, 2);
    check_int("accept __Secure- with Secure from HTTPS", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "http://example.com/", out, 2);
    check_int("__Secure- is not sent over HTTP", n, 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "http://example.com/",
        "__Host-session=x; Path=/; Secure");
    check_int("reject __Host- Set-Cookie from HTTP origin",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/",
        "__Host-session=x; Path=/; Secure; HttpOnly");
    n = neverc_cookiejar_cookies(
        jar, "https://example.com/", out, 2);
    check_int("accept __Host- Set-Cookie from HTTPS", n, 1);
    if (n == 1) check_int("__Host- HttpOnly preserved", out[0].http_only, 1);
    char *header = neverc_cookiejar_cookie_header(
        jar, "https://example.com/");
    check_not_null("HttpOnly cookie is sent on HTTP Cookie header", header);
    if (header) {
        check_int("Cookie header includes __Host- name",
                  strstr(header, "__Host-session=x") != NULL, 1);
        free(header);
    }
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

    n = neverc_cookiejar_cookies(jar, "https://example.com/apifoo", out, 10);
    check_int("path is not a string prefix of a sibling", n, 1);

    n = neverc_cookiejar_cookies(jar, "https://example.com/other", out, 10);
    check_int("other path root only", n, 1);

    neverc_cookiejar_free(jar);
}

static void test_default_path(void) {
    printf("[default_path]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_entry_t cookie = {
        .name = "account", .value = "1",
    };
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/account/login", &cookie, 1);

    neverc_cookiejar_entry_t out[1];
    int n = neverc_cookiejar_cookies(
        jar, "https://example.com/account/profile", out, 1);
    check_int("default path matches request directory", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://example.com/other", out, 1);
    check_int("default path excludes sibling directory", n, 0);

    neverc_cookiejar_free(jar);
}

static void test_percent_encoded_request_url(void) {
    printf("[percent_encoded_request_url]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_entry_t cookie = {
        .name = "session", .value = "1", .path = "/",
    };
    neverc_cookiejar_entry_t out[1];
    int n;

    cookie.path = NULL;
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/account%2Flogin", &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://example.com/account/profile", out, 1);
    check_int("percent-decoded path uses default directory", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "https://example.com/other", out, 1);
    check_int("percent-decoded default path excludes sibling", n, 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.path = "/";
    neverc_cookiejar_set_cookies(
        jar, "http://[fe80::1%25eth0]/", &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "http://[fe80::1%25eth0]/", out, 1);
    check_int("IPv6 zone %25 round-trips", n, 1);

    neverc_cookiejar_free(jar);
}

static void test_ipv4_mapped_isolation(void) {
    printf("[ipv4_mapped_isolation]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_entry_t cookie = {
        .name = "site", .value = "v4", .path = "/",
    };
    neverc_cookiejar_set_cookies(jar, "http://192.168.1.1/", &cookie, 1);

    neverc_cookiejar_entry_t out[1];
    int n = neverc_cookiejar_cookies(jar, "http://192.168.1.1/", out, 1);
    check_int("IPv4 host matches", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "http://[::ffff:192.168.1.1]/", out, 1);
    check_int("IPv4-mapped IPv6 is a different host", n, 0);
    n = neverc_cookiejar_cookies(jar, "http://192.168.1.2/", out, 1);
    check_int("IPv4 domain suffix does not match", n, 0);

    cookie.domain = "192.168.1.1";
    neverc_cookiejar_set_cookies(jar, "http://192.168.1.1/", &cookie, 1);
    n = neverc_cookiejar_cookies(jar, "http://192.168.1.2/", out, 1);
    check_int("IPv4 Domain attribute is not a suffix", n, 0);
    n = neverc_cookiejar_cookies(jar, "http://evil.192.168.1.1/", out, 1);
    check_int("IPv4 Domain attribute does not match hostname suffix", n, 0);

    neverc_cookiejar_free(jar);
}

static void test_ipv6_host_isolation(void) {
    printf("[ipv6_host_isolation]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_entry_t cookie = {
        .name = "ipv6", .value = "one", .path = "/",
    };
    neverc_cookiejar_set_cookies(
        jar, "http://[2001:db8::1]/", &cookie, 1);

    neverc_cookiejar_entry_t out[1];
    int n = neverc_cookiejar_cookies(
        jar, "http://[2001:db8::1]/", out, 1);
    check_int("same IPv6 host matches", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "http://[2001:db8::2]/", out, 1);
    check_int("different IPv6 host excluded", n, 0);

    cookie.domain = "2001:db8::1";
    neverc_cookiejar_set_cookies(
        jar, "http://[2001:db8::1]/", &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "http://[2001:db8::1]/", out, 1);
    check_int("IPv6 Domain attribute matches same host", n, 1);
    n = neverc_cookiejar_cookies(
        jar, "http://[2001:db8::2]/", out, 1);
    check_int("IPv6 Domain attribute is not a suffix", n, 0);

    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    cookie.domain = "[2001:db8::1]";
    neverc_cookiejar_set_cookies(
        jar, "http://[2001:db8::1]/", &cookie, 1);
    n = neverc_cookiejar_cookies(
        jar, "http://[2001:db8::1]/", out, 1);
    check_int("bracketed IPv6 Domain attribute accepted", n, 1);

    neverc_cookiejar_free(jar);
}

static void test_invalid_cookie_octets(void) {
    printf("[invalid_cookie_octets]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_entry_t invalid[4] = {
        {.name = "bad\r\nX-Injected", .value = "1", .path = "/"},
        {.name = "bad-value", .value = "one\r\ntwo", .path = "/"},
        {.name = "bad-semicolon", .value = "one;two", .path = "/"},
        {.name = "bad-path", .value = "1", .path = "/a\r\nb"},
    };
    neverc_cookiejar_set_cookies(
        jar, "https://example.com/", invalid, 4);
    check_int("reject unsafe cookie octets", neverc_cookiejar_count(jar), 0);

    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/", "sid=1; Path=/a\r\nb");
    check_int("reject Set-Cookie Path with CR/LF",
              neverc_cookiejar_count(jar), 0);

    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/", "sid=1; Path=/\xff");
    check_int("reject Set-Cookie Path with non-ASCII",
              neverc_cookiejar_count(jar), 0);

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

static void test_set_cookie_header_edges(void) {
    printf("[set_cookie_header_edges]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/", "token=old; Path=/");
    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/", "token=new; Max-Age=bogus; Path=/");

    neverc_cookiejar_entry_t out[4];
    int n = neverc_cookiejar_cookies(
        jar, "https://example.com/", out, 4);
    check_int("invalid Max-Age is ignored", n, 1);
    if (n == 1) check_str("invalid Max-Age keeps new value", out[0].value, "new");

    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/",
        "large=ok; Max-Age=999999999999999999999999999999; Path=/");
    n = neverc_cookiejar_cookies(jar, "https://example.com/", out, 4);
    check_int("overflowing positive Max-Age remains valid", n, 2);

    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/", "quoted=\"hello\"; Path=/");
    n = neverc_cookiejar_cookies(jar, "https://example.com/", out, 4);
    check_int("quoted cookie value accepted", n, 3);
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i].name, "quoted") == 0)
            check_str("quoted cookie value unwrapped", out[i].value, "hello");
    }

    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/",
        "past=gone; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/");
    n = neverc_cookiejar_cookies(jar, "https://example.com/", out, 4);
    check_int("past Expires cookie is discarded", n, 3);

    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/",
        "future=ok; Expires=Tue, 19 Jan 2038 03:14:07 GMT; Path=/");
    n = neverc_cookiejar_cookies(jar, "https://example.com/", out, 4);
    check_int("future Expires cookie accepted", n, 4);
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i].name, "future") == 0)
            check_int("future Expires timestamp parsed",
                      out[i].expires > (int64_t)time(NULL), 1);
    }

    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/account/login", "scoped=one");
    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/account/login", "scoped=gone; Max-Age=0");
    check_int("Max-Age deletes cookie at default path",
              neverc_cookiejar_count(jar), 0);
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    size_t long_value_length = 5000;
    char *long_header = (char *)malloc(long_value_length + 16);
    check_not_null("allocate long cookie header", long_header);
    if (long_header) {
        memcpy(long_header, "long=", 5);
        memset(long_header + 5, 'a', long_value_length);
        memcpy(long_header + 5 + long_value_length, "; Path=/", 9);
        long_header[14 + long_value_length] = '\0';
        neverc_cookiejar_set_cookie_header(
            jar, "https://example.com/", long_header);
        check_int("oversized cookie value is rejected",
                  neverc_cookiejar_count(jar), 0);
        free(long_header);
    }

    size_t long_path_length = 1300;
    long_header = (char *)malloc(long_path_length + 16);
    check_not_null("allocate long path header", long_header);
    if (long_header) {
        memcpy(long_header, "path=v; Path=/", 14);
        memset(long_header + 14, 'p', long_path_length);
        long_header[14 + long_path_length] = '\0';
        neverc_cookiejar_set_cookie_header(
            jar, "https://example.com/", long_header);
        check_int("oversized cookie path is rejected",
                  neverc_cookiejar_count(jar), 0);
        free(long_header);
    }
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

static void test_cookie_header_many_matches(void) {
    printf("[cookie_header_many_matches]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    for (int i = 0; i < 80; i++) {
        char name[16];
        snprintf(name, sizeof(name), "cookie%02d", i);
        neverc_cookiejar_entry_t cookie = {
            .name = name, .value = "value", .path = "/",
        };
        neverc_cookiejar_set_cookies(
            jar, "https://example.com/", &cookie, 1);
    }

    char *header = neverc_cookiejar_cookie_header(
        jar, "https://example.com/");
    check_not_null("many-match Cookie header", header);
    if (header) {
        check_int("Cookie header includes oldest match",
                  strstr(header, "cookie00=value") != NULL, 1);
        check_int("Cookie header includes newest match",
                  strstr(header, "cookie79=value") != NULL, 1);
        free(header);
    }
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

    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/", "domain=1; Domain=.example.com; Path=/");
    check_int("domain cookie added", neverc_cookiejar_count(jar), 3);
    neverc_cookiejar_clear_domain(jar, ".EXAMPLE.COM");
    check_int("clear normalized domain", neverc_cookiejar_count(jar), 2);

    neverc_cookiejar_set_cookie_header(
        jar, "https://www.example.com/",
        "parent=1; Domain=example.com; Path=/");
    neverc_cookiejar_set_cookie_header(
        jar, "https://www.example.com/",
        "child=1; Domain=www.example.com; Path=/");
    neverc_cookiejar_set_cookie_header(
        jar, "https://other.test/", "keep=1; Path=/");
    check_int("overlap cookies added", neverc_cookiejar_count(jar), 5);
    neverc_cookiejar_clear_domain(jar, "www.example.com");
    check_int("clear host removes parent and child domains",
              neverc_cookiejar_count(jar), 3);
    neverc_cookiejar_set_cookie_header(
        jar, "https://www.example.com/",
        "child=1; Domain=www.example.com; Path=/");
    neverc_cookiejar_clear_domain(jar, "example.com");
    check_int("clear parent removes subdomain cookies",
              neverc_cookiejar_count(jar), 3);

    neverc_cookiejar_clear_all(jar);
    check_int("after clear all", neverc_cookiejar_count(jar), 0);

    neverc_cookiejar_free(jar);
}

static void test_secure_origin(void) {
    printf("[secure_origin]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "http://example.com/", "token=attacker; Secure; Path=/");
    check_int("reject Secure cookie from HTTP origin",
              neverc_cookiejar_count(jar), 0);

    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/", "token=secret; Secure; Path=/");
    check_int("accept Secure cookie from HTTPS origin",
              neverc_cookiejar_count(jar), 1);
    neverc_cookiejar_set_cookie_header(
        jar, "http://example.com/", "token=attacker; Path=/");

    neverc_cookiejar_entry_t out[1];
    int n = neverc_cookiejar_cookies(
        jar, "https://example.com/", out, 1);
    check_int("HTTP origin cannot overwrite Secure cookie", n, 1);
    if (n == 1) check_str("Secure value preserved", out[0].value, "secret");
    neverc_cookiejar_free(jar);
}

static void test_secure_shadowing(void) {
    printf("[secure_shadowing]\n");

    neverc_cookiejar_t *jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://www.example.com/", "token=secret; Secure; Path=/");
    neverc_cookiejar_set_cookie_header(
        jar, "http://example.com/",
        "token=attacker; Domain=example.com; Path=/");

    neverc_cookiejar_entry_t out[2];
    int n = neverc_cookiejar_cookies(
        jar, "https://www.example.com/", out, 2);
    check_int("insecure parent domain cannot shadow Secure host cookie", n, 1);
    if (n == 1) check_str("Secure host value preserved", out[0].value, "secret");
    neverc_cookiejar_free(jar);

    jar = neverc_cookiejar_new();
    neverc_cookiejar_set_cookie_header(
        jar, "https://example.com/admin", "sid=secure; Secure; Path=/admin");
    neverc_cookiejar_set_cookie_header(
        jar, "http://example.com/", "sid=attacker; Path=/");
    n = neverc_cookiejar_cookies(
        jar, "https://example.com/admin", out, 2);
    check_int("insecure root path cannot shadow Secure path cookie", n, 1);
    if (n == 1) check_str("Secure path value preserved", out[0].value, "secure");
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
    test_public_suffix_domain();
    test_domain_security();
    test_http_schemes_only();
    test_cookie_prefixes();
    test_path_matching();
    test_default_path();
    test_percent_encoded_request_url();
    test_ipv4_mapped_isolation();
    test_ipv6_host_isolation();
    test_invalid_cookie_octets();
    test_secure();
    test_set_cookie_header();
    test_set_cookie_header_edges();
    test_cookie_header();
    test_cookie_header_many_matches();
    test_update();
    test_clear();
    test_secure_origin();
    test_secure_shadowing();
    test_null_safety();
    test_batch();

    printf("\n--- cookiejar: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ---\n");
    return tests_failed > 0 ? 1 : 0;
}
