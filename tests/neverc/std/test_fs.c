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
    check("com10", neverc_fs_valid_path("COM10") == 0);
    check("com0", neverc_fs_valid_path("COM0") == 0);
    check("conin$", neverc_fs_valid_path("CONIN$") == 0);
    check("conout$", neverc_fs_valid_path("CONOUT$") == 0);
    check("invalid_utf8", neverc_fs_valid_path("\xff") == 0);
    check("invalid_utf8_elem", neverc_fs_valid_path("ok/\x80") == 0);
    check("utf8_ok", neverc_fs_valid_path("\xe4\xb8\x96.txt") == 1);
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
    rc = neverc_fs_lstat("/nonexistent_path_12345", &info);
    check("lstat_nonexistent", rc != 0);
    check("stat_null_path", neverc_fs_stat(NULL, &info) != 0);
    check("stat_null_info", neverc_fs_stat(tmpdir, NULL) != 0);
    check("lstat_null_path", neverc_fs_lstat(NULL, &info) != 0);
    check("lstat_null_info", neverc_fs_lstat(tmpdir, NULL) != 0);

    {
        neverc_fs_file_info_t linfo;
        rc = neverc_fs_lstat(tmpdir, &linfo);
        check("lstat_tmp", rc == 0);
        check("lstat_tmp_is_dir", linfo.is_dir == 1);
        check("lstat_tmp_name_match", strcmp(linfo.name, tmp_name) == 0);
    }

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
    {
        size_t i, ok = count >= 1;
        size_t tlen = strlen(tmpdir);
        for (i = 0; i < count; i++) {
            const char *rest;
            if (!matches[i] || strncmp(matches[i], tmpdir, tlen) != 0) {
                ok = 0;
                break;
            }
            rest = matches[i] + tlen;
            if (*rest == '/' || *rest == '\\')
                rest++;
            if (rest[0] == '.' && rest[1] == '.' &&
                (rest[2] == '\0' || rest[2] == '/' || rest[2] == '\\'))
                ok = 0;
        }
        check("glob_stays_under_dir", ok);
    }
    neverc_fs_free_matches(matches, count);

    {
        char class_a[2048], class_b[2048];
#if defined(_WIN32)
        snprintf(class_a, sizeof(class_a), "%s\\neverc_glob_class_a.txt", tmpdir);
        snprintf(class_b, sizeof(class_b), "%s\\neverc_glob_class_b.txt", tmpdir);
#else
        snprintf(class_a, sizeof(class_a), "%s/neverc_glob_class_a.txt", tmpdir);
        snprintf(class_b, sizeof(class_b), "%s/neverc_glob_class_b.txt", tmpdir);
#endif
        FILE *fa = fopen(class_a, "w");
        if (fa) { fprintf(fa, "a"); fclose(fa); }
        FILE *fb = fopen(class_b, "w");
        if (fb) { fprintf(fb, "b"); fclose(fb); }
        matches = NULL;
        count = 0;
        rc = neverc_fs_glob(tmpdir, "neverc_glob_class_[ab].txt",
                            &matches, &count);
        check("glob_class_ok", rc == 0);
        check("glob_class_found", count >= 2);
        neverc_fs_free_matches(matches, count);
        remove(class_a);
        remove(class_b);
    }

#if !defined(_WIN32)
    {
        /* 世 is one UTF-8 rune. Go path.Match "?" matches it; a byte
         * matcher would consume one byte and miss the file. */
        char utf8path[2048];
        snprintf(utf8path, sizeof(utf8path),
                 "%s/neverc_glob_\xe4\xb8\x96.txt", tmpdir);
        FILE *fu = fopen(utf8path, "w");
        if (fu) { fprintf(fu, "u"); fclose(fu); }
        matches = NULL;
        count = 0;
        rc = neverc_fs_glob(tmpdir, "neverc_glob_?.txt", &matches, &count);
        check("glob_utf8_question_ok", rc == 0);
        check("glob_utf8_question_found", count >= 1);
        neverc_fs_free_matches(matches, count);
        matches = NULL;
        count = 0;
        rc = neverc_fs_glob(tmpdir, "neverc_glob_??.txt", &matches, &count);
        check("glob_utf8_two_questions_ok", rc == 0);
        {
            size_t i, hit = 0;
            for (i = 0; i < count; i++) {
                if (matches[i] && strstr(matches[i], "\xe4\xb8\x96"))
                    hit++;
            }
            check("glob_utf8_two_questions_no_rune", hit == 0);
        }
        neverc_fs_free_matches(matches, count);
        remove(utf8path);
    }
#endif

    matches = (char **)1;
    count = 99;
    rc = neverc_fs_glob(tmpdir, "neverc_glob_[", &matches, &count);
    check("glob_bad_pattern", rc != 0);
    check("glob_bad_clears", matches == NULL && count == 0);

    matches = (char **)1;
    count = 99;
    rc = neverc_fs_glob(tmpdir, "../neverc_glob_test_*.txt", &matches, &count);
    check("glob_rejects_slash_pattern", rc != 0);
    check("glob_slash_clears", matches == NULL && count == 0);

    matches = (char **)1;
    count = 99;
    rc = neverc_fs_glob(tmpdir, "neverc_glob_test_abc.txt/../x", &matches, &count);
    check("glob_rejects_nested_pattern", rc != 0);
    check("glob_nested_clears", matches == NULL && count == 0);

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
static int walk_saw_child;
static int walk_skip_hidden;
static int walk_skip_after;
static int walk_saw_null_entry;
static int walk_saw_zzz;
static int walk_saw_aaa;

static int walk_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)path; (void)ud;
    if (walk_count == 0)
        walk_root_is_dir = entry ? entry->is_dir : -1;
    walk_count++;
    return 0;
}

static int walk_err_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)path; (void)entry; (void)ud;
    return -1;
}

static int walk_skip_dir_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)ud;
    walk_count++;
    if (path && strstr(path, "hidden.txt")) walk_skip_hidden = 1;
    if (path && strstr(path, "after.txt")) walk_skip_after = 1;
    if (entry && strcmp(entry->name, "skipme") == 0)
        return NEVERC_FS_SKIP_DIR;
    return 0;
}

static int walk_skip_all_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)ud;
    walk_count++;
    if (path && strstr(path, "b.txt")) walk_saw_child = 1;
    if (entry && strcmp(entry->name, "sub") == 0)
        return NEVERC_FS_SKIP_ALL;
    return 0;
}

static int walk_child_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)entry; (void)ud;
    walk_count++;
    if (path && (strstr(path, "a.txt") || strstr(path, "b.txt")))
        walk_saw_child = 1;
    return 0;
}

static int walk_secret_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)entry; (void)ud;
    if (path && strstr(path, "outside_secret")) walk_saw_secret = 1;
    return 0;
}

static int walk_null_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)path; (void)ud;
    walk_count++;
    if (!entry) walk_saw_null_entry = 1;
    return 0;
}

static int walk_null_skip_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)path; (void)ud;
    walk_count++;
    if (!entry) return NEVERC_FS_SKIP_DIR;
    return 0;
}

static int walk_skip_file_cb(const char *path, const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)ud;
    walk_count++;
    if (path && strstr(path, "aaa.txt")) walk_saw_aaa = 1;
    if (path && strstr(path, "zzz.txt")) walk_saw_zzz = 1;
    if (entry && strcmp(entry->name, "skipfile") == 0)
        return NEVERC_FS_SKIP_DIR;
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

    walk_count = 0;
    walk_saw_null_entry = 0;
    rc = neverc_fs_walk_dir("/nonexistent_path_12345", walk_null_cb, NULL);
    check("walk_missing_calls_fn", rc != 0);
    check("walk_missing_null_entry", walk_saw_null_entry == 1);
    check("walk_missing_once", walk_count == 1);

    walk_count = 0;
    rc = neverc_fs_walk_dir("/nonexistent_path_12345", walk_null_skip_cb, NULL);
    check("walk_missing_skip_ok", rc == 0);
    check("walk_missing_skip_once", walk_count == 1);

    check("walk_err", neverc_fs_walk_dir(walkdir, walk_err_cb, NULL) == -1);

    {
        char skipdir[2048], hidden[2048], after[2048];
#if defined(_WIN32)
        snprintf(skipdir, sizeof(skipdir), "%s\\skipme", walkdir);
        snprintf(hidden, sizeof(hidden), "%s\\hidden.txt", skipdir);
        snprintf(after, sizeof(after), "%s\\after.txt", walkdir);
        CreateDirectoryA(skipdir, NULL);
#else
        snprintf(skipdir, sizeof(skipdir), "%s/skipme", walkdir);
        snprintf(hidden, sizeof(hidden), "%s/hidden.txt", skipdir);
        snprintf(after, sizeof(after), "%s/after.txt", walkdir);
        mkdir(skipdir, 0755);
#endif
        FILE *fh = fopen(hidden, "w");
        if (fh) { fprintf(fh, "h"); fclose(fh); }
        FILE *faf = fopen(after, "w");
        if (faf) { fprintf(faf, "a"); fclose(faf); }

        walk_count = 0;
        walk_skip_hidden = 0;
        walk_skip_after = 0;
        rc = neverc_fs_walk_dir(walkdir, walk_skip_dir_cb, NULL);
        check("walk_skip_dir_ok", rc == 0);
        check("walk_skip_dir_hides_child", walk_skip_hidden == 0);
        check("walk_skip_dir_keeps_sibling", walk_skip_after == 1);

        walk_count = 0;
        walk_saw_child = 0;
        rc = neverc_fs_walk_dir(walkdir, walk_skip_all_cb, NULL);
        check("walk_skip_all_ok", rc == 0);
        check("walk_skip_all_no_nested", walk_saw_child == 0);

        {
            char aaa[2048], skipfile[2048], zzz[2048];
#if defined(_WIN32)
            snprintf(aaa, sizeof(aaa), "%s\\aaa.txt", walkdir);
            snprintf(skipfile, sizeof(skipfile), "%s\\skipfile", walkdir);
            snprintf(zzz, sizeof(zzz), "%s\\zzz.txt", walkdir);
#else
            snprintf(aaa, sizeof(aaa), "%s/aaa.txt", walkdir);
            snprintf(skipfile, sizeof(skipfile), "%s/skipfile", walkdir);
            snprintf(zzz, sizeof(zzz), "%s/zzz.txt", walkdir);
#endif
            FILE *faaa = fopen(aaa, "w");
            if (faaa) { fprintf(faaa, "a"); fclose(faaa); }
            FILE *fsk = fopen(skipfile, "w");
            if (fsk) { fprintf(fsk, "s"); fclose(fsk); }
            FILE *fzzz = fopen(zzz, "w");
            if (fzzz) { fprintf(fzzz, "z"); fclose(fzzz); }

            walk_count = 0;
            walk_saw_aaa = 0;
            walk_saw_zzz = 0;
            rc = neverc_fs_walk_dir(walkdir, walk_skip_file_cb, NULL);
            check("walk_skip_file_ok", rc == 0);
            check("walk_skip_file_saw_prior", walk_saw_aaa == 1);
            check("walk_skip_file_hides_rest", walk_saw_zzz == 0);

            remove(aaa);
            remove(skipfile);
            remove(zzz);
        }

        remove(hidden);
        remove(after);
#if defined(_WIN32)
        RemoveDirectoryA(skipdir);
#else
        rmdir(skipdir);
#endif
    }

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
    {
        /* A directory entry named ".. " is a regular name on POSIX, not
         * parent. Walk must visit it without leaving the root. */
        char stay[2048], odd[2048], outside2[2048], secret2[2048];
        snprintf(stay, sizeof(stay), "%s/neverc_walk_dotspace", tmpdir);
        snprintf(odd, sizeof(odd), "%s/.. ", stay);
        snprintf(outside2, sizeof(outside2), "%s/neverc_walk_dotspace_out",
                 tmpdir);
        snprintf(secret2, sizeof(secret2), "%s/outside_secret", outside2);
        mkdir(stay, 0755);
        mkdir(outside2, 0755);
        FILE *fo = fopen(odd, "w");
        if (fo) { fprintf(fo, "o"); fclose(fo); }
        FILE *fs2 = fopen(secret2, "w");
        if (fs2) { fprintf(fs2, "s"); fclose(fs2); }
        walk_saw_secret = 0;
        rc = neverc_fs_walk_dir(stay, walk_secret_cb, NULL);
        check("walk_dotspace_ok", rc == 0);
        check("walk_dotspace_no_escape", walk_saw_secret == 0);
        unlink(odd);
        unlink(secret2);
        rmdir(outside2);
        rmdir(stay);
    }
    {
        char target[2048], linkroot[2048];
        snprintf(walkdir, sizeof(walkdir), "%s/neverc_walk_real", tmpdir);
        snprintf(subdir, sizeof(subdir), "%s/sub", walkdir);
        snprintf(file_a, sizeof(file_a), "%s/a.txt", walkdir);
        snprintf(file_b, sizeof(file_b), "%s/b.txt", subdir);
        snprintf(linkroot, sizeof(linkroot), "%s/neverc_walk_rootlink", tmpdir);
        mkdir(walkdir, 0755);
        mkdir(subdir, 0755);
        FILE *fa2 = fopen(file_a, "w");
        if (fa2) { fprintf(fa2, "a"); fclose(fa2); }
        FILE *fb2 = fopen(file_b, "w");
        if (fb2) { fprintf(fb2, "b"); fclose(fb2); }
        symlink(walkdir, linkroot);

        neverc_fs_file_info_t sinfo, linfo;
        check("stat_follows_link", neverc_fs_stat(linkroot, &sinfo) == 0);
        check("stat_link_is_dir", sinfo.is_dir == 1);
        check("stat_link_not_mode_link", (sinfo.mode & NEVERC_FS_MODE_LINK) == 0);
        check("lstat_is_link", neverc_fs_lstat(linkroot, &linfo) == 0);
        check("lstat_mode_link", (linfo.mode & NEVERC_FS_MODE_LINK) != 0);
        check("lstat_not_dir", linfo.is_dir == 0);

        walk_count = 0;
        walk_saw_child = 0;
        rc = neverc_fs_walk_dir(linkroot, walk_child_cb, NULL);
        check("walk_root_symlink_ok", rc == 0);
        check("walk_root_symlink_once", walk_count == 1);
        check("walk_root_symlink_no_follow", walk_saw_child == 0);

        snprintf(target, sizeof(target), "%s/neverc_fs_setuid", tmpdir);
        FILE *fu = fopen(target, "w");
        if (fu) { fprintf(fu, "u"); fclose(fu); }
        chmod(target, 04755);
        check("setuid_stat", neverc_fs_stat(target, &sinfo) == 0);
        check("setuid_bit", (sinfo.mode & NEVERC_FS_MODE_SETUID) != 0);
        check("setuid_perm", (sinfo.mode & NEVERC_FS_PERM_MASK) == 0755);
        remove(target);

        snprintf(target, sizeof(target), "%s/neverc_fs_fifo", tmpdir);
        unlink(target);
        if (mkfifo(target, 0644) == 0) {
            check("fifo_lstat", neverc_fs_lstat(target, &linfo) == 0);
            check("fifo_mode", (linfo.mode & NEVERC_FS_MODE_PIPE) != 0);
            unlink(target);
        }

        remove(file_b);
        remove(file_a);
        unlink(linkroot);
        rmdir(subdir);
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
    if (tests_passed == tests_run) puts("passed");
    return tests_passed == tests_run ? 0 : 1;
}
