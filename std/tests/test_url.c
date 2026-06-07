#include "neverc/std/net/url.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_STR_EQ(expr, expected) do { \
    const char *_v = (expr); const char *_e = (expected); tests_run++; \
    if (_v && _e && strcmp(_v, _e) == 0) tests_passed++; \
    else { tests_failed++; \
           printf("  FAIL: %s = \"%s\", expected \"%s\" (line %d)\n", \
                  #expr, _v ? _v : "(null)", _e ? _e : "(null)", __LINE__); } \
} while(0)
#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)
#define ASSERT_TRUE(expr) do { tests_run++; if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } } while(0)

static void test_parse_basic(void) {
    printf("[parse_basic]\n");
    neverc_url_t u;
    neverc_url_parse(&u, "https://example.com/path?q=1#frag");
    ASSERT_STR_EQ(u.scheme, "https");
    ASSERT_STR_EQ(u.host, "example.com");
    ASSERT_STR_EQ(u.path, "/path");
    ASSERT_STR_EQ(u.raw_query, "q=1");
    ASSERT_STR_EQ(u.fragment, "frag");
}

static void test_parse_port(void) {
    printf("[parse_port]\n");
    neverc_url_t u;
    neverc_url_parse(&u, "http://localhost:8080/api");
    ASSERT_STR_EQ(u.scheme, "http");
    ASSERT_STR_EQ(u.host, "localhost");
    ASSERT_STR_EQ(u.port, "8080");
    ASSERT_STR_EQ(u.path, "/api");
}

static void test_parse_userinfo(void) {
    printf("[parse_userinfo]\n");
    neverc_url_t u;
    neverc_url_parse(&u, "ftp://user:pass@host.com/dir");
    ASSERT_STR_EQ(u.scheme, "ftp");
    ASSERT_STR_EQ(u.user, "user");
    ASSERT_STR_EQ(u.password, "pass");
    ASSERT_STR_EQ(u.host, "host.com");
    ASSERT_STR_EQ(u.path, "/dir");
}

static void test_parse_no_path(void) {
    printf("[parse_no_path]\n");
    neverc_url_t u;
    neverc_url_parse(&u, "https://example.com");
    ASSERT_STR_EQ(u.scheme, "https");
    ASSERT_STR_EQ(u.host, "example.com");
    ASSERT_STR_EQ(u.path, "");
}

static void test_parse_relative(void) {
    printf("[parse_relative]\n");
    neverc_url_t u;
    neverc_url_parse(&u, "/path/to/resource?key=val");
    ASSERT_STR_EQ(u.scheme, "");
    ASSERT_STR_EQ(u.path, "/path/to/resource");
    ASSERT_STR_EQ(u.raw_query, "key=val");
    ASSERT_TRUE(!neverc_url_is_abs(&u));
}

static void test_string(void) {
    printf("[string]\n");
    neverc_url_t u;
    neverc_url_parse(&u, "https://example.com/path?q=1#frag");
    char buf[2048];
    neverc_url_string(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "https://example.com/path?q=1#frag");
}

static void test_values(void) {
    printf("[values]\n");
    neverc_url_values_t v;
    neverc_url_values_parse(&v, "name=alice&age=30&city=NY");
    ASSERT_INT_EQ(v.count, 3);
    ASSERT_STR_EQ(neverc_url_values_get(&v, "name"), "alice");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "age"), "30");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "city"), "NY");
    ASSERT_TRUE(neverc_url_values_get(&v, "missing") == NULL);
}

static void test_values_encoded(void) {
    printf("[values_encoded]\n");
    neverc_url_values_t v;
    neverc_url_values_parse(&v, "q=hello+world&lang=c%2B%2B");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "q"), "hello world");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "lang"), "c++");
}

static void test_escape(void) {
    printf("[escape]\n");
    char buf[256];
    neverc_url_query_escape("hello world", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "hello%20world");

    neverc_url_query_escape("a=b&c=d", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "a%3Db%26c%3Dd");
}

static void test_unescape(void) {
    printf("[unescape]\n");
    char buf[256];
    neverc_url_query_unescape("hello%20world", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "hello world");

    neverc_url_query_unescape("a%2Bb%3Dc", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "a+b=c");

    neverc_url_query_unescape("hello+world", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "hello world");
}

static void test_request_uri(void) {
    printf("[request_uri]\n");
    neverc_url_t u;
    neverc_url_parse(&u, "https://example.com/path?q=1");
    char buf[1024];
    neverc_url_request_uri(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/path?q=1");
}

int main(void) {
    printf("=== NeverC net/url Tests ===\n");
    test_parse_basic();
    test_parse_port();
    test_parse_userinfo();
    test_parse_no_path();
    test_parse_relative();
    test_string();
    test_values();
    test_values_encoded();
    test_escape();
    test_unescape();
    test_request_uri();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
