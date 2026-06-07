/*
 * NeverC os/exec tests.
 * Tests process execution, output capture, LookPath.
 */
#include "neverc/os/exec.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

static void test_command_echo(void) {
    printf("[echo]\n");
    const char *args[] = {"hello", "world"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("echo", args, 2);
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(out.len > 0);

    ASSERT_TRUE(out.len >= 11);
    ASSERT_TRUE(memcmp(out.data, "hello world", 11) == 0);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

static void test_command_false(void) {
    printf("[false_exit_code]\n");
    neverc_exec_cmd_t *cmd = neverc_exec_command("false", NULL, 0);
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_TRUE(st.exit_code != 0);

    neverc_exec_cmd_free(cmd);
}

static void test_command_true(void) {
    printf("[true_exit_code]\n");
    neverc_exec_cmd_t *cmd = neverc_exec_command("true", NULL, 0);
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);

    neverc_exec_cmd_free(cmd);
}

static void test_command_stdin(void) {
    printf("[stdin_pipe]\n");
    const char *args[] = {"-c", "cat"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_cmd_set_stdin(cmd, "input data", 10);

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_INT_EQ((int)out.len, 10);
    ASSERT_TRUE(memcmp(out.data, "input data", 10) == 0);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

static void test_look_path(void) {
    printf("[look_path]\n");
    char buf[4096];

    const char *p = neverc_exec_look_path("ls", buf, sizeof(buf));
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(strlen(p) > 0);

    p = neverc_exec_look_path("this_command_does_not_exist_xyz", buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
}

static void test_combined_output(void) {
    printf("[combined_output]\n");
    const char *args[] = {"-c", "echo stdout; echo stderr >&2"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_combined_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(out.len > 0);

    ASSERT_TRUE(memmem(out.data, out.len, "stdout", 6) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "stderr", 6) != NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

static void test_set_dir(void) {
    printf("[set_dir]\n");
    const char *args[] = {"-c", "pwd"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_cmd_set_dir(cmd, "/tmp");

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);

    char *outstr = (char *)malloc(out.len + 1);
    memcpy(outstr, out.data, out.len);
    outstr[out.len] = '\0';
    while (out.len > 0 && (outstr[out.len - 1] == '\n' || outstr[out.len - 1] == '\r'))
        outstr[--out.len] = '\0';

    tests_run++;
    if (strcmp(outstr, "/tmp") == 0 || strcmp(outstr, "/private/tmp") == 0)
        tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: pwd = '%s', expected '/tmp' or '/private/tmp'\n", outstr);
    }

    free(outstr);
    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

int main(void) {
    printf("=== NeverC os/exec Tests ===\n");
    test_command_echo();
    test_command_false();
    test_command_true();
    test_command_stdin();
    test_look_path();
    test_combined_output();
    test_set_dir();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
