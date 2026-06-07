#include "neverc/std/os.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(e) do{tests_run++;if(e){tests_passed++;}else{tests_failed++;printf("  FAIL [%d]: %s\n",__LINE__,#e);}}while(0)
#define ASSERT_EQ(a,b) do{int _a=(a),_b=(b);tests_run++;if(_a==_b){tests_passed++;}else{tests_failed++;printf("  FAIL [%d]: %s=%d, want %d\n",__LINE__,#a,_a,_b);}}while(0)

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
    const char *path = "/tmp/neverc_test_os_file.txt";
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

static void test_read_write_file(void) {
    printf("[read/write file]\n");
    const char *path = "/tmp/neverc_test_os_rw.txt";
    const unsigned char *data = (const unsigned char*)"test data 123";

    ASSERT_EQ(neverc_os_write_file(path, data, 13, 0644), 0);
    ASSERT_TRUE(neverc_os_exists(path));

    unsigned char *out = NULL;
    size_t out_len = 0;
    ASSERT_EQ(neverc_os_read_file(path, &out, &out_len), 0);
    ASSERT_EQ((int)out_len, 13);
    ASSERT_TRUE(memcmp(out, data, 13) == 0);
    free(out);

    neverc_os_remove(path);
}

static void test_stat(void) {
    printf("[stat]\n");
    const char *path = "/tmp/neverc_test_os_stat.txt";
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
    const char *dir = "/tmp/neverc_test_os_dir";
    neverc_os_remove_all(dir);

    ASSERT_EQ(neverc_os_mkdir(dir, 0755), 0);
    ASSERT_TRUE(neverc_os_is_dir(dir));

    const char *nested = "/tmp/neverc_test_os_dir/a/b/c";
    ASSERT_EQ(neverc_os_mkdir_all(nested, 0755), 0);
    ASSERT_TRUE(neverc_os_is_dir(nested));

    neverc_os_remove_all(dir);
    ASSERT_TRUE(!neverc_os_exists(dir));
}

static void test_rename(void) {
    printf("[rename]\n");
    const char *old = "/tmp/neverc_test_rename_old.txt";
    const char *new_path = "/tmp/neverc_test_rename_new.txt";
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

    neverc_os_file_t *f = neverc_os_create_temp(NULL, "neverc_test_");
    ASSERT_TRUE(f != NULL);
    neverc_os_write(f, "tmp", 3);
    neverc_os_close(f);
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
    char path[1024];
    snprintf(path, sizeof(path), "/tmp/neverc_chmod_test_%d", neverc_os_getpid());
    neverc_os_write_file(path, (const unsigned char *)"hello world", 11, 0644);

    int rc = neverc_os_chmod(path, 0755);
    ASSERT_TRUE(rc == 0);

    rc = neverc_os_truncate(path, 5);
    ASSERT_TRUE(rc == 0);

    unsigned char *data; size_t len;
    neverc_os_read_file(path, &data, &len);
    ASSERT_TRUE(len == 5);
    ASSERT_TRUE(memcmp(data, "hello", 5) == 0);
    free(data);
    neverc_os_remove(path);
}

int main(void) {
    printf("=== NeverC os Module Tests ===\n");
    test_env();
    test_getwd();
    test_hostname();
    test_file_ops();
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
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
