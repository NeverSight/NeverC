#include "neverc/path.h"
#include <stdio.h>
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

int main(void) {
    printf("=== NeverC Path Library Tests ===\n\n");

    test_base();
    test_dir();
    test_ext();
    test_isabs();
    test_clean();
    test_join();
    test_split();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
