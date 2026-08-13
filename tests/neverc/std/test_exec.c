/*
 * NeverC os/exec tests.
 * Tests process execution, output capture, LookPath.
 */
#include "neverc/std/os/exec.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

enum { BIDIRECTIONAL_BYTES = 256 * 1024 };

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

static void *local_memmem(const void *haystack, size_t haystacklen,
                          const void *needle, size_t needlelen) {
    if (needlelen == 0) return (void *)haystack;
    if (haystacklen < needlelen) return NULL;
    const unsigned char *h = (const unsigned char *)haystack;
    for (size_t i = 0; i <= haystacklen - needlelen; i++) {
        if (memcmp(h + i, needle, needlelen) == 0)
            return (void *)(h + i);
    }
    return NULL;
}
#define memmem local_memmem

static void test_command_echo(void) {
    printf("[echo]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "echo hello world"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    const char *args[] = {"hello", "world"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("echo", args, 2);
#endif
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(out.len > 0);

    ASSERT_TRUE(memmem(out.data, out.len, "hello world", 11) != NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

static void test_command_false(void) {
    printf("[false_exit_code]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "exit /B 1"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    neverc_exec_cmd_t *cmd = neverc_exec_command("false", NULL, 0);
#endif
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_TRUE(st.exit_code != 0);

    neverc_exec_cmd_free(cmd);
}

static void test_command_true(void) {
    printf("[true_exit_code]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "exit /B 0"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    neverc_exec_cmd_t *cmd = neverc_exec_command("true", NULL, 0);
#endif
    ASSERT_TRUE(cmd != NULL);

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);

    neverc_exec_cmd_free(cmd);
}

#if !defined(_WIN32)
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
#endif

static int run_bidirectional_child(void) {
    unsigned char block[4096];
    memset(block, 'O', sizeof(block));
    size_t remaining = BIDIRECTIONAL_BYTES;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(block) ? remaining : sizeof(block);
        if (fwrite(block, 1, chunk, stdout) != chunk)
            return 1;
        remaining -= chunk;
    }
    if (fflush(stdout) != 0)
        return 1;

    size_t input_length = 0;
    for (;;) {
        size_t count = fread(block, 1, sizeof(block), stdin);
        input_length += count;
        if (count < sizeof(block)) {
            if (ferror(stdin))
                return 1;
            break;
        }
    }
    return printf("\nstdin=%zu\n", input_length) < 0 ? 1 : 0;
}

static void test_bidirectional_pipes(const char *executable) {
    printf("[bidirectional_pipes]\n");
    const char *args[] = {"--bidirectional-child"};
    neverc_exec_cmd_t *cmd =
        neverc_exec_command(executable, args, 1);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) return;

    unsigned char *input = (unsigned char *)malloc(BIDIRECTIONAL_BYTES);
    ASSERT_TRUE(input != NULL);
    if (!input) {
        neverc_exec_cmd_free(cmd);
        return;
    }
    memset(input, 'I', BIDIRECTIONAL_BYTES);
    neverc_exec_cmd_set_stdin(cmd, input, BIDIRECTIONAL_BYTES);

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(out.len >= BIDIRECTIONAL_BYTES);
    ASSERT_TRUE(memmem(out.data, out.len,
                       "stdin=262144", 12) != NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
    free(input);
}

static void test_look_path(void) {
    printf("[look_path]\n");
    char buf[4096];

#if defined(_WIN32)
    const char *p = neverc_exec_look_path("cmd.exe", buf, sizeof(buf));
#else
    const char *p = neverc_exec_look_path("ls", buf, sizeof(buf));
#endif
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(strlen(p) > 0);

    p = neverc_exec_look_path("this_command_does_not_exist_xyz", buf, sizeof(buf));
    ASSERT_TRUE(p == NULL);
}

static void test_combined_output(void) {
    printf("[combined_output]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "echo stdout && echo stderr 1>&2"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    const char *args[] = {"-c", "echo stdout; echo stderr >&2"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
#endif
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

#if !defined(_WIN32)
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
#endif

int main(int argc, char **argv) {
    if (argc == 2 &&
        strcmp(argv[1], "--bidirectional-child") == 0)
        return run_bidirectional_child();

    printf("=== NeverC os/exec Tests ===\n");
    ASSERT_TRUE(neverc_exec_command(NULL, NULL, 0) == NULL);
    ASSERT_TRUE(neverc_exec_command("echo", NULL, -1) == NULL);
    ASSERT_INT_EQ(neverc_exec_cmd_run(NULL, NULL), -1);
    test_command_echo();
    test_command_false();
    test_command_true();
#if !defined(_WIN32)
    test_command_stdin();
#endif
    test_bidirectional_pipes(argv[0]);
    test_look_path();
    test_combined_output();
#if !defined(_WIN32)
    test_set_dir();
#endif
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
