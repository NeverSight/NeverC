#include "neverc/std/path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (strcmp(got, expected) == 0) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

/* ===== Test: Base ===== */
static void test_base(void) {
    printf("[base]\n");
    char buf[256];
    neverc_path_base("/foo/bar/baz.txt", buf, sizeof(buf));
    check_str("base normal", buf, "baz.txt");

    neverc_path_base("/foo/bar/", buf, sizeof(buf));
    check_str("base trailing /", buf, "bar");

    neverc_path_base("baz.txt", buf, sizeof(buf));
    check_str("base no dir", buf, "baz.txt");

    neverc_path_base("/", buf, sizeof(buf));
    check_str("base root", buf, "/");

    neverc_path_base("", buf, sizeof(buf));
    check_str("base empty", buf, ".");

    neverc_path_base("///", buf, sizeof(buf));
    check_str("base slashes", buf, "/");
}

/* ===== Test: Dir ===== */
static void test_dir(void) {
    printf("[dir]\n");
    char buf[256];
    neverc_path_dir("/foo/bar/baz.txt", buf, sizeof(buf));
    check_str("dir normal", buf, "/foo/bar");

    neverc_path_dir("/foo/bar/", buf, sizeof(buf));
    check_str("dir trailing /", buf, "/foo/bar");

    neverc_path_dir("baz.txt", buf, sizeof(buf));
    check_str("dir no dir", buf, ".");

    neverc_path_dir("/", buf, sizeof(buf));
    check_str("dir root", buf, "/");

    neverc_path_dir("", buf, sizeof(buf));
    check_str("dir empty", buf, ".");

    char *longpath = (char *)malloc(5003);
    if (longpath) {
        memset(longpath, 'a', 5000);
        longpath[5000] = '/';
        longpath[5001] = 'b';
        longpath[5002] = '\0';
        check_int("dir overflow", neverc_path_dir(longpath, buf, sizeof(buf)), -1);
        {
            char *out = (char *)malloc(5002);
            int n = out ? neverc_path_dir(longpath, out, 5002) : -1;
            check_int("dir long n", n, 5000);
            check_int("dir long bytes",
                      n == 5000 && out && out[0] == 'a' && out[4999] == 'a', 1);
            free(out);
        }
        free(longpath);
    }

    neverc_path_dir("/foo/bar/../baz", buf, sizeof(buf));
    check_str("dir cleans dotdot", buf, "/foo");
}

/* ===== Test: Ext ===== */
static void test_ext(void) {
    printf("[ext]\n");
    check_str("ext .txt",   neverc_path_ext("/foo/bar.txt"), ".txt");
    check_str("ext .tar.gz", neverc_path_ext("/foo/bar.tar.gz"), ".gz");
    check_str("ext none",   neverc_path_ext("/foo/bar"), "");
    check_str("ext dir.",   neverc_path_ext("/foo.bar/baz"), "");
    check_str("ext empty",  neverc_path_ext(""), "");
}

/* ===== Test: IsAbs ===== */
static void test_isabs(void) {
    printf("[isabs]\n");
    check_int("isabs /foo",  neverc_path_isabs("/foo"), 1);
    check_int("isabs foo",   neverc_path_isabs("foo"), 0);
    check_int("isabs empty", neverc_path_isabs(""), 0);
    check_int("isabs /",     neverc_path_isabs("/"), 1);
}

/* ===== Test: Clean ===== */
static void test_clean(void) {
    printf("[clean]\n");
    char buf[256];

    neverc_path_clean("/foo//bar", buf, sizeof(buf));
    check_str("clean double /", buf, "/foo/bar");

    neverc_path_clean("/foo/./bar", buf, sizeof(buf));
    check_str("clean dot", buf, "/foo/bar");

    neverc_path_clean("/foo/bar/../baz", buf, sizeof(buf));
    check_str("clean dotdot", buf, "/foo/baz");

    neverc_path_clean("/../foo", buf, sizeof(buf));
    check_str("clean root dotdot", buf, "/foo");

    neverc_path_clean("", buf, sizeof(buf));
    check_str("clean empty", buf, ".");

    neverc_path_clean(".", buf, sizeof(buf));
    check_str("clean dot only", buf, ".");

    neverc_path_clean("foo/../bar", buf, sizeof(buf));
    check_str("clean relative dotdot", buf, "bar");

    neverc_path_clean("../foo", buf, sizeof(buf));
    check_str("clean leading dotdot", buf, "../foo");

    neverc_path_clean("a/b/c", buf, sizeof(buf));
    check_str("clean simple", buf, "a/b/c");

    neverc_path_clean("abc/def/../..", buf, sizeof(buf));
    check_str("clean to dot", buf, ".");

    neverc_path_clean("/abc/def/../..", buf, sizeof(buf));
    check_str("clean to root", buf, "/");

    neverc_path_clean("abc/def/../../..", buf, sizeof(buf));
    check_str("clean extra dotdot", buf, "..");

    neverc_path_clean("foo/../..", buf, sizeof(buf));
    check_str("clean above start", buf, "..");
}

/* ===== Test: Join ===== */
static void test_join(void) {
    printf("[join]\n");
    char buf[256];

    neverc_path_join2("foo", "bar", buf, sizeof(buf));
    check_str("join simple", buf, "foo/bar");

    neverc_path_join2("foo/", "/bar", buf, sizeof(buf));
    check_str("join slashes", buf, "foo/bar");

    neverc_path_join2("", "bar", buf, sizeof(buf));
    check_str("join empty a", buf, "bar");

    neverc_path_join2("foo", "", buf, sizeof(buf));
    check_str("join empty b", buf, "foo");

    neverc_path_join2("", "", buf, sizeof(buf));
    check_str("join both empty", buf, "");

    neverc_path_join2("/foo", "bar/baz", buf, sizeof(buf));
    check_str("join abs", buf, "/foo/bar/baz");

    neverc_path_join2("a/b", "../c", buf, sizeof(buf));
    check_str("join dotdot", buf, "a/c");

    neverc_path_join2("/a", "../..", buf, sizeof(buf));
    check_str("join above root", buf, "/");

    /* Longer than the old 4096-byte stack scratch. */
    {
        enum { N = 3000 };
        char *a = (char *)malloc(N + 1);
        char *b = (char *)malloc(N + 1);
        char *out = (char *)malloc(2 * N + 2);
        int n, i, ok;
        if (!a || !b || !out) {
            check_int("join long alloc", 0, 1);
        } else {
            memset(a, 'x', N); a[N] = '\0';
            memset(b, 'y', N); b[N] = '\0';
            n = neverc_path_join2(a, b, out, 2 * N + 2);
            check_int("join long n", n, 2 * N + 1);
            ok = (n == 2 * N + 1);
            for (i = 0; ok && i < N; i++)
                if (out[i] != 'x') ok = 0;
            if (ok && out[N] != '/') ok = 0;
            for (i = 0; ok && i < N; i++)
                if (out[N + 1 + i] != 'y') ok = 0;
            check_int("join long bytes", ok, 1);
        }
        free(a); free(b); free(out);
    }
}

/* ===== Test: Split ===== */
static void test_split(void) {
    printf("[split]\n");
    char dir[256], file[256];

    neverc_path_split("/foo/bar/baz.txt", dir, sizeof(dir), file, sizeof(file));
    check_str("split dir", dir, "/foo/bar/");
    check_str("split file", file, "baz.txt");

    neverc_path_split("baz.txt", dir, sizeof(dir), file, sizeof(file));
    check_str("split no dir - dir", dir, "");
    check_str("split no dir - file", file, "baz.txt");

    neverc_path_split("/", dir, sizeof(dir), file, sizeof(file));
    check_str("split root - dir", dir, "/");
    check_str("split root - file", file, "");
}

static void test_match(void) {
    printf("[match]\n");
    check_int("match star", neverc_path_match("*.txt", "hello.txt"), 1);
    check_int("match star no", neverc_path_match("*.txt", "hello.go"), 0);
    check_int("match exact", neverc_path_match("hello", "hello"), 1);
    check_int("match exact no", neverc_path_match("hello", "world"), 0);
    check_int("match question", neverc_path_match("h?llo", "hello"), 1);
    check_int("match question no", neverc_path_match("h?llo", "hllo"), 0);
    check_int("match class", neverc_path_match("[abc]", "b"), 1);
    check_int("match class no", neverc_path_match("[abc]", "d"), 0);
    check_int("match range", neverc_path_match("[a-z]", "m"), 1);
    check_int("match range no", neverc_path_match("[a-z]", "M"), 0);
    check_int("match complex", neverc_path_match("test_*.c", "test_math.c"), 1);
    check_int("match empty", neverc_path_match("", ""), 1);
    check_int("match star empty", neverc_path_match("*", "anything"), 1);
    check_int("star does not cross slash",
              neverc_path_match("a*b", "a/b"), 0);
    check_int("trailing star does not cross slash",
              neverc_path_match("foo*", "foo/bar"), 0);
    check_int("star stops before literal slash",
              neverc_path_match("*/bar", "foo/bar"), 1);
    check_int("escaped star", neverc_path_match("\\*", "*"), 1);
    check_int("escaped star no", neverc_path_match("\\*", "a"), 0);
    check_int("bad unclosed class", neverc_path_match("[abc", "a"), -1);
    check_int("bad leftover class", neverc_path_match("x[", "y"), -1);
    check_int("bad trailing escape", neverc_path_match("\\", "a"), -1);
    check_int("bang is literal in class", neverc_path_match("[!a]", "!"), 1);
    check_int("bang is not negation", neverc_path_match("[!a]", "b"), 0);
    check_int("caret negation", neverc_path_match("[^a]", "b"), 1);
    check_int("caret negation no", neverc_path_match("[^a]", "a"), 0);
}

int main(void) {
    printf("=== NeverC Path Library Tests ===\n\n");

    test_base();
    test_dir();
    test_ext();
    test_isabs();
    test_clean();
    test_join();
    test_split();
    test_match();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
