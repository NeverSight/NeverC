/*
 * NeverC path/filepath tests.
 */
#include "neverc/path/filepath.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_STR_EQ(expr, expected) do { \
    const char *_v = (expr); const char *_e = (expected); tests_run++; \
    if (_v && _e && strcmp(_v, _e) == 0) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL line %d: %s = \"%s\", expected \"%s\"\n", __LINE__, #expr, _v?_v:"(null)", _e); } \
} while(0)

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL line %d: %s = %d, expected %d\n", __LINE__, #expr, _v, _e); } \
} while(0)

#define ASSERT_TRUE(expr)  ASSERT_INT_EQ(!!(expr), 1)
#define ASSERT_FALSE(expr) ASSERT_INT_EQ(!!(expr), 0)

static void test_base(void) {
    printf("[base]\n");
    char buf[256];
    ASSERT_STR_EQ(neverc_filepath_base("/foo/bar/baz.txt", buf, sizeof(buf)), "baz.txt");
    ASSERT_STR_EQ(neverc_filepath_base("/foo/bar/", buf, sizeof(buf)), "bar");
    ASSERT_STR_EQ(neverc_filepath_base("hello", buf, sizeof(buf)), "hello");
    ASSERT_STR_EQ(neverc_filepath_base("", buf, sizeof(buf)), ".");
    ASSERT_STR_EQ(neverc_filepath_base("/", buf, sizeof(buf)), "/");
}

static void test_dir(void) {
    printf("[dir]\n");
    char buf[256];
    ASSERT_STR_EQ(neverc_filepath_dir("/foo/bar/baz.txt", buf, sizeof(buf)), "/foo/bar");
    ASSERT_STR_EQ(neverc_filepath_dir("hello", buf, sizeof(buf)), ".");
    ASSERT_STR_EQ(neverc_filepath_dir("", buf, sizeof(buf)), ".");
    ASSERT_STR_EQ(neverc_filepath_dir("/foo", buf, sizeof(buf)), "/");
}

static void test_ext(void) {
    printf("[ext]\n");
    ASSERT_STR_EQ(neverc_filepath_ext("foo.txt"), ".txt");
    ASSERT_STR_EQ(neverc_filepath_ext("foo.tar.gz"), ".gz");
    ASSERT_STR_EQ(neverc_filepath_ext("foo"), "");
    ASSERT_STR_EQ(neverc_filepath_ext("/path/to/foo.c"), ".c");
}

static void test_isabs(void) {
    printf("[isabs]\n");
    ASSERT_TRUE(neverc_filepath_isabs("/foo"));
    ASSERT_TRUE(neverc_filepath_isabs("/"));
    ASSERT_FALSE(neverc_filepath_isabs("foo"));
    ASSERT_FALSE(neverc_filepath_isabs("./foo"));
    ASSERT_FALSE(neverc_filepath_isabs("../bar"));
}

static void test_clean(void) {
    printf("[clean]\n");
    char buf[256];
    ASSERT_STR_EQ(neverc_filepath_clean("/foo/bar/../baz", buf, sizeof(buf)), "/foo/baz");
    ASSERT_STR_EQ(neverc_filepath_clean("/foo/./bar", buf, sizeof(buf)), "/foo/bar");
    ASSERT_STR_EQ(neverc_filepath_clean("//foo//bar//", buf, sizeof(buf)), "/foo/bar");
    ASSERT_STR_EQ(neverc_filepath_clean(".", buf, sizeof(buf)), ".");
    ASSERT_STR_EQ(neverc_filepath_clean("", buf, sizeof(buf)), ".");
    ASSERT_STR_EQ(neverc_filepath_clean("a/b/../c", buf, sizeof(buf)), "a/c");
    ASSERT_STR_EQ(neverc_filepath_clean("../foo", buf, sizeof(buf)), "../foo");
    ASSERT_STR_EQ(neverc_filepath_clean("/", buf, sizeof(buf)), "/");
}

static void test_join(void) {
    printf("[join]\n");
    char buf[256];
    ASSERT_STR_EQ(neverc_filepath_join("/foo", "bar", buf, sizeof(buf)), "/foo/bar");
    ASSERT_STR_EQ(neverc_filepath_join("a", "b/c", buf, sizeof(buf)), "a/b/c");
    ASSERT_STR_EQ(neverc_filepath_join("", "foo", buf, sizeof(buf)), "foo");
    ASSERT_STR_EQ(neverc_filepath_join("foo", "", buf, sizeof(buf)), "foo");
}

static void test_split(void) {
    printf("[split]\n");
    const char *dir, *file;
    size_t dir_len;

    neverc_filepath_split("/foo/bar.txt", &dir, &dir_len, &file);
    ASSERT_INT_EQ((int)dir_len, 5);
    ASSERT_STR_EQ(file, "bar.txt");

    neverc_filepath_split("bar.txt", &dir, &dir_len, &file);
    ASSERT_INT_EQ((int)dir_len, 0);
    ASSERT_STR_EQ(file, "bar.txt");

    neverc_filepath_split("/foo/", &dir, &dir_len, &file);
    ASSERT_INT_EQ((int)dir_len, 5);
    ASSERT_STR_EQ(file, "");
}

static void test_match(void) {
    printf("[match]\n");
    ASSERT_TRUE(neverc_filepath_match("*.txt", "hello.txt"));
    ASSERT_FALSE(neverc_filepath_match("*.txt", "hello.go"));
    ASSERT_TRUE(neverc_filepath_match("foo*", "foobar"));
    ASSERT_TRUE(neverc_filepath_match("f?o", "foo"));
    ASSERT_FALSE(neverc_filepath_match("f?o", "fooo"));
    ASSERT_TRUE(neverc_filepath_match("*", "anything"));
    ASSERT_TRUE(neverc_filepath_match("hello", "hello"));
    ASSERT_FALSE(neverc_filepath_match("hello", "world"));
    ASSERT_FALSE(neverc_filepath_match("*.txt", "dir/file.txt"));
}

static void test_to_from_slash(void) {
    printf("[to_from_slash]\n");
    char buf[256];
    ASSERT_STR_EQ(neverc_filepath_to_slash("a\\b\\c", buf, sizeof(buf)), "a/b/c");
    ASSERT_STR_EQ(neverc_filepath_to_slash("a/b/c", buf, sizeof(buf)), "a/b/c");
#ifdef _WIN32
    ASSERT_STR_EQ(neverc_filepath_from_slash("a/b/c", buf, sizeof(buf)), "a\\b\\c");
#else
    ASSERT_STR_EQ(neverc_filepath_from_slash("a/b/c", buf, sizeof(buf)), "a/b/c");
#endif
}

int main(void) {
    printf("=== NeverC path/filepath Tests ===\n");
    test_base();
    test_dir();
    test_ext();
    test_isabs();
    test_clean();
    test_join();
    test_split();
    test_match();
    test_to_from_slash();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
