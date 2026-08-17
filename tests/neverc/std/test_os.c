#include "neverc/std/os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(e) do{tests_run++;if(e){tests_passed++;}else{tests_failed++;\
    printf("  FAIL [%d]: %s\n",__LINE__,#e);\
    fprintf(stderr,"FAIL [%d]: %s\n",__LINE__,#e);}}while(0)
#define ASSERT_EQ(a,b) do{int _a=(a),_b=(b);tests_run++;if(_a==_b){tests_passed++;}else{tests_failed++;\
    printf("  FAIL [%d]: %s=%d, want %d\n",__LINE__,#a,_a,_b);\
    fprintf(stderr,"FAIL [%d]: %s=%d, want %d\n",__LINE__,#a,_a,_b);}}while(0)

static void make_test_path(char *path, size_t path_size, const char *name) {
    char tmpdir[1024];
    if (neverc_os_temp_dir(tmpdir, sizeof(tmpdir)) != 0) {
        snprintf(path, path_size, "%s", name);
        return;
    }
#if defined(_WIN32)
    snprintf(path, path_size, "%s\\%s", tmpdir, name);
#else
    snprintf(path, path_size, "%s/%s", tmpdir, name);
#endif
}

static void test_env(void) {
    printf("[env]\n");
    ASSERT_EQ(neverc_os_setenv("NEVERC_TEST_KEY", "hello"), 0);
    const char *v = neverc_os_getenv("NEVERC_TEST_KEY");
    ASSERT_TRUE(v != NULL);
    ASSERT_TRUE(strcmp(v, "hello") == 0);
    ASSERT_EQ(neverc_os_unsetenv("NEVERC_TEST_KEY"), 0);
    ASSERT_TRUE(neverc_os_getenv("NEVERC_TEST_KEY") == NULL);
#if defined(_WIN32)
    ASSERT_EQ(neverc_os_setenv("NEVERC_EMPTY_VAR", ""), 0);
    v = neverc_os_getenv("NEVERC_EMPTY_VAR");
    ASSERT_TRUE(v != NULL && v[0] == '\0');
    neverc_os_unsetenv("NEVERC_EMPTY_VAR");
#endif
    ASSERT_TRUE(neverc_os_getenv("") == NULL);
    ASSERT_TRUE(neverc_os_getenv("NOEQUALS=HERE") == NULL);
    ASSERT_EQ(neverc_os_setenv("", "x"), -1);
    ASSERT_EQ(neverc_os_setenv("FOO=BAR", "x"), -1);
    ASSERT_EQ(neverc_os_unsetenv(""), -1);
    ASSERT_EQ(neverc_os_unsetenv("FOO=BAR"), -1);
}

static void test_getwd(void) {
    printf("[getwd]\n");
    char buf[4096];
    ASSERT_EQ(neverc_os_getwd(buf, sizeof(buf)), 0);
    ASSERT_TRUE(strlen(buf) > 0);
}

static void test_hostname(void) {
    printf("[hostname]\n");
    char buf[256];
    ASSERT_EQ(neverc_os_hostname(buf, sizeof(buf)), 0);
    ASSERT_TRUE(strlen(buf) > 0);

    char tiny[1];
    int rc = neverc_os_hostname(tiny, sizeof(tiny));
    if (rc == 0) {
        ASSERT_EQ((int)tiny[0], 0);
    } else {
        ASSERT_EQ(rc, -1);
    }
    ASSERT_EQ(neverc_os_hostname(NULL, 16), -1);
    ASSERT_EQ(neverc_os_hostname(buf, 0), -1);
}

static void test_file_ops(void) {
    printf("[file ops]\n");
    char pathbuf[1024];
    make_test_path(pathbuf, sizeof(pathbuf), "neverc_test_os_file.txt");
    const char *path = pathbuf;
    const char *data = "Hello from NeverC os module!";

    neverc_os_file_t *f = neverc_os_create(path);
    ASSERT_TRUE(f != NULL);
    int n = neverc_os_write(f, data, strlen(data));
    ASSERT_EQ(n, (int)strlen(data));
    neverc_os_close(f);

    f = neverc_os_open(path, NEVERC_OS_O_RDONLY, 0);
    ASSERT_TRUE(f != NULL);
    char readbuf[256];
    n = neverc_os_read(f, readbuf, sizeof(readbuf));
    ASSERT_EQ(n, (int)strlen(data));
    ASSERT_TRUE(memcmp(readbuf, data, (size_t)n) == 0);
    ASSERT_EQ((int)neverc_os_seek(f, 0, NEVERC_OS_SEEK_SET), 0);
    n = neverc_os_read(f, readbuf, 5);
    ASSERT_EQ(n, 5);
    ASSERT_EQ((int)neverc_os_seek(f, 0, NEVERC_OS_SEEK_END), (int)strlen(data));
    ASSERT_EQ((int)neverc_os_seek(f, -1, NEVERC_OS_SEEK_CUR), (int)strlen(data) - 1);
    neverc_os_close(f);

    neverc_os_remove(path);
    ASSERT_TRUE(!neverc_os_exists(path));
}

static void test_open_flag_semantics(void) {
    printf("[open flag semantics]\n");
    char pathbuf[1024];
    make_test_path(pathbuf, sizeof(pathbuf), "neverc_test_os_flags.txt");
    const unsigned char data[] = "preserve me";

    ASSERT_EQ(neverc_os_write_file(pathbuf, data, sizeof(data) - 1, 0600), 0);
    neverc_os_file_t *f = neverc_os_open(pathbuf, NEVERC_OS_O_WRONLY, 0);
    ASSERT_TRUE(f != NULL);
    neverc_os_close(f);

    f = neverc_os_open(pathbuf, NEVERC_OS_O_WRONLY | NEVERC_OS_O_CREATE,
                       0600);
    ASSERT_TRUE(f != NULL);
    neverc_os_close(f);

    unsigned char *out = NULL;
    size_t out_len = 0;
    ASSERT_EQ(neverc_os_read_file(pathbuf, &out, &out_len), 0);
    ASSERT_EQ((int)out_len, (int)(sizeof(data) - 1));
    ASSERT_TRUE(out != NULL && memcmp(out, data, out_len) == 0);
    free(out);

    f = neverc_os_open(pathbuf, NEVERC_OS_O_WRONLY | NEVERC_OS_O_APPEND, 0);
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ(neverc_os_write(f, "!", 1), 1);
    neverc_os_close(f);
    neverc_os_fileinfo_t info;
    ASSERT_EQ(neverc_os_stat(pathbuf, &info), 0);
    ASSERT_EQ((int)info.size, (int)sizeof(data));

    f = neverc_os_open(pathbuf, NEVERC_OS_O_WRONLY | NEVERC_OS_O_CREATE |
                                    NEVERC_OS_O_EXCL,
                       0600);
    ASSERT_TRUE(f == NULL);

    f = neverc_os_open(pathbuf, NEVERC_OS_O_WRONLY | NEVERC_OS_O_TRUNC, 0);
    ASSERT_TRUE(f != NULL);
    neverc_os_close(f);
    ASSERT_EQ(neverc_os_stat(pathbuf, &info), 0);
    ASSERT_EQ((int)info.size, 0);
    neverc_os_remove(pathbuf);
}

static void test_read_write_file(void) {
    printf("[read/write file]\n");
    char pathbuf[1024];
    make_test_path(pathbuf, sizeof(pathbuf), "neverc_test_os_rw.txt");
    const char *path = pathbuf;
    const unsigned char *data = (const unsigned char*)"test data 123";

    neverc_os_remove(path);
    ASSERT_EQ(neverc_os_write_file(path, data, 13, 0600), 0);
    ASSERT_TRUE(neverc_os_exists(path));
#if !defined(_WIN32)
    neverc_os_fileinfo_t info;
    ASSERT_EQ(neverc_os_stat(path, &info), 0);
    ASSERT_TRUE((info.mode & 0077U) == 0);
#endif

    unsigned char *out = NULL;
    size_t out_len = 0;
    ASSERT_EQ(neverc_os_read_file(path, &out, &out_len), 0);
    ASSERT_EQ((int)out_len, 13);
    ASSERT_TRUE(memcmp(out, data, 13) == 0);
    free(out);

    neverc_os_remove(path);

    {
        char dirbuf[1024];
        make_test_path(dirbuf, sizeof(dirbuf), "neverc_test_os_read_dir");
        neverc_os_remove_all(dirbuf);
        ASSERT_EQ(neverc_os_mkdir(dirbuf, 0755), 0);
        unsigned char *dir_out = (unsigned char *)1;
        size_t dir_len = 99;
        ASSERT_EQ(neverc_os_read_file(dirbuf, &dir_out, &dir_len), -1);
        ASSERT_TRUE(dir_out == NULL && dir_len == 0);
        neverc_os_remove_all(dirbuf);
    }

    ASSERT_EQ(neverc_os_write_file(path, NULL, 0, 0600), 0);
    ASSERT_TRUE(neverc_os_exists(path));
    neverc_os_remove(path);
    ASSERT_EQ(neverc_os_write_file(path, NULL, 1, 0600), -1);
    ASSERT_TRUE(!neverc_os_exists(path));
}

static void test_stat(void) {
    printf("[stat]\n");
    char pathbuf[1024];
    make_test_path(pathbuf, sizeof(pathbuf), "neverc_test_os_stat.txt");
    const char *path = pathbuf;
    neverc_os_write_file(path, (const unsigned char*)"hello", 5, 0644);

    neverc_os_fileinfo_t info;
    ASSERT_EQ(neverc_os_stat(path, &info), 0);
    ASSERT_EQ((int)info.size, 5);
    ASSERT_TRUE(!info.is_dir);
    ASSERT_TRUE(strcmp(info.name, "neverc_test_os_stat.txt") == 0);

    neverc_os_remove(path);

    char dirbuf[1024], slashbuf[1026];
    make_test_path(dirbuf, sizeof(dirbuf), "neverc_test_os_stat_dir");
    neverc_os_remove_all(dirbuf);
    ASSERT_EQ(neverc_os_mkdir(dirbuf, 0755), 0);
#if defined(_WIN32)
    snprintf(slashbuf, sizeof(slashbuf), "%s\\", dirbuf);
#else
    snprintf(slashbuf, sizeof(slashbuf), "%s/", dirbuf);
#endif
    ASSERT_EQ(neverc_os_stat(slashbuf, &info), 0);
    ASSERT_TRUE(info.is_dir);
    ASSERT_TRUE((info.mode & NEVERC_OS_MODE_DIR) != 0);
    ASSERT_TRUE(strcmp(info.name, "neverc_test_os_stat_dir") == 0);
    ASSERT_EQ(neverc_os_lstat(slashbuf, &info), 0);
    ASSERT_TRUE(strcmp(info.name, "neverc_test_os_stat_dir") == 0);
    neverc_os_remove_all(dirbuf);

    ASSERT_EQ(neverc_os_stat("/nonexistent_neverc_os_stat_xyz", &info), -1);
    ASSERT_TRUE(neverc_os_is_not_exist(errno));

#if !defined(_WIN32)
    {
        char target[1024], linkp[1024];
        make_test_path(target, sizeof(target), "neverc_test_os_stat_tgt");
        make_test_path(linkp, sizeof(linkp), "neverc_test_os_stat_lnk");
        neverc_os_remove(target);
        neverc_os_remove(linkp);
        ASSERT_EQ(neverc_os_write_file(target, (const unsigned char *)"hello", 5, 0644),
                  0);
        ASSERT_EQ(neverc_os_symlink(target, linkp), 0);

        ASSERT_EQ(neverc_os_stat(linkp, &info), 0);
        ASSERT_EQ((int)info.size, 5);
        ASSERT_TRUE((info.mode & NEVERC_OS_MODE_SYMLINK) == 0);
        ASSERT_TRUE(!info.is_dir);

        ASSERT_EQ(neverc_os_lstat(linkp, &info), 0);
        ASSERT_TRUE((info.mode & NEVERC_OS_MODE_SYMLINK) != 0);
        ASSERT_TRUE(!info.is_dir);

        neverc_os_remove(target);
        ASSERT_EQ(neverc_os_stat(linkp, &info), -1);
        ASSERT_TRUE(neverc_os_is_not_exist(errno));
        ASSERT_EQ(neverc_os_lstat(linkp, &info), 0);
        ASSERT_TRUE((info.mode & NEVERC_OS_MODE_SYMLINK) != 0);
        ASSERT_TRUE(neverc_os_exists(linkp));
        neverc_os_remove(linkp);

        char fifopath[1024];
        make_test_path(fifopath, sizeof(fifopath), "neverc_test_os_fifo");
        neverc_os_remove(fifopath);
        ASSERT_EQ(mkfifo(fifopath, 0644), 0);
        ASSERT_EQ(neverc_os_lstat(fifopath, &info), 0);
        ASSERT_TRUE((info.mode & NEVERC_OS_MODE_NAMEDPIPE) != 0);
        neverc_os_remove(fifopath);

        char setuidpath[1024];
        make_test_path(setuidpath, sizeof(setuidpath), "neverc_test_os_setuid");
        ASSERT_EQ(neverc_os_write_file(setuidpath, (const unsigned char *)"u", 1, 0644),
                  0);
        ASSERT_EQ(neverc_os_chmod(setuidpath, 04755), 0);
        ASSERT_EQ(neverc_os_stat(setuidpath, &info), 0);
        ASSERT_TRUE((info.mode & NEVERC_OS_MODE_SETUID) != 0);
        ASSERT_TRUE((info.mode & NEVERC_OS_MODE_PERM) == 0755);
        neverc_os_remove(setuidpath);
    }
#endif
}

static void test_mkdir(void) {
    printf("[mkdir]\n");
    char dirbuf[1024], nestedbuf[1024];
    make_test_path(dirbuf, sizeof(dirbuf), "neverc_test_os_dir");
    const char *dir = dirbuf;
    neverc_os_remove_all(dir);

    ASSERT_EQ(neverc_os_mkdir(dir, 0755), 0);
    ASSERT_TRUE(neverc_os_is_dir(dir));

#if defined(_WIN32)
    snprintf(nestedbuf, sizeof(nestedbuf), "%s\\a\\b\\c", dir);
#else
    snprintf(nestedbuf, sizeof(nestedbuf), "%s/a/b/c", dir);
#endif
    const char *nested = nestedbuf;
    ASSERT_EQ(neverc_os_mkdir_all(nested, 0755), 0);
    ASSERT_TRUE(neverc_os_is_dir(nested));

    neverc_os_remove_all(dir);
    ASSERT_TRUE(!neverc_os_exists(dir));

#if !defined(_WIN32)
    {
        char parent[1024], weird[1100], foo[1100];
        make_test_path(parent, sizeof(parent), "neverc_test_os_bs");
        neverc_os_remove_all(parent);
        ASSERT_EQ(neverc_os_mkdir(parent, 0755), 0);
        snprintf(weird, sizeof(weird), "%s/foo\\bar", parent);
        snprintf(foo, sizeof(foo), "%s/foo", parent);
        ASSERT_EQ(neverc_os_mkdir_all(weird, 0755), 0);
        ASSERT_TRUE(neverc_os_is_dir(weird));
        ASSERT_TRUE(!neverc_os_exists(foo));
        neverc_os_remove_all(parent);
    }
#endif
    ASSERT_EQ(neverc_os_mkdir_all("", 0755), -1);
}

#if !defined(_WIN32)
static void test_remove_all_does_not_follow_symlinks(void) {
    printf("[remove all symlinks]\n");
    char root[1024], outside[1024], link_path[1200];
    char dangling_path[1200], missing_target[1024], sentinel[1200];
    char direct_link[1024], direct_link_slash[1026];
    make_test_path(root, sizeof(root), "neverc_test_remove_tree");
    make_test_path(outside, sizeof(outside), "neverc_test_remove_outside");
    make_test_path(missing_target, sizeof(missing_target),
                   "neverc_test_remove_missing");
    make_test_path(direct_link, sizeof(direct_link),
                   "neverc_test_remove_direct_link");
    snprintf(link_path, sizeof(link_path), "%s/link", root);
    snprintf(dangling_path, sizeof(dangling_path), "%s/dangling", root);
    snprintf(sentinel, sizeof(sentinel), "%s/sentinel", outside);

    neverc_os_remove_all(root);
    neverc_os_remove_all(outside);
    neverc_os_remove(direct_link);
    neverc_os_remove(missing_target);
    ASSERT_EQ(neverc_os_mkdir(root, 0700), 0);
    ASSERT_EQ(neverc_os_mkdir(outside, 0700), 0);
    ASSERT_EQ(neverc_os_write_file(
                  sentinel, (const unsigned char *)"keep", 4, 0600), 0);
    ASSERT_EQ(neverc_os_symlink(outside, link_path), 0);
    ASSERT_EQ(neverc_os_symlink(missing_target, dangling_path), 0);
    ASSERT_EQ(neverc_os_symlink(outside, direct_link), 0);

    snprintf(direct_link_slash, sizeof(direct_link_slash),
             "%s/", direct_link);
    ASSERT_EQ(neverc_os_remove_all(direct_link_slash), 0);
    ASSERT_TRUE(neverc_os_exists(sentinel));

    ASSERT_EQ(neverc_os_remove_all(root), 0);
    ASSERT_TRUE(!neverc_os_exists(root));
    ASSERT_TRUE(neverc_os_exists(sentinel));

    ASSERT_EQ(neverc_os_remove_all(outside), 0);
}
#endif

static void test_rename(void) {
    printf("[rename]\n");
    char oldbuf[1024], newbuf[1024];
    make_test_path(oldbuf, sizeof(oldbuf), "neverc_test_rename_old.txt");
    make_test_path(newbuf, sizeof(newbuf), "neverc_test_rename_new.txt");
    const char *old = oldbuf;
    const char *new_path = newbuf;
    neverc_os_write_file(old, (const unsigned char*)"x", 1, 0644);
    ASSERT_EQ(neverc_os_rename(old, new_path), 0);
    ASSERT_TRUE(!neverc_os_exists(old));
    ASSERT_TRUE(neverc_os_exists(new_path));
    neverc_os_remove(new_path);
}

static void test_process(void) {
    printf("[process]\n");
    ASSERT_TRUE(neverc_os_getpid() > 0);
}

static void test_temp(void) {
    printf("[temp]\n");
    char tmpdir[1024];
    ASSERT_EQ(neverc_os_temp_dir(tmpdir, sizeof(tmpdir)), 0);
    ASSERT_TRUE(strlen(tmpdir) > 0);
    char tiny[1];
    ASSERT_EQ(neverc_os_temp_dir(tiny, sizeof(tiny)), -1);

    neverc_os_file_t *f = neverc_os_create_temp(NULL, "neverc_test_");
    ASSERT_TRUE(f != NULL);
    neverc_os_write(f, "tmp", 3);
    neverc_os_close(f);

    char dir_path[4096];
    ASSERT_EQ(neverc_os_mkdir_temp(
                  NULL, "neverc_test_dir_", dir_path, sizeof(dir_path)), 0);
    ASSERT_TRUE(neverc_os_is_dir(dir_path));
    ASSERT_EQ(neverc_os_remove_all(dir_path), 0);
    ASSERT_EQ(neverc_os_mkdir_temp(NULL, "x", NULL, 0), -1);

    ASSERT_TRUE(neverc_os_create_temp(tmpdir, "../neverc_trav_") == NULL);
    ASSERT_TRUE(neverc_os_create_temp(tmpdir, "foo/bar_") == NULL);
    ASSERT_EQ(neverc_os_mkdir_temp(tmpdir, "../neverc_trav_", dir_path,
                                  sizeof(dir_path)), -1);
    ASSERT_EQ(neverc_os_mkdir_temp(tmpdir, "a\\b_", dir_path, sizeof(dir_path)),
              -1);

    char long_pattern[4096];
    memset(long_pattern, 'x', sizeof(long_pattern) - 1);
    long_pattern[sizeof(long_pattern) - 1] = '\0';
    ASSERT_EQ(neverc_os_mkdir_temp(
                  NULL, long_pattern, dir_path, sizeof(dir_path)), -1);
    ASSERT_TRUE(neverc_os_create_temp(NULL, long_pattern) == NULL);
}

static void test_std_files(void) {
    printf("[stdin/stdout/stderr]\n");
    ASSERT_TRUE(neverc_os_stdin() != NULL);
    ASSERT_TRUE(neverc_os_stdout() != NULL);
    ASSERT_TRUE(neverc_os_stderr() != NULL);
}

static void test_lookup_env(void) {
    printf("[lookup_env]\n");
    neverc_os_setenv("NEVERC_LOOKUP_TEST", "found");
    const char *val;
    int found = neverc_os_lookup_env("NEVERC_LOOKUP_TEST", &val);
    ASSERT_TRUE(found);
    ASSERT_TRUE(val != NULL && strcmp(val, "found") == 0);

    found = neverc_os_lookup_env("NEVERC_DOES_NOT_EXIST_12345", &val);
    ASSERT_TRUE(!found);
    neverc_os_unsetenv("NEVERC_LOOKUP_TEST");
}

static void test_environ(void) {
    printf("[environ]\n");
    int count;
    char **env = neverc_os_environ(&count);
    ASSERT_TRUE(count > 0);
    ASSERT_TRUE(env != NULL);
    for (int i = 0; i < count; i++) free(env[i]);
    free(env);
    ASSERT_TRUE(neverc_os_environ(NULL) == NULL);
}

static void test_expand_env(void) {
    printf("[expand_env]\n");
    neverc_os_setenv("NEVERC_EXPAND_VAR", "hello");
    char *result = neverc_os_expand_env("say $NEVERC_EXPAND_VAR world");
    ASSERT_TRUE(result != NULL && strcmp(result, "say hello world") == 0);
    free(result);

    result = neverc_os_expand_env("${NEVERC_EXPAND_VAR}!");
    ASSERT_TRUE(result != NULL && strcmp(result, "hello!") == 0);
    free(result);

    result = neverc_os_expand_env("cost $- and ${UNCLOSED");
    ASSERT_TRUE(result != NULL && strcmp(result, "cost $- and ${UNCLOSED") == 0);
    free(result);

    result = neverc_os_expand_env("$$");
    ASSERT_TRUE(result != NULL && strcmp(result, "$$") == 0);
    free(result);
    neverc_os_unsetenv("NEVERC_EXPAND_VAR");
}

static void test_read_dir(void) {
    printf("[read_dir]\n");
    char tmpdir[1024];
    neverc_os_temp_dir(tmpdir, sizeof(tmpdir));

    neverc_os_dir_entry_t *entries;
    size_t count;
    int err = neverc_os_read_dir(tmpdir, &entries, &count);
    ASSERT_TRUE(err == 0);
    ASSERT_TRUE(count >= 0);
    free(entries);

    ASSERT_EQ(neverc_os_read_dir(NULL, &entries, &count), -1);
    ASSERT_EQ(neverc_os_read_dir("", &entries, &count), -1);
    ASSERT_EQ(neverc_os_readlink("unused", (char *)&count, 0), -1);

    char filebuf[1024];
    make_test_path(filebuf, sizeof(filebuf), "neverc_test_os_readdir_file");
    ASSERT_EQ(neverc_os_write_file(filebuf, (const unsigned char *)"x", 1, 0600),
              0);
    entries = (neverc_os_dir_entry_t *)1;
    count = 99;
    ASSERT_EQ(neverc_os_read_dir(filebuf, &entries, &count), -1);
    ASSERT_TRUE(entries == NULL);
    ASSERT_EQ((int)count, 0);
    neverc_os_remove(filebuf);

#if !defined(_WIN32)
    {
        char parent[1024], realdir[1024], linkp[1024];
        make_test_path(parent, sizeof(parent), "neverc_os_readdir_parent");
        neverc_os_remove_all(parent);
        ASSERT_EQ(neverc_os_mkdir(parent, 0755), 0);
        snprintf(realdir, sizeof(realdir), "%s/realdir", parent);
        snprintf(linkp, sizeof(linkp), "%s/dirlink", parent);
        ASSERT_EQ(neverc_os_mkdir(realdir, 0755), 0);
        ASSERT_EQ(neverc_os_symlink(realdir, linkp), 0);
        entries = NULL;
        count = 0;
        ASSERT_EQ(neverc_os_read_dir(parent, &entries, &count), 0);
        int saw_link = 0;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i].name, "dirlink") == 0) {
                saw_link = 1;
                ASSERT_EQ(entries[i].is_dir, 0);
            }
        }
        ASSERT_TRUE(saw_link);
        free(entries);
        neverc_os_remove_all(parent);
    }
#endif

#if !defined(_WIN32)
    char target[80], linkpath[1024], small[8], big[128];
    memset(target, 't', 70);
    target[70] = '\0';
    make_test_path(linkpath, sizeof(linkpath), "neverc_readlink_long");
    neverc_os_remove(linkpath);
    ASSERT_EQ(neverc_os_symlink(target, linkpath), 0);
    ASSERT_EQ(neverc_os_readlink(linkpath, small, sizeof(small)), -1);
    ASSERT_EQ(neverc_os_readlink(linkpath, big, sizeof(big)), 0);
    ASSERT_TRUE(strcmp(big, target) == 0);
    neverc_os_remove(linkpath);
#endif
}

static void test_user_dirs(void) {
    printf("[user_dirs]\n");
    char buf[1024];
    int rc = neverc_os_user_home_dir(buf, sizeof(buf));
    ASSERT_TRUE(rc == 0);
    ASSERT_TRUE(strlen(buf) > 0);

    rc = neverc_os_user_cache_dir(buf, sizeof(buf));
    ASSERT_TRUE(rc == 0);
    ASSERT_TRUE(strlen(buf) > 0);

    rc = neverc_os_user_config_dir(buf, sizeof(buf));
    ASSERT_TRUE(rc == 0);
    ASSERT_TRUE(strlen(buf) > 0);

    ASSERT_EQ(neverc_os_user_home_dir(NULL, 16), -1);
    ASSERT_EQ(neverc_os_user_home_dir(buf, 0), -1);
    char one[1];
    ASSERT_EQ(neverc_os_user_home_dir(one, 1), -1);
    ASSERT_EQ(neverc_os_user_cache_dir(NULL, 16), -1);
    ASSERT_EQ(neverc_os_user_config_dir(NULL, 16), -1);

#if defined(_WIN32)
    const char *home_key = "USERPROFILE";
#else
    const char *home_key = "HOME";
#endif
    const char *cur_home = neverc_os_getenv(home_key);
    char *saved_home = cur_home ? strdup(cur_home) : NULL;
    ASSERT_EQ(neverc_os_setenv(home_key, ""), 0);
    ASSERT_EQ(neverc_os_user_home_dir(buf, sizeof(buf)), -1);
    if (saved_home) {
        neverc_os_setenv(home_key, saved_home);
        free(saved_home);
    } else {
        neverc_os_unsetenv(home_key);
    }
}

static void test_executable(void) {
    printf("[executable]\n");
    char buf[4096];
    int rc = neverc_os_executable(buf, sizeof(buf));
    ASSERT_TRUE(rc == 0);
    ASSERT_TRUE(strlen(buf) > 0);
}

static void test_chmod_truncate(void) {
    printf("[chmod/truncate]\n");
    char path[1024], name[64];
    snprintf(name, sizeof(name), "neverc_chmod_test_%d", neverc_os_getpid());
    make_test_path(path, sizeof(path), name);
    neverc_os_write_file(path, (const unsigned char *)"hello world", 11, 0644);

    int rc = neverc_os_chmod(path, 0755);
    ASSERT_TRUE(rc == 0);

    rc = neverc_os_truncate(path, 5);
    ASSERT_TRUE(rc == 0);
    ASSERT_EQ(neverc_os_truncate(path, -1), -1);

    unsigned char *data; size_t len;
    neverc_os_read_file(path, &data, &len);
    ASSERT_TRUE(len == 5);
    ASSERT_TRUE(memcmp(data, "hello", 5) == 0);
    free(data);
    neverc_os_remove(path);
}

static void test_pipe(void) {
    printf("[pipe]\n");
    neverc_os_file_t *reader = NULL, *writer = NULL;
    ASSERT_EQ(neverc_os_pipe(&reader, &writer), 0);
    ASSERT_TRUE(reader != NULL);
    ASSERT_TRUE(writer != NULL);
    if (!reader || !writer) {
        neverc_os_close(reader);
        neverc_os_close(writer);
        return;
    }

    ASSERT_EQ(neverc_os_write(writer, "ping", 4), 4);
    char buf[4] = {0};
    ASSERT_EQ(neverc_os_read(reader, buf, sizeof(buf)), 4);
    ASSERT_TRUE(memcmp(buf, "ping", sizeof(buf)) == 0);
    neverc_os_close(reader);
    neverc_os_close(writer);

#if !defined(_WIN32)
    printf("[pipe_cloexec]\n");
    int before[256];
    for (int fd = 0; fd < 256; fd++)
        before[fd] = fcntl(fd, F_GETFD) >= 0;

    reader = NULL;
    writer = NULL;
    ASSERT_EQ(neverc_os_pipe(&reader, &writer), 0);
    ASSERT_TRUE(reader != NULL && writer != NULL);
    if (reader && writer) {
        int seen = 0, cloexec = 0;
        for (int fd = 0; fd < 256; fd++) {
            if (before[fd]) continue;
            int flags = fcntl(fd, F_GETFD);
            if (flags < 0) continue;
            seen++;
            if (flags & FD_CLOEXEC) cloexec++;
        }
        ASSERT_TRUE(seen >= 2);
        ASSERT_EQ(cloexec, seen);
        neverc_os_close(reader);
        neverc_os_close(writer);
    }
#endif
}

static void test_error_classification_and_ownership(void) {
    printf("[error classification/ownership]\n");
    ASSERT_TRUE(neverc_os_is_permission(13));
    ASSERT_TRUE(!neverc_os_is_permission(12));
    ASSERT_TRUE(neverc_os_is_permission(EACCES));
    ASSERT_TRUE(neverc_os_is_permission(EPERM));
    ASSERT_TRUE(neverc_os_is_exist(EEXIST));
    ASSERT_TRUE(neverc_os_is_not_exist(ENOENT));
    ASSERT_EQ(neverc_os_remove_all(""), -1);
    ASSERT_EQ(neverc_os_chown(NULL, 0, 0), -1);
    ASSERT_EQ(neverc_os_lchown(NULL, 0, 0), -1);

    char path[1024];
    make_test_path(path, sizeof(path), "neverc_missing_chown_target");
    neverc_os_remove(path);
#if defined(_WIN32)
    ASSERT_EQ(neverc_os_chown(path, 0, 0), -1);
    ASSERT_EQ(neverc_os_lchown(path, 0, 0), -1);
#else
    ASSERT_EQ(neverc_os_chown(path, neverc_os_geteuid(),
                              neverc_os_getegid()), -1);
    ASSERT_EQ(neverc_os_lchown(path, neverc_os_geteuid(),
                               neverc_os_getegid()), -1);
#endif
}

int main(void) {
    printf("=== NeverC os Module Tests ===\n");
    test_env();
    test_getwd();
    test_hostname();
    test_file_ops();
    test_open_flag_semantics();
    test_read_write_file();
    test_stat();
    test_mkdir();
#if !defined(_WIN32)
    test_remove_all_does_not_follow_symlinks();
#endif
    test_rename();
    test_process();
    test_temp();
    test_std_files();
    test_lookup_env();
    test_environ();
    test_expand_env();
    test_read_dir();
    test_user_dirs();
    test_executable();
    test_chmod_truncate();
    test_pipe();
    test_error_classification_and_ownership();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
