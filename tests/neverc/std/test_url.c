#include "neverc/std/net/url.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

typedef struct guarded_buffer {
    char          output[12];
    unsigned char guard[12];
} guarded_buffer_t;

static int guard_is_intact(const guarded_buffer_t *buffer) {
    for (size_t i = 0; i < sizeof(buffer->guard); i++)
        if (buffer->guard[i] != 0xa5) return 0;
    return 1;
}

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

static void test_parse_edges(void) {
    printf("[parse_edges]\n");
    neverc_url_t u;

    ASSERT_INT_EQ(neverc_url_parse(
        &u, "https://example.com?next=/path#fragment"), 0);
    ASSERT_STR_EQ(u.host, "example.com");
    ASSERT_STR_EQ(u.path, "");
    ASSERT_STR_EQ(u.raw_query, "next=/path");
    ASSERT_STR_EQ(u.fragment, "fragment");

    ASSERT_INT_EQ(neverc_url_parse(
        &u, "https://[2001:db8::1]:8443/api"), 0);
    ASSERT_STR_EQ(u.host, "2001:db8::1");
    ASSERT_STR_EQ(u.port, "8443");

    ASSERT_INT_EQ(neverc_url_parse(&u, "https://[2001:db8::1/api"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "https://host:abc/api"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "https://host:65536/api"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "https://host:/api"), 0);
    ASSERT_STR_EQ(u.host, "host");
    ASSERT_STR_EQ(u.port, "");
    ASSERT_STR_EQ(u.path, "/api");
    ASSERT_INT_EQ(neverc_url_parse(&u, "https://host:+80/api"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "1nvalid://host/api"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, ""), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http:"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "https:"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "a:"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http:/path"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "file:///tmp/foo"), 0);
    ASSERT_STR_EQ(u.scheme, "file");
    ASSERT_STR_EQ(u.host, "");
    ASSERT_STR_EQ(u.path, "/tmp/foo");
    char file_url[32];
    ASSERT_INT_EQ(neverc_url_string(&u, file_url, sizeof(file_url)),
                  (int)strlen("file:///tmp/foo"));
    ASSERT_STR_EQ(file_url, "file:///tmp/foo");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http:///foo"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "https:///foo"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "ws:///foo"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "file://"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://[::1]"), 0);
    ASSERT_STR_EQ(u.host, "::1");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://[::1]:80"), 0);
    ASSERT_STR_EQ(u.host, "::1");
    ASSERT_STR_EQ(u.port, "80");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://[::1]:"), 0);
    ASSERT_STR_EQ(u.host, "::1");
    ASSERT_STR_EQ(u.port, "");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://[hello]/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://[192.168.1.1]/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://[fe80::1%eth0]/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://[fe80::1%25eth0]/"), 0);
    ASSERT_STR_EQ(u.host, "fe80::1%eth0");
    {
        char zoned[64];
        ASSERT_INT_EQ(neverc_url_string(&u, zoned, sizeof(zoned)),
                      (int)strlen("http://[fe80::1%25eth0]/"));
        ASSERT_STR_EQ(zoned, "http://[fe80::1%25eth0]/");
    }
    ASSERT_INT_EQ(neverc_url_parse(
        &u, "http://[fe80::1%25Ethernet%202]/"), 0);
    ASSERT_STR_EQ(u.host, "fe80::1%Ethernet 2");
    {
        char zoned_space[80];
        ASSERT_INT_EQ(neverc_url_string(&u, zoned_space, sizeof(zoned_space)),
                      (int)strlen("http://[fe80::1%25Ethernet%202]/"));
        ASSERT_STR_EQ(zoned_space, "http://[fe80::1%25Ethernet%202]/");
        ASSERT_INT_EQ(neverc_url_parse(&u, zoned_space), 0);
    }
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://ex%25ample.com/"), 0);
    {
        char pct_host[64];
        ASSERT_INT_EQ(neverc_url_string(&u, pct_host, sizeof(pct_host)),
                      (int)strlen("http://ex%25ample.com/"));
        ASSERT_STR_EQ(pct_host, "http://ex%25ample.com/");
    }
    ASSERT_INT_EQ(neverc_url_parse(
        &u, "http://user:p%40ss@[fe80::1%25eth0]:8080/x"), 0);
    ASSERT_STR_EQ(u.user, "user");
    ASSERT_STR_EQ(u.password, "p%40ss");
    ASSERT_STR_EQ(u.host, "fe80::1%eth0");
    ASSERT_STR_EQ(u.port, "8080");
    ASSERT_STR_EQ(u.path, "/x");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://@host/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://user@"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://user%zz@host/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://user%00@host/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://user:p%zz@host/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://user%40name@host/"), 0);
    ASSERT_STR_EQ(u.user, "user%40name");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://:secret@host/"), 0);
    ASSERT_STR_EQ(u.user, "");
    ASSERT_STR_EQ(u.password, "secret");
    ASSERT_STR_EQ(u.host, "host");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://user:@host/x"), 0);
    ASSERT_STR_EQ(u.user, "user");
    ASSERT_STR_EQ(u.password, "");
    ASSERT_INT_EQ(u.has_password, 1);
    ASSERT_STR_EQ(u.host, "host");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://:@host/x"), 0);
    ASSERT_STR_EQ(u.user, "");
    ASSERT_STR_EQ(u.password, "");
    ASSERT_INT_EQ(u.has_password, 1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://evil.com\\@good.com/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://evil.com\\@good.com/x"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "//evil.com\\@good.com/path"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "/\\evil.com"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com/foo\\bar"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://user{name}@host/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://user[name@host/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com:80@evil.com/"), 0);
    ASSERT_STR_EQ(u.user, "example.com");
    ASSERT_STR_EQ(u.password, "80");
    ASSERT_STR_EQ(u.host, "evil.com");
    ASSERT_INT_EQ(neverc_url_parse(&u, "//example.com/path?q=1#frag"), 0);
    ASSERT_STR_EQ(u.scheme, "");
    ASSERT_STR_EQ(u.host, "example.com");
    ASSERT_STR_EQ(u.path, "/path");
    ASSERT_STR_EQ(u.raw_query, "q=1");
    ASSERT_STR_EQ(u.fragment, "frag");
    ASSERT_INT_EQ(neverc_url_parse(
        &u, "//user:p%40ss@[fe80::1%25eth0]:8080/x"), 0);
    ASSERT_STR_EQ(u.user, "user");
    ASSERT_STR_EQ(u.password, "p%40ss");
    ASSERT_STR_EQ(u.host, "fe80::1%eth0");
    ASSERT_STR_EQ(u.port, "8080");
    ASSERT_STR_EQ(u.path, "/x");
    ASSERT_INT_EQ(neverc_url_parse(&u, "//"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://ex%61mple.com/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://host%3a80/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://host%3A8080/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://[::ffff:192.168.1.1]/"), 0);
    ASSERT_STR_EQ(u.host, "::ffff:192.168.1.1");
    char mapped[64];
    ASSERT_INT_EQ(neverc_url_string(&u, mapped, sizeof(mapped)),
                  (int)strlen("http://[::ffff:192.168.1.1]/"));
    ASSERT_STR_EQ(mapped, "http://[::ffff:192.168.1.1]/");

    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com/%zz"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com/ok#bad%zz"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com/?q=%zz"), 0);
    ASSERT_STR_EQ(u.raw_query, "q=%zz");
    ASSERT_INT_EQ(u.has_query, 1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com/path?"), 0);
    ASSERT_STR_EQ(u.path, "/path");
    ASSERT_STR_EQ(u.raw_query, "");
    ASSERT_INT_EQ(u.has_query, 1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com/path"), 0);
    ASSERT_INT_EQ(u.has_query, 0);
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com?#frag"), 0);
    ASSERT_STR_EQ(u.raw_query, "");
    ASSERT_INT_EQ(u.has_query, 1);
    ASSERT_STR_EQ(u.fragment, "frag");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://b\xc3\xbc""cher.de/"), 0);
    ASSERT_STR_EQ(u.host, "xn--bcher-kva.de");
    ASSERT_INT_EQ(neverc_url_parse(&u, "http://\xff.com/"), -1);

    char long_url[400];
    memcpy(long_url, "https://", 8);
    memset(long_url + 8, 'h', 300);
    memcpy(long_url + 308, "/api", 5);
    ASSERT_INT_EQ(neverc_url_parse(&u, long_url), -1);
}

static void test_string(void) {
    printf("[string]\n");
    neverc_url_t u;
    neverc_url_parse(&u, "https://example.com/path?q=1#frag");
    char buf[2048];
    neverc_url_string(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "https://example.com/path?q=1#frag");

    ASSERT_INT_EQ(neverc_url_parse(
        &u, "https://[2001:db8::1]:8443/path"), 0);
    neverc_url_string(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "https://[2001:db8::1]:8443/path");

    ASSERT_INT_EQ(neverc_url_parse(&u, "//example.com/path"), 0);
    neverc_url_string(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "//example.com/path");

    ASSERT_INT_EQ(neverc_url_parse(&u, "http://:secret@host/x"), 0);
    neverc_url_string(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "http://:secret@host/x");

    ASSERT_INT_EQ(neverc_url_parse(&u, "http://user:@host/x"), 0);
    neverc_url_string(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "http://user:@host/x");

    ASSERT_INT_EQ(neverc_url_parse(&u, "http://:@host/x"), 0);
    neverc_url_string(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "http://:@host/x");

    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com/path?"), 0);
    neverc_url_string(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "http://example.com/path?");
    neverc_url_request_uri(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/path?");

    ASSERT_INT_EQ(neverc_url_parse(&u, "http://example.com?"), 0);
    neverc_url_string(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "http://example.com?");
    neverc_url_request_uri(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/?");
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

    neverc_url_values_set(&v, "age", "31");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "age"), "31");
    neverc_url_values_set(&v, "country", "US");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "country"), "US");
    ASSERT_INT_EQ(v.count, 4);
}

static void test_values_encoded(void) {
    printf("[values_encoded]\n");
    neverc_url_values_t v;
    neverc_url_values_parse(&v, "q=hello+world&lang=c%2B%2B");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "q"), "hello world");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "lang"), "c++");

    char long_query[400];
    memset(long_query, 'k', 300);
    memcpy(long_query + 300, "=value", 7);
    ASSERT_INT_EQ(neverc_url_values_parse(&v, long_query), -1);

    char encoded_query[700];
    size_t encoded_offset = 0;
    for (int i = 0; i < 200; i++) {
        memcpy(encoded_query + encoded_offset, "%61", 3);
        encoded_offset += 3;
    }
    memcpy(encoded_query + encoded_offset, "=value", 7);
    ASSERT_INT_EQ(neverc_url_values_parse(&v, encoded_query), 0);
    ASSERT_INT_EQ(v.count, 1);
    ASSERT_INT_EQ((int)strlen(v.keys[0]), 200);

    char many_query[512];
    size_t offset = 0;
    for (int i = 0; i < 65; i++) {
        int n = snprintf(many_query + offset, sizeof(many_query) - offset,
                         "%skey%d=v", i == 0 ? "" : "&", i);
        offset += (size_t)n;
    }
    ASSERT_INT_EQ(neverc_url_values_parse(&v, many_query), -1);

    /* Go 1.17+ ParseQuery rejects a raw semicolon separator. */
    ASSERT_INT_EQ(neverc_url_values_parse(&v, "a=1;b=2"), -1);
    ASSERT_INT_EQ(v.count, 0);
    ASSERT_INT_EQ(neverc_url_values_parse(&v, "a=1%3Bb=2"), 0);
    ASSERT_STR_EQ(neverc_url_values_get(&v, "a"), "1;b=2");

    /* Go ParseQuery skips empty "&" segments; `=x` keeps an empty key. */
    ASSERT_INT_EQ(neverc_url_values_parse(&v, "a=1&&b=2"), 0);
    ASSERT_INT_EQ(v.count, 2);
    ASSERT_STR_EQ(neverc_url_values_get(&v, "a"), "1");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "b"), "2");
    ASSERT_INT_EQ(neverc_url_values_parse(&v, "&a=1&"), 0);
    ASSERT_INT_EQ(v.count, 1);
    ASSERT_STR_EQ(neverc_url_values_get(&v, "a"), "1");
    ASSERT_INT_EQ(neverc_url_values_parse(&v, "&&"), 0);
    ASSERT_INT_EQ(v.count, 0);
    ASSERT_INT_EQ(neverc_url_values_parse(&v, "=x&a=1"), 0);
    ASSERT_INT_EQ(v.count, 2);
    ASSERT_STR_EQ(neverc_url_values_get(&v, ""), "x");
    ASSERT_STR_EQ(neverc_url_values_get(&v, "a"), "1");
}

static void test_escape(void) {
    printf("[escape]\n");
    char buf[256];
    neverc_url_query_escape("hello world", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "hello%20world");

    neverc_url_query_escape("a=b&c=d", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "a%3Db%26c%3Dd");

    /* Go url.PathEscape / encodePathSegment. */
    neverc_url_path_escape("a/b", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "a%2Fb");
    neverc_url_path_escape("a;b,c?d", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "a%3Bb%2Cc%3Fd");
    neverc_url_path_escape("$-_.+!*'(),", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "$-_.+%21%2A%27%28%29%2C");
    neverc_url_path_escape(":@&=+$", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, ":@&=+$");
    neverc_url_path_escape("../etc/passwd", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "..%2Fetc%2Fpasswd");
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

    neverc_url_query_unescape("%2B", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "+");

    neverc_url_path_unescape("hello+world", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "hello+world");

    ASSERT_INT_EQ(neverc_url_query_unescape(
        "bad%escape", buf, sizeof(buf)), -1);
    ASSERT_STR_EQ(buf, "");
    ASSERT_INT_EQ(neverc_url_query_unescape("ok%", buf, sizeof(buf)), -1);
    ASSERT_STR_EQ(buf, "");
    ASSERT_INT_EQ(neverc_url_query_unescape("%00", buf, sizeof(buf)), -1);
}

static void test_request_uri(void) {
    printf("[request_uri]\n");
    neverc_url_t u;
    neverc_url_parse(&u, "https://example.com/path?q=1");
    char buf[1024];
    neverc_url_request_uri(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/path?q=1");

    ASSERT_INT_EQ(neverc_url_parse_request_uri(&u, "/path?q=1"), 0);
    ASSERT_STR_EQ(u.path, "/path");
    ASSERT_STR_EQ(u.raw_query, "q=1");
    ASSERT_INT_EQ(neverc_url_parse_request_uri(
        &u, "https://example.com/path?q=1"), 0);
    ASSERT_STR_EQ(u.host, "example.com");
    ASSERT_INT_EQ(neverc_url_parse_request_uri(&u, "*"), 0);
    ASSERT_STR_EQ(u.path, "*");
    ASSERT_INT_EQ(neverc_url_parse_request_uri(&u, "foo"), -1);
    ASSERT_INT_EQ(neverc_url_parse_request_uri(&u, "//evil.com/phish"), -1);
    ASSERT_INT_EQ(neverc_url_parse_request_uri(
        &u, "https://example.com/path#frag"), -1);
    ASSERT_INT_EQ(neverc_url_parse_request_uri(&u, ""), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, "foo"), 0);
    ASSERT_STR_EQ(u.path, "foo");

    ASSERT_INT_EQ(neverc_url_parse(&u, "https://example.com//evil.com"), 0);
    neverc_url_request_uri(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/.//evil.com");

    ASSERT_INT_EQ(neverc_url_parse(&u, "https://example.com/%2f/evil.com"), 0);
    neverc_url_request_uri(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/./%2f/evil.com");
    ASSERT_INT_EQ(neverc_url_parse(&u, "https://example.com/%2fevil.com"), 0);
    neverc_url_request_uri(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/./%2fevil.com");
    ASSERT_INT_EQ(neverc_url_parse(&u, "https://example.com/%5cevil.com"), 0);
    neverc_url_request_uri(&u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/./%5cevil.com");
}

static void test_safe_redirect(void) {
    printf("[safe_redirect]\n");
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("/next", NULL), 1);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("/next?q=1", NULL), 1);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("//evil.com/", NULL), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("//evil.com/", "good.com"), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("/\\evil.com", NULL), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("foo", NULL), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect(
        "https://good.com/x", NULL), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect(
        "https://good.com/x", "good.com"), 1);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect(
        "https://GOOD.COM/x", "good.com"), 1);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect(
        "https://evil.com/x", "good.com"), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect(
        "https://user@good.com/", "good.com"), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect(
        "https://good.com:80@evil.com/", "good.com"), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect(
        "javascript:alert(1)", "good.com"), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect(
        "https://[::ffff:127.0.0.1]/", "good.com"), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("/%2f/evil.com", NULL), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("/%2F%2Fevil.com", NULL), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("/%2fevil.com", NULL), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("/%5cevil.com", NULL), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("/%5Cevil.com", NULL), 0);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect("/foo%2fbar", NULL), 1);
    ASSERT_INT_EQ(neverc_url_is_safe_redirect(
        "https://good.com/%2fevil.com", "good.com"), 1);
}

static void test_bounded_outputs(void) {
    printf("[bounded_outputs]\n");
    const char *expected = "https://example.com/path?q=1#frag";
    neverc_url_t u;
    ASSERT_INT_EQ(neverc_url_parse(&u, expected), 0);

    guarded_buffer_t guarded;
    memset(&guarded, 0xa5, sizeof(guarded));
    int length = neverc_url_string(
        &u, guarded.output, sizeof(guarded.output));
    ASSERT_INT_EQ(length, (int)strlen(expected));
    ASSERT_STR_EQ(guarded.output, "https://exa");
    ASSERT_TRUE(guard_is_intact(&guarded));
    ASSERT_INT_EQ(neverc_url_string(&u, NULL, 0), (int)strlen(expected));

    memset(&guarded, 0xa5, sizeof(guarded));
    length = neverc_url_request_uri(
        &u, guarded.output, sizeof(guarded.output));
    ASSERT_INT_EQ(length, (int)strlen("/path?q=1"));
    ASSERT_STR_EQ(guarded.output, "/path?q=1");
    ASSERT_TRUE(guard_is_intact(&guarded));

    neverc_url_values_t values;
    ASSERT_INT_EQ(neverc_url_values_parse(
        &values, "name=alice&city=New+York"), 0);
    memset(&guarded, 0xa5, sizeof(guarded));
    length = neverc_url_values_encode(
        &values, guarded.output, sizeof(guarded.output));
    ASSERT_INT_EQ(length, (int)strlen("name=alice&city=New%20York"));
    ASSERT_STR_EQ(guarded.output, "name=alice&");
    ASSERT_TRUE(guard_is_intact(&guarded));
    ASSERT_INT_EQ(neverc_url_values_encode(&values, NULL, 0), length);

    memset(&guarded, 0xa5, sizeof(guarded));
    length = neverc_url_query_escape(
        "a b&c", guarded.output, 5);
    ASSERT_INT_EQ(length, (int)strlen("a%20b%26c"));
    ASSERT_STR_EQ(guarded.output, "a%20");
    ASSERT_TRUE(guard_is_intact(&guarded));
    ASSERT_INT_EQ(neverc_url_query_escape("a b&c", NULL, 0), length);
}

static void test_invalid_arguments(void) {
    printf("[invalid_arguments]\n");
    neverc_url_t u;
    char output[16];

    ASSERT_INT_EQ(neverc_url_parse(NULL, "https://example.com/"), -1);
    ASSERT_INT_EQ(neverc_url_parse(&u, NULL), -1);
    ASSERT_INT_EQ(neverc_url_string(NULL, output, sizeof(output)), -1);
    ASSERT_INT_EQ(neverc_url_string(&u, NULL, sizeof(output)), -1);
    ASSERT_INT_EQ(neverc_url_hostname(NULL, output, sizeof(output)), -1);
    ASSERT_INT_EQ(neverc_url_request_uri(NULL, output, sizeof(output)), -1);
    ASSERT_INT_EQ(neverc_url_values_parse(NULL, "a=b"), -1);
    ASSERT_TRUE(neverc_url_values_get(NULL, "a") == NULL);
    ASSERT_TRUE(neverc_url_values_get(&(neverc_url_values_t){0}, NULL) == NULL);
    ASSERT_INT_EQ(neverc_url_values_encode(NULL, output, sizeof(output)), -1);
    ASSERT_INT_EQ(neverc_url_query_escape(NULL, output, sizeof(output)), -1);
    ASSERT_INT_EQ(neverc_url_query_escape("a", NULL, sizeof(output)), -1);
    ASSERT_INT_EQ(neverc_url_query_unescape(NULL, output, sizeof(output)), -1);
    ASSERT_INT_EQ(neverc_url_query_unescape("a", NULL, sizeof(output)), -1);
}

static uint32_t roundtrip_rng = 0x243f6a88U;

static uint32_t next_roundtrip_random(void) {
    roundtrip_rng = roundtrip_rng * 1664525U + 1013904223U;
    return roundtrip_rng;
}

static void test_roundtrips(void) {
    printf("[roundtrips]\n");
    int roundtrips_ok = 1;

    for (int iteration = 0; iteration < 1000; iteration++) {
        unsigned char input[65];
        size_t length = (size_t)(next_roundtrip_random() % 65U);
        for (size_t i = 0; i < length; i++)
            input[i] = (unsigned char)(next_roundtrip_random() % 255U + 1U);
        input[length] = '\0';

        char escaped[sizeof(input) * 3];
        unsigned char decoded[sizeof(input)];
        int escaped_length = neverc_url_query_escape(
            (const char *)input, escaped, sizeof(escaped));
        int decoded_length = neverc_url_query_unescape(
            escaped, (char *)decoded, sizeof(decoded));
        if (escaped_length < 0 || decoded_length != (int)length ||
            memcmp(decoded, input, length + 1) != 0)
            roundtrips_ok = 0;

        escaped_length = neverc_url_path_escape(
            (const char *)input, escaped, sizeof(escaped));
        decoded_length = neverc_url_path_unescape(
            escaped, (char *)decoded, sizeof(decoded));
        if (escaped_length < 0 || decoded_length != (int)length ||
            memcmp(decoded, input, length + 1) != 0)
            roundtrips_ok = 0;
    }

    for (int iteration = 0; iteration < 500; iteration++) {
        char raw[256];
        int port = 1 + (int)(next_roundtrip_random() % 65535U);
        int path_id = (int)(next_roundtrip_random() % 100000U);
        int query_id = (int)(next_roundtrip_random() % 100000U);
        int fragment_id = (int)(next_roundtrip_random() % 100000U);
        int required = snprintf(raw, sizeof(raw),
            "https://host%d.example:%d/path/%d?q=%d#frag%d",
            iteration, port, path_id, query_id, fragment_id);
        neverc_url_t parsed;
        char formatted[sizeof(raw)];
        int formatted_length = neverc_url_parse(&parsed, raw) == 0
            ? neverc_url_string(&parsed, formatted, sizeof(formatted)) : -1;
        if (required < 0 || formatted_length != required ||
            strcmp(formatted, raw) != 0)
            roundtrips_ok = 0;
    }
    ASSERT_TRUE(roundtrips_ok);
}

int main(void) {
    printf("=== NeverC net/url Tests ===\n");
    test_parse_basic();
    test_parse_port();
    test_parse_userinfo();
    test_parse_no_path();
    test_parse_relative();
    test_parse_edges();
    test_string();
    test_values();
    test_values_encoded();
    test_escape();
    test_unescape();
    test_request_uri();
    test_safe_redirect();
    test_bounded_outputs();
    test_invalid_arguments();
    test_roundtrips();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
