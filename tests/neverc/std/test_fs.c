#include "neverc/std/io/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#endif

_Static_assert(NEVERC_FS_MODE_DIR == (1 << 0),
               "v3389.1.4 FS_MODE_DIR value changed");
_Static_assert(NEVERC_FS_MODE_APPEND == (1 << 1),
               "v3389.1.4 FS_MODE_APPEND value changed");
_Static_assert(NEVERC_FS_MODE_EXCL == (1 << 2),
               "v3389.1.4 FS_MODE_EXCL value changed");
_Static_assert(NEVERC_FS_MODE_TEMP == (1 << 3),
               "v3389.1.4 FS_MODE_TEMP value changed");
_Static_assert(NEVERC_FS_MODE_LINK == (1 << 4),
               "v3389.1.4 FS_MODE_LINK value changed");
_Static_assert(NEVERC_FS_MODE_PIPE == (1 << 5),
               "v3389.1.4 FS_MODE_PIPE value changed");
_Static_assert(NEVERC_FS_MODE_SOCKET == (1 << 6),
               "v3389.1.4 FS_MODE_SOCKET value changed");
_Static_assert(NEVERC_FS_MODE_DEVICE == (1 << 7),
               "v3389.1.4 FS_MODE_DEVICE value changed");
_Static_assert(NEVERC_FS_MODE_CHAR_DEVICE == (1 << 8),
               "v3389.1.4 FS_MODE_CHAR_DEVICE value changed");
_Static_assert(NEVERC_FS_MODE_IRREGULAR == (1 << 9),
               "v3389.1.4 FS_MODE_IRREGULAR value changed");
_Static_assert(NEVERC_FS_MODE_SETUID == (1 << 10),
               "v3389.1.4 FS_MODE_SETUID value changed");
_Static_assert(NEVERC_FS_MODE_SETGID == (1 << 11),
               "v3389.1.4 FS_MODE_SETGID value changed");
_Static_assert(NEVERC_FS_MODE_STICKY == (1 << 12),
               "v3389.1.4 FS_MODE_STICKY value changed");
_Static_assert(NEVERC_FS_PERM_MASK == 0777,
               "v3389.1.4 FS_PERM_MASK value changed");

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
    check("reserved_con_space_txt", neverc_fs_valid_path("CON .txt") == 0);
    check("reserved_con_spaces_txt", neverc_fs_valid_path("CON  .txt") == 0);
    check("reserved_com1_space_log", neverc_fs_valid_path("COM1 .log") == 0);
    check("reserved_nul_space_ext", neverc_fs_valid_path("NUL .x") == 0);
    check("reserved_conin_space_txt", neverc_fs_valid_path("CONIN$ .txt") == 0);
    check("reserved_com_sup1_space_txt",
          neverc_fs_valid_path("COM\xC2\xB9 .txt") == 0);
    check("reserved_nested_con_space",
          neverc_fs_valid_path("dir/CON .txt") == 0);
    check("conlike_space_txt", neverc_fs_valid_path("console .txt") == 1);
    check("reserved_nul", neverc_fs_valid_path("nul") == 0);
    check("reserved_com1", neverc_fs_valid_path("dir/COM1") == 0);
    check("reserved_lpt9", neverc_fs_valid_path("LPT9.log") == 0);
    check("trailing_dot", neverc_fs_valid_path("foo.") == 0);
    check("trailing_space", neverc_fs_valid_path("foo ") == 0);
    check("conlike", neverc_fs_valid_path("console.txt") == 1);
    check("com10", neverc_fs_valid_path("COM10") == 0);
    check("com0", neverc_fs_valid_path("COM0") == 0);
    check("com_superscript_1", neverc_fs_valid_path("COM\xC2\xB9") == 0);
    check("lpt_superscript_2", neverc_fs_valid_path("LPT\xC2\xB2") == 0);
    check("dir_com_superscript_3",
          neverc_fs_valid_path("dir/COM\xC2\xB3.txt") == 0);
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
#if !defined(_WIN32)
        /* TMPDIR itself is often 0700 on macOS (per-user /var/folders). */
        char execdir[2048];
        snprintf(execdir, sizeof(execdir), "%s/neverc_test_fs_execdir", tmpdir);
        mkdir(execdir, 0755);
        chmod(execdir, 0755);
        rc = neverc_fs_lstat(execdir, &linfo);
        check("lstat_execdir", rc == 0);
        check("lstat_tmp_dir_exec", rc == 0 && (linfo.mode & 0111) == 0111);
        rmdir(execdir);
#else
        check("lstat_tmp_dir_exec", (linfo.mode & 0111) == 0111);
#endif
    }

#if defined(_WIN32)
    {
        char timepath[2048];
        snprintf(timepath, sizeof(timepath),
                 "%s\\neverc_test_fs_pre_epoch.txt", tmpdir);
        HANDLE h = CreateFileA(timepath, FILE_WRITE_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE |
                                   FILE_SHARE_DELETE,
                               NULL, CREATE_ALWAYS, 0, NULL);
        check("create_pre_epoch_file", h != INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE) {
            ULARGE_INTEGER ticks;
            FILETIME ft;
            ticks.QuadPart = 116444736000000000ULL -
                             315619200ULL * 10000000ULL;
            ft.dwLowDateTime = ticks.LowPart;
            ft.dwHighDateTime = ticks.HighPart;
            check("set_pre_epoch_filetime",
                  SetFileTime(h, NULL, NULL, &ft) != 0);
            CloseHandle(h);
            check("stat_pre_epoch_filetime",
                  neverc_fs_stat(timepath, &info) == 0 &&
                  info.mod_time == (time_t)-315619200);
            check("lstat_pre_epoch_filetime",
                  neverc_fs_lstat(timepath, &info) == 0 &&
                  info.mod_time == (time_t)-315619200);
            DeleteFileA(timepath);
        }
    }
    {
        char ropath[2048];
        snprintf(ropath, sizeof(ropath), "%s\\neverc_test_fs_ro.txt", tmpdir);
        FILE *rf = fopen(ropath, "wb");
        if (rf) {
            fputs("ro", rf);
            fclose(rf);
        }
        if (SetFileAttributesA(ropath, FILE_ATTRIBUTE_NORMAL)) {
            neverc_fs_file_info_t rw;
            check("lstat_writable", neverc_fs_lstat(ropath, &rw) == 0);
            check("lstat_writable_perm",
                  (rw.mode & NEVERC_FS_PERM_MASK) == 0644);
        }
        if (SetFileAttributesA(ropath, FILE_ATTRIBUTE_READONLY)) {
            neverc_fs_file_info_t ro;
            check("lstat_readonly", neverc_fs_lstat(ropath, &ro) == 0);
            check("lstat_readonly_perm",
                  (ro.mode & NEVERC_FS_PERM_MASK) == 0644);
            SetFileAttributesA(ropath, FILE_ATTRIBUTE_NORMAL);
        }
        DeleteFileA(ropath);
    }
#endif

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

#if defined(__linux__)
    {
        uint8_t *proc = NULL;
        size_t proc_size = 0;
        check("read_proc_status",
              neverc_fs_read_file("/proc/self/status", &proc, &proc_size) == 0);
        check("read_proc_status nonempty", proc != NULL && proc_size > 0);
        free(proc);
    }
#endif

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

#if defined(_WIN32)
    {
        char parent[1024], raw[1100], query[1100];
        snprintf(parent, sizeof(parent), "%s\\neverc_fs_dotdot_%lu",
                 tmpdir, (unsigned long)GetCurrentProcessId());
        CreateDirectoryA(parent, NULL);
        snprintf(raw, sizeof(raw), "\\\\?\\%s\\.. ", parent);
        HANDLE h = CreateFileA(raw, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        snprintf(query, sizeof(query), "\\\\?\\%s", parent);
        entries = NULL;
        count = 0;
        rc = neverc_fs_read_dir(query, &entries, &count);
        check("readdir_ads_parent_ok", rc == 0);
        {
            size_t i;
            int leaked = 0;
            for (i = 0; i < count; i++) {
                const char *n = entries[i].name;
                size_t len = strlen(n);
                while (len > 0 && (n[len - 1] == ' ' || n[len - 1] == '.'))
                    len--;
                if (len == 2 && n[0] == '.' && n[1] == '.')
                    leaked = 1;
            }
            check("readdir_skips_dotdot_space", !leaked);
        }
        neverc_fs_free_entries(entries);
        DeleteFileA(raw);
        RemoveDirectoryA(parent);
    }
    {
        char parent[1024], rwpath[1200], ropath[1200];
        snprintf(parent, sizeof(parent), "%s\\neverc_fs_rd_mode_%lu",
                 tmpdir, (unsigned long)GetCurrentProcessId());
        CreateDirectoryA(parent, NULL);
        snprintf(rwpath, sizeof(rwpath), "%s\\rw.txt", parent);
        snprintf(ropath, sizeof(ropath), "%s\\ro.txt", parent);
        {
            FILE *wf = fopen(rwpath, "wb");
            if (wf) { fputs("rw", wf); fclose(wf); }
            FILE *rf = fopen(ropath, "wb");
            if (rf) { fputs("ro", rf); fclose(rf); }
        }
        SetFileAttributesA(rwpath, FILE_ATTRIBUTE_NORMAL);
        SetFileAttributesA(ropath, FILE_ATTRIBUTE_READONLY);
        entries = NULL;
        count = 0;
        rc = neverc_fs_read_dir(parent, &entries, &count);
        check("readdir_mode_ok", rc == 0);
        {
            int saw_rw = 0, saw_ro = 0, i;
            for (i = 0; i < (int)count; i++) {
                if (strcmp(entries[i].name, "rw.txt") == 0) {
                    saw_rw = 1;
                    check("readdir_writable_perm",
                          (entries[i].mode & NEVERC_FS_PERM_MASK) == 0644);
                }
                if (strcmp(entries[i].name, "ro.txt") == 0) {
                    saw_ro = 1;
                    check("readdir_readonly_perm",
                          (entries[i].mode & NEVERC_FS_PERM_MASK) == 0644);
                }
            }
            check("readdir_saw_rw", saw_rw);
            check("readdir_saw_ro", saw_ro);
        }
        neverc_fs_free_entries(entries);
        SetFileAttributesA(ropath, FILE_ATTRIBUTE_NORMAL);
        DeleteFileA(rwpath);
        DeleteFileA(ropath);
        RemoveDirectoryA(parent);
    }
    {
        char parent[1024], keep[1200], victim[1200], escape[1400];
        snprintf(parent, sizeof(parent), "%s\\neverc_fs_rd_dotdot_%lu",
                 tmpdir, (unsigned long)GetCurrentProcessId());
        CreateDirectoryA(parent, NULL);
        snprintf(keep, sizeof(keep), "%s\\keep", parent);
        snprintf(victim, sizeof(victim), "%s\\victim", parent);
        CreateDirectoryA(keep, NULL);
        CreateDirectoryA(victim, NULL);
        snprintf(escape, sizeof(escape), "\\\\?\\%s\\victim\\..", parent);
        entries = NULL;
        count = 0;
        errno = 0;
        rc = neverc_fs_read_dir(escape, &entries, &count);
        check("readdir_extended_dotdot_rejected", rc == -1);
        check("readdir_extended_dotdot_einval", errno == EINVAL);
        neverc_fs_free_entries(entries);
        RemoveDirectoryA(keep);
        RemoveDirectoryA(victim);
        RemoveDirectoryA(parent);
    }
    {
        /* \??\ is an NT prefix, not a FindFirstFile wildcard. Listing an
         * existing directory through it must succeed once '?' is skipped. */
        char ntpath[1200];
        snprintf(ntpath, sizeof(ntpath), "\\??\\%s", tmpdir);
        entries = NULL;
        count = 0;
        rc = neverc_fs_read_dir(ntpath, &entries, &count);
        check("readdir_nt_prefix_ok", rc == 0 && count > 0);
        neverc_fs_free_entries(entries);
    }
    {
        char cwd[MAX_PATH], drive_ext[16];
        neverc_fs_dir_entry_t *fents = NULL;
        size_t n = 0;
        GetCurrentDirectoryA((DWORD)sizeof(cwd), cwd);
        if (((cwd[0] >= 'A' && cwd[0] <= 'Z') ||
             (cwd[0] >= 'a' && cwd[0] <= 'z')) && cwd[1] == ':') {
            snprintf(drive_ext, sizeof(drive_ext), "\\\\?\\%c:", cwd[0]);
            errno = 0;
            check("readdir_ext_bare_drive",
                  neverc_fs_read_dir(drive_ext, &fents, &n) == -1 &&
                  errno == EINVAL);
            snprintf(drive_ext, sizeof(drive_ext), "\\??\\%c:", cwd[0]);
            errno = 0;
            check("readdir_nt_bare_drive",
                  neverc_fs_read_dir(drive_ext, &fents, &n) == -1 &&
                  errno == EINVAL);
        }
    }
#endif
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
static int walk_null_nested;

#if defined(_WIN32)
typedef struct {
    ULONG reparse_tag;
    USHORT reparse_data_length;
    USHORT reserved;
    USHORT substitute_name_offset;
    USHORT substitute_name_length;
    USHORT print_name_offset;
    USHORT print_name_length;
    WCHAR path_buffer[4096];
} test_mount_point_reparse_buffer_t;

_Static_assert(offsetof(test_mount_point_reparse_buffer_t, path_buffer) == 16,
               "mount-point reparse buffer layout changed");

static int create_test_junction(const char *junction, const char *target) {
    WCHAR target_w[2048];
    static const WCHAR nt_prefix[] = L"\\??\\";
    test_mount_point_reparse_buffer_t data;
    size_t prefix_len = sizeof(nt_prefix) / sizeof(nt_prefix[0]) - 1;
    size_t target_len, substitute_len, total_chars, input_size;
    DWORD returned = 0;
    HANDLE h;
    int target_chars;

    target_chars = MultiByteToWideChar(CP_ACP, 0, target, -1, target_w,
                                       (int)(sizeof(target_w) /
                                             sizeof(target_w[0])));
    if (target_chars <= 1)
        return 0;
    target_len = (size_t)target_chars - 1;
    substitute_len = prefix_len + target_len;
    total_chars = substitute_len + 1 + target_len + 1;
    if (total_chars > sizeof(data.path_buffer) / sizeof(data.path_buffer[0]) ||
        substitute_len * sizeof(WCHAR) > USHRT_MAX ||
        target_len * sizeof(WCHAR) > USHRT_MAX)
        return 0;
    if (!CreateDirectoryA(junction, NULL))
        return 0;
    h = CreateFileA(junction, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT |
                        FILE_FLAG_BACKUP_SEMANTICS,
                    NULL);
    if (h == INVALID_HANDLE_VALUE) {
        RemoveDirectoryA(junction);
        return 0;
    }

    memset(&data, 0, sizeof(data));
    data.reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substitute_name_offset = 0;
    data.substitute_name_length =
        (USHORT)(substitute_len * sizeof(WCHAR));
    data.print_name_offset =
        (USHORT)((substitute_len + 1) * sizeof(WCHAR));
    data.print_name_length = (USHORT)(target_len * sizeof(WCHAR));
    memcpy(data.path_buffer, nt_prefix, prefix_len * sizeof(WCHAR));
    memcpy(data.path_buffer + prefix_len, target_w,
           target_len * sizeof(WCHAR));
    memcpy(data.path_buffer + substitute_len + 1, target_w,
           target_len * sizeof(WCHAR));
    input_size = offsetof(test_mount_point_reparse_buffer_t, path_buffer) +
                 total_chars * sizeof(WCHAR);
    data.reparse_data_length = (USHORT)(input_size - 8);
    int ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, &data,
                             (DWORD)input_size, NULL, 0, &returned, NULL) != 0;
    CloseHandle(h);
    if (!ok)
        RemoveDirectoryA(junction);
    return ok;
}

static char walk_win_junction_target[2048];
static int walk_win_replace_attempted;
static int walk_win_replace_blocked;
static int walk_win_replace_error;
static int walk_win_saw_secret;
static int walk_win_saw_junction;

static int walk_windows_reparse_cb(const char *path,
                                   const neverc_fs_dir_entry_t *entry,
                                   void *ud) {
    (void)ud;
    if (path && strstr(path, "outside_secret"))
        walk_win_saw_secret = 1;
    if (entry && strcmp(entry->name, "link") == 0) {
        walk_win_saw_junction = 1;
        if (entry->is_dir)
            walk_win_saw_secret = 1;
    }
    if (entry && strcmp(entry->name, "swap") == 0) {
        walk_win_replace_attempted = 1;
        SetLastError(ERROR_SUCCESS);
        if (!RemoveDirectoryA(path)) {
            walk_win_replace_blocked = 1;
            walk_win_replace_error = (int)GetLastError();
        } else if (create_test_junction(path, walk_win_junction_target)) {
            /* A path-only walker would now be exposed to a junction at the
             * exact point where it decides whether to recurse. */
            walk_win_replace_blocked = 0;
        }
    }
    return 0;
}
#endif

static int walk_nested_err_cb(const char *path,
                              const neverc_fs_dir_entry_t *entry, void *ud) {
    (void)ud;
    if (!entry) {
        walk_null_nested++;
        if (path && strstr(path, "locked"))
            return NEVERC_FS_SKIP_DIR;
        return 0;
    }
    return 0;
}
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
#if !defined(_WIN32)
    /* In the released ABI, 0775 group-write sets the MODE_LINK bit too.
     * WalkDir must use lstat/is_dir metadata and still visit the children. */
    chmod(walkdir, 0775);
    chmod(subdir, 0775);
    walk_count = 0;
    rc = neverc_fs_walk_dir(walkdir, walk_cb, NULL);
    check("walk_0775_ok", rc == 0);
    check("walk_0775_descends", walk_count >= 3);

    {
        char locked[2048];
        snprintf(locked, sizeof(locked), "%s/locked", walkdir);
        mkdir(locked, 0000);
        chmod(locked, 0000);
        if (geteuid() != 0) {
            walk_null_nested = 0;
            rc = neverc_fs_walk_dir(walkdir, walk_nested_err_cb, NULL);
            check("walk_nested_unreadable_ok", rc == 0);
            check("walk_nested_unreadable_notified", walk_null_nested >= 1);
        }
        chmod(locked, 0755);
        rmdir(locked);
    }

    /* Go WalkDir: root open failure + fn returns 0 is success. */
    if (geteuid() != 0) {
        chmod(walkdir, 0000);
        walk_count = 0;
        walk_saw_null_entry = 0;
        rc = neverc_fs_walk_dir(walkdir, walk_null_cb, NULL);
        check("walk_root_unreadable_fn0", rc == 0);
        check("walk_root_unreadable_notified", walk_saw_null_entry >= 1);
        chmod(walkdir, 0755);
    }
#endif

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

#if defined(_WIN32)
    {
        char outside[2048], secret[2048], linkpath[2048], swappath[2048];
        unsigned long pid = (unsigned long)GetCurrentProcessId();
        snprintf(outside, sizeof(outside), "%s\\neverc_walk_outside_%lu",
                 tmpdir, pid);
        snprintf(secret, sizeof(secret), "%s\\outside_secret", outside);
        snprintf(walkdir, sizeof(walkdir), "%s\\neverc_walk_reparse_%lu",
                 tmpdir, pid);
        snprintf(linkpath, sizeof(linkpath), "%s\\link", walkdir);
        snprintf(swappath, sizeof(swappath), "%s\\swap", walkdir);
        CreateDirectoryA(outside, NULL);
        CreateDirectoryA(walkdir, NULL);
        CreateDirectoryA(swappath, NULL);
        FILE *secret_file = fopen(secret, "w");
        if (secret_file) { fprintf(secret_file, "x"); fclose(secret_file); }

        int junction_ok = create_test_junction(linkpath, outside);
        check("walk_windows_junction_create", junction_ok == 1);
        snprintf(walk_win_junction_target, sizeof(walk_win_junction_target),
                 "%s", outside);
        walk_win_replace_attempted = 0;
        walk_win_replace_blocked = 0;
        walk_win_replace_error = ERROR_SUCCESS;
        walk_win_saw_secret = 0;
        walk_win_saw_junction = 0;
        rc = neverc_fs_walk_dir(walkdir, walk_windows_reparse_cb, NULL);
        check("walk_windows_reparse_ok", rc == 0);
        check("walk_windows_saw_junction", walk_win_saw_junction == 1);
        check("walk_windows_skips_junction", walk_win_saw_secret == 0);
        check("walk_windows_swap_attempted",
              walk_win_replace_attempted == 1);
        check("walk_windows_swap_blocked", walk_win_replace_blocked == 1);
        check("walk_windows_swap_block_reason",
              walk_win_replace_error == ERROR_SHARING_VIOLATION ||
              walk_win_replace_error == ERROR_ACCESS_DENIED);

        RemoveDirectoryA(linkpath);
        RemoveDirectoryA(swappath);
        remove(secret);
        RemoveDirectoryA(outside);
        RemoveDirectoryA(walkdir);
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
