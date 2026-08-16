/*
 * NeverC path/filepath tests.
 */
#include "neverc/std/path/filepath.h"
#include <stdio.h>
#include <stdlib.h>
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
#ifdef _WIN32
    ASSERT_STR_EQ(neverc_filepath_base("C:\\foo\\bar\\baz.txt", buf, sizeof(buf)), "baz.txt");
    ASSERT_STR_EQ(neverc_filepath_base("C:\\foo\\bar\\", buf, sizeof(buf)), "bar");
    ASSERT_STR_EQ(neverc_filepath_base("C:\\", buf, sizeof(buf)), "\\");
    ASSERT_STR_EQ(neverc_filepath_base("\\\\server\\share", buf, sizeof(buf)), "\\");
#else
    ASSERT_STR_EQ(neverc_filepath_base("/foo/bar/baz.txt", buf, sizeof(buf)), "baz.txt");
    ASSERT_STR_EQ(neverc_filepath_base("/foo/bar/", buf, sizeof(buf)), "bar");
#endif
    ASSERT_STR_EQ(neverc_filepath_base("hello", buf, sizeof(buf)), "hello");
    ASSERT_STR_EQ(neverc_filepath_base("", buf, sizeof(buf)), ".");
}

static void test_dir(void) {
    printf("[dir]\n");
    char buf[256];
#ifdef _WIN32
    ASSERT_STR_EQ(neverc_filepath_dir("C:\\foo\\bar\\baz.txt", buf, sizeof(buf)), "C:\\foo\\bar");
    ASSERT_STR_EQ(neverc_filepath_dir("\\\\server\\share\\foo\\bar.txt", buf, sizeof(buf)),
                  "\\\\server\\share\\foo");
#else
    ASSERT_STR_EQ(neverc_filepath_dir("/foo/bar/baz.txt", buf, sizeof(buf)), "/foo/bar");
    ASSERT_STR_EQ(neverc_filepath_dir("/foo", buf, sizeof(buf)), "/");
#endif
    ASSERT_STR_EQ(neverc_filepath_dir("hello", buf, sizeof(buf)), ".");
    ASSERT_STR_EQ(neverc_filepath_dir("", buf, sizeof(buf)), ".");
#ifdef _WIN32
    ASSERT_STR_EQ(neverc_filepath_dir("C:foo", buf, sizeof(buf)), "C:.");
    ASSERT_STR_EQ(neverc_filepath_dir("C:\\foo\\bar\\..\\baz", buf, sizeof(buf)),
                  "C:\\foo");
#else
    ASSERT_STR_EQ(neverc_filepath_dir("/foo/bar/../baz", buf, sizeof(buf)), "/foo");
#endif
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
#ifdef _WIN32
    ASSERT_TRUE(neverc_filepath_isabs("C:\\foo"));
    ASSERT_TRUE(neverc_filepath_isabs("D:\\"));
    ASSERT_FALSE(neverc_filepath_isabs("/foo"));
    ASSERT_FALSE(neverc_filepath_isabs("foo"));
    ASSERT_FALSE(neverc_filepath_isabs(".\\foo"));
    ASSERT_FALSE(neverc_filepath_isabs("..\\bar"));
    ASSERT_FALSE(neverc_filepath_isabs("C:foo"));
    ASSERT_TRUE(neverc_filepath_isabs("\\\\server\\share"));
    ASSERT_TRUE(neverc_filepath_isabs("\\\\server\\share\\foo"));
    ASSERT_TRUE(neverc_filepath_isabs("\\\\?\\C:\\foo"));
#else
    ASSERT_TRUE(neverc_filepath_isabs("/foo"));
    ASSERT_TRUE(neverc_filepath_isabs("/"));
    ASSERT_FALSE(neverc_filepath_isabs("foo"));
    ASSERT_FALSE(neverc_filepath_isabs("./foo"));
    ASSERT_FALSE(neverc_filepath_isabs("../bar"));
#endif
}

static void test_clean(void) {
    printf("[clean]\n");
    char buf[256];
#ifdef _WIN32
    ASSERT_STR_EQ(neverc_filepath_clean("C:\\foo\\bar\\..\\baz", buf, sizeof(buf)), "C:\\foo\\baz");
    ASSERT_STR_EQ(neverc_filepath_clean("C:\\foo\\.\\bar", buf, sizeof(buf)), "C:\\foo\\bar");
    ASSERT_STR_EQ(neverc_filepath_clean("C:foo", buf, sizeof(buf)), "C:foo");
    ASSERT_STR_EQ(neverc_filepath_clean("C:..", buf, sizeof(buf)), "C:..");
    ASSERT_STR_EQ(neverc_filepath_clean("\\\\server\\share\\foo\\..\\bar", buf, sizeof(buf)),
                  "\\\\server\\share\\bar");
#else
    ASSERT_STR_EQ(neverc_filepath_clean("/foo/bar/../baz", buf, sizeof(buf)), "/foo/baz");
    ASSERT_STR_EQ(neverc_filepath_clean("/foo/./bar", buf, sizeof(buf)), "/foo/bar");
    ASSERT_STR_EQ(neverc_filepath_clean("//foo//bar//", buf, sizeof(buf)), "/foo/bar");
    ASSERT_STR_EQ(neverc_filepath_clean("/", buf, sizeof(buf)), "/");
    ASSERT_STR_EQ(neverc_filepath_clean("../foo", buf, sizeof(buf)), "../foo");
#endif
    ASSERT_STR_EQ(neverc_filepath_clean(".", buf, sizeof(buf)), ".");
    ASSERT_STR_EQ(neverc_filepath_clean("", buf, sizeof(buf)), ".");
    ASSERT_STR_EQ(neverc_filepath_clean("a/b/../c", buf, sizeof(buf)),
#ifdef _WIN32
                  "a\\c"
#else
                  "a/c"
#endif
    );
#ifdef _WIN32
    ASSERT_STR_EQ(neverc_filepath_clean("C:", buf, sizeof(buf)), "C:.");
    ASSERT_STR_EQ(neverc_filepath_clean("C:foo\\..", buf, sizeof(buf)), "C:.");
    ASSERT_STR_EQ(neverc_filepath_clean("a\\..\\c:", buf, sizeof(buf)), ".\\c:");
    ASSERT_STR_EQ(neverc_filepath_clean("//host/share/foo/../baz", buf, sizeof(buf)),
                  "\\\\host\\share\\baz");
#else
    ASSERT_STR_EQ(neverc_filepath_clean("abc/def/../..", buf, sizeof(buf)), ".");
    ASSERT_STR_EQ(neverc_filepath_clean("/abc/def/../../..", buf, sizeof(buf)), "/");
#endif
}

static void test_long_and_invalid_paths(void) {
    printf("[long_and_invalid_paths]\n");
    const size_t component_len = 6000;
    char *path = (char *)malloc(component_len + 1);
    char *buf = (char *)malloc(component_len + 2);
    ASSERT_TRUE(path != NULL);
    ASSERT_TRUE(buf != NULL);
    if (path && buf) {
        memset(path, 'a', component_len);
        path[component_len] = '\0';
        ASSERT_TRUE(neverc_filepath_clean(
                        path, buf, component_len + 2) == buf);
        ASSERT_TRUE(strlen(buf) == component_len);
        ASSERT_TRUE(memcmp(buf, path, component_len + 1) == 0);
    }
    free(path);
    free(buf);

    const size_t left_len = 3000;
    const size_t right_len = 2000;
    char *left = (char *)malloc(left_len + 1);
    char *right = (char *)malloc(right_len + 1);
    char *joined = (char *)malloc(left_len + right_len + 2);
    ASSERT_TRUE(left != NULL && right != NULL && joined != NULL);
    if (left && right && joined) {
        memset(left, 'l', left_len);
        left[left_len] = '\0';
        memset(right, 'r', right_len);
        right[right_len] = '\0';
        ASSERT_TRUE(neverc_filepath_join(
                        left, right, joined, left_len + right_len + 2) ==
                    joined);
        ASSERT_TRUE(strlen(joined) == left_len + right_len + 1);
        ASSERT_INT_EQ(joined[left_len], NEVERC_FILEPATH_SEP);
    }
    free(left);
    free(right);
    free(joined);

    char tiny[1] = {'x'};
    ASSERT_TRUE(neverc_filepath_clean("", tiny, sizeof(tiny)) == NULL);
    ASSERT_STR_EQ(neverc_filepath_ext(NULL), "");
    ASSERT_FALSE(neverc_filepath_isabs(NULL));
}

static void test_join(void) {
    printf("[join]\n");
    char buf[256];
#ifdef _WIN32
    ASSERT_STR_EQ(neverc_filepath_join("C:\\foo", "bar", buf, sizeof(buf)), "C:\\foo\\bar");
    ASSERT_STR_EQ(neverc_filepath_join("a", "b\\c", buf, sizeof(buf)), "a\\b\\c");
    ASSERT_STR_EQ(neverc_filepath_join("C:\\foo", "D:\\bar", buf, sizeof(buf)), "D:\\bar");
    ASSERT_STR_EQ(neverc_filepath_join("safe", "C:\\Windows", buf, sizeof(buf)), "C:\\Windows");
    ASSERT_STR_EQ(neverc_filepath_join("C:\\foo", "\\bar", buf, sizeof(buf)), "C:\\bar");
    ASSERT_STR_EQ(neverc_filepath_join("a", "C:b", buf, sizeof(buf)), "C:b");
#else
    ASSERT_STR_EQ(neverc_filepath_join("/foo", "bar", buf, sizeof(buf)), "/foo/bar");
    ASSERT_STR_EQ(neverc_filepath_join("a", "b/c", buf, sizeof(buf)), "a/b/c");
    ASSERT_STR_EQ(neverc_filepath_join("safe", "/etc/passwd", buf, sizeof(buf)), "/etc/passwd");
    ASSERT_STR_EQ(neverc_filepath_join("/foo", "/bar", buf, sizeof(buf)), "/bar");
#endif
    ASSERT_STR_EQ(neverc_filepath_join("", "foo", buf, sizeof(buf)), "foo");
    ASSERT_STR_EQ(neverc_filepath_join("foo", "", buf, sizeof(buf)), "foo");
    ASSERT_STR_EQ(neverc_filepath_join("", "", buf, sizeof(buf)), "");
#ifdef _WIN32
    ASSERT_STR_EQ(neverc_filepath_join("C:", "a", buf, sizeof(buf)), "C:a");
    ASSERT_STR_EQ(neverc_filepath_join("C:", "\\a", buf, sizeof(buf)), "C:\\a");
#endif
}

static void test_split(void) {
    printf("[split]\n");
    const char *dir, *file;
    size_t dir_len;

#ifdef _WIN32
    neverc_filepath_split("C:\\foo\\bar.txt", &dir, &dir_len, &file);
    ASSERT_STR_EQ(file, "bar.txt");
#else
    neverc_filepath_split("/foo/bar.txt", &dir, &dir_len, &file);
    ASSERT_INT_EQ((int)dir_len, 5);
    ASSERT_STR_EQ(file, "bar.txt");
#endif

    neverc_filepath_split("bar.txt", &dir, &dir_len, &file);
    ASSERT_INT_EQ((int)dir_len, 0);
    ASSERT_STR_EQ(file, "bar.txt");

    neverc_filepath_split(NULL, &dir, &dir_len, &file);
    ASSERT_INT_EQ((int)dir_len, 0);
    ASSERT_STR_EQ(file, "");
    neverc_filepath_split("bar.txt", NULL, NULL, NULL);
}

static void test_match(void) {
    printf("[match]\n");
    ASSERT_TRUE(neverc_filepath_match("*.txt", "hello.txt"));
    ASSERT_FALSE(neverc_filepath_match("*.txt", "hello.go"));
    ASSERT_TRUE(neverc_filepath_match("foo*", "foobar"));
    ASSERT_TRUE(neverc_filepath_match("f?o", "foo"));
    ASSERT_FALSE(neverc_filepath_match("f?o", "fooo"));
    ASSERT_TRUE(neverc_filepath_match("*", "anything"));
    ASSERT_FALSE(neverc_filepath_match("*", "foo/bar"));
    ASSERT_FALSE(neverc_filepath_match("foo*", "foo/bar"));
    ASSERT_TRUE(neverc_filepath_match("hello", "hello"));
    ASSERT_FALSE(neverc_filepath_match("hello", "world"));
    ASSERT_FALSE(neverc_filepath_match("*.txt", "dir/file.txt"));
    ASSERT_TRUE(neverc_filepath_match("[abc]", "b"));
    ASSERT_FALSE(neverc_filepath_match("[abc]", "d"));
    ASSERT_INT_EQ(neverc_filepath_match("[abc", "a"), -1);
#ifndef _WIN32
    ASSERT_TRUE(neverc_filepath_match("\\*", "*"));
    ASSERT_FALSE(neverc_filepath_match("\\*", "a"));
#endif
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
    test_long_and_invalid_paths();
    test_join();
    test_split();
    test_match();
    test_to_from_slash();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
