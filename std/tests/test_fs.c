#include "neverc/io/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0;

static void check(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else printf("  FAIL: %s\n", name);
}

static void test_valid_path(void) {
    printf("[valid_path]\n");
    check("dot", neverc_fs_valid_path(".") == 1);
    check("simple", neverc_fs_valid_path("x/y/z") == 1);
    check("file", neverc_fs_valid_path("foo.txt") == 1);
    check("empty", neverc_fs_valid_path("") == 0);
    check("leading_slash", neverc_fs_valid_path("/foo") == 0);
    check("trailing_slash", neverc_fs_valid_path("foo/") == 0);
    check("dot_element", neverc_fs_valid_path("foo/./bar") == 0);
    check("dotdot_element", neverc_fs_valid_path("foo/../bar") == 0);
    check("double_slash", neverc_fs_valid_path("foo//bar") == 0);
}

static void test_stat(void) {
    printf("[stat]\n");
    neverc_fs_file_info_t info;
    int rc = neverc_fs_stat("/tmp", &info);
    check("stat_tmp", rc == 0);
    check("tmp_is_dir", info.is_dir == 1);

    rc = neverc_fs_stat("/nonexistent_path_12345", &info);
    check("stat_nonexistent", rc != 0);
}

static void test_read_file(void) {
    printf("[read_file]\n");
    FILE *f = fopen("/tmp/neverc_test_fs_read.txt", "w");
    if (f) {
        fprintf(f, "hello world");
        fclose(f);
    }

    uint8_t *data = NULL;
    size_t size = 0;
    int rc = neverc_fs_read_file("/tmp/neverc_test_fs_read.txt", &data, &size);
    check("read_ok", rc == 0);
    check("read_size", size == 11);
    check("read_content", data && memcmp(data, "hello world", 11) == 0);
    free(data);
    remove("/tmp/neverc_test_fs_read.txt");
}

static void test_read_dir(void) {
    printf("[read_dir]\n");
    neverc_fs_dir_entry_t *entries = NULL;
    size_t count = 0;
    int rc = neverc_fs_read_dir("/tmp", &entries, &count);
    check("readdir_ok", rc == 0);
    check("readdir_not_empty", count > 0);
    neverc_fs_free_entries(entries);
}

static void test_glob(void) {
    printf("[glob]\n");
    FILE *f = fopen("/tmp/neverc_glob_test_abc.txt", "w");
    if (f) { fprintf(f, "x"); fclose(f); }

    char **matches = NULL;
    size_t count = 0;
    int rc = neverc_fs_glob("/tmp", "neverc_glob_test_*.txt", &matches, &count);
    check("glob_ok", rc == 0);
    check("glob_found", count >= 1);
    neverc_fs_free_matches(matches, count);
    remove("/tmp/neverc_glob_test_abc.txt");
}

static int walk_count;
static int walk_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)path; (void)entry; (void)ud;
    walk_count++;
    return 0;
}

static void test_walk_dir(void) {
    printf("[walk_dir]\n");
    system("mkdir -p /tmp/neverc_walk_test/sub && touch /tmp/neverc_walk_test/a.txt /tmp/neverc_walk_test/sub/b.txt");
    walk_count = 0;
    int rc = neverc_fs_walk_dir("/tmp/neverc_walk_test", walk_cb, NULL);
    check("walk_ok", rc == 0);
    check("walk_found_files", walk_count >= 3);
    system("rm -rf /tmp/neverc_walk_test");
}

int main(void) {
    test_valid_path();
    test_stat();
    test_read_file();
    test_read_dir();
    test_glob();
    test_walk_dir();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
