#include "neverc/std/os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    ASSERT_EQ(neverc_os_write_file(path, NULL, 0, 0600), 0);
    ASSERT_TRUE(neverc_os_exists(path));
    neverc_os_remove(path);
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
}

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
    ASSERT_EQ(neverc_os_readlink("unused", (char *)&count, 0), -1);
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
}

static void test_error_classification_and_ownership(void) {
    printf("[error classification/ownership]\n");
    ASSERT_TRUE(neverc_os_is_permission(13));
    ASSERT_TRUE(!neverc_os_is_permission(12));

    char path[1024];
    make_test_path(path, sizeof(path), "neverc_missing_chown_target");
    neverc_os_remove(path);
#if defined(_WIN32)
    ASSERT_EQ(neverc_os_chown(path, 0, 0), 0);
    ASSERT_EQ(neverc_os_lchown(path, 0, 0), 0);
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
    return tests_failed > 0 ? 1 : 0;
}
