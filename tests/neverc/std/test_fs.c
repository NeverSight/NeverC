#include "neverc/std/io/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

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
    check("backslash", neverc_fs_valid_path("foo\\bar") == 0);
    check("dotdot_backslash", neverc_fs_valid_path("..\\etc\\passwd") == 0);
#if defined(_WIN32)
    check("drive relative", neverc_fs_valid_path("C:../evil") == 0);
    check("drive prefix", neverc_fs_valid_path("C:foo") == 0);
    check("ads colon", neverc_fs_valid_path("file:stream") == 0);
#endif
}

#if defined(_WIN32)
#include <windows.h>

static void get_temp_dir(char *buf, size_t cap) {
    GetTempPathA((DWORD)cap, buf);
    size_t len = strlen(buf);
    if (len > 0 && (buf[len-1] == '\\' || buf[len-1] == '/'))
        buf[len-1] = '\0';
}
#else
static void get_temp_dir(char *buf, size_t cap) {
    const char *t = getenv("TMPDIR");
    if (!t) t = "/tmp";
    snprintf(buf, cap, "%s", t);
}
#endif

static void test_stat(void) {
    printf("[stat]\n");
    char tmpdir[1024];
    get_temp_dir(tmpdir, sizeof(tmpdir));

    neverc_fs_file_info_t info;
    int rc = neverc_fs_stat(tmpdir, &info);
    check("stat_tmp", rc == 0);
    check("tmp_is_dir", info.is_dir == 1);

    rc = neverc_fs_stat("/nonexistent_path_12345", &info);
    check("stat_nonexistent", rc != 0);
}

static void test_read_file(void) {
    printf("[read_file]\n");
    char tmpdir[1024];
    get_temp_dir(tmpdir, sizeof(tmpdir));

    char filepath[2048];
#if defined(_WIN32)
    snprintf(filepath, sizeof(filepath), "%s\\neverc_test_fs_read.txt", tmpdir);
#else
    snprintf(filepath, sizeof(filepath), "%s/neverc_test_fs_read.txt", tmpdir);
#endif

    FILE *f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "hello world");
        fclose(f);
    }

    uint8_t *data = NULL;
    size_t size = 0;
    int rc = neverc_fs_read_file(filepath, &data, &size);
    check("read_ok", rc == 0);
    check("read_size", size == 11);
    check("read_content", data && memcmp(data, "hello world", 11) == 0);
    free(data);
    remove(filepath);
}

static void test_read_dir(void) {
    printf("[read_dir]\n");
    char tmpdir[1024];
    get_temp_dir(tmpdir, sizeof(tmpdir));

    neverc_fs_dir_entry_t *entries = NULL;
    size_t count = 0;
    int rc = neverc_fs_read_dir(tmpdir, &entries, &count);
    check("readdir_ok", rc == 0);
    check("readdir_not_empty", count > 0);
    neverc_fs_free_entries(entries);
}

static void test_glob(void) {
    printf("[glob]\n");
    char tmpdir[1024];
    get_temp_dir(tmpdir, sizeof(tmpdir));

    char filepath[2048];
#if defined(_WIN32)
    snprintf(filepath, sizeof(filepath), "%s\\neverc_glob_test_abc.txt", tmpdir);
#else
    snprintf(filepath, sizeof(filepath), "%s/neverc_glob_test_abc.txt", tmpdir);
#endif

    FILE *f = fopen(filepath, "w");
    if (f) { fprintf(f, "x"); fclose(f); }

    char **matches = NULL;
    size_t count = 0;
    int rc = neverc_fs_glob(tmpdir, "neverc_glob_test_*.txt", &matches, &count);
    check("glob_ok", rc == 0);
    check("glob_found", count >= 1);
    neverc_fs_free_matches(matches, count);
    remove(filepath);
}

static int walk_count;
static int walk_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)path; (void)entry; (void)ud;
    walk_count++;
    return 0;
}

static void test_walk_dir(void) {
    printf("[walk_dir]\n");
    char tmpdir[1024];
    get_temp_dir(tmpdir, sizeof(tmpdir));

    char walkdir[2048], subdir[2048], file_a[2048], file_b[2048];
#if defined(_WIN32)
    snprintf(walkdir, sizeof(walkdir), "%s\\neverc_walk_test", tmpdir);
    snprintf(subdir,  sizeof(subdir),  "%s\\sub", walkdir);
    snprintf(file_a,  sizeof(file_a),  "%s\\a.txt", walkdir);
    snprintf(file_b,  sizeof(file_b),  "%s\\b.txt", subdir);
    CreateDirectoryA(walkdir, NULL);
    CreateDirectoryA(subdir, NULL);
#else
    snprintf(walkdir, sizeof(walkdir), "%s/neverc_walk_test", tmpdir);
    snprintf(subdir,  sizeof(subdir),  "%s/sub", walkdir);
    snprintf(file_a,  sizeof(file_a),  "%s/a.txt", walkdir);
    snprintf(file_b,  sizeof(file_b),  "%s/b.txt", subdir);
    mkdir(walkdir, 0755);
    mkdir(subdir, 0755);
#endif

    FILE *fa = fopen(file_a, "w");
    if (fa) { fprintf(fa, "a"); fclose(fa); }
    FILE *fb = fopen(file_b, "w");
    if (fb) { fprintf(fb, "b"); fclose(fb); }

    walk_count = 0;
    int rc = neverc_fs_walk_dir(walkdir, walk_cb, NULL);
    check("walk_ok", rc == 0);
    check("walk_found_files", walk_count >= 3);

    remove(file_b);
    remove(file_a);
#if defined(_WIN32)
    RemoveDirectoryA(subdir);
    RemoveDirectoryA(walkdir);
#else
    rmdir(subdir);
    rmdir(walkdir);
#endif
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
