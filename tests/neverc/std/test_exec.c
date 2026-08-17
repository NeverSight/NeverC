/*
 * NeverC os/exec tests.
 * Tests process execution, output capture, LookPath.
 */
#include "neverc/std/os/exec.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#else
#include <windows.h>
#endif

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
    ASSERT_TRUE(neverc_exec_look_path("", buf, sizeof(buf)) == NULL);

#if !defined(_WIN32)
    p = neverc_exec_look_path("bin", buf, sizeof(buf));
    if (p) {
        struct stat st;
        ASSERT_TRUE(stat(p, &st) == 0 && S_ISREG(st.st_mode));
    } else {
        tests_run++;
        tests_passed++;
    }
    ASSERT_TRUE(neverc_exec_look_path("/usr/bin", buf, sizeof(buf)) == NULL);

    const char *old_path = getenv("PATH");
    char *saved = old_path ? strdup(old_path) : NULL;
    char cwd[1024], script[1200], name[64];
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    snprintf(name, sizeof(name), "neverc_lookpath_%d", (int)getpid());
    snprintf(script, sizeof(script), "%s/%s", cwd, name);
    FILE *sf = fopen(script, "w");
    ASSERT_TRUE(sf != NULL);
    if (sf) {
        fputs("#!/bin/sh\nexit 0\n", sf);
        fclose(sf);
    }
    chmod(script, 0755);
    setenv("PATH", ":", 1);
    p = neverc_exec_look_path(name, buf, sizeof(buf));
    ASSERT_TRUE(p != NULL && strcmp(p, "./") != 0);
    ASSERT_TRUE(p != NULL && p[0] == '.' && p[1] == '/');
    unlink(script);
    if (saved) {
        setenv("PATH", saved, 1);
        free(saved);
    } else {
        unsetenv("PATH");
    }
#endif
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
static int parse_int_output(const neverc_exec_output_t *out) {
    char tmp[32];
    size_t n = out->len < sizeof(tmp) - 1 ? out->len : sizeof(tmp) - 1;
    memcpy(tmp, out->data, n);
    tmp[n] = '\0';
    return atoi(tmp);
}

static int run_print_blocked_usr1(void) {
    sigset_t cur, empty;
    sigemptyset(&empty);
    if (sigprocmask(SIG_BLOCK, &empty, &cur) != 0) return 1;
    return printf("%d\n", sigismember(&cur, SIGUSR1)) < 0 ? 1 : 0;
}

static void test_unread_stdin_is_not_failure(void) {
    printf("[unread_stdin]\n");
    const char *args[] = {"-c", "exit 0"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) return;

    size_t n = 256 * 1024;
    char *data = (char *)malloc(n);
    ASSERT_TRUE(data != NULL);
    if (!data) {
        neverc_exec_cmd_free(cmd);
        return;
    }
    memset(data, 'x', n);
    neverc_exec_cmd_set_stdin(cmd, data, n);

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_INT_EQ(st.signaled, 0);

    neverc_exec_cmd_free(cmd);
    free(data);
}

static void test_signaled_status(void) {
    printf("[signaled_status]\n");
    const char *args[] = {"-c", "kill -s USR1 $$"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("/bin/sh", args, 2);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) return;

    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.signaled, 1);
    ASSERT_INT_EQ(st.signal_num, SIGUSR1);
    ASSERT_INT_EQ(st.exit_code, -1);
    neverc_exec_cmd_free(cmd);
}

static void test_exec_resets_signal_mask(const char *executable) {
    printf("[exec_signal_mask]\n");
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    ASSERT_INT_EQ(sigprocmask(SIG_BLOCK, &block, &old), 0);

    const char *args[] = {"--print-blocked-usr1"};
    neverc_exec_cmd_t *cmd = neverc_exec_command(executable, args, 1);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) {
        sigprocmask(SIG_SETMASK, &old, NULL);
        return;
    }

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_INT_EQ(parse_int_output(&out), 0);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
    sigprocmask(SIG_SETMASK, &old, NULL);
}

static void test_env_still_searches_path(void) {
    printf("[env_path_search]\n");
    const char *env[] = { "PATH=/usr/bin:/bin", "HOME=/" };
    neverc_exec_cmd_t *cmd = neverc_exec_command("true", NULL, 0);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_cmd_set_env(cmd, env, 2);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
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
#endif

static void test_argv_quoting(const char *executable) {
    printf("[argv_quoting]\n");
    const char *args[] = {"--print-argv", "foo\"bar", "a b", "$(whoami)", ";id", "a|b"};
    neverc_exec_cmd_t *cmd = neverc_exec_command(executable, args, 6);
    ASSERT_TRUE(cmd != NULL);
    if (!cmd) return;

    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(memmem(out.data, out.len, "foo\"bar", 7) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "a b", 3) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "$(whoami)", 9) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, ";id", 3) != NULL);
    ASSERT_TRUE(memmem(out.data, out.len, "a|b", 3) != NULL);

    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
}

static int run_pause_child(void) {
#if defined(_WIN32)
    Sleep(INFINITE);
#else
    pause();
#endif
    return 0;
}

#if !defined(_WIN32)
static int run_check_extra_fd(int fd) {
    int open_now = fcntl(fd, F_GETFD) >= 0;
    return printf("%d\n", open_now) < 0 ? 1 : 0;
}
#endif

static void test_missing_executable(void) {
    printf("[missing_executable]\n");
    neverc_exec_cmd_t *cmd =
        neverc_exec_command("neverc_no_such_cmd_xyz_12345", NULL, 0);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_start(cmd), -1);
    neverc_exec_cmd_free(cmd);
}

static void test_invalid_env_rejected(void) {
    printf("[invalid_env]\n");
#if defined(_WIN32)
    const char *args[] = {"/C", "exit /B 0"};
    neverc_exec_cmd_t *cmd = neverc_exec_command("cmd.exe", args, 2);
#else
    neverc_exec_cmd_t *cmd = neverc_exec_command("true", NULL, 0);
#endif
    ASSERT_TRUE(cmd != NULL);
    const char *bad[] = {"NOT_AN_ASSIGNMENT"};
    neverc_exec_cmd_set_env(cmd, bad, 1);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    neverc_exec_cmd_free(cmd);
}

static void test_batch_args_rejected(void) {
    printf("[batch_args_rejected]\n");
    char script[1200];
#if defined(_WIN32)
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(tmp), tmp);
    ASSERT_TRUE(n > 0 && n < sizeof(tmp));
    snprintf(script, sizeof(script), "%sneverc_probe_%lu.bat", tmp,
             (unsigned long)GetCurrentProcessId());
#else
    char cwd[1024];
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    snprintf(script, sizeof(script), "%s/neverc_probe_%d.bat", cwd, (int)getpid());
#endif
    FILE *sf = fopen(script, "w");
    ASSERT_TRUE(sf != NULL);
    if (sf) {
#if defined(_WIN32)
        fputs("@echo off\r\nexit /B 0\r\n", sf);
#else
        fputs("#!/bin/sh\nexit 0\n", sf);
#endif
        fclose(sf);
    }
#if !defined(_WIN32)
    chmod(script, 0755);
#endif
    const char *safe[] = {"ok"};
    neverc_exec_cmd_t *cmd = neverc_exec_command(script, safe, 1);
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    neverc_exec_cmd_free(cmd);

    const char *unsafe[] = {"&calc"};
    cmd = neverc_exec_command(script, unsafe, 1);
    ASSERT_INT_EQ(neverc_exec_cmd_run(cmd, &st), -1);
    neverc_exec_cmd_free(cmd);
#if defined(_WIN32)
    DeleteFileA(script);
#else
    unlink(script);
#endif
}

static void test_start_kill_wait(const char *executable) {
    printf("[start_kill_wait]\n");
    ASSERT_INT_EQ(neverc_exec_cmd_kill(NULL, 15), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_wait(NULL, NULL), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_pid(NULL), -1);

#if defined(_WIN32)
    const char *true_args[] = {"/C", "exit /B 0"};
    neverc_exec_cmd_t *idle = neverc_exec_command("cmd.exe", true_args, 2);
#else
    neverc_exec_cmd_t *idle = neverc_exec_command("true", NULL, 0);
#endif
    ASSERT_TRUE(idle != NULL);
    ASSERT_INT_EQ(neverc_exec_cmd_kill(idle, 15), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_wait(idle, NULL), -1);
    ASSERT_INT_EQ(neverc_exec_cmd_pid(idle), -1);
    neverc_exec_cmd_free(idle);

#if !defined(_WIN32)
    void (*old_term)(int) = signal(SIGTERM, SIG_IGN);
#endif
    const char *args[] = {"--pause"};
    neverc_exec_cmd_t *cmd = neverc_exec_command(executable, args, 1);
    ASSERT_TRUE(cmd != NULL);
    ASSERT_INT_EQ(neverc_exec_cmd_start(cmd), 0);
    ASSERT_TRUE(neverc_exec_cmd_pid(cmd) > 0);
    ASSERT_INT_EQ(neverc_exec_cmd_start(cmd), -1);
#if defined(_WIN32)
    ASSERT_INT_EQ(neverc_exec_cmd_kill(cmd, 15), 0);
#else
    ASSERT_INT_EQ(neverc_exec_cmd_kill(cmd, SIGTERM), 0);
#endif
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_wait(cmd, &st), 0);
#if defined(_WIN32)
    ASSERT_TRUE(st.exit_code != 0 || st.signaled);
#else
    ASSERT_INT_EQ(st.signaled, 1);
    ASSERT_INT_EQ(st.signal_num, SIGTERM);
#endif
    ASSERT_INT_EQ(neverc_exec_cmd_pid(cmd), -1);
    neverc_exec_cmd_free(cmd);
#if !defined(_WIN32)
    signal(SIGTERM, old_term);
#endif
}

#if !defined(_WIN32)
static void test_child_cloexec(const char *executable) {
    printf("[child_cloexec]\n");
    int fds[2];
    ASSERT_INT_EQ(pipe(fds), 0);
    char fdstr[16];
    snprintf(fdstr, sizeof(fdstr), "%d", fds[0]);
    const char *args[] = {"--check-extra-fd", fdstr};
    neverc_exec_cmd_t *cmd = neverc_exec_command(executable, args, 2);
    ASSERT_TRUE(cmd != NULL);
    neverc_exec_output_t out = {0};
    neverc_exec_exit_status_t st = {0};
    ASSERT_INT_EQ(neverc_exec_cmd_output(cmd, &out, &st), 0);
    ASSERT_INT_EQ(st.exit_code, 0);
    ASSERT_TRUE(out.len >= 1 && out.data[0] == '0');
    neverc_exec_output_free(&out);
    neverc_exec_cmd_free(cmd);
    close(fds[0]);
    close(fds[1]);
}
#endif

int main(int argc, char **argv) {
    if (argc == 2 &&
        strcmp(argv[1], "--bidirectional-child") == 0)
        return run_bidirectional_child();
    if (argc == 2 && strcmp(argv[1], "--pause") == 0)
        return run_pause_child();
#if !defined(_WIN32)
    if (argc == 2 && strcmp(argv[1], "--print-blocked-usr1") == 0)
        return run_print_blocked_usr1();
    if (argc == 3 && strcmp(argv[1], "--check-extra-fd") == 0)
        return run_check_extra_fd(atoi(argv[2]));
#endif
    if (argc >= 2 && strcmp(argv[1], "--print-argv") == 0) {
        int i;
        for (i = 2; i < argc; i++) {
            if (printf("%s\n", argv[i]) < 0)
                return 1;
        }
        return 0;
    }

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
    test_argv_quoting(argv[0]);
    test_look_path();
    test_combined_output();
    test_missing_executable();
    test_invalid_env_rejected();
    test_batch_args_rejected();
    test_start_kill_wait(argv[0]);
#if !defined(_WIN32)
    test_child_cloexec(argv[0]);
    test_unread_stdin_is_not_failure();
    test_signaled_status();
    test_exec_resets_signal_mask(argv[0]);
    test_env_still_searches_path();
    test_set_dir();
#endif
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
