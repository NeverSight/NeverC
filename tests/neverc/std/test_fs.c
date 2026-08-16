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
    check("reserved_con", neverc_fs_valid_path("CON") == 0);
    check("reserved_con_txt", neverc_fs_valid_path("CON.txt") == 0);
    check("reserved_nul", neverc_fs_valid_path("nul") == 0);
    check("reserved_com1", neverc_fs_valid_path("dir/COM1") == 0);
    check("reserved_lpt9", neverc_fs_valid_path("LPT9.log") == 0);
    check("trailing_dot", neverc_fs_valid_path("foo.") == 0);
    check("trailing_space", neverc_fs_valid_path("foo ") == 0);
    check("conlike", neverc_fs_valid_path("console.txt") == 1);
    check("com10", neverc_fs_valid_path("COM10") == 1);
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
    char tmp_name[256];
    memcpy(tmp_name, info.name, sizeof(tmp_name));

    rc = neverc_fs_stat("/nonexistent_path_12345", &info);
    check("stat_nonexistent", rc != 0);

    {
        char slashed[1100];
        size_t tlen = strlen(tmpdir);
        if (tlen > 0 && tlen + 2 < sizeof(slashed)) {
            memcpy(slashed, tmpdir, tlen);
#if defined(_WIN32)
            slashed[tlen] = '\\';
#else
            slashed[tlen] = '/';
#endif
            slashed[tlen + 1] = '\0';
            neverc_fs_file_info_t slash_info;
            rc = neverc_fs_stat(slashed, &slash_info);
            check("stat_trailing_slash", rc == 0);
            check("stat_trailing_slash_is_dir", slash_info.is_dir == 1);
            check("stat_trailing_slash_name", slash_info.name[0] != '\0');
            check("stat_trailing_slash_name_match",
                  strcmp(slash_info.name, tmp_name) == 0);
        }
    }
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

    neverc_fs_dir_entry_t *as_dir = (neverc_fs_dir_entry_t *)1;
    size_t as_dir_count = 99;
    check("readdir_file_fails",
          neverc_fs_read_dir(filepath, &as_dir, &as_dir_count) != 0);
    check("readdir_file_clears", as_dir == NULL && as_dir_count == 0);

    uint8_t *dir_data = (uint8_t *)1;
    size_t dir_size = 99;
    check("read_file_dir_fails",
          neverc_fs_read_file(tmpdir, &dir_data, &dir_size) != 0);
    check("read_file_dir_clears", dir_data == NULL && dir_size == 0);

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

    {
        char slashed[1100];
        size_t tlen = strlen(tmpdir);
        if (tlen > 0 && tlen + 2 < sizeof(slashed)) {
            memcpy(slashed, tmpdir, tlen);
#if defined(_WIN32)
            slashed[tlen] = '\\';
#else
            slashed[tlen] = '/';
#endif
            slashed[tlen + 1] = '\0';
            matches = NULL;
            count = 0;
            rc = neverc_fs_glob(slashed, "neverc_glob_test_*.txt",
                                &matches, &count);
            check("glob_trailing_slash_ok", rc == 0);
            check("glob_trailing_slash_found", count >= 1);
            neverc_fs_free_matches(matches, count);
        }
    }
    remove(filepath);
}

static int walk_count;
static int walk_saw_secret;
static int walk_root_is_dir;
static int walk_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)path; (void)ud;
    if (walk_count == 0)
        walk_root_is_dir = entry ? entry->is_dir : -1;
    walk_count++;
    return 0;
}

static int walk_secret_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)entry; (void)ud;
    if (path && strstr(path, "outside_secret")) walk_saw_secret = 1;
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
    walk_root_is_dir = -1;
    int rc = neverc_fs_walk_dir(walkdir, walk_cb, NULL);
    check("walk_ok", rc == 0);
    check("walk_found_files", walk_count >= 3);
    check("walk_root_is_dir", walk_root_is_dir == 1);

    walk_count = 0;
    walk_root_is_dir = -1;
    rc = neverc_fs_walk_dir(file_a, walk_cb, NULL);
    check("walk_file_ok", rc == 0);
    check("walk_file_once", walk_count == 1);
    check("walk_file_not_dir", walk_root_is_dir == 0);

    check("walk_missing",
          neverc_fs_walk_dir("/nonexistent_path_12345", walk_cb, NULL) != 0);

    remove(file_b);
    remove(file_a);
#if defined(_WIN32)
    RemoveDirectoryA(subdir);
    RemoveDirectoryA(walkdir);
#else
    rmdir(subdir);
    rmdir(walkdir);
#endif

#if !defined(_WIN32)
    {
        char outside[2048], secret[2048], linkpath[2048];
        snprintf(outside, sizeof(outside), "%s/neverc_walk_outside", tmpdir);
        snprintf(secret, sizeof(secret), "%s/outside_secret", outside);
        snprintf(walkdir, sizeof(walkdir), "%s/neverc_walk_nosym", tmpdir);
        snprintf(linkpath, sizeof(linkpath), "%s/link", walkdir);
        mkdir(outside, 0755);
        mkdir(walkdir, 0755);
        FILE *fs = fopen(secret, "w");
        if (fs) { fprintf(fs, "x"); fclose(fs); }
        symlink(outside, linkpath);
        walk_saw_secret = 0;
        rc = neverc_fs_walk_dir(walkdir, walk_secret_cb, NULL);
        check("walk_symlink_ok", rc == 0);
        check("walk_skips_symlink_dir", walk_saw_secret == 0);
        unlink(secret);
        unlink(linkpath);
        rmdir(outside);
        rmdir(walkdir);
    }
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
